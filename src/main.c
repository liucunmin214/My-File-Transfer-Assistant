/*
 * main.c - MyFileTransferAssistant GUI 主程序（Win32 原生）
 * 页面：
 *   发送端：附近设备列表 / 刷新按钮 / 选择文件按钮 / 发送按钮 / 发送进度
 *   接收端：接收开关 / 保存目录 / 接收历史 / 接收进度
 *   设置：  昵称 / 端口 / 保存目录 / 开机自启 / 自动接收 / 注册右键菜单 / 深色主题
 *   关于
 * 命令行参数：
 *   --send "path" ：启动后直接进入发送端，预置 path 到文件列表（右键「发送到附近设备」用）
 *   --tray ：隐藏主窗口，托盘常驻
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00   /* Win10+ 以便 DWM 云母 */
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <dwmapi.h>           /* DWM 玻璃/云母 */
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
#include "registry.h"
#include "upgrade.h"

#pragma comment(lib, "dwmapi.lib")      /* DWM 玻璃/云母 */
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "sync_core.lib")

/* ---------- DWM 云母/亚克力常量（避免版本缺失） ---------- */
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE   20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE      38
#endif
#ifndef DWMSBT_AUTO
#define DWMSBT_AUTO          0
#define DWMSBT_NONE          1
#define DWMSBT_MAINWINDOW    2     /* Mica */
#define DWMSBT_TRANSIENTWINDOW 3   /* MicaAlt */
#define DWMSBT_TABBEDWINDOW  4     /* Acrylic-like Alt */
#endif
#ifndef DWMWA_MICA_EFFECT
#define DWMWA_MICA_EFFECT 1029
#endif

/* ---------- SetWindowCompositionAttribute（用户区合成：亚克力模糊） ---------- */
typedef enum {
    WCA_ACCENT_POLICY = 19
} WINDOWCOMPOSITIONATTRIB;

typedef enum {
    ACCENT_DISABLED                   = 0,
    ACCENT_ENABLE_GRADIENT            = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND          = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND   = 4,
    ACCENT_INVALID_STATE              = 5
} ACCENT_STATE;

typedef struct {
    ACCENT_STATE AccentState;
    DWORD        AccentFlags;
    DWORD        GradientColor;   /* 0xAABBGGRR */
    DWORD        AnimationId;
} ACCENT_POLICY;

typedef struct {
    WINDOWCOMPOSITIONATTRIB Attribute;
    PVOID                   Data;
    ULONG                   DataSize;
} WINDOWCOMPOSITIONATTRIBDATA;

typedef BOOL (WINAPI *PFN_SET_WINDOW_COMPOSITION_ATTRIBUTE)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

/* ---------- 主题系统：6 主题 × 浅/深 共 12 套完整配色 ---------- */
typedef struct {
    COLORREF sidebar, content, card, card_bd, card_gloss;
    COLORREF text, sub, muted;
    COLORREF btn, btn_hov, btn_prs, btn_ghost, btn_ghost_hov;
    COLORREF btn_grn, btn_red;
    COLORREF accent, accent_soft;
    COLORREF log_bg, log_fg;
    COLORREF input_bg, input_bd;
    COLORREF list_bg, list_sel;
    COLORREF nav_active_bg, nav_hov_bg;
    COLORREF divider;
    COLORREF sidebar_text, sidebar_sub, sidebar_ver, nav_text;
} theme_colors_t;

#define NUM_THEMES 12
#define THEME_MODEES 2

static const wchar_t *theme_names[NUM_THEMES] = {
    L"靛紫", L"深绿", L"樱花", L"海蓝", L"日落橙", L"青碧",
    L"玫瑰金", L"暗夜紫", L"极光", L"薄荷", L"琥珀", L"炭黑"
};

static const theme_colors_t theme_data[NUM_THEMES][THEME_MODEES] = {
    { /* ===== 靛紫 (Indigo) ===== */
        /* Light */
        { RGB(246,245,252), RGB(250,250,252), RGB(255,255,255), RGB(228,226,238), RGB(139,124,255),
          RGB(26,28,44), RGB(108,108,128), RGB(150,150,170),
          RGB(92,70,255), RGB(113,94,255), RGB(76,57,217), RGB(238,236,248), RGB(225,222,242),
          RGB(34,197,94), RGB(239,68,68),
          RGB(92,70,255), RGB(197,187,253),
          RGB(243,242,250), RGB(56,58,82),
          RGB(255,255,255), RGB(212,210,224),
          RGB(255,255,255), RGB(232,227,255),
          RGB(226,220,250), RGB(236,232,248),
          RGB(232,230,244),
          RGB(36,34,54), RGB(128,124,150), RGB(160,158,178), RGB(86,82,110) },
        /* Dark */
        { RGB(24,25,42), RGB(20,21,36), RGB(32,34,54), RGB(55,57,82), RGB(139,124,255),
          RGB(232,232,240), RGB(168,168,190), RGB(120,120,145),
          RGB(139,124,255), RGB(163,149,255), RGB(119,104,245), RGB(45,47,72), RGB(58,60,88),
          RGB(52,211,153), RGB(248,113,113),
          RGB(139,124,255), RGB(99,102,241),
          RGB(14,15,28), RGB(210,210,225),
          RGB(28,30,50), RGB(60,62,88),
          RGB(28,30,50), RGB(60,55,120),
          RGB(55,48,108), RGB(42,40,72),
          RGB(50,52,78),
          RGB(232,230,245), RGB(148,146,172), RGB(100,98,125), RGB(200,198,220) }
    },
    { /* ===== 深绿 (Forest Green) ===== */
        /* Light */
        { RGB(240,248,242), RGB(245,250,246), RGB(255,255,255), RGB(210,228,215), RGB(34,197,94),
          RGB(25,42,30), RGB(90,120,100), RGB(140,160,145),
          RGB(22,163,74), RGB(34,197,94), RGB(21,128,61), RGB(232,245,236), RGB(220,238,225),
          RGB(34,197,94), RGB(239,68,68),
          RGB(22,163,74), RGB(187,247,208),
          RGB(235,244,238), RGB(50,70,55),
          RGB(255,255,255), RGB(200,220,205),
          RGB(255,255,255), RGB(220,252,231),
          RGB(220,252,231), RGB(232,245,236),
          RGB(225,238,228),
          RGB(30,50,35), RGB(100,125,105), RGB(150,170,155), RGB(70,95,80) },
        /* Dark */
        { RGB(20,35,25), RGB(16,30,22), RGB(25,42,30), RGB(50,70,55), RGB(74,222,128),
          RGB(230,240,232), RGB(160,180,165), RGB(115,135,120),
          RGB(74,222,128), RGB(134,239,172), RGB(34,197,94), RGB(40,60,45), RGB(52,75,58),
          RGB(74,222,128), RGB(248,113,113),
          RGB(74,222,128), RGB(34,197,94),
          RGB(12,24,16), RGB(200,220,205),
          RGB(22,38,27), RGB(50,72,55),
          RGB(22,38,27), RGB(35,80,50),
          RGB(25,70,35), RGB(35,55,42),
          RGB(42,62,48),
          RGB(230,240,232), RGB(155,175,160), RGB(110,130,115), RGB(190,210,195) }
    },
    { /* ===== 樱花 (Sakura) ===== */
        /* Light */
        { RGB(252,244,246), RGB(253,246,248), RGB(255,255,255), RGB(235,215,220), RGB(244,114,182),
          RGB(50,32,42), RGB(130,100,115), RGB(170,150,160),
          RGB(225,29,72), RGB(244,63,94), RGB(190,18,60), RGB(250,235,240), RGB(245,222,230),
          RGB(34,197,94), RGB(239,68,68),
          RGB(225,29,72), RGB(254,205,211),
          RGB(248,238,242), RGB(70,45,55),
          RGB(255,255,255), RGB(225,205,212),
          RGB(255,255,255), RGB(254,226,226),
          RGB(254,226,226), RGB(250,235,240),
          RGB(240,222,228),
          RGB(55,35,45), RGB(140,110,125), RGB(175,155,165), RGB(95,75,90) },
        /* Dark */
        { RGB(38,22,32), RGB(32,18,27), RGB(45,28,38), RGB(70,50,60), RGB(251,113,133),
          RGB(240,232,236), RGB(175,155,165), RGB(130,110,120),
          RGB(251,113,133), RGB(253,164,175), RGB(244,63,94), RGB(55,38,48), RGB(68,50,60),
          RGB(52,211,153), RGB(248,113,113),
          RGB(251,113,133), RGB(244,63,94),
          RGB(22,14,20), RGB(215,200,210),
          RGB(38,24,33), RGB(62,42,52),
          RGB(38,24,33), RGB(90,35,55),
          RGB(85,30,55), RGB(60,35,50),
          RGB(55,38,48),
          RGB(240,232,236), RGB(165,148,158), RGB(125,108,118), RGB(205,190,198) }
    },
    { /* ===== 海蓝 (Ocean Blue) ===== */
        /* Light */
        { RGB(242,246,252), RGB(246,249,253), RGB(255,255,255), RGB(215,225,240), RGB(59,130,246),
          RGB(25,38,55), RGB(95,115,140), RGB(145,160,180),
          RGB(37,99,235), RGB(59,130,246), RGB(29,78,216), RGB(232,240,250), RGB(220,232,246),
          RGB(34,197,94), RGB(239,68,68),
          RGB(37,99,235), RGB(147,197,253),
          RGB(237,243,250), RGB(50,68,90),
          RGB(255,255,255), RGB(205,220,235),
          RGB(255,255,255), RGB(219,234,254),
          RGB(219,234,254), RGB(232,240,250),
          RGB(228,236,246),
          RGB(30,42,60), RGB(105,122,145), RGB(155,170,190), RGB(75,95,120) },
        /* Dark */
        { RGB(20,28,45), RGB(16,22,38), RGB(25,34,52), RGB(50,65,90), RGB(96,165,250),
          RGB(230,235,245), RGB(160,175,195), RGB(115,130,150),
          RGB(96,165,250), RGB(125,211,252), RGB(59,130,246), RGB(38,50,72), RGB(50,65,90),
          RGB(52,211,153), RGB(248,113,113),
          RGB(96,165,250), RGB(59,130,246),
          RGB(12,18,32), RGB(205,215,230),
          RGB(22,30,48), RGB(48,62,85),
          RGB(22,30,48), RGB(30,58,138),
          RGB(30,58,138), RGB(38,50,72),
          RGB(42,55,78),
          RGB(230,235,245), RGB(155,170,192), RGB(108,125,148), RGB(195,205,220) }
    },
    { /* ===== 日落橙 (Sunset Orange) ===== */
        /* Light */
        { RGB(252,247,240), RGB(253,249,242), RGB(255,255,255), RGB(235,222,200), RGB(249,115,22),
          RGB(55,40,25), RGB(125,105,80), RGB(170,155,135),
          RGB(234,88,12), RGB(249,115,22), RGB(194,65,12), RGB(250,242,230), RGB(245,233,215),
          RGB(34,197,94), RGB(239,68,68),
          RGB(234,88,12), RGB(254,215,170),
          RGB(248,242,232), RGB(75,55,35),
          RGB(255,255,255), RGB(225,210,185),
          RGB(255,255,255), RGB(254,237,213),
          RGB(254,237,213), RGB(250,242,230),
          RGB(240,228,210),
          RGB(60,42,28), RGB(135,115,90), RGB(175,160,140), RGB(100,82,60) },
        /* Dark */
        { RGB(35,25,18), RGB(30,22,16), RGB(42,32,24), RGB(65,50,38), RGB(251,146,60),
          RGB(240,232,222), RGB(175,160,145), RGB(130,118,105),
          RGB(251,146,60), RGB(253,186,116), RGB(249,115,22), RGB(55,42,32), RGB(68,52,40),
          RGB(52,211,153), RGB(248,113,113),
          RGB(251,146,60), RGB(249,115,22),
          RGB(20,15,10), RGB(215,200,185),
          RGB(35,27,20), RGB(58,45,34),
          RGB(35,27,20), RGB(110,50,25),
          RGB(100,45,25), RGB(75,55,40),
          RGB(55,42,32),
          RGB(240,232,222), RGB(165,150,135), RGB(125,112,100), RGB(205,190,175) }
    },
    { /* ===== 青碧 (Teal) ===== */
        /* Light */
        { RGB(242,250,248), RGB(246,252,250), RGB(255,255,255), RGB(215,232,228), RGB(20,184,166),
          RGB(25,42,40), RGB(95,120,118), RGB(145,160,158),
          RGB(13,148,136), RGB(20,184,166), RGB(15,118,110), RGB(232,245,242), RGB(220,238,234),
          RGB(34,197,94), RGB(239,68,68),
          RGB(13,148,136), RGB(153,246,228),
          RGB(237,246,244), RGB(50,72,68),
          RGB(255,255,255), RGB(205,222,218),
          RGB(255,255,255), RGB(204,251,241),
          RGB(204,251,241), RGB(232,245,242),
          RGB(228,240,236),
          RGB(30,48,45), RGB(105,125,122), RGB(155,170,168), RGB(78,100,98) },
        /* Dark */
        { RGB(18,32,30), RGB(14,26,24), RGB(22,38,36), RGB(45,65,62), RGB(45,212,191),
          RGB(230,240,238), RGB(160,178,175), RGB(115,132,130),
          RGB(45,212,191), RGB(94,234,212), RGB(20,184,166), RGB(36,55,52), RGB(48,70,66),
          RGB(52,211,153), RGB(248,113,113),
          RGB(45,212,191), RGB(20,184,166),
          RGB(10,20,18), RGB(205,220,218),
          RGB(20,34,32), RGB(44,64,60),
          RGB(20,34,32), RGB(19,78,74),
          RGB(19,78,74), RGB(36,55,52),
          RGB(40,60,56),
          RGB(230,240,238), RGB(155,175,172), RGB(108,125,122), RGB(195,210,208) }
    },
    { /* ===== 玫瑰金 (Rose Gold) ===== */
        /* Light */
        { RGB(252,245,248), RGB(253,247,250), RGB(255,255,255), RGB(235,220,228), RGB(236,72,153),
          RGB(50,30,40), RGB(130,105,118), RGB(170,150,160),
          RGB(219,39,119), RGB(236,72,153), RGB(190,24,93), RGB(252,238,244), RGB(248,225,236),
          RGB(34,197,94), RGB(239,68,68),
          RGB(219,39,119), RGB(251,207,232),
          RGB(248,240,244), RGB(70,45,55),
          RGB(255,255,255), RGB(225,205,218),
          RGB(255,255,255), RGB(252,231,243),
          RGB(252,231,243), RGB(248,240,244),
          RGB(242,228,235),
          RGB(55,35,45), RGB(135,112,125), RGB(175,155,165), RGB(95,75,88) },
        /* Dark */
        { RGB(35,22,30), RGB(28,18,25), RGB(42,28,36), RGB(65,45,55), RGB(244,114,182),
          RGB(242,228,236), RGB(175,155,168), RGB(128,110,120),
          RGB(244,114,182), RGB(251,207,232), RGB(236,72,153), RGB(55,38,48), RGB(68,48,58),
          RGB(52,211,153), RGB(248,113,113),
          RGB(244,114,182), RGB(236,72,153),
          RGB(20,14,18), RGB(218,200,212),
          RGB(38,26,33), RGB(62,42,52),
          RGB(38,26,33), RGB(100,40,70),
          RGB(95,35,65), RGB(60,38,48),
          RGB(55,38,45),
          RGB(242,228,236), RGB(168,148,158), RGB(125,108,118), RGB(208,188,198) }
    },
    { /* ===== 暗夜紫 (Midnight Purple) ===== */
        /* Light */
        { RGB(248,244,252), RGB(250,246,254), RGB(255,255,255), RGB(230,222,242), RGB(147,51,234),
          RGB(30,20,45), RGB(110,95,135), RGB(150,135,168),
          RGB(126,34,206), RGB(147,51,234), RGB(109,40,217), RGB(242,236,250), RGB(232,222,245),
          RGB(34,197,94), RGB(239,68,68),
          RGB(126,34,206), RGB(216,180,254),
          RGB(244,238,250), RGB(55,40,75),
          RGB(255,255,255), RGB(222,210,240),
          RGB(255,255,255), RGB(237,233,254),
          RGB(237,233,254), RGB(242,236,250),
          RGB(234,228,245),
          RGB(35,25,50), RGB(120,105,140), RGB(155,142,172), RGB(80,68,100) },
        /* Dark */
        { RGB(28,20,42), RGB(22,16,34), RGB(35,25,50), RGB(55,42,72), RGB(167,139,250),
          RGB(234,228,244), RGB(168,158,185), RGB(122,112,140),
          RGB(167,139,250), RGB(196,181,253), RGB(147,51,234), RGB(45,35,60), RGB(58,45,75),
          RGB(52,211,153), RGB(248,113,113),
          RGB(167,139,250), RGB(147,51,234),
          RGB(16,12,24), RGB(215,205,230),
          RGB(32,24,42), RGB(55,42,68),
          RGB(32,24,42), RGB(60,35,95),
          RGB(65,40,100), RGB(45,35,60),
          RGB(50,42,65),
          RGB(234,228,244), RGB(162,152,178), RGB(118,108,130), RGB(200,190,218) }
    },
    { /* ===== 极光 (Aurora) ===== */
        /* Light */
        { RGB(242,250,250), RGB(246,252,252), RGB(255,255,255), RGB(215,235,235), RGB(6,182,212),
          RGB(20,40,42), RGB(90,120,122), RGB(145,165,168),
          RGB(8,145,178), RGB(6,182,212), RGB(14,116,144), RGB(232,248,248), RGB(220,238,238),
          RGB(34,197,94), RGB(239,68,68),
          RGB(8,145,178), RGB(165,243,252),
          RGB(237,246,246), RGB(45,70,72),
          RGB(255,255,255), RGB(205,228,228),
          RGB(255,255,255), RGB(207,250,254),
          RGB(207,250,254), RGB(232,248,248),
          RGB(226,240,240),
          RGB(25,48,50), RGB(100,128,130), RGB(155,172,175), RGB(72,100,102) },
        /* Dark */
        { RGB(15,30,32), RGB(10,24,26), RGB(20,35,38), RGB(42,62,65), RGB(34,211,238),
          RGB(228,240,240), RGB(158,178,180), RGB(112,132,135),
          RGB(34,211,238), RGB(103,232,249), RGB(6,182,212), RGB(32,52,55), RGB(45,65,68),
          RGB(52,211,153), RGB(248,113,113),
          RGB(34,211,238), RGB(6,182,212),
          RGB(8,18,20), RGB(205,222,225),
          RGB(20,35,38), RGB(42,62,65),
          RGB(20,35,38), RGB(14,90,105),
          RGB(14,90,105), RGB(32,52,55),
          RGB(38,55,58),
          RGB(228,240,240), RGB(152,172,175), RGB(108,128,130), RGB(195,210,212) }
    },
    { /* ===== 薄荷 (Mint) ===== */
        /* Light */
        { RGB(244,252,248), RGB(248,254,250), RGB(255,255,255), RGB(220,240,230), RGB(16,185,129),
          RGB(20,40,30), RGB(85,115,100), RGB(140,160,150),
          RGB(5,150,105), RGB(16,185,129), RGB(4,120,87), RGB(232,248,240), RGB(220,238,228),
          RGB(34,197,94), RGB(239,68,68),
          RGB(5,150,105), RGB(167,243,208),
          RGB(238,246,242), RGB(45,68,55),
          RGB(255,255,255), RGB(210,232,222),
          RGB(255,255,255), RGB(209,250,229),
          RGB(209,250,229), RGB(232,248,240),
          RGB(228,240,234),
          RGB(25,48,38), RGB(95,125,108), RGB(150,168,158), RGB(68,98,82) },
        /* Dark */
        { RGB(16,28,22), RGB(10,22,16), RGB(20,35,28), RGB(42,60,48), RGB(52,211,153),
          RGB(228,240,234), RGB(158,178,168), RGB(112,132,120),
          RGB(52,211,153), RGB(110,231,183), RGB(16,185,129), RGB(35,52,42), RGB(48,65,55),
          RGB(52,211,153), RGB(248,113,113),
          RGB(52,211,153), RGB(16,185,129),
          RGB(8,16,12), RGB(208,222,218),
          RGB(20,35,28), RGB(45,62,50),
          RGB(20,35,28), RGB(16,80,55),
          RGB(16,80,55), RGB(35,52,42),
          RGB(40,55,46),
          RGB(228,240,234), RGB(155,172,162), RGB(108,125,115), RGB(195,208,200) }
    },
    { /* ===== 琥珀 (Amber) ===== */
        /* Light */
        { RGB(253,250,242), RGB(254,252,245), RGB(255,255,255), RGB(238,228,200), RGB(245,158,11),
          RGB(50,38,20), RGB(120,105,75), RGB(165,150,120),
          RGB(217,119,6), RGB(245,158,11), RGB(180,83,9), RGB(252,244,228), RGB(245,233,205),
          RGB(34,197,94), RGB(239,68,68),
          RGB(217,119,6), RGB(254,215,170),
          RGB(250,244,232), RGB(70,55,30),
          RGB(255,255,255), RGB(225,210,180),
          RGB(255,255,255), RGB(254,243,199),
          RGB(254,243,199), RGB(252,244,228),
          RGB(244,234,214),
          RGB(55,42,25), RGB(130,115,85), RGB(170,158,130), RGB(92,80,55) },
        /* Dark */
        { RGB(32,26,16), RGB(26,20,12), RGB(40,32,20), RGB(62,50,32), RGB(251,191,36),
          RGB(242,234,218), RGB(175,162,138), RGB(128,118,95),
          RGB(251,191,36), RGB(253,224,71), RGB(245,158,11), RGB(52,42,28), RGB(65,52,38),
          RGB(52,211,153), RGB(248,113,113),
          RGB(251,191,36), RGB(245,158,11),
          RGB(18,14,8), RGB(218,208,188),
          RGB(35,28,18), RGB(58,48,32),
          RGB(35,28,18), RGB(95,65,20),
          RGB(85,60,25), RGB(52,42,28),
          RGB(50,42,28),
          RGB(242,234,218), RGB(168,155,130), RGB(125,115,92), RGB(205,195,175) }
    },
    { /* ===== 炭黑 (Carbon) ===== */
        /* Light */
        { RGB(246,246,248), RGB(248,248,250), RGB(255,255,255), RGB(225,225,232), RGB(99,102,103),
          RGB(28,28,34), RGB(95,95,108), RGB(145,145,158),
          RGB(64,64,70), RGB(82,82,90), RGB(50,50,56), RGB(235,235,240), RGB(225,225,232),
          RGB(34,197,94), RGB(239,68,68),
          RGB(64,64,70), RGB(200,200,208),
          RGB(242,242,245), RGB(55,55,65),
          RGB(255,255,255), RGB(218,218,225),
          RGB(255,255,255), RGB(232,232,238),
          RGB(232,232,238), RGB(238,238,242),
          RGB(230,230,235),
          RGB(30,30,36), RGB(100,100,112), RGB(148,148,160), RGB(72,72,82) },
        /* Dark */
        { RGB(18,18,22), RGB(14,14,18), RGB(24,24,28), RGB(45,45,52), RGB(160,160,170),
          RGB(232,232,238), RGB(160,160,172), RGB(112,112,125),
          RGB(160,160,170), RGB(180,180,190), RGB(120,120,130), RGB(35,35,42), RGB(48,48,55),
          RGB(52,211,153), RGB(248,113,113),
          RGB(160,160,170), RGB(120,120,130),
          RGB(10,10,14), RGB(210,210,220),
          RGB(28,28,34), RGB(50,50,58),
          RGB(28,28,34), RGB(55,55,65),
          RGB(55,55,65), RGB(35,35,42),
          RGB(42,42,48),
          RGB(232,232,238), RGB(155,155,168), RGB(108,108,120), RGB(195,195,205) }
    }
};

