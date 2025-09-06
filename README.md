一个“无操作系统、轮询方式、已具备 DMA+空闲中断+RingBuffer 接收”的 AT 指令框架。  
特性一览：

* **多端口**：支持多个 AT 串口（`AT_MAX_PORTS` 可配）。
* **异步**：命令通过**队列**排队发送，**回调**异步返回结果。
* **行解析状态机**：以 `\n` 结尾按行解析（自动忽略 `\r`），稳定高效。
* **分发器**：区分**命令响应**与 **URC**，提供 **URC 前缀注册/反注册**。
* **日志**：轻量可关的调试日志（`AT_ENABLE_LOG` 宏）。
* **底层接口**：只需你实现两个函数（读/写串口缓冲区）即可无缝接入你现有的 DMA+RingBuffer。

---

## 目录结构

```
at-framework/
├─ include/
│  └─ at.h                 # 对外统一 API（Doxygen 注释齐全）
├─ port/
│  ├─ at_port.h            # 底层抽象接口（你来实现 at_port_read / at_port_write）
│  └─ at_port.c            # 默认空实现（占位），工程中请替换为你的平台实现
├─ core/
│  ├─ at_engine.h/.c       # 引擎：轮询主循环、发送调度、响应处理
│  ├─ at_queue.h/.c        # 命令队列（循环队列）
│  ├─ at_parser.h/.c       # 行解析状态机（\n 结尾）
│  ├─ at_dispatcher.h/.c   # 行分发：URC/响应；URC 注册/反注册
│  ├─ at_log.h/.c          # 日志模块（可宏控关闭）
└─ example/
   └─ main.c               # 使用示例
```

> **你只需要实现：**
>
> * `size_t at_port_read(uint8_t port_id, uint8_t *buf, size_t len);`
> * `size_t at_port_write(uint8_t port_id, const uint8_t *data, size_t len);`

---

## 快速上手

1. 在工程里把 `include/` 和 `core/` 加入头文件搜索路径，加入源码。
2. 用你的 UART DMA+RingBuffer 实现 `port/at_port.c` 中的两个函数。
3. 在应用中：

```c
#include "at.h"

static void my_urc(uint8_t port, const char *urc, void *arg) { /* ... */ }
static void my_resp(uint8_t port, const char *resp, bool ok, void *arg) { /* ... */ }

int main(void) {
    at_engine_init(1);                                // 1 个端口：port 0
    at_register_urc_handler(0, "RING", my_urc, NULL);// 注册 URC
    at_send_cmd(0, "AT", my_resp, NULL);             // 异步发送命令
    for (;;) {
        at_engine_poll();                            // 轮询：收发、解析、分发
        // 你的其他循环逻辑...
    }
}
```

> **注意**：命令字符串**不需要**带 `\r\n`，引擎会自动追加 `\r\n`。
> 解析器**只以 `\n` 判行**（会忽略 `\r`），“\n 结尾”约定。

---

## 源码

> 说明：**对外 API 的 Doxygen 注释**写在 `include/at.h`，其余模块注释从简。可根据需要再精炼。

### `include/at.h`（对外 API，Doxygen 注释齐全）

