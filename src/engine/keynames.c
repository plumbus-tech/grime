#include "grime/keynames.h"

#include <string.h>

#include "grime/event.h"

struct keyname {
	const char *name;
	int code;
};

static const struct keyname table[] = {
#include "keynames_table.inc"
};

/* Friendlier spellings; the canonical evdev name always works too. */
static const struct keyname aliases[] = {
	{"shift", 42},     {"ctrl", 29},      {"control", 29},  {"alt", 56},
	{"super", 125},    {"meta", 125},     {"win", 125},     {"escape", 1},
	{"return", 28},    {"del", 111},      {"ins", 110},     {"pgup", 104},
	{"pgdn", 109},     {"caps", 58},      {"rightsuper", 126}, {"leftsuper", 125},
};

#define N(a) (sizeof(a) / sizeof((a)[0]))

int grime_key_from_name(const char *name)
{
	for (size_t i = 0; i < N(table); i++)
		if (!strcmp(table[i].name, name))
			return table[i].code;
	for (size_t i = 0; i < N(aliases); i++)
		if (!strcmp(aliases[i].name, name))
			return aliases[i].code;
	return -1;
}

const char *grime_key_name(int code)
{
	for (size_t i = 0; i < N(table); i++)
		if (table[i].code == code)
			return table[i].name;
	return "?";
}

void grime_key_list(FILE *f)
{
	for (size_t i = 0; i < N(table); i++)
		fprintf(f, "%-24s %d\n", table[i].name, table[i].code);
	for (size_t i = 0; i < N(aliases); i++)
		fprintf(f, "%-24s %d (alias)\n", aliases[i].name, aliases[i].code);
}
