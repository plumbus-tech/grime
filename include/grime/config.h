/* config.h — the single JSON config file -> engine tree. See docs/CONFIG.md. */
#ifndef GRIME_CONFIG_H
#define GRIME_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#include <stdint.h>

#include "grime/device.h"
#include "grime/engine.h"

#define GRIME_EMERGENCY_MAX 4

typedef struct {
	grime_device_match *devices; /* ndevices entries, in config order */
	size_t ndevices;
	grime_node *keymap;
	/* the keymap binds at least one button, so grime will need a pointer to
	 * emit through whether or not it grabs a mouse */
	bool wants_pointer;
	/* Held together for a second, this gets the keyboard back. Checked before
	 * the keymap, so no binding can shadow it. Empty means the user turned it
	 * off, which is their business but worth a warning. */
	uint16_t emergency[GRIME_EMERGENCY_MAX];
	size_t nemergency;
} grime_config;

int grime_config_load(const char *path, grime_config *out, char *err, size_t errlen);
int grime_config_parse(const char *json, grime_config *out, char *err, size_t errlen);
/* The config as the canonical tree grime actually walks, for --expand.
 * Returns a malloc'd string the caller frees, or NULL with err set. */
char *grime_config_expand(const char *path, char *err, size_t errlen);
void grime_config_free(grime_config *cfg); /* frees keymap unless it was taken (set to NULL) */

#endif
