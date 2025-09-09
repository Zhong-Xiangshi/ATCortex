/* core/at_dispatcher.h */
#ifndef AT_DISPATCHER_H
#define AT_DISPATCHER_H

#include "at.h"

/** 初始化 URC 分发器（清空已注册表） */
void at_dispatcher_init(void);

/** 注册某端口的 URC 前缀处理器（成功返回 0） */
int  at_register_urc_handler  (uint8_t port_id, const char *prefix, at_urc_cb_t cb, void *user_arg);

/** 反注册某端口的 URC 前缀处理器（成功返回 0） */
int  at_unregister_urc_handler(uint8_t port_id, const char *prefix);

/** 将一行尝试分发为 URC，若已处理返回 1，否则返回 0 */
int  at_dispatcher_dispatch_line(uint8_t port_id, const char *line);

#endif /* AT_DISPATCHER_H */
