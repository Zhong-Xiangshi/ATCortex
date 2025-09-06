/**
 * @file at.h
 * @brief AT 指令框架对外统一 API（无操作系统、轮询方式）。
 *
 * 功能特性：
 * - 多 AT 端口支持（可配置 AT_MAX_PORTS）
 * - 异步命令队列 + 回调，非阻塞
 * - 行级状态机解析（以 '\n' 结尾，忽略 '\r'）
 * - 分发器区分命令响应与 URC，支持 URC 前缀注册/反注册
 * - 轻量日志（AT_ENABLE_LOG）
 *
 * 使用示例：
 * @code
 * void urc_cb(uint8_t port, const char *urc, void *arg) { /* ... *\/ }
 * void resp_cb(uint8_t port, const char *resp, bool ok, void *arg) { /* ... *\/ }
 *
 * int main(void) {
 *     at_engine_init(1); // 只用 port 0
 *     at_register_urc_handler(0, "RING", urc_cb, NULL);
 *     at_send_cmd(0, "AT+GMR", resp_cb, NULL);
 *     for (;;) { at_engine_poll(); }
 * }
 * @endcode
 */
#ifndef AT_H
#define AT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/** @name 配置项（可在编译选项或此处修改）
 * @{ */
#ifndef AT_MAX_PORTS
/// 支持的最大 AT 端口数
#define AT_MAX_PORTS 1
#endif

#ifndef AT_MAX_QUEUE_SIZE
/// 每个端口的最大命令队列长度（含正在执行的那个）
#define AT_MAX_QUEUE_SIZE 8
#endif

#ifndef AT_MAX_CMD_LEN
/// 单条 AT 命令的最大长度（不含 CR/LF）
#define AT_MAX_CMD_LEN 128
#endif

#ifndef AT_MAX_RESP_LEN
/// 单条命令累计响应的最大长度（可覆盖多行）
#define AT_MAX_RESP_LEN 256
#endif

#ifndef AT_MAX_LINE_LEN
/// 解析器单行最大长度（URC/响应的行）
#define AT_MAX_LINE_LEN 256
#endif

#ifndef AT_MAX_URC_HANDLERS
/// 每个端口可注册的 URC 处理器上限
#define AT_MAX_URC_HANDLERS 10
#endif
/** @} */

/**
 * @brief 命令响应回调
 * @param port_id   源端口
 * @param response  累计的响应文本（中间行拼接，可能为空）
 * @param success   是否以 "OK" 结束（true=成功；false=ERROR/CME/CMS）
 * @param user_arg  用户参数
 */
typedef void (*at_resp_cb_t)(uint8_t port_id, const char *response, bool success, void *user_arg);

/**
 * @brief URC 回调（非命令响应的异步通知）
 * @param port_id   源端口
 * @param urc       完整 URC 文本（单行）
 * @param user_arg  用户参数
 */
typedef void (*at_urc_cb_t)(uint8_t port_id, const char *urc, void *user_arg);

/**
 * @brief 初始化 AT 引擎。
 *
 * 建立内部队列、解析器、分发器等。需在使用其它 API 前调用一次。
 *
 * @param port_count 使用的端口数量（1..AT_MAX_PORTS）；超过上限会被截断
 */
void at_engine_init(uint8_t port_count);

/**
 * @brief 引擎轮询（非阻塞）。
 *
 * 功能：
 * - 从各端口读取可用数据，按行解析
 * - 将行分发到 URC 或命令响应处理
 * - 若端口空闲，则发送队列中的下一条命令（并自动追加 CRLF）
 *
 * 应在主循环中高频调用以保证实时性。
 */
void at_engine_poll(void);

/**
 * @brief 注册 URC 处理器（按前缀匹配）。
 *
 * 任何以 @p prefix 开头的行，都会回调 @p cb。常见如 "+CMTI:", "RING" 等。
 *
 * @param port_id  端口号
 * @param prefix   URC 前缀（如 "+CMTI:"），不能为空字符串
 * @param cb       回调函数
 * @param user_arg 透传给回调的用户参数
 * @return 0 成功；-1 失败（如端口非法或已达上限）
 */
int at_register_urc_handler(uint8_t port_id, const char *prefix, at_urc_cb_t cb, void *user_arg);

/**
 * @brief 反注册 URC 处理器（按前缀）。
 *
 * @param port_id  端口号
 * @param prefix   先前注册的 URC 前缀
 * @return 0 成功；-1 未找到或参数错误
 */
int at_unregister_urc_handler(uint8_t port_id, const char *prefix);

/**
 * @brief 异步发送 AT 命令。
 *
 * 命令被入队，端口空闲时会自动发送（自动追加 "\r\n"）。命令完成（收齐 "OK"/"ERROR..."）
 * 后，通过回调异步报告结果。
 *
 * @note 命令字符串请**不要**包含 CR/LF。
 *
 * @param port_id  端口号
 * @param command  AT 命令字符串（如 "AT+GMR"）
 * @param cb       响应回调
 * @param user_arg 用户参数
 * @return 0 成功入队；-1 失败（如端口非法或队列满）
 */
int at_send_cmd(uint8_t port_id, const char *command, at_resp_cb_t cb, void *user_arg);

#endif /* AT_H */
