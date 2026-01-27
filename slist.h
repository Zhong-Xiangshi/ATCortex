//slist.h
#ifndef _SLIST_H_
#define _SLIST_H_

#include <stddef.h> /* for size_t */

#ifdef __cplusplus
extern "C" {
#endif

/* 节点定义 */
typedef struct slist_node {
    void *data;                 /* 用户数据指针 */
    struct slist_node *next;    /* 下一个节点 */
} slist_node_t;

/* 数据释放回调函数原型 data：用户数据 */
typedef void (*slist_free_cb)(void *data);

/* 链表定义 */
typedef struct slist {
    slist_node_t *head;         /* 头节点 */
    slist_node_t *tail;         /* 尾节点 (用于O(1)尾插) */
    size_t len;                 /* 链表长度 */
    slist_free_cb free_fn;      /* 数据释放回调 (可选) */
} slist_t;

/* 
 * 迭代宏
 * 用法:
 * slist_node_t *node;
 * SLIST_FOREACH(node, list) {
 *     MyData *d = (MyData*)node->data;
 * }
 */
#define SLIST_FOREACH(node, list) \
    for ((node) = (list)->head; (node) != NULL; (node) = (node)->next)

/* API 接口 */

/**
 * @brief 创建一个新链表
 * @param free_fn 可选的数据释放回调，如果不需要自动释放数据传 NULL
 * @return 链表指针，失败返回 NULL
 */
slist_t *slist_create(slist_free_cb free_fn);

/**
 * @brief 销毁链表
 * 会释放所有节点内存。如果设置了 free_fn，也会释放数据内存。
 */
void slist_destroy(slist_t *list);

/**
 * @brief 在链表尾部追加节点
 */
int slist_append(slist_t *list, void *data);

/**
 * @brief 在链表头部插入节点
 */
int slist_prepend(slist_t *list, void *data);

/**
 * @brief 获取链表长度
 */
size_t slist_len(const slist_t *list);

/**
 * @brief 获取指定索引的数据，0是第一个
 * @return 数据指针，如果索引越界返回 NULL
 */
void *slist_get(const slist_t *list, size_t index);

/**
 * @brief 删除包含指定数据指针的第一个节点。如果设置了 free_fn，也会释放数据内存。
 * 注意：这里比较的是指针地址
 * @return 0 成功, -1 未找到
 */
int slist_remove(slist_t *list, void *data);

/**
 * @brief 删除指定索引的节点。如果设置了 free_fn，也会释放数据内存。
 * @return 0 成功, -1 索引越界
 */
int slist_remove_at(slist_t *list, size_t index);

/**
 * @brief 反转链表
 */
void slist_reverse(slist_t *list);

#ifdef __cplusplus
}
#endif

#endif // _SLIST_H_