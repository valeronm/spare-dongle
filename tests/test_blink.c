#include "test.h"
#include "blink.h"

#define STEPS(ms) ((ms) / BLINK_STEP_MS)

static uint32_t lit_ms(enum indicator_mode mode, uint32_t from_ms, uint32_t ms)
{
	uint32_t lit = 0;

	for (uint32_t step = STEPS(from_ms); step < STEPS(from_ms + ms); step++) {
		lit += blink_lit(mode, step) ? BLINK_STEP_MS : 0;
	}
	return lit;
}

static uint32_t flashes(enum indicator_mode mode, uint32_t from_ms, uint32_t ms)
{
	uint32_t n = 0;

	for (uint32_t step = STEPS(from_ms); step < STEPS(from_ms + ms); step++) {
		n += blink_lit(mode, step) && (step == 0 || !blink_lit(mode, step - 1));
	}
	return n;
}

static void test_nothing_connected_is_a_short_flash_every_second(void)
{
	for (uint32_t second = 0; second < 5; second++) {
		CHECK_EQ(flashes(IND_IDLE, second * 1000, 1000), 1);
		CHECK_EQ(lit_ms(IND_IDLE, second * 1000, 1000), BLINK_STEP_MS);
	}
}

static void test_pairing_open_is_a_fast_blink(void)
{
	for (uint32_t second = 0; second < 5; second++) {
		CHECK_EQ(flashes(IND_PAIRING, second * 1000, 1000), 5);
		CHECK_EQ(lit_ms(IND_PAIRING, second * 1000, 1000), 500);
	}
}

static void test_connected_is_solid(void)
{
	CHECK_EQ(lit_ms(IND_CONNECTED, 0, 5000), 5000);
}

static void test_a_suspended_pc_is_off(void)
{
	CHECK_EQ(lit_ms(IND_SLEEP, 0, 5000), 0);
}

static void test_a_failed_radio_is_three_flashes_every_two_seconds(void)
{
	for (uint32_t period = 0; period < 3; period++) {
		CHECK_EQ(flashes(IND_ERROR, period * 2000, 2000), 3);
	}
}

int main(void)
{
	RUN(test_nothing_connected_is_a_short_flash_every_second);
	RUN(test_pairing_open_is_a_fast_blink);
	RUN(test_connected_is_solid);
	RUN(test_a_suspended_pc_is_off);
	RUN(test_a_failed_radio_is_three_flashes_every_two_seconds);
	return TEST_EXIT();
}
