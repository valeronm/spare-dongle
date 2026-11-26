#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/printk.h>
#include <nrfx.h>
#include "core.h"
#include "diag.h"
#include "radio.h"
#include "store.h"
#include "unifying.h"
#include "usb.h"

#define TICK_MS 10

_Static_assert(sizeof(((struct diag_status *)0)->name) >= UNIFYING_NAME_MAX, "status name");
_Static_assert(DIAG_NAME_MAX >= UNIFYING_NAME_MAX, "device list name");
_Static_assert(sizeof(((struct diag_status *)0)->keyboard_serial) == UNIFYING_SERIAL_LEN,
	       "status serial");

static struct unifying_session session;
static uint8_t next_nonce[UNIFYING_NONCE_LEN];

static void print_hex(const char *label, const uint8_t *buf, size_t len)
{
	printk("%s", label);
	for (size_t i = 0; i < len; i++) {
		printk(" %02x", buf[i]);
	}
	printk("\n");
}

static struct {
	uint8_t key[16];
	uint8_t clear[16];
	uint8_t cipher[16];
} ecb_block;

static void aes_ecb(const uint8_t key[UNIFYING_KEY_LEN], const uint8_t in[UNIFYING_AES_BLOCK_LEN],
		    uint8_t out[UNIFYING_AES_BLOCK_LEN])
{
	memcpy(ecb_block.key, key, sizeof(ecb_block.key));
	memcpy(ecb_block.clear, in, sizeof(ecb_block.clear));
	NRF_ECB->EVENTS_ENDECB = 0;
	NRF_ECB->EVENTS_ERRORECB = 0;
	NRF_ECB->ECBDATAPTR = (uint32_t)(uintptr_t)&ecb_block;
	NRF_ECB->TASKS_STARTECB = 1;
	while (NRF_ECB->EVENTS_ENDECB == 0 && NRF_ECB->EVENTS_ERRORECB == 0) {
	}
	memcpy(out, ecb_block.cipher, sizeof(ecb_block.cipher));
}

static bool active_pipe(uint8_t *pipe)
{
	if (session.active == UNIFYING_NO_PLACE) {
		return false;
	}
	*pipe = unifying_place_pipe(session.active);
	return true;
}

static void on_paired(uint8_t place)
{
	const struct unifying_device *d = &session.pairing.devices[place];
	uint8_t record[UNIFYING_RECORD_LEN];
	int err;

	unifying_record_of(&session.pairing, place, record);
	err = store_save_unifying(place, record, sizeof(record));
	printk("pairing saved in place %u: %d\n", place, err);
	core_log(0, (struct diag_detail){.tag = DIAG_DETAIL_UNIFYING_PAIRED,
					 .UNIFYING_PAIRED = {.wpid = {d->wpid[0], d->wpid[1]},
							     .kind = d->type,
							     .store_err = err}});
}

static int listen(uint8_t pipes)
{
	int err = core_listen(pipes);

	if (err) {
		printk("listening on pipes %02x failed: %d\n", pipes, err);
	}
	return err;
}

static void report_pairing(enum unifying_rx_event event, const uint8_t *reply, size_t n)
{
	const struct unifying_attempt *a = &session.pairing.attempt;

	switch (event) {
	case UNIFYING_RX_ANSWERED_1:
		printk("request 1: id %02x wpid %02x%02x type %02x caps %02x\n", a->id,
		       a->dev.wpid[0], a->dev.wpid[1], a->dev.type, a->caps);
		print_hex("reply", reply, n);
		break;
	case UNIFYING_RX_ANSWERED_2:
		printk("request 2: id %02x\n", a->id);
		break;
	case UNIFYING_RX_PAIRED:
		printk("request 3: name \"%.*s\", paired\n", UNIFYING_NAME_MAX, a->dev.name);
		break;
	default:
		break;
	}
}

static void queue_leds(uint8_t leds)
{
	uint8_t frame[UNIFYING_LED_FRAME_LEN];
	uint32_t taken;
	uint8_t pipe;
	size_t n = unifying_led_frame(leds, frame);

	if (!active_pipe(&pipe)) {
		return;
	}
	taken = radio_replies_taken();
	if (radio_reply(pipe, frame, n) == 0) {
		unifying_session_led_queued(&session, leds, taken);
	}
}

