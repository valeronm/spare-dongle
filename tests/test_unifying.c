#include <string.h>
#include "test.h"
#include "unifying.h"

#define PIPE_0 unifying_place_pipe(0)

static const struct unifying_receiver receiver = {
	.base = {0xa1, 0xb2, 0xc3, 0xd4},
	.wpid = {0x88, 0x02},
};
static const uint8_t nonce[4] = {0x11, 0x22, 0x33, 0x44};
static const uint8_t other_nonce[4] = {0x99, 0x99, 0x99, 0x99};
static const uint8_t mx_keys[UNIFYING_NAME_MAX] = "MX Keys";

static void seal(uint8_t *frame, size_t len)
{
	uint8_t sum = 0;

	for (size_t i = 0; i + 1 < len; i++) {
		sum += frame[i];
	}
	frame[len - 1] = (uint8_t)(0 - sum);
}

static void request_1(uint8_t *f, uint8_t id, uint8_t caps)
{
	static const uint8_t head[] = {0x00, 0x5f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
				       0x08, 0x40, 0x8b, 0x04, 0x00, 0x01};

	memset(f, 0, UNIFYING_FRAME_MAX);
	memcpy(f, head, sizeof(head));
	f[0] = id;
	f[14] = caps;
	seal(f, UNIFYING_FRAME_MAX);
}

static void request_2(uint8_t *f, uint8_t id)
{
	static const uint8_t head[] = {0x00, 0x5f, 0x02, 0xd1, 0xd2, 0xd3, 0xd4,
				       0x51, 0x52, 0x53, 0x54, 0x1e, 0x40, 0x00, 0x00, 0x01};

	memset(f, 0, UNIFYING_FRAME_MAX);
	memcpy(f, head, sizeof(head));
	f[0] = id;
	seal(f, UNIFYING_FRAME_MAX);
}

static void request_3(uint8_t *f, uint8_t id)
{
	memset(f, 0, UNIFYING_FRAME_MAX);
	f[0] = id;
	f[1] = 0x5f;
	f[2] = 0x03;
	f[3] = 0x01;
	f[4] = 7;
	memcpy(&f[5], mx_keys, 7);
	seal(f, UNIFYING_FRAME_MAX);
}

static size_t feed_ev(struct unifying_pairing *p, const uint8_t *f, size_t len, uint64_t now,
		      uint8_t *reply, enum unifying_rx_event *ev)
{
	return unifying_pairing_rx(p, &receiver, nonce, f, len, now, reply, ev);
}

static size_t feed(struct unifying_pairing *p, const uint8_t *f, size_t len, uint64_t now,
		   uint8_t *reply)
{
	enum unifying_rx_event ev;

	return feed_ev(p, f, len, now, reply, &ev);
}

static struct unifying_pairing open_pairing(void)
{
	struct unifying_pairing p = {0};

	unifying_pairing_open(&p, PAIRWIN_REQUEST, 0);
	return p;
}

static struct unifying_pairing open_pairing_with_device(void)
{
	static const struct unifying_device d = {0};
	struct unifying_pairing p = {0};

	unifying_pairing_restore(&p, 0, &d);
	unifying_pairing_open(&p, PAIRWIN_REQUEST, 0);
	return p;
}

static void pair_through_request_2(struct unifying_pairing *p)
{
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];

	request_1(f, 0xe1, 0x05);
	feed(p, f, sizeof(f), 0, reply);
	request_2(f, 0xe1);
	feed(p, f, sizeof(f), 10, reply);
}

static void pair_fully(struct unifying_pairing *p)
{
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];

	pair_through_request_2(p);
	request_3(f, 0x00);
	feed(p, f, sizeof(f), 20, reply);
}

static void restore_paired(struct unifying_pairing *p, struct unifying_device *d)
{
	struct unifying_pairing done = open_pairing();

	pair_fully(&done);
	*p = (struct unifying_pairing){0};
	*d = done.devices[0];
	unifying_pairing_restore(p, 0, d);
	unifying_pairing_open(p, PAIRWIN_REQUEST, 0);
}

static void start_second_attempt(struct unifying_pairing *p)
{
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];
	enum unifying_rx_event ev;

	request_1(f, 0xe7, 0x05);
	unifying_pairing_rx(p, &receiver, other_nonce, f, sizeof(f), 1000, reply, &ev);
	request_2(f, 0xe7);
	feed(p, f, sizeof(f), 1010, reply);
}

static void test_checksum_is_the_negated_sum(void)
{
	static const uint8_t bytes[] = {0x01, 0x02, 0x03};

	CHECK_EQ(unifying_checksum(bytes, sizeof(bytes)), 0xfa);
}

static void test_checksum_check_tolerates_a_wrong_low_bit(void)
{
	uint8_t f[UNIFYING_FRAME_MAX];

	request_1(f, 0xe1, 0x05);
	CHECK(unifying_checksum_ok(f, sizeof(f)));
	f[21] ^= 0x01;
	CHECK(unifying_checksum_ok(f, sizeof(f)));
	f[21] ^= 0x02;
	CHECK(!unifying_checksum_ok(f, sizeof(f)));
}

static void test_key_permutes_and_masks_the_key_data(void)
{
	static const uint8_t base[4] = {0, 1, 2, 3};
	static const uint8_t device_wpid[2] = {4, 5};
	static const uint8_t dongle_wpid[2] = {6, 7};
	static const uint8_t device_nonce[4] = {8, 9, 10, 11};
	static const uint8_t dongle_nonce[4] = {12, 13, 14, 15};
	static const uint8_t expected[16] = {0x07, 0xfe, 0x00, 0x03, 0x0a, 0xfd, 0x5c, 0x0e,
					     0x08, 0x06, 0xf3, 0x05, 0x0d, 0x5a, 0x04, 0x0b};
	uint8_t key[16];

	unifying_derive_key(key, base, device_wpid, dongle_wpid, device_nonce, dongle_nonce);
	CHECK_MEM(key, expected, 16);
}

static void test_request_1_is_answered_with_the_assigned_address(void)
{
	static const uint8_t expected[UNIFYING_FRAME_MAX] = {
		0xe1, 0x1f, 0x01, 0xa1, 0xb2, 0xc3, 0xd4, 0x07, 0x08, 0x88, 0x02,
		0x00, 0x00, 0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	struct unifying_pairing p = open_pairing();
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX], want[UNIFYING_FRAME_MAX];
	size_t n;

	memcpy(want, expected, sizeof(want));
	seal(want, sizeof(want));
	request_1(f, 0xe1, 0x05);
	n = feed(&p, f, sizeof(f), 0, reply);
	CHECK_EQ(n, UNIFYING_FRAME_MAX);
	CHECK_EQ(p.attempt.state, UNIFYING_ANSWERED_1);
	CHECK_MEM(reply, want, UNIFYING_FRAME_MAX);
	CHECK_EQ(p.attempt.caps, 0x05);
	CHECK_EQ(p.attempt.dev.wpid[0], 0x40);
	CHECK_EQ(p.attempt.dev.wpid[1], 0x8b);
}

static void test_request_1_without_the_unifying_bit_is_refused(void)
{
	struct unifying_pairing p = open_pairing();
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];
	enum unifying_rx_event ev;

	request_1(f, 0xe1, 0x01);
	CHECK_EQ(feed_ev(&p, f, sizeof(f), 0, reply, &ev), 0);
	CHECK_EQ(ev, UNIFYING_RX_REFUSED);
	CHECK_EQ(p.attempt.state, UNIFYING_IDLE);
}

static void test_request_1_from_a_mouse_is_refused(void)
{
	struct unifying_pairing p = open_pairing();
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];
	enum unifying_rx_event ev;

	request_1(f, 0xe1, 0x05);
	f[13] = 0x02;
	seal(f, UNIFYING_FRAME_MAX);
	CHECK_EQ(feed_ev(&p, f, sizeof(f), 0, reply, &ev), 0);
	CHECK_EQ(ev, UNIFYING_RX_REFUSED);
	CHECK_EQ(p.attempt.state, UNIFYING_IDLE);
}

static void test_request_2_is_answered_with_the_nonce_and_derives_the_key(void)
{
	static const uint8_t expected[UNIFYING_FRAME_MAX] = {
		0xe1, 0x1f, 0x02, 0x11, 0x22, 0x33, 0x44, 0x51, 0x52, 0x53, 0x54,
		0x1e, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	static const uint8_t device_wpid[2] = {0x40, 0x8b};
	static const uint8_t device_nonce[4] = {0xd1, 0xd2, 0xd3, 0xd4};
	struct unifying_pairing p = open_pairing();
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX], want[UNIFYING_FRAME_MAX];
	uint8_t key[16];
	enum unifying_rx_event ev;

	memcpy(want, expected, sizeof(want));
	seal(want, sizeof(want));
	request_1(f, 0xe1, 0x05);
	feed_ev(&p, f, sizeof(f), 0, reply, &ev);
	request_2(f, 0xe1);
	CHECK_EQ(feed_ev(&p, f, sizeof(f), 10, reply, &ev), UNIFYING_FRAME_MAX);
	CHECK_EQ(ev, UNIFYING_RX_ANSWERED_2);
	CHECK_EQ(p.attempt.state, UNIFYING_ANSWERED_2);
	CHECK_MEM(reply, want, UNIFYING_FRAME_MAX);
	unifying_derive_key(key, receiver.base, device_wpid, receiver.wpid, device_nonce, nonce);
	CHECK_MEM(p.attempt.dev.key, key, 16);
}

static void test_request_3_gets_the_final_response_and_completes(void)
{
	static const uint8_t expected[10] = {0xe1, 0x0f, 0x06, 0xd1, 0xd2, 0xd3,
					     0xd4, 0x11, 0x22, 0x00};
	struct unifying_pairing p = open_pairing();
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX], want[10];
	enum unifying_rx_event ev;

	memcpy(want, expected, sizeof(want));
	seal(want, sizeof(want));
	pair_through_request_2(&p);
	request_3(f, 0xe1);
	CHECK_EQ(feed_ev(&p, f, sizeof(f), 20, reply, &ev), 10);
	CHECK_EQ(ev, UNIFYING_RX_PAIRED);
	CHECK_MEM(reply, want, 10);
	CHECK(unifying_has_device(&p));
	CHECK_MEM(p.devices[0].key, p.attempt.dev.key, UNIFYING_KEY_LEN);
	CHECK_EQ(p.attempt.state, UNIFYING_IDLE);
}

static void test_a_paired_device_holds_what_the_keyboard_reported_about_itself(void)
{
	static const uint8_t serial[UNIFYING_SERIAL_LEN] = {0x51, 0x52, 0x53, 0x54};
	struct unifying_pairing p = open_pairing();

	pair_fully(&p);
	CHECK_MEM(p.devices[0].serial, serial, sizeof(serial));
	CHECK_EQ(p.devices[0].type, 0x01);
	CHECK_MEM(p.devices[0].name, mx_keys, sizeof(mx_keys));
}

static void test_a_name_longer_than_the_frame_holds_is_cut(void)
{
	struct unifying_pairing p = open_pairing();
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];

	pair_through_request_2(&p);
	request_3(f, 0x00);
	f[4] = 200;
	memset(&f[5], 'A', UNIFYING_NAME_MAX);
	seal(f, UNIFYING_FRAME_MAX);
	feed(&p, f, sizeof(f), 20, reply);
	CHECK_MEM(p.devices[0].name, "AAAAAAAAAAAAAAAA", UNIFYING_NAME_MAX);
}

static void test_request_3_is_accepted_though_its_id_byte_is_zero(void)
{
	struct unifying_pairing p = open_pairing();
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];

	pair_through_request_2(&p);
	request_3(f, 0x00);
	CHECK_EQ(feed(&p, f, sizeof(f), 20, reply), 10);
	CHECK(unifying_has_device(&p));
}

static void test_a_repeated_request_gets_no_second_reply(void)
{
	struct unifying_pairing p = open_pairing();
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];
	enum unifying_rx_event ev;

	request_1(f, 0xe1, 0x05);
	feed_ev(&p, f, sizeof(f), 0, reply, &ev);
	CHECK_EQ(ev, UNIFYING_RX_ANSWERED_1);
	CHECK_EQ(feed_ev(&p, f, sizeof(f), 5, reply, &ev), 0);
	CHECK_EQ(p.attempt.state, UNIFYING_ANSWERED_1);
}

static void test_a_request_out_of_order_or_with_another_id_is_ignored(void)
{
	struct unifying_pairing p = open_pairing();
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];

	request_2(f, 0xe1);
	CHECK_EQ(feed(&p, f, sizeof(f), 0, reply), 0);
	request_1(f, 0xe1, 0x05);
	feed(&p, f, sizeof(f), 0, reply);
	request_2(f, 0xe2);
	CHECK_EQ(feed(&p, f, sizeof(f), 5, reply), 0);
	request_3(f, 0xe1);
	CHECK_EQ(feed(&p, f, sizeof(f), 5, reply), 0);
	CHECK_EQ(p.attempt.state, UNIFYING_ANSWERED_1);
}

static void test_a_frame_with_a_bad_checksum_is_ignored(void)
{
	struct unifying_pairing p = open_pairing();
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];

	request_1(f, 0xe1, 0x05);
	f[21] ^= 0x10;
	CHECK_EQ(feed(&p, f, sizeof(f), 0, reply), 0);
	CHECK_EQ(p.attempt.state, UNIFYING_IDLE);
}

static void test_request_1_with_a_new_id_restarts_with_a_fresh_nonce(void)
{
	struct unifying_pairing p = open_pairing();
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];
	enum unifying_rx_event ev;

	pair_through_request_2(&p);
	request_1(f, 0xe7, 0x05);
	CHECK_EQ(unifying_pairing_rx(&p, &receiver, other_nonce, f, sizeof(f), 30, reply, &ev),
		 UNIFYING_FRAME_MAX);
	CHECK_EQ(reply[0], 0xe7);
	CHECK_EQ(p.attempt.state, UNIFYING_ANSWERED_1);
	request_2(f, 0xe7);
	unifying_pairing_rx(&p, &receiver, nonce, f, sizeof(f), 40, reply, &ev);
	CHECK_MEM(&reply[3], other_nonce, 4);
}

static void test_an_attempt_silent_for_the_timeout_returns_to_idle(void)
{
	struct unifying_pairing p = open_pairing();

	pair_through_request_2(&p);
	CHECK(!unifying_pairing_expired(&p, 10 + UNIFYING_PAIRING_TIMEOUT_MS));
	CHECK_EQ(p.attempt.state, UNIFYING_ANSWERED_2);
	CHECK(unifying_pairing_expired(&p, 11 + UNIFYING_PAIRING_TIMEOUT_MS));
	CHECK_EQ(p.attempt.state, UNIFYING_IDLE);
	CHECK(!unifying_pairing_expired(&p, 20000));
}

static void test_a_completed_pairing_does_not_time_out(void)
{
	struct unifying_pairing p = open_pairing();

	pair_fully(&p);
	CHECK(!unifying_pairing_expired(&p, 60000));
	CHECK(unifying_has_device(&p));
}

static void test_encrypted_keyboard_frame_is_recognized(void)
{
	uint8_t f[UNIFYING_FRAME_MAX] = {0x07, 0xd3};

	seal(f, sizeof(f));
	CHECK(unifying_is_encrypted_keyboard(f, sizeof(f)));
	f[1] = 0x93;
	seal(f, sizeof(f));
	CHECK(unifying_is_encrypted_keyboard(f, sizeof(f)));
	f[1] = 0xc1;
	seal(f, sizeof(f));
	CHECK(!unifying_is_encrypted_keyboard(f, sizeof(f)));
	f[1] = 0xd3;
	seal(f, sizeof(f));
	CHECK(!unifying_is_encrypted_keyboard(f, 10));
}

static void test_pairing_address_and_channels_are_the_fixed_ones(void)
{
	static const uint8_t address[5] = {0xbb, 0x0a, 0xdc, 0xa5, 0x75};
	static const uint8_t channels[11] = {5, 8, 17, 32, 35, 41, 44, 62, 65, 71, 74};

	CHECK_MEM(unifying_pairing_address, address, 5);
	CHECK_MEM(diag_unifying_channels, channels, 11);
}

static void test_device_address_is_the_base_then_the_device_octet(void)
{
	static const uint8_t expected[5] = {0xa1, 0xb2, 0xc3, 0xd4, 0x07};
	uint8_t address[5];

	unifying_device_address(&receiver, 0, address);
	CHECK_MEM(address, expected, 5);
}

static void test_idle_sweep_steps_through_the_pairing_channels(void)
{
	struct unifying_sweep s = {0};
	struct unifying_pairing p = open_pairing();
	uint64_t now = 1000;
	uint8_t at = unifying_sweep_channel(&s, &p, diag_unifying_channels[0], now);

	CHECK_EQ(at, diag_unifying_channels[1]);
	CHECK_EQ(unifying_sweep_channel(&s, &p, at, now + UNIFYING_SWEEP_DWELL_MS - 1), at);
	for (int i = 2; i <= UNIFYING_PAIRING_CHANNELS; i++) {
		now += UNIFYING_SWEEP_DWELL_MS;
		at = unifying_sweep_channel(&s, &p, at, now);
		CHECK_EQ(at, diag_unifying_channels[i % UNIFYING_PAIRING_CHANNELS]);
	}
}

static void test_sweep_holds_the_channel_from_the_first_request(void)
{
	struct unifying_sweep s = {0};
	struct unifying_pairing p = open_pairing();
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];
	uint8_t held;

	held = unifying_sweep_channel(&s, &p, diag_unifying_channels[0], 1000);
	CHECK_EQ(held, diag_unifying_channels[1]);
	request_1(f, 0xe1, 0x05);
	feed(&p, f, sizeof(f), 1005, reply);
	CHECK_EQ(unifying_sweep_channel(&s, &p, held, 1100), held);
	request_2(f, 0xe1);
	feed(&p, f, sizeof(f), 1110, reply);
	request_3(f, 0x00);
	feed(&p, f, sizeof(f), 1120, reply);
	CHECK_EQ(unifying_sweep_channel(&s, &p, held, 90000), held);
}

static void test_sweep_resumes_once_an_attempt_expires(void)
{
	struct unifying_sweep s = {0};
	struct unifying_pairing p = open_pairing();
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];
	uint8_t held;

	held = unifying_sweep_channel(&s, &p, diag_unifying_channels[0], 1000);
	request_1(f, 0xe1, 0x05);
	feed(&p, f, sizeof(f), 1005, reply);
	CHECK(unifying_pairing_expired(&p, 1006 + UNIFYING_PAIRING_TIMEOUT_MS));
	CHECK(unifying_sweep_channel(&s, &p, held, 1006 + UNIFYING_PAIRING_TIMEOUT_MS) != held);
}

static void test_request_1_after_the_window_is_ignored(void)
{
	struct unifying_pairing p = open_pairing_with_device();
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];
	enum unifying_rx_event ev;

	request_1(f, 0xe1, 0x05);
	CHECK_EQ(feed_ev(&p, f, sizeof(f), PAIRWIN_MS, reply, &ev), 0);
	CHECK_EQ(ev, UNIFYING_RX_NONE);
	CHECK_EQ(p.attempt.state, UNIFYING_IDLE);
}

static void test_an_attempt_started_in_the_window_finishes_after_it(void)
{
	struct unifying_pairing p = open_pairing();
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];
	uint64_t t = PAIRWIN_MS - 10;

	request_1(f, 0xe1, 0x05);
	CHECK_EQ(feed(&p, f, sizeof(f), t, reply), UNIFYING_FRAME_MAX);
	request_2(f, 0xe1);
	CHECK_EQ(feed(&p, f, sizeof(f), t + 20, reply), UNIFYING_FRAME_MAX);
	request_3(f, 0x00);
	CHECK_EQ(feed(&p, f, sizeof(f), t + 30, reply), 10);
	CHECK(unifying_has_device(&p));
}

