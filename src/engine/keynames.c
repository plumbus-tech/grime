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
	/* Mouse buttons. Deliberately no "mouse1"/"mouse2"/"mouse3": X11 numbers
	 * them 1=left 2=middle 3=right and evdev orders them left, right, middle,
	 * so any number is wrong for half the people reading it. */
	{"click", 0x110},     {"leftclick", 0x110}, {"rightclick", 0x111},
	{"middleclick", 0x112}, {"sideclick", 0x113}, {"extraclick", 0x114},
	{"forwardclick", 0x115}, {"backclick", 0x116},
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
