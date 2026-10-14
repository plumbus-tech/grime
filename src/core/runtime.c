#include "grime/runtime.h"

#include <string.h>

static void out_emit(grime_runtime *rt, uint16_t code, int value)
{
	rt->held[code] = value != 0;
	rt->out->emit(rt->out, code, value);
}

static bool is_consumed(const grime_runtime *rt, uint16_t code)
{
	for (int i = 0; i < rt->nconsumed; i++)
		if (rt->consumed[i] == code)
			return true;
	return false;
}

void grime_emit(grime_runtime *rt, uint16_t code, int value)
{
	if (code >= GRIME_KEY_COUNT)
		return;
	if (value == 0 && rt->lifted[code] && !rt->held[code]) {
		rt->lifted[code] = 0; /* the output already saw it go up */
		return;
	}
	/* once per action: its first press is where the chord's modifiers let go
	 * and any lifted earlier but not used up now come back */
	if (value == 1 && (rt->lift_pending || !rt->nconsumed)) {
		for (int m = 0; m < GRIME_KEY_COUNT; m++)
			if (rt->lifted[m] && !rt->held[m] && m != code && !is_consumed(rt, m)) {
				out_emit(rt, m, 1);
				rt->lifted[m] = 0;
			}
		for (int i = 0; rt->lift_pending && i < rt->nconsumed; i++) {
			uint16_t m = rt->consumed[i];
			if (rt->held[m]) {
				out_emit(rt, m, 0);
				rt->lifted[m] = 1;
			}
		}
		rt->lift_pending = false;
	}
	if (value == 1 && !rt->nconsumed)
		rt->lifted[code] = 0;
	if (value != 2)
		rt->held[code] = value;
	rt->out->emit(rt->out, code, value);
}

void grime_release_all(grime_runtime *rt)
{
	memset(rt->lifted, 0, sizeof rt->lifted);
	for (int code = 0; code < GRIME_KEY_COUNT; code++)
		if (rt->held[code])
			grime_emit(rt, code, 0);
}

void grime_consume_begin(grime_runtime *rt, const uint16_t *codes, int n)
{
	if (n > GRIME_CHORD_MAX)
		n = GRIME_CHORD_MAX;
	for (int i = 0; i < n; i++)
		rt->consumed[i] = codes[i];
	rt->nconsumed = n;
	rt->lift_pending = n > 0;
}

void grime_consume_end(grime_runtime *rt)
{
	rt->nconsumed = 0;
	rt->lift_pending = false;
}

/* Raw forwarding is optional: an output that has no emit_ev (the --dry-run
 * logger's twin, a test recorder) simply drops it. */
void grime_output_ev(grime_output *out, uint16_t type, uint16_t code, int32_t value)
{
	if (out && out->emit_ev)
		out->emit_ev(out, type, code, value);
}

void grime_output_prepare(grime_output *out)
{
	if (out && out->prepare)
		out->prepare(out);
}
