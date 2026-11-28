#include <string.h>

#include "btlist.h"
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
	for (int k = 0; k < DEVICE_KINDS; k++) {
		p->active[k] = DEVICE_NO_SLOT;
	}
	p->events = DEVICE_SLOTS;
}

/* btpolicy_wants refuses an advert of no kind. */
static bool can_reconnect(const struct btlist_entry *e)
{
	return e->kind != DEVICE_NO_KIND;
}

static bool setting_up(const struct btpolicy *p)
{
	for (int s = 0; s < DEVICE_SLOTS; s++) {
		if (p->slots[s].state == BTPOLICY_CONNECTING ||
		    p->slots[s].state == BTPOLICY_CONNECTED) {
			return true;
		}
	}
	return false;
}

static bool dropping(const struct btpolicy *p)
{
	for (int s = 0; s < DEVICE_SLOTS; s++) {
		if (p->slots[s].dropping) {
			return true;
		}
	}
	return false;
}

static bool waited(const struct btpolicy_slot *s, uint32_t now_ms)
{
	return !s->retry_pending || (uint32_t)(now_ms - s->failed_at_ms) >= BTPOLICY_RETRY_MS;
}

uint8_t btpolicy_slot_of(const struct btpolicy *p, const uint8_t addr[BTPOLICY_ADDR_LEN])
{
	for (uint8_t s = 0; s < DEVICE_SLOTS; s++) {
		if (p->slots[s].used && memcmp(p->slots[s].peer, addr, BTPOLICY_ADDR_LEN) == 0) {
			return s;
		}
	}
	return DEVICE_NO_SLOT;
}

/* A slot whose device waits out a retry stays that device's. */
static uint8_t free_slot(const struct btpolicy *p, uint32_t now_ms)
{
	uint8_t best = DEVICE_NO_SLOT;

	for (uint8_t s = 0; s < DEVICE_SLOTS; s++) {
		const struct btpolicy_slot *x = &p->slots[s];

		if (x->state != BTPOLICY_IDLE) {
			continue;
		}
		if (!x->used) {
			return s;
		}
		if (!waited(x, now_ms)) {
			continue;
		}
		if (best == DEVICE_NO_SLOT || x->order < p->slots[best].order) {
			best = s;
		}
	}
	return best;
}

static uint8_t standby_to_drop(const struct btpolicy *p)
{
	uint8_t best = DEVICE_NO_SLOT;

	for (uint8_t s = 0; s < DEVICE_SLOTS; s++) {
		const struct btpolicy_slot *x = &p->slots[s];

		if (x->state != BTPOLICY_READY || p->active[x->kind] == s) {
			continue;
		}
		if (best == DEVICE_NO_SLOT || x->order < p->slots[best].order) {
			best = s;
		}
	}
	return best;
}

static struct btpolicy_choice choice(enum btpolicy_want want, uint8_t slot)
{
	if (slot == DEVICE_NO_SLOT) {
		return BTPOLICY_CHOICE_IGNORE;
	}
	return (struct btpolicy_choice){.want = want, .slot = slot};
}

bool btpolicy_interesting(const struct btpolicy_advert *a)
{
	return a->connectable && (a->stored || (a->hid_service && a->kind != DEVICE_NO_KIND));
}

struct btpolicy_choice btpolicy_wants(const struct btpolicy *p, const struct btpolicy_advert *a,
				      bool window_open, uint32_t now_ms)
{
	uint8_t own;
	uint8_t idle;

	if (!btpolicy_interesting(a) || a->kind == DEVICE_NO_KIND || setting_up(p) || dropping(p)) {
		return BTPOLICY_CHOICE_IGNORE;
	}
	if (a->stored ? window_open : (!window_open || a->directed)) {
		return BTPOLICY_CHOICE_IGNORE;
	}
	own = btpolicy_slot_of(p, a->addr);
	if (own != DEVICE_NO_SLOT) {
		const struct btpolicy_slot *s = &p->slots[own];

		return choice(BTPOLICY_CONNECT,
			      s->state == BTPOLICY_IDLE && waited(s, now_ms) ? own : DEVICE_NO_SLOT);
	}
	idle = free_slot(p, now_ms);
	if (idle != DEVICE_NO_SLOT || a->stored) {
		return choice(BTPOLICY_CONNECT, idle);
	}
	return choice(BTPOLICY_REPLACE, standby_to_drop(p));
}

