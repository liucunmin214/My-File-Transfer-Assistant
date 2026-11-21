using System.IO;

namespace MyFileTransferAssistant;

public sealed class DeviceItem
{
    public string Name { get; set; } = "";
    public string Id { get; set; } = "";
    public string Ip { get; set; } = "";
    public int Port { get; set; }
    public int LastSeen { get; set; }
    public bool IsManual { get; set; }
    public string Status => IsManual ? "手动" : "在线";
    public string Endpoint => $"{Ip}:{Port}";
}

public sealed class SendFileItem
{
    public string FullPath { get; set; } = "";
    public string RelativeName { get; set; } = "";
    public long Size { get; set; }
    public string Name => Path.GetFileName(FullPath);
    public string SizeText => Format.Size(Size);
}

public sealed class TransferLogItem
{
    public string Time { get; set; } = DateTime.Now.ToString("HH:mm:ss");
    public string Name { get; set; } = "";
    public string SizeText { get; set; } = "";
    public string Status { get; set; } = "";
    public string Detail { get; set; } = "";
}

public sealed class TransferEvent
{
    public int Type { get; init; }
    public long TaskId { get; init; }
    public int Percent { get; init; }
    public long Bytes { get; init; }
    public long Total { get; init; }
    public string FileName { get; init; } = "";
    public string Extra { get; init; } = "";
}

public static class Format
{
    public static string Size(long bytes)
    {
        if (bytes < 1024) return $"{bytes} B";
        if (bytes < 1024 * 1024) return $"{bytes / 1024.0:F1} KB";
        if (bytes < 1024L * 1024 * 1024) return $"{bytes / 1024.0 / 1024:F2} MB";
        return $"{bytes / 1024.0 / 1024 / 1024:F2} GB";
    }

    public static string Speed(double bytesPerSecond)
    {
        if (bytesPerSecond <= 0) return "--";
        return $"{Size((long)bytesPerSecond)}/s";
    }
}
