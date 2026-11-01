/*
 * 同步引擎实现
 */
#include "sync.h"
#include "net.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYNC_INTERVAL_MS 1000
#define MAX_FILE_SIZE  (256UL * 1024 * 1024)

typedef struct {
    wchar_t rel[MAX_PATH];
    double mtime;
    unsigned long size;
} Entry;

typedef struct {
    Entry *items;
    size_t count;
    size_t cap;
} Snapshot;

static CRITICAL_SECTION *g_pcfg_lock = NULL;  /* 线程安全一次性初始化 */
#define LOCK()   EnterCriticalSection(g_pcfg_lock)
#define UNLOCK() LeaveCriticalSection(g_pcfg_lock)

static wchar_t g_source[MAX_PATH];
static int     g_sync_all = 1;
static wchar_t g_sel[256][MAX_PATH];  /* 最多 256 个选中文件 */
static int     g_sel_count = 0;
static char    g_host[128];
static int     g_port = 51820;

static sync_event_cb g_cb = NULL;

static HANDLE g_thread = NULL;
static volatile LONG g_running = 0;

/* 线程安全的一次性初始化（InterlockedCompareExchange + spin） */
static void cfg_ensure_init(void) {
    if (g_pcfg_lock) return;
    /* 先分配一个本地对象再 CAS 发布，避免泄漏 */
    CRITICAL_SECTION *pcs = (CRITICAL_SECTION *)malloc(sizeof(CRITICAL_SECTION));
    if (!pcs) return;
    InitializeCriticalSection(pcs);
    if (InterlockedCompareExchangePointer((PVOID*)&g_pcfg_lock, pcs, NULL) != NULL) {
        /* 另一个线程抢先了，释放本地 */
        DeleteCriticalSection(pcs);
        free(pcs);
    }
}

static void emit(int type, const wchar_t *arg, int num) {
    if (g_cb) g_cb(type, arg, num);
}

static int snap_init(Snapshot *s) {
    s->cap = 64; s->count = 0;
    s->items = (Entry *)malloc(s->cap * sizeof(Entry));
    return s->items != NULL;
}

static void snap_free(Snapshot *s) {
    free(s->items); s->items = NULL; s->count = 0; s->cap = 0;
}

static void snap_add(Snapshot *s, const wchar_t *rel, double mt,
                     unsigned long sz) {
    if (!s || !s->items) return;
    if (s->count >= s->cap) {
        s->cap *= 2;
        Entry *ni = (Entry *)realloc(s->items, s->cap * sizeof(Entry));
        if (!ni) return;
        s->items = ni;
    }
    wcscpy_s(s->items[s->count].rel, MAX_PATH, rel);
    s->items[s->count].mtime = mt;
    s->items[s->count].size = sz;
    s->count++;
}

static Entry *snap_find(Snapshot *s, const wchar_t *rel) {
    if (!s || !s->items) return NULL;
    for (size_t i = 0; i < s->count; i++) {
        if (_wcsicmp(s->items[i].rel, rel) == 0) return &s->items[i];
    }
    return NULL;
}

