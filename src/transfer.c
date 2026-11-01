/*
 * transfer.c - 文件传输核心（TCP 发送端 + 接收端）
 *
 * 协议（大端）：
 *   每个消息 = 4字节总长度(LEN) + 1字节类型(TYPE) + LEN-1字节载荷
 *     TYPE=1  HELLO   客户端 -> 服务端：发送端介绍（4字节任务数N + N个文件信息）
 *             文件信息 = 8字节大小 + 4字节rel_len + rel_len(UTF16-LE)相对路径 + 4字节full_len + full_len(UTF16-LE)仅发送端本地用full_path字段（实际未用此路径）
 *             为了简化：8字节size + 4字节nlen + n字节UTF-8 相对名
 *     TYPE=2  DATA    数据块：8字节offset + 4字节file_idx + 4字节chunk_len + chunk_len字节内容
 *     TYPE=3  PROGRESS 双向进度：4字节file_idx + 8字节offset + 8字节total
 *     TYPE=4  OK_DONE  服务端 -> 客户端：4字节file_idx + 8字节final_size
 *     TYPE=9  ERROR    错误消息：UTF-8字符串
 *
 * 简化版：发端依次串头发每个文件（HELLO-> 逐文件发送DATA），收端逐个接收保存。
 */
#include "transfer.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <shlwapi.h>
#include <shlobj.h>

#define MSG_HELLO   1
#define MSG_DATA    2
#define MSG_PROGRESS 3
#define MSG_OK      4
#define MSG_ERROR   9

/* ===== 工具：大端读写 ===== */
static void put_u32(unsigned char *p, unsigned v) {
    p[0]=(unsigned char)(v>>24); p[1]=(unsigned char)(v>>16);
    p[2]=(unsigned char)(v>>8);  p[3]=(unsigned char)(v);
}
static unsigned get_u32(const unsigned char *p) {
    return ((unsigned)p[0]<<24)|((unsigned)p[1]<<16)|((unsigned)p[2]<<8)|p[3];
}
static void put_u64(unsigned char *p, uint64_t v) {
    for (int i=7;i>=0;i--) p[7-i]=(unsigned char)(v>>(i*8));
}
static uint64_t get_u64(const unsigned char *p) {
    uint64_t r=0; for (int i=0;i<8;i++) r=(r<<8)|p[i]; return r;
}

static int recv_all(SOCKET s, char *buf, int len) {
    int got = 0;
    while (got < len) {
        int r = recv(s, buf + got, len - got, 0);
        if (r <= 0) return -1;
        got += r;
    }
    return 0;
}
static int send_all(SOCKET s, const char *buf, int len) {
    int sent = 0;
    while (sent < len) {
        int r = send(s, buf + sent, len - sent, 0);
        if (r <= 0) return -1;
        sent += r;
    }
    return 0;
}

/* ===== TCP 性能优化：禁用 Nagle、增大收发缓冲 ===== */
static void tcp_optimize(SOCKET s) {
    BOOL nodelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
    int buf_sz = 4 * 1024 * 1024;  /* 4MB 收发缓冲 */
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char*)&buf_sz, sizeof(buf_sz));
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&buf_sz, sizeof(buf_sz));
}

/* ===== 发送端 ===== */
struct send_group {
    int64_t id;
    ft_send_item *items;
    int count;
    char dest_ip[64];
    int dest_port;
    volatile LONG cancel;
    int status; /* 0 running, 1 done, 2 error */
    struct send_group *next;
};
static HANDLE g_send_mutex = NULL;
static struct send_group *g_groups = NULL;
static volatile int64_t g_next_gid = 1;
static transfer_event_cb_t g_send_cb = NULL;

static void fire_send_evt(int64_t gid, int type, int pct, int64_t b, int64_t total, const wchar_t *fn, const wchar_t *ex) {
    if (g_send_cb) g_send_cb(type, gid, pct, b, total, fn, ex);
}

