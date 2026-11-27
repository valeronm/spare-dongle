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
	CHECK_EQ(btpolicy_wants(&policy, &a, false, 0).want, BTPOLICY_CONNECT);
	a.directed = true;
	CHECK_EQ(btpolicy_wants(&policy, &a, false, 0).want, BTPOLICY_CONNECT);
	a.connectable = false;
	CHECK_EQ(btpolicy_wants(&policy, &a, false, 0).want, BTPOLICY_IGNORE);
}

static void a_stored_keyboard_waits_while_a_window_is_open(void)
{
	struct btpolicy_advert known = stored_advert(1);

	btpolicy_init(&policy);
	CHECK_EQ(btpolicy_wants(&policy, &known, true, 0).want, BTPOLICY_IGNORE);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 0).want, BTPOLICY_CONNECT);
}

static void an_unknown_keyboard_needs_an_open_window(void)
{
	struct btpolicy_advert a = new_advert(1);

	btpolicy_init(&policy);
	CHECK_EQ(btpolicy_wants(&policy, &a, false, 0).want, BTPOLICY_IGNORE);
	CHECK_EQ(btpolicy_wants(&policy, &a, true, 0).want, BTPOLICY_CONNECT);
}

static void an_unknown_keyboard_needs_the_hid_service_undirected(void)
{
	struct btpolicy_advert a = new_advert(1);

	btpolicy_init(&policy);
	CHECK_EQ(btpolicy_wants(&policy, &a, true, 0).want, BTPOLICY_CONNECT);
	a.hid_service = false;
	CHECK_EQ(btpolicy_wants(&policy, &a, true, 0).want, BTPOLICY_IGNORE);
	a.hid_service = true;
	a.directed = true;
	CHECK_EQ(btpolicy_wants(&policy, &a, true, 0).want, BTPOLICY_IGNORE);
	a.directed = false;
	a.connectable = false;
	CHECK_EQ(btpolicy_wants(&policy, &a, true, 0).want, BTPOLICY_IGNORE);
}

static void a_second_stored_keyboard_connects_beside_a_ready_one(void)
{
	struct btpolicy_advert first = stored_advert(1);
	struct btpolicy_advert second = stored_advert(2);
	struct btpolicy_advert fresh = new_advert(3);
	struct btpolicy_choice c;

	btpolicy_init(&policy);
	CHECK_EQ(btpolicy_scan(&policy, false), BTPOLICY_SCAN_PASSIVE);
	CHECK(btpolicy_peer(&policy, 0) == NULL);
	btpolicy_connecting(&policy, 0, &first);
	CHECK_MEM(btpolicy_peer(&policy, 0), first.addr, BTPOLICY_ADDR_LEN);
	CHECK_EQ(btpolicy_scan(&policy, false), BTPOLICY_SCAN_OFF);
	CHECK_EQ(btpolicy_wants(&policy, &second, false, 0).want, BTPOLICY_IGNORE);
	CHECK_EQ(btpolicy_wants(&policy, &fresh, true, 0).want, BTPOLICY_IGNORE);
	btpolicy_connected(&policy, 0);
	CHECK_EQ(btpolicy_scan(&policy, false), BTPOLICY_SCAN_OFF);
	CHECK_EQ(btpolicy_wants(&policy, &fresh, true, 0).want, BTPOLICY_IGNORE);
	btpolicy_ready(&policy, 0);
	CHECK(btpolicy_is_ready(&policy, 0));
	c = btpolicy_wants(&policy, &second, false, 0);
	CHECK_EQ(c.want, BTPOLICY_CONNECT);
	CHECK_EQ(c.slot, 1);
	c = btpolicy_wants(&policy, &fresh, true, 0);
	CHECK_EQ(c.want, BTPOLICY_CONNECT);
	CHECK_EQ(c.slot, 1);
	btpolicy_disconnected(&policy, 0, 500, false);
	CHECK(!btpolicy_is_ready(&policy, 0));
	CHECK_EQ(btpolicy_scan(&policy, false), BTPOLICY_SCAN_PASSIVE);
	CHECK_EQ(btpolicy_wants(&policy, &second, false, 500).want, BTPOLICY_CONNECT);
}

