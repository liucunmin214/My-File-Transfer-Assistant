/*
 * upgrade.c - 软件升级实现（WinHTTP 检测 + PowerShell IEX 升级）
 */
#include "upgrade.h"
#include "json.h"
#include "log.h"

#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string.h>

/* ---------- 版本比较 ---------- */

/* 解析 "V1.2.3" / "1.2.3" → 数字段（最多 4 段），返回段数 */
static int parse_ver(const wchar_t *ver, int nums[4]) {
    int n = 0;
    while (ver && *ver && n < 4) {
        if (*ver == L'V' || *ver == L'v' || *ver == L'.') { ver++; continue; }
        int v = 0, digits = 0;
        while (*ver >= L'0' && *ver <= L'9') {
            v = v * 10 + (*ver - L'0');
            ver++; digits++;
        }
        if (!digits) break;
        nums[n++] = v;
    }
    return n;
}

int up_compare_version(const wchar_t *a, const wchar_t *b) {
    int na[4] = {0,0,0,0}, nb[4] = {0,0,0,0};
    int ca = parse_ver(a, na), cb = parse_ver(b, nb);
    int n = ca > cb ? ca : cb;
    if (n < 1) n = 1;
    for (int i = 0; i < 4; i++) {
        if (na[i] != nb[i]) return na[i] > nb[i] ? 1 : -1;
    }
    return 0;
}

/* ---------- WinHTTP GET ---------- */

/* 通用 HTTP GET：拉取文本内容（UTF-8 → UTF-16），max_out_chars 为容量
 * 返回 0 成功；-1 URL/网络错误 */
static int http_get_text(const wchar_t *url, wchar_t *out, int max_out_chars) {
    URL_COMPONENTS uc;
    ZeroMemory(&uc, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256], path[1024];
    uc.lpszHostName = host;      uc.dwHostNameLength = 256;
    uc.lpszUrlPath  = path;      uc.dwUrlPathLength  = 1024;
    uc.dwSchemeLength = (DWORD)-1;
    uc.nPort = 0;
    if (!WinHttpCrackUrl(url, (DWORD)wcslen(url), 0, &uc)) return -1;

    BOOL tls = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    HINTERNET session = WinHttpOpen(L"My File Transfer Assistant/1.1",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return -1;
    WinHttpSetTimeouts(session, 0, 5000, 5000, 8000);  /* 解析/连接/发送/接收 */

    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    HINTERNET req = NULL;
    if (!conn) { WinHttpCloseHandle(session); return -1; }
    req = WinHttpOpenRequest(conn, L"GET", path,
                             NULL, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                             tls ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return -1; }

    int ok = 0;
    if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, NULL)) {
        /* 读取全部响应体（上限 256KB，update.json/ps1 远小于此） */
        static char buf[262144];
        DWORD total = 0;
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
            if (total + avail > sizeof(buf) - 1) avail = sizeof(buf) - 1 - total;
            DWORD got = 0;
            if (!WinHttpReadData(req, buf + total, avail, &got) || got == 0) break;
            total += got;
        }
        buf[total] = 0;
        if (total > 0) {
            int w = MultiByteToWideChar(CP_UTF8, 0, buf, total, out, max_out_chars - 1);
            if (w > 0) { out[w] = 0; ok = 1; }
        }
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return ok ? 0 : -1;
}

/* ---------- 检测 ---------- */

int up_check(const wchar_t *json_url, const wchar_t *local_ver, ft_update_info *out) {
    ZeroMemory(out, sizeof(*out));
    if (!json_url || !json_url[0] || !local_ver) return -1;

    wchar_t text[65536];
    if (http_get_text(json_url, text, 65536) != 0) {
        LOG_INFO(L"升级检测: 无法连接服务器 %s", json_url);
        return -1;
    }
    json_value *root = json_parse(text);
    if (!root || root->type != JSON_OBJECT) {
        if (root) json_free(root);
        return -2;
    }
    const wchar_t *ver = json_object_get_str(root, L"version", NULL);
    if (!ver || !ver[0]) { json_free(root); return -2; }

    wcsncpy_s(out->version, 32, ver, _TRUNCATE);
    const wchar_t *s = json_object_get_str(root, L"notes", NULL);
    if (s) wcsncpy_s(out->notes, UP_NOTES_MAX, s, _TRUNCATE);
    s = json_object_get_str(root, L"url", NULL);
    if (s) wcsncpy_s(out->url, 1024, s, _TRUNCATE);
    s = json_object_get_str(root, L"script", NULL);
    if (s) wcsncpy_s(out->script, 1024, s, _TRUNCATE);
    json_free(root);

    int cmp = up_compare_version(out->version, local_ver);
    out->has_update = (cmp > 0) ? 1 : 0;
    out->rollback   = (cmp < 0) ? 1 : 0;
    LOG_INFO(L"升级检测: 服务器=%s 本地=%s %s", out->version, local_ver,
             out->has_update ? L"→ 有新版本" :
             (out->rollback ? L"→ 服务器已回退版本（可回滚）" : L"→ 已是最新"));
    return 0;
}

/* ---------- 升级执行（PowerShell IEX） ---------- */

int up_run_upgrade(const wchar_t *script_url) {
    if (!script_url || !script_url[0]) return -1;

    /* powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden
     *   -Command "iex (New-Object Net.WebClient).DownloadString('脚本地址')"
     * IEX 下载服务器升级脚本并立即执行：
     * 脚本内完成 下载新版安装包 → 关闭运行中的旧程序 → 启动安装器覆盖安装 */
    wchar_t cmd[2048];
    _snwprintf_s(cmd, 2048, _TRUNCATE,
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden "
        L"-Command \"iex (New-Object Net.WebClient).DownloadString('%s')\"",
        script_url);

    STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        LOG_ERROR(L"升级启动失败: CreateProcess 错误 %u", GetLastError());
        return -1;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    LOG_INFO(L"升级已启动: %s", script_url);
    return 0;
}