static void test_request_1_before_pairing_opens_is_ignored(void)
{
	struct unifying_pairing p = {0};
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];
	enum unifying_rx_event ev;

	request_1(f, 0xe1, 0x05);
	CHECK_EQ(feed_ev(&p, f, sizeof(f), 100, reply, &ev), 0);
	CHECK_EQ(ev, UNIFYING_RX_NONE);
}

static void test_the_window_runs_30_s_from_opening(void)
{
	struct unifying_pairing p = open_pairing_with_device();

	unifying_pairing_open(&p, PAIRWIN_REQUEST, 5000);
	CHECK(unifying_pairing_window_open(&p, 5000));
	CHECK(unifying_pairing_window_open(&p, 5000 + 29999));
	CHECK(!unifying_pairing_window_open(&p, 5000 + 30000));
}

static void test_pairing_closes_the_window(void)
{
	struct unifying_pairing p = open_pairing();

	pair_fully(&p);
	CHECK(unifying_has_device(&p));
	CHECK(!unifying_pairing_window_open(&p, 30));
}

static void test_a_paired_dongle_holds_its_channel_with_pairing_closed_or_open(void)
{
	static const struct unifying_device d = {0};
	struct unifying_sweep s = {0};
	struct unifying_pairing p = {0};

	unifying_pairing_restore(&p, 0, &d);
	CHECK_EQ(unifying_sweep_channel(&s, &p, 41, 1000), 41);
	unifying_pairing_open(&p, PAIRWIN_REQUEST, 1000);
	CHECK_EQ(unifying_sweep_channel(&s, &p, 41, 1000 + 5 * UNIFYING_SWEEP_DWELL_MS), 41);
}

static void test_a_restored_device_is_paired_and_a_new_request_still_starts_an_attempt(void)
{
	struct unifying_pairing q;
	struct unifying_device d;
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];

	restore_paired(&q, &d);
	CHECK(unifying_has_device(&q));
	CHECK_MEM(&q.devices[0], &d, sizeof(d));
	request_1(f, 0xe7, 0x05);
	CHECK_EQ(feed(&q, f, sizeof(f), 1000, reply), UNIFYING_FRAME_MAX);
	CHECK_EQ(q.attempt.state, UNIFYING_ANSWERED_1);
}

static void test_a_refused_request_keeps_the_restored_device_paired(void)
{
	struct unifying_pairing p;
	struct unifying_device d;
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];
	enum unifying_rx_event ev;

	restore_paired(&p, &d);
	request_1(f, 0xe7, 0x01);
	CHECK_EQ(feed_ev(&p, f, sizeof(f), 1000, reply, &ev), 0);
	CHECK_EQ(ev, UNIFYING_RX_REFUSED);
	CHECK(unifying_has_device(&p));
	CHECK_MEM(&p.devices[0], &d, sizeof(d));
}

static void test_an_expired_attempt_returns_to_the_restored_device(void)
{
	struct unifying_pairing p;
	struct unifying_device d;

	restore_paired(&p, &d);
	start_second_attempt(&p);
	CHECK(unifying_pairing_expired(&p, 1011 + UNIFYING_PAIRING_TIMEOUT_MS));
	CHECK(unifying_has_device(&p));
	CHECK_MEM(&p.devices[0], &d, sizeof(d));
}

static void test_an_expired_attempt_can_be_retried_with_its_id(void)
{
	struct unifying_pairing p;
	struct unifying_device d;
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];

	restore_paired(&p, &d);
	request_1(f, 0xe7, 0x05);
	feed(&p, f, sizeof(f), 1000, reply);
	CHECK(unifying_pairing_expired(&p, 1001 + UNIFYING_PAIRING_TIMEOUT_MS));
	CHECK_EQ(feed(&p, f, sizeof(f), 7000, reply), UNIFYING_FRAME_MAX);
	CHECK_EQ(p.attempt.state, UNIFYING_ANSWERED_1);
}

static void test_the_paired_device_is_kept_while_an_attempt_runs(void)
{
	struct unifying_pairing p;
	struct unifying_device d;

	restore_paired(&p, &d);
	start_second_attempt(&p);
	CHECK_EQ(p.attempt.state, UNIFYING_ANSWERED_2);
	CHECK(unifying_has_device(&p));
	CHECK_MEM(&p.devices[0], &d, sizeof(d));
}

static void test_a_refused_request_during_an_attempt_keeps_the_paired_device(void)
{
	struct unifying_pairing p;
	struct unifying_device d;
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];
	enum unifying_rx_event ev;

	restore_paired(&p, &d);
	start_second_attempt(&p);
	request_1(f, 0xe8, 0x01);
	CHECK_EQ(feed_ev(&p, f, sizeof(f), 1020, reply, &ev), 0);
	CHECK_EQ(ev, UNIFYING_RX_REFUSED);
	CHECK(unifying_has_device(&p));
	CHECK_MEM(&p.devices[0], &d, sizeof(d));
	request_3(f, 0x00);
	CHECK_EQ(feed_ev(&p, f, sizeof(f), 1030, reply, &ev), 0);
	CHECK_MEM(&p.devices[0], &d, sizeof(d));
}

static void test_request_1_with_the_id_of_a_completed_attempt_starts_a_new_one(void)
{
	struct unifying_pairing p = open_pairing();
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];
	enum unifying_rx_event ev;

	pair_fully(&p);
	unifying_pairing_open(&p, PAIRWIN_REQUEST, 1000);
	request_1(f, 0xe1, 0x05);
	CHECK_EQ(feed_ev(&p, f, sizeof(f), 1000, reply, &ev), UNIFYING_FRAME_MAX);
	CHECK_EQ(ev, UNIFYING_RX_ANSWERED_1);
}

static void test_a_completed_attempt_takes_the_next_place_and_keeps_the_first(void)
{
	struct unifying_pairing p;
	struct unifying_device old;
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];
	enum unifying_rx_event ev;

	restore_paired(&p, &old);
	start_second_attempt(&p);
	CHECK_EQ(p.attempt.place, 1);
	request_3(f, 0x00);
	feed_ev(&p, f, sizeof(f), 1020, reply, &ev);
	CHECK_EQ(ev, UNIFYING_RX_PAIRED);
	CHECK_EQ(p.attempt.place, 1);
	CHECK(p.stored[1]);
	CHECK_MEM(&p.devices[0], &old, sizeof(old));
	CHECK(memcmp(p.devices[1].key, old.key, UNIFYING_KEY_LEN) != 0);
	CHECK_EQ(feed_ev(&p, f, sizeof(f), 1030, reply, &ev), 0);
	CHECK_EQ(ev, UNIFYING_RX_NONE);
}

static void test_a_place_has_a_fixed_pipe_and_octet(void)
{
	for (uint8_t place = 0; place < UNIFYING_PLACES; place++) {
		CHECK_EQ(unifying_pipe_place(unifying_place_pipe(place)), place);
		CHECK_EQ(unifying_octet_place(unifying_place_octet(place)), place);
		CHECK(unifying_place_octet(place) != 0x00);
	}
	CHECK_EQ(unifying_place_pipe(0), 1);
	CHECK_EQ(unifying_place_octet(0), 0x07);
	CHECK_EQ(unifying_pipe_place(UNIFYING_PIPE_PAIRING), UNIFYING_NO_PLACE);
	CHECK_EQ(unifying_pipe_place(UNIFYING_PIPE_RECEIVER), UNIFYING_NO_PLACE);
	CHECK_EQ(unifying_octet_place(0x00), UNIFYING_NO_PLACE);
	CHECK_EQ(unifying_octet_place(0x06), UNIFYING_NO_PLACE);
	CHECK_EQ(unifying_octet_place(0x0d), UNIFYING_NO_PLACE);
}

static void test_request_1_is_answered_with_the_lowest_empty_places_address(void)
{
	struct unifying_pairing p;
	struct unifying_device d;
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];

	restore_paired(&p, &d);
	request_1(f, 0xe7, 0x05);
	CHECK_EQ(feed(&p, f, sizeof(f), 1000, reply), UNIFYING_FRAME_MAX);
	CHECK_MEM(&reply[3], receiver.base, sizeof(receiver.base));
	CHECK_EQ(reply[7], unifying_place_octet(1));
}

static void store_every_place(struct unifying_pairing *p)
{
	static const struct unifying_device d = {0};

	for (uint8_t place = 0; place < UNIFYING_PLACES; place++) {
		unifying_pairing_restore(p, place, &d);
	}
}

static void test_pairing_does_not_open_with_every_place_stored(void)
{
	struct unifying_pairing p = {0};
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];

	store_every_place(&p);
	CHECK(!unifying_pairing_open(&p, PAIRWIN_REQUEST, 0));
	CHECK(!unifying_pairing_window_open(&p, 10));
	request_1(f, 0xe1, 0x05);
	CHECK_EQ(feed(&p, f, sizeof(f), 10, reply), 0);
	CHECK_EQ(p.attempt.state, UNIFYING_IDLE);
}

static void test_a_forgotten_place_is_the_next_one_paired(void)
{
	struct unifying_pairing p = {0};
	uint8_t f[UNIFYING_FRAME_MAX], reply[UNIFYING_FRAME_MAX];

	store_every_place(&p);
	unifying_pairing_forget(&p, 3);
	CHECK(!p.stored[3]);
	CHECK(unifying_has_device(&p));
	CHECK(unifying_pairing_open(&p, PAIRWIN_REQUEST, 0));
	request_1(f, 0xe1, 0x05);
	CHECK_EQ(feed(&p, f, sizeof(f), 10, reply), UNIFYING_FRAME_MAX);
	CHECK_EQ(p.attempt.place, 3);
}

static void test_forgetting_the_only_place_leaves_no_device(void)
{
	struct unifying_pairing p;
	struct unifying_device d;
	static const uint8_t zero[UNIFYING_KEY_LEN];

	restore_paired(&p, &d);
	unifying_pairing_forget(&p, 0);
	CHECK(!unifying_has_device(&p));
	CHECK_MEM(p.devices[0].key, zero, sizeof(zero));
}

static void test_the_radio_listens_on_stored_places_and_the_attempts(void)
{
	const uint8_t always = 1u << UNIFYING_PIPE_PAIRING | 1u << UNIFYING_PIPE_RECEIVER;
	struct unifying_pairing p = open_pairing();
	struct unifying_device d;

	CHECK_EQ(unifying_listen_pipes(&p), always);
	restore_paired(&p, &d);
	CHECK_EQ(unifying_listen_pipes(&p), always | 1u << unifying_place_pipe(0));
	start_second_attempt(&p);
	CHECK_EQ(unifying_listen_pipes(&p),
		 always | 1u << unifying_place_pipe(0) | 1u << unifying_place_pipe(1));
	CHECK(unifying_pairing_expired(&p, 1011 + UNIFYING_PAIRING_TIMEOUT_MS));
	CHECK_EQ(unifying_listen_pipes(&p), always | 1u << unifying_place_pipe(0));
}

static void test_a_record_is_kept_per_place(void)
{
	struct unifying_pairing done = open_pairing();
	struct unifying_pairing p = {0};
	uint8_t record[UNIFYING_RECORD_LEN], back[UNIFYING_RECORD_LEN];

	pair_fully(&done);
	unifying_record_of(&done, 0, record);
	unifying_record_restore(&p, 4, record);
	CHECK(p.stored[4]);
	CHECK(!p.stored[0]);
	unifying_record_of(&p, 4, back);
	CHECK_MEM(back, record, sizeof(record));
}

static void session_paired(struct unifying_session *s);

static void test_a_window_that_ran_out_stays_closed_past_32_bits_of_uptime(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_paired(&s);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 0);
	unifying_session_tick(&s, PAIRWIN_MS, false, 0, 0, &out);
	request_1(f, 0xe1, 0x05);
	unifying_session_frame(&s, UNIFYING_PIPE_PAIRING, f, sizeof(f), 0, (1ULL << 32) + 10, nonce,
			       NULL, &out);
	CHECK_EQ(out.reply_len, 0);
	CHECK_EQ(s.pairing.attempt.state, UNIFYING_IDLE);
}

static const uint8_t aes_key[UNIFYING_KEY_LEN] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
						  0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
static const uint8_t stream[UNIFYING_AES_BLOCK_LEN] = {
	0x3a, 0x91, 0x07, 0xc4, 0x55, 0xe2, 0x18, 0x6b,
	0xf0, 0x2d, 0x8e, 0x73, 0x49, 0xb6, 0x0c, 0xa5};
static const uint8_t press_a[REPORT_KEYBOARD_LEN] = {0x00, 0x04, 0x00, 0x00,
						     0x00, 0x00, 0x00, 0xc9};
static const uint8_t media_volume_up[] = {0x00, 0xc3, 0xe9, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x54};
static const uint8_t media_volume_down[] = {0x00, 0xc3, 0xea, 0x00, 0x00,
					    0x00, 0x00, 0x00, 0x00, 0x53};
static const uint8_t media_mute[] = {0x00, 0xc3, 0xcd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70};
static const uint8_t media_release[] = {0x00, 0xc3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3d};
static const uint8_t keep_alive_1100[] = {0x00, 0x40, 0x04, 0x4c, 0x70};
static const uint8_t set_keep_alive_88[] = {0x00, 0x4f, 0x00, 0x00, 0x58,
					    0x32, 0x14, 0x00, 0x00, 0x13};
static const uint8_t pairing_complete[] = {0x00, 0x4f, 0x06, 0x01, 0x00,
					   0x00, 0x00, 0x00, 0x00, 0xaa};

static uint8_t aes_seen_key[UNIFYING_KEY_LEN];
static uint8_t aes_seen_input[UNIFYING_AES_BLOCK_LEN];
static int aes_calls;

static void fake_aes(const uint8_t key[UNIFYING_KEY_LEN], const uint8_t in[UNIFYING_AES_BLOCK_LEN],
		     uint8_t out[UNIFYING_AES_BLOCK_LEN])
{
	memcpy(aes_seen_key, key, UNIFYING_KEY_LEN);
	memcpy(aes_seen_input, in, UNIFYING_AES_BLOCK_LEN);
	memcpy(out, stream, UNIFYING_AES_BLOCK_LEN);
	aes_calls++;
}

static void keystroke(uint8_t *f, uint32_t counter, const uint8_t plain[REPORT_KEYBOARD_LEN])
{
	memset(f, 0, UNIFYING_FRAME_MAX);
	f[1] = 0xd3;
	for (int i = 0; i < REPORT_KEYBOARD_LEN; i++) {
		f[2 + i] = plain[i] ^ stream[i];
	}
	f[10] = counter >> 24;
	f[11] = counter >> 16;
	f[12] = counter >> 8;
	f[13] = counter;
	seal(f, UNIFYING_FRAME_MAX);
}

static size_t receive_keystrokes_at(struct unifying_link *l, uint32_t counter,
				    const uint8_t plain[REPORT_KEYBOARD_LEN], uint64_t now,
				    struct report out[UNIFYING_KEYSTROKE_REPORTS])
{
	uint8_t f[UNIFYING_FRAME_MAX];

	keystroke(f, counter, plain);
	return unifying_keystroke(l, aes_key, f, sizeof(f), now, fake_aes, out);
}

static bool receive_keystroke_at(struct unifying_link *l, uint32_t counter,
				 const uint8_t plain[REPORT_KEYBOARD_LEN], uint64_t now,
				 struct report *r)
{
	struct report out[UNIFYING_KEYSTROKE_REPORTS];
	size_t n = receive_keystrokes_at(l, counter, plain, now, out);

	CHECK(n <= 1);
	if (n != 0) {
		*r = out[0];
	}
	return n != 0;
}

static bool receive_keystroke(struct unifying_link *l, uint32_t counter,
			      const uint8_t plain[REPORT_KEYBOARD_LEN], struct report *r)
{
	return receive_keystroke_at(l, counter, plain, 0, r);
}

static const uint8_t release[REPORT_KEYBOARD_LEN] = {0x00, 0x00, 0x00, 0x00,
						     0x00, 0x00, 0x00, 0xc9};

static void sync_at(struct unifying_link *l, uint32_t next_counter)
{
	struct report r;

	CHECK(receive_keystroke(l, next_counter - 1, release, &r));
	CHECK_EQ(l->next_counter, next_counter);
}

static void test_keystroke_passes_the_key_and_the_counter_block_to_aes(void)
{
	static const uint8_t expected[UNIFYING_AES_BLOCK_LEN] = {
		0x04, 0x14, 0x1d, 0x1f, 0x27, 0x28, 0x0d, 0x5f,
		0x66, 0xf4, 0x59, 0x0a, 0x0d, 0x13, 0x26, 0x0e};
	struct unifying_link l;
	struct report r;

	unifying_link_reset(&l);
	sync_at(&l, 0x5f66f459);
	aes_calls = 0;
	CHECK(receive_keystroke(&l, 0x5f66f459, press_a, &r));
	CHECK_EQ(aes_calls, 1);
	CHECK_MEM(aes_seen_input, expected, UNIFYING_AES_BLOCK_LEN);
	CHECK_MEM(aes_seen_key, aes_key, UNIFYING_KEY_LEN);
}

