/**
 * @Author: ZXS
 * @Date: 2026-01-26 16:43:05
 * @Description: 外部消息处理模块
 */

#include "extern_msg_handle.h"
#include "include/ATCortex.h"
#include "log.h"
#include <stdio.h>
#include "urc_handle.h"

enum msg_type{
    MSG_TYPE_URC_REGISTER,
};
struct msg{
    enum msg_type type;
    void *data;
    void (*free_fn)(void *data);
};
void msg_free(void *data){
    if(data) g_atc_interface.atc_free(data);
}
enum atc_result extern_msg_queue_init(struct atc_context *context){
    context->external_api_queue = g_atc_interface.atc_queue_create(5, sizeof(struct msg));
    if(context->external_api_queue == NULL){
        LOG_ERR("Failed to create external api queue");
        return ATC_ERROR;
    }
    return ATC_SUCCESS;
}
enum atc_result atc_urc_register(struct atc_context *context , const char *prefix, atc_urc_handler_t handler){
    if(context == NULL || prefix == NULL || handler == NULL){
        return ATC_ERROR;
    }
    //创建新的URC处理条目
    struct urc_handler_entry *entry = g_atc_interface.atc_malloc(sizeof(struct urc_handler_entry));
    if(entry == NULL){
        LOG_ERR("Failed to allocate memory for urc_handler_entry");
        return ATC_ERROR;
    }
    int count = snprintf(entry->prefix, sizeof(entry->prefix),"%s", prefix);
    if(count < 0 || count >= sizeof(entry->prefix)){
        LOG_ERR("URC prefix too long");
        g_atc_interface.atc_free(entry);
        return ATC_ERROR;
    }
    entry->handler = handler;

    struct msg msg={
        .type = MSG_TYPE_URC_REGISTER,
        .data = entry,
        .free_fn = msg_free,
    };

    enum atc_result ret=g_atc_interface.atc_queue_send(context->external_api_queue, &msg, 1000);
    if(ret!=ATC_SUCCESS){
        g_atc_interface.atc_free(entry);
        LOG_ERR("Failed to send urc register message to external api queue");
        return ATC_ERROR;
    }
    return ATC_SUCCESS;
}

void extern_msg_handle(struct atc_context *context){
    struct msg rmsg;
    while(g_atc_interface.atc_queue_recv(context->external_api_queue, &rmsg, 0) == ATC_SUCCESS){
        LOG_DEBUG("received api msg type:%d", rmsg.type);
        switch(rmsg.type){
            case MSG_TYPE_URC_REGISTER:{
                struct urc_handler_entry *entry = (struct urc_handler_entry *)rmsg.data;
                if(entry){
                    _atc_urc_register(context, entry);
                }
                break;
            }
            default:
                LOG_ERR("Unknown message type: %d", rmsg.type);
                break;
        }
        //释放消息数据
        if(rmsg.free_fn){
            rmsg.free_fn(rmsg.data);
        }
    }
}