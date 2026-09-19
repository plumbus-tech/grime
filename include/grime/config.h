/* config.h — the single JSON config file -> engine tree. See docs/CONFIG.md. */
#ifndef GRIME_CONFIG_H
#define GRIME_CONFIG_H

#include <stddef.h>

#include "grime/engine.h"

typedef struct {
	char **devices;
	size_t ndevices;
	grime_node *keymap;
} grime_config;

int grime_config_load(const char *path, grime_config *out, char *err, size_t errlen);
int grime_config_parse(const char *json, grime_config *out, char *err, size_t errlen);
void grime_config_free(grime_config *cfg); /* frees keymap unless it was taken (set to NULL) */

#endif