#define THEME(id, dark, field) (theme_data[(id)][(dark)].field)
#define COL_SIDEBAR     THEME(g.theme_id, g.dark_theme, sidebar)
#define COL_CONTENT     THEME(g.theme_id, g.dark_theme, content)
#define COL_CARD        THEME(g.theme_id, g.dark_theme, card)
#define COL_CARD_BD     (g.dark_theme ? THEME(g.theme_id,1,card_bd) : RGB(186,183,199))
#define COL_CARD_GLOSS  THEME(g.theme_id, g.dark_theme, card_gloss)
/* 浅色模式统一覆盖：柔和深字 + 清晰描边（12 套主题共用；深色模式保持主题自带浅色文字）
 * 文字用深板岩色 RGB(45,49,62) 而非近黑：既保证清晰可读，又避免"纯黑字贴白底"的刺眼感
 * （也不能用纯黑 RGB(0,0,0)：亚克力的"黑=透明"机制会把纯黑像素变透明） */
#define COL_TEXT        (g.dark_theme ? THEME(g.theme_id,1,text) : RGB(45,49,62))
#define COL_SUB         (g.dark_theme ? THEME(g.theme_id,1,sub)   : RGB(72,72,84))
#define COL_MUTED       (g.dark_theme ? THEME(g.theme_id,1,muted) : RGB(112,112,124))
#define COL_BTN         THEME(g.theme_id, g.dark_theme, btn)
#define COL_BTN_HOV     THEME(g.theme_id, g.dark_theme, btn_hov)
#define COL_BTN_PRS     THEME(g.theme_id, g.dark_theme, btn_prs)
#define COL_BTN_GHOST   THEME(g.theme_id, g.dark_theme, btn_ghost)
#define COL_BTN_GHOST_HOV THEME(g.theme_id, g.dark_theme, btn_ghost_hov)
#define COL_BTN_GRN     THEME(g.theme_id, g.dark_theme, btn_grn)
#define COL_BTN_RED     THEME(g.theme_id, g.dark_theme, btn_red)
#define COL_ACCENT      THEME(g.theme_id, g.dark_theme, accent)
#define COL_ACCENT_SOFT THEME(g.theme_id, g.dark_theme, accent_soft)
#define COL_LOG_BG      THEME(g.theme_id, g.dark_theme, log_bg)
#define COL_LOG_FG      THEME(g.theme_id, g.dark_theme, log_fg)
#define COL_INPUT_BG    THEME(g.theme_id, g.dark_theme, input_bg)
#define COL_INPUT_BD    THEME(g.theme_id, g.dark_theme, input_bd)
#define COL_LIST_BG     THEME(g.theme_id, g.dark_theme, list_bg)
#define COL_LIST_SEL    THEME(g.theme_id, g.dark_theme, list_sel)
#define COL_NAV_ACTIVE_BG THEME(g.theme_id, g.dark_theme, nav_active_bg)
#define COL_NAV_HOV_BG  THEME(g.theme_id, g.dark_theme, nav_hov_bg)
#define COL_DIVIDER     THEME(g.theme_id, g.dark_theme, divider)
#define COL_SIDEBAR_TEXT (g.dark_theme ? THEME(g.theme_id,1,sidebar_text) : RGB(45,49,62))
#define COL_SIDEBAR_SUB  (g.dark_theme ? THEME(g.theme_id,1,sidebar_sub)  : RGB(112,112,124))
#define COL_SIDEBAR_VER  (g.dark_theme ? THEME(g.theme_id,1,sidebar_ver)  : RGB(140,140,152))
#define COL_NAV_TEXT     (g.dark_theme ? THEME(g.theme_id,1,nav_text)     : RGB(45,49,62))

/* 状态颜色（统一语义色，不随主题变化太多） */
#define COL_GREEN       RGB(34, 197, 94)
#define COL_AMBER       RGB(245, 158, 11)
#define COL_RED         RGB(239, 68, 68)
#define COL_BLUE        RGB(59, 130, 246)

/* ---------- 控件 ID ---------- */
enum {
    ID_NAV_SEND = 100, ID_NAV_RECV, ID_NAV_SETTINGS, ID_NAV_ABOUT,

    /* 发送端 */
    ID_SEND_REFRESH, ID_SEND_DEVLIST, ID_SEND_PICK, ID_SEND_REMOVE,
    ID_SEND_CLEAR, ID_SEND_FILELIST, ID_SEND_COUNT,
    ID_SEND_START, ID_SEND_PROGRESS, ID_SEND_LOG, ID_SEND_STATUS,
    ID_SEND_IP, ID_SEND_DEVID, ID_SEND_ADDDEV,

    /* 接收端 */
    ID_RECV_TOGGLE, ID_RECV_BROWSE, ID_RECV_DIR, ID_RECV_PORT,
    ID_RECV_LIST, ID_RECV_LOG, ID_RECV_IP, ID_RECV_STATUS,

    /* 设置 */
    ID_SET_NICK, ID_SET_TCPPORT, ID_SET_UDPPORT,
    ID_SET_SAVEDIR, ID_SET_SAVEBROWSE,
    ID_SET_AUTOSTART, ID_SET_AUTOACCEPT, ID_SET_NOTIFY, ID_SET_AUTODISC,
    ID_SET_DARKMODE, ID_SET_REG_MENU, ID_SET_UNREG_MENU,
    ID_SET_ACCENT0, ID_SET_ACCENT1, ID_SET_ACCENT2,
    ID_SET_ACCENT3, ID_SET_ACCENT4, ID_SET_ACCENT5,
    ID_SET_ACCENT6, ID_SET_ACCENT7, ID_SET_ACCENT8,
    ID_SET_ACCENT9, ID_SET_ACCENT10, ID_SET_ACCENT11,
    ID_SET_ACRYLIC, ID_SET_OPACITY, ID_SET_BLUR,
    /* 高级设置切换开关 */
    ID_SET_TRAY, ID_SET_CONFIRMIT, ID_SET_SOUND,
    ID_SET_SPEED, ID_SET_AUTOCLEAN,
    /* 软件升级 */
    ID_SET_AUTOUPD, ID_SET_CHECKUPD,
    ID_SET_SAVE, ID_SET_STATUSMSG
};

#define WM_LOG       (WM_APP + 1)
#define WM_DISCOVER  (WM_APP + 2)
#define WM_SENDEV    (WM_APP + 3)
#define WM_RECVEV    (WM_APP + 4)
#define WM_UPDEV     (WM_APP + 5)   /* 升级检测结果（wp=ft_update_info* 动态分配，用完 free） */

static const wchar_t *APP_NAME = L"My File Transfer Assistant";
static const wchar_t *APP_VER  = L"V1.2.0";
#define MAX_FILES 512

typedef struct {
    HWND hwnd;
    float ui_scale;   /* LOGPIXELSX / 96.0，再乘以基础放大系数，解决用户说"字和按钮太小" */
    HFONT font, fontBold, fontTitle, fontTitleLg, fontSmall, fontCaption;
    HBRUSH brContent, brCard, brSidebar, brLog, brSendBg, brInput, brList;
    int page;
    int dark_theme;
    int theme_id;        /* 主题模式：0-11 */
    int acrylic;         /* 亚克力玻璃效果 0/1 */
    int opacity;         /* 窗口透明度 50-100 */
    int blur;            /* 背景模糊强度 0-100 */
    int blur_active;      /* accent 亚克力实际生效（决定客户区是否用黑色透明） */
    HWND navSend, navRecv, navSettings, navAbout;
    ft_config cfg;

    /* 发送端 */
    HWND sendStatus;
    HWND sendRefresh;
    HWND sendDevList;        /* ListView: 设备名/IP/设备号/状态 */
    HWND sendPick, sendRemove, sendClear;
    HWND sendIp;             /* 手动添加：IP 地址输入 */
    HWND sendDevId;          /* 手动添加：设备号输入 */
    HWND sendAddBtn;         /* 手动添加：+ 添加设备按钮 */
    HWND sendFileList;       /* ListBox: 选择的待发文件 */
    HWND sendCount;
    HWND sendStart;
    HWND sendLog;
    HWND sendProgress;       /* 进度条 */
    int selected_dev_idx;    /* 选中的设备索引 */
    ft_device devices[MAX_DEVICES];
    int dev_count;

    /* 待发文件 */
    wchar_t send_files[MAX_FILES][FT_MAX_PATH];
    wchar_t send_names[MAX_FILES][FT_MAX_PATH];
    int send_file_count;
    int64_t current_send_gid;

    /* 手动添加的设备（不会被发现模块清空） */
    ft_device manual_devices[MAX_DEVICES];
    int manual_count;

    /* 接收端 */
    HWND recvStatus;
    HWND recvToggle;
    HWND recvDir, recvBrowse;
    HWND recvPort;
    HWND recvIp;
    HWND recvList;
    HWND recvLog;
    int recv_running;

    /* 卡片矩形 */
    RECT rcard1, rcard2, rcard3;
    /* 滚动状态（内容区垂直滚动） */
    int scroll_y;        /* 当前滚动偏移（向下为正） */
    int content_total;   /* 当前页面虚拟内容总高度 */
    int sb_w;            /* 垂直滚动条宽度 */
} App;

static App g;

/* ---------- 工具：跨线程 Post 消息 ---------- */
typedef struct { wchar_t *msg; int level; } log_post;
static void log_cb_adapter(int level, const wchar_t *msg) {
    log_post *p = (log_post *)malloc(sizeof(*p));
    size_t n = wcslen(msg) + 1;
    p->msg = (wchar_t *)malloc(n * sizeof(wchar_t));
    wcscpy_s(p->msg, n, msg);
    p->level = level;
    PostMessageW(g.hwnd, WM_LOG, 0, (LPARAM)p);
}

static void discover_cb(int type, const wchar_t *arg) {
    (void)type;
    wchar_t *a = (wchar_t *)malloc((wcslen(arg) + 1) * sizeof(wchar_t));
    wcscpy_s(a, wcslen(arg)+1, arg);
    PostMessageW(g.hwnd, WM_DISCOVER, 0, (LPARAM)a);
}

typedef struct {
    int type; int64_t task_id; int percent;
    int64_t bytes; int64_t total;
    wchar_t *fname; wchar_t *extra;
} ev_post;
static ev_post *make_ev(int t, int64_t tid, int pct, int64_t b, int64_t tot, const wchar_t *fn, const wchar_t *ex) {
    ev_post *e = (ev_post *)calloc(1, sizeof(*e));
    e->type = t; e->task_id = tid; e->percent = pct; e->bytes = b; e->total = tot;
    if (fn) { size_t n = wcslen(fn)+1; e->fname = (wchar_t*)malloc(n*sizeof(wchar_t)); wcscpy_s(e->fname,n,fn); }
    if (ex) { size_t n = wcslen(ex)+1; e->extra = (wchar_t*)malloc(n*sizeof(wchar_t)); wcscpy_s(e->extra,n,ex); }
    return e;
}
static void send_cb_adapter(int type, int64_t tid, int pct, int64_t b, int64_t tot, const wchar_t *fn, const wchar_t *ex) {
    PostMessageW(g.hwnd, WM_SENDEV, 0, (LPARAM)make_ev(type, tid, pct, b, tot, fn, ex));
}
static void recv_cb_adapter(int type, int64_t tid, int pct, int64_t b, int64_t tot, const wchar_t *fn, const wchar_t *ex) {
    PostMessageW(g.hwnd, WM_RECVEV, 0, (LPARAM)make_ev(type, tid, pct, b, tot, fn, ex));
}

/* ---------- 日志 & 设备刷新 ---------- */
static void append_log(HWND hEdit, const wchar_t *msg) {
    int len = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, len, len);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)msg);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
}
static void set_status(HWND hStatic, int sid) {
    /* 0 待机 1 工作中 2 连接中 3 错误 4 监听中 */
    const wchar_t *txt; COLORREF c;
    switch (sid) {
        case 1: txt = L"传输中"; c = COL_AMBER; break;
        case 2: txt = L"连接中"; c = COL_BLUE;  break;
        case 3: txt = L"错误";   c = COL_RED;   break;
        case 4: txt = L"监听中"; c = COL_GREEN; break;
        default:txt = L"待机";   c = COL_SUB;   break;
    }
    SetWindowTextW(hStatic, txt);
    SetWindowLongPtrW(hStatic, GWLP_USERDATA, (LONG_PTR)c);
    InvalidateRect(hStatic, NULL, TRUE);
}

static void refresh_device_list(void) {
    if (!g.sendDevList) return;
    ft_device tmp[MAX_DEVICES];
    int n = discover_get_devices(tmp, MAX_DEVICES);
    /* 自动发现的设备先放 */
    int total = 0;
    if (n > 0 && n <= MAX_DEVICES) {
        memcpy(g.devices, tmp, sizeof(ft_device) * n);
        total = n;
    }
    /* 再追加手动添加的设备（避免重复：通过 ip+port 判重） */
    for (int mi = 0; mi < g.manual_count; mi++) {
        int dup = 0;
        for (int i = 0; i < total; i++) {
            if (g.devices[i].port == g.manual_devices[mi].port &&
                strcmp(g.devices[i].ip, g.manual_devices[mi].ip) == 0) { dup = 1; break; }
        }
        if (!dup && total < MAX_DEVICES) { g.devices[total++] = g.manual_devices[mi]; }
    }
    g.dev_count = total;
    ListView_DeleteAllItems(g.sendDevList);
    for (int i = 0; i < total; i++) {
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = i;
        wchar_t buf[256]; _snwprintf_s(buf, 256, _TRUNCATE, L"%s", g.devices[i].name);
        lvi.pszText = buf; lvi.lParam = (LPARAM)i;
        ListView_InsertItem(g.sendDevList, &lvi);
        _snwprintf_s(buf, 256, _TRUNCATE, L"%hs", g.devices[i].ip);
        ListView_SetItemText(g.sendDevList, i, 1, buf);
        _snwprintf_s(buf, 256, _TRUNCATE, L"%d", g.devices[i].port);
        ListView_SetItemText(g.sendDevList, i, 2, buf);
        /* last_seen < 0 表示手动添加；否则在线 */
        if (g.devices[i].last_seen < 0) {
            ListView_SetItemText(g.sendDevList, i, 3, L"手动");
        } else {
            ListView_SetItemText(g.sendDevList, i, 3, L"在线");
        }
    }
    wchar_t cnt[128]; _snwprintf_s(cnt, 128, _TRUNCATE, L"设备 %d 台 · 文件 %d 个", g.dev_count, g.send_file_count);
    SetWindowTextW(g.sendCount, cnt);
}

