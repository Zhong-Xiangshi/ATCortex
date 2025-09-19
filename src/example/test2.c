#if 1
/**
 * ATCortex 自测程序（模拟串口环境）
 * - 专门测试新增的 AT_TXN_PROMPT_BINARY_RX 功能
 * - 覆盖场景：按长度接收二进制、按终止符接收二进制
 * - 日志：仍为英文；注释为中文
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "at.h"

/* -------------------- 模拟底层串口 I/O 与时钟 (二进制安全版) -------------------- */

#define MAX_RECEIVE_BUFFER 512
#define MAX_SEND_BUFFER    512

static uint8_t receive_buffers[AT_MAX_PORTS][MAX_RECEIVE_BUFFER];
static size_t  receive_data_len[AT_MAX_PORTS];
static int     receive_indices[AT_MAX_PORTS];

static uint8_t send_buffers[AT_MAX_PORTS][MAX_SEND_BUFFER];
static int     send_indices[AT_MAX_PORTS];

/* 可选：声明（与引擎端 ../port/at_port.h 原型一致） */
void     at_port_init(uint8_t port_id);
size_t   at_port_read(uint8_t port_id, uint8_t *buf, size_t len);
size_t   at_port_write(uint8_t port_id, const uint8_t *data, size_t len);
uint32_t at_port_get_time_ms(uint8_t port_id);

void at_port_init(uint8_t port_id){
    (void)port_id;
    receive_data_len[port_id] = 0;
    receive_indices[port_id] = 0;
    send_indices[port_id] = 0;
    /* 这里无需真实硬件初始化，打印辅助定位 */
    printf("at_port_init %d\n", port_id);
}

/* 从“接收缓冲区”读出字节流，行为类似驱动层一次吐若干字节 */
size_t at_port_read(uint8_t port_id, uint8_t *buf, size_t len) {
    if (port_id >= AT_MAX_PORTS) return 0;
    size_t n = 0;
    size_t avail = receive_data_len[port_id];
    while (n < len && receive_indices[port_id] < (int)avail) {
        buf[n++] = receive_buffers[port_id][receive_indices[port_id]++];
    }
    return n;
}

/* 将待发送数据写入“发送缓冲区”（仅用于事后查看发送了什么） */
size_t at_port_write(uint8_t port_id, const uint8_t *data, size_t len) {
    if (port_id >= AT_MAX_PORTS) return 0;
    size_t n = 0;
    while (n < len && send_indices[port_id] < MAX_SEND_BUFFER) {
        send_buffers[port_id][send_indices[port_id]++] = data[n++];
    }
    return n;
}

/* 毫秒时钟：每次调用 +1ms，用于触发超时 */
uint32_t at_port_get_time_ms(uint8_t port_id) {
    (void)port_id;
    static uint32_t ms = 0;
    return ms++;
}

/* -------------------- 测试辅助工具 -------------------- */

/* 覆盖并重置某端口的“将要被 read() 读到”的接收数据 (文本) */
static void set_receive_data(uint8_t port, const char *data) {
    if (port >= AT_MAX_PORTS) return;
    size_t n = strlen(data);
    if (n >= MAX_RECEIVE_BUFFER) n = MAX_RECEIVE_BUFFER;
    memcpy(receive_buffers[port], data, n);
    receive_data_len[port] = n;
    receive_indices[port] = 0;
}

/* 覆盖并重置某端口的接收数据 (二进制安全) */
static void set_receive_binary_data(uint8_t port, const uint8_t *data, size_t size) {
    if (port >= AT_MAX_PORTS) return;
    size_t n = size;
    if (n >= MAX_RECEIVE_BUFFER) n = MAX_RECEIVE_BUFFER;
    memcpy(receive_buffers[port], data, n);
    receive_data_len[port] = n;
    receive_indices[port] = 0;
}


/* 清空所有端口发送缓冲，便于分段观察 */
static void clear_send_buffers(void) {
    for (uint8_t i = 0; i < AT_MAX_PORTS; ++i) {
        memset(send_buffers[i], 0, MAX_SEND_BUFFER);
        send_indices[i] = 0;
    }
}

/* 打印端口发送缓冲 */
static void dump_tx(uint8_t port, const char *title) {
    printf("\n[TX-DUMP] port=%u %s, bytes=%d\n", port, title ? title : "", send_indices[port]);
    for (int i = 0; i < send_indices[port]; ++i) {
        uint8_t b = send_buffers[port][i];
        if (b >= 32 && b <= 126) printf("%c", b); else printf("\\x%02X", b);
    }
    printf("\n");
}

