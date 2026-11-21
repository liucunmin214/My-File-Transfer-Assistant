# My File Transfer Assistant

一款 Windows 局域网文件传输工具，类似 LocalSend / 飞鸽传书。界面采用 **WPF + XAML（.NET 8）**，设备发现、文件传输、配置和更新仍由原生 **C 核心 `sync_core.dll`** 提供。

## 功能特性

- **设备发现**：UDP 广播自动发现同一局域网内的设备，也支持手动输入 IP 和端口
- **文件发送**：多文件与文件夹递归发送、实时进度与速度、逐文件回执、失败原因提示
- **文件接收**：可同时接收多台设备的文件，自动处理重名（追加 `_(n)`）
- **现代界面**：XAML 自适应布局、圆角卡片、12 套主题和深/浅模式
- **系统集成**：资源管理器右键「发送到附近设备…」、开机自启、命令行 `--send`
- **软件更新**：内置更新检查，可搭配 `server/admin.php` 自建发布后台

## 技术栈

| 模块 | 说明 |
|---|---|
| `ui/` | .NET 8 WPF 界面、主题系统与原生互操作层 |
| `src/transfer.c` | TCP 传输核心：发送端/接收端、分块传输、进度回调 |
| `src/discover.c` | UDP 广播设备发现 |
| `src/config.c` / `src/json.c` | 配置读写（自研轻量 JSON） |
| `src/net.c` / `src/log.c` | 套接字封装、日志 |
| `src/registry.c` | 右键菜单、开机自启（Win11 经典菜单适配） |
| `src/upgrade.c` | 更新检查与升级脚本执行 |
| `server/admin.php` | 单文件 PHP 升级发布后台（可选） |
| `driver/` | 可选的文件系统微过滤驱动（实验性，GUI 未使用） |

`src/main.c` 是旧 Win32 自绘界面，仅保留作参考，不再参与构建。`src/sync.c` 为早期桌面同步引擎的遗留代码，仍编译进 DLL，但当前界面不调用。

## 目录结构

```
ui/              WPF + XAML 界面项目
src/             C 核心源码与头文件
server/          可选更新发布后台
driver/          可选内核驱动（实验性）
resources/       图标资源
build.ps1        一键构建与发布脚本
installer.iss    Inno Setup 安装包脚本
```

## 构建

1. 安装 [.NET 8 SDK](https://dotnet.microsoft.com/download/dotnet/8.0)。
2. 如需重新编译 C 核心，安装 [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw/releases)，并将其 `bin` 目录设置为 `LLVM_MINGW_BIN`（或加入 `PATH`）。未安装时脚本会复用现有的 `build\sync_core.dll`。
3. 在项目根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

产物输出到 `dist\MyFileTransferAssistant\`。标准发布版本在目标电脑上需要 **.NET 8 Desktop Runtime (x64)**。

如需无需预装运行时的独立版本，执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -SelfContained
```

也可直接编译界面项目：

```powershell
dotnet build .\ui\MyFileTransferAssistant.csproj -c Release
```

4. 可选：安装 [Inno Setup](https://jrsoftware.org/isinfo.php)，编译 `installer.iss` 生成安装包。

## 使用

1. 两台设备在同一局域网内运行程序。
2. 发送端在「发送」页选择文件或文件夹和在线设备；未发现时可手动添加对方 IP。
3. 接收端在「接收」页开启接收服务，并选择保存目录。
4. 若系统防火墙拦截，请允许程序使用专用网络。

命令行：`MyFileTransferAssistant.exe --send "C:\path\to\file"`（由右键菜单调用），也支持传入文件夹。

配置文件位置：`%APPDATA%\My File Transfer Assistant\config.json`。

## 自建更新服务（可选）

把 `server/admin.php` 放到启用了 PHP 的 Web 目录（如 `/mfta/`），给该目录写权限，浏览器登录后台即可上传新版本。**部署前务必修改文件顶部的 `ADMIN_PASSWORD`**。随后把设置里的 `update_url` 指向生成的 `update.json`。建议使用 HTTPS。

## 已知限制

- 传输为明文 TCP，**没有加密与身份认证**，请只在可信局域网内使用。
- 接收端信任发送方给出的相对路径，未对接收目录做越界校验。
- 托盘、声音提醒等旧高级配置尚未纳入新界面。
- 桌面同步引擎（`src/sync.c`、`driver/`）为早期实验代码，未在产品中启用。

## 许可证

[MIT](LICENSE)
