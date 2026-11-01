/*
 * discover.c - 局域网设备发现（UDP 广播/单播）
 *   - 上线后每 2 秒在 udp_port 广播 "FT Q my_id my_name tcp_port"
 *   - 监听 udp_port，收到 "FT Q ..." 后给对方单播 "FT R my_id my_name tcp_port"
 *   - 维护设备列表，超过 8 秒未见则删除
 */
#include "discover.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <time.h>

static SOCKET g_sock = INVALID_SOCKET;
static HANDLE g_thread = NULL;
static volatile LONG g_running = 0;
static int g_udp_port = DISCOVER_PORT;
static int g_file_port = FILE_PORT;
static wchar_t g_my_name[MAX_NAME_LEN] = L"未命名设备";
static wchar_t g_my_id[MAX_ID_LEN] = L"";

static CRITICAL_SECTION g_cs;
static ft_device g_devs[MAX_DEVICES];
static int g_dev_count = 0;

static discover_event_cb_t g_cb = NULL;

static void gen_uuid(wchar_t *out, int out_chars) {
    /* 简单：用时间戳 + 随机数 + 主机名哈希，够唯一了 */
    wchar_t host[128]; DWORD sz = 128;
    GetComputerNameW(host, &sz);
    srand((unsigned)(time(NULL) ^ GetCurrentProcessId() ^ GetTickCount()));
    unsigned r1 = (unsigned)rand(), r2 = (unsigned)rand(), r3 = (unsigned)rand();
    _snwprintf_s(out, out_chars, _TRUNCATE, L"ft-%04X-%04X-%04X-%04X-%s",
        (unsigned)(time(NULL) & 0xFFFF), r1 & 0xFFFF, r2 & 0xFFFF, r3 & 0xFFFF, host);
    /* 截断到 MAX_ID_LEN-1 */
    out[MAX_ID_LEN-1] = 0;
}

static unsigned now_sec(void) {
    return (unsigned)(GetTickCount() / 1000);
}

static int sock_init(void) {
    WSADATA d;
    if (WSAStartup(MAKEWORD(2, 2), &d) != 0) { LOG_ERROR(L"WSAStartup 失败"); return -1; }
    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock == INVALID_SOCKET) { LOG_ERROR(L"socket 创建失败"); return -1; }
    /* 设置广播权限 */
    BOOL bcast = TRUE;
    setsockopt(g_sock, SOL_SOCKET, SO_BROADCAST, (const char*)&bcast, sizeof(bcast));
    /* 绑定端口（允许端口共享，方便两台在同一台机器测试） */
    BOOL reuse = TRUE;
    setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)g_udp_port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(g_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR(L"UDP bind 端口 %d 失败: %d", g_udp_port, WSAGetLastError());
        closesocket(g_sock); g_sock = INVALID_SOCKET;
        return -1;
    }
    /* 非阻塞式（加 500ms 超时） */
    int tm = 500;
    setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tm, sizeof(tm));
    return 0;
}

static void udp_send_to(const char *ip, int port, const char *buf, int len) {
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = inet_addr(ip);
    sendto(g_sock, buf, len, 0, (struct sockaddr*)&addr, sizeof(addr));
}

static void send_broadcast(void) {
    char pkt[1024];
    char name_utf8[256] = {0};
    WideCharToMultiByte(CP_UTF8, 0, g_my_name, -1, name_utf8, 256, NULL, NULL);
    char id_utf8[256] = {0};
    WideCharToMultiByte(CP_UTF8, 0, g_my_id, -1, id_utf8, 256, NULL, NULL);
    int n = _snprintf_s(pkt, 1024, _TRUNCATE, "FT Q %s %s %d", id_utf8, name_utf8, g_file_port);
    /* 典型广播：255.255.255.255 */
    udp_send_to("255.255.255.255", g_udp_port, pkt, n);
}

static void send_reply(const struct sockaddr_in *to) {
    char pkt[1024];
    char name_utf8[256] = {0};
    WideCharToMultiByte(CP_UTF8, 0, g_my_name, -1, name_utf8, 256, NULL, NULL);
    char id_utf8[256] = {0};
    WideCharToMultiByte(CP_UTF8, 0, g_my_id, -1, id_utf8, 256, NULL, NULL);
    int n = _snprintf_s(pkt, 1024, _TRUNCATE, "FT R %s %s %d", id_utf8, name_utf8, g_file_port);
    struct sockaddr_in addr = *to;
    sendto(g_sock, pkt, n, 0, (struct sockaddr*)&addr, sizeof(addr));
}

