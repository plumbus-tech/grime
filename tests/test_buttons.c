/* Mouse buttons walk the keymap like any other key. The engine's keys[] and the
 * runtime's held[] are GRIME_KEY_COUNT (0x300) wide, which already covers the
 * whole BTN_* range, so this is a test that nothing in the walker special-cases
 * the key space -- not a test of new walker code. */
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
static grime_runtime rt;

static grime_engine *load(const char *json)
{
	grime_config cfg;
	char err[512];
	if (grime_config_parse(json, &cfg, err, sizeof err) < 0) {
		fprintf(stderr, "  config error: %s\n", err);
		return NULL;
	}
	rt = (grime_runtime){.out = &rec_out};
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

TEST(a_button_can_fire_an_action)
{
	grime_engine *e = load("{\"keymap\": {\"btn_middle\": {\"do\": \"tap\", \"key\": \"btn_left\"}}}");
	CHECK(e != NULL);
	feed(e, "+btn_middle -btn_middle");
	CHECK(!strcmp(rec, "+btn_left -btn_left"));
	grime_engine_free(e);
}

TEST(a_button_can_be_another_button)
{
	grime_engine *e = load("{\"keymap\": {\"btn_left\": \"btn_right\"}}");
	CHECK(e != NULL);
	feed(e, "+btn_left -btn_left");
	CHECK(!strcmp(rec, "+btn_right -btn_right"));
	grime_engine_free(e);
}

/* The reason all devices feed one engine: capslock comes off the keyboard and
 * the click comes off the trackpoint, and together they are one chord. */
TEST(a_held_key_can_layer_a_button)
{
	grime_engine *e = load("{\"keymap\": {\"capslock\": {\"tap\": \"esc\","
			       " \"hold\": {\"btn_left\": \"btn_right\"}}}}");
	CHECK(e != NULL);
	feed(e, "+capslock +btn_left -btn_left -capslock");
	CHECK(!strcmp(rec, "+btn_right -btn_right"));
	grime_engine_free(e);
}

TEST(a_button_can_open_a_layer)
{
	grime_engine *e = load("{\"keymap\": {\"btn_middle\": {\"hold\": {\"j\": \"down\"}}}}");
	CHECK(e != NULL);
	feed(e, "+btn_middle +j -j -btn_middle");
	CHECK(!strcmp(rec, "+down -down"));
	rec[0] = 0;
	feed(e, "+j -j"); /* layer is gone with the button */
	CHECK(!strcmp(rec, ""));
	grime_engine_free(e);
}

TEST(the_friendly_spelling_binds_the_same_key)
{
	grime_engine *e = load("{\"keymap\": {\"middleclick\": {\"do\": \"tap\", \"key\": \"esc\"}}}");
	CHECK(e != NULL);
	feed(e, "+btn_middle -btn_middle");
	CHECK(!strcmp(rec, "+esc -esc"));
	grime_engine_free(e);
}

/* canon() folds spellings to one code, so this is the same "bound twice" error
 * that "capslock" and "caps" already produce. */
TEST(an_alias_and_its_canonical_name_collide)
{
	grime_config cfg;
	char err[512] = "";
	int rc = grime_config_parse(
		"{\"keymap\": {\"btn_middle\": \"esc\", \"middleclick\": \"tab\"}}", &cfg,
		err, sizeof err);
	CHECK(rc < 0);
	CHECK(strstr(err, "bound twice") != NULL);
	if (rc == 0)
		grime_config_free(&cfg);
}

int main(void)
{
	grime_log_level = GRIME_LOG_ERROR;
	RUN(a_button_can_fire_an_action);
	RUN(a_button_can_be_another_button);
	RUN(a_held_key_can_layer_a_button);
	RUN(a_button_can_open_a_layer);
	RUN(the_friendly_spelling_binds_the_same_key);
	RUN(an_alias_and_its_canonical_name_collide);
	return DONE();
}
