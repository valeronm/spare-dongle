#include "btpolicy.h"
#include "test.h"

static struct btpolicy policy;

static struct btpolicy_advert stored_advert(uint8_t last)
{
	return (struct btpolicy_advert){
		.addr = {1, 1, 2, 3, 4, 5, last},
		.connectable = true,
		.stored = true,
		.kind = DEVICE_KIND_KEYBOARD,
	};
}

static struct btpolicy_advert new_advert(uint8_t last)
{
	return (struct btpolicy_advert){
		.addr = {1, 9, 9, 9, 9, 9, last},
		.connectable = true,
		.hid_service = true,
		.kind = DEVICE_KIND_KEYBOARD,
	};
}

static void a_stored_keyboard_is_wanted_whatever_its_advert_carries(void)
{
	struct btpolicy_advert a = stored_advert(1);

	btpolicy_init(&policy);
	CHECK_EQ(btpolicy_wants(&policy, &a, false, 0), BTPOLICY_CONNECT);
	a.directed = true;
	CHECK_EQ(btpolicy_wants(&policy, &a, false, 0), BTPOLICY_CONNECT);
	a.connectable = false;
	CHECK_EQ(btpolicy_wants(&policy, &a, false, 0), BTPOLICY_IGNORE);
}

static void a_stored_keyboard_waits_while_a_window_is_open(void)
{
	struct btpolicy_advert known = stored_advert(1);

	btpolicy_init(&policy);
	CHECK_EQ(btpolicy_wants(&policy, &known, true, 0), BTPOLICY_IGNORE);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 0), BTPOLICY_CONNECT);
}

static void an_unknown_keyboard_needs_an_open_window(void)
{
	struct btpolicy_advert a = new_advert(1);

	btpolicy_init(&policy);
	CHECK_EQ(btpolicy_wants(&policy, &a, false, 0), BTPOLICY_IGNORE);
	CHECK_EQ(btpolicy_wants(&policy, &a, true, 0), BTPOLICY_CONNECT);
}

static void an_unknown_keyboard_needs_the_hid_service_undirected(void)
{
	struct btpolicy_advert a = new_advert(1);

	btpolicy_init(&policy);
	CHECK_EQ(btpolicy_wants(&policy, &a, true, 0), BTPOLICY_CONNECT);
	a.hid_service = false;
	CHECK_EQ(btpolicy_wants(&policy, &a, true, 0), BTPOLICY_IGNORE);
	a.hid_service = true;
	a.directed = true;
	CHECK_EQ(btpolicy_wants(&policy, &a, true, 0), BTPOLICY_IGNORE);
	a.directed = false;
	a.connectable = false;
	CHECK_EQ(btpolicy_wants(&policy, &a, true, 0), BTPOLICY_IGNORE);
}

static void a_ready_keyboard_is_kept_from_stored_ones_and_replaced_by_a_new_one(void)
{
	struct btpolicy_advert first = stored_advert(1);
	struct btpolicy_advert second = stored_advert(2);
	struct btpolicy_advert fresh = new_advert(3);

	btpolicy_init(&policy);
	CHECK_EQ(btpolicy_scan(&policy, false, 0), BTPOLICY_SCAN_PASSIVE);
	CHECK(btpolicy_peer(&policy, DEVICE_KIND_KEYBOARD) == NULL);
	btpolicy_connecting(&policy, &first);
	CHECK_MEM(btpolicy_peer(&policy, DEVICE_KIND_KEYBOARD), first.addr, BTPOLICY_ADDR_LEN);
	CHECK_EQ(btpolicy_scan(&policy, false, 0), BTPOLICY_SCAN_OFF);
	CHECK_EQ(btpolicy_wants(&policy, &second, false, 0), BTPOLICY_IGNORE);
	CHECK_EQ(btpolicy_wants(&policy, &fresh, true, 0), BTPOLICY_IGNORE);
	btpolicy_connected(&policy, DEVICE_KIND_KEYBOARD);
	CHECK_EQ(btpolicy_scan(&policy, false, 0), BTPOLICY_SCAN_OFF);
	CHECK_EQ(btpolicy_wants(&policy, &fresh, true, 0), BTPOLICY_IGNORE);
	btpolicy_ready(&policy, DEVICE_KIND_KEYBOARD);
	CHECK(btpolicy_is_ready(&policy, DEVICE_KIND_KEYBOARD));
	CHECK_EQ(btpolicy_wants(&policy, &second, false, 0), BTPOLICY_IGNORE);
	CHECK_EQ(btpolicy_wants(&policy, &fresh, true, 0), BTPOLICY_REPLACE);
	btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 500, false);
	CHECK(!btpolicy_is_ready(&policy, DEVICE_KIND_KEYBOARD));
	CHECK_EQ(btpolicy_scan(&policy, false, 0), BTPOLICY_SCAN_PASSIVE);
	CHECK_EQ(btpolicy_wants(&policy, &second, false, 500), BTPOLICY_CONNECT);
}

