/* The "devices" front end: the two spellings that predate matchers still mean
 * what they meant, the object form says what it says, and the errors are the
 * ones you would want at 1am with the wrong keyboard grabbed. */
#include <stdlib.h>
#include <string.h>

#include "grime/config.h"
#include "grime/log.h"
#include "tinytest.h"

#define KM "\"keymap\": {\"a\": \"a\"}"

static grime_config cfg;

static bool load(const char *json)
{
	char err[512];
	char buf[1024];
	snprintf(buf, sizeof buf, "{%s, %s}", json, KM);
	if (grime_config_parse(buf, &cfg, err, sizeof err) < 0) {
		fprintf(stderr, "  config error: %s\n", err);
		return false;
	}
	return true;
}

static bool fails(const char *json, const char *needle)
{
	char err[512] = "";
	char buf[1024];
	snprintf(buf, sizeof buf, "{%s, %s}", json, KM);
	grime_config c;
	if (grime_config_parse(buf, &c, err, sizeof err) == 0) {
		grime_config_free(&c);
		return false;
	}
	return strstr(err, needle) != NULL;
}

TEST(auto_still_means_every_keyboard)
{
	CHECK(load("\"devices\": [\"auto\"]"));
	CHECK(cfg.ndevices == 1);
	CHECK(cfg.devices[0].kind == GRIME_KIND_KEYBOARD);
	CHECK(cfg.devices[0].path == NULL);
	CHECK(cfg.devices[0].grab == true);
	CHECK(cfg.devices[0].unmatched == GRIME_UNMATCHED_DROP);
	grime_config_free(&cfg);
}

/* An explicit path always bypassed the keyboard heuristic. It still does. */
TEST(a_bare_path_takes_the_device_whatever_it_is)
{
	CHECK(load("\"devices\": [\"/dev/input/event5\"]"));
	CHECK(cfg.ndevices == 1);
	CHECK(cfg.devices[0].kind == GRIME_KIND_ANY);
	CHECK(!strcmp(cfg.devices[0].path, "/dev/input/event5"));
	grime_config_free(&cfg);
}

TEST(no_devices_key_is_the_same_as_auto)
{
	CHECK(load("\"base\": null"));
	CHECK(cfg.ndevices == 1);
	CHECK(cfg.devices[0].kind == GRIME_KIND_KEYBOARD);
	grime_config_free(&cfg);
}

TEST(a_matcher_carries_every_field)
{
	CHECK(load("\"devices\": [{\"name\": \"ThinkPad*\", \"phys\": \"thinkpad*\","
		   " \"vendor\": \"17aa\", \"product\": \"0x5054\", \"bus\": \"0019\","
		   " \"kind\": \"keys\", \"grab\": false}]"));
	CHECK(cfg.ndevices == 1);
	grime_device_match *m = &cfg.devices[0];
	CHECK(!strcmp(m->name, "ThinkPad*"));
	CHECK(!strcmp(m->phys, "thinkpad*"));
	CHECK(m->vendor == 0x17aa);
	CHECK(m->product == 0x5054); /* the 0x prefix is optional */
	CHECK(m->bus == 0x0019);
	CHECK(m->kind == GRIME_KIND_KEYS);
	CHECK(m->grab == false);
	grime_config_free(&cfg);
}

/* A grabbed pointer whose buttons the keymap doesn't mention would otherwise
 * be a dead mouse, so this one default is different. */
TEST(a_pointer_passes_unmatched_buttons_by_default)
{
	CHECK(load("\"devices\": [{\"kind\": \"pointer\"}, {\"kind\": \"keyboard\"}]"));
	CHECK(cfg.ndevices == 2);
	CHECK(cfg.devices[0].unmatched == GRIME_UNMATCHED_PASS);
	CHECK(cfg.devices[1].unmatched == GRIME_UNMATCHED_DROP);
	grime_config_free(&cfg);

	CHECK(load("\"devices\": [{\"kind\": \"pointer\", \"unmatched\": \"drop\"}]"));
	CHECK(cfg.devices[0].unmatched == GRIME_UNMATCHED_DROP);
	grime_config_free(&cfg);
}

