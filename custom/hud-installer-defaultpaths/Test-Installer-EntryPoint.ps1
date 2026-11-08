# Native Windows PowerShell 5.1 regression for the actual shipped launch path.
# All configuration, provider and payload files are synthetic authored fixtures.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$TestRoot,
    [Parameter(Mandatory = $true)][string]$ReportPath,
    [string]$SourceRoot = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $SourceRoot) { $SourceRoot = $PSScriptRoot }
$SourceRoot = [IO.Path]::GetFullPath($SourceRoot)
$utf8 = New-Object Text.UTF8Encoding($false)
$checks = New-Object 'System.Collections.Generic.List[object]'
$invokes = New-Object 'System.Collections.Generic.List[object]'
$session = Join-Path ([IO.Path]::GetFullPath($TestRoot)) ('entrypoint-' + [Guid]::NewGuid().ToString('N'))
$powershell = Join-Path $PSHOME 'powershell.exe'
$counter = 0

function Assert-That([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Write-Text([string]$Path, [string]$Value) {
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))
    [IO.File]::WriteAllText($Path, $Value, $utf8)
}
function Write-Json([string]$Path, $Value) { Write-Text $Path ($Value | ConvertTo-Json -Depth 30) }
function Get-Sha([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Get-TextSha([string]$Text) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($utf8.GetBytes($Text)))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Quote-Native([string]$Value) {
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}
function Run-Child([string]$Exe, [string]$Arguments, [string]$WorkingDirectory) {
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $Exe; $start.Arguments = $Arguments; $start.WorkingDirectory = $WorkingDirectory
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true; $start.RedirectStandardInput = $true
    $child = New-Object Diagnostics.Process
    $child.StartInfo = $start
    [void]$child.Start()
    $outTask = $child.StandardOutput.ReadToEndAsync(); $errTask = $child.StandardError.ReadToEndAsync()
    $child.StandardInput.WriteLine(' '); $child.StandardInput.Close()
    if (-not $child.WaitForExit(60000)) { $child.Kill(); throw 'Child exceeded 60 seconds.' }
    $result = [ordered]@{exe = $Exe; arguments = $Arguments; cwd = $WorkingDirectory;
        exit_code = $child.ExitCode; stdout = $outTask.Result; stderr = $errTask.Result}
    $child.Dispose()
    $invokes.Add($result)
    Write-Json (Join-Path $session ('invoke-{0:D2}.json' -f $invokes.Count)) $result
    return [pscustomobject]$result
}
function Run-PS([string]$Script, [string[]]$Arguments = @(), [string]$WorkingDirectory = $session) {
    $args = @('-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $Script) + $Arguments
    return Run-Child $powershell (($args | ForEach-Object { Quote-Native $_ }) -join ' ') $WorkingDirectory
}
function Run-Cmd([string]$Script, [string[]]$Arguments = @(), [string]$WorkingDirectory = $session) {
    # Preserve the exact shipped wrapper. Redirect stdin only to release its pause.
    $command = '"' + $Script + '"'
    foreach ($arg in $Arguments) { $command += ' ' + (Quote-Native $arg) }
    return Run-Child $env:ComSpec ('/d /s /c "' + $command + '"') $WorkingDirectory
}
function Assert-Exit($Result, [int]$Expected) {
    Assert-That ($Result.exit_code -eq $Expected) ("Expected $Expected, got $($Result.exit_code): $($Result.stdout) $($Result.stderr)")
}
function New-Fixture([string]$Name, [string]$Installer) {
    $script:counter++
    $unicode = 'Unicode-' + [char]0x00e9 + [char]0x96ea
    $root = Join-Path $session ('{0:D2}-{1}' -f $script:counter, $Name)
    $outer = Join-Path $root ('___temp\2\OpenMW_Town_Performance_Followup_20261108 [fixture] ' + $unicode)
    $package = Join-Path $outer 'AttendMe_Pixel_Updates'
    $relative = 'scripts/fixture/hud.lua'
    $providers = @((Join-Path $root 'Provider first'), (Join-Path $root ('Provider [last] ' + $unicode)))
    $config = Join-Path $root 'Config folder\openmw.cfg'
    $log = Join-Path $root 'Log folder\openmw.log'
    $backup = Join-Path $root 'Backups [fixture]'
    $before = "-- Authored entrypoint fixture`nreturn 'original'`n"
    $after = "-- Authored entrypoint fixture`nreturn 'changed'`n"
    foreach ($provider in $providers) { Write-Text (Join-Path $provider $relative) $before }
    Write-Text (Join-Path (Join-Path $package 'Data Files') $relative) $after
    Write-Text $config "# Authored entrypoint config`n"
    $manifest = [ordered]@{id = 'entrypoint-fixture'; name = 'Authored entrypoint fixture'; expected_revision = 'fixture123';
        expected_config_sha256 = @((Get-Sha $config)); expected_data_order_sha256 = Get-TextSha ($providers -join "`n");
        files = @(@{path = $relative; expected_sha256 = Get-TextSha $before; installed_sha256 = Get-TextSha $after})}
    Write-Json (Join-Path $package 'manifest.json') $manifest
    $lines = @()
    $lines += '[00:00:00.000 *] Loading config file: ' + $config
    $lines += '[00:00:00.001 I] Revision: fixture123'
    foreach ($provider in $providers) { $lines += '[00:00:00.002 I] Adding data directory ' + $provider }
    Write-Text $log ($lines -join "`r`n")
    [IO.File]::Copy($Installer, (Join-Path $package 'Install-Fix.ps1'), $true)
    foreach ($name in @('INSTALL_HUD_FIX.cmd', 'RESTORE_HUD_FIX.cmd')) {
        [IO.File]::Copy((Join-Path $SourceRoot $name), (Join-Path $outer $name), $true)
    }
    foreach ($name in @('INSTALL.cmd', 'RESTORE.cmd')) {
        [IO.File]::Copy((Join-Path $SourceRoot $name), (Join-Path $package $name), $true)
    }
    return [pscustomobject]@{root = $root; outer = $outer; package = $package; log = $log; backup = $backup;
        config = $config; providers = $providers; relative = $relative; manifest = $manifest;
        target = (Join-Path $providers[1] $relative); before = (Get-TextSha $before); after = (Get-TextSha $after)}
}
function Fixture-Args($Fixture) { return @('-LogPath', $Fixture.log, '-BackupRoot', $Fixture.backup) }
function Assert-Original($Fixture) {
    Assert-That ((Get-Sha $Fixture.target) -eq $Fixture.before) 'Winning provider changed unexpectedly.'
    Assert-That ((Get-Sha (Join-Path $Fixture.providers[0] $Fixture.relative)) -eq $Fixture.before) 'Earlier provider changed.'
    Assert-That ((Get-Sha $Fixture.config) -eq $Fixture.manifest.expected_config_sha256[0]) 'Config changed.'
}
function Check([string]$Name, [scriptblock]$Body) {
    try { & $Body; $checks.Add(@{name = $Name; passed = $true}); Write-Host ('PASS: ' + $Name) }
    catch { $checks.Add(@{name = $Name; passed = $false; error = $_.Exception.Message; location = $_.ScriptStackTrace}); Write-Host ('FAIL: ' + $Name + ': ' + $_.Exception.Message) }
}

[void][IO.Directory]::CreateDirectory($session)
$original = Join-Path $SourceRoot 'Original-Install-Fix.ps1'
$fixed = Join-Path $SourceRoot 'Install-Fix.ps1'
$probe = Join-Path $session 'default-parameter-probe.ps1'
Write-Text $probe @'
[CmdletBinding()]
param([string]$PackageRoot = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
$errorText = $null; $full = $null
try { $full = [IO.Path]::GetFullPath($PackageRoot) } catch { $errorText = $_.Exception.Message }
[ordered]@{parameter_value = $PackageRoot; parameter_length = $PackageRoot.Length;
    body_script_root = $PSScriptRoot; script_path = $PSCommandPath; full_path = $full; error = $errorText;
    version = $PSVersionTable.PSVersion.ToString()} | ConvertTo-Json -Compress
'@

Check 'Native Windows PowerShell 5.1 and actual source AST' {
    Assert-That ($env:OS -eq 'Windows_NT') 'Native Windows required.'
    Assert-That ($PSVersionTable.PSVersion.Major -eq 5 -and $PSVersionTable.PSVersion.Minor -eq 1) 'Native Windows PowerShell 5.1 required.'
    foreach ($path in @($original, $fixed)) {
        $tokens = $null; $errors = $null
        [void][Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
        Assert-That (@($errors).Count -eq 0) (($errors | Out-String))
    }
}
Check 'Diagnose automatic-variable parameter default under native -File' {
    $r = Run-PS $probe; Assert-Exit $r 0
    $p = $r.stdout | ConvertFrom-Json
    Write-Json (Join-Path $session 'default-parameter-probe.json') $p
    Assert-That ($p.parameter_length -eq 0 -and -not [string]::IsNullOrEmpty($p.body_script_root)) 'The hypothesized default-parameter cause did not reproduce; inspect the probe and do not claim it confirmed.'
    Assert-That ($p.error -match 'GetFullPath' -and $p.error -match 'legal form') 'Expected exact GetFullPath error did not reproduce.'
}
Check 'Exact original default-path failure leaves all targets untouched' {
    $f = New-Fixture 'original-default' $original
    $r = Run-Cmd (Join-Path $f.outer 'INSTALL_HUD_FIX.cmd') ((Fixture-Args $f) + @('-CheckOnly'))
    Assert-Exit $r 1
    Assert-That ($r.stdout -match 'GetFullPath' -and $r.stdout -match 'legal form') 'Original did not reproduce the reported error.'
    Assert-That ($r.stdout -notmatch 'Component:') 'Original failure occurred later than reported.'
    Assert-Original $f
    Assert-That (-not (Test-Path -LiteralPath $f.backup)) 'Original failure created a backup.'
}
Check 'Original explicit PackageRoot control reaches preflight successfully' {
    $f = New-Fixture 'original-explicit' $original
    Assert-Exit (Run-PS (Join-Path $f.package 'Install-Fix.ps1') ((Fixture-Args $f) + @('-PackageRoot', $f.package, '-CheckOnly'))) 0
    Assert-Original $f
}
Check 'Fixed default direct -File check uses its own folder from unrelated cwd' {
    $f = New-Fixture 'fixed-default-direct' $fixed
    Assert-Exit (Run-PS (Join-Path $f.package 'Install-Fix.ps1') ((Fixture-Args $f) + @('-CheckOnly')) $session) 0
    Assert-Original $f
    Assert-That (-not (Test-Path -LiteralPath $f.backup)) 'CheckOnly wrote a backup.'
}
Check 'Fixed exact top-level wrapper check works with spaces brackets and Unicode' {
    $f = New-Fixture 'fixed-top-check' $fixed
    Assert-Exit (Run-Cmd (Join-Path $f.outer 'INSTALL_HUD_FIX.cmd') ((Fixture-Args $f) + @('-CheckOnly')) $session) 0
    Assert-Original $f
    Assert-That (-not (Test-Path -LiteralPath $f.backup)) 'Wrapper CheckOnly wrote a backup.'
}
Check 'Fixed actual top wrappers install idempotently and restore exact backup' {
    $f = New-Fixture 'fixed-top-transaction' $fixed
    $install = Join-Path $f.outer 'INSTALL_HUD_FIX.cmd'; $restore = Join-Path $f.outer 'RESTORE_HUD_FIX.cmd'
    Assert-Exit (Run-Cmd $install (Fixture-Args $f) $session) 0
    Assert-That ((Get-Sha $f.target) -eq $f.after) 'Installed bytes differ.'
    $receipts = @(Get-ChildItem -LiteralPath $f.backup -Recurse -File -Filter 'receipt.json')
    Assert-That ($receipts.Count -eq 1) 'Expected one receipt.'
    $receipt = $receipts[0]; $receiptSha = Get-Sha $receipt.FullName
    $saved = Get-Content -LiteralPath $receipt.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert-That ($saved.state -eq 'installed') 'Install receipt not finalized.'
    $savedOriginal = Join-Path (Join-Path $receipt.DirectoryName 'originals') $f.relative
    Assert-That ((Get-Sha $savedOriginal) -eq $f.before) 'Backup is not exact original.'
    $again = Run-Cmd $install (Fixture-Args $f) $session; Assert-Exit $again 0
    Assert-That ($again.stdout -match 'ALREADY INSTALLED') 'Second install was not idempotent.'
    Assert-That ((Get-Sha $receipt.FullName) -eq $receiptSha) 'Idempotent install changed receipt.'
    Assert-Exit (Run-Cmd $restore @('-BackupRoot', $f.backup) $session) 0
    Assert-Original $f
    Assert-That ((Get-Content -LiteralPath $receipt.FullName -Raw -Encoding UTF8 | ConvertFrom-Json).state -eq 'restored') 'Restore receipt not finalized.'
}
Check 'Fixed nested wrappers also install and restore without PackageRoot' {
    $f = New-Fixture 'fixed-nested' $fixed
    Assert-Exit (Run-Cmd (Join-Path $f.package 'INSTALL.cmd') (Fixture-Args $f) $session) 0
    Assert-That ((Get-Sha $f.target) -eq $f.after) 'Nested install bytes differ.'
    Assert-Exit (Run-Cmd (Join-Path $f.package 'RESTORE.cmd') @('-BackupRoot', $f.backup) $session) 0
    Assert-Original $f
}
Check 'Explicit PackageRoot override is preserved' {
    $f = New-Fixture 'fixed-explicit' $fixed
    Assert-Exit (Run-PS $fixed ((Fixture-Args $f) + @('-PackageRoot', $f.package, '-CheckOnly')) $session) 0
    Assert-Original $f
}
Check 'Unknown winning source still refuses to overwrite through actual wrapper' {
    $f = New-Fixture 'fixed-unknown-source' $fixed
    Write-Text $f.target "-- Unknown authored source`n"
    $unknown = Get-Sha $f.target
    $r = Run-Cmd (Join-Path $f.outer 'INSTALL_HUD_FIX.cmd') (Fixture-Args $f) $session
    Assert-Exit $r 2
    Assert-That ($r.stdout -match 'Current mod file differs') 'Source guard did not explain refusal.'
    Assert-That ((Get-Sha $f.target) -eq $unknown) 'Unknown source overwritten.'
    Assert-That (@(Get-ChildItem -LiteralPath $f.backup -Recurse -File -Filter '*.zip').Count -eq 1) 'Missing bounded source evidence.'
    Assert-That (@(Get-ChildItem -LiteralPath $f.backup -Recurse -File -Filter 'receipt.json').Count -eq 0) 'Guard failure created transaction receipt.'
}

$failures = @($checks | Where-Object { -not $_.passed })
$sources = @()
foreach ($name in @('Original-Install-Fix.ps1', 'Install-Fix.ps1', 'INSTALL_HUD_FIX.cmd', 'RESTORE_HUD_FIX.cmd', 'INSTALL.cmd', 'RESTORE.cmd', 'Test-Installer-EntryPoint.ps1')) {
    $p = Join-Path $SourceRoot $name
    $sources += @{name = $name; bytes = (Get-Item -LiteralPath $p).Length; sha256 = Get-Sha $p}
}
Write-Json $ReportPath @{status = $(if ($failures.Count -eq 0) {'PASS'} else {'FAIL'}); powershell = $PSVersionTable.PSVersion.ToString();
    checks = $checks.ToArray(); passed = $checks.Count - $failures.Count; failed = $failures.Count; source_files = $sources;
    session = $session; invocations = $invokes.ToArray(); note = 'Authored fixture bytes only. No real mod source or user configuration is published or modified.'}
if ($failures.Count -gt 0) { exit 1 }
exit 0