static unsigned __stdcall send_worker(void *arg) {
    struct send_group *g = (struct send_group *)arg;
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2,2),&wd)!=0) {
        fire_send_evt(g->id, 3, 0, 0, 0, L"", L"WSAStartup 失败");
        return 1;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)g->dest_port);
    addr.sin_addr.s_addr = inet_addr(g->dest_ip);
    LOG_INFO(L"连接 %s:%d 共 %d 个文件", g->dest_ip, g->dest_port, g->count);
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        wchar_t e[128]; _snwprintf_s(e, 128, _TRUNCATE, L"无法连接 %s:%d (WSA=%d)", g->dest_ip, g->dest_port, WSAGetLastError());
        fire_send_evt(g->id, 3, 0, 0, 0, L"", e);
        closesocket(s); WSACleanup(); return 2;
    }
    tcp_optimize(s);  /* 禁用 Nagle + 4MB 缓冲，提升传输速度 */

    /* --- HELLO: 1字节类型(1) + 4字节 count + for each: 8字节size + 4字节utf8len + utf8相对名 --- */
    /* 先构建 HELLO 包体 */
    int body_cap = 4096, body_len = 4;
    unsigned char *body = (unsigned char *)malloc(body_cap);
    put_u32(body, (unsigned)g->count);
    for (int i = 0; i < g->count; i++) {
        char utf8[2048]; memset(utf8, 0, sizeof(utf8));
        WideCharToMultiByte(CP_UTF8, 0, g->items[i].rel_name, -1, utf8, 2048, NULL, NULL);
        int ulen = (int)strlen(utf8);
        /* 取文件大小 */
        WIN32_FILE_ATTRIBUTE_DATA f;
        uint64_t fsize = 0;
        if (GetFileAttributesExW(g->items[i].full_path, GetFileExInfoStandard, &f)) {
            ULARGE_INTEGER ul; ul.HighPart = f.nFileSizeHigh; ul.LowPart = f.nFileSizeLow;
            fsize = ul.QuadPart;
        }
        int need = 8 + 4 + ulen;
        while (body_len + need > body_cap) { body_cap *= 2; body = (unsigned char *)realloc(body, body_cap); }
        put_u64(body + body_len, fsize); body_len += 8;
        put_u32(body + body_len, ulen); body_len += 4;
        memcpy(body + body_len, utf8, ulen); body_len += ulen;
    }
    /* 总头：4字节长度(=1+body_len) + 1字节类型 + body */
    int total_len = 4 + 1 + body_len;
    unsigned char *pkt = (unsigned char *)malloc(total_len);
    put_u32(pkt, (unsigned)(1 + body_len));
    pkt[4] = MSG_HELLO;
    memcpy(pkt + 5, body, body_len);
    free(body);
    if (send_all(s, (char*)pkt, total_len) < 0) { free(pkt); closesocket(s); WSACleanup();
        fire_send_evt(g->id, 3, 0, 0, 0, L"", L"发送 HELLO 失败"); return 3; }
    free(pkt);
    LOG_INFO(L"HELLO 已发送");

    /* --- 逐个文件发送 DATA 块 --- */
    for (int i = 0; i < g->count; i++) {
        if (InterlockedCompareExchange(&g->cancel, 0, 0)) {
            fire_send_evt(g->id, 3, 0, 0, 0, g->items[i].rel_name, L"已取消");
            break;
        }
        const wchar_t *fp = g->items[i].full_path;
        const wchar_t *fn = g->items[i].rel_name;
        HANDLE hf = CreateFileW(fp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (hf == INVALID_HANDLE_VALUE) {
            wchar_t e[256]; _snwprintf_s(e, 256, _TRUNCATE, L"无法打开文件: %s", fn);
            fire_send_evt(g->id, 3, 0, 0, 0, fn, e);
            continue;
        }
        ULARGE_INTEGER fsize; fsize.HighPart = 0; fsize.LowPart = 0;
        fsize.LowPart = GetFileSize(hf, (LPDWORD)&fsize.HighPart);
        fire_send_evt(g->id, 0, 0, 0, (int64_t)fsize.QuadPart, fn, NULL);

        uint64_t off = 0;
        unsigned char *buf = (unsigned char *)malloc(4 + 1 + 4 + 4 + 8 + FT_CHUNK_SIZE); /* 预留最大长度头 */
        int last_pct = -1;  /* 进度节流：百分比变化才上报，避免事件洪泛 */
        while (off < fsize.QuadPart) {
            if (InterlockedCompareExchange(&g->cancel, 0, 0)) break;
            DWORD to_read = FT_CHUNK_SIZE;
            if (fsize.QuadPart - off < FT_CHUNK_SIZE) to_read = (DWORD)(fsize.QuadPart - off);
            DWORD actually = 0;
            OVERLAPPED ov; memset(&ov, 0, sizeof(ov));
            LARGE_INTEGER li; li.QuadPart = (LONGLONG)off;
            ov.Offset = li.LowPart; ov.OffsetHigh = li.HighPart;
            if (!ReadFile(hf, buf + 4+1+4+4+8, to_read, &actually, &ov) && GetLastError() != ERROR_IO_PENDING) break;
            if (actually == 0) break;
            /* 组织 DATA 包: len(4) | type(2) | idx(4) | off(8) | len(4) | data */
            int pay = 4 + 8 + 4 + (int)actually; /* idx + off + dlen + data */
            put_u32(buf, (unsigned)(1 + pay));
            buf[4] = MSG_DATA;
            put_u32(buf + 5, (unsigned)i);
            put_u64(buf + 9, off);
            put_u32(buf + 17, actually);
            int total = 4 + 1 + pay;
            if (send_all(s, (char*)buf, total) < 0) break;
            off += actually;
            int pct = (fsize.QuadPart > 0) ? (int)(off * 100 / fsize.QuadPart) : 100;
            if (pct != last_pct) {   /* 1% 变化才触发事件：进度条平滑又不淹没消息队列 */
                fire_send_evt(g->id, 1, pct, (int64_t)off, (int64_t)fsize.QuadPart, fn, NULL);
                last_pct = pct;
            }
        }
        free(buf);
        CloseHandle(hf);

        /* 等待 OK_DONE */
        unsigned char hdr[5];
        if (recv_all(s, (char*)hdr, 5) < 0) break;
        unsigned plen = get_u32(hdr);
        unsigned char t = hdr[4];
        unsigned char *resp = (unsigned char *)malloc(plen);
        if (recv_all(s, (char*)resp, plen-1) < 0) { free(resp); break; }
        if (t == MSG_OK) {
            fire_send_evt(g->id, 2, 100, (int64_t)fsize.QuadPart, (int64_t)fsize.QuadPart, fn, NULL);
            LOG_INFO(L"已完成: %s", fn);
        } else if (t == MSG_ERROR) {
            wchar_t msg[512]; memset(msg, 0, sizeof(msg));
            MultiByteToWideChar(CP_UTF8, 0, (char*)resp, (int)plen-1, msg, 512);
            fire_send_evt(g->id, 3, 0, 0, 0, fn, msg);
        }
        free(resp);
    }

    shutdown(s, SD_BOTH);
    closesocket(s);
    WSACleanup();
    return 0;
}