static void a_failed_stored_keyboard_waits_before_the_next_attempt(void)
{
	struct btpolicy_advert known = stored_advert(1);
	struct btpolicy_advert other = stored_advert(2);
	struct btpolicy_advert fresh = new_advert(1);
	struct btpolicy_choice c;

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, 0, &known);
	btpolicy_disconnected(&policy, 0, 10000, false);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000 + BTPOLICY_RETRY_MS - 1).want, BTPOLICY_IGNORE);
	c = btpolicy_wants(&policy, &other, false, 10000 + BTPOLICY_RETRY_MS - 1);
	CHECK_EQ(c.want, BTPOLICY_CONNECT);
	CHECK_EQ(c.slot, 1);
	CHECK_EQ(btpolicy_wants(&policy, &fresh, true, 10000).want, BTPOLICY_CONNECT);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000 + BTPOLICY_RETRY_MS).want, BTPOLICY_CONNECT);
}

static void a_connection_that_ended_normally_is_retried_at_once(void)
{
	struct btpolicy_advert known = stored_advert(1);

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, 0, &known);
	btpolicy_connected(&policy, 0);
	btpolicy_ready(&policy, 0);
	btpolicy_disconnected(&policy, 0, 10000, false);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000).want, BTPOLICY_CONNECT);
}

static void a_stored_keyboard_that_never_joined_is_retried_at_once(void)
{
	struct btpolicy_advert known = stored_advert(1);

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, 0, &known);
	btpolicy_connected(&policy, 0);
	btpolicy_disconnected(&policy, 0, 10000, true);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000).want, BTPOLICY_CONNECT);
}

static void a_keyboard_that_keeps_not_joining_waits_after_the_quick_retries(void)
{
	struct btpolicy_advert known = stored_advert(1);

	btpolicy_init(&policy);
	for (int i = 0; i < BTPOLICY_QUICK_RETRIES; i++) {
		btpolicy_connecting(&policy, 0, &known);
		btpolicy_disconnected(&policy, 0, 10000, true);
		CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000).want, BTPOLICY_CONNECT);
	}
	btpolicy_connecting(&policy, 0, &known);
	btpolicy_disconnected(&policy, 0, 10000, true);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000 + BTPOLICY_RETRY_MS - 1).want, BTPOLICY_IGNORE);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000 + BTPOLICY_RETRY_MS).want, BTPOLICY_CONNECT);
	btpolicy_connecting(&policy, 0, &known);
	btpolicy_disconnected(&policy, 0, 20000, true);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 20000).want, BTPOLICY_CONNECT);
}

static void a_keyboard_that_got_ready_has_its_quick_retries_again(void)
{
	struct btpolicy_advert known = stored_advert(1);

	btpolicy_init(&policy);
	for (int i = 0; i < BTPOLICY_QUICK_RETRIES; i++) {
		btpolicy_connecting(&policy, 0, &known);
		btpolicy_disconnected(&policy, 0, 10000, true);
	}
	btpolicy_connecting(&policy, 0, &known);
	btpolicy_connected(&policy, 0);
	btpolicy_ready(&policy, 0);
	btpolicy_disconnected(&policy, 0, 10000, false);
	btpolicy_connecting(&policy, 0, &known);
	btpolicy_disconnected(&policy, 0, 10000, true);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000).want, BTPOLICY_CONNECT);
}

static void a_failed_new_keyboard_sets_no_wait(void)
{
	struct btpolicy_advert fresh = new_advert(1);
	struct btpolicy_advert known = stored_advert(1);

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, 0, &fresh);
	btpolicy_disconnected(&policy, 0, 10000, false);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000).want, BTPOLICY_CONNECT);
	CHECK_EQ(btpolicy_wants(&policy, &fresh, true, 10000).want, BTPOLICY_CONNECT);
}

static void a_keyboard_paired_in_this_connection_waits_after_a_failure(void)
{
	struct btpolicy_advert fresh = new_advert(1);
	struct btpolicy_advert known = fresh;

	known.stored = true;
	btpolicy_init(&policy);
	btpolicy_connecting(&policy, 0, &fresh);
	btpolicy_paired(&policy, 0);
	btpolicy_disconnected(&policy, 0, 10000, false);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000).want, BTPOLICY_IGNORE);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, 10000 + BTPOLICY_RETRY_MS).want, BTPOLICY_CONNECT);
}

