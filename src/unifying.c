#include <string.h>
#include "unifying.h"

#define MARKER_REQUEST 0x5f
#define MARKER_RESPONSE 0x1f
#define MARKER_FINAL 0x0f
#define STEP_FINAL 0x06
#define CAP_UNIFYING 0x04
#define TYPE_KEYBOARD 0x01
#define REPORT_TYPE_MASK 0x1f
#define REPORT_ENCRYPTED_KEYBOARD 0x13
#define REPORT_MEDIA 0x03
#define REPORT_LEDS 0x0e
#define FINAL_LEN 10
#define MEDIA_FRAME_LEN 10
#define PLAINTEXT_MARKER 0xc9
#define REPORT_KEEPALIVE 0x40
#define REPORT_SET_KEEPALIVE 0x0f
#define KEEPALIVE_LEN 5
#define SET_KEEPALIVE_LEN 10
#define CONNECT_SHORT_LEN 10
#define HIDPP_SHORT_FRAME_LEN 10
#define FIRST_OCTET 0x07

const uint8_t unifying_pairing_address[UNIFYING_ADDRESS_LEN] = {0xbb, 0x0a, 0xdc, 0xa5, 0x75};

uint8_t unifying_place_pipe(uint8_t place)
{
	return place + 1;
}

uint8_t unifying_place_octet(uint8_t place)
{
	return FIRST_OCTET + place;
}

uint8_t unifying_pipe_place(uint8_t pipe)
{
	return pipe >= 1 && pipe <= UNIFYING_PLACES ? pipe - 1 : UNIFYING_NO_PLACE;
}

uint8_t unifying_octet_place(uint8_t octet)
{
	return octet >= FIRST_OCTET && octet < FIRST_OCTET + UNIFYING_PLACES ? octet - FIRST_OCTET
									      : UNIFYING_NO_PLACE;
}

void unifying_prefixes(uint8_t out[UNIFYING_PIPES])
{
	out[UNIFYING_PIPE_PAIRING] = unifying_pairing_address[UNIFYING_ADDRESS_LEN - 1];
	out[UNIFYING_PIPE_RECEIVER] = 0x00;
	for (uint8_t place = 0; place < UNIFYING_PLACES; place++) {
		out[unifying_place_pipe(place)] = unifying_place_octet(place);
	}
}

void unifying_device_address(const struct unifying_receiver *r, uint8_t place,
			     uint8_t out[UNIFYING_ADDRESS_LEN])
{
	memcpy(out, r->base, sizeof(r->base));
	out[4] = unifying_place_octet(place);
}

static bool attempting(const struct unifying_pairing *p)
{
	return p->attempt.state != UNIFYING_IDLE;
}

static uint8_t first_place(const struct unifying_pairing *p, bool stored)
{
	for (uint8_t place = 0; place < UNIFYING_PLACES; place++) {
		if (p->stored[place] == stored) {
			return place;
		}
	}
	return UNIFYING_NO_PLACE;
}

bool unifying_has_device(const struct unifying_pairing *p)
{
	return first_place(p, true) != UNIFYING_NO_PLACE;
}

_Static_assert(DIAG_ADDR_LEN >= 2 + UNIFYING_SERIAL_LEN + 1, "device list address");

void unifying_list_address(const struct unifying_pairing *p, uint8_t place,
			   uint8_t out[DIAG_ADDR_LEN])
{
	const struct unifying_device *d = &p->devices[place];

	memset(out, 0, DIAG_ADDR_LEN);
	if (place >= UNIFYING_PLACES) {
		return;
	}
	memcpy(out, d->wpid, sizeof(d->wpid));
	memcpy(&out[sizeof(d->wpid)], d->serial, sizeof(d->serial));
	out[sizeof(d->wpid) + sizeof(d->serial)] = unifying_place_octet(place);
}

uint8_t unifying_find_place(const struct unifying_pairing *p, const uint8_t addr[DIAG_ADDR_LEN])
{
	for (uint8_t place = 0; place < UNIFYING_PLACES; place++) {
		uint8_t listed[DIAG_ADDR_LEN];

		unifying_list_address(p, place, listed);
		if (p->stored[place] && memcmp(addr, listed, sizeof(listed)) == 0) {
			return place;
		}
	}
	return UNIFYING_NO_PLACE;
}

bool unifying_pairing_open(struct unifying_pairing *p, enum pairwin_cause cause, uint64_t now_ms)
{
	if (first_place(p, false) == UNIFYING_NO_PLACE) {
		return false;
	}
	pairwin_open(&p->window, cause, unifying_has_device(p), (uint32_t)now_ms);
	return true;
}

uint8_t unifying_listen_pipes(const struct unifying_pairing *p)
{
	uint8_t pipes = 1u << UNIFYING_PIPE_PAIRING | 1u << UNIFYING_PIPE_RECEIVER;

	for (uint8_t place = 0; place < UNIFYING_PLACES; place++) {
		if (p->stored[place] || (attempting(p) && p->attempt.place == place)) {
			pipes |= 1u << unifying_place_pipe(place);
		}
	}
	return pipes;
}

