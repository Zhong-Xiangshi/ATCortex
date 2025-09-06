/* core/at_parser.c */
#include <string.h>
#include <stdbool.h>
#include "at_parser.h"
#include "at_log.h"
#include "at.h"

typedef struct {
    char   buf[AT_MAX_LINE_LEN];
    size_t len;
    bool   overflow;
} parser_ctx_t;

static parser_ctx_t  g_ctx[AT_MAX_PORTS];
static at_line_cb_t  g_cb = NULL;

void at_parser_init(at_line_cb_t cb) {
    g_cb = cb;
    for (uint8_t i = 0; i < AT_MAX_PORTS; ++i) {
        g_ctx[i].len = 0; g_ctx[i].overflow = false;
    }
}

void at_parser_process(uint8_t port_id, const uint8_t *data, size_t len) {
    if (port_id >= AT_MAX_PORTS) return;
    parser_ctx_t *ctx = &g_ctx[port_id];
    for (size_t i = 0; i < len; ++i) {
        char ch = (char)data[i];
        if (ch == '\r') continue;     // 忽略 CR
        if (ch == '\n') {             // 行结束
            if (ctx->overflow) AT_LOG("警告: 端口 %d 的行过长已截断", port_id);
            ctx->buf[ctx->len] = '\0';
            if ((ctx->len > 0 || ctx->overflow) && g_cb) g_cb(port_id, ctx->buf);
            ctx->len = 0; ctx->overflow = false;
        } else {
            if (ctx->len < AT_MAX_LINE_LEN - 1) ctx->buf[ctx->len++] = ch;
            else ctx->overflow = true; // 超长，丢弃后续字符，直到遇到 '\n'
        }
    }
}
