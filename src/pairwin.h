#ifndef PAIRWIN_H
#define PAIRWIN_H

#include <stdbool.h>
#include <stdint.h>

#include "device.h"
#include "diagwire.h"

struct pairwin {
	bool open;
	enum device_kind holder;
	uint32_t opened_ms;
	uint32_t length_ms;
};

void pairwin_open(struct pairwin *w, uint32_t length_ms, uint32_t now_ms);
bool pairwin_is_open(const struct pairwin *w, uint32_t now_ms);
void pairwin_close(struct pairwin *w);
/* Keeps an open window open past its length until pairwin_close, or the holder's
 * pairwin_end_attempt; returns false and does nothing for a closed window. */
bool pairwin_hold(struct pairwin *w, enum device_kind holder, uint32_t now_ms);
/* DEVICE_NO_KIND for a window nothing holds open. */
enum device_kind pairwin_holder(const struct pairwin *w);
/* Closes the window if this holder holds it, and returns whether it did. */
bool pairwin_end_attempt(struct pairwin *w, enum device_kind holder);
/* DIAG_WINDOW_PAIRING while a holder pairs, otherwise open or closed. */
enum diag_window pairwin_window(const struct pairwin *w, uint32_t now_ms);
/* Returns true once when a window that was open has passed its length, and closes it. */
bool pairwin_ran_out(struct pairwin *w, uint32_t now_ms);

#endif
