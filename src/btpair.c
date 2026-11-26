#include <string.h>

#include "btpair.h"

enum btpair_state {
	BTPAIR_CLOSED,
	/* Opened at plug-in; the first new device pairs. */
	BTPAIR_OPEN,
	/* Opened from the tool; the new devices heard are listed until the user picks one. */
	BTPAIR_PICKING,
	BTPAIR_PICKED,
	/* A device waits for its passkey and holds the window open until its attempt ends. */
	BTPAIR_PASSKEY,
};

void btpair_init(struct btpair *p)
{
	*p = (struct btpair){0};
}

static void opened(struct btpair *p)
{
	p->has_pick = false;
	p->refused_count = 0;
}

void btpair_open_at_boot(struct btpair *p, uint32_t now_ms)
{
	pairwin_open(&p->window, PAIRWIN_BOOT, false, DIAG_PAIR_WINDOW_MS, now_ms);
	opened(p);
}

void btpair_open_to_pick(struct btpair *p, uint32_t now_ms)
{
	pairwin_open(&p->window, PAIRWIN_REQUEST, true, DIAG_PICK_WINDOW_MS, now_ms);
	opened(p);
	btseen_clear(&p->seen);
}

static enum btpair_state btpair_state(const struct btpair *p, uint32_t now_ms)
{
	if (!pairwin_is_open(&p->window, now_ms)) {
		return BTPAIR_CLOSED;
	}
	if (pairwin_holder(&p->window) != DEVICE_NO_KIND) {
		return BTPAIR_PASSKEY;
	}
	if (p->window.cause == PAIRWIN_BOOT) {
		return BTPAIR_OPEN;
	}
	return p->has_pick ? BTPAIR_PICKED : BTPAIR_PICKING;
}

bool btpair_is_open(const struct btpair *p, uint32_t now_ms)
{
	return btpair_state(p, now_ms) != BTPAIR_CLOSED;
}

static bool refused(const struct btpair *p, const uint8_t *addr)
{
	for (uint8_t i = 0; i < p->refused_count; i++) {
		if (memcmp(p->refused[i], addr, BTPOLICY_ADDR_LEN) == 0) {
			return true;
		}
	}
	return false;
}

/* Returns false for a device refused before. */
static bool refuse(struct btpair *p, const uint8_t *addr)
{
	if (refused(p, addr)) {
		return false;
	}
	if (p->refused_count < BTPOLICY_STORED_MAX) {
		memcpy(p->refused[p->refused_count++], addr, BTPOLICY_ADDR_LEN);
	}
	return true;
}

enum btpolicy_want btpair_wants(struct btpair *p, struct btpolicy *policy,
				const struct btpolicy_advert *a, uint8_t stored_count,
				uint32_t now_ms)
{
	enum btpair_state state = btpair_state(p, now_ms);
	enum btpolicy_want want =
		btpolicy_wants(policy, a, state != BTPAIR_CLOSED, stored_count, now_ms);

	if (a->stored || want == BTPOLICY_IGNORE) {
		return want;
	}
	if (refused(p, a->addr)) {
		return BTPOLICY_IGNORE;
	}
	if (state == BTPAIR_PICKING) {
		btseen_heard(&p->seen, a, now_ms);
		return BTPOLICY_IGNORE;
	}
	if (p->has_pick && memcmp(p->picked.addr, a->addr, BTPOLICY_ADDR_LEN) != 0) {
		return BTPOLICY_IGNORE;
	}
	return want;
}

size_t btpair_heard(const struct btpair *p, uint32_t now_ms, struct diag_heard out[BTSEEN_MAX])
{
	return btpair_state(p, now_ms) == BTPAIR_PICKING ? btseen_fresh(&p->seen, now_ms, out) : 0;
}

bool btpair_pick(struct btpair *p, const uint8_t addr[BTPOLICY_ADDR_LEN], uint32_t now_ms)
{
	const struct diag_heard *heard = btseen_find(&p->seen, addr, now_ms);

	if (btpair_state(p, now_ms) != BTPAIR_PICKING || heard == NULL) {
		return false;
	}
	p->has_pick = true;
	p->picked = *heard;
	return true;
}

bool btpair_passkey_awaited(struct btpair *p, enum device_kind kind, uint32_t passkey, bool clicked,
			    const char *name, uint32_t now_ms)
{
	if (!pairwin_hold(&p->window, kind, now_ms)) {
		return false;
	}
	p->passkey.value = passkey;
	p->passkey.clicked = clicked;
	p->passkey.entered = 0;
	strncpy((char *)p->passkey.name, name, sizeof(p->passkey.name));
	return true;
}

/* A device may send its keypress notifications in any order. */
static uint8_t entered(uint8_t n, enum btpolicy_keypress keypress)
{
	switch (keypress) {
	case BTPOLICY_KEY_ENTERED:
		return n < DIAG_CLICK_PASSKEY_BITS ? n + 1 : DIAG_CLICK_PASSKEY_BITS;
	case BTPOLICY_KEY_ERASED:
		return n > 0 ? n - 1 : 0;
	case BTPOLICY_ENTRY_COMPLETED:
		return DIAG_CLICK_PASSKEY_BITS;
	case BTPOLICY_ENTRY_STARTED:
	case BTPOLICY_ENTRY_CLEARED:
	default:
		return 0;
	}
}

bool btpair_keypress(struct btpair *p, enum device_kind kind, enum btpolicy_keypress keypress)
{
	if (pairwin_holder(&p->window) != kind || !p->passkey.clicked) {
		return false;
	}
	p->passkey.entered = entered(p->passkey.entered, keypress);
	return true;
}

void btpair_paired(struct btpair *p, struct btpolicy *policy, enum device_kind kind)
{
	pairwin_close(&p->window);
	btpolicy_paired(policy, kind);
}

enum btpolicy_security btpair_security_failed(struct btpair *p, struct btpolicy *policy,
					      enum device_kind kind, enum btpolicy_failure why)
{
	enum btpolicy_security outcome = btpolicy_security_failed(policy, kind, why);

	if (outcome == BTPOLICY_PAIRING_REFUSED && !refuse(p, btpolicy_peer(policy, kind))) {
		return BTPOLICY_REFUSED_BEFORE;
	}
	return outcome;
}



void btpair_status(const struct btpair *p, uint32_t now_ms, struct diag_status *st)
{
	st->passkey = 0;
	st->passkey_entered = 0;
	memset(st->pairing_name, 0, sizeof(st->pairing_name));
	switch (btpair_state(p, now_ms)) {
	case BTPAIR_CLOSED:
		st->pairing_window = DIAG_WINDOW_CLOSED;
		break;
	case BTPAIR_OPEN:
		st->pairing_window = DIAG_WINDOW_OPEN;
		break;
	case BTPAIR_PICKING:
		st->pairing_window = DIAG_WINDOW_PICKING;
		break;
	case BTPAIR_PICKED:
		st->pairing_window = DIAG_WINDOW_PICKED;
		memcpy(st->pairing_name, p->picked.name, sizeof(st->pairing_name));
		break;
	case BTPAIR_PASSKEY:
		st->pairing_window = p->passkey.clicked ? DIAG_WINDOW_PASSKEY_CLICKED
							: DIAG_WINDOW_PASSKEY_AWAITED;
		st->passkey = p->passkey.value;
		st->passkey_entered = p->passkey.entered;
		memcpy(st->pairing_name, p->passkey.name, sizeof(st->pairing_name));
		break;
	}
}
