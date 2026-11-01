/*
 * send_module.c - 文件传输 · 独立发送模块 (FileSender.exe)
 *
 * 交互逻辑：
 *   右键菜单选择"传输"后，资源管理器调用本 EXE 并把选中文件路径作为
 *   命令行参数传入；本模块直接进入传输界面，列出文件 + 附近设备，
 *   用户选中目标设备后点"发送"即可。传输完成后自动提示并退出。
 *
 * 命令行：
 *   FileSender.exe "C:\path\file1" "D:\folder2" ...
 *
 * 设计：
 *   - 复用主程序的 sync_core.dll (transfer / discover) 与 config
 *   - 自带轻量主题（深色/浅色跟随配置），不依赖主程序进程
 *   - 关闭即退出（无托盘常驻），符合"独立模块"定位
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "config.h"
#include "discover.h"
#include "transfer.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "sync_core.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#define TIMER_ANIM 1
#define ANIM_INTERVAL 40

/* ---------- 主题色（全新设计） ---------- */
/* Light theme */
#define COL_BG_L        RGB(245,247,251)
#define COL_BG_2_L      RGB(238,242,249)
#define COL_SURFACE_L   RGB(255,255,255)
#define COL_SURFACE_HI_L RGB(252,253,255)
#define COL_BORDER_L    RGB(224,228,238)
#define COL_BORDER_SOFT_L RGB(232,236,246)
#define COL_TEXT_L      RGB(18,22,38)
#define COL_TEXT_2_L    RGB(72,78,96)
#define COL_SUB_L       RGB(130,138,160)
#define COL_INPUT_BG_L  RGB(255,255,255)
#define COL_INPUT_BD_L  RGB(210,216,230)
#define COL_INPUT_FOCUS_L RGB(92,70,255)
#define COL_LIST_BG_L   RGB(252,253,255)
#define COL_LIST_SEL_L  RGB(235,230,255)
#define COL_LIST_HI_L   RGB(245,242,255)
#define COL_GHOST_L     RGB(236,240,248)
#define COL_SUCCESS_L   RGB(34,197,94)
#define COL_WARN_L      RGB(245,158,11)
#define COL_ERROR_L     RGB(239,68,68)
#define COL_SHADOW_L    RGB(200,205,220)

/* Dark theme */
#define COL_BG_D        RGB(16,18,30)
#define COL_BG_2_D      RGB(22,24,40)
#define COL_SURFACE_D   RGB(28,30,50)
#define COL_SURFACE_HI_D RGB(34,36,58)
#define COL_BORDER_D    RGB(50,54,78)
#define COL_BORDER_SOFT_D RGB(42,46,68)
#define COL_TEXT_D      RGB(235,236,245)
#define COL_TEXT_2_D    RGB(180,185,205)
#define COL_SUB_D       RGB(140,148,172)
#define COL_INPUT_BG_D  RGB(22,24,42)
#define COL_INPUT_BD_D  RGB(55,58,84)
#define COL_INPUT_FOCUS_D RGB(139,124,255)
#define COL_LIST_BG_D   RGB(24,26,44)
#define COL_LIST_SEL_D  RGB(55,48,110)
#define COL_LIST_HI_D   RGB(40,36,72)
#define COL_GHOST_D     RGB(40,44,66)
#define COL_SUCCESS_D   RGB(74,222,128)
#define COL_WARN_D      RGB(251,191,36)
#define COL_ERROR_D     RGB(248,113,113)
#define COL_SHADOW_D    RGB(10,12,22)

/* Accent colors */
#define NUM_ACCENTS 6
static COLORREF accent_light[NUM_ACCENTS] = {
    RGB(92,70,255), RGB(37,99,235), RGB(22,163,74),
    RGB(225,29,72), RGB(234,88,12), RGB(13,148,136)
};
static COLORREF accent_dark[NUM_ACCENTS] = {
    RGB(139,124,255), RGB(96,165,250), RGB(74,222,128),
    RGB(251,113,133), RGB(251,146,60), RGB(45,212,191)
};
#define AC() (g.dark ? accent_dark[g.accent] : accent_light[g.accent])

/* Theme accessors */
#define C_L(dark, light) ((dark) ? (light##_D) : (light##_L))
#define COL_BG          C_L(g.dark, COL_BG)
#define COL_BG_2        C_L(g.dark, COL_BG_2)
#define COL_SURFACE     C_L(g.dark, COL_SURFACE)
#define COL_SURFACE_HI  C_L(g.dark, COL_SURFACE_HI)
#define COL_BORDER      C_L(g.dark, COL_BORDER)
#define COL_BORDER_SOFT C_L(g.dark, COL_BORDER_SOFT)
#define COL_TEXT        C_L(g.dark, COL_TEXT)
#define COL_TEXT_2      C_L(g.dark, COL_TEXT_2)
#define COL_SUB         C_L(g.dark, COL_SUB)
#define COL_INPUT_BG    C_L(g.dark, COL_INPUT_BG)
#define COL_INPUT_BD    C_L(g.dark, COL_INPUT_BD)
#define COL_INPUT_FOCUS C_L(g.dark, COL_INPUT_FOCUS)
#define COL_LIST_BG     C_L(g.dark, COL_LIST_BG)
#define COL_LIST_SEL    C_L(g.dark, COL_LIST_SEL)
#define COL_LIST_HI     C_L(g.dark, COL_LIST_HI)
#define COL_GHOST       C_L(g.dark, COL_GHOST)
#define COL_SUCCESS     C_L(g.dark, COL_SUCCESS)
#define COL_WARN        C_L(g.dark, COL_WARN)
#define COL_ERROR       C_L(g.dark, COL_ERROR)
#define COL_SHADOW      C_L(g.dark, COL_SHADOW)

