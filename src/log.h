/*
 * log.h - 日志模块头文件
 * MyFileTransferAssistant Core DLL - 日志输出到文件和回调
 */
#ifndef FT_LOG_H
#define FT_LOG_H

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 日志级别 */
enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3
};

/* 日志回调函数类型 */
typedef void (*log_sink_cb_t)(int level, const wchar_t *msg);

/* 初始化日志（传入 AppData 目录，可为 NULL 则只用回调） */
int log_init(const wchar_t *appdata_dir);

/* 设置日志回调（GUI 线程接收日志并显示） */
void log_set_sink(log_sink_cb_t cb);

/* 设置日志输出级别过滤（默认 DEBUG） */
void log_set_level(int level);

/* 写日志函数（内部用） */
void log_write(int level, const wchar_t *fmt, ...);

/* 关闭日志 */
void log_close(void);

/* 便捷宏 */
#define LOG_DEBUG(...) log_write(LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  log_write(LOG_INFO,  __VA_ARGS__)
#define LOG_WARN(...)  log_write(LOG_WARN,  __VA_ARGS__)
#define LOG_ERROR(...) log_write(LOG_ERROR, __VA_ARGS__)

/* 兼容 net.c / sync.c 历史写法：log_line(...) 等价于 LOG_INFO(...) */
#define log_line(...)   log_write(LOG_INFO,  __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* FT_LOG_H */
