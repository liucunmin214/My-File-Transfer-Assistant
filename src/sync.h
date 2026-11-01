/*
 * 同步引擎 - 每秒扫描源目录，diff 后推送到接收端
 */
#ifndef SYNC_SYNC_H
#define SYNC_SYNC_H
#include <windows.h>

#ifdef SYNC_DLL_EXPORTS
#define SYNC_API __declspec(dllexport)
#else
#define SYNC_API __declspec(dllimport)
#endif

/* 事件回调
 * type: 0=log(arg=text)
 *       1=status(arg=状态串: L"idle"/L"syncing"/L"connecting"/L"error")
 *       2=file_synced(arg=relpath, num=size)
 *       3=file_deleted(arg=relpath)
 *       4=scanned(num=文件数)
 */
typedef void (*sync_event_cb)(int type, const wchar_t *arg, int num);

SYNC_API void sync_set_event_cb(sync_event_cb cb);
SYNC_API void sync_set_source(const wchar_t *path);
SYNC_API void sync_set_selection(int sync_all, const wchar_t **paths, int count);
SYNC_API void sync_set_target(const char *host, int port);

SYNC_API int  sync_start(void);
SYNC_API void sync_stop(void);
SYNC_API int  sync_running(void);

#endif
