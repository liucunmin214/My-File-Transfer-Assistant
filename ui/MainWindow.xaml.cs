using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;
using Microsoft.Win32;

namespace MyFileTransferAssistant;

public partial class MainWindow : Window
{
    private readonly ObservableCollection<DeviceItem> _devices = new();
    private readonly ObservableCollection<SendFileItem> _files = new();
    private readonly ObservableCollection<TransferLogItem> _receiveLogs = new();
    private readonly List<DeviceItem> _manualDevices = new();
    private readonly Dictionary<long, TransferLogItem> _receiveCurrent = new();
    private readonly DispatcherTimer _deviceTimer;
    private readonly Stopwatch _speedWatch = new();
    private readonly List<Button> _themeButtons = new();
    private string _lastDeviceSignature = "";
    private long _currentSendId = -1;
    private bool _sendActive;
    private bool _receiveRunning;
    private bool _autoUpdateChecked;

    public MainWindow(IEnumerable<SendFileItem> startupFiles)
    {
        InitializeComponent();
        DeviceList.ItemsSource = _devices;
        SendFileList.ItemsSource = _files;
        ReceiveList.ItemsSource = _receiveLogs;

        Core.SendEvent += e => Dispatcher.BeginInvoke(() => OnSendEvent(e));
        Core.ReceiveEvent += e => Dispatcher.BeginInvoke(() => OnReceiveEvent(e));

        LoadSettingsToUi();
        BuildThemeButtons();
        RefreshLocalIp();
        _receiveRunning = Core.IsReceiveRunning;
        UpdateReceiveUi();

        foreach (var file in startupFiles)
        {
            if (!_files.Any(f => f.FullPath.Equals(file.FullPath, StringComparison.OrdinalIgnoreCase)))
                _files.Add(file);
        }
        UpdateSummary();
        RefreshDevices(true);

        _deviceTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1.5) };
        _deviceTimer.Tick += (_, _) => RefreshDevices();
        _deviceTimer.Start();

        Loaded += (_, _) =>
        {
            if (_files.Count > 0) ShowPage(0);
            if (Core.Config.auto_update != 0 && !_autoUpdateChecked)
            {
                _autoUpdateChecked = true;
                CheckForUpdates(false);
            }
        };
        ShowPage(0);
    }

    private void ShowPage(int page)
    {
        PageSend.Visibility = page == 0 ? Visibility.Visible : Visibility.Collapsed;
        PageReceive.Visibility = page == 1 ? Visibility.Visible : Visibility.Collapsed;
        PageSettings.Visibility = page == 2 ? Visibility.Visible : Visibility.Collapsed;
        PageAbout.Visibility = page == 3 ? Visibility.Visible : Visibility.Collapsed;
        PageTitle.Text = page switch { 0 => "发送文件", 1 => "接收文件", 2 => "设置", _ => "关于与更新" };
        NavSend.IsChecked = page == 0;
        NavReceive.IsChecked = page == 1;
        NavSettings.IsChecked = page == 2;
        NavAbout.IsChecked = page == 3;
    }

    private void Nav_Checked(object sender, RoutedEventArgs e)
    {
        if (PageSend is null) return;
        if (NavSend.IsChecked == true) ShowPage(0);
        else if (NavReceive.IsChecked == true) ShowPage(1);
        else if (NavSettings.IsChecked == true) ShowPage(2);
        else if (NavAbout.IsChecked == true) ShowPage(3);
    }

    private void UpdateGlobalStatus()
    {
        GlobalStatus.Text = _sendActive ? "传输中" : _receiveRunning ? "监听中" : "待机";
    }

    /* ---------------- 设备发现 ---------------- */

    private void RefreshDevices(bool force = false)
    {
        List<DeviceItem> discovered;
        try { discovered = Core.GetDevices(); }
        catch { return; }

        var all = new List<DeviceItem>(discovered);
        foreach (var manual in _manualDevices)
        {
            if (!all.Any(d => d.Ip == manual.Ip && d.Port == manual.Port)) all.Add(manual);
        }
        string signature = string.Join("|", all.Select(d => $"{d.Name};{d.Ip};{d.Port};{d.IsManual}"));
        if (!force && signature == _lastDeviceSignature) return;
        _lastDeviceSignature = signature;

        string? selectedKey = (DeviceList.SelectedItem as DeviceItem) is { } old ? $"{old.Ip}:{old.Port}" : null;
        _devices.Clear();
        foreach (var device in all) _devices.Add(device);
        DeviceItem? select = _devices.FirstOrDefault(d => $"{d.Ip}:{d.Port}" == selectedKey);
        DeviceList.SelectedItem = select ?? _devices.FirstOrDefault();
        UpdateSummary();
    }

    private void RefreshDevices_Click(object sender, RoutedEventArgs e)
    {
        Core.RefreshDevices();
        RefreshDevices(true);
    }

    private void AddManualDevice_Click(object sender, RoutedEventArgs e)
    {
        string ip = ManualIp.Text.Trim();
        if (!IPAddress.TryParse(ip, out _))
        {
            MessageBox.Show("请输入有效的 IP 地址。", "提示", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        if (!int.TryParse(ManualPort.Text.Trim(), out int port) || port < 1 || port > 65535)
        {
            MessageBox.Show("端口必须是 1–65535 之间的数字。", "提示", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        var device = new DeviceItem { Name = $"手动设备 ({ip})", Ip = ip, Port = port, IsManual = true, LastSeen = -1 };
        if (!_manualDevices.Any(d => d.Ip == ip && d.Port == port)) _manualDevices.Add(device);
        RefreshDevices(true);
        DeviceList.SelectedItem = _devices.FirstOrDefault(d => d.Ip == ip && d.Port == port);
        ManualIp.Clear();
    }

    /* ---------------- 待发送文件 ---------------- */

    private void UpdateSummary()
    {
        SendSummary.Text = $"设备 {_devices.Count} 台 · 文件 {_files.Count} 个 · 共 {Format.Size(_files.Sum(f => f.Size))}";
    }

    private void PickFiles_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog { Multiselect = true, Title = "选择要发送的文件" };
        if (dialog.ShowDialog(this) != true) return;
        foreach (string file in dialog.FileNames) App.AddPath(_files, file);
        UpdateSummary();
    }

    private void PickFolder_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFolderDialog { Title = "选择要发送的文件夹" };
        if (dialog.ShowDialog(this) != true) return;
        App.AddPath(_files, dialog.FolderName);
        UpdateSummary();
    }

    private void RemoveFiles_Click(object sender, RoutedEventArgs e)
    {
        foreach (var item in SendFileList.SelectedItems.Cast<SendFileItem>().ToList()) _files.Remove(item);
        UpdateSummary();
    }

    private void ClearFiles_Click(object sender, RoutedEventArgs e)
    {
        _files.Clear();
        UpdateSummary();
    }

    private void AddSendLog(string text)
    {
        SendLog.Items.Add($"{DateTime.Now:HH:mm:ss}  {text}");
        if (SendLog.Items.Count > 200) SendLog.Items.RemoveAt(0);
        SendLog.ScrollIntoView(SendLog.Items[^1]);
    }

    /* ---------------- 发送 ---------------- */

    private void SendButton_Click(object sender, RoutedEventArgs e)
    {
        if (DeviceList.SelectedItem is not DeviceItem target)
        {
            MessageBox.Show("请先选择一个目标设备。", "提示", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        if (_files.Count == 0)
        {
            MessageBox.Show("请先选择要发送的文件。", "提示", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }

        long groupId = Core.SendFiles(target.Ip, target.Port, _files.ToList());
        if (groupId < 0)
        {
            MessageBox.Show("启动发送失败。", "错误", MessageBoxButton.OK, MessageBoxImage.Error);
            return;
        }
        _currentSendId = groupId;
        _sendActive = true;
        SendButton.IsEnabled = false;
        CancelSendButton.IsEnabled = true;
        SendProgressBar.Value = 0;
        SendProgressText.Text = $"正在连接 {target.Endpoint}…";
        _speedWatch.Restart();
        AddSendLog($"开始发送到 {target.Name}（{target.Endpoint}），共 {_files.Count} 个文件");
        Core.Config.last_dest_ip = target.Ip;
        Core.Config.last_dest_port = target.Port;
        Core.SaveConfig();
        UpdateGlobalStatus();
    }

    private void CancelSend_Click(object sender, RoutedEventArgs e)
    {
        if (_currentSendId > 0) Core.CancelSend(_currentSendId);
        AddSendLog("已请求取消发送");
    }

    private void FinishSend(bool success, string message)
    {
        _sendActive = false;
        SendButton.IsEnabled = true;
        CancelSendButton.IsEnabled = false;
        SendProgressText.Text = message;
        if (!success) SendSpeedText.Text = "--";
        UpdateGlobalStatus();
    }

    private void OnSendEvent(TransferEvent e)
    {
        if (e.TaskId != _currentSendId) return;
        switch (e.Type)
        {
            case 0:
                _sendActive = true;
                _speedWatch.Restart();
                SendProgressBar.Value = 0;
                SendProgressText.Text = $"正在发送：{e.FileName}";
                AddSendLog($"开始：{e.FileName}（{Format.Size(e.Total)}）");
                UpdateGlobalStatus();
                break;
            case 1:
                SendProgressBar.Value = e.Percent;
                SendProgressText.Text = $"{e.FileName}  ·  {e.Percent}%  ·  {Format.Size(e.Bytes)} / {Format.Size(e.Total)}";
                if (Core.Config.show_speed != 0 && _speedWatch.Elapsed.TotalSeconds > 0.2)
                    SendSpeedText.Text = Format.Speed(e.Bytes / _speedWatch.Elapsed.TotalSeconds);
                break;
            case 2:
                SendProgressBar.Value = 100;
                AddSendLog($"完成：{e.FileName}");
                FinishSend(true, $"发送完成：{e.FileName}");
                if (Core.Config.auto_cleanup_sent != 0)
                {
                    _files.Clear();
                    UpdateSummary();
                }
                break;
            case 3:
                AddSendLog($"失败：{e.FileName} {e.Extra}".Trim());
                FinishSend(false, string.IsNullOrWhiteSpace(e.Extra) ? "发送失败" : e.Extra);
                break;
        }
    }

    /* ---------------- 接收 ---------------- */

    private void ReceiveToggle_Click(object sender, RoutedEventArgs e)
    {
        if (_receiveRunning)
        {
            Core.StopReceive();
            _receiveRunning = false;
            UpdateReceiveUi();
            return;
        }

        if (!int.TryParse(ReceivePortBox.Text.Trim(), out int port) || port < 1 || port > 65535)
        {
            MessageBox.Show("端口无效。", "错误", MessageBoxButton.OK, MessageBoxImage.Error);
            return;
        }
        string dir = ReceiveDirBox.Text.Trim();
        if (string.IsNullOrWhiteSpace(dir))
        {
            MessageBox.Show("请选择文件保存目录。", "提示", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        try { Directory.CreateDirectory(dir); }
        catch (Exception ex)
        {
            MessageBox.Show($"无法创建保存目录：{ex.Message}", "错误", MessageBoxButton.OK, MessageBoxImage.Error);
            return;
        }

        if (Core.StartReceive(port, dir) < 0)
        {
            MessageBox.Show("开启接收失败：端口可能被占用，请检查防火墙或修改端口。",
                "错误", MessageBoxButton.OK, MessageBoxImage.Error);
            return;
        }
        _receiveRunning = true;
        Core.Config.tcp_port = port;
        Core.Config.save_dir = dir;
        Core.SaveConfig();
        Core.RestartDiscovery();
        UpdateReceiveUi();
    }

    private void UpdateReceiveUi()
    {
        ReceiveToggleButton.Content = _receiveRunning ? "停止接收服务" : "开启接收服务";
        ReceiveStatusText.Text = _receiveRunning ? "正在监听，等待其他设备发送" : "未开启";
        ReceiveStatusText.Foreground = (Brush)(FindResource(_receiveRunning ? "SuccessBrush" : "SubTextBrush"));
        ReceivePortBox.IsEnabled = !_receiveRunning;
        ReceiveDirBox.IsEnabled = !_receiveRunning;
        UpdateGlobalStatus();
    }

    private void OnReceiveEvent(TransferEvent e)
    {
        switch (e.Type)
        {
            case 0:
            {
                var item = new TransferLogItem
                {
                    Name = e.FileName, SizeText = Format.Size(e.Total), Status = "接收中", Detail = e.Extra
                };
                _receiveCurrent[e.TaskId] = item;
                _receiveLogs.Insert(0, item);
                while (_receiveLogs.Count > 500) _receiveLogs.RemoveAt(_receiveLogs.Count - 1);
                ReceiveProgressBar.Value = 0;
                ReceiveProgressText.Text = $"正在接收：{e.FileName}";
                break;
            }
            case 1:
                ReceiveProgressBar.Value = e.Percent;
                ReceiveProgressText.Text = $"{e.FileName}  ·  {e.Percent}%  ·  {Format.Size(e.Bytes)} / {Format.Size(e.Total)}";
                if (_receiveCurrent.TryGetValue(e.TaskId, out var progressItem)) progressItem.Status = $"{e.Percent}%";
                break;
            case 2:
            case 4:
                ReceiveProgressBar.Value = 100;
                ReceiveProgressText.Text = $"接收完成：{e.FileName}";
                if (_receiveCurrent.TryGetValue(e.TaskId, out var doneItem))
                {
                    doneItem.Status = "已完成";
                    doneItem.Detail = e.Extra;
                }
                break;
            case 3:
                ReceiveProgressText.Text = $"接收失败：{e.FileName} {e.Extra}".Trim();
                if (_receiveCurrent.TryGetValue(e.TaskId, out var errorItem))
                {
                    errorItem.Status = "失败";
                    errorItem.Detail = e.Extra;
                }
                break;
        }
    }

    private void BrowseReceiveDir_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFolderDialog { Title = "选择接收文件保存目录" };
        if (dialog.ShowDialog(this) == true) ReceiveDirBox.Text = dialog.FolderName;
    }

    private void OpenReceiveDir_Click(object sender, RoutedEventArgs e)
    {
        string dir = ReceiveDirBox.Text.Trim();
        if (!Directory.Exists(dir)) return;
        Process.Start(new ProcessStartInfo("explorer.exe", $"\"{dir}\"") { UseShellExecute = true });
    }

    private void ClearReceiveLog_Click(object sender, RoutedEventArgs e)
    {
        _receiveLogs.Clear();
        _receiveCurrent.Clear();
        ReceiveProgressBar.Value = 0;
        ReceiveProgressText.Text = "记录已清空。";
    }

    private void RefreshLocalIp()
    {
        try
        {
            var addresses = Dns.GetHostAddresses(Dns.GetHostName())
                .Where(a => a.AddressFamily == AddressFamily.InterNetwork && !IPAddress.IsLoopback(a))
                .Select(a => a.ToString()).ToList();
            LocalIpText.Text = "本机 IP：" + (addresses.Count > 0 ? string.Join(" / ", addresses) : "未知");
        }
        catch { LocalIpText.Text = "本机 IP：未知"; }
    }

    /* ---------------- 设置与主题 ---------------- */

    private void LoadSettingsToUi()
    {
        var cfg = Core.Config;
        NickBox.Text = cfg.nickname;
        SettingsSaveDirBox.Text = cfg.save_dir;
        ReceiveDirBox.Text = cfg.save_dir;
        TcpPortBox.Text = cfg.tcp_port.ToString();
        UdpPortBox.Text = cfg.udp_port.ToString();
        ReceivePortBox.Text = cfg.tcp_port.ToString();
        AutostartSwitch.IsChecked = cfg.autostart != 0;
        ShellMenuSwitch.IsChecked = Core.IsShellMenuRegistered;
        DarkSwitch.IsChecked = cfg.dark_mode != 0;
        AutoUpdateSwitch.IsChecked = cfg.auto_update != 0;
        UpdateUrlBox.Text = cfg.update_url;
        SidebarDeviceName.Text = cfg.nickname;
    }

    private void BuildThemeButtons()
    {
        foreach (var theme in ThemeManager.Themes)
        {
            var swatch = new Border
            {
                Width = 18, Height = 18, CornerRadius = new CornerRadius(9),
                Background = theme.Swatch, VerticalAlignment = VerticalAlignment.Center
            };
            var text = new TextBlock
            {
                Text = theme.Name, Margin = new Thickness(8, 0, 0, 0),
                VerticalAlignment = VerticalAlignment.Center
            };
            var content = new StackPanel { Orientation = Orientation.Horizontal };
            content.Children.Add(swatch);
            content.Children.Add(text);
            var button = new Button
            {
                Style = (Style)FindResource("SecondaryButton"),
                Content = content, Tag = theme.Id, Width = 116,
                Margin = new Thickness(0, 0, 10, 10), Padding = new Thickness(12, 8, 12, 8)
            };
            button.Click += ThemeButton_Click;
            _themeButtons.Add(button);
            ThemePanel.Children.Add(button);
        }
        MarkThemeSelection();
    }

    private void MarkThemeSelection()
    {
        foreach (var button in _themeButtons)
        {
            bool selected = (int)button.Tag == Core.Config.theme_id;
            button.BorderBrush = (Brush)FindResource(selected ? "AccentBrush" : "CardBorderBrush");
            button.FontWeight = selected ? FontWeights.SemiBold : FontWeights.Normal;
        }
    }

    private void ThemeButton_Click(object sender, RoutedEventArgs e)
    {
        Core.Config.theme_id = (int)((Button)sender).Tag;
        ThemeManager.Apply(Core.Config.theme_id, Core.Config.dark_mode != 0);
        Core.SaveConfig();
        MarkThemeSelection();
    }

    private void DarkSwitch_Changed(object sender, RoutedEventArgs e)
    {
        Core.Config.dark_mode = DarkSwitch.IsChecked == true ? 1 : 0;
        ThemeManager.Apply(Core.Config.theme_id, Core.Config.dark_mode != 0);
        Core.SaveConfig();
        MarkThemeSelection();
    }

    private void BrowseSettingsDir_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFolderDialog { Title = "选择默认保存目录" };
        if (dialog.ShowDialog(this) == true) SettingsSaveDirBox.Text = dialog.FolderName;
    }

    private void SaveSettings_Click(object sender, RoutedEventArgs e)
    {
        if (!int.TryParse(TcpPortBox.Text.Trim(), out int tcp) || tcp < 1 || tcp > 65535 ||
            !int.TryParse(UdpPortBox.Text.Trim(), out int udp) || udp < 1 || udp > 65535)
        {
            SettingsStatus.Text = "端口必须是 1–65535 之间的数字";
            return;
        }
        string saveDir = SettingsSaveDirBox.Text.Trim();
        if (string.IsNullOrWhiteSpace(saveDir))
        {
            SettingsStatus.Text = "默认保存目录不能为空";
            return;
        }

        var cfg = Core.Config;
        cfg.nickname = string.IsNullOrWhiteSpace(NickBox.Text) ? "未命名设备" : NickBox.Text.Trim();
        cfg.tcp_port = tcp;
        cfg.udp_port = udp;
        cfg.save_dir = saveDir;
        cfg.autostart = AutostartSwitch.IsChecked == true ? 1 : 0;
        cfg.dark_mode = DarkSwitch.IsChecked == true ? 1 : 0;
        cfg.auto_update = AutoUpdateSwitch.IsChecked == true ? 1 : 0;
        cfg.update_url = UpdateUrlBox.Text.Trim();
        Core.Config = cfg;

        string exe = Environment.ProcessPath ?? Process.GetCurrentProcess().MainModule?.FileName ?? "";
        Core.ApplyAutostart(cfg.autostart != 0, exe);
        bool menuOk = Core.ApplyShellMenu(ShellMenuSwitch.IsChecked == true, exe);
        Core.SaveConfig();
        Core.RestartDiscovery();
        if (_receiveRunning) Core.SetReceiveDirectory(saveDir);
        ReceiveDirBox.Text = saveDir;
        ReceivePortBox.Text = tcp.ToString();
        SidebarDeviceName.Text = cfg.nickname;
        SettingsStatus.Text = menuOk ? "设置已保存" : "设置已保存，但右键菜单注册失败";
        RefreshDevices(true);
    }

    /* ---------------- 关于与更新 ---------------- */

    private void CheckUpdate_Click(object sender, RoutedEventArgs e) => CheckForUpdates(true);

    private async void CheckForUpdates(bool manual)
    {
        string url = Core.Config.update_url?.Trim() ?? "";
        if (url.Length == 0)
        {
            if (manual) UpdateStatus.Text = "未配置更新地址，请先在设置中填写 update.json 地址。";
            return;
        }
        CheckUpdateButton.IsEnabled = false;
        UpdateStatus.Text = "正在检查更新…";
        var info = new FtUpdateInfo();
        int result = await Task.Run(() => Core.CheckUpdate(url, ref info));
        CheckUpdateButton.IsEnabled = true;

        if (result != 0)
        {
            if (manual) UpdateStatus.Text = "检查失败：无法连接更新服务器或返回内容无效。";
            else UpdateStatus.Text = "自动检查更新失败。";
            return;
        }
        if (info.has_update != 0)
        {
            UpdateStatus.Text = $"发现新版本 {info.version}";
            var choice = MessageBox.Show($"发现新版本 {info.version}\n\n{info.notes}\n\n是否现在下载并安装？",
                "软件更新", MessageBoxButton.YesNo, MessageBoxImage.Information);
            if (choice == MessageBoxResult.Yes)
            {
                if (Core.RunUpgrade(info.script ?? string.Empty) != 0)
                    MessageBox.Show("启动升级程序失败。", "错误", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }
        else if (info.rollback != 0)
        {
            UpdateStatus.Text = $"服务器版本 {info.version} 低于当前版本（服务器可能已回滚）。";
        }
        else
        {
            UpdateStatus.Text = $"当前已是最新版本（{Core.AppVersion}）。";
        }
    }

    private void RepoHyperlink_RequestNavigate(object sender, System.Windows.Navigation.RequestNavigateEventArgs e)
    {
        Process.Start(new ProcessStartInfo(e.Uri.AbsoluteUri) { UseShellExecute = true });
        e.Handled = true;
    }

    private void OpenLog_Click(object sender, RoutedEventArgs e)
    {
        string dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "My File Transfer Assistant");
        Directory.CreateDirectory(dir);
        Process.Start(new ProcessStartInfo("explorer.exe", $"\"{dir}\"") { UseShellExecute = true });
    }

    /* ---------------- 窗口 ---------------- */

    private void TitleBar_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.LeftButton != MouseButtonState.Pressed) return;
        if (e.ClickCount == 2) ToggleMaximize();
        else DragMove();
    }

    private void ToggleMaximize() => WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;

    private void MinButton_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;
    private void MaxButton_Click(object sender, RoutedEventArgs e) => ToggleMaximize();
    private void CloseButton_Click(object sender, RoutedEventArgs e) => Close();

    private void Window_StateChanged(object? sender, EventArgs e)
    {
        MaxButton.Content = WindowState == WindowState.Maximized ? "❐" : "□";
    }

    private void Window_Closing(object? sender, System.ComponentModel.CancelEventArgs e)
    {
        _deviceTimer.Stop();
        Core.Shutdown();
    }
}
