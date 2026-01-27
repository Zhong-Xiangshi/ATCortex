#ifndef LOG_H
#define LOG_H

#include "interface.h"

// --- 日志级别定义 ---
#define LOG_LEVEL_ERROR 0
#define LOG_LEVEL_WARN  1
#define LOG_LEVEL_INFO  2
#define LOG_LEVEL_DEBUG 3
#define LOG_LEVEL_TRACE 4

#define DBG_NAME "ATCortex"
#define LOG_LEVEL LOG_LEVEL_TRACE

#ifndef LOG_LEVEL 
    #define LOG_LEVEL LOG_LEVEL_TRACE
#endif
#ifndef DBG_NAME 
    #define DBG_NAME "Unknown"
#endif
// #define LOG_LEVEL LOG_LEVEL_DEBUG
// #define DBG_NAME "Unknown"

// --- 日志宏定义 ---

#if LOG_LEVEL >= LOG_LEVEL_ERROR
    #define LOG_ERR(fmt,...) g_atc_interface.atc_log(DBG_NAME"[ERROR][line:%d][%s]:"fmt"\r\n", __LINE__,__func__,##__VA_ARGS__)
#else
    #define LOG_ERR(fmt,...) do { if(0) g_atc_interface.atc_log(fmt, ##__VA_ARGS__); } while(0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARN
    #define LOG_WARN(fmt,...) g_atc_interface.atc_log(DBG_NAME"[WARN][line:%d][%s]:"fmt"\r\n", __LINE__,__func__,##__VA_ARGS__)
#else
    #define LOG_WARN(fmt,...) do { if(0) g_atc_interface.atc_log(fmt, ##__VA_ARGS__); } while(0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
    #define LOG_INFO(fmt,...) g_atc_interface.atc_log(DBG_NAME"[INFO][line:%d][%s]:"fmt"\r\n", __LINE__,__func__,##__VA_ARGS__)
#else
    #define LOG_INFO(fmt,...) do { if(0) g_atc_interface.atc_log(fmt, ##__VA_ARGS__); } while(0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
    #define LOG_DEBUG(fmt,...) g_atc_interface.atc_log(DBG_NAME"[DEBUG][line:%d][%s]:"fmt"\r\n", __LINE__,__func__,##__VA_ARGS__)
#else
    #define LOG_DEBUG(fmt,...) do { if(0) g_atc_interface.atc_log(fmt, ##__VA_ARGS__); } while(0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_TRACE
    #define LOG_TRACE g_atc_interface.atc_log(DBG_NAME"[TRACE][%s] to line:%d\r\n", __func__,__LINE__)
#else
    #define LOG_TRACE
#endif

#endif // LOG_H