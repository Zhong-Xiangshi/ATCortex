/**
 * @file at.h
 * @brief ATCortex —— 轻量 AT 指令框架（无操作系统，轮询式）
 *
 * @details
 * 特性：
 * - 多端口支持（由 AT_MAX_PORTS 配置）
 * - 异步命令队列 + 回调（每端口循环队列）
 * - 行级解析（'\n' 结束，忽略 '\r'）
 * - 分发器区分命令响应与 URC（可注册/反注册前缀）
 * - 轻量日志（AT_ENABLE_LOG 控制）
 * - 命令超时（默认 100ms，可自定义）
 * - 回显策略：可按端口忽略首个回显行
 * - 事务型命令：支持 **PROMPT 提示模式** 与 **LENGTH 固定长度模式**，与 **PROMPT 接收模式**
 *
 * 使用约定：
 * - 所有 API 为线程无关假设，典型用法是在主循环中周期性调用 at_engine_poll()。
 * - 事务型发送中涉及的指针（payload/terminator/prompt）在回调前必须保持有效。
 * - 日志输出为英文，源码注释使用中文。
 */

#ifndef AT_H
#define AT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/** @name 配置项
 *  @brief 可在编译期通过 -D 宏覆盖
 *  @{ */
#ifndef AT_MAX_PORTS
#define AT_MAX_PORTS 2           /**< 最大 AT 端口数 */
#endif

#ifndef AT_MAX_QUEUE_SIZE
#define AT_MAX_QUEUE_SIZE 8      /**< 每端口最大队列长度（含正在执行） */
#endif

#ifndef AT_MAX_CMD_LEN
#define AT_MAX_CMD_LEN 128       /**< 单条命令最大长度（不含 CR/LF） */
#endif

#ifndef AT_MAX_RESP_LEN
#define AT_MAX_RESP_LEN 512      /**< 单条命令累计响应的最大长度（所有中间行 + 最终行） */
#endif

#ifndef AT_MAX_LINE_LEN
#define AT_MAX_LINE_LEN 256      /**< 解析器单行最大长度 */
#endif

#ifndef AT_MAX_URC_HANDLERS
#define AT_MAX_URC_HANDLERS 10   /**< 每端口可注册的 URC 处理器上限 */
#endif

#ifndef AT_DEFAULT_TIMEOUT_MS
#define AT_DEFAULT_TIMEOUT_MS 100u /**< 默认超时（毫秒） */
#endif
/** @} */

/** @name 库信息
 *  @{ */
#define ATCORTEX_NAME    "ATCortex"
#define ATCORTEX_VERSION "1.1.0"
/** @} */

/** @name 回调类型
 *  @{ */
typedef void (*at_resp_cb_t)(uint8_t port_id, const char *response, bool success, void *user_arg);
typedef void (*at_urc_cb_t )(uint8_t port_id, const char *urc,     void *user_arg);
/** @} */

/** @name 事务发送
 *  @details 仅在事务型命令使用
 *  @{ */
/**
 * @brief 事务类型
 *
 * - AT_TXN_NONE      ：普通行命令（无二进制数据阶段）
 * - AT_TXN_PROMPT    ：等待提示（默认 "> "）后发送负载，可选终止符（如 Ctrl+Z=0x1A）
 * - AT_TXN_LENGTH    ：长度模式，命令发出后**立即**发送固定长度负载（可选终止符）
 * - AT_TXN_PROMPT_RX ：等待提示（默认 "> "）后，进入数据接收模式，直到最终态（OK/ERROR）
 */
typedef enum {
    AT_TXN_NONE      = 0,
    AT_TXN_PROMPT    = 1,
    AT_TXN_LENGTH    = 2,
    AT_TXN_PROMPT_RX = 3,
} at_txn_type_t;

/**
 * @brief 事务描述
 * @note  所有指针字段在回调前必须保持有效
 */
typedef struct {
    at_txn_type_t    type;          /**< 事务类型 */
    const uint8_t   *payload;       /**< 负载指针 (PROMPT/LENGTH 模式用) */
    size_t           payload_len;   /**< 负载长度 (PROMPT/LENGTH 模式用) */
    const uint8_t   *terminator;    /**< 终止符指针，可为 NULL (PROMPT/LENGTH 模式用) */
    size_t           term_len;      /**< 终止符长度，可为 0 (PROMPT/LENGTH 模式用) */
    const char      *prompt;        /**< 提示串（PROMPT/PROMPT_RX 模式用；NULL=默认"> "） */
    size_t           prompt_len;    /**< 提示长度（0=自动） */
} at_txn_desc_t;
/** @} */

/** @name 引擎初始化与轮询
 *  @{ */
/**
 * @brief 初始化引擎
 * @param port_count 端口数量（会被裁剪到 [1, AT_MAX_PORTS]）
 */
void at_engine_init(uint8_t port_count);

/**
 * @brief 扩展初始化：配置每端口回显忽略策略
 * @param port_count     端口数量
 * @param echo_ignore_map 每端口是否忽略首个回显行的布尔数组（可为 NULL 表示全为 false）
 */
void at_engine_init_ex(uint8_t port_count, const bool *echo_ignore_map);

/**
 * @brief 引擎轮询函数（应在主循环高频调用）
 * @note  负责读取端口、解析行、推进事务、检测超时与派发回调
 */
void at_engine_poll(void);
/** @} */

/** @name URC 处理器
 *  @{ */
int at_register_urc_handler  (uint8_t port_id, const char *prefix, at_urc_cb_t cb, void *user_arg);
int at_unregister_urc_handler(uint8_t port_id, const char *prefix);
/** @} */

/** @name 发送命令
 *  @{ */
/**
 * @brief 发送普通命令（使用默认超时）
 */
int at_send_cmd(uint8_t port_id, const char *command, at_resp_cb_t cb, void *user_arg);

/**
 * @brief 发送普通命令（自定义超时）
 * @param timeout_ms 0 表示使用默认超时
 */
int at_send_cmd_ex(uint8_t port_id, const char *command, uint32_t timeout_ms, at_resp_cb_t cb, void *user_arg);

/**
 * @brief 发送事务型命令（通用）
 */
int at_send_cmd_txn(uint8_t port_id, const char *command, const at_txn_desc_t *txn,
                    uint32_t timeout_ms, at_resp_cb_t cb, void *user_arg);

/**
 * @brief 事务便捷：提示模式（等待 prompt 后发负载，可选终止符）。所有指针字段在回调前必须保持有效
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
        .prompt_len  = 0,
    };
    return at_send_cmd_txn(port_id, command, &t, timeout_ms, cb, user_arg);
}

/**
 * @brief 事务便捷：长度模式（命令后立即发定长负载，可选终止符）。所有指针字段在回调前必须保持有效
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

/**
 * @brief 事务便捷：提示接收模式（等待 prompt 后，将后续所有行作为响应，直到 OK/ERROR）。所有指针字段在回调前必须保持有效
 */
static inline int at_send_cmd_txn_prompt_rx(uint8_t port_id, const char *command,
                                            const char *prompt, uint32_t timeout_ms,
                                            at_resp_cb_t cb, void *user_arg)
{
    at_txn_desc_t t = {
        .type        = AT_TXN_PROMPT_RX,
        .payload     = NULL,
        .payload_len = 0,
        .terminator  = NULL,
        .term_len    = 0,
        .prompt      = prompt,
        .prompt_len  = 0,
    };
    return at_send_cmd_txn(port_id, command, &t, timeout_ms, cb, user_arg);
}
/** @} */

#endif /* AT_H */
