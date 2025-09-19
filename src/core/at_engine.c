/*
 * @file at_engine.c
 * @brief Core AT engine implementation with timeout, echo handling, and transactional commands.
 */

#include "at.h"
#include "../port/at_port.h"
#include "at_queue.h"
#include "at_parser.h"
#include "at_dispatcher.h"
#include "at_log.h"

#include <string.h>

/** 每端口（port）运行时上下文 */
typedef struct {
    at_queue_t queue;       /**< 命令队列 */
    bool       busy;        /**< 是否有命令在执行 */
    bool       echo_ignore; /**< 是否忽略首个回显行 */
    bool       echo_pending;/**< 发送后等待丢弃的回显行仍未到达 */
    bool       suppress_lines;  /**< 二进制阶段：抑制行处理（避免把 payload 当响应） */
} at_port_context_t;

static at_port_context_t g_port_ctx[AT_MAX_PORTS];
static uint8_t           g_port_count = 1;

/* 行回调：由解析器触发 */
static void engine_on_line(uint8_t port_id, const char *line);

/* 工具：确保 PROMPT 模式有默认提示（"> "） */
static inline void txn_ensure_prompt_defaults(ATCommand *cmd) {
    if (cmd->txn.type != AT_TXN_PROMPT && cmd->txn.type != AT_TXN_PROMPT_RX && cmd->txn.type != AT_TXN_PROMPT_BINARY_RX) return;
    if (!cmd->txn.prompt) {
        cmd->txn.prompt     = "> ";
        cmd->txn.prompt_len = 2;
    } else if (cmd->txn.prompt_len == 0) {
        cmd->txn.prompt_len = strlen(cmd->txn.prompt);
    }
}

/* 工具：确保二进制接收的终止符有长度 */
static inline void txn_ensure_rx_terminator_len(ATCommand *cmd) {
    if (cmd->txn.type != AT_TXN_PROMPT_BINARY_RX) return;
    if (cmd->txn.rx_terminator && cmd->txn.rx_term_len == 0) {
        cmd->txn.rx_term_len = strlen(cmd->txn.rx_terminator);
    }
}

/* 结束命令：收尾拼接/裁剪响应并回调 */
static void finish_command(uint8_t port_id, ATCommand *cmd, bool success, const char *maybe_error_line) {
    cmd->resp_success = success;
    cmd->line_data_receiving = false;
    cmd->binary_receiving = false;

    if (!success && maybe_error_line && strcmp(maybe_error_line, "TIMEOUT") == 0) {
        if (cmd->cb) cmd->cb(port_id, "TIMEOUT", false, cmd->arg);
        AT_LOG("Command timeout (port %d)", port_id);
        return;
    }

    if (cmd->txn.type != AT_TXN_PROMPT_BINARY_RX) {
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
    } else {
        cmd->resp[cmd->resp_len] = '\0';
    }

    if (cmd->cb) cmd->cb(port_id, cmd->resp, cmd->resp_success, cmd->arg);
    AT_LOG("Command finished (port %d), success=%d", port_id, (int)cmd->resp_success);
}

/**
 * @brief 在原始字节流中扫描提示串
 */
static size_t engine_scan_prompt(uint8_t port_id, const uint8_t *data, size_t len) {
    ATCommand *cmd = at_queue_front(&g_port_ctx[port_id].queue);
    if (!cmd || !cmd->txn_enabled || cmd->prompt_received) return 0;
    if (cmd->txn.type != AT_TXN_PROMPT && cmd->txn.type != AT_TXN_PROMPT_RX && cmd->txn.type != AT_TXN_PROMPT_BINARY_RX) return 0;

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
                AT_LOG("Prompt matched (port %d)", port_id);
                if (cmd->txn.type == AT_TXN_PROMPT_RX) {
                    cmd->line_data_receiving = true;
                    AT_LOG("PROMPT_RX: Data receiving started (port %d)", port_id);
                } else if (cmd->txn.type == AT_TXN_PROMPT_BINARY_RX) {
                    cmd->binary_receiving = true;
                    txn_ensure_rx_terminator_len(cmd);
                    cmd->resp_len = 0;
                    cmd->resp[0] = '\0';
                    at_parser_reset(port_id);
                    AT_LOG("PROMPT_BINARY_RX: Binary data receiving started (port %d)", port_id);
                }
                return i + 1;
            }
        } else {
            m = (ch == pat[0]) ? 1u : 0u;
        }
    }
    cmd->prompt_matched = m;
    return 0;
}

