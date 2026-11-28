#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/printk.h>
#include "ble.h"
#include "btlist.h"
#include "btpair.h"
#include "btpolicy.h"
#include "core.h"
#include "diag.h"
#include "hidkeys.h"
#include "hidmap.h"
#include "hidpp.h"
#include "store.h"
#include "usb.h"

#define TICK_MS 10
#define RSSI_PERIOD_MS 1000
/* A device reached over Bluetooth answers HID++ as its own receiver. */
#define HIDPP_DEVICE_INDEX 0xff
#define HIDPP_RETRY_MS 1000

static int no_home(void)
{
	return CHANMOVE_NO_HOME;
}

static void nothing(void)
{
}

static uint8_t map_bytes[BLE_MAP_MAX];
static struct hidmap maps[DEVICE_SLOTS];
static struct btpolicy policy;
static struct btlist list;
static struct btpair pair;

/* What the dongle learns of a connected device; it starts over at each connection. */
struct connected {
	char name[BLE_NAME_MAX + 1];
	/* From the Battery Service. */
	uint8_t level;
	/* From HID++, the only source of the charging state. */
	struct device_battery battery;
	/* The consumer usages the device's reports and its diverted keys each hold. */
	uint16_t media_usage;
	uint16_t divert_usage;
	int wheel_carry;
	bool rssi_read;
	int8_t rssi;
	bool reported;
	uint32_t report_ms;
	struct hidkeys keys;
	/* A button already down when the connection starts was pressed for something before it, such
	 * as closing a clicked passkey. */
	bool buttons_muted;
};

static struct connected devices[DEVICE_SLOTS];

/* A device's feature indexes are fixed by its firmware, so a run is kept while the same address
 * reconnects. */
static struct {
	uint8_t addr[BTPOLICY_ADDR_LEN];
	struct hidpp hidpp;
} runs[DEVICE_SLOTS];
static uint32_t rssi_tried_ms;

static void connection_over(uint8_t slot)
{
	devices[slot] = (struct connected){
		.level = DEVICE_NO_BATTERY,
		.battery = DEVICE_BATTERY_UNKNOWN,
		.buttons_muted = true,
	};
}

/* A HID++ level replaces the Battery Service's, which a device can keep reporting while HID++
 * calls its level unknown. */
static struct device_battery battery_of(const struct connected *c)
{
	struct device_battery b = c->battery;

	if (b.level == DEVICE_NO_BATTERY) {
		b.level = c->level;
	}
	return b;
}

static void slots(struct diag_slot *out)
{
	for (uint8_t s = 0; s < DEVICE_SLOTS; s++) {
		const uint8_t *last = btpolicy_last_peer(&policy, s);

		out[s] = (struct diag_slot){
			.kind = last ? btpolicy_slot_kind(&policy, s) : DEVICE_KIND_KEYBOARD,
			.stored = btlist_has(&list, last),
			.ready = btpolicy_is_ready(&policy, s),
			.standby = btpolicy_is_standby(&policy, s),
		};
	}
}

static uint8_t pairing_of(uint8_t slot)
{
	const struct btlist_entry *e;

	if (!btpolicy_is_ready(&policy, slot)) {
		return 0;
	}
	e = btlist_entry(&list, btpolicy_peer(&policy, slot));
	if (!e) {
		return 0;
	}
	return btlist_pairing(e, ble_bond_secure(e->addr));
}

static void fill_status(struct diag_status *st, uint8_t slot)
{
	const struct connected *c = &devices[slot];
	btpair_status(&pair, k_uptime_get_32(), st);
	st->pairing = pairing_of(slot);
	st->battery = battery_of(c);
	ble_fill_link(slot, st);
	if (c->rssi_read) {
		st->last_rssi = c->rssi;
	}
	if (c->reported) {
		st->last_rx_age_ms = k_uptime_get_32() - c->report_ms;
	}
	if (btpolicy_is_ready(&policy, slot)) {
		strncpy((char *)st->name, c->name, sizeof(st->name));
	} else {
		const struct btlist_entry *e =
			btlist_entry(&list, btpolicy_last_peer(&policy, slot));

		if (e) {
			memcpy(st->name, e->name, sizeof(st->name));
		}
	}
}

