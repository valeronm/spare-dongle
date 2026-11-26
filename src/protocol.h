#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#include "diagwire.h"

/*
 * A dongle that never chose, or holds a value this firmware does not know, runs NuPhy. save is set
 * when nothing is stored: a stored protocol keeps a dongle in the mode it first ran when the
 * default changes, and an unknown value is kept for the newer firmware that wrote it.
 */
enum protocol protocol_from_stored(bool loaded, uint8_t value, bool *save);

/* Returns false for an arg naming no protocol, 0 included. */
bool protocol_from_arg(uint32_t arg, enum protocol *out);

#endif
