/* core/at_engine.c */
#include <string.h>
#include "at_engine.h"
#include "at_port.h"
#include "at_queue.h"
#include "at_parser.h"
#include "at_dispatcher.h"
#include "at_log.h"

typedef struct {
    at_queue_t queue;  // 每端口独立队列
    bool       busy;   // 是否有命令正在等待最终结果
} at_port_context_t;

static at_port_context_t g_port_ctx[AT_MAX_PORTS];
static uint8_t           g_port_count = 1;

static void engine_on_line(uint8_t port_id, const char *line);

void at_engine_init(uint8_t port_count) {
    if (port_count < 1) port_count = 1;
    if (port_count > AT_MAX_PORTS) {
        AT_LOG("at_engine_init: port_count 超限，按 %d 处理", AT_MAX_PORTS);
        port_count = AT_MAX_PORTS;
    }
    g_port_count = port_count;
    for (uint8_t i = 0; i < g_port_count; ++i) {
        at_queue_init(&g_port_ctx[i].queue);
        g_port_ctx[i].busy = false;
    }
    at_parser_init(engine_on_line);
    at_dispatcher_init();
    AT_LOG("AT 引擎初始化完成, 端口数=%d", g_port_count);
}

static void finish_command(uint8_t port_id, ATCommand *cmd, bool success, const char *maybe_error_line) {
    cmd->resp_success = success;
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
    AT_LOG("命令完成 (port %d), success=%d", port_id, (int)cmd->resp_success);
}

static void engine_on_line(uint8_t port_id, const char *line) {
    if (port_id >= g_port_count) return;

    AT_LOG("端口 %d 收到行: %s", port_id, line);

    // 先尝试按 URC 处理
    if (at_dispatcher_dispatch_line(port_id, line)) return;

    // 若非 URC，作为响应处理（仅当有进行中的命令）
    at_port_context_t *ctx = &g_port_ctx[port_id];
    ATCommand *cmd = at_queue_front(&ctx->queue);
    if (ctx->busy && cmd) {
        bool is_ok    = (strcmp(line, "OK") == 0);
        bool is_error = (strncmp(line, "ERROR", 5) == 0) ||
                        (strncmp(line, "+CME ERROR", 10) == 0) ||
                        (strncmp(line, "+CMS ERROR", 10) == 0);
        if (is_ok || is_error) {
            finish_command(port_id, cmd, is_ok, is_error ? line : NULL);
            at_queue_pop(&ctx->queue);
            ctx->busy = false;
        } else {
            // 中间行，累加到 resp
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
                AT_LOG("警告: 响应缓冲区已满，发生截断 (port %d)", port_id);
            }
        }
    } else {
        // 没有进行中的命令且非 URC：忽略或按需上报
        AT_LOG("提示: 未处理的行 (port %d): %s", port_id, line);
    }
}

void at_engine_poll(void) {
    uint8_t buf[64];

    // 1) 读入并解析
    for (uint8_t p = 0; p < g_port_count; ++p) {
        size_t n;
        do {
            n = at_port_read(p, buf, sizeof(buf));
            if (n > 0) at_parser_process(p, buf, n);
        } while (n > 0);
    }

    // 2) 发送调度
    for (uint8_t p = 0; p < g_port_count; ++p) {
        at_port_context_t *ctx = &g_port_ctx[p];
        if (!ctx->busy) {
            ATCommand *next = at_queue_front(&ctx->queue);
            if (next) {
                size_t n = strlen(next->cmd);
                if (n > 0) {
                    AT_LOG("发送命令 (port %d): %s", p, next->cmd);
                    at_port_write(p, (const uint8_t*)next->cmd, n);
                    const uint8_t crlf[2] = {'\r','\n'};
                    at_port_write(p, crlf, 2);
                }
                ctx->busy = true;
            }
        }
    }
}

int at_send_cmd(uint8_t port_id, const char *command, at_resp_cb_t cb, void *user_arg) {
    if (port_id >= g_port_count || !command) return -1;
    at_port_context_t *ctx = &g_port_ctx[port_id];
    if (at_queue_push(&ctx->queue, command, cb, user_arg) != 0) return -1;
    AT_LOG("命令入队 (port %d): %s", port_id, command);

    // 若端口空闲，立即发送第一条
    if (!ctx->busy) {
        ATCommand *cmd = at_queue_front(&ctx->queue);
        if (cmd) {
            size_t n = strlen(cmd->cmd);
            if (n > 0) {
                AT_LOG("立即发送 (port %d): %s", port_id, cmd->cmd);
                at_port_write(port_id, (const uint8_t*)cmd->cmd, n);
                const uint8_t crlf[2] = {'\r','\n'};
                at_port_write(port_id, crlf, 2);
            }
            ctx->busy = true;
        }
    }
    return 0;
}
