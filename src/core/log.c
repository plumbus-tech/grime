#include "grime/log.h"

#include <stdarg.h>
#include <stdio.h>

#include "grime/loop.h"

int grime_log_level = GRIME_LOG_INFO;

void grime_log(int level, const char *fmt, ...)
{
	static const char *tags[] = {"error", "warn", "info", "debug"};
	if (level > grime_log_level)
		return;
	uint64_t t = grime_now_ms();
	fprintf(stderr, "[%5llu.%03llu] %-5s ", (unsigned long long)(t / 1000 % 100000),
		(unsigned long long)(t % 1000), tags[level]);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}
