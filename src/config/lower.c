/* The config front-end. See lower.h for the shape it produces.
 *
 * Nothing here knows what an action does or what a key code is worth; it only
 * rewrites JSON into JSON, so `grime --expand` can show you exactly what your
 * config means and the tree parser stays small. */
#include "lower.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "grime/keynames.h"

#define MAX_LAYER_DEPTH 16
#define MAX_INCLUDES 64
#define MAX_PATH 1024
#define MAX_MODS 4
#define MAX_STEPS 8

struct ctx {
	json_object *layers;               /* every layer, this file's and its includes' */
	const char *open[MAX_LAYER_DEPTH]; /* layer names being expanded, for cycles */
	int nopen;
	char included[MAX_INCLUDES][MAX_PATH]; /* files already pulled in, for cycles */
	int nincluded;
	char *err;
	size_t errlen;
};

static int fail(struct ctx *c, const char *path, const char *fmt, ...)
{
	int n = snprintf(c->err, c->errlen, "%s: ", path);
	if (n < 0 || (size_t)n >= c->errlen)
		return -1;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(c->err + n, c->errlen - n, fmt, ap);
	va_end(ap);
	return -1;
}

/* json-c hands back a NULL pointer for JSON null, and get_string() on a
 * non-string is a trap; every read of a user string goes through this. */
static const char *str_of(json_object *v)
{
	if (!v || !json_object_is_type(v, json_type_string))
		return NULL;
	return json_object_get_string(v);
}

/* The canonical spelling, so "caps" and "capslock" can't both bind key 58. */
static const char *canon(const char *name)
{
	int code = grime_key_from_name(name);
	return code < 0 ? NULL : grime_key_name(code);
}

/* Side-agnostic modifier aliases bind both sides: "ctrl+f" means either ctrl. */
static int mod_variants(const char *name, const char *out[2])
{
	static const struct {
		const char *alias, *left, *right;
	} pairs[] = {
		{"shift", "leftshift", "rightshift"}, {"ctrl", "leftctrl", "rightctrl"},
		{"control", "leftctrl", "rightctrl"}, {"alt", "leftalt", "rightalt"},
		{"super", "leftmeta", "rightmeta"},   {"meta", "leftmeta", "rightmeta"},
		{"win", "leftmeta", "rightmeta"},
	};
	for (size_t i = 0; i < sizeof pairs / sizeof *pairs; i++)
		if (!strcmp(pairs[i].alias, name)) {
			out[0] = pairs[i].left;
			out[1] = pairs[i].right;
			return 2;
		}
	out[0] = canon(name);
	return out[0] ? 1 : 0;
}

/* ---- little JSON builders ---- */

static json_object *key_action(const char *what, const char *key)
{
	json_object *o = json_object_new_object();
	json_object_object_add(o, "do", json_object_new_string(what));
	json_object_object_add(o, "key", json_object_new_string(key));
	return o;
}

static json_object *obj_get(json_object *o, const char *k)
{
	json_object *v;
	return json_object_object_get_ex(o, k, &v) ? v : NULL;
}

/* ---- chords: "ctrl+shift+z" ---- */

struct chord {
	const char *mods[MAX_MODS];
	int nmods;
	const char *key;
};

/* Splits `buf` (modified in place). Returns 0, or -1 with *bad set. */
static int parse_chord(char *buf, struct chord *ch, const char **bad)
{
	ch->nmods = 0;
	char *p = buf;
	for (;;) {
		char *plus = strchr(p, '+');
		if (!plus)
			break;
		*plus = 0;
		if (ch->nmods == MAX_MODS) {
			*bad = "too many modifiers";
			return -1;
		}
		if (!*p || grime_key_from_name(p) < 0) {
			*bad = p;
			return -1;
		}
		ch->mods[ch->nmods++] = p;
		p = plus + 1;
	}
	ch->key = p;
	return 0;
}

/* A chord as one moment: {"do":"tap","key":"z","mods":["leftctrl"]} */
static json_object *chord_tap(struct chord *ch)
{
	json_object *o = key_action("tap", canon(ch->key));
	if (ch->nmods) {
		json_object *mods = json_object_new_array();
		for (int i = 0; i < ch->nmods; i++)
			json_object_array_add(mods, json_object_new_string(canon(ch->mods[i])));
		json_object_object_add(o, "mods", mods);
	}
	return o;
}

