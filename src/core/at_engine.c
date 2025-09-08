/*
 * @file at_engine.c
 * @brief Core AT engine implementation with timeout, echo handling, and transactional commands.
 */

#include "at_engine.h"
#include "at_port.h"
#include "at_queue.h"
#include "at_parser.h"
#include "at_dispatcher.h"
#include "at_log.h"

#include <string.h>

/** 每端口上下文 / Per-port context */
typedef struct {
    at_queue_t queue;
    bool       busy;
    bool       echo_ignore;
    bool       echo_pending;

    bool       suppress_lines;  /* 二进制阶段抑制行处理 / suppress line handling during binary phase */
} at_port_context_t;

static at_port_context_t g_port_ctx[AT_MAX_PORTS];
static uint8_t           g_port_count = 1;

static void engine_on_line(uint8_t port_id, const char *line);

/* 工具：获取默认提示配置 / helper for default prompt "> " */
static inline void txn_ensure_prompt_defaults(ATCommand *cmd) {
    if (!cmd->txn.prompt) {
        cmd->txn.prompt     = "> ";
        cmd->txn.prompt_len = 2;
    } else if (cmd->txn.prompt_len == 0) {
        cmd->txn.prompt_len = strlen(cmd->txn.prompt);
    }
}

/* 在原始字节流中匹配提示串（仅 PROMPT 模式）/ scan incoming raw bytes to detect prompt (PROMPT mode) */
static void engine_scan_prompt(uint8_t port_id, const uint8_t *data, size_t len) {
    at_port_context_t *ctx = &g_port_ctx[port_id];
    if (!ctx->busy) return;

    ATCommand *cmd = at_queue_front(&ctx->queue);
    if (!cmd || !cmd->txn_enabled || cmd->txn.type != AT_TXN_PROMPT || cmd->prompt_received) return;

    txn_ensure_prompt_defaults(cmd);

    const char *pat = cmd->txn.prompt;
    size_t plen     = cmd->txn.prompt_len;
    size_t m        = cmd->prompt_matched;

    for (size_t i = 0; i < len; ++i) {
        char ch = (char)data[i];
        if (ch == pat[m]) {
            m++;
            if (m == plen) {
                cmd->prompt_received = true;
                break;
            }
        } else {
            /* 失败回退：若当前字符刚好匹配首字符，则 m=1，否则 m=0 */
            m = (ch == pat[0]) ? 1u : 0u;
        }
    }
    cmd->prompt_matched = m;
}

/* 向端口推进发送事务数据（负载与终止符）/ progress sending txn payload/terminator */
static void engine_progress_txn(uint8_t port_id, at_port_context_t *ctx, ATCommand *cmd) {
    if (!cmd->txn_enabled) return;

    /* 若是 PROMPT 模式但尚未收到提示，先不发 */
    if (cmd->txn.type == AT_TXN_PROMPT && !cmd->prompt_received) return;

    /* 一旦开始发二进制，抑制行处理，避免 payload 中的 '\n' 被当成响应 */
    if (!cmd->payload_started) {
        ctx->suppress_lines = true;
        cmd->payload_started = true;
    }

    /* 先发 payload */
    if (cmd->txn_sent < cmd->txn.payload_len) {
        const uint8_t *p = cmd->txn.payload + cmd->txn_sent;
        size_t remain = cmd->txn.payload_len - cmd->txn_sent;
        size_t n = at_port_write(port_id, p, remain);
        cmd->txn_sent += n;
        return; /* 下次轮询继续推进 */
    }

    /* 再发 terminator（若有）*/
    if (cmd->txn.term_len > 0 && cmd->term_sent < cmd->txn.term_len) {
        const uint8_t *t = cmd->txn.terminator + cmd->term_sent;
        size_t trem = cmd->txn.term_len - cmd->term_sent;
        size_t n = at_port_write(port_id, t, trem);
        cmd->term_sent += n;
        if (cmd->term_sent < cmd->txn.term_len) {
            return; /* 还没发完，下次继续 */
        }
    }

    /* 数据阶段结束，恢复行解析，等待最终态 */
    ctx->suppress_lines = false;
}