TEST(mixed_spellings_keep_their_order)
{
	CHECK(load("\"devices\": [\"auto\", \"/dev/input/event5\", {\"name\": \"Foot*\"}]"));
	CHECK(cfg.ndevices == 3);
	CHECK(cfg.devices[0].kind == GRIME_KIND_KEYBOARD);
	CHECK(cfg.devices[1].path != NULL);
	CHECK(cfg.devices[2].name != NULL);
	grime_config_free(&cfg);
}

TEST(the_errors_say_what_to_do)
{
	CHECK(fails("\"devices\": []", "grab nothing"));
	CHECK(fails("\"devices\": [null]", "matcher object"));
	CHECK(fails("\"devices\": [{\"nmae\": \"x\"}]", "unknown \"nmae\""));
	CHECK(fails("\"devices\": [{\"kind\": \"mouse\"}]", "expected \"keyboard\""));
	CHECK(fails("\"devices\": [{\"kind\": 3}]", "expected \"keyboard\""));
	CHECK(fails("\"devices\": [{\"unmatched\": \"maybe\"}]", "\"drop\" or \"pass\""));
	CHECK(fails("\"devices\": [{\"grab\": \"yes\"}]", "true or false"));
	CHECK(fails("\"devices\": [{}]", "matches nothing in particular"));
	/* an empty matcher is an error rather than "everything", because the one
	 * that grabs your whole desk should have to say so */
	CHECK(load("\"devices\": [{\"kind\": \"any\"}]"));
	grime_config_free(&cfg);
}

/* 5054 read as decimal is 0x13be: a silently wrong device, found weeks later. */
TEST(ids_must_be_hex_strings)
{
	CHECK(fails("\"devices\": [{\"vendor\": 5054}]", "hex string"));
	CHECK(fails("\"devices\": [{\"product\": \"nope\"}]", "four hex digits"));
	CHECK(fails("\"devices\": [{\"bus\": \"123456\"}]", "four hex digits"));
	CHECK(load("\"devices\": [{\"vendor\": \"17aa\"}]"));
	CHECK(cfg.devices[0].vendor == 0x17aa);
	grime_config_free(&cfg);
}

/* The way out is config, but it defaults to what it has always been, and you
 * cannot set it to something you would trip on by accident. */
TEST(the_emergency_chord_is_configurable)
{
	CHECK(load("\"devices\": [\"auto\"]"));
	CHECK(cfg.nemergency == 2);
	CHECK(cfg.emergency[0] == 1);  /* esc */
	CHECK(cfg.emergency[1] == 14); /* backspace */
	grime_config_free(&cfg);

	CHECK(load("\"emergency\": [\"f12\", \"f11\"]"));
	CHECK(cfg.nemergency == 2);
	CHECK(cfg.emergency[0] == 88); /* f12 */
	CHECK(cfg.emergency[1] == 87); /* f11 */
	grime_config_free(&cfg);

	CHECK(load("\"emergency\": false"));
	CHECK(cfg.nemergency == 0);
	grime_config_free(&cfg);

	CHECK(fails("\"emergency\": [\"esc\"]", "at least two keys"));
	CHECK(fails("\"emergency\": [\"a\",\"b\",\"c\",\"d\",\"e\"]", "at most 4 keys"));
	CHECK(fails("\"emergency\": [\"esc\", \"nosuchkey\"]", "unknown key"));
	CHECK(fails("\"emergency\": true", "or false to turn it off"));
	CHECK(fails("\"emergency\": \"esc\"", "list of key names"));
}

TEST(the_expansion_shows_what_a_matcher_became)
{
	char err[512];
	char *text = grime_config_expand("configs/default.json", err, sizeof err);
	CHECK(text != NULL);
	if (text) {
		CHECK(strstr(text, "\"kind\":\"keyboard\"") != NULL);
		CHECK(strstr(text, "\"grab\":true") != NULL);
		free(text);
	}
}

int main(void)
{
	grime_log_level = GRIME_LOG_ERROR;
	RUN(auto_still_means_every_keyboard);
	RUN(a_bare_path_takes_the_device_whatever_it_is);
	RUN(no_devices_key_is_the_same_as_auto);
	RUN(a_matcher_carries_every_field);
	RUN(a_pointer_passes_unmatched_buttons_by_default);
	RUN(mixed_spellings_keep_their_order);
	RUN(the_errors_say_what_to_do);
	RUN(ids_must_be_hex_strings);
	RUN(the_emergency_chord_is_configurable);
	RUN(the_expansion_shows_what_a_matcher_became);
	return DONE();
}
