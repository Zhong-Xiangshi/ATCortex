/**
 * @file at.h
 * @brief AT 指令框架对外统一 API（无操作系统、轮询方式）/ Public API for a no-OS, polling AT-command framework.
 *
 * 功能特性 (Features):
 * - 多 AT 端口 (multiple ports, configurable via AT_MAX_PORTS)
 * - 异步命令队列 + 回调 (async queue + callbacks)
 * - 行级状态机解析（以 '\n' 结尾，忽略 '\r'）(line-based parser: '\n' terminated, '\r' ignored)
 * - 分发器区分命令响应与 URC，支持 URC 前缀注册/反注册
 *   (dispatcher for responses vs. URCs; supports URC prefix register/unregister)
 * - 轻量日志（AT_ENABLE_LOG）(lightweight logging via AT_ENABLE_LOG)
 * - **命令超时**（默认 100ms，支持自定义）(**command timeout**: default 100ms; per-command custom timeout)
 * - **回显处理**（按端口配置是否忽略）(**echo handling**: per-port ignore policy)
 *
 * 使用示例 (Usage):
 * @code
 * // Echo 忽略配置(每端口): ignore echo on port 0, keep on port 1
 * bool echo_map[2] = { true, false };
 * at_engine_init_ex(2, echo_map);
 *
 * // 注册URC (Register URC)
 * at_register_urc_handler(0, "RING", my_urc_cb, NULL);
 *
 * // 默认超时 100ms (default timeout 100ms)
 * at_send_cmd(0, "AT", my_resp_cb, NULL);
 *
 * // 自定义超时 500ms (custom timeout 500ms)
 * at_send_cmd_ex(0, "AT+GMR", 500, my_resp_cb, NULL);
 *
 * for (;;) { at_engine_poll(); }
 * @endcode
 */