static void finish_command(uint8_t port_id, ATCommand *cmd, bool success, const char *maybe_error_line) {
    cmd->resp_success = success;

    if (!success && maybe_error_line && strcmp(maybe_error_line, "TIMEOUT") == 0) {
        if (cmd->cb) cmd->cb(port_id, "TIMEOUT", false, cmd->arg);
        AT_LOG("Command timeout (port %d)", port_id);
        return;
    }

    if (!success && maybe_error_line && maybe_error_line[0]) {
        size_t n = strlen(maybe_error_line);
        if (n >= (AT_MAX_RESP_LEN - cmd->resp_len)) n = (AT_MAX_RESP_LEN - 1) - cmd->resp_len;
        if (n > 0) {
            memcpy(cmd->resp + cmd->resp_len, maybe_error_line, n);
            cmd->resp_len += n;
            cmd->resp[cmd->resp_len] = '\0';
        }
    }
    if (cmd->resp_len > 0 && cmd->resp[cmd->resp_len - 1] == '\n') {
        cmd->resp[--cmd->resp_len] = '\0';
    }
    if (cmd->cb) cmd->cb(port_id, cmd->resp, cmd->resp_success, cmd->arg);
    AT_LOG("Command finished (port %d), success=%d", port_id, (int)cmd->resp_success);
}

void at_engine_init(uint8_t port_count) {
    if (port_count < 1) port_count = 1;
    if (port_count > AT_MAX_PORTS) {
        AT_LOG("at_engine_init: port_count out of bounds, limited to %d", AT_MAX_PORTS);
        port_count = AT_MAX_PORTS;
    }
    g_port_count = port_count;
    for (uint8_t i = 0; i < g_port_count; ++i) {
        at_port_init(i);
        at_queue_init(&g_port_ctx[i].queue);
        g_port_ctx[i].busy = false;
        g_port_ctx[i].echo_ignore = false;
        g_port_ctx[i].echo_pending = false;
        g_port_ctx[i].suppress_lines = false;
    }
    at_parser_init(engine_on_line);
    at_dispatcher_init();
    AT_LOG("AT engine initialized, ports=%d", g_port_count);
}

void at_engine_init_ex(uint8_t port_count, const bool *echo_ignore_map) {
    at_engine_init(port_count);
    for (uint8_t i = 0; i < g_port_count; ++i) {
        g_port_ctx[i].echo_ignore  = echo_ignore_map ? !!echo_ignore_map[i] : false;
        g_port_ctx[i].echo_pending = false;
        g_port_ctx[i].suppress_lines = false;
    }
    AT_LOG("AT engine extended initialization: echo ignore policy set per port");
}

/* 行回调：二进制阶段可选择抑制行；否则按 URC/响应处理 */
static void engine_on_line(uint8_t port_id, const char *line) {
    if (port_id >= g_port_count) return;

    at_port_context_t *ctx = &g_port_ctx[port_id];
    ATCommand *cmd = at_queue_front(&ctx->queue);

    /* 二进制阶段：抑制任何行（避免把payload误判为响应/URC） */
    if (ctx->suppress_lines) {
        return;
    }

    /* 先尝试作为 URC 分发 */
    if (at_dispatcher_dispatch_line(port_id, line)) {
        return;
    }

    /* 命令响应 */
    if (ctx->busy && cmd) {
        /* 扩展成功态：除 "OK" 也接受 "SEND OK"（常见于数据发送命令） */
        bool is_ok    = (strcmp(line, "OK") == 0) || (strcmp(line, "SEND OK") == 0);
        bool is_error = (strncmp(line, "ERROR", 5) == 0) ||
                        (strncmp(line, "+CME ERROR", 10) == 0) ||
                        (strncmp(line, "+CMS ERROR", 10) == 0);

        if (is_ok || is_error) {
            finish_command(port_id, cmd, is_ok, is_error ? line : NULL);
            at_queue_pop(&ctx->queue);
            ctx->busy = false;
            ctx->echo_pending = false;
            ctx->suppress_lines = false;
        } else {
            size_t n = strlen(line);
            if (n < (AT_MAX_RESP_LEN - cmd->resp_len - 2)) {
                memcpy(cmd->resp + cmd->resp_len, line, n);
                cmd->resp_len += n;
                cmd->resp[cmd->resp_len++] = '\n';
                cmd->resp[cmd->resp_len] = '\0';
            } else {
                size_t cpy = (AT_MAX_RESP_LEN - cmd->resp_len - 2);
                if (cpy > 0) {
                    memcpy(cmd->resp + cmd->resp_len, line, cpy);
                    cmd->resp_len += cpy;
                    cmd->resp[cmd->resp_len] = '\0';
                }
                AT_LOG("Warning: Response buffer overflow, truncating (port %d)", port_id);
            }
        }
    } else {
        AT_LOG("Info: Unhandled line (port %d): %s", port_id, line);
    }
}

