#include "ring_buffer.h"

// 为了判空，需要包含 NULL 定义，通常在 stddef.h 或 stdio.h
#ifndef NULL
#define NULL ((void*)0)
#endif

/**
 * @brief 初始化环形缓冲区
 *
 * @param handle   环形缓冲区控制句柄
 * @param buffer   外部提供的内存区域
 * @param capacity 内存区域的大小
 * @return int     1 表示成功, 0 表示失败
 */
int ring_buffer_init(ring_buffer_t *handle, unsigned char *buffer, unsigned int capacity)
{
    // 至少需要2个单元才能区分空/满状态 (因为有一个单元总是保留为空)
    if (handle == NULL || buffer == NULL || capacity < 2) {
        return 0;
    }

    handle->buffer      = buffer;
    handle->capacity    = capacity;
    handle->read_index  = 0;
    handle->write_index = 0;

    return 1;
}

/**
 * @brief 重置/反初始化环形缓冲区
 *
 * @param handle 环形缓冲区控制句柄
 */
void ring_buffer_deinit(ring_buffer_t *handle)
{
    if (handle == NULL) return;

    // 因为内存是外部传入的，这里不进行 free，只将指针置空以防误用
    handle->buffer      = NULL;
    handle->capacity    = 0;
    handle->read_index  = 0;
    handle->write_index = 0;
}

/**
 * @brief 【生产者调用】向缓冲区写入一个字节
 *
 * @param handle 环形缓冲区控制句柄
 * @param data   要写入的数据
 * @return int   1 表示写入成功, 0 表示缓冲区已满
 */
int ring_buffer_write(ring_buffer_t *handle, unsigned char data)
{
    if (handle == NULL || handle->buffer == NULL) {
        return 0;
    }

    // 计算下一个写指针的位置
    unsigned int next_write = (handle->write_index + 1) % handle->capacity;

    // 判断缓冲区是否已满。如果下一个写指针追上了读指针，则表示缓冲区已满。
    if (next_write == handle->read_index) {
        return 0; // 缓冲区满
    }

    // 写入数据并更新写指针
    handle->buffer[handle->write_index] = data;
    handle->write_index = next_write;

    return 1;
}

/**
 * @brief 【消费者调用】从缓冲区读取一个字节
 *
 * @param handle 环形缓冲区控制句柄
 * @param data   用于接收数据的指针
 * @return int   1 表示读取成功, 0 表示缓冲区为空
 */
int ring_buffer_read(ring_buffer_t *handle, unsigned char *data)
{
    if (handle == NULL || handle->buffer == NULL || data == NULL) {
        return 0;
    }

    // 判断缓冲区是否为空。如果读写指针相等，则表示缓冲区为空。
    if (handle->read_index == handle->write_index) {
        return 0; // 缓冲区空
    }

    // 读取数据并更新读指针
    *data = handle->buffer[handle->read_index];
    handle->read_index = (handle->read_index + 1) % handle->capacity;

    return 1;
}

/**
 * @brief 获取缓冲区中当前的数据量
 * @note 在并发环境下，返回的值可能在你拿到它的时候就已经过时了。
 *
 * @param handle 环形缓冲区控制句柄
 * @return int   -1 表示缓冲区无效，否则返回数据量
 */
int ring_buffer_data_count(const ring_buffer_t *handle)
{
    if (handle == NULL || handle->buffer == NULL) {
        return -1;
    }

    // 动态计算数据量
    return (handle->write_index - handle->read_index + handle->capacity) % handle->capacity;
}