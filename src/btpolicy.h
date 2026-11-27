#ifndef BTPOLICY_H
#define BTPOLICY_H

#include <stdbool.h>
#include <stdint.h>

#include "device.h"

#define BTPOLICY_STORED_MAX 8
#define BTPOLICY_RETRY_MS 1000
#define BTPOLICY_QUICK_RETRIES 3
/* The address type, then the six address bytes. */
#define BTPOLICY_ADDR_LEN 7
#define BTPOLICY_NAME_MAX 32
#define BTPOLICY_APPEARANCE_KEYBOARD 0x03c1
#define BTPOLICY_APPEARANCE_MOUSE 0x03c2

struct btpolicy_advert {
	uint8_t addr[BTPOLICY_ADDR_LEN];
	bool connectable;
	bool directed;
	bool hid_service;
	/* Lists Logitech's service, which a Logi Bolt device advertises. */
	bool bolt;
	bool stored;
	/* From the appearance for a new device, from the stored list for a stored one. */
	enum device_kind kind;
	/* Empty unless the advert or its scan response named the device. */
	char name[BTPOLICY_NAME_MAX + 1];
	/* The signal of the advert, or of its scan response once that arrived. */
	int8_t rssi;
};

enum btpolicy_state {
	BTPOLICY_IDLE,
	BTPOLICY_CONNECTING,
	BTPOLICY_CONNECTED,
	/* The device's report map is read and its reports are forwarded. */
	BTPOLICY_READY,
};

enum btpolicy_want {
	BTPOLICY_IGNORE,
	BTPOLICY_CONNECT,
	/* The slot's device is to be dropped first; the advert is wanted once the slot is idle. */
	BTPOLICY_REPLACE,
};

enum btpolicy_scan {
	BTPOLICY_SCAN_OFF,
	BTPOLICY_SCAN_PASSIVE,
	BTPOLICY_SCAN_ACTIVE,
};

enum btpolicy_failure {
	/* The device or the dongle turned the pairing down. */
	BTPOLICY_FAILURE_REFUSAL,
	/* The device no longer holds its side of the bond. */
	BTPOLICY_FAILURE_KEY_MISSING,
	/* The link dropped before security was settled. */
	BTPOLICY_FAILURE_LINK,
};

enum btpolicy_security {
	BTPOLICY_PAIRING_REFUSED,
	BTPOLICY_BOND_LOST,
	BTPOLICY_SECURITY_FAILED,
};

struct btpolicy_slot {
	enum btpolicy_state state;
	/* The device connecting or connected, and once idle the last device the slot held. */
	uint8_t peer[BTPOLICY_ADDR_LEN];
	bool used;
	enum device_kind kind;
	bool peer_stored;
	bool dropping;
	bool retry_pending;
	uint8_t quick_retries;
	uint32_t failed_at_ms;
	/* btpolicy.events at the last disconnect, ready or press; for a seeded slot, below every
	 * event and lower for each later seed. */
	uint32_t order;
	/* btpolicy.events at the last press of the slot's device, or 0 for a device never pressed. */
	uint32_t pressed_order;
};

struct btpolicy {
	struct btpolicy_slot slots[DEVICE_SLOTS];
	/* DEVICE_NO_SLOT while no device of the kind is ready. */
	uint8_t active[DEVICE_KINDS];
	/* Uptime misorders events 2^31 ms apart, so slots are ordered by this count of disconnects,
	 * readies and presses. */
	uint32_t events;
};

struct btpolicy_choice {
	enum btpolicy_want want;
	/* The slot to connect into or to drop; DEVICE_NO_SLOT with BTPOLICY_IGNORE. */
	uint8_t slot;
};

#define BTPOLICY_CHOICE_IGNORE                                                                    \
	((struct btpolicy_choice){.want = BTPOLICY_IGNORE, .slot = DEVICE_NO_SLOT})

enum btpolicy_input {
	BTPOLICY_INPUT_ACTIVE,
	/* The caller releases what the released slot holds before forwarding. */
	BTPOLICY_INPUT_TOOK_OVER,
	BTPOLICY_INPUT_STANDBY,
};

struct btpolicy_input_result {
	enum btpolicy_input input;
	/* The slot active before a takeover; DEVICE_NO_SLOT for any other input. */
	uint8_t released;
};

