/*
 * registry.h - 注册表操作模块
 *   1. 右键文件/文件夹添加菜单项「发送到附近设备」
 *   2. 记录安装目录、开机自启
 *   3. 卸载时清理所有写入
 */
#ifndef FT_REGISTRY_H
#define FT_REGISTRY_H

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 注册文件右键菜单（文件 + 文件夹 + 目录背景）。
 * exe_path 是 MyFileTransferAssistant.exe 的完整路径（含文件名）。
 * icon_path 可选，传 NULL 则用 exe 自身图标
 */
int reg_register_shell_menu(const wchar_t *exe_path, const wchar_t *icon_path);

/* 注销右键菜单（卸载时调用） */
int reg_unregister_shell_menu(void);

/* 检测右键菜单是否已注册 */
int reg_is_shell_menu_registered(void);

/* 注册开机自启（当前用户） */
int reg_register_autostart(const wchar_t *exe_path);

/* 注销开机自启 */
int reg_unregister_autostart(void);

/* 写入安装路径（卸载时 Inno Setup 也会写，这里只是 DLL 导出） */
int reg_write_install_path(const wchar_t *install_dir);

/* 读取安装路径（未设置返回 NULL 字符串为空） */
int reg_read_install_path(wchar_t *out, int out_chars);

/* 应用注册表根: HKEY_CURRENT_USER\Software\My File Transfer Assistant */
#define FT_REG_ROOT L"Software\\My File Transfer Assistant"
#define FT_REG_AUTOSTART_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define FT_REG_AUTOSTART_VAL L"MyFileTransferAssistant"
#define FT_SHELL_MENU_NAME   L"发送到附近设备..."
#define FT_SHELL_MENU_VERB   L"myfiletransferassistant.send"

#ifdef __cplusplus
}
#endif

#endif /* FT_REGISTRY_H */