static void test_keystroke_report_is_the_decrypted_modifier_and_keys(void)
{
	static const struct {
		uint8_t plain[REPORT_KEYBOARD_LEN];
		uint8_t want[REPORT_KEYBOARD_LEN];
	} cases[] = {
		{{0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc9},
		 {0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}},
		{{0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc9},
		 {0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}},
		{{0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc9},
		 {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
		{{0x00, 0x4f, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc9},
		 {0x00, 0x00, 0x4f, 0x00, 0x00, 0x00, 0x00, 0x00}},
	};
	struct unifying_link l;
	struct report r, want;

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		unifying_link_reset(&l);
		sync_at(&l, 1);
		CHECK(receive_keystroke(&l, 1, cases[i].plain, &r));
		CHECK_EQ(r.kind, REPORT_KEYBOARD);
		want = reports_held_keys(cases[i].want[0], &cases[i].want[2]);
		CHECK_MEM(r.keys, want.keys, REPORT_KEYS_LEN);
		CHECK(l.keyboard_down);
	}
}

static void test_a_counter_below_the_next_expected_one_is_refused(void)
{
	struct unifying_link l;
	struct report r;

	unifying_link_reset(&l);
	sync_at(&l, 1000);
	CHECK(receive_keystroke(&l, 1000, press_a, &r));
	aes_calls = 0;
	CHECK(!receive_keystroke(&l, 1000, press_a, &r));
	CHECK(!receive_keystroke(&l, 999, press_a, &r));
	CHECK_EQ(aes_calls, 0);
	CHECK(receive_keystroke(&l, 1001, press_a, &r));
	CHECK(!receive_keystroke(&l, 1001, press_a, &r));
	unifying_link_reset(&l);
	aes_calls = 0;
	CHECK(!receive_keystroke(&l, 1000, press_a, &r));
	CHECK_EQ(aes_calls, 1);
}

static const uint8_t forged[REPORT_KEYBOARD_LEN] = {0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static void accept_counter_1000(struct unifying_link *l)
{
	struct report r;

	unifying_link_reset(l);
	sync_at(l, 1000);
	CHECK(receive_keystroke(l, 1000, press_a, &r));
}

static void test_counters_inside_the_window_are_accepted_up_to_its_edge(void)
{
	struct unifying_link l;
	struct report r;

	accept_counter_1000(&l);
	CHECK(receive_keystroke(&l, 1001 + UNIFYING_COUNTER_WINDOW - 1, press_a, &r));
	CHECK_EQ(l.next_counter, 1001 + UNIFYING_COUNTER_WINDOW);
	CHECK(!receive_keystroke(&l, 1001 + 2 * UNIFYING_COUNTER_WINDOW, press_a, &r));
	CHECK_EQ(l.next_counter, 1001 + UNIFYING_COUNTER_WINDOW);
	CHECK(receive_keystroke(&l, 1001 + 2 * UNIFYING_COUNTER_WINDOW + 1, press_a, &r));
}

static void test_a_far_jump_alone_is_not_typed(void)
{
	struct unifying_link l;
	struct report r = {.kind = REPORT_SYSTEM};

	accept_counter_1000(&l);
	CHECK(!receive_keystroke(&l, 6000, press_a, &r));
	CHECK_EQ(r.kind, REPORT_SYSTEM);
	CHECK_EQ(l.next_counter, 1001);
}

static void test_a_far_jump_confirmed_by_the_next_counter_is_typed(void)
{
	struct unifying_link l;
	struct report r;

	accept_counter_1000(&l);
	CHECK(!receive_keystroke(&l, 6000, press_a, &r));
	CHECK(receive_keystroke(&l, 6001, press_a, &r));
	CHECK_EQ(r.kind, REPORT_KEYBOARD);
	CHECK_EQ(l.next_counter, 6002);
	CHECK(!receive_keystroke(&l, 6000, press_a, &r));
}

static void test_a_far_jump_followed_by_another_counter_is_dropped(void)
{
	struct unifying_link l;
	struct report r;

	accept_counter_1000(&l);
	CHECK(!receive_keystroke(&l, 6000, press_a, &r));
	CHECK(receive_keystroke(&l, 1005, press_a, &r));
	CHECK(!receive_keystroke(&l, 6001, press_a, &r));
	CHECK_EQ(l.next_counter, 1006);
	CHECK(!receive_keystroke(&l, 7000, press_a, &r));
	CHECK(!receive_keystroke(&l, 8000, press_a, &r));
	CHECK(!receive_keystroke(&l, 7001, press_a, &r));
	CHECK_EQ(l.next_counter, 1006);
}

static void test_a_refused_counter_keeps_the_jump_candidate(void)
{
	struct unifying_link l;
	struct report r;

	accept_counter_1000(&l);
	CHECK(!receive_keystroke(&l, 6000, press_a, &r));
	CHECK(!receive_keystroke(&l, 900, press_a, &r));
	CHECK(receive_keystroke(&l, 6001, press_a, &r));
}

static const uint8_t press_b[REPORT_KEYBOARD_LEN] = {0x00, 0x05, 0x00, 0x00,
						     0x00, 0x00, 0x00, 0xc9};

#define ATTACK_START 10000

static void start_attack(struct unifying_link *l)
{
	struct report r;

	accept_counter_1000(l);
	CHECK(!receive_keystroke_at(l, 1001, forged, ATTACK_START, &r));
}

static void test_under_attack_a_lone_valid_frame_types_nothing(void)
{
	struct unifying_link l;
	struct report r;

	start_attack(&l);
	CHECK(!receive_keystroke_at(&l, 1001, press_a, ATTACK_START + 10, &r));
	CHECK_EQ(l.next_counter, 1001);
}

static void test_under_attack_a_consecutive_pair_types_the_first(void)
{
	struct unifying_link l;
	struct report r = {.kind = REPORT_SYSTEM};

	start_attack(&l);
	CHECK(!receive_keystroke_at(&l, 1001, press_a, ATTACK_START + 10, &r));
	CHECK(receive_keystroke_at(&l, 1002, press_b, ATTACK_START + 20, &r));
	CHECK_EQ(r.kind, REPORT_KEYBOARD);
	CHECK(reports_key_held(&r, 0x04));
	CHECK_EQ(l.next_counter, 1002);
	CHECK(receive_keystroke_at(&l, 1003, press_a, ATTACK_START + 30, &r));
	CHECK(reports_key_held(&r, 0x05));
	CHECK_EQ(l.next_counter, 1003);
}

static void test_under_attack_a_pair_with_a_gap_types_nothing(void)
{
	struct unifying_link l;
	struct report r;

	start_attack(&l);
	CHECK(!receive_keystroke_at(&l, 1001, press_a, ATTACK_START + 10, &r));
	CHECK(!receive_keystroke_at(&l, 1003, press_a, ATTACK_START + 20, &r));
	CHECK(!receive_keystroke_at(&l, 1005, press_a, ATTACK_START + 30, &r));
	CHECK_EQ(l.next_counter, 1001);
}

static void test_under_attack_a_jump_is_refused_even_with_its_successor(void)
{
	struct unifying_link l;
	struct report r;

	start_attack(&l);
	CHECK(!receive_keystroke_at(&l, 6000, press_a, ATTACK_START + 10, &r));
	CHECK(!receive_keystroke_at(&l, 6001, press_b, ATTACK_START + 20, &r));
	CHECK(!receive_keystroke_at(&l, 6002, press_b, ATTACK_START + 30, &r));
	CHECK_EQ(l.next_counter, 1001);
}

static void test_under_attack_a_pair_ending_at_the_window_edge_types_the_first(void)
{
	struct unifying_link l;
	struct report r;

	start_attack(&l);
	CHECK(!receive_keystroke_at(&l, 1001 + UNIFYING_COUNTER_WINDOW - 1, press_a,
				    ATTACK_START + 10, &r));
	CHECK(receive_keystroke_at(&l, 1001 + UNIFYING_COUNTER_WINDOW, press_b, ATTACK_START + 20,
				   &r));
	CHECK(reports_key_held(&r, 0x04));
	CHECK_EQ(l.next_counter, 1001 + UNIFYING_COUNTER_WINDOW);
}

static void test_a_marker_failure_drops_the_pending_frame(void)
{
	struct unifying_link l;
	struct report r;

	start_attack(&l);
	CHECK(!receive_keystroke_at(&l, 1001, press_a, ATTACK_START + 10, &r));
	CHECK(!receive_keystroke_at(&l, 1002, forged, ATTACK_START + 20, &r));
	CHECK(!receive_keystroke_at(&l, 1002, press_a, ATTACK_START + 30, &r));
	CHECK_EQ(l.next_counter, 1001);
}

static void test_a_frame_pending_when_the_attack_ends_is_dropped(void)
{
	struct unifying_link l;
	struct report r;

	start_attack(&l);
	CHECK(!receive_keystroke_at(&l, 1001, press_a, ATTACK_START + 10, &r));
	CHECK(receive_keystroke_at(&l, 1002, press_b, ATTACK_START + UNIFYING_ATTACK_MS, &r));
	CHECK(reports_key_held(&r, 0x05));
	CHECK_EQ(l.next_counter, 1003);
}

static void test_a_forged_frame_after_a_marker_failure_does_not_raise_the_counter(void)
{
	struct unifying_link l;
	struct report r;

	start_attack(&l);
	CHECK(!receive_keystroke_at(&l, 1016, press_a, ATTACK_START + 10, &r));
	CHECK_EQ(l.next_counter, 1001);
}

static void test_under_attack_a_press_confirmed_by_its_release_types_both(void)
{
	static const uint8_t no_keys[REPORT_KEYS_LEN] = {0};
	struct unifying_link l;
	struct report out[UNIFYING_KEYSTROKE_REPORTS];

	start_attack(&l);
	CHECK_EQ(receive_keystrokes_at(&l, 1001, press_a, ATTACK_START + 10, out), 0);
	CHECK_EQ(receive_keystrokes_at(&l, 1002, release, ATTACK_START + 20, out), 2);
	CHECK(reports_key_held(&out[0], 0x04));
	CHECK_EQ(out[1].kind, REPORT_KEYBOARD);
	CHECK_MEM(out[1].keys, no_keys, REPORT_KEYS_LEN);
	CHECK_EQ(l.next_counter, 1003);
	CHECK(!l.keyboard_down);
	CHECK_EQ(receive_keystrokes_at(&l, 1004, press_a, ATTACK_START + 30, out), 0);
}

static void test_under_attack_a_lone_release_is_typed_at_once(void)
{
	struct unifying_link l;
	struct report out[UNIFYING_KEYSTROKE_REPORTS];

	start_attack(&l);
	CHECK_EQ(receive_keystrokes_at(&l, 1001, release, ATTACK_START + 10, out), 1);
	CHECK_EQ(l.next_counter, 1002);
	CHECK_EQ(receive_keystrokes_at(&l, 1001 + 1 + UNIFYING_COUNTER_WINDOW, release,
				       ATTACK_START + 20, out),
		 0);
}

static void test_a_first_lone_press_after_reset_types_nothing(void)
{
	struct unifying_link l;
	struct report r;

	unifying_link_reset(&l);
	CHECK(!receive_keystroke(&l, 500, press_a, &r));
	CHECK_EQ(l.next_counter, 0);
}

static void test_a_first_press_confirmed_by_the_next_counter_types_both(void)
{
	struct unifying_link l;
	struct report out[UNIFYING_KEYSTROKE_REPORTS];

	unifying_link_reset(&l);
	CHECK_EQ(receive_keystrokes_at(&l, 500, press_a, 0, out), 0);
	CHECK_EQ(receive_keystrokes_at(&l, 501, press_b, 0, out), 2);
	CHECK(reports_key_held(&out[0], 0x04));
	CHECK(reports_key_held(&out[1], 0x05));
	CHECK_EQ(l.next_counter, 502);
	CHECK_EQ(receive_keystrokes_at(&l, 502, press_a, 0, out), 1);
	CHECK(reports_key_held(&out[0], 0x04));
}

static void test_a_first_release_after_reset_is_typed_at_once(void)
{
	struct unifying_link l;
	struct report r;

	unifying_link_reset(&l);
	CHECK(receive_keystroke(&l, 500, release, &r));
	CHECK_EQ(l.next_counter, 501);
}

static void test_counters_near_the_top_are_refused_before_aes(void)
{
	struct unifying_link l;
	struct report r;

	unifying_link_reset(&l);
	aes_calls = 0;
	CHECK(!receive_keystroke(&l, 0xffffffff, release, &r));
	CHECK(!receive_keystroke(&l, 0xffffffff - (1 << 20) + 1, release, &r));
	CHECK_EQ(aes_calls, 0);
	CHECK_EQ(l.next_counter, 0);
	sync_at(&l, 0xfff00000 - UNIFYING_COUNTER_WINDOW);
	aes_calls = 0;
	CHECK(!receive_keystroke(&l, 0xfff00000, release, &r));
	CHECK_EQ(aes_calls, 0);
	CHECK(receive_keystroke(&l, 0xfff00000 - 1, press_a, &r));
	CHECK_EQ(l.next_counter, 0xfff00000);
}

static void test_with_no_counter_an_attack_refuses_every_press(void)
{
	struct unifying_link l;
	struct report out[UNIFYING_KEYSTROKE_REPORTS];
	uint64_t t = 10000;

	unifying_link_reset(&l);
	CHECK_EQ(receive_keystrokes_at(&l, 700, forged, t, out), 0);
	CHECK_EQ(receive_keystrokes_at(&l, 800, press_a, t + 10, out), 0);
	CHECK_EQ(receive_keystrokes_at(&l, 801, press_b, t + 20, out), 0);
	CHECK_EQ(receive_keystrokes_at(&l, 802, press_a, t + 30, out), 0);
	CHECK_EQ(l.next_counter, 0);
	CHECK_EQ(receive_keystrokes_at(&l, 800, press_a, t + UNIFYING_ATTACK_MS, out), 0);
	CHECK_EQ(receive_keystrokes_at(&l, 801, press_b, t + UNIFYING_ATTACK_MS + 10, out), 2);
	CHECK_EQ(l.next_counter, 802);
}

static void test_with_no_counter_an_attack_still_types_a_release(void)
{
	struct unifying_link l;
	struct report out[UNIFYING_KEYSTROKE_REPORTS];

	unifying_link_reset(&l);
	CHECK_EQ(receive_keystrokes_at(&l, 700, forged, 10000, out), 0);
	CHECK_EQ(receive_keystrokes_at(&l, 800, release, 10010, out), 1);
	CHECK_EQ(l.next_counter, 801);
}

static const uint8_t connect_22[UNIFYING_FRAME_MAX] = {
	0x07, 0x51, 0x07, 0x04, 0x00, 0x01, 0x01, 0x01, [21] = 0x9a};
static const uint8_t connect_10[] = {0x07, 0x4f, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa3};

static void test_after_a_reconnect_a_counter_below_the_old_one_is_confirmed_by_the_next(void)
{
	struct unifying_link l;
	struct report out[UNIFYING_KEYSTROKE_REPORTS];

	unifying_link_reset(&l);
	sync_at(&l, 3703468736u);
	unifying_link_reconnect(&l, 0x07, connect_22, sizeof(connect_22));
	CHECK(!l.synced);
	CHECK_EQ(receive_keystrokes_at(&l, 0x69c4de07, press_a, 0, out), 0);
	CHECK_EQ(receive_keystrokes_at(&l, 0x69c4de08, release, 0, out), 2);
	CHECK(reports_key_held(&out[0], 0x04));
	CHECK_EQ(l.next_counter, 0x69c4de09);
}

static void test_a_lone_press_after_a_reconnect_is_held(void)
{
	struct unifying_link l;
	struct report r;

	accept_counter_1000(&l);
	unifying_link_reconnect(&l, 0x07, connect_10, sizeof(connect_10));
	CHECK(!l.synced);
	CHECK(!receive_keystroke(&l, 1001, press_a, &r));
	CHECK(!l.synced);
}

static void test_a_reconnect_drops_a_held_jump(void)
{
	struct unifying_link l;
	struct report r;

	accept_counter_1000(&l);
	CHECK(!receive_keystroke(&l, 6000, press_a, &r));
	unifying_link_reconnect(&l, 0x07, connect_22, sizeof(connect_22));
	CHECK(!l.synced);
	CHECK(!l.has_candidate);
	CHECK(!receive_keystroke(&l, 6001, press_a, &r));
}

static void test_a_reconnect_releases_held_keys(void)
{
	struct unifying_link l;
	struct report r;

	accept_counter_1000(&l);
	CHECK(l.keyboard_down);
	CHECK(unifying_link_reconnect(&l, 0x07, connect_22, sizeof(connect_22)));
	CHECK(!l.keyboard_down);
	CHECK(!unifying_link_reconnect(&l, 0x07, connect_10, sizeof(connect_10)));
	unifying_media_report(&l, media_volume_up, sizeof(media_volume_up), &r);
	CHECK(unifying_link_reconnect(&l, 0x07, connect_10, sizeof(connect_10)));
	CHECK_EQ(l.media_usage, 0);
}

static void test_a_reconnect_with_no_key_held_releases_nothing(void)
{
	struct unifying_link l;

	unifying_link_reset(&l);
	sync_at(&l, 1000);
	CHECK(!unifying_link_reconnect(&l, 0x07, connect_22, sizeof(connect_22)));
	CHECK(!l.synced);
}

static void test_other_receiver_frames_leave_the_counter(void)
{
	uint8_t bad[sizeof(connect_10)];
	struct unifying_link l;
	struct report r;

	accept_counter_1000(&l);
	memcpy(bad, connect_10, sizeof(bad));
	bad[sizeof(bad) - 1] ^= 0x10;
	CHECK(!unifying_link_reconnect(&l, 0x07, bad, sizeof(bad)));
	CHECK(!unifying_link_reconnect(&l, 0x07, keep_alive_1100, sizeof(keep_alive_1100)));
	CHECK(!unifying_link_reconnect(&l, 0x07, pairing_complete, sizeof(pairing_complete)));
	CHECK(!unifying_link_reconnect(&l, 0x06, connect_22, sizeof(connect_22)));
	CHECK(!unifying_link_reconnect(&l, 0x06, connect_10, sizeof(connect_10)));
	CHECK(receive_keystroke(&l, 1001, press_a, &r));
}

static void test_an_attack_after_a_reconnect_refuses_every_press(void)
{
	struct unifying_link l;
	struct report out[UNIFYING_KEYSTROKE_REPORTS];

	accept_counter_1000(&l);
	unifying_link_reconnect(&l, 0x07, connect_22, sizeof(connect_22));
	CHECK(!l.synced);
	CHECK_EQ(receive_keystrokes_at(&l, 50, forged, ATTACK_START, out), 0);
	CHECK_EQ(receive_keystrokes_at(&l, 60, press_a, ATTACK_START + 10, out), 0);
	CHECK_EQ(receive_keystrokes_at(&l, 61, press_a, ATTACK_START + 20, out), 0);
	CHECK(!l.synced);
}

static void test_the_first_counter_may_be_zero(void)
{
	struct unifying_link l;
	struct report out[UNIFYING_KEYSTROKE_REPORTS];

	unifying_link_reset(&l);
	CHECK_EQ(receive_keystrokes_at(&l, 0, press_a, 0, out), 0);
	CHECK_EQ(receive_keystrokes_at(&l, 1, press_a, 0, out), 2);
	CHECK_EQ(l.next_counter, 2);
	CHECK_EQ(receive_keystrokes_at(&l, 0, press_a, 0, out), 0);
}

static void test_a_plaintext_without_the_marker_byte_is_rejected_and_keeps_the_counter(void)
{
	struct unifying_link l;
	struct report r = {.kind = REPORT_SYSTEM};

	unifying_link_reset(&l);
	sync_at(&l, 1000);
	CHECK(!receive_keystroke(&l, 1000, forged, &r));
	CHECK_EQ(r.kind, REPORT_SYSTEM);
	CHECK(!l.keyboard_down);
	CHECK_EQ(l.next_counter, 1000);
	CHECK(receive_keystroke_at(&l, 1000, press_a, UNIFYING_ATTACK_MS, &r));
	CHECK(l.keyboard_down);
	CHECK_EQ(l.next_counter, 1001);
}

static void test_an_unencrypted_keyboard_frame_is_refused(void)
{
	struct unifying_link l;
	struct report r[UNIFYING_KEYSTROKE_REPORTS];
	uint8_t f[UNIFYING_FRAME_MAX] = {0x00, 0xc1};

	unifying_link_reset(&l);
	aes_calls = 0;
	seal(f, sizeof(f));
	CHECK_EQ(unifying_keystroke(&l, aes_key, f, sizeof(f), 0, fake_aes, r), 0);
	CHECK_EQ(aes_calls, 0);
	CHECK_EQ(l.next_counter, 0);
}

static void test_media_frames_carry_the_usage_in_bytes_2_and_3(void)
{
	struct unifying_link l;
	struct report r;

	unifying_link_reset(&l);
	CHECK(unifying_media_report(&l, media_volume_up, sizeof(media_volume_up), &r));
	CHECK_EQ(r.kind, REPORT_CONSUMER);
	CHECK_EQ(r.usage, 0x00e9);
	CHECK(l.media_usage != 0);
	CHECK(unifying_media_report(&l, media_volume_down, sizeof(media_volume_down), &r));
	CHECK_EQ(r.usage, 0x00ea);
	CHECK(unifying_media_report(&l, media_mute, sizeof(media_mute), &r));
	CHECK_EQ(r.usage, 0x00cd);
	CHECK(unifying_media_report(&l, media_release, sizeof(media_release), &r));
	CHECK_EQ(r.kind, REPORT_CONSUMER);
	CHECK_EQ(r.usage, 0);
	CHECK_EQ(l.media_usage, 0);
}

static void test_a_media_frame_with_a_bad_checksum_or_another_type_is_refused(void)
{
	uint8_t f[sizeof(media_volume_up)];
	struct unifying_link l;
	struct report r;

	unifying_link_reset(&l);
	memcpy(f, media_volume_up, sizeof(f));
	f[sizeof(f) - 1] ^= 0x10;
	CHECK(!unifying_media_report(&l, f, sizeof(f), &r));
	f[1] = 0xc1;
	seal(f, sizeof(f));
	CHECK(!unifying_media_report(&l, f, sizeof(f), &r));
	f[1] = 0xc3;
	seal(f, sizeof(f));
	CHECK(!unifying_media_report(&l, f, sizeof(f) - 1, &r));
	CHECK_EQ(l.media_usage, 0);
}

static void test_the_led_frame_carries_the_led_bits(void)
{
	static const uint8_t expected[UNIFYING_LED_FRAME_LEN] = {0x00, 0x0e, 0x02, 0x00, 0x00,
								 0x00, 0x00, 0x00, 0x00, 0xf0};
	uint8_t f[UNIFYING_LED_FRAME_LEN];

	CHECK_EQ(unifying_led_frame(0x02, f), UNIFYING_LED_FRAME_LEN);
	CHECK_MEM(f, expected, UNIFYING_LED_FRAME_LEN);
}

static void hold_a_key(struct unifying_link *l)
{
	struct report r;

	sync_at(l, 1);
	receive_keystroke(l, 1, press_a, &r);
}

static void test_keep_alive_frames_set_the_timeout(void)
{
	struct unifying_link l;

	unifying_link_reset(&l);
	CHECK_EQ(l.keepalive_ms, UNIFYING_KEEPALIVE_DEFAULT_MS);
	unifying_link_heard(&l, set_keep_alive_88, sizeof(set_keep_alive_88), 5);
	CHECK_EQ(l.keepalive_ms, 88);
	CHECK_EQ(l.last_ms, 5);
	unifying_link_heard(&l, keep_alive_1100, sizeof(keep_alive_1100), 9);
	CHECK_EQ(l.keepalive_ms, 1100);
	CHECK_EQ(l.last_ms, 9);
}

static void test_other_frames_leave_the_timeout_but_count_as_heard(void)
{
	struct unifying_link l;
	uint8_t bad[sizeof(keep_alive_1100)];

	unifying_link_reset(&l);
	unifying_link_heard(&l, pairing_complete, sizeof(pairing_complete), 7);
	CHECK_EQ(l.keepalive_ms, UNIFYING_KEEPALIVE_DEFAULT_MS);
	CHECK_EQ(l.last_ms, 7);
	memcpy(bad, keep_alive_1100, sizeof(bad));
	bad[sizeof(bad) - 1] ^= 0x10;
	unifying_link_heard(&l, bad, sizeof(bad), 8);
	CHECK_EQ(l.keepalive_ms, UNIFYING_KEEPALIVE_DEFAULT_MS);
}

static void test_a_held_key_goes_stuck_after_twice_the_timeout_once(void)
{
	struct unifying_link l;
	uint64_t t = 10000;

	unifying_link_reset(&l);
	unifying_link_heard(&l, set_keep_alive_88, sizeof(set_keep_alive_88), t);
	hold_a_key(&l);
	CHECK(!unifying_link_silent(&l, t + 176));
	CHECK(unifying_link_silent(&l, t + 177));
	CHECK(!unifying_link_silent(&l, t + 1000));
	CHECK(!l.keyboard_down);
}

static void test_a_frame_heard_in_time_postpones_the_stuck_key(void)
{
	struct unifying_link l;

	unifying_link_reset(&l);
	unifying_link_heard(&l, set_keep_alive_88, sizeof(set_keep_alive_88), 0);
	hold_a_key(&l);
	unifying_link_heard(&l, keep_alive_1100, sizeof(keep_alive_1100), 100);
	CHECK(!unifying_link_silent(&l, 1000));
	CHECK(!unifying_link_silent(&l, 100 + 2 * 1100));
	CHECK(unifying_link_silent(&l, 100 + 2 * 1100 + 1));
}

static void test_nothing_held_is_never_silent(void)
{
	struct unifying_link l;

	unifying_link_reset(&l);
	unifying_link_heard(&l, set_keep_alive_88, sizeof(set_keep_alive_88), 0);
	CHECK(!unifying_link_silent(&l, 100000));
}

static void test_a_release_report_clears_the_held_state(void)
{
	static const uint8_t released[REPORT_KEYBOARD_LEN] = {0x00, 0x00, 0x00, 0x00,
							      0x00, 0x00, 0x00, 0xc9};
	struct unifying_link l;
	struct report r;

	unifying_link_reset(&l);
	unifying_link_heard(&l, set_keep_alive_88, sizeof(set_keep_alive_88), 0);
	hold_a_key(&l);
	receive_keystroke(&l, 2, released, &r);
	CHECK(!unifying_link_silent(&l, 100000));
	unifying_media_report(&l, media_volume_up, sizeof(media_volume_up), &r);
	CHECK(unifying_link_silent(&l, 100000));
	unifying_media_report(&l, media_volume_up, sizeof(media_volume_up), &r);
	unifying_media_report(&l, media_release, sizeof(media_release), &r);
	CHECK(!unifying_link_silent(&l, 200000));
}

static void test_a_known_counter_refuses_counters_below_it_and_bounds_the_window(void)
{
	struct unifying_link l;
	struct report r;

	unifying_link_reset(&l);
	sync_at(&l, 5000);
	CHECK(!receive_keystroke(&l, 4999, press_a, &r));
	CHECK(!receive_keystroke(&l, 5000 + UNIFYING_COUNTER_WINDOW, press_a, &r));
	CHECK(receive_keystroke(&l, 5000, press_a, &r));
}

static void test_the_record_is_the_wpid_the_key_then_the_keyboards_own_report(void)
{
	struct unifying_pairing p;
	struct unifying_device d;
	uint8_t rec[UNIFYING_RECORD_LEN];

	CHECK_EQ(UNIFYING_RECORD_LEN, 39);
	restore_paired(&p, &d);
	unifying_record_of(&p, 0, rec);
	CHECK_MEM(&rec[0], d.wpid, 2);
	CHECK_MEM(&rec[2], d.key, UNIFYING_KEY_LEN);
	CHECK_MEM(&rec[18], d.serial, UNIFYING_SERIAL_LEN);
	CHECK_EQ(rec[22], d.type);
	CHECK_MEM(&rec[23], mx_keys, sizeof(mx_keys));
}

static void test_a_restored_record_pairs_the_device(void)
{
	struct unifying_pairing p = open_pairing(), q;
	uint8_t rec[UNIFYING_RECORD_LEN];

	pair_fully(&p);
	unifying_record_of(&p, 0, rec);
	unifying_record_restore(&q, 0, rec);
	CHECK(unifying_has_device(&q));
	CHECK_MEM(&q.devices[0], &p.devices[0], sizeof(p.devices[0]));
	CHECK_EQ(q.attempt.state, UNIFYING_IDLE);
}

static void check_due(const struct unifying_led *led, uint8_t want)
{
	uint8_t leds = 0xff;

	CHECK(unifying_led_due(led, true, &leds));
	CHECK_EQ(leds, want);
}

static void test_no_led_frame_is_due_before_a_change_or_resend(void)
{
	struct unifying_led led = {0};
	uint8_t leds;

	CHECK(!unifying_led_due(&led, true, &leds));
}

static void test_an_led_change_is_sent_at_once(void)
{
	struct unifying_led led = {0};
	uint8_t leds;

	unifying_led_want(&led, 0x02);
	check_due(&led, 0x02);
	unifying_led_sent(&led, 0x02);
	CHECK(!unifying_led_due(&led, true, &leds));
}

static void test_the_first_led_state_is_sent_even_when_it_is_zero(void)
{
	struct unifying_led led = {0};

	unifying_led_want(&led, 0x00);
	check_due(&led, 0x00);
}

static void test_changes_back_to_the_sent_state_send_nothing_more(void)
{
	struct unifying_led led = {0};
	uint8_t leds;

	unifying_led_want(&led, 0x02);
	unifying_led_sent(&led, 0x02);
	unifying_led_want(&led, 0x00);
	unifying_led_want(&led, 0x02);
	CHECK(!unifying_led_due(&led, true, &leds));
}

static void test_a_resend_sends_the_wanted_state(void)
{
	struct unifying_led led = {0};

	unifying_led_resend(&led);
	check_due(&led, 0x00);
}

static void test_an_led_frame_not_queued_stays_due(void)
{
	struct unifying_led led = {0};

	unifying_led_want(&led, 0x02);
	check_due(&led, 0x02);
	check_due(&led, 0x02);
}

static void test_no_led_frame_is_due_without_a_paired_device(void)
{
	struct unifying_led led = {0};
	uint8_t leds;

	unifying_led_want(&led, 0x02);
	CHECK(!unifying_led_due(&led, false, &leds));
	check_due(&led, 0x02);
}

static void session_paired(struct unifying_session *s)
{
	struct unifying_pairing done = open_pairing();
	uint8_t record[UNIFYING_RECORD_LEN];

	pair_fully(&done);
	unifying_record_of(&done, 0, record);
	unifying_session_init(s, &receiver, 0);
	unifying_session_restore(s, 0, record);
}

static void test_a_session_without_a_record_is_not_paired(void)
{
	struct unifying_session s;

	unifying_session_init(&s, &receiver, 0);
	CHECK_EQ(unifying_session_status(&s, 0), UNIFYING_STATUS_NOT_PAIRED);
}

static void test_a_forgotten_keyboard_leaves_the_session_unpaired_and_pairing_open(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t channel;

	session_paired(&s);
	channel = s.channel;
	CHECK_EQ(unifying_session_status(&s, 0), UNIFYING_STATUS_SILENT);
	unifying_session_forget(&s, 0, 100, &out);
	CHECK(out.release_all);
	CHECK(out.window_opened);
	CHECK(out.listen);
	CHECK_EQ(out.pipes, unifying_listen_pipes(&s.pairing));
	CHECK_EQ(out.set_channel, s.channel != channel);
	CHECK_EQ(unifying_session_status(&s, 100), UNIFYING_STATUS_NOT_PAIRED);
	CHECK(unifying_pairing_window_open(&s.pairing, 100000));
	CHECK_MEM(s.receiver.base, receiver.base, sizeof(receiver.base));
}

static void test_a_session_answers_pairing_on_the_frames_pipe(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	unifying_session_init(&s, &receiver, 0);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 0);
	CHECK(unifying_pairing_window_open(&s.pairing, 10));
	request_1(f, 0xe1, 0x05);
	unifying_session_frame(&s, UNIFYING_PIPE_PAIRING, f, sizeof(f), -50, 10, nonce, NULL, &out);
	CHECK_EQ(out.event, UNIFYING_RX_ANSWERED_1);
	CHECK_EQ(out.reply_pipe, UNIFYING_PIPE_PAIRING);
	CHECK_EQ(out.reply_len, UNIFYING_FRAME_MAX);
}

static void test_a_refused_request_gets_no_reply_from_the_session(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	unifying_session_init(&s, &receiver, 0);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 0);
	request_1(f, 0xe1, 0x01);
	unifying_session_frame(&s, UNIFYING_PIPE_PAIRING, f, sizeof(f), 0, 10, nonce, NULL, &out);
	CHECK_EQ(out.event, UNIFYING_RX_REFUSED);
	CHECK_EQ(out.reply_len, 0);
}

