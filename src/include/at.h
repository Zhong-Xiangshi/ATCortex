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
 * - 命令超时（默认 100ms，支持自定义）(command timeout: default 100 ms; per-command custom)
 * - 回显处理（端口级开关）(echo handling per port)
 * - **事务型命令**：支持 `"> "` 提示模式与**长度模式**二进制发送
 *   (**Transactional commands**: prompt-based `"> "` and **length-based** binary sending)
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

/** 回调类型 / Callbacks */
typedef void (*at_resp_cb_t)(uint8_t port_id, const char *response, bool success, void *user_arg);
typedef void (*at_urc_cb_t )(uint8_t port_id, const char *urc,     void *user_arg);

/** ---------------- 新增：事务描述 / Transaction descriptor ---------------- */

/**
 * @brief 事务类型 / Transaction type
 *
 * - AT_TXN_NONE：普通行模式（不带数据阶段）
 * - AT_TXN_PROMPT：等待某个提示（默认 "> "）后，再发送数据负载，可选终止符（如 0x1A）
 * - AT_TXN_LENGTH：长度模式，发送完命令后**立即**发送固定长度负载（可选终止符）
 *
 * - AT_TXN_NONE: normal line-oriented command (no binary data stage)
 * - AT_TXN_PROMPT: wait for a prompt (default "> ") then send payload, optional terminator (e.g. 0x1A)
 * - AT_TXN_LENGTH: length-based; send fixed-size payload immediately after the command (optional terminator)
 */
typedef enum {
    AT_TXN_NONE   = 0,
    AT_TXN_PROMPT = 1,
    AT_TXN_LENGTH = 2,
} at_txn_type_t;

/**
 * @brief 事务描述（仅在事务型命令使用）/ Transaction descriptor (used for transactional commands)
 *
 * @note 所有指针（payload/terminator/prompt）在命令完成回调前必须保持有效。
 *       All pointers MUST remain valid until the command completes (callback fired).
 */
typedef struct {
    at_txn_type_t    type;          /**< 事务类型 / Transaction type */

    const uint8_t   *payload;       /**< 数据负载指针 / Payload pointer */
    size_t           payload_len;   /**< 数据负载长度 / Payload length */

    const uint8_t   *terminator;    /**< 终止符指针（可为 NULL）/ Terminator pointer (may be NULL) */
    size_t           term_len;      /**< 终止符长度（可为 0）/ Terminator length (may be 0) */

    const char      *prompt;        /**< 提示串（AT_TXN_PROMPT 使用；NULL=默认"> "）/ Prompt string (PROMPT mode; NULL = default "> ") */
    size_t           prompt_len;    /**< 提示长度（0=自动 strlen 或 2 对于默认）/ Prompt length (0 = auto strlen or 2 for default) */
} at_txn_desc_t;

/** ---------------- 基础 API / Base API ---------------- */

void at_engine_init   (uint8_t port_count);
void at_engine_init_ex(uint8_t port_count, const bool *echo_ignore_map);
void at_engine_poll   (void);

int  at_register_urc_handler  (uint8_t port_id, const char *prefix, at_urc_cb_t cb, void *user_arg);
int  at_unregister_urc_handler(uint8_t port_id, const char *prefix);

int  at_send_cmd   (uint8_t port_id, const char *command, at_resp_cb_t cb, void *user_arg);
int  at_send_cmd_ex(uint8_t port_id, const char *command, uint32_t timeout_ms, at_resp_cb_t cb, void *user_arg);

/** ---------------- 新增：事务型发送 API / New: Transactional send APIs ---------------- */

/**
 * @brief 事务型发送（通用）/ Send AT command with a transaction (generic)
 *
 * 发送命令后根据 @p txn 的描述执行数据阶段（等待提示或直接发送固定长度负载），
 * 然后继续等待最终行（OK/ERROR/SEND OK 等）并回调结果。
 *
 * After sending the command, perform the data phase per @p txn (either wait for a prompt or send a fixed-length payload),
 * then wait for final lines (OK/ERROR/SEND OK etc.) and invoke the callback.
 *
 * @param port_id    端口号 / Port index
 * @param command    命令字符串（不含 CR/LF）/ AT command string (without CR/LF)
 * @param txn        事务描述（不可为 NULL）/ Transaction descriptor (must not be NULL)
 * @param timeout_ms 超时时间（ms）/ Timeout in milliseconds
 * @param cb         响应回调 / Response callback
 * @param user_arg   透传参数 / User argument
 * @return 0 入队成功；-1 失败 / 0 on success; -1 on failure
 */
int at_send_cmd_txn(uint8_t port_id, const char *command, const at_txn_desc_t *txn,
                    uint32_t timeout_ms, at_resp_cb_t cb, void *user_arg);

/**
 * @brief 提示模式便捷函数：等待 `"> "`（或自定义）后发送负载，可选终止符
 *        Convenience: prompt-based transaction (wait for `"> "` then send payload; optional terminator).
 */
static inline int at_send_cmd_txn_prompt(uint8_t port_id, const char *command,
                                         const uint8_t *payload, size_t payload_len,
                                         const uint8_t *terminator, size_t term_len,
                                         const char *prompt, uint32_t timeout_ms,
                                         at_resp_cb_t cb, void *user_arg)
{
    at_txn_desc_t t = {
        .type        = AT_TXN_PROMPT,
        .payload     = payload,
        .payload_len = payload_len,
        .terminator  = terminator,
        .term_len    = term_len,
        .prompt      = prompt,
        .prompt_len  = 0,      // 0 => auto
    };
    return at_send_cmd_txn(port_id, command, &t, timeout_ms, cb, user_arg);
}

/**
 * @brief 长度模式便捷函数：命令发出后立即发送固定长度负载（可选终止符）
 *        Convenience: length-based transaction (send fixed-length payload immediately after command; optional terminator).
 */
static inline int at_send_cmd_txn_len(uint8_t port_id, const char *command,
                                      const uint8_t *payload, size_t payload_len,
                                      const uint8_t *terminator, size_t term_len,
                                      uint32_t timeout_ms, at_resp_cb_t cb, void *user_arg)
{
    at_txn_desc_t t = {
        .type        = AT_TXN_LENGTH,
        .payload     = payload,
        .payload_len = payload_len,
        .terminator  = terminator,
        .term_len    = term_len,
        .prompt      = NULL,
        .prompt_len  = 0,
    };
    return at_send_cmd_txn(port_id, command, &t, timeout_ms, cb, user_arg);
}

#endif /* AT_H */