static void the_wait_holds_across_the_clock_wrap(void)
{
	struct btpolicy_advert known = stored_advert(1);
	uint32_t before_wrap = UINT32_MAX - 100;

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, 0, &known);
	btpolicy_disconnected(&policy, 0, before_wrap, false);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, before_wrap + 500).want, BTPOLICY_IGNORE);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, before_wrap + BTPOLICY_RETRY_MS).want, BTPOLICY_CONNECT);
}

static void a_stored_keyboard_without_its_key_lost_its_bond(void)
{
	struct btpolicy_advert known = stored_advert(1);
	struct btpolicy_advert fresh = new_advert(1);

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, 0, &known);
	CHECK_EQ(btpolicy_security_failed(&policy, 0, BTPOLICY_FAILURE_KEY_MISSING), BTPOLICY_BOND_LOST);
	CHECK_EQ(btpolicy_security_failed(&policy, 0, BTPOLICY_FAILURE_REFUSAL), BTPOLICY_SECURITY_FAILED);
	btpolicy_disconnected(&policy, 0, 0, false);
	btpolicy_connecting(&policy, 1, &fresh);
	btpolicy_paired(&policy, 1);
	CHECK_EQ(btpolicy_security_failed(&policy, 1, BTPOLICY_FAILURE_REFUSAL), BTPOLICY_SECURITY_FAILED);
}

static void a_new_device_refusing_is_a_refused_pairing_and_a_lost_link_is_not(void)
{
	struct btpolicy_advert fresh = new_advert(1);

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, 0, &fresh);
	CHECK_EQ(btpolicy_security_failed(&policy, 0, BTPOLICY_FAILURE_REFUSAL),
		 BTPOLICY_PAIRING_REFUSED);
	btpolicy_disconnected(&policy, 0, 0, false);
	btpolicy_connecting(&policy, 0, &fresh);
	CHECK_EQ(btpolicy_security_failed(&policy, 0, BTPOLICY_FAILURE_LINK),
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

static void fill(const struct btpolicy_advert *a, uint8_t slot)
{
	btpolicy_connecting(&policy, slot, a);
	btpolicy_connected(&policy, slot);
	btpolicy_ready(&policy, slot);
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
	CHECK_EQ(btpolicy_wants(&policy, &a, true, 0).want, BTPOLICY_IGNORE);
	a.stored = true;
	CHECK(btpolicy_interesting(&a));
	CHECK_EQ(btpolicy_wants(&policy, &a, false, 0).want, BTPOLICY_IGNORE);
}

static void a_mouse_connects_beside_a_ready_keyboard(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);
	struct btpolicy_advert mouse = new_mouse(1);

	btpolicy_init(&policy);
	fill(&keyboard, 0);
	CHECK_EQ(btpolicy_scan(&policy, false), BTPOLICY_SCAN_PASSIVE);
	CHECK_EQ(btpolicy_wants(&policy, &mouse, true, 0).want, BTPOLICY_CONNECT);
	btpolicy_connecting(&policy, 1, &mouse);
	CHECK_MEM(btpolicy_peer(&policy, 1), mouse.addr, BTPOLICY_ADDR_LEN);
	CHECK_MEM(btpolicy_peer(&policy, 0), keyboard.addr, BTPOLICY_ADDR_LEN);
	CHECK(btpolicy_is_ready(&policy, 0));
	CHECK(!btpolicy_is_ready(&policy, 1));
}

static void one_device_is_set_up_at_a_time(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);
	struct btpolicy_advert mouse = new_mouse(1);

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, 0, &keyboard);
	CHECK_EQ(btpolicy_scan(&policy, false), BTPOLICY_SCAN_OFF);
	CHECK_EQ(btpolicy_wants(&policy, &mouse, true, 0).want, BTPOLICY_IGNORE);
	btpolicy_connected(&policy, 0);
	CHECK_EQ(btpolicy_wants(&policy, &mouse, true, 0).want, BTPOLICY_IGNORE);
	btpolicy_ready(&policy, 0);
	CHECK_EQ(btpolicy_wants(&policy, &mouse, true, 0).want, BTPOLICY_CONNECT);
}

