#include "blink.h"

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
