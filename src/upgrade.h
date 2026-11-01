/*
 * upgrade.h - 软件升级模块（检测 + 下载执行）
 *   流程：WinHTTP GET 服务器 update.json → 解析版本/简介/脚本地址
 *        → 与本地版本比较 → GUI 弹窗提醒 → 用户确认后 PowerShell IEX
 *          下载并执行服务器 upgrade.ps1（下载安装包→关旧程序→启动安装）
 */
#ifndef FT_UPGRADE_H
#define FT_UPGRADE_H

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UP_NOTES_MAX  1024   /* 新版简介最大字符数 */

/* 检测结果 */
typedef struct {
int      has_update;              /* 1=有新版 0=已是最新 */
int      rollback;                /* 1=服务器当前发布低于本地（管理后台回滚了版本） */
wchar_t  version[32];             /* 服务器版本号，如 V1.2.0 */
    wchar_t  notes[UP_NOTES_MAX];      /* 新版简介（管理后台填写） */
    wchar_t  url[1024];               /* 新版安装包下载地址（手动下载备用） */
    wchar_t  script[1024];            /* 升级脚本 upgrade.ps1 地址 */
} ft_update_info;

/* 比较版本号：形如 V1.1.0 / 1.1.0，逐段数字比较
 * 返回：>0 表示 a 更新，<0 表示 b 更新，0 相同 */
int up_compare_version(const wchar_t *a, const wchar_t *b);

/* 连接服务器检测新版本（同步、内部超时 5 秒）
 * json_url : update.json 完整地址（http/https）
 * local_ver: 本地版本号，如 V1.1.0
 * out      : 检测结果
 * 返回 0=检测成功（看 out->has_update），-1=网络错误，-2=JSON 解析错误 */
int up_check(const wchar_t *json_url, const wchar_t *local_ver, ft_update_info *out);

/* 启动升级：PowerShell IEX 下载并执行服务器升级脚本
 * （脚本负责下载安装包→关闭旧程序→启动安装器覆盖安装）
 * script_url: upgrade.ps1 完整地址
 * 返回 0=命令已成功启动，-1=失败 */
int up_run_upgrade(const wchar_t *script_url);

#ifdef __cplusplus
}
#endif

#endif /* FT_UPGRADE_H */