/* ---------- 控件 ID ---------- */
enum {
    ID_REFRESH = 100, ID_DEVLIST, ID_FILELIST,
    ID_ADDIP, ID_ADDPORT, ID_ADDBTN,
    ID_REMOVEFILE, ID_CLEARFILE, ID_ADDFILE,
    ID_SEND, ID_LOG, ID_STATUS, ID_COUNT,
    ID_PROGRESS
};

#define WM_DEVUPD  (WM_APP + 1)
#define WM_SENDEV  (WM_APP + 2)

#define MAX_FILES 512

typedef struct {
    HWND hwnd;
    HFONT font, fontBold, fontTitle, fontSmall, fontCaption;
    HBRUSH brBg, brBg2, brSurface, brSurfaceHi, brInput, brList, brBorder;
    int dark;
    int accent;
    ft_config cfg;

    HWND devList;
    HWND fileList;
    HWND refreshBtn;
    HWND addIp, addPort, addBtn;
    HWND removeFile, clearFile, addFile;
    HWND sendBtn;
    HWND log;
    HWND status;
    HWND count;
    int selected_dev;

    ft_device devices[MAX_DEVICES];
    int dev_count;
    ft_device manual[MAX_DEVICES];
    int manual_count;

    wchar_t files[MAX_FILES][FT_MAX_PATH];
    wchar_t names[MAX_FILES][FT_MAX_PATH];
    int file_count;
    int64_t cur_gid;
    int sending;
    int progress_pct;
    float ui_scale;
    int hover_btn;
    int tick_anim;
} Mod;

static Mod g;

#define S(x) ((int)((x) * g.ui_scale + 0.5f))

static float compute_scale(void) {
    HDC dc = GetDC(NULL);
    int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    float s = (float)dpi / 96.0f * 1.25f;
    if (s < 1.0f) s = 1.0f;
    if (s > 2.5f) s = 2.5f;
    return s;
}

/* ---------- 跨线程回调 ---------- */
static void dev_cb(int type, const wchar_t *arg) {
    (void)type; (void)arg;
    PostMessageW(g.hwnd, WM_DEVUPD, 0, 0);
}

typedef struct { int type; int64_t tid; int pct; int64_t b, tot; wchar_t *fn, *ex; } ev;
static void send_cb(int type, int64_t tid, int pct, int64_t b, int64_t tot,
                    const wchar_t *fn, const wchar_t *ex) {
    ev *e = (ev*)calloc(1, sizeof(*e));
    e->type = type; e->tid = tid; e->pct = pct; e->b = b; e->tot = tot;
    if (fn) { size_t n = wcslen(fn)+1; e->fn = (wchar_t*)malloc(n*sizeof(wchar_t)); wcscpy_s(e->fn,n,fn); }
    if (ex) { size_t n = wcslen(ex)+1; e->ex = (wchar_t*)malloc(n*sizeof(wchar_t)); wcscpy_s(e->ex,n,ex); }
    PostMessageW(g.hwnd, WM_SENDEV, 0, (LPARAM)e);
}

/* ---------- 工具 ---------- */
static void append_log(HWND h, const wchar_t *m) {
    int len = GetWindowTextLengthW(h);
    SendMessageW(h, EM_SETSEL, len, len);
    SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)m);
    SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    SendMessageW(h, EM_SCROLLCARET, 0, 0);
}

static void set_status(int sid) {
    const wchar_t *t; COLORREF c;
    switch (sid) {
        case 1: t = L"传输中"; c = COL_WARN;    break;
        case 2: t = L"已完成"; c = COL_SUCCESS; break;
        case 3: t = L"错误";   c = COL_ERROR;   break;
        default:t = L"待机";   c = COL_SUB;     break;
    }
    SetWindowTextW(g.status, t);
    SetWindowLongPtrW(g.status, GWLP_USERDATA, (LONG_PTR)c);
    InvalidateRect(g.status, NULL, TRUE);
}

static void refresh_devs(void) {
    ft_device tmp[MAX_DEVICES];
    int n = discover_get_devices(tmp, MAX_DEVICES);
    int total = 0;
    if (n > 0 && n <= MAX_DEVICES) { memcpy(g.devices, tmp, sizeof(ft_device)*n); total = n; }
    for (int mi = 0; mi < g.manual_count; mi++) {
        int dup = 0;
        for (int i = 0; i < total; i++)
            if (g.devices[i].port == g.manual[mi].port && !strcmp(g.devices[i].ip, g.manual[mi].ip)) { dup=1; break; }
        if (!dup && total < MAX_DEVICES) g.devices[total++] = g.manual[mi];
    }
    g.dev_count = total;
    ListView_DeleteAllItems(g.devList);
    for (int i = 0; i < total; i++) {
        LVITEMW lvi = {0}; lvi.mask = LVIF_TEXT|LVIF_PARAM; lvi.iItem = i; lvi.lParam = (LPARAM)i;
        wchar_t b[256]; _snwprintf_s(b,256,_TRUNCATE,L"%s", g.devices[i].name); lvi.pszText = b;
        ListView_InsertItem(g.devList, &lvi);
        _snwprintf_s(b,256,_TRUNCATE,L"%hs", g.devices[i].ip); ListView_SetItemText(g.devList,i,1,b);
        _snwprintf_s(b,256,_TRUNCATE,L"%d", g.devices[i].port); ListView_SetItemText(g.devList,i,2,b);
        ListView_SetItemText(g.devList,i,3, g.devices[i].last_seen<0 ? L"手动" : L"在线");
    }
    wchar_t cnt[128]; _snwprintf_s(cnt,128,_TRUNCATE,L"设备 %d 台 · 文件 %d 个", g.dev_count, g.file_count);
    SetWindowTextW(g.count, cnt);
}

