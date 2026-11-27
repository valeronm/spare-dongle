#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <bluetooth/gatt_dm.h>
#include <bluetooth/hci_vs_sdc.h>
#include <bluetooth/services/bas_client.h>
#include <bluetooth/services/hogp.h>
/* Host-internal: the public API gives a bond's address but not how it paired. */
#include <host/keys.h>
#include "ble.h"
#include "clock.h"
#include "core.h"
#include "store.h"

K_MSGQ_DEFINE(ble_events, sizeof(struct ble_event), 16, 4);

#define UUID_LOGITECH 0xfd72

BUILD_ASSERT(CONFIG_BT_MAX_CONN == DEVICE_SLOTS);
BUILD_ASSERT(CONFIG_BT_MAX_PAIRED == BTPOLICY_STORED_MAX);
BUILD_ASSERT(BTPOLICY_APPEARANCE_KEYBOARD == BT_APPEARANCE_HID_KEYBOARD);
BUILD_ASSERT(BTPOLICY_APPEARANCE_MOUSE == BT_APPEARANCE_HID_MOUSE);

/* In the order waiting discoveries start. */
enum discovery {
	DISCOVERY_NONE,
	DISCOVERY_HID,
	DISCOVERY_BATTERY,
};

struct link {
	struct bt_conn *conn;
	struct bt_hogp hogp;
	struct bt_bas_client bas;
	uint8_t map[BLE_MAP_MAX];
	size_t map_len;
	char name[BLE_NAME_MAX + 1];
	struct bt_gatt_read_params name_read;
	struct bt_gatt_discover_params mode_discover;
	struct bt_gatt_write_params mode_write;
	enum discovery wanted;
	bool clicks;
	bool failed;
	bool passkey_used;
	/* The device took the Bolt mode write on this connection. */
	bool bolt_mode;
	struct diag_link_params params;
	struct diag_asked_params asked;
};

static struct link links[DEVICE_SLOTS];
/* A new device's advert that lacked the HID service or a kind, kept for its scan response. */
static struct btpolicy_advert pending;
static bool pending_set;
static enum btpolicy_scan scan_mode;
static uint32_t passkey;
static bool offering;

static uint8_t slot_of_link(const struct link *l)
{
	return (uint8_t)(l - links);
}

static struct link *link_of_conn(const struct bt_conn *conn)
{
	for (int s = 0; s < DEVICE_SLOTS; s++) {
		if (links[s].conn == conn) {
			return &links[s];
		}
	}
	return NULL;
}

static void put_event(const struct ble_event *ev)
{
	if (k_msgq_put(&ble_events, ev, K_NO_WAIT) != 0) {
		core_count_dropped();
		printk("bluetooth event dropped\n");
	}
}

static void emit(struct link *l, struct ble_event ev)
{
	ev.slot = slot_of_link(l);
	put_event(&ev);
}

static enum btpolicy_failure failure_of(enum ble_stage stage, int error)
{
	if (stage != BLE_STAGE_SECURITY || error == BT_SECURITY_ERR_UNSPECIFIED) {
		return BTPOLICY_FAILURE_LINK;
	}
	return error == BT_SECURITY_ERR_PIN_OR_KEY_MISSING ? BTPOLICY_FAILURE_KEY_MISSING
							   : BTPOLICY_FAILURE_REFUSAL;
}

static void emit_failed(struct link *l, enum ble_stage stage, int error)
{
	emit(l, (struct ble_event){
			.type = BLE_FAILED,
			.failed = {.stage = stage, .error = error, .why = failure_of(stage, error)},
		});
}

static void fail(struct link *l, enum ble_stage stage, int error)
{
	/* The stages still running when one fails, or when the connection is being closed, end in
	 * errors of their own. */
	if (l->failed) {
		return;
	}
	l->failed = true;
	emit_failed(l, stage, error);
	ble_disconnect(slot_of_link(l));
}

static void next_discovery(void);

static void want_discovery(struct link *l, enum discovery what)
{
	l->wanted = what;
	next_discovery();
}

static uint8_t on_report(struct bt_hogp *h, struct bt_hogp_rep_info *rep, uint8_t err,
			 const uint8_t *data)
{
	struct ble_event ev = {
		.type = BLE_REPORT,
		.report = {.received_us = clock_now_us(), .id = bt_hogp_rep_id(rep)},
	};
	size_t len = bt_hogp_rep_size(rep);

	if (err || !data) {
		return BT_GATT_ITER_STOP;
	}
	ev.report.len = MIN(len, sizeof(ev.report.data));
	memcpy(ev.report.data, data, ev.report.len);
	emit(CONTAINER_OF(h, struct link, hogp), ev);
	return BT_GATT_ITER_CONTINUE;
}