```c
/**
 * @file at.h
 * @brief AT 指令框架对外统一 API（无操作系统、轮询方式）。
 *
 * 功能特性：
 * - 多 AT 端口支持（可配置 AT_MAX_PORTS）
 * - 异步命令队列 + 回调，非阻塞
 * - 行级状态机解析（以 '\n' 结尾，忽略 '\r'）
 * - 分发器区分命令响应与 URC，支持 URC 前缀注册/反注册
 * - 轻量日志（AT_ENABLE_LOG）
 *
 * 使用示例：
 * @code
 * void urc_cb(uint8_t port, const char *urc, void *arg) { /* ... *\/ }
 * void resp_cb(uint8_t port, const char *resp, bool ok, void *arg) { /* ... *\/ }
 *
 * int main(void) {
 *     at_engine_init(1); // 只用 port 0
 *     at_register_urc_handler(0, "RING", urc_cb, NULL);
 *     at_send_cmd(0, "AT+GMR", resp_cb, NULL);
 *     for (;;) { at_engine_poll(); }
 * }
 * @endcode
 */
#ifndef AT_H
#define AT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/** @name 配置项（可在编译选项或此处修改）
 * @{ */
#ifndef AT_MAX_PORTS
/// 支持的最大 AT 端口数
#define AT_MAX_PORTS 1
#endif

#ifndef AT_MAX_QUEUE_SIZE
/// 每个端口的最大命令队列长度（含正在执行的那个）
#define AT_MAX_QUEUE_SIZE 8
#endif

#ifndef AT_MAX_CMD_LEN
/// 单条 AT 命令的最大长度（不含 CR/LF）
#define AT_MAX_CMD_LEN 128
#endif

#ifndef AT_MAX_RESP_LEN
/// 单条命令累计响应的最大长度（可覆盖多行）
#define AT_MAX_RESP_LEN 256
#endif

#ifndef AT_MAX_LINE_LEN
/// 解析器单行最大长度（URC/响应的行）
#define AT_MAX_LINE_LEN 256
#endif

#ifndef AT_MAX_URC_HANDLERS
/// 每个端口可注册的 URC 处理器上限
#define AT_MAX_URC_HANDLERS 10
#endif
/** @} */

/**
 * @brief 命令响应回调
 * @param port_id   源端口
 * @param response  累计的响应文本（中间行拼接，可能为空）
 * @param success   是否以 "OK" 结束（true=成功；false=ERROR/CME/CMS）
 * @param user_arg  用户参数
 */
typedef void (*at_resp_cb_t)(uint8_t port_id, const char *response, bool success, void *user_arg);

/**
 * @brief URC 回调（非命令响应的异步通知）
 * @param port_id   源端口
 * @param urc       完整 URC 文本（单行）
 * @param user_arg  用户参数
 */
typedef void (*at_urc_cb_t)(uint8_t port_id, const char *urc, void *user_arg);

/**
 * @brief 初始化 AT 引擎。
 *
 * 建立内部队列、解析器、分发器等。需在使用其它 API 前调用一次。
 *
 * @param port_count 使用的端口数量（1..AT_MAX_PORTS）；超过上限会被截断
 */
void at_engine_init(uint8_t port_count);

/**
 * @brief 引擎轮询（非阻塞）。
 *
 * 功能：
 * - 从各端口读取可用数据，按行解析
 * - 将行分发到 URC 或命令响应处理
 * - 若端口空闲，则发送队列中的下一条命令（并自动追加 CRLF）
 *
 * 应在主循环中高频调用以保证实时性。
 */
void at_engine_poll(void);

/**
 * @brief 注册 URC 处理器（按前缀匹配）。
 *
 * 任何以 @p prefix 开头的行，都会回调 @p cb。常见如 "+CMTI:", "RING" 等。
 *
 * @param port_id  端口号
 * @param prefix   URC 前缀（如 "+CMTI:"），不能为空字符串
 * @param cb       回调函数
 * @param user_arg 透传给回调的用户参数
 * @return 0 成功；-1 失败（如端口非法或已达上限）
 */
int at_register_urc_handler(uint8_t port_id, const char *prefix, at_urc_cb_t cb, void *user_arg);

/**
 * @brief 反注册 URC 处理器（按前缀）。
 *
 * @param port_id  端口号
 * @param prefix   先前注册的 URC 前缀
 * @return 0 成功；-1 未找到或参数错误
 */
int at_unregister_urc_handler(uint8_t port_id, const char *prefix);

/**
 * @brief 异步发送 AT 命令。
 *
 * 命令被入队，端口空闲时会自动发送（自动追加 "\r\n"）。命令完成（收齐 "OK"/"ERROR..."）
 * 后，通过回调异步报告结果。
 *
 * @note 命令字符串请**不要**包含 CR/LF。
 *
 * @param port_id  端口号
 * @param command  AT 命令字符串（如 "AT+GMR"）
 * @param cb       响应回调
 * @param user_arg 用户参数
 * @return 0 成功入队；-1 失败（如端口非法或队列满）
 */
int at_send_cmd(uint8_t port_id, const char *command, at_resp_cb_t cb, void *user_arg);

#endif /* AT_H */
```

