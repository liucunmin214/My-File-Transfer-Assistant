/*
 * config.c - 配置加载与保存
 */
#include "config.h"
#include "json.h"
#include "log.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>

void config_get_dir(wchar_t *out, int out_chars) {
    out[0] = 0;
    wchar_t roaming[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, roaming) != S_OK) {
        /* 回退到 %TEMP% */
        GetTempPathW(MAX_PATH, roaming);
    }
    _snwprintf_s(out, out_chars, _TRUNCATE, L"%s\\My File Transfer Assistant", roaming);
    CreateDirectoryW(out, NULL);
}

void config_default_save_dir(wchar_t *out, int out_chars) {
    wchar_t docs[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docs) == S_OK) {
        _snwprintf_s(out, out_chars, _TRUNCATE, L"%s\\文件传输接收", docs);
    } else {
        _snwprintf_s(out, out_chars, _TRUNCATE, L"C:\\文件传输接收");
    }
    CreateDirectoryW(out, NULL);
}

void config_default_nickname(wchar_t *out, int out_chars) {
    DWORD sz = (DWORD)out_chars;
    if (!GetComputerNameW(out, &sz)) {
        _snwprintf_s(out, out_chars, _TRUNCATE, L"我的电脑");
    }
}

static void fill_defaults(ft_config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    config_default_save_dir(cfg->save_dir, 1024);
    config_default_nickname(cfg->nickname, 64);
    cfg->tcp_port = 51831;
    cfg->udp_port = 51830;
    cfg->autostart = 0;
    cfg->auto_accept = 1;
    cfg->show_notification = 1;
    cfg->autodiscover = 1;
    cfg->dark_mode = 0;
    cfg->theme_id = 0;
    /* 高级设置默认值 */
    cfg->minimize_to_tray = 0;
    cfg->confirm_on_exit = 1;
    cfg->sound_notify = 1;
    cfg->show_speed = 1;
    cfg->auto_cleanup_sent = 0;
    cfg->acrylic_effect = 1;
    cfg->window_opacity = 100;   /* 默认完全不透明：整窗半透明会与亚克力叠加导致发白 */
    cfg->blur_amount = 70;
    cfg->last_dest_ip[0] = 0;
    cfg->last_dest_port = 51831;
    /* 软件升级默认值：自动检查开启；检测地址留空，部署自己的更新服务后在设置中填写 */
    cfg->auto_update = 1;
    cfg->update_url[0] = 0;
}