void at_engine_poll(void) {
    uint8_t buf[64];

    /* 1) 输入处理：预扫描提示 + 交给行解析器 */
    for (uint8_t p = 0; p < g_port_count; ++p) {
        size_t n;
        do {
            n = at_port_read(p, buf, sizeof(buf));
            if (n > 0) {
                /* 若在等待提示（PROMPT），先扫描原始字节 */
                at_port_context_t *ctx = &g_port_ctx[p];
                if (ctx->busy) {
                    ATCommand *cmd = at_queue_front(&ctx->queue);
                    if (cmd && cmd->txn_enabled && cmd->txn.type == AT_TXN_PROMPT && !cmd->prompt_received) {
                        engine_scan_prompt(p, buf, n);
                    }
                }
                at_parser_process(p, buf, n);
            }
        } while (n > 0);
    }

    /* 2) 超时检测 */
    for (uint8_t p = 0; p < g_port_count; ++p) {
        at_port_context_t *ctx = &g_port_ctx[p];
        if (ctx->busy) {
            ATCommand *cmd = at_queue_front(&ctx->queue);
            if (cmd) {
                uint32_t now = at_port_get_time_ms(p);
                uint32_t elapsed = (uint32_t)(now - cmd->start_ms);
                if (elapsed >= cmd->timeout_ms) {
                    AT_LOG("Command timeout (port %d): %s, elapsed=%u ms", p, cmd->cmd, (unsigned)elapsed);
                    finish_command(p, cmd, false, "TIMEOUT");
                    at_queue_pop(&ctx->queue);
                    ctx->busy = false;
                    ctx->echo_pending = false;
                    ctx->suppress_lines = false;
                }
            }
        }
    }

    /* 3) 如果端口空闲则发送队头命令（初始行） */
    for (uint8_t p = 0; p < g_port_count; ++p) {
        at_port_context_t *ctx = &g_port_ctx[p];
        if (!ctx->busy) {
            ATCommand *next = at_queue_front(&ctx->queue);
            if (next) {
                size_t n = strlen(next->cmd);
                if (n > 0) {
                    AT_LOG("Sending command (port %d): %s", p, next->cmd);
                    at_port_write(p, (const uint8_t*)next->cmd, n);
                    const uint8_t crlf[2] = {'\r','\n'};
                    at_port_write(p, crlf, 2);
                }
                /* 设置初始事务状态 */
                if (next->txn_enabled) {
                    if (next->txn.type == AT_TXN_LENGTH) {
                        /* 长度模式：立刻进入数据阶段 */
                        next->prompt_received = true;
                        ctx->suppress_lines   = true;
                    } else if (next->txn.type == AT_TXN_PROMPT) {
                        /* 提示模式：等待提示（默认 "> "） */
                        txn_ensure_prompt_defaults(next);
                        next->prompt_matched  = 0;
                        next->prompt_received = false;
                        /* 此时尚未抑制行；收到提示并开始发数据时再抑制 */
                    }
                }
                next->start_ms = at_port_get_time_ms(p);
                ctx->busy = true;
                ctx->echo_pending = ctx->echo_ignore;
            }
        }
    }

    /* 4) 推进事务的数据发送（即便端口“忙”，也要推进二进制发送） */
    for (uint8_t p = 0; p < g_port_count; ++p) {
        at_port_context_t *ctx = &g_port_ctx[p];
        if (ctx->busy) {
            ATCommand *cmd = at_queue_front(&ctx->queue);
            if (cmd && cmd->txn_enabled) {
                engine_progress_txn(p, ctx, cmd);
            }
        }
    }
}