enum device_kind btpolicy_kind_of(uint16_t appearance);
void btpolicy_init(struct btpolicy *p);
/* False for an advert no rule can want, whatever the state. */
bool btpolicy_interesting(const struct btpolicy_advert *a);
/* A device goes into the slot it last held, else a slot never used, else the slot idle longest;
 * a new device in an open window with no such slot replaces the standby device whose last press,
 * or else its ready, is oldest. A stored device is not connected to while a pairing window is
 * open, and only one device is set up at a time. */
struct btpolicy_choice btpolicy_wants(const struct btpolicy *p, const struct btpolicy_advert *a,
				      bool window_open, uint32_t now_ms);
void btpolicy_connecting(struct btpolicy *p, uint8_t slot, const struct btpolicy_advert *a);
void btpolicy_dropping(struct btpolicy *p, uint8_t slot);
void btpolicy_connected(struct btpolicy *p, uint8_t slot);
/* The device being connected to or connected in the slot, or NULL. */
const uint8_t *btpolicy_peer(const struct btpolicy *p, uint8_t slot);
/* The device the slot holds or last held, or NULL for a slot never used. */
const uint8_t *btpolicy_last_peer(const struct btpolicy *p, uint8_t slot);
/* The slot holding or last holding the device, or DEVICE_NO_SLOT. */
uint8_t btpolicy_slot_of(const struct btpolicy *p, const uint8_t addr[BTPOLICY_ADDR_LEN]);
/* DEVICE_NO_KIND for a slot never used. */
enum device_kind btpolicy_slot_kind(const struct btpolicy *p, uint8_t slot);
void btpolicy_paired(struct btpolicy *p, uint8_t slot);
/* The device becomes active when no device of its kind is. */
void btpolicy_ready(struct btpolicy *p, uint8_t slot);
bool btpolicy_is_ready(const struct btpolicy *p, uint8_t slot);
bool btpolicy_is_active(const struct btpolicy *p, uint8_t slot);
bool btpolicy_is_standby(const struct btpolicy *p, uint8_t slot);
/* A stored device whose connection ended before it was ready leaves its slot alone for
 * BTPOLICY_RETRY_MS, except for the first BTPOLICY_QUICK_RETRIES in a row that never joined the
 * connection. When the slot was active, the ready device of its kind pressed last, or with none
 * pressed the one ready last, becomes active and is returned; otherwise DEVICE_NO_SLOT. */
uint8_t btpolicy_disconnected(struct btpolicy *p, uint8_t slot, uint32_t now_ms, bool never_joined);
enum btpolicy_security btpolicy_security_failed(struct btpolicy *p, uint8_t slot,
						enum btpolicy_failure why);
/* Marks the first never-used slot as last holding this stored device, so it reconnects there and
 * the slot shows it while idle, and returns that slot, or DEVICE_NO_SLOT once every slot is
 * used; for the boot, before any connection. The caller seeds the most recent device first: each
 * seed counts as idle longer than the one before. */
uint8_t btpolicy_seed(struct btpolicy *p, const uint8_t addr[BTPOLICY_ADDR_LEN],
		      enum device_kind kind);
/* pressed is reports_pressed of what the report translated to. */
struct btpolicy_input_result btpolicy_input(struct btpolicy *p, uint8_t slot, bool pressed);
/* Active while a pairing window is open, since a new device may give its appearance only in its
 * scan response. */
enum btpolicy_scan btpolicy_scan(const struct btpolicy *p, bool window_open);
/* A Bolt mouse demands a passkey once told it pairs with a Bolt receiver, and enters it by
 * clicks; a Bolt keyboard has not been seen to take that. */
bool btpolicy_pairs_by_clicks(enum device_kind kind, bool bolt);
/* The passkey cut to the DIAG_CLICK_PASSKEY_BITS a device enters by clicks. */
uint32_t btpolicy_click_passkey(uint32_t passkey);

/* The keypress notification types of the Bluetooth Security Manager, in its numbering. */
enum btpolicy_keypress {
	BTPOLICY_ENTRY_STARTED,
	BTPOLICY_KEY_ENTERED,
	BTPOLICY_KEY_ERASED,
	BTPOLICY_ENTRY_CLEARED,
	BTPOLICY_ENTRY_COMPLETED,
};

#endif
