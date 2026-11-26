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

/* One device of each kind connects at once, each in the slot numbered by its kind. */
BUILD_ASSERT(DEVICE_KIND_KEYBOARD == 0 && DEVICE_KIND_MOUSE == 1 && DEVICE_KINDS == DEVICE_SLOTS);

static uint8_t slot_of(enum device_kind kind)
{
	return (uint8_t)kind;
}

static enum device_kind kind_in(uint8_t slot)
{
	return (enum device_kind)slot;
}

static uint8_t map_bytes[BLE_MAP_MAX];
static struct hidmap maps[DEVICE_KINDS];
static struct hidkeys keys;
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
	/* The consumer usages the keyboard's reports and its diverted keys each hold. */
	uint16_t media_usage;
	uint16_t divert_usage;
	int wheel_carry;
	bool rssi_read;
	int8_t rssi;
	/* A button already down when the connection starts was pressed for something before it, such
	 * as closing a clicked passkey. */
	bool buttons_muted;
};

static struct connected devices[DEVICE_KINDS];

/* A device's feature indexes are fixed by its firmware, so a run is kept while the same address
 * reconnects. */
static struct {
	uint8_t addr[BTPOLICY_ADDR_LEN];
	struct hidpp hidpp;
} runs[DEVICE_KINDS];
static uint32_t rssi_tried_ms;
static bool reported[DEVICE_SLOTS];
static uint32_t report_ms[DEVICE_SLOTS];

static void connection_over(enum device_kind kind)
{
	devices[kind] = (struct connected){
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
		enum device_kind kind = kind_in(s);

		out[s] = (struct diag_slot){
			.kind = kind,
			.stored = btlist_has_kind(&list, kind),
			.ready = btpolicy_is_ready(&policy, kind),
		};
	}
}

static uint8_t pairing_of(enum device_kind kind)
{
	const struct btlist_entry *e;

	if (!btpolicy_is_ready(&policy, kind)) {
		return 0;
	}
	e = btlist_entry(&list, btpolicy_peer(&policy, kind));
	if (!e) {
		return 0;
	}
	return btlist_pairing(e, ble_bond_secure(e->addr));
}

