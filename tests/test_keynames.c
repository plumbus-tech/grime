/* Key name table: mouse buttons, and the guarantee that one code has one spelling. */
#include <string.h>

#include "grime/event.h"
#include "grime/keynames.h"
#include "tinytest.h"

TEST(mouse_buttons_have_names)
{
	CHECK(grime_key_from_name("btn_left") == 0x110);
	CHECK(grime_key_from_name("btn_right") == 0x111);
	CHECK(grime_key_from_name("btn_middle") == 0x112);
	CHECK(grime_key_from_name("btn_side") == 0x113);
	CHECK(grime_key_from_name("btn_extra") == 0x114);
	CHECK(grime_key_from_name("btn_task") == 0x117);
	/* every button code grime's virtual pointer carries is inside the key space */
	CHECK(0x117 < GRIME_KEY_COUNT);
}

TEST(friendly_click_aliases)
{
	CHECK(grime_key_from_name("click") == 0x110);
	CHECK(grime_key_from_name("leftclick") == 0x110);
	CHECK(grime_key_from_name("rightclick") == 0x111);
	CHECK(grime_key_from_name("middleclick") == 0x112);
	/* deliberately absent: X11 and evdev disagree about what mouse2 means */
	CHECK(grime_key_from_name("mouse1") == -1);
	CHECK(grime_key_from_name("mouse2") == -1);
	CHECK(grime_key_from_name("mouse3") == -1);
}

/* Seven BTN_* group headers share a code with the name people actually mean,
 * and come first in input-event-codes.h. If the generator stops skipping them
 * they silently become the canonical spelling and canon() starts lying. */
TEST(group_aliases_never_win_the_canonical_spelling)
{
	CHECK(!strcmp(grime_key_name(0x100), "btn_0"));              /* not btn_misc */
	CHECK(!strcmp(grime_key_name(0x110), "btn_left"));           /* not btn_mouse */
	CHECK(!strcmp(grime_key_name(0x120), "btn_trigger"));        /* not btn_joystick */
	CHECK(!strcmp(grime_key_name(0x130), "btn_south"));          /* not btn_gamepad */
	CHECK(!strcmp(grime_key_name(0x140), "btn_tool_pen"));       /* not btn_digi */
	CHECK(!strcmp(grime_key_name(0x150), "btn_gear_down"));      /* not btn_wheel */
	CHECK(!strcmp(grime_key_name(0x2c0), "btn_trigger_happy1")); /* not btn_trigger_happy */
}

/* This is why buttons are prefixed: unprefixed, 23 of them would shadow a key. */
TEST(buttons_do_not_shadow_key_names)
{
	CHECK(grime_key_from_name("left") == 105);     /* the arrow, not BTN_LEFT */
	CHECK(grime_key_from_name("right") == 106);
	CHECK(grime_key_from_name("back") == 158);     /* KEY_BACK, not BTN_BACK */
	CHECK(grime_key_from_name("forward") == 159);
	CHECK(grime_key_from_name("select") == 0x161); /* KEY_SELECT, not BTN_SELECT */
	CHECK(grime_key_from_name("mode") == 0x175);   /* KEY_MODE, not BTN_MODE */
	CHECK(grime_key_from_name("a") == 30);         /* KEY_A, not BTN_A */
	CHECK(grime_key_from_name("c") == 46);
	CHECK(grime_key_from_name("z") == 44);
	CHECK(grime_key_from_name("0") == 11);
	CHECK(grime_key_from_name("9") == 10);
}

/* The permanent net under scripts/gen-keynames.sh: whatever spelling the table
 * hands back for a code must resolve to that same code. Any future alias
 * ordering bug shows up here as one line. */
TEST(every_name_round_trips_to_its_own_code)
{
	for (int code = 0; code < GRIME_KEY_COUNT; code++) {
		const char *name = grime_key_name(code);
		if (!strcmp(name, "?"))
			continue;
		if (grime_key_from_name(name) != code) {
			fprintf(stderr, "  %d -> \"%s\" -> %d\n", code, name,
				grime_key_from_name(name));
			CHECK(grime_key_from_name(name) == code);
		}
	}
	CHECK(1); /* so the test counts even when the table is clean */
}

int main(void)
{
	RUN(mouse_buttons_have_names);
	RUN(friendly_click_aliases);
	RUN(group_aliases_never_win_the_canonical_spelling);
	RUN(buttons_do_not_shadow_key_names);
	RUN(every_name_round_trips_to_its_own_code);
	return DONE();
}