/* ---------- 文件添加 ---------- */
static void add_file(const wchar_t *full) {
    if (g.file_count >= MAX_FILES) return;
    const wchar_t *nm = wcsrchr(full, L'\\'); nm = nm ? nm+1 : full;
    for (int i = 0; i < g.file_count; i++)
        if (!wcscmp(g.files[i], full)) return;
    wcscpy_s(g.files[g.file_count], FT_MAX_PATH, full);
    wcscpy_s(g.names[g.file_count], FT_MAX_PATH, nm);
    SendMessageW(g.fileList, LB_ADDSTRING, 0, (LPARAM)full);
    g.file_count++;
    wchar_t cnt[128]; _snwprintf_s(cnt,128,_TRUNCATE,L"设备 %d 台 · 文件 %d 个", g.dev_count, g.file_count);
    SetWindowTextW(g.count, cnt);
}

static void pick_files(HWND owner) {
    wchar_t buf[32768] = {0};
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = owner;
    ofn.lpstrFile = buf; ofn.nMaxFile = 32768;
    ofn.lpstrFilter = L"所有文件\0*.*\0";
    ofn.Flags = OFN_EXPLORER|OFN_ALLOWMULTISELECT|OFN_FILEMUSTEXIST|OFN_DONTADDTORECENT;
    if (!GetOpenFileNameW(&ofn)) return;
    const wchar_t *p = buf;
    wchar_t dir[MAX_PATH]; wcscpy_s(dir, MAX_PATH, p); p += wcslen(p)+1;
    if (*p == 0) add_file(dir);
    else while (*p) { wchar_t f[MAX_PATH]; _snwprintf_s(f,MAX_PATH,_TRUNCATE,L"%s\\%s",dir,p); add_file(f); p += wcslen(p)+1; }
}

/* ---------- 发送 ---------- */
static void do_send(void) {
    if (g.file_count == 0) { MessageBoxW(g.hwnd, L"请先添加要发送的文件", L"提示", MB_OK|MB_ICONINFORMATION); return; }
    LRESULT sel = ListView_GetNextItem(g.devList, -1, LVNI_SELECTED);
    if (sel < 0) { MessageBoxW(g.hwnd, L"请先选择目标设备", L"提示", MB_OK|MB_ICONINFORMATION); return; }
    if (sel >= g.dev_count) return;
    ft_device *d = &g.devices[sel];
    ft_send_item *items = (ft_send_item*)malloc(sizeof(ft_send_item)*g.file_count);
    for (int i = 0; i < g.file_count; i++) {
        wcscpy_s(items[i].full_path, FT_MAX_PATH, g.files[i]);
        wcscpy_s(items[i].rel_name, FT_MAX_PATH, g.names[i]);
    }
    int64_t gid = ft_send_files(d->ip, d->port, items, g.file_count);
    free(items);
    if (gid < 0) { MessageBoxW(g.hwnd, L"启动发送失败", L"错误", MB_OK|MB_ICONERROR); return; }
    g.cur_gid = gid; g.sending = 1;
    g.progress_pct = 0;
    set_status(1);
    EnableWindow(g.sendBtn, FALSE);
    MultiByteToWideChar(CP_ACP, 0, d->ip, -1, g.cfg.last_dest_ip, 64);
    g.cfg.last_dest_port = d->port;
}

/* ---------- 创建控件 ---------- */
static HWND mk(HWND parent, const wchar_t *cls, const wchar_t *text,
               DWORD style, DWORD ex, int id, HFONT f) {
    HWND h = CreateWindowExW(ex, cls, text?text:L"", style,
        0,0,120,24, parent, (HMENU)(LONG_PTR)id,
        (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE), NULL);
    if (f) SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE);
    return h;
}