/* Each subscription holds an ATT buffer until the device answers, and one device's subscriptions
 * are all sent at once, with the battery discovery's first request behind them. */
BUILD_ASSERT(CONFIG_BT_ATT_TX_COUNT >= CONFIG_BT_HOGP_REPORTS_MAX / DEVICE_SLOTS + 1);

static void subscribe(struct link *l)
{
	struct bt_hogp_rep_info *rep = NULL;

	while ((rep = bt_hogp_rep_next(&l->hogp, rep)) != NULL) {
		int err;

		if (bt_hogp_rep_type(rep) != BT_HIDS_REPORT_TYPE_INPUT) {
			continue;
		}
		err = bt_hogp_rep_subscribe(&l->hogp, rep, on_report);
		if (err) {
			fail(l, BLE_STAGE_SUBSCRIBE, err);
			return;
		}
	}
	want_discovery(l, DISCOVERY_BATTERY);
}

static void on_map_part(struct bt_hogp *h, uint8_t err, const uint8_t *data, size_t size,
			size_t offset)
{
	struct link *l = CONTAINER_OF(h, struct link, hogp);
	int ret;

	if (err) {
		fail(l, BLE_STAGE_MAP, err);
		return;
	}
	if (!data || size == 0) {
		l->map_len = offset;
		emit(l, (struct ble_event){.type = BLE_MAP_READ});
		subscribe(l);
		return;
	}
	if (offset + size > sizeof(l->map)) {
		fail(l, BLE_STAGE_MAP, -ENOMEM);
		return;
	}
	memcpy(&l->map[offset], data, size);
	ret = bt_hogp_map_read(h, on_map_part, offset + size, K_NO_WAIT);
	if (ret) {
		fail(l, BLE_STAGE_MAP, ret);
	}
}

static void on_hogp_ready(struct bt_hogp *h)
{
	int err = bt_hogp_map_read(h, on_map_part, 0, K_NO_WAIT);

	if (err) {
		fail(CONTAINER_OF(h, struct link, hogp), BLE_STAGE_MAP, err);
	}
}

static void on_hogp_failed(struct bt_hogp *h, int err)
{
	fail(CONTAINER_OF(h, struct link, hogp), BLE_STAGE_HID_SETUP, err);
}

static const struct bt_hogp_init_params hogp_params = {
	.ready_cb = on_hogp_ready,
	.prep_error_cb = on_hogp_failed,
};

/* bt_gatt_dm can end a discovery after its connection closed. */
static struct link *discovering;

/* Must run before next_discovery, which overwrites discovering. */
static struct link *take_discovering(void)
{
	struct link *l = discovering;

	discovering = NULL;
	return l;
}

static void on_discovered(struct bt_gatt_dm *dm, void *context)
{
	struct link *l = take_discovering();
	int err;

	ARG_UNUSED(context);
	if (!l) {
		bt_gatt_dm_data_release(dm);
		next_discovery();
		return;
	}
	err = bt_hogp_handles_assign(dm, &l->hogp);
	bt_gatt_dm_data_release(dm);
	next_discovery();
	if (err) {
		fail(l, BLE_STAGE_HID_SETUP, err);
	}
}

static void on_discovery_error(struct bt_conn *conn, int err, void *context)
{
	struct link *l = take_discovering();

	ARG_UNUSED(conn);
	ARG_UNUSED(context);
	next_discovery();
	if (l) {
		fail(l, BLE_STAGE_DISCOVERY, err);
	}
}

static void on_no_hid_service(struct bt_conn *conn, void *context)
{
	on_discovery_error(conn, -ENOENT, context);
}

static const struct bt_gatt_dm_cb hid_discovery = {
	.completed = on_discovered,
	.service_not_found = on_no_hid_service,
	.error_found = on_discovery_error,
};

static void emit_battery(struct bt_bas_client *bas, uint8_t level)
{
	struct device_battery b = DEVICE_BATTERY_UNKNOWN;

	if (level != BT_BAS_VAL_INVALID) {
		b.level = level;
	}
	emit(CONTAINER_OF(bas, struct link, bas),
	     (struct ble_event){.type = BLE_BATTERY, .battery = b});
}

static void on_battery_read(struct bt_bas_client *bas, uint8_t level, int err)
{
	emit_battery(bas, err ? BT_BAS_VAL_INVALID : level);
}

static void use_battery(struct link *l, struct bt_gatt_dm *dm)
{
	int err = bt_bas_handles_assign(dm, &l->bas);

	if (!err) {
		err = bt_bas_read_battery_level(&l->bas, on_battery_read);
	}
	if (!err && bt_bas_notify_supported(&l->bas)) {
		err = bt_bas_subscribe_battery_level(&l->bas, emit_battery);
	}
	if (err) {
		printk("bluetooth link %u battery unread: %d\n", slot_of_link(l), err);
	}
}

