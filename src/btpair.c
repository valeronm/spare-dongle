#include <string.h>

#include "btpair.h"

enum btpair_state {
	BTPAIR_CLOSED,
	/* The new devices heard are listed until the user picks one. */
	BTPAIR_PICKING,
	BTPAIR_PICKED,
	/* A device waits for its passkey and holds the window open until its attempt ends. */
	BTPAIR_PASSKEY,
};

void btpair_init(struct btpair *p)
{
	*p = (struct btpair){0};
}

void btpair_open_to_pick(struct btpair *p, uint32_t now_ms)
{
	pairwin_open(&p->window, DIAG_PICK_WINDOW_MS, now_ms);
	p->picked = (struct diag_heard){0};
	btseen_clear(&p->seen);
}

static bool has_pick(const struct btpair *p)
{
	static const uint8_t none[BTPOLICY_ADDR_LEN];

	return memcmp(p->picked.addr, none, BTPOLICY_ADDR_LEN) != 0;
}

static enum btpair_state btpair_state(const struct btpair *p, uint32_t now_ms)
{
	if (!pairwin_is_open(&p->window, now_ms)) {
		return BTPAIR_CLOSED;
	}
	if (pairwin_holder(&p->window) != DEVICE_NO_KIND) {
		return BTPAIR_PASSKEY;
	}
	return has_pick(p) ? BTPAIR_PICKED : BTPAIR_PICKING;
}

bool btpair_is_open(const struct btpair *p, uint32_t now_ms)
{
	return btpair_state(p, now_ms) != BTPAIR_CLOSED;
}

enum btpolicy_want btpair_wants(struct btpair *p, const struct btpolicy *policy,
				const struct btpolicy_advert *a, uint32_t now_ms)
{
	enum btpair_state state = btpair_state(p, now_ms);
	enum btpolicy_want want =
		btpolicy_wants(policy, a, state != BTPAIR_CLOSED, now_ms);

	if (a->stored || want == BTPOLICY_IGNORE) {
		return want;
	}
	if (state == BTPAIR_PICKING) {
		btseen_heard(&p->seen, a, now_ms);
		return BTPOLICY_IGNORE;
	}
	if (memcmp(p->picked.addr, a->addr, BTPOLICY_ADDR_LEN) != 0) {
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

bool btpair_attempt_failed(struct btpair *p, enum device_kind kind, bool refused, uint32_t now_ms)
{
	if (refused && pairwin_is_open(&p->window, now_ms)) {
		pairwin_close(&p->window);
		return true;
	}
	return pairwin_end_attempt(&p->window, kind);
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
