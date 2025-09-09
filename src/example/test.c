#if 1
/**
 * ATCortex 自测程序（模拟串口环境）
 * - 覆盖特性：回显抑制、URC 分发、OK/ERROR/SEND OK/SEND FAIL 终态、
 *            PROMPT（> ）事务、LENGTH 事务、超时路径
 * - 日志：仍为英文；注释为中文
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "at.h"

/* -------------------- 模拟底层串口 I/O 与时钟 -------------------- */

#define MAX_RECEIVE_BUFFER 512
#define MAX_SEND_BUFFER    512

static char receive_buffers[AT_MAX_PORTS][MAX_RECEIVE_BUFFER];
static int  receive_indices[AT_MAX_PORTS];

static uint8_t send_buffers[AT_MAX_PORTS][MAX_SEND_BUFFER];
static int     send_indices[AT_MAX_PORTS];

/* 可选：声明（与引擎端 ../port/at_port.h 原型一致） */
void   at_port_init(uint8_t port_id);
size_t at_port_read(uint8_t port_id, uint8_t *buf, size_t len);
size_t at_port_write(uint8_t port_id, const uint8_t *data, size_t len);
uint32_t at_port_get_time_ms(uint8_t port_id);

void at_port_init(uint8_t port_id){
    (void)port_id;
    /* 这里无需真实硬件初始化，打印辅助定位 */
    printf("at_port_init %d\n", port_id);
}

