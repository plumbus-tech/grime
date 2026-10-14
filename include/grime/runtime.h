/* runtime.h — what actions get to touch when they run. */
#ifndef GRIME_RUNTIME_H
#define GRIME_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "grime/event.h"
#include "grime/loop.h"
#include "grime/output.h"

#define GRIME_CHORD_MAX 8

typedef struct grime_runtime grime_runtime;

/* Gets the keyboard while an action holds it: every press, and every release
 * whose press it got. `keymap_has_it` marks a release the keymap is handling
 * because its press came first; that one is only for keeping count. */
typedef void (*grime_capture_fn)(grime_runtime *rt, const grime_key_event *ev, bool keymap_has_it,
				 void *ud);

struct grime_runtime {
	grime_loop *loop;
	grime_output *out;
	bool dry_run; /* side-effecting actions (exec, http) only log */
	void (*quit)(grime_runtime *rt);
	void (*reload)(grime_runtime *rt); /* must defer: may be called mid-walk */
	/* Take the keyboard away from the keymap, and give it back. May be NULL
	 * (tests), in which case there is no keyboard to take. */
	void (*capture)(grime_runtime *rt, grime_capture_fn fn, void *ud);
	void (*uncapture)(grime_runtime *rt);
	/* Walk an event through the keymap as if it had just been typed: for a
	 * capture to hand back what it did not want. */
	void (*feed)(grime_runtime *rt, const grime_key_event *ev);
	void *ud;

	const uint8_t *down; /* physical keys held right now; may be NULL */
	uint16_t key;        /* the key whose press or release runs the current action */
	/* Who exec'd children run as and with what environment. No env means as
	 * grime itself, with its environment. */
	int child_uid, child_gid;
	char **child_env;

	uint8_t held[GRIME_KEY_COUNT]; /* output keys currently pressed, see grime_emit */
	/* Modifiers the running binding's chord used up: "ctrl+a": "home" sends a
	 * bare Home. They are let go at the output just before the action's first
	 * press, and pressed again before the next key that does not consume them,
	 * if still held. */
	uint16_t consumed[GRIME_CHORD_MAX];
	uint8_t nconsumed;
	bool lift_pending;
	uint8_t lifted[GRIME_KEY_COUNT];
};

/* Emit through rt->out, tracking held keys so they can be released on exit/reload. */
void grime_emit(grime_runtime *rt, uint16_t code, int value);
void grime_release_all(grime_runtime *rt);
/* Bracket an action that consumes these modifiers (n may be 0). */
void grime_consume_begin(grime_runtime *rt, const uint16_t *codes, int n);
void grime_consume_end(grime_runtime *rt);

#endif
