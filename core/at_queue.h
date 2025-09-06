/* core/at_queue.h */
#ifndef AT_QUEUE_H
#define AT_QUEUE_H

#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "at.h"

/** 单条命令上下文 */
typedef struct {
    char   cmd[AT_MAX_CMD_LEN];   /**< 命令字符串（不含 CR/LF） */
    char   resp[AT_MAX_RESP_LEN]; /**< 累计响应缓冲区 */
    size_t resp_len;              /**< 当前响应长度 */
    bool   resp_success;          /**< 是否最终 OK */
    at_resp_cb_t cb;              /**< 响应回调 */
    void  *arg;                   /**< 用户参数 */
} ATCommand;

/** 循环队列 */
typedef struct {
    ATCommand commands[AT_MAX_QUEUE_SIZE];
    int head, tail, count;
} at_queue_t;

static inline bool at_queue_is_empty(const at_queue_t *q) { return q->count == 0; }
static inline bool at_queue_is_full (const at_queue_t *q) { return q->count >= AT_MAX_QUEUE_SIZE; }

void       at_queue_init (at_queue_t *q);
int        at_queue_push (at_queue_t *q, const char *command, at_resp_cb_t cb, void *arg);
ATCommand* at_queue_front(at_queue_t *q);
void       at_queue_pop  (at_queue_t *q);

#endif /* AT_QUEUE_H */