void btpolicy_connecting(struct btpolicy *p, uint8_t slot, const struct btpolicy_advert *a)
{
	struct btpolicy_slot *s = &p->slots[slot];

	if (!s->used || memcmp(s->peer, a->addr, BTPOLICY_ADDR_LEN) != 0) {
		s->quick_retries = 0;
		s->retry_pending = false;
		s->pressed_order = 0;
	}
	s->state = BTPOLICY_CONNECTING;
	s->used = true;
	s->kind = a->kind;
	memcpy(s->peer, a->addr, BTPOLICY_ADDR_LEN);
	s->peer_stored = a->stored;
}

void btpolicy_dropping(struct btpolicy *p, uint8_t slot)
{
	p->slots[slot].dropping = true;
}

void btpolicy_connected(struct btpolicy *p, uint8_t slot)
{
	p->slots[slot].state = BTPOLICY_CONNECTED;
}

const uint8_t *btpolicy_peer(const struct btpolicy *p, uint8_t slot)
{
	return p->slots[slot].state == BTPOLICY_IDLE ? NULL : p->slots[slot].peer;
}

const uint8_t *btpolicy_last_peer(const struct btpolicy *p, uint8_t slot)
{
	return p->slots[slot].used ? p->slots[slot].peer : NULL;
}

enum device_kind btpolicy_slot_kind(const struct btpolicy *p, uint8_t slot)
{
	return p->slots[slot].used ? p->slots[slot].kind : DEVICE_NO_KIND;
}

void btpolicy_paired(struct btpolicy *p, uint8_t slot)
{
	p->slots[slot].peer_stored = true;
}

void btpolicy_ready(struct btpolicy *p, uint8_t slot)
{
	struct btpolicy_slot *s = &p->slots[slot];

	s->state = BTPOLICY_READY;
	s->order = ++p->events;
	if (p->active[s->kind] == DEVICE_NO_SLOT) {
		p->active[s->kind] = slot;
	}
}

bool btpolicy_is_ready(const struct btpolicy *p, uint8_t slot)
{
	return p->slots[slot].state == BTPOLICY_READY;
}

bool btpolicy_is_active(const struct btpolicy *p, uint8_t slot)
{
	return btpolicy_is_ready(p, slot) && p->active[p->slots[slot].kind] == slot;
}

bool btpolicy_is_standby(const struct btpolicy *p, uint8_t slot)
{
	return btpolicy_is_ready(p, slot) && !btpolicy_is_active(p, slot);
}

static bool pressed_later(const struct btpolicy_slot *a, const struct btpolicy_slot *b)
{
	if (a->pressed_order != b->pressed_order) {
		return a->pressed_order > b->pressed_order;
	}
	return a->order > b->order;
}

static uint8_t latest_standby(const struct btpolicy *p, enum device_kind kind)
{
	uint8_t best = DEVICE_NO_SLOT;

	for (uint8_t s = 0; s < DEVICE_SLOTS; s++) {
		const struct btpolicy_slot *x = &p->slots[s];

		if (x->state != BTPOLICY_READY || x->kind != kind) {
			continue;
		}
		if (best == DEVICE_NO_SLOT || pressed_later(x, &p->slots[best])) {
			best = s;
		}
	}
	return best;
}

uint8_t btpolicy_disconnected(struct btpolicy *p, uint8_t slot, uint32_t now_ms, bool never_joined)
{
	struct btpolicy_slot *s = &p->slots[slot];
	bool failed = s->state != BTPOLICY_READY && s->peer_stored;
	bool quick = failed && never_joined && s->quick_retries < BTPOLICY_QUICK_RETRIES;
	uint8_t promoted = DEVICE_NO_SLOT;

	s->quick_retries = quick ? s->quick_retries + 1 : 0;
	s->retry_pending = failed && !quick;
	s->failed_at_ms = now_ms;
	s->order = ++p->events;
	s->state = BTPOLICY_IDLE;
	s->dropping = false;
	if (!s->peer_stored) {
		s->used = false;
	}
	if (p->active[s->kind] == slot) {
		promoted = latest_standby(p, s->kind);
		p->active[s->kind] = promoted;
	}
	return promoted;
}