static void a_failed_stored_keyboard_waits_before_the_next_attempt(void)
{
	struct btpolicy_advert known = stored_advert(1);
	struct btpolicy_advert other = stored_advert(2);
	struct btpolicy_advert fresh = new_advert(1);

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, &known);
	btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 10000, false);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000 + BTPOLICY_RETRY_MS - 1), BTPOLICY_IGNORE);
	CHECK_EQ(btpolicy_wants(&policy, &other, false, 10000 + BTPOLICY_RETRY_MS - 1), BTPOLICY_IGNORE);
	CHECK_EQ(btpolicy_wants(&policy, &fresh, true, 10000), BTPOLICY_CONNECT);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000 + BTPOLICY_RETRY_MS), BTPOLICY_CONNECT);
}

static void a_connection_that_ended_normally_is_retried_at_once(void)
{
	struct btpolicy_advert known = stored_advert(1);

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, &known);
	btpolicy_connected(&policy, DEVICE_KIND_KEYBOARD);
	btpolicy_ready(&policy, DEVICE_KIND_KEYBOARD);
	btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 10000, false);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000), BTPOLICY_CONNECT);
}

static void a_stored_keyboard_that_never_joined_is_retried_at_once(void)
{
	struct btpolicy_advert known = stored_advert(1);

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, &known);
	btpolicy_connected(&policy, DEVICE_KIND_KEYBOARD);
	btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 10000, true);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000), BTPOLICY_CONNECT);
}

static void a_keyboard_that_keeps_not_joining_waits_after_the_quick_retries(void)
{
	struct btpolicy_advert known = stored_advert(1);

	btpolicy_init(&policy);
	for (int i = 0; i < BTPOLICY_QUICK_RETRIES; i++) {
		btpolicy_connecting(&policy, &known);
		btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 10000, true);
		CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000), BTPOLICY_CONNECT);
	}
	btpolicy_connecting(&policy, &known);
	btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 10000, true);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000 + BTPOLICY_RETRY_MS - 1), BTPOLICY_IGNORE);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000 + BTPOLICY_RETRY_MS), BTPOLICY_CONNECT);
	btpolicy_connecting(&policy, &known);
	btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 20000, true);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 20000), BTPOLICY_CONNECT);
}

static void a_keyboard_that_got_ready_has_its_quick_retries_again(void)
{
	struct btpolicy_advert known = stored_advert(1);

	btpolicy_init(&policy);
	for (int i = 0; i < BTPOLICY_QUICK_RETRIES; i++) {
		btpolicy_connecting(&policy, &known);
		btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 10000, true);
	}
	btpolicy_connecting(&policy, &known);
	btpolicy_connected(&policy, DEVICE_KIND_KEYBOARD);
	btpolicy_ready(&policy, DEVICE_KIND_KEYBOARD);
	btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 10000, false);
	btpolicy_connecting(&policy, &known);
	btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 10000, true);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000), BTPOLICY_CONNECT);
}

static void a_failed_new_keyboard_sets_no_wait(void)
{
	struct btpolicy_advert fresh = new_advert(1);
	struct btpolicy_advert known = stored_advert(1);

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, &fresh);
	btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 10000, false);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000), BTPOLICY_CONNECT);
	CHECK_EQ(btpolicy_wants(&policy, &fresh, true, 10000), BTPOLICY_CONNECT);
}

