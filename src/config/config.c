/* JSON -> keymap tree. Errors carry the JSON path, e.g. keymap.capslock.press.then.h */
#include "grime/config.h"

#include <json-c/json.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "grime/device.h"
#include "grime/keynames.h"
#include "lower.h"

static int fail(char *err, size_t errlen, const char *path, const char *fmt, ...)
{
	int n = snprintf(err, errlen, "%s: ", path);
	if (n < 0 || (size_t)n >= errlen)
		return -1;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(err + n, errlen - n, fmt, ap);
	va_end(ap);
	return -1;
}

static int parse_keymap(json_object *obj, grime_node *node, const char *path, char *err,
			size_t errlen);

static int parse_binding(json_object *obj, grime_binding *b, grime_edge edge, const char *path,
			 char *err, size_t errlen)
{
	if (!json_object_is_type(obj, json_type_object))
		return fail(err, errlen, path, "expected an object like {\"do\": ...} or {\"then\": {...}}");

	json_object *v;
	if (json_object_object_get_ex(obj, "do", &v)) {
		char sub[512];
		b->action = grime_action_compile(obj, sub, sizeof sub);
		if (!b->action)
			return fail(err, errlen, path, "%s", sub);
	}

	/* a release can't be "released" again, so its layers default to prefix-style */
	b->exit = edge == GRIME_RELEASE ? GRIME_EXIT_ACTION : GRIME_EXIT_RELEASE;
	if (json_object_object_get_ex(obj, "exit", &v)) {
		const char *s = json_object_get_string(v);
		if (!strcmp(s, "release") && edge != GRIME_RELEASE)
			b->exit = GRIME_EXIT_RELEASE;
		else if (!strcmp(s, "action"))
			b->exit = GRIME_EXIT_ACTION;
		else if (!strcmp(s, "toggle") && edge == GRIME_PRESS)
			b->exit = GRIME_EXIT_TOGGLE;
		else if (!strcmp(s, "toggle"))
			return fail(err, errlen, path, "\"exit\": \"toggle\" only works on a press");
		else
			return fail(err, errlen, path, "\"exit\" must be \"release\", \"action\" or \"toggle\"");
	}
	if (json_object_object_get_ex(obj, "fallthrough", &v))
		b->fallthrough = json_object_get_boolean(v);
	if (json_object_object_get_ex(obj, "alone", &v)) {
		if (edge != GRIME_RELEASE)
			return fail(err, errlen, path, "\"alone\" only makes sense on a release");
		b->alone = json_object_get_boolean(v);
	}
	if (json_object_object_get_ex(obj, "then", &v)) {
		char sub[512];
		snprintf(sub, sizeof sub, "%s.then", path);
		b->then = grime_node_new();
		if (parse_keymap(v, b->then, sub, err, errlen) < 0)
			return -1;
	}
	if (b->exit == GRIME_EXIT_TOGGLE && !b->then)
		return fail(err, errlen, path, "\"exit\": \"toggle\" needs a \"then\" keymap to toggle");
	if (!b->action && !b->then)
		return fail(err, errlen, path, "needs \"do\" and/or \"then\"");
	return 0;
}

static int parse_keymap(json_object *obj, grime_node *node, const char *path, char *err,
			size_t errlen)
{
	if (!json_object_is_type(obj, json_type_object))
		return fail(err, errlen, path, "expected an object of key names");

	json_object_object_foreach(obj, name, val)
	{
		char sub[512];
		snprintf(sub, sizeof sub, "%s.%s", path, name);
		int code = grime_key_from_name(name);
		if (code < 0)
			return fail(err, errlen, sub, "unknown key \"%s\" (see grime --list-keys)", name);

		if (!json_object_is_type(val, json_type_object))
			return fail(err, errlen, sub, "expected a binding object");

		json_object_object_foreach(val, edge_name, spec)
		{
			char esub[512];
			snprintf(esub, sizeof esub, "%s.%s", sub, edge_name);
			grime_edge edge;
			if (!strcmp(edge_name, "press"))
				edge = GRIME_PRESS;
			else if (!strcmp(edge_name, "release"))
				edge = GRIME_RELEASE;
			else
				return fail(err, errlen, esub, "expected \"press\" or \"release\"");
			grime_binding *b = grime_node_bind(node, code, edge);
			if (parse_binding(spec, b, edge, esub, err, errlen) < 0)
				return -1;
		}
	}
	return 0;
}

/* One entry of "devices", as lower.c leaves it: an object with every default
 * filled in. The friendly spellings ("auto", a bare path) are gone by here. */
static int dup_str(json_object *o, const char *field, char **out, char *err, size_t errlen)
{
	json_object *v;
	if (!json_object_object_get_ex(o, field, &v))
		return 0;
	*out = strdup(json_object_get_string(v));
	if (!*out)
		return fail(err, errlen, "devices", "out of memory");
	return 0;
}

static int dup_id(json_object *o, const char *field, int *out)
{
	json_object *v;
	if (!json_object_object_get_ex(o, field, &v))
		return 0;
	*out = (int)strtol(json_object_get_string(v), NULL, 16);
	return 0;
}

