/* Device matching policy. Pure: no ioctls, no /dev, no root -- which is the
 * whole point of keeping grime_device_match_find() out of the backend. */
#include <string.h>

#include "grime/device.h"
#include "tinytest.h"

#define ANY_IDS .vendor = -1, .product = -1, .bus = -1

static const char *const tp_links[] = {"/dev/input/by-path/platform-i8042-serio-1-event-mouse"};

static const grime_device_info keyboard = {
	.path = "/dev/input/event3",
	.name = "AT Translated Set 2 keyboard",
	.phys = "isa0060/serio0/input0",
	.vendor = 0x0001, .product = 0x0001, .bus = 0x0011,
	.kind = GRIME_KIND_KEYBOARD,
};

static const grime_device_info trackpoint = {
	.path = "/dev/input/event5",
	.name = "TPPS/2 Elan TrackPoint",
	.phys = "isa0060/serio1/input0",
	.links = tp_links, .nlinks = 1,
	.vendor = 0x0002, .product = 0x000a, .bus = 0x0011,
	.kind = GRIME_KIND_POINTER,
};

static const grime_device_info extra_buttons = {
	.path = "/dev/input/event4",
	.name = "ThinkPad Extra Buttons",
	.phys = "thinkpad_acpi/input0",
	.vendor = 0x17aa, .product = 0x5054, .bus = 0x0019,
	.kind = GRIME_KIND_KEYS,
};

TEST(kind_selects_a_class_of_device)
{
	grime_device_match m[] = {{ANY_IDS, .kind = GRIME_KIND_POINTER}};
	CHECK(grime_device_match_find(m, 1, &trackpoint) == 0);
	CHECK(grime_device_match_find(m, 1, &keyboard) == -1);
	CHECK(grime_device_match_find(m, 1, &extra_buttons) == -1);
}

/* "auto" lowers to exactly this, and it is why a laptop's fn-key block is
 * invisible until you ask for it by name or by kind. */
TEST(auto_takes_keyboards_only)
{
	grime_device_match m[] = {{ANY_IDS, .kind = GRIME_KIND_KEYBOARD}};
	CHECK(grime_device_match_find(m, 1, &keyboard) == 0);
	CHECK(grime_device_match_find(m, 1, &extra_buttons) == -1);
	CHECK(grime_device_match_find(m, 1, &trackpoint) == -1);
}

TEST(fields_in_one_matcher_are_anded)
{
	grime_device_match both[] = {{.product = -1, .bus = -1, .name = "ThinkPad*", .vendor = 0x17aa}};
	CHECK(grime_device_match_find(both, 1, &extra_buttons) == 0);

	grime_device_match wrong_vendor[] = {{.product = -1, .bus = -1, .name = "ThinkPad*", .vendor = 0x1234}};
	CHECK(grime_device_match_find(wrong_vendor, 1, &extra_buttons) == -1);

	grime_device_match wrong_name[] = {{.product = -1, .bus = -1, .name = "Nope*", .vendor = 0x17aa}};
	CHECK(grime_device_match_find(wrong_name, 1, &extra_buttons) == -1);
}

TEST(matchers_are_ored_and_the_first_one_wins)
{
	grime_device_match m[] = {
		{ANY_IDS, .kind = GRIME_KIND_KEYBOARD, .grab = true},
		{ANY_IDS, .name = "TPPS/2*", .grab = false},
		{ANY_IDS, .kind = GRIME_KIND_POINTER, .grab = true},
	};
	CHECK(grime_device_match_find(m, 3, &keyboard) == 0);
	/* the pointer matches entries 1 and 2; entry 1 supplies its options */
	CHECK(grime_device_match_find(m, 3, &trackpoint) == 1);
	CHECK(m[grime_device_match_find(m, 3, &trackpoint)].grab == false);
	CHECK(grime_device_match_find(m, 3, &extra_buttons) == -1);
}

/* eventN numbering moves between boots; by-path and by-id names don't. */
TEST(a_path_glob_matches_the_symlinks_too)
{
	grime_device_match link[] = {{ANY_IDS, .path = "/dev/input/by-path/*serio-1-event-mouse"}};
	CHECK(grime_device_match_find(link, 1, &trackpoint) == 0);
	CHECK(grime_device_match_find(link, 1, &keyboard) == -1);

	grime_device_match node[] = {{ANY_IDS, .path = "/dev/input/event5"}};
	CHECK(grime_device_match_find(node, 1, &trackpoint) == 0);

	/* a device with no symlinks is not matched by a by-path glob */
	grime_device_match any_link[] = {{ANY_IDS, .path = "/dev/input/by-path/*"}};
	CHECK(grime_device_match_find(any_link, 1, &keyboard) == -1);
}

TEST(unset_fields_are_wildcards)
{
	grime_device_match m[] = {{ANY_IDS}};
	CHECK(grime_device_match_find(m, 1, &keyboard) == 0);
	CHECK(grime_device_match_find(m, 1, &trackpoint) == 0);

	grime_device_match phys[] = {{ANY_IDS, .phys = "isa0060/serio1/*"}};
	CHECK(grime_device_match_find(phys, 1, &trackpoint) == 0);
	CHECK(grime_device_match_find(phys, 1, &keyboard) == -1);

	grime_device_match bus[] = {{.vendor = -1, .product = -1, .bus = 0x0019}};
	CHECK(grime_device_match_find(bus, 1, &extra_buttons) == 0);
	CHECK(grime_device_match_find(bus, 1, &keyboard) == -1);
}

/* A device that reports no name or no phys must not crash a matcher that asks. */
TEST(a_missing_field_never_matches_a_pattern)
{
	grime_device_info bare = {.path = "/dev/input/event9", .vendor = -1, .product = -1, .bus = -1};
	grime_device_match m[] = {{ANY_IDS, .name = "*"}};
	CHECK(grime_device_match_find(m, 1, &bare) == -1);
	grime_device_match u[] = {{ANY_IDS, .uniq = "*"}};
	CHECK(grime_device_match_find(u, 1, &keyboard) == -1);
}

TEST(kind_names_round_trip)
{
	CHECK(grime_device_kind_from_name("any") == GRIME_KIND_ANY);
	CHECK(grime_device_kind_from_name("keyboard") == GRIME_KIND_KEYBOARD);
	CHECK(grime_device_kind_from_name("keys") == GRIME_KIND_KEYS);
	CHECK(grime_device_kind_from_name("pointer") == GRIME_KIND_POINTER);
	CHECK(grime_device_kind_from_name("mouse") == -1);
	CHECK(!strcmp(grime_device_kind_name(GRIME_KIND_POINTER), "pointer"));
	CHECK(!strcmp(grime_device_kind_name(GRIME_KIND_KEYS), "keys"));
}

int main(void)
{
	RUN(kind_selects_a_class_of_device);
	RUN(auto_takes_keyboards_only);
	RUN(fields_in_one_matcher_are_anded);
	RUN(matchers_are_ored_and_the_first_one_wins);
	RUN(a_path_glob_matches_the_symlinks_too);
	RUN(unset_fields_are_wildcards);
	RUN(a_missing_field_never_matches_a_pattern);
	RUN(kind_names_round_trip);
	return DONE();
}
