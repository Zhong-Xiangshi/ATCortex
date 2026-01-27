#ifndef SEND_MSG_HANDLE_H
#define SEND_MSG_HANDLE_H

#include "include/ATCortex.h"

struct send_task{
    char *data;
    size_t length;
    atc_cmd_response_handler_t response_handler;
    uint32_t timeout;
    uint32_t timestamp;
};

void send_msg_handle(struct atc_context *context);
enum atc_result send_msg_queue_init(struct atc_context *context);

#endif // SEND_MSG_HANDLE_H