int config_load(ft_config *cfg) {
    fill_defaults(cfg);
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    config_get_dir(dir, MAX_PATH);
    _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\config.json", dir);

    json_value *root = json_parse_file(path);
    if (!root) {
        /* 改名迁移：软件由「文件传输助手」更名为 My File Transfer Assistant，
         * 若旧目录存在配置则直接沿用，避免用户设置丢失 */
        wchar_t roaming[MAX_PATH];
        if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, roaming) == S_OK) {
            wchar_t oldp[MAX_PATH];
            _snwprintf_s(oldp, MAX_PATH, _TRUNCATE,
                         L"%s\\文件传输助手\\config.json", roaming);
            root = json_parse_file(oldp);
            if (root) LOG_INFO(L"已从旧版目录迁移配置: %s", oldp);
        }
    }
    if (!root) {
        LOG_INFO(L"配置文件不存在，写入默认值: %s", path);
        config_save(cfg);
        return 0;
    }
    const wchar_t *sv = json_object_get_str(root, L"save_dir", NULL);
    if (sv && sv[0]) wcsncpy_s(cfg->save_dir, 1024, sv, _TRUNCATE);
    sv = json_object_get_str(root, L"nickname", NULL);
    if (sv && sv[0]) wcsncpy_s(cfg->nickname, 64, sv, _TRUNCATE);
    cfg->tcp_port          = json_object_get_int(root, L"tcp_port", cfg->tcp_port);
    cfg->udp_port          = json_object_get_int(root, L"udp_port", cfg->udp_port);
    cfg->autostart         = json_object_get_bool(root, L"autostart", cfg->autostart);
    cfg->auto_accept       = json_object_get_bool(root, L"auto_accept", cfg->auto_accept);
    cfg->show_notification = json_object_get_bool(root, L"show_notification", cfg->show_notification);
    cfg->autodiscover      = json_object_get_bool(root, L"autodiscover", cfg->autodiscover);
    cfg->dark_mode         = json_object_get_bool(root, L"dark_mode", cfg->dark_mode);
    cfg->theme_id         = json_object_get_int(root, L"theme_id", cfg->theme_id);
    cfg->minimize_to_tray  = json_object_get_bool(root, L"minimize_to_tray", cfg->minimize_to_tray);
    cfg->confirm_on_exit   = json_object_get_bool(root, L"confirm_on_exit", cfg->confirm_on_exit);
    cfg->sound_notify      = json_object_get_bool(root, L"sound_notify", cfg->sound_notify);
    cfg->show_speed        = json_object_get_bool(root, L"show_speed", cfg->show_speed);
    cfg->auto_cleanup_sent = json_object_get_bool(root, L"auto_cleanup_sent", cfg->auto_cleanup_sent);
    cfg->acrylic_effect   = json_object_get_bool(root, L"acrylic_effect", cfg->acrylic_effect);
    cfg->window_opacity    = json_object_get_int(root, L"window_opacity", cfg->window_opacity);
    cfg->blur_amount       = json_object_get_int(root, L"blur_amount", cfg->blur_amount);
    sv = json_object_get_str(root, L"last_dest_ip", NULL);
    if (sv) wcsncpy_s(cfg->last_dest_ip, 64, sv, _TRUNCATE);
    cfg->last_dest_port    = json_object_get_int(root, L"last_dest_port", cfg->last_dest_port);
    cfg->auto_update       = json_object_get_bool(root, L"auto_update", cfg->auto_update);
    sv = json_object_get_str(root, L"update_url", NULL);
    if (sv && sv[0]) wcsncpy_s(cfg->update_url, 1024, sv, _TRUNCATE);
    json_free(root);
    LOG_INFO(L"已加载配置: nickname=%s port=%d save_dir=%s",
             cfg->nickname, cfg->tcp_port, cfg->save_dir);
    return 0;
}

int config_save(const ft_config *cfg) {
    json_value *root = json_new_object();
    json_object_set(root, L"save_dir",          json_new_string(cfg->save_dir));
    json_object_set(root, L"nickname",          json_new_string(cfg->nickname));
    json_object_set(root, L"tcp_port",          json_new_number(cfg->tcp_port));
    json_object_set(root, L"udp_port",          json_new_number(cfg->udp_port));
    json_object_set(root, L"autostart",         json_new_bool(cfg->autostart));
    json_object_set(root, L"auto_accept",       json_new_bool(cfg->auto_accept));
    json_object_set(root, L"show_notification", json_new_bool(cfg->show_notification));
    json_object_set(root, L"autodiscover",      json_new_bool(cfg->autodiscover));
    json_object_set(root, L"dark_mode",         json_new_bool(cfg->dark_mode));
    json_object_set(root, L"theme_id",         json_new_number(cfg->theme_id));
    json_object_set(root, L"minimize_to_tray",  json_new_bool(cfg->minimize_to_tray));
    json_object_set(root, L"confirm_on_exit",   json_new_bool(cfg->confirm_on_exit));
    json_object_set(root, L"sound_notify",      json_new_bool(cfg->sound_notify));
    json_object_set(root, L"show_speed",        json_new_bool(cfg->show_speed));
    json_object_set(root, L"auto_cleanup_sent", json_new_bool(cfg->auto_cleanup_sent));
    json_object_set(root, L"acrylic_effect",   json_new_bool(cfg->acrylic_effect));
    json_object_set(root, L"window_opacity",    json_new_number(cfg->window_opacity));
    json_object_set(root, L"blur_amount",       json_new_number(cfg->blur_amount));
    json_object_set(root, L"last_dest_ip",      json_new_string(cfg->last_dest_ip));
    json_object_set(root, L"last_dest_port",    json_new_number(cfg->last_dest_port));
    json_object_set(root, L"auto_update",       json_new_bool(cfg->auto_update));
    json_object_set(root, L"update_url",        json_new_string(cfg->update_url));

    wchar_t dir[MAX_PATH], path[MAX_PATH];
    config_get_dir(dir, MAX_PATH);
    _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\config.json", dir);
    int r = json_write_file(path, root);
    json_free(root);
    if (r == 0) LOG_INFO(L"配置已保存: %s", path);
    else        LOG_ERROR(L"配置保存失败: %s", path);
    return r;
}