#ifndef AT_H
#define AT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/** @name 配置项 / Configuration
 * @{ */
#ifndef AT_MAX_PORTS
/// 支持的最大 AT 端口数 / Max number of AT ports supported
#define AT_MAX_PORTS 2
#endif

#ifndef AT_MAX_QUEUE_SIZE
/// 每端口最大命令队列长度（含正在执行）/ Max commands queued per port (including in-flight)
#define AT_MAX_QUEUE_SIZE 8
#endif

#ifndef AT_MAX_CMD_LEN
/// 单条 AT 命令最大长度（不含 CR/LF）/ Max AT command length (excluding CR/LF)
#define AT_MAX_CMD_LEN 128
#endif

#ifndef AT_MAX_RESP_LEN
/// 单条命令累计响应最大长度 / Max accumulated response length per command
#define AT_MAX_RESP_LEN 256
#endif

#ifndef AT_MAX_LINE_LEN
/// 解析器单行最大长度 / Max length of a parsed input line
#define AT_MAX_LINE_LEN 256
#endif

#ifndef AT_MAX_URC_HANDLERS
/// 每端口可注册的 URC 处理器上限 / Max URC handlers per port
#define AT_MAX_URC_HANDLERS 10
#endif

#ifndef AT_DEFAULT_TIMEOUT_MS
/// 默认命令超时（毫秒）/ Default command timeout in milliseconds
#define AT_DEFAULT_TIMEOUT_MS 100u
#endif
/** @} */

/**
 * @brief 命令响应回调 / AT command response callback
 * @param port_id   源端口 / Source port
 * @param response  累计的响应文本（可能为空）/ Concatenated response text (may be empty)
 * @param success   是否以 "OK" 结束（true 成功；false ERROR/CME/CMS）/
 *                  Whether final line indicates success ("OK") or failure (ERROR/CME/CMS)
 * @param user_arg  用户参数 / User argument passthrough
 */
typedef void (*at_resp_cb_t)(uint8_t port_id, const char *response, bool success, void *user_arg);

/**
 * @brief URC 回调（非命令响应的异步通知）/ URC callback (unsolicited result)
 * @param port_id   源端口 / Source port
 * @param urc       完整 URC 文本（单行）/ Full URC text (single line)
 * @param user_arg  用户参数 / User argument passthrough
 */
typedef void (*at_urc_cb_t)(uint8_t port_id, const char *urc, void *user_arg);

/**
 * @brief 初始化 AT 引擎（基础版）/ Initialize AT engine (basic)
 *
 * 建立内部队列、解析器、分发器等；所有端口默认**不忽略回显**。  
 * Set up queues, parser, dispatcher; all ports default to **do not ignore echo**.
 *
 * @param port_count 使用的端口数量（1..AT_MAX_PORTS；超出将被截断）/
 *                   Number of ports to use; capped to AT_MAX_PORTS.
 */
void at_engine_init(uint8_t port_count);

/**
 * @brief 初始化 AT 引擎（扩展版：端口回显策略）/ Initialize AT engine (extended: per-port echo policy)
 *
 * 与 @ref at_engine_init 相同，但可为每个端口配置是否忽略回显。  
 * Same as @ref at_engine_init, additionally allows per-port echo ignore configuration.
 *
 * @param port_count         端口数量（会被截断至 AT_MAX_PORTS）/ Number of ports (capped to AT_MAX_PORTS)
 * @param echo_ignore_map    长度为 port_count 的布尔数组；true=忽略回显，false=不忽略。  
 *                           传 NULL 则等效于全部 false。/
 *                           Boolean array of length port_count; true=ignore echo, false=keep.
 *                           If NULL, all treated as false.
 */
void at_engine_init_ex(uint8_t port_count, const bool *echo_ignore_map);

/**
 * @brief 引擎轮询（非阻塞）/ Poll the AT engine (non-blocking)
 *
 * 功能 / What it does:
 * - 从各端口读取数据并按行解析 / Read per-port input and parse by line
 * - 分发到 URC 或命令响应 / Dispatch to URC or command response
 * - 若端口空闲则发送下一条命令（自动追加 CRLF）/ Send next queued command if port is idle (auto-append CRLF)
 * - 检查进行中命令是否**超时** / Check **timeout** for in-flight command
 *
 * 应在主循环中高频调用。/ Call frequently in the main loop.
 */
void at_engine_poll(void);

/**
 * @brief 注册 URC 处理器（前缀匹配）/ Register a URC handler (prefix match)
 *
 * 任一以 @p prefix 开头的行，都会触发回调。  
 * Any incoming line starting with @p prefix triggers the callback.
 *
 * @param port_id  端口号 / Port index
 * @param prefix   URC 前缀（如 "+CMTI:"），不能为空 / URC prefix (e.g., "+CMTI:"); must not be empty
 * @param cb       回调函数 / Callback function
 * @param user_arg 透传参数 / User argument passthrough
 * @return 0 成功；-1 失败（端口非法/已满/参数错误）/
 *         0 on success; -1 on failure (invalid port/full/invalid args)
 */
int at_register_urc_handler(uint8_t port_id, const char *prefix, at_urc_cb_t cb, void *user_arg);

/**
 * @brief 反注册 URC 处理器（按前缀）/ Unregister a URC handler (by prefix)
 *
 * @param port_id  端口号 / Port index
 * @param prefix   需移除的前缀 / Prefix to remove
 * @return 0 成功；-1 未找到或参数错误 / 0 on success; -1 if not found or invalid args
 */
int at_unregister_urc_handler(uint8_t port_id, const char *prefix);

/**
 * @brief 异步发送 AT 命令（默认超时）/ Send AT command asynchronously (default timeout)
 *
 * 命令入队，端口空闲时自动发送（自动追加 "\r\n"），并在收到最终行（OK/ERROR/CME/CMS）后回调。  
 * Queues the command; if port is idle, it is sent (with "\r\n" appended). The callback is invoked
 * when a final line (OK/ERROR/CME/CMS) is received.
 *
 * @note 命令字符串应**不包含** CR/LF。/ Command string **must not** include CR/LF.
 * @note 超时默认为 @ref AT_DEFAULT_TIMEOUT_MS。/ Timeout defaults to @ref AT_DEFAULT_TIMEOUT_MS.
 *
 * @param port_id  端口号 / Port index
 * @param command  AT 命令字符串，如 "AT+GMR" / AT command string, e.g., "AT+GMR"
 * @param cb       响应回调 / Response callback
 * @param user_arg 用户参数 / User argument passthrough
 * @return 0 成功入队；-1 失败（端口非法/队列满等）/
 *         0 on success (queued); -1 on failure (invalid port/queue full/etc.)
 */
int at_send_cmd(uint8_t port_id, const char *command, at_resp_cb_t cb, void *user_arg);

/**
 * @brief 异步发送 AT 命令（自定义超时）/ Send AT command asynchronously (custom timeout)
 *
 * 与 @ref at_send_cmd 相同，但可为该命令指定 @p timeout_ms。  
 * Same as @ref at_send_cmd, but with per-command @p timeout_ms.
 *
 * @param port_id     端口号 / Port index
 * @param command     AT 命令字符串 / AT command string
 * @param timeout_ms  超时时间（毫秒）/ Timeout in milliseconds
 * @param cb          响应回调 / Response callback
 * @param user_arg    用户参数 / User argument passthrough
 * @return 0 成功入队；-1 失败 / 0 on success; -1 on failure
 */
int at_send_cmd_ex(uint8_t port_id, const char *command, uint32_t timeout_ms, at_resp_cb_t cb, void *user_arg);

#endif /* AT_H */
