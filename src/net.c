/*
 * 网络层实现 (Winsock)
 */
#include "net.h"
#include "log.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#define NET_MAGIC "SYNC"

/* ---------- 工具：精确收发 ---------- */
static int recv_exactly(SOCKET s, void *buf, int n) {
    char *p = (char *)buf;
    while (n > 0) {
        int r = recv(s, p, n, 0);
        if (r <= 0) return 0;
        p += r; n -= r;
    }
    return 1;
}

static int send_all(SOCKET s, const void *buf, int n) {
    const char *p = (const char *)buf;
    while (n > 0) {
        int r = send(s, p, n, 0);
        if (r <= 0) return 0;
        p += r; n -= r;
    }
    return 1;
}

/* wide -> utf8 */
static int wide_to_utf8(const wchar_t *w, char *out, int outlen) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, outlen, NULL, NULL);
    return n;
}
/* utf8 -> wide */
static int utf8_to_wide(const char *u, wchar_t *out, int outlen) {
    return MultiByteToWideChar(CP_UTF8, 0, u, -1, out, outlen);
}

/* 对 json 字符串做转义（in-place 到新缓冲） */
static void json_escape(const char *in, char *out, int outlen) {
    int o = 0;
    for (int i = 0; in[i] && o < outlen - 8; i++) {
        char c = in[i];
        if (c == '\\' || c == '"') { out[o++] = '\\'; out[o++] = c; }
        else if (c == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
        else if (c == '\r') { out[o++] = '\\'; out[o++] = 'r'; }
        else if (c == '\t') { out[o++] = '\\'; out[o++] = 't'; }
        else out[o++] = c;
    }
    out[o] = 0;
}

/* 发送一帧 */
static int send_frame(SOCKET s, const char *type, const char *path_utf8,
                      const void *data, unsigned long size, double mtime) {
    char header[1100];
    char path_esc[1024];
    json_escape(path_utf8 ? path_utf8 : "", path_esc, 1024);
    if (strcmp(type, "file") == 0) {
        _snprintf_s(header, sizeof(header), _TRUNCATE,
            "{\"type\":\"file\",\"path\":\"%s\",\"size\":%lu,\"mtime\":%.3f}",
            path_esc, size, mtime);
    } else if (strcmp(type, "delete") == 0) {
        _snprintf_s(header, sizeof(header), _TRUNCATE,
            "{\"type\":\"delete\",\"path\":\"%s\"}", path_esc);
    } else if (strcmp(type, "bye") == 0) {
        _snprintf_s(header, sizeof(header), _TRUNCATE, "{\"type\":\"bye\"}");
    } else {
        _snprintf_s(header, sizeof(header), _TRUNCATE,
            "{\"type\":\"%s\"}", type);
    }
    int hlen = (int)strlen(header);
    char prefix[8];
    memcpy(prefix, NET_MAGIC, 4);
    unsigned int hlen_be = htonl((unsigned int)hlen);
    memcpy(prefix + 4, &hlen_be, 4);

    if (!send_all(s, prefix, 8)) return 0;
    if (!send_all(s, header, hlen)) return 0;
    if (size > 0 && data) {
        if (!send_all(s, data, (int)size)) return 0;
    }
    return 1;
}

/* 读取一帧到 caller 提供的 header 缓冲与 data 缓冲(需 caller free) */
static int recv_frame(SOCKET s, char *header, int header_cap,
                      char **data_out, unsigned long *size_out) {
    char prefix[8];
    if (!recv_exactly(s, prefix, 8)) return 0;
    if (memcmp(prefix, NET_MAGIC, 4) != 0) return 0;
    unsigned int hlen_be;
    memcpy(&hlen_be, prefix + 4, 4);
    int hlen = ntohl(hlen_be);
    if (hlen <= 0 || hlen >= header_cap) return 0;
    if (!recv_exactly(s, header, hlen)) return 0;
    header[hlen] = 0;
    /* 简易取 size */
    *size_out = 0;
    const char *sp = strstr(header, "\"size\":");
    if (sp) *size_out = strtoul(sp + 7, NULL, 10);
    *data_out = NULL;
    if (*size_out > 0 && *size_out < 64 * 1024 * 1024) {
        *data_out = (char *)malloc(*size_out);
        if (!*data_out) return 0;
        if (!recv_exactly(s, *data_out, (int)*size_out)) {
            free(*data_out); *data_out = NULL; return 0;
        }
    }
    return 1;
}

/* 从 header 取 path 字段到 wide 缓冲 */
static int header_get_path(const char *header, wchar_t *out, int outlen) {
    const char *p = strstr(header, "\"path\":\"");
    if (!p) return 0;
    p += 8;
    char utf8[1024];
    int o = 0;
    while (*p && *p != '"' && o < 1023) {
        if (*p == '\\' && p[1]) { utf8[o++] = p[1]; p += 2; }
        else utf8[o++] = *p++;
    }
    utf8[o] = 0;
    utf8_to_wide(utf8, out, outlen);
    return 1;
}

/* 从 header 取 type */
static void header_get_type(const char *header, char *out, int outlen) {
    const char *p = strstr(header, "\"type\":\"");
    if (!p) { out[0] = 0; return; }
    p += 8;
    int o = 0;
    while (*p && *p != '"' && o < outlen - 1) out[o++] = *p++;
    out[o] = 0;
}

/* 从 header 取 mtime */
static double header_get_mtime(const char *header) {
    const char *p = strstr(header, "\"mtime\":");
    if (!p) return 0;
    return strtod(p + 8, NULL);
}

/* ===================== 接收端 ===================== */
typedef struct {
    int port;
    wchar_t dest_dir[MAX_PATH];
    SOCKET listen_sock;
    HANDLE thread;
    volatile LONG running;
    volatile LONG inited;
} Receiver;

static Receiver g_recv;

/* 全局一次性 Winsock 启动（线程安全） */
static volatile LONG g_wsa_started = 0;
static int ensure_wsa(void) {
    if (InterlockedCompareExchange(&g_wsa_started, 1, 0) == 0) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            InterlockedExchange(&g_wsa_started, 0);
            return 0;
        }
    }
    return 1;
}

