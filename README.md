## ATCortex

嵌入式 AT 指令框架库，用于向 AT 模块发送命令并获得返回结果。

**特性**：
- 事件驱动的信号量阻塞模型，无空转轮询
- 支持异步/同步发送
- 支持二进制数据接收（prompt 匹配后收定长数据）
- 支持 URC（Unsolicited Result Code）注册与回调
- 多实例支持（每个 context 独立线程）

### 架构

ATCortex 采用**单线程事件循环**模型。`atc_process(context)` 内部死循环，通过信号量阻塞等待事件：

```
UART ISR → atc_receive_data() ──give_isr──┐
其他线程 → atc_send_xxx()      ──give─────┼──→ atc_process 唤醒处理
其他线程 → atc_urc_register()  ──阻塞等待─┘    (同步，信号量阻塞直到注册完成)
其他线程 → atc_urc_unregister()──阻塞等待─┘
超时 → semaphore_take 超时返回 ────────────┘
```

### 依赖注入

库本身与 OS 无关，所有系统调用通过 `struct atc_interface` 注入。使用前必须调用 `atc_interface_register()` 注册以下**全部**接口：

| 函数指针 | 说明 |
|---------|------|
| `atc_malloc` / `atc_free` | 内存分配/释放 |
| `atc_queue_create` / `atc_queue_send` / `atc_queue_recv` | 消息队列 |
| `atc_log` | 日志输出 |
| `atc_send` | 硬件数据发送 |
| `atc_semaphore_create_binary` / `atc_semaphore_take` / `atc_semaphore_give` / `atc_semaphore_delete` | 信号量（线程上下文） |
| `atc_semaphore_give_isr` | 信号量 give（**ISR 安全版本**） |
| `atc_get_tick_ms` | 获取系统毫秒 tick（单调递增） |

### 使用方法

**1. 实现并注册底层接口**

```c
#include <ATCortex.h>

static struct atc_interface at_interface = {
    .atc_malloc = my_malloc,
    .atc_free   = my_free,
    // ... 其余接口 ...
    .atc_semaphore_give_isr = my_sem_give_isr,
    .atc_get_tick_ms        = my_get_tick_ms,
};
atc_interface_register(&at_interface);
```

**2. 初始化 context，在线程中启动 `atc_process`**

```c
static struct atc_context at_ctx;

void at_thread(void)
{
    atc_init(&at_ctx);
    atc_process(&at_ctx);  // 内部死循环，永不返回
}
```

**3. UART 接收中断中推送数据**

```c
void UART_IRQHandler(void)
{
    uint8_t byte;
    while (uart_rx_ready()) {
        byte = uart_read_byte();
        atc_receive_data(&at_ctx, (char *)&byte, 1);
    }
}
```

**4. 任意线程中发送 AT 命令**

```c
// 同步发送，等待 OK/ERROR，超时 2s
char rep_buf[64];
size_t rep_len = sizeof(rep_buf);
enum atc_result result;
atc_send_sync(&at_ctx, "AT+RST\r\n", 7, &result, rep_buf, &rep_len, 2000);

// 异步发送
atc_send_async(&at_ctx, "AT\r\n", 4, my_handler, 1000);

// 同步发送+等待提示符+接收二进制数据
char prompt[] = "CONNECT OK";
char data_buf[256];
size_t data_len = sizeof(data_buf);
atc_send_with_prompt_binary_rx_sync(&at_ctx, cmd, cmd_len,
    prompt, strlen(prompt), 100,  // 收到 prompt 后再收 100 字节
    &result, data_buf, &data_len, 5000);
```

### API 速查

| API | 说明 |
|-----|------|
| `atc_interface_register(&if)` | 注册底层接口（必须先调用） |
| `atc_init(&ctx)` | 初始化上下文 |
| `atc_process(&ctx)` | 阻塞事件循环（永不返回） |
| `atc_receive_data(&ctx, data, len)` | 推送接收数据（ISR 中调用） |
| `atc_send_sync(...)` | 同步发送，等待 OK/ERROR |
| `atc_send_async(...)` | 异步发送，结果通过回调通知 |
| `atc_send_with_prompt_binary_rx_sync(...)` | 同步发送，匹配 prompt 后接收定长二进制数据 |
| `atc_send_with_prompt_binary_rx_async(...)` | 上述的异步版本 |
| `atc_urc_register(&ctx, prefix, handler)` | 同步注册 URC 回调，返回分配的ID（>0） |
| `atc_urc_unregister(&ctx, id)` | 同步反注册，根据ID移除 URC 回调 |

### 关键缓冲区大小（ATCortex.h）

| 宏 | 默认值 | 说明 |
|----|-----|------|
| `ATC_RX_BUFFER_SIZE` | 256 | 环形接收缓冲区 |
| `ATC_RX_LINE_MAX_SIZE` | 256 | 单行最大字节 |
| `ATC_RX_RESPONSE_MAX` | 512 | 响应累计最大字节 |
| `ATC_PROMPT_STACK_MAX_DEPTH` | 20 | prompt 匹配栈深度 |

### 注意事项

- `atc_semaphore_give_isr` 从 UART ISR 上下文调用，必须 ISR 安全
- `atc_process` 内部循环不返回，调用线程将其作为主循环
- 多实例：每个 context 一个独立线程，各自调用 `atc_process(ctx)`