void unifying_pairing_restore(struct unifying_pairing *p, uint8_t place,
			      const struct unifying_device *d)
{
	if (place >= UNIFYING_PLACES) {
		return;
	}
	p->stored[place] = true;
	p->devices[place] = *d;
}

void unifying_pairing_forget(struct unifying_pairing *p, uint8_t place)
{
	if (place >= UNIFYING_PLACES) {
		return;
	}
	p->stored[place] = false;
	p->devices[place] = (struct unifying_device){0};
}

bool unifying_pairing_window_open(const struct unifying_pairing *p, uint64_t now_ms)
{
	return pairwin_is_open(&p->window, (uint32_t)now_ms);
}

uint8_t unifying_sweep_channel(struct unifying_sweep *s, const struct unifying_pairing *p,
			       uint8_t current, uint64_t now_ms)
{
	/* A stored keyboard types on current, and a pairing keyboard hops until it finds it. */
	if (!unifying_pairing_window_open(p, now_ms) || attempting(p) || unifying_has_device(p) ||
	    now_ms - s->last_ms < UNIFYING_SWEEP_DWELL_MS) {
		return current;
	}
	s->index = (s->index + 1) % UNIFYING_PAIRING_CHANNELS;
	s->last_ms = now_ms;
	return diag_unifying_channels[s->index];
}

uint8_t unifying_checksum(const uint8_t *buf, size_t len)
{
	uint8_t sum = 0;

	for (size_t i = 0; i < len; i++) {
		sum += buf[i];
	}
	return (uint8_t)(0 - sum);
}

bool unifying_checksum_ok(const uint8_t *frame, size_t len)
{
	uint8_t want;

	if (len < 2) {
		return false;
	}
	want = unifying_checksum(frame, len - 1);
	return (frame[len - 1] | 0x01) == (want | 0x01);
}

void unifying_derive_key(uint8_t key[UNIFYING_KEY_LEN], const uint8_t base[4],
			 const uint8_t device_wpid[2], const uint8_t dongle_wpid[2],
			 const uint8_t device_nonce[UNIFYING_NONCE_LEN],
			 const uint8_t dongle_nonce[UNIFYING_NONCE_LEN])
{
	static const uint8_t index[UNIFYING_KEY_LEN] = {7,  1, 0, 3,  10, 2,  9, 14,
							8,  6, 12, 5, 13, 15, 4, 11};
	static const uint8_t flip[UNIFYING_KEY_LEN] = {0x00, 0xff, 0x00, 0x00, 0x00, 0xff,
						       0x55, 0x00, 0x00, 0x00, 0xff, 0x00,
						       0x00, 0x55, 0x00, 0x00};
	uint8_t data[UNIFYING_KEY_LEN];

	memcpy(&data[0], base, 4);
	memcpy(&data[4], device_wpid, 2);
	memcpy(&data[6], dongle_wpid, 2);
	memcpy(&data[8], device_nonce, UNIFYING_NONCE_LEN);
	memcpy(&data[12], dongle_nonce, UNIFYING_NONCE_LEN);
	for (size_t i = 0; i < UNIFYING_KEY_LEN; i++) {
		key[i] = data[index[i]] ^ flip[i];
	}
}

static size_t seal(uint8_t *frame, size_t len)
{
	frame[len - 1] = unifying_checksum(frame, len - 1);
	return len;
}

static size_t response_1(const struct unifying_attempt *a, const struct unifying_receiver *r,
			 uint8_t *out)
{
	memset(out, 0, UNIFYING_FRAME_MAX);
	out[0] = a->id;
	out[1] = MARKER_RESPONSE;
	out[2] = 0x01;
	memcpy(&out[3], r->base, 4);
	out[7] = unifying_place_octet(a->place);
	out[8] = 0x08;
	memcpy(&out[9], r->wpid, 2);
	out[13] = a->dev.type;
	out[14] = a->caps;
	return seal(out, UNIFYING_FRAME_MAX);
}

static size_t response_2(const struct unifying_attempt *a, const uint8_t *request, uint8_t *out)
{
	memset(out, 0, UNIFYING_FRAME_MAX);
	out[0] = a->id;
	out[1] = MARKER_RESPONSE;
	out[2] = 0x02;
	memcpy(&out[3], a->dongle_nonce, UNIFYING_NONCE_LEN);
	memcpy(&out[7], a->dev.serial, UNIFYING_SERIAL_LEN);
	memcpy(&out[11], &request[11], 2);
	return seal(out, UNIFYING_FRAME_MAX);
}

static size_t response_final(const struct unifying_attempt *a, uint8_t *out)
{
	memset(out, 0, FINAL_LEN);
	out[0] = a->id;
	out[1] = MARKER_FINAL;
	out[2] = STEP_FINAL;
	memcpy(&out[3], a->device_nonce, UNIFYING_NONCE_LEN);
	memcpy(&out[7], a->dongle_nonce, 2);
	return seal(out, FINAL_LEN);
}