static void ensure_dir_for_file(const wchar_t *full) {
    /* 创建父目录 */
    wchar_t tmp[MAX_PATH];
    wcscpy_s(tmp, MAX_PATH, full);
    wchar_t *slash = wcsrchr(tmp, L'\\');
    if (slash) {
        *slash = 0;
        CreateDirectoryW(tmp, NULL); /* 递归简化：仅一层，足够大部分场景 */
        /* 处理多层：逐级创建 */
        for (wchar_t *q = tmp + 3; *q; q++) {
            if (*q == L'\\') {
                *q = 0;
                CreateDirectoryW(tmp, NULL);
                *q = L'\\';
            }
        }
        CreateDirectoryW(tmp, NULL);
    }
}

static DWORD WINAPI recv_conn_thread(LPVOID arg) {
    SOCKET conn = (SOCKET)(ULONG_PTR)arg;
    char header[1200];
    char *data;
    unsigned long size;
    while (g_recv.running) {
        if (!recv_frame(conn, header, sizeof(header), &data, &size)) break;
        char type[16];
        header_get_type(header, type, sizeof(type));
        if (strcmp(type, "file") == 0) {
            wchar_t rel[MAX_PATH], full[MAX_PATH];
            if (header_get_path(header, rel, MAX_PATH)) {
                /* 防路径穿越 */
                if (wcsncmp(rel, L"..", 2) == 0 ||
                    (rel[0] && rel[1] == L':')) {
                    log_line(L"[错误] 非法路径被拒绝: %ls", rel);
                    free(data); continue;
                }
                _snwprintf_s(full, MAX_PATH, _TRUNCATE, L"%ls\\%ls",
                             g_recv.dest_dir, rel);
                ensure_dir_for_file(full);
                wchar_t tmp[MAX_PATH];
                _snwprintf_s(tmp, MAX_PATH, _TRUNCATE, L"%ls.syncpart", full);
                HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (h != INVALID_HANDLE_VALUE) {
                    DWORD wr = 0;
                    if (data && size) WriteFile(h, data, (DWORD)size, &wr, NULL);
                    CloseHandle(h);
                    MoveFileExW(tmp, full,
                                MOVEFILE_REPLACE_EXISTING |
                                MOVEFILE_COPY_ALLOWED);
                    double mt = header_get_mtime(header);
                    if (mt > 0) {
                        HANDLE hf = CreateFileW(full,
                            FILE_WRITE_ATTRIBUTES, 0, NULL,
                            OPEN_EXISTING, 0, NULL);
                        if (hf != INVALID_HANDLE_VALUE) {
                            FILETIME ft;
                            LONGLONG ll = (LONGLONG)(mt * 10000000.0) +
                                          116444736000000000LL;
                            ft.dwLowDateTime = (DWORD)ll;
                            ft.dwHighDateTime = (DWORD)(ll >> 32);
                            SetFileTime(hf, NULL, NULL, &ft);
                            CloseHandle(hf);
                        }
                    }
                    log_line(L"接收 ← %ls (%lu B)", rel, size);
                }
            }
        } else if (strcmp(type, "delete") == 0) {
            wchar_t rel[MAX_PATH], full[MAX_PATH];
            if (header_get_path(header, rel, MAX_PATH)) {
                _snwprintf_s(full, MAX_PATH, _TRUNCATE, L"%ls\\%ls",
                             g_recv.dest_dir, rel);
                DeleteFileW(full);
                log_line(L"删除 ← %ls", rel);
            }
        } else if (strcmp(type, "bye") == 0) {
            log_line(L"发送端断开");
            free(data); break;
        }
        free(data);
    }
    closesocket(conn);
    return 0;
}

