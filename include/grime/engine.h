/* engine.h — the keymap tree and the walker that feeds events through it.
 *
 * A node maps (key, edge) -> binding. A binding may fire an action, descend
 * into a child node ("then"), or both. See docs/CONFIG.md for the rules. */
#ifndef GRIME_ENGINE_H
#define GRIME_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#include "grime/action.h"
#include "grime/event.h"
#include "grime/runtime.h"

typedef struct grime_node grime_node;

typedef enum {
	GRIME_EXIT_RELEASE, /* leave the child when the key that entered it is released */
	GRIME_EXIT_ACTION,  /* leave after the first action fires inside it (emacs prefix) */
	GRIME_EXIT_TOGGLE,  /* stay until the same binding is pressed again; survives other pops */
} grime_exit_mode;

typedef struct grime_binding {
	grime_action *action; /* may be NULL */
	grime_node *then;     /* may be NULL */
	grime_exit_mode exit;
	bool fallthrough; /* unmatched presses in the child try the parent */
	bool alone;       /* release only: fire only if no other key was pressed meanwhile */
} grime_binding;

grime_node *grime_node_new(void);
void grime_node_free(grime_node *node); /* recursive; frees actions too */
/* Returns the (zeroed on creation) binding slot for (code, edge). */
grime_binding *grime_node_bind(grime_node *node, uint16_t code, grime_edge edge);
grime_binding *grime_node_lookup(const grime_node *node, uint16_t code, grime_edge edge);

typedef struct grime_engine grime_engine;

grime_engine *grime_engine_new(grime_runtime *rt, grime_node *root);
/* Swap in a new tree (takes ownership, frees the old one) and reset walk state. */
void grime_engine_set_keymap(grime_engine *e, grime_node *root);
void grime_engine_feed(grime_engine *e, const grime_key_event *ev);
void grime_engine_free(grime_engine *e);

#endif
