#include "pairwin.h"

void pairwin_open(struct pairwin *w, uint32_t length_ms, uint32_t now_ms)
{
	w->open = true;
	w->holder = DEVICE_NO_KIND;
	w->opened_ms = now_ms;
	w->length_ms = length_ms;
}

bool pairwin_is_open(const struct pairwin *w, uint32_t now_ms)
{
	return w->open && (w->holder != DEVICE_NO_KIND || now_ms - w->opened_ms < w->length_ms);
}

void pairwin_close(struct pairwin *w)
{
	w->open = false;
}

bool pairwin_hold(struct pairwin *w, enum device_kind holder, uint32_t now_ms)
{
	if (!pairwin_is_open(w, now_ms)) {
		return false;
	}
	w->holder = holder;
	return true;
}

enum device_kind pairwin_holder(const struct pairwin *w)
{
	return w->open ? w->holder : DEVICE_NO_KIND;
}

bool pairwin_end_attempt(struct pairwin *w, enum device_kind holder)
{
	if (holder == DEVICE_NO_KIND || pairwin_holder(w) != holder) {
		return false;
	}
	pairwin_close(w);
	return true;
}

enum diag_window pairwin_window(const struct pairwin *w, uint32_t now_ms)
{
	if (pairwin_holder(w) != DEVICE_NO_KIND) {
		return DIAG_WINDOW_PAIRING;
	}
	return pairwin_is_open(w, now_ms) ? DIAG_WINDOW_OPEN : DIAG_WINDOW_CLOSED;
}

bool pairwin_ran_out(struct pairwin *w, uint32_t now_ms)
{
	if (!w->open || pairwin_is_open(w, now_ms)) {
		return false;
	}
	w->open = false;
	return true;
}
