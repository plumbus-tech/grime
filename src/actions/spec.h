/* Internal to src/actions: reading action parameters without trusting the JSON.
 *
 * json-c hands back a NULL pointer for a JSON null and happily stringifies a
 * number or an object, so every parameter goes through one of these. */
#ifndef GRIME_ACTIONS_SPEC_H
#define GRIME_ACTIONS_SPEC_H

#include <json-c/json.h>
#include <stdbool.h>
#include <stddef.h>

/* A string parameter, or NULL if it is missing or is not a string. */
const char *grime_spec_str(json_object *spec, const char *name);
/* Like the above, but writes "missing \"name\"" / "\"name\" must be a string". */
const char *grime_spec_str_req(json_object *spec, const char *name, char *err, size_t errlen);
/* An array parameter, or NULL if missing. Sets err if present but not an array. */
json_object *grime_spec_array(json_object *spec, const char *name, char *err, size_t errlen,
			      bool *bad);

#endif
