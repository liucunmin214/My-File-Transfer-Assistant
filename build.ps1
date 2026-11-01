# PowerShell build script for My File Transfer Assistant (MyFileTransferAssistant) using LLVM-MinGW clang
# 依赖 LLVM-MinGW：https://github.com/mstorsjo/llvm-mingw/releases
# 用环境变量 LLVM_MINGW_BIN 指向其 bin 目录；未设置时自动从 PATH 查找 clang。
$bin = $env:LLVM_MINGW_BIN
if (-not $bin) {
    $clang = Get-Command clang -ErrorAction SilentlyContinue
    if ($clang) { $bin = Split-Path -Parent $clang.Source }
}
if (-not $bin -or -not (Test-Path (Join-Path $bin 'clang.exe'))) {
    Write-Host "ERROR: 未找到 LLVM-MinGW。请安装后设置环境变量 LLVM_MINGW_BIN 为其中的 bin 目录。"
    exit 1
}
$env:PATH = $bin + ';' + $env:PATH

$root  = Split-Path -Parent $MyInvocation.MyCommand.Path
$src   = Join-Path $root 'src'
$out   = Join-Path $root 'build'
$dist  = Join-Path $root 'dist\MyFileTransferAssistant'

New-Item -ItemType Directory -Path $out -Force | Out-Null
New-Item -ItemType Directory -Path $dist -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $dist 'Assets') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $dist 'Config') -Force | Out-Null

$cflags = @(
    '-O2', '-Wall', '-Wno-unused-function', '-Wno-unused-parameter',
    '-std=c11', '-municode', '-DUNICODE', '-D_UNICODE',
    '-D_WIN32_WINNT=0x0601', '-DWIN32_LEAN_AND_MEAN', '-DSYNC_DLL_EXPORTS',
    "-I$src"
)

Write-Host ""
Write-Host "=== [1/6] Compiling DLL sources ==="
foreach ($f in @('dllmain','json','log','net','sync','discover','transfer','config','registry','upgrade')) {
    Write-Host "  CC $f.c"
    & clang @cflags -c "$src\$f.c" -o "$out\$f.o" 2>&1
    if ($LASTEXITCODE -ne 0) { Write-Host "FAILED: $f.c"; exit 1 }
}

Write-Host ""
Write-Host "=== [2/6] Linking sync_core.dll (static CRT) ==="
$dllArgs = @(
    '-shared', '-static',
    '-Wl,--export-all-symbols',
    "-Wl,--output-def=$out\sync_core.def",
    "-Wl,--out-implib=$out\sync_core.lib",
    '-o', "$out\sync_core.dll",
    "$out\dllmain.o", "$out\json.o", "$out\log.o",
    "$out\net.o", "$out\sync.o",
    "$out\discover.o", "$out\transfer.o", "$out\config.o", "$out\registry.o",
    "$out\upgrade.o",
    '-lws2_32', '-luser32', '-lkernel32', '-ladvapi32', '-lole32', '-lshell32', '-lshlwapi',
    '-lwinhttp'
)
& clang @dllArgs 2>&1
if ($LASTEXITCODE -ne 0) { Write-Host "FAILED: DLL link"; exit 1 }

Write-Host ""
Write-Host "=== [3/6] Compiling resource.rc ==="
$mingwInc  = Join-Path $bin '..\include'
$mingwInc2 = Join-Path $bin '..\x86_64-w64-mingw32\include'
& windres -i "$src\resource.rc" -J rc -o "$out\resource.o" -O coff `
    "-I$mingwInc" "-I$mingwInc2" "-I$src" "-I$src\..\resources" 2>&1
if ($LASTEXITCODE -ne 0) { Write-Host "FAILED: resource"; exit 1 }

Write-Host ""
Write-Host "=== [4/5] Compiling main.c ==="
$exeCflags = @(
    '-O2', '-Wall', '-std=c11', '-municode', '-DUNICODE', '-D_UNICODE',
    '-D_WIN32_WINNT=0x0A00',
    "-I$src"
)
& clang @exeCflags -c "$src\main.c" -o "$out\main.o" 2>&1
if ($LASTEXITCODE -ne 0) { Write-Host "FAILED: main.c"; exit 1 }

Write-Host ""
Write-Host "=== [5/5] Linking MyFileTransferAssistant.exe ==="
$exeArgs = @('-municode','-mwindows','-static','-o',"$out\MyFileTransferAssistant.exe","$out\main.o","$out\resource.o","-L$out",'-lsync_core','-lcomctl32','-lcomdlg32','-lgdi32','-lmsimg32','-lshlwapi','-lshell32','-luser32','-lkernel32','-lws2_32','-lole32','-ldwmapi','-ladvapi32','-luxtheme','-lversion')
& clang @exeArgs 2>&1
if ($LASTEXITCODE -ne 0) { Write-Host "FAILED: MyFileTransferAssistant.exe link"; exit 1 }

Write-Host ""
Write-Host "=== Copying to dist\MyFileTransferAssistant\ ==="
Copy-Item "$out\sync_core.dll" $dist -Force
Copy-Item "$out\MyFileTransferAssistant.exe" $dist -Force
Copy-Item "$root\resources\icon.ico" "$dist\app.ico" -Force
Copy-Item "$root\resources\icon.ico" "$dist\Assets\app.ico" -Force
# 默认配置模板（仅在不存在时创建）
$cfgPath = Join-Path $dist 'Config\config.json'
if (-not (Test-Path $cfgPath)) {
    '{"nickname":"","tcp_port":51831,"udp_port":51820,"save_dir":"","auto_start":false,"auto_accept":false,"notify":true,"auto_discover":true,"dark_mode":false}' | Out-File -FilePath $cfgPath -Encoding UTF8
}

Write-Host ""
Write-Host "=== BUILD SUCCESS ==="
Get-ChildItem $dist -Recurse | Select-Object Name, @{N='Size';E={'{0:N0}' -f $_.Length}} | Format-Table -AutoSize