static size_t no_candidates(const uint8_t **channels)
{
	static const uint8_t none[1];

	*channels = none;
	return 0;
}

static enum chanmove_result no_move(uint8_t channel, uint8_t *from)
{
	ARG_UNUSED(channel);
	*from = 0;
	return CHANMOVE_NOT_MOVABLE;
}

/* Losing its link is the only notice of a suspend some keyboards act on. */
static void host_suspended(bool suspended)
{
	if (!suspended) {
		return;
	}
	for (uint8_t s = 0; s < DEVICE_SLOTS; s++) {
		if (btpolicy_peer(&policy, s)) {
			btpolicy_dropping(&policy, s);
			ble_power_off(s);
		}
	}
}

static bool pairing(void)
{
	return btpair_is_open(&pair, k_uptime_get_32());
}

static const struct core_loop loop = {
	.home = no_home,
	.take_noise = ble_take_noise,
	.radio_resumed = nothing,
	.slot_count = DEVICE_SLOTS,
	.slots = slots,
	.fill_status = fill_status,
	.movable = no_home,
	.candidates = no_candidates,
	.replies_dropped = nothing,
	.move = no_move,
	.host_suspended = host_suspended,
	.pairing = pairing,
};

static const char *map_fault(enum hidmap_result result)
{
	switch (result) {
	case HIDMAP_MALFORMED:
		return "malformed";
	case HIDMAP_TOO_LARGE:
		return "too large";
	case HIDMAP_NO_KEYBOARD:
		return "without a keyboard";
	case HIDMAP_NO_POINTER:
		return "without a pointer";
	case HIDMAP_OK:
		break;
	}
	return "usable";
}

static void save_list(bool changed)
{
	if (changed) {
		store_save_bluetooth(&list);
	}
}

/* Bonds decide which devices are listed; the list adds their kinds, names, passkey mark and
 * order. */
static void load_list(void)
{
	uint8_t bonded[BTPOLICY_STORED_MAX][BTPOLICY_ADDR_LEN];
	uint8_t n = ble_stored(bonded, BTPOLICY_STORED_MAX);

	if (!store_load_bluetooth(&list) || list.count > BTPOLICY_STORED_MAX) {
		list = (struct btlist){0};
	}
	save_list(btlist_reconcile(&list, bonded, MIN(n, BTPOLICY_STORED_MAX)));
}

/* The cause is a HISTORY_FORGOTTEN_* value, logged once the bond is deleted. */
static int forget(uint8_t slot, const uint8_t *addr, uint8_t cause)
{
	int err = ble_forget(slot, addr, cause);

	if (err) {
		printk("bluetooth bond not queued for deletion: %d\n", err);
	}
	return err;
}

static void log_forgotten(uint8_t slot, const uint8_t *addr, uint8_t cause)
{
	core_log_address(slot,
			 (struct diag_detail){.tag = DIAG_DETAIL_BT_FORGOTTEN,
					      .BT_FORGOTTEN = {.cause = cause}},
			 addr);
}

/* The tool's forget is answered once the bond's delete is done. */
static struct diag_request forget_request;
static bool forget_answering;

static void answer_forget(const uint8_t *addr, int error)
{
	if (!forget_answering || memcmp(forget_request.data, addr, BTPOLICY_ADDR_LEN) != 0) {
		return;
	}
	forget_answering = false;
	if (error) {
		diag_reply_error(&forget_request, DIAG_ERROR_STORE, 0);
	} else {
		diag_reply(&forget_request, NULL, 0, DIAG_AFTER_NONE);
	}
}

