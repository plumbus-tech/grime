#include "spec.h"

#include <stdio.h>

const char *grime_spec_str(json_object *spec, const char *name)
{
	json_object *v;
	if (!json_object_object_get_ex(spec, name, &v) || !json_object_is_type(v, json_type_string))
		return NULL;
	return json_object_get_string(v);
}

const char *grime_spec_str_req(json_object *spec, const char *name, char *err, size_t errlen)
{
	json_object *v;
	if (!json_object_object_get_ex(spec, name, &v)) {
		snprintf(err, errlen, "missing \"%s\"", name);
		return NULL;
	}
	if (!json_object_is_type(v, json_type_string)) {
		snprintf(err, errlen, "\"%s\" must be a string", name);
		return NULL;
	}
	return json_object_get_string(v);
}

json_object *grime_spec_array(json_object *spec, const char *name, char *err, size_t errlen,
			      bool *bad)
{
	json_object *v;
	*bad = false;
	if (!json_object_object_get_ex(spec, name, &v))
		return NULL;
	if (!json_object_is_type(v, json_type_array)) {
		snprintf(err, errlen, "\"%s\" must be an array", name);
		*bad = true;
		return NULL;
	}
	return v;
}
