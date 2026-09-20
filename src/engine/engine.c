/* The tree walker. Rules (docs/CONFIG.md has the user-facing version):
 *  1. A press is looked up in the current (top) frame; with fallthrough, in
 *     the frames below it too. No match -> nothing happens.
 *  2. A release is looked up in the node where that key's press was handled,
 *     so press/release stay paired even if the context changed meanwhile.
 *  3. A binding with "then" pushes a frame. Frames are an ordered set of live
 *     layers, not a path: each one lives by its own rule and the ones below it
 *     can come and go underneath it (survivors slide down).
 *       EXIT_RELEASE lives exactly as long as the key that pushed it is down.
 *       EXIT_ACTION  lives until an action fires inside it, or an unmatched
 *                    press cancels it -- the whole prefix chain goes at once.
 *       EXIT_TOGGLE  lives until the binding that pushed it is pressed again,
 *                    from wherever that binding is reachable.
 *  4. "alone" release bindings only fire if no other key was pressed after
 *     this one was pressed (a key already down does not count).
 * The walk is driven purely by press/release events: no timers, no clocks. */
#include "grime/engine.h"

#include <stdlib.h>

#include "grime/keynames.h"
#include "grime/log.h"

struct entry {
	uint16_t code;
	grime_binding *press;
	grime_binding *release;
};

struct grime_node {
	struct entry *entries;
	size_t n, cap;
};

grime_node *grime_node_new(void)
{
	return calloc(1, sizeof(grime_node));
}

static void binding_free(grime_binding *b)
{
	if (!b)
		return;
	grime_action_free(b->action);
	grime_node_free(b->then);
	free(b);
}

void grime_node_free(grime_node *node)
{
	if (!node)
		return;
	for (size_t i = 0; i < node->n; i++) {
		binding_free(node->entries[i].press);
		binding_free(node->entries[i].release);
	}
	free(node->entries);
	free(node);
}

static struct entry *find(const grime_node *node, uint16_t code)
{
	for (size_t i = 0; i < node->n; i++)
		if (node->entries[i].code == code)
			return &node->entries[i];
	return NULL;
}

grime_binding *grime_node_bind(grime_node *node, uint16_t code, grime_edge edge)
{
	struct entry *e = find(node, code);
	if (!e) {
		if (node->n == node->cap) {
			size_t cap = node->cap ? node->cap * 2 : 8;
			struct entry *p = realloc(node->entries, cap * sizeof *p);
			if (!p)
				return NULL;
			node->entries = p;
			node->cap = cap;
		}
		e = &node->entries[node->n++];
		*e = (struct entry){.code = code};
	}
	grime_binding **slot = edge == GRIME_RELEASE ? &e->release : &e->press;
	if (!*slot)
		*slot = calloc(1, sizeof(grime_binding));
	return *slot;
}

grime_binding *grime_node_lookup(const grime_node *node, uint16_t code, grime_edge edge)
{
	struct entry *e = find(node, code);
	if (!e)
		return NULL;
	return edge == GRIME_RELEASE ? e->release : e->press;
}

/* ---- walker ---- */

#define MAX_DEPTH 64

struct frame {
	const grime_node *node;
	const grime_binding *via; /* binding that pushed it (NULL for root) */
	int key;                  /* key whose press/release pushed it, -1 for root */
};

struct keystate {
	bool down;
	const grime_node *node; /* where the press was handled */
	uint64_t press_seq;
};

struct grime_engine {
	grime_runtime *rt;
	grime_node *root;
	struct frame stack[MAX_DEPTH];
	int depth; /* frames in use; stack[0] is root */
	struct keystate keys[GRIME_KEY_COUNT];
	uint64_t press_seq;
};

static bool is_toggle(const struct frame *f)
{
	return f->via && f->via->exit == GRIME_EXIT_TOGGLE;
}

/* A hold frame lives exactly as long as its key is down, wherever it sits. */
static bool still_held(const grime_engine *e, const struct frame *f)
{
	return f->via && f->via->exit == GRIME_EXIT_RELEASE && f->key >= 0 &&
	       e->keys[f->key].down;
}

/* Drop frames from index `depth` up; toggles and still-held layers slide down. */
static void pop_to(grime_engine *e, int depth)
{
	if (depth < 1)
		depth = 1;
	int w = depth;
	for (int r = depth; r < e->depth; r++)
		if (is_toggle(&e->stack[r]) || still_held(e, &e->stack[r]))
			e->stack[w++] = e->stack[r];
	if (w < e->depth) {
		LOG_DEBUG("leave %d level(s)", e->depth - w);
		e->depth = w;
	}
}

/* Pop the chain of emacs-style prefix frames sitting on top of the stack
 * (toggles inside the chain stay). */
