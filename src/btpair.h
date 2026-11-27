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
	/* Holds the picked device from the pick until the window opens again; all zero before a
	 * pick. */
	struct diag_heard picked;
	struct {
		uint32_t value;
		bool clicked;
		uint8_t entered;
		uint8_t name[DIAG_NAME_MAX];
	} passkey;
};

void btpair_init(struct btpair *p);
/* Stays open for DIAG_PICK_WINDOW_MS, or while a device it took waits for its passkey. */
void btpair_open_to_pick(struct btpair *p, uint32_t now_ms);
bool btpair_is_open(const struct btpair *p, uint32_t now_ms);
/* btpolicy_wants with the window's rules for a new device: one heard while picking is listed, and
 * only the picked one is wanted. */
enum btpolicy_want btpair_wants(struct btpair *p, const struct btpolicy *policy,
				const struct btpolicy_advert *a, uint32_t now_ms);
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
/* Closes the window when the picked device refused or a device held it for its passkey; returns
 * whether it closed an open window. */
bool btpair_attempt_failed(struct btpair *p, enum device_kind kind, bool refused, uint32_t now_ms);
/* Fills the status's pairing window, passkey, entered clicks and device name. */
void btpair_status(const struct btpair *p, uint32_t now_ms, struct diag_status *st);

#endif
