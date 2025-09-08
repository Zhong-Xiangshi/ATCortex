/*
 * @file at_engine.c
 * @brief Core AT engine implementation with timeout and echo handling.
 */

#include "at_engine.h"
#include "at_port.h"
#include "at_queue.h"
#include "at_parser.h"
#include "at_dispatcher.h"
#include "at_log.h"

#include <string.h>

/**
 * Per-port context structure holding state and queue for each port.
 */
typedef struct {
    at_queue_t queue;   // 命令队列
    bool       busy;    // 是否有命令正在执行
    bool       echo_ignore;  // 是否忽略回显
    bool       echo_pending; // 本条命令是否仍需丢弃一次回显行
} at_port_context_t;

static at_port_context_t g_port_ctx[AT_MAX_PORTS];
static uint8_t g_port_count = 1;

static void engine_on_line(uint8_t port_id, const char *line);

/**
 * @brief Complete the current command and invoke its callback.
 *
 * Special handling: when maybe_error_line is "TIMEOUT", we only call the callback once
 * with "TIMEOUT" as the response.
 */
static void finish_command(uint8_t port_id, ATCommand *cmd, bool success, const char *maybe_error_line) {
    cmd->resp_success = success;
    // Handle timeout specially
    if (!success && maybe_error_line && strcmp(maybe_error_line, "TIMEOUT") == 0) {
        if (cmd->cb) {
            cmd->cb(port_id, "TIMEOUT", false, cmd->arg);
        }
        AT_LOG("Command timeout (port %d)", port_id);
        return;
    }
    // Append error line if provided
    if (!success && maybe_error_line && maybe_error_line[0] != '\0') {
        size_t n = strlen(maybe_error_line);
        if (n >= (AT_MAX_RESP_LEN - cmd->resp_len)) {
            n = (AT_MAX_RESP_LEN - 1) - cmd->resp_len;
        }
        if (n > 0) {
            memcpy(cmd->resp + cmd->resp_len, maybe_error_line, n);
            cmd->resp_len += n;
            cmd->resp[cmd->resp_len] = '\0';
        }
    }
    // Trim trailing newline
    if (cmd->resp_len > 0 && cmd->resp[cmd->resp_len - 1] == '\n') {
        cmd->resp[--cmd->resp_len] = '\0';
    }
    // Invoke callback
    if (cmd->cb) {
        cmd->cb(port_id, cmd->resp, cmd->resp_success, cmd->arg);
    }
    AT_LOG("Command finished (port %d), success=%d", port_id, (int)cmd->resp_success);
}

void at_engine_init(uint8_t port_count) {
    if (port_count < 1) {
        port_count = 1;
    }
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
    }
    at_parser_init(engine_on_line);
    at_dispatcher_init();
    AT_LOG("AT engine initialized, ports=%d", g_port_count);
}

void at_engine_init_ex(uint8_t port_count, const bool *echo_ignore_map) {
    at_engine_init(port_count);
    for (uint8_t i = 0; i < g_port_count; ++i) {
        bool ignore = false;
        if (echo_ignore_map) {
            ignore = echo_ignore_map[i] ? true : false;
        }
        g_port_ctx[i].echo_ignore = ignore;
        g_port_ctx[i].echo_pending = false;
    }
    AT_LOG("AT engine extended initialization: echo ignore policy set per port");
}

