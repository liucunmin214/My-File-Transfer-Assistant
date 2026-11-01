/*
 * registry.c - 注册表操作（右键菜单、开机自启、安装路径）
 */
#include "registry.h"
#include "log.h"
#include <windows.h>
#include <shlobj.h>

static LONG set_str(HKEY root, const wchar_t *sub, const wchar_t *val, const wchar_t *data) {
    HKEY k; LONG r;
    r = RegCreateKeyExW(root, sub, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL);
    if (r != ERROR_SUCCESS) return r;
    r = RegSetValueExW(k, val, 0, REG_SZ, (const BYTE*)data,
        (DWORD)((wcslen(data) + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return r;
}

static LONG del_tree(HKEY root, const wchar_t *sub) {
    return RegDeleteTreeW(root, sub); /* WinVista+ */
}

/* ---- 右键菜单（文件/文件夹/目录背景，全部写入 HKCU 即可，无需管理员）
 * Win11 一级菜单适配要点（无 MSIX/COM 场景）：
 *   - 设置 NoWorkingDirectory 为空（避免在网络路径崩 / 让 Shell 优先在现代菜单列项）
 *   - 同时设置 MUIVerb + verb key 默认值
 *   - Position=Top + SeparatorBefore，放在最前分隔区
 *   - 注册到 AllFilesystemObjects / * / Directory / Directory\Background / Drive 五个入口
 *   - 命令中用 %V（Vista+）替代 %1，兼容更多场景；目录背景传 %V=当前文件夹路径 ---- */

static int reg_shell_one(const wchar_t *root_path, const wchar_t *exe, const wchar_t *icon, int is_bg) {
    wchar_t sub[512], cmd[2048];

    /* HKEY_CURRENT_USER\Software\Classes\<type>\shell\FTMenu */
    _snwprintf_s(sub, 512, _TRUNCATE, L"Software\\Classes\\%s\\shell\\%s", root_path, FT_SHELL_MENU_VERB);
    /* 默认值 + MUIVerb 同时设置，Win11 两边都有可能读 */
    if (set_str(HKEY_CURRENT_USER, sub, NULL, FT_SHELL_MENU_NAME) != ERROR_SUCCESS) return -1;
    if (set_str(HKEY_CURRENT_USER, sub, L"MUIVerb", FT_SHELL_MENU_NAME) != ERROR_SUCCESS) return -1;
    if (icon && icon[0]) set_str(HKEY_CURRENT_USER, sub, L"Icon", icon);
    /* 放在顶部 */
    set_str(HKEY_CURRENT_USER, sub, L"Position", L"Top");
    /* 分隔线 */
    set_str(HKEY_CURRENT_USER, sub, L"SeparatorBefore", L"");
    /* Win11 关键：必须设置 NoWorkingDirectory 为空串，Shell 才敢在网络驱动器 / 现代菜单里挂这个 verb */
    set_str(HKEY_CURRENT_USER, sub, L"NoWorkingDirectory", L"");
    /* 让这个 verb 既出现在选中单/多文件，也允许目录背景场景 */
    set_str(HKEY_CURRENT_USER, sub, L"NeverDefault", L"");
    /* MultiSelectModel=Player 表示支持多文件同时传入（Explorer 会把命令合并，我们的 argv 需要支持；
       这里先设置避免被 Shell 标成"只支持单文件"而在多选时隐藏） */
    {
        HKEY k;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, sub, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
            DWORD ms = 1; /* Player = 1 */
            RegSetValueExW(k, L"MultiSelectModel", 0, REG_DWORD, (const BYTE*)&ms, sizeof(ms));
            RegCloseKey(k);
        }
    }

    /* Command — 目录背景用 %V（当前打开文件夹），其他场景 %V 也等同于 %1 */
    _snwprintf_s(sub, 512, _TRUNCATE, L"Software\\Classes\\%s\\shell\\%s\\command", root_path, FT_SHELL_MENU_VERB);
    if (is_bg) {
        _snwprintf_s(cmd, 2048, _TRUNCATE, L"\"%s\" --send \"%%V\"", exe);
    } else {
        _snwprintf_s(cmd, 2048, _TRUNCATE, L"\"%s\" --send \"%%V\"", exe);
    }
    if (set_str(HKEY_CURRENT_USER, sub, NULL, cmd) != ERROR_SUCCESS) return -2;
    return 0;
}

int reg_register_shell_menu(const wchar_t *exe_path, const wchar_t *icon_path) {
    int ok = 0;
    LOG_INFO(L"注册右键菜单: 主程序=%s", exe_path);
    /* 清理旧版（文件传输助手）遗留的 verb 键，避免菜单出现失效重复项 */
    {
        static const wchar_t *legacy_roots[] = {
            L"AllFilesystemObjects", L"*", L"Directory",
            L"Directory\\Background", L"Drive"
        };
        for (int i = 0; i < 5; i++) {
            wchar_t sub[512];
            _snwprintf_s(sub, 512, _TRUNCATE,
                L"Software\\Classes\\%s\\shell\\filetransfer.send", legacy_roots[i]);
            del_tree(HKEY_CURRENT_USER, sub);
        }
    }
    /* 图标仍用主程序图标 */
    const wchar_t *icon = icon_path ? icon_path : exe_path;
    /* 文件 & 文件夹统一入口（Win11 现代菜单会扫这个） */
    ok += reg_shell_one(L"AllFilesystemObjects", exe_path, icon, 0);
    /* 文件 */
    ok += reg_shell_one(L"*", exe_path, icon, 0);
    /* 文件夹 */
    ok += reg_shell_one(L"Directory", exe_path, icon, 0);
    /* 目录背景（文件夹空白处右键） */
    ok += reg_shell_one(L"Directory\\Background", exe_path, icon, 1);
    /* 驱动器 */
    ok += reg_shell_one(L"Drive", exe_path, icon, 0);

    /* === Win11 一级菜单适配 ===
     * Win11 默认使用现代菜单，传统 verb 只出现在"显示更多选项"二级菜单。
     * 通过注册 CLSID {86ca1aa0-...} 将 InprocServer32 设为空串，
     * 系统将回退到经典（Win10）菜单 —— 我们的 verb 直接显示在一级菜单。
     * 这是业界通用做法（7-Zip / Notepad++ 等也用此法）。 */
    set_str(HKEY_CURRENT_USER,
            L"Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}\\InprocServer32",
            NULL, L"");

    /* 通知 shell 刷新 */
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    LOG_INFO(L"右键菜单注册完成（含 Win11 经典菜单适配）(%d)", ok);
    return 0;
}

static int unreg_shell_one(const wchar_t *root_path) {
    wchar_t sub[512];
    _snwprintf_s(sub, 512, _TRUNCATE, L"Software\\Classes\\%s\\shell\\%s", root_path, FT_SHELL_MENU_VERB);
    del_tree(HKEY_CURRENT_USER, sub);
    return 0;
}

int reg_unregister_shell_menu(void) {
    unreg_shell_one(L"AllFilesystemObjects");
    unreg_shell_one(L"*");
    unreg_shell_one(L"Directory");
    unreg_shell_one(L"Directory\\Background");
    unreg_shell_one(L"Drive");
    /* 恢复 Win11 现代菜单 */
    del_tree(HKEY_CURRENT_USER,
             L"Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    LOG_INFO(L"右键菜单已注销（含恢复 Win11 现代菜单）");
    return 0;
}

int reg_is_shell_menu_registered(void) {
    wchar_t sub[512];
    HKEY k;
    _snwprintf_s(sub, 512, _TRUNCATE, L"Software\\Classes\\*\\shell\\%s", FT_SHELL_MENU_VERB);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, sub, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        RegCloseKey(k);
        return 1;
    }
    return 0;
}

/* ---- 开机自启 ---- */
int reg_register_autostart(const wchar_t *exe_path) {
    wchar_t cmd[2048];
    _snwprintf_s(cmd, 2048, _TRUNCATE, L"\"%s\" --tray", exe_path);
    /* 清理旧版遗留的自启值（FileTransferAssistant），避免开机重复启动 */
    {
        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, FT_REG_AUTOSTART_KEY, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
            RegDeleteValueW(k, L"FileTransferAssistant");
            RegCloseKey(k);
        }
    }
    LONG r = set_str(HKEY_CURRENT_USER, FT_REG_AUTOSTART_KEY, FT_REG_AUTOSTART_VAL, cmd);
    if (r == ERROR_SUCCESS) LOG_INFO(L"开机自启已注册");
    else LOG_ERROR(L"开机自启注册失败: %d", (int)r);
    return (r == ERROR_SUCCESS) ? 0 : -1;
}

int reg_unregister_autostart(void) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, FT_REG_AUTOSTART_KEY, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
        RegDeleteValueW(k, FT_REG_AUTOSTART_VAL);
        RegCloseKey(k);
    }
    LOG_INFO(L"开机自启已注销");
    return 0;
}

/* ---- 安装路径 ---- */
int reg_write_install_path(const wchar_t *install_dir) {
    return (set_str(HKEY_CURRENT_USER, FT_REG_ROOT, L"InstallPath", install_dir) == ERROR_SUCCESS) ? 0 : -1;
}

int reg_read_install_path(wchar_t *out, int out_chars) {
    out[0] = 0;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, FT_REG_ROOT, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return -1;
    DWORD sz = (DWORD)(out_chars * sizeof(wchar_t));
    DWORD type = 0;
    LONG r = RegQueryValueExW(k, L"InstallPath", 0, &type, (LPBYTE)out, &sz);
    RegCloseKey(k);
    return (r == ERROR_SUCCESS && type == REG_SZ) ? 0 : -1;
}