/* A chord as something you hold: mods go down first and come up last, so the
 * OS autorepeats it exactly like the real chord would. */
static json_object *chord_hold(struct chord *ch, bool down)
{
	if (!ch->nmods)
		return key_action(down ? "press" : "release", canon(ch->key));
	json_object *list = json_object_new_array();
	for (int i = 0; i < ch->nmods; i++)
		json_object_array_add(list, key_action(down ? "press" : "release",
						       canon(ch->mods[down ? i : ch->nmods - 1 - i])));
	if (down)
		json_object_array_add(list, key_action("press", canon(ch->key)));
	else
		json_object_array_add(list, key_action("release", canon(ch->key)));
	if (!down) { /* release the key before the mods */
		json_object *fixed = json_object_new_array();
		size_t n = json_object_array_length(list);
		json_object_array_add(fixed, json_object_get(json_object_array_get_idx(list, n - 1)));
		for (size_t i = 0; i + 1 < n; i++)
			json_object_array_add(fixed, json_object_get(json_object_array_get_idx(list, i)));
		json_object_put(list);
		list = fixed;
	}
	json_object *o = json_object_new_object();
	json_object_object_add(o, "do", json_object_new_string("seq"));
	json_object_object_add(o, "actions", list);
	return o;
}

/* ---- the slot vocabulary ---- */

static const char *const SLOTS[] = {"hold",    "prefix",  "toggle", "press",
				    "release", "tap",     "pass",   "do"};

static bool is_slot(const char *name)
{
	for (size_t i = 0; i < sizeof SLOTS / sizeof *SLOTS; i++)
		if (!strcmp(SLOTS[i], name))
			return true;
	return false;
}

/* A fresh object holding everything in `o` except the slot names. */
static json_object *action_copy(json_object *o, bool drop_slots)
{
	json_object *out = json_object_new_object();
	json_object_object_foreach(o, k, v)
	{
		if (drop_slots && is_slot(k) && strcmp(k, "do"))
			continue;
		json_object_object_add(out, k, json_object_get(v));
	}
	return out;
}

/* The old spelling said how the walker works; the new one says what you mean. */
static int check_retired(struct ctx *c, json_object *o, const char *path)
{
	static const struct {
		const char *gone, *use;
	} retired[] = {
		{"then", "\"hold\" (while held), \"prefix\" (emacs style) or \"toggle\" (sticky)"},
		{"exit", "nothing -- \"hold\"/\"prefix\"/\"toggle\" already say when the layer ends"},
		{"alone", "the \"tap\" slot"},
		{"fallthrough", "\"pass\""},
	};
	for (size_t i = 0; i < sizeof retired / sizeof *retired; i++)
		if (obj_get(o, retired[i].gone))
			return fail(c, path, "\"%s\" is gone; use %s", retired[i].gone,
				    retired[i].use);
	return 0;
}

static json_object *lower_keymap(struct ctx *c, json_object *in, const char *path);

/* An action slot: "esc", "ctrl+z", or {"do": …}. Always returns a fresh object. */
static json_object *lower_action(struct ctx *c, json_object *v, const char *path)
{
	const char *s = str_of(v);
	if (s) {
		char buf[128];
		struct chord ch;
		const char *bad = NULL;
		if (strlen(s) >= sizeof buf) {
			fail(c, path, "key name too long");
			return NULL;
		}
		strcpy(buf, s);
		if (parse_chord(buf, &ch, &bad) < 0 || grime_key_from_name(ch.key) < 0) {
			fail(c, path, "unknown key \"%s\" (see grime --list-keys)",
			     bad ? bad : ch.key);
			return NULL;
		}
		return chord_tap(&ch);
	}
	if (!json_object_is_type(v, json_type_object)) {
		fail(c, path, "expected a key like \"esc\", a chord like \"ctrl+z\", or {\"do\": …}");
		return NULL;
	}
	if (check_retired(c, v, path) < 0)
		return NULL;
	if (!obj_get(v, "do")) {
		fail(c, path, "needs a \"do\" (see grime --list-actions)");
		return NULL;
	}
	return action_copy(v, false);
}