static uint8_t link_up_kind(enum unifying_heard h)
{
	switch (h) {
	case UNIFYING_HEARD_WOKE:
		return HISTORY_UP_WOKE;
	case UNIFYING_HEARD_AGAIN:
		return HISTORY_UP_AGAIN;
	default:
		return HISTORY_UP_FIRST;
	}
}

static void log_hidpp_event(const struct hidpp_event *e)
{
	if (e->pending) {
		core_log(0, e->detail);
	}
}

static void apply_frame(const struct unifying_out *out, const struct rx_item *item)
{
	if (out->invalid) {
		core_count(CORE_INVALID);
	} else if (out->from_active) {
		core_packet(0);
		core_rssi(0, item->rssi);
	}
	if (out->listen) {
		listen(out->pipes);
	}
	if (out->heard != UNIFYING_HEARD_NONE) {
		core_log(0, (struct diag_detail){.tag = DIAG_DETAIL_LINK_UP,
					     .LINK_UP = {.channel = session.channel,
							 .rssi = item->rssi,
							 .up = link_up_kind(out->heard),
							 .down_ms = out->down_ms}});
	}
	if (out->reconnected) {
		core_log(0, (struct diag_detail){.tag = DIAG_DETAIL_RECONNECT,
						 .RECONNECT = {.release_all = out->release_all}});
	}
	if (out->attack_started) {
		core_log(0, (struct diag_detail){.tag = DIAG_DETAIL_ATTACK});
	}
	if (out->event == UNIFYING_RX_ANSWERED_1) {
		sys_rand_get(next_nonce, sizeof(next_nonce));
	}
	if (out->event == UNIFYING_RX_REFUSED) {
		print_hex("request 1 refused, not Unifying-compatible:", item->data, item->len);
	}
	if (out->reply_len != 0) {
		int err = radio_reply(out->reply_pipe, out->reply, out->reply_len);

		if (err) {
			printk("queueing reply failed: %d\n", err);
		}
		report_pairing(out->event, out->reply, out->reply_len);
	}
	if (out->event == UNIFYING_RX_PAIRED) {
		on_paired(out->paired_place);
	}
	if (out->active_identified) {
		uint8_t addr[DIAG_ADDR_LEN];

		unifying_list_address(&session.pairing, session.active, addr);
		core_log_address(0, (struct diag_detail){.tag = DIAG_DETAIL_UNIFYING_ACTIVE}, addr);
	}
	if (out->release_all) {
		core_release_keys();
	}
	for (size_t i = 0; i < out->reports; i++) {
		core_submit(0, &out->report[i], &item->received_us);
	}
	log_hidpp_event(&out->hidpp_event);
}

static void apply_tick(const struct unifying_out *out)
{
	uint8_t pipe;

	if (out->listen) {
		listen(out->pipes);
	}
	if (out->release_all) {
		core_release_keys();
	}
	if (out->send_led) {
		queue_leds(out->leds);
	}
	if (out->hidpp_len != 0 && active_pipe(&pipe)) {
		uint32_t taken = radio_replies_taken();
		int err = radio_reply(pipe, out->hidpp, out->hidpp_len);

		if (err) {
			printk("queueing HID++ request failed: %d\n", err);
		} else {
			unifying_session_hidpp_queued(&session, taken);
		}
	}
	if (out->went_silent) {
		core_log(0, (struct diag_detail){.tag = DIAG_DETAIL_LINK_LOST,
						 .LINK_LOST = {.rssi = session.rssi,
							       .since_input_s = core_since_input_s(0),
							       .gap_ms = out->silent_ms}});
	}
	if (out->attempt_expired) {
		printk("pairing timed out\n");
		core_log(DEVICE_NO_SLOT,
			 (struct diag_detail){.tag = DIAG_DETAIL_PAIRING_TIMEOUT,
						 .PAIRING_TIMEOUT = {.step = out->expired_step}});
	}
	if (out->window_closed) {
		core_log_pairing_closed();
	}
	if (out->set_channel) {
		core_set_channel(out->channel);
	}
	log_hidpp_event(&out->hidpp_event);
}

static struct radio_config unifying_radio(const struct unifying_receiver *r, uint8_t channel)
{
	const uint8_t *pairing = unifying_pairing_address;

