/* core/at_parser.h */
#ifndef AT_PARSER_H
#define AT_PARSER_H

#include <stdint.h>
#include <stddef.h>

typedef void (*at_line_cb_t)(uint8_t port_id, const char *line);

void at_parser_init(at_line_cb_t cb);
void at_parser_process(uint8_t port_id, const uint8_t *data, size_t len);

#endif /* AT_PARSER_H */