/* A layer slot: an inline keymap, or the name of one from "layers". */
static json_object *resolve_layer(struct ctx *c, json_object *v, const char *path)
{
	const char *s = str_of(v);
	if (!s)
		return lower_keymap(c, v, path);
	json_object *l = c->layers ? obj_get(c->layers, s) : NULL;
	if (!l) {
		fail(c, path, "no layer named \"%s\" in \"layers\"", s);
		return NULL;
	}
	for (int i = 0; i < c->nopen; i++)
		if (!strcmp(c->open[i], s)) {
			fail(c, path, "layer \"%s\" contains itself", s);
			return NULL;
		}
	if (c->nopen == MAX_LAYER_DEPTH) {
		fail(c, path, "layers nested deeper than %d", MAX_LAYER_DEPTH);
		return NULL;
	}
	char sub[512];
	snprintf(sub, sizeof sub, "layers.%s", s);
	c->open[c->nopen++] = s;
	json_object *r = lower_keymap(c, l, sub);
	c->nopen--;
	return r;
}

/* Fills `entry` with the canonical {"press": …, "release": …}. */
static int lower_entry(struct ctx *c, json_object *val, const char *path, json_object *entry)
{
	if (!val)
		return 0; /* explicit null: this key does nothing */

	const char *s = str_of(val);
	if (s) { /* "a": "b" / "u": "ctrl+z" -- behave like that key, hold and all */
		char buf[128];
		struct chord ch;
		const char *bad = NULL;
		if (strlen(s) >= sizeof buf)
			return fail(c, path, "key name too long");
		strcpy(buf, s);
		if (parse_chord(buf, &ch, &bad) < 0 || grime_key_from_name(ch.key) < 0)
			return fail(c, path, "unknown key \"%s\" (see grime --list-keys)",
				    bad ? bad : ch.key);
		json_object_object_add(entry, "press", chord_hold(&ch, true));
		json_object_object_add(entry, "release", chord_hold(&ch, false));
		return 0;
	}
	if (!json_object_is_type(val, json_type_object))
		return fail(c, path, "expected a key name, a chord, {…}, or null");

	if (check_retired(c, val, path) < 0)
		return -1;

	static const struct {
		const char *slot, *exit;
	} LAYERS[] = {{"hold", "release"}, {"prefix", "action"}, {"toggle", "toggle"}};
	json_object *layer_v = NULL;
	const char *exit = NULL, *layer_slot = NULL;
	for (size_t i = 0; i < sizeof LAYERS / sizeof *LAYERS; i++) {
		json_object *v = obj_get(val, LAYERS[i].slot);
		if (!v)
			continue;
		if (layer_v)
			return fail(c, path, "\"%s\" and \"%s\" can't both be on one key",
				    layer_slot, LAYERS[i].slot);
		layer_v = v;
		exit = LAYERS[i].exit;
		layer_slot = LAYERS[i].slot;
	}

	json_object *do_v = obj_get(val, "do"), *press_v = obj_get(val, "press");
	if (do_v && press_v)
		return fail(c, path, "\"do\" already means \"on press\"; drop the \"press\" slot");
	if (!do_v) { /* with "do", the leftovers are that action's parameters */
		json_object_object_foreach(val, k, unused)
		{
			(void)unused;
			if (!is_slot(k))
				return fail(c, path,
					    "unknown \"%s\" (expected hold, prefix, toggle, "
					    "press, release, tap, pass or do)",
					    k);
		}
	}

	char sub[512];
	json_object *press = NULL;
	if (do_v) {
		press = action_copy(val, true);
	} else if (press_v) {
		snprintf(sub, sizeof sub, "%s.press", path);
		if (!(press = lower_action(c, press_v, sub)))
			return -1;
	}
	if (layer_v) {
		snprintf(sub, sizeof sub, "%s.%s", path, layer_slot);
		json_object *km = resolve_layer(c, layer_v, sub);
		if (!km) {
			json_object_put(press);
			return -1;
		}
		if (!press)
			press = json_object_new_object();
		json_object_object_add(press, "then", km);
		json_object_object_add(press, "exit", json_object_new_string(exit));
	}
	json_object *pass_v = obj_get(val, "pass");
	if (pass_v && json_object_get_boolean(pass_v)) {
		if (!press)
			return fail(c, path,
				    "\"pass\" needs a layer (\"hold\", \"prefix\" or \"toggle\")");
		json_object_object_add(press, "fallthrough", json_object_new_boolean(1));
	}

	json_object *tap_v = obj_get(val, "tap"), *rel_v = obj_get(val, "release");
	json_object *release = NULL;
	if (tap_v && rel_v) {
		json_object_put(press);
		return fail(c, path, "\"tap\" and \"release\" can't both be on one key");
	}
	if (tap_v || rel_v) {
		snprintf(sub, sizeof sub, "%s.%s", path, tap_v ? "tap" : "release");
		if (!(release = lower_action(c, tap_v ? tap_v : rel_v, sub))) {
			json_object_put(press);
			return -1;
		}
		if (tap_v)
			json_object_object_add(release, "alone", json_object_new_boolean(1));
	}

	if (!press && !release)
		return fail(c, path, "nothing to do: give it a key, an action, or a layer");
	if (press)
		json_object_object_add(entry, "press", press);
	if (release)
		json_object_object_add(entry, "release", release);
	return 0;
}

