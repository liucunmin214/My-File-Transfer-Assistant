/*
 * discover.h - 局域网设备发现模块
 * 协议：UDP 广播 + 单播回应
 *   - 发现方：发送 UDP 广播到端口 DISCOVER_PORT，内容 { type:"query", name:"xxx", id:"uuid" }
 *   - 应答方：收到后单播回 { type:"reply", name:"PC-Name", id:"uuid", port:FILE_PORT }
 */
#ifndef FT_DISCOVER_H
#define FT_DISCOVER_H

#include <winsock2.h>
#include <ws2tcpip.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DISCOVER_PORT   51830   /* UDP 广播端口 */
#define FILE_PORT       51831   /* TCP 文件传输端口（接收端监听） */
#define DISCOVER_MAGIC  0x4654  /* "FT" */
#define MAX_NAME_LEN    64
#define MAX_ID_LEN      40
#define MAX_DEVICES     32

/* 设备信息 */
typedef struct {
    wchar_t  name[MAX_NAME_LEN];      /* 显示名称，如 "刘慧的电脑" */
    wchar_t  id[MAX_ID_LEN];          /* UUID（唯一标识） */
    char     ip[64];                  /* IP 地址字符串 */
    int      port;                    /* 文件传输 TCP 端口 */
    int      last_seen;               /* 上次应答时间戳（秒） */
} ft_device;

/* 事件回调（GUI 线程用） */
/* type: 0=设备更新（列表变化） 1=自己的 UUID 生成成功 */
typedef void (*discover_event_cb_t)(int type, const wchar_t *arg);

/* 初始化设备发现（同时开启监听与周期广播查询） */
int  discover_init(const wchar_t *my_name, int file_tcp_port);

/* 设置事件回调 */
void discover_set_callback(discover_event_cb_t cb);

/* 获取当前在线设备列表，返回数量（<= MAX_DEVICES） */
int  discover_get_devices(ft_device out[], int max_count);

/* 手动触发一次查询（刷新附近设备） */
void discover_refresh(void);

/* 停止并释放资源 */
void discover_shutdown(void);

/* 获取自己的 ID */
const wchar_t *discover_my_id(void);

/* 获取自己的显示名称 */
const wchar_t *discover_my_name(void);

#ifdef __cplusplus
}
#endif

#endif /* FT_DISCOVER_H */
