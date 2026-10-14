/* The tree walker. Rules (docs/CONFIG.md has the user-facing version):
 *  1. The stack holds layers. A press is looked up in the top one; with
 *     fallthrough ("pass", and always for a mode), in the ones below it too.
 *     No match -> nothing happens.
 *  2. Within a layer, held chord keys are followed first: with ctrl held,
 *     "a" is looked up under that layer's ctrl, then in the layer itself.
 *     Chord keys push nothing, so the same held ctrl means each layer's own
 *     ctrl, whichever layers come and go while it is down.
 *  3. A release is looked up in the node where that key's press was handled,
 *     so press/release stay paired even if the context changed meanwhile.
 *  4. A binding with "then" pushes a layer. Layers are an ordered set, not a
 *     path: each one lives by its own rule and the ones below it can come and
 *     go underneath it (survivors slide down).
 *       EXIT_RELEASE lives exactly as long as the key that pushed it is down.
 *       EXIT_ACTION  lives until an action fires inside it, or an unmatched
 *                    press cancels it -- the whole prefix chain goes at once.
 *       EXIT_TOGGLE  lives until the binding that pushed it is pressed again,
 *                    from wherever that binding is reachable.
 *       EXIT_MODE    as a toggle, but a non-modifier press it does not define
 *                    also ends it, then goes on to the layers below.
 *     A "leave" binding ends the layer it was found in.
 *  5. "alone" release bindings only fire if no other key was pressed after
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
#define MAX_DOWN 16

struct frame {
	const grime_node *node;
	const grime_binding *via; /* binding that pushed it (NULL for root) */
	int key;                  /* key whose press/release pushed it, -1 for root */
};

struct keystate {
	bool down;
	const grime_node *node;  /* where the press was handled */
	const grime_node *layer; /* the layer that node belongs to */
	uint64_t press_seq;
	uint16_t consumed[GRIME_CHORD_MAX]; /* chord keys its press binding used up */
	uint8_t nconsumed;
};

struct grime_engine {
	grime_runtime *rt;
	grime_node *root;
	struct frame stack[MAX_DEPTH];
	int depth; /* frames in use; stack[0] is root */
	struct keystate keys[GRIME_KEY_COUNT];
	uint64_t press_seq;
	uint16_t down[MAX_DOWN]; /* held keys in press order, for chords */
	int ndown;
};

/* What a lookup found, and how. */
struct hit {
	const grime_binding *b;
	int layer; /* stack index */
	const grime_node *node;
	uint16_t consumed[GRIME_CHORD_MAX];
	int nconsumed;
};

static grime_exit_mode exit_of(const struct frame *f)
{
	return f->via ? f->via->exit : GRIME_EXIT_RELEASE;
}

/* Toggles and modes leave on their own terms, never because a layer under
 * them went. */
static bool is_sticky(const struct frame *f)
{
	return f->via && (f->via->exit == GRIME_EXIT_TOGGLE || f->via->exit == GRIME_EXIT_MODE);
}

static bool passes(const struct frame *f)
{
	return f->via && (f->via->fallthrough || f->via->exit == GRIME_EXIT_MODE);
}

/* A hold frame lives exactly as long as its key is down, wherever it sits. */
static bool still_held(const grime_engine *e, const struct frame *f)
{
	return f->via && f->via->exit == GRIME_EXIT_RELEASE && f->key >= 0 &&
	       e->keys[f->key].down;
}

/* Drop frames from index `depth` up; toggles, modes and still-held layers slide down. */
static void pop_to(grime_engine *e, int depth)
{
	if (depth < 1)
		depth = 1;
	int w = depth;
	for (int r = depth; r < e->depth; r++)
		if (is_sticky(&e->stack[r]) || still_held(e, &e->stack[r]))
			e->stack[w++] = e->stack[r];
	if (w < e->depth) {
		LOG_DEBUG("leave %d level(s)", e->depth - w);
		e->depth = w;
	}
}

/* Pop the chain of emacs-style prefix frames sitting on top of the stack
 * (toggles, modes and held layers inside the chain stay). */
