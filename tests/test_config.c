/* Front-end tests: the friendly syntax, checked by what the engine then does. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "grime/config.h"
#include "grime/engine.h"
#include "grime/keynames.h"
#include "grime/log.h"
#include "tinytest.h"

static char rec[4096];

static void rec_emit(grime_output *out, uint16_t code, int value)
{
	strcat(rec, rec[0] ? " " : "");
	strcat(rec, value ? "+" : "-");
	strcat(rec, grime_key_name(code));
}

static grime_output rec_out = {.emit = rec_emit};
static grime_loop *loop;
static grime_runtime rt;
static void rt_quit(grime_runtime *r) {}
static void rt_reload(grime_runtime *r) {}

static grime_engine *load(const char *json)
{
	grime_config cfg;
	char err[512];
	if (grime_config_parse(json, &cfg, err, sizeof err) < 0) {
		fprintf(stderr, "  config error: %s\n", err);
		return NULL;
	}
	rt = (grime_runtime){.loop = loop, .out = &rec_out, .quit = rt_quit, .reload = rt_reload};
	grime_engine *e = grime_engine_new(&rt, cfg.keymap);
	cfg.keymap = NULL;
	grime_config_free(&cfg);
	rec[0] = 0;
	return e;
}

static void feed(grime_engine *e, const char *events)
{
	char buf[512];
	strcpy(buf, events);
	for (char *tok = strtok(buf, " "); tok; tok = strtok(NULL, " ")) {
		grime_key_event ev = {
			.code = grime_key_from_name(tok + 1),
			.edge = tok[0] == '+' ? GRIME_PRESS : GRIME_RELEASE,
		};
		grime_engine_feed(e, &ev);
	}
}

/* Load `json`, feed `events`, and compare what came out. */
static bool emits(const char *json, const char *events, const char *want)
{
	grime_engine *e = load(json);
	if (!e)
		return false;
	feed(e, events);
	bool ok = !strcmp(rec, want);
	if (!ok)
		fprintf(stderr, "  got [%s], wanted [%s]\n", rec, want);
	grime_engine_free(e);
	return ok;
}

static bool fails(const char *json, const char *needle)
{
	grime_config cfg;
	char err[512] = "";
	if (grime_config_parse(json, &cfg, err, sizeof err) == 0) {
		grime_config_free(&cfg);
		fprintf(stderr, "  expected an error mentioning \"%s\"\n", needle);
		return false;
	}
	if (!strstr(err, needle))
		fprintf(stderr, "  error was [%s], wanted \"%s\"\n", err, needle);
	return strstr(err, needle) != NULL;
}

/* Layouts are config, not code, so the tests carry their own little one. */
#define QWERTY                                                                                 \
	"\"qwerty\": {"                                                                          \
	"  \"a\":\"a\", \"b\":\"b\", \"c\":\"c\", \"f\":\"f\", \"h\":\"h\", \"j\":\"j\", \"k\":\"k\"," \
	"  \"l\":\"l\", \"q\":\"q\", \"s\":\"s\", \"t\":\"t\", \"x\":\"x\", \"z\":\"z\", \"5\":\"5\"," \
	"  \"esc\":\"esc\", \"tab\":\"tab\", \"space\":\"space\", \"capslock\":\"capslock\","       \
	"  \"leftshift\":\"leftshift\", \"leftctrl\":\"leftctrl\", \"rightctrl\":\"rightctrl\","    \
	"  \"rightalt\":\"rightalt\", \"f1\":\"f1\", \"f12\":\"f12\", \"f13\":\"f13\","             \
	"  \"up\":\"up\", \"down\":\"down\", \"left\":\"left\", \"right\":\"right\"}"

#define BASE "\"layers\": {" QWERTY "}, \"base\": \"qwerty\","

TEST(base_fills_in_what_the_keymap_did_not_say)
{
	CHECK(emits("{" BASE "\"keymap\": {}}", "+a -a +5 -5", "+a -a +5 -5"));
	/* what the config does say wins over the base */
	CHECK(emits("{" BASE "\"keymap\": {\"a\": \"b\"}}", "+a -a", "+b -b"));
	/* and null takes a key back out */
	CHECK(emits("{" BASE "\"keymap\": {\"a\": null}}", "+a -a +b -b", "+b -b"));
	/* without a base, nothing is bound */
	CHECK(emits("{\"keymap\": {}}", "+a -a", ""));
	/* a base has to be a layer somebody defined */
	CHECK(fails("{\"base\": \"qwerty\", \"keymap\": {}}", "include the layout"));
}

