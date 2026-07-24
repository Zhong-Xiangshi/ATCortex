#include "urc_handle.h"
#include "log.h"
#include <limits.h>
#include <string.h>

static void urc_free(void *data){
    if(data) g_atc_interface.atc_free(data);
}

//URC行处理,返回true表示匹配到URC前缀并处理，false表示未匹配到URC前缀
bool urc_line_handle(struct atc_context *context, const char *line_data){
    bool is_urc = false;
    if(line_data == NULL){
        return false;
    }
    LOG_TRACE;
    //遍历URC处理链表
    slist_node_t *node;
    SLIST_FOREACH(node, context->urc_handler_list){
        struct urc_handler_entry *entry = (struct urc_handler_entry *)node->data;
        LOG_DEBUG("Checking URC handler id:%d, prefix:%s, line_data:%s", entry->id, entry->prefix, line_data);
        if(entry && strncmp(line_data, entry->prefix, strlen(entry->prefix)) == 0){
            //匹配到URC前缀，调用处理函数
            LOG_DEBUG("Match id:%d",entry->id);
            if(entry->handler){
                entry->handler(context, line_data);
            }
            is_urc = true;
        }
    }
    return is_urc;
}

enum atc_result urc_init(struct atc_context *context){
    //初始化URC链表
    if(context->urc_handler_list == NULL){
        context->urc_handler_list = slist_create(urc_free);
        if(context->urc_handler_list == NULL){
            LOG_ERR("Failed to create urc handler list");
            return ATC_ERROR;
        }
        context->urc_next_id = 1; // ID从1开始分配
    }

    return ATC_SUCCESS;
}
int _atc_urc_register(struct atc_context *context , struct urc_handler_entry *entry){
    if(context->urc_handler_list == NULL){
        LOG_ERR("urc_handler_list is null!");
        return -1;
    }
    if(entry == NULL){
        LOG_ERR("entry is null!");
        return -1;
    }
    // 分配未被占用的ID：跳过0，避开链表中已有ID
    int candidate = context->urc_next_id;
    if(candidate == 0) candidate = 1;
    int collision;
    do {
        collision = 0;
        slist_node_t *node;
        SLIST_FOREACH(node, context->urc_handler_list){
            struct urc_handler_entry *e = (struct urc_handler_entry *)node->data;
            if(e && e->id == candidate){
                collision = 1;
                // 安全递增，避免有符号溢出，到INT_MAX时回绕到1
                if(candidate < INT_MAX) candidate++;
                else candidate = 1;
                if(candidate == 0) candidate = 1; // 跳过0
                break;
            }
        }
    } while(collision);

    int assigned_id = candidate;
    // 安全递增，避免有符号溢出，到INT_MAX时回绕到1
    context->urc_next_id = (candidate < INT_MAX) ? candidate + 1 : 1;

    LOG_DEBUG("prefix:%s, register urc handler, id:%d", entry->prefix, assigned_id);

    struct urc_handler_entry *tmp = g_atc_interface.atc_malloc(sizeof(struct urc_handler_entry));
    if(tmp == NULL){
        LOG_ERR("Failed to allocate memory for urc_handler_entry");
        return -1;
    }
    *tmp = *entry;
    tmp->id = assigned_id; // 填入分配的ID
    if(slist_append(context->urc_handler_list, tmp) != 0){
        LOG_ERR("Failed to append urc handler entry to list");
        g_atc_interface.atc_free(tmp);
        return -1;
    }
    return assigned_id;
}

enum atc_result _atc_urc_unregister(struct atc_context *context, int id){
    if(context->urc_handler_list == NULL){
        LOG_ERR("urc_handler_list is null!");
        return ATC_ERROR;
    }

    slist_node_t *node;
    SLIST_FOREACH(node, context->urc_handler_list){
        struct urc_handler_entry *entry = (struct urc_handler_entry *)node->data;
        if(entry && entry->id == id){
            // slist_remove 按 data 指针匹配，会调用 free_fn 释放数据，立即 return 安全
            slist_remove(context->urc_handler_list, node->data);
            LOG_DEBUG("Unregistered URC handler id:%d", id);
            return ATC_SUCCESS;
        }
    }

    LOG_WARN("URC handler with id %d not found", id);
    return ATC_ERROR;
}