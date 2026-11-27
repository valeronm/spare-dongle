#include <string.h>
#include "chanpick.h"
#include "link.h"

#define IDLE_AFTER_MS 1000
#define SLOT_MS 400
#define STATUS_EVERY_MS 100
/* A suspend frame can be lost on air, and NuPhy's dongle writes it five times. */
#define SUSPEND_COPIES 5
/* A connected keyboard sends about every 40 ms, even with no key change. */
#define GAP_MS 250
/* The keyboard repeats a pair request that goes unanswered while a survey has the receiver off. */
#define OFFER_REUSE_MS 10000

bool link_connected(const struct link *l)
{
	return l->state == LINK_STATE_CONNECTED || l->state == LINK_STATE_MOVING_HOME;
}

static void set_channel(struct link *l, uint8_t channel, struct link_out *out)
{
	l->channel = channel;
	out->set_channel = true;
	out->channel = channel;
}

static void list_channels(struct link *l)
{
	memcpy(l->channels, diag_nuphy_fixed_channels, sizeof(diag_nuphy_fixed_channels));
	l->channel_count = sizeof(diag_nuphy_fixed_channels);
	for (size_t i = 0; l->own_known && i < sizeof(l->own); i++) {
		if (memchr(l->channels, l->own[i], l->channel_count) == NULL) {
			l->channels[l->channel_count++] = l->own[i];
		}
	}
}

static void set_own(struct link *l, const uint8_t own[LINK_OWN_CHANNELS])
{
	memcpy(l->own, own, sizeof(l->own));
	l->own_known = true;
	l->home = own[0];
	list_channels(l);
}

/* Neither the fixed channels nor a paired keyboard's channels include CHAN_PAIRING. */
static bool on_pairing_channel(const struct link *l)
{
	return l->channel == CHAN_PAIRING;
}

static uint8_t own_slot_channel(const struct link *l)
{
	return l->own_known ? l->home : diag_nuphy_fixed_channels[l->fixed_next];
}

static void go_idle(struct link *l, uint32_t now_ms, struct link_out *out)
{
	/* A keyboard holds the window only while it pairs. */
	out->window_closed |= pairwin_end_attempt(&l->window, 0);
	l->state = LINK_STATE_IDLE;
	l->suspends_sent = 0;
	l->slot_start_ms = now_ms;
	set_channel(l, own_slot_channel(l), out);
}

static void reply(struct link *l, uint8_t pipe, struct link_out *out)
{
	l->seq = nuphy_next_seq(l->seq);
	out->reply = true;
	out->reply_pipe = pipe;
}

static void reply_status(struct link *l, uint32_t now_ms, struct link_out *out)
{
	reply(l, 1, out);
	out->reply_len = nuphy_status(out->reply_buf, l->seq, l->dongle_id, l->host_leds);
	l->last_status_ms = now_ms;
}

static void reply_pairing(struct link *l, struct link_out *out)
{
	reply(l, 0, out);
	out->reply_len = nuphy_pair_reply(out->reply_buf, l->seq, l->dongle_id, l->requester_id,
					  l->offered);
}

void link_init(struct link *l, const uint8_t dongle_id[NUPHY_ID_LEN],
	       const struct link_record *record, uint32_t now_ms, struct link_out *out)
{
	memset(l, 0, sizeof(*l));
	memcpy(l->dongle_id, dongle_id, NUPHY_ID_LEN);
	if (record != NULL && link_channels_allowed(record->own)) {
		set_own(l, record->own);
		memcpy(l->keyboard_id, record->keyboard_id, NUPHY_ID_LEN);
		memcpy(l->keyboard_name.name, record->name, NUPHY_NAME_MAX);
	} else {
		list_channels(l);
	}
	go_idle(l, now_ms, out);
}

void link_forget(struct link *l, uint32_t now_ms, struct link_out *out)
{
	struct link kept = *l;

	link_init(l, kept.dongle_id, NULL, now_ms, out);
	if (kept.next_offer_kind == OFFER_FORCED) {
		link_force_channels(l, kept.next_offer);
	}
	l->host_leds = kept.host_leds;
	l->host_suspended = kept.host_suspended;
	l->window = kept.window;
	out->window_closed |= pairwin_end_attempt(&l->window, 0);
}

void link_record_of(const struct link *l, struct link_record *out)
{
	memcpy(out->own, l->own, LINK_OWN_CHANNELS);
	memcpy(out->keyboard_id, l->keyboard_id, NUPHY_ID_LEN);
	memcpy(out->name, l->keyboard_name.name, NUPHY_NAME_MAX);
}

static bool next_offer_ready(const struct link *l, uint32_t now_ms)
{
	return l->next_offer_kind == OFFER_FORCED ||
	       (l->next_offer_kind == OFFER_SURVEYED && now_ms - l->surveyed_ms < OFFER_REUSE_MS);
}

static void offer(struct link *l, struct link_out *out)
{
	memcpy(l->offered, l->next_offer, sizeof(l->offered));
	l->state = LINK_STATE_PAIRING;
	reply_pairing(l, out);
}