/* ---- placing an entry: "f", "ctrl+f", "rightalt x f" ---- */

/* The frame a path step walks through. Steps of the same kind merge, so
 * "ctrl+f" and "ctrl+t" share one ctrl frame. */
static json_object *ensure_frame(struct ctx *c, json_object *dest, const char *name, bool chord,
				 const char *path)
{
	const char *k = canon(name);
	if (!k) {
		fail(c, path, "unknown key \"%s\" (see grime --list-keys)", name);
		return NULL;
	}
	json_object *e = obj_get(dest, k);
	if (e) {
		json_object *press = obj_get(e, "press");
		json_object *then = press ? obj_get(press, "then") : NULL;
		if (!then || (obj_get(press, "do") != NULL) != chord) {
			fail(c, path, "\"%s\" is already bound to something else here", k);
			return NULL;
		}
		return then;
	}
	e = json_object_new_object();
	json_object *press = json_object_new_object(), *then = json_object_new_object();
	if (chord) {
		/* the modifier still reaches the app, and keys we don't override
		 * fall back out of the frame, so ctrl+s stays ctrl+s */
		json_object_object_add(press, "do", json_object_new_string("press"));
		json_object_object_add(press, "key", json_object_new_string(k));
		json_object_object_add(press, "fallthrough", json_object_new_boolean(1));
		json_object_object_add(e, "release", key_action("release", k));
	} else {
		json_object_object_add(press, "exit", json_object_new_string("action"));
	}
	json_object_object_add(press, "then", then);
	json_object_object_add(e, "press", press);
	json_object_object_add(dest, k, e);
	return then;
}

static int place_leaf(struct ctx *c, json_object *dest, const char *name, json_object *entry,
		      const char *path)
{
	const char *k = canon(name);
	if (!k)
		return fail(c, path, "unknown key \"%s\" (see grime --list-keys)", name);
	if (obj_get(dest, k))
		return fail(c, path, "\"%s\" is bound twice here", k);
	json_object_object_add(dest, k, json_object_get(entry));
	return 0;
}

/* Modifier aliases bind both sides, so "ctrl+f" lands under left and right ctrl. */
static int place_chord(struct ctx *c, json_object *dest, struct chord *ch, int i,
		       json_object *entry, const char *path)
{
	if (i == ch->nmods)
		return place_leaf(c, dest, ch->key, entry, path);
	const char *v[2];
	int n = mod_variants(ch->mods[i], v);
	if (!n)
		return fail(c, path, "unknown key \"%s\" (see grime --list-keys)", ch->mods[i]);
	for (int k = 0; k < n; k++) {
		json_object *inner = ensure_frame(c, dest, v[k], true, path);
		if (!inner || place_chord(c, inner, ch, i + 1, entry, path) < 0)
			return -1;
	}
	return 0;
}

