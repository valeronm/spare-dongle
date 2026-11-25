#ifndef UNIFYING_H
#define UNIFYING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "chanmove.h"
#include "diagwire.h"
#include "hidpp.h"
#include "history.h"
#include "pairwin.h"
#include "reports.h"

#define UNIFYING_FRAME_MAX 22
#define UNIFYING_KEY_LEN 16
#define UNIFYING_NAME_MAX 16
#define UNIFYING_NONCE_LEN 4
#define UNIFYING_PAIRING_TIMEOUT_MS 5000
#define UNIFYING_ADDRESS_LEN 5
#define UNIFYING_SWEEP_DWELL_MS 20
#define UNIFYING_AES_BLOCK_LEN 16
#define UNIFYING_KEEPALIVE_DEFAULT_MS 1100
/* An idle keyboard found a moved receiver within one keep-alive, at most about 1 s. */
#define UNIFYING_MOVE_SEARCH_MS 1000
#define UNIFYING_LED_FRAME_LEN 10
#define UNIFYING_COUNTER_WINDOW 16
#define UNIFYING_ATTACK_MS 5000
#define UNIFYING_KEYSTROKE_REPORTS 2
#define UNIFYING_SERIAL_LEN 4
#define UNIFYING_COUNTER_LIMIT (0x100000000ULL - (1ULL << 20))
#define UNIFYING_PLACES 6
#define UNIFYING_NO_PLACE UNIFYING_PLACES
#define UNIFYING_PIPE_PAIRING 0
#define UNIFYING_PIPE_RECEIVER 7
#define UNIFYING_PIPES 8

/* Addresses are written in the order the Unifying sources print them, the device octet last. */
extern const uint8_t unifying_pairing_address[UNIFYING_ADDRESS_LEN];

/* What the receiver hands out in Pairing Response 1; base and wpid also enter the key. */
struct unifying_receiver {
	uint8_t base[4];
	uint8_t wpid[2];
};

enum unifying_state {
	UNIFYING_IDLE,
	UNIFYING_ANSWERED_1,
	UNIFYING_ANSWERED_2,
};

/* The flash record is these bytes in this order. */
struct unifying_device {
	uint8_t wpid[2];
	uint8_t key[UNIFYING_KEY_LEN];
	uint8_t serial[UNIFYING_SERIAL_LEN];
	uint8_t type;
	/* Padded with zeros. */
	uint8_t name[UNIFYING_NAME_MAX];
};

#define UNIFYING_RECORD_LEN sizeof(struct unifying_device)

struct unifying_attempt {
	enum unifying_state state;
	uint8_t id;
	/* An empty place while the attempt runs; the place it paired once it completes. */
	uint8_t place;
	uint64_t last_ms;
	struct unifying_device dev;
	uint8_t caps;
	uint8_t device_nonce[UNIFYING_NONCE_LEN];
	uint8_t dongle_nonce[UNIFYING_NONCE_LEN];
};

struct unifying_pairing {
	bool stored[UNIFYING_PLACES];
	struct unifying_device devices[UNIFYING_PLACES];
	struct unifying_attempt attempt;
	struct pairwin window;
};

enum unifying_rx_event {
	UNIFYING_RX_NONE,
	UNIFYING_RX_REFUSED,
	UNIFYING_RX_ANSWERED_1,
	UNIFYING_RX_ANSWERED_2,
	UNIFYING_RX_PAIRED,
};

struct unifying_sweep {
	uint8_t index;
	uint64_t last_ms;
};

uint8_t unifying_checksum(const uint8_t *buf, size_t len);

/* A frame whose checksum held, which only unifying_frame_checked fills in. */
struct unifying_frame {
	const uint8_t *bytes;
	size_t len;
};

/* The last byte is the checksum of the rest; keyboards sometimes send its low bit wrong. */
bool unifying_frame_checked(const uint8_t *bytes, size_t len, struct unifying_frame *out);

void unifying_derive_key(uint8_t key[UNIFYING_KEY_LEN], const uint8_t base[4],
			 const uint8_t device_wpid[2], const uint8_t dongle_wpid[2],
			 const uint8_t device_nonce[UNIFYING_NONCE_LEN],
			 const uint8_t dongle_nonce[UNIFYING_NONCE_LEN]);

