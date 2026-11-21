using System.IO;
using System.Windows;

namespace MyFileTransferAssistant;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        DispatcherUnhandledException += (_, args) =>
        {
            MessageBox.Show(args.Exception.Message, "界面错误", MessageBoxButton.OK, MessageBoxImage.Error);
            args.Handled = true;
        };

        List<SendFileItem> startupFiles = new();
        bool sendMode = false;
        foreach (string arg in e.Args)
        {
            if (arg.Equals("--send", StringComparison.OrdinalIgnoreCase))
            {
                sendMode = true;
                continue;
            }
            if (arg.StartsWith("--", StringComparison.Ordinal))
            {
                sendMode = false;
                continue;
            }
            if (sendMode) AddPath(startupFiles, arg);
        }

        try
        {
            Core.Initialize();
        }
        catch (Exception ex)
        {
            MessageBox.Show($"无法加载 sync_core.dll 或初始化核心：\n{ex.Message}",
                "启动失败", MessageBoxButton.OK, MessageBoxImage.Error);
            Shutdown(1);
            return;
        }

        ThemeManager.Apply(Core.Config.theme_id, Core.Config.dark_mode != 0);
        var window = new MainWindow(startupFiles);
        MainWindow = window;
        window.Show();
    }

    internal static void AddPath(ICollection<SendFileItem> files, string path)
    {
        try
        {
            if (File.Exists(path))
            {
                AddFile(files, path, Path.GetFileName(path));
            }
            else if (Directory.Exists(path))
            {
                string? parent = Path.GetDirectoryName(path.TrimEnd(Path.DirectorySeparatorChar));
                foreach (string file in Directory.EnumerateFiles(path, "*", SearchOption.AllDirectories))
                {
                    string relative = parent != null ? Path.GetRelativePath(parent, file) : Path.GetFileName(file);
                    AddFile(files, file, relative);
                }
            }
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine(ex);
        }
    }

    private static void AddFile(ICollection<SendFileItem> files, string fullPath, string relativeName)
    {
        if (files.Any(f => f.FullPath.Equals(fullPath, StringComparison.OrdinalIgnoreCase))) return;
        files.Add(new SendFileItem
        {
            FullPath = fullPath,
            RelativeName = relativeName,
            Size = new FileInfo(fullPath).Length
        });
    }
}