static void refresh_recv_ip(HWND hStatic) {
    wchar_t ips[1024] = L"本机 IP: ";
    int first = 1;
    char host[256];
    if (gethostname(host, sizeof(host)) == 0) {
        struct hostent *he = gethostbyname(host);
        if (he) {
            for (int i = 0; he->h_addr_list[i]; i++) {
                struct in_addr a; memcpy(&a, he->h_addr_list[i], sizeof(a));
                char *s = inet_ntoa(a);
                if (!strncmp(s, "127.", 4)) continue;
                wchar_t ws[64]; MultiByteToWideChar(CP_ACP, 0, s, -1, ws, 64);
                if (!first) wcscat_s(ips, 1024, L" / ");
                wcscat_s(ips, 1024, ws);
                first = 0;
            }
        }
    }
    if (first) wcscat_s(ips, 1024, L"未知");
    SetWindowTextW(hStatic, ips);
}

/* ---------- 创建控件 ---------- */
static HWND mk(HWND parent, const wchar_t *cls, const wchar_t *text,
               DWORD style, DWORD ex, int id, HFONT f) {
    HWND h = CreateWindowExW(ex, cls, text ? text : L"", style,
                             0, 0, 120, 24, parent, (HMENU)(LONG_PTR)id,
                             (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE), NULL);
    if (f) SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE);
    return h;
}

/* ---------- 输入框/列表框：主题色描边 ----------
 * 子类化 WM_NCPAINT，用主题强调色画 1px 边框，替换系统 3D 凹陷边框。
 * 颜色实时取 COL_ACCENT_SOFT → 切换主题后自动跟随。 */
static LRESULT CALLBACK theme_border_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                          UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (msg == WM_NCPAINT) {
        HDC dc = GetWindowDC(h);
        RECT r; GetWindowRect(h, &r);
        OffsetRect(&r, -r.left, -r.top);
        HPEN pn = CreatePen(PS_SOLID, 1, COL_ACCENT_SOFT);
        HGDIOBJ op = SelectObject(dc, pn);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, r.left, r.top, r.right, r.bottom);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(pn);
        ReleaseDC(h, dc);
        return 0;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

