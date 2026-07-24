#ifndef URC_HANDLE_H
#define URC_HANDLE_H
#include "include/ATCortex.h"

struct urc_handler_entry{
    int id;                     // 注册ID，由_atc_urc_register分配
    char prefix[32];
    atc_urc_handler_t handler;
};
enum atc_result urc_init(struct atc_context *context);
int _atc_urc_register(struct atc_context *context , struct urc_handler_entry *entry);
enum atc_result _atc_urc_unregister(struct atc_context *context, int id);
bool urc_line_handle(struct atc_context *context, const char *line_data);

#endif // URC_HANDLE_H