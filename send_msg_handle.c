/**
 * @Author: ZXS
 * @Date: 2026-01-26 16:43:05
 * @Description: 外部消息处理模块
 */

#include "send_msg_handle.h"
#include "include/ATCortex.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include "recv_data_handle.h"
#include <ctype.h>

enum atc_result atc_send_async(struct atc_context *context, const char *data, size_t length, atc_cmd_response_handler_t response_handler,uint32_t timeout){
    if(context == NULL || data == NULL || length == 0){
        return ATC_ERROR;
    }
    //分配内存
    struct send_task task;
    task.data = g_atc_interface.atc_malloc(length);
    if(task.data == NULL){
        LOG_ERR("Failed to allocate memory for send_task data");
        g_atc_interface.atc_free(task.data);
        return ATC_ERROR;
    }
    memcpy(task.data, data, length);
    task.length = length;
    task.response_handler = response_handler;
    task.timeout = timeout;
    task.timestamp = 0; //初始化时间戳
    //发送到“发送消息队列”
    enum atc_result ret=g_atc_interface.atc_queue_send(context->send_queue, &task, 1000);
    if(ret!=ATC_SUCCESS){
        g_atc_interface.atc_free(task.data);
        LOG_ERR("Failed to send message to send queue");
        return ATC_ERROR;
    }
    return ATC_SUCCESS;
}

enum atc_result send_msg_queue_init(struct atc_context *context){
    context->send_queue = g_atc_interface.atc_queue_create(6, sizeof(struct send_task));
    if(context->send_queue == NULL){
        LOG_ERR("Failed to create send queue");
        return ATC_ERROR;
    }
    return ATC_SUCCESS;
}

void send_msg_handle(struct atc_context *context){
    struct send_task rtask = {0};
    if(context->current_send_task != NULL){
        //如果有当前发送任务，不处理新的发送任务
        return;
    }
    if(g_atc_interface.atc_queue_recv(context->send_queue, &rtask, 0) == ATC_SUCCESS){
        //清空响应缓冲区
        clear_response_buffer(context);
        //记录发送时间
        rtask.timestamp = _atc_time_get();
#if LOG_LEVEL >= LOG_LEVEL_DEBUG
        g_atc_interface.atc_log(DBG_NAME"[SEND]:");
        for(size_t i = 0; i < rtask.length; i++){
            if(isprint((int)rtask.data[i])){
                g_atc_interface.atc_log("%c", rtask.data[i]);
            }
            else{
                g_atc_interface.atc_log("[0x%02X]", (unsigned char)rtask.data[i]);
            }
        }
        g_atc_interface.atc_log("\r\n");
#endif
        //发送数据
        enum atc_result send_ret = g_atc_interface.atc_send(context, rtask.data, rtask.length);
        //发送后释放发送字符串内存
        g_atc_interface.atc_free(rtask.data); 
        rtask.data = NULL;
        if(send_ret != ATC_SUCCESS){
            //处理硬件发送失败
            LOG_ERR("Failed to send AT command");
            //调用响应处理回调，通知发送失败
            if(rtask.response_handler){
                rtask.response_handler(context, ATC_ERROR, 0, 0);
            }
            //直接返回，不记录当前发送任务
            return;
        }
        //记录当前发送任务
        context->current_send_task = g_atc_interface.atc_malloc(sizeof(struct send_task));
        if(context->current_send_task == NULL){
            LOG_ERR("Failed to allocate memory for current_send_task");
            return;
        }
        *(context->current_send_task) = rtask;
    }
}