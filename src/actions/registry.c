#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "grime/action.h"
#include "grime/log.h"

#define MAX_TYPES 64

static const grime_action_type *types[MAX_TYPES];
static size_t ntypes;

void grime_action_register(const grime_action_type *type)
{
	if (ntypes < MAX_TYPES)
		types[ntypes++] = type;
}

const grime_action_type *grime_action_find(const char *name)
{
	for (size_t i = 0; i < ntypes; i++)
		if (!strcmp(types[i]->name, name))
			return types[i];
	return NULL;
}

void grime_action_list(void (*fn)(const char *name, void *ud), void *ud)
{
	for (size_t i = 0; i < ntypes; i++)
		fn(types[i]->name, ud);
}

grime_action *grime_action_compile(json_object *spec, char *err, size_t errlen)
{
	json_object *v;
	if (!json_object_object_get_ex(spec, "do", &v) || !json_object_is_type(v, json_type_string)) {
		snprintf(err, errlen, "missing \"do\"");
		return NULL;
	}
	const grime_action_type *type = grime_action_find(json_object_get_string(v));
	if (!type) {
		snprintf(err, errlen, "unknown action \"%s\" (see grime --list-actions)",
			 json_object_get_string(v));
		return NULL;
	}
	grime_action *a = calloc(1, sizeof *a);
	a->type = type;
	if (type->compile && type->compile(spec, &a->state, err, errlen) < 0) {
		free(a);
		return NULL;
	}
	return a;
}

void grime_action_run(grime_action *a, grime_runtime *rt)
{
	a->type->run(rt, a->state);
}

void grime_action_free(grime_action *a)
{
	if (!a)
		return;
	if (a->type->free)
		a->type->free(a->state);
	free(a);
}
