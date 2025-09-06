#include <stdio.h>
#include "at.h"

static void urc_ring(uint8_t port, const char *urc, void *arg) {
    (void)arg;
    printf("[URC] port=%d: %s\n", port, urc);
}

static void resp_print(uint8_t port, const char *resp, bool ok, void *arg) {
    (void)arg;
    printf("[RESP] port=%d, ok=%d\n", port, (int)ok);
    if (resp && resp[0]) printf("%s\n", resp);
}

int main(void) {
    // 假设只用 1 个端口：port 0
    at_engine_init(1);

    // 注册常见 URC
    at_register_urc_handler(0, "RING",  urc_ring,  NULL);
    at_register_urc_handler(0, "+CMTI", urc_ring,  NULL); // 短信到来示例

    // 发送几条命令
    at_send_cmd(0, "AT",      resp_print, NULL);
    at_send_cmd(0, "AT+GMR",  resp_print, NULL);
    at_send_cmd(0, "ATI",     resp_print, NULL);

    // 主循环
    for (;;) {
        at_engine_poll();
        // 你的其他逻辑...
    }

    return 0;
}
