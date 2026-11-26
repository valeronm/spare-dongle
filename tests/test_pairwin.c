#include "test.h"
#include "pairwin.h"

static void test_a_window_is_closed_until_opened(void)
{
	struct pairwin w = {0};

	CHECK(!pairwin_is_open(&w, 0));
	CHECK(!pairwin_is_open(&w, 100000));
}

static void test_a_window_runs_for_30_s(void)
{
	struct pairwin w = {0};

	pairwin_open(&w, DIAG_PAIR_WINDOW_MS, 5000);
	CHECK(pairwin_is_open(&w, 5000));
	CHECK(pairwin_is_open(&w, 5000 + DIAG_PAIR_WINDOW_MS - 1));
	CHECK(!pairwin_is_open(&w, 5000 + DIAG_PAIR_WINDOW_MS));
}

static void test_a_window_runs_across_a_clock_wrap(void)
{
	struct pairwin w = {0};

	pairwin_open(&w, DIAG_PAIR_WINDOW_MS, UINT32_MAX - 1000);
	CHECK(pairwin_is_open(&w, 1000));
	CHECK(!pairwin_is_open(&w, DIAG_PAIR_WINDOW_MS));
}

static void test_opening_again_restarts_the_30_s(void)
{
	struct pairwin w = {0};

	pairwin_open(&w, DIAG_PAIR_WINDOW_MS, 0);
	pairwin_open(&w, DIAG_PAIR_WINDOW_MS, 20000);
	CHECK(pairwin_is_open(&w, 20000 + DIAG_PAIR_WINDOW_MS - 1));
}

static void test_a_pairing_closes_a_timed_window(void)
{
	struct pairwin w = {0};

	pairwin_open(&w, DIAG_PAIR_WINDOW_MS, 0);
	pairwin_close(&w);
	CHECK(!pairwin_is_open(&w, 1));
}

static void test_a_timed_window_running_out_is_reported_once(void)
{
	struct pairwin w = {0};

	pairwin_open(&w, DIAG_PAIR_WINDOW_MS, 0);
	CHECK(!pairwin_ran_out(&w, DIAG_PAIR_WINDOW_MS - 1));
	CHECK(pairwin_ran_out(&w, DIAG_PAIR_WINDOW_MS));
	CHECK(!pairwin_ran_out(&w, DIAG_PAIR_WINDOW_MS + 1));
}

static void test_a_window_closed_early_does_not_run_out(void)
{
	struct pairwin w = {0};

	pairwin_open(&w, DIAG_PAIR_WINDOW_MS, 0);
	pairwin_close(&w);
	CHECK(!pairwin_ran_out(&w, DIAG_PAIR_WINDOW_MS));
}

static void test_a_held_window_stays_open_past_its_30_s(void)
{
	struct pairwin w = {0};

	pairwin_open(&w, DIAG_PAIR_WINDOW_MS, 0);
	CHECK(pairwin_hold(&w, DEVICE_KIND_KEYBOARD, 0));
	CHECK(pairwin_is_open(&w, 10 * DIAG_PAIR_WINDOW_MS));
	CHECK_EQ(pairwin_holder(&w), DEVICE_KIND_KEYBOARD);
	CHECK(!pairwin_ran_out(&w, 10 * DIAG_PAIR_WINDOW_MS));
}

static void test_the_holders_attempt_ending_closes_the_window_without_running_out(void)
{
	struct pairwin w = {0};

	pairwin_open(&w, DIAG_PAIR_WINDOW_MS, 0);
	pairwin_hold(&w, DEVICE_KIND_KEYBOARD, 0);
	CHECK(pairwin_end_attempt(&w, DEVICE_KIND_KEYBOARD));
	CHECK(!pairwin_is_open(&w, 1));
	CHECK_EQ(pairwin_holder(&w), DEVICE_NO_KIND);
	CHECK(!pairwin_ran_out(&w, DIAG_PAIR_WINDOW_MS));
	CHECK(!pairwin_end_attempt(&w, DEVICE_KIND_KEYBOARD));
}