static void fill_status(struct diag_status *st, uint8_t slot)
{
	enum device_kind kind = kind_in(slot);
	const struct connected *c = &devices[kind];
	btpair_status(&pair, k_uptime_get_32(), st);
	st->pairing = pairing_of(kind);
	st->battery = battery_of(c);
	if (c->rssi_read) {
		st->last_rssi = c->rssi;
	}
	if (reported[slot]) {
		st->last_rx_age_ms = k_uptime_get_32() - report_ms[slot];
	}
	if (btpolicy_is_ready(&policy, kind)) {
		strncpy((char *)st->name, c->name, sizeof(st->name));
	} else {
		const struct btlist_entry *latest = btlist_latest(&list, kind);

		if (latest) {
			memcpy(st->name, latest->name, sizeof(st->name));
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

static void drop_link(enum device_kind kind)
{
	btpolicy_hush(&policy, kind, k_uptime_get_32());
	ble_power_off(kind);
}

/* Losing its link is the only notice of a suspend some keyboards act on. */
static void host_suspended(bool suspended)
{
	if (!suspended) {
		btpolicy_unhush(&policy);
		return;
	}
	for (int k = 0; k < DEVICE_KINDS; k++) {
		if (btpolicy_peer(&policy, k)) {
			drop_link(k);
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
static int forget(enum device_kind kind, const uint8_t *addr, uint8_t cause)
{
	int err = ble_forget(kind, addr, cause);

	if (err) {
		printk("bluetooth bond not queued for deletion: %d\n", err);
	}
	return err;
}

static void log_forgotten(enum device_kind kind, const uint8_t *addr, uint8_t cause)
{
	core_log_address(slot_of(kind),
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
		core_log_address(slot_of(ev->kind),
				 (struct diag_detail){.tag = DIAG_DETAIL_BT_NOT_FORGOTTEN}, addr);
		return;
	}
	save_list(btlist_remove(&list, addr));
	log_forgotten(ev->kind, addr, ev->forgotten.tag);
}

BUILD_ASSERT(DIAG_REQUEST_DATA_LEN == BTPOLICY_ADDR_LEN);

static void log_device(struct diag_detail d, enum device_kind kind)
{
	core_log_address(slot_of(kind), d, btpolicy_peer(&policy, kind));
}

static void send_leds(uint8_t leds)
{
	const struct hidmap *map = &maps[DEVICE_KIND_KEYBOARD];
	int err;

	if (!btpolicy_is_ready(&policy, DEVICE_KIND_KEYBOARD) || !map->has_leds) {
		return;
	}
	err = ble_write_report(DEVICE_KIND_KEYBOARD, map->led_report_id, &leds, sizeof(leds));
	if (err) {
		printk("bluetooth leds not sent: %d\n", err);
	}
}

static void on_advert(const struct btpolicy_advert *heard)
{
	struct btpolicy_advert a = *heard;
	uint32_t now = k_uptime_get_32();
	int err;

	if (a.stored) {
		a.kind = btlist_kind(&list, a.addr);
	}
	switch (btpair_wants(&pair, &policy, &a, now)) {
	case BTPOLICY_IGNORE:
		return;
	case BTPOLICY_REPLACE:
		btpolicy_dropping(&policy, a.kind);
		ble_disconnect(a.kind);
		return;
	case BTPOLICY_CONNECT:
		break;
	}
	ble_scan(BTPOLICY_SCAN_OFF);
	err = ble_connect(a.kind, a.addr, a.name, btpolicy_pairs_by_clicks(a.kind, a.bolt));
	if (err) {
		printk("bluetooth connect not started: %d\n", err);
		return;
	}
	btpolicy_connecting(&policy, &a);
}

static void attempt_failed(enum device_kind kind, bool refused)
{
	if (btpair_attempt_failed(&pair, kind, refused, k_uptime_get_32())) {
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
	outcome = btpolicy_security_failed(&policy, ev->kind, ev->failed.why);
	switch (outcome) {
	case BTPOLICY_PAIRING_REFUSED:
		printk("bluetooth pairing refused\n");
		core_log(slot_of(ev->kind),
			 (struct diag_detail){.tag = DIAG_DETAIL_BT_REFUSED,
					       .BT_REFUSED = {.refused = HISTORY_REFUSED_BY_KEYBOARD}});
		break;
	case BTPOLICY_BOND_LOST:
		printk("bluetooth device lost its bond\n");
		log_device((struct diag_detail){.tag = DIAG_DETAIL_BT_BOND_LOST}, ev->kind);
		break;
	case BTPOLICY_SECURITY_FAILED:
		break;
	}
	attempt_failed(ev->kind, outcome == BTPOLICY_PAIRING_REFUSED);
}

static void device_ready(enum device_kind kind)
{
	btpolicy_ready(&policy, kind);
	if (usb_suspended() && usb_wakeup() != 0) {
		drop_link(kind);
	}
	log_device((struct diag_detail){.tag = DIAG_DETAIL_BT_CONNECTED}, kind);
	save_list(btlist_connected(&list, btpolicy_peer(&policy, kind), devices[kind].name));
}

static void on_map_read(enum device_kind kind)
{
	const char *what = device_kind_name(kind);
	char *name = devices[kind].name;
	size_t len;
	enum hidmap_result result;

	ble_copy_name(kind, name);
	len = ble_copy_map(kind, map_bytes, sizeof(map_bytes));
	result = hidmap_parse(map_bytes, len, kind, &maps[kind]);
	if (result != HIDMAP_OK) {
		printk("bluetooth %s \"%s\" refused: report map %s\n", what, name,
		       map_fault(result));
		if (result == HIDMAP_NO_KEYBOARD || result == HIDMAP_NO_POINTER) {
			forget(kind, btpolicy_peer(&policy, kind), HISTORY_FORGOTTEN_UNUSABLE_MAP);
		}
		ble_disconnect(kind);
		return;
	}
	printk("bluetooth %s \"%s\" ready\n", what, name);
	device_ready(kind);
	if (maps[kind].has_hidpp) {
		const uint8_t *peer = btpolicy_peer(&policy, kind);

		if (memcmp(runs[kind].addr, peer, BTPOLICY_ADDR_LEN) != 0) {
			hidpp_init(&runs[kind].hidpp, kind);
			memcpy(runs[kind].addr, peer, BTPOLICY_ADDR_LEN);
		}
		hidpp_start(&runs[kind].hidpp);
	}
	if (kind == DEVICE_KIND_KEYBOARD) {
		uint8_t leds;

		core_take_leds(&leds);
		send_leds(leds);
	}
}

static void on_disconnected(enum device_kind kind, uint8_t reason)
{
	if (btpolicy_is_ready(&policy, kind)) {
		core_log(slot_of(kind),
			 (struct diag_detail){.tag = DIAG_DETAIL_BT_DISCONNECTED,
					       .BT_DISCONNECTED = {.reason = reason,
								   .since_input_s = core_since_input_s(slot_of(kind))}});
		if (kind == DEVICE_KIND_MOUSE) {
			const struct report released = {.kind = REPORT_MOUSE};

			core_submit(slot_of(DEVICE_KIND_MOUSE), &released, NULL);
		}
	}
	if (kind == DEVICE_KIND_KEYBOARD) {
		if (btpolicy_is_ready(&policy, kind)) {
			core_release_keys();
		}
		hidkeys_reset(&keys);
	}
	attempt_failed(kind, false);
	connection_over(kind);
	btpolicy_disconnected(&policy, kind, k_uptime_get_32(),
			      reason == BLE_REASON_NOT_ESTABLISHED);
}

static void on_mouse_report(const struct ble_event *ev)
{
	struct report out;

	if (hidkeys_pointer(&maps[DEVICE_KIND_MOUSE].pointer, ev->report.id, ev->report.data,
			    ev->report.len, &out)) {
		int wheel = reports_scale_wheel(&devices[DEVICE_KIND_MOUSE].wheel_carry, out.mouse.wheel,
						hidpp_wheel_steps(&runs[DEVICE_KIND_MOUSE].hidpp),
						usb_mouse_wheel_steps());

		out.mouse.wheel = (int8_t)reports_clamp(wheel, -INT8_MAX, INT8_MAX);
		reports_mute_buttons(&devices[DEVICE_KIND_MOUSE].buttons_muted, &out);
		core_submit(slot_of(DEVICE_KIND_MOUSE), &out, &ev->report.received_us);
	}
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
	struct connected *c = &devices[ev->kind];
	struct device_battery before = c->battery;
	struct hidpp_msg m;
	uint16_t usage;

	hidpp_msg_read(ev->report.data, ev->report.len, true, &m);
	if (hidpp_rx(&runs[ev->kind].hidpp, &c->battery, &m, &usage) && usage != c->divert_usage) {
		const struct report out = reports_consumer(usage, c->media_usage);

		c->divert_usage = usage;
		core_submit(slot_of(ev->kind), &out, &ev->report.received_us);
	}
	if (before.level != c->battery.level || before.charging != c->battery.charging) {
		print_battery(ev->kind, battery_of(c));
	}
}

static void on_report(const struct ble_event *ev)
{
	struct report out[REPORT_KINDS];
	size_t n;

	uint8_t slot = slot_of(ev->kind);

	if (!btpolicy_is_ready(&policy, ev->kind)) {
		return;
	}
	core_packet(slot);
	reported[slot] = true;
	report_ms[slot] = k_uptime_get_32();
	if (maps[ev->kind].has_hidpp && ev->report.id == HIDPP_LONG_REPORT_ID &&
	    ev->report.len == HIDPP_LONG_LEN) {
		on_hidpp_report(ev);
		return;
	}
	if (ev->kind == DEVICE_KIND_MOUSE) {
		on_mouse_report(ev);
		return;
	}
	n = hidkeys_translate(&keys, &maps[DEVICE_KIND_KEYBOARD], ev->report.id, ev->report.data,
			      ev->report.len, out);
	for (size_t i = 0; i < n; i++) {
		if (out[i].kind == REPORT_CONSUMER) {
			devices[ev->kind].media_usage = out[i].usage;
			out[i] = reports_consumer(out[i].usage, devices[ev->kind].divert_usage);
		}
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
		btpolicy_connected(&policy, ev->kind);
		break;
	case BLE_DISCONNECTED:
		printk("bluetooth disconnected: reason 0x%02x\n", ev->reason);
		on_disconnected(ev->kind, ev->reason);
		break;
	case BLE_FAILED:
		on_failed(ev);
		break;
	case BLE_PAIRED:
		btpair_paired(&pair, &policy, ev->kind);
		core_slot_paired(slot_of(ev->kind));
		printk("bluetooth paired\n");
		log_device((struct diag_detail){.tag = DIAG_DETAIL_BT_PAIRED, .BT_PAIRED = {.passkey = ev->paired.passkey_used}},
			   ev->kind);
		ble_copy_name(ev->kind, name);
		save_list(btlist_paired(&list, btpolicy_peer(&policy, ev->kind), ev->kind,
					ev->paired.passkey_used, ev->paired.bolt_mode, name));
		break;
	case BLE_PASSKEY_AWAITED:
		ble_copy_name(ev->kind, name);
		if (btpair_passkey_awaited(&pair, ev->kind, ev->passkey.value, ev->passkey.clicked,
					   name, k_uptime_get_32())) {
			printk("bluetooth passkey awaited by \"%s\"\n", name);
			log_device((struct diag_detail){.tag = DIAG_DETAIL_BT_PASSKEY_AWAITED}, ev->kind);
		}
		break;
	case BLE_KEYPRESS:
		if (btpair_keypress(&pair, ev->kind, ev->keypress)) {
			printk("bluetooth keypress %d, %u entered\n", ev->keypress,
			       pair.passkey.entered);
		}
		break;
	case BLE_MAP_READ:
		on_map_read(ev->kind);
		break;
	case BLE_REPORT:
		on_report(ev);
		break;
	case BLE_BATTERY:
		devices[ev->kind].level = ev->battery.level;
		print_battery(ev->kind, battery_of(&devices[ev->kind]));
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
		enum device_kind kind = kind_in(s);
		bool ready = btpolicy_is_ready(&policy, kind);

		connected[s] = ready ? btpolicy_peer(&policy, kind) : NULL;
	}
	n = btlist_devices(&list, connected, ble_bond_secure, devices);
	core_reply_devices(req, devices, n);
}

static void forget_listed(const struct diag_request *req)
{
	enum device_kind kind;

	if (!btlist_has(&list, req->data)) {
		diag_reply_error(req, DIAG_ERROR_NO_SUCH_DEVICE, 0);
		return;
	}
	kind = btlist_kind(&list, req->data);
	if (forget_answering || forget(kind, req->data, HISTORY_FORGOTTEN_ON_REQUEST) != 0) {
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
	for (int k = 0; k < DEVICE_KINDS; k++) {
		int8_t dbm;

		if (!btpolicy_is_ready(&policy, k) || ble_rssi(k, &dbm) != 0) {
			continue;
		}
		core_rssi(slot_of(k), dbm);
		devices[k].rssi = dbm;
		devices[k].rssi_read = true;
	}
}

static void run_hidpp(enum device_kind kind, uint32_t now)
{
	struct hidpp *x = &runs[kind].hidpp;
	struct hidpp_event event;
	struct hidpp_msg m;

	if (!btpolicy_is_ready(&policy, kind) || !maps[kind].has_hidpp) {
		return;
	}
	hidpp_want_hires(x, usb_mouse_wheel_steps() != 1);
	if (hidpp_request(x, HIDPP_DEVICE_INDEX, now, HIDPP_RETRY_MS, &m)) {
		uint8_t data[HIDPP_LONG_LEN] = {0};

		hidpp_msg_write(&m, data, sizeof(data));
		/* A failed write counts as a try, so a report the device lacks ends the run. */
		ble_write_report(kind, HIDPP_LONG_REPORT_ID, data, sizeof(data));
		hidpp_taken(x, now);
	}
	if (hidpp_take_event(x, &event)) {
		core_log(slot_of(kind), event.detail);
	}
}

FUNC_NORETURN void bluetooth_run(int store_err)
{
	int start_err;

	ARG_UNUSED(store_err);
	core_start_history(&loop);
	btpolicy_init(&policy);
	btpair_init(&pair);
	for (int k = 0; k < DEVICE_KINDS; k++) {
		connection_over(k);
	}
	start_err = ble_start();
	if (start_err) {
		core_mark_radio_failed();
		printk("bluetooth start failed: %d\n", start_err);
	} else {
		load_list();
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
		for (int k = 0; k < DEVICE_KINDS; k++) {
			run_hidpp(k, now);
		}
		if (!core_radio_has_failed()) {
			int err = ble_scan(btpolicy_scan(&policy, btpair_is_open(&pair, now), now));

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
