/* seq: run several actions in order.
 *   {"do": "seq", "actions": [{"do": "tap", "key": "a"}, {"do": "exec", "cmd": "..."}]} */
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>

#include "grime/action.h"

struct seq_state {
	size_t n;
	grime_action *actions[];
};

static void seq_free(void *state)
{
	struct seq_state *s = state;
	for (size_t i = 0; i < s->n; i++)
		grime_action_free(s->actions[i]);
	free(s);
}

static int compile(json_object *spec, void **out, char *err, size_t errlen)
{
	json_object *v;
	if (!json_object_object_get_ex(spec, "actions", &v) || !json_object_is_type(v, json_type_array)) {
		snprintf(err, errlen, "missing \"actions\" array");
		return -1;
	}
	size_t n = json_object_array_length(v);
	struct seq_state *s = calloc(1, sizeof *s + n * sizeof(grime_action *));
	for (size_t i = 0; i < n; i++) {
		char sub[400];
		grime_action *a = grime_action_compile(json_object_array_get_idx(v, i), sub, sizeof sub);
		if (!a) {
			snprintf(err, errlen, "actions[%zu]: %s", i, sub);
			seq_free(s);
			return -1;
		}
		s->actions[s->n++] = a;
	}
	*out = s;
	return 0;
}

static void run(grime_runtime *rt, void *state)
{
	struct seq_state *s = state;
	for (size_t i = 0; i < s->n; i++)
		grime_action_run(s->actions[i], rt);
}

static const grime_action_type seq_action = {"seq", compile, run, seq_free};
GRIME_REGISTER_ACTION(seq_action)
