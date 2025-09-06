
#if 1

#include <stdio.h>
#include <string.h>
#include "at.h"

// 模拟串口接收缓冲区 / Simulated receive buffer per port
#define MAX_RECEIVE_BUFFER 256
static char receive_buffers[AT_MAX_PORTS][MAX_RECEIVE_BUFFER];
static int  receive_indices[AT_MAX_PORTS];

// 模拟发送缓冲区（仅用于调试输出）/ Simulated send buffer
#define MAX_SEND_BUFFER 256
static char send_buffers[AT_MAX_PORTS][MAX_SEND_BUFFER];
static int  send_indices[AT_MAX_PORTS];

// 模拟底层读串口 / Simulated port read
size_t at_port_read(uint8_t port_id, uint8_t *buf, size_t len) {
    if (port_id >= AT_MAX_PORTS) {
        return 0;
    }
    size_t n = 0;
    size_t avail = strlen(receive_buffers[port_id]);
    while (n < len && receive_indices[port_id] < (int)avail) {
        buf[n++] = (uint8_t)receive_buffers[port_id][receive_indices[port_id]++];
    }
    return n;
}

// 模拟底层写串口 / Simulated port write
size_t at_port_write(uint8_t port_id, const uint8_t *data, size_t len) {
    if (port_id >= AT_MAX_PORTS) {
        return 0;
    }
    size_t n = 0;
    while (n < len && send_indices[port_id] < MAX_SEND_BUFFER) {
        send_buffers[port_id][send_indices[port_id]++] = (char)data[n++];
    }
    return n;
}

// 模拟毫秒计时器 / Simulated millisecond timer
uint32_t at_port_get_time_ms(uint8_t port_id) {
    (void)port_id;
    static uint32_t ms = 0;
    return ms++;
}

// 命令响应回调 / Response callback
static void response_callback(uint8_t port, const char *resp, bool ok, void *arg) {
    (void)arg;
    if (ok) {
        printf("[RESP] port=%d OK\n", port);
        if (resp && resp[0]) {
            printf("%s\n", resp);
        }
    } else {
        printf("[RESP] port=%d FAIL: %s\n", port, resp);
    }
}

// URC 回调 / URC callback
static void urc_callback(uint8_t port, const char *urc, void *arg) {
    (void)arg;
    printf("[URC] port=%d: %s\n", port, urc);
}

// 设置接收数据 / Helper to set receive buffer
static void set_receive_data(uint8_t port, const char *data) {
    if (port >= AT_MAX_PORTS) {
        return;
    }
    strncpy(receive_buffers[port], data, MAX_RECEIVE_BUFFER - 1);
    receive_buffers[port][MAX_RECEIVE_BUFFER - 1] = '\0';
    receive_indices[port] = 0;
}

// 清理发送缓冲区 / Helper to clear send buffers
static void clear_send_buffers(void) {
    for (uint8_t i = 0; i < AT_MAX_PORTS; ++i) {
        memset(send_buffers[i], 0, MAX_SEND_BUFFER);
        send_indices[i] = 0;
    }
}

int main(void) {
    // 设置回显策略：port0 忽略回显，port1 不忽略
    bool echo_map[AT_MAX_PORTS] = { true, false };
    at_engine_init_ex(2, echo_map);

    // 注册 URC 处理器
    at_register_urc_handler(0, "RING", urc_callback, NULL);
    at_register_urc_handler(1, "+CMTI", urc_callback, NULL);

    printf("Sending AT commands...\n");
    // 默认超时命令
    at_send_cmd(0, "AT", response_callback, NULL);
    // 自定义超时命令
    at_send_cmd_ex(1, "AT+GMR", 500, response_callback, NULL);

    // 模拟 port0 收到 OK
    set_receive_data(0, "OK\n");
    at_engine_poll();

    // 模拟 port1 收到 URC
    set_receive_data(1, "+CMTI: \"SM\",1\n");
    at_engine_poll();

    // 模拟 port0 收到 ERROR
    set_receive_data(0, "ERROR\n");
    at_send_cmd_ex(0, "AT+TEST", 200, response_callback, NULL);
    for (int i = 0; i < 10; ++i) {
        at_engine_poll();
    }

    // 模拟超时：port0 无响应
    set_receive_data(0, "");
    at_send_cmd_ex(0, "AT+TIMEOUT", 200, response_callback, NULL);
    for (int i = 0; i < 250; ++i) {
        at_engine_poll();
    }

    return 0;
}

#endif
