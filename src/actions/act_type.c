/* type: type a string (US QWERTY, ASCII only for now).
 *   {"do": "type", "text": "hello, world\n"} */
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "grime/action.h"
#include "grime/keynames.h"

#define SHIFT 0x8000

/* ASCII 0x20..0x7e -> key code (| SHIFT) */
static const char *const unshifted = " `1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./";
static const char *const shifted = " ~!@#$%^&*()_+QWERTYUIOP{}|ASDFGHJKL:\"ZXCVBNM<>?";
static const char *const names[] = {
	"space", "grave", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "minus", "equal",
	"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "leftbrace", "rightbrace", "backslash",
	"a", "s", "d", "f", "g", "h", "j", "k", "l", "semicolon", "apostrophe",
	"z", "x", "c", "v", "b", "n", "m", "comma", "dot", "slash",
};

static int char_to_key(char c)
{
	if (c == '\n')
		return grime_key_from_name("enter");
	if (c == '\t')
		return grime_key_from_name("tab");
	const char *p;
	if ((p = strchr(unshifted, c)) && c)
		return grime_key_from_name(names[p - unshifted]);
	if ((p = strchr(shifted, c)) && c)
		return grime_key_from_name(names[p - shifted]) | SHIFT;
	return -1;
}

struct type_state {
	int n;
	int keys[];
};

static int compile(json_object *spec, void **out, char *err, size_t errlen)
{
	json_object *v;
	if (!json_object_object_get_ex(spec, "text", &v)) {
		snprintf(err, errlen, "missing \"text\"");
		return -1;
	}
	const char *text = json_object_get_string(v);
	size_t n = strlen(text);
	struct type_state *s = malloc(sizeof *s + n * sizeof(int));
	s->n = n;
	for (size_t i = 0; i < n; i++) {
		if ((s->keys[i] = char_to_key(text[i])) < 0) {
			snprintf(err, errlen, "can't type character 0x%02x", (unsigned char)text[i]);
			free(s);
			return -1;
		}
	}
	*out = s;
	return 0;
}

static void run(grime_runtime *rt, void *state)
{
	struct type_state *s = state;
	int shift = grime_key_from_name("leftshift");
	for (int i = 0; i < s->n; i++) {
		int k = s->keys[i];
		if (k & SHIFT)
			grime_emit(rt, shift, 1);
		grime_emit(rt, k & ~SHIFT, 1);
		grime_emit(rt, k & ~SHIFT, 0);
		if (k & SHIFT)
			grime_emit(rt, shift, 0);
	}
}

static const grime_action_type type_action = {"type", compile, run, free};
GRIME_REGISTER_ACTION(type_action)