static void test_completing_pairing_through_the_session_closes_the_window(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	unifying_session_init(&s, &receiver, 0);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 0);
	request_1(f, 0xe1, 0x05);
	unifying_session_frame(&s, UNIFYING_PIPE_PAIRING, f, sizeof(f), 0, 0, nonce, NULL, &out);
	request_2(f, 0xe1);
	unifying_session_frame(&s, PIPE_0, f, sizeof(f), 0, 10, nonce, fake_aes, &out);
	CHECK_EQ(out.event, UNIFYING_RX_ANSWERED_2);
	request_3(f, 0x00);
	unifying_session_frame(&s, PIPE_0, f, sizeof(f), 0, 20, nonce, fake_aes, &out);
	CHECK_EQ(out.event, UNIFYING_RX_PAIRED);
	CHECK(!unifying_pairing_window_open(&s.pairing, 30));
}

static void test_a_connect_frame_on_the_receiver_pipe_resends_the_leds(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_paired(&s);
	unifying_session_tick(&s, 0, true, 0x02, 0, &out);
	CHECK(out.send_led);
	unifying_session_led_queued(&s, out.leds, 0);
	unifying_session_tick(&s, 10, false, 0x02, 1, &out);
	CHECK(!out.send_led);
	unifying_session_frame(&s, UNIFYING_PIPE_RECEIVER, connect_10, sizeof(connect_10), 0, 20,
			       nonce, fake_aes, &out);
	unifying_session_tick(&s, 30, false, 0x02, 1, &out);
	CHECK(out.send_led);
	CHECK_EQ(out.leds, 0x02);
}

static void test_a_forgotten_keyboard_still_sending_is_ignored(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_paired(&s);
	unifying_session_forget(&s, 0, 0, &out);
	CHECK(!unifying_session_frame(&s, PIPE_0, keep_alive_1100,
				      sizeof(keep_alive_1100), -60, 1000, nonce, fake_aes, &out));
	CHECK_EQ(out.heard, UNIFYING_HEARD_NONE);
	CHECK(!unifying_session_frame(&s, UNIFYING_PIPE_RECEIVER, connect_10, sizeof(connect_10), 0,
				      1010, nonce, fake_aes, &out));
	CHECK(!out.reconnected);
	CHECK(!s.heard);
	CHECK_EQ(unifying_session_status(&s, 1010), UNIFYING_STATUS_NOT_PAIRED);
	CHECK(unifying_pairing_window_open(&s.pairing, 1010));
}

static void test_a_heard_paired_keyboard_is_connected_then_silent(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_paired(&s);
	CHECK_EQ(unifying_session_status(&s, 0), UNIFYING_STATUS_SILENT);
	unifying_session_frame(&s, PIPE_0, keep_alive_1100, sizeof(keep_alive_1100),
			       -60, 1000, nonce, fake_aes, &out);
	CHECK_EQ(s.rssi, -60);
	CHECK_EQ(unifying_session_status(&s, 1000 + 2 * s.links[0].keepalive_ms - 1),
		 UNIFYING_STATUS_CONNECTED);
	CHECK_EQ(unifying_session_status(&s, 1000 + 2 * s.links[0].keepalive_ms),
		 UNIFYING_STATUS_SILENT);
}

static void test_the_tick_moves_the_channel_while_pairing_is_open(void)
{
	struct unifying_session s;
	struct unifying_out out;

	unifying_session_init(&s, &receiver, 0);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 0);
	unifying_session_tick(&s, UNIFYING_SWEEP_DWELL_MS, false, 0, 0, &out);
	CHECK(out.set_channel);
	CHECK_EQ(out.channel, s.channel);
	unifying_session_tick(&s, UNIFYING_SWEEP_DWELL_MS + 1, false, 0, 0, &out);
	CHECK(!out.set_channel);
}

static void test_a_move_sets_the_channel_and_holds_while_pairing_is_closed(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_paired(&s);
	CHECK_EQ(unifying_session_move(&s, 41, 1000), CHANMOVE_DONE);
	unifying_session_tick(&s, 1001, false, 0, 0, &out);
	CHECK(!out.set_channel);
	CHECK_EQ(s.channel, 41);
}

static void test_a_stored_keyboard_keeps_its_channel_while_pairing_is_open(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_paired(&s);
	CHECK_EQ(unifying_session_move(&s, 41, 1000), CHANMOVE_DONE);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 1000);
	for (uint64_t t = 1000; t < 1000 + PAIRWIN_MS; t += UNIFYING_SWEEP_DWELL_MS) {
		unifying_session_tick(&s, t, false, 0, 0, &out);
		CHECK(!out.set_channel);
	}
	CHECK_EQ(s.channel, 41);
	CHECK_EQ(unifying_session_home(&s), 41);
}

static void test_a_move_is_refused_while_an_attempt_outlasts_the_window(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_paired(&s);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 0);
	request_1(f, 0xe1, 0x05);
	unifying_session_frame(&s, UNIFYING_PIPE_PAIRING, f, sizeof(f), -50, PAIRWIN_MS - 10, nonce,
			       NULL, &out);
	CHECK_EQ(out.event, UNIFYING_RX_ANSWERED_1);
	CHECK_EQ(unifying_session_move(&s, 41, PAIRWIN_MS + 1), CHANMOVE_NOT_MOVABLE);
	CHECK(s.channel != 41);
}

static void session_holding_a_key(struct unifying_session *s);

static void holding_a_key(struct unifying_session *s)
{
	struct unifying_out out;

	session_holding_a_key(s);
	unifying_session_frame(s, PIPE_0, set_keep_alive_88,
			       sizeof(set_keep_alive_88), -60, 30, nonce, fake_aes, &out);
}

static void test_a_move_holds_keys_and_silence_while_the_keyboard_searches(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint64_t found_by = 100 + UNIFYING_MOVE_SEARCH_MS;

	holding_a_key(&s);
	CHECK_EQ(unifying_session_move(&s, 41, 100), CHANMOVE_DONE);
	unifying_session_tick(&s, found_by - 1, false, 0, 0, &out);
	CHECK(!out.release_all);
	CHECK(!out.went_silent);
	unifying_session_tick(&s, found_by, false, 0, 0, &out);
	CHECK(out.release_all);
	unifying_session_tick(&s, found_by + 2 * s.links[0].keepalive_ms - 1, false, 0, 0, &out);
	CHECK(!out.went_silent);
	unifying_session_tick(&s, found_by + 2 * s.links[0].keepalive_ms, false, 0, 0, &out);
	CHECK(out.went_silent);
}

static void test_hearing_the_keyboard_after_a_move_ends_the_wait(void)
{
	struct unifying_session s;
	struct unifying_out out;

	holding_a_key(&s);
	CHECK_EQ(unifying_session_move(&s, 41, 100), CHANMOVE_DONE);
	unifying_session_frame(&s, PIPE_0, keep_alive_1100, sizeof(keep_alive_1100),
			       -60, 150, nonce, fake_aes, &out);
	unifying_session_tick(&s, 150 + 2 * s.links[0].keepalive_ms, false, 0, 0, &out);
	CHECK(out.went_silent);
	unifying_session_tick(&s, 150 + 2 * s.links[0].keepalive_ms + 1, false, 0, 0, &out);
	CHECK(out.release_all);
}

static void test_a_move_to_the_current_channel_keeps_the_silence_timer(void)
{
	struct unifying_session s;
	struct unifying_out out;

	holding_a_key(&s);
	CHECK_EQ(unifying_session_move(&s, s.channel, 100), CHANMOVE_DONE);
	unifying_session_tick(&s, 30 + 2 * s.links[0].keepalive_ms, false, 0, 0, &out);
	CHECK(out.went_silent);
}

static void test_a_move_off_the_pairing_channels_is_refused(void)
{
	struct unifying_session s;
	uint8_t at;

	holding_a_key(&s);
	at = s.channel;
	CHECK_EQ(unifying_session_move(&s, 6, 100), CHANMOVE_NOT_A_CHANNEL);
	CHECK_EQ(unifying_session_move(&s, 38, 100), CHANMOVE_NOT_A_CHANNEL);
	CHECK_EQ(s.channel, at);
}

static void test_a_move_needs_a_paired_keyboard(void)
{
	struct unifying_session s;

	unifying_session_init(&s, &receiver, 0);
	CHECK_EQ(unifying_session_move(&s, 41, 100), CHANMOVE_NOT_MOVABLE);
	session_paired(&s);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 100);
	CHECK_EQ(unifying_session_move(&s, 41, 100), CHANMOVE_DONE);
	CHECK_EQ(s.channel, 41);
}

