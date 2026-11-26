#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>
#include "btlist.h"
#include "store.h"

#define NUPHY_KEY "dongle/channels"
#define FORCED_KEY "dongle/forced"
#define UNIFYING_KEY_FORMAT "dongle/uni/%u"
#define UNIFYING_KEY_MAX sizeof("dongle/uni/255")
#define UNIFYING_SINGLE_KEY "dongle/unifying"
#define PROTOCOL_KEY "dongle/protocol"
#define BLUETOOTH_KEY "dongle/bluetooth"

static bool load_exact(const char *key, void *out, size_t len)
{
	return settings_load_one(key, out, len) == (ssize_t)len;
}

/* A flash write waits for gaps between radio events, so a page erase can stall for seconds while a
 * link runs at a short interval. */
static K_THREAD_STACK_DEFINE(slow_writes_stack, 2048);
static struct k_work_q slow_writes;

int store_init(void)
{
	k_work_queue_start(&slow_writes, slow_writes_stack, K_THREAD_STACK_SIZEOF(slow_writes_stack),
			   K_LOWEST_APPLICATION_THREAD_PRIO, NULL);
	return settings_subsys_init();
}

void store_run(struct k_work *work)
{
	k_work_submit_to_queue(&slow_writes, work);
}

void store_flush(void)
{
	k_work_queue_drain(&slow_writes, false);
}

BUILD_ASSERT(IS_ENABLED(CONFIG_SETTINGS_NVS), "store_erase clears the settings as NVS");

void store_erase(void)
{
	void *fs;
	int err;

	/* A write still queued would land after the erase. */
	store_flush();
	err = settings_storage_get(&fs);
	if (!err) {
		err = nvs_clear(fs);
	}
	if (err) {
		printk("erasing the store failed: %d\n", err);
	}
}

bool store_load_nuphy(void *out, size_t len)
{
	return load_exact(NUPHY_KEY, out, len);
}

int store_save_nuphy(const void *in, size_t len)
{
	return settings_save_one(NUPHY_KEY, in, len);
}

int store_delete_nuphy(void)
{
	return settings_delete(NUPHY_KEY);
}

bool store_load_forced(uint8_t out[3])
{
	return load_exact(FORCED_KEY, out, 3);
}

int store_save_forced(const uint8_t *channels)
{
	return channels == NULL ? settings_delete(FORCED_KEY)
				: settings_save_one(FORCED_KEY, channels, 3);
}

static void unifying_key(char key[UNIFYING_KEY_MAX], uint8_t place)
{
	snprintf(key, UNIFYING_KEY_MAX, UNIFYING_KEY_FORMAT, place);
}

int store_delete_single_unifying(void)
{
	return settings_delete(UNIFYING_SINGLE_KEY);
}

bool store_load_unifying(uint8_t place, void *out, size_t len)
{
	char key[UNIFYING_KEY_MAX];

	unifying_key(key, place);
	return load_exact(key, out, len);
}

int store_save_unifying(uint8_t place, const void *in, size_t len)
{
	char key[UNIFYING_KEY_MAX];

	unifying_key(key, place);
	return settings_save_one(key, in, len);
}

int store_delete_unifying(uint8_t place)
{
	char key[UNIFYING_KEY_MAX];

	unifying_key(key, place);
	return settings_delete(key);
}

bool store_load_protocol(uint8_t *out)
{
	return load_exact(PROTOCOL_KEY, out, 1);
}

int store_save_protocol(uint8_t value)
{
	return settings_save_one(PROTOCOL_KEY, &value, 1);
}

bool store_load_bluetooth(struct btlist *out)
{
	return load_exact(BLUETOOTH_KEY, out, sizeof(*out));
}

static struct btlist list_pending;
static K_MUTEX_DEFINE(pending_lock);

static void save_list(struct k_work *work)
{
	static struct btlist list;
	uint32_t started;
	int err;

	k_mutex_lock(&pending_lock, K_FOREVER);
	list = list_pending;
	k_mutex_unlock(&pending_lock);
	started = k_uptime_get_32();
	err = settings_save_one(BLUETOOTH_KEY, &list, sizeof(list));
	if (err) {
		printk("bluetooth device list not saved: %d\n", err);
	}
	if (k_uptime_get_32() - started > 200) {
		printk("bluetooth device list save took %u ms\n", k_uptime_get_32() - started);
	}
}

static K_WORK_DEFINE(list_save, save_list);

void store_save_bluetooth(const struct btlist *list)
{
	k_mutex_lock(&pending_lock, K_FOREVER);
	list_pending = *list;
	k_mutex_unlock(&pending_lock);
	store_run(&list_save);
}