/* 发送端启动 */
int ft_send_init(void) {
    g_send_mutex = CreateMutexW(NULL, FALSE, NULL);
    g_next_gid = 1;
    return 0;
}
void ft_send_set_callback(transfer_event_cb_t cb) { g_send_cb = cb; }

int64_t ft_send_files(const char *dest_ip, int dest_port, const ft_send_item *items, int item_count) {
    if (!items || item_count <= 0) return -1;
    struct send_group *g = (struct send_group *)calloc(1, sizeof(*g));
    g->items = (ft_send_item *)malloc(sizeof(ft_send_item) * item_count);
    memcpy(g->items, items, sizeof(ft_send_item) * item_count);
    g->count = item_count;
    g->cancel = 0;
    strcpy_s(g->dest_ip, sizeof(g->dest_ip), dest_ip);
    g->dest_port = dest_port;
    WaitForSingleObject(g_send_mutex, INFINITE);
    g->id = g_next_gid++;
    g->next = g_groups; g_groups = g;
    ReleaseMutex(g_send_mutex);
    HANDLE t = (HANDLE)_beginthreadex(NULL, 0, send_worker, g, 0, NULL);
    CloseHandle(t); /* detach 即可 */
    return g->id;
}

void ft_send_cancel(int64_t group_id) {
    WaitForSingleObject(g_send_mutex, INFINITE);
    for (struct send_group *p = g_groups; p; p = p->next) {
        if (p->id == group_id) InterlockedExchange(&p->cancel, 1);
    }
    ReleaseMutex(g_send_mutex);
}

