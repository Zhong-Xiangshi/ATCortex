#if 0

#include <stdio.h>
#include "at.h"

static void urc_cb(uint8_t port, const char *urc, void *arg) {
    (void)arg;
    printf("[URC] port=%d: %s\n", port, urc);
}

static void resp_cb(uint8_t port, const char *resp, bool ok, void *arg) {
    (void)arg;
    printf("[RESP] port=%d, ok=%d\n", port, (int)ok);
    if (resp && resp[0]) printf("%s\n", resp);
}

int main(void) {
    // 两个端口：port0 忽略回显，port1 不忽略
    bool echo_map[2] = { true, false };
    at_engine_init_ex(2, echo_map);

    // 注册几个常见 URC
    at_register_urc_handler(0, "RING",  urc_cb, NULL);
    at_register_urc_handler(0, "+CMTI", urc_cb, NULL);

    // 默认超时 100ms
    at_send_cmd(0, "AT", resp_cb, NULL);

    // 自定义超时 500ms
    at_send_cmd_ex(0, "AT+GMR", 500, resp_cb, NULL);

    for (;;) {
        at_engine_poll();
        // 你的其他循环逻辑...
    }
    // 不会到达
    // return 0;
}

#endif
