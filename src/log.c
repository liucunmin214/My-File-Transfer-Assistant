/*
 * log.c - 日志实现
 */
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <windows.h>
#include <shlwapi.h>

static FILE *g_log_fp = NULL;
static log_sink_cb_t g_sink = NULL;
static int g_level = LOG_DEBUG;
static CRITICAL_SECTION g_cs;
static int g_inited = 0;

static const wchar_t *level_str(int lv) {
    switch (lv) {
        case LOG_DEBUG: return L"DEBUG";
        case LOG_INFO:  return L"INFO ";
        case LOG_WARN:  return L"WARN ";
        case LOG_ERROR: return L"ERROR";
    }
    return L"?";
}

int log_init(const wchar_t *appdata_dir) {
    if (g_inited) return 0;
    InitializeCriticalSection(&g_cs);
    g_inited = 1;
    if (appdata_dir && appdata_dir[0]) {
        wchar_t path[MAX_PATH];
        _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\Logs", appdata_dir);
        CreateDirectoryW(path, NULL);
        SYSTEMTIME t; GetLocalTime(&t);
        _snwprintf_s(path, MAX_PATH, _TRUNCATE,
            L"%s\\Logs\\ft_%04d%02d%02d.log", appdata_dir,
            t.wYear, t.wMonth, t.wDay);
        g_log_fp = _wfopen(path, L"a+, ccs=UTF-8");
    }
    return 0;
}

void log_set_sink(log_sink_cb_t cb) {
    EnterCriticalSection(&g_cs);
    g_sink = cb;
    LeaveCriticalSection(&g_cs);
}

void log_set_level(int level) { g_level = level; }

void log_write(int level, const wchar_t *fmt, ...) {
    if (!g_inited) log_init(NULL);
    if (level < g_level) return;
    EnterCriticalSection(&g_cs);
    wchar_t buf[4096];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf_s(buf, 4096, _TRUNCATE, fmt, ap);
    va_end(ap);

    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t line[4200];
    _snwprintf_s(line, 4200, _TRUNCATE,
        L"%02d:%02d:%02d.%03d [%s] %s",
        t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
        level_str(level), buf);

    if (g_log_fp) {
        fwprintf(g_log_fp, L"%s\n", line);
        fflush(g_log_fp);
    }
    if (g_sink) g_sink(level, line);
    LeaveCriticalSection(&g_cs);
}

void log_close(void) {
    if (!g_inited) return;
    EnterCriticalSection(&g_cs);
    if (g_log_fp) { fclose(g_log_fp); g_log_fp = NULL; }
    g_sink = NULL;
    LeaveCriticalSection(&g_cs);
    DeleteCriticalSection(&g_cs);
    g_inited = 0;
}