static uint8_t request_step(const uint8_t *frame, size_t len)
{
	if (len != UNIFYING_FRAME_MAX || (frame[1] & 0x7f) != MARKER_REQUEST ||
	    !unifying_checksum_ok(frame, len) || frame[2] < 1 || frame[2] > 3) {
		return 0;
	}
	return frame[2];
}

static size_t start(struct unifying_attempt *a, const struct unifying_receiver *r, uint8_t place,
		    const uint8_t nonce[UNIFYING_NONCE_LEN], const uint8_t *frame, uint64_t now_ms,
		    uint8_t *reply, enum unifying_rx_event *event)
{
	if (!(frame[14] & CAP_UNIFYING) || frame[13] != TYPE_KEYBOARD) {
		*a = (struct unifying_attempt){0};
		*event = UNIFYING_RX_REFUSED;
		return 0;
	}
	*a = (struct unifying_attempt){
		.state = UNIFYING_ANSWERED_1,
		.id = frame[0],
		.place = place,
		.last_ms = now_ms,
		.dev = {.wpid = {frame[9], frame[10]}, .type = frame[13]},
		.caps = frame[14],
	};
	memcpy(a->dongle_nonce, nonce, UNIFYING_NONCE_LEN);
	*event = UNIFYING_RX_ANSWERED_1;
	return response_1(a, r, reply);
}

size_t unifying_pairing_rx(struct unifying_pairing *p, const struct unifying_receiver *r,
			   const uint8_t nonce[UNIFYING_NONCE_LEN], const uint8_t *frame, size_t len,
			   uint64_t now_ms, uint8_t *reply, enum unifying_rx_event *event)
{
	struct unifying_attempt *a = &p->attempt;
	uint8_t step = request_step(frame, len);

	*event = UNIFYING_RX_NONE;
	if (step == 1) {
		bool repeat = attempting(p) && frame[0] == a->id;
		uint8_t place = first_place(p, false);

		return !unifying_pairing_window_open(p, now_ms) || repeat ||
				       place == UNIFYING_NO_PLACE
			       ? 0
			       : start(a, r, place, nonce, frame, now_ms, reply, event);
	}
	if (step == 2 && a->state == UNIFYING_ANSWERED_1 && frame[0] == a->id) {
		memcpy(a->device_nonce, &frame[3], UNIFYING_NONCE_LEN);
		memcpy(a->dev.serial, &frame[7], UNIFYING_SERIAL_LEN);
		unifying_derive_key(a->dev.key, r->base, a->dev.wpid, r->wpid, a->device_nonce,
				    a->dongle_nonce);
		a->state = UNIFYING_ANSWERED_2;
		a->last_ms = now_ms;
		*event = UNIFYING_RX_ANSWERED_2;
		return response_2(a, frame, reply);
	}
	/* Request 3 carries 0x00 in the id byte, not the attempt's id. */
	if (step == 3 && a->state == UNIFYING_ANSWERED_2) {
		memcpy(a->dev.name, &frame[5],
		       frame[4] < UNIFYING_NAME_MAX ? frame[4] : UNIFYING_NAME_MAX);
		a->state = UNIFYING_IDLE;
		unifying_pairing_restore(p, a->place, &a->dev);
		pairwin_close(&p->window);
		*event = UNIFYING_RX_PAIRED;
		return response_final(a, reply);
	}
	return 0;
}

bool unifying_pairing_expired(struct unifying_pairing *p, uint64_t now_ms)
{
	if (attempting(p) && now_ms - p->attempt.last_ms > UNIFYING_PAIRING_TIMEOUT_MS) {
		p->attempt = (struct unifying_attempt){0};
		return true;
	}
	return false;
}

static bool shaped(const uint8_t *frame, size_t len, size_t want_len, uint8_t want_type)
{
	return len == want_len && (frame[1] & REPORT_TYPE_MASK) == want_type;
}

static bool frame_is(const uint8_t *frame, size_t len, size_t want_len, uint8_t want_type)
{
	return shaped(frame, len, want_len, want_type) && unifying_checksum_ok(frame, len);
}

bool unifying_is_encrypted_keyboard(const uint8_t *frame, size_t len)
{
	return frame_is(frame, len, UNIFYING_FRAME_MAX, REPORT_ENCRYPTED_KEYBOARD);
}

void unifying_link_reset(struct unifying_link *l)
{
	*l = (struct unifying_link){.keepalive_ms = UNIFYING_KEEPALIVE_DEFAULT_MS};
}

bool unifying_is_connect_frame(const uint8_t *frame, size_t len, uint8_t device_octet)
{
	bool shape = (len == UNIFYING_FRAME_MAX && frame[1] == 0x51 && frame[3] == 0x04) ||
		     (len == CONNECT_SHORT_LEN && frame[1] == 0x4f && frame[3] == 0x00);

	return shape && frame[2] == 0x07 && frame[0] == device_octet &&
	       unifying_checksum_ok(frame, len);
}

