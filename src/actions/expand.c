#include "expand.h"

#include <stdlib.h>
#include <string.h>

char *grime_expand_env(const char *s)
{
	size_t cap = strlen(s) + 1, len = 0;
	char *out = malloc(cap);
	while (*s) {
		const char *val = NULL;
		size_t vlen = 0, skip = 1;
		const char *end;
		if (s[0] == '$' && s[1] == '{' && (end = strchr(s + 2, '}'))) {
			char name[256];
			size_t n = end - (s + 2);
			if (n < sizeof name) {
				memcpy(name, s + 2, n);
				name[n] = 0;
				val = getenv(name);
				vlen = val ? strlen(val) : 0;
				skip = n + 3;
			}
		}
		if (skip == 1) {
			val = s;
			vlen = 1;
		}
		if (len + vlen + 1 > cap) {
			cap = (len + vlen + 1) * 2;
			out = realloc(out, cap);
		}
		if (vlen)
			memcpy(out + len, val, vlen);
		len += vlen;
		s += skip;
	}
	out[len] = 0;
	return out;
}