static void on_forgotten(const struct ble_event *ev)
{
	const uint8_t *addr = ev->forgotten.addr;

	answer_forget(addr, ev->forgotten.error);
	if (ev->forgotten.error) {
		printk("bluetooth bond not deleted: %d\n", ev->forgotten.error);
		core_log_address(ev->slot, (struct diag_detail){.tag = DIAG_DETAIL_BT_NOT_FORGOTTEN},
				 addr);
		return;
	}
	save_list(btlist_remove(&list, addr));
	btpolicy_forgotten(&policy, addr);
	log_forgotten(ev->slot, addr, ev->forgotten.tag);
}

BUILD_ASSERT(DIAG_REQUEST_DATA_LEN == BTPOLICY_ADDR_LEN);

static void log_device(struct diag_detail d, uint8_t slot)
{
	core_log_address(slot, d, btpolicy_peer(&policy, slot));
}

static void send_leds(uint8_t leds)
{
	for (uint8_t s = 0; s < DEVICE_SLOTS; s++) {
		const struct hidmap *map = &maps[s];
		int err;

		if (!btpolicy_is_ready(&policy, s) ||
		    btpolicy_slot_kind(&policy, s) != DEVICE_KIND_KEYBOARD || !map->has_leds) {
			continue;
		}
		err = ble_write_report(s, map->led_report_id, &leds, sizeof(leds));
		if (err) {
			printk("bluetooth leds not sent to link %u: %d\n", s, err);
		}
	}
}

static void on_advert(const struct btpolicy_advert *heard)
{
	struct btpolicy_advert a = *heard;
	uint32_t now = k_uptime_get_32();
	struct btpolicy_choice c;
	int err;

	if (a.stored) {
		a.kind = btlist_kind(&list, a.addr);
	}
	c = btpair_wants(&pair, &policy, &a, now);
	switch (c.want) {
	case BTPOLICY_IGNORE:
		return;
	case BTPOLICY_REPLACE:
		btpolicy_dropping(&policy, c.slot);
		ble_disconnect(c.slot);
		return;
	case BTPOLICY_CONNECT:
		break;
	}
	ble_scan(BTPOLICY_SCAN_OFF);
	err = ble_connect(c.slot, a.addr, a.name, btpolicy_pairs_by_clicks(a.kind, a.bolt));
	if (err) {
		printk("bluetooth connect not started: %d\n", err);
		return;
	}
	btpolicy_connecting(&policy, c.slot, &a);
}

static void attempt_failed(uint8_t slot, bool refused)
{
	if (btpair_attempt_failed(&pair, slot, refused, k_uptime_get_32())) {
		core_log_pairing_closed();
	}
}

static void on_failed(const struct ble_event *ev)
{
	enum btpolicy_security outcome;

	printk("bluetooth %s failed: %d\n", ble_stage_name(ev->failed.stage), ev->failed.error);
	if (ev->failed.stage != BLE_STAGE_SECURITY) {
		return;
	}
	outcome = btpolicy_security_failed(&policy, ev->slot, ev->failed.why);
	switch (outcome) {
	case BTPOLICY_PAIRING_REFUSED:
		printk("bluetooth pairing refused\n");
		core_log(ev->slot,
			 (struct diag_detail){.tag = DIAG_DETAIL_BT_REFUSED,
					       .BT_REFUSED = {.refused = HISTORY_REFUSED_BY_KEYBOARD}});
		break;
	case BTPOLICY_BOND_LOST:
		printk("bluetooth device lost its bond\n");
		log_device((struct diag_detail){.tag = DIAG_DETAIL_BT_BOND_LOST}, ev->slot);
		break;
	case BTPOLICY_SECURITY_FAILED:
		break;
	}
	attempt_failed(ev->slot, outcome == BTPOLICY_PAIRING_REFUSED);
}

static void device_ready(uint8_t slot)
{
	btpolicy_ready(&policy, slot);
	log_device((struct diag_detail){.tag = DIAG_DETAIL_BT_CONNECTED}, slot);
	save_list(btlist_connected(&list, btpolicy_peer(&policy, slot), devices[slot].name));
}

