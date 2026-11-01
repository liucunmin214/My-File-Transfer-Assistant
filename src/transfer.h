/*
 * transfer.h - 文件传输核心模块
 * 协议：TCP 长连接，分帧 JSON 头 + 二进制内容
 *   - 接收端（服务端）listen FILE_PORT，接受多个 send 任务
 *   - 发送端（客户端）连接 -> 发送任务列表 -> 逐文件发送
 */
#ifndef FT_TRANSFER_H
#define FT_TRANSFER_H

#include <winsock2.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FT_MAX_PATH 520
#define FT_CHUNK_SIZE (2 * 1024 * 1024)  /* 2MB 块 — 大块减少系统调用次数 */

/* 传输进度事件 */
/* type: 0=任务开始 1=任务进度(%) 2=任务完成 3=任务错误 4=接收文件已保存 */
typedef void (*transfer_event_cb_t)(int type, int64_t task_id,
                                    int percent, int64_t bytes, int64_t total,
                                    const wchar_t *file_name, const wchar_t *extra);

/* 发送任务结构（用户填写） */
typedef struct {
    wchar_t full_path[FT_MAX_PATH];  /* 本地绝对路径 */
    wchar_t rel_name[FT_MAX_PATH];   /* 对端保存名（可含相对子目录） */
} ft_send_item;

/* ===== 发送端 API ===== */

/* 初始化发送端（可复用连接） */
int  ft_send_init(void);

/* 设置进度/状态回调 */
void ft_send_set_callback(transfer_event_cb_t cb);

/* 发送一批文件到目标 IP:port。返回任务组 ID（正数），失败返回 -1 */
int64_t ft_send_files(const char *dest_ip, int dest_port,
                      const ft_send_item *items, int item_count);

/* 取消指定任务组（如果正在发送） */
void ft_send_cancel(int64_t group_id);

/* 关闭发送端 */
void ft_send_shutdown(void);

/* ===== 接收端 API ===== */

/* 启动接收端（监听 TCP port），save_dir 是默认保存目录 */
int  ft_recv_start(int port, const wchar_t *save_dir);

/* 设置接收回调（进度/完成/错误） */
void ft_recv_set_callback(transfer_event_cb_t cb);

/* 动态修改保存目录 */
void ft_recv_set_save_dir(const wchar_t *save_dir);

/* 停止接收端 */
void ft_recv_stop(void);

/* 是否正在运行 */
int  ft_recv_is_running(void);

/* 获取保存目录 */
const wchar_t *ft_recv_save_dir(void);

#ifdef __cplusplus
}
#endif

#endif /* FT_TRANSFER_H */