void ft_send_shutdown(void) {
    WaitForSingleObject(g_send_mutex, INFINITE);
    struct send_group *p = g_groups;
    while (p) { struct send_group *nx = p->next; free(p->items); free(p); p = nx; }
    g_groups = NULL;
    ReleaseMutex(g_send_mutex);
    CloseHandle(g_send_mutex); g_send_mutex = NULL;
}

/* ===== 接收端 ===== */
static SOCKET g_listen = INVALID_SOCKET;
static HANDLE g_listen_th = NULL;
static volatile LONG g_recv_running = 0;
static wchar_t g_save_dir[1024] = L"";
static transfer_event_cb_t g_recv_cb = NULL;
static CRITICAL_SECTION g_recv_cs;

static void fire_recv_evt(int64_t tid, int type, int pct, int64_t b, int64_t total, const wchar_t *fn, const wchar_t *ex) {
    if (g_recv_cb) g_recv_cb(type, tid, pct, b, total, fn, ex);
}

/* 保证子目录存在 */
static int mkdirs_for(wchar_t *path) {
    wchar_t *p = path;
    while ((p = wcschr(p, L'\\')) != NULL) {
        *p = 0;
        CreateDirectoryW(path, NULL);
        *p = L'\\'; p++;
    }
    return 0;
}