static void create_controls(HWND hwnd, HINSTANCE hi) {
    (void)hi;
    g.ui_scale = compute_scale();
    int fs_body = S(13), fs_title = S(16), fs_sm = S(11), fs_cap = S(10);
    g.font       = CreateFontW(fs_body, 0,0,0, FW_NORMAL, 0,0,0, GB2312_CHARSET,0,0,CLEARTYPE_QUALITY,VARIABLE_PITCH|FF_MODERN, L"Microsoft YaHei UI, Segoe UI Variable");
    g.fontBold   = CreateFontW(fs_body, 0,0,0, FW_SEMIBOLD,0,0,0,GB2312_CHARSET,0,0,CLEARTYPE_QUALITY,VARIABLE_PITCH|FF_MODERN, L"Microsoft YaHei UI, Segoe UI Variable");
    g.fontTitle  = CreateFontW(fs_title,0,0,0, FW_SEMIBOLD,0,0,0,GB2312_CHARSET,0,0,CLEARTYPE_QUALITY,VARIABLE_PITCH|FF_MODERN, L"Microsoft YaHei UI, Segoe UI Variable");
    g.fontSmall  = CreateFontW(fs_sm,  0,0,0, FW_NORMAL,0,0,0, GB2312_CHARSET,0,0,CLEARTYPE_QUALITY,VARIABLE_PITCH|FF_MODERN, L"Microsoft YaHei UI, Segoe UI Variable");
    g.fontCaption= CreateFontW(fs_cap, 0,0,0, FW_NORMAL,0,0,0, GB2312_CHARSET,0,0,CLEARTYPE_QUALITY,VARIABLE_PITCH|FF_MODERN, L"Microsoft YaHei UI, Segoe UI Variable");

    g.dark = g.cfg.dark_mode ? 1 : 0;
    g.accent = g.cfg.accent_id;
    g.brBg        = CreateSolidBrush(COL_BG);
    g.brBg2       = CreateSolidBrush(COL_BG_2);
    g.brSurface   = CreateSolidBrush(COL_SURFACE);
    g.brSurfaceHi = CreateSolidBrush(COL_SURFACE_HI);
    g.brInput     = CreateSolidBrush(COL_INPUT_BG);
    g.brList      = CreateSolidBrush(COL_LIST_BG);
    g.brBorder    = CreateSolidBrush(COL_BORDER);

    /* 设备列表 - 自绘 */
    g.devList = CreateWindowExW(0, WC_LISTVIEWW, L"",
        WS_CHILD|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_VISIBLE,
        0,0,400,160, hwnd, (HMENU)(LONG_PTR)ID_DEVLIST,
        (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE), NULL);
    SendMessageW(g.devList, WM_SETFONT, (WPARAM)g.font, TRUE);
    ListView_SetExtendedListViewStyle(g.devList,
        LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
    LVCOLUMNW col = {0}; col.mask = LVCF_FMT|LVCF_WIDTH|LVCF_TEXT; col.fmt = LVCFMT_LEFT;
    col.cx=S(180); col.pszText=L"设备名称";  ListView_InsertColumn(g.devList,0,&col);
    col.cx=S(120); col.pszText=L"IP 地址";   ListView_InsertColumn(g.devList,1,&col);
    col.cx=S(80);  col.pszText=L"端口";      ListView_InsertColumn(g.devList,2,&col);
    col.cx=S(60);  col.pszText=L"状态";      ListView_InsertColumn(g.devList,3,&col);

    g.refreshBtn = mk(hwnd, L"BUTTON", L"刷新", WS_CHILD|BS_OWNERDRAW|WS_VISIBLE, 0, ID_REFRESH, g.font);
    g.addIp   = mk(hwnd, L"EDIT", L"", WS_CHILD|ES_AUTOHSCROLL|WS_VISIBLE, WS_EX_CLIENTEDGE, ID_ADDIP, g.font);
    g.addPort = mk(hwnd, L"EDIT", L"51831", WS_CHILD|ES_AUTOHSCROLL|WS_VISIBLE, WS_EX_CLIENTEDGE, ID_ADDPORT, g.font);
    g.addBtn  = mk(hwnd, L"BUTTON", L"添加设备", WS_CHILD|BS_OWNERDRAW|WS_VISIBLE, 0, ID_ADDBTN, g.fontBold);
    SendMessageW(g.addIp, EM_SETCUEBANNER, TRUE, (LPARAM)L"输入 IP 地址");
    SendMessageW(g.addPort, EM_SETCUEBANNER, TRUE, (LPARAM)L"端口");

    /* 文件列表 */
    g.fileList = mk(hwnd, L"LISTBOX", L"", WS_CHILD|WS_VSCROLL|LBS_NOINTEGRALHEIGHT|WS_VISIBLE|WS_TABSTOP,
                    WS_EX_CLIENTEDGE, ID_FILELIST, g.font);
    g.addFile    = mk(hwnd, L"BUTTON", L"添加文件", WS_CHILD|BS_OWNERDRAW|WS_VISIBLE, 0, ID_ADDFILE, g.fontBold);
    g.removeFile = mk(hwnd, L"BUTTON", L"移除选中", WS_CHILD|BS_OWNERDRAW|WS_VISIBLE, 0, ID_REMOVEFILE, g.font);
    g.clearFile  = mk(hwnd, L"BUTTON", L"清空全部", WS_CHILD|BS_OWNERDRAW|WS_VISIBLE, 0, ID_CLEARFILE, g.font);

    /* 其他 */
    g.sendBtn = mk(hwnd, L"BUTTON", L"发送文件", WS_CHILD|BS_OWNERDRAW|WS_VISIBLE, 0, ID_SEND, g.fontBold);
    g.log     = mk(hwnd, L"EDIT", L"", WS_CHILD|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_VISIBLE,
                   WS_EX_CLIENTEDGE, ID_LOG, g.fontSmall);
    g.status  = mk(hwnd, L"STATIC", L"待机", WS_CHILD|SS_CENTER|WS_VISIBLE, 0, ID_STATUS, g.fontBold);
    g.count   = mk(hwnd, L"STATIC", L"设备 0 台 · 文件 0 个", WS_CHILD|SS_LEFT|WS_VISIBLE, 0, ID_COUNT, g.fontSmall);

    refresh_devs();
    set_status(0);
}

/* ---------- 布局 ---------- */
static void layout(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    int pad = S(20);
    int contentW = W - pad * 2;
    if (contentW < S(320)) contentW = S(320);
    int maxRight = W - pad;

    int y = pad;

    /* ============ 页头 ============ */
    int headerH = S(56);
    y += headerH;

    /* ============ 设备卡片 ============ */
    int cardH1 = S(300);
    int cardPad = S(16);
    int innerW = contentW - cardPad * 2;

    /* 刷新按钮：卡片右上角 */
    int refreshW = S(80), refreshH = S(28);
    MoveWindow(g.refreshBtn, maxRight - cardPad - refreshW, y + S(8), refreshW, refreshH, TRUE);

    /* 手动添加行 */
    int rowY = y + S(48);
    int rowH = S(36);
    int gap = S(8);
    int addBtnW = S(100);
    int portW = S(80);
    int ipW = innerW - addBtnW - portW - gap * 2;
    if (ipW < S(80)) { ipW = S(80); portW = S(64); addBtnW = innerW - ipW - portW - gap * 2; }
    MoveWindow(g.addIp,   pad + cardPad,           rowY, ipW, rowH, TRUE);
    MoveWindow(g.addPort, pad + cardPad + ipW + gap, rowY, portW, rowH, TRUE);
    MoveWindow(g.addBtn,  pad + cardPad + ipW + portW + gap * 2, rowY, addBtnW, rowH, TRUE);

    /* 设备列表 */
    int listTop = rowY + rowH + S(12);
    int listBottom = y + cardH1 - S(12);
    int listH = listBottom - listTop;
    if (listH < S(40)) listH = S(40);
    MoveWindow(g.devList, pad + cardPad, listTop, innerW, listH, TRUE);

    y += cardH1 + S(12);

    /* ============ 文件卡片 ============ */
    int cardH2 = S(260);

    /* 操作按钮行 */
    int by = y + S(48);
    int btnH = S(34);
    int b3w = S(92), b2w = S(92), b1w = innerW - b2w - b3w - gap * 2;
    if (b1w < S(96)) { b1w = S(96); b2w = (innerW - b1w - gap * 2) / 2; b3w = b2w; }
    MoveWindow(g.addFile,    pad + cardPad,                  by, b1w, btnH, TRUE);
    MoveWindow(g.removeFile,  pad + cardPad + b1w + gap,      by, b2w, btnH, TRUE);
    MoveWindow(g.clearFile,  pad + cardPad + b1w + b2w + gap*2, by, b3w, btnH, TRUE);

    /* 文件列表 */
    int flTop = by + btnH + S(10);
    int flBottom = y + cardH2 - S(36);
    int flH = flBottom - flTop;
    if (flH < S(40)) flH = S(40);
    MoveWindow(g.fileList, pad + cardPad, flTop, innerW, flH, TRUE);

    /* 底部 count */
    int bottomY = y + cardH2 - S(28);
    MoveWindow(g.count, pad + cardPad, bottomY, innerW, S(22), TRUE);

    y += cardH2 + S(12);

    /* ============ 发送卡片 ============ */
    int cardH3 = S(140);

    /* 自定义进度条绘制区域（由 on_paint 绘制，这里用一个隐藏按钮容器） */
    /* 发送按钮 */
    int sendBtnH = S(46);
    int sendBtnW = innerW;
    int sendBtnY = y + S(76);
    MoveWindow(g.sendBtn, pad + cardPad, sendBtnY, sendBtnW, sendBtnH, TRUE);

    y += cardH3 + S(12);

    /* ============ 日志卡片 ============ */
    int logTop = y;
    int logBottom = H - pad;
    int logH = logBottom - logTop;
    if (logH < S(60)) logH = S(60);
    MoveWindow(g.log, pad + cardPad, logTop + S(28), innerW, logH - S(28) - S(8), TRUE);
}

/* ---------- 绘制工具 ---------- */
static void fill_round_rect(HDC dc, RECT *r, int rad, COLORREF color) {
    HRGN rg = CreateRoundRectRgn(r->left, r->top, r->right+1, r->bottom+1, rad, rad);
    HBRUSH br = CreateSolidBrush(color);
    FillRgn(dc, rg, br);
    DeleteObject(br);
    DeleteObject(rg);
}

static void frame_round_rect(HDC dc, RECT *r, int rad, int bw, COLORREF color) {
    HRGN rg = CreateRoundRectRgn(r->left, r->top, r->right+1, r->bottom+1, rad, rad);
    HBRUSH br = CreateSolidBrush(color);
    FrameRgn(dc, rg, br, bw, bw);
    DeleteObject(br);
    DeleteObject(rg);
}

static void draw_text_in_rect(HDC dc, const wchar_t *text, RECT *r, UINT format, COLORREF color, HFONT font) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    HGDIOBJ of = SelectObject(dc, font);
    DrawTextW(dc, text, -1, r, format);
    SelectObject(dc, of);
}

