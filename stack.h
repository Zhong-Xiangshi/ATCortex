#ifndef STACK_H
#define STACK_H

#include <stddef.h> // size_t
#include <stdbool.h> // bool

// 定义一个函数指针类型，用于释放元素
// 参数 item 是栈中存储的 void* 指针
typedef void (*StackItemFreeFunc)(void* item);

// 不透明结构体指针
typedef struct Stack Stack;

/**
 * @brief 创建固定大小的堆栈
 * @param capacity 最大容量 (一旦创建不可改变)
 * @return 堆栈指针
 */
Stack* stack_create(size_t capacity);

/**
 * @brief 销毁堆栈
 */
void stack_destroy(Stack* s);

/**
 * @brief 压栈
 * @return true 成功, false 失败 (栈已满或内存错误)
 */
bool stack_push(Stack* s, void* data);

/**
 * @brief 出栈
 */
void* stack_pop(Stack* s);

/**
 * @brief 查看栈顶
 */
void* stack_peek(Stack* s);

/**
 * @brief 查看指定位置 (0 = 栈顶, 1 = 栈顶下一个...)
 */
void* stack_peek_at(Stack* s, size_t index);

/**
 * @brief 检查栈是否为空
 */
bool stack_is_empty(Stack* s);

/**
 * @brief 检查栈是否已满 (固定大小栈专用)
 */
bool stack_is_full(Stack* s);

/**
 * @brief 获取当前元素个数
 */
size_t stack_size(Stack* s);

/**
 * @brief 获取栈的最大容量
 */
size_t stack_capacity(Stack* s);

/**
 * @brief 清空堆栈
 * @param s 堆栈指针
 * @param free_func 数据释放回调函数。
 *        - 如果栈里存的是 malloc 的数据，请传入 free 或自定义释放函数。
 *        - 如果栈里存的是不需要释放的数据（如 int 值或静态字符串），请传入 NULL。
 */
void stack_clear(Stack* s, StackItemFreeFunc free_func);

#endif // STACK_H