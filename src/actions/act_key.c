/* press / release / tap: synthesize key events.
 *   {"do": "press",   "key": "a"}
 *   {"do": "release", "key": "a"}
 *   {"do": "tap",     "key": "left", "mods": ["leftctrl", "leftshift"]} */
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>

#include "grime/action.h"
#include "spec.h"
#include "grime/keynames.h"

#define MAX_MODS 8

struct key_state {
	uint16_t key;
	uint16_t mods[MAX_MODS];
	int nmods;
};

static int compile(json_object *spec, void **out, char *err, size_t errlen)
{
	const char *key = grime_spec_str_req(spec, "key", err, errlen);
	if (!key)
		return -1;
	int code = grime_key_from_name(key);
	if (code < 0) {
		snprintf(err, errlen, "unknown key \"%s\"", key);
		return -1;
	}
	bool bad;
	json_object *mods = grime_spec_array(spec, "mods", err, errlen, &bad);
	if (bad)
		return -1;
	struct key_state *s = calloc(1, sizeof *s);
	if (!s) {
		snprintf(err, errlen, "out of memory");
		return -1;
	}
	s->key = code;
	size_t n = mods ? json_object_array_length(mods) : 0;
	if (n > MAX_MODS) {
		snprintf(err, errlen, "at most %d \"mods\"", MAX_MODS);
		free(s);
		return -1;
	}
	for (size_t i = 0; i < n; i++) {
		json_object *e = json_object_array_get_idx(mods, i);
		const char *name = json_object_is_type(e, json_type_string)
					   ? json_object_get_string(e)
					   : NULL;
		int m = name ? grime_key_from_name(name) : -1;
		if (m < 0) {
			snprintf(err, errlen, "unknown mod key \"%s\"", name ? name : "(not a string)");
			free(s);
			return -1;
		}
		s->mods[s->nmods++] = m;
	}
	*out = s;
	return 0;
}

static void run_press(grime_runtime *rt, void *state)
{
	grime_emit(rt, ((struct key_state *)state)->key, 1);
}

static void run_release(grime_runtime *rt, void *state)
{
	grime_emit(rt, ((struct key_state *)state)->key, 0);
}

static void run_tap(grime_runtime *rt, void *state)
{
	struct key_state *s = state;
	for (int i = 0; i < s->nmods; i++)
		grime_emit(rt, s->mods[i], 1);
	grime_emit(rt, s->key, 1);
	grime_emit(rt, s->key, 0);
	for (int i = s->nmods - 1; i >= 0; i--)
		grime_emit(rt, s->mods[i], 0);
}

static const grime_action_type press_action = {"press", compile, run_press, free};
static const grime_action_type release_action = {"release", compile, run_release, free};
static const grime_action_type tap_action = {"tap", compile, run_tap, free};
GRIME_REGISTER_ACTION(press_action)
GRIME_REGISTER_ACTION(release_action)
GRIME_REGISTER_ACTION(tap_action)
