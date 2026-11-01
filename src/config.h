/*
 * config.h - 配置读写接口（封装 JSON + AppData 路径）
 */
#ifndef FT_CONFIG_H
#define FT_CONFIG_H

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    wchar_t  save_dir[1024];       /* 默认保存目录 */
    wchar_t  nickname[64];          /* 本机显示昵称 */
    int      tcp_port;              /* 文件传输监听端口 */
    int      udp_port;              /* 设备发现 UDP 端口 */
    int      autostart;             /* 是否开机自启 0/1 */
    int      auto_accept;           /* 自动接收 0/1（默认 1） */
    int      show_notification;     /* 完成后弹窗 0/1（默认 1） */
    int      autodiscover;          /* 自动扫描附近设备 0/1（默认 1） */
    int      dark_mode;             /* 深色主题 0/1（默认 0） */
    int      theme_id;             /* 主题模式 0-5（默认 0 靛紫） */
    /* 高级设置 */
    int      minimize_to_tray;      /* 关闭时最小化到托盘 0/1（默认 0） */
    int      confirm_on_exit;       /* 退出前确认 0/1（默认 1） */
    int      sound_notify;          /* 传输完成声音提示 0/1（默认 1） */
    int      show_speed;            /* 显示传输速度 MB/s 0/1（默认 1） */
    int      auto_cleanup_sent;     /* 发送完成后清空文件列表 0/1（默认 0） */
    int      acrylic_effect;        /* 亚克力玻璃效果 0/1（默认 1） */
    int      window_opacity;         /* 窗口透明度 50-100（默认 96） */
    int      blur_amount;            /* 背景模糊强度 0-100（默认 70） */
    wchar_t  last_dest_ip[64];     /* 上次发送目标 IP（历史） */
    int      last_dest_port;        /* 上次发送目标端口 */
    /* 软件升级 */
    int      auto_update;           /* 启动时自动检查更新 0/1（默认 1） */
    wchar_t  update_url[1024];      /* 服务器 update.json 地址 */
} ft_config;

/* 初始化：从 AppData\Roaming\My File Transfer Assistant\config.json 加载，不存在则写默认值 */
int  config_load(ft_config *cfg);

/* 保存到文件 */
int  config_save(const ft_config *cfg);

/* 取 AppData 配置目录（结尾不带反斜杠） */
void config_get_dir(wchar_t *out, int out_chars);

/* 取默认保存目录：用户文档\文件传输接收 或 桌面\文件传输接收 */
void config_default_save_dir(wchar_t *out, int out_chars);

/* 取本机默认昵称：主机名 */
void config_default_nickname(wchar_t *out, int out_chars);

#ifdef __cplusplus
}
#endif

#endif /* FT_CONFIG_H */