/* ---------- 全新绘制函数 ---------- */

/* 顶部页头 */
static void draw_header(HDC dc, RECT *rc) {
    int pad = S(20);
    int titleH = S(32);
    RECT titleR = { pad, S(10), rc->right - pad - S(140), S(10) + titleH };
    draw_text_in_rect(dc, L"文件传输 · 发送", &titleR, DT_LEFT|DT_VCENTER|DT_SINGLELINE, COL_TEXT, g.fontTitle);

    RECT subR = { pad, S(10) + titleH + S(2), rc->right - pad, S(10) + titleH + S(2) + S(20) };
    draw_text_in_rect(dc, L"选择目标设备并发送文件", &subR, DT_LEFT|DT_VCENTER|DT_SINGLELINE, COL_SUB, g.fontSmall);
}

/* 卡片：带左侧 accent 条 + 柔和阴影 + 精致边框 */
static void draw_card(HDC dc, RECT *r, const wchar_t *title, const wchar_t *hint) {
    int rad = S(12);
    int accentW = S(4);

    /* 阴影层 */
    RECT shadow = { r->left + S(2), r->top + S(3), r->right + S(2), r->bottom + S(3) };
    fill_round_rect(dc, &shadow, rad, COL_SHADOW);

    /* 卡片主体 */
    RECT body = { r->left, r->top, r->right, r->bottom };
    fill_round_rect(dc, &body, rad, COL_SURFACE);

    /* 边框 */
    frame_round_rect(dc, &body, rad, 1, COL_BORDER);

    /* 左侧 accent 条 */
    RECT accent = { r->left + S(4), r->top + S(14), r->left + S(4) + accentW, r->bottom - S(14) };
    if (accent.bottom > accent.top) {
        fill_round_rect(dc, &accent, S(2), AC());
    }

    /* 标题 */
    RECT tr = { r->left + S(20), r->top + S(10), r->right - S(120), r->top + S(10) + S(28) };
    draw_text_in_rect(dc, title, &tr, DT_LEFT|DT_VCENTER|DT_SINGLELINE, COL_TEXT, g.fontBold);

    /* 副标题 */
    if (hint) {
        RECT hr = { r->left + S(20), r->top + S(10) + S(28), r->right - S(20), r->top + S(10) + S(28) + S(18) };
        draw_text_in_rect(dc, hint, &hr, DT_LEFT|DT_VCENTER|DT_SINGLELINE, COL_SUB, g.fontCaption);
    }
}