static void device_add_or_update(const char *id_utf8, const char *name_utf8,
                                 const char *ip, int port) {
    wchar_t id[MAX_ID_LEN], name[MAX_NAME_LEN];
    MultiByteToWideChar(CP_UTF8, 0, id_utf8, -1, id, MAX_ID_LEN);
    MultiByteToWideChar(CP_UTF8, 0, name_utf8, -1, name, MAX_NAME_LEN);
    /* 跳过自己 */
    if (!wcscmp(id, g_my_id)) return;

    EnterCriticalSection(&g_cs);
    int idx = -1;
    for (int i = 0; i < g_dev_count; i++) {
        if (!wcscmp(g_devs[i].id, id)) { idx = i; break; }
    }
    unsigned t = now_sec();
    int changed = 0;
    if (idx >= 0) {
        if (wcscmp(g_devs[idx].name, name) || strcmp(g_devs[idx].ip, ip) ||
            g_devs[idx].port != port) {
            changed = 1;
        }
        wcscpy_s(g_devs[idx].name, MAX_NAME_LEN, name);
        strcpy_s(g_devs[idx].ip, sizeof(g_devs[idx].ip), ip);
        g_devs[idx].port = port;
        g_devs[idx].last_seen = t;
    } else {
        if (g_dev_count < MAX_DEVICES) {
            idx = g_dev_count++;
            wcscpy_s(g_devs[idx].name, MAX_NAME_LEN, name);
            wcscpy_s(g_devs[idx].id, MAX_ID_LEN, id);
            strcpy_s(g_devs[idx].ip, sizeof(g_devs[idx].ip), ip);
            g_devs[idx].port = port;
            g_devs[idx].last_seen = t;
            changed = 1;
        }
    }
    LeaveCriticalSection(&g_cs);
    if (changed && g_cb) g_cb(0, L"update");
}

static void device_cleanup_expired(void) {
    EnterCriticalSection(&g_cs);
    unsigned t = now_sec();
    int w = 0, removed = 0;
    for (int i = 0; i < g_dev_count; i++) {
        if ((int)(t - g_devs[i].last_seen) < 8) {
            if (w != i) g_devs[w] = g_devs[i];
            w++;
        } else removed++;
    }
    g_dev_count = w;
    LeaveCriticalSection(&g_cs);
    if (removed && g_cb) g_cb(0, L"remove");
}

static void parse_packet(const char *buf, int len, const struct sockaddr_in *from) {
    if (len < 4) return;
    if (memcmp(buf, "FT ", 3)) return;
    char type = buf[3];
    const char *p = buf + 4;
    char id_a[256] = {0}, name_a[256] = {0};
    int port = 0;
    int n = 0;
    if (sscanf_s(p, "%255s %255s %d%n", id_a, (unsigned)_countof(id_a),
                 name_a, (unsigned)_countof(name_a), &port, &n) < 2) return;
    if (port <= 0 || port > 65535) port = FILE_PORT;
    char ip[64];
    strcpy_s(ip, sizeof(ip), inet_ntoa(from->sin_addr));

    if (type == 'Q') {
        /* 别人的查询 -> 回应，同时把别人加入列表 */
        send_reply(from);
        device_add_or_update(id_a, name_a, ip, port);
    } else if (type == 'R') {
        /* 别人的回应 -> 加入列表 */
        device_add_or_update(id_a, name_a, ip, port);
    }
}

static unsigned __stdcall discover_loop(void *unused) {
    (void)unused;
    unsigned last_bcast = 0, last_cleanup = 0;
    while (InterlockedCompareExchange(&g_running, 0, 0)) {
        unsigned t = now_sec();
        if (t - last_bcast >= 2) { last_bcast = t; send_broadcast(); }
        if (t - last_cleanup >= 3) { last_cleanup = t; device_cleanup_expired(); }

        struct sockaddr_in from; int fromlen = sizeof(from);
        char buf[2048];
        int r = recvfrom(g_sock, buf, sizeof(buf)-1, 0, (struct sockaddr*)&from, &fromlen);
        if (r > 0) { buf[r] = 0; parse_packet(buf, r, &from); }
    }
    return 0;
}

/* ====== API ====== */

int discover_init(const wchar_t *my_name, int file_tcp_port) {
    InitializeCriticalSection(&g_cs);
    g_file_port = file_tcp_port > 0 ? file_tcp_port : FILE_PORT;
    if (my_name && my_name[0]) wcsncpy_s(g_my_name, MAX_NAME_LEN, my_name, _TRUNCATE);
    gen_uuid(g_my_id, MAX_ID_LEN);

    if (sock_init() < 0) { DeleteCriticalSection(&g_cs); return -1; }
    InterlockedExchange(&g_running, 1);
    g_thread = (HANDLE)_beginthreadex(NULL, 0, discover_loop, NULL, 0, NULL);
    LOG_INFO(L"设备发现已启动: id=%s name=%s UDP=%d TCP=%d",
             g_my_id, g_my_name, g_udp_port, g_file_port);
    discover_refresh();
    return 0;
}

void discover_set_callback(discover_event_cb_t cb) { g_cb = cb; }

void discover_refresh(void) {
    if (g_sock != INVALID_SOCKET) send_broadcast();
}

int discover_get_devices(ft_device out[], int max_count) {
    EnterCriticalSection(&g_cs);
    int n = g_dev_count < max_count ? g_dev_count : max_count;
    for (int i = 0; i < n; i++) out[i] = g_devs[i];
    LeaveCriticalSection(&g_cs);
    return n;
}

void discover_shutdown(void) {
    InterlockedExchange(&g_running, 0);
    if (g_sock != INVALID_SOCKET) { closesocket(g_sock); g_sock = INVALID_SOCKET; }
    if (g_thread) {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread); g_thread = NULL;
    }
    DeleteCriticalSection(&g_cs);
    WSACleanup();
    LOG_INFO(L"设备发现已停止");
}

const wchar_t *discover_my_id(void)   { return g_my_id; }
const wchar_t *discover_my_name(void) { return g_my_name; }
