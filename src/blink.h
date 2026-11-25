#ifndef BLINK_H
#define BLINK_H

#include <stdbool.h>
#include <stdint.h>

#define BLINK_STEP_MS 50

enum indicator_mode {
	IND_IDLE,
	IND_PAIRING,
	IND_CONNECTED,
	IND_SLEEP,
	IND_ERROR,
};

struct blink_state {
	bool radio_failed;
	bool usb_suspended;
	bool pairing_open;
	bool connected;
};

enum indicator_mode blink_mode(struct blink_state s);

/* step counts BLINK_STEP_MS periods from when the indicator started. */
bool blink_lit(enum indicator_mode mode, uint32_t step);

#endif