static void apply_theme_borders(HWND parent) {
    HWND child = GetWindow(parent, GW_CHILD);
    while (child) {
        wchar_t cls[64] = {0};
        GetClassNameW(child, cls, 64);
        if (_wcsicmp(cls, L"Edit") == 0 || _wcsicmp(cls, L"ListBox") == 0 ||
            _wcsicmp(cls, L"SysListView32") == 0) {
            SetWindowSubclass(child, theme_border_proc, 1, 0);
            SetWindowPos(child, NULL, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

static void create_send_controls(HWND hwnd, int cx, int *yPtr);
static void create_recv_controls(HWND hwnd, int cx, int *yPtr);
static void create_settings_controls(HWND hwnd, int cx, int *yPtr);

/* 根据系统 DPI + 一个基础放大系数算出 UI 缩放。
 * 用户反馈："字和按钮都太小" —— 所以基础放大 1.15~1.25，保证 1080p / 2K / 4K 都清晰。
 * DPI 96 → 1.0，DPI 120 → 1.25，DPI 144 → 1.5 等
 */
static float compute_ui_scale(void) {
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    float dpi_scale = (float)dpi / 96.0f;
    /* 1.35x 基础缩放：既不挤也不松散，符合 Win11 Fluent 比例 */
    float base_boost = 1.35f;
    float s = dpi_scale * base_boost;
    if (s < 1.00f) s = 1.00f;
    if (s > 2.50f) s = 2.50f;
    return s;
}
#define S(x) ((int)((x) * g.ui_scale + 0.5f))

static void create_controls(HWND hwnd, HINSTANCE hi) {
    (void)hi;
    g.ui_scale = compute_ui_scale();

    /* 字体层级：
       - fs_titleLg = 大标题（品牌/页面主标题）
       - fs_title   = 卡片标题
       - fs_body    = 正文
       - fs_small   = 副文字/说明
       - fs_caption = 最小标签
       使用 Segoe UI Variable / Microsoft YaHei UI 双栈，
       英文走 Segoe，中文走微软雅黑 UI，自动回落 */
    int fs_caption  = S(10);
    int fs_small    = S(11);
    int fs_body     = S(13);
    int fs_title    = S(14);
    int fs_titleLg  = S(20);

    g.font       = CreateFontW(fs_body,   0, 0, 0, FW_NORMAL, 0, 0, 0,
        GB2312_CHARSET, 0, 0, CLEARTYPE_QUALITY, VARIABLE_PITCH|FF_MODERN, L"Microsoft YaHei UI, Segoe UI Variable");
    g.fontBold   = CreateFontW(fs_body,   0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
        GB2312_CHARSET, 0, 0, CLEARTYPE_QUALITY, VARIABLE_PITCH|FF_MODERN, L"Microsoft YaHei UI, Segoe UI Variable");
    g.fontTitle  = CreateFontW(fs_title,  0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
        GB2312_CHARSET, 0, 0, CLEARTYPE_QUALITY, VARIABLE_PITCH|FF_MODERN, L"Microsoft YaHei UI, Segoe UI Variable");
    g.fontTitleLg = CreateFontW(fs_titleLg, 0, 0, 0, FW_BOLD, 0, 0, 0,
        GB2312_CHARSET, 0, 0, CLEARTYPE_QUALITY, VARIABLE_PITCH|FF_MODERN, L"Microsoft YaHei UI, Segoe UI Variable");
    g.fontSmall  = CreateFontW(fs_small,  0, 0, 0, FW_NORMAL, 0, 0, 0,
        GB2312_CHARSET, 0, 0, CLEARTYPE_QUALITY, VARIABLE_PITCH|FF_MODERN, L"Microsoft YaHei UI, Segoe UI Variable");
    g.fontCaption = CreateFontW(fs_caption, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        GB2312_CHARSET, 0, 0, CLEARTYPE_QUALITY, VARIABLE_PITCH|FF_MODERN, L"Microsoft YaHei UI, Segoe UI Variable");

    g.dark_theme = g.cfg.dark_mode ? 1 : 0;
    g.theme_id = g.cfg.theme_id;  /* 从配置加载主题模式 */
    g.acrylic = g.cfg.acrylic_effect ? 1 : 0;
    g.opacity = g.cfg.window_opacity;
    if (g.opacity < 50) g.opacity = 50; if (g.opacity > 100) g.opacity = 100;
    g.blur = g.cfg.blur_amount;
    if (g.blur < 0) g.blur = 0; if (g.blur > 100) g.blur = 100;
    g.scroll_y = 0;
    g.content_total = 0;
    g.sb_w = GetSystemMetrics(SM_CXVSCROLL) + S(2);
    g.brContent = CreateSolidBrush(COL_CONTENT);
    g.brCard    = CreateSolidBrush(COL_CARD);
    g.brSidebar = CreateSolidBrush(COL_SIDEBAR);
    g.brLog     = CreateSolidBrush(COL_LOG_BG);
    g.brSendBg  = CreateSolidBrush(COL_CONTENT);
    g.brInput   = CreateSolidBrush(COL_INPUT_BG);
    g.brList    = CreateSolidBrush(COL_LIST_BG);

    DWORD nav = WS_CHILD | WS_VISIBLE | BS_OWNERDRAW;
    g.navSend     = mk(hwnd, L"BUTTON", L"  发送文件", nav, 0, ID_NAV_SEND, g.fontBold);
    g.navRecv     = mk(hwnd, L"BUTTON", L"  接收文件", nav, 0, ID_NAV_RECV, g.fontBold);
    g.navSettings = mk(hwnd, L"BUTTON", L"  设置",     nav, 0, ID_NAV_SETTINGS, g.fontBold);
    g.navAbout    = mk(hwnd, L"BUTTON", L"  关于",     nav, 0, ID_NAV_ABOUT, g.fontBold);

    int y = S(24), cx = S(228) + S(28);
    create_send_controls(hwnd, cx, &y);
    create_recv_controls(hwnd, cx, &y);
    create_settings_controls(hwnd, cx, &y);

    /* 初始页面 */
    g.page = 0;
    ShowWindow(g.navSend, SW_SHOW); ShowWindow(g.navRecv, SW_SHOW);
    ShowWindow(g.navSettings, SW_SHOW); ShowWindow(g.navAbout, SW_SHOW);

    refresh_device_list();
    refresh_recv_ip(g.recvIp);
    set_status(g.sendStatus, 0);
    set_status(g.recvStatus, 0);
}

/* ---- 发送端页面布局（按卡片） ---- */
/* 全局 layout 变量（相对 cx）：每次 layout 重新 MoveWindow，控件在 create 时先隐藏到合适位置即可。
 * 我们改为：在 layout() 中完全定位，这里仅创建控件。
 */
static void create_send_controls(HWND hwnd, int cx, int *yPtr) {
    (void)cx; (void)yPtr;
    /* 标题/副文本/状态 */
    g.sendStatus = mk(hwnd, L"STATIC", L"待机", WS_CHILD | SS_RIGHT, 0, ID_SEND_STATUS, g.fontBold);

    /* 卡片1：附近设备 */
    g.sendRefresh = mk(hwnd, L"BUTTON", L"刷新", WS_CHILD | BS_OWNERDRAW, 0, ID_SEND_REFRESH, g.fontBold);
    /* 手动添加设备入口：IP / 设备号 / + 添加按钮 */
    g.sendIp    = mk(hwnd, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | ES_CENTER,  WS_EX_CLIENTEDGE, ID_SEND_IP,    g.font);
    g.sendDevId = mk(hwnd, L"EDIT", L"51831", WS_CHILD | ES_AUTOHSCROLL | ES_CENTER, WS_EX_CLIENTEDGE, ID_SEND_DEVID, g.font);
    g.sendAddBtn = mk(hwnd, L"BUTTON", L"+ 添加设备", WS_CHILD | BS_OWNERDRAW, 0, ID_SEND_ADDDEV, g.fontBold);
    /* 给 IP / 设备号加上 cue banner（灰色提示文字，需要 WinVista+） */
    SendMessageW(g.sendIp, EM_SETCUEBANNER, TRUE, (LPARAM)L"对方 IP，如 192.168.1.10");
    SendMessageW(g.sendDevId, EM_SETCUEBANNER, TRUE, (LPARAM)L"对方设备号（默认 51831）");
    /* 设备列表 ListView */
    g.sendDevList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 400, 160, hwnd, (HMENU)(LONG_PTR)ID_SEND_DEVLIST,
        (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE), NULL);
    SendMessageW(g.sendDevList, WM_SETFONT, (WPARAM)g.font, TRUE);
    ListView_SetExtendedListViewStyle(g.sendDevList, LVS_EX_FULLROWSELECT);
    LVCOLUMNW col = {0};
    col.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
    col.fmt = LVCFMT_LEFT;
    col.cx = S(180); col.pszText = L"设备名称"; ListView_InsertColumn(g.sendDevList, 0, &col);
    col.cx = S(130); col.pszText = L"IP 地址";   ListView_InsertColumn(g.sendDevList, 1, &col);
    col.cx = S(80);  col.pszText = L"设备号";    ListView_InsertColumn(g.sendDevList, 2, &col);
    col.cx = S(80);  col.pszText = L"状态";      ListView_InsertColumn(g.sendDevList, 3, &col);

    /* 卡片2：选择文件 */
    g.sendPick   = mk(hwnd, L"BUTTON", L"选择文件", WS_CHILD | BS_OWNERDRAW, 0, ID_SEND_PICK,   g.fontBold);
    g.sendRemove = mk(hwnd, L"BUTTON", L"移除",       WS_CHILD | BS_OWNERDRAW, 0, ID_SEND_REMOVE, g.font);
    g.sendClear  = mk(hwnd, L"BUTTON", L"清空",       WS_CHILD | BS_OWNERDRAW, 0, ID_SEND_CLEAR, g.font);
    g.sendFileList = mk(hwnd, L"LISTBOX", L"", WS_CHILD | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | WS_TABSTOP,
                        WS_EX_CLIENTEDGE, ID_SEND_FILELIST, g.font);
    g.sendCount    = mk(hwnd, L"STATIC", L"设备 0 台 · 文件 0 个", WS_CHILD | SS_LEFT, 0, ID_SEND_COUNT, g.fontSmall);

    /* 卡片3：进度条 + 发送 */
    g.sendProgress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_SMOOTH,
                                     0, 0, 400, 20, hwnd, (HMENU)(LONG_PTR)ID_SEND_PROGRESS,
                                     (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE), NULL);
    SendMessageW(g.sendProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    g.sendStart = mk(hwnd, L"BUTTON", L"发送", WS_CHILD | BS_OWNERDRAW, 0, ID_SEND_START, g.fontBold);
    g.sendLog   = mk(hwnd, L"EDIT", L"",
        WS_CHILD | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
        WS_EX_CLIENTEDGE, ID_SEND_LOG, g.fontSmall);
}

static void create_recv_controls(HWND hwnd, int cx, int *yPtr) {
    (void)cx; (void)yPtr;
    g.recvStatus = mk(hwnd, L"STATIC", L"待机", WS_CHILD | SS_RIGHT, 0, ID_RECV_STATUS, g.fontBold);
    g.recvToggle = mk(hwnd, L"BUTTON", L"开启服务", WS_CHILD | BS_OWNERDRAW, 0, ID_RECV_TOGGLE, g.fontBold);
    g.recvDir    = mk(hwnd, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, ID_RECV_DIR, g.font);
    g.recvBrowse = mk(hwnd, L"BUTTON", L"浏览", WS_CHILD | BS_OWNERDRAW, 0, ID_RECV_BROWSE, g.font);
    g.recvPort   = mk(hwnd, L"EDIT", L"51831", WS_CHILD | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, ID_RECV_PORT, g.font);
    g.recvIp     = mk(hwnd, L"STATIC", L"本机 IP：获取中…", WS_CHILD | SS_RIGHT, 0, ID_RECV_IP, g.fontSmall);
    g.recvList   = mk(hwnd, L"LISTBOX", L"", WS_CHILD | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                      WS_EX_CLIENTEDGE, ID_RECV_LIST, g.font);
    g.recvLog    = mk(hwnd, L"EDIT", L"",
        WS_CHILD | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
        WS_EX_CLIENTEDGE, ID_RECV_LOG, g.fontSmall);
}

static void create_settings_controls(HWND hwnd, int cx, int *yPtr) {
    (void)cx; (void)yPtr;
    mk(hwnd, L"STATIC", L"本机昵称", WS_CHILD, 0, 9000, g.fontBold);
    mk(hwnd, L"STATIC", L"设备号（本机）", WS_CHILD, 0, 9001, g.fontBold);
    mk(hwnd, L"STATIC", L"发现端口（自动）", WS_CHILD, 0, 9002, g.fontBold);
    mk(hwnd, L"STATIC", L"保存目录", WS_CHILD, 0, 9003, g.fontBold);
    mk(hwnd, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, ID_SET_NICK, g.font);
    mk(hwnd, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, ID_SET_TCPPORT, g.font);
    mk(hwnd, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, ID_SET_UDPPORT, g.font);
    mk(hwnd, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, ID_SET_SAVEDIR, g.font);
    mk(hwnd, L"BUTTON", L"浏览", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_SAVEBROWSE, g.font);

    /* 切换开关（BS_OWNERDRAW + 手动状态管理） */
    mk(hwnd, L"BUTTON", L"开机自启", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_AUTOSTART, g.font);
    mk(hwnd, L"BUTTON", L"自动接收", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_AUTOACCEPT, g.font);
    mk(hwnd, L"BUTTON", L"完成通知", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_NOTIFY, g.font);
    mk(hwnd, L"BUTTON", L"自动扫描", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_AUTODISC, g.font);
    mk(hwnd, L"BUTTON", L"深色主题", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_DARKMODE, g.font);

    /* 高级设置切换开关 */
    mk(hwnd, L"BUTTON", L"最小化到托盘", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_TRAY, g.font);
    mk(hwnd, L"BUTTON", L"退出前确认",   WS_CHILD | BS_OWNERDRAW, 0, ID_SET_CONFIRMIT, g.font);
    mk(hwnd, L"BUTTON", L"声音提示",     WS_CHILD | BS_OWNERDRAW, 0, ID_SET_SOUND, g.font);
    mk(hwnd, L"BUTTON", L"显示传输速度", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_SPEED, g.font);
    mk(hwnd, L"BUTTON", L"发送后清空列表", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_AUTOCLEAN, g.font);

    /* 主题模式：12 个切换开关（单选互斥） */
    mk(hwnd, L"BUTTON", L"靛紫", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_ACCENT0, g.font);
    mk(hwnd, L"BUTTON", L"深绿", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_ACCENT1, g.font);
    mk(hwnd, L"BUTTON", L"樱花", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_ACCENT2, g.font);
    mk(hwnd, L"BUTTON", L"海蓝", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_ACCENT3, g.font);
    mk(hwnd, L"BUTTON", L"日落橙", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_ACCENT4, g.font);
    mk(hwnd, L"BUTTON", L"青碧", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_ACCENT5, g.font);
    mk(hwnd, L"BUTTON", L"玫瑰金", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_ACCENT6, g.font);
    mk(hwnd, L"BUTTON", L"暗夜紫", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_ACCENT7, g.font);
    mk(hwnd, L"BUTTON", L"极光", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_ACCENT8, g.font);
    mk(hwnd, L"BUTTON", L"薄荷", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_ACCENT9, g.font);
    mk(hwnd, L"BUTTON", L"琥珀", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_ACCENT10, g.font);
    mk(hwnd, L"BUTTON", L"炭黑", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_ACCENT11, g.font);

    /* 外观效果区已移除（2026-10-12 按用户要求去掉亚克力模糊） */

    /* 右键菜单：单切换开关 */
    mk(hwnd, L"BUTTON", L"右键菜单", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_REG_MENU, g.font);
    /* 软件升级：自动检查开关 + 立即检查按钮 */
    mk(hwnd, L"BUTTON", L"启动时自动检查更新", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_AUTOUPD, g.font);
    mk(hwnd, L"BUTTON", L"检查更新", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_CHECKUPD, g.fontBold);
    mk(hwnd, L"BUTTON", L"保存设置", WS_CHILD | BS_OWNERDRAW, 0, ID_SET_SAVE, g.fontBold);
    mk(hwnd, L"STATIC", L"", WS_CHILD, 0, ID_SET_STATUSMSG, g.fontBold);
}

/* ---------- 布局函数 ---------- */
static void show_send(int show) {
    HWND a[] = { g.sendStatus, g.sendRefresh, g.sendDevList,
        g.sendIp, g.sendDevId, g.sendAddBtn,
        g.sendPick, g.sendRemove, g.sendClear, g.sendFileList, g.sendCount,
        g.sendProgress, g.sendStart, g.sendLog };
    for (int i = 0; i < (int)(sizeof(a)/sizeof(a[0])); i++)
        ShowWindow(a[i], show ? SW_SHOW : SW_HIDE);
}
static void show_recv(int show) {
    HWND a[] = { g.recvStatus, g.recvToggle, g.recvDir, g.recvBrowse,
        g.recvPort, g.recvIp, g.recvList, g.recvLog };
    for (int i = 0; i < (int)(sizeof(a)/sizeof(a[0])); i++)
        ShowWindow(a[i], show ? SW_SHOW : SW_HIDE);
}
/* Settings 页面的所有控件 ID：我们用遍历不太容易，直接把设置控件存成数组的另一种做法是用 EnumChildWindows。
 * 简化：设置控件 ID 范围在 9000-9003（label）、ID_SET_*（edit/check/btn），我们显式列出 */
static void show_settings(int show, HWND hwnd) {
    int ids[] = { 9000,9001,9002,9003,
        ID_SET_NICK,ID_SET_TCPPORT,ID_SET_UDPPORT,
        ID_SET_SAVEDIR,ID_SET_SAVEBROWSE,
        ID_SET_AUTOSTART,ID_SET_AUTOACCEPT,ID_SET_NOTIFY,ID_SET_AUTODISC,
        ID_SET_DARKMODE,
        ID_SET_TRAY,ID_SET_CONFIRMIT,ID_SET_SOUND,ID_SET_SPEED,ID_SET_AUTOCLEAN,
        ID_SET_ACCENT0,ID_SET_ACCENT1,ID_SET_ACCENT2,
        ID_SET_ACCENT3,ID_SET_ACCENT4,ID_SET_ACCENT5,
        ID_SET_ACCENT6,ID_SET_ACCENT7,ID_SET_ACCENT8,
        ID_SET_ACCENT9,ID_SET_ACCENT10,ID_SET_ACCENT11,
        ID_SET_REG_MENU,ID_SET_AUTOUPD,ID_SET_CHECKUPD,ID_SET_SAVE,ID_SET_STATUSMSG };
    for (int i = 0; i < (int)(sizeof(ids)/sizeof(ids[0])); i++)
        ShowWindow(GetDlgItem(hwnd, ids[i]), show ? SW_SHOW : SW_HIDE);
}

static void switch_page(int p) {
    g.page = p;
    g.scroll_y = 0;   /* 切页回到顶部 */
    show_send(p == 0);
    show_recv(p == 1);
    show_settings(p == 2, g.hwnd);
    if (p != 3) { /* 关于页，直接在 on_paint 里绘制，不用控件 */ }
    SendMessageW(g.navSend,     BM_SETCHECK, p == 0 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.navRecv,     BM_SETCHECK, p == 1 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.navSettings, BM_SETCHECK, p == 2 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.navAbout,    BM_SETCHECK, p == 3 ? BST_CHECKED : BST_UNCHECKED, 0);
    InvalidateRect(g.hwnd, NULL, FALSE);
}

static void layout(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    int sideW = S(228);
    /* 极窄窗口：侧栏自动收窄 */
    if (W < S(980)) { sideW = S(196); }
    if (W < S(880)) { sideW = S(172); }
    g.sb_w = GetSystemMetrics(SM_CXVSCROLL) + S(2);
    int cx = sideW + S(28);
    int cw = W - cx - g.sb_w - S(16);
    if (cw < S(200)) cw = S(200);

    /* 导航按钮：侧栏内垂直排列，向下留出 Logo+分割线空间 */
    int navH = S(44), navGap = S(6), navX = S(12);
    int navY0 = S(140);  /* 品牌标题 + 副标 + 分割线 + 16px 留白 */
    if (rc.right < S(980)) { navY0 = S(130); }
    if (rc.right < S(880)) { navY0 = S(120); }
    int navW = sideW - S(24);
    MoveWindow(g.navSend,     navX, navY0,                          navW, navH, TRUE);
    MoveWindow(g.navRecv,     navX, navY0 + (navH + navGap),         navW, navH, TRUE);
    MoveWindow(g.navSettings, navX, navY0 + (navH + navGap) * 2,     navW, navH, TRUE);
    MoveWindow(g.navAbout,    navX, navY0 + (navH + navGap) * 3,     navW, navH, TRUE);

    /* 计算当前页面"自然内容总高度"
     * 注意要含顶部偏移 S(64)（yoff=S(20) + 状态行 S(44)），
     * 否则滚动范围偏小 → 底部日志永远滚不到、被窗口底边截断 */
    int total = 0;
    if (g.page == 0) {
        total = S(64) + S(250) + S(18) + S(200) + S(18) + S(132) + S(18) + S(170);
    } else if (g.page == 1) {
        total = S(64) + S(175) + S(18) + S(195) + S(18) + S(175);
    } else if (g.page == 2) {
        /* 设置页：基础4行 + 切换3排 + 高级3排 + 主题12个 + 右键菜单 + 软件升级 + 保存（外观效果区已移除） */
        total = S(1480);
    } else {
        total = S(820);  /* 关于页：签名 + 新手教程 */
    }
    g.content_total = total;

    /* 滚动条范围 / 位置 */
    int visH = H - S(20);
    if (visH < S(120)) visH = S(120);
    int scroll_max = total - visH;
    if (scroll_max < 0) scroll_max = 0;
    if (g.scroll_y > scroll_max) g.scroll_y = scroll_max;
    if (g.scroll_y < 0) g.scroll_y = 0;
    SCROLLINFO si = { sizeof(si) };
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = total - 1;
    si.nPage = (UINT)visH;
    si.nPos = g.scroll_y;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

    int yoff = S(20) - g.scroll_y;
    int sW = S(140); if (cw < S(340)) sW = S(110);
    MoveWindow(g.sendStatus, cx + cw - sW, yoff + S(6), sW, S(24), TRUE);
    MoveWindow(g.recvStatus, cx + cw - sW, yoff + S(6), sW, S(24), TRUE);

    if (g.page == 0) {
        int gap = S(18);
        int c1h = S(250), c2h = S(200), c3h = S(132), logH = S(170);
        int y = yoff + S(44);
        /* 卡片1：附近设备 */
        g.rcard1.left = cx; g.rcard1.top = y; g.rcard1.right = cx + cw; g.rcard1.bottom = y + c1h;
        MoveWindow(g.sendRefresh, cx + cw - S(20) - S(120), y + S(18), S(120), S(36), TRUE);
        /* 手动添加设备栏（放在列表上面，更易发现） */
        int addY = y + S(64);
        int ipW = cw - S(40) - S(10) - S(160) - S(10) - S(130);
        if (ipW < S(140)) ipW = S(140);
        int addX = cx + S(20);
        MoveWindow(g.sendIp, addX, addY, ipW, S(32), TRUE);
        MoveWindow(g.sendDevId, addX + ipW + S(10), addY, S(160), S(32), TRUE);
        int addBtnX = addX + ipW + S(10) + S(160) + S(10);
        int addBtnW = cx + cw - S(20) - addBtnX; if (addBtnW < S(100)) addBtnW = S(100);
        MoveWindow(g.sendAddBtn, addBtnX, addY, addBtnW, S(32), TRUE);
        /* 设备列表（放在"添加设备"下方，直到卡片底预留内边距） */
        int listTop = addY + S(32) + S(14);
        int listBot = y + c1h - S(20);
        int dlh1 = listBot - listTop; if (dlh1 < S(50)) dlh1 = S(50);
        MoveWindow(g.sendDevList, cx + S(20), listTop, cw - S(40), dlh1, TRUE);
        /* 列宽随列表宽度自适应铺满整行（固定列宽会在宽窗口下留出大片
         * 空白列区，视觉上像表格整体错位） */
        {
            int lw = cw - S(40);
            int cIp = lw * 24 / 100, cId = lw * 18 / 100, cSt = lw * 14 / 100;
            if (cIp < S(130)) cIp = S(130);
            if (cId < S(90))  cId = S(90);
            if (cSt < S(80))  cSt = S(80);
            int cName = lw - cIp - cId - cSt;
            if (cName < S(180)) cName = S(180);
            ListView_SetColumnWidth(g.sendDevList, 0, cName);
            ListView_SetColumnWidth(g.sendDevList, 1, cIp);
            ListView_SetColumnWidth(g.sendDevList, 2, cId);
            ListView_SetColumnWidth(g.sendDevList, 3, cSt);
        }
        y += c1h + gap;
        /* 卡片2：待发送文件 */
        g.rcard2.left = cx; g.rcard2.top = y; g.rcard2.right = cx + cw; g.rcard2.bottom = y + c2h;
        int by = y + S(62);
        int b1w = S(160), b2w = S(120), b3w = S(110);
        int totalBtns = b1w + S(10) + b2w + S(10) + b3w;
        int availBtns = cw - S(40);
        if (totalBtns > availBtns) {
            b3w = S(80); b2w = S(100); b1w = availBtns - b2w - S(20) - b3w;
            if (b1w < S(80)) b1w = S(80);
        }
        MoveWindow(g.sendPick,   cx + S(20),                by, b1w, S(34), TRUE);
        MoveWindow(g.sendRemove, cx + S(20) + b1w + S(10),   by, b2w, S(34), TRUE);
        int bx3 = cx + cw - S(20) - b3w;
        if (bx3 < cx + S(20) + b1w + S(10) + b2w + S(10)) bx3 = cx + S(20) + b1w + S(10) + b2w + S(10);
        MoveWindow(g.sendClear,  bx3, by, b3w, S(34), TRUE);
        int fly = S(62) + S(48);
        int flh = c2h - fly - S(32); if (flh < S(40)) flh = S(40);
        MoveWindow(g.sendFileList, cx + S(20), y + fly, cw - S(40), flh, TRUE);
        MoveWindow(g.sendCount, cx + S(20), y + c2h - S(24), cw - S(40), S(18), TRUE);
        y += c2h + gap;
        /* 卡片3：发送进度 — 进度条/发送按钮在卡片标题下方，不再遮挡标题 */
        g.rcard3.left = cx; g.rcard3.top = y; g.rcard3.right = cx + cw; g.rcard3.bottom = y + c3h;
        MoveWindow(g.sendProgress, cx + S(20), y + S(66), cw - S(40), S(22), TRUE);
        /* 发送按钮：居中适度宽度（整行宽按钮会贴住卡片两边，不美观） */
        {
            int sbW2 = S(240);
            int sbAvail = cw - S(40);
            if (sbW2 > sbAvail) sbW2 = sbAvail;
            MoveWindow(g.sendStart, cx + S(20) + (sbAvail - sbW2) / 2,
                       y + S(96), sbW2, S(34), TRUE);
        }
        y += c3h + gap;
        /* 日志 */
        MoveWindow(g.sendLog, cx, y, cw, logH, TRUE);
    } else if (g.page == 1) {
        int gap = S(18);
        int c1h = S(175), listH = S(195), logH = S(175);
        int y = yoff + S(44);
        g.rcard1.left = cx; g.rcard1.top = y; g.rcard1.right = cx + cw; g.rcard1.bottom = y + c1h;
        MoveWindow(g.recvToggle, cx + cw - S(20) - S(200), y + S(20), S(200), S(38), TRUE);
        int ry = y + S(78);
        int labelW = S(130), btnW = S(100);
        if (cw < S(520)) { labelW = S(110); btnW = S(80); }
        MoveWindow(GetDlgItem(hwnd, 9003), cx + S(20), ry + S(2), labelW, S(26), TRUE);
        int dirLeft  = cx + S(20) + labelW;
        int dirRight = cx + cw - S(20) - btnW - S(10);
        int dirW     = dirRight - dirLeft; if (dirW < S(80)) dirW = S(80);
        MoveWindow(g.recvDir,    dirLeft, ry, dirW, S(30), TRUE);
        MoveWindow(g.recvBrowse, dirRight, ry, btnW, S(30), TRUE);
        MoveWindow(GetDlgItem(hwnd, 9001), cx + S(20), ry + S(44), labelW, S(26), TRUE);
        MoveWindow(g.recvPort, cx + S(20) + labelW, ry + S(42), S(100), S(30), TRUE);
        int ipW = cw - S(320); if (ipW < S(140)) ipW = S(140);
        MoveWindow(g.recvIp,   cx + cw - S(20) - ipW, ry + S(44), ipW, S(26), TRUE);
        y += c1h + gap;
        /* 卡片2：接收列表（画卡框 + 标题，列表嵌进卡片内部，不再裸放成"看不见的卡片"） */
        g.rcard2.left = cx; g.rcard2.top = y; g.rcard2.right = cx + cw; g.rcard2.bottom = y + listH;
        MoveWindow(g.recvList, cx + S(20), y + S(64), cw - S(40), listH - S(84), TRUE);
        y += listH + gap;
        /* 卡片3：接收日志 */
        g.rcard3.left = cx; g.rcard3.top = y; g.rcard3.right = cx + cw; g.rcard3.bottom = y + logH;
        MoveWindow(g.recvLog, cx + S(20), y + S(64), cw - S(40), logH - S(84), TRUE);
    } else if (g.page == 2) {
        int y = yoff + S(96);
        int labelW = S(130), btnW = S(100);
        if (cw < S(520)) { labelW = S(110); btnW = S(80); }
        int inputX = cx + S(20) + labelW, inputW = cw - S(40) - labelW - btnW - S(10);
        if (inputW < S(80)) inputW = S(80);
        int step = S(48);
        MoveWindow(GetDlgItem(hwnd, 9000), cx + S(20), y + S(8), labelW, S(30), TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_SET_NICK), inputX, y, inputW, S(30), TRUE);
        y += step;
        MoveWindow(GetDlgItem(hwnd, 9001), cx + S(20), y + S(8), labelW, S(30), TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_SET_TCPPORT), inputX, y, S(200), S(30), TRUE);
        y += step;
        MoveWindow(GetDlgItem(hwnd, 9002), cx + S(20), y + S(8), labelW, S(30), TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_SET_UDPPORT), inputX, y, S(200), S(30), TRUE);
        y += step;
        MoveWindow(GetDlgItem(hwnd, 9003), cx + S(20), y + S(8), labelW, S(30), TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_SET_SAVEDIR), inputX, y, inputW, S(30), TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_SET_SAVEBROWSE), cx + cw - S(20) - btnW, y, btnW, S(30), TRUE);
        y += step + S(16);
        int cbH = S(40), cbW = cw / 2 - S(28);
        MoveWindow(GetDlgItem(hwnd, ID_SET_AUTOSTART), cx + S(20),            y, cbW, cbH, TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_SET_AUTOACCEPT), cx + S(20) + cw / 2,   y, cbW, cbH, TRUE);
        y += cbH + S(12);
        MoveWindow(GetDlgItem(hwnd, ID_SET_NOTIFY),    cx + S(20),            y, cbW, cbH, TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_SET_AUTODISC),  cx + S(20) + cw / 2,   y, cbW, cbH, TRUE);
        y += cbH + S(12);
        MoveWindow(GetDlgItem(hwnd, ID_SET_DARKMODE),  cx + S(20),            y, cbW, cbH, TRUE);
        y += cbH + S(24);
        /* 高级设置区：分隔线 + 标题（on_paint 会画）—— 留足空间 */
        y += S(48);
        MoveWindow(GetDlgItem(hwnd, ID_SET_TRAY),      cx + S(20),            y, cbW, cbH, TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_SET_CONFIRMIT), cx + S(20) + cw / 2,    y, cbW, cbH, TRUE);
        y += cbH + S(12);
        MoveWindow(GetDlgItem(hwnd, ID_SET_SOUND),    cx + S(20),            y, cbW, cbH, TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_SET_SPEED),    cx + S(20) + cw / 2,    y, cbW, cbH, TRUE);
        y += cbH + S(12);
        MoveWindow(GetDlgItem(hwnd, ID_SET_AUTOCLEAN),cx + S(20),            y, cbW, cbH, TRUE);
        y += cbH + S(24);
        /* 主题模式：12 个切换开关，6 行 x 2 列 */
        y += S(48);  /* 分隔线 + 标题空间 */
        MoveWindow(GetDlgItem(hwnd, ID_SET_ACCENT0), cx + S(20),            y, cbW, cbH, TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_SET_ACCENT1), cx + S(20) + cw / 2,   y, cbW, cbH, TRUE);
        y += cbH + S(12);
        MoveWindow(GetDlgItem(hwnd, ID_SET_ACCENT2), cx + S(20),            y, cbW, cbH, TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_SET_ACCENT3), cx + S(20) + cw / 2,   y, cbW, cbH, TRUE);
        y += cbH + S(12);
        MoveWindow(GetDlgItem(hwnd, ID_SET_ACCENT4), cx + S(20),            y, cbW, cbH, TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_SET_ACCENT5), cx + S(20) + cw / 2,   y, cbW, cbH, TRUE);
        y += cbH + S(12);
        MoveWindow(GetDlgItem(hwnd, ID_SET_ACCENT6), cx + S(20),            y, cbW, cbH, TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_SET_ACCENT7), cx + S(20) + cw / 2,   y, cbW, cbH, TRUE);
        y += cbH + S(12);
        MoveWindow(GetDlgItem(hwnd, ID_SET_ACCENT8), cx + S(20),            y, cbW, cbH, TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_SET_ACCENT9), cx + S(20) + cw / 2,   y, cbW, cbH, TRUE);
        y += cbH + S(12);
        MoveWindow(GetDlgItem(hwnd, ID_SET_ACCENT10), cx + S(20),            y, cbW, cbH, TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_SET_ACCENT11), cx + S(20) + cw / 2,   y, cbW, cbH, TRUE);
        y += cbH + S(24);
        /* 右键菜单：单切换开关（外观效果区已整体移除，分隔线直接画在当前 y 处） */
        y += S(48);  /* 分隔线 + 标题空间：避让 on_paint 画的分区标题 */
        MoveWindow(GetDlgItem(hwnd, ID_SET_REG_MENU), cx + S(20), y, cbW, cbH, TRUE);
        y += cbH + S(24);
        /* 软件升级：自动检查开关（左）+ 立即检查按钮（右） */
        y += S(48);  /* 分隔线 + 标题空间 */
        MoveWindow(GetDlgItem(hwnd, ID_SET_AUTOUPD), cx + S(20), y, cbW, cbH, TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_SET_CHECKUPD), cx + S(20) + cw / 2, y, cw / 2 - S(40), cbH, TRUE);
        y += cbH + S(24);
        /* 保存按钮 + 状态消息 */
        int saveH = S(52);
        MoveWindow(GetDlgItem(hwnd, ID_SET_SAVE), cx + S(20), y, cw - S(40), saveH, TRUE);
        y += saveH + S(16);
        MoveWindow(GetDlgItem(hwnd, ID_SET_STATUSMSG), cx + S(20), y, cw - S(40), S(48), TRUE);
    }
}