static void pop_prefixes(grime_engine *e)
{
	int d = e->depth;
	while (d > 1 && (exit_of(&e->stack[d - 1]) == GRIME_EXIT_ACTION ||
			 is_sticky(&e->stack[d - 1]) || still_held(e, &e->stack[d - 1])))
		d--;
	pop_to(e, d);
}

static bool same_binding(const grime_binding *a, const grime_binding *b)
{
	return a == b || (a && b && a->id && a->id == b->id);
}

static int find_frame(const grime_engine *e, const grime_binding *b)
{
	for (int d = 1; d < e->depth; d++)
		if (same_binding(e->stack[d].via, b))
			return d;
	return -1;
}

static void remove_frame(grime_engine *e, int d)
{
	LOG_DEBUG("leave layer %d", d);
	for (; d < e->depth - 1; d++)
		e->stack[d] = e->stack[d + 1];
	e->depth--;
}

/* Modes above `layer` were passed over: they end. */
static void end_modes_above(grime_engine *e, int layer)
{
	for (int d = e->depth - 1; d > layer; d--)
		if (exit_of(&e->stack[d]) == GRIME_EXIT_MODE)
			remove_frame(e, d);
}

static void push(grime_engine *e, const grime_binding *b, int key)
{
	if (e->depth == MAX_DEPTH) {
		LOG_WARN("keymap nesting deeper than %d, ignoring", MAX_DEPTH);
		return;
	}
	e->stack[e->depth++] = (struct frame){b->then, b, key};
}

/* Toggles marked "on" start pushed: at the root, or under a chord there. */
static void push_initial(grime_engine *e, const grime_node *node)
{
	for (size_t i = 0; i < node->n; i++) {
		const grime_binding *b = node->entries[i].press;
		if (!b)
			continue;
		if (b->exit == GRIME_EXIT_TOGGLE && b->on && b->then && find_frame(e, b) < 0)
			push(e, b, -1);
		else if (b->chord && b->then)
			push_initial(e, b->then);
	}
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
	e->ndown = 0;
	if (root)
		push_initial(e, root);
}

void grime_engine_free(grime_engine *e)
{
	if (!e)
		return;
	grime_node_free(e->root);
	free(e);
}

static void fire(grime_engine *e, const grime_binding *b, uint16_t code, const char *what,
		 const uint16_t *consumed, int nconsumed)
{
	LOG_DEBUG("%s %s -> %s", grime_key_name(code), what,
		  b->action ? b->action->type->name : "(descend)");
	if (!b->action)
		return;
	e->rt->key = code;
	grime_consume_begin(e->rt, consumed, nconsumed);
	grime_action_run(b->action, e->rt);
	grime_consume_end(e->rt);
}

/* Look `code` up in one layer: under the chords its held keys make there,
 * deepest first, then in the layer itself. */
static const grime_binding *lookup_layer(const grime_engine *e, const grime_node *layer,
					 uint16_t code, struct hit *h)
{
	const grime_node *chain[GRIME_CHORD_MAX + 1] = {layer};
	uint16_t via[GRIME_CHORD_MAX];
	bool used[MAX_DOWN] = {0};
	int n = 0;
	for (bool more = true; more && n < GRIME_CHORD_MAX;) {
		more = false;
		for (int i = 0; i < e->ndown; i++) {
			if (used[i] || e->down[i] == code)
				continue;
			const grime_binding *b = grime_node_lookup(chain[n], e->down[i], GRIME_PRESS);
			if (b && b->chord && b->then) {
				used[i] = true;
				via[n] = e->down[i];
				chain[++n] = b->then;
				more = true;
				break;
			}
		}
	}
	for (int c = n; c >= 0; c--) {
		const grime_binding *b = grime_node_lookup(chain[c], code, GRIME_PRESS);
		if (b) {
			h->node = chain[c];
			h->nconsumed = c;
			for (int i = 0; i < c; i++)
				h->consumed[i] = via[i];
			return b;
		}
	}
	return NULL;
}

static bool lookup(const grime_engine *e, uint16_t code, struct hit *h)
{
	for (int d = e->depth - 1; d >= 0; d--) {
		h->b = lookup_layer(e, e->stack[d].node, code, h);
		h->layer = d;
		if (h->b)
			return true;
		if (!passes(&e->stack[d]))
			break;
	}
	return false;
}