static void on_battery_found(struct bt_gatt_dm *dm, void *context)
{
	struct link *l = take_discovering();

	ARG_UNUSED(context);
	if (l) {
		use_battery(l, dm);
	}
	bt_gatt_dm_data_release(dm);
	next_discovery();
}

static void on_no_battery(struct bt_conn *conn, void *context)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(context);
	take_discovering();
	next_discovery();
}

static void on_battery_error(struct bt_conn *conn, int err, void *context)
{
	struct link *l = take_discovering();

	ARG_UNUSED(conn);
	ARG_UNUSED(context);
	if (l) {
		printk("bluetooth link %u battery discovery failed: %d\n", slot_of_link(l), err);
	}
	next_discovery();
}

static const struct bt_gatt_dm_cb battery_discovery = {
	.completed = on_battery_found,
	.service_not_found = on_no_battery,
	.error_found = on_battery_error,
};

static int start_discovery(struct link *l, enum discovery what)
{
	int err = what == DISCOVERY_HID
			  ? bt_gatt_dm_start(l->conn, BT_UUID_HIDS, &hid_discovery, NULL)
			  : bt_gatt_dm_start(l->conn, BT_UUID_BAS, &battery_discovery, NULL);

	if (!err) {
		discovering = l;
	}
	return err;
}

/* bt_gatt_dm runs one discovery at a time and refuses a second start with -EALREADY. */
static void next_discovery(void)
{
	for (;;) {
		struct link *l = NULL;
		enum discovery what;
		int err;

		for (int s = 0; s < DEVICE_SLOTS; s++) {
			if (links[s].wanted != DISCOVERY_NONE &&
			    (!l || links[s].wanted < l->wanted)) {
				l = &links[s];
			}
		}
		if (!l) {
			return;
		}
		what = l->wanted;
		err = start_discovery(l, what);
		if (err == -EALREADY) {
			return;
		}
		l->wanted = DISCOVERY_NONE;
		if (!err) {
			return;
		}
		if (what == DISCOVERY_HID) {
			fail(l, BLE_STAGE_DISCOVERY, err);
		} else {
			printk("bluetooth link %u battery discovery not started: %d\n",
			       slot_of_link(l), err);
		}
	}
}

static void copy_name(char out[BLE_NAME_MAX + 1], const void *data, size_t len)
{
	len = MIN(len, BLE_NAME_MAX);
	memcpy(out, data, len);
	out[len] = '\0';
}

static uint8_t on_name(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
		       const void *data, uint16_t length)
{
	struct link *l = CONTAINER_OF(params, struct link, name_read);

	ARG_UNUSED(conn);
	if (!err && data) {
		copy_name(l->name, data, length);
	}
	want_discovery(l, DISCOVERY_HID);
	return BT_GATT_ITER_STOP;
}

static void read_name(struct link *l)
{
	l->name_read = (struct bt_gatt_read_params){
		.func = on_name,
		.by_uuid = {
			.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE,
			.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE,
			.uuid = BT_UUID_GAP_DEVICE_NAME,
		},
	};
	if (bt_gatt_read(l->conn, &l->name_read) != 0) {
		want_discovery(l, DISCOVERY_HID);
	}
}

BUILD_ASSERT(BLE_ADDRESS_MAX >= BT_ADDR_LE_STR_LEN);
BUILD_ASSERT(BLE_REASON_NOT_ESTABLISHED == BT_HCI_ERR_CONN_FAIL_TO_ESTAB);
BUILD_ASSERT(sizeof(bt_addr_le_t) == BTPOLICY_ADDR_LEN);
BUILD_ASSERT((int)DIAG_PHY_LE1_M == (int)BT_GAP_LE_PHY_1M &&
	     (int)DIAG_PHY_LE2_M == (int)BT_GAP_LE_PHY_2M &&
	     (int)DIAG_PHY_CODED == (int)BT_GAP_LE_PHY_CODED);

static void connection_ended(struct link *l, uint8_t reason)
{
	k_sched_lock();
	l->params = (struct diag_link_params){0};
	l->asked = (struct diag_asked_params){0};
	k_sched_unlock();
	if (bt_hogp_assign_check(&l->hogp)) {
		bt_hogp_release(&l->hogp);
	}
	l->wanted = DISCOVERY_NONE;
	if (discovering == l) {
		discovering = NULL;
	}
	bt_conn_unref(l->conn);
	l->conn = NULL;
	emit(l, (struct ble_event){.type = BLE_DISCONNECTED, .reason = reason});
}