static int parse_device(json_object *o, grime_device_match *m, const char *path, char *err,
			size_t errlen)
{
	*m = (grime_device_match){.vendor = -1, .product = -1, .bus = -1, .grab = true};
	if (!json_object_is_type(o, json_type_object))
		return fail(err, errlen, path, "expected a matcher object");
	if (dup_str(o, "path", &m->path, err, errlen) < 0 ||
	    dup_str(o, "name", &m->name, err, errlen) < 0 ||
	    dup_str(o, "phys", &m->phys, err, errlen) < 0 ||
	    dup_str(o, "uniq", &m->uniq, err, errlen) < 0)
		return -1;
	dup_id(o, "vendor", &m->vendor);
	dup_id(o, "product", &m->product);
	dup_id(o, "bus", &m->bus);

	json_object *v;
	if (json_object_object_get_ex(o, "kind", &v)) {
		int k = grime_device_kind_from_name(json_object_get_string(v));
		if (k < 0)
			return fail(err, errlen, path, "unknown kind");
		m->kind = (grime_device_kind)k;
	}
	if (json_object_object_get_ex(o, "grab", &v))
		m->grab = json_object_get_boolean(v);
	if (json_object_object_get_ex(o, "unmatched", &v))
		m->unmatched = strcmp(json_object_get_string(v), "pass") ? GRIME_UNMATCHED_DROP
									: GRIME_UNMATCHED_PASS;
	return 0;
}

static int parse_root(json_object *root, grime_config *out, char *err, size_t errlen)
{
	memset(out, 0, sizeof *out);
	if (!json_object_is_type(root, json_type_object))
		return fail(err, errlen, "config", "expected a JSON object");

	json_object *v;
	if (json_object_object_get_ex(root, "devices", &v)) {
		size_t n = json_object_array_length(v);
		out->devices = calloc(n, sizeof *out->devices);
		if (!out->devices)
			return fail(err, errlen, "devices", "out of memory");
		for (size_t i = 0; i < n; i++) {
			char path[64];
			snprintf(path, sizeof path, "devices[%zu]", i);
			if (parse_device(json_object_array_get_idx(v, i),
					 &out->devices[out->ndevices++], path, err, errlen) < 0)
				return -1;
		}
	} else {
		/* no "devices": every keyboard-looking thing, same as ["auto"] */
		out->devices = calloc(1, sizeof *out->devices);
		if (!out->devices)
			return fail(err, errlen, "devices", "out of memory");
		out->devices[out->ndevices++] = (grime_device_match){
			.vendor = -1, .product = -1, .bus = -1, .grab = true,
			.kind = GRIME_KIND_KEYBOARD,
		};
	}

	if (!json_object_object_get_ex(root, "keymap", &v))
		return fail(err, errlen, "config", "missing \"keymap\"");
	out->keymap = grime_node_new();
	return parse_keymap(v, out->keymap, "keymap", err, errlen);
}

static int finish(json_object *root, const char *path, grime_config *out, char *err,
		  size_t errlen)
{
	json_object *canon = grime_config_lower(root, path, err, errlen);
	json_object_put(root);
	if (!canon) {
		memset(out, 0, sizeof *out);
		return -1;
	}
	int rc = parse_root(canon, out, err, errlen);
	json_object_put(canon);
	if (rc < 0)
		grime_config_free(out);
	return rc;
}

char *grime_config_expand(const char *path, char *err, size_t errlen)
{
	json_object *root = json_object_from_file(path);
	if (!root) {
		fail(err, errlen, path, "%s", json_util_get_last_err());
		return NULL;
	}
	json_object *canon = grime_config_lower(root, path, err, errlen);
	json_object_put(root);
	if (!canon)
		return NULL;
	const char *s = json_object_to_json_string_ext(canon, JSON_C_TO_STRING_PRETTY);
	char *copy = s ? strdup(s) : NULL;
	json_object_put(canon);
	return copy;
}

int grime_config_parse(const char *json, grime_config *out, char *err, size_t errlen)
{
	json_tokener *tok = json_tokener_new();
	json_object *root = json_tokener_parse_ex(tok, json, -1);
	enum json_tokener_error jerr = json_tokener_get_error(tok);
	json_tokener_free(tok);
	if (!root || jerr != json_tokener_success) {
		memset(out, 0, sizeof *out);
		return fail(err, errlen, "config", "invalid JSON: %s", json_tokener_error_desc(jerr));
	}
	return finish(root, NULL, out, err, errlen);
}

int grime_config_load(const char *path, grime_config *out, char *err, size_t errlen)
{
	json_object *root = json_object_from_file(path);
	if (!root) {
		memset(out, 0, sizeof *out);
		return fail(err, errlen, path, "%s", json_util_get_last_err());
	}
	return finish(root, path, out, err, errlen);
}

void grime_config_free(grime_config *cfg)
{
	grime_device_match_free(cfg->devices, cfg->ndevices);
	grime_node_free(cfg->keymap);
	memset(cfg, 0, sizeof *cfg);
}
