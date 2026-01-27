//slist.c
#include "interface.h"
#include "slist.h"

#define SLIST_MALLOC g_atc_interface.atc_malloc
#define SLIST_FREE  g_atc_interface.atc_free
/* 
 * 内存管理宏定义
 * 定义 SLIST_MALLOC 和 SLIST_FREE 
 * 来覆盖默认的 malloc/free 实现。
 */

#ifndef SLIST_MALLOC
    #include <stdlib.h>
    #define SLIST_MALLOC malloc
#endif

#ifndef SLIST_FREE
    #include <stdlib.h>
    #define SLIST_FREE free
#endif

/* 内部辅助函数：创建新节点 */
static slist_node_t *_slist_new_node(void *data) {
    slist_node_t *node = (slist_node_t *)SLIST_MALLOC(sizeof(slist_node_t));
    if (node) {
        node->data = data;
        node->next = NULL;
    }
    return node;
}

slist_t *slist_create(slist_free_cb free_fn) {
    slist_t *list = (slist_t *)SLIST_MALLOC(sizeof(slist_t));
    if (list) {
        list->head = NULL;
        list->tail = NULL;
        list->len = 0;
        list->free_fn = free_fn;
    }
    return list;
}

void slist_destroy(slist_t *list) {
    if (!list) return;

    slist_node_t *curr = list->head;
    slist_node_t *next;

    while (curr != NULL) {
        next = curr->next;
        // 如果用户注册了释放函数，释放用户数据
        if (list->free_fn && curr->data) {
            list->free_fn(curr->data);
        }
        // 释放节点本身
        SLIST_FREE(curr);
        curr = next;
    }

    // 释放链表结构体
    SLIST_FREE(list);
}

int slist_append(slist_t *list, void *data) {
    if (!list) return -1;

    slist_node_t *node = _slist_new_node(data);
    if (!node) return -1; // 内存分配失败

    if (list->len == 0) {
        list->head = node;
        list->tail = node;
    } else {
        list->tail->next = node;
        list->tail = node;
    }

    list->len++;
    return 0;
}

int slist_prepend(slist_t *list, void *data) {
    if (!list) return -1;

    slist_node_t *node = _slist_new_node(data);
    if (!node) return -1;

    if (list->len == 0) {
        list->head = node;
        list->tail = node;
    } else {
        node->next = list->head;
        list->head = node;
    }

    list->len++;
    return 0;
}

size_t slist_len(const slist_t *list) {
    return list ? list->len : 0;
}

void *slist_get(const slist_t *list, size_t index) {
    if (!list || index >= list->len) return NULL;

    // 优化：如果是取最后一个，直接返回 tail
    if (index == list->len - 1) return list->tail->data;

    slist_node_t *curr = list->head;
    while (index > 0 && curr) {
        curr = curr->next;
        index--;
    }
    return curr ? curr->data : NULL;
}

int slist_remove(slist_t *list, void *data) {
    if (!list || list->len == 0) return -1;

    slist_node_t *curr = list->head;
    slist_node_t *prev = NULL;

    while (curr) {
        if (curr->data == data) { // 找到匹配的指针
            if (prev) {
                prev->next = curr->next;
                // 如果删除了尾节点，更新 tail
                if (curr == list->tail) {
                    list->tail = prev;
                }
            } else {
                // 删除的是头节点
                list->head = curr->next;
                if (list->len == 1) {
                    list->tail = NULL;
                }
            }

            // 释放数据（如果需要）和节点
            if (list->free_fn && curr->data) {
                list->free_fn(curr->data);
            }
            SLIST_FREE(curr);
            list->len--;
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }

    return -1; // 未找到
}

int slist_remove_at(slist_t *list, size_t index) {
    if (!list || index >= list->len) return -1;

    slist_node_t *curr = list->head;
    slist_node_t *prev = NULL;

    // 定位节点
    for (size_t i = 0; i < index; i++) {
        prev = curr;
        curr = curr->next;
    }

    // 执行删除逻辑
    if (prev) {
        prev->next = curr->next;
        if (curr == list->tail) {
            list->tail = prev;
        }
    } else {
        // 删除头节点
        list->head = curr->next;
        if (list->len == 1) {
            list->tail = NULL;
        }
    }

    if (list->free_fn && curr->data) {
        list->free_fn(curr->data);
    }
    SLIST_FREE(curr);
    list->len--;

    return 0;
}

void slist_reverse(slist_t *list) {
    if (!list || list->len < 2) return;

    slist_node_t *prev = NULL;
    slist_node_t *curr = list->head;
    slist_node_t *next = NULL;

    list->tail = list->head; // 原来的头变成尾

    while (curr != NULL) {
        next = curr->next; // 保存下一个
        curr->next = prev; // 反转指针
        prev = curr;       // 前进
        curr = next;
    }

    list->head = prev; // 最后的 curr 是 NULL，prev 是新的头
}