uint8_t unifying_place_pipe(uint8_t place);
uint8_t unifying_place_octet(uint8_t place);
uint8_t unifying_pipe_place(uint8_t pipe);
void unifying_prefixes(uint8_t out[UNIFYING_PIPES]);
uint8_t unifying_octet_place(uint8_t octet);

void unifying_device_address(const struct unifying_receiver *r, uint8_t place,
			     uint8_t out[UNIFYING_ADDRESS_LEN]);

/*
 * Feeds one frame received on the pairing address or the attempt's place address, since Requests 2
 * and 3 arrive on the place address. Returns the length of the response to queue as the next ACK
 * payload, written to reply (UNIFYING_FRAME_MAX bytes), or 0 when there is none, and sets *event to
 * what the frame did.
 * nonce is copied in when Request 1 starts an attempt, so it must be one no earlier attempt used.
 * A repeat of an answered request gets 0, since ESB keeps its reply queued until the keyboard moves
 * on. A Request 1 from anything but a Unifying-compatible keyboard discards any attempt in
 * progress. Request 1 starts an attempt on the lowest empty place and is ignored when none is
 * empty; Request 3 stores the device there. A Request 1 is ignored while the window is
 * closed; Requests 2 and 3 are not.
 */
size_t unifying_pairing_rx(struct unifying_pairing *p, const struct unifying_receiver *r,
			   const uint8_t nonce[UNIFYING_NONCE_LEN], const struct unifying_frame *f,
			   uint64_t now_ms, uint8_t *reply, enum unifying_rx_event *event);

/*
 * Discards an attempt that has heard no request for UNIFYING_PAIRING_TIMEOUT_MS and returns true;
 * the stored devices are kept.
 */
bool unifying_pairing_expired(struct unifying_pairing *p, uint64_t now_ms);

/*
 * Returns the channel to listen on: while pairing is open with no keyboard stored and no attempt
 * running it steps through the pairing channels every UNIFYING_SWEEP_DWELL_MS, since the keyboard
 * hops them too; otherwise it is current.
 */
uint8_t unifying_sweep_channel(struct unifying_sweep *s, const struct unifying_pairing *p,
			       uint8_t current, uint64_t now_ms);

/* False, with the window left closed, when every place is stored. */
bool unifying_pairing_open(struct unifying_pairing *p, enum pairwin_cause cause, uint64_t now_ms);

bool unifying_pairing_window_open(const struct unifying_pairing *p, uint64_t now_ms);

/* The window is open or an attempt is in progress, which may outlast the window. */
bool unifying_pairing_active(const struct unifying_pairing *p, uint64_t now_ms);

bool unifying_has_device(const struct unifying_pairing *p);

void unifying_pairing_restore(struct unifying_pairing *p, uint8_t place,
			      const struct unifying_device *d);
void unifying_pairing_forget(struct unifying_pairing *p, uint8_t place);
/* One bit per pipe: the pairing and receiver pipes, each stored place and the place of a running
 * attempt. */
uint8_t unifying_listen_pipes(const struct unifying_pairing *p);

/* The address a device list shows for the place: product ID, serial, then the place's octet. */
void unifying_list_address(const struct unifying_pairing *p, uint8_t place,
			   uint8_t out[DIAG_ADDR_LEN]);
/* The stored place whose list address is addr, else UNIFYING_NO_PLACE. */
uint8_t unifying_find_place(const struct unifying_pairing *p, const uint8_t addr[DIAG_ADDR_LEN]);

bool unifying_is_encrypted_keyboard(const struct unifying_frame *f);

typedef void (*unifying_aes_fn)(const uint8_t key[UNIFYING_KEY_LEN],
				const uint8_t in[UNIFYING_AES_BLOCK_LEN],
				uint8_t out[UNIFYING_AES_BLOCK_LEN]);

struct unifying_candidate {
	uint64_t counter;
	struct report report;
	bool held_in_attack;
};

struct unifying_link {
	bool synced;
	uint64_t next_counter;
	bool has_candidate;
	struct unifying_candidate candidate;
	uint64_t attack_until_ms;
	uint16_t keepalive_ms;
	uint64_t last_ms;
	bool keyboard_down;
	/* Media frames and diverted controls each hold a consumer usage; a report carries one. */
	uint16_t media_usage;
	uint16_t divert_usage;
	uint32_t refused;
};

void unifying_link_reset(struct unifying_link *l);

/*
 * A connect frame is what a keyboard sends on the receiver's own address after it boots. The
 * keyboard picks a new counter on every boot, so a connect frame from the keyboard with
 * device_octet, received on the receiver's own address, forgets the known counter and drops any
 * held frame. Returns true when keys were held, which are then to be released.
 */
