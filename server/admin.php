<?php
/* ====================================================================
 * My File Transfer Assistant - 升级发布管理后台（单文件 PHP，零依赖）
 * --------------------------------------------------------------------
 * 部署方法：
 *   1. 在服务器网站根目录建 mfta/ 目录，把本文件放进去（PHP 7+）
 *   2. 给 mfta/ 目录和 mfta/files/ 写权限
 *   3. 浏览器打开 http(s)://你的域名或IP/mfta/admin.php
 *   4. 输入密码（首次使用请改掉下面的 ADMIN_PASSWORD！）
 *
 * 文件结构（自动生成/维护）：
 *   mfta/admin.php     ← 本管理页
 *   mfta/update.json   ← 客户端检测地址（config.json 的 update_url 指向它）
 *   mfta/upgrade.ps1   ← 升级脚本（发布时自动重写，勿手动改）
 *   mfta/versions.json ← 版本发布历史（回滚/删除的数据源）
 *   mfta/files/        ← 上传的新版安装包（历史版本自动保留 = 备份）
 *
 * 客户端配置（AppData\Roaming\My File Transfer Assistant\config.json）：
 *   "update_url": "http://你的域名或IP/mfta/update.json"
 * ==================================================================== */

define('ADMIN_PASSWORD', 'CHANGE_ME');   /* ★ 部署前务必改成你自己的强密码 ★ */

session_start();
$err = ''; $ok = ''; $test = null;

/* 当前目录 URL（自动推导，兼容 http/https、域名或 IP） */
function base_url() {
    $https = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off')
          || (($_SERVER['HTTP_HOST'] ?? '') && ($_SERVER['SERVER_PORT'] ?? '') == '443');
    $scheme = $https ? 'https' : 'http';
    $dir = rtrim(str_replace('\\', '/', dirname($_SERVER['SCRIPT_NAME'] ?? '/')), '/');
    return $scheme . '://' . ($_SERVER['HTTP_HOST'] ?? 'localhost') . $dir;
}

/* 读当前已发布的 update.json */
function load_published() {
    $f = __DIR__ . '/update.json';
    if (!is_file($f)) return null;
    $d = json_decode(file_get_contents($f), true);
    return is_array($d) ? $d : null;
}

/* 读版本历史；versions.json 不存在时从 update.json 兼容初始化 */
function load_history() {
    $f = __DIR__ . '/versions.json';
    if (is_file($f)) {
        $d = json_decode(file_get_contents($f), true);
        if (is_array($d)) return $d;
    }
    /* 兼容旧部署：把当前发布转成第一条历史记录 */
    $pub = load_published();
    if ($pub && !empty($pub['version'])) {
        $h = [[
            'version'  => $pub['version'],
            'notes'    => $pub['notes'] ?? '',
            'url'      => $pub['url'] ?? '',
            'filename' => basename($pub['url'] ?? ''),
            'size'     => 0,
            'time'     => date('Y-m-d H:i'),
        ]];
        file_put_contents($f, json_encode($h, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES | JSON_PRETTY_PRINT));
        return $h;
    }
    return [];
}

function save_history(array $h) {
    file_put_contents(__DIR__ . '/versions.json',
        json_encode($h, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES | JSON_PRETTY_PRINT));
}

/* 把某个版本记录写为"当前发布"：update.json + upgrade.ps1 */
function publish_record(array $rec) {
    $json = [
        'version' => $rec['version'],
        'notes'   => $rec['notes'],
        'url'     => $rec['url'],
        'script'  => base_url() . '/upgrade.ps1',
    ];
    file_put_contents(__DIR__ . '/update.json',
        json_encode($json, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES | JSON_PRETTY_PRINT));

    $ps  = "\$ErrorActionPreference = 'Stop'\n";
    $ps .= "\$u = '" . $rec['url'] . "'\n";
    $ps .= "\$t = Join-Path \$env:TEMP 'MFTA_setup.exe'\n";
    $ps .= "(New-Object Net.WebClient).DownloadFile(\$u, \$t)\n";
    $ps .= "Stop-Process -Name 'MyFileTransferAssistant' -Force -ErrorAction SilentlyContinue\n";
    $ps .= "Start-Process \$t\n";
    file_put_contents(__DIR__ . '/upgrade.ps1', $ps);
}