static bool any_held(const struct unifying_link *l)
{
	return l->keyboard_down || l->media_usage != 0 || l->divert_usage != 0;
}

static void forget_held(struct unifying_link *l)
{
	l->keyboard_down = false;
	l->media_usage = 0;
	l->divert_usage = 0;
}

bool unifying_link_reconnect(struct unifying_link *l, uint8_t device_octet, const uint8_t *frame,
			     size_t len)
{
	bool held = any_held(l);

	if (!unifying_is_connect_frame(frame, len, device_octet)) {
		return false;
	}
	l->synced = false;
	l->next_counter = 0;
	l->has_candidate = false;
	forget_held(l);
	return held;
}

static void counter_block(const uint8_t *frame, uint8_t block[UNIFYING_AES_BLOCK_LEN])
{
	static const uint8_t head[7] = {0x04, 0x14, 0x1d, 0x1f, 0x27, 0x28, 0x0d};
	static const uint8_t tail[5] = {0x0a, 0x0d, 0x13, 0x26, 0x0e};

	memcpy(block, head, sizeof(head));
	memcpy(&block[sizeof(head)], &frame[10], 4);
	memcpy(&block[sizeof(head) + 4], tail, sizeof(tail));
}

static bool beyond_window(const struct unifying_link *l, uint64_t counter)
{
	return counter >= l->next_counter + UNIFYING_COUNTER_WINDOW;
}

enum verdict {
	REFUSE,
	HOLD,
	TYPE,
};

struct classified {
	enum verdict verdict;
	bool types_candidate;
};

/* A held jump is never typed itself; only the frame that confirms it is. */
static struct classified classify(const struct unifying_link *l, uint64_t counter, bool release,
				  bool attack)
{
	bool confirms = l->has_candidate && counter == l->candidate.counter + 1;

	if (l->synced && !attack) {
		return (struct classified){
			.verdict = confirms || !beyond_window(l, counter) ? TYPE : HOLD};
	}
	if (attack && (l->synced ? !confirms && beyond_window(l, counter) : !release)) {
		return (struct classified){.verdict = REFUSE};
	}
	return (struct classified){
		.verdict = release || (confirms && !attack) ? TYPE : HOLD,
		.types_candidate = confirms,
	};
}

static void type(struct unifying_link *l, const struct report *r, uint64_t next_counter,
		 struct report *out)
{
	l->synced = true;
	l->next_counter = next_counter;
	l->keyboard_down = !reports_keys_empty(r);
	*out = *r;
}

size_t unifying_keystroke(struct unifying_link *l, const uint8_t key[UNIFYING_KEY_LEN],
			  const uint8_t *frame, size_t len, uint64_t now_ms, unifying_aes_fn aes,
			  struct report out[UNIFYING_KEYSTROKE_REPORTS])
{
	uint8_t block[UNIFYING_AES_BLOCK_LEN], stream[UNIFYING_AES_BLOCK_LEN];
	uint8_t p[REPORT_KEYBOARD_LEN];
	struct report r;
	bool attack = now_ms < l->attack_until_ms;
	struct classified c;
	uint64_t counter;
	size_t n = 0;

	if (!unifying_is_encrypted_keyboard(frame, len)) {
		return 0;
	}
	if (!attack && l->candidate.held_in_attack) {
		l->has_candidate = false;
	}
	counter = (uint64_t)frame[10] << 24 | (uint64_t)frame[11] << 16 | (uint64_t)frame[12] << 8 |
		  frame[13];
	if (counter < l->next_counter || counter >= UNIFYING_COUNTER_LIMIT) {
		/* Only a jump, held with a counter known and no attack, survives a refused frame. */
		if (!l->synced || attack) {
			l->has_candidate = false;
		}
		l->refused++;
		return 0;
	}
	counter_block(frame, block);
	aes(key, block, stream);
	for (size_t i = 0; i < sizeof(p); i++) {
		p[i] = frame[2 + i] ^ stream[i];
	}
	if (p[7] != PLAINTEXT_MARKER) {
		l->has_candidate = false;
		l->attack_until_ms = now_ms + UNIFYING_ATTACK_MS;
		l->refused++;
		return 0;
	}
	r = reports_held_keys(p[0], &p[1]);
	c = classify(l, counter, reports_keys_empty(&r), attack);
	if (c.verdict == REFUSE) {
		l->refused++;
	}
	if (c.types_candidate) {
		type(l, &l->candidate.report, counter, &out[n++]);
	}
	l->has_candidate = c.verdict == HOLD;
	if (c.verdict == HOLD) {
		l->candidate = (struct unifying_candidate){
			.counter = counter, .report = r, .held_in_attack = attack};
	} else if (c.verdict == TYPE) {
		type(l, &r, counter + 1, &out[n++]);
	}
	return n;
}

bool unifying_media_report(struct unifying_link *l, const uint8_t *frame, size_t len,
			   struct report *out)
{
	if (!frame_is(frame, len, MEDIA_FRAME_LEN, REPORT_MEDIA)) {
		return false;
	}
	l->media_usage = frame[2] | frame[3] << 8;
	*out = reports_consumer(l->media_usage, l->divert_usage);
	return true;
}