static void on_map_read(uint8_t slot)
{
	enum device_kind kind = btpolicy_slot_kind(&policy, slot);
	const char *what = device_kind_name(kind);
	char *name = devices[slot].name;
	size_t len;
	enum hidmap_result result;

	ble_copy_name(slot, name);
	len = ble_copy_map(slot, map_bytes, sizeof(map_bytes));
	result = hidmap_parse(map_bytes, len, kind, &maps[slot]);
	if (result != HIDMAP_OK) {
		printk("bluetooth %s \"%s\" refused: report map %s\n", what, name,
		       map_fault(result));
		if (result == HIDMAP_NO_KEYBOARD || result == HIDMAP_NO_POINTER) {
			forget(slot, btpolicy_peer(&policy, slot), HISTORY_FORGOTTEN_UNUSABLE_MAP);
		}
		ble_disconnect(slot);
		return;
	}
	printk("bluetooth %s \"%s\" ready\n", what, name);
	device_ready(slot);
	if (maps[slot].has_hidpp) {
		const uint8_t *peer = btpolicy_peer(&policy, slot);

		if (memcmp(runs[slot].addr, peer, BTPOLICY_ADDR_LEN) != 0) {
			hidpp_init(&runs[slot].hidpp, kind);
			memcpy(runs[slot].addr, peer, BTPOLICY_ADDR_LEN);
		}
		hidpp_start(&runs[slot].hidpp);
	}
	if (kind == DEVICE_KIND_KEYBOARD) {
		uint8_t leds;

		core_take_leds(&leds);
		send_leds(leds);
	}
}

static void release_slot(uint8_t slot)
{
	const struct connected *c = &devices[slot];

	core_release_device(btpolicy_slot_kind(&policy, slot),
			    c->media_usage != 0 || c->divert_usage != 0);
}

static bool take_input(uint8_t slot, bool pressed)
{
	struct btpolicy_input_result r = btpolicy_input(&policy, slot, pressed);

	if (r.input == BTPOLICY_INPUT_TOOK_OVER) {
		release_slot(r.released);
		log_device((struct diag_detail){.tag = DIAG_DETAIL_BT_ACTIVE}, slot);
	}
	return r.input != BTPOLICY_INPUT_STANDBY;
}

static void on_disconnected(uint8_t slot, uint8_t reason)
{
	uint8_t promoted;

	if (btpolicy_is_ready(&policy, slot)) {
		core_log(slot,
			 (struct diag_detail){.tag = DIAG_DETAIL_BT_DISCONNECTED,
					       .BT_DISCONNECTED = {.reason = reason,
								   .since_input_s = core_since_input_s(slot)}});
	}
	if (btpolicy_is_active(&policy, slot)) {
		release_slot(slot);
	}
	attempt_failed(slot, false);
	connection_over(slot);
	promoted = btpolicy_disconnected(&policy, slot, k_uptime_get_32(),
					 reason == BLE_REASON_NOT_ESTABLISHED);
	if (promoted != DEVICE_NO_SLOT) {
		log_device((struct diag_detail){.tag = DIAG_DETAIL_BT_ACTIVE}, promoted);
	}
}

static size_t mouse_report(uint8_t slot, const struct ble_event *ev, struct report *out)
{
	struct connected *c = &devices[slot];
	int wheel;

	if (!hidkeys_pointer(&maps[slot].pointer, ev->report.id, ev->report.data, ev->report.len,
			     out)) {
		return 0;
	}
	wheel = reports_scale_wheel(&c->wheel_carry, out->mouse.wheel,
				    hidpp_wheel_steps(&runs[slot].hidpp), usb_mouse_wheel_steps());
	out->mouse.wheel = (int8_t)reports_clamp(wheel, -INT8_MAX, INT8_MAX);
	reports_mute_buttons(&c->buttons_muted, out);
	return 1;
}

