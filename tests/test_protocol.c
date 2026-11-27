#include "test.h"
#include "protocol.h"

static void test_a_stored_protocol_is_used_and_not_saved_again(void)
{
	bool save = true;

	CHECK_EQ(protocol_from_stored(true, PROTOCOL_NUPHY, &save), PROTOCOL_NUPHY);
	CHECK(!save);
	CHECK_EQ(protocol_from_stored(true, PROTOCOL_UNIFYING, &save), PROTOCOL_UNIFYING);
	CHECK_EQ(protocol_from_stored(true, PROTOCOL_BLUETOOTH, &save), PROTOCOL_BLUETOOTH);
}

static void test_nothing_stored_runs_bluetooth_and_saves_it(void)
{
	bool save = false;

	CHECK_EQ(protocol_from_stored(false, PROTOCOL_UNIFYING, &save), PROTOCOL_BLUETOOTH);
	CHECK(save);
}

static void test_an_unknown_stored_value_runs_bluetooth_and_is_kept(void)
{
	bool save = true;

	CHECK_EQ(protocol_from_stored(true, 0, &save), PROTOCOL_BLUETOOTH);
	CHECK(!save);
	CHECK_EQ(protocol_from_stored(true, 4, &save), PROTOCOL_BLUETOOTH);
	CHECK_EQ(protocol_from_stored(true, 0xff, &save), PROTOCOL_BLUETOOTH);
	CHECK(!save);
}

static void test_an_arg_names_a_protocol_or_none(void)
{
	enum protocol p = PROTOCOL_NUPHY;

	CHECK(protocol_from_arg(2, &p));
	CHECK_EQ(p, PROTOCOL_UNIFYING);
	CHECK(protocol_from_arg(3, &p));
	CHECK_EQ(p, PROTOCOL_BLUETOOTH);
	CHECK(protocol_from_arg(1, &p));
	CHECK_EQ(p, PROTOCOL_NUPHY);
	CHECK(!protocol_from_arg(0, &p));
	CHECK(!protocol_from_arg(4, &p));
	CHECK(!protocol_from_arg(0x101, &p));
}

static void test_only_the_2g4_protocols_run_esb(void)
{
	CHECK(diag_protocol_has_esb(PROTOCOL_NUPHY));
	CHECK(diag_protocol_has_esb(PROTOCOL_UNIFYING));
	CHECK(!diag_protocol_has_esb(PROTOCOL_BLUETOOTH));
}

int main(void)
{
	RUN(test_a_stored_protocol_is_used_and_not_saved_again);
	RUN(test_nothing_stored_runs_bluetooth_and_saves_it);
	RUN(test_an_unknown_stored_value_runs_bluetooth_and_is_kept);
	RUN(test_an_arg_names_a_protocol_or_none);
	RUN(test_only_the_2g4_protocols_run_esb);
	return TEST_EXIT();
}
