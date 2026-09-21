#include "grime/runtime.h"

void grime_emit(grime_runtime *rt, uint16_t code, int value)
{
	if (code >= GRIME_KEY_COUNT)
		return;
	if (value == 1)
		rt->held[code] = 1;
	else if (value == 0)
		rt->held[code] = 0;
	rt->out->emit(rt->out, code, value);
}

void grime_release_all(grime_runtime *rt)
{
	for (int code = 0; code < GRIME_KEY_COUNT; code++)
		if (rt->held[code])
			grime_emit(rt, code, 0);
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