/* 推进事务：按顺序发送 payload 与 terminator */
static void engine_progress_txn(uint8_t port_id, at_port_context_t *ctx, ATCommand *cmd) {
    if (!cmd->txn_enabled || cmd->txn.type == AT_TXN_PROMPT_RX || cmd->txn.type == AT_TXN_PROMPT_BINARY_RX) return;
    if (cmd->txn.type == AT_TXN_PROMPT && !cmd->prompt_received) return;
    if (!cmd->payload_started) {
        ctx->suppress_lines = true;
        cmd->payload_started = true;
    }
    while (cmd->txn_sent < cmd->txn.payload_len) {
        const uint8_t *p = cmd->txn.payload + cmd->txn_sent;
        size_t n = at_port_write(port_id, p, cmd->txn.payload_len - cmd->txn_sent);
        if (n > 0) cmd->txn_sent += n; else return;
    }
    while (cmd->txn.term_len > 0 && cmd->term_sent < cmd->txn.term_len) {
        const uint8_t *t = cmd->txn.terminator + cmd->term_sent;
        size_t n = at_port_write(port_id, t, cmd->txn.term_len - cmd->term_sent);
        if (n > 0) cmd->term_sent += n; else return;
    }
    ctx->suppress_lines = false;
}

/* 引擎初始化：端口、队列、解析器、分发器 */
void at_engine_init(uint8_t port_count) {
    if (port_count < 1) port_count = 1;
    if (port_count > AT_MAX_PORTS) port_count = AT_MAX_PORTS;
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

/* 扩展初始化：设置每端口是否忽略回显 */
void at_engine_init_ex(uint8_t port_count, const bool *echo_ignore_map) {
    at_engine_init(port_count);
    if (echo_ignore_map) {
        for (uint8_t i = 0; i < g_port_count; ++i) {
            g_port_ctx[i].echo_ignore = echo_ignore_map[i];
        }
    }
    AT_LOG("AT engine extended initialization: echo ignore policy set per port");
}

/* 将收到的行追加到命令的响应缓冲区 */
static void append_line_to_resp(ATCommand *cmd, const char *line, uint8_t port_id) {
    size_t n = strlen(line);
    if (cmd->resp_len + n + 1 < AT_MAX_RESP_LEN) {
        memcpy(cmd->resp + cmd->resp_len, line, n);
        cmd->resp_len += n;
        cmd->resp[cmd->resp_len++] = '\n';
        cmd->resp[cmd->resp_len] = '\0';
    } else {
        AT_LOG("Warning: Response buffer overflow, truncating (port %d)", port_id);
    }
}

/**
 * @brief 处理二进制数据流。
 */
static size_t engine_process_binary_data(uint8_t port_id, const uint8_t *data, size_t len) {
    ATCommand *cmd = at_queue_front(&g_port_ctx[port_id].queue);
    if (!cmd) return len;

    if (cmd->txn.rx_len > 0) {
        size_t bytes_to_copy = len;
        size_t space_left = (AT_MAX_RESP_LEN > cmd->resp_len) ? (AT_MAX_RESP_LEN - cmd->resp_len - 1) : 0;
        size_t bytes_needed = cmd->txn.rx_len - cmd->binary_rx_count;
        if (bytes_to_copy > bytes_needed) bytes_to_copy = bytes_needed;
        if (bytes_to_copy > space_left) {
            bytes_to_copy = space_left;
            AT_LOG("Warning: Response buffer overflow on binary RX (port %d)", port_id);
        }
        if (bytes_to_copy > 0) {
            memcpy(cmd->resp + cmd->resp_len, data, bytes_to_copy);
            cmd->resp_len += bytes_to_copy;
            cmd->binary_rx_count += bytes_to_copy;
        }
        if (cmd->binary_rx_count >= cmd->txn.rx_len) {
            finish_command(port_id, cmd, true, NULL);
            at_queue_pop(&g_port_ctx[port_id].queue);
            g_port_ctx[port_id].busy = false;
        }
        return bytes_to_copy;
    }
    else if (cmd->txn.rx_terminator) {
        const char *term = cmd->txn.rx_terminator;
        size_t term_len = cmd->txn.rx_term_len;
        size_t consumed = 0;
        for (size_t i = 0; i < len; ++i, ++consumed) {
            char ch = data[i];
            if (ch == term[cmd->rx_term_matched]) {
                cmd->rx_term_matched++;
            } else {
                if (cmd->rx_term_matched > 0) {
                    size_t space_left = (AT_MAX_RESP_LEN > cmd->resp_len) ? (AT_MAX_RESP_LEN - cmd->resp_len - 1) : 0;
                    size_t flush_len = cmd->rx_term_matched;
                    if (flush_len > space_left) flush_len = space_left;
                    if (flush_len > 0) {
                        memcpy(cmd->resp + cmd->resp_len, term, flush_len);
                        cmd->resp_len += flush_len;
                    }
                }
                cmd->rx_term_matched = (ch == term[0]) ? 1 : 0;
                if (cmd->rx_term_matched == 0) {
                    if (cmd->resp_len < AT_MAX_RESP_LEN - 1) {
                        cmd->resp[cmd->resp_len++] = ch;
                    }
                }
            }
            if (cmd->rx_term_matched == term_len) {
                finish_command(port_id, cmd, true, NULL);
                at_queue_pop(&g_port_ctx[port_id].queue);
                g_port_ctx[port_id].busy = false;
                return consumed + 1;
            }
        }
        return consumed;
    }
    return len;
}

/**
 * @brief 行解析回调函数
 */
static void engine_on_line(uint8_t port_id, const char *line) {
    if (port_id >= g_port_count) return;
    at_port_context_t *ctx = &g_port_ctx[port_id];
    ATCommand *cmd = at_queue_front(&ctx->queue);

    if (ctx->busy && cmd && cmd->binary_receiving) return;
    if (ctx->suppress_lines) return;
    if (ctx->busy && cmd && cmd->txn.type == AT_TXN_PROMPT_BINARY_RX && !cmd->prompt_received) return;
    if (ctx->busy && cmd && ctx->echo_ignore && ctx->echo_pending) {
        if (strcmp(line, cmd->cmd) == 0) {
            AT_LOG("Echo ignored (port %d): %s", port_id, line);
            ctx->echo_pending = false;
            return;
        }
    }
    if (ctx->busy && cmd && cmd->line_data_receiving) {
        bool is_ok = (strcmp(line, "OK") == 0) || (strcmp(line, "SEND OK") == 0);
        bool is_error = (strncmp(line, "ERROR", 5) == 0) || strncmp(line, "+CME ERROR", 10) == 0 || strncmp(line, "+CMS ERROR", 10) == 0 || strncmp(line, "SEND FAIL", 9) == 0;
        if (is_ok || is_error) {
            finish_command(port_id, cmd, is_ok, is_error ? line : NULL);
            at_queue_pop(&ctx->queue);
            ctx->busy = false;
            ctx->echo_pending = false;
        } else {
            append_line_to_resp(cmd, line, port_id);
        }
        return;
    }
    if (at_dispatcher_dispatch_line(port_id, line)) return;
    if (ctx->busy && cmd) {
        bool is_ok = (strcmp(line, "OK") == 0) || (strcmp(line, "SEND OK") == 0);
        bool is_error = (strncmp(line, "ERROR", 5) == 0) || strncmp(line, "+CME ERROR", 10) == 0 || strncmp(line, "+CMS ERROR", 10) == 0 || strncmp(line, "SEND FAIL", 9) == 0;
        if (is_ok || is_error) {
            finish_command(port_id, cmd, is_ok, is_error ? line : NULL);
            at_queue_pop(&ctx->queue);
            ctx->busy = false;
            ctx->echo_pending = false;
        } else {
            append_line_to_resp(cmd, line, port_id);
        }
    } else {
        AT_LOG("Info: Unhandled line (port %d): %s", port_id, line);
    }
}

/**
 * @brief 引擎主轮询函数
 */
void at_engine_poll(void) {
    uint8_t buf[64];
    for (uint8_t p = 0; p < g_port_count; ++p) {
        at_port_context_t *ctx = &g_port_ctx[p];
        size_t n_read;
        do {
            n_read = at_port_read(p, buf, sizeof(buf));
            if (n_read > 0) {
                ATCommand *cmd = at_queue_front(&ctx->queue);
                size_t consumed = 0;
                if (ctx->busy && cmd) {
                    if (cmd->binary_receiving) {
                        consumed = engine_process_binary_data(p, buf, n_read);
                    } else if (cmd->txn_enabled && !cmd->prompt_received) {
                        bool was_waiting_for_binary_prompt = (cmd->txn.type == AT_TXN_PROMPT_BINARY_RX);
                        consumed = engine_scan_prompt(p, buf, n_read);
                        if (consumed > 0 && was_waiting_for_binary_prompt && cmd->binary_receiving) {
                            
                            
                            const uint8_t *remnant_start = buf + consumed;
                            size_t remnant_len = n_read - consumed;
                            size_t skipped_count = 0;

                            // 跳过紧跟在 prompt 后的行尾结束符 (\r\n or \n)
                            if (remnant_len >= 2 && remnant_start[0] == '\r' && remnant_start[1] == '\n') {
                                skipped_count = 2;
                            } else if (remnant_len >= 1 && remnant_start[0] == '\n') {
                                skipped_count = 1;
                            }
                            
                            if (skipped_count > 0) {
                                AT_LOG("Skipped %u line ending chars after prompt.", (unsigned)skipped_count);
                                remnant_start += skipped_count;
                                remnant_len -= skipped_count;
                                consumed += skipped_count;
                            }
                            
                            // 将真正干净的数据交给二进制处理器
                            if (remnant_len > 0) {
                                consumed += engine_process_binary_data(p, remnant_start, remnant_len);
                            }
                        }
                    }
                }
                if (n_read > consumed) {
                    at_parser_process(p, buf + consumed, n_read - consumed);
                }
            }
        } while (n_read > 0);
    }

    for (uint8_t p = 0; p < g_port_count; ++p) {
        at_port_context_t *ctx = &g_port_ctx[p];
        if (ctx->busy) {
            ATCommand *cmd = at_queue_front(&ctx->queue);
            if (cmd) {
                uint32_t now = at_port_get_time_ms(p);
                if (now - cmd->start_ms >= cmd->timeout_ms) {
                    AT_LOG("Command timeout (port %d): %s, elapsed=%u ms", p, cmd->cmd, (unsigned)(now - cmd->start_ms));
                    finish_command(p, cmd, false, "TIMEOUT");
                    at_queue_pop(&ctx->queue);
                    ctx->busy = false;
                    ctx->echo_pending = false;
                    ctx->suppress_lines = false;
                }
            }
        }
    }

    for (uint8_t p = 0; p < g_port_count; ++p) {
        at_port_context_t *ctx = &g_port_ctx[p];
        if (!ctx->busy) {
            ATCommand *next = at_queue_front(&ctx->queue);
            if (next) {
                at_port_write(p, (const uint8_t*)next->cmd, strlen(next->cmd));
                at_port_write(p, (const uint8_t*)"\r\n", 2);
                AT_LOG("Sending command (port %d): %s", p, next->cmd);
                if (next->txn_enabled) {
                    if (next->txn.type == AT_TXN_LENGTH) {
                        next->prompt_received = true;
                        ctx->suppress_lines = true;
                    } else if (next->txn.type == AT_TXN_PROMPT || next->txn.type == AT_TXN_PROMPT_RX || next->txn.type == AT_TXN_PROMPT_BINARY_RX) {
                        txn_ensure_prompt_defaults(next);
                        next->prompt_matched = 0;
                        next->prompt_received = false;
                    }
                }
                next->start_ms = at_port_get_time_ms(p);
                ctx->busy = true;
                ctx->echo_pending = ctx->echo_ignore;
                ctx->suppress_lines = (next->txn_enabled && next->txn.type == AT_TXN_LENGTH);
            }
        }
    }
    
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
    if (at_queue_push(&g_port_ctx[port_id].queue, command, cb, user_arg) != 0) return -1;
    AT_LOG("Command queued (port %d): %s", port_id, command);
    return 0;
}

int at_send_cmd_ex(uint8_t port_id, const char *command, uint32_t timeout_ms, at_resp_cb_t cb, void *user_arg) {
    if (port_id >= g_port_count || !command) return -1;
    if (at_queue_push_ex(&g_port_ctx[port_id].queue, command, timeout_ms, cb, user_arg) != 0) return -1;
    AT_LOG("Command queued (port %d): %s (timeout %u ms)", port_id, command, (unsigned)timeout_ms);
    return 0;
}

int at_send_cmd_txn(uint8_t port_id, const char *command, const at_txn_desc_t *txn,
                    uint32_t timeout_ms, at_resp_cb_t cb, void *user_arg)
{
    if (port_id >= g_port_count || !command || !txn) return -1;
    if (txn->type < AT_TXN_PROMPT || txn->type > AT_TXN_PROMPT_BINARY_RX) return -1;
    if (at_queue_push_txn(&g_port_ctx[port_id].queue, command, txn, timeout_ms, cb, user_arg) != 0) return -1;
    uint32_t actual_to = (timeout_ms == 0) ? AT_DEFAULT_TIMEOUT_MS : timeout_ms;
    AT_LOG("Command (txn) queued (port %d): %s (timeout %u ms, type=%d)", port_id, command, (unsigned)actual_to, (int)txn->type);
    return 0;
}