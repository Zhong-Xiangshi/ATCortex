#ifndef SEND_MSG_HANDLE_H
#define SEND_MSG_HANDLE_H

#include "include/ATCortex.h"

struct send_task{
    char *data;
    size_t length;
    atc_cmd_response_handler_t response_handler;
    uint32_t timeout;
    uint32_t timestamp;
    
    //同步发送相关
    void *semaphore;
    enum atc_result *sync_send_result;
    char *sync_response_buf;
    size_t *sync_response_length;
};

void send_msg_handle(struct atc_context *context);
enum atc_result send_msg_queue_init(struct atc_context *context);

#endif // SEND_MSG_HANDLE_H