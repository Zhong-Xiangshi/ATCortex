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
