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

库本身与 OS 无关，系统调用通过 `struct atc_interface` 注入，每个实例的数据发送函数通过 `atc_init` 单独注入。使用前必须调用 `atc_interface_register()` 注册以下**全部**接口：

| 函数指针 | 说明 |
|---------|------|
| `atc_malloc` / `atc_free` | 内存分配/释放 |
| `atc_queue_create` / `atc_queue_send` / `atc_queue_recv` | 消息队列 |
| `atc_log` | 日志输出 |
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

// 每个实例可以注入不同的发送实现
static enum atc_result uart1_send(const char *data, size_t length)
{
    return uart_write(uart1, data, length) == length ? ATC_SUCCESS : ATC_ERROR;
}

void at_thread(void)
{
    atc_init(&at_ctx, uart1_send);
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

### 收发示例

**接收机制**：库按行（`\n` 结尾）处理数据——每行先与**已注册**的 URC 前缀做行首匹配，命中则只调 URC 回调、**不进响应缓冲区**；未命中的行累积进响应缓冲区（保留 `\r\n`，空行忽略）。当某行以内置结束符开头时，该行同样进缓冲区，随后本次命令结束并按结束符表返回结果码：

| 行首匹配 | 返回结果 |
|---------|---------|
| `OK` | `ATC_SUCCESS` |
| `ERROR` | `ATC_ERROR` |
| `BUSY_ERROR` | `ATC_BUSY`（模块忙） |

注意：一行是否算 URC 只取决于其前缀是否已通过 `atc_urc_register` 注册，与行内容（如 `+` 开头）无关。`+CSQ:` 注册前只是普通响应行。因此同一条命令有两种互斥的收法：不注册 URC，结果从 `response_buf` 拿（示例 1）；注册了 URC，响应也会走回调，只能用信号量等（示例 3）。

#### 1. 同步发送 + 解析响应（最常用）

```
发送: AT+CSQ\r\n
回复: +CSQ: 24,0\r\n   ← 进 response_buf
      \r\n             ← 空行忽略
      OK\r\n           ← 进 response_buf，并结束命令，result = ATC_SUCCESS
```

```c
enum atc_result result;
char rx_buf[64];
size_t rx_len = sizeof(rx_buf);
atc_send_sync(&at_ctx, "AT+CSQ\r\n", 8, &result, rx_buf, &rx_len, 1000);
if (result == ATC_SUCCESS) {
    int rssi = 99, ber = 99;
    sscanf(rx_buf, "+CSQ:%d,%d", &rssi, &ber);  // rssi=24, ber=0
}
```

注意：`response_buf` 收到的是所有非 URC 行（含 `\r\n`），**包括最后的 `OK`/`ERROR` 行本身**；只有 `\r\n` 的空行被忽略。若 `+CSQ:` 已被注册为 URC，该行会走 URC 回调而不进 `response_buf`（见示例 3）。

#### 2. URC 注册与注销（模组主动上报）

模组主动推送的行（如周期信号上报）不与任何命令对应，用 URC 回调异步接收：

```c
static int urc_id_csq = -1;

static void urc_csq_handler(struct atc_context *context, const char *line_data) {
    int rssi, ber;
    // line_data 形如 "+CSQ: 24,0\r\n"
    if (sscanf(line_data, "+CSQ:%d,%d", &rssi, &ber) == 2) {
        // 存储/通知其他线程
    }
}

void at_base_init(void) {
    // 注册成功返回 ID(>0)，保存用于注销
    urc_id_csq = atc_urc_register(&at_ctx, "+CSQ:", urc_csq_handler);
}

void at_base_deinit(void) {
    if (urc_id_csq > 0) {
        atc_urc_unregister(&at_ctx, urc_id_csq);
        urc_id_csq = -1;
    }
}
```

#### 3. 发命令 + 等 URC（结果以 URC 形式返回）

查询结果以 URC 上报的命令：`OK` 只表示受理，需再等 URC 回调才能拿到数据。用信号量把两步串成同步：

```c
static K_SEM_DEFINE(csq_sem, 0, 1);
static int cur_rssi, cur_ber;

static void urc_csq_handler(struct atc_context *context, const char *line_data) {
    if (sscanf(line_data, "+CSQ:%d,%d", &cur_rssi, &cur_ber) == 2)
        k_sem_give(&csq_sem);  // 通知等待线程：数据已就绪
}

int at_get_csq(int *rssi, int *ber) {
    enum atc_result result;
    k_sem_reset(&csq_sem);                                  // 清掉历史信号
    atc_send_sync(&at_ctx, "AT+CSQ\r\n", 8, &result, NULL, NULL, 1000);
    if (result != ATC_SUCCESS)
        return -1;
    if (k_sem_take(&csq_sem, K_MSEC(1000)) != 0)            // 等 URC 回调填充
        return -1;
    *rssi = cur_rssi;
    *ber  = cur_ber;
    return 0;
}
```

#### 4. 临时 URC 注册（一次性查询）

只出现一次的响应也可注册为临时 URC，用完即注销：

```c
static void urc_vref_handler(struct atc_context *context, const char *line_data) {
    LOG_INF("vref: %s", line_data);
}

int at_get_vref(void) {
    enum atc_result result;
    int urc_id = atc_urc_register(&at_ctx, "+VREF:", urc_vref_handler);
    atc_send_sync(&at_ctx, "AT+VREF?\r\n", 10, &result, NULL, NULL, 1000);
    atc_urc_unregister(&at_ctx, urc_id);
    return (result == ATC_SUCCESS) ? 0 : -1;
}
```

#### 5. prompt 匹配（等特定提示串）

命令的结束标志不是 `OK` 而是特定提示串（如 TCP 连接结果、`>` 输入提示符）时，用 prompt 匹配接口。`recv_len=0` 表示只等 prompt 不收后续二进制数据，prompt 匹配成功即返回：

```c
// 发 AT+MIPSTART 等 "CONNECT OK"（TCP 建立结果）
char cmd[] = "AT+MIPSTART=\"srv\",4333\r\n";
char prompt[] = "CONNECT OK";
atc_send_with_prompt_binary_rx_sync(&at_ctx, cmd, strlen(cmd),
    prompt, strlen(prompt), 0, &result, NULL, NULL, 1000);

// MQTT 发布：发 AT+MPUBEX 等 ">" 提示符，再发 payload 等 "PUBACK"（QoS1）
atc_send_with_prompt_binary_rx_sync(&at_ctx, mpubex_cmd, strlen(mpubex_cmd),
    ">", 1, 0, &result, NULL, NULL, 1000);
if (result == ATC_SUCCESS) {
    atc_send_with_prompt_binary_rx_sync(&at_ctx, payload, payload_len,
        "PUBACK", 6, 0, &result, NULL, NULL, 1000);
}
```


### API 速查

| API | 说明 |
|-----|------|
| `atc_interface_register(&if)` | 注册底层接口（必须先调用） |
| `atc_init(&ctx, send_fn)` | 初始化上下文，注入本实例的发送函数 |
| `atc_process(&ctx)` | 阻塞事件循环（永不返回） |
| `atc_receive_data(&ctx, data, len)` | 推送接收数据（ISR 中调用） |
| `atc_send_sync(...)` | 同步发送，等待 OK/ERROR/BUSY_ERROR |
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