static DWORD WINAPI recv_listen_thread(LPVOID arg) {
    (void)arg;
    log_line(L"接收端监听端口 %d ...", g_recv.port);
    while (g_recv.running) {
        SOCKET conn = accept(g_recv.listen_sock, NULL, NULL);
        if (conn == INVALID_SOCKET) break;
        log_line(L"发送端已连接");
        HANDLE h = CreateThread(NULL, 0, recv_conn_thread,
                                (LPVOID)(ULONG_PTR)conn, 0, NULL);
        if (h) CloseHandle(h);
    }
    log_line(L"接收端已停止");
    return 0;
}

int net_receiver_start(int port, const wchar_t *dest_dir) {
    if (InterlockedCompareExchange(&g_recv.inited, 1, 0) != 0) {
        /* 已经启动过一次，先把之前的关掉 */
    }
    g_recv.port = port;
    wcscpy_s(g_recv.dest_dir, MAX_PATH, dest_dir);
    if (!ensure_wsa()) {
        log_line(L"[错误] WSAStartup 失败");
        return 0;
    }
    g_recv.listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_recv.listen_sock == INVALID_SOCKET) {
        log_line(L"[错误] socket 创建失败");
        return 0;
    }
    BOOL reuse = TRUE;
    setsockopt(g_recv.listen_sock, SOL_SOCKET, SO_REUSEADDR,
               (char *)&reuse, sizeof(reuse));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((u_short)port);
    if (bind(g_recv.listen_sock, (struct sockaddr *)&addr, sizeof(addr)) ==
        SOCKET_ERROR) {
        log_line(L"[错误] 端口 %d 绑定失败", port);
        closesocket(g_recv.listen_sock);
        g_recv.listen_sock = INVALID_SOCKET;
        return 0;
    }
    if (listen(g_recv.listen_sock, 8) == SOCKET_ERROR) {
        log_line(L"[错误] listen 失败");
        closesocket(g_recv.listen_sock);
        g_recv.listen_sock = INVALID_SOCKET;
        return 0;
    }
    InterlockedExchange(&g_recv.running, 1);
    g_recv.thread = CreateThread(NULL, 0, recv_listen_thread, NULL, 0, NULL);
    return 1;
}

