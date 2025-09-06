/* core/at_dispatcher.c */
#include <string.h>
#include "at_dispatcher.h"
#include "at_log.h"

typedef struct {
    char       prefix[AT_MAX_CMD_LEN];
    at_urc_cb_t cb;
    void      *arg;
} urc_entry_t;

static urc_entry_t g_urc[AT_MAX_PORTS][AT_MAX_URC_HANDLERS];
static uint8_t     g_cnt[AT_MAX_PORTS];

void at_dispatcher_init(void) {
    for (uint8_t p = 0; p < AT_MAX_PORTS; ++p) g_cnt[p] = 0;
}

int at_register_urc_handler(uint8_t port_id, const char *prefix, at_urc_cb_t cb, void *user_arg) {
    if (port_id >= AT_MAX_PORTS || !prefix || !prefix[0] || !cb) return -1;
    if (g_cnt[port_id] >= AT_MAX_URC_HANDLERS) return -1;
    urc_entry_t *e = &g_urc[port_id][g_cnt[port_id]];
    size_t n = strlen(prefix);
    if (n >= AT_MAX_CMD_LEN) n = AT_MAX_CMD_LEN - 1;
    memcpy(e->prefix, prefix, n);
    e->prefix[n] = '\0';
    e->cb  = cb;
    e->arg = user_arg;
    g_cnt[port_id]++;
    return 0;
}

int at_unregister_urc_handler(uint8_t port_id, const char *prefix) {
    if (port_id >= AT_MAX_PORTS || !prefix || !prefix[0]) return -1;
    uint8_t cnt = g_cnt[port_id];
    for (uint8_t i = 0; i < cnt; ++i) {
        if (strcmp(g_urc[port_id][i].prefix, prefix) == 0) {
            // 用最后一个元素覆盖，O(1) 删除
            if (i != cnt - 1) g_urc[port_id][i] = g_urc[port_id][cnt - 1];
            g_cnt[port_id]--;
            return 0;
        }
    }
    return -1;
}

int at_dispatcher_dispatch_line(uint8_t port_id, const char *line) {
    if (port_id >= AT_MAX_PORTS) return 0;
    for (uint8_t i = 0; i < g_cnt[port_id]; ++i) {
        urc_entry_t *e = &g_urc[port_id][i];
        size_t pre = strlen(e->prefix);
        if (strncmp(line, e->prefix, pre) == 0) {
            AT_LOG("URC dispatch (port %d): %s", port_id, line);
            e->cb(port_id, line, e->arg);
            return 1; // 已作为 URC 处理
        }
    }
    return 0; // 非 URC
}