/* ---------- 绘制 ---------- */
static void apply_dwm_mica(HWND hwnd);                                       /* 前向声明 */
static void draw_card(HDC dc, RECT *r, const wchar_t *title, const wchar_t *hint) {
    /* 卡片本体：圆角 + 1px 精致描边 */
    int rad = S(10);
    HRGN rg = CreateRoundRectRgn(r->left, r->top, r->right + 1, r->bottom + 1, rad, rad);
    HBRUSH br = CreateSolidBrush(COL_CARD);
    FillRgn(dc, rg, br);
    DeleteObject(br);

    /* 卡片描边：1px 精致边框 */
    HBRUSH bd_br = CreateSolidBrush(COL_CARD_BD);
    FrameRgn(dc, rg, bd_br, 1, 1);
    DeleteObject(bd_br);
    DeleteObject(rg);

    /* 卡片标题左侧小圆角色条：与导航激活态指示条呼应，统一的视觉语言 */
    {
        int barCy = r->top + S(28);
        HRGN barRgn = CreateRoundRectRgn(r->left + S(20), barCy - S(9),
                                         r->left + S(24) + 1, barCy + S(9) + 1, 2, 2);
        HBRUSH barBr = CreateSolidBrush(COL_ACCENT);
        FillRgn(dc, barRgn, barBr);
        DeleteObject(barBr);
        DeleteObject(barRgn);
    }

    /* 卡片标题 */
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, COL_TEXT);
    HGDIOBJ of = SelectObject(dc, g.fontTitle);
    RECT tr = { r->left + S(32), r->top + S(16), r->right - S(20), r->top + S(40) };
    DrawTextW(dc, title, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    /* 副提示：右侧对齐的小说明 */
    if (hint) {
        SetTextColor(dc, COL_MUTED);
        SelectObject(dc, g.fontSmall);
        RECT hr = { r->left + S(32), r->top + S(40), r->right - S(20), r->top + S(60) };
        DrawTextW(dc, hint, -1, &hr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(dc, of);
}

static void draw_ownerdraw_btn(LPDRAWITEMSTRUCT dis, const wchar_t *text,
                               COLORREF bg, COLORREF textcol) {
    HDC dc = dis->hDC; RECT r = dis->rcItem;
    int radius = S(8);
    HRGN rgn = CreateRoundRectRgn(r.left, r.top, r.right + 1, r.bottom + 1, radius, radius);

    /* 默认填充：纯色（2026-10-12 用户定稿：不要渐变，纯色简洁、跟随主题色） */
    {
        HBRUSH br_main = CreateSolidBrush(bg);
        FillRgn(dc, rgn, br_main);
        DeleteObject(br_main);
    }

    /* 悬停/按下状态：紫调加深 */
    if (dis->itemState & ODS_HOTLIGHT) {
        HBRUSH hv = CreateSolidBrush(COL_BTN_HOV);
        FillRgn(dc, rgn, hv);
        DeleteObject(hv);
    }
    if (dis->itemState & ODS_SELECTED) {
        HBRUSH pv = CreateSolidBrush(COL_BTN_PRS);
        FillRgn(dc, rgn, pv);
        DeleteObject(pv);
    }
    if (dis->itemState & ODS_DISABLED) {
        HBRUSH dv = CreateSolidBrush(COL_BTN_GHOST);
        FillRgn(dc, rgn, dv);
        DeleteObject(dv);
    }

    /* 按钮外描边：1px 清晰线条（取当前状态背景色加深 25%，彩色/幽灵按钮统一适用） */
    {
        COLORREF curBg = bg;
        if (dis->itemState & ODS_SELECTED)      curBg = COL_BTN_PRS;
        else if (dis->itemState & ODS_HOTLIGHT) curBg = COL_BTN_HOV;
        else if (dis->itemState & ODS_DISABLED) curBg = COL_BTN_GHOST;
        COLORREF edge = RGB((GetRValue(curBg) * 75) / 100,
                            (GetGValue(curBg) * 75) / 100,
                            (GetBValue(curBg) * 75) / 100);
        HRGN frgn = CreateRoundRectRgn(r.left, r.top, r.right + 1, r.bottom + 1, radius, radius);
        HBRUSH fb = CreateSolidBrush(edge);
        FrameRgn(dc, frgn, fb, 1, 1);
        DeleteObject(fb);
        DeleteObject(frgn);
    }
    DeleteObject(rgn);

    /* 文字 */
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, textcol);
    HGDIOBJ of = SelectObject(dc, dis->itemData ? (HFONT)dis->itemData : g.fontBold);
    RECT ir = r; InflateRect(&ir, -S(12), -S(4));
    DrawTextW(dc, text, -1, &ir, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, of);

    /* 焦点框（极简：半透明 1px 描边） */
    if (dis->itemState & ODS_FOCUS) {
        RECT fr = r; fr.left += 2; fr.top += 2; fr.right -= 2; fr.bottom -= 2;
        int r2 = radius - 1; if (r2 < 1) r2 = 1;
        HRGN fg = CreateRoundRectRgn(fr.left, fr.top, fr.right + 1, fr.bottom + 1, r2, r2);
        HBRUSH none = (HBRUSH)GetStockObject(NULL_BRUSH);
        FrameRgn(dc, fg, none, 1, 1);
        DeleteObject(fg);
    }
}

static void draw_nav_btn(LPDRAWITEMSTRUCT dis, const wchar_t *text) {
    HDC dc = dis->hDC; RECT r = dis->rcItem;
    int checked = (dis->itemState & ODS_SELECTED) ||
                  (SendMessageW(dis->hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED);
    int radius = S(8);
    RECT ir = r; InflateRect(&ir, -S(4), -S(2));
    HRGN rgn = CreateRoundRectRgn(ir.left, ir.top, ir.right + 1, ir.bottom + 1, radius, radius);

    /* 【分层窗口修复】子控件设为 WS_EX_LAYERED 后不再透出父窗口背景，
     * 未选中/未悬停态必须自己填侧栏底色，否则旧的选中高亮、悬停底色
     * 和旧文字会永远残留在按钮自己的合成表面里（表现为：点击后高亮
     * "动画"不消失、新旧文字直接重叠） */
    {
        HBRUSH baseBr = CreateSolidBrush(COL_SIDEBAR);
        FillRect(dc, &r, baseBr);
        DeleteObject(baseBr);
    }

    if (checked) {
        /* 激活态：薰衣草紫实色背景 + 白色文字 */
        HBRUSH br = CreateSolidBrush(COL_NAV_ACTIVE_BG);
        FillRgn(dc, rgn, br);
        DeleteObject(br);

        /* 左侧小色条：紫色指示条 */
        RECT ind = { ir.left + 2, ir.top + S(8), ir.left + 4, ir.bottom - S(8) };
        HBRUSH indBr = CreateSolidBrush(COL_ACCENT);
        FillRect(dc, &ind, indBr);
        DeleteObject(indBr);
    } else if (dis->itemState & ODS_HOTLIGHT) {
        HBRUSH br = CreateSolidBrush(COL_NAV_HOV_BG);
        FillRgn(dc, rgn, br);
        DeleteObject(br);
    }

    /* 导航按钮外描边：1px 清晰线条（选中/悬停 = 背景色加深 22%；未选中 = 淡描边） */
    {
        COLORREF nbg = checked ? COL_NAV_ACTIVE_BG :
                       ((dis->itemState & ODS_HOTLIGHT) ? COL_NAV_HOV_BG : COL_CARD_BD);
        COLORREF edge = (checked || (dis->itemState & ODS_HOTLIGHT))
            ? RGB((GetRValue(nbg) * 78) / 100, (GetGValue(nbg) * 78) / 100, (GetBValue(nbg) * 78) / 100)
            : nbg;
        HBRUSH fb = CreateSolidBrush(edge);
        FrameRgn(dc, rgn, fb, 1, 1);
        DeleteObject(fb);
    }
    DeleteObject(rgn);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, checked ? COL_ACCENT : COL_NAV_TEXT);
    HGDIOBJ of = SelectObject(dc, checked ? g.fontBold : g.font);
    RECT tr = ir; tr.left += S(14); tr.right -= S(10);
    DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, of);
}