static void a_keyboard_paired_in_this_connection_waits_after_a_failure(void)
{
	struct btpolicy_advert fresh = new_advert(1);
	struct btpolicy_advert known = stored_advert(1);

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, &fresh);
	btpolicy_paired(&policy, DEVICE_KIND_KEYBOARD);
	btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 10000, false);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000), BTPOLICY_IGNORE);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000 + BTPOLICY_RETRY_MS), BTPOLICY_CONNECT);
}

static void the_wait_holds_across_the_clock_wrap(void)
{
	struct btpolicy_advert known = stored_advert(1);
	uint32_t before_wrap = UINT32_MAX - 100;

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, &known);
	btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, before_wrap, false);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, before_wrap + 500), BTPOLICY_IGNORE);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, before_wrap + BTPOLICY_RETRY_MS), BTPOLICY_CONNECT);
}

static void a_stored_keyboard_without_its_key_lost_its_bond(void)
{
	struct btpolicy_advert known = stored_advert(1);
	struct btpolicy_advert fresh = new_advert(1);

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, &known);
	CHECK_EQ(btpolicy_security_failed(&policy, DEVICE_KIND_KEYBOARD, BTPOLICY_FAILURE_KEY_MISSING), BTPOLICY_BOND_LOST);
	CHECK_EQ(btpolicy_security_failed(&policy, DEVICE_KIND_KEYBOARD, BTPOLICY_FAILURE_REFUSAL), BTPOLICY_SECURITY_FAILED);
	btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 0, false);
	btpolicy_connecting(&policy, &fresh);
	btpolicy_paired(&policy, DEVICE_KIND_KEYBOARD);
	CHECK_EQ(btpolicy_security_failed(&policy, DEVICE_KIND_KEYBOARD, BTPOLICY_FAILURE_REFUSAL), BTPOLICY_SECURITY_FAILED);
}

static void a_new_device_refusing_is_a_refused_pairing_and_a_lost_link_is_not(void)
{
	struct btpolicy_advert fresh = new_advert(1);

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, &fresh);
	CHECK_EQ(btpolicy_security_failed(&policy, DEVICE_KIND_KEYBOARD, BTPOLICY_FAILURE_REFUSAL),
		 BTPOLICY_PAIRING_REFUSED);
	btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 0, false);
	btpolicy_connecting(&policy, &fresh);
	CHECK_EQ(btpolicy_security_failed(&policy, DEVICE_KIND_KEYBOARD, BTPOLICY_FAILURE_LINK),
		 BTPOLICY_SECURITY_FAILED);
}

static void only_connectable_stored_or_hid_adverts_are_of_interest(void)
{
	struct btpolicy_advert a = new_advert(1);

	CHECK(btpolicy_interesting(&a));
	a.hid_service = false;
	CHECK(!btpolicy_interesting(&a));
	a.stored = true;
	CHECK(btpolicy_interesting(&a));
	a.connectable = false;
	CHECK(!btpolicy_interesting(&a));
}

static struct btpolicy_advert new_mouse(uint8_t last)
{
	struct btpolicy_advert a = new_advert(last);

	a.addr[1] = 8;
	a.kind = DEVICE_KIND_MOUSE;
	return a;
}

static void ready(const struct btpolicy_advert *a)
{
	btpolicy_connecting(&policy, a);
	btpolicy_connected(&policy, a->kind);
	btpolicy_ready(&policy, a->kind);
}

static void the_appearance_gives_the_kind(void)
{
	CHECK_EQ(btpolicy_kind_of(0x03c1), DEVICE_KIND_KEYBOARD);
	CHECK_EQ(btpolicy_kind_of(0x03c2), DEVICE_KIND_MOUSE);
	CHECK_EQ(btpolicy_kind_of(0x0000), DEVICE_NO_KIND);
	CHECK_EQ(btpolicy_kind_of(0x03c0), DEVICE_NO_KIND);
	CHECK_EQ(btpolicy_kind_of(0x03c4), DEVICE_NO_KIND);
}

static void only_a_bolt_mouse_pairs_by_clicks(void)
{
	CHECK(btpolicy_pairs_by_clicks(DEVICE_KIND_MOUSE, true));
	CHECK(!btpolicy_pairs_by_clicks(DEVICE_KIND_MOUSE, false));
	CHECK(!btpolicy_pairs_by_clicks(DEVICE_KIND_KEYBOARD, true));
}

