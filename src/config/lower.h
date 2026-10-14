/* lower.h -- the config front-end: friendly syntax -> canonical keymap tree.
 *
 * Users write slots ("tap", "hold", "prefix", "toggle", "pass"), key paths
 * ("rightalt x f"), chords ("ctrl+f") and named layers. The tree parser in
 * config.c only ever sees the canonical form:
 *
 *   {"keymap": {"<key>": {"press":   {"do":…, "then":{…}, "exit":…, "fallthrough":…},
 *                         "release": {"do":…, "alone":…}}},
 *    "layers": {"<name>": {…}}}
 *
 * A named layer stays a name ("then": "<name>"), lowered once under "layers",
 * so every use of it is the same layer and it may lead back to itself.
 *
 * `grime --expand` prints the result, so the sugar is never a black box. */
#ifndef GRIME_LOWER_H
#define GRIME_LOWER_H

#include <json-c/json.h>
#include <stddef.h>

/* Returns a new root object (caller json_object_put()s it), or NULL with err
 * set. `root` is left untouched. `path` is the file it came from, which is what
 * relative "include" paths resolve against; NULL means the current directory. */
json_object *grime_config_lower(json_object *root, const char *path, char *err, size_t errlen);

#endif