enum btpolicy_security btpolicy_security_failed(struct btpolicy *p, uint8_t slot,
						enum btpolicy_failure why)
{
	struct btpolicy_slot *s = &p->slots[slot];

	if (s->peer_stored) {
		return why == BTPOLICY_FAILURE_KEY_MISSING ? BTPOLICY_BOND_LOST
							   : BTPOLICY_SECURITY_FAILED;
	}
	return why == BTPOLICY_FAILURE_REFUSAL ? BTPOLICY_PAIRING_REFUSED : BTPOLICY_SECURITY_FAILED;
}

static bool holds_link(const struct btpolicy *p, const uint8_t addr[BTPOLICY_ADDR_LEN])
{
	uint8_t slot = btpolicy_slot_of(p, addr);

	return slot != DEVICE_NO_SLOT && btpolicy_peer(p, slot) != NULL;
}

enum btpolicy_scan btpolicy_scan(const struct btpolicy *p, bool window_open,
				 const struct btlist *l)
{
	bool slot_free = false;

	if (setting_up(p)) {
		return BTPOLICY_SCAN_OFF;
	}
	if (window_open) {
		return BTPOLICY_SCAN_ACTIVE;
	}
	for (int s = 0; s < DEVICE_SLOTS; s++) {
		slot_free |= p->slots[s].state == BTPOLICY_IDLE;
	}
	if (!slot_free) {
		return BTPOLICY_SCAN_OFF;
	}
	for (uint8_t i = 0; i < l->count; i++) {
		if (can_reconnect(&l->entries[i]) && !holds_link(p, l->entries[i].addr)) {
			return BTPOLICY_SCAN_PASSIVE;
		}
	}
	return BTPOLICY_SCAN_OFF;
}

bool btpolicy_pairs_by_clicks(enum device_kind kind, bool bolt)
{
	return bolt && kind == DEVICE_KIND_MOUSE;
}

uint32_t btpolicy_click_passkey(uint32_t passkey)
{
	return passkey % (1u << DIAG_CLICK_PASSKEY_BITS);
}

uint8_t btpolicy_seed(struct btpolicy *p, const uint8_t addr[BTPOLICY_ADDR_LEN],
		      enum device_kind kind)
{
	for (uint8_t slot = 0; slot < DEVICE_SLOTS; slot++) {
		struct btpolicy_slot *s = &p->slots[slot];

		if (!s->used) {
			s->used = true;
			s->kind = kind;
			s->order = DEVICE_SLOTS - slot;
			memcpy(s->peer, addr, BTPOLICY_ADDR_LEN);
			return slot;
		}
	}
	return DEVICE_NO_SLOT;
}

void btpolicy_forgotten(struct btpolicy *p, const uint8_t addr[BTPOLICY_ADDR_LEN])
{
	uint8_t slot = btpolicy_slot_of(p, addr);
	struct btpolicy_slot *s;

	if (slot == DEVICE_NO_SLOT) {
		return;
	}
	s = &p->slots[slot];
	s->peer_stored = false;
	if (s->state == BTPOLICY_IDLE) {
		s->used = false;
	}
}

void btpolicy_seed_all(struct btpolicy *p, const struct btlist *l)
{
	for (uint8_t i = 0; i < l->count; i++) {
		const struct btlist_entry *e = &l->entries[i];

		if (can_reconnect(e) &&
		    btpolicy_seed(p, e->addr, (enum device_kind)e->kind) == DEVICE_NO_SLOT) {
			return;
		}
	}
}

struct btpolicy_input_result btpolicy_input(struct btpolicy *p, uint8_t slot, bool pressed)
{
	struct btpolicy_slot *s = &p->slots[slot];
	struct btpolicy_input_result r = {.input = BTPOLICY_INPUT_STANDBY,
					  .released = DEVICE_NO_SLOT};

	if (s->state != BTPOLICY_READY) {
		return r;
	}
	if (pressed) {
		s->order = ++p->events;
		s->pressed_order = s->order;
	}
	if (p->active[s->kind] == slot) {
		r.input = BTPOLICY_INPUT_ACTIVE;
	} else if (pressed) {
		r.input = BTPOLICY_INPUT_TOOK_OVER;
		r.released = p->active[s->kind];
		p->active[s->kind] = slot;
	}
	return r;
}
