/* keynames.h — "capslock" <-> 58. Names are evdev KEY_* names lowercased
 * without the prefix, plus a few aliases (shift, ctrl, alt, super, ...). */
#ifndef GRIME_KEYNAMES_H
#define GRIME_KEYNAMES_H

#include <stdio.h>

int grime_key_from_name(const char *name); /* -1 if unknown */
const char *grime_key_name(int code);      /* "?" if unknown */
void grime_key_list(FILE *f);

#endif