static void read_params(struct link *l)
{
	struct bt_conn_info info;
	struct diag_link_params p;

	if (bt_conn_get_info(l->conn, &info) != 0) {
		return;
	}
	p = (struct diag_link_params){
		.interval = BT_GAP_US_TO_CONN_INTERVAL(info.le.interval_us),
		.latency = info.le.latency,
		.timeout = info.le.timeout,
		.tx_phy = info.le.phy->tx_phy,
		.rx_phy = info.le.phy->rx_phy,
	};
	k_sched_lock();
	l->params = p;
	k_sched_unlock();
	printk("bluetooth link %u: interval %u us, latency %u, timeout %u ms, phy %u/%u\n",
	       slot_of_link(l), info.le.interval_us, info.le.latency, info.le.timeout * 10u,
	       info.le.phy->tx_phy, info.le.phy->rx_phy);
}

/* A link lost during the Bolt mode write reports its loss through its disconnect. */
static void request_security(struct link *l)
{
	int ret = bt_conn_set_security(l->conn, BT_SECURITY_L2);

	if (ret && ret != -ENOTCONN) {
		fail(l, BLE_STAGE_SECURITY, ret);
	}
}

/* A Bolt receiver writes 02 here before pairing, after which the device demands a passkey. */
static struct bt_uuid_128 bolt_mode_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(UUID_LOGITECH << 16 | 0x0003, 0x0000, 0x1000, 0x8000, 0x011f2000046d));
static const uint8_t bolt_mode = 0x02;

/* Pairing goes ahead whatever the write's outcome. */
static void on_bolt_mode_written(struct bt_conn *conn, uint8_t err,
				 struct bt_gatt_write_params *params)
{
	struct link *l = CONTAINER_OF(params, struct link, mode_write);

	ARG_UNUSED(conn);
	if (err) {
		printk("bluetooth link %u Bolt mode not written: att error %u\n", slot_of_link(l),
		       err);
	}
	l->bolt_mode = !err;
	request_security(l);
}

static uint8_t on_bolt_mode_found(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				  struct bt_gatt_discover_params *params)
{
	struct link *l = CONTAINER_OF(params, struct link, mode_discover);

	if (!attr) {
		printk("bluetooth link %u has no Bolt mode\n", slot_of_link(l));
		request_security(l);
		return BT_GATT_ITER_STOP;
	}
	l->mode_write = (struct bt_gatt_write_params){
		.func = on_bolt_mode_written,
		.handle = ((const struct bt_gatt_chrc *)attr->user_data)->value_handle,
		.data = &bolt_mode,
		.length = sizeof(bolt_mode),
	};
	if (bt_gatt_write(conn, &l->mode_write) != 0) {
		request_security(l);
	}
	return BT_GATT_ITER_STOP;
}

/* Only a new pairing gets the write, since a bonded device reconnects encrypted. */
static bool write_bolt_mode(struct link *l)
{
	if (!offering || !l->clicks ||
	    bt_le_bond_exists(BT_ID_DEFAULT, bt_conn_get_dst(l->conn))) {
		return false;
	}
	l->mode_discover = (struct bt_gatt_discover_params){
		.uuid = &bolt_mode_uuid.uuid,
		.func = on_bolt_mode_found,
		.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE,
		.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE,
		.type = BT_GATT_DISCOVER_CHARACTERISTIC,
	};
	return bt_gatt_discover(l->conn, &l->mode_discover) == 0;
}

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	struct link *l = link_of_conn(conn);
	struct ble_event ev = {.type = BLE_CONNECTED};

	if (!l) {
		return;
	}
	if (err) {
		emit_failed(l, BLE_STAGE_CONNECT, err);
		connection_ended(l, err);
		return;
	}
	l->map_len = 0;
	bt_addr_le_to_str(bt_conn_get_dst(conn), ev.address, sizeof(ev.address));
	emit(l, ev);
	read_params(l);
	l->passkey_used = false;
	l->bolt_mode = false;
	l->failed = false;
	if (!write_bolt_mode(l)) {
		request_security(l);
	}
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	struct link *l = link_of_conn(conn);

	if (l) {
		connection_ended(l, reason);
	}
}

static void on_security_changed(struct bt_conn *conn, bt_security_t level,
				enum bt_security_err err)
{
	struct link *l = link_of_conn(conn);

	ARG_UNUSED(level);
	if (!l) {
		return;
	}
	if (err) {
		fail(l, BLE_STAGE_SECURITY, err);
		return;
	}
	read_name(l);
}

