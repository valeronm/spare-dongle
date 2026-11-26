#ifndef BTPAIR_H
#define BTPAIR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "btpolicy.h"
#include "btseen.h"
#include "pairwin.h"

struct btpair {
	/* A device holding it open waits for its passkey. */
	struct pairwin window;
	struct btseen seen;
	/* Holds the picked device from the pick until the window opens again. */
	bool has_pick;
	struct diag_heard picked;
	struct {
		uint32_t value;
		bool clicked;
		uint8_t entered;
		uint8_t name[DIAG_NAME_MAX];
	} passkey;
	/* Devices that refused to pair since the window opened. */
	uint8_t refused[BTPOLICY_STORED_MAX][BTPOLICY_ADDR_LEN];
	uint8_t refused_count;
};

void btpair_init(struct btpair *p);
/* Stays open until a device pairs. */
void btpair_open_at_boot(struct btpair *p, uint32_t now_ms);
/* Stays open for DIAG_PICK_WINDOW_MS, or while a device it took waits for its passkey. */
void btpair_open_to_pick(struct btpair *p, uint32_t now_ms);
bool btpair_is_open(const struct btpair *p, uint32_t now_ms);
/* btpolicy_wants with the window's rules for a new device: one that refused is not wanted again
 * until the window opens again, one heard while picking is listed, and once a device is picked
 * only it is wanted. */
enum btpolicy_want btpair_wants(struct btpair *p, struct btpolicy *policy,
				const struct btpolicy_advert *a, uint8_t stored_count,
				uint32_t now_ms);
/* Writes the devices listed while picking, in the order they were first heard, and returns how
 * many. */
size_t btpair_heard(const struct btpair *p, uint32_t now_ms, struct diag_heard out[BTSEEN_MAX]);
/* Picks a device listed within BTSEEN_FRESH_MS; false outside picking or for any other device. */
bool btpair_pick(struct btpair *p, const uint8_t addr[BTPOLICY_ADDR_LEN], uint32_t now_ms);
/* Returns false and does nothing in a closed window. */
bool btpair_passkey_awaited(struct btpair *p, enum device_kind kind, uint32_t passkey, bool clicked,
			    const char *name, uint32_t now_ms);
/* Counts a click of the device that waits for a clicked passkey, and returns whether it did. */
bool btpair_keypress(struct btpair *p, enum device_kind kind, enum btpolicy_keypress keypress);
/* Closes the window and tells the policy. */
void btpair_paired(struct btpair *p, struct btpolicy *policy, enum device_kind kind);
/* btpolicy_security_failed; a device that refuses is not wanted again until the window opens
 * again, and its refusals after the first are told apart. */
enum btpolicy_security btpair_security_failed(struct btpair *p, struct btpolicy *policy,
					      enum device_kind kind, enum btpolicy_failure why);
/* Fills the status's pairing window, passkey, entered clicks and device name. */
void btpair_status(const struct btpair *p, uint32_t now_ms, struct diag_status *st);

#endif
