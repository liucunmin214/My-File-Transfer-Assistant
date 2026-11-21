using System.Runtime.InteropServices;
using System.Text;

namespace MyFileTransferAssistant;

/* 与 src/config.h 中 ft_config 完全一致的内存布局。 */
[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
public struct FtConfig
{
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 1024)] public string save_dir;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string nickname;
    public int tcp_port;
    public int udp_port;
    public int autostart;
    public int auto_accept;
    public int show_notification;
    public int autodiscover;
    public int dark_mode;
    public int theme_id;
    public int minimize_to_tray;
    public int confirm_on_exit;
    public int sound_notify;
    public int show_speed;
    public int auto_cleanup_sent;
    public int acrylic_effect;
    public int window_opacity;
    public int blur_amount;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string last_dest_ip;
    public int last_dest_port;
    public int auto_update;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 1024)] public string update_url;
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
public struct FtUpdateInfo
{
    public int has_update;
    public int rollback;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string version;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 1024)] public string notes;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 1024)] public string url;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 1024)] public string script;
}

public static class Core
{
    private const string Dll = "sync_core.dll";
    public const string AppVersion = "V1.3.0";
    public const string RepoUrl = "https://github.com/liucunmin214/My-File-Transfer-Assistant";

    public static FtConfig Config;
    public static event Action<TransferEvent>? SendEvent;
    public static event Action<TransferEvent>? ReceiveEvent;

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void TransferCallback(int type, long taskId, int percent, long bytes,
        long total, IntPtr fileName, IntPtr extra);

    private static TransferCallback? _sendCallback;
    private static TransferCallback? _receiveCallback;

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int config_load(ref FtConfig cfg);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int config_save(ref FtConfig cfg);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern void config_get_dir(StringBuilder output, int outputChars);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern int log_init(string appDataDir);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern void log_close();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern int discover_init(string myName, int fileTcpPort);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern void discover_shutdown();
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int discover_get_devices(IntPtr output, int maxCount);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern void discover_refresh();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int ft_send_init();
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern void ft_send_set_callback(TransferCallback callback);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    private static extern long ft_send_files(string destIp, int destPort, IntPtr items, int itemCount);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern void ft_send_cancel(long groupId);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern void ft_send_shutdown();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern int ft_recv_start(int port, string saveDir);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern void ft_recv_stop();
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int ft_recv_is_running();
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern void ft_recv_set_save_dir(string saveDir);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern void ft_recv_set_callback(TransferCallback callback);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern int reg_register_shell_menu(string exePath, string iconPath);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int reg_unregister_shell_menu();
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int reg_is_shell_menu_registered();
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern int reg_register_autostart(string exePath);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int reg_unregister_autostart();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern int up_check(string jsonUrl, string localVersion, ref FtUpdateInfo output);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern int up_run_upgrade(string scriptUrl);

    public static void Initialize()
    {
        var dir = new StringBuilder(260);
        config_get_dir(dir, dir.Capacity);
        log_init(dir.ToString());
        config_load(ref Config);

        discover_init(string.IsNullOrWhiteSpace(Config.nickname) ? "未命名设备" : Config.nickname,
            Config.tcp_port);
        ft_send_init();

        // 静态保存委托，防止 native 回调期间被 GC 回收。
        _sendCallback = OnNativeSendEvent;
        _receiveCallback = OnNativeReceiveEvent;
        ft_send_set_callback(_sendCallback);
        ft_recv_set_callback(_receiveCallback);
    }

    public static void Shutdown()
    {
        discover_shutdown();
        ft_recv_stop();
        ft_send_shutdown();
        log_close();
    }

    public static void RestartDiscovery()
    {
        discover_shutdown();
        discover_init(string.IsNullOrWhiteSpace(Config.nickname) ? "未命名设备" : Config.nickname,
            Config.tcp_port);
    }

    public static void RefreshDevices() => discover_refresh();