static bool is_hidpp(const uint8_t *frame, size_t len)
{
	return shaped(frame, len, HIDPP_SHORT_FRAME_LEN, HIDPP_SHORT_REPORT_ID) ||
	       shaped(frame, len, UNIFYING_FRAME_MAX, HIDPP_LONG_REPORT_ID);
}

/* A frame carries the message from byte 2 up to its checksum. */
#define HIDPP_FRAME_OVERHEAD 3

static size_t hidpp_frame(const struct hidpp_msg *m, uint8_t out[UNIFYING_FRAME_MAX])
{
	size_t len = m->is_long ? UNIFYING_FRAME_MAX : HIDPP_SHORT_FRAME_LEN;

	memset(out, 0, UNIFYING_FRAME_MAX);
	out[1] = m->is_long ? HIDPP_LONG_REPORT_ID : HIDPP_SHORT_REPORT_ID;
	hidpp_msg_write(m, &out[2], len - HIDPP_FRAME_OVERHEAD);
	return seal(out, len);
}

/* Call it only while no other frame of the dongle's waits in the queue. */
static size_t hidpp_request_frame(struct hidpp *x, uint8_t device_index, uint64_t now_ms,
				  uint32_t retry_ms, uint8_t out[UNIFYING_FRAME_MAX])
{
	struct hidpp_msg m;

	return hidpp_request(x, device_index, now_ms, retry_ms, &m) ? hidpp_frame(&m, out) : 0;
}

/* Takes only a frame whose checksum the caller checked. */
static bool hidpp_rx_frame(struct hidpp *x, struct unifying_link *l, struct device_battery *b,
			   const uint8_t *frame, size_t len, struct report *out)
{
	struct hidpp_msg m;
	uint16_t usage;

	if (!is_hidpp(frame, len)) {
		return false;
	}
	hidpp_msg_read(&frame[2], len - HIDPP_FRAME_OVERHEAD, len == UNIFYING_FRAME_MAX, &m);
	if (!hidpp_rx(x, b, &m, &usage) || usage == l->divert_usage) {
		return false;
	}
	l->divert_usage = usage;
	*out = reports_consumer(usage, l->media_usage);
	return true;
}

size_t unifying_led_frame(uint8_t leds, uint8_t out[UNIFYING_LED_FRAME_LEN])
{
	memset(out, 0, UNIFYING_LED_FRAME_LEN);
	out[1] = REPORT_LEDS;
	out[2] = leds;
	return seal(out, UNIFYING_LED_FRAME_LEN);
}

void unifying_led_want(struct unifying_led *led, uint8_t leds)
{
	led->wanted = leds;
	if (!led->has_sent) {
		led->resend = true;
	}
}

void unifying_led_resend(struct unifying_led *led)
{
	led->resend = true;
}

bool unifying_led_due(const struct unifying_led *led, bool has_device, uint8_t *leds)
{
	if (!has_device || !(led->resend || led->wanted != led->sent)) {
		return false;
	}
	*leds = led->wanted;
	return true;
}

void unifying_led_sent(struct unifying_led *led, uint8_t leds)
{
	led->sent = leds;
	led->has_sent = true;
	led->resend = false;
}

static uint16_t keepalive_timeout(const uint8_t *frame, size_t len)
{
	if (frame_is(frame, len, KEEPALIVE_LEN, REPORT_KEEPALIVE & REPORT_TYPE_MASK) &&
	    frame[1] == REPORT_KEEPALIVE) {
		return (uint16_t)(frame[2] << 8 | frame[3]);
	}
	if (frame_is(frame, len, SET_KEEPALIVE_LEN, REPORT_SET_KEEPALIVE) && frame[2] == 0x00) {
		return (uint16_t)(frame[3] << 8 | frame[4]);
	}
	return 0;
}

void unifying_link_heard(struct unifying_link *l, const uint8_t *frame, size_t len,
			 uint64_t now_ms)
{
	uint16_t timeout = keepalive_timeout(frame, len);

	l->last_ms = now_ms;
	if (timeout != 0) {
		l->keepalive_ms = timeout;
	}
}

bool unifying_link_silent(struct unifying_link *l, uint64_t now_ms)
{
	bool silent = any_held(l) && now_ms - l->last_ms > 2 * (uint64_t)l->keepalive_ms;

	if (silent) {
		forget_held(l);
	}
	return silent;
}

void unifying_record_of(const struct unifying_pairing *p, uint8_t place,
			uint8_t out[UNIFYING_RECORD_LEN])
{
	memset(out, 0, UNIFYING_RECORD_LEN);
	if (place < UNIFYING_PLACES) {
		memcpy(out, &p->devices[place], UNIFYING_RECORD_LEN);
	}
}

void unifying_record_restore(struct unifying_pairing *p, uint8_t place,
			     const uint8_t in[UNIFYING_RECORD_LEN])
{
	struct unifying_device d;

	memcpy(&d, in, UNIFYING_RECORD_LEN);
	unifying_pairing_restore(p, place, &d);
}

