#include <string.h>
#include "btpair.h"
#include "test.h"

static struct btpair pair;
static struct btpolicy policy;

static struct btpolicy_advert new_advert(uint8_t last, enum device_kind kind, const char *name)
{
	struct btpolicy_advert a = {
		.addr = {1, 9, 9, 9, 9, 9, last},
		.connectable = true,
		.hid_service = true,
		.kind = kind,
	};

	strncpy(a.name, name, sizeof(a.name) - 1);
	return a;
}

static struct btpolicy_advert new_keyboard(uint8_t last)
{
	return new_advert(last, DEVICE_KIND_KEYBOARD, "MX Keys");
}

static struct btpolicy_advert new_mouse(uint8_t last)
{
	return new_advert(last, DEVICE_KIND_MOUSE, "MX Master 3S");
}

static void start(void)
{
	btpair_init(&pair);
	btpolicy_init(&policy);
}

static enum btpolicy_want wants(const struct btpolicy_advert *a, uint32_t now_ms)
{
	return btpair_wants(&pair, &policy, a, 0, now_ms);
}

static struct diag_status status(uint32_t now_ms)
{
	struct diag_status st = {0};

	btpair_status(&pair, now_ms, &st);
	return st;
}

static bool named(const uint8_t name[DIAG_NAME_MAX], const char *want)
{
	uint8_t padded[DIAG_NAME_MAX] = {0};

	strncpy((char *)padded, want, sizeof(padded));
	return memcmp(name, padded, sizeof(padded)) == 0;
}

static void a_closed_window_takes_no_new_device(void)
{
	struct btpolicy_advert keyboard = new_keyboard(1);

	start();
	CHECK_EQ(status(0).pairing_window, DIAG_WINDOW_CLOSED);
	CHECK_EQ(wants(&keyboard, 0), BTPOLICY_IGNORE);
	CHECK(!btpair_is_open(&pair, 0));
}

static void the_window_opened_at_plug_in_takes_the_first_new_device(void)
{
	struct btpolicy_advert keyboard = new_keyboard(1);

	start();
	btpair_open_at_boot(&pair, 0);
	CHECK_EQ(status(0).pairing_window, DIAG_WINDOW_OPEN);
	CHECK_EQ(wants(&keyboard, 0), BTPOLICY_CONNECT);
	CHECK(btpair_is_open(&pair, 10 * DIAG_PAIR_WINDOW_MS));
}

static void a_window_opened_to_pick_lists_the_new_devices_it_hears(void)
{
	struct btpolicy_advert mouse = new_mouse(1);
	struct diag_heard heard[BTSEEN_MAX];

	start();
	btpair_open_to_pick(&pair, 0);
	CHECK_EQ(status(0).pairing_window, DIAG_WINDOW_PICKING);
	CHECK_EQ(wants(&mouse, 0), BTPOLICY_IGNORE);
	CHECK_EQ(btpair_heard(&pair, 0, heard), 1);
	CHECK_MEM(heard[0].addr, mouse.addr, BTPOLICY_ADDR_LEN);
}

static void a_stored_or_unconnectable_device_is_not_listed(void)
{
	struct btpolicy_advert stored = new_keyboard(1);
	struct btpolicy_advert unconnectable = new_keyboard(2);
	struct diag_heard heard[BTSEEN_MAX];

	stored.stored = true;
	unconnectable.connectable = false;
	start();
	btpair_open_to_pick(&pair, 0);
	CHECK_EQ(wants(&stored, 0), BTPOLICY_IGNORE);
	CHECK_EQ(wants(&unconnectable, 0), BTPOLICY_IGNORE);
	CHECK_EQ(btpair_heard(&pair, 0, heard), 0);
}

