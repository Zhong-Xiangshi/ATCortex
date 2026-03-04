#include "stack.h"
#include <stddef.h>
#include "interface.h"
/* ============================================================
 * 内存管理宏定义
 * 用户可以通过编译器参数 -DSTACK_MALLOC=my_malloc 覆盖这些定义
 * ============================================================ */

#define STACK_MALLOC g_atc_interface.atc_malloc
#define STACK_FREE g_atc_interface.atc_free
// 如果未定义，则包含 stdlib.h 并使用标准 malloc/free
#ifndef STACK_MALLOC
    #include <stdlib.h>
    #define STACK_MALLOC malloc
#endif

#ifndef STACK_FREE
    #include <stdlib.h>
    #define STACK_FREE free
#endif

/* ============================================================
 * 结构体定义
 * ============================================================ */
struct Stack {
    void** items;     // 数据数组
    size_t capacity;  // 最大固定容量
    size_t top;       // 当前元素个数
};

/* ============================================================
 * 函数实现
 * ============================================================ */

Stack* stack_create(size_t capacity) {
    if (capacity == 0) return NULL;

    // 1. 分配结构体内存
    Stack* s = (Stack*)STACK_MALLOC(sizeof(Stack));
    if (s == NULL) return NULL;

    s->capacity = capacity;
    s->top = 0;

    // 2. 分配固定大小的指针数组
    // 注意：这里一次性分配好所有需要的内存，不再 realloc
    s->items = (void**)STACK_MALLOC(sizeof(void*) * capacity);
    
    if (s->items == NULL) {
        STACK_FREE(s); // 数组分配失败，回滚释放结构体
        return NULL;
    }

    return s;
}

void stack_destroy(Stack* s) {
    if (s == NULL) return;
    
    if (s->items != NULL) {
        STACK_FREE(s->items);
    }
    STACK_FREE(s);
}

bool stack_push(Stack* s, void* data) {
    if (s == NULL) return false;

    // 核心修改：检查是否已满
    if (s->top >= s->capacity) {
        return false; // 栈满，压栈失败
    }

    s->items[s->top++] = data;
    return true;
}

void* stack_pop(Stack* s) {
    if (s == NULL || s->top == 0) return NULL;
    s->top--;
    return s->items[s->top];
}

void* stack_peek(Stack* s) {
    if (s == NULL || s->top == 0) return NULL;
    return s->items[s->top - 1];
}

void* stack_peek_at(Stack* s, size_t index) {
    if (s == NULL || index >= s->top) return NULL;
    return s->items[s->top - 1 - index];
}

bool stack_is_empty(Stack* s) {
    return (s == NULL || s->top == 0);
}

bool stack_is_full(Stack* s) {
    return (s != NULL && s->top == s->capacity);
}

size_t stack_size(Stack* s) {
    return (s == NULL) ? 0 : s->top;
}

size_t stack_capacity(Stack* s) {
    return (s == NULL) ? 0 : s->capacity;
}

void stack_clear(Stack* s, StackItemFreeFunc free_func) {
    if (s == NULL) return;

    // 1. 如果用户提供了释放函数，先释放所有元素指向的内存
    if (free_func != NULL) {
        for (size_t i = 0; i < s->top; i++) {
            // 只有当指针非空时才释放，防止用户存了 NULL 进去导致 crash
            if (s->items[i] != NULL) {
                free_func(s->items[i]);
            }
        }
    }

    // 2. 逻辑清空：直接将计数器归零
    // 下次 push 会直接覆盖旧数据，无需 memset 清零数组，效率最高
    s->top = 0;
}