static struct unifying_link *active_link(struct unifying_session *s)
{
	return &s->links[s->active];
}

static bool quiet_too_long(const struct unifying_session *s, uint64_t now_ms)
{
	uint64_t since = s->heard_ms > s->deaf_until_ms ? s->heard_ms : s->deaf_until_ms;

	if (s->search_until_ms > since) {
		since = s->search_until_ms;
	}

	/* search_until_ms can lie after now_ms. */
	if (now_ms <= since) {
		return false;
	}
	return now_ms - since >= 2 * (uint64_t)s->links[s->active].keepalive_ms;
}

static void take_hidpp_event(struct unifying_session *s, struct unifying_out *out)
{
	hidpp_take_event(&s->hidpp, &out->hidpp_event);
}

static bool pressed(const struct report *r, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		if (r[i].kind == REPORT_KEYBOARD ? !reports_keys_empty(&r[i]) : r[i].usage != 0) {
			return true;
		}
	}
	return false;
}

static void activate(struct unifying_session *s, uint8_t place, struct unifying_out *out)
{
	if (s->active != UNIFYING_NO_PLACE && any_held(active_link(s))) {
		forget_held(active_link(s));
		out->release_all = true;
	}
	/* A reply still queued would be taken by the old keyboard and counted as this one's. */
	out->listen = s->queue.frame != UNIFYING_QUEUED_NONE;
	s->active = place;
	s->connect_in_silence = false;
	s->search_until_ms = 0;
	s->battery = DEVICE_BATTERY_UNKNOWN;
	hidpp_init(&s->hidpp, DEVICE_KIND_KEYBOARD);
	hidpp_start(&s->hidpp);
	unifying_led_resend(&s->led);
}

void unifying_session_init(struct unifying_session *s, const struct unifying_receiver *r,
			   uint64_t now_ms)
{
	memset(s, 0, sizeof(*s));
	hidpp_init(&s->hidpp, DEVICE_KIND_KEYBOARD);
	s->receiver = *r;
	s->active = UNIFYING_NO_PLACE;
	s->started_ms = now_ms;
	s->battery = DEVICE_BATTERY_UNKNOWN;
	for (uint8_t place = 0; place < UNIFYING_PLACES; place++) {
		unifying_link_reset(&s->links[place]);
	}
	s->channel = diag_unifying_channels[s->sweep.index];
}

void unifying_session_restore(struct unifying_session *s, uint8_t place,
			      const uint8_t record[UNIFYING_RECORD_LEN])
{
	if (place >= UNIFYING_PLACES) {
		return;
	}
	unifying_record_restore(&s->pairing, place, record);
	if (s->active == UNIFYING_NO_PLACE) {
		s->active = place;
	}
}

void unifying_session_forget(struct unifying_session *s, uint8_t place, uint64_t now_ms,
			     struct unifying_out *out)
{
	bool was_active = place == s->active;
	uint8_t channel = s->channel;

	*out = (struct unifying_out){0};
	out->pipes = unifying_listen_pipes(&s->pairing);
	if (place >= UNIFYING_PLACES) {
		return;
	}
	unifying_pairing_forget(&s->pairing, place);
	unifying_link_reset(&s->links[place]);
	if (!unifying_has_device(&s->pairing)) {
		struct unifying_receiver r = s->receiver;

		unifying_session_init(s, &r, now_ms);
		unifying_session_open_pairing(s, PAIRWIN_REQUEST, now_ms);
		out->window_opened = true;
	} else if (was_active) {
		activate(s, first_place(&s->pairing, true), out);
		s->heard = false;
		s->silent = false;
		s->started_ms = now_ms;
	}
	out->release_all = was_active;
	out->set_channel = s->channel != channel;
	out->channel = s->channel;
	out->listen = true;
	out->pipes = unifying_listen_pipes(&s->pairing);
}

bool unifying_session_open_pairing(struct unifying_session *s, enum pairwin_cause cause,
				   uint64_t now_ms)
{
	return unifying_pairing_open(&s->pairing, cause, now_ms);
}

static void take_over(struct unifying_session *s, uint8_t place, struct unifying_out *out)
{
	activate(s, place, out);
	out->active_identified = true;
	/* A gap in the host's link began with the old keyboard. */
	if (s->heard && !s->silent) {
		s->silent = true;
		s->silent_since_ms = s->heard_ms;
	}
}

static void mark_heard(struct unifying_session *s, int8_t rssi, uint64_t now_ms,
		       struct unifying_out *out)
{
	out->from_active = true;
	if (!s->heard) {
		out->heard = UNIFYING_HEARD_FIRST;
		out->down_ms = (uint32_t)(now_ms - s->started_ms);
		out->active_identified = true;
	} else if (s->silent) {
		out->heard = s->connect_in_silence ? UNIFYING_HEARD_WOKE : UNIFYING_HEARD_AGAIN;
		out->down_ms = (uint32_t)(now_ms - s->silent_since_ms);
	}
	/* The keyboard forgets a divert whenever it reconnects, which a silence can hide. */
	if (out->heard != UNIFYING_HEARD_NONE) {
		hidpp_start(&s->hidpp);
	}
	s->silent = false;
	s->connect_in_silence = false;
	s->search_until_ms = 0;
	s->heard = true;
	s->heard_ms = now_ms;
	s->rssi = rssi;
}

