/* Internal to src/actions: ${VAR} expansion from the environment at run time. */
#ifndef GRIME_ACTIONS_EXPAND_H
#define GRIME_ACTIONS_EXPAND_H

char *grime_expand_env(const char *s); /* malloc'd; unset vars expand to "" */

#endif
