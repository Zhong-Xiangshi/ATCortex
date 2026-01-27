#include "urc_handle.h"
#include "log.h"
#include <string.h>

static void urc_free(void *data){
    if(data) g_atc_interface.atc_free(data);
}

//URC行处理
void urc_line_handle(struct atc_context *context, const char *line_data){
    if(line_data == NULL){
        return;
    }
    LOG_TRACE;
    //遍历URC处理链表
    slist_node_t *node;
    SLIST_FOREACH(node, context->urc_handler_list){
        struct urc_handler_entry *entry = (struct urc_handler_entry *)node->data;
        if(entry && strncmp(line_data + 1, entry->prefix, strlen(entry->prefix)) == 0){
            //匹配到URC前缀，调用处理函数
            if(entry->handler){
                entry->handler(context, line_data);
            }
            return; //找到一个匹配项后退出
        }
    }
}

enum atc_result urc_init(struct atc_context *context){
    //初始化URC链表
    if(context->urc_handler_list == NULL){
        context->urc_handler_list = slist_create(urc_free);
        if(context->urc_handler_list == NULL){
            LOG_ERR("Failed to create urc handler list");
            return ATC_ERROR;
        }
    }

    return ATC_SUCCESS;
}
enum atc_result _atc_urc_register(struct atc_context *context , struct urc_handler_entry *entry){
    if(context->urc_handler_list == NULL){
        LOG_ERR("urc_handler_list is null!");
        return ATC_ERROR;
    }
    LOG_DEBUG("prefix:%s, register urc handler", entry->prefix);
    struct urc_handler_entry *tmp = g_atc_interface.atc_malloc(sizeof(struct urc_handler_entry));
    if(tmp == NULL){
        LOG_ERR("Failed to allocate memory for urc_handler_entry");
        return ATC_ERROR;
    }
    *tmp = *entry;
    if(slist_append(context->urc_handler_list, tmp) != 0){
        LOG_ERR("Failed to append urc handler entry to list");
        return ATC_ERROR;
    }
    return ATC_SUCCESS;
}