static void test_another_devices_attempt_ending_leaves_the_hold(void)
{
	struct pairwin w = {0};

	pairwin_open(&w, DIAG_PAIR_WINDOW_MS, 0);
	CHECK(!pairwin_end_attempt(&w, DEVICE_KIND_KEYBOARD));
	CHECK(pairwin_is_open(&w, 1));
	pairwin_hold(&w, DEVICE_KIND_KEYBOARD, 0);
	CHECK(!pairwin_end_attempt(&w, DEVICE_KIND_MOUSE));
	CHECK_EQ(pairwin_holder(&w), DEVICE_KIND_KEYBOARD);
}

static void test_closing_ends_a_held_window(void)
{
	struct pairwin w = {0};

	pairwin_open(&w, DIAG_PAIR_WINDOW_MS, 0);
	pairwin_hold(&w, DEVICE_KIND_KEYBOARD, 0);
	pairwin_close(&w);
	CHECK(!pairwin_is_open(&w, 1));
	CHECK_EQ(pairwin_holder(&w), DEVICE_NO_KIND);
}

static void test_opening_again_drops_the_hold(void)
{
	struct pairwin w = {0};

	pairwin_open(&w, DIAG_PAIR_WINDOW_MS, 0);
	pairwin_hold(&w, DEVICE_KIND_KEYBOARD, 0);
	pairwin_open(&w, DIAG_PAIR_WINDOW_MS, 1000);
	CHECK_EQ(pairwin_holder(&w), DEVICE_NO_KIND);
	CHECK(!pairwin_is_open(&w, 1000 + DIAG_PAIR_WINDOW_MS));
}

static void test_holding_a_closed_window_leaves_it_closed(void)
{
	struct pairwin w = {0};

	CHECK(!pairwin_hold(&w, DEVICE_KIND_KEYBOARD, 0));
	CHECK(!pairwin_is_open(&w, 0));
	CHECK_EQ(pairwin_holder(&w), DEVICE_NO_KIND);
	pairwin_open(&w, DIAG_PAIR_WINDOW_MS, 0);
	CHECK(!pairwin_hold(&w, DEVICE_KIND_KEYBOARD, DIAG_PAIR_WINDOW_MS));
	CHECK(!pairwin_is_open(&w, DIAG_PAIR_WINDOW_MS));
	CHECK(pairwin_ran_out(&w, DIAG_PAIR_WINDOW_MS));
}

static void test_a_window_opened_for_a_length_runs_that_long(void)
{
	struct pairwin w = {0};

	pairwin_open(&w, 60000, 1000);
	CHECK(pairwin_is_open(&w, 60999));
	CHECK(!pairwin_is_open(&w, 61000));
}

static void test_a_held_window_reads_pairing_until_its_attempt_ends(void)
{
	struct pairwin w = {0};

	CHECK_EQ(pairwin_window(&w, 0), DIAG_WINDOW_CLOSED);
	pairwin_open(&w, DIAG_PAIR_WINDOW_MS, 0);
	CHECK_EQ(pairwin_window(&w, 0), DIAG_WINDOW_OPEN);
	CHECK(pairwin_hold(&w, DEVICE_KIND_KEYBOARD, 10));
	CHECK_EQ(pairwin_window(&w, DIAG_PAIR_WINDOW_MS), DIAG_WINDOW_PAIRING);
	CHECK(pairwin_end_attempt(&w, DEVICE_KIND_KEYBOARD));
	CHECK_EQ(pairwin_window(&w, DIAG_PAIR_WINDOW_MS), DIAG_WINDOW_CLOSED);
}

int main(void)
{
	RUN(test_a_held_window_reads_pairing_until_its_attempt_ends);
	RUN(test_a_window_opened_for_a_length_runs_that_long);
	RUN(test_a_window_is_closed_until_opened);
	RUN(test_a_window_runs_for_30_s);
	RUN(test_a_window_runs_across_a_clock_wrap);
	RUN(test_opening_again_restarts_the_30_s);
	RUN(test_a_pairing_closes_a_timed_window);
	RUN(test_a_timed_window_running_out_is_reported_once);
	RUN(test_a_window_closed_early_does_not_run_out);
	RUN(test_a_held_window_stays_open_past_its_30_s);
	RUN(test_the_holders_attempt_ending_closes_the_window_without_running_out);
	RUN(test_another_devices_attempt_ending_leaves_the_hold);
	RUN(test_closing_ends_a_held_window);
	RUN(test_opening_again_drops_the_hold);
	RUN(test_holding_a_closed_window_leaves_it_closed);
	return TEST_EXIT();
}
