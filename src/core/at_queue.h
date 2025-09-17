/* core/at_queue.h */
#ifndef AT_QUEUE_H
#define AT_QUEUE_H

#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "at.h"

/** 单条命令上下文（队列元素） */
typedef struct {
    char     cmd[AT_MAX_CMD_LEN];        /**< 命令行（不含 CR/LF） */
    char     resp[AT_MAX_RESP_LEN];      /**< 累计响应缓冲（多行拼接，以 '\n' 分隔） */
    size_t   resp_len;                   /**< 当前已写入长度 */
    bool     resp_success;               /**< 最终是否成功（OK/SEND OK） */

    uint32_t timeout_ms;                 /**< 超时（ms） */
    uint32_t start_ms;                   /**< 启动时间戳（ms） */

    at_resp_cb_t cb;                     /**< 响应回调 */
    void    *arg;                        /**< 用户参数 */

    /* ---------- 事务相关 ---------- */
    bool           txn_enabled;          /**< 是否为事务型命令 */
    at_txn_desc_t  txn;                  /**< 事务描述（浅拷贝指针） */
    size_t         txn_sent;             /**< 已发送负载字节数 */
    size_t         term_sent;            /**< 已发送终止符字节数 */
    size_t         prompt_matched;       /**< 已匹配的提示前缀长度 */
    bool           prompt_received;      /**< 是否已收到提示 */
    bool           payload_started;      /**< 是否已开始二进制阶段（抑制行解析） */
    bool           data_receiving;       //PROMPT_RX 模式的数据接收标志
} ATCommand;

/** 循环队列：O(1) 入队/出队 */
typedef struct {
    ATCommand commands[AT_MAX_QUEUE_SIZE];
    int head, tail, count;
} at_queue_t;

/** 判空/判满 */
static inline bool at_queue_is_empty(const at_queue_t *q) { return q->count == 0; }
static inline bool at_queue_is_full (const at_queue_t *q) { return q->count >= AT_MAX_QUEUE_SIZE; }

/** 初始化队列（清零头尾与计数） */
void       at_queue_init (at_queue_t *q);

/** 入队普通命令（默认超时） */
int        at_queue_push (at_queue_t *q, const char *command, at_resp_cb_t cb, void *arg);

/** 入队普通命令（自定义超时） */
int        at_queue_push_ex(at_queue_t *q, const char *command, uint32_t timeout_ms, at_resp_cb_t cb, void *arg);

/** 入队事务型命令 */
int        at_queue_push_txn(at_queue_t *q, const char *command, const at_txn_desc_t *txn,
                             uint32_t timeout_ms, at_resp_cb_t cb, void *arg);

/** 取队头元素指针（为空返回 NULL） */
ATCommand* at_queue_front(at_queue_t *q);

/** 出队（空队列时无操作） */
void       at_queue_pop  (at_queue_t *q);

#endif /* AT_QUEUE_H */
