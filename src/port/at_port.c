/**
 * @file at_port.c
 * @brief 串口底层默认占位实现（请替换为你的平台实现）
 *        Default stub implementation; replace with your platform-specific code.
 */

#if 0

#include "at_port.h"

void at_port_init(uint8_t port_id){
    printf("at_port_init %d\n",port_id);
    if(port_id==0){
    
    }
}

size_t at_port_read(uint8_t port_id, uint8_t *buf, size_t len) {
    (void)port_id; (void)buf; (void)len;
    return 0; // 占位：无数据 / Stub: no data
}

size_t at_port_write(uint8_t port_id, const uint8_t *data, size_t len) {
    (void)port_id; (void)data;
    return len;    // 占位：假定全部写入 / Stub: pretend all written
}

uint32_t at_port_get_time_ms(uint8_t port_id) {
    (void)port_id;
    // 占位：返回0。请使用硬件定时器/节拍计数器实现毫秒时基。
    // Stub: return 0. Implement with a hardware timer / systick counter.
    return 0u;
}

#endif