static bool attempting(const struct link *l)
{
	return l->state == LINK_STATE_SURVEY || l->state == LINK_STATE_PAIRING || l->state == LINK_STATE_CONFIRMED;
}

static void on_pairing_message(struct link *l, const struct nuphy_msg *m, uint32_t now_ms,
			       struct link_out *out)
{
	bool same_keyboard = memcmp(m->ids.keyboard, l->requester_id, NUPHY_ID_LEN) == 0;
	if (attempting(l) && same_keyboard) {
		l->last_rx_ms = now_ms;
	}
	if (m->kind == NUPHY_PAIR_REQUEST) {
		if (l->state == LINK_STATE_IDLE &&
		    pairwin_hold(&l->window, 0, now_ms)) {
			memcpy(l->requester_id, m->ids.keyboard, NUPHY_ID_LEN);
			l->last_rx_ms = now_ms;
			if (next_offer_ready(l, now_ms)) {
				offer(l, out);
			} else {
				l->state = LINK_STATE_SURVEY;
				out->start_survey = true;
			}
		} else if (l->state == LINK_STATE_PAIRING && same_keyboard) {
			reply_pairing(l, out);
		}
		return;
	}
	if (m->kind == NUPHY_PAIR_CONFIRM && l->state == LINK_STATE_PAIRING && same_keyboard &&
	    memcmp(m->ids.dongle, l->dongle_id, NUPHY_ID_LEN) == 0) {
		set_own(l, l->offered);
		memcpy(l->keyboard_id, l->requester_id, NUPHY_ID_LEN);
		l->keyboard_name = (struct nuphy_name){0};
		out->save_pairing = true;
		l->state = LINK_STATE_CONFIRMED;
		/* The keyboard addresses pipe 1 once this confirm is acknowledged. */
		reply_status(l, now_ms, out);
	}
}

static void on_connected_message(struct link *l, uint32_t now_ms, struct link_out *out)
{
	if (l->host_suspended && l->suspends_sent < SUSPEND_COPIES) {
		reply(l, 1, out);
		out->reply_len = nuphy_suspend(out->reply_buf, l->seq);
		l->suspends_sent++;
		l->last_status_ms = now_ms;
	} else if (now_ms - l->last_status_ms >= STATUS_EVERY_MS) {
		reply_status(l, now_ms, out);
	}
}

static void move_home(struct link *l, uint32_t now_ms, struct link_out *out)
{
	l->state = LINK_STATE_CONNECTED;
	set_channel(l, l->home, out);
	reply_status(l, now_ms, out);
}

bool link_on_message(struct link *l, const struct nuphy_msg *m, uint8_t pipe, uint32_t now_ms,
		     struct link_out *out)
{
	if (m->kind == NUPHY_INVALID) {
		return false;
	}
	if (pipe == 0) {
		on_pairing_message(l, m, now_ms, out);
		return true;
	}
	/* A keyboard keeps the dongle ID of a forgotten pairing. */
	if (pipe != 1 || !l->own_known) {
		return false;
	}
	/* Pipe 1 carries no keyboard ID, so a pairing cannot tell the keyboard it pairs from another. */
	if (l->state == LINK_STATE_SURVEY || l->state == LINK_STATE_PAIRING) {
		return true;
	}
	if (link_connected(l) && now_ms - l->last_rx_ms >= GAP_MS) {
		out->gap_ms = now_ms - l->last_rx_ms;
		out->gap_channel = l->channel;
	}
	l->last_rx_ms = now_ms;
	if (m->kind == NUPHY_NAME) {
		out->save_name = nuphy_name_add(&l->keyboard_name, m->name_chunk);
	}
	switch (l->state) {
	case LINK_STATE_CONFIRMED:
		/* This packet's ACK carried the status queued on entering this state. */
		pairwin_close(&l->window);
		move_home(l, now_ms, out);
		break;
	case LINK_STATE_MOVING_HOME:
		/* A keyboard follows a move only after taking a status, which its hello shows. */
		if (m->kind != NUPHY_HELLO) {
			on_connected_message(l, now_ms, out);
			break;
		}
		out->moved = true;
		out->moved_from = l->channel;
		move_home(l, now_ms, out);
		break;
	case LINK_STATE_IDLE:
		l->state = on_pairing_channel(l) ? LINK_STATE_MOVING_HOME : LINK_STATE_CONNECTED;
		out->up = true;
		reply_status(l, now_ms, out);
		break;
	case LINK_STATE_CONNECTED:
		on_connected_message(l, now_ms, out);
		break;
	default:
		break;
	}
	return true;
}

void link_on_survey(struct link *l, const uint8_t channels[LINK_OWN_CHANNELS], uint32_t now_ms,
		    struct link_out *out)
{
	if (l->state != LINK_STATE_SURVEY) {
		return;
	}
	memcpy(l->next_offer, channels, sizeof(l->next_offer));
	l->next_offer_kind = OFFER_SURVEYED;
	l->surveyed_ms = now_ms;
	/* The survey sweeps the radio across channels with the receiver off. */
	l->last_rx_ms = now_ms;
	set_channel(l, l->channel, out);
	offer(l, out);
}

