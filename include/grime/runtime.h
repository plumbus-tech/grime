/* runtime.h — what actions get to touch when they run. */
#ifndef GRIME_RUNTIME_H
#define GRIME_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "grime/event.h"
#include "grime/loop.h"
#include "grime/output.h"

typedef struct grime_runtime grime_runtime;
struct grime_runtime {
	grime_loop *loop;
	grime_output *out;
	bool dry_run; /* side-effecting actions (exec, http) only log */
	void (*quit)(grime_runtime *rt);
	void (*reload)(grime_runtime *rt); /* must defer: may be called mid-walk */
	void *ud;
	uint8_t held[GRIME_KEY_COUNT]; /* output keys currently pressed, see grime_emit */
};

/* Emit through rt->out, tracking held keys so they can be released on exit/reload. */
void grime_emit(grime_runtime *rt, uint16_t code, int value);
void grime_release_all(grime_runtime *rt);

#endif