static void a_slot_being_dropped_wants_nothing_until_it_is_idle(void)
{
	struct btpolicy_advert keyboards[] = {stored_advert(1), stored_advert(2)};
	struct btpolicy_advert mice[] = {new_mouse(1), new_mouse(2)};
	struct btpolicy_advert fresh = new_advert(2);
	struct btpolicy_choice c;

	btpolicy_init(&policy);
	fill(&keyboards[0], 0);
	fill(&keyboards[1], 1);
	fill(&mice[0], 2);
	fill(&mice[1], 3);
	btpolicy_input(&policy, 0, true);
	btpolicy_input(&policy, 2, true);
	c = btpolicy_wants(&policy, &fresh, true, 20);
	CHECK_EQ(c.want, BTPOLICY_REPLACE);
	CHECK_EQ(c.slot, 1);
	btpolicy_dropping(&policy, 1);
	CHECK_EQ(btpolicy_wants(&policy, &fresh, true, 20).want, BTPOLICY_IGNORE);
	btpolicy_disconnected(&policy, 1, 100, false);
	c = btpolicy_wants(&policy, &fresh, true, 100);
	CHECK_EQ(c.want, BTPOLICY_CONNECT);
	CHECK_EQ(c.slot, 1);
}

static void every_slot_ready_scans_only_while_the_window_is_open(void)
{
	btpolicy_init(&policy);
	for (uint8_t s = 0; s < DEVICE_SLOTS; s++) {
		struct btpolicy_advert a = stored_advert(s);

		CHECK_EQ(btpolicy_scan(&policy, false), BTPOLICY_SCAN_PASSIVE);
		fill(&a, s);
	}
	CHECK_EQ(btpolicy_scan(&policy, false), BTPOLICY_SCAN_OFF);
	CHECK_EQ(btpolicy_scan(&policy, true), BTPOLICY_SCAN_ACTIVE);
}

static void a_failed_keyboard_leaves_the_mouse_slot_free_to_connect(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);
	struct btpolicy_advert mouse = stored_advert(2);

	mouse.kind = DEVICE_KIND_MOUSE;
	btpolicy_init(&policy);
	btpolicy_connecting(&policy, 0, &keyboard);
	btpolicy_disconnected(&policy, 0, 10000, false);
	CHECK_EQ(btpolicy_wants(&policy, &keyboard, false, 10000).want, BTPOLICY_IGNORE);
	CHECK_EQ(btpolicy_wants(&policy, &mouse, false, 10000).want, BTPOLICY_CONNECT);
}

static void a_pairing_window_scans_actively_for_scan_responses(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);

	btpolicy_init(&policy);
	CHECK_EQ(btpolicy_scan(&policy, true), BTPOLICY_SCAN_ACTIVE);
	btpolicy_connecting(&policy, 0, &keyboard);
	CHECK_EQ(btpolicy_scan(&policy, true), BTPOLICY_SCAN_OFF);
	btpolicy_disconnected(&policy, 0, 0, false);
	CHECK_EQ(btpolicy_scan(&policy, false), BTPOLICY_SCAN_PASSIVE);
}

static void a_device_reconnects_into_the_slot_it_last_held(void)
{
	struct btpolicy_advert first = stored_advert(1);
	struct btpolicy_advert second = stored_advert(2);
	struct btpolicy_choice c;

	btpolicy_init(&policy);
	fill(&first, 0);
	fill(&second, 1);
	btpolicy_disconnected(&policy, 0, 100, false);
	btpolicy_disconnected(&policy, 1, 200, false);
	c = btpolicy_wants(&policy, &second, false, 300);
	CHECK_EQ(c.want, BTPOLICY_CONNECT);
	CHECK_EQ(c.slot, 1);
	c = btpolicy_wants(&policy, &first, false, 300);
	CHECK_EQ(c.slot, 0);
	CHECK_MEM(btpolicy_last_peer(&policy, 0), first.addr, BTPOLICY_ADDR_LEN);
	CHECK(btpolicy_peer(&policy, 0) == NULL);
	CHECK_EQ(btpolicy_slot_kind(&policy, 0), DEVICE_KIND_KEYBOARD);
	CHECK_EQ(btpolicy_slot_kind(&policy, 2), DEVICE_NO_KIND);
}

