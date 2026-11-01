/*
 * dllmain.c - MyFileTransferAssistant 核心 DLL 入口与导出定义
 *   导出: 日志 / 配置 / 设备发现 / 文件传输(收发) / 注册表菜单
 */
#include <windows.h>

#define SYNC_DLL_EXPORTS 1
#include "log.h"
#include "json.h"
#include "config.h"
#include "discover.h"
#include "transfer.h"
#include "registry.h"

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID r) {
    (void)h; (void)r;
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(h);
            break;
        case DLL_PROCESS_DETACH:
            break;
        default: break;
    }
    return TRUE;
}