int at_send_cmd(uint8_t port_id, const char *command, at_resp_cb_t cb, void *user_arg) {
    if (port_id >= g_port_count || !command) return -1;
    at_port_context_t *ctx = &g_port_ctx[port_id];
    if (at_queue_push(&ctx->queue, command, cb, user_arg) != 0) return -1;
    AT_LOG("Command queued (port %d): %s (default timeout %u ms)", port_id, command, (unsigned)AT_DEFAULT_TIMEOUT_MS);

    if (!ctx->busy) {
        ATCommand *cmd = at_queue_front(&ctx->queue);
        if (cmd) {
            size_t n = strlen(cmd->cmd);
            if (n > 0) {
                AT_LOG("Sending immediately (port %d): %s", port_id, cmd->cmd);
                at_port_write(port_id, (const uint8_t*)cmd->cmd, n);
                const uint8_t crlf[2] = {'\r','\n'};
                at_port_write(port_id, crlf, 2);
            }
            cmd->start_ms = at_port_get_time_ms(port_id);
            ctx->busy = true;
            ctx->echo_pending   = ctx->echo_ignore;
            ctx->suppress_lines = false;
        }
    }
    return 0;
}

int at_send_cmd_ex(uint8_t port_id, const char *command, uint32_t timeout_ms, at_resp_cb_t cb, void *user_arg) {
    if (port_id >= g_port_count || !command) return -1;
    at_port_context_t *ctx = &g_port_ctx[port_id];
    if (at_queue_push_ex(&ctx->queue, command, timeout_ms, cb, user_arg) != 0) return -1;
    uint32_t actual_to = (timeout_ms == 0u) ? AT_DEFAULT_TIMEOUT_MS : timeout_ms;
    AT_LOG("Command queued (port %d): %s (timeout %u ms)", port_id, command, (unsigned)actual_to);

    if (!ctx->busy) {
        ATCommand *cmd = at_queue_front(&ctx->queue);
        if (cmd) {
            size_t n = strlen(cmd->cmd);
            if (n > 0) {
                AT_LOG("Sending immediately (port %d): %s", port_id, cmd->cmd);
                at_port_write(port_id, (const uint8_t*)cmd->cmd, n);
                const uint8_t crlf[2] = {'\r','\n'};
                at_port_write(port_id, crlf, 2);
            }
            cmd->start_ms = at_port_get_time_ms(port_id);
            ctx->busy = true;
            ctx->echo_pending   = ctx->echo_ignore;
            ctx->suppress_lines = false;
        }
    }
    return 0;
}

int at_send_cmd_txn(uint8_t port_id, const char *command, const at_txn_desc_t *txn,
                    uint32_t timeout_ms, at_resp_cb_t cb, void *user_arg)
{
    if (port_id >= g_port_count || !command || !txn) return -1;
    if (txn->type != AT_TXN_PROMPT && txn->type != AT_TXN_LENGTH) return -1;

    at_port_context_t *ctx = &g_port_ctx[port_id];
    if (at_queue_push_txn(&ctx->queue, command, txn, timeout_ms, cb, user_arg) != 0) return -1;

    uint32_t actual_to = (timeout_ms == 0u) ? AT_DEFAULT_TIMEOUT_MS : timeout_ms;
    AT_LOG("Command (txn) queued (port %d): %s (timeout %u ms, type=%d)", port_id, command, (unsigned)actual_to, (int)txn->type);

    /* 若端口空闲，立即发出命令行；数据阶段将由轮询推进 */
    if (!ctx->busy) {
        ATCommand *cmd = at_queue_front(&ctx->queue);
        if (cmd) {
            size_t n = strlen(cmd->cmd);
            if (n > 0) {
                AT_LOG("Sending immediately (port %d): %s", port_id, cmd->cmd);
                at_port_write(port_id, (const uint8_t*)cmd->cmd, n);
                const uint8_t crlf[2] = {'\r','\n'};
                at_port_write(port_id, crlf, 2);
            }
            /* 初始化事务开关 */
            if (cmd->txn_enabled) {
                if (cmd->txn.type == AT_TXN_LENGTH) {
                    cmd->prompt_received = true;
                    ctx->suppress_lines  = true;
                } else { /* PROMPT */
                    txn_ensure_prompt_defaults(cmd);
                    cmd->prompt_matched  = 0;
                    cmd->prompt_received = false;
                }
            }
            cmd->start_ms = at_port_get_time_ms(port_id);
            ctx->busy = true;
            ctx->echo_pending   = ctx->echo_ignore;
            /* PROMPT 模式初始不抑制行，收到提示并开始发数据时再抑制 */
        }
    }
    return 0;
}
