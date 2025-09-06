/* core/at_log.c */
#include "at_log.h"

#if AT_ENABLE_LOG
void at_log_print(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf("[AT] ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}
#endif