static void down_add(grime_engine *e, uint16_t code)
{
	if (e->ndown < MAX_DOWN)
		e->down[e->ndown++] = code;
}

static void down_remove(grime_engine *e, uint16_t code)
{
	for (int i = 0; i < e->ndown; i++)
		if (e->down[i] == code) {
			for (; i < e->ndown - 1; i++)
				e->down[i] = e->down[i + 1];
			e->ndown--;
			return;
		}
}

/* Returns true if a binding matched -- see grime_engine_feed. */
static bool on_press(grime_engine *e, uint16_t code)
{
	struct keystate *ks = &e->keys[code];
	if (ks->down)
		down_remove(e, code); /* a press we never saw released */
	ks->down = true;
	ks->press_seq = ++e->press_seq;
	ks->nconsumed = 0;
	down_add(e, code);

	struct hit h = {0};
	if (!lookup(e, code, &h)) {
		LOG_DEBUG("%s press: unmapped", grime_key_name(code));
		ks->node = ks->layer = e->stack[e->depth - 1].node;
		pop_prefixes(e); /* like emacs: an undefined key cancels the prefix */
		if (!grime_is_modifier(code))
			end_modes_above(e, 0);
		return false;
	}
	const grime_binding *b = h.b;
	ks->node = h.node;
	ks->layer = e->stack[h.layer].node;
	ks->nconsumed = h.nconsumed;
	for (int i = 0; i < h.nconsumed; i++)
		ks->consumed[i] = h.consumed[i];

	if (b->chord) { /* a modifier: it pushes nothing and uses nothing up */
		ks->nconsumed = 0;
		fire(e, b, code, "press", NULL, 0);
		return true;
	}
	if (!grime_is_modifier(code))
		end_modes_above(e, h.layer);

	if (b->exit == GRIME_EXIT_TOGGLE || b->exit == GRIME_EXIT_MODE) {
		fire(e, b, code, "press", h.consumed, h.nconsumed); /* both ways, so "do" can announce it */
		int on = find_frame(e, b);
		LOG_INFO("%s: %s %s", grime_key_name(code),
			 b->exit == GRIME_EXIT_MODE ? "mode" : "toggle", on > 0 ? "off" : "on");
		if (on > 0)
			remove_frame(e, on);
		else
			push(e, b, code);
		pop_prefixes(e); /* a toggle ends any prefix that led to it */
		return true;
	}
	fire(e, b, code, "press", h.consumed, h.nconsumed);
	if (b->leave && h.layer > 0) {
		remove_frame(e, h.layer);
		pop_prefixes(e);
	} else if (b->then) {
		push(e, b, code);
	} else if (b->action) {
		pop_prefixes(e);
	}
	return true;
}

static bool on_release(grime_engine *e, uint16_t code)
{
	struct keystate *ks = &e->keys[code];
	if (!ks->down)
		return false; /* pressed before we started, or before a reload */
	ks->down = false;
	down_remove(e, code);
	bool alone = ks->press_seq == e->press_seq;

	/* rule 4: leave hold-layers this key entered (and everything above them) */
	for (int d = 1; d < e->depth; d++) {
		if (e->stack[d].key == code && e->stack[d].via->exit == GRIME_EXIT_RELEASE) {
			pop_to(e, d);
			break;
		}
	}

	const grime_binding *b = grime_node_lookup(ks->node, code, GRIME_RELEASE);
	if (!b)
		return false;
	if (b->alone && !alone) {
		LOG_DEBUG("%s release: not alone, skipped", grime_key_name(code));
		return false;
	}
	fire(e, b, code, "release", ks->consumed, ks->nconsumed);
	if (b->then)
		push(e, b, code);
	else if (b->action && ks->layer == e->stack[e->depth - 1].node)
		pop_prefixes(e); /* only if it fired *inside* the prefix */
	return true;
}

bool grime_engine_feed(grime_engine *e, const grime_key_event *ev)
{
	if (ev->code >= GRIME_KEY_COUNT)
		return false;
	switch (ev->edge) {
	case GRIME_PRESS:
		return on_press(e, ev->code);
	case GRIME_RELEASE:
		return on_release(e, ev->code);
	case GRIME_REPEAT:
		break; /* the OS does its own autorepeat for held output keys */
	}
	return false;
}