static size_t keyboard_reports(uint8_t slot, const struct ble_event *ev, struct report *out)
{
	struct connected *c = &devices[slot];
	size_t n = hidkeys_translate(&c->keys, &maps[slot], ev->report.id, ev->report.data,
				     ev->report.len, out);

	for (size_t i = 0; i < n; i++) {
		if (out[i].kind == REPORT_CONSUMER) {
			c->media_usage = out[i].usage;
			out[i] = reports_consumer(out[i].usage, c->divert_usage);
		}
	}
	return n;
}

static void print_battery(enum device_kind kind, struct device_battery b)
{
	if (b.level == DEVICE_NO_BATTERY) {
		printk("bluetooth %s battery unknown, charging state %d\n", device_kind_name(kind),
		       b.charging);
	} else {
		printk("bluetooth %s battery %u%%, charging state %d\n", device_kind_name(kind),
		       b.level, b.charging);
	}
}

static void on_hidpp_report(const struct ble_event *ev)
{
	uint8_t slot = ev->slot;
	struct connected *c = &devices[slot];
	struct device_battery before = c->battery;
	struct hidpp_msg m;
	uint16_t usage;

	hidpp_msg_read(ev->report.data, ev->report.len, true, &m);
	if (hidpp_rx(&runs[slot].hidpp, &c->battery, &m, &usage) && usage != c->divert_usage) {
		const struct report out = reports_consumer(usage, c->media_usage);

		c->divert_usage = usage;
		if (take_input(slot, usage != 0)) {
			core_submit(slot, &out, &ev->report.received_us);
		}
	}
	if (before.level != c->battery.level || before.charging != c->battery.charging) {
		print_battery(btpolicy_slot_kind(&policy, slot), battery_of(c));
	}
}

static void on_report(const struct ble_event *ev)
{
	struct report out[REPORT_KINDS];
	uint8_t slot = ev->slot;
	size_t n;

	if (!btpolicy_is_ready(&policy, slot)) {
		return;
	}
	core_packet(slot);
	devices[slot].reported = true;
	devices[slot].report_ms = k_uptime_get_32();
	if (maps[slot].has_hidpp && ev->report.id == HIDPP_LONG_REPORT_ID &&
	    ev->report.len == HIDPP_LONG_LEN) {
		on_hidpp_report(ev);
		return;
	}
	n = btpolicy_slot_kind(&policy, slot) == DEVICE_KIND_MOUSE ? mouse_report(slot, ev, out)
								    : keyboard_reports(slot, ev, out);
	if (!take_input(slot, reports_pressed(out, n))) {
		return;
	}
	for (size_t i = 0; i < n; i++) {
		core_submit(slot, &out[i], &ev->report.received_us);
	}
}

static void on_event(const struct ble_event *ev)
{
	char name[BLE_NAME_MAX + 1];

	switch (ev->type) {
	case BLE_ADVERT:
		on_advert(&ev->advert);
		break;
	case BLE_CONNECTED:
		printk("bluetooth connected: %s\n", ev->address);
		btpolicy_connected(&policy, ev->slot);
		break;
	case BLE_DISCONNECTED:
		printk("bluetooth disconnected: reason 0x%02x\n", ev->reason);
		on_disconnected(ev->slot, ev->reason);
		break;
	case BLE_FAILED:
		on_failed(ev);
		break;
	case BLE_PAIRED:
		btpair_paired(&pair, &policy, ev->slot);
		core_slot_paired(ev->slot);
		printk("bluetooth paired\n");
		log_device((struct diag_detail){.tag = DIAG_DETAIL_BT_PAIRED, .BT_PAIRED = {.passkey = ev->paired.passkey_used}},
			   ev->slot);
		ble_copy_name(ev->slot, name);
		save_list(btlist_paired(&list, btpolicy_peer(&policy, ev->slot),
					btpolicy_slot_kind(&policy, ev->slot),
					ev->paired.passkey_used, ev->paired.bolt_mode, name));
		break;
	case BLE_PASSKEY_AWAITED:
		ble_copy_name(ev->slot, name);
		if (btpair_passkey_awaited(&pair, ev->slot, ev->passkey.value, ev->passkey.clicked,
					   name, k_uptime_get_32())) {
			printk("bluetooth passkey awaited by \"%s\"\n", name);
			log_device((struct diag_detail){.tag = DIAG_DETAIL_BT_PASSKEY_AWAITED}, ev->slot);
		}
		break;
	case BLE_KEYPRESS:
		if (btpair_keypress(&pair, ev->slot, ev->keypress)) {
			printk("bluetooth keypress %d, %u entered\n", ev->keypress,
			       pair.passkey.entered);
		}
		break;
	case BLE_MAP_READ:
		on_map_read(ev->slot);
		break;
	case BLE_REPORT:
		on_report(ev);
		break;
	case BLE_BATTERY:
		devices[ev->slot].level = ev->battery.level;
		print_battery(btpolicy_slot_kind(&policy, ev->slot), battery_of(&devices[ev->slot]));
		break;
	case BLE_FORGOTTEN:
		on_forgotten(ev);
		break;
	}
}

