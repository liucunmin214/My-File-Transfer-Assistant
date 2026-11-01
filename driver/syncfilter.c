/*
 * syncfilter.sys - DesktopSync 文件系统过滤驱动
 *
 * 作用：在用户启用"驱动增强模式"时挂载到目标同步目录，
 *      通过共享内存向用户态推送文件变化事件，实现亚秒级实时同步。
 *
 * 说明：本驱动需要通过 WDK 编译（bld.bat 仅在用户态构建 EXE/DLL；
 *       驱动构建脚本见 driver/build_wdk.bat）。默认安装不会启用本驱动，
 *       应用将退回到 ReadDirectoryChangesW 用户态轮询模式。
 *
 * Altitude：360500（FSFilter Activity Monitor 区间）
 */

#include <fltKernel.h>
#include <dontuse.h>
#include <suppress.h>

#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION, "Not consistent with PREfast")

#define SYNCFLT_TAG        'SFSy'   /* ySFS - SyncFilter */
#define SYNCFLT_PORT_NAME  L"\\SyncFilterPort"
#define SYNCFLT_MAX_MSG    4096

/* ---- 前置声明 ---- */
DRIVER_INITIALIZE DriverEntry;

DRIVER_UNLOAD SyncFilterUnload;

FLT_INSTANCE_SETUP_CALLBACK SyncFilterInstanceSetup;
FLT_INSTANCE_QUERY_TEARDOWN_CALLBACK SyncFilterInstanceQueryTeardown;
FLT_INSTANCE_TEARDOWN_CALLBACK SyncFilterInstanceTeardown;

FLT_PREOP_CALLBACK_STATUS SyncFilterPreCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext);

NTSTATUS SyncFilterConnect(
    _In_ PFLT_PORT ClientPort,
    _In_opt_ PVOID ServerPortCookie,
    _In_reads_bytes_opt_(SizeOfContext) PVOID ConnectionContext,
    _In_ ULONG SizeOfContext,
    _Outptr_result_maybenull_ PVOID *ConnectionCookie);

VOID SyncFilterDisconnect(
    _In_opt_ PVOID ConnectionCookie);

/* ---- 全局：过滤管理器句柄 + 用户态通信端口 ---- */
PFLT_FILTER  g_FilterHandle = NULL;
PFLT_PORT    g_ServerPort   = NULL;
PFLT_PORT    g_ClientPort   = NULL;
FAST_MUTEX   g_PortLock;

/* ---- 上下文：记录在用户态已注册的同步根路径 ---- */
typedef struct _SYNC_ROOT_CTX {
    UNICODE_STRING RootPath;        /* 例：\Device\HarddiskVolume3\Users\YourName\Desktop */
    USHORT          RootPathLen;
    UCHAR           Buffer[512];
} SYNC_ROOT_CTX, *PSYNC_ROOT_CTX;

/* ---- 操作回调表 ---- */
const FLT_OPERATION_REGISTRATION Callbacks[] = {
    { IRP_MJ_CREATE,
      0,
      SyncFilterPreCreate,
      NULL },

    { IRP_MJ_CLEANUP,
      0,
      SyncFilterPreCreate,
      NULL },

    { IRP_MJ_WRITE,
      0,
      SyncFilterPreCreate,
      NULL },

    { IRP_MJ_SET_INFORMATION,
      0,
      SyncFilterPreCreate,
      NULL },

    { IRP_MJ_OPERATION_END }
};

/* ---- 上下文注册 ---- */
const FLT_CONTEXT_REGISTRATION ContextRegistration[] = {
    { FLT_STREAMHANDLE_CONTEXT,
      0,
      NULL,
      sizeof(SYNC_ROOT_CTX),
      SYNCFLT_TAG },

    { FLT_CONTEXT_END }
};

/* ---- 实例回调 ---- */
const FLT_INSTANCE_SETUP_INFO SyncFilterInstanceSetupInfo = {
    sizeof(FLT_INSTANCE_SETUP_INFO),
    SyncFilterInstanceSetup,
    SyncFilterInstanceQueryTeardown,
    SyncFilterInstanceTeardown,
    NULL,
    NULL
};