static void a_pick_names_the_device_and_wants_only_it(void)
{
	struct btpolicy_advert mouse = new_mouse(1);
	struct btpolicy_advert keyboard = new_keyboard(2);
	struct diag_heard heard[BTSEEN_MAX];
	struct diag_status st;

	start();
	btpair_open_to_pick(&pair, 0);
	wants(&mouse, 0);
	wants(&keyboard, 0);
	CHECK(btpair_pick(&pair, keyboard.addr, 100));
	st = status(100);
	CHECK_EQ(st.pairing_window, DIAG_WINDOW_PICKED);
	CHECK(named(st.pairing_name, "MX Keys"));
	CHECK_EQ(wants(&mouse, 100), BTPOLICY_IGNORE);
	CHECK_EQ(wants(&keyboard, 100), BTPOLICY_CONNECT);
	CHECK_EQ(btpair_heard(&pair, 100, heard), 0);
}

static void only_a_device_heard_while_picking_can_be_picked(void)
{
	struct btpolicy_advert keyboard = new_keyboard(1);
	struct btpolicy_advert mouse = new_mouse(2);

	start();
	CHECK(!btpair_pick(&pair, keyboard.addr, 0));
	btpair_open_at_boot(&pair, 0);
	wants(&keyboard, 0);
	CHECK(!btpair_pick(&pair, keyboard.addr, 0));
	btpair_open_to_pick(&pair, 0);
	wants(&keyboard, 0);
	CHECK(!btpair_pick(&pair, mouse.addr, 0));
	CHECK(!btpair_pick(&pair, keyboard.addr, BTSEEN_FRESH_MS));
	CHECK(btpair_pick(&pair, keyboard.addr, 100));
	CHECK(!btpair_pick(&pair, keyboard.addr, 100));
}

static void a_window_that_runs_out_reads_closed_and_forgets_the_pick(void)
{
	struct btpolicy_advert keyboard = new_keyboard(1);
	struct diag_heard heard[BTSEEN_MAX];

	start();
	btpair_open_to_pick(&pair, 0);
	wants(&keyboard, 0);
	btpair_pick(&pair, keyboard.addr, 0);
	CHECK(!pairwin_ran_out(&pair.window, DIAG_PICK_WINDOW_MS - 1));
	CHECK_EQ(status(DIAG_PICK_WINDOW_MS).pairing_window, DIAG_WINDOW_CLOSED);
	CHECK_EQ(wants(&keyboard, DIAG_PICK_WINDOW_MS), BTPOLICY_IGNORE);
	CHECK(pairwin_ran_out(&pair.window, DIAG_PICK_WINDOW_MS));
	CHECK(!pairwin_ran_out(&pair.window, DIAG_PICK_WINDOW_MS));
	btpair_open_to_pick(&pair, DIAG_PICK_WINDOW_MS);
	CHECK_EQ(status(DIAG_PICK_WINDOW_MS).pairing_window, DIAG_WINDOW_PICKING);
	CHECK_EQ(btpair_heard(&pair, DIAG_PICK_WINDOW_MS, heard), 0);
}

static void a_clicked_passkey_holds_the_window_and_counts_the_clicks(void)
{
	struct btpolicy_advert mouse = new_mouse(1);
	struct diag_status st;

	start();
	btpair_open_to_pick(&pair, 0);
	wants(&mouse, 0);
	btpair_pick(&pair, mouse.addr, 0);
	CHECK(btpair_passkey_awaited(&pair, DEVICE_KIND_MOUSE, 623, true, "MX Master 3S", 100));
	CHECK(btpair_keypress(&pair, DEVICE_KIND_MOUSE, BTPOLICY_KEY_ENTERED));
	CHECK(!btpair_keypress(&pair, DEVICE_KIND_KEYBOARD, BTPOLICY_KEY_ENTERED));
	st = status(10 * DIAG_PICK_WINDOW_MS);
	CHECK_EQ(st.pairing_window, DIAG_WINDOW_PASSKEY_CLICKED);
	CHECK_EQ(st.passkey, 623);
	CHECK_EQ(st.passkey_entered, 1);
	CHECK(named(st.pairing_name, "MX Master 3S"));
	CHECK(!pairwin_end_attempt(&pair.window, DEVICE_KIND_KEYBOARD));
	CHECK(pairwin_end_attempt(&pair.window, DEVICE_KIND_MOUSE));
	CHECK_EQ(status(10 * DIAG_PICK_WINDOW_MS).pairing_window, DIAG_WINDOW_CLOSED);
}

