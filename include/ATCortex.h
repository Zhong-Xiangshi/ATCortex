#ifndef ATCORTEX_H
#define ATCORTEX_H
//遵循 Linux 内核风格，除了宏（Macros）和枚举常量（Enum constants）之外，所有东西都使用 snake_case（全小写 + 下划线）。

#include <stddef.h>
#include "../ring_buffer.h"
#include "../slist.h"


//串口接收环形缓冲区大小(Bytes)
#define ATC_RX_BUFFER_SIZE 256
//接收到单行的最大长度(Bytes)
#define ATC_RX_LINE_MAX_SIZE 128
//接收到响应的最大字节数
#define ATC_RX_RESPONSE_MAX 256

struct atc_context;

enum atc_result{
    ATC_SUCCESS = 0,
    ATC_ERROR = -1,
    ATC_TIMEOUT = -2,
};

//内存分配函数
typedef void *(*atc_malloc_t)(size_t size);
typedef void (*atc_free_t)(void *ptr);

//消息队列函数
#define ATC_TIMEOUT_MAX 0xFFFFFFFF  //永久等待
/**
 * @brief 创建消息队列
 * 
 * @param queue_size 队列深度（最大消息数量）
 * @param item_size  单个消息的大小（字节）
 * @return void*     成功返回队列句柄，失败返回 NULL
 */
typedef void *(*atc_queue_create_t)(size_t queue_size,size_t item_size);

/**
 * @brief 向队列发送消息
 * 
 * @param queue   队列句柄
 * @param data    要发送的数据指针
 * @param timeout 超时时间（毫秒），0表示非阻塞，ATC_QUEUE_INFINITE_WAIT表示无限等待
 * @return int    成功返回 0 (ATC_SUCCESS)，失败返回 -1 (ATC_ERROR)
 */
typedef int (*atc_queue_send_t)(void *queue, const void *data, uint32_t timeout);

/**
 * @brief 从队列接收消息
 * 
 * @param queue   队列句柄
 * @param data    接收数据的缓冲区指针
 * @param timeout 超时时间（毫秒），0表示非阻塞，ATC_QUEUE_INFINITE_WAIT表示无限等待
 * @return int    成功返回 0 (ATC_SUCCESS)，失败返回 -1 (ATC_ERROR)
 */
typedef int (*atc_queue_recv_t)(void *queue, void *data, uint32_t timeout);

//log函数
typedef int (*atc_log_t)(const char *format, ...);

//数据发送函数
typedef enum atc_result (*atc_send_t)(struct atc_context *context, const char *data, size_t length);

//底层接口
struct atc_interface{
    atc_malloc_t atc_malloc;
    atc_free_t atc_free;
    atc_queue_create_t atc_queue_create;
    atc_queue_send_t atc_queue_send;
    atc_queue_recv_t atc_queue_recv;
    atc_log_t atc_log;
    atc_send_t atc_send;
};



struct atc_context{
    ring_buffer_t rx_buffer;
    uint8_t rx_buffer_data[ATC_RX_BUFFER_SIZE];

    //外部API消息队列
    void *external_api_queue;
    //发送消息队列
    void *send_queue;

    //URC处理链表
    slist_t *urc_handler_list;

    //当前发送任务
    struct send_task *current_send_task;

    //行缓冲数组
    char line_buffer[ATC_RX_LINE_MAX_SIZE];
    uint32_t line_buffer_index;

    //响应缓冲区
    char response[ATC_RX_RESPONSE_MAX];
    size_t response_length; //当前响应数据长度
};

//URC处理函数类型定义
typedef void (*atc_urc_handler_t)(struct atc_context *context, const char *line_data);

//AT命令发送返回的结果回调
typedef void (*atc_cmd_response_handler_t)(struct atc_context *context, enum atc_result result, const char *response, size_t response_length);

/* ==========================================================================
 * Section: Public API (Exposed)
 * Description: 供外部模块调用的接口
 * ========================================================================== */

 /**
  * @brief ATC底层接口注册
  * 
  * @param interface 底层接口实现
  * @return enum atc_result 成功返回 ATC_SUCCESS，失败返回 ATC_ERROR
  */
enum atc_result atc_interface_register(struct atc_interface *interface);

/**
 * @brief 初始化ATC上下文，必须先调用atc_interface_register注册底层接口，然后才能调用此函数
 * 
 * @param context ATC上下文
 * @return enum atc_result 成功返回 ATC_SUCCESS，失败返回 ATC_ERROR
 */
enum atc_result atc_init(struct atc_context *context);

/**
 * @brief ATC处理函数，需周期性调用
 * 
 * @param ms_elapsed 距离上次调用经过的时间（ms）
 */
void atc_process(uint32_t ms_elapsed);

/**
 * @brief URC注册函数
 * 
 * @param context ATC上下文
 * @param prefix URC前缀，不包含'+'
 * @param handler URC处理函数   
 * @return enum atc_result 
 */
enum atc_result atc_urc_register(struct atc_context *context , const char *prefix, atc_urc_handler_t handler);

/**
 * @brief 异步发送AT命令
 * 
 * @param context ATC上下文
 * @param data 要发送的AT命令数据
 * @param length 数据长度
 * @param response_handler 命令响应处理回调
 * @param timeout 超时时间（ms）
 */
enum atc_result atc_send_async(struct atc_context *context, const char *data, size_t length, atc_cmd_response_handler_t response_handler,uint32_t timeout);

/**
 * @brief 串口接收到数据推到ATC模块
 * 
 * @param context ATC上下文
 * @param data 接收到的数据
 * @param length 接收到的数据长度
 * @return int 实际推入缓冲区的字节数
 */
int atc_receive_data(struct atc_context *context, const char *data, size_t length);

/* ==========================================================================
 * Section: Private / Internal
 * Description: 内部使用的辅助函数或结构体
 * Warning:   请勿在模块外部直接使用
 * ========================================================================== */
uint32_t _atc_time_get();

#endif // ATCORTEX_H