	struct radio_config rc = {
		.rate = RADIO_2MBPS,
		.crc = RADIO_CRC_16,
		.base0 = {pairing[3], pairing[2], pairing[1], pairing[0]},
		.base1 = {r->base[3], r->base[2], r->base[1], r->base[0]},
		.pipes = UNIFYING_PIPES,
		.channel = channel,
		.noise = true,
	};

	_Static_assert(sizeof(rc.prefixes) == UNIFYING_PIPES, "one prefix per pipe");
	unifying_prefixes(rc.prefixes);
	return rc;
}

static int home(void)
{
	return unifying_session_home(&session);
}

static int movable(void)
{
	return unifying_session_movable(&session);
}

static void replies_dropped(void)
{
	unifying_session_replies_dropped(&session);
}

static enum chanmove_result move_on_request(uint8_t channel, uint8_t *from)
{
	*from = session.channel;
	return unifying_session_move(&session, channel, (uint64_t)k_uptime_get());
}

static void radio_resumed(void)
{
	unifying_session_radio_resumed(&session, (uint64_t)k_uptime_get());
}

static bool pairing(void)
{
	return unifying_pairing_active(&session.pairing, (uint64_t)k_uptime_get());
}

static void slots(struct diag_slot *out)
{
	out[0] = (struct diag_slot){
		.kind = DEVICE_KIND_KEYBOARD,
		.stored = unifying_has_device(&session.pairing),
		.ready = unifying_session_status(&session, (uint64_t)k_uptime_get()) ==
			 UNIFYING_STATUS_CONNECTED,
	};
}

static void fill_status(struct diag_status *st, uint8_t slot)
{
	uint64_t now = (uint64_t)k_uptime_get();

	ARG_UNUSED(slot);
	st->link_state = unifying_session_status(&session, now);
	st->pairing_window = unifying_pairing_window_open(&session.pairing, now) ? DIAG_WINDOW_OPEN
										  : DIAG_WINDOW_CLOSED;
	st->channel = session.channel;
	if (session.heard) {
		st->last_rssi = session.rssi;
		st->last_rx_age_ms = (uint32_t)MIN(now - session.heard_ms, UINT32_MAX - 1);
	}
	if (unifying_has_device(&session.pairing)) {
		const struct unifying_device *d = &session.pairing.devices[session.active];

		memcpy(st->keyboard_id, d->wpid, sizeof(d->wpid));
		memcpy(st->keyboard_serial, d->serial, sizeof(st->keyboard_serial));
		st->keyboard_type = d->type;
		memcpy(st->name, d->name, sizeof(d->name));
		st->battery = session.battery;
	}
}

static void listed(struct diag_device *out, uint8_t place, uint64_t now)
{
	const struct unifying_device *d = &session.pairing.devices[place];
	bool connected = place == session.active &&
			 unifying_session_status(&session, now) == UNIFYING_STATUS_CONNECTED;

	*out = (struct diag_device){.kind = DEVICE_KIND_KEYBOARD,
				    .slot = connected ? 0 : DEVICE_NO_SLOT};
	unifying_list_address(&session.pairing, place, out->addr);
	memcpy(out->name, d->name, sizeof(d->name));
}

static void send_devices(const struct diag_request *req, uint64_t now)
{
	struct diag_device devices[UNIFYING_PLACES];
	size_t n = 0;

	for (uint8_t place = 0; place < UNIFYING_PLACES; place++) {
		if (session.pairing.stored[place]) {
			listed(&devices[n++], place, now);
		}
	}
	core_reply_devices(req, devices, n);
}

static void forget_keyboard(const struct diag_request *req, uint64_t now)
{
	struct diag_device device = {0};
	struct unifying_out out;
	uint8_t place = unifying_find_place(&session.pairing, req->data);
	int radio_err = 0;
	int listen_err;

	if (place != UNIFYING_NO_PLACE) {
		listed(&device, place, now);
	}
	if (!core_forget_matches(req, &device, place != UNIFYING_NO_PLACE)) {
		return;
	}
	if (store_delete_unifying(place) != 0) {
		diag_reply_error(req, DIAG_ERROR_STORE, 0);
		return;
	}
	unifying_session_forget(&session, place, now, &out);
	if (out.set_channel) {
		radio_err = core_set_channel(out.channel);
	}
	listen_err = listen(out.pipes);
	if (out.release_all) {
		core_release_keys();
	}
	core_keyboard_forgotten(req, out.window_opened ? &session.pairing.window : NULL,
				radio_err != 0 || listen_err != 0 ? DIAG_ERROR_RADIO : 0);
}