static void pop_prefixes(grime_engine *e)
{
	int d = e->depth;
	while (d > 1 && (e->stack[d - 1].via->exit == GRIME_EXIT_ACTION ||
			 is_toggle(&e->stack[d - 1]) || still_held(e, &e->stack[d - 1])))
		d--;
	pop_to(e, d);
}

static int find_frame(const grime_engine *e, const grime_binding *b)
{
	for (int d = 1; d < e->depth; d++)
		if (e->stack[d].via == b)
			return d;
	return -1;
}

static void remove_frame(grime_engine *e, int d)
{
	for (; d < e->depth - 1; d++)
		e->stack[d] = e->stack[d + 1];
	e->depth--;
}

static void push(grime_engine *e, const grime_binding *b, int key)
{
	if (e->depth == MAX_DEPTH) {
		LOG_WARN("keymap nesting deeper than %d, ignoring", MAX_DEPTH);
		return;
	}
	e->stack[e->depth++] = (struct frame){b->then, b, key};
}

grime_engine *grime_engine_new(grime_runtime *rt, grime_node *root)
{
	grime_engine *e = calloc(1, sizeof *e);
	if (!e)
		return NULL;
	e->rt = rt;
	grime_engine_set_keymap(e, root);
	return e;
}

void grime_engine_set_keymap(grime_engine *e, grime_node *root)
{
	grime_node_free(e->root);
	e->root = root;
	e->depth = 1;
	e->stack[0] = (struct frame){root, NULL, -1};
	/* keys held across a reload are forgotten: their releases are ignored */
	for (int i = 0; i < GRIME_KEY_COUNT; i++)
		e->keys[i] = (struct keystate){0};
}

void grime_engine_free(grime_engine *e)
{
	if (!e)
		return;
	grime_node_free(e->root);
	free(e);
}

static void fire(grime_engine *e, const grime_binding *b, uint16_t code, const char *what)
{
	LOG_DEBUG("%s %s -> %s", grime_key_name(code), what,
		  b->action ? b->action->type->name : "(descend)");
	if (b->action)
		grime_action_run(b->action, e->rt);
}

static void on_press(grime_engine *e, uint16_t code)
{
	struct keystate *ks = &e->keys[code];
	ks->down = true;
	ks->press_seq = ++e->press_seq;

	const grime_binding *b = NULL;
	int d = e->depth;
	while (d > 0) {
		b = grime_node_lookup(e->stack[d - 1].node, code, GRIME_PRESS);
		if (b || d == 1 || !e->stack[d - 1].via->fallthrough)
			break;
		d--;
	}
	ks->node = e->stack[(b ? d : e->depth) - 1].node;

	if (!b) {
		LOG_DEBUG("%s press: unmapped", grime_key_name(code));
		pop_prefixes(e); /* like emacs: an undefined key cancels the prefix */
		return;
	}
	if (b->exit == GRIME_EXIT_TOGGLE) {
		fire(e, b, code, "press"); /* both ways, so "do" can announce it */
		int on = find_frame(e, b);
		LOG_INFO("%s: toggle %s", grime_key_name(code), on > 0 ? "off" : "on");
		if (on > 0)
			remove_frame(e, on);
		else
			push(e, b, code);
		pop_prefixes(e); /* a toggle ends any prefix that led to it */
		return;
	}
	fire(e, b, code, "press");
	if (b->then)
		push(e, b, code);
	else if (b->action)
		pop_prefixes(e);
}

static void on_release(grime_engine *e, uint16_t code)
{
	struct keystate *ks = &e->keys[code];
	if (!ks->down)
		return; /* pressed before we started, or before a reload */
	ks->down = false;
	bool alone = ks->press_seq == e->press_seq;

	/* rule 3: leave hold-layers this key entered (and everything above them) */
	for (int d = 1; d < e->depth; d++) {
		if (e->stack[d].key == code && e->stack[d].via->exit == GRIME_EXIT_RELEASE) {
			pop_to(e, d);
			break;
		}
	}

	const grime_binding *b = grime_node_lookup(ks->node, code, GRIME_RELEASE);
	if (!b)
		return;
	if (b->alone && !alone) {
		LOG_DEBUG("%s release: not alone, skipped", grime_key_name(code));
		return;
	}
	fire(e, b, code, "release");
	if (b->then)
		push(e, b, code);
	else if (b->action && ks->node == e->stack[e->depth - 1].node)
		pop_prefixes(e); /* only if it fired *inside* the prefix */
}

void grime_engine_feed(grime_engine *e, const grime_key_event *ev)
{
	if (ev->code >= GRIME_KEY_COUNT)
		return;
	switch (ev->edge) {
	case GRIME_PRESS:
		on_press(e, ev->code);
		break;
	case GRIME_RELEASE:
		on_release(e, ev->code);
		break;
	case GRIME_REPEAT:
		break; /* the OS does its own autorepeat for held output keys */
	}
}
