//ring_buffer.h
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 环形缓冲区控制句柄 */
typedef struct {
    unsigned char *buffer;              /* 指向外部传入的数据缓冲区 */
    unsigned int   capacity;            /* 缓冲区总容量（注意：采用“空一格”判满策略） */
    volatile unsigned int read_index;   /* 读指针 */
    volatile unsigned int write_index;  /* 写指针 */
} ring_buffer_t;

/**
 * @brief 初始化环形缓冲区（静态模式）
 * @param handle    句柄指针（控制块）
 * @param buffer    外部传入的缓冲区数组指针
 * @param capacity  buffer 数组的大小。实际可存放的最大字节数为 capacity-1。
 * @return 1 成功，0 失败（参数非法）
 */
int ring_buffer_init(ring_buffer_t *handle, unsigned char *buffer, unsigned int capacity);

/**
 * @brief 重置/反初始化环形缓冲区
 * @note 不会释放 buffer 内存，因为是由外部管理的，只重置句柄状态
 * @param handle 句柄指针
 */
void ring_buffer_deinit(ring_buffer_t *handle);

/**
 * @brief 向环形缓冲区写入一个字节
 * @param handle 句柄指针
 * @param data   待写入字节
 * @return 1 成功，0 失败（满或未初始化）
 */
int ring_buffer_write(ring_buffer_t *handle, unsigned char data);

/**
 * @brief 从环形缓冲区读取一个字节
 * @param handle 句柄指针
 * @param data   输出读取到的字节
 * @return 1 成功，0 失败（空或未初始化）
 */
int ring_buffer_read(ring_buffer_t *handle, unsigned char *data);

/**
 * @brief 获取当前缓冲区的数据量
 * @param handle 句柄指针
 * @return >=0 当前数据量，<0 表示未初始化
 */
int ring_buffer_data_count(const ring_buffer_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* RING_BUFFER_H */