/* 快速“推进引擎”若干轮 */
static void pump_cycles(int cycles) {
    for (int i = 0; i < cycles; ++i) at_engine_poll();
}

/* -------------------- 回调 -------------------- */

/**
 * @brief 专门用于显示二进制响应的回调函数
 * @note  由于原回调接口没有长度参数，这里使用 strlen。
 *        这意味着测试用的二进制载荷不应包含 \0 字符，以确保打印正确。
 *        这是一个测试层面的妥协，实际应用中可能需要调整回调函数原型。
 */
static void binary_response_callback(uint8_t port, const char *resp, bool ok, void *arg) {
    (void)arg;
    if (ok) {
        printf("[RESP] port=%d OK\n", port);
        if (resp) {
            size_t len = 8; 
            printf("  Received %zu bytes:\n  ", len);
            for(size_t i = 0; i < len; ++i) {
                if (resp[i] >= 32 && resp[i] <= 126) {
                    printf("%c", resp[i]);
                } else {
                    printf("\\x%02X", (uint8_t)resp[i]);
                }
            }
            printf("\n");
        }
    } else {
        printf("[RESP] port=%d FAIL: %s\n", port, resp ? resp : "(null)");
    }
}

/* -------------------- 各测试用例 -------------------- */

/* 7) 事务：PROMPT_BINARY_RX，按指定长度接收二进制数据 */
static void tc_binary_rx_by_length(void) {
    clear_send_buffers();
    printf("\n== TC7: TXN PROMPT_BINARY_RX by Length ==\n");

    const uint8_t expected_binary[] = {0x01, 0x03, 0xf0, 0x0a, 0x03, 0x0f, 0x0f,6};

    at_send_cmd_txn_prompt_binary_rx(0, "ATD*99#",
                                     "CONNECT",
                                     sizeof(expected_binary),
                                     NULL,
                                     1000,
                                     binary_response_callback, NULL);
    pump_cycles(2);

    /* 1. 模拟模块返回 "CONNECT" 提示，并带上回车换行，这更真实 */
    set_receive_data(0, "CONNECT\r\n");
    pump_cycles(5); // 引擎处理 prompt，并因为新的守卫逻辑，忽略 CONNECT 这一行

    /* 2. 模拟模块发送二进制数据 */
    set_receive_binary_data(0, expected_binary, sizeof(expected_binary));

    /* 3. 轮询引擎，完成接收 */
    pump_cycles(10);

    dump_tx(0, "after ATD*99#");
}

/* 8) 事务：PROMPT_BINARY_RX，按终止符接收二进制数据 */
static void tc_binary_rx_by_terminator(void) {
    clear_send_buffers();
    printf("\n== TC8: TXN PROMPT_BINARY_RX by Terminator ==\n");

    const char* terminator = "END_DATA";
    const uint8_t binary_part[] = {'R', 'A', 'W', '_', 0xDE, 0xAD, 0xBE, 0xEF};

    // 构造完整的模拟设备响应 = 二进制数据 + 终止符
    uint8_t full_response[100];
    memcpy(full_response, binary_part, sizeof(binary_part));
    memcpy(full_response + sizeof(binary_part), terminator, strlen(terminator));
    size_t full_len = sizeof(binary_part) + strlen(terminator);


    // 发送命令，等待 "DOWNLOAD" 提示，然后接收数据直到 "END_DATA" 终止符
    at_send_cmd_txn_prompt_binary_rx(0, "AT+DOWNLOAD",
                                     "DOWNLOAD",    // prompt
                                     0,             // rx_len = 0 表示使用终止符
                                     terminator,    // rx_terminator
                                     1000,
                                     binary_response_callback, NULL);
    pump_cycles(2);

    // 1. 模拟模块返回 "DOWNLOAD" 提示
    set_receive_data(0, "DOWNLOAD\n");
    pump_cycles(5); // 引擎进入二进制接收模式

    // 2. 模拟模块发送二进制数据 + 终止符
    set_receive_binary_data(0, full_response, full_len);

    // 3. 轮询引擎，直到它匹配到终止符并完成命令
    pump_cycles(20);

    dump_tx(0, "after AT+DOWNLOAD");
}


/* -------------------- 主函数 -------------------- */

int main(void) {
    /* 初始化 AT 引擎，使用 1 个端口 */
    at_engine_init(1);

    printf("=== ATCortex new feature tests start ===\n");

    // 运行测试用例
    tc_binary_rx_by_length();
    tc_binary_rx_by_terminator();

    printf("\n=== ATCortex new feature tests done ===\n");
    return 0;
}
#endif