static void the_pick_holds_while_the_picked_device_waits_for_its_passkey(void)
{
	struct btpolicy_advert keyboard = new_keyboard(1);
	struct btpolicy_advert mouse = new_mouse(2);

	start();
	btpair_open_to_pick(&pair, 0);
	wants(&keyboard, 0);
	btpair_pick(&pair, keyboard.addr, 0);
	btpair_passkey_awaited(&pair, DEVICE_KIND_KEYBOARD, 123456, false, "MX Keys", 0);
	CHECK_EQ(wants(&mouse, 0), BTPOLICY_IGNORE);
}

static void a_typed_passkey_counts_no_keypresses(void)
{
	struct diag_status st;

	start();
	btpair_open_at_boot(&pair, 0);
	CHECK(btpair_passkey_awaited(&pair, DEVICE_KIND_KEYBOARD, 123456, false, "MX Keys", 0));
	CHECK(!btpair_keypress(&pair, DEVICE_KIND_KEYBOARD, BTPOLICY_KEY_ENTERED));
	st = status(0);
	CHECK_EQ(st.pairing_window, DIAG_WINDOW_PASSKEY_AWAITED);
	CHECK_EQ(st.passkey, 123456);
	CHECK_EQ(st.passkey_entered, 0);
}

static void a_passkey_in_a_closed_window_is_not_held(void)
{
	start();
	CHECK(!btpair_passkey_awaited(&pair, DEVICE_KIND_KEYBOARD, 123456, false, "MX Keys", 0));
	CHECK_EQ(status(0).pairing_window, DIAG_WINDOW_CLOSED);
}

static uint8_t entered_after(const enum btpolicy_keypress *presses, size_t n)
{
	start();
	btpair_open_at_boot(&pair, 0);
	btpair_passkey_awaited(&pair, DEVICE_KIND_MOUSE, 623, true, "", 0);
	for (size_t i = 0; i < n; i++) {
		btpair_keypress(&pair, DEVICE_KIND_MOUSE, presses[i]);
	}
	return status(0).passkey_entered;
}

static void the_entered_count_follows_the_keypresses(void)
{
	enum btpolicy_keypress two[] = {BTPOLICY_KEY_ENTERED, BTPOLICY_KEY_ENTERED};
	enum btpolicy_keypress erased[] = {BTPOLICY_KEY_ENTERED, BTPOLICY_KEY_ENTERED,
					   BTPOLICY_KEY_ERASED};
	enum btpolicy_keypress restarted[] = {BTPOLICY_KEY_ENTERED, BTPOLICY_ENTRY_STARTED};
	enum btpolicy_keypress cleared[] = {BTPOLICY_KEY_ENTERED, BTPOLICY_ENTRY_CLEARED};
	enum btpolicy_keypress completed[] = {BTPOLICY_KEY_ENTERED, BTPOLICY_ENTRY_COMPLETED};

	CHECK_EQ(entered_after(two, 2), 2);
	CHECK_EQ(entered_after(erased, 3), 1);
	CHECK_EQ(entered_after(restarted, 2), 0);
	CHECK_EQ(entered_after(cleared, 2), 0);
	CHECK_EQ(entered_after(completed, 2), DIAG_CLICK_PASSKEY_BITS);
}

static void the_entered_count_stays_within_the_passkey(void)
{
	enum btpolicy_keypress erased[] = {BTPOLICY_KEY_ERASED};
	enum btpolicy_keypress many[DIAG_CLICK_PASSKEY_BITS + 2];

	for (size_t i = 0; i < DIAG_CLICK_PASSKEY_BITS + 2; i++) {
		many[i] = BTPOLICY_KEY_ENTERED;
	}
	CHECK_EQ(entered_after(erased, 1), 0);
	CHECK_EQ(entered_after(many, DIAG_CLICK_PASSKEY_BITS + 2), DIAG_CLICK_PASSKEY_BITS);
}

