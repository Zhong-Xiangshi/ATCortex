#ifndef SEND_MSG_HANDLE_H
#define SEND_MSG_HANDLE_H

#include "include/ATCortex.h"
//当前任务状态
enum send_task_status{
    SEND_TASK_STATUS_LINE_RECV = 0,   //行接收中
    SEND_TASK_STATUS_PROMPT,    //提示符匹配中
    SEND_TASK_STATUS_BINARY, //接收二进制数据中
};

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

    //二进制数据接收相关
    char *prompt;
    size_t prompt_len;
    size_t need_recv_len;   //需要接收的二进制数据长度

    enum send_task_status status;
};

void send_msg_handle(struct atc_context *context);
enum atc_result send_msg_queue_init(struct atc_context *context);

#endif // SEND_MSG_HANDLE_H