/* 递归扫描，root=源根，sub=当前子目录相对路径 */
static void scan_dir(const wchar_t *root, const wchar_t *sub, Snapshot *s) {
    wchar_t pattern[MAX_PATH];
    if (sub[0])
        _snwprintf_s(pattern, MAX_PATH, _TRUNCATE, L"%ls\\%ls\\*", root, sub);
    else
        _snwprintf_s(pattern, MAX_PATH, _TRUNCATE, L"%ls\\*", root);

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 ||
            wcscmp(fd.cFileName, L"..") == 0) continue;
        wchar_t relsub[MAX_PATH];
        if (sub[0])
            _snwprintf_s(relsub, MAX_PATH, _TRUNCATE, L"%ls\\%ls", sub, fd.cFileName);
        else
            _snwprintf_s(relsub, MAX_PATH, _TRUNCATE, L"%ls", fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_dir(root, relsub, s);
        } else {
            unsigned long sz = (unsigned long)fd.nFileSizeLow;
            /* 忽略超大文件 */
            if (fd.nFileSizeHigh > 0 || sz > MAX_FILE_SIZE) continue;
            double mt = (double)(((LONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) |
                                  fd.ftLastWriteTime.dwLowDateTime);
            mt = mt / 10000000.0 - 11644473600.0;
            if (mt < 0) mt = 0;
            snap_add(s, relsub, mt, sz);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void build_snapshot(Snapshot *s) {
    cfg_ensure_init();
    snap_init(s);
    LOCK();
    wchar_t src[MAX_PATH];
    wcscpy_s(src, MAX_PATH, g_source);
    int sync_all = g_sync_all;
    UNLOCK();

    if (!src[0] || !s->items) return;
    if (sync_all) {
        scan_dir(src, L"", s);
    } else {
        /* 仅扫描选中文件 */
        LOCK();
        int n = g_sel_count;
        wchar_t sel_copy[256][MAX_PATH];
        for (int i = 0; i < n; i++)
            wcscpy_s(sel_copy[i], MAX_PATH, g_sel[i]);
        UNLOCK();
        for (int i = 0; i < n; i++) {
            wchar_t full[MAX_PATH];
            _snwprintf_s(full, MAX_PATH, _TRUNCATE, L"%ls\\%ls", src, sel_copy[i]);
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW(full, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    unsigned long sz = (unsigned long)fd.nFileSizeLow;
                    if (fd.nFileSizeHigh == 0 && sz <= MAX_FILE_SIZE) {
                        double mt = (double)(((LONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) |
                                              fd.ftLastWriteTime.dwLowDateTime);
                        mt = mt / 10000000.0 - 11644473600.0;
                        if (mt < 0) mt = 0;
                        snap_add(s, sel_copy[i], mt, sz);
                    }
                }
                FindClose(h);
            }
        }
    }
}

static int read_file_all(const wchar_t *full, char **out, unsigned long *sz) {
    HANDLE h = CreateFileW(full, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER fs;
    if (!GetFileSizeEx(h, &fs) || fs.HighPart != 0 ||
        fs.LowPart > MAX_FILE_SIZE) {
        CloseHandle(h); return 0;
    }
    *sz = fs.LowPart;
    *out = (char *)malloc(*sz ? *sz : 1);
    if (!*out) { CloseHandle(h); return 0; }
    DWORD readn = 0;
    if (*sz) {
        if (!ReadFile(h, *out, *sz, &readn, NULL) || readn != *sz) {
            CloseHandle(h); free(*out); return 0;
        }
    }
    CloseHandle(h);
    return 1;
}

static DWORD WINAPI sync_thread(LPVOID arg) {
    (void)arg;
    Snapshot prev = {0};
    int initialized = 0;

    while (g_running) {
        /* 确保连接 */
        if (!net_sender_connected()) {
            char host[128]; int port;
            LOCK();
            strncpy(host, g_host, sizeof(host) - 1); host[sizeof(host)-1]=0;
            port = g_port;
            UNLOCK();
            if (host[0]) {
                emit(1, L"connecting", 0);
                if (!net_sender_connect(host, port)) {
                    emit(1, L"error", 0);
                    log_line(L"[错误] 无法连接 %hs:%d", host, port);
                    Sleep(SYNC_INTERVAL_MS);
                    continue;
                }
            } else {
                emit(1, L"error", 0);
                Sleep(SYNC_INTERVAL_MS);
                continue;
            }
        }

        Snapshot cur;
        build_snapshot(&cur);
        emit(4, NULL, (int)cur.count);

        /* diff */
        size_t changed_count = 0, deleted_count = 0;
        for (size_t i = 0; i < cur.count; i++) {
            Entry *e = &cur.items[i];
            Entry *p = initialized ? snap_find(&prev, e->rel) : NULL;
            if (!p || p->mtime != e->mtime || p->size != e->size) {
                /* 发送 */
                wchar_t full[MAX_PATH];
                _snwprintf_s(full, MAX_PATH, _TRUNCATE, L"%ls\\%ls", g_source, e->rel);
                char *data = NULL; unsigned long sz = 0;
                if (read_file_all(full, &data, &sz)) {
                    if (net_sender_send_file(e->rel, data, sz, e->mtime)) {
                        emit(2, e->rel, (int)sz);
                        log_line(L"同步 → %ls (%lu B)", e->rel, sz);
                        changed_count++;
                    } else {
                        emit(1, L"error", 0);
                    }
                    free(data);
                }
            }
        }
        if (initialized) {
            for (size_t i = 0; i < prev.count; i++) {
                if (!snap_find(&cur, prev.items[i].rel)) {
                    if (net_sender_send_delete(prev.items[i].rel)) {
                        emit(3, prev.items[i].rel, 0);
                        log_line(L"删除 → %ls", prev.items[i].rel);
                        deleted_count++;
                    }
                }
            }
        }

        snap_free(&prev);
        prev = cur;
        initialized = 1;

        if (changed_count || deleted_count) emit(1, L"syncing", 0);
        else emit(1, L"idle", 0);

        Sleep(SYNC_INTERVAL_MS);
    }

    snap_free(&prev);
    net_sender_disconnect();
    emit(1, L"idle", 0);
    return 0;
}

void sync_set_event_cb(sync_event_cb cb) {
    cfg_ensure_init();
    LOCK();
    g_cb = cb;
    UNLOCK();
}

void sync_set_source(const wchar_t *path) {
    cfg_ensure_init();
    LOCK();
    wcscpy_s(g_source, MAX_PATH, path ? path : L"");
    UNLOCK();
}

void sync_set_selection(int sync_all, const wchar_t **paths, int count) {
    cfg_ensure_init();
    LOCK();
    g_sync_all = sync_all;
    g_sel_count = 0;
    if (paths && count > 0) {
        int n = count > 256 ? 256 : count;
        for (int i = 0; i < n; i++) {
            wcscpy_s(g_sel[i], MAX_PATH, paths[i]);
            g_sel_count++;
        }
    }
    UNLOCK();
}

void sync_set_target(const char *host, int port) {
    cfg_ensure_init();
    LOCK();
    if (host) { strncpy(g_host, host, sizeof(g_host) - 1); g_host[sizeof(g_host)-1]=0; }
    g_port = port;
    UNLOCK();
}

int sync_start(void) {
    cfg_ensure_init();
    if (g_running) return 1;
    LOCK();
    int have_source = g_source[0] != L'\0';
    UNLOCK();
    if (!have_source) {
        log_line(L"[错误] 未设置源目录");
        return 0;
    }
    InterlockedExchange(&g_running, 1);
    g_thread = CreateThread(NULL, 0, sync_thread, NULL, 0, NULL);
    log_line(L"同步已启动（每 %d ms 一次）", SYNC_INTERVAL_MS);
    return g_thread != NULL;
}

void sync_stop(void) {
    InterlockedExchange(&g_running, 0);
    if (g_thread) {
        WaitForSingleObject(g_thread, 3000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    net_sender_disconnect();
    log_line(L"同步已停止");
}

int sync_running(void) {
    return g_running;
}
