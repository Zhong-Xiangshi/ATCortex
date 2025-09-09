/* core/at_parser.c */
#include <string.h>
#include <stdbool.h>
#include "at_parser.h"
#include "at_log.h"
#include "at.h"

/** 解析器上下文：每端口一份 */
typedef struct {
    char   buf[AT_MAX_LINE_LEN]; /**< 行缓存（不含结尾 '\0'） */
    size_t len;                  /**< 当前已写入长度 */
    bool   overflow;             /**< 当前行是否已超长被截断 */
} parser_ctx_t;

static parser_ctx_t  g_ctx[AT_MAX_PORTS];
static at_line_cb_t  g_cb = NULL;

/** 初始化解析器并清空所有端口缓冲 */
void at_parser_init(at_line_cb_t cb) {
    g_cb = cb;
    for (uint8_t i = 0; i < AT_MAX_PORTS; ++i) {
        g_ctx[i].len = 0; g_ctx[i].overflow = false;
    }
}

/** 按字节流增量解析，遇到 '\n' 视为一行并触发回调，忽略所有 '\r' */
void at_parser_process(uint8_t port_id, const uint8_t *data, size_t len) {
    if (port_id >= AT_MAX_PORTS) return;
    parser_ctx_t *ctx = &g_ctx[port_id];
    for (size_t i = 0; i < len; ++i) {
        char ch = (char)data[i];
        if (ch == '\r') continue;     // 忽略 CR
        if (ch == '\n') {             // 行结束
            if (ctx->overflow) AT_LOG("Warning: port %d line too long, truncated", port_id);
            ctx->buf[ctx->len] = '\0';
            if ((ctx->len > 0 || ctx->overflow) && g_cb) g_cb(port_id, ctx->buf);
            ctx->len = 0; ctx->overflow = false;
        } else {
            if (ctx->len < AT_MAX_LINE_LEN - 1) ctx->buf[ctx->len++] = ch;
            else ctx->overflow = true; // 超长：继续吞字符直到 '\n'
        }
    }
}