bool unifying_link_reconnect(struct unifying_link *l, uint8_t device_octet,
			     const struct unifying_frame *f);

/*
 * Decrypts an encrypted keystroke and writes up to UNIFYING_KEYSTROKE_REPORTS reports to out,
 * returning their number, to be typed in order. A counter below next_counter or from
 * UNIFYING_COUNTER_LIMIT up is refused before aes is called; a plaintext without 0xc9 is refused
 * and starts an attack lasting UNIFYING_ATTACK_MS.
 *
 * A release is typed at once. A press is typed at once only with a counter known, no attack and a
 * counter within UNIFYING_COUNTER_WINDOW of next_counter; otherwise it is held and typed when the
 * next frame carries its counter + 1. Beyond the window with a counter known and no attack, the
 * held press is a jump whose own report is never typed, only the confirming frame's. During an
 * attack a press with no counter known is refused, as is any frame beyond the window that
 * confirms nothing.
 */
size_t unifying_keystroke(struct unifying_link *l, const uint8_t key[UNIFYING_KEY_LEN],
			  const struct unifying_frame *f, uint64_t now_ms, unifying_aes_fn aes,
			  struct report out[UNIFYING_KEYSTROKE_REPORTS]);

/* The usage's high byte (frame byte 3) is unconfirmed: every key seen used a usage below 0x100. */
bool unifying_media_report(struct unifying_link *l, const struct unifying_frame *f,
			   struct report *out);

size_t unifying_led_frame(uint8_t leds, uint8_t out[UNIFYING_LED_FRAME_LEN]);

struct unifying_led {
	uint8_t wanted;
	uint8_t sent;
	bool has_sent;
	bool resend;
};

void unifying_led_want(struct unifying_led *led, uint8_t leds);

void unifying_led_resend(struct unifying_led *led);

/*
 * Returns true, with *leds to send, while a device is paired and either a resend is requested or
 * the wanted state differs from the last one queued.
 */
bool unifying_led_due(const struct unifying_led *led, bool has_device, uint8_t *leds);

void unifying_led_sent(struct unifying_led *led, uint8_t leds);

enum unifying_queued {
	UNIFYING_QUEUED_NONE,
	UNIFYING_QUEUED_LED,
	UNIFYING_QUEUED_HIDPP,
};

/*
 * At most one of the dongle's own frames waits on the active keyboard's pipe, and pairing replies
 * share its taken count. The frame counts as taken once the taken count moves past taken_count,
 * which is sampled before queueing, as the frame can be taken at once.
 */
struct unifying_queue {
	enum unifying_queued frame;
	uint32_t taken_count;
};

void unifying_record_of(const struct unifying_pairing *p, uint8_t place,
			uint8_t out[UNIFYING_RECORD_LEN]);

void unifying_record_restore(struct unifying_pairing *p, uint8_t place,
			     const uint8_t in[UNIFYING_RECORD_LEN]);

/* Records any frame from the keyboard, including one that failed its checksum. */
void unifying_link_heard(struct unifying_link *l, uint64_t now_ms);

/* Keep-alive and set-keep-alive frames carry the keyboard's timeout. */
void unifying_link_keepalive(struct unifying_link *l, const struct unifying_frame *f);

/*
 * Returns true once when a key is held and nothing was heard for twice the keep-alive timeout; the
 * held state is then cleared.
 */
bool unifying_link_silent(struct unifying_link *l, uint64_t now_ms);

struct unifying_session {
	struct unifying_receiver receiver;
	struct unifying_pairing pairing;
	struct unifying_link links[UNIFYING_PLACES];
	/* A stored place while any is stored, else UNIFYING_NO_PLACE. */
	uint8_t active;
	struct unifying_led led;
	struct hidpp hidpp;
	struct device_battery battery;
	struct unifying_queue queue;
	struct unifying_sweep sweep;
	uint8_t channel;
	bool heard;
	uint64_t heard_ms;
	int8_t rssi;
	bool silent;
	uint64_t silent_since_ms;
	bool connect_in_silence;
	uint64_t deaf_until_ms;
	/* After a move, silence detection and the release of held keys wait until this time or
	 * until the keyboard is heard. */
	uint64_t search_until_ms;
};