static void a_new_device_takes_a_never_used_slot_before_the_longest_idle(void)
{
	struct btpolicy_advert a = stored_advert(1);
	struct btpolicy_advert b = stored_advert(2);
	struct btpolicy_advert c = stored_advert(3);
	struct btpolicy_advert d = stored_advert(4);
	struct btpolicy_advert e = stored_advert(5);

	btpolicy_init(&policy);
	fill(&a, 0);
	btpolicy_disconnected(&policy, 0, 100, false);
	CHECK_EQ(btpolicy_wants(&policy, &b, false, 200).slot, 1);
	fill(&b, 1);
	fill(&c, 2);
	fill(&d, 3);
	btpolicy_disconnected(&policy, 2, 300, false);
	CHECK_EQ(btpolicy_wants(&policy, &e, false, 400).slot, 0);
}

static void a_new_device_that_failed_takes_its_slot_back(void)
{
	struct btpolicy_advert other = stored_advert(1);
	struct btpolicy_advert fresh = new_advert(9);

	btpolicy_init(&policy);
	fill(&other, 0);
	btpolicy_disconnected(&policy, 0, 0, false);
	btpolicy_connecting(&policy, 1, &fresh);
	btpolicy_disconnected(&policy, 1, 100, false);
	CHECK_EQ(btpolicy_wants(&policy, &fresh, true, 200).slot, 1);
}

static void a_stored_device_is_ignored_while_every_link_is_held(void)
{
	struct btpolicy_choice c;

	btpolicy_init(&policy);
	for (uint8_t s = 0; s < DEVICE_SLOTS; s++) {
		struct btpolicy_advert a = stored_advert(s);

		fill(&a, s);
	}
	struct btpolicy_advert extra = stored_advert(9);

	CHECK_EQ(btpolicy_wants(&policy, &extra, false, 0).want, BTPOLICY_IGNORE);
	CHECK_EQ(btpolicy_scan(&policy, false), BTPOLICY_SCAN_OFF);
	btpolicy_disconnected(&policy, 1, 100, false);
	c = btpolicy_wants(&policy, &extra, false, 100);
	CHECK_EQ(c.want, BTPOLICY_CONNECT);
	CHECK_EQ(c.slot, 1);
	CHECK_EQ(btpolicy_scan(&policy, false), BTPOLICY_SCAN_PASSIVE);
}

static void pairing_with_every_link_held_drops_the_standby_with_the_oldest_input(void)
{
	struct btpolicy_advert mouse = new_mouse(1);
	struct btpolicy_advert fresh = new_advert(9);
	struct btpolicy_choice c;

	btpolicy_init(&policy);
	for (uint8_t s = 0; s < DEVICE_SLOTS; s++) {
		struct btpolicy_advert a = s == 3 ? mouse : stored_advert(s);

		fill(&a, s);
	}
	btpolicy_input(&policy, 0, true);
	btpolicy_input(&policy, 3, true);
	btpolicy_input(&policy, 2, true);
	CHECK(btpolicy_is_active(&policy, 2));
	CHECK(!btpolicy_is_active(&policy, 0));
	btpolicy_input(&policy, 1, true);
	c = btpolicy_wants(&policy, &fresh, true, 40);
	CHECK_EQ(c.want, BTPOLICY_REPLACE);
	CHECK_EQ(c.slot, 0);
}

static void pairing_never_drops_an_active_device(void)
{
	struct btpolicy_advert mouse = new_mouse(1);
	struct btpolicy_advert fresh = new_advert(9);

	struct btpolicy_advert k = stored_advert(1);
	struct btpolicy_choice c;

	btpolicy_init(&policy);
	fill(&k, 0);
	fill(&mouse, 1);
	btpolicy_input(&policy, 0, true);
	btpolicy_input(&policy, 1, true);
	for (uint8_t s = 2; s < DEVICE_SLOTS; s++) {
		struct btpolicy_advert a = stored_advert(s);

		fill(&a, s);
	}
	c = btpolicy_wants(&policy, &fresh, true, 30);
	CHECK_EQ(c.want, BTPOLICY_REPLACE);
	CHECK_EQ(c.slot, 2);
}

static void a_press_on_a_standby_device_takes_over_and_a_release_does_not(void)
{
	struct btpolicy_advert first = stored_advert(1);
	struct btpolicy_advert second = stored_advert(2);
	struct btpolicy_input_result r;

	btpolicy_init(&policy);
	fill(&first, 0);
	fill(&second, 1);
	r = btpolicy_input(&policy, 0, true);
	CHECK_EQ(r.input, BTPOLICY_INPUT_ACTIVE);
	CHECK_EQ(r.released, DEVICE_NO_SLOT);
	CHECK_EQ(btpolicy_input(&policy, 0, false).input, BTPOLICY_INPUT_ACTIVE);
	CHECK_EQ(btpolicy_input(&policy, 1, false).input, BTPOLICY_INPUT_STANDBY);
	r = btpolicy_input(&policy, 1, true);
	CHECK_EQ(r.input, BTPOLICY_INPUT_TOOK_OVER);
	CHECK_EQ(r.released, 0);
	CHECK_EQ(btpolicy_input(&policy, 0, true).released, 1);
}