static bool place_frame(struct unifying_session *s, uint8_t place, const uint8_t *frame,
			size_t len, int8_t rssi, uint64_t now_ms, unifying_aes_fn aes,
			struct unifying_out *out)
{
	struct unifying_link *l = &s->links[place];
	bool checksum_ok = unifying_checksum_ok(frame, len);
	uint32_t refused = l->refused;
	bool attack = now_ms < l->attack_until_ms;
	struct report typed[UNIFYING_KEYSTROKE_REPORTS];
	size_t n;

	if (!s->pairing.stored[place]) {
		/* Requests 2 and 3 arrive on the place the attempt targets. */
		return attempting(&s->pairing) && s->pairing.attempt.place == place;
	}
	unifying_link_heard(l, frame, len, now_ms);
	n = unifying_keystroke(l, s->pairing.devices[place].key, frame, len, now_ms, aes, typed);
	if (n == 0 && unifying_media_report(l, frame, len, &typed[0])) {
		n = 1;
	}
	out->invalid = !checksum_ok || l->refused != refused;
	out->attack_started = !attack && now_ms < l->attack_until_ms;
	if (place != s->active) {
		if (!pressed(typed, n) && !(checksum_ok && (!s->heard || s->silent))) {
			return true;
		}
		take_over(s, place, out);
	}
	mark_heard(s, rssi, now_ms, out);
	if (!attempting(&s->pairing) && checksum_ok) {
		pairwin_link_up(&s->pairing.window);
	}
	if (n == 0 && checksum_ok &&
	    hidpp_rx_frame(&s->hidpp, l, &s->battery, frame, len, &typed[0])) {
		n = 1;
	}
	out->reports = n;
	memcpy(out->report, typed, n * sizeof(typed[0]));
	return true;
}

static bool receiver_frame(struct unifying_session *s, const uint8_t *frame, size_t len,
			   struct unifying_out *out)
{
	uint8_t place = len > 0 ? unifying_octet_place(frame[0]) : UNIFYING_NO_PLACE;

	if (!unifying_has_device(&s->pairing)) {
		return false;
	}
	if (place != UNIFYING_NO_PLACE && s->pairing.stored[place] &&
	    unifying_is_connect_frame(frame, len, unifying_place_octet(place))) {
		bool held = unifying_link_reconnect(&s->links[place], unifying_place_octet(place),
						    frame, len);

		if (place == s->active) {
			out->reconnected = true;
			out->release_all = held;
			hidpp_start(&s->hidpp);
			s->connect_in_silence |= s->silent;
		}
	}
	unifying_led_resend(&s->led);
	return true;
}

bool unifying_session_frame(struct unifying_session *s, uint8_t pipe, const uint8_t *frame,
			    size_t len, int8_t rssi, uint64_t now_ms,
			    const uint8_t nonce[UNIFYING_NONCE_LEN], unifying_aes_fn aes,
			    struct unifying_out *out)
{
	uint8_t pipes = unifying_listen_pipes(&s->pairing);
	uint8_t place = unifying_pipe_place(pipe);
	bool handled = true;

	*out = (struct unifying_out){0};
	out->reply_len = unifying_pairing_rx(&s->pairing, &s->receiver, nonce, frame, len, now_ms,
					     out->reply, &out->event);
	out->reply_pipe = pipe;
	if (out->event == UNIFYING_RX_PAIRED) {
		out->paired_place = s->pairing.attempt.place;
		unifying_link_reset(&s->links[out->paired_place]);
		take_over(s, out->paired_place, out);
		unifying_link_heard(&s->links[out->paired_place], frame, len, now_ms);
		mark_heard(s, rssi, now_ms, out);
	} else if (place != UNIFYING_NO_PLACE) {
		handled = place_frame(s, place, frame, len, rssi, now_ms, aes, out);
	} else if (pipe == UNIFYING_PIPE_RECEIVER) {
		handled = receiver_frame(s, frame, len, out);
	}
	out->pipes = unifying_listen_pipes(&s->pairing);
	out->listen |= out->pipes != pipes;
	take_hidpp_event(s, out);
	return handled;
}