static void serve_diag(const struct diag_request *req, uint64_t now)
{
	switch (req->command) {
	case DIAG_COMMAND_DEVICES:
		send_devices(req, now);
		break;
	case DIAG_COMMAND_FORGET:
		forget_keyboard(req, now);
		break;
	case DIAG_COMMAND_PAIR:
		if (!unifying_session_open_pairing(&session, PAIRWIN_REQUEST, now)) {
			diag_reply_error(req, DIAG_ERROR_LIST_FULL, 0);
			break;
		}
		printk("pairing open for %u s\n", DIAG_PAIR_WINDOW_MS / 1000);
		core_log_pairing_opened(&session.pairing.window);
		diag_reply(req, NULL, 0, DIAG_AFTER_NONE);
		break;
	default:
		diag_reply_error(req, DIAG_ERROR_UNKNOWN_COMMAND, 0);
		break;
	}
}

static void ignore_host(bool suspended)
{
	ARG_UNUSED(suspended);
}

static const struct core_loop loop = {
	.home = home,
	.take_noise = core_take_radio_noise,
	.radio_resumed = radio_resumed,
	.slot_count = 1,
	.slots = slots,
	.fill_status = fill_status,
	.movable = movable,
	.candidates = unifying_session_channels,
	.replies_dropped = replies_dropped,
	.move = move_on_request,
	.host_suspended = ignore_host,
	.pairing = pairing,
};

FUNC_NORETURN void unifying_run(int store_err)
{
	uint32_t raw = NRF_FICR->DEVICEID[0] ^ NRF_FICR->DEVICEID[1];
	struct radio_config rc;
	struct unifying_receiver receiver = {.wpid = {0x88, 0x02}};
	uint8_t record[UNIFYING_RECORD_LEN];
	uint32_t reported_drops = 0;
	uint64_t now = (uint64_t)k_uptime_get();

	core_start_history(&loop);
	memcpy(receiver.base, &raw, sizeof(receiver.base));
	unifying_session_init(&session, &receiver);
	if (store_err == 0) {
		store_delete_single_unifying();
	}
	for (uint8_t place = 0; store_err == 0 && place < UNIFYING_PLACES; place++) {
		if (store_load_unifying(place, record, sizeof(record))) {
			unifying_session_restore(&session, place, record);
			printk("restored the keyboard in place %u\n", place);
		}
	}
	if (unifying_session_open_pairing(&session, PAIRWIN_BOOT, now)) {
		core_log_pairing_opened(&session.pairing.window);
	}
	sys_rand_get(next_nonce, sizeof(next_nonce));

	rc = unifying_radio(&receiver, session.channel);
	core_start_radio(&rc);
	listen(unifying_listen_pipes(&session.pairing));
	print_hex("unifying receiver, base address", receiver.base, sizeof(receiver.base));

	for (;;) {
		struct unifying_out out;
		struct diag_request req;
		struct rx_item item;
		uint32_t dropped;
		uint8_t leds;
		bool changed;
		bool got;

		got = core_take_rx(&item, K_MSEC(TICK_MS));
		now = (uint64_t)k_uptime_get();
		if (got && unifying_session_frame(&session, item.pipe, item.data, item.len, item.rssi,
						  now, next_nonce, aes_ecb, &out)) {
			apply_frame(&out, &item);
		}
		changed = core_take_leds(&leds);
		core_poll();
		unifying_session_tick(&session, now, changed, leds, radio_replies_taken(),
				      &out);
		apply_tick(&out);
		while (core_take_request(&req)) {
			serve_diag(&req, now);
		}
		dropped = core_rx_dropped();
		/* A clear request zeroes the drop count. */
		if (dropped > reported_drops) {
			printk("receive queue full, %u frames dropped\n", dropped - reported_drops);
		}
		reported_drops = dropped;
		core_show_state();
		core_watchdog_feed();
	}
}