const FLT_REGISTRATION FilterRegistration = {
    sizeof(FLT_REGISTRATION),               /* Size */
    FLT_REGISTRATION_VERSION,               /* Version */
    0,                                      /* Flags */
    ContextRegistration,                    /* Context */
    Callbacks,                              /* OperationRegistration */
    SyncFilterUnload,                       /* FilterUnloadCallback */
    &SyncFilterInstanceSetupInfo,           /* InstanceSetupCallback */
    NULL,                                   /* InstanceQueryTeardownCallback */
    NULL,                                   /* InstanceTeardownCallback */
    NULL,                                   /* GenerateFileNameCallback */
    NULL,                                   /* NormalizeNameComponentCallback */
    NULL,                                   /* NormalizeContextCleanupCallback */
    NULL,                                   /* TransactionNotificationCallback */
    NULL,                                   /* NormalizeNameComponentExCallback */
    NULL                                    /* SectionNotificationCallback */
};

/* ---- 实例回调实现 ---- */
NTSTATUS FLTAPI
SyncFilterInstanceSetup(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ ULONG VolumeStatus,
    _In_ FLT_FILESYSTEM_TYPE VolFsType)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(VolumeStatus);

    /* 只挂载到 NTFS / ReFS / FAT32 卷；跳过网络盘与光驱 */
    if (VolFsType == FLT_FSTYPE_UNKNOWN  ||
        VolFsType == FLT_FSTYPE_NETWORK  ||
        VolFsType == FLT_FSTYPE_CDFS     ||
        VolFsType == FLT_FSTYPE_UDFS) {
        return STATUS_FLT_DO_NOT_ATTACH;
    }
    return STATUS_SUCCESS;
}

NTSTATUS FLTAPI
SyncFilterInstanceQueryTeardown(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);
    return STATUS_SUCCESS;
}

VOID FLTAPI
SyncFilterInstanceTeardown(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);
}

/* ---- 用户态通信：连接 / 断开 ---- */
NTSTATUS FLTAPI
SyncFilterConnect(
    _In_ PFLT_PORT ClientPort,
    _In_opt_ PVOID ServerPortCookie,
    _In_reads_bytes_opt_(SizeOfContext) PVOID ConnectionContext,
    _In_ ULONG SizeOfContext,
    _Outptr_result_maybenull_ PVOID *ConnectionCookie)
{
    UNREFERENCED_PARAMETER(ServerPortCookie);
    UNREFERENCED_PARAMETER(ConnectionContext);
    UNREFERENCED_PARAMETER(SizeOfContext);

    ExAcquireFastMutex(&g_PortLock);
    if (g_ClientPort != NULL) {
        ExReleaseFastMutex(&g_PortLock);
        return STATUS_PORT_DISCONNECTED;  /* 只允许单个用户态连接 */
    }
    g_ClientPort = ClientPort;
    *ConnectionCookie = NULL;
    ExReleaseFastMutex(&g_PortLock);

    DbgPrint("[SyncFilter] User-mode client connected\n");
    return STATUS_SUCCESS;
}

VOID FLTAPI
SyncFilterDisconnect(
    _In_opt_ PVOID ConnectionCookie)
{
    UNREFERENCED_PARAMETER(ConnectionCookie);

    ExAcquireFastMutex(&g_PortLock);
    if (g_ClientPort) {
        FltCloseClientPort(g_FilterHandle, &g_ClientPort);
        g_ClientPort = NULL;
    }
    ExReleaseFastMutex(&g_PortLock);

    DbgPrint("[SyncFilter] User-mode client disconnected\n");
}

/* ---- 通知用户态：发送一条变化事件 ---- */
static VOID SyncFilterNotifyEvent(
    _In_ LPCWSTR Type,         /* "CREATE" / "WRITE" / "DELETE" / "RENAME" */
    _In_ PUNICODE_STRING Path)
{
    PFLT_PORT client;

    ExAcquireFastMutex(&g_PortLock);
    client = g_ClientPort;
    if (client) FltObjectReference(client);
    ExReleaseFastMutex(&g_PortLock);

    if (!client) return;

    /* 帧：[type][\0][path_utf16] */
    struct {
        USHORT TypeLen; USHORT PathLen; USHORT Reserved;
        WCHAR  Buffer[(SYNCFLT_MAX_MSG - sizeof(USHORT) * 3) / sizeof(WCHAR)];
    } msg;
    RtlZeroMemory(&msg, sizeof(msg));

    msg.TypeLen = (USHORT)(wcslen(Type) * sizeof(WCHAR));
    msg.PathLen = (USHORT)Path->Length;

    size_t cap = sizeof(msg.Buffer) - msg.TypeLen - sizeof(WCHAR);
    if (msg.PathLen > cap) msg.PathLen = (USHORT)cap;

    RtlCopyMemory(msg.Buffer, Type, msg.TypeLen);
    RtlCopyMemory((PUCHAR)msg.Buffer + msg.TypeLen,
                  Path->Buffer, msg.PathLen);

    LARGE_INTEGER timeout;
    timeout.QuadPart = -100000;  /* 10ms 超时，绝不阻塞系统线程 */
    FltSendMessage(client, NULL, &msg, sizeof(msg), &timeout);

    FltObjectDereference(client);
}