/* 从“接收字符串缓冲区”读出字节流，行为类似驱动层一次吐若干字节 */
size_t at_port_read(uint8_t port_id, uint8_t *buf, size_t len) {
    if (port_id >= AT_MAX_PORTS) return 0;
    size_t n = 0;
    size_t avail = strlen(receive_buffers[port_id]); /* 以 '\0' 为结束 */
    while (n < len && receive_indices[port_id] < (int)avail) {
        buf[n++] = (uint8_t)receive_buffers[port_id][receive_indices[port_id]++];
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

/* 覆盖并重置某端口的“将要被 read() 读到”的接收数据 */
static void set_receive_data(uint8_t port, const char *data) {
    if (port >= AT_MAX_PORTS) return;
    size_t n = strlen(data);
    if (n >= MAX_RECEIVE_BUFFER) n = MAX_RECEIVE_BUFFER - 1;
    memcpy(receive_buffers[port], data, n);
    receive_buffers[port][n] = '\0';
    receive_indices[port] = 0;
}

/* 在现有“未读完”的接收数据后面追加更多数据（更贴近真实边到边收） */
static void append_receive_data(uint8_t port, const char *data) {
    if (port >= AT_MAX_PORTS) return;
    size_t cur = strlen(receive_buffers[port]);
    size_t add = strlen(data);
    if (cur + add >= MAX_RECEIVE_BUFFER) add = MAX_RECEIVE_BUFFER - 1 - cur;
    memcpy(receive_buffers[port] + cur, data, add);
    receive_buffers[port][cur + add] = '\0';
}

/* 清空所有端口发送缓冲，便于分段观察 */
static void clear_send_buffers(void) {
    for (uint8_t i = 0; i < AT_MAX_PORTS; ++i) {
        memset(send_buffers[i], 0, MAX_SEND_BUFFER);
        send_indices[i] = 0;
    }
}

/* 打印端口发送缓冲（可查事务阶段是否把 payload/terminator 发出） */
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

static void response_callback(uint8_t port, const char *resp, bool ok, void *arg) {
    (void)arg;
    if (ok) {
        printf("[RESP] port=%d OK\n", port);
        if (resp && resp[0]) printf("%s\n", resp);
    } else {
        printf("[RESP] port=%d FAIL: %s\n", port, resp ? resp : "(null)");
    }
}

static void urc_callback(uint8_t port, const char *urc, void *arg) {
    (void)arg;
    printf("[URC]  port=%d: %s\n", port, urc);
}

/* -------------------- 各测试用例 -------------------- */

/* 1) 基础：回显抑制（port0 忽略回显） + OK 终态 */
static void tc_basic_ok_with_echo_ignore(void) {
    clear_send_buffers();
    printf("\n== TC1: echo-ignore + OK ==\n");
    at_send_cmd(0, "AT", response_callback, NULL);

    /* 设备回显同名行 + OK；引擎应丢弃第一行回显，仅以 OK 收尾 */
    set_receive_data(0, "AT\nOK\n");
    pump_cycles(5);
    dump_tx(0, "after AT");
}

/* 2) 基础：URC 与命令响应并存（port1 不忽略回显） */
static void tc_basic_urc_mix(void) {
    clear_send_buffers();
    printf("\n== TC2: URC while busy + response ==\n");
    at_send_cmd_ex(1, "AT+GMR", 500, response_callback, NULL);

    /* 先来一条 URC */
    set_receive_data(1, "+CMTI: \"SM\",1\n");
    pump_cycles(2);

    /* 再来正常响应体 + 终态 OK */
    set_receive_data(1, "VERSION: 1.0.0\nOK\n");
    pump_cycles(5);
    dump_tx(1, "after AT+GMR");
}

/* 3) 错误终态：SEND FAIL */
static void tc_error_send_fail(void) {
    clear_send_buffers();
    printf("\n== TC3: error terminal 'SEND FAIL' ==\n");
    at_send_cmd_ex(0, "AT+SND", 200, response_callback, NULL);
    set_receive_data(0, "SEND FAIL\n");
    pump_cycles(3);
}

/* 4) 超时 */
static void tc_timeout(void) {
    clear_send_buffers();
    printf("\n== TC4: timeout ==\n");
    at_send_cmd_ex(0, "AT+TIMEOUT", 200, response_callback, NULL);
    /* 不注入任何接收数据；推进直到超时 */
    pump_cycles(250);
}

/* 5) 事务：PROMPT（> 空格），payload + 0x1A 终止符，终态 SEND OK */
static void tc_txn_prompt(void) {
    clear_send_buffers();
    printf("\n== TC5: TXN PROMPT (> ) + payload + 0x1A + SEND OK ==\n");

    const uint8_t payload[]  = "HELLO";
    const uint8_t terminator = 0x1A; // Ctrl+Z

    at_send_cmd_txn_prompt(0, "AT+CMGS=5",
                           payload, sizeof(payload) - 1,
                           &terminator, 1,
                           NULL,      // 默认提示 "> "（注意有空格）
                           1000,
                           response_callback, NULL);

    // 先喂回显+提示（默认是 "> " 两个字符）
    set_receive_data(0, "AT+CMGS=5\n> ");
    pump_cycles(1);  // 本轮匹配提示
    pump_cycles(1);  // 本轮完成 payload + 0x1A 发送，并解除行抑制

    dump_tx(0, "after prompt+payload");

    // 解除抑制后再喂终态
    append_receive_data(0, "\nSEND OK\n");
    pump_cycles(6);  // 这里给足 2~3 轮，确保读入与判定
}



/* 6) 事务：LENGTH，命令后立即发送定长 payload，无终止符，终态 SEND OK */
static void tc_txn_length(void) {
    clear_send_buffers();
    printf("\n== TC6: TXN LENGTH (immediate payload) + SEND OK ==\n");

    const uint8_t payload[] = { 'X','Y','Z' };

    at_send_cmd_txn_len(1, "AT#BIN=3",
                        payload, sizeof(payload),
                        NULL, 0,
                        500,
                        response_callback, NULL);

    /* 立即注入终态 */
    pump_cycles(1);           // 进入二进制阶段（抑制行）
    pump_cycles(1);           // 完成 payload 发送并解除抑制
    append_receive_data(1, "SEND OK\n");
    pump_cycles(2);
    pump_cycles(5);
    dump_tx(1, "after length payload");
}

/* -------------------- 主函数 -------------------- */

int main(void) {
    /* 设置端口与回显策略：port0 忽略首行回显，port1 不忽略 */
    bool echo_map[AT_MAX_PORTS] = { true, false };
    at_engine_init_ex(2, echo_map);

    /* 注册 URC 前缀：演示 +CMTI/RING 等（仅举例） */
    at_register_urc_handler(0, "RING",  urc_callback, NULL);
    at_register_urc_handler(0, "+CMTI", urc_callback, NULL);
    at_register_urc_handler(1, "+CMTI", urc_callback, NULL);

    printf("=== ATCortex tests start ===\n");

    tc_basic_ok_with_echo_ignore();
    tc_basic_urc_mix();
    tc_error_send_fail();
    tc_timeout();
    tc_txn_prompt();
    tc_txn_length();

    printf("\n=== ATCortex tests done ===\n");
    return 0;
}
#endif
