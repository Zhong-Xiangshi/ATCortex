## ATCortex
这是一个AT指令框架，用于向AT模块发送命令并获得返回结果。
系统接口只需要实现基本的内存分配、消息队列和信号量接口即可使用。
支持异步/同步发送。
### 框架图
![alt text](document/at框架设计.jpg)

### 使用方法
1. 参考ATCortex.h初始化，开一个线程专门定时执行atc_process()。
2. 在UART接收中断处理函数中调用atc_receive_data()将收到的数据推送到ATCortex框架。
3. 然后在其他线程中尽情调用同步/异步发送API。
### 示例
AT线程：
``` C
#include <ATCortex.h>
...

static struct atc_context atc_ctx;

struct atc_context *get_atc_context(){
    return &atc_ctx;
}

//中断处理函数
void UART1_Handler(void)
{
    uint32_t chr=0;
    //中断接收。检查阈值中断和空闲中断
    if(UART_INTStat(UART1, UART_IT_RX_THR | UART_IT_RX_TOUT))
    {
        //硬件缓冲区不为空
        while(UART_IsRXFIFOEmpty(UART1) == 0)
        {
            //读取一个字节
            if(UART_ReadByte(UART1, &chr) == 0)
            {
                //推送到框架处理
                atc_receive_data(&atc_ctx,&chr,1);
            }
        }
        //如果是接收空闲中断，清除空闲中断标志位
        if(UART_INTStat(UART1, UART_IT_RX_TOUT))
        {
            UART_INTClr(UART1, UART_IT_RX_TOUT);
        }
    }
}

//定义接口函数
static void *atc_queue_create(size_t queue_size, size_t item_size) {
    ...
}
static int atc_queue_send(void *queue, const void *data, uint32_t timeout) {
    ...
}
//其他接口函数省略
... 

//实现底层接口函数指针
static struct atc_interface atc_interface_impl = {
    //内存分配接口
    .atc_malloc = k_malloc,
    .atc_free = k_free,

    //信号量接口
    .atc_queue_create = atc_queue_create,
    .atc_queue_send = atc_queue_send,
    .atc_queue_recv = atc_queue_recv,

    //LOG接口
    .atc_log = printf,

    //数据发送接口
    .atc_send = atc_send,

    //信号量接口，使用同步函数需要实现
    .atc_semaphore_create_binary = atc_semaphore_create_binary,
    .atc_semaphore_take = atc_semaphore_take,
    .atc_semaphore_give = atc_semaphore_give,
    .atc_semaphore_delete = atc_semaphore_delete,
};

//单独线程
static void this_task(void *p1, void *p2, void *p3){

    //1.注册接口
    atc_interface_register(&atc_interface_impl);
    //2.初始化上下文
    int ret=atc_init(&atc_ctx);
    printk("atc_init ret=%d\r\n", ret);
    
    for(;;){
        //3.定时运行atc_process处理数据
        atc_process(100);
        k_sleep(K_MSEC(100));
    }
}
```
其他线程中：
``` C
#include <ATCortex.h>
//假设这是其他线程...
void xxx(){

    char tmp[64];
    char rep_buf[64];
    int rep_len=0;
    enum atc_result send_result;

    rep_len=sizeof(rep_buf);
    //准备发送的内容...
    snprintf(tmp, sizeof(tmp), "AT+RST\r\n");
    //阻塞发送，设置超时时间2s
    atc_send_sync(get_atc_context(), tmp, strlen(tmp), &send_result, rep_buf, &rep_len, 2000);
    //打印响应结果
    printf("result=%d rep_len=%d rep=%.*s\r\n", send_result, rep_len, rep_len, rep_buf);
}

```