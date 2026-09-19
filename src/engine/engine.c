/* The tree walker. Rules (docs/CONFIG.md has the user-facing version):
 *  1. A press is looked up in the current (top) frame; with fallthrough, in
 *     the frames below it too. No match -> nothing happens.
 *  2. A release is looked up in the node where that key's press was handled,
 *     so press/release stay paired even if the context changed meanwhile.
 *  3. A binding with "then" pushes a frame. EXIT_RELEASE frames pop when the
 *     key that pushed them is released; EXIT_ACTION frames pop (as a chain)
 *     once an action fires inside them, on an unmatched press, or on timeout.
 *  4. "alone" release bindings only fire if no other key was pressed while
 *     this one was held. */
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
	grime_timer *timeout;
};

static struct frame *top(grime_engine *e)
{
	return &e->stack[e->depth - 1];
}

static void update_timeout(grime_engine *e)
{
	if (!e->timeout)
		return;
	const grime_binding *via = top(e)->via;
	if (via && via->timeout_ms)
		grime_timer_arm(e->timeout, via->timeout_ms);
	else
		grime_timer_disarm(e->timeout);
}

static void pop_to(grime_engine *e, int depth)
{
	if (depth < 1)
		depth = 1;
	if (depth < e->depth) {
		LOG_DEBUG("leave %d level(s)", e->depth - depth);
		e->depth = depth;
		update_timeout(e);
	}
}

/* Pop the chain of emacs-style prefix frames sitting on top of the stack. */
static void pop_prefixes(grime_engine *e)
{
	int d = e->depth;
	while (d > 1 && e->stack[d - 1].via->exit == GRIME_EXIT_ACTION)
		d--;
	pop_to(e, d);
}

static void push(grime_engine *e, const grime_binding *b, int key)
{
	if (e->depth == MAX_DEPTH) {
		LOG_WARN("keymap nesting deeper than %d, ignoring", MAX_DEPTH);
		return;
	}
	e->stack[e->depth++] = (struct frame){b->then, b, key};
	update_timeout(e);
}

static void on_timeout(grime_loop *loop, void *ud)
{
	(void)loop;
	grime_engine *e = ud;
	LOG_DEBUG("sequence timed out");
	pop_prefixes(e);
}

grime_engine *grime_engine_new(grime_runtime *rt, grime_node *root)
{
	grime_engine *e = calloc(1, sizeof *e);
	if (!e)
		return NULL;
	e->rt = rt;
	if (rt->loop)
		e->timeout = grime_timer_new(rt->loop, on_timeout, e);
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
	update_timeout(e);
}

void grime_engine_free(grime_engine *e)
{
	if (!e)
		return;
	grime_timer_free(e->timeout);
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
	fire(e, b, code, "press");
	if (b->then)
		push(e, b, code);
	else if (b->action)
		pop_prefixes(e);
	else
		update_timeout(e);
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
	else if (b->action)
		pop_prefixes(e);
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
