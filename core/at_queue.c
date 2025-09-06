/* core/at_queue.c */
#include "at_queue.h"

void at_queue_init(at_queue_t *q) {
    q->head = q->tail = q->count = 0;
}

int at_queue_push(at_queue_t *q, const char *command, at_resp_cb_t cb, void *arg) {
    if (at_queue_is_full(q)) return -1;
    ATCommand *c = &q->commands[q->tail];
    size_t n = strlen(command);
    if (n >= AT_MAX_CMD_LEN) n = AT_MAX_CMD_LEN - 1;
    memcpy(c->cmd, command, n);
    c->cmd[n] = '\0';
    c->resp_len = 0; c->resp[0] = '\0'; c->resp_success = false;
    c->cb = cb; c->arg = arg;
    q->tail = (q->tail + 1) % AT_MAX_QUEUE_SIZE;
    q->count++;
    return 0;
}

ATCommand* at_queue_front(at_queue_t *q) {
    if (at_queue_is_empty(q)) return NULL;
    return &q->commands[q->head];
}

void at_queue_pop(at_queue_t *q) {
    if (at_queue_is_empty(q)) return;
    q->head = (q->head + 1) % AT_MAX_QUEUE_SIZE;
    q->count--;
}
