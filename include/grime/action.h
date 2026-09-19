/* action.h — the "function" at the end of a key path.
 *
 * Adding an action = one new file in src/actions/ that defines a
 * grime_action_type and calls GRIME_REGISTER_ACTION(it). Nothing else changes.
 *
 * compile() runs at config load (validate + pre-parse the JSON spec into
 * state); run() runs on the event loop and must not block. */
#ifndef GRIME_ACTION_H
#define GRIME_ACTION_H

#include <stddef.h>

#include "grime/runtime.h"

struct json_object;

typedef struct grime_action_type {
	const char *name; /* the value of "do" */
	int (*compile)(struct json_object *spec, void **state, char *err, size_t errlen);
	void (*run)(grime_runtime *rt, void *state);
	void (*free)(void *state); /* may be NULL */
} grime_action_type;

typedef struct grime_action {
	const grime_action_type *type;
	void *state;
} grime_action;

void grime_action_register(const grime_action_type *type);
const grime_action_type *grime_action_find(const char *name);
void grime_action_list(void (*fn)(const char *name, void *ud), void *ud);

/* spec is an object with a "do" member. NULL + err on failure. */
grime_action *grime_action_compile(struct json_object *spec, char *err, size_t errlen);
void grime_action_run(grime_action *a, grime_runtime *rt);
void grime_action_free(grime_action *a);

#define GRIME_REGISTER_ACTION(type)                                                    \
	static void __attribute__((constructor)) grime_register_##type(void)            \
	{                                                                               \
		grime_action_register(&(type));                                         \
	}

#endif
