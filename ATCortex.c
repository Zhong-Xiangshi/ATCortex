#include "include/ATCortex.h"
#include "log.h"
#include "extern_msg_handle.h"
#include "send_msg_handle.h"
#include "urc_handle.h"
#include "recv_data_handle.h"


//检查发送消息是否超时
static void check_send_timeout(struct atc_context *context){
    if(context->current_send_task != NULL){
        uint32_t current_time = _atc_time_get();
        if(current_time - context->current_send_task->timestamp >= context->current_send_task->timeout){
            LOG_WARN("send task timeout:%.*s", context->current_send_task->length, context->current_send_task->data);
            //超时，返回TIMEOUT结果
            command_end_handle(context, ATC_TIMEOUT);
        }
    }
}

enum atc_result atc_init(struct atc_context *context){
    LOG_TRACE;
    //初始化接收处理
    if(recv_data_init(context) != ATC_SUCCESS){
        LOG_ERR("Failed to initialize receive data handler");
        return ATC_ERROR;
    }
    LOG_TRACE;
    //初始化消息队列
    if(extern_msg_queue_init(context)!=ATC_SUCCESS){
        LOG_ERR("Failed to initialize external message queue");
        return ATC_ERROR;
    }
    LOG_TRACE;
    if(send_msg_queue_init(context)!=ATC_SUCCESS){
        LOG_ERR("Failed to initialize send message queue");
        return ATC_ERROR;
    }
    LOG_TRACE;
    if(urc_init(context) != ATC_SUCCESS){
        LOG_ERR("Failed to initialize URC handler list");
        return ATC_ERROR;
    }
    LOG_TRACE;
    //创建唤醒信号量
    context->wake_semaphore = g_atc_interface.atc_semaphore_create_binary();
    if(!context->wake_semaphore){
        LOG_ERR("Failed to create wake semaphore");
        return ATC_ERROR;
    }
    LOG_INFO("init %p success!", context);
    return ATC_SUCCESS;
}
void atc_process(struct atc_context *context){
    for(;;){
        uint32_t wait_ms = ATC_TIMEOUT_MAX;

        //处理"外部API"消息队列
        extern_msg_handle(context);
        //处理"发送"消息队列
        send_msg_handle(context);
        //处理接收缓冲区
        recv_data_handle(context);
        //检查发送消息是否超时
        check_send_timeout(context);

        //计算剩余超时
        if(context->current_send_task != NULL){
            uint32_t elapsed = _atc_time_get() - context->current_send_task->timestamp;
            if(elapsed < context->current_send_task->timeout){
                wait_ms = context->current_send_task->timeout - elapsed;
            }else{
                wait_ms = 0;  //已超时，下一轮立即处理
            }
        }

        g_atc_interface.atc_semaphore_take(context->wake_semaphore, wait_ms);
    }
}

uint32_t _atc_time_get(){
    return g_atc_interface.atc_get_tick_ms();
}