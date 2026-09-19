# Sources are globbed: adding a .c file under src/ needs no Makefile change.
CC      ?= cc
PKGS    := json-c libcurl
CFLAGS  ?= -O2 -g
# user CFLAGS (e.g. make CFLAGS=-Werror) are added to, not replacing, what the build needs
GRIME_CFLAGS := -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation -Iinclude $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS))

BUILD   := build
SRCS    := $(shell find src -name '*.c')
MAIN    := src/core/main.c
OBJS    := $(SRCS:%.c=$(BUILD)/%.o)
LIBOBJS := $(filter-out $(BUILD)/$(MAIN:.c=.o),$(OBJS))
TESTS   := $(patsubst tests/%.c,$(BUILD)/tests/%,$(wildcard tests/test_*.c))

all: $(BUILD)/grime

$(BUILD)/grime: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(GRIME_CFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/tests/%: tests/%.c $(LIBOBJS)
	@mkdir -p $(dir $@)
	$(CC) $(GRIME_CFLAGS) $(CFLAGS) -Itests -o $@ $^ $(LDLIBS)

test: $(TESTS)
	@set -e; for t in $(TESTS); do echo "== $$t"; $$t; done

check-configs: $(BUILD)/grime
	@for f in configs/*.json configs/examples/*.json; do $(BUILD)/grime --check -c $$f || exit 1; done

fmt:
	clang-format -i $(SRCS) $(wildcard include/grime/*.h tests/*.c tests/*.h)

clean:
	rm -rf $(BUILD)

.PHONY: all test check-configs fmt clean
-include $(OBJS:.o=.d)