static void on_params_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
			      uint16_t timeout)
{
	struct link *l = link_of_conn(conn);

	ARG_UNUSED(interval);
	ARG_UNUSED(latency);
	ARG_UNUSED(timeout);
	if (l) {
		read_params(l);
	}
}

/* A controller left to choose from a requested range takes the longest interval. */
static bool on_param_request(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	struct link *l = link_of_conn(conn);

	if (l) {
		k_sched_lock();
		l->asked = (struct diag_asked_params){
			.interval_min = param->interval_min,
			.interval_max = param->interval_max,
			.latency = param->latency,
			.timeout = param->timeout,
		};
		k_sched_unlock();
	}
	param->interval_max = param->interval_min;
	return true;
}

static void on_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param)
{
	struct link *l = link_of_conn(conn);

	ARG_UNUSED(param);
	if (l) {
		read_params(l);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = on_connected,
	.disconnected = on_disconnected,
	.security_changed = on_security_changed,
	.le_param_req = on_param_request,
	.le_param_updated = on_params_updated,
	.le_phy_updated = on_phy_updated,
};

static void on_pairing_complete(struct bt_conn *conn, bool bonded)
{
	struct link *l = link_of_conn(conn);

	if (l && bonded) {
		emit(l, (struct ble_event){.type = BLE_PAIRED,
					   .paired = {.passkey_used = l->passkey_used,
						      .bolt_mode = l->bolt_mode}});
	}
}

static struct bt_conn_auth_info_cb pairing_callbacks = {
	.pairing_complete = on_pairing_complete,
};

static bool parse_field(struct bt_data *data, void *user_data)
{
	struct btpolicy_advert *a = user_data;

	if (data->type == BT_DATA_NAME_COMPLETE || data->type == BT_DATA_NAME_SHORTENED) {
		copy_name(a->name, data->data, data->data_len);
	}

	if (data->type == BT_DATA_GAP_APPEARANCE && data->data_len == 2 &&
	    a->kind == DEVICE_NO_KIND) {
		a->kind = btpolicy_kind_of(sys_get_le16(data->data));
	}
	if (data->type != BT_DATA_UUID16_SOME && data->type != BT_DATA_UUID16_ALL) {
		return true;
	}
	for (size_t i = 0; i + 1 < data->data_len; i += 2) {
		uint16_t uuid = sys_get_le16(&data->data[i]);

		a->hid_service |= uuid == BT_UUID_HIDS_VAL;
		a->bolt |= uuid == UUID_LOGITECH;
	}
	return true;
}

#define FORGETS_MAX 4

/* Each holds the event that reports its delete. */
static struct {
	bool due;
	struct ble_event ev;
} forgets[FORGETS_MAX];
/* The receive thread reads the deletes due while the store's thread finishes them. */
static struct k_spinlock forgets_lock;

static void forget_due(struct k_work *work)
{
	for (int i = 0; i < FORGETS_MAX; i++) {
		struct ble_event ev;
		bt_addr_le_t peer;
		k_spinlock_key_t key = k_spin_lock(&forgets_lock);
		bool due = forgets[i].due;

		ev = forgets[i].ev;
		k_spin_unlock(&forgets_lock, key);
		if (!due) {
			continue;
		}
		memcpy(&peer, ev.forgotten.addr, sizeof(peer));
		ev.forgotten.error = bt_unpair(BT_ID_DEFAULT, &peer);
		key = k_spin_lock(&forgets_lock);
		forgets[i].due = false;
		k_spin_unlock(&forgets_lock, key);
		put_event(&ev);
	}
}

static K_WORK_DEFINE(forget_work, forget_due);

int ble_forget(uint8_t slot, const uint8_t addr[BTPOLICY_ADDR_LEN], uint8_t tag)
{
	k_spinlock_key_t key = k_spin_lock(&forgets_lock);

	for (int i = 0; i < FORGETS_MAX; i++) {
		if (!forgets[i].due) {
			forgets[i].due = true;
			forgets[i].ev = (struct ble_event){
				.type = BLE_FORGOTTEN, .slot = slot, .forgotten = {.tag = tag}};
			memcpy(forgets[i].ev.forgotten.addr, addr, BTPOLICY_ADDR_LEN);
			k_spin_unlock(&forgets_lock, key);
			store_run(&forget_work);
			return 0;
		}
	}
	k_spin_unlock(&forgets_lock, key);
	return -ENOMEM;
}

static bool forgetting(const bt_addr_le_t *addr)
{
	k_spinlock_key_t key = k_spin_lock(&forgets_lock);
	bool found = false;

	for (int i = 0; i < FORGETS_MAX; i++) {
		found |= forgets[i].due &&
			 memcmp(forgets[i].ev.forgotten.addr, addr, BTPOLICY_ADDR_LEN) == 0;
	}
	k_spin_unlock(&forgets_lock, key);
	return found;
}

static void on_advert(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
		      struct net_buf_simple *data)
{
	struct ble_event ev = {.type = BLE_ADVERT};
	struct btpolicy_advert *a = &ev.advert;
	bool response = type == BT_GAP_ADV_TYPE_SCAN_RSP;

	if (forgetting(addr)) {
		return;
	}
	if (response) {
		if (!pending_set || memcmp(pending.addr, addr, sizeof(pending.addr)) != 0) {
			return;
		}
		*a = pending;
		pending_set = false;
	} else {
		a->directed = type == BT_GAP_ADV_TYPE_ADV_DIRECT_IND;
		a->connectable = a->directed || type == BT_GAP_ADV_TYPE_ADV_IND;
		a->stored = bt_le_bond_exists(BT_ID_DEFAULT, addr);
		a->kind = DEVICE_NO_KIND;
		memcpy(a->addr, addr, sizeof(a->addr));
	}
	a->rssi = rssi;
	if (!a->directed) {
		bt_data_parse(data, parse_field, a);
	}
	/* Adverts from other devices would fill the event queue. */
	if (!btpolicy_interesting(a)) {
		if (!response && scan_mode == BTPOLICY_SCAN_ACTIVE && a->connectable &&
		    !a->directed) {
			pending = *a;
			pending_set = true;
		}
		return;
	}
	put_event(&ev);
}

int ble_scan(enum btpolicy_scan mode)
{
	bool pairing;
	int err;

	if (mode == scan_mode) {
		return 0;
	}
	if (scan_mode != BTPOLICY_SCAN_OFF) {
		err = bt_le_scan_stop();
		if (err) {
			return err;
		}
		scan_mode = BTPOLICY_SCAN_OFF;
	}
	if (mode == BTPOLICY_SCAN_OFF) {
		return 0;
	}
	pending_set = false;
	pairing = mode == BTPOLICY_SCAN_ACTIVE;
	/* A scan of 11.25 ms every 1.28 s takes tens of seconds to hear a device advertising every
	 * few hundred milliseconds. */
	err = bt_le_scan_start(
		BT_LE_SCAN_PARAM(pairing ? BT_LE_SCAN_TYPE_ACTIVE : BT_LE_SCAN_TYPE_PASSIVE,
				 BT_LE_SCAN_OPT_NONE, BT_GAP_SCAN_FAST_INTERVAL,
				 pairing ? BT_GAP_SCAN_FAST_WINDOW : BT_GAP_SCAN_FAST_INTERVAL),
		on_advert);
	if (!err) {
		scan_mode = mode;
	}
	return err;
}

/* Reading a device's services takes dozens of exchanges, each two connection intervals long. */
#define SHORTEST_INTERVAL 6 /* 7.5 ms */
BUILD_ASSERT(DEVICE_SLOTS * CONFIG_BT_CTLR_SDC_MAX_CONN_EVENT_LEN_DEFAULT <=
	     BT_CONN_INTERVAL_TO_US(SHORTEST_INTERVAL));
#define SETUP_CONN_PARAM                                                                           \
	BT_LE_CONN_PARAM(SHORTEST_INTERVAL, SHORTEST_INTERVAL, 0, BT_GAP_MS_TO_CONN_TIMEOUT(4000))

int ble_connect(uint8_t slot, const uint8_t addr[BTPOLICY_ADDR_LEN],
		const char *advertised_name, bool clicks)
{
	struct link *l = &links[slot];
	bt_addr_le_t peer;
	int err;

	if (l->conn) {
		return -EALREADY;
	}
	copy_name(l->name, advertised_name, strlen(advertised_name));
	l->clicks = clicks;
	memcpy(&peer, addr, sizeof(peer));
	err = bt_conn_le_create(&peer, BT_CONN_LE_CREATE_CONN, SETUP_CONN_PARAM, &l->conn);
	if (err) {
		l->conn = NULL;
	}
	return err;
}

/* Each passkey serves one pairing, since a failed or abandoned attempt can reveal it. */
static uint32_t on_app_passkey(struct bt_conn *conn)
{
	struct link *l = link_of_conn(conn);
	uint32_t offered = passkey;

	if (l) {
		if (l->clicks) {
			offered = btpolicy_click_passkey(offered);
		}
		l->passkey_used = true;
		emit(l, (struct ble_event){
				.type = BLE_PASSKEY_AWAITED,
				.passkey = {.value = offered, .clicked = l->clicks},
			});
	}
	passkey = BT_PASSKEY_RAND;
	return offered;
}

BUILD_ASSERT((int)BT_CONN_AUTH_KEYPRESS_ENTRY_STARTED == BTPOLICY_ENTRY_STARTED &&
	     (int)BT_CONN_AUTH_KEYPRESS_DIGIT_ENTERED == BTPOLICY_KEY_ENTERED &&
	     (int)BT_CONN_AUTH_KEYPRESS_DIGIT_ERASED == BTPOLICY_KEY_ERASED &&
	     (int)BT_CONN_AUTH_KEYPRESS_CLEARED == BTPOLICY_ENTRY_CLEARED &&
	     (int)BT_CONN_AUTH_KEYPRESS_ENTRY_COMPLETED == BTPOLICY_ENTRY_COMPLETED);

static void on_keypress(struct bt_conn *conn, enum bt_conn_auth_keypress type)
{
	struct link *l = link_of_conn(conn);

	if (l) {
		emit(l, (struct ble_event){.type = BLE_KEYPRESS,
					   .keypress = (enum btpolicy_keypress)type});
	}
}

static void on_pairing_cancel(struct bt_conn *conn)
{
	ARG_UNUSED(conn);
}

static const struct bt_conn_auth_cb passkey_callbacks = {
	.app_passkey = on_app_passkey,
	.passkey_display_keypress = on_keypress,
	.cancel = on_pairing_cancel,
};

int ble_offer_passkey(bool on, uint32_t value)
{
	int err;

	passkey = value;
	if (on == offering) {
		return 0;
	}
	err = bt_conn_auth_cb_register(on ? &passkey_callbacks : NULL);
	if (!err) {
		offering = on;
	}
	return err;
}

struct bonds {
	uint8_t (*addrs)[BTPOLICY_ADDR_LEN];
	uint8_t cap;
	uint8_t count;
};

static void add_bond(const struct bt_bond_info *info, void *user_data)
{
	struct bonds *bonds = user_data;

	if (bonds->count < bonds->cap) {
		memcpy(bonds->addrs[bonds->count], &info->addr, BTPOLICY_ADDR_LEN);
	}
	bonds->count++;
}

uint8_t ble_stored(uint8_t addrs[][BTPOLICY_ADDR_LEN], uint8_t cap)
{
	struct bonds bonds = {.addrs = addrs, .cap = cap};

	bt_foreach_bond(BT_ID_DEFAULT, add_bond, &bonds);
	return bonds.count;
}

uint8_t ble_stored_count(void)
{
	return ble_stored(NULL, 0);
}

/* The advertising channels hold the indexes above these, and nearby devices advertise there all the
 * time. */
#define DATA_CHANNELS 37
#define ENERGY_NOT_MEASURED 127
#define SURVEY_INTERVAL_US 10000

/* The host's receive thread adds to the tally while the main loop takes it. */
static struct noise_tally data_noise;

static bool on_vendor_event(struct net_buf_simple *buf)
{
	const sdc_hci_subevent_vs_qos_channel_survey_report_t *r;

	if (net_buf_simple_pull_u8(buf) != SDC_HCI_SUBEVENT_VS_QOS_CHANNEL_SURVEY_REPORT ||
	    buf->len < sizeof(*r)) {
		return false;
	}
	r = (const void *)buf->data;
	k_sched_lock();
	for (int ch = 0; ch < DATA_CHANNELS; ch++) {
		if (r->channel_energy[ch] != ENERGY_NOT_MEASURED) {
			noise_add(&data_noise, r->channel_energy[ch]);
		}
	}
	k_sched_unlock();
	return true;
}

static int start_survey(void)
{
	sdc_hci_cmd_vs_qos_channel_survey_enable_t survey = {
		.enable = 1,
		.interval_us = SURVEY_INTERVAL_US,
	};
	int err = bt_hci_register_vnd_evt_cb(on_vendor_event);

	return err ? err : hci_vs_sdc_qos_channel_survey_enable(&survey);
}

void ble_fill_link(uint8_t slot, struct diag_status *st)
{
	k_sched_lock();
	st->params = links[slot].params;
	st->asked = links[slot].asked;
	k_sched_unlock();
}

bool ble_take_noise(struct noise_tally *t, int *home)
{
	k_sched_lock();
	*t = data_noise;
	data_noise = (struct noise_tally){0};
	k_sched_unlock();
	*home = CHANMOVE_NO_HOME;
	return true;
}

int ble_start(void)
{
	int err = bt_enable(NULL);

	if (!err) {
		err = settings_load();
	}
	if (!err) {
		err = bt_conn_auth_info_cb_register(&pairing_callbacks);
	}
	for (int s = 0; !err && s < DEVICE_SLOTS; s++) {
		bt_hogp_init(&links[s].hogp, &hogp_params);
		bt_bas_client_init(&links[s].bas);
	}
	if (!err) {
		int survey_err = start_survey();

		if (survey_err) {
			printk("bluetooth noise survey not started: %d\n", survey_err);
		}
	}
	return err;
}

bool ble_take_event(struct ble_event *ev, k_timeout_t wait)
{
	return k_msgq_get(&ble_events, ev, wait) == 0;
}

size_t ble_copy_map(uint8_t slot, uint8_t *out, size_t cap)
{
	size_t len;

	k_sched_lock();
	len = MIN(links[slot].map_len, cap);
	memcpy(out, links[slot].map, len);
	k_sched_unlock();
	return len;
}

void ble_copy_name(uint8_t slot, char out[BLE_NAME_MAX + 1])
{
	k_sched_lock();
	memcpy(out, links[slot].name, sizeof(links[slot].name));
	k_sched_unlock();
}

/* The receive thread releases a link's connection when it ends; NULL without one. */
static struct bt_conn *link_conn(struct link *l)
{
	struct bt_conn *conn = NULL;

	k_sched_lock();
	if (l->conn) {
		conn = bt_conn_ref(l->conn);
	}
	k_sched_unlock();
	return conn;
}

static void disconnect(uint8_t slot, uint8_t reason)
{
	struct link *l = &links[slot];
	struct bt_conn *conn;

	l->failed = true;
	conn = link_conn(l);
	if (conn) {
		bt_conn_disconnect(conn, reason);
		bt_conn_unref(conn);
	}
}

void ble_disconnect(uint8_t slot)
{
	disconnect(slot, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

void ble_power_off(uint8_t slot)
{
	disconnect(slot, BT_HCI_ERR_REMOTE_POWER_OFF);
}

static void on_written(struct bt_hogp *h, struct bt_hogp_rep_info *rep, uint8_t err)
{
	ARG_UNUSED(h);
	ARG_UNUSED(rep);
	ARG_UNUSED(err);
}

int ble_write_report(uint8_t slot, uint8_t report_id, const uint8_t *data, uint8_t len)
{
	struct bt_hogp *hogp = &links[slot].hogp;
	struct bt_hogp_rep_info *rep;

	if (!bt_hogp_ready_check(hogp)) {
		return -ENOTCONN;
	}
	rep = bt_hogp_rep_find(hogp, BT_HIDS_REPORT_TYPE_OUTPUT, report_id);
	if (!rep) {
		return -ENOENT;
	}
	/* The library refuses a write without a completion callback. */
	return bt_hogp_rep_write_wo_rsp(hogp, rep, data, len, on_written);
}

bool ble_bond_secure(const uint8_t addr[BTPOLICY_ADDR_LEN])
{
	bt_addr_le_t peer;
	const struct bt_keys *keys;

	memcpy(&peer, addr, sizeof(peer));
	keys = bt_keys_find_addr(BT_ID_DEFAULT, &peer);
	return keys && (keys->flags & BT_KEYS_SC);
}

int ble_rssi(uint8_t slot, int8_t *dbm)
{
	struct bt_conn *conn = link_conn(&links[slot]);
	struct bt_hci_cp_read_rssi *cp;
	struct net_buf *buf;
	struct net_buf *rsp;
	uint16_t handle;
	int8_t rssi;
	int err;

	if (!conn) {
		return -ENOTCONN;
	}
	err = bt_hci_get_conn_handle(conn, &handle);
	bt_conn_unref(conn);
	if (err) {
		return -ENOTCONN;
	}
	buf = bt_hci_cmd_alloc(K_MSEC(10));
	if (!buf) {
		return -ENOBUFS;
	}
	cp = net_buf_add(buf, sizeof(*cp));
	cp->handle = sys_cpu_to_le16(handle);
	err = bt_hci_cmd_send_sync(BT_HCI_OP_READ_RSSI, buf, &rsp);
	if (err) {
		return err;
	}
	rssi = ((struct bt_hci_rp_read_rssi *)rsp->data)->rssi;
	net_buf_unref(rsp);
	if (rssi == BT_HCI_LE_RSSI_NOT_AVAILABLE) {
		return -ENODATA;
	}
	*dbm = rssi;
	return 0;
}

const char *ble_stage_name(enum ble_stage stage)
{
	switch (stage) {
	case BLE_STAGE_CONNECT:
		return "connect";
	case BLE_STAGE_SECURITY:
		return "security";
	case BLE_STAGE_DISCOVERY:
		return "discovery";
	case BLE_STAGE_HID_SETUP:
		return "hid setup";
	case BLE_STAGE_MAP:
		return "report map";
	case BLE_STAGE_SUBSCRIBE:
		return "subscribe";
	}
	return "unknown";
}
