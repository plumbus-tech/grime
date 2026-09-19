/* log.h — stderr logging. */
#ifndef GRIME_LOG_H
#define GRIME_LOG_H

enum { GRIME_LOG_ERROR, GRIME_LOG_WARN, GRIME_LOG_INFO, GRIME_LOG_DEBUG };

extern int grime_log_level;

void grime_log(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#define LOG_ERR(...) grime_log(GRIME_LOG_ERROR, __VA_ARGS__)
#define LOG_WARN(...) grime_log(GRIME_LOG_WARN, __VA_ARGS__)
#define LOG_INFO(...) grime_log(GRIME_LOG_INFO, __VA_ARGS__)
#define LOG_DEBUG(...) grime_log(GRIME_LOG_DEBUG, __VA_ARGS__)

#endif