static int place(struct ctx *c, json_object *out, const char *name, json_object *entry,
		 const char *path)
{
	char buf[256];
	if (strlen(name) >= sizeof buf)
		return fail(c, path, "key path too long");
	strcpy(buf, name);

	char *steps[MAX_STEPS];
	int n = 0;
	for (char *t = strtok(buf, " "); t; t = strtok(NULL, " ")) {
		if (n == MAX_STEPS)
			return fail(c, path, "key path longer than %d steps", MAX_STEPS);
		steps[n++] = t;
	}
	if (!n)
		return fail(c, path, "empty key name");

	json_object *dest = out;
	for (int i = 0; i < n - 1; i++) {
		if (strchr(steps[i], '+'))
			return fail(c, path,
				    "a chord can only be the last step of a key path, but "
				    "\"%s\" is step %d of %d",
				    steps[i], i + 1, n);
		if (!(dest = ensure_frame(c, dest, steps[i], false, path)))
			return -1;
	}
	if (!strchr(steps[n - 1], '+'))
		return place_leaf(c, dest, steps[n - 1], entry, path);

	struct chord ch;
	const char *bad = NULL;
	if (parse_chord(steps[n - 1], &ch, &bad) < 0)
		return fail(c, path, "unknown key \"%s\" (see grime --list-keys)", bad);
	return place_chord(c, dest, &ch, 0, entry, path);
}

static json_object *lower_keymap(struct ctx *c, json_object *in, const char *path)
{
	if (!json_object_is_type(in, json_type_object)) {
		fail(c, path, "expected an object of key names");
		return NULL;
	}
	json_object *out = json_object_new_object();
	json_object_object_foreach(in, name, val)
	{
		char sub[512];
		snprintf(sub, sizeof sub, "%s.%s", path, name);
		json_object *entry = json_object_new_object();
		int rc = lower_entry(c, val, sub, entry);
		if (rc == 0)
			rc = place(c, out, name, entry, sub);
		json_object_put(entry);
		if (rc < 0) {
			json_object_put(out);
			return NULL;
		}
	}
	return out;
}

/* ---- the root ---- */

/* "base": "<layer>" fills in every key the keymap didn't mention itself. The
 * layer is an ordinary one from "layers", so layouts are config, not code. */
static int seed_from(struct ctx *c, json_object *km, json_object *layer, const char *name)
{
	char path[256];
	snprintf(path, sizeof path, "layers.%s", name);
	if (!json_object_is_type(layer, json_type_object))
		return fail(c, path, "expected an object of key names");
	json_object_object_foreach(layer, key, val)
	{
		if (strpbrk(key, " +"))
			return fail(c, path,
				    "a base layer holds plain key names, but \"%s\" is a path "
				    "or a chord",
				    key);
		const char *k = canon(key);
		if (!k)
			return fail(c, path, "unknown key \"%s\" (see grime --list-keys)", key);
		if (obj_get(km, k))
			continue; /* the keymap already said what this key does */
		json_object *entry = json_object_new_object();
		int rc = lower_entry(c, val, path, entry);
		if (rc == 0)
			rc = place_leaf(c, km, k, entry, path);
		json_object_put(entry);
		if (rc < 0)
			return -1;
	}
	return 0;
}

/* ---- include: one config out of several files ---- */

/* `rel` against the directory of the file that named it. */
static int resolve_path(const char *dir, const char *rel, char *out, size_t outlen)
{
	const char *home = getenv("HOME");
	int n;
	if (rel[0] == '/')
		n = snprintf(out, outlen, "%s", rel);
	else if (rel[0] == '~' && rel[1] == '/' && home)
		n = snprintf(out, outlen, "%s/%s", home, rel + 2);
	else
		n = snprintf(out, outlen, "%s/%s", dir, rel);
	return (n < 0 || (size_t)n >= outlen) ? -1 : 0;
}

static void dir_of(const char *path, char *out, size_t outlen)
{
	const char *slash = path ? strrchr(path, '/') : NULL;
	if (!slash) {
		snprintf(out, outlen, ".");
		return;
	}
	size_t n = (size_t)(slash - path);
	if (n >= outlen)
		n = outlen - 1;
	memcpy(out, path, n);
	out[n] = 0;
}

/* Copies `src`'s entries into `dst`, keeping whoever got there first. */
static void merge_into(json_object *dst, json_object *src)
{
	json_object_object_foreach(src, k, v)
		if (!obj_get(dst, k))
			json_object_object_add(dst, k, json_object_get(v));
}

/* Walks a config file and everything it includes, collecting layers and keymap
 * entries. The file you are reading wins over the files it pulls in. */
