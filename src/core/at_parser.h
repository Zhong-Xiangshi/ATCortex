/* core/at_parser.h */
#ifndef AT_PARSER_H
#define AT_PARSER_H

#include <stdint.h>
#include <stddef.h>

/** 行回调：每收到一行（以 '\n' 结尾，'\r' 已被忽略）调用一次 */
typedef void (*at_line_cb_t)(uint8_t port_id, const char *line);

/** 初始化解析器（注册行回调） */
void at_parser_init(at_line_cb_t cb);

/** 处理一段原始字节流（可重复调用，内部会按行缓冲并回调） */
void at_parser_process(uint8_t port_id, const uint8_t *data, size_t len);

#endif /* AT_PARSER_H */