/* 一键连通性自测：逐项检查升级链路（不向客户端发任何通知） */
function selftest(&$report) {
    $report = [];
    $pass = true;

    /* ① update.json 存在且 JSON 合法、四字段齐全 */
    $pub = load_published();
    if (!$pub) { $report[] = ['✗', 'update.json 不存在或 JSON 无法解析', '请先发布一个版本']; $pass = false; }
    else {
        $miss = [];
        foreach (['version', 'url', 'script'] as $k) if (empty($pub[$k])) $miss[] = $k;
        if ($miss) { $report[] = ['✗', 'update.json 缺少字段：' . implode(', ', $miss), '请重新发布版本']; $pass = false; }
        else $report[] = ['✓', 'update.json 正常（当前发布 ' . $pub['version'] . '）', ''];
    }

    /* ② 安装包可下载：本地文件存在，或外链 HTTP 200 */
    if ($pub && !empty($pub['url'])) {
        $base = basename($pub['url']);
        $local = __DIR__ . '/files/' . $base;
        if (is_file($local)) {
            $sz = filesize($local);
            $report[] = ['✓', "安装包就绪：files/$base（" . number_format($sz / 1048576, 2) . " MB）", ''];
        } else {
            /* 外链：HEAD 请求验证（注意：不能请求本服务器自身，php -S 单线程会死锁） */
            $host = parse_url($pub['url'], PHP_URL_HOST);
            $self = $_SERVER['HTTP_HOST'] ?? '';
            if ($host && $self && (parse_url($pub['url'], PHP_URL_HOST) === $self || gethostbyname($host) === gethostbyname($self))) {
                $report[] = ['✗', "安装包缺失：files/$base 不存在（URL 指向本服务器）", '请重新上传该版本的安装包'];
                $pass = false;
            } else {
                $ctx = stream_context_create(['http' => ['method' => 'HEAD', 'timeout' => 10]]);
                $hdr = @file_get_contents($pub['url'], false, $ctx);
                $status = $http_response_header[0] ?? '';
                if (strpos($status, ' 200') !== false) $report[] = ['✓', "外链安装包可访问（$status）", ''];
                else { $report[] = ['✗', "外链安装包无法访问（" . ($status ?: '无响应') . "）", '请检查下载地址']; $pass = false; }
            }
        }
    }

    /* ③ upgrade.ps1 存在且内容有效 */
    $psf = __DIR__ . '/upgrade.ps1';
    if (!is_file($psf)) { $report[] = ['✗', 'upgrade.ps1 不存在', '请重新发布版本']; $pass = false; }
    else {
        $ps = file_get_contents($psf);
        $bad = (strpos($ps, 'DownloadFile') === false) || (strpos($ps, 'Start-Process') === false)
            || ($pub && !empty($pub['url']) && strpos($ps, $pub['url']) === false);
        if ($bad) { $report[] = ['✗', 'upgrade.ps1 内容与当前发布不一致', '请重新发布版本']; $pass = false; }
        else $report[] = ['✓', 'upgrade.ps1 有效（下载→关闭旧程序→启动安装）', ''];
    }

    /* ④ files/ 目录可写 */
    $probe = __DIR__ . '/files/.write_test';
    if (@file_put_contents($probe, 'ok') !== false) { @unlink($probe); $report[] = ['✓', 'files/ 目录可写', '']; }
    else { $report[] = ['✗', 'files/ 目录不可写', 'chmod 755 并给 Web 用户写权限']; $pass = false; }

    /* ⑤ 版本历史 */
    $h = load_history();
    $report[] = $h ? ['✓', '版本历史 ' . count($h) . ' 条（历史安装包即旧版备份）', '']
                   : ['!', '暂无版本历史（发布第一个版本后生成）', ''];

    /* ⑥ 客户端检测地址提醒 */
    $report[] = ['i', '客户端 update_url 应为：' . base_url() . '/update.json', ''];
    return $pass;
}

if (isset($_GET['logout'])) { session_destroy(); header('Location: ' . ($_SERVER['SCRIPT_NAME'] ?? '')); exit; }

if (($_SERVER['REQUEST_METHOD'] ?? '') === 'POST' && ($_POST['action'] ?? '') === 'login') {
    if (($_POST['password'] ?? '') === ADMIN_PASSWORD) { $_SESSION['mfta_admin'] = true; }
    else { $err = '密码错误'; }
}
$logged = !empty($_SESSION['mfta_admin']);

