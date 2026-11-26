#ifndef BTSEEN_H
#define BTSEEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "btpolicy.h"

#define BTSEEN_MAX DIAG_HEARD_MAX
/* Longer than a device in pairing mode leaves between adverts. */
#define BTSEEN_FRESH_MS 5000

struct btseen {
	struct {
		struct diag_heard heard;
		uint32_t heard_ms;
	} devices[BTSEEN_MAX];
	uint8_t count;
};

void btseen_clear(struct btseen *s);
/* Records a candidate's advert; when the table is full, it replaces the device heard longest ago. */
void btseen_heard(struct btseen *s, const struct btpolicy_advert *a, uint32_t now_ms);
/* Writes the devices heard within BTSEEN_FRESH_MS, in the order they were first heard, and returns
 * how many. */
size_t btseen_fresh(const struct btseen *s, uint32_t now_ms, struct diag_heard out[BTSEEN_MAX]);
/* The device heard within BTSEEN_FRESH_MS at this address, or NULL. */
const struct diag_heard *btseen_find(const struct btseen *s, const uint8_t addr[BTPOLICY_ADDR_LEN],
				     uint32_t now_ms);

#endif
