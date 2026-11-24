#include <stdio.h>
#include <zephyr/settings/settings.h>
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

int store_init(void)
{
	return settings_subsys_init();
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

bool store_load_bluetooth(void *out, size_t len)
{
	return load_exact(BLUETOOTH_KEY, out, len);
}

int store_save_bluetooth(const void *in, size_t len)
{
	return settings_save_one(BLUETOOTH_KEY, in, len);
}