void link_survey_failed(struct link *l, uint32_t now_ms, struct link_out *out)
{
	if (l->state == LINK_STATE_SURVEY) {
		go_idle(l, now_ms, out);
	}
}

void link_tick(struct link *l, uint32_t now_ms, struct link_out *out)
{
	switch (l->state) {
	case LINK_STATE_CONNECTED:
	case LINK_STATE_CONFIRMED:
	case LINK_STATE_MOVING_HOME:
	case LINK_STATE_PAIRING:
		if (now_ms - l->last_rx_ms >= IDLE_AFTER_MS) {
			out->lost = link_connected(l);
			out->pairing_expired = l->state == LINK_STATE_PAIRING;
			go_idle(l, now_ms, out);
		}
		break;
	case LINK_STATE_IDLE:
		/* A keyboard that fails a send sweeps its channels within milliseconds. */
		if (!link_pairing_open(l, now_ms)) {
			if (l->own_known && on_pairing_channel(l)) {
				set_channel(l, l->home, out);
			}
		} else if (now_ms - l->slot_start_ms >= SLOT_MS) {
			if (!on_pairing_channel(l)) {
				set_channel(l, CHAN_PAIRING, out);
			} else {
				if (!l->own_known) {
					l->fixed_next = (l->fixed_next + 1) % sizeof(diag_nuphy_fixed_channels);
				}
				set_channel(l, own_slot_channel(l), out);
			}
			l->slot_start_ms = now_ms;
		}
		break;
	case LINK_STATE_SURVEY:
		break;
	}
	out->window_closed |= pairwin_ran_out(&l->window, now_ms);
}

void link_radio_resumed(struct link *l, uint32_t now_ms)
{
	l->last_rx_ms = now_ms;
}

void link_replies_dropped(struct link *l, uint32_t now_ms)
{
	l->last_status_ms = now_ms - STATUS_EVERY_MS;
}

void link_set_host_leds(struct link *l, uint8_t leds)
{
	l->host_leds = leds;
}

void link_set_host_suspended(struct link *l, bool suspended)
{
	l->host_suspended = suspended;
	if (!suspended) {
		l->suspends_sent = 0;
	}
}

void link_open_pairing(struct link *l, uint32_t now_ms)
{
	pairwin_open(&l->window, DIAG_PAIR_WINDOW_MS, now_ms);
}

enum diag_window link_window(const struct link *l, uint32_t now_ms)
{
	return pairwin_window(&l->window, now_ms);
}

bool link_pairing_open(const struct link *l, uint32_t now_ms)
{
	return pairwin_is_open(&l->window, now_ms);
}

bool link_pairing(const struct link *l, uint32_t now_ms)
{
	return link_window(l, now_ms) != DIAG_WINDOW_CLOSED;
}

bool link_channels_allowed(const uint8_t channels[LINK_OWN_CHANNELS])
{
	for (int i = 0; i < LINK_OWN_CHANNELS; i++) {
		if (!chan_operable(channels[i])) {
			return false;
		}
		for (int j = 0; j < i; j++) {
			if (channels[i] == channels[j]) {
				return false;
			}
		}
	}
	return true;
}

bool link_force_channels(struct link *l, const uint8_t *channels)
{
	if (channels == NULL) {
		if (l->next_offer_kind == OFFER_FORCED) {
			l->next_offer_kind = OFFER_NONE;
		}
		return true;
	}
	if (!link_channels_allowed(channels)) {
		return false;
	}
	memcpy(l->next_offer, channels, sizeof(l->next_offer));
	l->next_offer_kind = OFFER_FORCED;
	return true;
}

const uint8_t *link_forced_channels(const struct link *l)
{
	return l->next_offer_kind == OFFER_FORCED ? l->next_offer : NULL;
}

/* A retune drops the replies queued on the radio, suspend frames among them. */
static bool movable(const struct link *l, uint32_t now_ms)
{
	return l->own_known && !link_pairing_open(l, now_ms) && !l->host_suspended &&
	       (l->state == LINK_STATE_CONNECTED || l->state == LINK_STATE_IDLE);
}

enum chanmove_result link_move(struct link *l, uint8_t channel, uint32_t now_ms)
{
	if (memchr(l->channels, channel, l->channel_count) == NULL) {
		return CHANMOVE_NOT_A_CHANNEL;
	}
	if (!movable(l, now_ms)) {
		return CHANMOVE_NOT_MOVABLE;
	}
	l->home = channel;
	l->channel = channel;
	return CHANMOVE_DONE;
}

int link_home(const struct link *l)
{
	return l->own_known ? l->home : CHANMOVE_NO_HOME;
}

int link_movable(const struct link *l, uint32_t now_ms)
{
	return movable(l, now_ms) ? l->home : CHANMOVE_NO_HOME;
}

size_t link_channels(const struct link *l, const uint8_t **out)
{
	*out = l->channels;
	return l->channel_count;
}