static void test_a_move_works_on_a_silent_paired_keyboard(void)
{
	struct unifying_session s;
	struct unifying_out out;

	holding_a_key(&s);
	unifying_session_tick(&s, 100000, false, 0, 0, &out);
	CHECK_EQ(unifying_session_move(&s, 44, 100000), CHANMOVE_DONE);
	CHECK_EQ(s.channel, 44);
}

static void test_a_stored_keyboard_has_a_movable_home(void)
{
	struct unifying_session s;

	session_paired(&s);
	CHECK_EQ(unifying_session_movable(&s), s.channel);
	CHECK_EQ(unifying_session_home(&s), s.channel);
}

static void test_a_session_types_a_keystroke_from_the_device_pipe(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_paired(&s);
	keystroke(f, 0x5f66f458, release);
	unifying_session_frame(&s, PIPE_0, f, sizeof(f), 0, 10, nonce, fake_aes, &out);
	CHECK_EQ(out.reports, 1);
	keystroke(f, 0x5f66f459, press_a);
	unifying_session_frame(&s, PIPE_0, f, sizeof(f), 0, 20, nonce, fake_aes, &out);
	CHECK_EQ(out.reports, 1);
	CHECK_EQ(out.report[0].kind, REPORT_KEYBOARD);
	CHECK(reports_key_held(&out.report[0], 0x04));
}

static void hear(struct unifying_session *s, uint64_t now, struct unifying_out *out)
{
	unifying_session_frame(s, PIPE_0, keep_alive_1100, sizeof(keep_alive_1100),
			       -60, now, nonce, fake_aes, out);
}

static uint64_t silence_after(const struct unifying_session *s, uint64_t heard)
{
	return heard + 2 * (uint64_t)s->links[0].keepalive_ms;
}

static const uint8_t find_request[] = {0x00, 0x10, 0x07, 0x00, 0x01, 0x1b, 0x04, 0x00, 0x00, 0xc9};
static const uint8_t find_battery_request[] = {0x00, 0x10, 0x07, 0x00, 0x01,
					       0x10, 0x00, 0x00, 0x00, 0xd8};
static const uint8_t find_unified_battery_request[] = {0x00, 0x10, 0x07, 0x00, 0x01,
						       0x10, 0x04, 0x00, 0x00, 0xd4};
static const uint8_t read_battery_request[] = {0x00, 0x10, 0x07, 0x07, 0x01,
					       0x00, 0x00, 0x00, 0x00, 0xe1};

static void hidpp_frame(uint8_t f[UNIFYING_FRAME_MAX], uint8_t type, uint8_t feature,
			uint8_t function, uint8_t p0, uint8_t p1, uint8_t p2)
{
	memset(f, 0, UNIFYING_FRAME_MAX);
	f[1] = type;
	f[2] = 0x07;
	f[3] = feature;
	f[4] = function;
	f[5] = p0;
	f[6] = p1;
	f[7] = p2;
	seal(f, UNIFYING_FRAME_MAX);
}

static void hidpp(uint8_t f[UNIFYING_FRAME_MAX], uint8_t feature, uint8_t function, uint8_t p0,
		  uint8_t p1, uint8_t p2)
{
	hidpp_frame(f, 0x51, feature, function, p0, p1, p2);
}

static void divert_request(uint8_t f[UNIFYING_FRAME_MAX], uint8_t cid)
{
	hidpp_frame(f, 0x11, 0x08, 0x31, 0x00, cid, 0x03);
}

static void divert_reply(uint8_t f[UNIFYING_FRAME_MAX], uint8_t cid)
{
	hidpp(f, 0x08, 0x31, 0x00, cid, 0x03);
}

static void divert_refusal(uint8_t f[UNIFYING_FRAME_MAX])
{
	hidpp(f, 0xff, 0x08, 0x31, 0x02, 0x00);
}

static void session_feed(struct unifying_session *s, const uint8_t *f, size_t len, uint64_t now,
			 struct unifying_out *out)
{
	unifying_session_frame(s, PIPE_0, f, len, 0, now, nonce, fake_aes, out);
}

static void session_heard(struct unifying_session *s)
{
	struct unifying_out out;

	session_paired(s);
	hear(s, 10, &out);
}

static void session_found_the_feature(struct unifying_session *s)
{
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_heard(s);
	unifying_session_tick(s, 20, false, 0, 0, &out);
	hidpp(f, 0x00, 0x01, 0x08, 0x00, 0x04);
	session_feed(s, f, sizeof(f), 30, &out);
}

static void session_diverted(struct unifying_session *s)
{
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_found_the_feature(s);
	unifying_session_tick(s, 40, false, 0, 0, &out);
	divert_reply(f, 0xc7);
	session_feed(s, f, sizeof(f), 50, &out);
	unifying_session_tick(s, 60, false, 0, 0, &out);
	divert_reply(f, 0xc8);
	session_feed(s, f, sizeof(f), 70, &out);
}

static void check_hidpp(const struct unifying_out *out, const uint8_t *want, size_t len)
{
	CHECK_EQ(out->hidpp_len, len);
	CHECK(memcmp(out->hidpp, want, len) == 0);
}

static void check_end(const struct unifying_out *out, enum history_divert outcome,
		      uint8_t diverted, uint8_t refused, uint16_t cid)
{
	CHECK(out->hidpp_event.pending);
	CHECK_EQ(out->hidpp_event.detail.tag, DIAG_DETAIL_KEYS_DIVERTED);
	CHECK_EQ(out->hidpp_event.detail.KEYS_DIVERTED.outcome, outcome);
	CHECK_EQ(out->hidpp_event.detail.KEYS_DIVERTED.diverted, diverted);
	CHECK_EQ(out->hidpp_event.detail.KEYS_DIVERTED.refused, refused);
	CHECK_EQ(out->hidpp_event.detail.KEYS_DIVERTED.control, cid);
}

static void tick_queue(struct unifying_session *s, uint64_t now, uint32_t taken,
		       struct unifying_out *out)
{
	unifying_session_tick(s, now, false, 0, taken, out);
	if (out->send_led) {
		unifying_session_led_queued(s, out->leds, taken);
	}
	if (out->hidpp_len != 0) {
		unifying_session_hidpp_queued(s, taken);
	}
}

static uint64_t run_out_retries(struct unifying_session *s, uint64_t now, uint32_t *taken,
				const uint8_t *want, size_t len)
{
	uint64_t retry = 2 * (uint64_t)s->links[0].keepalive_ms;
	struct unifying_out out;

	for (int i = 0; i < HIDPP_TRIES; i++) {
		tick_queue(s, now, *taken, &out);
		check_hidpp(&out, want, len);
		CHECK(!out.hidpp_event.pending);
		tick_queue(s, now + 1, ++*taken, &out);
		CHECK_EQ(out.hidpp_len, 0);
		tick_queue(s, now + retry, *taken, &out);
		CHECK_EQ(out.hidpp_len, 0);
		now += retry + 1;
	}
	return now;
}

static void test_a_heard_keyboard_is_asked_where_its_reprogrammable_controls_are(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_heard(&s);
	unifying_session_tick(&s, 20, false, 0, 0, &out);
	check_hidpp(&out, find_request, sizeof(find_request));
}

static void test_the_brightness_keys_are_diverted_in_turn_and_reported_once(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];
	uint8_t want[UNIFYING_FRAME_MAX];

	session_found_the_feature(&s);
	unifying_session_tick(&s, 40, false, 0, 0, &out);
	divert_request(want, 0xc7);
	check_hidpp(&out, want, sizeof(want));
	CHECK_EQ(want[UNIFYING_FRAME_MAX - 1], 0xe5);
	divert_reply(f, 0xc7);
	session_feed(&s, f, sizeof(f), 50, &out);
	CHECK(!out.hidpp_event.pending);
	unifying_session_tick(&s, 60, false, 0, 0, &out);
	divert_request(want, 0xc8);
	check_hidpp(&out, want, sizeof(want));
	divert_reply(f, 0xc8);
	session_feed(&s, f, sizeof(f), 70, &out);
	check_end(&out, HISTORY_DIVERT_FINISHED, 2, 0, 0);
	unifying_session_tick(&s, 80, false, 0, 0, &out);
	check_hidpp(&out, find_battery_request, sizeof(find_battery_request));
	CHECK(!out.hidpp_event.pending);
}

static void test_an_unanswered_lookup_is_sent_again_then_given_up(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint64_t now;

	uint32_t taken = 0;

	session_heard(&s);
	now = run_out_retries(&s, 20, &taken, find_request, sizeof(find_request));
	unifying_session_tick(&s, now, false, 0, 0, &out);
	CHECK_EQ(out.hidpp_len, 0);
	check_end(&out, HISTORY_DIVERT_UNANSWERED, 0, 0, 0);
	tick_queue(&s, now + 1, taken, &out);
	check_hidpp(&out, find_battery_request, sizeof(find_battery_request));
	CHECK(!out.hidpp_event.pending);
}

static void test_an_unanswered_divert_reports_its_control(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t want[UNIFYING_FRAME_MAX];
	uint64_t now;

	uint32_t taken = 0;

	session_found_the_feature(&s);
	divert_request(want, 0xc7);
	now = run_out_retries(&s, 40, &taken, want, sizeof(want));
	unifying_session_tick(&s, now, false, 0, 0, &out);
	check_end(&out, HISTORY_DIVERT_UNANSWERED, 0, 0, 0x00c7);
}

static void test_a_keyboard_without_reprogrammable_controls_is_reported_and_left_alone(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_heard(&s);
	unifying_session_tick(&s, 20, false, 0, 0, &out);
	hidpp(f, 0x00, 0x01, 0x00, 0x00, 0x00);
	session_feed(&s, f, sizeof(f), 30, &out);
	check_end(&out, HISTORY_DIVERT_NO_FEATURE, 0, 0, 0);
	unifying_session_tick(&s, 40, false, 0, 0, &out);
	check_hidpp(&out, find_battery_request, sizeof(find_battery_request));
}

static void test_a_refused_divert_is_counted_and_moves_on_to_the_other_key(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];
	uint8_t want[UNIFYING_FRAME_MAX];

	session_found_the_feature(&s);
	unifying_session_tick(&s, 40, false, 0, 0, &out);
	divert_refusal(f);
	session_feed(&s, f, sizeof(f), 50, &out);
	unifying_session_tick(&s, 60, false, 0, 0, &out);
	divert_request(want, 0xc8);
	check_hidpp(&out, want, sizeof(want));
	divert_reply(f, 0xc8);
	session_feed(&s, f, sizeof(f), 70, &out);
	check_end(&out, HISTORY_DIVERT_FINISHED, 1, 1, 0);
}

static void test_diverted_brightness_keys_type_the_brightness_usages(void)
{
	static const struct {
		uint8_t cid;
		uint16_t usage;
	} steps[] = {{0xc7, HIDPP_BRIGHTNESS_DOWN},
		     {0x00, 0x0000},
		     {0xc8, HIDPP_BRIGHTNESS_UP},
		     {0x00, 0x0000}};
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_diverted(&s);
	for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
		hidpp(f, 0x08, 0x00, 0x00, steps[i].cid, 0x00);
		session_feed(&s, f, sizeof(f), 100 + i, &out);
		CHECK_EQ(out.reports, 1);
		CHECK_EQ(out.report[0].kind, REPORT_CONSUMER);
		CHECK_EQ(out.report[0].usage, steps[i].usage);
	}
}

static void test_an_fn_event_types_nothing(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_diverted(&s);
	hidpp(f, 0x08, 0x00, 0x00, 0x34, 0x00);
	session_feed(&s, f, sizeof(f), 100, &out);
	CHECK_EQ(out.reports, 0);
	hidpp(f, 0x08, 0x00, 0x00, 0x00, 0x00);
	session_feed(&s, f, sizeof(f), 110, &out);
	CHECK_EQ(out.reports, 0);
}

static void test_a_brightness_event_before_the_feature_is_found_types_nothing(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_heard(&s);
	hidpp(f, 0x08, 0x00, 0x00, 0xc7, 0x00);
	session_feed(&s, f, sizeof(f), 20, &out);
	CHECK_EQ(out.reports, 0);
}

static void test_a_connect_frame_diverts_again_without_a_second_lookup(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t want[UNIFYING_FRAME_MAX];

	session_diverted(&s);
	unifying_session_frame(&s, UNIFYING_PIPE_RECEIVER, connect_10, sizeof(connect_10), 0, 100,
			       nonce, fake_aes, &out);
	tick_queue(&s, 110, 0, &out);
	CHECK(out.send_led);
	tick_queue(&s, 111, 1, &out);
	divert_request(want, 0xc7);
	check_hidpp(&out, want, sizeof(want));
}

static void test_no_divert_request_goes_out_during_a_pairing_attempt(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_heard(&s);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 15);
	request_1(f, 0xe1, 0x05);
	unifying_session_frame(&s, UNIFYING_PIPE_PAIRING, f, sizeof(f), 0, 16, nonce, fake_aes,
			       &out);
	unifying_session_tick(&s, 20, false, 0, 0, &out);
	CHECK_EQ(out.hidpp_len, 0);
}

static void test_an_led_change_waits_until_the_queued_frame_is_taken(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_paired(&s);
	unifying_session_tick(&s, 0, true, 0x02, 7, &out);
	CHECK(out.send_led);
	unifying_session_led_queued(&s, out.leds, 7);
	unifying_session_tick(&s, 1, true, 0x00, 7, &out);
	unifying_session_tick(&s, 2, true, 0x03, 7, &out);
	CHECK(!out.send_led);
	unifying_session_tick(&s, 3, false, 0x03, 8, &out);
	CHECK(out.send_led);
	CHECK_EQ(out.leds, 0x03);
	unifying_session_led_queued(&s, out.leds, 8);
	unifying_session_tick(&s, 4, false, 0x03, 9, &out);
	CHECK(!out.send_led);
}

static void test_resends_while_a_frame_is_queued_are_sent_once_after_it_is_taken(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_paired(&s);
	unifying_session_tick(&s, 0, true, 0x02, 7, &out);
	unifying_session_led_queued(&s, out.leds, 7);
	unifying_session_frame(&s, UNIFYING_PIPE_RECEIVER, connect_10, sizeof(connect_10), 0, 1,
			       nonce, fake_aes, &out);
	unifying_session_frame(&s, UNIFYING_PIPE_RECEIVER, connect_10, sizeof(connect_10), 0, 2,
			       nonce, fake_aes, &out);
	unifying_session_tick(&s, 3, false, 0x02, 7, &out);
	CHECK(!out.send_led);
	unifying_session_tick(&s, 4, false, 0x02, 8, &out);
	CHECK(out.send_led);
	unifying_session_led_queued(&s, out.leds, 8);
	unifying_session_tick(&s, 5, false, 0x02, 9, &out);
	CHECK(!out.send_led);
}

static void test_a_dropped_led_frame_is_sent_again(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_paired(&s);
	unifying_session_tick(&s, 0, true, 0x02, 7, &out);
	unifying_session_led_queued(&s, out.leds, 7);
	unifying_session_replies_dropped(&s);
	unifying_session_tick(&s, 1, false, 0x02, 7, &out);
	CHECK(out.send_led);
	unifying_session_led_queued(&s, out.leds, 7);
	unifying_session_tick(&s, 2, false, 0x02, 8, &out);
	CHECK(!out.send_led);
	unifying_session_replies_dropped(&s);
	unifying_session_tick(&s, 3, false, 0x02, 8, &out);
	CHECK(!out.send_led);
}

static void test_a_divert_request_waits_behind_a_queued_led_frame(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_heard(&s);
	unifying_session_tick(&s, 20, true, 0x02, 0, &out);
	CHECK(out.send_led);
	CHECK_EQ(out.hidpp_len, 0);
	unifying_session_led_queued(&s, out.leds, 0);
	tick_queue(&s, 21, 0, &out);
	CHECK_EQ(out.hidpp_len, 0);
	tick_queue(&s, 22, 1, &out);
	check_hidpp(&out, find_request, sizeof(find_request));
}

static void test_a_request_the_sleeping_keyboard_never_takes_is_not_sent_again(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_heard(&s);
	tick_queue(&s, 20, 0, &out);
	check_hidpp(&out, find_request, sizeof(find_request));
	tick_queue(&s, 20 + 100000, 0, &out);
	CHECK_EQ(out.hidpp_len, 0);
	CHECK(!out.hidpp_event.pending);
}

static void test_a_dropped_request_is_sent_again_without_counting_a_try(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint32_t taken = 0;
	uint64_t now;

	session_heard(&s);
	tick_queue(&s, 20, taken, &out);
	unifying_session_replies_dropped(&s);
	now = run_out_retries(&s, 21, &taken, find_request, sizeof(find_request));
	tick_queue(&s, now, taken, &out);
	check_end(&out, HISTORY_DIVERT_UNANSWERED, 0, 0, 0);
}

static void test_a_wake_sends_the_divert_once(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t want[UNIFYING_FRAME_MAX];
	uint64_t silent;

	session_diverted(&s);
	silent = silence_after(&s, 70) + 1;
	unifying_session_tick(&s, silent, false, 0, 0, &out);
	unifying_session_frame(&s, UNIFYING_PIPE_RECEIVER, connect_10, sizeof(connect_10), 0,
			       silent + 10, nonce, fake_aes, &out);
	tick_queue(&s, silent + 11, 0, &out);
	CHECK(out.send_led);
	tick_queue(&s, silent + 12, 1, &out);
	divert_request(want, 0xc7);
	check_hidpp(&out, want, sizeof(want));
	unifying_session_frame(&s, UNIFYING_PIPE_RECEIVER, connect_10, sizeof(connect_10), 0,
			       silent + 13, nonce, fake_aes, &out);
	tick_queue(&s, silent + 14, 2, &out);
	CHECK(out.send_led);
	hear(&s, silent + 15, &out);
	CHECK_EQ(out.heard, UNIFYING_HEARD_WOKE);
	tick_queue(&s, silent + 16, 3, &out);
	CHECK(!out.send_led);
	CHECK_EQ(out.hidpp_len, 0);
}

static void test_releasing_a_brightness_key_leaves_a_held_media_key_down(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_diverted(&s);
	session_feed(&s, media_volume_up, sizeof(media_volume_up), 100, &out);
	CHECK_EQ(out.report[0].usage, 0x00e9);
	hidpp(f, 0x08, 0x00, 0x00, 0xc7, 0x00);
	session_feed(&s, f, sizeof(f), 110, &out);
	CHECK_EQ(out.report[0].usage, HIDPP_BRIGHTNESS_DOWN);
	hidpp(f, 0x08, 0x00, 0x00, 0x00, 0x00);
	session_feed(&s, f, sizeof(f), 120, &out);
	CHECK_EQ(out.reports, 1);
	CHECK_EQ(out.report[0].usage, 0x00e9);
	session_feed(&s, media_release, sizeof(media_release), 130, &out);
	CHECK_EQ(out.report[0].usage, 0);
}

static void test_a_media_key_held_through_a_noise_survey_is_not_revived(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_diverted(&s);
	session_feed(&s, media_volume_up, sizeof(media_volume_up), 100, &out);
	unifying_session_radio_resumed(&s, 1600);
	hidpp(f, 0x08, 0x00, 0x00, 0xc7, 0x00);
	session_feed(&s, f, sizeof(f), 1700, &out);
	hidpp(f, 0x08, 0x00, 0x00, 0x00, 0x00);
	session_feed(&s, f, sizeof(f), 1710, &out);
	CHECK_EQ(out.reports, 1);
	CHECK_EQ(out.report[0].usage, 0);
}

static void test_a_held_brightness_key_is_released_by_the_silence(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_diverted(&s);
	hidpp(f, 0x08, 0x00, 0x00, 0xc7, 0x00);
	session_feed(&s, f, sizeof(f), 100, &out);
	unifying_session_tick(&s, silence_after(&s, 100) + 1, false, 0, 0, &out);
	CHECK(out.release_all);
}

