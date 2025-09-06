/* core/at_log.h */
#ifndef AT_LOG_H
#define AT_LOG_H

#include <stdio.h>
#include <stdarg.h>

#ifndef AT_ENABLE_LOG
#define AT_ENABLE_LOG 0
#endif

#if AT_ENABLE_LOG
void at_log_print(const char *fmt, ...);
#define AT_LOG(fmt, ...) at_log_print(fmt, ##__VA_ARGS__)
#else
#define AT_LOG(fmt, ...) (void)0
#endif

#endif /* AT_LOG_H */