/* ---- Pre-operation 回调 ---- */
FLT_PREOP_CALLBACK_STATUS FLTAPI
SyncFilterPreCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext)
{
    UNREFERENCED_PARAMETER(FltObjects);
    *CompletionContext = NULL;

    /* 只关心对文件实际写入/创建/删除/改名时的事件 */
    if (Data->RequestorMode != UserMode &&
        Data->RequestorMode != KernelMode) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    NTSTATUS st = FltGetFileNameInformation(Data,
                FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT,
                &nameInfo);
    if (!NT_SUCCESS(st)) return FLT_PREOP_SUCCESS_NO_CALLBACK;

    FltParseFileNameInformation(nameInfo);

    /* 仅在用户启用了驱动模式时发送事件，否则放行 */
    if (g_ClientPort) {
        LPCWSTR type = L"WRITE";
        if (Data->Iopb->MajorFunction == IRP_MJ_CREATE) {
            if (Data->Iopb->Parameters.Create.Options & FILE_DELETE_ON_CLOSE)
                type = L"DELETE";
            else if (Data->Iopb->OperationFlags & FLTFL_OPERATION_REGISTRATION_SKIP_PAGING_IO)
                type = L"CREATE";
        }
        else if (Data->Iopb->MajorFunction == IRP_MJ_SET_INFORMATION) {
            FILE_INFORMATION_CLASS fic =
                Data->Iopb->Parameters.SetFileInformation.FileInformationClass;
            if (fic == FileDispositionInformation ||
                fic == FileDispositionInformationEx) type = L"DELETE";
            else if (fic == FileRenameInformation ||
                     fic == FileRenameInformationEx) type = L"RENAME";
        }
        SyncFilterNotifyEvent(type, &nameInfo->Name);
    }

    FltReleaseFileNameInformation(nameInfo);
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

/* ---- 卸载 ---- */
VOID FLTAPI
SyncFilterUnload(
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    DbgPrint("[SyncFilter] Unloading...\n");

    ExAcquireFastMutex(&g_PortLock);
    if (g_ClientPort) FltCloseClientPort(g_FilterHandle, &g_ClientPort);
    ExReleaseFastMutex(&g_PortLock);

    if (g_ServerPort) FltCloseServerPort(g_FilterHandle, &g_ServerPort);
    if (g_FilterHandle) FltUnregisterFilter(g_FilterHandle);

    DbgPrint("[SyncFilter] Unloaded\n");
}

/* ---- 入口 ---- */
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    ExInitializeFastMutex(&g_PortLock);

    NTSTATUS st = FltRegisterFilter(DriverObject, &FilterRegistration, &g_FilterHandle);
    if (!NT_SUCCESS(st)) {
        DbgPrint("[SyncFilter] FltRegisterFilter failed: 0x%08X\n", st);
        return st;
    }

    /* 创建用户态通信端口 */
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING portName = RTL_CONSTANT_STRING(SYNCFLT_PORT_NAME);
    InitializeObjectAttributes(&oa, &portName,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL, NULL);

    st = FltCreateCommunicationPort(g_FilterHandle,
            &g_ServerPort,
            &oa,
            NULL,
            SyncFilterConnect,
            SyncFilterDisconnect,
            1,
            FLT_PORT_LISTEN_TYPE_SYNC);   /* 仅允许单连接 */

    if (!NT_SUCCESS(st)) {
        DbgPrint("[SyncFilter] FltCreateCommunicationPort failed: 0x%08X\n", st);
        FltUnregisterFilter(g_FilterHandle);
        g_FilterHandle = NULL;
        return st;
    }

    DbgPrint("[SyncFilter] Loaded, port=%ws\n", SYNCFLT_PORT_NAME);
    return STATUS_SUCCESS;
}