static void session_battery_answered(struct unifying_session *s, uint8_t index)
{
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_diverted(s);
	unifying_session_tick(s, 80, false, 0, 0, &out);
	hidpp(f, 0x00, 0x01, index, 0x00, 0x00);
	session_feed(s, f, sizeof(f), 90, &out);
}

static void session_battery_found(struct unifying_session *s)
{
	session_battery_answered(s, 0x07);
}

static void session_battery_read(struct unifying_session *s)
{
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_battery_found(s);
	unifying_session_tick(s, 100, false, 0, 0, &out);
	hidpp(f, 0x07, 0x01, 0x55, 0x32, 0x01);
	session_feed(s, f, sizeof(f), 110, &out);
}

static void test_the_battery_is_read_once_after_the_divert(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_diverted(&s);
	unifying_session_tick(&s, 80, false, 0, 0, &out);
	check_hidpp(&out, find_battery_request, sizeof(find_battery_request));
	hidpp(f, 0x00, 0x01, 0x07, 0x00, 0x00);
	session_feed(&s, f, sizeof(f), 90, &out);
	unifying_session_tick(&s, 100, false, 0, 0, &out);
	check_hidpp(&out, read_battery_request, sizeof(read_battery_request));
	hidpp(f, 0x07, 0x01, 0x55, 0x32, 0x01);
	session_feed(&s, f, sizeof(f), 110, &out);
	CHECK_EQ(s.battery.level, 85);
	CHECK_EQ(s.battery.charging, DEVICE_CHARGING_CHARGING);
	unifying_session_tick(&s, 120, false, 0, 0, &out);
	CHECK_EQ(out.hidpp_len, 0);
}

static void test_battery_events_update_the_level_and_charging(void)
{
	static const struct {
		uint8_t level, status;
		enum device_charging charging;
	} steps[] = {{80, 0x00, DEVICE_CHARGING_DISCHARGING},
		     {90, 0x02, DEVICE_CHARGING_CHARGING},
		     {100, 0x03, DEVICE_CHARGING_CHARGED},
		     {95, 0x04, DEVICE_CHARGING_CHARGING},
		     {95, 0x08, DEVICE_CHARGING_UNKNOWN}};
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_battery_read(&s);
	for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
		hidpp(f, 0x07, 0x00, steps[i].level, 0x00, steps[i].status);
		session_feed(&s, f, sizeof(f), 200 + i, &out);
		CHECK_EQ(out.reports, 0);
		CHECK_EQ(s.battery.level, steps[i].level);
		CHECK_EQ(s.battery.charging, steps[i].charging);
	}
}

static void test_a_charging_event_without_a_level_keeps_the_level_unknown(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_battery_read(&s);
	hidpp(f, 0x07, 0x00, 0x00, 0x00, 0x01);
	session_feed(&s, f, sizeof(f), 200, &out);
	CHECK_EQ(s.battery.level, DEVICE_NO_BATTERY);
	CHECK_EQ(s.battery.charging, DEVICE_CHARGING_CHARGING);
}

static void test_a_battery_event_before_the_lookup_is_ignored(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_diverted(&s);
	hidpp(f, 0x07, 0x00, 0x64, 0x32, 0x00);
	session_feed(&s, f, sizeof(f), 80, &out);
	CHECK_EQ(s.battery.level, DEVICE_NO_BATTERY);
}

static void test_a_keyboard_without_battery_status_is_asked_for_unified_battery(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_battery_answered(&s, 0x00);
	unifying_session_tick(&s, 100, false, 0, 0, &out);
	check_hidpp(&out, find_unified_battery_request, sizeof(find_unified_battery_request));
}

static void test_a_keyboard_without_a_battery_feature_is_not_asked_its_level(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_battery_answered(&s, 0x00);
	unifying_session_tick(&s, 100, false, 0, 0, &out);
	hidpp(f, 0x00, 0x01, 0x00, 0x00, 0x00);
	session_feed(&s, f, sizeof(f), 110, &out);
	unifying_session_tick(&s, 120, false, 0, 0, &out);
	CHECK_EQ(out.hidpp_len, 0);
	CHECK_EQ(s.battery.level, DEVICE_NO_BATTERY);
}

static void test_an_unanswered_battery_read_logs_no_divert_event(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint32_t taken = 0;
	uint64_t now;

	session_battery_found(&s);
	now = run_out_retries(&s, 100, &taken, read_battery_request,
			      sizeof(read_battery_request));
	tick_queue(&s, now, taken, &out);
	CHECK_EQ(out.hidpp_len, 0);
	CHECK(!out.hidpp_event.pending);
	CHECK_EQ(s.battery.level, DEVICE_NO_BATTERY);
}

static void session_reconnected_after_a_full_run(struct unifying_session *s)
{
	struct unifying_out out;

	session_battery_read(s);
	unifying_session_frame(s, UNIFYING_PIPE_RECEIVER, connect_10, sizeof(connect_10), 0, 200,
			       nonce, fake_aes, &out);
}

static void test_a_reconnect_keeps_the_battery_level(void)
{
	struct unifying_session s;

	session_reconnected_after_a_full_run(&s);
	CHECK_EQ(s.battery.level, 85);
}

static void test_a_reconnect_takes_the_connect_battery_event_at_once(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_reconnected_after_a_full_run(&s);
	hidpp(f, 0x07, 0x00, 0x32, 0x00, 0x00);
	session_feed(&s, f, sizeof(f), 210, &out);
	CHECK_EQ(s.battery.level, 50);
}

static void test_a_reconnect_reads_no_battery_after_the_divert(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_reconnected_after_a_full_run(&s);
	unifying_session_tick(&s, 210, false, 0, 0, &out);
	divert_reply(f, 0xc7);
	session_feed(&s, f, sizeof(f), 220, &out);
	unifying_session_tick(&s, 230, false, 0, 0, &out);
	divert_reply(f, 0xc8);
	session_feed(&s, f, sizeof(f), 240, &out);
	check_end(&out, HISTORY_DIVERT_FINISHED, 2, 0, 0);
	unifying_session_tick(&s, 250, false, 0, 0, &out);
	CHECK_EQ(out.hidpp_len, 0);
}

static void test_a_battery_fault_is_its_own_state(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_battery_read(&s);
	for (uint8_t status = 5; status <= 7; status++) {
		hidpp(f, 0x07, 0x00, 0x40, 0x00, status);
		session_feed(&s, f, sizeof(f), 200 + status, &out);
		CHECK_EQ(s.battery.charging, DEVICE_CHARGING_BATTERY_FAULT);
	}
}

static void test_pairing_a_keyboard_forgets_the_old_ones_battery(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];
	uint8_t request[sizeof(find_request)];

	session_battery_read(&s);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 200);
	request_1(f, 0xe1, 0x05);
	unifying_session_frame(&s, UNIFYING_PIPE_PAIRING, f, sizeof(f), 0, 210, nonce, fake_aes,
			       &out);
	request_2(f, 0xe1);
	unifying_session_frame(&s, unifying_place_pipe(1), f, sizeof(f), 0, 220, nonce, fake_aes,
			       &out);
	request_3(f, 0x00);
	unifying_session_frame(&s, unifying_place_pipe(1), f, sizeof(f), 0, 230, nonce, fake_aes,
			       &out);
	CHECK_EQ(out.event, UNIFYING_RX_PAIRED);
	CHECK_EQ(s.battery.level, DEVICE_NO_BATTERY);
	unifying_session_tick(&s, 240, false, 0, 0, &out);
	CHECK(out.send_led);
	unifying_session_led_queued(&s, out.leds, 0);
	unifying_session_tick(&s, 250, false, 0, 1, &out);
	memcpy(request, find_request, sizeof(request));
	request[2] = unifying_place_octet(1);
	seal(request, sizeof(request));
	check_hidpp(&out, request, sizeof(request));
}

static void session_holding_a_key(struct unifying_session *s)
{
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_paired(s);
	keystroke(f, 0x5f66f458, release);
	unifying_session_frame(s, PIPE_0, f, sizeof(f), 0, 10, nonce, fake_aes, &out);
	keystroke(f, 0x5f66f459, press_a);
	unifying_session_frame(s, PIPE_0, f, sizeof(f), 0, 20, nonce, fake_aes, &out);
	CHECK_EQ(out.reports, 1);
}

static void test_a_session_releases_a_held_key_once_after_twice_the_keep_alive(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint64_t limit;

	session_holding_a_key(&s);
	limit = 20 + 2 * (uint64_t)s.links[0].keepalive_ms;
	unifying_session_tick(&s, limit, false, 0, 0, &out);
	CHECK(!out.release_all);
	unifying_session_tick(&s, limit + 1, false, 0, 0, &out);
	CHECK(out.release_all);
	unifying_session_tick(&s, limit + 1000, false, 0, 0, &out);
	CHECK(!out.release_all);
}

static void test_a_session_releases_a_held_key_on_a_connect_frame_only(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t other[sizeof(connect_10)];

	session_holding_a_key(&s);
	memcpy(other, connect_10, sizeof(other));
	other[sizeof(other) - 1] ^= 0x10;
	unifying_session_frame(&s, UNIFYING_PIPE_RECEIVER, other, sizeof(other), 0, 30, nonce,
			       fake_aes, &out);
	CHECK(!out.release_all);
	unifying_session_frame(&s, UNIFYING_PIPE_RECEIVER, connect_10, sizeof(connect_10), 0, 40,
			       nonce, fake_aes, &out);
	CHECK(out.release_all);
}

static void test_the_first_frame_is_first_contact_counted_from_the_start(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_paired(&s);
	hear(&s, 700, &out);
	CHECK_EQ(out.heard, UNIFYING_HEARD_FIRST);
	CHECK_EQ(out.down_ms, 700);
	hear(&s, 800, &out);
	CHECK_EQ(out.heard, UNIFYING_HEARD_NONE);
}

static void test_silence_is_reported_once_and_the_next_frame_ends_it(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint64_t silent;

	session_paired(&s);
	hear(&s, 1000, &out);
	silent = silence_after(&s, 1000);
	unifying_session_tick(&s, silent - 1, false, 0, 0, &out);
	CHECK(!out.went_silent);
	unifying_session_tick(&s, silent, false, 0, 0, &out);
	CHECK(out.went_silent);
	CHECK_EQ(out.silent_ms, silent - 1000);
	unifying_session_tick(&s, silent + 10, false, 0, 0, &out);
	CHECK(!out.went_silent);
	hear(&s, silent + 5000, &out);
	CHECK_EQ(out.heard, UNIFYING_HEARD_AGAIN);
	CHECK_EQ(out.down_ms, 5000);
}

static void test_a_connect_frame_during_the_silence_makes_it_a_wake(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint64_t silent;

	session_paired(&s);
	hear(&s, 1000, &out);
	silent = silence_after(&s, 1000);
	unifying_session_tick(&s, silent, false, 0, 0, &out);
	unifying_session_frame(&s, UNIFYING_PIPE_RECEIVER, connect_10, sizeof(connect_10), 0,
			       silent + 100, nonce, fake_aes, &out);
	CHECK(out.reconnected);
	hear(&s, silent + 120, &out);
	CHECK_EQ(out.heard, UNIFYING_HEARD_WOKE);
	unifying_session_tick(&s, silence_after(&s, silent + 120), false, 0, 0, &out);
	hear(&s, silence_after(&s, silent + 120) + 50, &out);
	CHECK_EQ(out.heard, UNIFYING_HEARD_AGAIN);
}

static void test_a_connect_frame_while_connected_is_a_reconnect_not_a_wake(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t other[sizeof(connect_10)];

	session_paired(&s);
	hear(&s, 1000, &out);
	memcpy(other, connect_10, sizeof(other));
	other[sizeof(other) - 1] ^= 0x10;
	unifying_session_frame(&s, UNIFYING_PIPE_RECEIVER, other, sizeof(other), 0, 1010, nonce,
			       fake_aes, &out);
	CHECK(!out.reconnected);
	unifying_session_frame(&s, UNIFYING_PIPE_RECEIVER, connect_10, sizeof(connect_10), 0, 1020,
			       nonce, fake_aes, &out);
	CHECK(out.reconnected);
	hear(&s, 1030, &out);
	CHECK_EQ(out.heard, UNIFYING_HEARD_NONE);
}

static void test_an_unpaired_session_never_goes_silent(void)
{
	struct unifying_session s;
	struct unifying_out out;

	unifying_session_init(&s, &receiver, 0);
	hear(&s, 1000, &out);
	unifying_session_tick(&s, 100000, false, 0, 0, &out);
	CHECK(!out.went_silent);
}

static void test_a_radio_pause_postpones_the_silence(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_paired(&s);
	hear(&s, 1000, &out);
	unifying_session_radio_resumed(&s, 2500);
	unifying_session_tick(&s, silence_after(&s, 2500) - 1, false, 0, 0, &out);
	CHECK(!out.went_silent);
	CHECK_EQ(unifying_session_status(&s, silence_after(&s, 2500) - 1),
		 UNIFYING_STATUS_CONNECTED);
	unifying_session_tick(&s, silence_after(&s, 2500), false, 0, 0, &out);
	CHECK(out.went_silent);
}

static void test_a_keyboard_going_quiet_while_pairing_is_open_is_reported_silent(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_paired(&s);
	hear(&s, 1000, &out);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 1500);
	unifying_session_tick(&s, silence_after(&s, 1000) - 1, false, 0, 0, &out);
	CHECK(!out.went_silent);
	unifying_session_tick(&s, silence_after(&s, 1000), false, 0, 0, &out);
	CHECK(out.went_silent);
	CHECK_EQ(unifying_session_status(&s, silence_after(&s, 1000)), UNIFYING_STATUS_SILENT);
}

static void test_silence_after_a_pairing_counts_from_its_last_frame(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	unifying_session_init(&s, &receiver, 0);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 0);
	request_1(f, 0xe1, 0x05);
	unifying_session_frame(&s, UNIFYING_PIPE_PAIRING, f, sizeof(f), 0, 0, nonce, NULL, &out);
	unifying_session_tick(&s, 5, false, 0, 0, &out);
	request_2(f, 0xe1);
	unifying_session_frame(&s, PIPE_0, f, sizeof(f), 0, 10, nonce, fake_aes, &out);
	request_3(f, 0x00);
	unifying_session_frame(&s, PIPE_0, f, sizeof(f), 0, 20, nonce, fake_aes, &out);
	CHECK_EQ(out.event, UNIFYING_RX_PAIRED);
	unifying_session_tick(&s, silence_after(&s, 20) - 1, false, 0, 0, &out);
	CHECK(!out.went_silent);
	unifying_session_tick(&s, silence_after(&s, 20), false, 0, 0, &out);
	CHECK(out.went_silent);
}

static void test_silence_after_pairing_another_keyboard_counts_from_its_last_frame(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_paired(&s);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 0);
	unifying_session_tick(&s, 1, false, 0, 0, &out);
	request_1(f, 0xe1, 0x05);
	unifying_session_frame(&s, UNIFYING_PIPE_PAIRING, f, sizeof(f), 0, 2, nonce, NULL, &out);
	request_2(f, 0xe1);
	unifying_session_frame(&s, unifying_place_pipe(1), f, sizeof(f), 0, 10, nonce, fake_aes,
			       &out);
	request_3(f, 0x00);
	unifying_session_frame(&s, unifying_place_pipe(1), f, sizeof(f), 0, 20, nonce, fake_aes,
			       &out);
	CHECK_EQ(out.event, UNIFYING_RX_PAIRED);
	unifying_session_tick(&s, 30, false, 0, 0, &out);
	unifying_session_tick(&s, silence_after(&s, 20) - 1, false, 0, 0, &out);
	CHECK(!out.went_silent);
	unifying_session_tick(&s, silence_after(&s, 20), false, 0, 0, &out);
	CHECK(out.went_silent);
}

static void test_a_refused_keystroke_and_a_bad_checksum_are_invalid(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];
	uint8_t bad[sizeof(keep_alive_1100)];

	session_holding_a_key(&s);
	keystroke(f, 0x5f66f400, press_a);
	unifying_session_frame(&s, PIPE_0, f, sizeof(f), 0, 30, nonce, fake_aes, &out);
	CHECK(out.invalid);
	hear(&s, 40, &out);
	CHECK(!out.invalid);
	memcpy(bad, keep_alive_1100, sizeof(bad));
	bad[sizeof(bad) - 1] ^= 0x10;
	unifying_session_frame(&s, PIPE_0, bad, sizeof(bad), 0, 50, nonce, fake_aes,
			       &out);
	CHECK(out.invalid);
}

static void test_a_forged_keystroke_starts_the_attack_once(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_holding_a_key(&s);
	keystroke(f, 0x5f66f45a, forged);
	unifying_session_frame(&s, PIPE_0, f, sizeof(f), 0, 30, nonce, fake_aes, &out);
	CHECK(out.attack_started);
	CHECK(out.invalid);
	keystroke(f, 0x5f66f45b, forged);
	unifying_session_frame(&s, PIPE_0, f, sizeof(f), 0, 40, nonce, fake_aes, &out);
	CHECK(!out.attack_started);
	keystroke(f, 0x5f66f45c, forged);
	unifying_session_frame(&s, PIPE_0, f, sizeof(f), 0, 40 + UNIFYING_ATTACK_MS,
			       nonce, fake_aes, &out);
	CHECK(out.attack_started);
}

static void test_a_window_that_runs_out_is_reported_closed_once(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_paired(&s);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 0);
	unifying_session_tick(&s, PAIRWIN_MS - 1, false, 0, 0, &out);
	CHECK(!out.window_closed);
	unifying_session_tick(&s, PAIRWIN_MS, false, 0, 0, &out);
	CHECK(out.window_closed);
	unifying_session_tick(&s, PAIRWIN_MS + 10, false, 0, 0, &out);
	CHECK(!out.window_closed);
}

static void test_a_window_closed_by_pairing_is_not_reported(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	unifying_session_init(&s, &receiver, 0);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 0);
	request_1(f, 0xe1, 0x05);
	unifying_session_frame(&s, UNIFYING_PIPE_PAIRING, f, sizeof(f), 0, 0, nonce, NULL, &out);
	request_2(f, 0xe1);
	unifying_session_frame(&s, PIPE_0, f, sizeof(f), 0, 10, nonce, fake_aes, &out);
	request_3(f, 0x00);
	unifying_session_frame(&s, PIPE_0, f, sizeof(f), 0, 20, nonce, fake_aes, &out);
	CHECK_EQ(out.event, UNIFYING_RX_PAIRED);
	unifying_session_tick(&s, 30, false, 0, 0, &out);
	CHECK(!out.window_closed);
}

static void test_an_expired_attempt_reports_the_step_it_reached(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	unifying_session_init(&s, &receiver, 0);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 0);
	request_1(f, 0xe1, 0x05);
	unifying_session_frame(&s, UNIFYING_PIPE_PAIRING, f, sizeof(f), 0, 0, nonce, NULL, &out);
	unifying_session_tick(&s, UNIFYING_PAIRING_TIMEOUT_MS + 1, false, 0, 0, &out);
	CHECK(out.attempt_expired);
	CHECK_EQ(out.expired_step, 1);
}

