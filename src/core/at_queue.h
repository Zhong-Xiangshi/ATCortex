/* core/at_queue.h */
#ifndef AT_QUEUE_H
#define AT_QUEUE_H

#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "at.h"

/** 单条命令上下文 / Per-command context */
typedef struct {
    char     cmd[AT_MAX_CMD_LEN];
    char     resp[AT_MAX_RESP_LEN];
    size_t   resp_len;
    bool     resp_success;

    uint32_t timeout_ms;
    uint32_t start_ms;

    at_resp_cb_t cb;
    void    *arg;

    /* ---------- 事务相关 / Transactional fields ---------- */
    bool           txn_enabled;      /**< 是否为事务型命令 / Is transactional command */
    at_txn_desc_t  txn;              /**< 事务描述（浅拷贝指针） / Descriptor (shallow copy of pointers) */
    size_t         txn_sent;         /**< 已发送负载字节数 / Bytes of payload sent */
    size_t         term_sent;        /**< 已发送终止符字节数 / Bytes of terminator sent */
    size_t         prompt_matched;   /**< 已匹配的提示前缀长度 / Matched prompt prefix length */
    bool           prompt_received;  /**< 已收到提示 / Prompt has been observed */
    bool           payload_started;  /**< 已开始二进制发送，抑制行解析 / Binary phase started */
} ATCommand;

/** 循环队列 / Circular queue */
typedef struct {
    ATCommand commands[AT_MAX_QUEUE_SIZE];
    int head, tail, count;
} at_queue_t;

static inline bool at_queue_is_empty(const at_queue_t *q) { return q->count == 0; }
static inline bool at_queue_is_full (const at_queue_t *q) { return q->count >= AT_MAX_QUEUE_SIZE; }

void       at_queue_init (at_queue_t *q);
int        at_queue_push (at_queue_t *q, const char *command, at_resp_cb_t cb, void *arg);
int        at_queue_push_ex(at_queue_t *q, const char *command, uint32_t timeout_ms, at_resp_cb_t cb, void *arg);

/** 事务型命令入队 / Push a transactional command */
int        at_queue_push_txn(at_queue_t *q, const char *command, const at_txn_desc_t *txn,
                             uint32_t timeout_ms, at_resp_cb_t cb, void *arg);

ATCommand* at_queue_front(at_queue_t *q);
void       at_queue_pop  (at_queue_t *q);

#endif /* AT_QUEUE_H */
