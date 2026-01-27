#ifndef URC_HANDLE_H
#define URC_HANDLE_H
#include "include/ATCortex.h"

struct urc_handler_entry{
    char prefix[32];
    atc_urc_handler_t handler;
};
enum atc_result urc_init(struct atc_context *context);
enum atc_result _atc_urc_register(struct atc_context *context , struct urc_handler_entry *entry);
void urc_line_handle(struct atc_context *context, const char *line_data);

#endif // URC_HANDLE_H