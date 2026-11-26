#include <string.h>
#include "btseen.h"
#include "test.h"

static struct btpolicy_advert advert(uint8_t last, enum device_kind kind, const char *name)
{
	struct btpolicy_advert a = {
		.addr = {1, 0x10, 0x20, 0x30, 0x40, 0x50, last},
		.connectable = true,
		.kind = kind,
	};

	strncpy(a.name, name, sizeof(a.name) - 1);
	return a;
}

static bool has_name(const struct diag_heard *h, const char *name)
{
	char want[DIAG_NAME_MAX] = {0};

	strncpy(want, name, sizeof(want));
	return memcmp(h->name, want, sizeof(want)) == 0;
}

static void a_new_device_is_listed_with_its_signal_and_name(void)
{
	struct btseen s = {0};
	struct diag_heard out[BTSEEN_MAX];
	struct btpolicy_advert a = advert(1, DEVICE_KIND_MOUSE, "MX Master 3S");

	a.bolt = true;
	a.rssi = -60;
	btseen_heard(&s, &a, 1000);
	CHECK_EQ(btseen_fresh(&s, 1000, out), 1);
	CHECK_MEM(out[0].addr, a.addr, BTPOLICY_ADDR_LEN);
	CHECK_EQ(out[0].kind, DEVICE_KIND_MOUSE);
	CHECK(out[0].flags & DIAG_HEARD_BOLT);
	CHECK_EQ(out[0].rssi, -60);
	CHECK(has_name(&out[0], "MX Master 3S"));
}

static void hearing_a_device_again_updates_its_signal_and_keeps_its_name(void)
{
	struct btseen s = {0};
	struct diag_heard out[BTSEEN_MAX];
	struct btpolicy_advert named = advert(1, DEVICE_KIND_KEYBOARD, "MX Keys");
	struct btpolicy_advert unnamed = advert(1, DEVICE_KIND_KEYBOARD, "");

	named.rssi = -70;
	btseen_heard(&s, &named, 0);
	unnamed.rssi = -55;
	btseen_heard(&s, &unnamed, 100);
	CHECK_EQ(btseen_fresh(&s, 100, out), 1);
	CHECK_EQ(out[0].rssi, -55);
	CHECK(has_name(&out[0], "MX Keys"));
}

static void a_device_not_heard_for_a_while_drops_out(void)
{
	struct btseen s = {0};
	struct diag_heard out[BTSEEN_MAX];
	struct btpolicy_advert a = advert(1, DEVICE_KIND_KEYBOARD, "");

	a.rssi = -60;
	btseen_heard(&s, &a, UINT32_MAX - 1000);
	CHECK_EQ(btseen_fresh(&s, BTSEEN_FRESH_MS - 1002, out), 1);
	CHECK_EQ(btseen_fresh(&s, BTSEEN_FRESH_MS, out), 0);
}

static void devices_keep_the_order_they_were_first_heard_in(void)
{
	struct btseen s = {0};
	struct diag_heard out[BTSEEN_MAX];
	struct btpolicy_advert far = advert(1, DEVICE_KIND_KEYBOARD, "far");
	struct btpolicy_advert near = advert(2, DEVICE_KIND_MOUSE, "near");

	far.rssi = -80;
	btseen_heard(&s, &far, 0);
	near.rssi = -40;
	btseen_heard(&s, &near, 0);
	far.rssi = -90;
	btseen_heard(&s, &far, 100);
	CHECK_EQ(btseen_fresh(&s, 100, out), 2);
	CHECK(has_name(&out[0], "far"));
	CHECK(has_name(&out[1], "near"));
}

static void a_full_table_replaces_the_device_heard_longest_ago(void)
{
	struct btseen s = {0};
	struct diag_heard out[BTSEEN_MAX];

	for (uint8_t i = 0; i < BTSEEN_MAX; i++) {
		struct btpolicy_advert a = advert(i, DEVICE_KIND_KEYBOARD, "");

		a.rssi = -60;
		btseen_heard(&s, &a, i == 3 ? 0 : 10);
	}
	struct btpolicy_advert newcomer = advert(99, DEVICE_KIND_MOUSE, "");

	newcomer.rssi = -60;
	btseen_heard(&s, &newcomer, 20);
	CHECK_EQ(btseen_fresh(&s, 20, out), BTSEEN_MAX);
	for (uint8_t i = 0; i < BTSEEN_MAX; i++) {
		CHECK(out[i].addr[6] != 3);
	}
}

static void a_device_is_found_only_while_fresh(void)
{
	struct btseen s = {0};
	struct btpolicy_advert a = advert(1, DEVICE_KIND_KEYBOARD, "MX Keys");
	struct btpolicy_advert other = advert(2, DEVICE_KIND_KEYBOARD, "");
	const struct diag_heard *found;

	btseen_heard(&s, &a, 0);
	found = btseen_find(&s, a.addr, 100);
	CHECK(found != NULL && has_name(found, "MX Keys"));
	CHECK(btseen_find(&s, other.addr, 100) == NULL);
	CHECK(btseen_find(&s, a.addr, BTSEEN_FRESH_MS) == NULL);
}

static void clearing_empties_the_list(void)
{
	struct btseen s = {0};
	struct diag_heard out[BTSEEN_MAX];
	struct btpolicy_advert a = advert(1, DEVICE_KIND_KEYBOARD, "");

	a.rssi = -60;
	btseen_heard(&s, &a, 0);
	btseen_clear(&s);
	CHECK_EQ(btseen_fresh(&s, 0, out), 0);
}

int main(void)
{
	RUN(a_new_device_is_listed_with_its_signal_and_name);
	RUN(hearing_a_device_again_updates_its_signal_and_keeps_its_name);
	RUN(a_device_not_heard_for_a_while_drops_out);
	RUN(devices_keep_the_order_they_were_first_heard_in);
	RUN(a_full_table_replaces_the_device_heard_longest_ago);
	RUN(a_device_is_found_only_while_fresh);
	RUN(clearing_empties_the_list);
	return TEST_EXIT();
}