enum unifying_heard {
	UNIFYING_HEARD_NONE,
	UNIFYING_HEARD_FIRST,
	UNIFYING_HEARD_AGAIN,
	/* Heard again after a connect frame arrived during the silence. */
	UNIFYING_HEARD_WOKE,
};

/* What the caller must do after a session call; zeroed by the call. */
struct unifying_out {
	enum unifying_rx_event event;
	uint8_t reply_pipe;
	size_t reply_len;
	uint8_t reply[UNIFYING_FRAME_MAX];
	size_t reports;
	struct report report[UNIFYING_KEYSTROKE_REPORTS];
	bool release_all;
	bool send_led;
	uint8_t leds;
	size_t hidpp_len;
	uint8_t hidpp[UNIFYING_FRAME_MAX];
	struct hidpp_event hidpp_event;
	bool attempt_expired;
	bool set_channel;
	uint8_t channel;
	bool went_silent;
	/* Time since the last frame when the silence was noticed. */
	uint32_t silent_ms;
	enum unifying_heard heard;
	/* The clock reading at the frame for UNIFYING_HEARD_FIRST; otherwise the time since the
	 * silence was noticed. */
	uint32_t down_ms;
	bool reconnected;
	bool attack_started;
	/* A device-address frame that fails its checksum or a refused keystroke. */
	bool invalid;
	bool window_closed;
	/* The handshake step an expired attempt had answered, 1 or 2. */
	uint8_t expired_step;
	/* The frame came from the active keyboard and counts as its packet. */
	bool from_active;
	/* Listening on pipes drops the queued replies, so it precedes any reply this call queues. */
	bool listen;
	uint8_t pipes;
	/* Meaningful only for UNIFYING_RX_PAIRED. */
	uint8_t paired_place;
	bool window_opened;
	/* The active keyboard was heard for the first time or took over from another. */
	bool active_identified;
};

void unifying_session_init(struct unifying_session *s, const struct unifying_receiver *r);
/* For each stored record after unifying_session_init, lowest place first. */
void unifying_session_restore(struct unifying_session *s, uint8_t place,
			      const uint8_t record[UNIFYING_RECORD_LEN]);
/* Drops the place's keyboard; out tells the caller to release the keys when it was the active one,
 * to set the channel when it changed, to listen on pipes, and to log the pairing window, which
 * opens when no place is left stored. */
void unifying_session_forget(struct unifying_session *s, uint8_t place, uint64_t now_ms,
			     struct unifying_out *out);

/* False for a frame the session ignores. Frames on a place's pipe come from that place's
 * keyboard. */
bool unifying_session_frame(struct unifying_session *s, uint8_t pipe, const uint8_t *frame,
			    size_t len, int8_t rssi, uint64_t now_ms,
			    const uint8_t nonce[UNIFYING_NONCE_LEN], unifying_aes_fn aes,
			    struct unifying_out *out);

void unifying_session_tick(struct unifying_session *s, uint64_t now_ms, bool leds_changed,
			   uint8_t host_leds, uint32_t taken_count, struct unifying_out *out);

/* Moves a paired session with pairing closed to channel, one of the pairing channels; the caller
 * retunes the radio. */
enum chanmove_result unifying_session_move(struct unifying_session *s, uint8_t channel,
					   uint64_t now_ms);

void unifying_session_led_queued(struct unifying_session *s, uint8_t leds, uint32_t taken_count);

void unifying_session_hidpp_queued(struct unifying_session *s, uint32_t taken_count);

/* The replies queued on the radio were dropped. */
void unifying_session_replies_dropped(struct unifying_session *s);

/* False when every place is stored. */
bool unifying_session_open_pairing(struct unifying_session *s, enum pairwin_cause cause,
				   uint64_t now_ms);

/* The receiver was off until now_ms, so silence before it is not the keyboard's. */
void unifying_session_radio_resumed(struct unifying_session *s, uint64_t now_ms);

enum unifying_status unifying_session_status(const struct unifying_session *s, uint64_t now_ms);

/* Points *out at the channels unifying_session_move accepts and returns how many. */
size_t unifying_session_channels(const uint8_t **out);

/* The channel whose noise counts as home's; CHANMOVE_NO_HOME while no keyboard is stored. */
int unifying_session_home(const struct unifying_session *s);

/* The home channel while a noise move may leave it; CHANMOVE_NO_HOME also while a handshake
 * runs. */
int unifying_session_movable(const struct unifying_session *s);

#endif