void net_receiver_stop(void) {
    InterlockedExchange(&g_recv.running, 0);
    if (g_recv.listen_sock != INVALID_SOCKET) {
        closesocket(g_recv.listen_sock);
        g_recv.listen_sock = INVALID_SOCKET;
    }
    if (g_recv.thread) {
        WaitForSingleObject(g_recv.thread, 2000);
        CloseHandle(g_recv.thread);
        g_recv.thread = NULL;
    }
    InterlockedExchange(&g_recv.inited, 0);
}

/* ===================== 发送端 ===================== */
typedef struct {
    SOCKET sock;
    CRITICAL_SECTION *plock;
    volatile LONG connected;
    volatile LONG lock_inited;
    char host[128];
    int port;
} Sender;

static Sender g_send;

/* 一次性初始化发送端 lock（线程安全） */
static void sender_ensure_lock(void) {
    if (g_send.plock) return;
    CRITICAL_SECTION *pcs = (CRITICAL_SECTION *)malloc(sizeof(CRITICAL_SECTION));
    if (!pcs) return;
    InitializeCriticalSection(pcs);
    if (InterlockedCompareExchangePointer((PVOID*)&g_send.plock, pcs, NULL) != NULL) {
        DeleteCriticalSection(pcs);
        free(pcs);
    }
}

#define SEND_LOCK()   EnterCriticalSection(g_send.plock)
#define SEND_UNLOCK() LeaveCriticalSection(g_send.plock)

int net_sender_connect(const char *host, int port) {
    sender_ensure_lock();
    SEND_LOCK();
    if (g_send.connected) { SEND_UNLOCK(); return 1; }
    if (!ensure_wsa()) { SEND_UNLOCK(); return 0; }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    g_send.sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_send.sock == INVALID_SOCKET) {
        SEND_UNLOCK();
        log_line(L"[错误] socket 创建失败");
        return 0;
    }
    /* 设置 4s 超时连接 */
    DWORD to = 4000;
    setsockopt(g_send.sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&to, sizeof(to));
    if (connect(g_send.sock, (struct sockaddr *)&addr, sizeof(addr)) ==
        SOCKET_ERROR) {
        log_line(L"[错误] 连接失败 %hs:%d", host, port);
        closesocket(g_send.sock);
        g_send.sock = INVALID_SOCKET;
        SEND_UNLOCK();
        return 0;
    }
    strncpy(g_send.host, host, sizeof(g_send.host) - 1);
    g_send.port = port;
    /* hello */
    send_frame(g_send.sock, "hello", "", NULL, 0, 0);
    InterlockedExchange(&g_send.connected, 1);
    SEND_UNLOCK();
    log_line(L"已连接到接收端 %hs:%d", host, port);
    return 1;
}

int net_sender_connected(void) {
    return g_send.connected;
}

void net_sender_disconnect(void) {
    sender_ensure_lock();
    SEND_LOCK();
    if (g_send.sock != INVALID_SOCKET) {
        send_frame(g_send.sock, "bye", "", NULL, 0, 0);
        closesocket(g_send.sock);
        g_send.sock = INVALID_SOCKET;
    }
    InterlockedExchange(&g_send.connected, 0);
    SEND_UNLOCK();
}

int net_sender_send_file(const wchar_t *relpath, const void *data,
                         unsigned long size, double mtime) {
    sender_ensure_lock();
    SEND_LOCK();
    if (g_send.sock == INVALID_SOCKET) {
        SEND_UNLOCK(); return 0;
    }
    char utf8[1024];
    wide_to_utf8(relpath, utf8, sizeof(utf8));
    int ok = send_frame(g_send.sock, "file", utf8, data, size, mtime);
    SEND_UNLOCK();
    return ok;
}

int net_sender_send_delete(const wchar_t *relpath) {
    sender_ensure_lock();
    SEND_LOCK();
    if (g_send.sock == INVALID_SOCKET) {
        SEND_UNLOCK(); return 0;
    }
    char utf8[1024];
    wide_to_utf8(relpath, utf8, sizeof(utf8));
    int ok = send_frame(g_send.sock, "delete", utf8, NULL, 0, 0);
    SEND_UNLOCK();
    return ok;
}
