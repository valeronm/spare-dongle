#ifndef CHANPICK_H
#define CHANPICK_H

#include <stdbool.h>
#include <stdint.h>
#include "diagwire.h"

#define CHAN_COUNT (CHAN_LAST - CHAN_FIRST + 1)
#define CHAN_SAMPLES 32
#define CHAN_MIN_SPACING 10

static inline bool chan_in_range(int ch)
{
	return ch >= CHAN_FIRST && ch <= CHAN_LAST;
}

/* A keyboard heard on CHAN_PAIRING is taken to be away from its home channels. */
static inline bool chan_operable(int ch)
{
	return chan_in_range(ch) && ch != CHAN_PAIRING;
}

struct chan_survey {
	int8_t dbm[CHAN_COUNT][CHAN_SAMPLES];
};

/*
 * Writes the three quietest operable channels at least CHAN_MIN_SPACING apart, quietest first.
 */
void chan_pick(const struct chan_survey *s, uint8_t out[3]);

#endif
