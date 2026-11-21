param(
    [switch]$SelfContained
)

# 界面使用 .NET 8 WPF；C 核心继续编译为 sync_core.dll。
# 未安装 LLVM-MinGW 时，复用 build\sync_core.dll。
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = Join-Path $root 'src'
$out = Join-Path $root 'build'
$dist = Join-Path $root 'dist\MyFileTransferAssistant'
$uiProject = Join-Path $root 'ui\MyFileTransferAssistant.csproj'
$coreDll = Join-Path $out 'sync_core.dll'

if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw '未找到 dotnet。请安装 .NET 8 SDK 后重试。'
}

New-Item -ItemType Directory -Path $out -Force | Out-Null

$bin = $env:LLVM_MINGW_BIN
if (-not $bin) {
    $clang = Get-Command clang -ErrorAction SilentlyContinue
    if ($clang) { $bin = Split-Path -Parent $clang.Source }
}

if ($bin -and (Test-Path (Join-Path $bin 'clang.exe'))) {
    $env:PATH = $bin + ';' + $env:PATH
    $cflags = @(
        '-O2', '-Wall', '-Wno-unused-function', '-Wno-unused-parameter',
        '-std=c11', '-municode', '-DUNICODE', '-D_UNICODE',
        '-D_WIN32_WINNT=0x0601', '-DWIN32_LEAN_AND_MEAN', '-DSYNC_DLL_EXPORTS',
        "-I$src"
    )

    Write-Host '=== [1/2] 编译 sync_core.dll ==='
    foreach ($f in @('dllmain', 'json', 'log', 'net', 'sync', 'discover', 'transfer', 'config', 'registry', 'upgrade')) {
        Write-Host "  CC $f.c"
        & clang @cflags -c "$src\$f.c" -o "$out\$f.o"
        if ($LASTEXITCODE -ne 0) { throw "编译失败：$f.c" }
    }

    $dllArgs = @(
        '-shared', '-static', '-Wl,--export-all-symbols',
        "-Wl,--output-def=$out\sync_core.def",
        "-Wl,--out-implib=$out\sync_core.lib",
        '-o', $coreDll,
        "$out\dllmain.o", "$out\json.o", "$out\log.o", "$out\net.o", "$out\sync.o",
        "$out\discover.o", "$out\transfer.o", "$out\config.o", "$out\registry.o", "$out\upgrade.o",
        '-lws2_32', '-luser32', '-lkernel32', '-ladvapi32', '-lole32', '-lshell32', '-lshlwapi', '-lwinhttp'
    )
    & clang @dllArgs
    if ($LASTEXITCODE -ne 0) { throw '链接 sync_core.dll 失败。' }
}
else {
    Write-Host '未找到 LLVM-MinGW，复用现有 build\sync_core.dll。'
}

if (-not (Test-Path $coreDll)) {
    throw '缺少 build\sync_core.dll。请安装 LLVM-MinGW 并设置 LLVM_MINGW_BIN，或放入已编译的核心 DLL。'
}

Write-Host '=== [2/2] 发布 WPF 界面 ==='
$publishArgs = @('publish', $uiProject, '-c', 'Release', '-o', $dist)
if ($SelfContained) {
    $publishArgs += @('-r', 'win-x64', '--self-contained', 'true')
}
else {
    $publishArgs += @('--self-contained', 'false')
}
& dotnet @publishArgs
if ($LASTEXITCODE -ne 0) { throw 'WPF 项目发布失败。' }

New-Item -ItemType Directory -Path (Join-Path $dist 'Assets') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $dist 'Config') -Force | Out-Null
Copy-Item $coreDll (Join-Path $dist 'sync_core.dll') -Force
Copy-Item (Join-Path $root 'resources\icon.ico') (Join-Path $dist 'app.ico') -Force
Copy-Item (Join-Path $root 'resources\icon.ico') (Join-Path $dist 'Assets\app.ico') -Force

$cfgPath = Join-Path $dist 'Config\config.json'
if (-not (Test-Path $cfgPath)) {
    '{"nickname":"","tcp_port":51831,"udp_port":51830,"save_dir":"","autostart":0,"auto_accept":1,"show_notification":1,"autodiscover":1,"dark_mode":0}' |
        Out-File -FilePath $cfgPath -Encoding UTF8
}

Write-Host ''
Write-Host '=== BUILD SUCCESS ==='
Write-Host "产物目录：$dist"
if (-not $SelfContained) {
    Write-Host '运行此版本需要安装 .NET 8 Desktop Runtime (x64)。'
}