void unifying_session_tick(struct unifying_session *s, uint64_t now_ms, bool leds_changed,
			   uint8_t host_leds, uint32_t taken_count, struct unifying_out *out)
{
	uint8_t step = (uint8_t)s->pairing.attempt.state;
	uint8_t pipes = unifying_listen_pipes(&s->pairing);
	uint8_t next;
	bool pending;

	*out = (struct unifying_out){0};
	if (s->heard && !s->silent && unifying_has_device(&s->pairing) &&
	    quiet_too_long(s, now_ms)) {
		s->silent = true;
		s->silent_since_ms = now_ms;
		out->went_silent = true;
		out->silent_ms = (uint32_t)(now_ms - s->heard_ms);
	}
	out->release_all = unifying_has_device(&s->pairing) && now_ms >= s->search_until_ms &&
			   unifying_link_silent(active_link(s), now_ms);
	if (leds_changed) {
		unifying_led_want(&s->led, host_leds);
	}
	if (s->queue.frame != UNIFYING_QUEUED_NONE && taken_count != s->queue.taken_count) {
		if (s->queue.frame == UNIFYING_QUEUED_HIDPP) {
			hidpp_taken(&s->hidpp, now_ms);
		}
		s->queue.frame = UNIFYING_QUEUED_NONE;
	}
	if (s->queue.frame == UNIFYING_QUEUED_NONE) {
		out->send_led = unifying_led_due(&s->led, unifying_has_device(&s->pairing),
						 &out->leds);
	}
	/* The ACK payloads of a pairing attempt share the queue. */
	if (s->queue.frame == UNIFYING_QUEUED_NONE && !out->send_led &&
	    unifying_has_device(&s->pairing) && !attempting(&s->pairing)) {
		out->hidpp_len = hidpp_request_frame(&s->hidpp, unifying_place_octet(s->active),
							 now_ms, 2U * active_link(s)->keepalive_ms,
							 out->hidpp);
	}
	take_hidpp_event(s, out);
	out->attempt_expired = unifying_pairing_expired(&s->pairing, now_ms);
	if (out->attempt_expired) {
		_Static_assert(UNIFYING_ANSWERED_1 == 1 && UNIFYING_ANSWERED_2 == 2,
			       "expired_step is the state");
		out->expired_step = step;
	}
	pending = step != UNIFYING_IDLE && !out->attempt_expired;
	out->window_closed = !pending && pairwin_ran_out(&s->pairing.window, (uint32_t)now_ms);
	next = unifying_sweep_channel(&s->sweep, &s->pairing, s->channel, now_ms);
	if (next != s->channel) {
		out->set_channel = true;
		out->channel = next;
		s->channel = next;
	}
	out->pipes = unifying_listen_pipes(&s->pairing);
	out->listen = out->pipes != pipes;
}

void unifying_session_led_queued(struct unifying_session *s, uint8_t leds, uint32_t taken_count)
{
	unifying_led_sent(&s->led, leds);
	s->queue = (struct unifying_queue){.frame = UNIFYING_QUEUED_LED, .taken_count = taken_count};
}

void unifying_session_hidpp_queued(struct unifying_session *s, uint32_t taken_count)
{
	s->queue = (struct unifying_queue){.frame = UNIFYING_QUEUED_HIDPP, .taken_count = taken_count};
}

void unifying_session_replies_dropped(struct unifying_session *s)
{
	if (s->queue.frame == UNIFYING_QUEUED_LED) {
		unifying_led_resend(&s->led);
	}
	s->queue.frame = UNIFYING_QUEUED_NONE;
}

enum unifying_status unifying_session_status(const struct unifying_session *s, uint64_t now_ms)
{
	if (!unifying_has_device(&s->pairing)) {
		return UNIFYING_STATUS_NOT_PAIRED;
	}
	if (s->heard && !quiet_too_long(s, now_ms)) {
		return UNIFYING_STATUS_CONNECTED;
	}
	return UNIFYING_STATUS_SILENT;
}

void unifying_session_radio_resumed(struct unifying_session *s, uint64_t now_ms)
{
	/* The survey released the host's keys, and a release the keyboard sent meanwhile is lost. */
	if (unifying_has_device(&s->pairing)) {
		forget_held(active_link(s));
	}
	if (!s->silent) {
		s->deaf_until_ms = now_ms;
	}
}

int unifying_session_home(const struct unifying_session *s)
{
	return unifying_has_device(&s->pairing) ? s->channel : CHANMOVE_NO_HOME;
}

int unifying_session_movable(const struct unifying_session *s)
{
	return attempting(&s->pairing) ? CHANMOVE_NO_HOME : unifying_session_home(s);
}

size_t unifying_session_channels(const uint8_t **out)
{
	*out = diag_unifying_channels;
	return UNIFYING_PAIRING_CHANNELS;
}

enum chanmove_result unifying_session_move(struct unifying_session *s, uint8_t channel,
					   uint64_t now_ms)
{
	const uint8_t *channels;
	size_t n = unifying_session_channels(&channels);

	if (memchr(channels, channel, n) == NULL) {
		return CHANMOVE_NOT_A_CHANNEL;
	}
	if (unifying_session_movable(s) == CHANMOVE_NO_HOME) {
		return CHANMOVE_NOT_MOVABLE;
	}
	if (channel != s->channel) {
		s->channel = channel;
		s->search_until_ms = now_ms + UNIFYING_MOVE_SEARCH_MS;
	}
	return CHANMOVE_DONE;
}