TEST(tap_and_hold_are_one_key)
{
	static const char *cfg = "{" BASE "\"keymap\": {"
				 "  \"capslock\": {\"tap\": \"esc\", \"hold\": {\"h\": \"left\"}}}}";
	CHECK(emits(cfg, "+capslock -capslock", "+esc -esc"));
	CHECK(emits(cfg, "+capslock +h -h -capslock", "+left -left"));
	/* used as a modifier, so the tap does not fire */
	CHECK(emits(cfg, "+capslock +h -h -capslock +h -h", "+left -left +h -h"));
}

TEST(a_chord_binds_both_sides_of_the_modifier)
{
	static const char *cfg = "{" BASE "\"keymap\": {"
				 "  \"ctrl+f\": {\"do\": \"tap\", \"key\": \"f1\"}}}";
	CHECK(emits(cfg, "+leftctrl +f -f -leftctrl", "+leftctrl +f1 -f1 -leftctrl"));
	CHECK(emits(cfg, "+rightctrl +f -f -rightctrl", "+rightctrl +f1 -f1 -rightctrl"));
	/* the modifier still reaches the app, and other chords are untouched */
	CHECK(emits(cfg, "+leftctrl +s -s -leftctrl", "+leftctrl +s -s -leftctrl"));
}

TEST(key_paths_with_a_shared_prefix_merge)
{
	static const char *cfg = "{" BASE "\"keymap\": {"
				 "  \"rightalt x f\": {\"do\": \"type\", \"text\": \"F\"},"
				 "  \"rightalt x t\": {\"do\": \"type\", \"text\": \"T\"},"
				 "  \"rightalt l\":   {\"do\": \"type\", \"text\": \"L\"}}}";
	CHECK(emits(cfg, "+rightalt -rightalt +x -x +f -f", "+leftshift +f -f -leftshift"));
	CHECK(emits(cfg, "+rightalt -rightalt +x -x +t -t", "+leftshift +t -t -leftshift"));
	CHECK(emits(cfg, "+rightalt -rightalt +l -l", "+leftshift +l -l -leftshift"));
	/* a key the chain doesn't define cancels it, and is swallowed doing so */
	CHECK(emits(cfg, "+rightalt -rightalt +x -x +q -q +f -f", "+f -f"));
}

TEST(a_held_key_does_not_cancel_a_prefix)
{
	/* shift is held across the whole chord; releasing it must not end the prefix */
	static const char *cfg = "{" BASE "\"keymap\": {"
				 "  \"rightalt x f\": {\"do\": \"type\", \"text\": \"F\"}}}";
	CHECK(emits(cfg, "+leftshift +rightalt -rightalt -leftshift +x -x +f -f",
		    "+leftshift -leftshift +leftshift +f -f -leftshift"));
}

TEST(a_hold_layer_outlives_a_layer_released_beneath_it)
{
	static const char *cfg =
		"{" BASE "\"keymap\": {"
		"  \"space\": {\"pass\": true, \"hold\": {\"j\": \"down\"}},"
		"  \"tab\":   {\"pass\": true, \"hold\": {\"f\": {\"do\": \"tap\", \"key\": \"f1\"}}}}}";
	/* tab is still held when space is released, so tab's layer is still live */
	CHECK(emits(cfg, "+space +tab -space +f -f -tab", "+f1 -f1"));
	CHECK(emits(cfg, "+tab +space -space +f -f -tab", "+f1 -f1"));
}

TEST(named_layers_can_be_reused)
{
	static const char *cfg = "{\"layers\": {" QWERTY ","
				 "   \"nav\": {\"j\": \"down\", \"k\": \"up\"}},"
				 " \"base\": \"qwerty\","
				 " \"keymap\": {"
				 "   \"capslock\": {\"hold\": \"nav\"},"
				 "   \"f13\":      {\"toggle\": \"nav\", \"pass\": true}}}";
	CHECK(emits(cfg, "+capslock +j -j -capslock", "+down -down"));
	CHECK(emits(cfg, "+f13 -f13 +j -j", "+down -down"));
	/* two uses of one layer are two independent toggles, not one shared frame */
	CHECK(emits(cfg, "+f13 -f13 +capslock +j -j -capslock +f13 -f13 +j -j",
		    "+down -down +j -j"));
}

TEST(chord_shorthand_holds_its_modifiers)
{
	/* "ctrl+z" as a value behaves like the real chord: mods down first, up last */
	CHECK(emits("{" BASE "\"keymap\": {\"f13\": \"ctrl+z\"}}", "+f13 -f13",
		    "+leftctrl +z -z -leftctrl"));
	/* in an action slot it is a single moment instead */
	CHECK(emits("{" BASE "\"keymap\": {\"f13\": {\"tap\": \"ctrl+z\"}}}",
		    "+f13 -f13", "+leftctrl +z -z -leftctrl"));
}

