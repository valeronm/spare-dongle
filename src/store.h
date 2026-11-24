#ifndef STORE_H
#define STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int store_init(void);
/* True only when exactly len bytes were stored. */
bool store_load_nuphy(void *out, size_t len);
int store_save_nuphy(const void *in, size_t len);
int store_delete_nuphy(void);
bool store_load_forced(uint8_t out[3]);
/* NULL deletes them. */
int store_save_forced(const uint8_t *channels);
/* True only when exactly len bytes were stored for the place. */
bool store_load_unifying(uint8_t place, void *out, size_t len);
int store_save_unifying(uint8_t place, const void *in, size_t len);
int store_delete_unifying(uint8_t place);
/* Firmware up to 0.5.0 kept its one Unifying keyboard, link key included, in a record of its own. */
int store_delete_single_unifying(void);
/* True only when exactly len bytes were stored. */
bool store_load_bluetooth(void *out, size_t len);
int store_save_bluetooth(const void *in, size_t len);
bool store_load_protocol(uint8_t *out);
int store_save_protocol(uint8_t value);

#endif