    public static List<DeviceItem> GetDevices()
    {
        const int maxDevices = 32;
        const int deviceSize = 280; // wchar[64] + wchar[40] + char[64] + int + int
        var buffer = Marshal.AllocHGlobal(deviceSize * maxDevices);
        var result = new List<DeviceItem>();
        try
        {
            int count = discover_get_devices(buffer, maxDevices);
            for (int i = 0; i < count; i++)
            {
                IntPtr item = buffer + i * deviceSize;
                result.Add(new DeviceItem
                {
                    Name = Marshal.PtrToStringUni(item, 64)?.TrimEnd('\0') ?? "",
                    Id = Marshal.PtrToStringUni(item + 128, 40)?.TrimEnd('\0') ?? "",
                    Ip = Marshal.PtrToStringAnsi(item + 208, 64)?.TrimEnd('\0') ?? "",
                    Port = Marshal.ReadInt32(item + 272),
                    LastSeen = Marshal.ReadInt32(item + 276)
                });
            }
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
        return result;
    }

    public static long SendFiles(string ip, int port, IReadOnlyList<SendFileItem> files)
    {
        const int itemSize = 520 * 2 * 2; // 两个 wchar_t[520]
        int totalSize = itemSize * files.Count;
        IntPtr buffer = Marshal.AllocHGlobal(totalSize);
        try
        {
            Marshal.Copy(new byte[totalSize], 0, buffer, totalSize);
            for (int i = 0; i < files.Count; i++)
            {
                string full = files[i].FullPath.Length > 519 ? files[i].FullPath[..519] : files[i].FullPath;
                string rel = files[i].RelativeName.Length > 519 ? files[i].RelativeName[..519] : files[i].RelativeName;
                char[] fullChars = (full + '\0').ToCharArray();
                char[] relChars = (rel + '\0').ToCharArray();
                Marshal.Copy(fullChars, 0, buffer + i * itemSize, fullChars.Length);
                Marshal.Copy(relChars, 0, buffer + i * itemSize + 1040, relChars.Length);
            }
            return ft_send_files(ip, port, buffer, files.Count);
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    public static void CancelSend(long groupId) => ft_send_cancel(groupId);

    public static int StartReceive(int port, string saveDir) => ft_recv_start(port, saveDir);
    public static void StopReceive() => ft_recv_stop();
    public static bool IsReceiveRunning => ft_recv_is_running() != 0;
    public static void SetReceiveDirectory(string saveDir) => ft_recv_set_save_dir(saveDir);

    public static void SaveConfig() => config_save(ref Config);

    public static void ApplyAutostart(bool enabled, string exePath)
    {
        if (enabled) reg_register_autostart(exePath);
        else reg_unregister_autostart();
    }

    public static bool IsShellMenuRegistered => reg_is_shell_menu_registered() != 0;

    public static bool ApplyShellMenu(bool enabled, string exePath)
    {
        int result = enabled ? reg_register_shell_menu(exePath, exePath) : reg_unregister_shell_menu();
        return result == 0;
    }

    public static int CheckUpdate(string url, ref FtUpdateInfo info) => up_check(url, AppVersion, ref info);
    public static int RunUpgrade(string scriptUrl) => up_run_upgrade(scriptUrl);

    private static void OnNativeSendEvent(int type, long taskId, int percent, long bytes,
        long total, IntPtr fileName, IntPtr extra)
    {
        SendEvent?.Invoke(new TransferEvent
        {
            Type = type, TaskId = taskId, Percent = percent, Bytes = bytes, Total = total,
            FileName = Marshal.PtrToStringUni(fileName) ?? "",
            Extra = Marshal.PtrToStringUni(extra) ?? ""
        });
    }

    private static void OnNativeReceiveEvent(int type, long taskId, int percent, long bytes,
        long total, IntPtr fileName, IntPtr extra)
    {
        ReceiveEvent?.Invoke(new TransferEvent
        {
            Type = type, TaskId = taskId, Percent = percent, Bytes = bytes, Total = total,
            FileName = Marshal.PtrToStringUni(fileName) ?? "",
            Extra = Marshal.PtrToStringUni(extra) ?? ""
        });
    }
}
