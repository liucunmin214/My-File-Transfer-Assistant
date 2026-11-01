; ====================================================================
; My File Transfer Assistant - 安装脚本 (Inno Setup)
; ====================================================================

#define AppName "My File Transfer Assistant"
#define AppPublisher "ModularData"
#define AppVersion "V1.2.0"
#define AppExeName "MyFileTransferAssistant.exe"
#define AppSetupName "Setup_MyFileTransferAssistant_V1.2.0"
#define AppRegKey "Software\My File Transfer Assistant"
#ifndef DistDir
#define DistDir "dist\MyFileTransferAssistant"
#endif

[Setup]
AppId={{B3E7C2A1-4F8D-5A6E-7F9A-0B1C2D3E4F5A}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL=https://github.com
AppSupportURL=https://github.com
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
OutputDir={#DistDir}
OutputBaseFilename={#AppSetupName}
SetupIconFile={#DistDir}\Assets\app.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=6.1sp1

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
Source: "{#DistDir}\MyFileTransferAssistant.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DistDir}\sync_core.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DistDir}\app.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DistDir}\Assets\app.ico"; DestDir: "{app}\Assets"; Flags: ignoreversion
Source: "{#DistDir}\Config\config.json"; DestDir: "{app}\Config"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"; IconFilename: "{app}\app.ico"; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; IconFilename: "{app}\app.ico"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch {#AppName} now"; Flags: nowait postinstall skipifsilent

[Registry]
; Write the install path for the app to find itself
Root: HKCU; Subkey: "{#AppRegKey}"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletekeyifempty

[UninstallDelete]
Type: filesandordirs; Name: "{app}\Config"
Type: filesandordirs; Name: "{app}\Assets"

[Code]
{ 卸载时清理应用运行时注册的 HKCU 条目：右键菜单 verb（五个入口）、
  Win11 经典菜单 CLSID 开关、开机自启残留值。安装器写不到这些键
  （它们由应用在用户开启右键菜单时动态注册），不清理就会卸载后残留。 }
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    { 右键菜单 verb：AllFilesystemObjects / * / Directory / Directory\Background / Drive }
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Classes\AllFilesystemObjects\shell\myfiletransferassistant.send');
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Classes\*\shell\myfiletransferassistant.send');
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Classes\Directory\shell\myfiletransferassistant.send');
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Classes\Directory\Background\shell\myfiletransferassistant.send');
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Classes\Drive\shell\myfiletransferassistant.send');
    { Win11 经典菜单 CLSID 开关（应用注册右键菜单时写入，卸载时一并清除，恢复现代菜单） }
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Classes\CLSID\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}');
    { 开机自启残留值（含旧版本遗留值名） }
    RegDeleteValue(HKEY_CURRENT_USER, 'Software\Microsoft\Windows\CurrentVersion\Run', 'MyFileTransferAssistant');
    RegDeleteValue(HKEY_CURRENT_USER, 'Software\Microsoft\Windows\CurrentVersion\Run', 'FileTransferAssistant');
  end;
end;