static void a_keyboard_and_a_mouse_are_active_side_by_side(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);
	struct btpolicy_advert mouse = new_mouse(1);

	btpolicy_init(&policy);
	fill(&keyboard, 0);
	fill(&mouse, 1);
	CHECK(btpolicy_is_active(&policy, 0));
	CHECK(btpolicy_is_active(&policy, 1));
	CHECK_EQ(btpolicy_input(&policy, 1, true).input, BTPOLICY_INPUT_ACTIVE);
	CHECK(btpolicy_is_active(&policy, 0));
}

static void the_first_device_of_a_kind_is_active_once_ready(void)
{
	struct btpolicy_advert first = stored_advert(1);
	struct btpolicy_advert second = stored_advert(2);
	struct btpolicy_advert mouse = new_mouse(1);

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, 0, &first);
	btpolicy_connected(&policy, 0);
	CHECK(!btpolicy_is_active(&policy, 0));
	btpolicy_ready(&policy, 0);
	CHECK(btpolicy_is_active(&policy, 0));
	fill(&second, 1);
	CHECK(!btpolicy_is_active(&policy, 1));
	CHECK_EQ(btpolicy_input(&policy, 1, false).input, BTPOLICY_INPUT_STANDBY);
	fill(&mouse, 2);
	CHECK(btpolicy_is_active(&policy, 2));
	CHECK(btpolicy_is_active(&policy, 0));
}

static void a_ready_device_that_is_not_active_is_standby(void)
{
	struct btpolicy_advert first = stored_advert(1);
	struct btpolicy_advert second = stored_advert(2);
	struct btpolicy_advert third = stored_advert(3);

	btpolicy_init(&policy);
	fill(&first, 0);
	fill(&second, 1);
	btpolicy_connecting(&policy, 2, &third);
	CHECK(!btpolicy_is_standby(&policy, 0));
	CHECK(btpolicy_is_standby(&policy, 1));
	CHECK(!btpolicy_is_standby(&policy, 2));
	CHECK(!btpolicy_is_standby(&policy, 3));
}

static void an_active_device_that_disconnects_hands_over_to_the_latest_standby(void)
{
	btpolicy_init(&policy);
	for (uint8_t s = 0; s < DEVICE_SLOTS; s++) {
		struct btpolicy_advert a = stored_advert(s);

		fill(&a, s);
	}
	btpolicy_input(&policy, 2, true);
	CHECK_EQ(btpolicy_input(&policy, 0, true).released, 2);
	CHECK_EQ(btpolicy_disconnected(&policy, 0, 20, false), 2);
	CHECK(!btpolicy_is_active(&policy, 0));
	CHECK(btpolicy_is_active(&policy, 2));
	CHECK(!btpolicy_is_active(&policy, 1));
	CHECK(!btpolicy_is_active(&policy, 3));
	CHECK_EQ(btpolicy_input(&policy, 2, false).input, BTPOLICY_INPUT_ACTIVE);
}

static void a_pressed_standby_outranks_one_only_ready_since(void)
{
	btpolicy_init(&policy);
	for (uint8_t s = 0; s < 2; s++) {
		struct btpolicy_advert a = stored_advert(s);

		fill(&a, s);
	}
	btpolicy_input(&policy, 1, true);
	btpolicy_input(&policy, 0, true);
	struct btpolicy_advert later = stored_advert(2);

	fill(&later, 2);
	CHECK_EQ(btpolicy_disconnected(&policy, 0, 20, false), 1);
	CHECK(btpolicy_is_active(&policy, 1));
	CHECK(!btpolicy_is_active(&policy, 2));
}

static void among_standbys_never_pressed_the_latest_ready_takes_over(void)
{
	btpolicy_init(&policy);
	for (uint8_t s = 0; s < 3; s++) {
		struct btpolicy_advert a = stored_advert(s);

		fill(&a, s);
	}
	CHECK_EQ(btpolicy_disconnected(&policy, 0, 20, false), 2);
	CHECK(btpolicy_is_active(&policy, 2));
}