static void a_click_passkey_keeps_the_low_ten_bits(void)
{
	CHECK_EQ(btpolicy_click_passkey(999999), 575);
	CHECK_EQ(btpolicy_click_passkey(1023), 1023);
}

static void a_new_device_of_no_kind_is_refused(void)
{
	struct btpolicy_advert a = new_advert(1);

	a.kind = DEVICE_NO_KIND;
	btpolicy_init(&policy);
	CHECK(!btpolicy_interesting(&a));
	CHECK_EQ(btpolicy_wants(&policy, &a, true, 0), BTPOLICY_IGNORE);
	a.stored = true;
	CHECK(btpolicy_interesting(&a));
	CHECK_EQ(btpolicy_wants(&policy, &a, false, 0), BTPOLICY_IGNORE);
}

static void a_mouse_connects_beside_a_ready_keyboard(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);
	struct btpolicy_advert mouse = new_mouse(1);

	btpolicy_init(&policy);
	ready(&keyboard);
	CHECK_EQ(btpolicy_scan(&policy, false, 0), BTPOLICY_SCAN_PASSIVE);
	CHECK_EQ(btpolicy_wants(&policy, &mouse, true, 0), BTPOLICY_CONNECT);
	btpolicy_connecting(&policy, &mouse);
	CHECK_MEM(btpolicy_peer(&policy, DEVICE_KIND_MOUSE), mouse.addr, BTPOLICY_ADDR_LEN);
	CHECK_MEM(btpolicy_peer(&policy, DEVICE_KIND_KEYBOARD), keyboard.addr, BTPOLICY_ADDR_LEN);
	CHECK(btpolicy_is_ready(&policy, DEVICE_KIND_KEYBOARD));
	CHECK(!btpolicy_is_ready(&policy, DEVICE_KIND_MOUSE));
}

static void one_device_is_set_up_at_a_time(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);
	struct btpolicy_advert mouse = new_mouse(1);

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, &keyboard);
	CHECK_EQ(btpolicy_scan(&policy, false, 0), BTPOLICY_SCAN_OFF);
	CHECK_EQ(btpolicy_wants(&policy, &mouse, true, 0), BTPOLICY_IGNORE);
	btpolicy_connected(&policy, DEVICE_KIND_KEYBOARD);
	CHECK_EQ(btpolicy_wants(&policy, &mouse, true, 0), BTPOLICY_IGNORE);
	btpolicy_ready(&policy, DEVICE_KIND_KEYBOARD);
	CHECK_EQ(btpolicy_wants(&policy, &mouse, true, 0), BTPOLICY_CONNECT);
}

static void a_slot_being_dropped_wants_nothing_until_it_is_idle(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);
	struct btpolicy_advert fresh = new_advert(2);

	btpolicy_init(&policy);
	ready(&keyboard);
	CHECK_EQ(btpolicy_wants(&policy, &fresh, true, 0), BTPOLICY_REPLACE);
	btpolicy_dropping(&policy, DEVICE_KIND_KEYBOARD);
	CHECK_EQ(btpolicy_wants(&policy, &fresh, true, 0), BTPOLICY_IGNORE);
	btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 100, false);
	CHECK_EQ(btpolicy_wants(&policy, &fresh, true, 100), BTPOLICY_CONNECT);
}

static void drop_hushed(enum device_kind kind, uint32_t now_ms)
{
	btpolicy_hush(&policy, kind, now_ms);
	btpolicy_disconnected(&policy, kind, now_ms, false);
}

static bool hushed(const struct btpolicy_advert *a, uint32_t now_ms)
{
	return btpolicy_wants(&policy, a, false, now_ms) == BTPOLICY_IGNORE;
}

static void a_hushed_device_that_keeps_advertising_is_ignored_until_it_goes_quiet(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);
	uint32_t t = 1000;

	btpolicy_init(&policy);
	ready(&keyboard);
	drop_hushed(DEVICE_KIND_KEYBOARD, t);
	for (int i = 0; i < 5; i++) {
		t += BTPOLICY_QUIET_MS - 1;
		CHECK(hushed(&keyboard, t));
	}
	CHECK(!hushed(&keyboard, t + BTPOLICY_QUIET_MS));
}