/* 自定义进度条 */
static void draw_progress_bar(HDC dc, RECT *r, int pct) {
    int rad = S(6);
    int h = r->bottom - r->top;
    if (h < 4) h = 4;

    /* 背景槽 */
    RECT bg = { r->left, r->top, r->right, r->top + h };
    fill_round_rect(dc, &bg, rad, COL_BORDER_SOFT);

    /* 填充 */
    if (pct > 0) {
        int fillW = (int)((r->right - r->left) * pct / 100.0f);
        if (fillW > 2) {
            RECT fill = { r->left, r->top, r->left + fillW, r->top + h };
            fill_round_rect(dc, &fill, rad, AC());
        }
    }

    /* 百分比文字 */
    wchar_t buf[16];
    _snwprintf_s(buf, 16, _TRUNCATE, L"%d%%", pct);
    RECT tr = { r->left, r->top - S(20), r->right, r->top };
    draw_text_in_rect(dc, buf, &tr, DT_RIGHT|DT_VCENTER|DT_SINGLELINE, COL_TEXT_2, g.fontSmall);
}

/* 状态徽章 */
static void draw_status_badge(HDC dc, RECT *r, const wchar_t *text, COLORREF color) {
    int rad = S(10);
    int padX = S(12), padY = S(4);
    RECT body = { r->left, r->top, r->right, r->bottom };
    fill_round_rect(dc, &body, rad, color);
    InflateRect(&body, -padX, -padY);
    draw_text_in_rect(dc, text, &body, DT_CENTER|DT_VCENTER|DT_SINGLELINE, RGB(255,255,255), g.fontSmall);
}

/* 按钮：三态 + 精致边框 */
static void draw_btn(LPDRAWITEMSTRUCT dis, const wchar_t *txt, int kind) {
    HDC dc = dis->hDC;
    RECT r = dis->rcItem;
    int rad = S(8);
    int hover = (dis->itemState & ODS_HOTLIGHT) ? 1 : 0;
    int pressed = (dis->itemState & ODS_SELECTED) ? 1 : 0;
    int disabled = (dis->itemState & ODS_DISABLED) ? 1 : 0;

    COLORREF bg, fg, bd;

    if (kind == 0) {
        /* 主按钮：accent 色 */
        bg = AC();
        fg = RGB(255,255,255);
        bd = AC();
        if (pressed) { bg = RGB(20,20,40); }
        else if (hover) { } /* 保持 */
        if (disabled) { bg = COL_BORDER; fg = COL_SUB; bd = COL_BORDER; }
    } else if (kind == 1) {
        /* 次按钮：ghost style */
        bg = COL_GHOST;
        fg = COL_TEXT;
        bd = COL_BORDER;
        if (hover) { bg = COL_LIST_HI; }
        if (disabled) { bg = COL_GHOST; fg = COL_SUB; bd = COL_BORDER_SOFT; }
    } else {
        /* 文字按钮 */
        bg = COL_SURFACE;
        fg = COL_TEXT_2;
        bd = COL_BORDER;
        if (hover) { bg = COL_SURFACE_HI; fg = COL_TEXT; }
        if (disabled) { fg = COL_SUB; }
    }

    RECT body = r;
    if (pressed) { OffsetRect(&body, 0, 1); }

    fill_round_rect(dc, &body, rad, bg);
    frame_round_rect(dc, &body, rad, 1, bd);

    /* 发光效果（仅主按钮 hover 时） */
    if (kind == 0 && hover && !disabled && !pressed) {
        RECT glow = body;
        InflateRect(&glow, 1, 1);
        frame_round_rect(dc, &glow, rad, 1, AC());
    }

    /* 文字 */
    RECT tr = body;
    InflateRect(&tr, -S(12), -S(4));
    draw_text_in_rect(dc, txt, &tr, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS, fg, g.fontBold);
}