static void a_standby_device_that_disconnects_leaves_the_active_one(void)
{
	struct btpolicy_advert first = stored_advert(1);
	struct btpolicy_advert second = stored_advert(2);

	btpolicy_init(&policy);
	fill(&first, 0);
	fill(&second, 1);
	CHECK_EQ(btpolicy_disconnected(&policy, 1, 20, false), DEVICE_NO_SLOT);
	CHECK(btpolicy_is_active(&policy, 0));
}

static void an_active_device_alone_of_its_kind_leaves_its_kind_without_one(void)
{
	struct btpolicy_advert keyboard = stored_advert(1);
	struct btpolicy_advert other = stored_advert(2);
	struct btpolicy_advert mouse = new_mouse(1);

	btpolicy_init(&policy);
	fill(&keyboard, 0);
	fill(&mouse, 1);
	btpolicy_connecting(&policy, 2, &other);
	btpolicy_connected(&policy, 2);
	CHECK_EQ(btpolicy_disconnected(&policy, 0, 20, false), DEVICE_NO_SLOT);
	CHECK(!btpolicy_is_active(&policy, 0));
	CHECK(btpolicy_is_active(&policy, 1));
	btpolicy_ready(&policy, 2);
	CHECK(btpolicy_is_active(&policy, 2));
}

static void a_seeded_slot_takes_its_device_back(void)
{
	struct btpolicy_advert seeded = stored_advert(1);
	struct btpolicy_advert other = stored_advert(2);

	btpolicy_init(&policy);
	CHECK_EQ(btpolicy_seed(&policy, seeded.addr, DEVICE_KIND_KEYBOARD), 0);
	CHECK_MEM(btpolicy_last_peer(&policy, 0), seeded.addr, BTPOLICY_ADDR_LEN);
	CHECK(btpolicy_peer(&policy, 0) == NULL);
	CHECK_EQ(btpolicy_slot_kind(&policy, 0), DEVICE_KIND_KEYBOARD);
	CHECK_EQ(btpolicy_wants(&policy, &other, false, 0).slot, 1);
	CHECK_EQ(btpolicy_wants(&policy, &seeded, false, 0).slot, 0);
}

static void seeding_stops_once_every_slot_is_used(void)
{
	btpolicy_init(&policy);
	for (uint8_t s = 0; s < DEVICE_SLOTS; s++) {
		struct btpolicy_advert a = stored_advert(s);

		CHECK_EQ(btpolicy_seed(&policy, a.addr, DEVICE_KIND_KEYBOARD), s);
	}
	struct btpolicy_advert extra = stored_advert(9);

	CHECK_EQ(btpolicy_seed(&policy, extra.addr, DEVICE_KIND_KEYBOARD), DEVICE_NO_SLOT);
}

static void a_device_without_a_slot_takes_the_least_recent_seeded_one(void)
{
	struct btpolicy_advert extra = stored_advert(9);
	struct btpolicy_choice c;

	btpolicy_init(&policy);
	for (uint8_t s = 0; s < DEVICE_SLOTS; s++) {
		struct btpolicy_advert a = stored_advert(s);

		btpolicy_seed(&policy, a.addr, DEVICE_KIND_KEYBOARD);
	}
	c = btpolicy_wants(&policy, &extra, false, 0);
	CHECK_EQ(c.want, BTPOLICY_CONNECT);
	CHECK_EQ(c.slot, DEVICE_SLOTS - 1);
}

static void a_device_is_found_in_the_slot_that_last_held_it(void)
{
	struct btpolicy_advert seeded = stored_advert(1);
	struct btpolicy_advert dropped = new_advert(2);
	struct btpolicy_advert never = stored_advert(3);

	btpolicy_init(&policy);
	btpolicy_seed(&policy, seeded.addr, DEVICE_KIND_KEYBOARD);
	btpolicy_connecting(&policy, 1, &dropped);
	btpolicy_connected(&policy, 1);
	btpolicy_disconnected(&policy, 1, 0, false);
	CHECK_EQ(btpolicy_slot_of(&policy, seeded.addr), 0);
	CHECK_EQ(btpolicy_slot_of(&policy, dropped.addr), 1);
	CHECK_EQ(btpolicy_slot_of(&policy, never.addr), DEVICE_NO_SLOT);
}