#define PASSKEY_LIMIT 1000000

static void open_pairing(const struct diag_request *req)
{
	uint32_t passkey;
	int err;

	if (core_radio_has_failed()) {
		diag_reply_error(req, DIAG_ERROR_RADIO, 0);
		return;
	}
	/* Only a pairing adds to the list, and it closes the window. */
	if (ble_stored_count() >= BTPOLICY_STORED_MAX) {
		diag_reply_error(req, DIAG_ERROR_LIST_FULL, 0);
		return;
	}
	sys_csrand_get(&passkey, sizeof(passkey));
	passkey %= PASSKEY_LIMIT;
	err = ble_offer_passkey(true, passkey);
	if (err) {
		printk("bluetooth passkey not offered: %d\n", err);
		diag_reply_error(req, DIAG_ERROR_RADIO, 0);
		return;
	}
	btpair_open_to_pick(&pair, k_uptime_get_32());
	core_log_pairing_opened();
	printk("pairing open for %u s\n", DIAG_PICK_WINDOW_MS / 1000);
	diag_reply(req, NULL, 0, DIAG_AFTER_NONE);
}

static void send_devices(const struct diag_request *req)
{
	static struct diag_device devices[BTPOLICY_STORED_MAX];
	const uint8_t *connected[DEVICE_SLOTS];
	size_t n;

	for (uint8_t s = 0; s < DEVICE_SLOTS; s++) {
		connected[s] = btpolicy_is_ready(&policy, s) ? btpolicy_peer(&policy, s) : NULL;
	}
	n = btlist_devices(&list, connected, ble_bond_secure, devices);
	core_reply_devices(req, devices, n);
}

static void forget_listed(const struct diag_request *req)
{
	uint8_t slot = btpolicy_slot_of(&policy, req->data);

	if (!btlist_has(&list, req->data)) {
		diag_reply_error(req, DIAG_ERROR_NO_SUCH_DEVICE, 0);
		return;
	}
	if (forget_answering || forget(slot, req->data, HISTORY_FORGOTTEN_ON_REQUEST) != 0) {
		diag_reply_error(req, DIAG_ERROR_STORE, 0);
		return;
	}
	forget_request = *req;
	forget_answering = true;
}

static void send_heard(const struct diag_request *req)
{
	struct diag_heard heard[BTSEEN_MAX];

	core_reply_heard(req, heard, btpair_heard(&pair, k_uptime_get_32(), heard));
}

static void pair_with(const struct diag_request *req)
{
	if (!btpair_pick(&pair, req->data, k_uptime_get_32())) {
		diag_reply_error(req, DIAG_ERROR_NO_SUCH_DEVICE, 0);
		return;
	}
	printk("bluetooth pairing picked a device\n");
	diag_reply(req, NULL, 0, DIAG_AFTER_NONE);
}