/* ---------- 主绘制入口 ---------- */
static void on_paint(HWND hwnd) {
    PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);
    HDC screen = dc;

    HDC mem = CreateCompatibleDC(dc);
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = rc.right;
    bi.bmiHeader.biHeight = -rc.bottom;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bmpBits = NULL;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bmpBits, NULL, 0);
    HBITMAP ob = (HBITMAP)SelectObject(mem, bmp);
    dc = mem;

    /* 背景：渐变效果 */
    HBRUSH brBg = CreateSolidBrush(COL_BG);
    FillRect(dc, &rc, brBg);
    DeleteObject(brBg);

    /* 背景装饰：微妙的径向高亮 */
    {
        RECT glow = { -rc.right/4, -rc.bottom/4, rc.right/3, rc.bottom/3 };
        HBRUSH brGlow = CreateSolidBrush(g.dark ? RGB(30,32,58) : RGB(235,232,255));
        FillRect(dc, &glow, brGlow);
        DeleteObject(brGlow);
    }

    /* 页头 */
    draw_header(dc, &rc);

    int pad = S(20);
    int cardPad = S(16);
    int contentW = rc.right - pad * 2;

    int y = pad + S(56);

    /* ========== 卡片 1：附近设备 ========== */
    int cardH1 = S(300);
    RECT r1 = { pad, y, pad + contentW, y + cardH1 };
    draw_card(dc, &r1, L"附近设备", L"自动发现局域网设备，可手动添加 IP");

    /* 设备数量徽章 */
    wchar_t devBadge[32];
    _snwprintf_s(devBadge, 32, _TRUNCATE, L"%d 台", g.dev_count);
    RECT badgeR = { r1.right - S(60), r1.top + S(12), r1.right - S(12), r1.top + S(12) + S(24) };
    draw_status_badge(dc, &badgeR, devBadge, AC());

    y += cardH1 + S(12);

    /* ========== 卡片 2：待发送文件 ========== */
    int cardH2 = S(260);
    RECT r2 = { pad, y, pad + contentW, y + cardH2 };
    draw_card(dc, &r2, L"待发送文件", L"支持多选，点击文件列表中的条目");

    /* 文件数量徽章 */
    wchar_t fileBadge[32];
    _snwprintf_s(fileBadge, 32, _TRUNCATE, L"%d 个", g.file_count);
    RECT badgeR2 = { r2.right - S(60), r2.top + S(12), r2.right - S(12), r2.top + S(12) + S(24) };
    draw_status_badge(dc, &badgeR2, fileBadge, g.file_count > 0 ? COL_SUCCESS : COL_SUB);

    y += cardH2 + S(12);

    /* ========== 卡片 3：传输控制 ========== */
    int cardH3 = S(140);
    RECT r3 = { pad, y, pad + contentW, y + cardH3 };
    draw_card(dc, &r3, L"传输控制", L"确认设备与文件后点击发送");

    /* 自定义进度条 */
    int progY = y + S(50);
    int progH = S(10);
    RECT progR = { pad + cardPad, progY, pad + contentW - cardPad, progY + progH };
    draw_progress_bar(dc, &progR, g.progress_pct);

    y += cardH3 + S(12);

    /* ========== 卡片 4：传输日志 ========== */
    int logH = rc.bottom - y - pad;
    if (logH < S(60)) logH = S(60);
    RECT r4 = { pad, y, pad + contentW, y + logH };
    draw_card(dc, &r4, L"传输日志", L"实时显示传输状态");

    /* 状态徽章（卡片右上角） */
    wchar_t statusText[16];
    GetWindowTextW(g.status, statusText, 16);
    COLORREF statusColor = (COLORREF)GetWindowLongPtrW(g.status, GWLP_USERDATA);
    if (statusColor == 0) statusColor = COL_SUB;
    RECT badgeR4 = { r4.right - S(80), r4.top + S(12), r4.right - S(12), r4.top + S(12) + S(24) };
    draw_status_badge(dc, &badgeR4, statusText, statusColor);

    BitBlt(screen, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

/* ---------- 动画计时器 ---------- */
static void on_timer(void) {
    g.tick_anim++;
    if (g.sending) {
        /* 进度平滑更新 */
        RECT rc; GetClientRect(g.hwnd, &rc);
        InvalidateRect(g.hwnd, NULL, FALSE);
    }
}

/* ---------- 事件处理 ---------- */
static void on_send_ev(ev *e) {
    wchar_t line[1024];
    if (e->type == 0) {
        _snwprintf_s(line,1024,_TRUNCATE,L"▶ 开始: %s", e->fn?e->fn:L"", e->tot);
        append_log(g.log, line);
    } else if (e->type == 1) {
        g.progress_pct = e->pct;
        RECT rc; GetClientRect(g.hwnd, &rc);
        InvalidateRect(g.hwnd, NULL, FALSE);
    } else if (e->type == 2) {
        g.progress_pct = 100;
        _snwprintf_s(line,1024,_TRUNCATE,L"✔ 完成: %s (%lld B)", e->fn?e->fn:L"", e->tot);
        append_log(g.log, line);
        set_status(2);
        g.sending = 0;
        EnableWindow(g.sendBtn, TRUE);
        config_save(&g.cfg);
        MessageBoxW(g.hwnd, L"文件传输完成！", L"完成", MB_OK|MB_ICONINFORMATION);
        if (g.cfg.auto_cleanup_sent) {
            g.file_count = 0;
            SendMessageW(g.fileList, LB_RESETCONTENT, 0, 0);
            wchar_t cnt[128]; _snwprintf_s(cnt,128,_TRUNCATE,L"设备 %d 台 · 文件 0 个", g.dev_count);
            SetWindowTextW(g.count, cnt);
        }
    } else if (e->type == 3) {
        g.progress_pct = 0;
        _snwprintf_s(line,1024,_TRUNCATE,L"✖ 错误: %s - %s", e->fn?e->fn:L"", e->ex?e->ex:L"未知");
        append_log(g.log, line);
        set_status(3);
        g.sending = 0;
        EnableWindow(g.sendBtn, TRUE);
    }
}

static void on_command(int id) {
    switch (id) {
        case ID_REFRESH: discover_refresh(); refresh_devs(); break;
        case ID_ADDBTN: {
            wchar_t ipw[128]={0}, pw[32]={0};
            GetWindowTextW(g.addIp, ipw, 128);
            GetWindowTextW(g.addPort, pw, 32);
            if (!ipw[0]) { MessageBoxW(g.hwnd, L"请输入对方 IP", L"提示", MB_OK|MB_ICONINFORMATION); break; }
            int port = _wtoi(pw); if (port<=0||port>65535) port = FILE_PORT;
            char ipa[64]={0}; WideCharToMultiByte(CP_ACP, 0, ipw, -1, ipa, 64, NULL, NULL);
            if (g.manual_count < MAX_DEVICES) {
                ft_device *d = &g.manual[g.manual_count++];
                ZeroMemory(d, sizeof(*d));
                _snwprintf_s(d->name, MAX_NAME_LEN, _TRUNCATE, L"手动 (%s)", ipw);
                strncpy_s(d->ip, sizeof(d->ip), ipa, _TRUNCATE);
                d->port = port; d->last_seen = -1;
                SetWindowTextW(g.addIp, L"");
                refresh_devs();
            }
            break;
        }
        case ID_ADDFILE: pick_files(g.hwnd); break;
        case ID_REMOVEFILE: {
            int s = (int)SendMessageW(g.fileList, LB_GETCURSEL, 0, 0);
            if (s != LB_ERR) {
                SendMessageW(g.fileList, LB_DELETESTRING, s, 0);
                for (int i=s; i<g.file_count-1; i++) {
                    wcscpy_s(g.files[i], FT_MAX_PATH, g.files[i+1]);
                    wcscpy_s(g.names[i], FT_MAX_PATH, g.names[i+1]);
                }
                g.file_count--;
                wchar_t cnt[128]; _snwprintf_s(cnt,128,_TRUNCATE,L"设备 %d 台 · 文件 %d 个", g.dev_count, g.file_count);
                SetWindowTextW(g.count, cnt);
            }
            break;
        }
        case ID_CLEARFILE:
            g.file_count = 0;
            SendMessageW(g.fileList, LB_RESETCONTENT, 0, 0);
            { wchar_t cnt[128]; _snwprintf_s(cnt,128,_TRUNCATE,L"设备 %d 台 · 文件 0 个", g.dev_count);
              SetWindowTextW(g.count, cnt); }
            break;
        case ID_SEND: do_send(); break;
    }
}

/* ---------- 窗口过程 ---------- */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g.hwnd = hwnd;
            create_controls(hwnd, ((LPCREATESTRUCT)lp)->hInstance);
            BOOL dark = g.dark ? TRUE : FALSE;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            SetTimer(hwnd, TIMER_ANIM, ANIM_INTERVAL, NULL);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            MINMAXINFO *mm = (MINMAXINFO*)lp;
            if (mm->ptMinTrackSize.x < S(680)) mm->ptMinTrackSize.x = S(680);
            if (mm->ptMinTrackSize.y < S(520)) mm->ptMinTrackSize.y = S(520);
            return 0;
        }
        case WM_SIZE: layout(hwnd); InvalidateRect(hwnd, NULL, FALSE); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_TIMER:
            if (wp == TIMER_ANIM) { on_timer(); return 0; }
            break;
        case WM_PAINT: on_paint(hwnd); return 0;
        case WM_COMMAND: on_command(LOWORD(wp)); return 0;
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
            if (!dis) break;
            wchar_t txt[128]={0}; GetWindowTextW(dis->hwndItem, txt, 128);
            int kind = 2; /* 默认文字按钮 */
            if (dis->CtlID == ID_SEND) kind = 0;
            else if (dis->CtlID == ID_ADDBTN || dis->CtlID == ID_ADDFILE) kind = 0;
            else if (dis->CtlID == ID_REFRESH) kind = 2;
            else if (dis->CtlID == ID_REMOVEFILE || dis->CtlID == ID_CLEARFILE) kind = 1;
            draw_btn(dis, txt, kind);
            return TRUE;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wp; HWND child = (HWND)lp;
            SetBkMode(dc, TRANSPARENT);
            if (child == g.status) {
                SetTextColor(dc, (COLORREF)GetWindowLongPtrW(child, GWLP_USERDATA));
            } else {
                SetTextColor(dc, COL_SUB);
            }
            return (LRESULT)g.brBg;
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wp;
            if ((HWND)lp == g.log) {
                SetTextColor(dc, COL_TEXT); SetBkColor(dc, COL_LIST_BG);
                return (LRESULT)g.brList;
            }
            SetTextColor(dc, COL_TEXT); SetBkColor(dc, COL_INPUT_BG);
            return (LRESULT)g.brInput;
        }
        case WM_CTLCOLORLISTBOX: {
            HDC dc = (HDC)wp;
            SetTextColor(dc, COL_TEXT); SetBkColor(dc, COL_LIST_BG);
            return (LRESULT)g.brList;
        }
        case WM_NOTIFY: {
            NMHDR *hdr = (NMHDR*)lp;
            if (hdr && hdr->idFrom == ID_DEVLIST && hdr->code == LVN_ITEMCHANGED) {
                LRESULT sel = ListView_GetNextItem(g.devList, -1, LVNI_SELECTED);
                g.selected_dev = (int)sel;
            }
            if (hdr && hdr->idFrom == ID_DEVLIST && hdr->code == NM_CUSTOMDRAW) {
                LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lp;
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    cd->clrText = COL_TEXT;
                    cd->clrTextBk = ((int)cd->nmcd.dwItemSpec == g.selected_dev) ? COL_LIST_SEL : COL_LIST_BG;
                    return CDRF_NEWFONT;
                }
            }
            return 0;
        }
        case WM_DEVUPD: refresh_devs(); return 0;
        case WM_SENDEV: {
            ev *e = (ev*)lp;
            if (e) { on_send_ev(e); free(e->fn); free(e->ex); free(e); }
            return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd, TIMER_ANIM);
            discover_shutdown();
            ft_send_shutdown();
            ft_recv_stop();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------- 入口 ---------- */
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE prev, LPWSTR cmd, int show) {
    (void)prev;
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    wchar_t appdata[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata) == S_OK) {
        wchar_t d[MAX_PATH]; _snwprintf_s(d, MAX_PATH, _TRUNCATE, L"%s\\My File Transfer Assistant", appdata);
        CreateDirectoryW(d, NULL); log_init(d);
    }

    config_load(&g.cfg);

    discover_init(g.cfg.nickname[0] ? g.cfg.nickname : L"未命名设备", g.cfg.tcp_port);
    discover_set_callback(dev_cb);
    ft_send_init();
    ft_send_set_callback(send_cb);

    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(cmd, &argc);
    if (argv) {
        for (int i = 0; i < argc; i++) {
            if (argv[i][0] && wcscmp(argv[i], L"--send") != 0) {
                add_file(argv[i]);
            }
        }
        LocalFree(argv);
    }

    const wchar_t *cls = L"FileSenderModule";
    g.ui_scale = compute_scale();
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(g.dark ? RGB(16,18,30) : RGB(245,247,251));
    wc.lpszClassName = cls;
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = (int)(sw*0.52), wh = (int)(sh*0.75);
    if (ww < S(720)) ww = sw - S(60);
    if (wh < S(560)) wh = sh - S(60);
    HWND hwnd = CreateWindowExW(0, cls, L"文件传输 · 发送",
        WS_OVERLAPPEDWINDOW|WS_THICKFRAME,
        (sw-ww)/2, (sh-wh)/2, ww, wh, NULL, NULL, hi, NULL);
    if (!hwnd) return 0;

    HICON big = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    if (big) { SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)big); SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)big); }

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    layout(hwnd);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) { TranslateMessage(&m); DispatchMessageW(&m); }
    CoUninitialize();
    return (int)m.wParam;
}