---

### `port/at_port.h`（底层抽象接口：**你来实现**）

```c
/**
 * @file at_port.h
 * @brief 底层串口抽象接口（由用户实现以接入硬件：DMA+RingBuffer）。
 */
#ifndef AT_PORT_H
#define AT_PORT_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief 从指定端口的“接收环形缓冲区”读取数据（非阻塞）。
 * @param port_id 端口号
 * @param buf     输出缓冲区
 * @param len     希望读取的最大字节数
 * @return 实际读取的字节数（若无数据可读则为 0）
 */
size_t at_port_read(uint8_t port_id, uint8_t *buf, size_t len);

/**
 * @brief 将数据写入指定端口的“发送缓冲区/外设”（尽量非阻塞）。
 * @param port_id 端口号
 * @param data    待发送的数据
 * @param len     数据长度
 * @return 实际写入/排队发送的字节数
 */
size_t at_port_write(uint8_t port_id, const uint8_t *data, size_t len);

#endif /* AT_PORT_H */
```

### `port/at_port.c`（默认空实现；请用你的平台实现替换）

```c
/**
 * @file at_port.c
 * @brief 串口底层默认占位实现（请在你的工程中替换为真实硬件实现）。
 */
#include "at_port.h"

size_t at_port_read(uint8_t port_id, uint8_t *buf, size_t len) {
    (void)port_id; (void)buf; (void)len;
    return 0; // 默认：无数据（占位）
}

size_t at_port_write(uint8_t port_id, const uint8_t *data, size_t len) {
    (void)port_id; (void)data;
    return len;    // 默认：假定已全部写入（占位）
}
```

---

### `core/at_log.h/.c`（日志）

```c
/* core/at_log.h */
#ifndef AT_LOG_H
#define AT_LOG_H

#include <stdio.h>
#include <stdarg.h>

#ifndef AT_ENABLE_LOG
#define AT_ENABLE_LOG 0
#endif

#if AT_ENABLE_LOG
void at_log_print(const char *fmt, ...);
#define AT_LOG(fmt, ...) at_log_print(fmt, ##__VA_ARGS__)
#else
#define AT_LOG(fmt, ...) (void)0
#endif

#endif /* AT_LOG_H */
```

```c
/* core/at_log.c */
#include "at_log.h"

#if AT_ENABLE_LOG
void at_log_print(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf("[AT] ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}
#endif
```

---

### `core/at_queue.h/.c`（命令队列）

```c
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
```

```c
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
```

---

### `core/at_parser.h/.c`（按行解析）

```c
/* core/at_parser.h */
#ifndef AT_PARSER_H
#define AT_PARSER_H

#include <stdint.h>
#include <stddef.h>

typedef void (*at_line_cb_t)(uint8_t port_id, const char *line);

void at_parser_init(at_line_cb_t cb);
void at_parser_process(uint8_t port_id, const uint8_t *data, size_t len);

#endif /* AT_PARSER_H */
```

```c
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
```

---

### `core/at_dispatcher.h/.c`（分发器 + URC 注册/反注册）