static void a_kept_pairing_closes_the_window(void)
{
	struct btpolicy_advert keyboard = new_keyboard(1);

	start();
	btpair_open_to_pick(&pair, 0);
	wants(&keyboard, 0);
	btpair_pick(&pair, keyboard.addr, 0);
	btpolicy_connecting(&policy, &keyboard);
	btpair_paired(&pair, &policy, DEVICE_KIND_KEYBOARD);
	CHECK_EQ(status(0).pairing_window, DIAG_WINDOW_CLOSED);
	CHECK_EQ(btpolicy_security_failed(&policy, DEVICE_KIND_KEYBOARD, BTPOLICY_FAILURE_KEY_MISSING),
		 BTPOLICY_BOND_LOST);
}

static void a_refused_device_is_ignored_until_the_window_opens_again(void)
{
	struct btpolicy_advert keyboard = new_keyboard(1);
	struct btpolicy_advert another = new_keyboard(2);

	start();
	btpair_open_at_boot(&pair, 0);
	btpolicy_connecting(&policy, &keyboard);
	CHECK_EQ(btpair_security_failed(&pair, &policy, DEVICE_KIND_KEYBOARD,
					BTPOLICY_FAILURE_REFUSAL),
		 BTPOLICY_PAIRING_REFUSED);
	CHECK_EQ(btpair_security_failed(&pair, &policy, DEVICE_KIND_KEYBOARD,
					BTPOLICY_FAILURE_REFUSAL),
		 BTPOLICY_REFUSED_BEFORE);
	btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 100, false);
	CHECK_EQ(wants(&keyboard, 100), BTPOLICY_IGNORE);
	CHECK_EQ(wants(&another, 100), BTPOLICY_CONNECT);
	btpair_open_at_boot(&pair, 200);
	CHECK_EQ(wants(&keyboard, 200), BTPOLICY_CONNECT);
}

static void a_pairing_cut_short_by_a_lost_link_is_not_a_refusal(void)
{
	struct btpolicy_advert keyboard = new_keyboard(1);

	start();
	btpair_open_at_boot(&pair, 0);
	btpolicy_connecting(&policy, &keyboard);
	CHECK_EQ(btpair_security_failed(&pair, &policy, DEVICE_KIND_KEYBOARD, BTPOLICY_FAILURE_LINK),
		 BTPOLICY_SECURITY_FAILED);
	btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 100, false);
	CHECK_EQ(wants(&keyboard, 100), BTPOLICY_CONNECT);
}

static void more_refusals_than_fit_are_still_reported_once_each(void)
{
	start();
	btpair_open_at_boot(&pair, 0);
	for (uint8_t i = 0; i < BTPOLICY_STORED_MAX + 2; i++) {
		struct btpolicy_advert a = new_keyboard(i);

		btpolicy_connecting(&policy, &a);
		CHECK_EQ(btpair_security_failed(&pair, &policy, DEVICE_KIND_KEYBOARD,
						BTPOLICY_FAILURE_REFUSAL),
			 BTPOLICY_PAIRING_REFUSED);
		btpolicy_disconnected(&policy, DEVICE_KIND_KEYBOARD, 0, false);
	}
}

int main(void)
{
	RUN(a_closed_window_takes_no_new_device);
	RUN(the_window_opened_at_plug_in_takes_the_first_new_device);
	RUN(a_window_opened_to_pick_lists_the_new_devices_it_hears);
	RUN(a_stored_or_unconnectable_device_is_not_listed);
	RUN(a_pick_names_the_device_and_wants_only_it);
	RUN(only_a_device_heard_while_picking_can_be_picked);
	RUN(a_window_that_runs_out_reads_closed_and_forgets_the_pick);
	RUN(a_clicked_passkey_holds_the_window_and_counts_the_clicks);
	RUN(the_pick_holds_while_the_picked_device_waits_for_its_passkey);
	RUN(a_typed_passkey_counts_no_keypresses);
	RUN(a_passkey_in_a_closed_window_is_not_held);
	RUN(the_entered_count_follows_the_keypresses);
	RUN(the_entered_count_stays_within_the_passkey);
	RUN(a_kept_pairing_closes_the_window);
	RUN(a_refused_device_is_ignored_until_the_window_opens_again);
	RUN(a_pairing_cut_short_by_a_lost_link_is_not_a_refusal);
	RUN(more_refusals_than_fit_are_still_reported_once_each);
	return TEST_EXIT();
}
