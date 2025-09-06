/**
 * @file at_port.h
 * @brief 底层串口抽象接口（由用户实现）/ Low-level port abstraction (to be implemented by user).
 */
#ifndef AT_PORT_H
#define AT_PORT_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief 读取指定端口接收环形缓冲区（非阻塞）
 *        Read from the specified port's RX ring buffer (non-blocking).
 *
 * @param port_id 端口号 / Port index
 * @param buf     输出缓冲区 / Destination buffer
 * @param len     期望读取的最大字节数 / Max bytes to read
 * @return 实际读取字节数（无数据返回0）/ Bytes read (0 if none)
 */
size_t at_port_read(uint8_t port_id, uint8_t *buf, size_t len);

/**
 * @brief 向指定端口发送缓冲区写入数据（尽量非阻塞）
 *        Write data to the specified port's TX buffer (non-blocking if possible).
 *
 * @param port_id 端口号 / Port index
 * @param data    数据指针 / Data pointer
 * @param len     数据长度 / Data length
 * @return 实际写入/排队字节数 / Bytes written/enqueued
 */
size_t at_port_write(uint8_t port_id, const uint8_t *data, size_t len);

/**
 * @brief 获取当前毫秒时间戳（用于命令超时）
 *        Get current time in milliseconds (used for command timeout).
 *
 * @param port_id 端口号（若无多端口时可忽略）/ Port index (can be ignored if single port)
 * @return 当前时间（ms）/ Current time in milliseconds
 */
uint32_t at_port_get_time_ms(uint8_t port_id);

#endif /* AT_PORT_H */