static void a_hushed_slot_is_being_dropped(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);
	struct btpolicy_advert fresh = new_advert(2);

	btpolicy_init(&policy);
	ready(&keyboard);
	btpolicy_hush(&policy, DEVICE_KIND_KEYBOARD, 0);
	CHECK_EQ(btpolicy_wants(&policy, &fresh, true, 0), BTPOLICY_IGNORE);
}

static void a_hushed_device_quiet_since_the_drop_is_heard(void)
{
	struct btpolicy_advert mouse = new_mouse(1);

	mouse.stored = true;
	btpolicy_init(&policy);
	ready(&mouse);
	drop_hushed(DEVICE_KIND_MOUSE, 1000);
	CHECK(!hushed(&mouse, 1000 + BTPOLICY_QUIET_MS));
}

static void another_device_of_the_same_kind_neither_is_hushed_nor_extends_the_hush(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);
	struct btpolicy_advert other = stored_advert(2);

	btpolicy_init(&policy);
	ready(&keyboard);
	drop_hushed(DEVICE_KIND_KEYBOARD, 0);
	CHECK(!hushed(&other, BTPOLICY_QUIET_MS - 1));
	CHECK(!hushed(&keyboard, BTPOLICY_QUIET_MS));
}

static void a_device_disconnected_before_the_hush_is_heard_at_once(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);

	btpolicy_init(&policy);
	ready(&keyboard);
	btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 0, false);
	btpolicy_hush(&policy, DEVICE_KIND_KEYBOARD, 0);
	CHECK(!hushed(&keyboard, 10));
}

static void lifting_the_hushes_hears_every_device(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);

	btpolicy_init(&policy);
	ready(&keyboard);
	drop_hushed(DEVICE_KIND_KEYBOARD, 0);
	btpolicy_unhush(&policy);
	CHECK(!hushed(&keyboard, 10));
}

static void the_hush_holds_across_the_clock_wrap(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);
	uint32_t before_wrap = UINT32_MAX - 100;

	btpolicy_init(&policy);
	ready(&keyboard);
	drop_hushed(DEVICE_KIND_KEYBOARD, before_wrap);
	CHECK(hushed(&keyboard, before_wrap + 500));
	CHECK(!hushed(&keyboard, before_wrap + 500 + BTPOLICY_QUIET_MS));
}

static void a_hush_older_than_half_the_clock_has_lapsed(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);

	btpolicy_init(&policy);
	ready(&keyboard);
	drop_hushed(DEVICE_KIND_KEYBOARD, 0);
	CHECK(!hushed(&keyboard, 0x80000000u + 10));
}

static void a_hush_scans_fast_no_longer_than_the_cap(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);
	uint32_t t = UINT32_MAX - 100;
	uint32_t dropped = t;

	btpolicy_init(&policy);
	ready(&keyboard);
	drop_hushed(DEVICE_KIND_KEYBOARD, dropped);
	while (t - dropped < BTPOLICY_FAST_SCAN_MS) {
		CHECK_EQ(btpolicy_scan(&policy, false, t), BTPOLICY_SCAN_PASSIVE_FAST);
		t += BTPOLICY_QUIET_MS - 1;
		CHECK(hushed(&keyboard, t));
	}
	CHECK_EQ(btpolicy_scan(&policy, false, t), BTPOLICY_SCAN_PASSIVE);
	CHECK(hushed(&keyboard, t + 10));
}

static void a_hush_scans_fast_until_the_device_goes_quiet(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);

	btpolicy_init(&policy);
	ready(&keyboard);
	drop_hushed(DEVICE_KIND_KEYBOARD, 0);
	CHECK_EQ(btpolicy_scan(&policy, false, 10), BTPOLICY_SCAN_PASSIVE_FAST);
	CHECK(hushed(&keyboard, BTPOLICY_QUIET_MS - 1));
	CHECK_EQ(btpolicy_scan(&policy, false, BTPOLICY_QUIET_MS + 10), BTPOLICY_SCAN_PASSIVE_FAST);
	CHECK_EQ(btpolicy_scan(&policy, false, 2 * BTPOLICY_QUIET_MS - 1), BTPOLICY_SCAN_PASSIVE);
}