static unsigned __stdcall client_handler(void *arg) {
    SOCKET s = (SOCKET)(LONG_PTR)arg;
    /* 读 HELLO 头 */
    unsigned char hdr[5];
    if (recv_all(s, (char*)hdr, 5) < 0) { closesocket(s); return 1; }
    unsigned plen = get_u32(hdr);
    unsigned char type = hdr[4];
    if (type != MSG_HELLO) { closesocket(s); return 2; }
    unsigned char *hello = (unsigned char *)malloc(plen);
    if (recv_all(s, (char*)hello, plen - 1) < 0) { free(hello); closesocket(s); return 3; }
    unsigned count = get_u32(hello);
    /* 分配文件信息数组 */
    typedef struct { uint64_t size; wchar_t rel[FT_MAX_PATH]; } ft_recv_fileinfo_t;
    ft_recv_fileinfo_t *files = NULL;
    if (count > 0) files = (ft_recv_fileinfo_t *)calloc(count, sizeof(ft_recv_fileinfo_t));
    int pos = 4;
    for (unsigned i = 0; i < count; i++) {
        files[i].size = get_u64(hello + pos); pos += 8;
        unsigned ulen = get_u32(hello + pos); pos += 4;
        MultiByteToWideChar(CP_UTF8, 0, (char*)(hello + pos), (int)ulen, files[i].rel, FT_MAX_PATH);
        pos += ulen;
    }
    free(hello);
    LOG_INFO(L"接收端收到传输请求: %u 个文件", count);

    for (unsigned i = 0; i < count; i++) {
        wchar_t full[FT_MAX_PATH];
        _snwprintf_s(full, FT_MAX_PATH, _TRUNCATE, L"%s\\%s", g_save_dir, files[i].rel);
        mkdirs_for(full);
        /* 若存在同名则加 (1)(2) 后缀 */
        int ver = 1;
        while (GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES) {
            wchar_t ext[64] = L""; const wchar_t *dot = wcsrchr(files[i].rel, L'.');
            wchar_t base[FT_MAX_PATH]; wcscpy_s(base, FT_MAX_PATH, files[i].rel);
            if (dot) { base[dot - files[i].rel] = 0; wcscpy_s(ext, 64, dot); }
            wchar_t new_rel[FT_MAX_PATH];
            _snwprintf_s(new_rel, FT_MAX_PATH, _TRUNCATE, L"%s_(%d)%s", base, ver++, ext);
            _snwprintf_s(full, FT_MAX_PATH, _TRUNCATE, L"%s\\%s", g_save_dir, new_rel);
        }
        fire_recv_evt((int64_t)i, 0, 0, 0, (int64_t)files[i].size, files[i].rel, full);
        HANDLE hf = CreateFileW(full, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (hf == INVALID_HANDLE_VALUE) {
            /* 发送 ERROR */
            char emsg[256] = "无法创建目标文件";
            int blen = (int)strlen(emsg);
            unsigned char pkt[8 + 256];
            put_u32(pkt, (unsigned)(1 + blen)); pkt[4] = MSG_ERROR;
            memcpy(pkt + 5, emsg, blen);
            send_all(s, (char*)pkt, 4 + 1 + blen);
            wchar_t e[512]; _snwprintf_s(e, 512, _TRUNCATE, L"无法创建目标: %s", full);
            fire_recv_evt((int64_t)i, 3, 0, 0, 0, files[i].rel, e);
            continue;
        }
        uint64_t got = 0;
        unsigned max_pay = 4 + 8 + 4 + FT_CHUNK_SIZE;
        unsigned char *pay = (unsigned char *)malloc(max_pay); /* 循环外一次分配，块间复用 */
        int last_pct = -1;  /* 接收进度同样按百分比节流 */
        while (got < files[i].size) {
            /* 读 DATA 头 */
            if (recv_all(s, (char*)hdr, 5) < 0) break;
            unsigned dlen = get_u32(hdr);
            unsigned char dtype = hdr[4];
            int remain = dlen - 1;
            int need = remain < (int)max_pay ? remain : (int)max_pay;
            if (recv_all(s, (char*)pay, need) < 0) break;
            if (dtype != MSG_DATA) break;
            unsigned idx = get_u32(pay); (void)idx;
            uint64_t off = get_u64(pay + 4);
            unsigned chunklen = get_u32(pay + 12);
            LARGE_INTEGER li; li.QuadPart = (LONGLONG)off;
            OVERLAPPED ov; memset(&ov, 0, sizeof(ov));
            ov.Offset = li.LowPart; ov.OffsetHigh = li.HighPart;
            DWORD written = 0;
            WriteFile(hf, pay + 16, chunklen, &written, &ov);
            got += chunklen;
            int pct = files[i].size ? (int)(got * 100 / files[i].size) : 100;
            if (pct != last_pct) {
                fire_recv_evt((int64_t)i, 1, pct, (int64_t)got, (int64_t)files[i].size, files[i].rel, full);
                last_pct = pct;
            }
        }
        free(pay);
        CloseHandle(hf);
        /* 发送 OK_DONE */
        unsigned char okpkt[16];
        put_u32(okpkt, (unsigned)(1 + 4 + 8));
        okpkt[4] = MSG_OK;
        put_u32(okpkt + 5, i);
        put_u64(okpkt + 9, got);
        send_all(s, (char*)okpkt, 4 + 1 + 4 + 8);
        if (got >= files[i].size) {
            fire_recv_evt((int64_t)i, 2, 100, (int64_t)got, (int64_t)files[i].size, files[i].rel, full);
            fire_recv_evt((int64_t)i, 4, 100, (int64_t)got, (int64_t)files[i].size, files[i].rel, full);
            LOG_INFO(L"接收完成: %s", full);
        }
    }
    free(files);
    shutdown(s, SD_BOTH);
    closesocket(s);
    return 0;
}

static unsigned __stdcall listen_loop(void *unused) {
    (void)unused;
    while (InterlockedCompareExchange(&g_recv_running, 0, 0)) {
        fd_set fds; FD_ZERO(&fds); FD_SET(g_listen, &fds);
        struct timeval tv = {1, 0};  /* 1s 超时，减少 CPU 唤醒 */
        int r = select(0, &fds, NULL, NULL, &tv);
        if (r <= 0) continue;
        struct sockaddr_in from; int flen = sizeof(from);
        SOCKET c = accept(g_listen, (struct sockaddr*)&from, &flen);
        if (c == INVALID_SOCKET) continue;
        tcp_optimize(c);  /* 接收端也优化 TCP 缓冲 */
        HANDLE t = (HANDLE)_beginthreadex(NULL, 0, client_handler, (void*)(LONG_PTR)c, 0, NULL);
        CloseHandle(t);
    }
    return 0;
}

int ft_recv_start(int port, const wchar_t *save_dir) {
    if (InterlockedCompareExchange(&g_recv_running, 0, 0)) {
        LOG_WARN(L"接收端已运行");
        return 0;
    }
    InitializeCriticalSection(&g_recv_cs);
    wcscpy_s(g_save_dir, 1024, save_dir ? save_dir : L"");
    if (!g_save_dir[0]) {
        SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, g_save_dir);
        wcscat_s(g_save_dir, 1024, L"\\My文件传输");
    }
    CreateDirectoryW(g_save_dir, NULL);
    WSADATA wd; WSAStartup(MAKEWORD(2,2), &wd);
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL one = TRUE;
    setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(g_listen, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR(L"TCP bind %d 失败: %d", port, WSAGetLastError());
        closesocket(g_listen); g_listen = INVALID_SOCKET;
        DeleteCriticalSection(&g_recv_cs);
        return -1;
    }
    listen(g_listen, 8);
    InterlockedExchange(&g_recv_running, 1);
    g_listen_th = (HANDLE)_beginthreadex(NULL, 0, listen_loop, NULL, 0, NULL);
    LOG_INFO(L"接收端已启动: port=%d save_dir=%s", port, g_save_dir);
    return 0;
}

void ft_recv_set_callback(transfer_event_cb_t cb) { g_recv_cb = cb; }
void ft_recv_set_save_dir(const wchar_t *save_dir) {
    EnterCriticalSection(&g_recv_cs);
    if (save_dir && save_dir[0]) wcsncpy_s(g_save_dir, 1024, save_dir, _TRUNCATE);
    LeaveCriticalSection(&g_recv_cs);
}
void ft_recv_stop(void) {
    if (!InterlockedCompareExchange(&g_recv_running, 0, 0)) return;
    InterlockedExchange(&g_recv_running, 0);
    if (g_listen != INVALID_SOCKET) { closesocket(g_listen); g_listen = INVALID_SOCKET; }
    if (g_listen_th) { WaitForSingleObject(g_listen_th, 2000); CloseHandle(g_listen_th); g_listen_th = NULL; }
    DeleteCriticalSection(&g_recv_cs);
    WSACleanup();
    LOG_INFO(L"接收端已停止");
}
int  ft_recv_is_running(void)     { return InterlockedCompareExchange(&g_recv_running, 0, 0) ? 1 : 0; }
const wchar_t *ft_recv_save_dir(void) { return g_save_dir; }