```c
/* core/at_dispatcher.h */
#ifndef AT_DISPATCHER_H
#define AT_DISPATCHER_H

#include "at.h"

void at_dispatcher_init(void);
int  at_register_urc_handler  (uint8_t port_id, const char *prefix, at_urc_cb_t cb, void *user_arg);
int  at_unregister_urc_handler(uint8_t port_id, const char *prefix);
int  at_dispatcher_dispatch_line(uint8_t port_id, const char *line);

#endif /* AT_DISPATCHER_H */
```

```c
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
            AT_LOG("URC 分发 (port %d): %s", port_id, line);
            e->cb(port_id, line, e->arg);
            return 1; // 已作为 URC 处理
        }
    }
    return 0; // 非 URC
}
```

---

### `core/at_engine.h/.c`（引擎：轮询/调度/响应）

```c
/* core/at_engine.h */
#ifndef AT_ENGINE_H
#define AT_ENGINE_H
#include "at.h"
#endif /* AT_ENGINE_H */
```

```c
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
```

---

### `example/main.c`（示例）

```c
#include <stdio.h>
#include "at.h"

static void urc_ring(uint8_t port, const char *urc, void *arg) {
    (void)arg;
    printf("[URC] port=%d: %s\n", port, urc);
}

static void resp_print(uint8_t port, const char *resp, bool ok, void *arg) {
    (void)arg;
    printf("[RESP] port=%d, ok=%d\n", port, (int)ok);
    if (resp && resp[0]) printf("%s\n", resp);
}

int main(void) {
    // 假设只用 1 个端口：port 0
    at_engine_init(1);

    // 注册常见 URC
    at_register_urc_handler(0, "RING",  urc_ring,  NULL);
    at_register_urc_handler(0, "+CMTI", urc_ring,  NULL); // 短信到来示例

    // 发送几条命令
    at_send_cmd(0, "AT",      resp_print, NULL);
    at_send_cmd(0, "AT+GMR",  resp_print, NULL);
    at_send_cmd(0, "ATI",     resp_print, NULL);

    // 主循环
    for (;;) {
        at_engine_poll();
        // 你的其他逻辑...
    }

    return 0;
}
```

---

## 如何对接你的 DMA+RingBuffer（实现 `at_port_read/write`）

**接收（read）**

* 从**你的**“串口接收环形缓冲区”中取数据（非阻塞）。
* 每次最多拷贝 `len` 字节到 `buf` 并返回实际字节数。
* 若无数据则返回 0。引擎会循环读取直到返回 0 为止。
* 常见写法（伪代码）：

  ```c
  size_t at_port_read(uint8_t port_id, uint8_t *buf, size_t len) {
      return ringbuffer_read(&uart_rx[port_id], buf, len);
  }
  ```

**发送（write）**

* 将数据写入你的 UART 发送机制（寄存器/FIFO/DMA/TX ring）。
* 尽量**非阻塞**：只做“塞入发送缓冲区/触发 DMA”，立即返回。
* 返回写入/排队成功的字节数（可做分片/拥塞处理）。
* 常见写法（伪代码）：

  ```c
  size_t at_port_write(uint8_t port_id, const uint8_t *data, size_t len) {
      return uart_tx_write_nonblock(&uart_tx[port_id], data, len);
  }
  ```

---

## 扩展建议（可按需添加）

* **命令超时**：为每个端口维护“当前命令起始 tick”，若超时未收到最终行（OK/ERROR），回调超时失败并出队。
* **AT 回显处理**：若模块回显命令，可在 `engine_on_line` 中识别并忽略与当前命令相同的首行。
* **更多最终态**：如 `"NO CARRIER"` 等可作为失败终态纳入判断。
* **URC 优先级/模糊匹配**：现用“前缀严格匹配”，可增加更复杂的匹配器（正则/回调自定义匹配）。
* **日志后端**：将 `printf` 改为串口日志或 RTT。

---

## 编译选项

* 开启日志：`-DAT_ENABLE_LOG=1`
* 调整容量：在编译命令或 `at.h` 中重定义 `AT_MAX_*` 宏。

---
