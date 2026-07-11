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
    MSG_TYPE_URC_UNREGISTER,
};

struct msg{
    enum msg_type type;
    void *data;
    void (*free_fn)(void *data);
    void *semaphore;             // 通用同步信号量，NULL=异步
};

// 注册消息（通过 void *data 携带，堆分配）
struct urc_register_msg {
    struct urc_handler_entry entry;  // prefix + handler
    int *out_id;                     // 指向调用者栈上变量，事件循环填入分配的ID
};

// 反注册消息（通过 void *data 携带，堆分配）
struct urc_unregister_msg {
    int id;                          // 要移除的ID
    enum atc_result *out_result;     // 指向调用者栈上变量，事件循环填入结果
};

static void msg_free(void *data){
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

int atc_urc_register(struct atc_context *context , const char *prefix, atc_urc_handler_t handler){
    if(context == NULL || prefix == NULL || handler == NULL){
        return -1;
    }
    if(prefix[0] == '\0'){
        LOG_ERR("URC prefix is empty");
        return -1;
    }
    // 检查信号量函数是否实现
    if(g_atc_interface.atc_semaphore_create_binary == NULL || g_atc_interface.atc_semaphore_give == NULL
        || g_atc_interface.atc_semaphore_take == NULL || g_atc_interface.atc_semaphore_delete == NULL){
        LOG_ERR("Semaphore functions are not implemented");
        return -1;
    }
    // 创建注册消息
    struct urc_register_msg *reg_msg = g_atc_interface.atc_malloc(sizeof(struct urc_register_msg));
    if(reg_msg == NULL){
        LOG_ERR("Failed to allocate memory for urc_register_msg");
        return -1;
    }
    int count = snprintf(reg_msg->entry.prefix, sizeof(reg_msg->entry.prefix), "%s", prefix);
    if(count < 0 || count >= sizeof(reg_msg->entry.prefix)){
        LOG_ERR("URC prefix too long");
        g_atc_interface.atc_free(reg_msg);
        return -1;
    }
    reg_msg->entry.handler = handler;
    // id 由事件循环分配
    int assigned_id = -1;
    reg_msg->out_id = &assigned_id;

    // 创建同步信号量
    void *sem = g_atc_interface.atc_semaphore_create_binary();
    if(sem == NULL){
        LOG_ERR("Failed to create semaphore for sync urc register");
        g_atc_interface.atc_free(reg_msg);
        return -1;
    }

    struct msg msg = {
        .type = MSG_TYPE_URC_REGISTER,
        .data = reg_msg,
        .free_fn = msg_free,
        .semaphore = sem,
    };

    enum atc_result ret = g_atc_interface.atc_queue_send(context->external_api_queue, &msg, 1000);
    if(ret != ATC_SUCCESS){
        g_atc_interface.atc_free(reg_msg);
        g_atc_interface.atc_semaphore_delete(sem);
        LOG_ERR("Failed to send urc register message to external api queue");
        return -1;
    }
    // 唤醒阻塞等待的处理线程
    g_atc_interface.atc_semaphore_give(context->wake_semaphore);
    // 阻塞等待事件循环处理完成
    g_atc_interface.atc_semaphore_take(sem, ATC_TIMEOUT_MAX);
    // 清理
    g_atc_interface.atc_semaphore_delete(sem);

    return assigned_id;
}

enum atc_result atc_urc_unregister(struct atc_context *context, int id){
    if(context == NULL || id <= 0){
        return ATC_ERROR;
    }
    // 检查信号量函数是否实现
    if(g_atc_interface.atc_semaphore_create_binary == NULL || g_atc_interface.atc_semaphore_give == NULL
        || g_atc_interface.atc_semaphore_take == NULL || g_atc_interface.atc_semaphore_delete == NULL){
        LOG_ERR("Semaphore functions are not implemented");
        return ATC_ERROR;
    }
    // 创建反注册消息
    struct urc_unregister_msg *unreg_msg = g_atc_interface.atc_malloc(sizeof(struct urc_unregister_msg));
    if(unreg_msg == NULL){
        LOG_ERR("Failed to allocate memory for urc_unregister_msg");
        return ATC_ERROR;
    }
    unreg_msg->id = id;

    enum atc_result result = ATC_ERROR;
    unreg_msg->out_result = &result;

    // 创建同步信号量
    void *sem = g_atc_interface.atc_semaphore_create_binary();
    if(sem == NULL){
        LOG_ERR("Failed to create semaphore for sync urc unregister");
        g_atc_interface.atc_free(unreg_msg);
        return ATC_ERROR;
    }

    struct msg msg = {
        .type = MSG_TYPE_URC_UNREGISTER,
        .data = unreg_msg,
        .free_fn = msg_free,
        .semaphore = sem,
    };

    enum atc_result ret = g_atc_interface.atc_queue_send(context->external_api_queue, &msg, 1000);
    if(ret != ATC_SUCCESS){
        g_atc_interface.atc_free(unreg_msg);
        g_atc_interface.atc_semaphore_delete(sem);
        LOG_ERR("Failed to send urc unregister message to external api queue");
        return ATC_ERROR;
    }
    // 唤醒阻塞等待的处理线程
    g_atc_interface.atc_semaphore_give(context->wake_semaphore);
    // 阻塞等待事件循环处理完成
    g_atc_interface.atc_semaphore_take(sem, ATC_TIMEOUT_MAX);
    // 清理
    g_atc_interface.atc_semaphore_delete(sem);

    return result;
}

void extern_msg_handle(struct atc_context *context){
    struct msg rmsg;
    while(g_atc_interface.atc_queue_recv(context->external_api_queue, &rmsg, 0) == ATC_SUCCESS){
        LOG_DEBUG("received api msg type:%d", rmsg.type);
        switch(rmsg.type){
            case MSG_TYPE_URC_REGISTER:{
                struct urc_register_msg *m = (struct urc_register_msg *)rmsg.data;
                int assigned_id = -1;
                if(m){
                    assigned_id = _atc_urc_register(context, &m->entry);
                    if(m->out_id) *m->out_id = assigned_id;
                }
                break;
            }
            case MSG_TYPE_URC_UNREGISTER:{
                struct urc_unregister_msg *m = (struct urc_unregister_msg *)rmsg.data;
                enum atc_result result = ATC_ERROR;
                if(m){
                    result = _atc_urc_unregister(context, m->id);
                    if(m->out_result) *m->out_result = result;
                }
                break;
            }
            default:
                LOG_ERR("Unknown message type: %d", rmsg.type);
                break;
        }
        if(rmsg.semaphore){
            g_atc_interface.atc_semaphore_give(rmsg.semaphore);
        }
        // 释放消息数据
        if(rmsg.free_fn && rmsg.data){
            rmsg.free_fn(rmsg.data);
        }
    }
}
