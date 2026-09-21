/* What can be tested about output without /dev/uinput: the --dry-run twin, and
 * that raw forwarding is safe to call on an output that has no emit_ev. The
 * uinput devices themselves need the manual recipe in docs/CONFIG.md. */
#include <string.h>

#include "grime/log.h"
#include "grime/output.h"
#include "grime/runtime.h"
#include "tinytest.h"

static int emits, raws;

static void count_emit(grime_output *out, uint16_t code, int value) { emits++; }
static void count_ev(grime_output *out, uint16_t type, uint16_t code, int32_t value) { raws++; }

TEST(raw_forwarding_is_optional)
{
	grime_output bare = {.emit = count_emit};
	emits = raws = 0;
	grime_output_ev(&bare, 2 /* EV_REL */, 0, -3); /* must not crash */
	CHECK(raws == 0);

	grime_output full = {.emit = count_emit, .emit_ev = count_ev};
	grime_output_ev(&full, 2, 0, -3);
	CHECK(raws == 1);

	grime_output_ev(NULL, 2, 0, -3); /* --dry-run may have no pointer at all */
	CHECK(raws == 1);
}

/* grime_emit tracks held keys so they can be released on exit and reload; the
 * array is GRIME_KEY_COUNT wide, so a held button is released like any key. */
TEST(a_held_button_is_released_on_the_way_out)
{
	grime_output out = {.emit = count_emit};
	grime_runtime rt = {.out = &out};
	emits = 0;
	grime_emit(&rt, 0x110 /* btn_left */, 1);
	CHECK(rt.held[0x110] == 1);
	grime_release_all(&rt);
	CHECK(rt.held[0x110] == 0);
	CHECK(emits == 2); /* the press, and the release release_all sent */
}

TEST(the_log_output_accepts_everything)
{
	grime_output *out = grime_output_log_new();
	CHECK(out != NULL);
	CHECK(out->emit != NULL);
	CHECK(out->emit_ev != NULL); /* --dry-run shows forwarded motion too */
	out->emit(out, 0x112 /* btn_middle */, 1);
	grime_output_ev(out, 2 /* EV_REL */, 1 /* REL_Y */, -3);
	out->destroy(out);
	CHECK(1);
}

int main(void)
{
	grime_log_level = GRIME_LOG_ERROR;
	RUN(raw_forwarding_is_optional);
	RUN(a_held_button_is_released_on_the_way_out);
	RUN(the_log_output_accepts_everything);
	return DONE();
}