static void the_slot_idle_longest_is_found_whatever_the_uptime(void)
{
	struct btpolicy_advert extra = stored_advert(9);

	btpolicy_init(&policy);
	for (uint8_t s = 0; s < DEVICE_SLOTS; s++) {
		struct btpolicy_advert a = stored_advert(s);

		fill(&a, s);
	}
	btpolicy_disconnected(&policy, 2, 0x10, false);
	btpolicy_disconnected(&policy, 0, 0x7ffffff0, false);
	btpolicy_disconnected(&policy, 3, 0x90000010, false);
	btpolicy_disconnected(&policy, 1, 0xfffffff0, false);
	CHECK_EQ(btpolicy_wants(&policy, &extra, false, 0x00000100).slot, 2);
}

static void a_slot_that_failed_long_ago_is_free_again(void)
{
	struct btpolicy_advert known = stored_advert(1);
	struct btpolicy_advert extra = stored_advert(9);
	uint32_t failed = 10000;

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, 0, &known);
	btpolicy_disconnected(&policy, 0, failed, false);
	for (uint8_t s = 1; s < DEVICE_SLOTS; s++) {
		struct btpolicy_advert a = stored_advert(10 + s);

		fill(&a, s);
	}
	CHECK_EQ(btpolicy_wants(&policy, &extra, false, failed + 1).want, BTPOLICY_IGNORE);
	CHECK_EQ(btpolicy_wants(&policy, &extra, false, failed + 0x90000000).slot, 0);
	CHECK_EQ(btpolicy_wants(&policy, &known, false, failed + 0x90000000).slot, 0);
}

static void input_from_a_slot_not_ready_is_standby(void)
{
	struct btpolicy_advert a = stored_advert(1);

	btpolicy_init(&policy);
	btpolicy_connecting(&policy, 0, &a);
	CHECK_EQ(btpolicy_input(&policy, 0, true).input, BTPOLICY_INPUT_STANDBY);
}

int main(void)
{
	RUN(a_stored_keyboard_is_wanted_whatever_its_advert_carries);
	RUN(a_stored_keyboard_waits_while_a_window_is_open);
	RUN(an_unknown_keyboard_needs_an_open_window);
	RUN(an_unknown_keyboard_needs_the_hid_service_undirected);
	RUN(a_second_stored_keyboard_connects_beside_a_ready_one);
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
	RUN(every_slot_ready_scans_only_while_the_window_is_open);
	RUN(a_failed_keyboard_leaves_the_mouse_slot_free_to_connect);
	RUN(a_pairing_window_scans_actively_for_scan_responses);
	RUN(a_device_reconnects_into_the_slot_it_last_held);
	RUN(a_new_device_takes_a_never_used_slot_before_the_longest_idle);
	RUN(a_new_device_that_failed_takes_its_slot_back);
	RUN(a_stored_device_is_ignored_while_every_link_is_held);
	RUN(pairing_with_every_link_held_drops_the_standby_with_the_oldest_input);
	RUN(pairing_never_drops_an_active_device);
	RUN(a_press_on_a_standby_device_takes_over_and_a_release_does_not);
	RUN(a_keyboard_and_a_mouse_are_active_side_by_side);
	RUN(the_first_device_of_a_kind_is_active_once_ready);
	RUN(a_ready_device_that_is_not_active_is_standby);
	RUN(an_active_device_that_disconnects_hands_over_to_the_latest_standby);
	RUN(a_pressed_standby_outranks_one_only_ready_since);
	RUN(among_standbys_never_pressed_the_latest_ready_takes_over);
	RUN(a_standby_device_that_disconnects_leaves_the_active_one);
	RUN(an_active_device_alone_of_its_kind_leaves_its_kind_without_one);
	RUN(a_seeded_slot_takes_its_device_back);
	RUN(seeding_stops_once_every_slot_is_used);
	RUN(a_device_without_a_slot_takes_the_least_recent_seeded_one);
	RUN(a_device_is_found_in_the_slot_that_last_held_it);
	RUN(the_slot_idle_longest_is_found_whatever_the_uptime);
	RUN(a_slot_that_failed_long_ago_is_free_again);
	RUN(input_from_a_slot_not_ready_is_standby);
	return TEST_EXIT();
}