static void both_slots_ready_scan_only_while_the_window_is_open(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);
	struct btpolicy_advert mouse = new_mouse(1);

	btpolicy_init(&policy);
	ready(&keyboard);
	btpolicy_connecting(&policy, &mouse);
	btpolicy_connected(&policy, DEVICE_KIND_MOUSE);
	btpolicy_ready(&policy, DEVICE_KIND_MOUSE);
	CHECK_EQ(btpolicy_scan(&policy, false, 0), BTPOLICY_SCAN_OFF);
	CHECK_EQ(btpolicy_scan(&policy, true, 0), BTPOLICY_SCAN_ACTIVE);
}

static void a_failed_keyboard_leaves_the_mouse_slot_free_to_connect(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);
	struct btpolicy_advert mouse = stored_advert(2);

	mouse.kind = DEVICE_KIND_MOUSE;
	btpolicy_init(&policy);
	btpolicy_connecting(&policy, &keyboard);
	btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 10000, false);
	CHECK_EQ(btpolicy_wants(&policy, &keyboard, false, 10000), BTPOLICY_IGNORE);
	CHECK_EQ(btpolicy_wants(&policy, &mouse, false, 10000), BTPOLICY_CONNECT);
}

static void a_pairing_window_scans_actively_for_scan_responses(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);

	btpolicy_init(&policy);
	CHECK_EQ(btpolicy_scan(&policy, true, 0), BTPOLICY_SCAN_ACTIVE);
	btpolicy_connecting(&policy, &keyboard);
	CHECK_EQ(btpolicy_scan(&policy, true, 0), BTPOLICY_SCAN_OFF);
	btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 0, false);
	CHECK_EQ(btpolicy_scan(&policy, false, 0), BTPOLICY_SCAN_PASSIVE);
}

int main(void)
{
	RUN(a_stored_keyboard_is_wanted_whatever_its_advert_carries);
	RUN(a_stored_keyboard_waits_while_a_window_is_open);
	RUN(an_unknown_keyboard_needs_an_open_window);
	RUN(an_unknown_keyboard_needs_the_hid_service_undirected);
	RUN(a_ready_keyboard_is_kept_from_stored_ones_and_replaced_by_a_new_one);
	RUN(a_failed_stored_keyboard_waits_before_the_next_attempt);
	RUN(a_connection_that_ended_normally_is_retried_at_once);
	RUN(a_stored_keyboard_that_never_joined_is_retried_at_once);
	RUN(a_keyboard_that_keeps_not_joining_waits_after_the_quick_retries);
	RUN(a_keyboard_that_got_ready_has_its_quick_retries_again);
	RUN(a_failed_new_keyboard_sets_no_wait);
	RUN(a_keyboard_paired_in_this_connection_waits_after_a_failure);
	RUN(the_wait_holds_across_the_clock_wrap);
	RUN(a_stored_keyboard_without_its_key_lost_its_bond);
	RUN(a_new_device_refusing_is_a_refused_pairing_and_a_lost_link_is_not);
	RUN(only_connectable_stored_or_hid_adverts_are_of_interest);
	RUN(the_appearance_gives_the_kind);
	RUN(only_a_bolt_mouse_pairs_by_clicks);
	RUN(a_click_passkey_keeps_the_low_ten_bits);
	RUN(a_new_device_of_no_kind_is_refused);
	RUN(a_mouse_connects_beside_a_ready_keyboard);
	RUN(one_device_is_set_up_at_a_time);
	RUN(a_slot_being_dropped_wants_nothing_until_it_is_idle);
	RUN(a_hushed_device_that_keeps_advertising_is_ignored_until_it_goes_quiet);
	RUN(a_hushed_slot_is_being_dropped);
	RUN(a_hushed_device_quiet_since_the_drop_is_heard);
	RUN(another_device_of_the_same_kind_neither_is_hushed_nor_extends_the_hush);
	RUN(a_device_disconnected_before_the_hush_is_heard_at_once);
	RUN(lifting_the_hushes_hears_every_device);
	RUN(the_hush_holds_across_the_clock_wrap);
	RUN(a_hush_older_than_half_the_clock_has_lapsed);
	RUN(a_hush_scans_fast_until_the_device_goes_quiet);
	RUN(a_hush_scans_fast_no_longer_than_the_cap);
	RUN(both_slots_ready_scan_only_while_the_window_is_open);
	RUN(a_failed_keyboard_leaves_the_mouse_slot_free_to_connect);
	RUN(a_pairing_window_scans_actively_for_scan_responses);
	return TEST_EXIT();
}
