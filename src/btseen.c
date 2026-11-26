#include <string.h>

#include "btseen.h"

_Static_assert(BTPOLICY_ADDR_LEN == DIAG_ADDR_LEN, "a heard record carries the whole address");

void btseen_clear(struct btseen *s)
{
	s->count = 0;
}

static bool fresh(const struct btseen *s, uint8_t i, uint32_t now_ms)
{
	return now_ms - s->devices[i].heard_ms < BTSEEN_FRESH_MS;
}

static uint8_t index_of(const struct btseen *s, const uint8_t *addr)
{
	for (uint8_t i = 0; i < s->count; i++) {
		if (memcmp(s->devices[i].heard.addr, addr, BTPOLICY_ADDR_LEN) == 0) {
			return i;
		}
	}
	return BTSEEN_MAX;
}

static uint8_t slot_for(struct btseen *s, const uint8_t *addr, uint32_t now_ms)
{
	uint8_t at = index_of(s, addr);

	if (at < BTSEEN_MAX) {
		return at;
	}
	if (s->count < BTSEEN_MAX) {
		at = s->count++;
	} else {
		at = 0;
		for (uint8_t i = 1; i < s->count; i++) {
			if (now_ms - s->devices[i].heard_ms > now_ms - s->devices[at].heard_ms) {
				at = i;
			}
		}
	}
	s->devices[at].heard = (struct diag_heard){0};
	memcpy(s->devices[at].heard.addr, addr, BTPOLICY_ADDR_LEN);
	return at;
}

void btseen_heard(struct btseen *s, const struct btpolicy_advert *a, uint32_t now_ms)
{
	uint8_t at = slot_for(s, a->addr, now_ms);
	struct diag_heard *h = &s->devices[at].heard;

	s->devices[at].heard_ms = now_ms;
	h->kind = a->kind;
	h->flags |= a->bolt ? DIAG_HEARD_BOLT : 0;
	h->rssi = a->rssi;
	if (a->name[0] != '\0') {
		strncpy((char *)h->name, a->name, sizeof(h->name));
	}
}

size_t btseen_fresh(const struct btseen *s, uint32_t now_ms, struct diag_heard out[BTSEEN_MAX])
{
	size_t n = 0;

	for (uint8_t i = 0; i < s->count; i++) {
		if (fresh(s, i, now_ms)) {
			out[n++] = s->devices[i].heard;
		}
	}
	return n;
}

const struct diag_heard *btseen_find(const struct btseen *s, const uint8_t addr[BTPOLICY_ADDR_LEN],
				     uint32_t now_ms)
{
	uint8_t at = index_of(s, addr);

	return at < BTSEEN_MAX && fresh(s, at, now_ms) ? &s->devices[at].heard : NULL;
}
