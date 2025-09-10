
---

# ATCortex

一个轻量、可移植的 **AT 指令框架**。特点：

* **零依赖/易移植**：不依赖C标准库以外的其他库。仅需实现 `port/at_port.*` 的 4 个函数
* **多端口**：多块模块/多路串口同时管理
* **命令队列**：异步发送、按行解析、回调返回
* **URC 分发**：基于前缀的异步提示（URC）处理
* **事务发送**：支持两类数据阶段

  * `PROMPT`：等待提示符（默认 `"> "`）再发负载
  * `LENGTH`：命令发出后**立即**发送定长负载
* **回显策略**：可按端口忽略首行回显
* **超时与日志**：统一毫秒超时；日志可开关


---

## 目录结构

```
.
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
└── src
    ├── core
    │   ├── at_dispatcher.c/.h   # URC 分发器
    │   ├── at_engine.c/.h       # 引擎（队列、状态机、超时、事务）
    │   ├── at_log.c/.h          # 日志封装（可开关）
    │   ├── at_parser.c/.h       # 行解析器（'\n' 结束，忽略 '\r'）
    │   ├── at_queue.c/.h        # 循环队列（命令上下文）
    ├── example
    │   ├── main.c               # 最小示例
    │   └── test.c               # 自测/演示用例（含事务、URC、超时等）
    ├── include
    │   └── at.h                 # **对外 API（doxygen 中文注释）**
    └── port
        ├── at_port.c            # 需按平台实现（串口/时钟）
        └── at_port.h
```

---

## 快速开始

### 1) 构建(添加到项目)

添加.c  src/core/\*.c和src/port/\*.c  
添加.h  src/include  
### 2) 移植 `port/at_port.*`

框架只依赖以下 4 个函数（**非阻塞**语义）：

```c
void    at_port_init(uint8_t port_id);                       // 初始化串口/端口
size_t  at_port_read(uint8_t port_id, uint8_t *buf, size_t); // 读若干字节，返回实际读到
size_t  at_port_write(uint8_t port_id, const uint8_t*, size_t); // 写若干字节，返回写入数
uint32_t at_port_get_time_ms(uint8_t port_id);               // 单调递增毫秒计时
```

实现建议：

* `read()` 返回当前可读字节，不阻塞；通常从 **RX 环形缓冲**中取数据。
* `write()` 尽量一次性写入；若底层非阻塞，返回短写即可，框架会在后续轮询继续推进。
* `get_time_ms()` 返回**单调递增**的毫秒时间（溢出即可按无符号回绕处理）。
* 如在中断接收，ISR 仅写入环形缓冲，由 `read()` 在任务/主循环里取走。

### 3) 最小用例

```c
#include "at.h"
#include <stdio.h>

static void resp_cb(uint8_t port, const char *resp, bool ok, void *arg) {
    (void)arg;
    printf("[RESP] port=%u %s\n", port, ok ? "OK" : "FAIL");
    if (resp && resp[0]) puts(resp);
}

static void urc_cb(uint8_t port, const char *urc, void *arg) {
    (void)arg;
    printf("[URC]  port=%u: %s\n", port, urc);
}

int main(void) {
    bool echo_map[] = { true, false };     // 端口0忽略首行回显，端口1不忽略
    at_engine_init_ex(2, echo_map);        // 初始化引擎 + 回显策略
    at_register_urc_handler(0, "+CMTI", urc_cb, NULL);

    at_send_cmd(0, "AT", resp_cb, NULL);   // 发送命令（默认超时）

    for (;;) {
        at_engine_poll();                  // 周期性轮询（主循环/单任务）
        // 其他业务...
    }
}
```

---

## API 速览

> ⚠️ 指针参数（如事务负载、终止符、提示串）在回调返回之前必须保持有效。

**初始化与轮询**

* `void at_engine_init(uint8_t port_count);`
* `void at_engine_init_ex(uint8_t port_count, const bool *echo_ignore_map);`
  每端口 `echo_ignore_map[i] = true` 时，发送后**丢弃第一行与命令完全一致的回显**。
* `void at_engine_poll(void);`
  建议在主循环频繁调用；引擎负责：读串口→解析行→分发URC→拼接响应→推进事务→检测超时。

**URC**

* `int at_register_urc_handler(uint8_t port, const char *prefix, at_urc_cb_t cb, void *arg);`
* `int at_unregister_urc_handler(uint8_t port, const char *prefix);`
  行以某前缀开头即认为是 URC 并回调；否则作为**当前命令响应**拼接。

**发送**

* `int at_send_cmd(uint8_t port, const char *cmd, at_resp_cb_t cb, void *arg);`
* `int at_send_cmd_ex(uint8_t port, const char *cmd, uint32_t timeout_ms, at_resp_cb_t cb, void *arg);`
* `int at_send_cmd_txn(uint8_t port, const char *cmd, const at_txn_desc_t *txn, uint32_t timeout_ms, at_resp_cb_t cb, void *arg);`
* 便捷事务：

  * `at_send_cmd_txn_prompt(...)`（等待提示后发送负载，可带终止符）
  * `at_send_cmd_txn_len(...)`（命令后立即发送定长负载，可带终止符）

