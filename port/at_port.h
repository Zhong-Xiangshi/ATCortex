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
