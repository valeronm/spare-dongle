#include <string.h>

#include "btpolicy.h"

enum device_kind btpolicy_kind_of(uint16_t appearance)
{
	switch (appearance) {
	case BTPOLICY_APPEARANCE_KEYBOARD:
		return DEVICE_KIND_KEYBOARD;
	case BTPOLICY_APPEARANCE_MOUSE:
		return DEVICE_KIND_MOUSE;
	default:
		return DEVICE_NO_KIND;
	}
}

void btpolicy_init(struct btpolicy *p)
{
	*p = (struct btpolicy){0};
}

static bool setting_up(const struct btpolicy *p)
{
	for (int k = 0; k < DEVICE_KINDS; k++) {
		if (p->slots[k].state == BTPOLICY_CONNECTING ||
		    p->slots[k].state == BTPOLICY_CONNECTED) {
			return true;
		}
	}
	return false;
}

static bool hush_running(const struct btpolicy_hush *h, uint32_t now_ms)
{
	return h->on && now_ms - h->heard_ms < BTPOLICY_QUIET_MS;
}

/* Each advert a running hush ignores restarts its quiet time. */
static bool hushed(struct btpolicy *p, const struct btpolicy_advert *a, uint32_t now_ms)
{
	for (int k = 0; k < DEVICE_KINDS; k++) {
		struct btpolicy_hush *h = &p->hushes[k];

		if (!h->on || memcmp(h->addr, a->addr, BTPOLICY_ADDR_LEN) != 0) {
			continue;
		}
		if (hush_running(h, now_ms)) {
			h->heard_ms = now_ms;
			return true;
		}
		h->on = false;
	}
	return false;
}

bool btpolicy_interesting(const struct btpolicy_advert *a)
{
	return a->connectable && (a->stored || (a->hid_service && a->kind != DEVICE_NO_KIND));
}

enum btpolicy_want btpolicy_wants(struct btpolicy *p, const struct btpolicy_advert *a,
				  bool window_open, uint8_t stored_count, uint32_t now_ms)
{
	const struct btpolicy_slot *s;

	if (!btpolicy_interesting(a) || hushed(p, a, now_ms) || a->kind == DEVICE_NO_KIND ||
	    setting_up(p)) {
		return BTPOLICY_IGNORE;
	}
	s = &p->slots[a->kind];
	if (s->dropping) {
		return BTPOLICY_IGNORE;
	}
	if (a->stored) {
		bool waited = !s->retry_pending || (int32_t)(now_ms - s->retry_at_ms) >= 0;

		return s->state == BTPOLICY_IDLE && !window_open && waited ? BTPOLICY_CONNECT
									    : BTPOLICY_IGNORE;
	}
	if (!window_open || a->directed || stored_count >= BTPOLICY_STORED_MAX) {
		return BTPOLICY_IGNORE;
	}
	return s->state == BTPOLICY_IDLE ? BTPOLICY_CONNECT : BTPOLICY_REPLACE;
}

void btpolicy_connecting(struct btpolicy *p, const struct btpolicy_advert *a)
{
	struct btpolicy_slot *s = &p->slots[a->kind];

	s->state = BTPOLICY_CONNECTING;
	memcpy(s->peer, a->addr, BTPOLICY_ADDR_LEN);
	s->peer_stored = a->stored;
}

void btpolicy_dropping(struct btpolicy *p, enum device_kind kind)
{
	p->slots[kind].dropping = true;
}

void btpolicy_connected(struct btpolicy *p, enum device_kind kind)
{
	p->slots[kind].state = BTPOLICY_CONNECTED;
}

const uint8_t *btpolicy_peer(const struct btpolicy *p, enum device_kind kind)
{
	return p->slots[kind].state == BTPOLICY_IDLE ? NULL : p->slots[kind].peer;
}

void btpolicy_paired(struct btpolicy *p, enum device_kind kind)
{
	p->slots[kind].peer_stored = true;
}

void btpolicy_ready(struct btpolicy *p, enum device_kind kind)
{
	p->slots[kind].state = BTPOLICY_READY;
}

bool btpolicy_is_ready(const struct btpolicy *p, enum device_kind kind)
{
	return p->slots[kind].state == BTPOLICY_READY;
}

void btpolicy_disconnected(struct btpolicy *p, enum device_kind kind, uint32_t now_ms,
			   bool never_joined)
{
	struct btpolicy_slot *s = &p->slots[kind];
	bool failed = s->state != BTPOLICY_READY && s->peer_stored;
	bool quick = failed && never_joined && s->quick_retries < BTPOLICY_QUICK_RETRIES;

	s->quick_retries = quick ? s->quick_retries + 1 : 0;
	s->retry_pending = failed && !quick;
	s->retry_at_ms = now_ms + BTPOLICY_RETRY_MS;
	s->state = BTPOLICY_IDLE;
	s->dropping = false;
}

enum btpolicy_security btpolicy_security_failed(struct btpolicy *p, enum device_kind kind,
						enum btpolicy_failure why)
{
	struct btpolicy_slot *s = &p->slots[kind];

	if (s->peer_stored) {
		return why == BTPOLICY_FAILURE_KEY_MISSING ? BTPOLICY_BOND_LOST
							   : BTPOLICY_SECURITY_FAILED;
	}
	return why == BTPOLICY_FAILURE_REFUSAL ? BTPOLICY_PAIRING_REFUSED : BTPOLICY_SECURITY_FAILED;
}

enum btpolicy_scan btpolicy_scan(const struct btpolicy *p, bool window_open, uint32_t now_ms)
{
	bool slot_free = false;
	bool fast = false;

	if (setting_up(p)) {
		return BTPOLICY_SCAN_OFF;
	}
	if (window_open) {
		return BTPOLICY_SCAN_ACTIVE;
	}
	for (int k = 0; k < DEVICE_KINDS; k++) {
		const struct btpolicy_hush *h = &p->hushes[k];

		slot_free |= p->slots[k].state == BTPOLICY_IDLE;
		fast |= hush_running(h, now_ms) && now_ms - h->dropped_ms < BTPOLICY_FAST_SCAN_MS;
	}
	if (fast) {
		return BTPOLICY_SCAN_PASSIVE_FAST;
	}
	return slot_free ? BTPOLICY_SCAN_PASSIVE : BTPOLICY_SCAN_OFF;
}

void btpolicy_hush(struct btpolicy *p, enum device_kind kind, uint32_t now_ms)
{
	struct btpolicy_slot *s = &p->slots[kind];
	struct btpolicy_hush *h = &p->hushes[kind];

	if (s->state == BTPOLICY_IDLE) {
		return;
	}
	s->dropping = true;
	h->on = true;
	memcpy(h->addr, s->peer, BTPOLICY_ADDR_LEN);
	h->dropped_ms = now_ms;
	h->heard_ms = now_ms;
}

void btpolicy_unhush(struct btpolicy *p)
{
	for (int k = 0; k < DEVICE_KINDS; k++) {
		p->hushes[k].on = false;
	}
}

bool btpolicy_pairs_by_clicks(enum device_kind kind, bool bolt)
{
	return bolt && kind == DEVICE_KIND_MOUSE;
}

uint32_t btpolicy_click_passkey(uint32_t passkey)
{
	return passkey % (1u << DIAG_CLICK_PASSKEY_BITS);
}