static int gather(struct ctx *c, json_object *file, const char *dir, json_object *layers,
		  json_object *keymap, const char *path, bool is_root)
{
	json_object *v;
	if (!is_root && (obj_get(file, "devices") || obj_get(file, "base")))
		return fail(c, path, "only the main config can set \"devices\" or \"base\"");
	json_object_object_foreach(file, k, unused)
	{
		(void)unused;
		if (strcmp(k, "devices") && strcmp(k, "keymap") && strcmp(k, "base") &&
		    strcmp(k, "layers") && strcmp(k, "include"))
			return fail(c, path,
				    "unknown \"%s\" (expected devices, base, include, layers "
				    "or keymap)",
				    k);
	}
	if ((v = obj_get(file, "layers"))) {
		if (!json_object_is_type(v, json_type_object))
			return fail(c, path, "\"layers\": expected an object of named keymaps");
		merge_into(layers, v);
	}
	if ((v = obj_get(file, "keymap"))) {
		if (!json_object_is_type(v, json_type_object))
			return fail(c, path, "\"keymap\": expected an object of key names");
		merge_into(keymap, v);
	}
	if (!(v = obj_get(file, "include")))
		return 0;
	if (!json_object_is_type(v, json_type_array))
		return fail(c, path, "\"include\": expected an array of file paths");

	for (size_t i = 0; i < json_object_array_length(v); i++) {
		const char *rel = str_of(json_object_array_get_idx(v, i));
		if (!rel)
			return fail(c, path, "\"include\": expected an array of file paths");
		char full[MAX_PATH];
		if (resolve_path(dir, rel, full, sizeof full) < 0)
			return fail(c, path, "include \"%s\": path too long", rel);
		bool seen = false;
		for (int j = 0; j < c->nincluded; j++)
			seen = seen || !strcmp(c->included[j], full);
		if (seen)
			continue; /* already pulled in, by us or by someone else */
		if (c->nincluded == MAX_INCLUDES)
			return fail(c, path, "more than %d includes", MAX_INCLUDES);
		snprintf(c->included[c->nincluded++], MAX_PATH, "%s", full);

		json_object *sub = json_object_from_file(full);
		if (!sub)
			return fail(c, path, "include \"%s\": %s", rel, json_util_get_last_err());
		if (!json_object_is_type(sub, json_type_object)) {
			json_object_put(sub);
			return fail(c, full, "expected a JSON object");
		}
		char subdir[MAX_PATH];
		dir_of(full, subdir, sizeof subdir);
		int rc = gather(c, sub, subdir, layers, keymap, full, false);
		json_object_put(sub); /* merged entries hold their own references */
		if (rc < 0)
			return -1;
	}
	return 0;
}

json_object *grime_config_lower(json_object *root, const char *path, char *err, size_t errlen)
{
	struct ctx c = {.err = err, .errlen = errlen};
	if (!json_object_is_type(root, json_type_object)) {
		fail(&c, "config", "expected a JSON object");
		return NULL;
	}
	char dir[MAX_PATH];
	dir_of(path, dir, sizeof dir);

	json_object *layers = json_object_new_object(), *keymap = json_object_new_object();
	json_object *out = NULL, *out_km = NULL;
	if (gather(&c, root, dir, layers, keymap, path ? path : "config", true) < 0)
		goto done;
	if (!obj_get(root, "keymap")) {
		fail(&c, "config", "missing \"keymap\"");
		goto done;
	}
	c.layers = layers;
	if (!(out_km = lower_keymap(&c, keymap, "keymap")))
		goto done;

	json_object *base = obj_get(root, "base");
	if (base) {
		const char *b = str_of(base);
		json_object *layer = b ? obj_get(layers, b) : NULL;
		if (!b) {
			fail(&c, "base", "expected the name of a layer, like \"qwerty\"");
			goto done;
		}
		if (!layer) {
			fail(&c, "base",
			     "no layer named \"%s\" -- include the layout that defines it, "
			     "e.g. \"include\": [\"layouts/%s.json\"]",
			     b, b);
			goto done;
		}
		if (seed_from(&c, out_km, layer, b) < 0)
			goto done;
	}

	out = json_object_new_object();
	json_object *dev = obj_get(root, "devices");
	if (dev)
		json_object_object_add(out, "devices", json_object_get(dev));
	json_object_object_add(out, "keymap", out_km);
	out_km = NULL;
done:
	json_object_put(out_km);
	json_object_put(layers);
	json_object_put(keymap);
	return out;
}
