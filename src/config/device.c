/* Device matching policy. Deliberately free of Linux headers and ioctls: the
 * backend probes a device into a grime_device_info, this decides what happens
 * to it, and tests/test_devmatch.c can exercise every rule with no /dev at all.
 * It is also what --list-devices calls, so the preview can never disagree with
 * what the daemon does. */
#include "grime/device.h"

#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>

static bool glob_matches(const char *pattern, const char *value)
{
	if (!pattern)
		return true; /* field not set: don't care */
	if (!value)
		return false;
	return fnmatch(pattern, value, 0) == 0;
}

/* A path matches the event node itself or any symlink pointing at it, so
 * a by-id glob works even though we walk the event nodes themselves. */
static bool path_matches(const grime_device_match *m, const grime_device_info *info)
{
	if (!m->path)
		return true;
	if (glob_matches(m->path, info->path))
		return true;
	for (size_t i = 0; i < info->nlinks; i++)
		if (glob_matches(m->path, info->links[i]))
			return true;
	return false;
}

static bool id_matches(int want, int got)
{
	return want < 0 || want == got;
}

static bool one_matches(const grime_device_match *m, const grime_device_info *info)
{
	if (m->kind != GRIME_KIND_ANY && m->kind != info->kind)
		return false;
	return path_matches(m, info) && glob_matches(m->name, info->name) &&
	       glob_matches(m->phys, info->phys) && glob_matches(m->uniq, info->uniq) &&
	       id_matches(m->vendor, info->vendor) && id_matches(m->product, info->product) &&
	       id_matches(m->bus, info->bus);
}

int grime_device_match_find(const grime_device_match *m, size_t n,
			    const grime_device_info *info)
{
	for (size_t i = 0; i < n; i++)
		if (one_matches(&m[i], info))
			return (int)i;
	return -1;
}

static char *dup_or_null(const char *s)
{
	return s ? strdup(s) : NULL;
}

grime_device_match *grime_device_match_dup(const grime_device_match *m, size_t n)
{
	grime_device_match *out = calloc(n ? n : 1, sizeof *out);
	if (!out)
		return NULL;
	for (size_t i = 0; i < n; i++) {
		out[i] = m[i];
		out[i].path = dup_or_null(m[i].path);
		out[i].name = dup_or_null(m[i].name);
		out[i].phys = dup_or_null(m[i].phys);
		out[i].uniq = dup_or_null(m[i].uniq);
	}
	return out;
}

void grime_device_match_free(grime_device_match *m, size_t n)
{
	if (!m)
		return;
	for (size_t i = 0; i < n; i++) {
		free(m[i].path);
		free(m[i].name);
		free(m[i].phys);
		free(m[i].uniq);
	}
	free(m);
}

static const char *const kind_names[] = {"any", "keyboard", "keys", "pointer"};

const char *grime_device_kind_name(grime_device_kind k)
{
	if (k < 0 || (size_t)k >= sizeof kind_names / sizeof kind_names[0])
		return "?";
	return kind_names[k];
}

int grime_device_kind_from_name(const char *s)
{
	for (size_t i = 0; i < sizeof kind_names / sizeof kind_names[0]; i++)
		if (!strcmp(s, kind_names[i]))
			return (int)i;
	return -1;
}