**事务类型**

```c
typedef enum { AT_TXN_NONE=0, AT_TXN_PROMPT=1, AT_TXN_LENGTH=2 } at_txn_type_t;

typedef struct {
    at_txn_type_t  type;
    const uint8_t *payload;     size_t payload_len;
    const uint8_t *terminator;  size_t term_len;   // 可为 NULL/0
    const char    *prompt;      size_t prompt_len; // PROMPT 用；NULL=默认 "> "
} at_txn_desc_t;
```

**回调**

```c
typedef void (*at_resp_cb_t)(uint8_t port_id, const char *response, bool success, void *user_arg);
typedef void (*at_urc_cb_t )(uint8_t port_id, const char *urc,      void *user_arg);
```

* `response` 为**累计响应**（多行以 `\n` 分隔，**不含终态行**），可能包含提示行 `"> "`（见“行为细节”）。
* `success == true` 当终态为 `"OK"` 或 `"SEND OK"`；失败识别 `"ERROR"`, `"+CME ERROR"`, `"+CMS ERROR"`, `"SEND FAIL"`。

---

## 行为细节

* **按行解析**：`\n` 结束，**忽略所有 `\r`**；空行不回调。
* **回显抑制**：若启用且**首行与命令完全一致**，该行会被丢弃；其余行照常处理。
* **URC 优先**：先尝试 URC 分发；未命中才拼入当前命令响应。
* **事务阶段行抑制**：进入数据阶段（发送负载/终止符）时，**临时关闭行处理**，防止把 payload 中的换行误识别为响应/URC；阶段结束后自动恢复。
* **PROMPT 默认值**：`NULL` 表示提示串为 **`"> "`（带空格）**。若固件提示为 `">"` 或其他，自行传入并设置 `prompt_len`（或置 0 由库自动计算）。
* **终态行示例**：

  * 成功：`OK`、`SEND OK`
  * 失败：`ERROR`、`+CME ERROR: <n>`、`+CMS ERROR: <n>`、`SEND FAIL`
    若你的模块还有其它终态，请在 `core/at_engine.c` 的判定处扩展。
* **时间基准**：所有超时为毫秒，来自 `at_port_get_time_ms()`。请确保该计时单调递增且频率稳定。

---

## 常见问题（FAQ）

**Q1：模块回显导致响应重复？**
A：为端口启用“忽略首行回显”：`at_engine_init_ex(..., echo_map)`，并确保设备真的会**逐字回显命令**（完全一致）——否则无法匹配丢弃。

**Q2：`at_port_read()`/`write()` 是否要阻塞？**
A：不需要、**也不建议**。这两个函数都应尽快返回，框架会在后续 `at_engine_poll()` 中继续推进。

**Q3：如何区分 URC 与响应？**
A：注册 URC 前缀（如 `"+CMTI"`、`"RING"`）。以该前缀开头的行会被**优先**当作 URC 处理，不会进入当前命令的响应拼接。

---

## 编译期配置宏

定义在src/include/at.h和src/core/at_log.h

| 宏名                      | 默认值 | 说明                        |
| ----------------------- | --- | ------------------------- |
| `AT_MAX_PORTS`          | 2   | 最大端口数                     |
| `AT_MAX_QUEUE_SIZE`     | 8   | 每端口队列长度                   |
| `AT_MAX_CMD_LEN`        | 128 | 单条命令最大长度（不含 CR/LF）        |
| `AT_MAX_RESP_LEN`       | 256 | 单条命令累计响应最大长度              |
| `AT_MAX_LINE_LEN`       | 256 | 单行解析缓冲大小                  |
| `AT_MAX_URC_HANDLERS`   | 10  | 每端口 URC 处理器上限             |
| `AT_DEFAULT_TIMEOUT_MS` | 100 | 默认超时（毫秒）                  |
| `AT_ENABLE_LOG`         | 未定义 | 定义后启用日志（`AT_LOG(...)` 输出） |

---

## 进阶示例：事务发送

**PROMPT（默认提示 `"> "`，负载 + Ctrl+Z 终止）**

```c
const uint8_t msg[] = "HELLO";
const uint8_t z = 0x1A; // Ctrl+Z
at_send_cmd_txn_prompt(0, "AT+CMGS=5",
    msg, sizeof(msg) - 1,
    &z, 1,
    NULL,              // 使用默认 "> "（带空格）
    5000,
    resp_cb, NULL);
```

**LENGTH（命令后立即发送定长负载）**

```c
const uint8_t bin[] = {0x01,0x02,0x03};
at_send_cmd_txn_len(1, "AT#BIN=3",
    bin, sizeof(bin),
    NULL, 0,
    1000,
    resp_cb, NULL);
```

---

## 线程/任务模型

* 推荐在**单一上下文**（主循环或某个任务）周期性调用 `at_engine_poll()`。
* 不建议在**中断**里调用对外 API；ISR 仅做收发缓冲搬运。
* 若你必须在多任务中使用，请自行保证调用的**互斥**（本库默认无锁）。

---