#include "include/ATCortex.h"
#include "log.h"
#include "extern_msg_handle.h"
#include "send_msg_handle.h"
#include "urc_handle.h"
#include "recv_data_handle.h"


static slist_t *atc_ctx_list=NULL;
static uint32_t atc_time=0;

//检查发送消息是否超时
static void check_send_timeout(struct atc_context *context){
    if(context->current_send_task != NULL){
        uint32_t current_time = _atc_time_get();
        if(current_time - context->current_send_task->timestamp >= context->current_send_task->timeout){
            LOG_WARN("Current send task timeout");
            //超时，返回TIMEOUT结果
            command_end_handle(context, ATC_TIMEOUT);
        }
    }
}

enum atc_result atc_init(struct atc_context *context){
    LOG_TRACE;
    //初始化链表
    if(!atc_ctx_list){
        atc_ctx_list = slist_create(NULL);
        if(!atc_ctx_list){
            LOG_ERR("Failed to create atc context list");
            return ATC_ERROR;
        }
    }
    //初始化接收处理
    if(recv_data_init(context) != ATC_SUCCESS){
        LOG_ERR("Failed to initialize receive data handler");
        return ATC_ERROR;
    }
    //初始化消息队列
    if(extern_msg_queue_init(context)!=ATC_SUCCESS){
        LOG_ERR("Failed to initialize external message queue");
        return ATC_ERROR;
    }
    if(send_msg_queue_init(context)!=ATC_SUCCESS){
        LOG_ERR("Failed to initialize send message queue");
        return ATC_ERROR;
    }
    if(urc_init(context) != ATC_SUCCESS){
        LOG_ERR("Failed to initialize URC handler list");
        return ATC_ERROR;
    }
    //将context加入链表
    if(slist_append(atc_ctx_list, context) != 0){
        LOG_ERR("Failed to append context to list");
        return ATC_ERROR;
    }
    LOG_INFO("init %p success!", context);
    return ATC_SUCCESS;
}
void atc_process(uint32_t ms_elapsed){
    atc_time += ms_elapsed;
    slist_node_t *node;
    //遍历atc实例
    SLIST_FOREACH(node, atc_ctx_list){
        struct atc_context *context = (struct atc_context *)node->data;
        if(context==NULL){
            LOG_WARN("context is NULL, skipping");
            continue;
        }
        //处理“外部API”消息队列
        extern_msg_handle(context);
        //处理“发送”消息队列
        send_msg_handle(context);
        //处理接收缓冲区
        recv_data_handle(context);
        //检查发送消息是否超时
        check_send_timeout(context);
    }

}

uint32_t _atc_time_get(){
    return atc_time;
}