static void test_a_window_is_not_reported_closed_while_an_attempt_finishes(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];
	uint64_t end = PAIRWIN_MS;

	unifying_session_init(&s, &receiver, 0);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 0);
	request_1(f, 0xe1, 0x05);
	unifying_session_frame(&s, UNIFYING_PIPE_PAIRING, f, sizeof(f), 0, end - 1, nonce, NULL,
			       &out);
	unifying_session_tick(&s, end, false, 0, 0, &out);
	CHECK(!out.window_closed);
	request_2(f, 0xe1);
	unifying_session_frame(&s, PIPE_0, f, sizeof(f), 0, end + 10, nonce, fake_aes,
			       &out);
	request_3(f, 0x00);
	unifying_session_frame(&s, PIPE_0, f, sizeof(f), 0, end + 20, nonce, fake_aes,
			       &out);
	CHECK_EQ(out.event, UNIFYING_RX_PAIRED);
	unifying_session_tick(&s, end + 30, false, 0, 0, &out);
	CHECK(!out.window_closed);
}

static void test_a_window_is_reported_closed_when_the_attempt_it_outlasted_expires(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];
	uint64_t last = PAIRWIN_MS - 1;

	session_paired(&s);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 0);
	request_1(f, 0xe1, 0x05);
	unifying_session_frame(&s, UNIFYING_PIPE_PAIRING, f, sizeof(f), 0, last, nonce, NULL, &out);
	unifying_session_tick(&s, PAIRWIN_MS, false, 0, 0, &out);
	CHECK(!out.window_closed);
	unifying_session_tick(&s, last + UNIFYING_PAIRING_TIMEOUT_MS + 1, false, 0, 0, &out);
	CHECK(out.attempt_expired);
	CHECK(out.window_closed);
	unifying_session_tick(&s, last + UNIFYING_PAIRING_TIMEOUT_MS + 2, false, 0, 0, &out);
	CHECK(!out.window_closed);
}

static void test_a_radio_pause_during_a_silence_leaves_the_keyboard_silent(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint64_t silent;

	session_paired(&s);
	hear(&s, 1000, &out);
	silent = silence_after(&s, 1000);
	unifying_session_tick(&s, silent, false, 0, 0, &out);
	unifying_session_radio_resumed(&s, silent + 500);
	CHECK_EQ(unifying_session_status(&s, silent + 600), UNIFYING_STATUS_SILENT);
}

static void test_a_status_read_before_the_radio_resumed_is_not_silent(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_paired(&s);
	hear(&s, 1000, &out);
	unifying_session_radio_resumed(&s, 2000);
	CHECK_EQ(unifying_session_status(&s, 1990), UNIFYING_STATUS_CONNECTED);
}

static void test_pairing_opened_at_boot_closes_once_the_paired_keyboard_is_heard(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_paired(&s);
	unifying_session_open_pairing(&s, PAIRWIN_BOOT, 0);
	CHECK(unifying_pairing_window_open(&s.pairing, 10));
	hear(&s, 100, &out);
	CHECK_EQ(unifying_session_status(&s, 110), UNIFYING_STATUS_CONNECTED);
	unifying_session_tick(&s, PAIRWIN_MS, false, 0, 0, &out);
	CHECK(!out.window_closed);
}

static void test_pairing_opened_on_request_stays_open_when_the_keyboard_is_heard(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_paired(&s);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 0);
	hear(&s, 100, &out);
	CHECK(unifying_pairing_window_open(&s.pairing, 110));
	CHECK_EQ(unifying_session_status(&s, 110), UNIFYING_STATUS_CONNECTED);
}

static void test_a_pairing_attempt_is_not_the_keyboard_connecting(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_paired(&s);
	unifying_session_open_pairing(&s, PAIRWIN_BOOT, 0);
	request_1(f, 0xe1, 0x05);
	unifying_session_frame(&s, UNIFYING_PIPE_PAIRING, f, sizeof(f), 0, 0, nonce, NULL, &out);
	request_2(f, 0xe1);
	unifying_session_frame(&s, unifying_place_pipe(1), f, sizeof(f), 0, 10, nonce, fake_aes,
			       &out);
	CHECK_EQ(out.event, UNIFYING_RX_ANSWERED_2);
	CHECK(unifying_pairing_window_open(&s.pairing, 20));
	unifying_session_tick(&s, 10 + UNIFYING_PAIRING_TIMEOUT_MS + 1, false, 0, 0, &out);
	CHECK(out.attempt_expired);
	hear(&s, 6000, &out);
	CHECK_EQ(unifying_session_status(&s, 6010), UNIFYING_STATUS_CONNECTED);
}

static void test_a_frame_with_a_bad_checksum_leaves_the_boot_window_open(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t bad[sizeof(keep_alive_1100)];

	session_paired(&s);
	unifying_session_open_pairing(&s, PAIRWIN_BOOT, 0);
	memcpy(bad, keep_alive_1100, sizeof(bad));
	bad[sizeof(bad) - 1] ^= 0x80;
	unifying_session_frame(&s, PIPE_0, bad, sizeof(bad), 0, 10, nonce, fake_aes,
			       &out);
	CHECK(unifying_pairing_window_open(&s.pairing, 20));
}

static void test_a_session_with_no_keyboard_stays_in_pairing(void)
{
	struct unifying_session s;
	struct unifying_out out;

	unifying_session_init(&s, &receiver, 0);
	unifying_session_open_pairing(&s, PAIRWIN_BOOT, 0);
	unifying_session_tick(&s, 10 * PAIRWIN_MS, false, 0, 0, &out);
	CHECK(!out.window_closed);
	CHECK(unifying_pairing_window_open(&s.pairing, 10 * PAIRWIN_MS));
	CHECK_EQ(unifying_session_status(&s, 10 * PAIRWIN_MS), UNIFYING_STATUS_NOT_PAIRED);
}

static void give_place_key(uint8_t record[UNIFYING_RECORD_LEN], uint8_t place)
{
	record[sizeof(((struct unifying_device *)0)->wpid)] = 0x10 + place;
}

static void session_with_places(struct unifying_session *s, uint8_t n)
{
	struct unifying_pairing done = open_pairing();
	uint8_t record[UNIFYING_RECORD_LEN];

	pair_fully(&done);
	unifying_record_of(&done, 0, record);
	unifying_session_init(s, &receiver, 0);
	for (uint8_t place = 0; place < n; place++) {
		give_place_key(record, place);
		unifying_session_restore(s, place, record);
	}
}

static void send(struct unifying_session *s, uint8_t place, const uint8_t *f, size_t len,
		 uint64_t now, struct unifying_out *out)
{
	unifying_session_frame(s, unifying_place_pipe(place), f, len, -60, now, nonce, fake_aes,
			       out);
}

static void send_key(struct unifying_session *s, uint8_t place, uint32_t counter,
		     const uint8_t plain[REPORT_KEYBOARD_LEN], uint64_t now,
		     struct unifying_out *out)
{
	uint8_t f[UNIFYING_FRAME_MAX];

	keystroke(f, counter, plain);
	send(s, place, f, sizeof(f), now, out);
}

static void test_the_lowest_stored_place_is_active_after_boot(void)
{
	struct unifying_session s;

	unifying_session_init(&s, &receiver, 0);
	CHECK_EQ(s.active, UNIFYING_NO_PLACE);
	session_with_places(&s, 3);
	CHECK_EQ(s.active, 0);
	CHECK_EQ(unifying_session_status(&s, 0), UNIFYING_STATUS_SILENT);
}

static void test_a_stored_keyboard_takes_over_with_a_key_press(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_with_places(&s, 2);
	hear(&s, 1000, &out);
	CHECK(out.from_active);
	send_key(&s, 1, 500, release, 1010, &out);
	CHECK_EQ(s.active, 0);
	CHECK(!out.from_active);
	CHECK_EQ(out.reports, 0);
	CHECK_EQ(out.heard, UNIFYING_HEARD_NONE);
	send_key(&s, 1, 501, press_a, 1020, &out);
	CHECK_EQ(s.active, 1);
	CHECK(out.from_active);
	CHECK_EQ(out.reports, 1);
	CHECK(reports_key_held(&out.report[0], 0x04));
	CHECK_EQ(out.heard, UNIFYING_HEARD_AGAIN);
	CHECK_EQ(out.down_ms, 20);
	CHECK_MEM(aes_seen_key, s.pairing.devices[1].key, UNIFYING_KEY_LEN);
}

static void test_a_takeover_releases_the_keys_the_old_keyboard_held(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_with_places(&s, 2);
	send_key(&s, 0, 100, release, 1000, &out);
	send_key(&s, 0, 101, press_a, 1010, &out);
	CHECK_EQ(out.reports, 1);
	send_key(&s, 1, 500, release, 1020, &out);
	CHECK(!out.release_all);
	send_key(&s, 1, 501, press_a, 1030, &out);
	CHECK(out.release_all);
	CHECK_EQ(out.reports, 1);
	CHECK(!s.links[0].keyboard_down);
}

static void test_a_takeover_with_nothing_held_releases_nothing(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_with_places(&s, 2);
	hear(&s, 1000, &out);
	send_key(&s, 1, 500, release, 1010, &out);
	send_key(&s, 1, 501, press_a, 1020, &out);
	CHECK(!out.release_all);
}

static void test_a_keep_alive_does_not_take_over_from_a_connected_keyboard(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_with_places(&s, 2);
	hear(&s, 1000, &out);
	send(&s, 1, keep_alive_1100, sizeof(keep_alive_1100), 1010, &out);
	CHECK_EQ(s.active, 0);
	CHECK(!out.from_active);
	CHECK_EQ(s.heard_ms, 1000);
}

static void test_any_valid_frame_takes_over_before_the_active_keyboard_is_heard(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t bad[sizeof(keep_alive_1100)];

	session_with_places(&s, 2);
	memcpy(bad, keep_alive_1100, sizeof(bad));
	bad[sizeof(bad) - 1] ^= 0x10;
	send(&s, 1, bad, sizeof(bad), 990, &out);
	CHECK_EQ(s.active, 0);
	CHECK(out.invalid);
	send(&s, 1, keep_alive_1100, sizeof(keep_alive_1100), 1000, &out);
	CHECK_EQ(s.active, 1);
	CHECK_EQ(out.heard, UNIFYING_HEARD_FIRST);
	CHECK_EQ(out.down_ms, 1000);
	CHECK_EQ(unifying_session_status(&s, 1000), UNIFYING_STATUS_CONNECTED);
}

static void test_any_valid_frame_takes_over_from_a_silent_keyboard(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint64_t silent_at;

	session_with_places(&s, 2);
	hear(&s, 1000, &out);
	silent_at = 1000 + 2 * (uint64_t)s.links[0].keepalive_ms;
	unifying_session_tick(&s, silent_at, false, 0, 0, &out);
	CHECK(out.went_silent);
	send(&s, 1, keep_alive_1100, sizeof(keep_alive_1100), silent_at + 10, &out);
	CHECK_EQ(s.active, 1);
	CHECK_EQ(out.heard, UNIFYING_HEARD_AGAIN);
	CHECK_EQ(out.down_ms, 10);
}

static void test_a_place_outside_the_table_changes_nothing(void)
{
	struct unifying_session s, before;
	struct unifying_out out;
	uint8_t record[UNIFYING_RECORD_LEN];
	uint8_t address[DIAG_ADDR_LEN];
	static const uint8_t zero[UNIFYING_RECORD_LEN];

	session_with_places(&s, 2);
	unifying_record_of(&s.pairing, 0, record);
	memcpy(&before, &s, sizeof(s));
	unifying_session_restore(&s, UNIFYING_PLACES, record);
	CHECK(memcmp(&s, &before, sizeof(s)) == 0);
	unifying_session_forget(&s, UNIFYING_NO_PLACE, 2000, &out);
	CHECK(memcmp(&s, &before, sizeof(s)) == 0);
	CHECK(!out.release_all);
	CHECK(!out.window_opened);
	CHECK(!out.set_channel);
	CHECK_EQ(out.pipes, unifying_listen_pipes(&s.pairing));
	memset(record, 0xff, sizeof(record));
	unifying_record_of(&s.pairing, UNIFYING_PLACES, record);
	CHECK_MEM(record, zero, sizeof(record));
	memset(address, 0xff, sizeof(address));
	unifying_list_address(&s.pairing, UNIFYING_PLACES, address);
	CHECK_MEM(address, zero, sizeof(address));
}

static void test_a_media_key_takes_over_and_its_release_does_not(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_with_places(&s, 2);
	hear(&s, 1000, &out);
	send(&s, 1, media_release, sizeof(media_release), 1010, &out);
	CHECK_EQ(s.active, 0);
	CHECK_EQ(out.reports, 0);
	send(&s, 1, media_volume_up, sizeof(media_volume_up), 1020, &out);
	CHECK_EQ(s.active, 1);
	CHECK_EQ(out.reports, 1);
	CHECK_EQ(out.report[0].usage, 0xe9);
}

static void test_each_place_keeps_its_counter_across_takeovers(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_with_places(&s, 2);
	send_key(&s, 0, 100, release, 1000, &out);
	send_key(&s, 0, 101, press_a, 1010, &out);
	send_key(&s, 0, 102, release, 1020, &out);
	send_key(&s, 1, 500, release, 1030, &out);
	send_key(&s, 1, 501, press_a, 1040, &out);
	send_key(&s, 1, 502, release, 1050, &out);
	CHECK_EQ(s.active, 1);
	send_key(&s, 0, 101, press_a, 1060, &out);
	CHECK(out.invalid);
	CHECK_EQ(s.active, 1);
	CHECK_EQ(out.reports, 0);
	send_key(&s, 0, 103, press_a, 1070, &out);
	CHECK_EQ(s.active, 0);
	CHECK_EQ(out.reports, 1);
}

static void test_a_connect_frame_resets_only_its_own_places_counter(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t connect[sizeof(connect_10)];

	session_with_places(&s, 2);
	send_key(&s, 0, 100, release, 1000, &out);
	send_key(&s, 1, 500, release, 1010, &out);
	CHECK(s.links[0].synced);
	CHECK(s.links[1].synced);
	memcpy(connect, connect_10, sizeof(connect));
	connect[0] = unifying_place_octet(1);
	seal(connect, sizeof(connect));
	unifying_session_frame(&s, UNIFYING_PIPE_RECEIVER, connect, sizeof(connect), 0, 1020, nonce,
			       fake_aes, &out);
	CHECK(!s.links[1].synced);
	CHECK(s.links[0].synced);
	CHECK(!out.reconnected);
	CHECK_EQ(s.active, 0);
}

static void test_a_new_pairing_takes_a_place_beside_the_stored_one(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];
	uint8_t with_place_1;

	session_with_places(&s, 1);
	CHECK(unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 0));
	with_place_1 = unifying_listen_pipes(&s.pairing) | 1u << unifying_place_pipe(1);
	request_1(f, 0xe1, 0x05);
	unifying_session_frame(&s, UNIFYING_PIPE_PAIRING, f, sizeof(f), 0, 10, nonce, fake_aes,
			       &out);
	CHECK_EQ(out.event, UNIFYING_RX_ANSWERED_1);
	CHECK(out.listen);
	CHECK_EQ(out.pipes, with_place_1);
	CHECK_EQ(out.reply[7], unifying_place_octet(1));
	request_2(f, 0xe1);
	send(&s, 1, f, sizeof(f), 20, &out);
	CHECK_EQ(out.event, UNIFYING_RX_ANSWERED_2);
	CHECK_EQ(out.reply_pipe, unifying_place_pipe(1));
	CHECK(!out.from_active);
	request_3(f, 0x00);
	send(&s, 1, f, sizeof(f), 30, &out);
	CHECK_EQ(out.event, UNIFYING_RX_PAIRED);
	CHECK_EQ(out.heard, UNIFYING_HEARD_FIRST);
	CHECK_EQ(out.down_ms, 30);
	CHECK_EQ(out.paired_place, 1);
	CHECK_EQ(s.active, 1);
	CHECK(s.pairing.stored[0]);
}

static void test_an_expired_attempt_stops_listening_on_its_place(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];
	uint8_t before;

	session_with_places(&s, 1);
	before = unifying_listen_pipes(&s.pairing);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 0);
	request_1(f, 0xe1, 0x05);
	unifying_session_frame(&s, UNIFYING_PIPE_PAIRING, f, sizeof(f), 0, 10, nonce, fake_aes,
			       &out);
	unifying_session_tick(&s, 11 + UNIFYING_PAIRING_TIMEOUT_MS, false, 0, 0, &out);
	CHECK(out.attempt_expired);
	CHECK(out.listen);
	CHECK_EQ(out.pipes, before);
}

static void test_pairing_does_not_open_on_a_full_session(void)
{
	struct unifying_session s;

	session_with_places(&s, UNIFYING_PLACES);
	CHECK(!unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 0));
	CHECK(!unifying_pairing_window_open(&s.pairing, 10));
}

static void test_forgetting_another_place_leaves_the_active_keyboard(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_with_places(&s, 3);
	hear(&s, 1000, &out);
	unifying_session_forget(&s, 1, 1010, &out);
	CHECK(!out.release_all);
	CHECK(!out.set_channel);
	CHECK(!out.window_opened);
	CHECK(out.listen);
	CHECK_EQ(out.pipes, unifying_listen_pipes(&s.pairing));
	CHECK(!(out.pipes & 1u << unifying_place_pipe(1)));
	CHECK_EQ(s.active, 0);
	CHECK(s.heard);
	CHECK(!s.pairing.stored[1]);
	CHECK(!unifying_pairing_window_open(&s.pairing, 1010));
	CHECK(!unifying_session_frame(&s, unifying_place_pipe(1), keep_alive_1100,
				      sizeof(keep_alive_1100), -60, 1020, nonce, fake_aes, &out));
}

static void test_forgetting_the_active_place_makes_the_lowest_stored_one_active(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_with_places(&s, 3);
	hear(&s, 1000, &out);
	unifying_session_forget(&s, 0, 1010, &out);
	CHECK(out.release_all);
	CHECK(!out.window_opened);
	CHECK_EQ(s.active, 1);
	CHECK(!s.heard);
	CHECK_EQ(unifying_session_status(&s, 1010), UNIFYING_STATUS_SILENT);
	CHECK(!unifying_pairing_window_open(&s.pairing, 1010));
}

static void test_a_takeover_drops_a_reply_queued_for_the_old_keyboard(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_with_places(&s, 2);
	hear(&s, 1000, &out);
	unifying_session_tick(&s, 1001, true, 0x02, 0, &out);
	CHECK(out.send_led);
	unifying_session_led_queued(&s, out.leds, 0);
	send_key(&s, 1, 500, release, 1010, &out);
	CHECK(!out.listen);
	send_key(&s, 1, 501, press_a, 1020, &out);
	CHECK(out.listen);
	CHECK_EQ(out.pipes, unifying_listen_pipes(&s.pairing));
	unifying_session_replies_dropped(&s);
	unifying_session_tick(&s, 1030, false, 0x02, 0, &out);
	CHECK(out.send_led);
	CHECK_EQ(out.leds, 0x02);
}

static void test_hidpp_requests_carry_the_active_places_octet(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_with_places(&s, 2);
	send(&s, 1, keep_alive_1100, sizeof(keep_alive_1100), 1000, &out);
	CHECK_EQ(s.active, 1);
	unifying_session_tick(&s, 1001, false, 0, 0, &out);
	CHECK(out.send_led);
	unifying_session_led_queued(&s, out.leds, 0);
	unifying_session_tick(&s, 1002, false, 0, 1, &out);
	CHECK(out.hidpp_len != 0);
	CHECK_EQ(out.hidpp[2], unifying_place_octet(1));
}