static void write_file(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");
	fputs(text, f);
	fclose(f);
}

TEST(include_pulls_in_layouts_from_other_files)
{
	const char *dir = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
	char layout[512], main_cfg[512], text[2048];
	snprintf(layout, sizeof layout, "%s/grime-test-layout.json", dir);
	snprintf(main_cfg, sizeof main_cfg, "%s/grime-test-main.json", dir);
	write_file(layout, "{\"layers\": {" QWERTY ", \"swap\": {\"a\": \"b\"}}}");
	snprintf(text, sizeof text,
		 "{\"include\": [\"%s\"], \"base\": \"qwerty\","
		 " \"keymap\": {\"f13\": {\"toggle\": \"swap\", \"pass\": true}}}",
		 layout);
	write_file(main_cfg, text);

	grime_config cfg;
	char err[512] = "";
	bool ok = grime_config_load(main_cfg, &cfg, err, sizeof err) == 0;
	if (!ok)
		fprintf(stderr, "  config error: %s\n", err);
	CHECK(ok);
	if (ok) {
		rt = (grime_runtime){.loop = loop, .out = &rec_out, .quit = rt_quit,
				     .reload = rt_reload};
		grime_engine *e = grime_engine_new(&rt, cfg.keymap);
		cfg.keymap = NULL;
		grime_config_free(&cfg);
		rec[0] = 0;
		feed(e, "+a -a +f13 -f13 +a -a");
		CHECK(!strcmp(rec, "+a -a +b -b"));
		grime_engine_free(e);
	}
	remove(layout);
	remove(main_cfg);
}

TEST(the_front_end_rejects_nonsense)
{
	CHECK(fails("{\"keymap\": {\"capslock\": \"a\", \"caps\": \"b\"}}", "bound twice"));
	CHECK(fails("{\"keymap\": {\"a\": {\"do\": \"log\", \"msg\": null}}}", "must be a string"));
	CHECK(fails("{\"keymap\": {\"a\": {\"do\": \"tap\", \"key\": null}}}", "must be a string"));
	CHECK(fails("{\"keymap\": {\"a\": {\"do\": \"tap\", \"key\": \"b\", \"mods\": \"ctrl\"}}}",
		    "must be an array"));
	CHECK(fails("{\"keymap\": {\"a\": {\"do\": \"exec\", \"argv\": [null]}}}", "must hold strings"));
	CHECK(fails("{\"devices\": [], \"keymap\": {\"a\": \"a\"}}", "grab nothing"));
	CHECK(fails("{\"devices\": [null], \"keymap\": {\"a\": \"a\"}}", "matcher object"));
	CHECK(fails("{\"keymap\": {\"a\": {\"tap\": \"b\", \"allone\": true}}}", "unknown \"allone\""));
	CHECK(fails("{\"keymap\": {\"ctrl+x ctrl+f\": {\"do\": \"quit\"}}}", "last step"));
	CHECK(fails("{\"keymap\": {\"a\": {\"hold\": \"nope\"}}}", "no layer named"));
	CHECK(fails("{\"layers\": {\"x\": {\"a\": {\"hold\": \"x\"}}},"
		    " \"keymap\": {\"b\": {\"hold\": \"x\"}}}",
		    "contains itself"));
	CHECK(fails("{\"keymap\": {\"a\": \"a\"}, \"colour\": \"red\"}", "unknown \"colour\""));
	CHECK(fails("{\"base\": 5, \"keymap\": {}}", "name of a layer"));
	CHECK(fails("{\"keymap\": {}, \"include\": \"x.json\"}", "array of file paths"));
	CHECK(fails("{\"keymap\": {}, \"include\": [\"/nope/missing.json\"]}", "missing.json"));
	/* a path step that collides with a plain binding */
	CHECK(fails("{\"keymap\": {\"a\": \"a\", \"a b\": {\"do\": \"quit\"}}}", "already bound"));
}

int main(void)
{
	grime_log_level = GRIME_LOG_ERROR;
	loop = grime_loop_new();
	RUN(base_fills_in_what_the_keymap_did_not_say);
	RUN(tap_and_hold_are_one_key);
	RUN(a_chord_binds_both_sides_of_the_modifier);
	RUN(key_paths_with_a_shared_prefix_merge);
	RUN(a_held_key_does_not_cancel_a_prefix);
	RUN(a_hold_layer_outlives_a_layer_released_beneath_it);
	RUN(named_layers_can_be_reused);
	RUN(chord_shorthand_holds_its_modifiers);
	RUN(include_pulls_in_layouts_from_other_files);
	RUN(the_front_end_rejects_nonsense);
	grime_loop_free(loop);
	return DONE();
}