static void on_request(const struct diag_request *req)
{
	switch (req->command) {
	case DIAG_COMMAND_PAIR:
		open_pairing(req);
		break;
	case DIAG_COMMAND_DEVICES:
		send_devices(req);
		break;
	case DIAG_COMMAND_FORGET:
		forget_listed(req);
		break;
	case DIAG_COMMAND_HEARD:
		send_heard(req);
		break;
	case DIAG_COMMAND_PAIR_WITH:
		pair_with(req);
		break;
	default:
		diag_reply_error(req, DIAG_ERROR_UNKNOWN_COMMAND, 0);
		break;
	}
}

static void read_rssi(uint32_t now)
{
	if (now - rssi_tried_ms < RSSI_PERIOD_MS) {
		return;
	}
	rssi_tried_ms = now;
	for (uint8_t s = 0; s < DEVICE_SLOTS; s++) {
		int8_t dbm;

		if (!btpolicy_is_ready(&policy, s) || ble_rssi(s, &dbm) != 0) {
			continue;
		}
		core_rssi(s, dbm);
		devices[s].rssi = dbm;
		devices[s].rssi_read = true;
	}
}

static void run_hidpp(uint8_t slot, uint32_t now)
{
	struct hidpp *x = &runs[slot].hidpp;
	struct hidpp_event event;
	struct hidpp_msg m;

	if (!btpolicy_is_ready(&policy, slot) || !maps[slot].has_hidpp) {
		return;
	}
	hidpp_want_hires(x, usb_mouse_wheel_steps() != 1);
	if (hidpp_request(x, HIDPP_DEVICE_INDEX, now, HIDPP_RETRY_MS, &m)) {
		uint8_t data[HIDPP_LONG_LEN] = {0};

		hidpp_msg_write(&m, data, sizeof(data));
		/* A failed write counts as a try, so a report the device lacks ends the run. */
		ble_write_report(slot, HIDPP_LONG_REPORT_ID, data, sizeof(data));
		hidpp_taken(x, now);
	}
	if (hidpp_take_event(x, &event)) {
		core_log(slot, event.detail);
	}
}

FUNC_NORETURN void bluetooth_run(int store_err)
{
	int start_err;

	ARG_UNUSED(store_err);
	core_start_history(&loop);
	btpolicy_init(&policy);
	btpair_init(&pair);
	for (uint8_t s = 0; s < DEVICE_SLOTS; s++) {
		connection_over(s);
	}
	start_err = ble_start();
	if (start_err) {
		core_mark_radio_failed();
		printk("bluetooth start failed: %d\n", start_err);
	} else {
		load_list();
		btpolicy_seed_all(&policy, &list);
		printk("bluetooth started, %u stored\n", ble_stored_count());
	}

	for (;;) {
		struct diag_request req;
		struct ble_event ev;
		uint32_t now;
		uint8_t leds;

		/* A report must not wait out the tick. */
		for (k_timeout_t wait = K_MSEC(TICK_MS); ble_take_event(&ev, wait); wait = K_NO_WAIT) {
			on_event(&ev);
		}
		core_poll();
		while (core_take_request(&req)) {
			on_request(&req);
		}
		now = k_uptime_get_32();
		if (pairwin_ran_out(&pair.window, now)) {
			core_log_pairing_closed();
		}
		if (!btpair_is_open(&pair, now)) {
			ble_offer_passkey(false, 0);
		}
		read_rssi(now);
		for (uint8_t s = 0; s < DEVICE_SLOTS; s++) {
			run_hidpp(s, now);
		}
		if (!core_radio_has_failed()) {
			int err = ble_scan(btpolicy_scan(&policy, btpair_is_open(&pair, now), &list));

			if (err) {
				printk("bluetooth scan not switched: %d\n", err);
			}
		}
		if (core_take_leds(&leds)) {
			send_leds(leds);
		}
		core_show_state();
		core_watchdog_feed();
	}
}