static void on_paint(HWND hwnd) {
    PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);

    /* === 双缓冲：32-bit DIB Section 支持 ClearType 子像素渲染，消除字体间距错乱 === */
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
    HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);
    dc = mem;  /* 后续全部画到内存 DC */
    int sideW = S(228);
    if (rc.right < S(980)) { sideW = S(196); }
    if (rc.right < S(880)) { sideW = S(172); }
    int cx_clip = sideW + S(28);

    /* ============= 侧边栏 ============= */
    RECT side = { 0, 0, sideW, rc.bottom };
    FillRect(dc, &side, g.brSidebar);

    /* 侧边栏底部柔和渐变：从 sidebar 过渡到 content 色（避免硬切） */
    RECT sideFoot = { 0, rc.bottom - S(80), sideW, rc.bottom };
    HBRUSH footBr = CreateSolidBrush(COL_SIDEBAR);
    FillRect(dc, &sideFoot, footBr);
    DeleteObject(footBr);

    /* 顶部 Logo 色块 — 小方形品牌徽标 */
    int logoSize = S(32);
    int logoX = S(20), logoY = S(28);
    RECT logoRect = { logoX, logoY, logoX + logoSize, logoY + logoSize };
    HRGN logoRgn = CreateRoundRectRgn(logoRect.left, logoRect.top,
        logoRect.right + 1, logoRect.bottom + 1, S(8), S(8));
    HBRUSH logoBr = CreateSolidBrush(COL_ACCENT);
    FillRgn(dc, logoRgn, logoBr);
    DeleteObject(logoBr);
    /* 徽标上的小方块（模拟图标） */
    RECT logoInner = { logoX + S(9), logoY + S(9), logoX + logoSize - S(9), logoY + logoSize - S(9) };
    HBRUSH logoInnerBr = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(dc, &logoInner, logoInnerBr);
    DeleteObject(logoInnerBr);
    DeleteObject(logoRgn);

    /* Logo 文字：品牌简写 MFTA + 完整英文名副标（副标在下方，不与标题重叠） */
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, COL_SIDEBAR_TEXT);
    HGDIOBJ of = SelectObject(dc, g.fontTitleLg);
    int titleTop = logoY + S(2);
    int titleBot = logoY + logoSize - S(2);
    RECT nr = { logoX + logoSize + S(12), titleTop, sideW - S(20), titleBot };
    DrawTextW(dc, L"MFTA", -1, &nr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    /* 副标：完整软件名（小字号）+ 版本号，独立一行不重叠 */
    SelectObject(dc, g.fontCaption);
    SetTextColor(dc, COL_SIDEBAR_SUB);
    int subTop = titleBot + S(2);
    int subBot = logoY + logoSize + S(14);
    RECT nsr = { logoX + logoSize + S(12), subTop, sideW - S(8), subBot };
    DrawTextW(dc, L"My File Transfer Assistant", -1, &nsr,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, of);

    /* 分割线 — 细线 + 左右留白；按侧栏品牌底部下移 16px 避免靠太近 */
    int sepY = subBot + S(14);
    RECT sep = { S(20), sepY, sideW - S(20), sepY + 1 };
    HBRUSH sepBr = CreateSolidBrush(COL_DIVIDER);
    FillRect(dc, &sep, sepBr);
    DeleteObject(sepBr);

    /* 底部版本号 */
    SetTextColor(dc, COL_SIDEBAR_VER);
    SelectObject(dc, g.fontCaption);
    RECT vr = { S(20), rc.bottom - S(28), sideW - S(10), rc.bottom - S(10) };
    wchar_t vbuf[64]; _snwprintf_s(vbuf, 64, _TRUNCATE, L"%ls  ·  轻量 & 原生", APP_VER);
    DrawTextW(dc, vbuf, -1, &vr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    /* 侧边栏右侧 1px 柔分割线 */
    RECT sideBd = { sideW - 1, 0, sideW, rc.bottom };
    HBRUSH sideBdBr = CreateSolidBrush(COL_DIVIDER);
    FillRect(dc, &sideBd, sideBdBr);
    DeleteObject(sideBdBr);

    /* ============= 主内容区 ============= */
    RECT cont = { sideW, 0, rc.right, rc.bottom };
    /* 实色背景（亚克力已废弃，永远走实色路径） */
    FillRect(dc, &cont, g.brContent);

    /* 内容裁剪 */
    HRGN clipRgn = CreateRectRgn(cx_clip, 0, rc.right - g.sb_w, rc.bottom);
    SelectClipRgn(dc, clipRgn);

    if (g.page == 0) {
        draw_card(dc, &g.rcard1, L"附近设备", L"在同一局域网内自动发现的可用设备");
        draw_card(dc, &g.rcard2, L"待发送文件", L"支持多选 · 支持文件夹");
        draw_card(dc, &g.rcard3, L"发送进度", L"传输过程实时显示，可随时查看日志");
    } else if (g.page == 1) {
        RECT r1 = g.rcard1;
        if (r1.bottom != 0) {
            draw_card(dc, &r1, L"接收设置", L"开启后局域网内设备可发现此电脑");
        }
        RECT r2 = g.rcard2;
        if (r2.bottom != 0) {
            draw_card(dc, &r2, L"接收列表", L"收到的文件自动保存到上方设置的目录");
        }
        RECT r3 = g.rcard3;
        if (r3.bottom != 0) {
            draw_card(dc, &r3, L"接收日志", L"连接与传输过程实时显示");
        }
    } else if (g.page == 2) {
        /* 设置页：大标题 + 分区（基础设置 / 高级设置 / 主题模式 / 右键菜单） */
        int yo = S(20) - g.scroll_y;
        /* 页面标题：左侧 accent 小色条 + 大标题 + 副标（三者左缘对齐） */
        {
            HRGN pbar = CreateRoundRectRgn(cx_clip, yo + S(10), cx_clip + S(4) + 1, yo + S(32) + 1, 2, 2);
            HBRUSH pbarBr = CreateSolidBrush(COL_ACCENT);
            FillRgn(dc, pbar, pbarBr);
            DeleteObject(pbarBr);
            DeleteObject(pbar);
        }
        SetTextColor(dc, COL_TEXT);
        SelectObject(dc, g.fontTitleLg);
        RECT pageTitle = { cx_clip + S(14), yo + S(4), rc.right - S(40), yo + S(38) };
        DrawTextW(dc, L"个性化设置", -1, &pageTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, g.fontSmall);
        SetTextColor(dc, COL_SUB);
        RECT pageSub = { cx_clip + S(14), yo + S(42), rc.right - S(40), yo + S(58) };
        DrawTextW(dc, L"基础配置 · 开关 · 主题模式 · 右键菜单 · 保存", -1, &pageSub, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        /* 基础设置分区标题：在控件上方 S(28) 处，与副标题之间留 S(10) 间距 */
        int baseTop = yo + S(96);  /* layout 中控件起始 y */
        SetTextColor(dc, COL_TEXT);
        SelectObject(dc, g.fontTitle);
        RECT bsTitle = { cx_clip + S(20), baseTop - S(30), rc.right - S(40), baseTop - S(30) + S(26) };
        DrawTextW(dc, L"基础设置", -1, &bsTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        /* 基础开关与高级开关之间：分割线 + "高级设置" 标题 */
        /* layout: 4行输入(yo+96起,4*48+16=208) + 3排基础开关(3*52=156) + 24 = yo+472 */
        int advSepTop = yo + S(472);
        RECT advSep = { cx_clip + S(20), advSepTop, rc.right - S(40), advSepTop + 1 };
        HBRUSH advSepBr = CreateSolidBrush(COL_DIVIDER);
        FillRect(dc, &advSep, advSepBr);
        DeleteObject(advSepBr);
        SetTextColor(dc, COL_TEXT);
        SelectObject(dc, g.fontTitle);
        RECT advTitle = { cx_clip + S(20), advSepTop + S(8), rc.right - S(40), advSepTop + S(8) + S(28) };
        DrawTextW(dc, L"高级设置", -1, &advTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        /* 主题模式分区标题 */
        /* layout: 高级区 yo+520起, 3排开关(3*52=156) + 24 = yo+688 */
        int themeSepTop = yo + S(688);
        RECT themeSep = { cx_clip + S(20), themeSepTop, rc.right - S(40), themeSepTop + 1 };
        HBRUSH themeSepBr = CreateSolidBrush(COL_DIVIDER);
        FillRect(dc, &themeSep, themeSepBr);
        DeleteObject(themeSepBr);
        SetTextColor(dc, COL_TEXT);
        SelectObject(dc, g.fontTitle);
        RECT themeTitle = { cx_clip + S(20), themeSepTop + S(8), rc.right - S(40), themeSepTop + S(8) + S(28) };
        DrawTextW(dc, L"主题模式", -1, &themeTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        /* 右键菜单分区标题（外观效果区已移除，直接衔接主题模式之后） */
        /* layout: 主题区 yo+736起, 6排开关(6*52=312) + 24 = yo+1060 */
        int regSepTop = yo + S(1060);
        RECT regSep = { cx_clip + S(20), regSepTop, rc.right - S(40), regSepTop + 1 };
        HBRUSH regSepBr = CreateSolidBrush(COL_DIVIDER);
        FillRect(dc, &regSep, regSepBr);
        DeleteObject(regSepBr);
        SetTextColor(dc, COL_TEXT);
        SelectObject(dc, g.fontTitle);
        RECT regTitle = { cx_clip + S(20), regSepTop + S(8), rc.right - S(40), regSepTop + S(8) + S(28) };
        DrawTextW(dc, L"右键菜单", -1, &regTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        /* 软件升级分区标题（layout: 右键菜单行 + cbH + 24 后接 S(48) 分区空间） */
        int updSepTop = regSepTop + S(116);
        RECT updSep = { cx_clip + S(20), updSepTop, rc.right - S(40), updSepTop + 1 };
        HBRUSH updSepBr = CreateSolidBrush(COL_DIVIDER);
        FillRect(dc, &updSep, updSepBr);
        DeleteObject(updSepBr);
        SetTextColor(dc, COL_TEXT);
        SelectObject(dc, g.fontTitle);
        RECT updTitle = { cx_clip + S(20), updSepTop + S(8), rc.right - S(40), updSepTop + S(8) + S(28) };
        DrawTextW(dc, L"软件升级", -1, &updTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else if (g.page == 3) {
        /* 关于页 — 精致排版：大标题 + 版本徽章 + 简介 + 使用步骤 + 新手教程 + 工作室签名 */
        int yo = S(20) - g.scroll_y;

        /* 页面大标题：品牌简写 + 左侧 accent 小色条（完整名见下方简介与产品信息） */
        {
            HRGN abar = CreateRoundRectRgn(cx_clip, yo + S(14), cx_clip + S(4) + 1, yo + S(38) + 1, 2, 2);
            HBRUSH abarBr = CreateSolidBrush(COL_ACCENT);
            FillRgn(dc, abar, abarBr);
            DeleteObject(abarBr);
            DeleteObject(abar);
        }
        SetTextColor(dc, COL_TEXT);
        SelectObject(dc, g.fontTitleLg);
        RECT rl = { cx_clip + S(14), yo + S(8), rc.right - S(40), yo + S(44) };
        DrawTextW(dc, L"MFTA", -1, &rl, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        /* 徽章：当前版本 */
        wchar_t tagbuf[64]; _snwprintf_s(tagbuf, 64, _TRUNCATE, L"v%ls", APP_VER);
        SetTextColor(dc, COL_ACCENT);
        SelectObject(dc, g.fontCaption);
        RECT tagR = { cx_clip, yo + S(44), cx_clip + S(90), yo + S(64) };
        HRGN tagRgn = CreateRoundRectRgn(tagR.left, tagR.top, tagR.right + 1, tagR.bottom + 1, S(6), S(6));
        HBRUSH tagBr = CreateSolidBrush(g.dark_theme ? RGB(60, 55, 110) : RGB(226, 220, 250));
        FillRgn(dc, tagRgn, tagBr);
        DeleteObject(tagBr);
        DeleteObject(tagRgn);
        RECT tagIr = { tagR.left + S(8), tagR.top, tagR.right - S(8), tagR.bottom };
        DrawTextW(dc, tagbuf, -1, &tagIr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        /* 简介 */
        SetTextColor(dc, COL_SUB);
        SelectObject(dc, g.font);
        RECT d = { cx_clip, yo + S(76), rc.right - S(40), yo + S(176) };
        DrawTextW(dc,
            L"My File Transfer Assistant（简称 MFTA）—— 一款原生、轻量的 Windows 局域网文件传输工具。\r\n\r\n"
            L"支持同一局域网内的电脑互相发现、互发文件和文件夹；\r\n"
            L"右键任何文件/文件夹即可一键发送；无需注册账户、无需联网。",
            -1, &d, DT_LEFT | DT_WORDBREAK);

        /* 分割线 */
        RECT sep2 = { cx_clip, yo + S(186), rc.right - S(40), yo + S(187) };
        HBRUSH sep2Br = CreateSolidBrush(COL_DIVIDER);
        FillRect(dc, &sep2, sep2Br);
        DeleteObject(sep2Br);

        /* 分区标题：使用说明 */
        SetTextColor(dc, COL_TEXT);
        SelectObject(dc, g.fontTitle);
        RECT st = { cx_clip, yo + S(200), rc.right - S(40), yo + S(224) };
        DrawTextW(dc, L"新手教程 · 4 步即可使用", -1, &st, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(dc, g.font);
        SetTextColor(dc, COL_SUB);
        const wchar_t *steps[] = {
            L"①  两台电脑都先打开本软件（系统会提示防火墙放行，点击「允许」）；",
            L"②  在「接收方」电脑：切换到「接收文件」页 → 点「开启服务」；",
            L"③  在「发送方」电脑：「发送文件」页 → 列表里选对方设备 → 加文件 → 点「发送」。\r\n"
            L"      ✦ 若没搜到对方设备：用卡片里的「+ 添加设备」，输入对方 IP + 设备号（默认 51831）即可；",
            L"④  或者：资源管理器里右键任何文件/文件夹 → 「发送到附近设备…」，软件会自动弹出发送。",
        };
        int sy = yo + S(232);
        for (int i = 0; i < 4; i++) {
            RECT sr = { cx_clip, sy, rc.right - S(40), sy + S(54) };
            if (i == 2) sr.bottom = sy + S(82);
            DrawTextW(dc, steps[i], -1, &sr, DT_LEFT | DT_WORDBREAK);
            sy += (i == 2) ? S(88) : S(60);
        }

        /* 「什么是设备号？」说明卡 */
        int qTop = sy + S(4);
        RECT pillR = { cx_clip, qTop, rc.right - S(40), qTop + S(92) };
        HRGN pillRgn = CreateRoundRectRgn(pillR.left, pillR.top, pillR.right + 1, pillR.bottom + 1, S(10), S(10));
        HBRUSH pillBr = CreateSolidBrush(g.dark_theme ? RGB(42, 40, 72) : RGB(246, 245, 252));
        FillRgn(dc, pillRgn, pillBr);
        DeleteObject(pillBr);
        HBRUSH pillBd = CreateSolidBrush(COL_DIVIDER);
        FrameRgn(dc, pillRgn, pillBd, 1, 1);
        DeleteObject(pillBd);
        DeleteObject(pillRgn);
        SetTextColor(dc, COL_ACCENT);
        SelectObject(dc, g.fontBold);
        RECT tipTitle = { pillR.left + S(16), pillR.top + S(10), pillR.right - S(16), pillR.top + S(32) };
        DrawTextW(dc, L"💡 小知识：什么是「设备号」？", -1, &tipTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(dc, COL_SUB);
        SelectObject(dc, g.fontSmall);
        RECT tipBody = { pillR.left + S(16), pillR.top + S(34), pillR.right - S(16), pillR.bottom - S(12) };
        DrawTextW(dc,
            L"「设备号」就是以前的「传输端口」，只是换了更易懂的名字。\r\n"
            L"默认值是 51831，一般不用改；如果对方改了设备号（设置页），发送时也要填对方改后的值才能连上。",
            -1, &tipBody, DT_LEFT | DT_WORDBREAK);

        /* 分割线 */
        sy = qTop + S(92) + S(16);
        RECT sep3 = { cx_clip, sy, rc.right - S(40), sy + 1 };
        HBRUSH sep3Br = CreateSolidBrush(COL_DIVIDER);
        FillRect(dc, &sep3, sep3Br);
        DeleteObject(sep3Br);

        /* 分区：工作室 / 签名信息 */
        sy += S(18);
        SetTextColor(dc, COL_TEXT);
        SelectObject(dc, g.fontTitle);
        RECT sst = { cx_clip, sy, rc.right - S(40), sy + S(28) };
        DrawTextW(dc, L"工作室签名 · 数字发布信息", -1, &sst, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        sy += S(36);

        /* 签名卡 */
        RECT sigR = { cx_clip, sy, rc.right - S(40), sy + S(150) };
        HRGN sigRgn = CreateRoundRectRgn(sigR.left, sigR.top, sigR.right + 1, sigR.bottom + 1, S(10), S(10));
        HBRUSH sigBr = CreateSolidBrush(g.dark_theme ? RGB(36, 34, 60) : RGB(250, 249, 255));
        FillRgn(dc, sigRgn, sigBr);
        DeleteObject(sigBr);
        HBRUSH sigBd = CreateSolidBrush(COL_CARD_BD);
        FrameRgn(dc, sigRgn, sigBd, 1, 1);
        DeleteObject(sigBd);
        DeleteObject(sigRgn);

        HGDIOBJ of2 = SelectObject(dc, g.font);
        int sl = sigR.left + S(20), srx = sigR.right - S(20);
        int y1 = sigR.top + S(14);
        /* 签名信息行（双列） */
        struct { const wchar_t *k, *v; } rows[] = {
            { L"产品名称", L"My File Transfer Assistant" },
            { L"出品工作室", L"MY 工作室" },
            { L"构建版本", L"V1.2.0 · Native Windows (Win32 / x86-64)" },
            { L"签名类型", L"MY 工作室 · 数字签名 MY-SIG-2026" },
            { L"发布日期", L"2026-08-19" },
        };
        for (int i = 0; i < 5; i++) {
            SetTextColor(dc, COL_MUTED);
            SelectObject(dc, g.fontSmall);
            RECT rk = { sl, y1, sl + S(110), y1 + S(22) };
            DrawTextW(dc, rows[i].k, -1, &rk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SetTextColor(dc, COL_TEXT);
            SelectObject(dc, g.font);
            RECT rv = { sl + S(110), y1, srx, y1 + S(22) };
            DrawTextW(dc, rows[i].v, -1, &rv, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            y1 += S(24);
        }
        SelectObject(dc, of2);
    }

    SelectClipRgn(dc, NULL);
    DeleteObject(clipRgn);
    SelectObject(dc, of);

    /* === 双缓冲收尾：内存 DC → 屏幕 ===
     * （曾经的亚克力 alpha 修正循环已彻底废弃：实色模式下 GDI 不写 alpha 字节无影响） */
    (void)bmpBits;
    BitBlt(screen, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

/* ---------- 文件添加逻辑 ---------- */
static void sendfile_add_one(const wchar_t *fullpath) {
    if (g.send_file_count >= MAX_FILES) return;
    const wchar_t *name = wcsrchr(fullpath, L'\\');
    name = name ? (name + 1) : fullpath;
    int dup = 0;
    for (int i = 0; i < g.send_file_count; i++)
        if (!wcscmp(g.send_files[i], fullpath)) { dup = 1; break; }
    if (dup) return;
    wcscpy_s(g.send_files[g.send_file_count], FT_MAX_PATH, fullpath);
    wcscpy_s(g.send_names[g.send_file_count], FT_MAX_PATH, name);
    SendMessageW(g.sendFileList, LB_ADDSTRING, 0, (LPARAM)fullpath);
    g.send_file_count++;
    wchar_t cnt[128]; _snwprintf_s(cnt, 128, _TRUNCATE, L"设备 %d 台 · 文件 %d 个", g.dev_count, g.send_file_count);
    SetWindowTextW(g.sendCount, cnt);
}

static void pick_files_send(HWND owner) {
    wchar_t buf[32768] = {0};
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 32768;
    ofn.lpstrFilter = L"所有文件\0*.*\0";
    ofn.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_DONTADDTORECENT;
    if (!GetOpenFileNameW(&ofn)) return;
    const wchar_t *p = buf;
    wchar_t dir[MAX_PATH]; wcscpy_s(dir, MAX_PATH, p);
    p += wcslen(p) + 1;
    if (*p == 0) sendfile_add_one(dir);
    else {
        while (*p) {
            wchar_t full[MAX_PATH];
            _snwprintf_s(full, MAX_PATH, _TRUNCATE, L"%s\\%s", dir, p);
            sendfile_add_one(full);
            p += wcslen(p) + 1;
        }
    }
}

/* ---------- 发送执行 ---------- */
static void do_send(void) {
    if (g.send_file_count == 0) {
        MessageBoxW(g.hwnd, L"请先选择要发送的文件", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }
    LRESULT sel = ListView_GetNextItem(g.sendDevList, -1, LVNI_SELECTED);
    if (sel < 0) {
        MessageBoxW(g.hwnd, L"请先选择一个目标设备", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (sel >= g.dev_count) return;
    ft_device *d = &g.devices[sel];
    ft_send_item *items = (ft_send_item *)malloc(sizeof(ft_send_item) * g.send_file_count);
    for (int i = 0; i < g.send_file_count; i++) {
        wcscpy_s(items[i].full_path, FT_MAX_PATH, g.send_files[i]);
        wcscpy_s(items[i].rel_name,  FT_MAX_PATH, g.send_names[i]);
    }
    int64_t gid = ft_send_files(d->ip, d->port, items, g.send_file_count);
    free(items);
    if (gid < 0) {
        MessageBoxW(g.hwnd, L"启动发送失败", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    g.current_send_gid = gid;
    set_status(g.sendStatus, 1);
    SendMessageW(g.sendProgress, PBM_SETPOS, 0, 0);
    /* 记录上次目标 IP/端口 */
    MultiByteToWideChar(CP_ACP, 0, d->ip, -1, g.cfg.last_dest_ip, 64);
    g.cfg.last_dest_port = d->port;
}

/* ---------- 接收端切换 ---------- */
static void toggle_recv(void) {
    if (g.recv_running) {
        ft_recv_stop();
        g.recv_running = 0;
        SetWindowTextW(g.recvToggle, L"开启服务");
        set_status(g.recvStatus, 0);
        EnableWindow(g.recvDir, TRUE); EnableWindow(g.recvBrowse, TRUE);
        EnableWindow(g.recvPort, TRUE);
    } else {
        wchar_t dir[1024]; GetWindowTextW(g.recvDir, dir, 1024);
        if (!dir[0]) config_default_save_dir(dir, 1024);
        CreateDirectoryW(dir, NULL);
        SetWindowTextW(g.recvDir, dir);
        wchar_t port_s[16]; GetWindowTextW(g.recvPort, port_s, 16);
        int port = _wtoi(port_s);
        if (port < 1 || port > 65535) { MessageBoxW(g.hwnd, L"端口无效", L"错误", MB_OK|MB_ICONERROR); return; }
        wcscpy_s(g.cfg.save_dir, 1024, dir);
        g.cfg.tcp_port = port;
        if (ft_recv_start(port, dir) < 0) {
            MessageBoxW(g.hwnd, L"开启接收失败（端口被占用？请检查防火墙或修改端口）", L"错误", MB_OK|MB_ICONERROR);
            set_status(g.recvStatus, 3);
            return;
        }
        g.recv_running = 1;
        SetWindowTextW(g.recvToggle, L"停止服务");
        set_status(g.recvStatus, 4);
        EnableWindow(g.recvDir, FALSE); EnableWindow(g.recvBrowse, FALSE);
        EnableWindow(g.recvPort, FALSE);
    }
}

/* ---------- 设置页 ---------- */
/* 设置切换开关的 on/off 状态（存于 GWLP_USERDATA，绘制代码从这里读） */
static void set_toggle(int id, int on) {
    HWND h = GetDlgItem(g.hwnd, id);
    if (h) { SetWindowLongPtrW(h, GWLP_USERDATA, on ? 1 : 0); InvalidateRect(h, NULL, TRUE); }
}
static int get_toggle(int id) {
    HWND h = GetDlgItem(g.hwnd, id);
    return h ? ((int)GetWindowLongPtrW(h, GWLP_USERDATA) ? 1 : 0) : 0;
}

static void load_settings_to_controls(void) {
    SetDlgItemTextW(g.hwnd, ID_SET_NICK, g.cfg.nickname);
    wchar_t tmp[32];
    _snwprintf_s(tmp, 32, _TRUNCATE, L"%d", g.cfg.tcp_port);
    SetDlgItemTextW(g.hwnd, ID_SET_TCPPORT, tmp);
    _snwprintf_s(tmp, 32, _TRUNCATE, L"%d", g.cfg.udp_port);
    SetDlgItemTextW(g.hwnd, ID_SET_UDPPORT, tmp);
    SetDlgItemTextW(g.hwnd, ID_SET_SAVEDIR, g.cfg.save_dir);
    /* 切换开关：状态写入 GWLP_USERDATA（绘制代码从 GWLP_USERDATA 读） */
    set_toggle(ID_SET_AUTOSTART,  g.cfg.autostart);
    set_toggle(ID_SET_AUTOACCEPT, g.cfg.auto_accept);
    set_toggle(ID_SET_NOTIFY,    g.cfg.show_notification);
    set_toggle(ID_SET_AUTODISC,  g.cfg.autodiscover);
    set_toggle(ID_SET_DARKMODE,  g.cfg.dark_mode);
    set_toggle(ID_SET_TRAY,      g.cfg.minimize_to_tray);
    set_toggle(ID_SET_CONFIRMIT, g.cfg.confirm_on_exit);
    set_toggle(ID_SET_SOUND,     g.cfg.sound_notify);
    set_toggle(ID_SET_SPEED,     g.cfg.show_speed);
    set_toggle(ID_SET_AUTOCLEAN, g.cfg.auto_cleanup_sent);
    /* 主题模式：选中当前主题 */
    for (int i = 0; i < NUM_THEMES; i++)
        set_toggle(ID_SET_ACCENT0 + i, i == g.theme_id);
    /* 右键菜单：检测是否已注册 */
    set_toggle(ID_SET_REG_MENU, reg_is_shell_menu_registered());
    set_toggle(ID_SET_AUTOUPD, g.cfg.auto_update);
    g.theme_id = g.cfg.theme_id;  /* 同步主题模式 */
    InvalidateRect(g.hwnd, NULL, TRUE);
}

static void save_settings(void) {
    GetDlgItemTextW(g.hwnd, ID_SET_NICK, g.cfg.nickname, 64);
    wchar_t tmp[32];
    GetDlgItemTextW(g.hwnd, ID_SET_TCPPORT, tmp, 32); g.cfg.tcp_port = _wtoi(tmp);
    GetDlgItemTextW(g.hwnd, ID_SET_UDPPORT, tmp, 32); g.cfg.udp_port = _wtoi(tmp);
    GetDlgItemTextW(g.hwnd, ID_SET_SAVEDIR, g.cfg.save_dir, 1024);
    g.cfg.autostart         = get_toggle(ID_SET_AUTOSTART);
    g.cfg.auto_accept       = get_toggle(ID_SET_AUTOACCEPT);
    g.cfg.show_notification = get_toggle(ID_SET_NOTIFY);
    g.cfg.autodiscover      = get_toggle(ID_SET_AUTODISC);
    g.cfg.dark_mode         = get_toggle(ID_SET_DARKMODE);
    g.cfg.minimize_to_tray  = get_toggle(ID_SET_TRAY);
    g.cfg.confirm_on_exit   = get_toggle(ID_SET_CONFIRMIT);
    g.cfg.sound_notify      = get_toggle(ID_SET_SOUND);
    g.cfg.show_speed        = get_toggle(ID_SET_SPEED);
    g.cfg.auto_cleanup_sent = get_toggle(ID_SET_AUTOCLEAN);
    g.cfg.auto_update = get_toggle(ID_SET_AUTOUPD);
    g.cfg.theme_id         = g.theme_id;  /* 主题模式由主题卡片直接设置 */

    /* 开机自启注册表 */
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (g.cfg.autostart) reg_register_autostart(exe);
    else                 reg_unregister_autostart();

    config_save(&g.cfg);

    /* 若接收端运行中，且保存目录改了，更新运行中的 */
    if (g.recv_running) ft_recv_set_save_dir(g.cfg.save_dir);

    /* 设备发现昵称变更（重启发现） */
    discover_shutdown();
    discover_init(g.cfg.nickname[0] ? g.cfg.nickname : L"未命名设备", g.cfg.tcp_port);

    wchar_t msg[512];
    _snwprintf_s(msg, 512, _TRUNCATE, L"设置已保存（昵称=%s）", g.cfg.nickname);
    SetDlgItemTextW(g.hwnd, ID_SET_STATUSMSG, msg);
}

/* ---------- 浏览 ---------- */
static void pick_folder_edit(HWND owner, HWND edit) {
    BROWSEINFOW bi = {0}; bi.hwndOwner = owner; bi.lpszTitle = L"选择文件夹";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path)) SetWindowTextW(edit, path);
        CoTaskMemFree(pidl);
    }
}

/* ---------- 命令：注册/注销右键菜单 ---------- */
static void do_reg_menu(int reg) {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t icon[MAX_PATH]; wcscpy_s(icon, MAX_PATH, exe);
    if (reg) {
        int r = reg_register_shell_menu(exe, icon);
        SetDlgItemTextW(g.hwnd, ID_SET_STATUSMSG, r == 0 ? L"已注册右键菜单" : L"注册失败");
    } else {
        reg_unregister_shell_menu();
        SetDlgItemTextW(g.hwnd, ID_SET_STATUSMSG, L"已移除右键菜单");
    }
    set_toggle(ID_SET_REG_MENU, reg);
}

/* ---------- 命令 ---------- */
static void apply_theme(void) {
    DeleteObject(g.brContent);
    DeleteObject(g.brCard);
    DeleteObject(g.brSidebar);
    DeleteObject(g.brLog);
    DeleteObject(g.brSendBg);
    DeleteObject(g.brInput);
    DeleteObject(g.brList);
    g.brContent = CreateSolidBrush(COL_CONTENT);
    g.brCard    = CreateSolidBrush(COL_CARD);
    g.brSidebar = CreateSolidBrush(COL_SIDEBAR);
    g.brLog     = CreateSolidBrush(COL_LOG_BG);
    g.brSendBg  = CreateSolidBrush(COL_CONTENT);
    g.brInput   = CreateSolidBrush(COL_INPUT_BG);
    g.brList    = CreateSolidBrush(COL_LIST_BG);
    /* 同步窗口类背景画笔，消除深色/浅色切换时边缘白边 */
    SetClassLongPtrW(g.hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)g.brContent);
    apply_dwm_mica(g.hwnd); /* 同步标题栏 + 云母 */
    /* 让复选框、按钮、文本框立刻刷新颜色 */
    HWND list[] = { g.navSend, g.navRecv, g.navSettings, g.navAbout,
                    g.sendPick, g.sendRemove, g.sendClear, g.sendRefresh, g.sendStart,
                    g.sendIp, g.sendDevId, g.sendAddBtn,
                    g.recvToggle, g.recvBrowse, g.recvDir, g.recvPort,
                    g.sendDevList, g.sendFileList, g.recvList,
                    g.recvIp, g.sendCount, g.sendStatus, g.recvStatus,
                    g.sendLog, g.recvLog,
                    GetDlgItem(g.hwnd, ID_SET_NICK), GetDlgItem(g.hwnd, ID_SET_TCPPORT), GetDlgItem(g.hwnd, ID_SET_UDPPORT),
                    GetDlgItem(g.hwnd, ID_SET_SAVEDIR), GetDlgItem(g.hwnd, ID_SET_SAVEBROWSE),
                    GetDlgItem(g.hwnd, ID_SET_AUTOSTART), GetDlgItem(g.hwnd, ID_SET_AUTOACCEPT),
                    GetDlgItem(g.hwnd, ID_SET_NOTIFY), GetDlgItem(g.hwnd, ID_SET_AUTODISC),
                    GetDlgItem(g.hwnd, ID_SET_DARKMODE), GetDlgItem(g.hwnd, ID_SET_REG_MENU),
                    GetDlgItem(g.hwnd, ID_SET_SAVE),
                    GetDlgItem(g.hwnd, ID_SET_STATUSMSG),
                    GetDlgItem(g.hwnd, ID_SET_TRAY), GetDlgItem(g.hwnd, ID_SET_CONFIRMIT),
                    GetDlgItem(g.hwnd, ID_SET_SOUND), GetDlgItem(g.hwnd, ID_SET_SPEED),
                    GetDlgItem(g.hwnd, ID_SET_AUTOCLEAN),
    GetDlgItem(g.hwnd, ID_SET_AUTOUPD), GetDlgItem(g.hwnd, ID_SET_CHECKUPD),
                    GetDlgItem(g.hwnd, ID_SET_ACCENT0), GetDlgItem(g.hwnd, ID_SET_ACCENT1),
                    GetDlgItem(g.hwnd, ID_SET_ACCENT2), GetDlgItem(g.hwnd, ID_SET_ACCENT3),
                    GetDlgItem(g.hwnd, ID_SET_ACCENT4), GetDlgItem(g.hwnd, ID_SET_ACCENT5),
                    GetDlgItem(g.hwnd, ID_SET_ACCENT6), GetDlgItem(g.hwnd, ID_SET_ACCENT7),
                    GetDlgItem(g.hwnd, ID_SET_ACCENT8), GetDlgItem(g.hwnd, ID_SET_ACCENT9),
                    GetDlgItem(g.hwnd, ID_SET_ACCENT10), GetDlgItem(g.hwnd, ID_SET_ACCENT11),
                    GetDlgItem(g.hwnd, 9000), GetDlgItem(g.hwnd, 9001),
                    GetDlgItem(g.hwnd, 9002), GetDlgItem(g.hwnd, 9003) };
    for (size_t i = 0; i < _countof(list); i++) {
        if (IsWindow(list[i])) InvalidateRect(list[i], NULL, TRUE);
    }
    InvalidateRect(g.hwnd, NULL, TRUE);
}

/* ---------- 软件升级：后台检测线程 + 结果处理 ---------- */
static DWORD WINAPI update_check_thread(LPVOID arg) {
    int manual = (int)(intptr_t)arg;   /* 1=用户点击"检查更新" 0=启动自动检测 */
    ft_update_info *info = (ft_update_info *)malloc(sizeof(ft_update_info));
    if (!info) return 0;
    int r = up_check(g.cfg.update_url, APP_VER, info);
    if (r != 0) {
        free(info);
        /* 手动检查才弹网络错误；启动静默检测失败不打扰用户 */
        if (manual) PostMessageW(g.hwnd, WM_UPDEV, (WPARAM)-1, (LPARAM)manual);
        return 0;
    }
    PostMessageW(g.hwnd, WM_UPDEV, (WPARAM)info, (LPARAM)manual);
    return 0;
}

static void start_update_check(int manual) {
    HANDLE th = CreateThread(NULL, 0, update_check_thread,
                              (LPVOID)(intptr_t)manual, 0, NULL);
    if (th) CloseHandle(th);
}

/* WM_UPDEV：wp=ft_update_info*（检测成功）或 -1（网络错误，仅手动时到达） */
static void on_update_ev(WPARAM wp, LPARAM lp) {
    int manual = (int)lp;
    if (wp == (WPARAM)-1) {
        MessageBoxW(g.hwnd, L"无法连接升级服务器，请检查网络后重试。",
                    APP_NAME, MB_OK | MB_ICONWARNING);
        return;
    }
    ft_update_info *info = (ft_update_info *)wp;
    if (!info->has_update && !info->rollback) {
        if (manual) MessageBoxW(g.hwnd, L"当前已是最新版本。", APP_NAME, MB_OK | MB_ICONINFORMATION);
        free(info);
        return;
    }
    /* 有新版本或服务器已回退版本（管理后台切换了当前发布）：展示简介并询问 */
    wchar_t msg[UP_NOTES_MAX + 512];
    if (info->rollback) {
        /* 管理后台把当前发布切回了旧版本 → 客户端可回退 */
        _snwprintf_s(msg, (int)(sizeof(msg)/sizeof(msg[0])), _TRUNCATE,
            L"检测到服务器已回退发布版本 %s。\n\n%s\n\n是否回退到该版本？\n选择\"是\"后将自动下载该版本并启动安装（覆盖安装到原目录）。",
            info->version, info->notes[0] ? info->notes : L"（该版本暂无说明）");
    } else {
        _snwprintf_s(msg, (int)(sizeof(msg)/sizeof(msg[0])), _TRUNCATE,
            L"发现新版本 %s！\n\n%s\n\n是否立即升级？\n选择\"是\"后将自动下载新版并启动安装（覆盖升级到原目录）。",
            info->version, info->notes[0] ? info->notes : L"（本次更新暂无说明）");
    }
    if (MessageBoxW(g.hwnd, msg, APP_NAME, MB_YESNO | MB_ICONINFORMATION) == IDYES) {
        if (info->script[0] && up_run_upgrade(info->script) == 0) {
            MessageBoxW(g.hwnd,
                L"正在后台下载安装包，请稍候…\n下载完成后将自动关闭本程序并启动安装向导。",
                APP_NAME, MB_OK | MB_ICONINFORMATION);
        } else if (info->url[0]) {
            /* 服务器未提供升级脚本：退化为直接打开下载地址手动安装 */
            MessageBoxW(g.hwnd,
                L"自动升级脚本不可用，已为你打开下载地址，\n请下载后运行安装（会覆盖升级到原目录）。",
                APP_NAME, MB_OK | MB_ICONWARNING);
            ShellExecuteW(NULL, L"open", info->url, NULL, NULL, SW_SHOWNORMAL);
        } else {
            MessageBoxW(g.hwnd, L"升级信息不完整（缺少下载地址），请联系发布者。",
                        APP_NAME, MB_OK | MB_ICONWARNING);
        }
    }
    free(info);
}

static void on_command(HWND hwnd, int id) {
    switch (id) {
        case ID_NAV_SEND:     switch_page(0); layout(hwnd); /* 补布局：在其他页改窗口大小后返回时控件坐标才不会错位 */ break;
        case ID_NAV_RECV:     switch_page(1); layout(hwnd); /* 触发 1 次画卡片框 */ break;
        case ID_NAV_SETTINGS: switch_page(2); load_settings_to_controls(); layout(hwnd); break;
        case ID_NAV_ABOUT:    switch_page(3); layout(hwnd); break;

        case ID_SEND_REFRESH: discover_refresh(); refresh_device_list(); break;
        case ID_SEND_ADDDEV: {
            wchar_t ipw[128] = {0}, devidw[32] = {0};
            GetWindowTextW(g.sendIp, ipw, 128);
            GetWindowTextW(g.sendDevId, devidw, 32);
            if (!ipw[0]) { MessageBoxW(hwnd, L"请先输入对方的 IP 地址", APP_NAME, MB_OK|MB_ICONINFORMATION); break; }
            int port = _wtoi(devidw);
            if (port <= 0 || port > 65535) port = FILE_PORT;
            /* 转 ANSI IP */
            char ipa[64] = {0};
            WideCharToMultiByte(CP_ACP, 0, ipw, -1, ipa, 64, NULL, NULL);
            if (g.manual_count < MAX_DEVICES) {
                ft_device *d = &g.manual_devices[g.manual_count++];
                ZeroMemory(d, sizeof(*d));
                _snwprintf_s(d->name, MAX_NAME_LEN, _TRUNCATE, L"手动设备 (%s)", ipw);
                strncpy_s(d->ip, sizeof(d->ip), ipa, _TRUNCATE);
                d->port = port;
                d->last_seen = -1;  /* 标记为手动，永不超时 */
                SetWindowTextW(g.sendIp, L"");
                refresh_device_list();
            }
            break;
        }
        case ID_SEND_PICK:    pick_files_send(hwnd); break;
        case ID_SEND_REMOVE: {
            int s = (int)SendMessageW(g.sendFileList, LB_GETCURSEL, 0, 0);
            if (s != LB_ERR) {
                SendMessageW(g.sendFileList, LB_DELETESTRING, s, 0);
                for (int i = s; i < g.send_file_count - 1; i++) {
                    wcscpy_s(g.send_files[i], FT_MAX_PATH, g.send_files[i+1]);
                    wcscpy_s(g.send_names[i], FT_MAX_PATH, g.send_names[i+1]);
                }
                g.send_file_count--;
                wchar_t cnt[128]; _snwprintf_s(cnt, 128, _TRUNCATE, L"设备 %d 台 · 文件 %d 个", g.dev_count, g.send_file_count);
                SetWindowTextW(g.sendCount, cnt);
            }
            break;
        }
        case ID_SEND_CLEAR:
            g.send_file_count = 0;
            SendMessageW(g.sendFileList, LB_RESETCONTENT, 0, 0);
            { wchar_t cnt[128]; _snwprintf_s(cnt, 128, _TRUNCATE, L"附近设备：%d 台  已选文件：0 个", g.dev_count);
              SetWindowTextW(g.sendCount, cnt); }
            break;
        case ID_SEND_START: do_send(); break;

        case ID_RECV_TOGGLE: toggle_recv(); break;
        case ID_RECV_BROWSE: pick_folder_edit(hwnd, g.recvDir); break;

        case ID_SET_SAVEBROWSE: pick_folder_edit(hwnd, GetDlgItem(hwnd, ID_SET_SAVEDIR)); break;

        /* ===== 切换开关：点击时翻转 GWLP_USERDATA ===== */
        case ID_SET_DARKMODE:
            set_toggle(ID_SET_DARKMODE, !get_toggle(ID_SET_DARKMODE));
            g.dark_theme = get_toggle(ID_SET_DARKMODE);
            apply_theme();
            break;
        case ID_SET_AUTOSTART:  set_toggle(ID_SET_AUTOSTART,  !get_toggle(ID_SET_AUTOSTART));  break;
        case ID_SET_AUTOACCEPT: set_toggle(ID_SET_AUTOACCEPT, !get_toggle(ID_SET_AUTOACCEPT)); break;
        case ID_SET_NOTIFY:    set_toggle(ID_SET_NOTIFY,     !get_toggle(ID_SET_NOTIFY));     break;
        case ID_SET_AUTODISC:  set_toggle(ID_SET_AUTODISC,   !get_toggle(ID_SET_AUTODISC));   break;
        case ID_SET_TRAY:      set_toggle(ID_SET_TRAY,       !get_toggle(ID_SET_TRAY));       break;
        case ID_SET_CONFIRMIT: set_toggle(ID_SET_CONFIRMIT,   !get_toggle(ID_SET_CONFIRMIT));  break;
        case ID_SET_SOUND:     set_toggle(ID_SET_SOUND,       !get_toggle(ID_SET_SOUND));     break;
        case ID_SET_SPEED:     set_toggle(ID_SET_SPEED,       !get_toggle(ID_SET_SPEED));     break;
        case ID_SET_AUTOCLEAN: set_toggle(ID_SET_AUTOCLEAN,   !get_toggle(ID_SET_AUTOCLEAN)); break;
        case ID_SET_AUTOUPD:   set_toggle(ID_SET_AUTOUPD,     !get_toggle(ID_SET_AUTOUPD));   break;
        case ID_SET_CHECKUPD:  start_update_check(1); break;

        /* ===== 主题模式：单选互斥，点击选中该主题 ===== */
        case ID_SET_ACCENT0: case ID_SET_ACCENT1: case ID_SET_ACCENT2:
        case ID_SET_ACCENT3: case ID_SET_ACCENT4: case ID_SET_ACCENT5:
        case ID_SET_ACCENT6: case ID_SET_ACCENT7: case ID_SET_ACCENT8:
        case ID_SET_ACCENT9: case ID_SET_ACCENT10: case ID_SET_ACCENT11:
            g.theme_id = id - ID_SET_ACCENT0;
            for (int i = 0; i < NUM_THEMES; i++)
                set_toggle(ID_SET_ACCENT0 + i, i == g.theme_id);
            apply_theme();
            break;

        case ID_SET_REG_MENU:
            {   int on = !get_toggle(ID_SET_REG_MENU);
                set_toggle(ID_SET_REG_MENU, on);
                do_reg_menu(on);
            }
            break;
        case ID_SET_SAVE:       save_settings(); break;
    }
}

/* ---------- 事件：发送/接收进度 ---------- */
static void on_send_ev(ev_post *e) {
    wchar_t line[1024];
    if (e->type == 0) {
        _snwprintf_s(line, 1024, _TRUNCATE, L"[发送开始] %s (总计 %lld B)", e->fname ? e->fname : L"", e->total);
        append_log(g.sendLog, line);
        set_status(g.sendStatus, 1);
        SendMessageW(g.sendProgress, PBM_SETPOS, 0, 0);  /* 每个新文件开始时归零，避免沿用上一文件的 100% */
    } else if (e->type == 1) {
        SendMessageW(g.sendProgress, PBM_SETPOS, e->percent, 0);
    } else if (e->type == 2) {
        SendMessageW(g.sendProgress, PBM_SETPOS, 100, 0);
        _snwprintf_s(line, 1024, _TRUNCATE, L"[发送完成] %s (%lld B)", e->fname ? e->fname : L"", e->total);
        append_log(g.sendLog, line);
        set_status(g.sendStatus, 0);
    } else if (e->type == 3) {
        _snwprintf_s(line, 1024, _TRUNCATE, L"[发送错误] %s - %s", e->fname ? e->fname : L"", e->extra ? e->extra : L"未知错误");
        append_log(g.sendLog, line);
        set_status(g.sendStatus, 3);
    }
}

static void on_recv_ev(ev_post *e) {
    wchar_t line[1024];
    if (e->type == 0) {
        _snwprintf_s(line, 1024, _TRUNCATE, L"[接收开始] %s -> %s (总计 %lld B)",
            e->fname ? e->fname : L"", e->extra ? e->extra : L"", e->total);
        append_log(g.recvLog, line);
    } else if (e->type == 2 || (e->type == 1 && e->percent == 100)) {
        if (e->type == 2) {
            _snwprintf_s(line, 1024, _TRUNCATE, L"[接收完成] %s", e->extra ? e->extra : e->fname);
            append_log(g.recvLog, line);
        }
    } else if (e->type == 3) {
        _snwprintf_s(line, 1024, _TRUNCATE, L"[接收错误] %s", e->extra ? e->extra : L"");
        append_log(g.recvLog, line);
        set_status(g.recvStatus, 3);
    } else if (e->type == 4) {
        wchar_t list_line[FT_MAX_PATH + 32];
        _snwprintf_s(list_line, FT_MAX_PATH+32, _TRUNCATE, L"%s  (%lld KB)",
            e->extra ? e->extra : e->fname, (long long)(e->bytes / 1024));
        int idx = (int)SendMessageW(g.recvList, LB_ADDSTRING, 0, (LPARAM)list_line);
        SendMessageW(g.recvList, LB_SETTOPINDEX, idx, 0);
    }
}

/* ---------- 消息处理 ---------- */

/* 2026-10-12 三次定稿：用户要求退回实色版本，不要亚克力板效果。
 * 仅保留：深色标题栏同步 + 关闭合成特效 + 玻璃帧清零 + 窗口完全不透明。
 * 亚克力/子控件分层方案（set_child_layering + 玻璃帧扩展 + alpha 修正）
 * 已整体移除：洗白、残留、滚动残影问题反复出现，收益配不上维护成本。 */
static void apply_dwm_mica(HWND hwnd) {
    /* 深色模式开关同步到标题栏 */
    BOOL dark = g.dark_theme ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    PFN_SET_WINDOW_COMPOSITION_ATTRIBUTE pSetWCA = hUser ?
        (PFN_SET_WINDOW_COMPOSITION_ATTRIBUTE)GetProcAddress(hUser, "SetWindowCompositionAttribute") : NULL;

    g.blur_active = 0;   /* 客户区永远实色 */

    /* 彻底关闭合成特效（撤销任何历史残留的亚克力状态） */
    if (pSetWCA) {
        ACCENT_POLICY ac = {0};
        ac.AccentState = ACCENT_DISABLED;
        WINDOWCOMPOSITIONATTRIBDATA data = {0};
        data.Attribute = WCA_ACCENT_POLICY;
        data.Data      = &ac;
        data.DataSize  = sizeof(ac);
        pSetWCA(hwnd, &data);
    }
    /* 玻璃帧清零，客户区全部由自己实色绘制 */
    MARGINS zero = { 0, 0, 0, 0 };
    DwmExtendFrameIntoClientArea(hwnd, &zero);

    /* 窗口保持完全不透明 */
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_LAYERED) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex & ~((LONG_PTR)WS_EX_LAYERED));
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g.hwnd = hwnd;
            create_controls(hwnd, ((LPCREATESTRUCT)lp)->hInstance);
            /* 输入框/列表框统一替换为主题色描边 */
            apply_theme_borders(hwnd);
            /* 先在设置页面预填，再切到页面 0 */
            load_settings_to_controls();
            switch_page(0);
            apply_dwm_mica(hwnd);
            /* 启动时后台静默检测新版本（有新版才弹窗，网络失败不打扰） */
            if (g.cfg.auto_update && g.cfg.update_url[0])
                start_update_check(0);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            /* 最小尺寸：足够容纳所有控件且不挤爆 */
            MINMAXINFO *mm = (MINMAXINFO*)lp;
            int minW = S(780), minH = S(520);
            if (mm->ptMinTrackSize.x < minW) mm->ptMinTrackSize.x = minW;
            if (mm->ptMinTrackSize.y < minH) mm->ptMinTrackSize.y = minH;
            return 0;
        }
        case WM_SETCURSOR: {
            /* 鼠标指针不见：明确恢复系统默认箭头，避免 hCursor 无效或子控件误传 */
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
                return TRUE;
            }
            break;
        }
        case WM_SETTINGCHANGE:
        case WM_DPICHANGED:
        case WM_THEMECHANGED:
            apply_dwm_mica(hwnd);
            layout(hwnd);
            return 0;
        case WM_SIZE:
            layout(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_VSCROLL: {
            SCROLLINFO si = { sizeof(si) }; si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int old = si.nPos;
            switch (LOWORD(wp)) {
                case SB_LINEUP:        si.nPos -= S(26); break;
                case SB_LINEDOWN:      si.nPos += S(26); break;
                case SB_PAGEUP:        si.nPos -= (int)si.nPage; break;
                case SB_PAGEDOWN:      si.nPos += (int)si.nPage; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: si.nPos = si.nTrackPos; break;
                case SB_TOP:           si.nPos = si.nMin; break;
                case SB_BOTTOM:        si.nPos = si.nMax; break;
            }
            si.fMask = SIF_POS;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            GetScrollInfo(hwnd, SB_VERT, &si);
            if (si.nPos != old) {
                g.scroll_y = si.nPos;
                layout(hwnd);
                InvalidateRect(hwnd, NULL, TRUE);
                UpdateWindow(hwnd);
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            RECT rc; GetClientRect(hwnd, &rc);
            int visH = rc.bottom - S(20); if (visH < S(120)) visH = S(120);
            int scroll_max = g.content_total - visH; if (scroll_max < 0) scroll_max = 0;
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            if (lines < 1) lines = 1;
            int step = S(26) * (int)lines;
            int newy = g.scroll_y - (delta / WHEEL_DELTA) * step;
            if (newy < 0) newy = 0;
            if (newy > scroll_max) newy = scroll_max;
            if (newy != g.scroll_y) {
                g.scroll_y = newy;
                SCROLLINFO si = { sizeof(si) }; si.fMask = SIF_POS; si.nPos = g.scroll_y;
                SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
                layout(hwnd);
                InvalidateRect(hwnd, NULL, TRUE);
                UpdateWindow(hwnd);
            }
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: on_paint(hwnd); return 0;
        case WM_COMMAND: on_command(hwnd, LOWORD(wp)); return 0;
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
            if (!dis) break;
            wchar_t txt[128] = {0};
            GetWindowTextW(dis->hwndItem, txt, 128);
            /* 切换开关 */
            if (dis->CtlID == ID_SET_AUTOSTART || dis->CtlID == ID_SET_AUTOACCEPT ||
                dis->CtlID == ID_SET_NOTIFY || dis->CtlID == ID_SET_AUTODISC ||
                dis->CtlID == ID_SET_DARKMODE || dis->CtlID == ID_SET_TRAY ||
                dis->CtlID == ID_SET_CONFIRMIT || dis->CtlID == ID_SET_SOUND ||
                dis->CtlID == ID_SET_SPEED || dis->CtlID == ID_SET_AUTOCLEAN ||
                dis->CtlID == ID_SET_REG_MENU || dis->CtlID == ID_SET_AUTOUPD ||
                dis->CtlID == ID_SET_ACCENT0 || dis->CtlID == ID_SET_ACCENT1 ||
                dis->CtlID == ID_SET_ACCENT2 || dis->CtlID == ID_SET_ACCENT3 ||
                dis->CtlID == ID_SET_ACCENT4 || dis->CtlID == ID_SET_ACCENT5 ||
                dis->CtlID == ID_SET_ACCENT6 || dis->CtlID == ID_SET_ACCENT7 ||
                dis->CtlID == ID_SET_ACCENT8 || dis->CtlID == ID_SET_ACCENT9 ||
                dis->CtlID == ID_SET_ACCENT10 || dis->CtlID == ID_SET_ACCENT11) {
                int checked = (int)GetWindowLongPtrW(dis->hwndItem, GWLP_USERDATA);
                RECT r = dis->rcItem;
                HDC dc = dis->hDC;
                /* 第一步：整块控件用 COL_CONTENT 填实底，彻底擦掉系统默认绘制留下的白边/灰边 */
                HBRUSH bgBr = CreateSolidBrush(COL_CONTENT);
                FillRect(dc, &r, bgBr);
                DeleteObject(bgBr);

                /* 开关轨道（居中垂直，宽 44 高 24） */
                int tw = S(44), th = S(24);
                int kx = r.left;
                int ky = r.top + (r.bottom - r.top - th) / 2;
                RECT track = { kx, ky, kx + tw, ky + th };
                /* 轨道：先画圆角外框区域，不使用纯色矩形导致周围白边 */
                int trkR = th / 2;
                HRGN trkRgn = CreateRoundRectRgn(track.left, track.top, track.right + 1, track.bottom + 1, trkR, trkR);
                COLORREF trkCol = checked ? COL_BTN : (g.dark_theme ? RGB(70, 72, 100) : RGB(200, 202, 215));
                HBRUSH trkBr = CreateSolidBrush(trkCol);
                FillRgn(dc, trkRgn, trkBr);
                DeleteObject(trkBr);
                /* 开关轨道外描边：1px 清晰线条（轨道色加深 22%） */
                {
                    COLORREF te = RGB((GetRValue(trkCol) * 78) / 100,
                                      (GetGValue(trkCol) * 78) / 100,
                                      (GetBValue(trkCol) * 78) / 100);
                    HBRUSH tb = CreateSolidBrush(te);
                    FrameRgn(dc, trkRgn, tb, 1, 1);
                    DeleteObject(tb);
                }
                /* 圆形旋钮：内切圆，直径 th-4，左右切换位置 */
                int ksz = th - S(4);
                int knobX = checked ? (track.right - ksz - S(2)) : (track.left + S(2));
                int knobY = ky + S(2);
                HRGN knRgn = CreateEllipticRgn(knobX, knobY, knobX + ksz, knobY + ksz);
                /* 旋钮填充：白色本体 */
                HBRUSH knBr = CreateSolidBrush(RGB(255, 255, 255));
                FillRgn(dc, knRgn, knBr);
                DeleteObject(knBr);
                /* 旋钮边缘：抗锯齿描边 1px 柔和色（避免纯白色圆与背景直接接触造成锯齿感） */
                COLORREF knobEdge = checked ? COL_BTN_PRS : (g.dark_theme ? RGB(90, 92, 120) : RGB(220, 222, 232));
                HPEN knPen = CreatePen(PS_SOLID, 1, knobEdge);
                HPEN oldPen = (HPEN)SelectObject(dc, knPen);
                SetBkMode(dc, TRANSPARENT);
                Ellipse(dc, knobX, knobY, knobX + ksz, knobY + ksz);
                SelectObject(dc, oldPen);
                DeleteObject(knPen);
                DeleteObject(trkRgn);
                DeleteObject(knRgn);

                /* 文字标签（开关右侧，带足够间距不重叠） */
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, COL_TEXT);
                HFONT oldF = (HFONT)SelectObject(dc, g.font);
                /* 文字左侧预留 S(56) = 开关宽 + 12px 间距；右侧距离控件边距 8px */
                RECT tr = { r.left + S(56), r.top, r.right - S(8), r.bottom };
                DrawTextW(dc, txt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                SelectObject(dc, oldF);
                return TRUE;
            }
            if (dis->CtlID == ID_NAV_SEND || dis->CtlID == ID_NAV_RECV ||
                dis->CtlID == ID_NAV_SETTINGS || dis->CtlID == ID_NAV_ABOUT) {
                draw_nav_btn(dis, txt);
            } else if (dis->CtlID == ID_SEND_START || dis->CtlID == ID_SET_SAVE ||
                       dis->CtlID == ID_SET_CHECKUPD) {
                draw_ownerdraw_btn(dis, txt, COL_BTN, RGB(255,255,255));
            } else if (dis->CtlID == ID_SEND_PICK) {
                draw_ownerdraw_btn(dis, txt, COL_BTN_GRN, RGB(255,255,255));
            } else if (dis->CtlID == ID_SEND_ADDDEV ||
                       dis->CtlID == ID_SEND_REFRESH || dis->CtlID == ID_RECV_TOGGLE) {
                COLORREF c = COL_BTN;
                if (dis->CtlID == ID_RECV_TOGGLE && g.recv_running) c = COL_BTN_RED;
                draw_ownerdraw_btn(dis, txt, c, RGB(255,255,255));
            } else if (dis->CtlID == ID_SEND_REMOVE || dis->CtlID == ID_SEND_CLEAR ||
                       dis->CtlID == ID_RECV_BROWSE || dis->CtlID == ID_SET_SAVEBROWSE) {
                draw_ownerdraw_btn(dis, txt, COL_BTN_GHOST, COL_TEXT);
            }
            return TRUE;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wp; HWND child = (HWND)lp;
            /* 子控件背景一律实色：黑色透明背景会让系统绘制的子控件
             * 穿模/发虚（它们不经过双缓冲 DIB 的 alpha 修正）。
             * COL_CONTENT 与模糊染色层同色，视觉上无色差 */
            if (child == g.sendLog || child == g.recvLog) {
                /* 只读日志框：保持深色日志底 */
                SetBkMode(dc, OPAQUE);
                SetTextColor(dc, COL_LOG_FG);
                SetBkColor(dc, COL_LOG_BG);
                return (LRESULT)g.brLog;
            }
            SetBkMode(dc, TRANSPARENT);
            if (child == g.sendStatus || child == g.recvStatus) {
                SetTextColor(dc, (COLORREF)GetWindowLongPtrW(child, GWLP_USERDATA));
                return (LRESULT)g.brContent;
            }
            if (child == g.recvIp || child == g.sendCount) {
                SetTextColor(dc, COL_SUB);
                return (LRESULT)g.brContent;
            }
            SetTextColor(dc, COL_TEXT);
            return (LRESULT)g.brContent;
        }
        case WM_CTLCOLORBTN: {
            HDC dc = (HDC)wp; HWND child = (HWND)lp;
            (void)child;
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, COL_TEXT);
            return (LRESULT)g.brContent;
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wp; HWND child = (HWND)lp;
            if (child == g.sendLog || child == g.recvLog) {
                SetBkMode(dc, OPAQUE);
                SetTextColor(dc, COL_LOG_FG);
                SetBkColor(dc, COL_LOG_BG);
                return (LRESULT)g.brLog;
            }
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, COL_TEXT); SetBkColor(dc, COL_INPUT_BG);
            return (LRESULT)g.brInput;
        }
        case WM_CTLCOLORLISTBOX: {
            HDC dc = (HDC)wp;
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, COL_TEXT); SetBkColor(dc, COL_LIST_BG);
            return (LRESULT)g.brList;
        }
        case WM_NOTIFY: {
            NMHDR *hdr = (NMHDR*)lp;
            if (hdr && hdr->idFrom == ID_SEND_DEVLIST && hdr->code == LVN_ITEMCHANGED) {
                LRESULT sel = ListView_GetNextItem(g.sendDevList, -1, LVNI_SELECTED);
                g.selected_dev_idx = (int)sel;
            }
            /* ListView 自定义绘制：文字/背景色适配 + 黑色行分隔线 */
            if (hdr && hdr->idFrom == ID_SEND_DEVLIST && hdr->code == NM_CUSTOMDRAW) {
                LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lp;
                switch (cd->nmcd.dwDrawStage) {
                    case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
                    case CDDS_ITEMPREPAINT:
                        cd->clrText = COL_TEXT;
                        cd->clrTextBk = COL_LIST_BG;
                        if ((int)cd->nmcd.dwItemSpec == g.selected_dev_idx) {
                            cd->clrTextBk = COL_LIST_SEL;
                        }
                        return CDRF_NEWFONT | CDRF_NOTIFYPOSTPAINT;
                    case CDDS_ITEMPOSTPAINT: {
                        /* 行分隔线：浅色模式近黑、深色模式主题分割色
                         * （替代原白色系统网格线，用户要求表格线条改黑） */
                        RECT rr = cd->nmcd.rc;
                        if (rr.right > rr.left && rr.bottom > rr.top) {
                            HPEN pn = CreatePen(PS_SOLID, 1,
                                g.dark_theme ? COL_DIVIDER : RGB(12, 12, 12));
                            HGDIOBJ op = SelectObject(cd->nmcd.hdc, pn);
                            MoveToEx(cd->nmcd.hdc, rr.left, rr.bottom - 1, NULL);
                            LineTo(cd->nmcd.hdc, rr.right, rr.bottom - 1);
                            SelectObject(cd->nmcd.hdc, op);
                            DeleteObject(pn);
                        }
                        return CDRF_DODEFAULT;
                    }
                }
            }
            return 0;
        }
        case WM_LOG: {
            log_post *p = (log_post*)lp;
            if (p) {
                HWND ed = (g.page == 0) ? g.sendLog : (g.page == 1 ? g.recvLog : NULL);
                if (ed) append_log(ed, p->msg);
                free(p->msg); free(p);
            }
            return 0;
        }
        case WM_DISCOVER: {
            wchar_t *a = (wchar_t*)lp;
            if (a) { free(a); refresh_device_list(); }
            return 0;
        }
        case WM_SENDEV: {
            ev_post *e = (ev_post*)lp;
            if (e) { on_send_ev(e); free(e->fname); free(e->extra); free(e); }
            return 0;
        }
        case WM_RECVEV: {
            ev_post *e = (ev_post*)lp;
            if (e) { on_recv_ev(e); free(e->fname); free(e->extra); free(e); }
            return 0;
        }
        case WM_UPDEV: {
            on_update_ev(wp, lp);
            return 0;
        }
        case WM_DESTROY:
            discover_shutdown();
            ft_recv_stop();
            ft_send_shutdown();
            log_close();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------- 把命令行 --send 参数里的文件，加入待发送列表 ---------- */
static void process_cmdline_send(int argc, wchar_t **argv) {
    /* argc/argv 已解析：寻找 --send 后面的所有非选项参数作为文件路径 */
    int in_send_mode = 0;
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--send")) {
            in_send_mode = 1;
            continue;
        }
        if (!wcscmp(argv[i], L"--tray")) {
            in_send_mode = 0;
            continue;
        }
        /* 如果当前处于 --send 模式，并且参数不以 -- 开头，则认为是文件路径 */
        if (in_send_mode && argv[i][0] != L'-') {
            sendfile_add_one(argv[i]);
        }
    }
}

/* ---------- 入口 ---------- */
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE prev, LPWSTR cmd, int show) {
    (void)prev;
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    /* --- 初始化日志 --- */
    wchar_t appdata[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata) == S_OK) {
        wchar_t ft_dir[MAX_PATH];
        _snwprintf_s(ft_dir, MAX_PATH, _TRUNCATE, L"%s\\My File Transfer Assistant", appdata);
        CreateDirectoryW(ft_dir, NULL);
        log_init(ft_dir);
    }
    log_set_sink(log_cb_adapter);

    /* --- 加载配置 --- */
    config_load(&g.cfg);
    LOG_INFO(L"已加载配置: nickname=%s tcp=%d udp=%d save_dir=%s",
             g.cfg.nickname, g.cfg.tcp_port, g.cfg.udp_port, g.cfg.save_dir);

    /* --- 启动设备发现（UDP） --- */
    discover_init(g.cfg.nickname[0] ? g.cfg.nickname : L"未命名设备", g.cfg.tcp_port);
    discover_set_callback(discover_cb);

    /* --- 初始化发送端 --- */
    ft_send_init();
    ft_send_set_callback(send_cb_adapter);
    ft_recv_set_callback(recv_cb_adapter);

    /* --- 命令行：--send "path" --- */
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(cmd, &argc);
    /* argv 不包含程序自身，所以我们重取完整命令行（含程序名）也不影响 */
    process_cmdline_send(argc, argv);
    LocalFree(argv);

    /* --- 主窗口 --- */
    const wchar_t *cls = L"MyFileTransferAssistantMain";
    /* 提前同步暗色模式，用于窗口类背景画笔（消除白边） */
    g.dark_theme = g.cfg.dark_mode ? 1 : 0;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    /* 背景画笔匹配内容区颜色，避免窗口边缘/调整大小时露出白边 */
    wc.hbrBackground = CreateSolidBrush(g.dark_theme ? RGB(20,21,36) : RGB(250,250,252));
    wc.lpszClassName = cls;
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    /* 根据屏幕分辨率 + DPI 计算窗口大小 */
    g.ui_scale = compute_ui_scale();
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = (int)(sw * 0.82);
    int wh = (int)(sh * 0.82);
    if (ww < S(1000)) ww = sw - S(60);
    if (wh < S(700)) wh = sh - S(60);
    int wx = (sw - ww) / 2;
    int wy = (sh - wh) / 2;

    HWND hwnd = CreateWindowExW(0, cls, APP_NAME,
        WS_OVERLAPPEDWINDOW | WS_MAXIMIZEBOX | WS_THICKFRAME | WS_VSCROLL,
        wx, wy, ww, wh, NULL, NULL, hi, NULL);
    if (!hwnd) return 0;

    HICON big = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    if (big) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)big);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)big);
    }

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    /* 把接收端 dir/port 设置框填上 */
    SetWindowTextW(g.recvDir, g.cfg.save_dir);
    wchar_t tp[16]; _snwprintf_s(tp, 16, _TRUNCATE, L"%d", g.cfg.tcp_port);
    SetWindowTextW(g.recvPort, tp);

    /* 如果有 --send 参数，自动切到发送页 */
    if (g.send_file_count > 0) {
        switch_page(0);
    }

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    CoUninitialize();
    return (int)m.wParam;
}