if ($logged && ($_SERVER['REQUEST_METHOD'] ?? '') === 'POST') {
    $action = $_POST['action'] ?? '';

    /* ---- 发布新版本 ---- */
    if ($action === 'publish') {
        $version = trim($_POST['version'] ?? '');
        $notes   = trim($_POST['notes'] ?? '');
        $url     = trim($_POST['url'] ?? '');
        $name    = trim($_POST['filename'] ?? '');

        if (!empty($_FILES['setup']['name']) && ($_FILES['setup']['error'] ?? 1) === UPLOAD_ERR_OK) {
            $dir = __DIR__ . '/files';
            if (!is_dir($dir)) mkdir($dir, 0755, true);
            $safe = preg_replace('/[^\w.\-]+/u', '_', basename($_FILES['setup']['name']));
            if (move_uploaded_file($_FILES['setup']['tmp_name'], $dir . '/' . $safe)) {
                $url  = base_url() . '/files/' . rawurlencode($safe);
                $name = $safe;
                $ok .= '安装包已上传：' . htmlspecialchars($safe) . '<br>';
            } else { $err = '安装包上传失败（检查 files/ 目录写权限）'; }
        }

        if ($err === '' && $version === '') $err = '请填写版本号';
        if ($err === '' && $url === '')    $err = '请上传安装包或填写下载地址';

        if ($err === '') {
            $rec = [
                'version'  => $version,
                'notes'    => $notes,
                'url'      => $url,
                'filename' => $name,
                'size'     => is_file(__DIR__ . '/files/' . $name) ? filesize(__DIR__ . '/files/' . $name) : 0,
                'time'     => date('Y-m-d H:i'),
            ];
            /* 版本号相同 → 更新历史中该记录；否则追加（旧版本安装包自动保留为备份） */
            $h = load_history();
            $idx = -1;
            foreach ($h as $i => $r) if ($r['version'] === $version) { $idx = $i; break; }
            if ($idx >= 0) $h[$idx] = $rec; else $h[] = $rec;
            save_history($h);
            publish_record($rec);   /* 写 update.json + upgrade.ps1 */
            $ok .= "版本 $version 已发布！update.json 与 upgrade.ps1 已更新。";
        }
    }

    /* ---- 把历史版本设为当前发布（回滚/切换） ---- */
    if ($action === 'setcurrent') {
        $h = load_history();
        $i = (int)($_POST['idx'] ?? -1);
        if (isset($h[$i])) {
            publish_record($h[$i]);
            $ok .= '当前发布已切换为 ' . $h[$i]['version'] . '。已运行更高版本的用户会在下次检测时收到"回退到该版本"的提示。';
        } else { $err = '指定的历史版本不存在'; }
    }

    /* ---- 删除历史版本 ---- */
    if ($action === 'delete') {
        $h = load_history();
        $i = (int)($_POST['idx'] ?? -1);
        if (!isset($h[$i])) { $err = '指定的历史版本不存在'; }
        else {
            $pub = load_published();
            $isCurrent = $pub && ($pub['version'] ?? '') === $h[$i]['version']
                                  && ($pub['url'] ?? '') === ($h[$i]['url'] ?? '');
            if ($isCurrent) {
                $err = '不能删除当前发布中的版本（' . $h[$i]['version'] . '），请先把当前发布切换到其他版本';
            } else {
                $name = $h[$i]['filename'] ?? '';
                $f = __DIR__ . '/files/' . $name;
                /* 只有本地文件且不被其他历史版本引用时才物理删除 */
                if ($name && is_file($f)) {
                    $ref = 0;
                    foreach ($h as $r) if (($r['filename'] ?? '') === $name) $ref++;
                    if ($ref <= 1) unlink($f);
                }
                $v = $h[$i]['version'];
                unset($h[$i]);
                save_history(array_values($h));
                $ok .= "版本 $v 已删除" . ($name ? "（安装包 $name 已一并清理）" : '') . '。';
            }
        }
    }

    /* ---- 一键测试连通性 ---- */
    if ($action === 'selftest') { selftest($test); }
}
$pub = load_published();
$hist = $logged ? load_history() : [];
function fmt_size($b) { return $b > 0 ? number_format($b / 1048576, 2) . ' MB' : '—'; }
?>
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MFTA 升级发布后台</title>
<style>
  body{font-family:"Segoe UI","Microsoft YaHei",sans-serif;background:#f3f4f9;margin:0;color:#262a38}
  .card{max-width:860px;margin:32px auto;background:#fff;border-radius:14px;padding:30px 36px;
        box-shadow:0 4px 18px rgba(30,35,60,.08)}
  h1{font-size:20px;margin:0 0 6px} h2{font-size:16px;margin:0 0 14px}
  .sub{color:#8a8fa3;font-size:13px;margin-bottom:22px}
  label{display:block;font-size:13px;color:#5a5f72;margin:16px 0 6px;font-weight:600}
  input[type=text],input[type=password],textarea{width:100%;box-sizing:border-box;padding:10px 12px;
        border:1px solid #d5d8e3;border-radius:9px;font-size:14px;background:#fafbfe}
  textarea{height:96px;resize:vertical}
  input[type=file]{width:100%;padding:8px;font-size:13px}
  button{margin-top:20px;background:#6c5ce7;color:#fff;border:0;border-radius:10px;
        padding:12px 26px;font-size:15px;cursor:pointer}
  button:hover{background:#5b4bd1}
  button.mini{margin-top:0;padding:6px 14px;font-size:13px;border-radius:8px}
  button.gray{background:#8a8fa3} button.gray:hover{background:#767b8f}
  button.red{background:#d64541} button.red:hover{background:#bb3a37}
  .msg-ok{background:#e9f9ef;color:#1c7c3c;border-radius:9px;padding:12px;font-size:14px;margin-bottom:10px}
  .msg-err{background:#fdeaea;color:#b3261e;border-radius:9px;padding:12px;font-size:14px;margin-bottom:10px}
  .cur{background:#f6f7fc;border-radius:10px;padding:14px 16px;font-size:13px;color:#5a5f72;margin-bottom:6px}
  .logout{float:right;font-size:13px;color:#6c5ce7;text-decoration:none}
  .hint{font-size:12px;color:#9aa0b4;margin-top:4px}
  table{width:100%;border-collapse:collapse;font-size:13px;margin-top:8px}
  th{background:#f6f7fc;color:#5a5f72;font-weight:600;padding:10px 8px;text-align:left}
  td{padding:10px 8px;border-top:1px solid #eef0f6;vertical-align:top}
  tr.cur-row td{background:#f3f0ff}
  .tag{display:inline-block;background:#6c5ce7;color:#fff;font-size:11px;border-radius:6px;padding:2px 8px}
  .notes{color:#8a8fa3;font-size:12px;max-width:260px}
  .btns{white-space:nowrap}
  .test{border:1px solid #e3e6f0;border-radius:10px;padding:6px 14px;margin-top:10px;background:#fafbff}
  .test table{margin-top:4px} .test td{border-top:0;padding:7px 6px}
  .ok{color:#1c7c3c;font-weight:700} .bad{color:#b3261e;font-weight:700}
  .warn{color:#b8860b;font-weight:700} .info{color:#5a5f72}
</style>
</head>
<body>
<div class="card">
<?php if (!$logged): ?>
  <h1>MFTA 升级发布后台</h1>
  <div class="sub">请输入管理密码</div>
  <?php if ($err): ?><div class="msg-err"><?= $err ?></div><?php endif; ?>
  <form method="post">
    <input type="hidden" name="action" value="login">
    <label>密码</label>
    <input type="password" name="password" autofocus>
    <button>登录</button>
  </form>
<?php else: ?>
  <h1>升级发布后台 <a class="logout" href="?logout=1">退出</a></h1>
  <div class="sub">客户端拉取 update.json 检测版本 → 弹窗提醒 → 确认后由 upgrade.ps1 自动下载安装（升级或回滚）</div>
  <?php if ($ok): ?><div class="msg-ok"><?= $ok ?></div><?php endif; ?>
  <?php if ($err): ?><div class="msg-err"><?= $err ?></div><?php endif; ?>

  <?php if ($pub): ?>
    <div class="cur">当前发布：<b><?= htmlspecialchars($pub['version'] ?? '?') ?></b>
      &nbsp;·&nbsp; <a href="update.json" target="_blank">update.json</a>
      &nbsp;·&nbsp; 客户端检测地址 <code><?= base_url() ?>/update.json</code></div>
  <?php else: ?>
    <div class="cur">尚未发布任何版本。</div>
  <?php endif; ?>

  <form method="post" style="margin:0">
    <input type="hidden" name="action" value="selftest">
    <button class="gray" type="submit">一键测试连通性</button>
    <span class="hint">在服务器端模拟客户端升级链路逐项自检（不影响客户端，客户端无任何感知）</span>
  </form>
  <?php if (is_array($test)): ?>
  <div class="test">
    <table>
      <?php foreach ($test as [$s, $t, $tip]): ?>
      <tr>
        <td style="width:26px"><?= $s === '✓' ? '<span class="ok">✓</span>' : ($s === '!' ? '<span class="warn">!</span>' : ($s === 'i' ? '<span class="info">i</span>' : '<span class="bad">✗</span>')) ?></td>
        <td><?= htmlspecialchars($t) ?><?= $tip ? '<br><span class="hint">' . htmlspecialchars($tip) . '</span>' : '' ?></td>
      </tr>
      <?php endforeach; ?>
    </table>
  </div>
  <?php endif; ?>

  <h2 style="margin-top:30px">发布新版本</h2>
  <form method="post" enctype="multipart/form-data">
    <input type="hidden" name="action" value="publish">
    <label>新版版本号 *</label>
    <input type="text" name="version" placeholder="例如 V1.3.0" required>
    <div class="hint">比客户端版本高触发"升级"提醒；比客户端低可通过历史"设为当前发布"触发"回退"</div>
    <label>新版简介（升级弹窗中展示给用户）</label>
    <textarea name="notes" placeholder="1. 修复了…&#10;2. 新增了…"></textarea>
    <label>上传新版安装包（推荐）</label>
    <input type="file" name="setup" accept=".exe">
    <div class="hint">上传后下载地址自动生成；历史版本的安装包会一直保留在 files/ 作为回滚备份</div>
    <label>或手动填写下载地址（不选文件时必填）</label>
    <input type="text" name="url" placeholder="http://…/Setup_MyFileTransferAssistant_V1.3.0.exe">
    <button>发布版本</button>
  </form>

  <h2 style="margin-top:34px">版本历史（<?= count($hist) ?> 条<?= $hist ? '，最新在前' : '' ?>）</h2>
  <?php if (!$hist): ?>
    <div class="sub" style="margin-top:4px">还没有历史版本，发布第一个版本后会出现在这里。</div>
  <?php else: ?>
  <table>
    <tr><th style="width:80px">版本</th><th style="width:130px">发布时间</th><th style="width:80px">大小</th><th>简介</th><th style="width:200px">操作</th></tr>
    <?php foreach (array_reverse($hist, true) as $idx => $r):
        $isCur = $pub && ($pub['version'] ?? '') === $r['version'] && ($pub['url'] ?? '') === ($r['url'] ?? ''); ?>
    <tr class="<?= $isCur ? 'cur-row' : '' ?>">
      <td><b><?= htmlspecialchars($r['version']) ?></b><?= $isCur ? ' <span class="tag">当前发布</span>' : '' ?></td>
      <td><?= htmlspecialchars($r['time'] ?? '') ?></td>
      <td><?= fmt_size($r['size'] ?? 0) ?></td>
      <td class="notes"><?= htmlspecialchars($r['notes'] ?? '') ?: '—' ?></td>
      <td class="btns">
        <?php if (!$isCur): ?>
        <form method="post" style="display:inline;margin:0;padding:0">
          <input type="hidden" name="action" value="setcurrent"><input type="hidden" name="idx" value="<?= $idx ?>">
          <button class="mini" type="submit">设为当前发布</button>
        </form>
        <?php endif; ?>
        <?php if (!$isCur): ?>
        <form method="post" style="display:inline;margin:0;padding:0" onsubmit="return confirm('确定删除版本 <?= htmlspecialchars($r['version']) ?> ？其安装包备份将一并清理，且无法恢复。')">
          <input type="hidden" name="action" value="delete"><input type="hidden" name="idx" value="<?= $idx ?>">
          <button class="mini red" type="submit">删除</button>
        </form>
        <?php endif; ?>
      </td>
    </tr>
    <?php endforeach; ?>
  </table>
  <div class="hint" style="margin-top:10px">"设为当前发布"= 回滚：已装更高版本的用户会收到"回退到该版本"的提示；装旧版本的用户会收到"升级"提示。</div>
  <?php endif; ?>
<?php endif; ?>
</div>
</body>
</html>
