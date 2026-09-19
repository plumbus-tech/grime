/* Control grime itself.
 *   {"do": "reload"}   re-read the config file (same as SIGHUP)
 *   {"do": "quit"}     exit and give the keyboard back
 *   {"do": "log", "msg": "hi"} */
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "grime/action.h"
#include "grime/log.h"

static void run_reload(grime_runtime *rt, void *state)
{
	(void)state;
	rt->reload(rt);
}

static void run_quit(grime_runtime *rt, void *state)
{
	(void)state;
	rt->quit(rt);
}

static int compile_log(json_object *spec, void **out, char *err, size_t errlen)
{
	json_object *v;
	if (!json_object_object_get_ex(spec, "msg", &v)) {
		snprintf(err, errlen, "missing \"msg\"");
		return -1;
	}
	*out = strdup(json_object_get_string(v));
	return 0;
}

static void run_log(grime_runtime *rt, void *state)
{
	(void)rt;
	LOG_INFO("%s", (char *)state);
}

static const grime_action_type reload_action = {"reload", NULL, run_reload, NULL};
static const grime_action_type quit_action = {"quit", NULL, run_quit, NULL};
static const grime_action_type log_action = {"log", compile_log, run_log, free};
GRIME_REGISTER_ACTION(reload_action)
GRIME_REGISTER_ACTION(quit_action)
GRIME_REGISTER_ACTION(log_action)
