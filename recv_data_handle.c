#include "recv_data_handle.h"
#include "log.h"
#include "urc_handle.h"
#include <string.h>
#include "send_msg_handle.h"
#include <stdbool.h>
#include <ctype.h>

//命令结束符数组
static const char *command_end_markers[] = {
    "OK",
    "ERROR",
    NULL
};

//响应缓冲区清空
void clear_response_buffer(struct atc_context *context){
    memset(context->response, 0, sizeof(context->response));
    context->response_length = 0;
}
//推入数据到响应缓冲区
static void push_to_response_buffer(struct atc_context *context, const char *line_data ,size_t length){
    size_t current_length = context->response_length;
    if(current_length + length < ATC_RX_RESPONSE_MAX){
        strncat(context->response, line_data, length);
        context->response_length += length;
    }
    else{
        LOG_ERR("Response buffer overflow, cannot push more data");
    }
}

//命令结束处理函数
void command_end_handle(struct atc_context *context, enum atc_result result){
    if(context->current_send_task != NULL){
        LOG_DEBUG("Response result: %d", result);
        //打印所有响应
        if(context->response_length > 0)LOG_DEBUG("response:\r\n%s", context->response);
        //调用响应处理回调
        if(context->current_send_task->response_handler){
            context->current_send_task->response_handler(context, result, context->response, context->response_length);
        }
        else{
            LOG_WARN("No response handler for current send task");
        }
        //释放当前发送任务内存
        g_atc_interface.atc_free(context->current_send_task);
        context->current_send_task = NULL;
    }
    //清除响应缓冲区
    clear_response_buffer(context);
}

//普通行处理
static void normal_line_handle(struct atc_context *context, const char *line_data ,size_t length){
    LOG_TRACE;
    //推入响应缓冲区
    push_to_response_buffer(context, line_data, length);
    //检查最新一行是否为命令结束符
    for(int i = 0; command_end_markers[i] != NULL; i++){
        size_t marker_length = strlen(command_end_markers[i]);
        if(length >= marker_length && strncmp(line_data, command_end_markers[i], marker_length) == 0){
            //命令结束符匹配，处理命令结束
            bool is_ok = (strcmp(command_end_markers[i], "OK") == 0);
            command_end_handle(context, is_ok ? ATC_SUCCESS : ATC_ERROR);
            break;
        }
    }
}

//行处理函数,行包含\r\n
static void line_handle(struct atc_context *context, const char *line_data ,size_t length){
    if(line_data == NULL){
        return;
    }
    LOG_INFO("Received line: %s", line_data);
    if(length <= 2){
        //空行，忽略
        return;
    }
    if(line_data[0] == '+'){
        //URC处理
        urc_line_handle(context, line_data);
    }
    else{
        //非URC行处理
        normal_line_handle(context, line_data, length);
    }
}

void recv_data_handle(struct atc_context *context){
    //读取环形缓冲区数据
    unsigned char byte;
    while(ring_buffer_read(&context->rx_buffer, &byte)){
        //放到行缓冲区
        if(context->line_buffer_index < ATC_RX_LINE_MAX_SIZE - 1){  //保留一个字节给字符串结束符
            context->line_buffer[context->line_buffer_index] = (char)byte;
            context->line_buffer_index++;
            //检查是否为行结束符
            if(byte == '\n'){
                //行结束，处理该行数据
                context->line_buffer[context->line_buffer_index] = '\0'; //添加字符串结束符
                line_handle(context, context->line_buffer, context->line_buffer_index);
                context->line_buffer_index = 0; //重置行缓冲区索引
            }
        }
        else{
            //行缓冲区满，丢弃数据
            LOG_WARN("Line buffer overflow, discarding data");
        }
    }
}

int atc_receive_data(struct atc_context *context, const char *data, size_t length){
    if(!context || !data || length == 0)
        return 0;
    int count=0;
#if LOG_LEVEL >= LOG_LEVEL_DEBUG
    g_atc_interface.atc_log(DBG_NAME"[RECV]:");
    for(size_t i = 0; i < length; i++){
        if(isprint((int)data[i])){
            g_atc_interface.atc_log("%c", data[i]);
        }
        else{
            g_atc_interface.atc_log("[0x%02X]", (unsigned char)data[i]);
        }
    }
    g_atc_interface.atc_log("\r\n");
#endif
    for(size_t i = 0; i < length; i++){
        if(!ring_buffer_write(&context->rx_buffer, (unsigned char)data[i])){
            LOG_ERR("Failed to write data to ring buffer, buffer full");
            return count;
        }
        count++;
    }
    return count;
}

enum atc_result recv_data_init(struct atc_context *context){
    int ret=ring_buffer_init(&context->rx_buffer, context->rx_buffer_data, sizeof(context->rx_buffer_data));
    if(ret==0){
        LOG_ERR("Failed to initialize ring buffer");
        return ATC_ERROR;
    }
    return ATC_SUCCESS;
}