static void test_the_old_keyboard_does_not_take_back_a_just_paired_one(void)
{
	struct unifying_session s;
	struct unifying_out out;
	uint8_t f[UNIFYING_FRAME_MAX];

	session_with_places(&s, 1);
	hear(&s, 1000, &out);
	unifying_session_open_pairing(&s, PAIRWIN_REQUEST, 1005);
	request_1(f, 0xe1, 0x05);
	unifying_session_frame(&s, UNIFYING_PIPE_PAIRING, f, sizeof(f), 0, 1010, nonce, fake_aes,
			       &out);
	request_2(f, 0xe1);
	send(&s, 1, f, sizeof(f), 1020, &out);
	request_3(f, 0x00);
	send(&s, 1, f, sizeof(f), 1030, &out);
	CHECK_EQ(out.event, UNIFYING_RX_PAIRED);
	CHECK_EQ(out.heard, UNIFYING_HEARD_AGAIN);
	CHECK_EQ(out.down_ms, 30);
	CHECK(out.from_active);
	CHECK_EQ(s.active, 1);
	hear(&s, 1040, &out);
	CHECK_EQ(s.active, 1);
	CHECK(!out.from_active);
	CHECK_EQ(unifying_session_status(&s, 1040), UNIFYING_STATUS_CONNECTED);
}

static void test_a_takeover_forgets_the_battery_and_restarts_hidpp(void)
{
	struct unifying_session s;
	struct unifying_out out;
	struct unifying_pairing done = open_pairing();
	uint8_t record[UNIFYING_RECORD_LEN];
	uint8_t request[sizeof(find_request)];

	session_battery_read(&s);
	CHECK(s.battery.level != DEVICE_NO_BATTERY);
	pair_fully(&done);
	unifying_record_of(&done, 0, record);
	give_place_key(record, 1);
	unifying_session_restore(&s, 1, record);
	send_key(&s, 1, 500, release, 200, &out);
	send_key(&s, 1, 501, press_a, 210, &out);
	CHECK_EQ(s.active, 1);
	CHECK_EQ(s.battery.level, DEVICE_NO_BATTERY);
	unifying_session_tick(&s, 220, false, 0, 0, &out);
	CHECK(out.send_led);
	unifying_session_led_queued(&s, out.leds, 0);
	unifying_session_tick(&s, 230, false, 0, 1, &out);
	memcpy(request, find_request, sizeof(request));
	request[2] = unifying_place_octet(1);
	seal(request, sizeof(request));
	check_hidpp(&out, request, sizeof(request));
}

static void test_a_frame_from_an_inactive_keyboard_leaves_the_signal(void)
{
	struct unifying_session s;
	struct unifying_out out;

	session_with_places(&s, 2);
	hear(&s, 1000, &out);
	unifying_session_frame(&s, unifying_place_pipe(1), keep_alive_1100,
			       sizeof(keep_alive_1100), -30, 1010, nonce, fake_aes, &out);
	CHECK_EQ(s.rssi, -60);
	CHECK_EQ(s.active, 0);
}

static void test_stored_places_with_one_id_and_serial_have_their_own_list_addresses(void)
{
	struct unifying_session s;
	uint8_t first[DIAG_ADDR_LEN];
	uint8_t second[DIAG_ADDR_LEN];

	session_with_places(&s, 2);
	unifying_list_address(&s.pairing, 0, first);
	unifying_list_address(&s.pairing, 1, second);
	CHECK_MEM(first, s.pairing.devices[0].wpid, sizeof(s.pairing.devices[0].wpid));
	CHECK_MEM(&first[2], s.pairing.devices[0].serial, UNIFYING_SERIAL_LEN);
	CHECK_EQ(first[2 + UNIFYING_SERIAL_LEN], unifying_place_octet(0));
	CHECK(memcmp(first, second, sizeof(first)) != 0);
	CHECK_EQ(unifying_find_place(&s.pairing, first), 0);
	CHECK_EQ(unifying_find_place(&s.pairing, second), 1);
}

static void test_a_list_address_of_an_unstored_place_finds_no_place(void)
{
	struct unifying_session s;
	uint8_t addr[DIAG_ADDR_LEN];

	session_with_places(&s, 1);
	unifying_list_address(&s.pairing, 1, addr);
	CHECK_EQ(unifying_find_place(&s.pairing, addr), UNIFYING_NO_PLACE);
	unifying_list_address(&s.pairing, 0, addr);
	addr[2 + UNIFYING_SERIAL_LEN] = unifying_place_octet(1);
	CHECK_EQ(unifying_find_place(&s.pairing, addr), UNIFYING_NO_PLACE);
}

static void test_the_prefix_table_has_the_pairing_receiver_and_place_octets(void)
{
	uint8_t prefixes[UNIFYING_PIPES];

	unifying_prefixes(prefixes);
	CHECK_EQ(prefixes[UNIFYING_PIPE_PAIRING], unifying_pairing_address[4]);
	CHECK_EQ(prefixes[UNIFYING_PIPE_RECEIVER], 0x00);
	for (uint8_t place = 0; place < UNIFYING_PLACES; place++) {
		CHECK_EQ(prefixes[unifying_place_pipe(place)], unifying_place_octet(place));
	}
}

int main(void)
{
	RUN(test_checksum_is_the_negated_sum);
	RUN(test_checksum_check_tolerates_a_wrong_low_bit);
	RUN(test_key_permutes_and_masks_the_key_data);
	RUN(test_request_1_is_answered_with_the_assigned_address);
	RUN(test_request_1_without_the_unifying_bit_is_refused);
	RUN(test_request_1_from_a_mouse_is_refused);
	RUN(test_request_2_is_answered_with_the_nonce_and_derives_the_key);
	RUN(test_request_3_gets_the_final_response_and_completes);
	RUN(test_a_paired_device_holds_what_the_keyboard_reported_about_itself);
	RUN(test_a_name_longer_than_the_frame_holds_is_cut);
	RUN(test_request_3_is_accepted_though_its_id_byte_is_zero);
	RUN(test_a_repeated_request_gets_no_second_reply);
	RUN(test_a_request_out_of_order_or_with_another_id_is_ignored);
	RUN(test_a_frame_with_a_bad_checksum_is_ignored);
	RUN(test_request_1_with_a_new_id_restarts_with_a_fresh_nonce);
	RUN(test_an_attempt_silent_for_the_timeout_returns_to_idle);
	RUN(test_a_completed_pairing_does_not_time_out);
	RUN(test_encrypted_keyboard_frame_is_recognized);
	RUN(test_pairing_address_and_channels_are_the_fixed_ones);
	RUN(test_device_address_is_the_base_then_the_device_octet);
	RUN(test_idle_sweep_steps_through_the_pairing_channels);
	RUN(test_sweep_holds_the_channel_from_the_first_request);
	RUN(test_sweep_resumes_once_an_attempt_expires);
	RUN(test_request_1_after_the_window_is_ignored);
	RUN(test_an_attempt_started_in_the_window_finishes_after_it);
	RUN(test_request_1_before_pairing_opens_is_ignored);
	RUN(test_the_window_runs_30_s_from_opening);
	RUN(test_pairing_closes_the_window);
	RUN(test_a_paired_dongle_holds_its_channel_with_pairing_closed_or_open);
	RUN(test_a_restored_device_is_paired_and_a_new_request_still_starts_an_attempt);
	RUN(test_a_refused_request_keeps_the_restored_device_paired);
	RUN(test_an_expired_attempt_returns_to_the_restored_device);
	RUN(test_an_expired_attempt_can_be_retried_with_its_id);
	RUN(test_the_paired_device_is_kept_while_an_attempt_runs);
	RUN(test_a_refused_request_during_an_attempt_keeps_the_paired_device);
	RUN(test_request_1_with_the_id_of_a_completed_attempt_starts_a_new_one);
	RUN(test_a_completed_attempt_takes_the_next_place_and_keeps_the_first);
	RUN(test_a_place_has_a_fixed_pipe_and_octet);
	RUN(test_request_1_is_answered_with_the_lowest_empty_places_address);
	RUN(test_pairing_does_not_open_with_every_place_stored);
	RUN(test_a_forgotten_place_is_the_next_one_paired);
	RUN(test_forgetting_the_only_place_leaves_no_device);
	RUN(test_the_radio_listens_on_stored_places_and_the_attempts);
	RUN(test_a_record_is_kept_per_place);
	RUN(test_a_window_that_ran_out_stays_closed_past_32_bits_of_uptime);
	RUN(test_keystroke_passes_the_key_and_the_counter_block_to_aes);
	RUN(test_keystroke_report_is_the_decrypted_modifier_and_keys);
	RUN(test_a_counter_below_the_next_expected_one_is_refused);
	RUN(test_counters_inside_the_window_are_accepted_up_to_its_edge);
	RUN(test_a_far_jump_alone_is_not_typed);
	RUN(test_a_far_jump_confirmed_by_the_next_counter_is_typed);
	RUN(test_a_far_jump_followed_by_another_counter_is_dropped);
	RUN(test_a_refused_counter_keeps_the_jump_candidate);
	RUN(test_under_attack_a_lone_valid_frame_types_nothing);
	RUN(test_under_attack_a_consecutive_pair_types_the_first);
	RUN(test_under_attack_a_pair_with_a_gap_types_nothing);
	RUN(test_under_attack_a_jump_is_refused_even_with_its_successor);
	RUN(test_under_attack_a_pair_ending_at_the_window_edge_types_the_first);
	RUN(test_a_marker_failure_drops_the_pending_frame);
	RUN(test_a_frame_pending_when_the_attack_ends_is_dropped);
	RUN(test_a_forged_frame_after_a_marker_failure_does_not_raise_the_counter);
	RUN(test_under_attack_a_press_confirmed_by_its_release_types_both);
	RUN(test_under_attack_a_lone_release_is_typed_at_once);
	RUN(test_a_first_lone_press_after_reset_types_nothing);
	RUN(test_a_first_press_confirmed_by_the_next_counter_types_both);
	RUN(test_a_first_release_after_reset_is_typed_at_once);
	RUN(test_counters_near_the_top_are_refused_before_aes);
	RUN(test_with_no_counter_an_attack_refuses_every_press);
	RUN(test_with_no_counter_an_attack_still_types_a_release);
	RUN(test_after_a_reconnect_a_counter_below_the_old_one_is_confirmed_by_the_next);
	RUN(test_a_lone_press_after_a_reconnect_is_held);
	RUN(test_a_reconnect_drops_a_held_jump);
	RUN(test_a_reconnect_releases_held_keys);
	RUN(test_a_reconnect_with_no_key_held_releases_nothing);
	RUN(test_other_receiver_frames_leave_the_counter);
	RUN(test_an_attack_after_a_reconnect_refuses_every_press);
	RUN(test_the_first_counter_may_be_zero);
	RUN(test_a_plaintext_without_the_marker_byte_is_rejected_and_keeps_the_counter);
	RUN(test_an_unencrypted_keyboard_frame_is_refused);
	RUN(test_media_frames_carry_the_usage_in_bytes_2_and_3);
	RUN(test_a_media_frame_with_a_bad_checksum_or_another_type_is_refused);
	RUN(test_the_led_frame_carries_the_led_bits);
	RUN(test_keep_alive_frames_set_the_timeout);
	RUN(test_other_frames_leave_the_timeout_but_count_as_heard);
	RUN(test_a_held_key_goes_stuck_after_twice_the_timeout_once);
	RUN(test_a_frame_heard_in_time_postpones_the_stuck_key);
	RUN(test_nothing_held_is_never_silent);
	RUN(test_a_release_report_clears_the_held_state);
	RUN(test_a_known_counter_refuses_counters_below_it_and_bounds_the_window);
	RUN(test_the_record_is_the_wpid_the_key_then_the_keyboards_own_report);
	RUN(test_a_restored_record_pairs_the_device);
	RUN(test_no_led_frame_is_due_before_a_change_or_resend);
	RUN(test_an_led_change_is_sent_at_once);
	RUN(test_the_first_led_state_is_sent_even_when_it_is_zero);
	RUN(test_changes_back_to_the_sent_state_send_nothing_more);
	RUN(test_a_resend_sends_the_wanted_state);
	RUN(test_an_led_frame_not_queued_stays_due);
	RUN(test_no_led_frame_is_due_without_a_paired_device);
	RUN(test_a_session_without_a_record_is_not_paired);
	RUN(test_a_forgotten_keyboard_leaves_the_session_unpaired_and_pairing_open);
	RUN(test_a_session_answers_pairing_on_the_frames_pipe);
	RUN(test_a_refused_request_gets_no_reply_from_the_session);
	RUN(test_completing_pairing_through_the_session_closes_the_window);
	RUN(test_a_connect_frame_on_the_receiver_pipe_resends_the_leds);
	RUN(test_a_forgotten_keyboard_still_sending_is_ignored);
	RUN(test_a_heard_paired_keyboard_is_connected_then_silent);
	RUN(test_the_tick_moves_the_channel_while_pairing_is_open);
	RUN(test_a_move_sets_the_channel_and_holds_while_pairing_is_closed);
	RUN(test_a_stored_keyboard_keeps_its_channel_while_pairing_is_open);
	RUN(test_a_move_is_refused_while_an_attempt_outlasts_the_window);
	RUN(test_a_stored_keyboard_has_a_movable_home);
	RUN(test_a_move_holds_keys_and_silence_while_the_keyboard_searches);
	RUN(test_hearing_the_keyboard_after_a_move_ends_the_wait);
	RUN(test_a_move_to_the_current_channel_keeps_the_silence_timer);
	RUN(test_a_move_off_the_pairing_channels_is_refused);
	RUN(test_a_move_needs_a_paired_keyboard);
	RUN(test_a_move_works_on_a_silent_paired_keyboard);
	RUN(test_a_session_types_a_keystroke_from_the_device_pipe);
	RUN(test_a_heard_keyboard_is_asked_where_its_reprogrammable_controls_are);
	RUN(test_the_brightness_keys_are_diverted_in_turn_and_reported_once);
	RUN(test_an_unanswered_lookup_is_sent_again_then_given_up);
	RUN(test_an_unanswered_divert_reports_its_control);
	RUN(test_a_keyboard_without_reprogrammable_controls_is_reported_and_left_alone);
	RUN(test_a_refused_divert_is_counted_and_moves_on_to_the_other_key);
	RUN(test_diverted_brightness_keys_type_the_brightness_usages);
	RUN(test_an_fn_event_types_nothing);
	RUN(test_a_brightness_event_before_the_feature_is_found_types_nothing);
	RUN(test_a_connect_frame_diverts_again_without_a_second_lookup);
	RUN(test_no_divert_request_goes_out_during_a_pairing_attempt);
	RUN(test_a_media_key_held_through_a_noise_survey_is_not_revived);
	RUN(test_a_held_brightness_key_is_released_by_the_silence);
	RUN(test_the_battery_is_read_once_after_the_divert);
	RUN(test_battery_events_update_the_level_and_charging);
	RUN(test_a_charging_event_without_a_level_keeps_the_level_unknown);
	RUN(test_a_battery_event_before_the_lookup_is_ignored);
	RUN(test_a_keyboard_without_battery_status_is_asked_for_unified_battery);
	RUN(test_a_keyboard_without_a_battery_feature_is_not_asked_its_level);
	RUN(test_an_unanswered_battery_read_logs_no_divert_event);
	RUN(test_a_reconnect_keeps_the_battery_level);
	RUN(test_a_reconnect_takes_the_connect_battery_event_at_once);
	RUN(test_a_reconnect_reads_no_battery_after_the_divert);
	RUN(test_a_battery_fault_is_its_own_state);
	RUN(test_pairing_a_keyboard_forgets_the_old_ones_battery);
	RUN(test_an_led_change_waits_until_the_queued_frame_is_taken);
	RUN(test_resends_while_a_frame_is_queued_are_sent_once_after_it_is_taken);
	RUN(test_a_dropped_led_frame_is_sent_again);
	RUN(test_a_divert_request_waits_behind_a_queued_led_frame);
	RUN(test_a_request_the_sleeping_keyboard_never_takes_is_not_sent_again);
	RUN(test_a_dropped_request_is_sent_again_without_counting_a_try);
	RUN(test_a_wake_sends_the_divert_once);
	RUN(test_releasing_a_brightness_key_leaves_a_held_media_key_down);
	RUN(test_a_session_releases_a_held_key_once_after_twice_the_keep_alive);
	RUN(test_a_session_releases_a_held_key_on_a_connect_frame_only);
	RUN(test_the_first_frame_is_first_contact_counted_from_the_start);
	RUN(test_silence_is_reported_once_and_the_next_frame_ends_it);
	RUN(test_a_connect_frame_during_the_silence_makes_it_a_wake);
	RUN(test_a_connect_frame_while_connected_is_a_reconnect_not_a_wake);
	RUN(test_an_unpaired_session_never_goes_silent);
	RUN(test_a_radio_pause_postpones_the_silence);
	RUN(test_a_keyboard_going_quiet_while_pairing_is_open_is_reported_silent);
	RUN(test_silence_after_a_pairing_counts_from_its_last_frame);
	RUN(test_a_refused_keystroke_and_a_bad_checksum_are_invalid);
	RUN(test_a_forged_keystroke_starts_the_attack_once);
	RUN(test_a_window_that_runs_out_is_reported_closed_once);
	RUN(test_a_window_closed_by_pairing_is_not_reported);
	RUN(test_an_expired_attempt_reports_the_step_it_reached);
	RUN(test_a_window_is_not_reported_closed_while_an_attempt_finishes);
	RUN(test_a_window_is_reported_closed_when_the_attempt_it_outlasted_expires);
	RUN(test_a_radio_pause_during_a_silence_leaves_the_keyboard_silent);
	RUN(test_a_status_read_before_the_radio_resumed_is_not_silent);
	RUN(test_pairing_opened_at_boot_closes_once_the_paired_keyboard_is_heard);
	RUN(test_pairing_opened_on_request_stays_open_when_the_keyboard_is_heard);
	RUN(test_a_pairing_attempt_is_not_the_keyboard_connecting);
	RUN(test_a_frame_with_a_bad_checksum_leaves_the_boot_window_open);
	RUN(test_a_session_with_no_keyboard_stays_in_pairing);
	RUN(test_silence_after_pairing_another_keyboard_counts_from_its_last_frame);
	RUN(test_the_lowest_stored_place_is_active_after_boot);
	RUN(test_a_stored_keyboard_takes_over_with_a_key_press);
	RUN(test_a_takeover_releases_the_keys_the_old_keyboard_held);
	RUN(test_a_takeover_with_nothing_held_releases_nothing);
	RUN(test_a_keep_alive_does_not_take_over_from_a_connected_keyboard);
	RUN(test_any_valid_frame_takes_over_before_the_active_keyboard_is_heard);
	RUN(test_any_valid_frame_takes_over_from_a_silent_keyboard);
	RUN(test_a_place_outside_the_table_changes_nothing);
	RUN(test_a_media_key_takes_over_and_its_release_does_not);
	RUN(test_each_place_keeps_its_counter_across_takeovers);
	RUN(test_a_connect_frame_resets_only_its_own_places_counter);
	RUN(test_a_new_pairing_takes_a_place_beside_the_stored_one);
	RUN(test_an_expired_attempt_stops_listening_on_its_place);
	RUN(test_pairing_does_not_open_on_a_full_session);
	RUN(test_forgetting_another_place_leaves_the_active_keyboard);
	RUN(test_forgetting_the_active_place_makes_the_lowest_stored_one_active);
	RUN(test_a_takeover_drops_a_reply_queued_for_the_old_keyboard);
	RUN(test_hidpp_requests_carry_the_active_places_octet);
	RUN(test_the_old_keyboard_does_not_take_back_a_just_paired_one);
	RUN(test_a_takeover_forgets_the_battery_and_restarts_hidpp);
	RUN(test_a_frame_from_an_inactive_keyboard_leaves_the_signal);
	RUN(test_stored_places_with_one_id_and_serial_have_their_own_list_addresses);
	RUN(test_a_list_address_of_an_unstored_place_finds_no_place);
	RUN(test_the_prefix_table_has_the_pairing_receiver_and_place_octets);
	return TEST_EXIT();
}
