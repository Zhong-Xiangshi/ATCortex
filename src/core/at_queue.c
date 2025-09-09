/* core/at_queue.c */
#include "at_queue.h"

/** 初始化一条命令的公共字段 */
static void init_cmd_entry_base(ATCommand *c, const char *command,
                                uint32_t timeout_ms, at_resp_cb_t cb, void *arg)
{
    size_t n = strlen(command);
    if (n >= AT_MAX_CMD_LEN) n = AT_MAX_CMD_LEN - 1;
    memcpy(c->cmd, command, n);
    c->cmd[n] = '\0';

    c->resp_len = 0;
    c->resp[0] = '\0';
    c->resp_success = false;

    c->timeout_ms = (timeout_ms == 0u) ? AT_DEFAULT_TIMEOUT_MS : timeout_ms;
    c->start_ms   = 0u;

    c->cb  = cb;
    c->arg = arg;

    /* 事务字段清零/默认 */
    c->txn_enabled     = false;
    c->txn.type        = AT_TXN_NONE;
    c->txn.payload     = NULL;
    c->txn.payload_len = 0;
    c->txn.terminator  = NULL;
    c->txn.term_len    = 0;
    c->txn.prompt      = NULL;
    c->txn.prompt_len  = 0;

    c->txn_sent        = 0;
    c->term_sent       = 0;
    c->prompt_matched  = 0;
    c->prompt_received = false;
    c->payload_started = false;
}

/** 初始化队列 */
void at_queue_init(at_queue_t *q) {
    q->head = q->tail = q->count = 0;
}

/** 入队默认超时命令 */
int at_queue_push(at_queue_t *q, const char *command, at_resp_cb_t cb, void *arg) {
    if (at_queue_is_full(q)) return -1;
    ATCommand *c = &q->commands[q->tail];
    init_cmd_entry_base(c, command, AT_DEFAULT_TIMEOUT_MS, cb, arg);
    q->tail = (q->tail + 1) % AT_MAX_QUEUE_SIZE;
    q->count++;
    return 0;
}

/** 入队自定义超时命令 */
int at_queue_push_ex(at_queue_t *q, const char *command, uint32_t timeout_ms, at_resp_cb_t cb, void *arg) {
    if (at_queue_is_full(q)) return -1;
    ATCommand *c = &q->commands[q->tail];
    init_cmd_entry_base(c, command, timeout_ms, cb, arg);
    q->tail = (q->tail + 1) % AT_MAX_QUEUE_SIZE;
    q->count++;
    return 0;
}

/** 入队事务型命令（描述指针浅拷贝） */
int at_queue_push_txn(at_queue_t *q, const char *command, const at_txn_desc_t *txn,
                      uint32_t timeout_ms, at_resp_cb_t cb, void *arg)
{
    if (at_queue_is_full(q) || !txn) return -1;
    ATCommand *c = &q->commands[q->tail];
    init_cmd_entry_base(c, command, timeout_ms, cb, arg);

    c->txn_enabled = true;
    c->txn         = *txn;   /* 浅拷贝指针 */
    c->txn_sent    = 0;
    c->term_sent   = 0;
    c->prompt_matched  = 0;
    c->prompt_received = (txn->type == AT_TXN_LENGTH) ? true : false; /* 长度模式：立即可发 */
    c->payload_started = false;

    q->tail = (q->tail + 1) % AT_MAX_QUEUE_SIZE;
    q->count++;
    return 0;
}

/** 取队头元素 */
ATCommand* at_queue_front(at_queue_t *q) {
    if (at_queue_is_empty(q)) return NULL;
    return &q->commands[q->head];
}

/** 出队 */
void at_queue_pop(at_queue_t *q) {
    if (at_queue_is_empty(q)) return;
    q->head = (q->head + 1) % AT_MAX_QUEUE_SIZE;
    q->count--;
}
