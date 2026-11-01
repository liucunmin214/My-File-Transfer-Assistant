/*
 * 网络层 - Winsock TCP 接收端服务器 + 发送端客户端
 * 协议帧: [4B magic "SYNC"][4B header_len BE][header json][file data]
 */
#ifndef SYNC_NET_H
#define SYNC_NET_H
#include <windows.h>

#ifdef SYNC_DLL_EXPORTS
#define NET_API __declspec(dllexport)
#else
#define NET_API __declspec(dllimport)
#endif

/* 接收端 */
NET_API int  net_receiver_start(int port, const wchar_t *dest_dir);
NET_API void  net_receiver_stop(void);

/* 发送端 */
NET_API int  net_sender_connect(const char *host, int port);
NET_API void  net_sender_disconnect(void);
NET_API int  net_sender_connected(void);
NET_API int  net_sender_send_file(const wchar_t *relpath,
                                  const void *data, unsigned long size,
                                  double mtime);
NET_API int  net_sender_send_delete(const wchar_t *relpath);

#endif
