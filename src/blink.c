#include "blink.h"

enum indicator_mode blink_mode(struct blink_state s)
{
	/* A radio that failed to start stays failed when the PC wakes. */
	if (s.radio_failed) {
		return IND_ERROR;
	}
	if (s.usb_suspended) {
		return IND_SLEEP;
	}
	if (s.pairing_open) {
		return IND_PAIRING;
	}
	return s.connected ? IND_CONNECTED : IND_IDLE;
}

bool blink_lit(enum indicator_mode mode, uint32_t step)
{
	switch (mode) {
	case IND_IDLE:
		return step % 20 == 0;
	case IND_PAIRING:
		return step % 4 < 2;
	case IND_CONNECTED:
		return true;
	case IND_SLEEP:
		return false;
	case IND_ERROR:
		return step % 40 < 6 && step % 2 == 0;
	}
	return false;
}
