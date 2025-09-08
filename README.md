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


## 编译选项

* 开启日志：`-DAT_ENABLE_LOG=1`
* 调整容量：在编译命令或 `at.h` 中重定义 `AT_MAX_*` 宏。

---