// 行回调：优先处理回显丢弃，其次 URC，再到命令响应
static void engine_on_line(uint8_t port_id, const char *line) {
    if (port_id >= g_port_count) {
        return;
    }
    at_port_context_t *ctx = &g_port_ctx[port_id];
    ATCommand *cmd = at_queue_front(&ctx->queue);

    // 1) 回显处理（若端口有命令在执行 且 配置忽略回显 且 仍待丢弃一次回显）
    if (ctx->busy && cmd && ctx->echo_ignore && ctx->echo_pending) {
        if (strcmp(line, cmd->cmd) == 0) {
            // 丢弃回显行
            ctx->echo_pending = false;
            AT_LOG("Discarding echo line (port %d): %s", port_id, line);
            return;
        }
        // 非回显行则继续走后续逻辑
    }

    // 2) URC 尝试分发
    if (at_dispatcher_dispatch_line(port_id, line)) {
        return;
    }

    // 3) 命令响应处理
    if (ctx->busy && cmd) {
        bool is_ok    = (strcmp(line, "OK") == 0);
        bool is_error = (strncmp(line, "ERROR", 5) == 0) ||
                        (strncmp(line, "+CME ERROR", 10) == 0) ||
                        (strncmp(line, "+CMS ERROR", 10) == 0);
        if (is_ok || is_error) {
            finish_command(port_id, cmd, is_ok, is_error ? line : NULL);
            at_queue_pop(&ctx->queue);
            ctx->busy = false;
            ctx->echo_pending = false;
        } else {
            // accumulate intermediate response
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
        // 没有进行中的命令且非 URC：忽略或日志提示
        AT_LOG("Info: Unhandled line (port %d): %s", port_id, line);
    }
}

void at_engine_poll(void) {
    uint8_t buf[64];

    // 1) 读入并解析
    for (uint8_t p = 0; p < g_port_count; ++p) {
        size_t n;
        do {
            n = at_port_read(p, buf, sizeof(buf));
            if (n > 0) {
                at_parser_process(p, buf, n);
            }
        } while (n > 0);
    }

    // 2) 超时检测（对正在执行的命令）
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
                }
            }
        }
    }

    // 3) 发送调度（端口空闲时发送下一条）
    for (uint8_t p = 0; p < g_port_count; ++p) {
        at_port_context_t *ctx = &g_port_ctx[p];
        if (!ctx->busy) {
            ATCommand *next = at_queue_front(&ctx->queue);
            if (next) {
                size_t n = strlen(next->cmd);
                if (n > 0) {
                    AT_LOG("Sending command (port %d): %s", p, next->cmd);
                    at_port_write(p, (const uint8_t*)next->cmd, n);
                    const uint8_t crlf[2] = { '\r', '\n' };
                    at_port_write(p, crlf, 2);
                }
                next->start_ms = at_port_get_time_ms(p);
                ctx->busy = true;
                ctx->echo_pending = ctx->echo_ignore;
            }
        }
    }
}

int at_send_cmd(uint8_t port_id, const char *command, at_resp_cb_t cb, void *user_arg) {
    if (port_id >= g_port_count || !command) {
        return -1;
    }
    at_port_context_t *ctx = &g_port_ctx[port_id];
    if (at_queue_push(&ctx->queue, command, cb, user_arg) != 0) {
        return -1;
    }
    AT_LOG("Command queued (port %d): %s (default timeout %u ms)", port_id, command, (unsigned)AT_DEFAULT_TIMEOUT_MS);
    if (!ctx->busy) {
        ATCommand *cmd = at_queue_front(&ctx->queue);
        if (cmd) {
            size_t n = strlen(cmd->cmd);
            if (n > 0) {
                AT_LOG("Sending immediately (port %d): %s", port_id, cmd->cmd);
                at_port_write(port_id, (const uint8_t*)cmd->cmd, n);
                const uint8_t crlf[2] = { '\r', '\n' };
                at_port_write(port_id, crlf, 2);
            }
            cmd->start_ms = at_port_get_time_ms(port_id);
            ctx->busy = true;
            ctx->echo_pending = ctx->echo_ignore;
        }
    }
    return 0;
}

int at_send_cmd_ex(uint8_t port_id, const char *command, uint32_t timeout_ms, at_resp_cb_t cb, void *user_arg) {
    if (port_id >= g_port_count || !command) {
        return -1;
    }
    at_port_context_t *ctx = &g_port_ctx[port_id];
    if (at_queue_push_ex(&ctx->queue, command, timeout_ms, cb, user_arg) != 0) {
        return -1;
    }
    uint32_t actual_timeout = (timeout_ms == 0u) ? AT_DEFAULT_TIMEOUT_MS : timeout_ms;
    AT_LOG("Command queued (port %d): %s (timeout %u ms)", port_id, command, (unsigned)actual_timeout);
    if (!ctx->busy) {
        ATCommand *cmd = at_queue_front(&ctx->queue);
        if (cmd) {
            size_t n = strlen(cmd->cmd);
            if (n > 0) {
                AT_LOG("Sending immediately (port %d): %s", port_id, cmd->cmd);
                at_port_write(port_id, (const uint8_t*)cmd->cmd, n);
                const uint8_t crlf[2] = { '\r', '\n' };
                at_port_write(port_id, crlf, 2);
            }
            cmd->start_ms = at_port_get_time_ms(port_id);
            ctx->busy = true;
            ctx->echo_pending = ctx->echo_ignore;
        }
    }
    return 0;
}
