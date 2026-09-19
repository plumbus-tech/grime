/* Minimal test harness: TEST(name) { CHECK(...); }  then RUN(name) in main. */
#ifndef TINYTEST_H
#define TINYTEST_H

#include <stdio.h>

static int tt_failures, tt_checks;

#define CHECK(cond)                                                                         \
	do {                                                                                \
		tt_checks++;                                                                \
		if (!(cond)) {                                                              \
			tt_failures++;                                                      \
			fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
		}                                                                           \
	} while (0)

#define TEST(name) static void name(void)
#define RUN(name)                                                                           \
	do {                                                                                \
		int before = tt_failures;                                                   \
		name();                                                                     \
		printf("%s %s\n", tt_failures == before ? "ok  " : "FAIL", #name);          \
	} while (0)
#define DONE() (printf("%d checks, %d failures\n", tt_checks, tt_failures), tt_failures != 0)

#endif
