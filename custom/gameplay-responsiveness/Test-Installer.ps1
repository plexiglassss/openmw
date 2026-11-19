# Native Windows PowerShell 5.1 checks for the authored installer.
# Generates all configuration, log and Lua fixture bytes locally. No game or
# third-party mod source is required, downloaded, executed or published.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$InstallerScript,
    [string]$WrapperScript = '',
    [Parameter(Mandatory = $true)][string]$TestRoot,
    [Parameter(Mandatory = $true)][string]$ReportPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$utf8 = New-Object System.Text.UTF8Encoding($false)
$checks = New-Object System.Collections.Generic.List[object]
$caseNumber = 0
$invokeNumber = 0
$sessionRoot = Join-Path ([IO.Path]::GetFullPath($TestRoot)) ('session-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($sessionRoot)
$InstallerScript = [IO.Path]::GetFullPath($InstallerScript)
if (-not $WrapperScript) { $WrapperScript = Join-Path ([IO.Path]::GetDirectoryName($InstallerScript)) 'Install-All.ps1' }
$WrapperScript = [IO.Path]::GetFullPath($WrapperScript)
$powershellExe = Join-Path $PSHOME 'powershell.exe'

function Assert-That([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Write-Text([string]$Path, [string]$Value) {
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))
    [IO.File]::WriteAllText($Path, $Value, $utf8)
}
function Write-Json([string]$Path, $Value) {
    Write-Text $Path ($Value | ConvertTo-Json -Depth 30)
}
function Get-Sha([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Get-TextSha([string]$Value) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($utf8.GetBytes($Value)))).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}
function Save-Manifest($Fixture) {
    Write-Json (Join-Path $Fixture.package 'manifest.json') $Fixture.manifest
}
function Save-Log($Fixture) {
    Write-Text $Fixture.log ($Fixture.lines -join "`r`n")
}
function New-Fixture([string]$Name) {
    $script:caseNumber++
    $root = Join-Path $sessionRoot ('{0:D2}-{1}' -f $script:caseNumber, $Name)
    $unicode = 'Unicode-' + [char]0x00E9 + [char]0x96EA
    $package = Join-Path $root ('Package [fixture] ' + $unicode)
    $providers = @((Join-Path $root 'Provider first'), (Join-Path $root ('Provider [last] ' + $unicode)))
    $configPaths = @((Join-Path $root 'Engine config\openmw.cfg'), (Join-Path $root 'User config\openmw.cfg'))
    Write-Text $configPaths[0] "# Authored engine configuration fixture`r`nencoding=win1252`r`n"
    Write-Text $configPaths[1] ("# Authored user configuration fixture`r`n" + 'data="' + $providers[1] + '"' + "`r`n")
    $files = @()
    foreach ($number in 1..2) {
        $relative = 'scripts/installerFixture/file' + $number + '.lua'
        $before = "-- Authored installer fixture, original $number`nreturn { state = 'original-$number' }`n"
        $after = "-- Authored installer fixture, candidate $number`nreturn { state = 'candidate-$number' }`n"
        foreach ($provider in $providers) { Write-Text (Join-Path $provider $relative) $before }
        Write-Text (Join-Path (Join-Path $package 'Data Files') $relative) $after
        $files += [ordered]@{path = $relative; expected_sha256 = Get-TextSha $before; installed_sha256 = Get-TextSha $after}
    }
    $manifest = [ordered]@{
        id = ('fixture-' + $script:caseNumber); name = ('Authored fixture ' + $Name)
        expected_revision = '0123456789'
        expected_config_sha256 = @((Get-Sha $configPaths[0]), (Get-Sha $configPaths[1]))
        expected_data_order_sha256 = Get-TextSha ($providers -join "`n")
        files = $files
    }
    $lines = @()
    foreach ($config in $configPaths) { $lines += ('[12:00:00.000 *] Loading config file: ' + $config) }
    $lines += '[12:00:00.001 I] OpenMW version 0.52.0'
    $lines += ('[12:00:00.002 I] Revision: ' + $manifest.expected_revision)
    foreach ($provider in $providers) { $lines += ('[12:00:00.003 I] Adding data directory ' + $provider) }
    $fixture = [pscustomobject]@{
        root = $root; package = $package; providers = $providers; configs = $configPaths
        log = (Join-Path $root 'Logs [fixture]\openmw.log'); backup = (Join-Path $root 'Backups [fixture]')
        manifest = $manifest; lines = $lines
    }
    Save-Manifest $fixture
    Save-Log $fixture
    return $fixture
}
function ConvertTo-NativeArgument([string]$Value) {
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}
function Invoke-Script($Fixture, [string]$Script, [string[]]$Parameters) {
    $script:invokeNumber++
    $arguments = @('-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $Script) + $Parameters
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $powershellExe
    $start.Arguments = (($arguments | ForEach-Object { ConvertTo-NativeArgument $_ }) -join ' ')
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $start
    [void]$process.Start()
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(60000)) {
        $process.Kill()
        throw 'The isolated installer test child exceeded 60 seconds.'
    }
    $stdout = $stdoutTask.Result
    $stderr = $stderrTask.Result
    $exitCode = $process.ExitCode
    $process.Dispose()
    $prefix = Join-Path $Fixture.root ('invoke-{0:D2}' -f $script:invokeNumber)
    Write-Text ($prefix + '.stdout.txt') $stdout
    Write-Text ($prefix + '.stderr.txt') $stderr
    Write-Json ($prefix + '.json') @{arguments = $arguments; exit_code = $exitCode}
    return [pscustomobject]@{code = $exitCode; stdout = $stdout; stderr = $stderr}
}
function Invoke-Installer($Fixture, [string[]]$Extra = @()) {
    return Invoke-Script $Fixture $InstallerScript (@('-PackageRoot', $Fixture.package,
        '-LogPath', $Fixture.log, '-BackupRoot', $Fixture.backup) + $Extra)
}
function Assert-Exit($Result, [int]$Expected) {
    Assert-That ($Result.code -eq $Expected) ("Expected exit $Expected, got $($Result.code).`n$($Result.stdout)`n$($Result.stderr)")
}
function Assert-Bytes($Fixture, [string]$State = 'original') {
    foreach ($file in $Fixture.manifest.files) {
        $expected = if ($State -eq 'installed') { $file.installed_sha256 } else { $file.expected_sha256 }
        Assert-That ((Get-Sha (Join-Path $Fixture.providers[1] $file.path)) -eq $expected) ('Wrong winning bytes: ' + $file.path)
        Assert-That ((Get-Sha (Join-Path $Fixture.providers[0] $file.path)) -eq $file.expected_sha256) ('Earlier provider changed: ' + $file.path)
    }
    foreach ($index in 0..1) {
        Assert-That ((Get-Sha $Fixture.configs[$index]) -eq $Fixture.manifest.expected_config_sha256[$index]) 'Configuration bytes changed.'
    }
}
function Get-Receipts($Fixture) {
    if (-not (Test-Path -LiteralPath $Fixture.backup)) { return @() }
    return @(Get-ChildItem -LiteralPath $Fixture.backup -Recurse -File -Filter 'receipt.json')
}
function Get-InstalledReceipt($Fixture) {
    $receipts = @(Get-Receipts $Fixture | Where-Object {
        (Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8 | ConvertFrom-Json).state -eq 'installed'
    })
    Assert-That ($receipts.Count -eq 1) 'Expected exactly one installed receipt.'
    return $receipts[0]
}
function Assert-NoTemporaryFiles($Fixture) {
    $temporary = @(Get-ChildItem -LiteralPath $Fixture.root -Recurse -File -Filter '*.tmp')
    Assert-That ($temporary.Count -eq 0) 'Transaction temporary files leaked.'
}
function Check([string]$Name, [scriptblock]$Body) {
    try {
        & $Body
        $checks.Add([ordered]@{name = $Name; passed = $true})
        Write-Host ('PASS: ' + $Name)
    }
    catch {
        $checks.Add([ordered]@{name = $Name; passed = $false; error = $_.Exception.Message; location = $_.ScriptStackTrace})
        Write-Host ('FAIL: ' + $Name + ': ' + $_.Exception.Message)
    }
}


function Add-DefinitionContext($Fixture) {
    $prefix = 'scripts/omw_achievements/achievements/'
    $dependencies = @()
    foreach ($relative in @(($prefix + 'achievements.lua'), ($prefix + 'baseAchievements.lua'), 'omw-achievements.omwscripts')) {
        $text = '-- Entirely authored dependency fixture: ' + $relative + "`nreturn { fixture = true }`n"
        foreach ($provider in $Fixture.providers) { Write-Text (Join-Path $provider $relative) $text }
        $dependencies += [ordered]@{path = $relative; sha256 = Get-TextSha $text}
    }
    $Fixture.manifest.read_only_dependencies = $dependencies
    $Fixture.manifest.definition_prefix = $prefix
    $Fixture.manifest.expected_archive_order_sha256 = Get-TextSha ''
    Save-Manifest $Fixture
}
function Write-BsaFixture([string]$Path, [string[]]$Names) {
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))
    $encoded = @(); $offsets = @(); [uint32]$bytes = 0
    foreach ($name in $Names) {
        $part = [Text.Encoding]::ASCII.GetBytes($name + [char]0)
        $offsets += $bytes; $bytes += $part.Length
        $encoded += ,$part
    }
    $stream = [IO.File]::Create($Path)
    $writer = New-Object IO.BinaryWriter($stream)
    try {
        $writer.Write([uint32]0x100)
        $writer.Write([uint32](12 * $Names.Count + $bytes))
        $writer.Write([uint32]$Names.Count)
        for ($i = 0; $i -lt $Names.Count; $i++) { $writer.Write([uint32]1); $writer.Write([uint32]$i) }
        foreach ($offset in $offsets) { $writer.Write([uint32]$offset) }
        foreach ($part in $encoded) { $writer.Write([byte[]]$part) }
        foreach ($name in $Names) { $writer.Write([uint64]0) }
        foreach ($name in $Names) { $writer.Write([byte]0) }
    }
    finally { $writer.Dispose() }
}
function Add-BsaFixture($Fixture, [string[]]$Names, [string]$Name = 'authored.bsa') {
    $path = Join-Path $Fixture.root $Name
    Write-BsaFixture $path $Names
    $Fixture.lines += ('[12:00:00.003 I] Adding BSA archive ' + $path)
    $paths = @()
    foreach ($line in $Fixture.lines) { if ($line -match 'Adding BSA archive (.+)$') { $paths += $Matches[1] } }
    $Fixture.manifest.expected_archive_order_sha256 = Get-TextSha ($paths -join "`n")
    Save-Manifest $Fixture; Save-Log $Fixture
    return $path
}

Check 'Native Windows PowerShell 5.1 runtime and installer AST' {
    Assert-That ($env:OS -eq 'Windows_NT') 'This harness requires native Windows.'
    Assert-That ($PSVersionTable.PSVersion.Major -eq 5 -and $PSVersionTable.PSVersion.Minor -eq 1) 'This run must use Windows PowerShell 5.1.'
    $tokens = $null; $errors = $null
    foreach ($scriptPath in @($InstallerScript, $WrapperScript)) {
        [void][Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$errors)
        Assert-That (@($errors).Count -eq 0) (($errors | Out-String))
    }
}
Check 'CheckOnly validates Unicode and literal bracket paths without writes' {
    $f = New-Fixture 'check-only'
    Assert-Exit (Invoke-Installer $f @('-CheckOnly')) 0
    Assert-Bytes $f
    Assert-That (-not (Test-Path -LiteralPath $f.backup)) 'CheckOnly created backup or evidence files.'
}
Check 'Install selects latest provider, records exact originals and is idempotent' {
    $f = New-Fixture 'install'
    Assert-Exit (Invoke-Installer $f) 0
    Assert-Bytes $f 'installed'
    $receipt = Get-InstalledReceipt $f
    $saved = Get-Content -LiteralPath $receipt.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert-That (@($saved.files).Count -eq 2) 'Receipt omitted a file.'
    foreach ($file in $f.manifest.files) {
        Assert-That ((Get-Sha (Join-Path (Join-Path $receipt.DirectoryName 'originals') $file.path)) -eq $file.expected_sha256) 'Backup bytes differ.'
    }
    $before = Get-Sha $receipt.FullName
    Assert-Exit (Invoke-Installer $f) 0
    Assert-Bytes $f 'installed'
    Assert-That (@(Get-Receipts $f).Count -eq 1) 'Idempotent install made another backup.'
    Assert-That ((Get-Sha $receipt.FullName) -eq $before) 'Idempotent install changed receipt.'
    Assert-NoTemporaryFiles $f
}
Check 'Explicit restoration restores exact bytes and records completion' {
    $f = New-Fixture 'restore-explicit'
    Assert-Exit (Invoke-Installer $f) 0
    $receipt = Get-InstalledReceipt $f
    Assert-Exit (Invoke-Installer $f @('-RestoreBackup', $receipt.DirectoryName)) 0
    Assert-Bytes $f
    Assert-That ((Get-Content -LiteralPath $receipt.FullName -Raw -Encoding UTF8 | ConvertFrom-Json).state -eq 'restored') 'Receipt not marked restored.'
    Assert-NoTemporaryFiles $f
}
Check 'RestoreLatest selects the installed receipt and supports reinstall' {
    $f = New-Fixture 'restore-latest'
    Assert-Exit (Invoke-Installer $f) 0
    Assert-Exit (Invoke-Installer $f @('-RestoreLatest')) 0
    Assert-Bytes $f
    Assert-Exit (Invoke-Installer $f) 0
    Assert-Bytes $f 'installed'
    Assert-That (@(Get-Receipts $f).Count -eq 2) 'Reinstall did not retain the earlier restore record.'
    Assert-Exit (Invoke-Installer $f @('-RestoreLatest')) 0
    Assert-Bytes $f
}
Check 'Unknown source blocks the whole component and produces source evidence' {
    $f = New-Fixture 'source-mismatch'
    $target = Join-Path $f.providers[1] $f.manifest.files[1].path
    Write-Text $target '-- Authored unrecognized source'
    $unknown = Get-Sha $target
    Assert-Exit (Invoke-Installer $f) 2
    Assert-That ((Get-Sha (Join-Path $f.providers[1] $f.manifest.files[0].path)) -eq $f.manifest.files[0].expected_sha256) 'First source changed before whole-component validation.'
    Assert-That ((Get-Sha $target) -eq $unknown) 'Unknown source overwritten.'
    Assert-That (@(Get-ChildItem -LiteralPath $f.backup -Recurse -File -Filter '*.zip').Count -eq 1) 'Missing bounded source-evidence ZIP.'
    Assert-That (@(Get-Receipts $f).Count -eq 0) 'Blocked install wrote a transaction receipt.'
}
Check 'Missing loose source is blocked without installing the other file' {
    $f = New-Fixture 'missing-source'
    foreach ($provider in $f.providers) { Remove-Item -LiteralPath (Join-Path $provider $f.manifest.files[1].path) }
    Assert-Exit (Invoke-Installer $f) 2
    Assert-That ((Get-Sha (Join-Path $f.providers[1] $f.manifest.files[0].path)) -eq $f.manifest.files[0].expected_sha256) 'First source changed before missing-source detection.'
    Assert-That (@(Get-Receipts $f).Count -eq 0) 'Missing-source guard wrote an install receipt.'
}
Check 'Tampered payload is blocked before source changes' {
    $f = New-Fixture 'payload-tamper'
    Write-Text (Join-Path (Join-Path $f.package 'Data Files') $f.manifest.files[1].path) '-- Authored damaged payload'
    Assert-Exit (Invoke-Installer $f) 2
    Assert-Bytes $f
}
Check 'Partial prior installation is blocked as a complete component' {
    $f = New-Fixture 'partial-install'
    [IO.File]::Copy((Join-Path (Join-Path $f.package 'Data Files') $f.manifest.files[0].path), (Join-Path $f.providers[1] $f.manifest.files[0].path), $true)
    Assert-Exit (Invoke-Installer $f) 2
    Assert-That ((Get-Sha (Join-Path $f.providers[1] $f.manifest.files[0].path)) -eq $f.manifest.files[0].installed_sha256) 'Already-patched source changed.'
    Assert-That ((Get-Sha (Join-Path $f.providers[1] $f.manifest.files[1].path)) -eq $f.manifest.files[1].expected_sha256) 'Incomplete component was silently completed.'
}
Check 'Changed configuration blocks installation without editing configuration' {
    $f = New-Fixture 'config-mismatch'
    Write-Text $f.configs[1] "# Authored changed config`n"
    $changed = Get-Sha $f.configs[1]
    Assert-Exit (Invoke-Installer $f) 2
    Assert-That ((Get-Sha $f.configs[1]) -eq $changed) 'Changed configuration was overwritten.'
    foreach ($file in $f.manifest.files) { Assert-That ((Get-Sha (Join-Path $f.providers[1] $file.path)) -eq $file.expected_sha256) 'Source changed under unknown config.' }
}
Check 'Config hash comparison is order-independent but counts duplicates' {
    $f = New-Fixture 'config-multiset'
    $swap = $f.lines[0]; $f.lines[0] = $f.lines[1]; $f.lines[1] = $swap
    Save-Log $f
    Assert-Exit (Invoke-Installer $f @('-CheckOnly')) 0
    $f.lines += $f.lines[0]
    Save-Log $f
    Assert-Exit (Invoke-Installer $f @('-CheckOnly')) 2
    Assert-Bytes $f
}
Check 'Changed provider order and wrong revision are rejected' {
    $f = New-Fixture 'order-revision'
    $swap = $f.lines[4]; $f.lines[4] = $f.lines[5]; $f.lines[5] = $swap
    Save-Log $f
    Assert-Exit (Invoke-Installer $f) 2
    Assert-Bytes $f
    $swap = $f.lines[4]; $f.lines[4] = $f.lines[5]; $f.lines[5] = $swap
    $f.lines[3] = '[12:00:00.002 I] Revision: 9876543210'
    Save-Log $f
    Assert-Exit (Invoke-Installer $f) 2
    Assert-Bytes $f
}
Check 'Missing or repeated startup revision and absent log are rejected' {
    $f = New-Fixture 'ambiguous-log'
    $originalLines = @($f.lines)
    $f.lines += $f.lines[3]
    Save-Log $f
    Assert-Exit (Invoke-Installer $f) 2
    $f.lines = @($originalLines | Where-Object { $_ -notmatch 'Revision:' })
    Save-Log $f
    Assert-Exit (Invoke-Installer $f) 2
    Remove-Item -LiteralPath $f.log
    Assert-Exit (Invoke-Installer $f) 2
    Assert-Bytes $f
}
Check 'Case-insensitive duplicate paths and path traversal are rejected' {
    $f = New-Fixture 'invalid-path'
    $f.manifest.files[1].path = $f.manifest.files[0].path.ToUpperInvariant().Replace('/', '\')
    Save-Manifest $f
    Assert-Exit (Invoke-Installer $f) 2
    $f.manifest.files[1].path = 'scripts/../../outside.lua'
    Save-Manifest $f
    Assert-Exit (Invoke-Installer $f) 2
    Assert-That (-not (Test-Path -LiteralPath (Join-Path $f.root 'outside.lua'))) 'Traversal created an outside file.'
}
Check 'A later edit blocks restoration of every file' {
    $f = New-Fixture 'restore-later-edit'
    Assert-Exit (Invoke-Installer $f) 0
    $receipt = Get-InstalledReceipt $f
    $target = Join-Path $f.providers[1] $f.manifest.files[1].path
    Write-Text $target '-- Authored later user edit'
    $changed = Get-Sha $target
    Assert-Exit (Invoke-Installer $f @('-RestoreBackup', $receipt.DirectoryName)) 2
    Assert-That ((Get-Sha $target) -eq $changed) 'Later edit overwritten by restoration.'
    Assert-That ((Get-Sha (Join-Path $f.providers[1] $f.manifest.files[0].path)) -eq $f.manifest.files[0].installed_sha256) 'Restoration partly ran before checking all targets.'
    Assert-That ((Get-Content -LiteralPath $receipt.FullName -Raw -Encoding UTF8 | ConvertFrom-Json).state -eq 'installed') 'Guard changed the installed receipt state.'
}
Check 'Tampered original backup blocks restoration' {
    $f = New-Fixture 'restore-bad-backup'
    Assert-Exit (Invoke-Installer $f) 0
    $receipt = Get-InstalledReceipt $f
    Write-Text (Join-Path (Join-Path $receipt.DirectoryName 'originals') $f.manifest.files[1].path) '-- Authored corrupt backup'
    Assert-Exit (Invoke-Installer $f @('-RestoreLatest')) 2
    Assert-Bytes $f 'installed'
}
Check 'Incompatible restoration modes and absent backup are rejected' {
    $f = New-Fixture 'restore-modes'
    Assert-Exit (Invoke-Installer $f @('-RestoreLatest')) 2
    Assert-Exit (Invoke-Installer $f @('-RestoreLatest', '-CheckOnly')) 2
    Assert-Exit (Invoke-Installer $f @('-RestoreLatest', '-RestoreBackup', $f.root)) 2
    Assert-Bytes $f
}
Check 'Second-target replacement failure rolls the first target back' {
    $f = New-Fixture 'install-rollback'
    $target = Join-Path $f.providers[1] $f.manifest.files[1].path
    $lock = [IO.File]::Open($target, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try { $result = Invoke-Installer $f }
    finally { $lock.Dispose() }
    Assert-Exit $result 1
    Assert-Bytes $f
    $receipts = @(Get-Receipts $f)
    Assert-That ($receipts.Count -eq 1) 'Failed transaction receipt missing.'
    $receipt = Get-Content -LiteralPath $receipts[0].FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert-That ($receipt.state -eq 'failed_rolled_back') 'Failed transaction was not recorded as rolled back.'
    Assert-That (@($receipt.rollback_errors).Count -eq 0) 'Rollback reported errors.'
    Assert-NoTemporaryFiles $f
    Assert-Exit (Invoke-Installer $f) 0
    Assert-Bytes $f 'installed'
}
Check 'Failed restoration rolls back to patched bytes and remains retryable' {
    $f = New-Fixture 'restore-rollback'
    Assert-Exit (Invoke-Installer $f) 0
    $receipt = Get-InstalledReceipt $f
    $target = Join-Path $f.providers[1] $f.manifest.files[1].path
    $lock = [IO.File]::Open($target, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try { $result = Invoke-Installer $f @('-RestoreBackup', $receipt.DirectoryName) }
    finally { $lock.Dispose() }
    Assert-Exit $result 1
    Assert-Bytes $f 'installed'
    Assert-NoTemporaryFiles $f
    Assert-Exit (Invoke-Installer $f @('-RestoreLatest')) 0
    Assert-Bytes $f
}
Check 'Receipt finalization failure rolls installed files back and preserves the prior receipt' {
    $f = New-Fixture 'receipt-finalization'
    # A native watcher acquires a read-share lock as soon as the prepared
    # receipt appears. A larger authored dummy payload keeps staging open long
    # enough for the watcher; it does not add any installer test hook.
    $source = @'
using System;
using System.IO;
public sealed class InstallerReceiptLock : IDisposable {
    private readonly object gate = new object();
    private FileSystemWatcher watcher;
    private FileStream held;
    private bool disposed;
    public InstallerReceiptLock(string root) {
        watcher = new FileSystemWatcher(root, "receipt.json");
        watcher.IncludeSubdirectories = true;
        watcher.NotifyFilter = NotifyFilters.FileName | NotifyFilters.LastWrite;
        watcher.Created += TryLock;
        watcher.Changed += TryLock;
        watcher.Renamed += delegate(object sender, RenamedEventArgs e) { TryLock(sender, e); };
        watcher.EnableRaisingEvents = true;
    }
    private void TryLock(object sender, FileSystemEventArgs e) {
        lock (gate) {
            if (held != null || disposed) return;
            try { held = File.Open(e.FullPath, FileMode.Open, FileAccess.Read, FileShare.Read); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
    }
    public bool HasLock { get { lock (gate) { return held != null; } } }
    public void Dispose() {
        watcher.EnableRaisingEvents = false;
        watcher.Dispose();
        lock (gate) { disposed = true; if (held != null) { held.Dispose(); held = null; } }
    }
}
'@
    Add-Type -TypeDefinition $source -Language CSharp
    $payload = Join-Path (Join-Path $f.package 'Data Files') $f.manifest.files[0].path
    [IO.File]::WriteAllBytes($payload, (New-Object byte[] (16 * 1024 * 1024)))
    $f.manifest.files[0].installed_sha256 = Get-Sha $payload
    Save-Manifest $f
    [void][IO.Directory]::CreateDirectory($f.backup)
    $watcher = New-Object InstallerReceiptLock -ArgumentList $f.backup
    try {
        $result = Invoke-Installer $f
        Assert-That $watcher.HasLock 'Native receipt lock did not engage; failure was not injected.'
    }
    finally { $watcher.Dispose() }
    Assert-Exit $result 1
    Assert-Bytes $f
    $receipts = @(Get-Receipts $f)
    Assert-That ($receipts.Count -eq 1) 'Prepared receipt missing after finalization failure.'
    $receipt = Get-Content -LiteralPath $receipts[0].FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert-That ($receipt.state -eq 'prepared' -and @($receipt.files).Count -eq 2) 'Original prepared receipt was corrupted or lost.'
    Assert-NoTemporaryFiles $f
    Assert-Exit (Invoke-Installer $f) 0
    Assert-Bytes $f 'installed'
    Assert-Exit (Invoke-Installer $f @('-RestoreLatest')) 0
    Assert-Bytes $f
}
Check 'Running OpenMW or launcher is rejected without stopping it' {
    $source = 'public static class InstallerFixtureWait { public static void Main() { System.Threading.Thread.Sleep(120000); } }'
    $stub = Join-Path $sessionRoot 'AuthoredProcessStub.exe'
    Add-Type -TypeDefinition $source -Language CSharp -OutputAssembly $stub -OutputType ConsoleApplication
    foreach ($processName in @('openmw', 'openmw-launcher')) {
        $f = New-Fixture ('process-' + $processName)
        $exe = Join-Path $f.root ($processName + '.exe')
        [IO.File]::Copy($stub, $exe, $false)
        $stubProcess = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden
        try {
            Assert-Exit (Invoke-Installer $f) 2
            Assert-That (-not $stubProcess.HasExited) 'Installer stopped the authored test process.'
            Assert-Bytes $f
        }
        finally {
            if (-not $stubProcess.HasExited) { $stubProcess.Kill(); $stubProcess.WaitForExit() }
            $stubProcess.Dispose()
        }
    }
}
Check 'Install-All forwards literal arguments, visits every component and aggregates status' {
    $f = New-Fixture 'all-wrapper'
    $wrapper = Join-Path $f.root 'Install-All.ps1'
    [IO.File]::Copy($WrapperScript, $wrapper, $false)
    $components = @('01_Proximity_Tool', '02_ZHI', '03_Attend_Me', '04_Gravity_Enforcement_Act')
    $stub = @'
param([string]$PackageRoot, [string]$LogPath, [string]$BackupRoot, [switch]$CheckOnly)
$utf8 = New-Object System.Text.UTF8Encoding($false)
$record = @{package = $PackageRoot; log = $LogPath; backup = $BackupRoot; check_only = [bool]$CheckOnly}
[IO.File]::WriteAllText((Join-Path $PackageRoot 'call.json'), ($record | ConvertTo-Json), $utf8)
exit [int]([IO.File]::ReadAllText((Join-Path $PackageRoot 'exit.txt')))
'@
    foreach ($component in $components) { Write-Text (Join-Path (Join-Path $f.root $component) 'Install-Fix.ps1') $stub }
    foreach ($case in @(@{codes = @(0,0,0,0); expected = 0}, @{codes = @(0,2,0,0); expected = 2}, @{codes = @(1,0,2,0); expected = 1})) {
        foreach ($index in 0..3) {
            $componentRoot = Join-Path $f.root $components[$index]
            Write-Text (Join-Path $componentRoot 'exit.txt') ([string]$case.codes[$index])
            $record = Join-Path $componentRoot 'call.json'
            if (Test-Path -LiteralPath $record) { Remove-Item -LiteralPath $record }
        }
        Assert-Exit (Invoke-Script $f $wrapper @('-LogPath', $f.log, '-BackupRoot', $f.backup, '-CheckOnly')) $case.expected
        foreach ($component in $components) {
            $componentRoot = Join-Path $f.root $component
            $call = Get-Content -LiteralPath (Join-Path $componentRoot 'call.json') -Raw -Encoding UTF8 | ConvertFrom-Json
            Assert-That ($call.package -eq $componentRoot -and $call.log -eq $f.log -and $call.backup -eq $f.backup -and $call.check_only) 'Wrapper arguments or continuation differ.'
        }
    }
}


Check 'Reviewed engine revision list accepts known revisions and rejects unknown ones' {
    $f = New-Fixture 'allowed-revisions'
    $f.manifest.allowed_revisions = @('0123456789', 'abcdef0123')
    Save-Manifest $f
    $f.lines[3] = '[12:00:00.002 I] Revision: abcdef0123'; Save-Log $f
    Assert-Exit (Invoke-Installer $f @('-CheckOnly')) 0
    $f.lines[3] = '[12:00:00.002 I] Revision: 9876543210'; Save-Log $f
    Assert-Exit (Invoke-Installer $f @('-CheckOnly')) 2
    Assert-Bytes $f
}
Check 'Read-only dependency preflight uses the winning provider and leaves dependencies untouched' {
    $f = New-Fixture 'read-only-winners'; Add-DefinitionContext $f
    $dep = $f.manifest.read_only_dependencies[1]
    Write-Text (Join-Path $f.providers[0] $dep.path) '-- Old nonwinning helper; final provider is authoritative'
    $before = Get-Sha (Join-Path $f.providers[0] $dep.path)
    Assert-Exit (Invoke-Installer $f @('-CheckOnly')) 0
    Assert-Exit (Invoke-Installer $f) 0
    Assert-Bytes $f 'installed'
    foreach ($dependency in $f.manifest.read_only_dependencies) {
        Assert-That ((Get-Sha (Join-Path $f.providers[1] $dependency.path)) -eq $dependency.sha256) 'Read-only winning dependency changed.'
    }
    Assert-That ((Get-Sha (Join-Path $f.providers[0] $dep.path)) -eq $before) 'Nonwinning dependency changed.'
    $receipt = Get-Content -LiteralPath (Get-InstalledReceipt $f).FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert-That (@($receipt.dependencies).Count -eq 3) 'Dependency evidence missing from receipt.'
}
Check 'Changed required helper blocks every target before mutation' {
    $f = New-Fixture 'changed-dependency'; Add-DefinitionContext $f
    $dep = $f.manifest.read_only_dependencies[0]
    Write-Text (Join-Path $f.providers[1] $dep.path) '-- Authored replacement loader with unknown semantics'
    Assert-Exit (Invoke-Installer $f) 2
    Assert-Bytes $f
    Assert-That (@(Get-Receipts $f).Count -eq 0) 'Dependency guard wrote an install receipt.'
}
Check 'Descriptor changes are read-only dependency mismatches' {
    $f = New-Fixture 'changed-descriptor'; Add-DefinitionContext $f
    Write-Text (Join-Path $f.providers[1] 'omw-achievements.omwscripts') '-- Authored changed attachment descriptor'
    Assert-Exit (Invoke-Installer $f) 2
    Assert-Bytes $f
}
Check 'Unknown loose addon definition in any active provider blocks installation' {
    foreach ($index in 0..1) {
        $f = New-Fixture ('loose-addon-' + $index); Add-DefinitionContext $f
        Write-Text (Join-Path $f.providers[$index] 'scripts/omw_achievements/achievements/nested/Unknown.LUA') '-- Authored unreviewed addon'
        Assert-Exit (Invoke-Installer $f) 2
        Assert-Bytes $f
    }
}
Check 'Hidden Lua addon definitions are included in the prefix inventory' {
    $f = New-Fixture 'hidden-addon'; Add-DefinitionContext $f
    $path = Join-Path $f.providers[1] 'scripts/omw_achievements/achievements/hidden.lua'
    Write-Text $path '-- Authored hidden addon'
    [IO.File]::SetAttributes($path, [IO.FileAttributes]::Hidden)
    Assert-Exit (Invoke-Installer $f) 2
    Assert-Bytes $f
}
Check 'Nested directory links cannot silently hide addon definitions' {
    $f = New-Fixture 'linked-addon'; Add-DefinitionContext $f
    $target = Join-Path $f.root 'External authored definitions'
    Write-Text (Join-Path $target 'unknown.lua') '-- Authored linked addon'
    $link = Join-Path $f.providers[1] 'scripts/omw_achievements/achievements/linked'
    [void](New-Item -ItemType Junction -Path $link -Target $target)
    Assert-Exit (Invoke-Installer $f) 2
    Assert-Bytes $f
}
Check 'Non-Lua files under definition prefix do not affect module inventory' {
    $f = New-Fixture 'non-lua-prefix'; Add-DefinitionContext $f
    Write-Text (Join-Path $f.providers[1] 'scripts/omw_achievements/achievements/readme.txt') 'Authored harmless documentation'
    Assert-Exit (Invoke-Installer $f @('-CheckOnly')) 0
    Assert-Bytes $f
}
Check 'BSA directory inventory accepts reviewed names shadowed by loose dependencies' {
    $f = New-Fixture 'bsa-known-definitions'; Add-DefinitionContext $f
    [void](Add-BsaFixture $f @('textures\author_fixture.dds', 'SCRIPTS\OMW_ACHIEVEMENTS\ACHIEVEMENTS\BASEACHIEVEMENTS.LUA'))
    Assert-Exit (Invoke-Installer $f) 0
    Assert-Bytes $f 'installed'
    $receipt = Get-Content -LiteralPath (Get-InstalledReceipt $f).FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    $archives = @($receipt.definition_inventory | Where-Object { $_.kind -eq 'archive' })
    Assert-That ($archives.Count -eq 1 -and $archives[0].entries -eq 2) 'BSA directory was not fully inventoried.'
}
Check 'Unknown BSA-supplied Lua addon cannot bypass the loose provider check' {
    $f = New-Fixture 'bsa-unknown-addon'; Add-DefinitionContext $f
    [void](Add-BsaFixture $f @('textures\author_fixture.dds', 'scripts\omw_achievements\achievements\unknown.lua'))
    Assert-Exit (Invoke-Installer $f) 2
    Assert-Bytes $f
}
Check 'Missing BSA from logged archive order blocks incomplete inventory' {
    $f = New-Fixture 'bsa-order'; Add-DefinitionContext $f
    [void](Add-BsaFixture $f @('textures\author_fixture.dds'))
    $f.lines = @($f.lines | Where-Object { $_ -notmatch 'Adding BSA archive' }); Save-Log $f
    Assert-Exit (Invoke-Installer $f) 2
    Assert-Bytes $f
}
Check 'Unsupported and malformed BSA directories fail closed' {
    foreach ($mode in @('format', 'truncated', 'offset', 'unterminated')) {
        $f = New-Fixture ('bsa-bad-' + $mode); Add-DefinitionContext $f
        $bsa = Add-BsaFixture $f @('scripts\omw_achievements\achievements\unknown.lua')
        $bytes = [IO.File]::ReadAllBytes($bsa)
        if ($mode -eq 'format') { $bytes[0] = 1 }
        elseif ($mode -eq 'truncated') { $bytes = [byte[]]@(0, 1, 0) }
        elseif ($mode -eq 'offset') { foreach ($i in 20..23) { $bytes[$i] = 255 } }
        else { $dirSize = [BitConverter]::ToUInt32($bytes, 4); $bytes[12 + $dirSize - 1] = 65 }
        [IO.File]::WriteAllBytes($bsa, $bytes)
        Assert-Exit (Invoke-Installer $f) 2
        Assert-Bytes $f
    }
}
Check 'Rollback remains available after helper, addon, config, and log context change' {
    $f = New-Fixture 'restore-context-changed'; Add-DefinitionContext $f
    Assert-Exit (Invoke-Installer $f) 0
    $receipt = Get-InstalledReceipt $f
    Write-Text (Join-Path $f.providers[1] $f.manifest.read_only_dependencies[0].path) '-- Authored later helper update'
    Write-Text (Join-Path $f.providers[1] 'scripts/omw_achievements/achievements/later_addon.lua') '-- Authored later addon'
    Write-Text $f.configs[1] '# Authored later config'
    Remove-Item -LiteralPath $f.log
    Assert-Exit (Invoke-Installer $f @('-RestoreBackup', $receipt.DirectoryName)) 0
    foreach ($file in $f.manifest.files) {
        Assert-That ((Get-Sha (Join-Path $f.providers[1] $file.path)) -eq $file.expected_sha256) 'Original target was not restored after context changed.'
    }
    Assert-That ((Get-Content -LiteralPath $receipt.FullName -Raw -Encoding UTF8 | ConvertFrom-Json).state -eq 'restored') 'Changed-context restoration was not recorded.'
}

function Assert-ContextSourceInFailureZip($Fixture, [string]$Relative, [string]$ExpectedHash, [string]$ExpectedKind) {
    $jsonFiles = @(Get-ChildItem -LiteralPath $Fixture.backup -Recurse -File -Filter 'Source-check.json')
    Assert-That ($jsonFiles.Count -eq 1) 'Expected one source-check metadata file.'
    $evidence = Get-Content -LiteralPath $jsonFiles[0].FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    $records = @($evidence.context_sources | Where-Object { $_.path -ieq $Relative -and $_.kind -eq $ExpectedKind })
    Assert-That ($records.Count -eq 1 -and $records[0].status -eq 'copied') 'The exact mismatching context source was not copied.'
    $record = $records[0]
    Assert-That ($record.sha256 -eq $ExpectedHash) 'Context-source hash differs from the offending bytes.'
    Assert-That ((Get-Sha (Join-Path $jsonFiles[0].DirectoryName $record.copy)) -eq $ExpectedHash) 'Context-source file bytes differ.'
    $zips = @(Get-ChildItem -LiteralPath $Fixture.backup -Recurse -File -Filter '*.zip')
    Assert-That ($zips.Count -eq 1) 'Expected one failure evidence archive.'
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($zips[0].FullName)
    try {
        $entries = @($zip.Entries | Where-Object { $_.FullName.Replace('\', '/').EndsWith('/' + $record.copy, [StringComparison]::Ordinal) })
        Assert-That ($entries.Count -eq 1) 'Failure ZIP omitted the captured source member.'
        $stream = $entries[0].Open(); $memory = New-Object IO.MemoryStream
        try {
            $stream.CopyTo($memory)
            $algorithm = [Security.Cryptography.SHA256]::Create()
            try { $hash = ([BitConverter]::ToString($algorithm.ComputeHash($memory.ToArray()))).Replace('-', '').ToLowerInvariant() }
            finally { $algorithm.Dispose() }
            Assert-That ($hash -eq $ExpectedHash) 'The exact failure ZIP source member differs.'
        }
        finally { $stream.Dispose(); $memory.Dispose() }
    }
    finally { $zip.Dispose() }
}

Check 'Mismatching helper bytes are present in the generated source-check ZIP' {
    $f = New-Fixture 'evidence-helper'; Add-DefinitionContext $f
    $dependency = $f.manifest.read_only_dependencies[0]
    $file = Join-Path $f.providers[1] $dependency.path
    Write-Text $file '-- Authored alternative definition loader that needs review'
    $hash = Get-Sha $file
    Assert-Exit (Invoke-Installer $f) 2
    Assert-Bytes $f
    Assert-ContextSourceInFailureZip $f $dependency.path $hash 'loose'
}
Check 'Offending loose addon bytes are present in the generated source-check ZIP' {
    $f = New-Fixture 'evidence-loose-addon'; Add-DefinitionContext $f
    $relative = 'scripts/omw_achievements/achievements/review_me.lua'
    $file = Join-Path $f.providers[1] $relative
    Write-Text $file '-- Authored addon initializer requiring review'
    $hash = Get-Sha $file
    Assert-Exit (Invoke-Installer $f) 2
    Assert-Bytes $f
    Assert-ContextSourceInFailureZip $f $relative $hash 'loose'
}
Check 'Offending BSA addon is read back as an exact bounded source member in failure ZIP' {
    $f = New-Fixture 'evidence-archive-addon'; Add-DefinitionContext $f
    $relative = 'scripts/omw_achievements/achievements/archived_addon.lua'
    [void](Add-BsaFixture $f @('textures/fixture.dds', $relative))
    Assert-Exit (Invoke-Installer $f) 2
    Assert-Bytes $f
    Assert-ContextSourceInFailureZip $f $relative (Get-TextSha ([string][char]0)) 'archive'
}
Check 'Oversized addon evidence is explicitly limited without installing anything' {
    $f = New-Fixture 'evidence-limit'; Add-DefinitionContext $f
    $relative = 'scripts/omw_achievements/achievements/large_authored_addon.lua'
    $path = Join-Path $f.providers[1] $relative
    [IO.File]::WriteAllBytes($path, (New-Object byte[] (2 * 1024 * 1024 + 1)))
    Assert-Exit (Invoke-Installer $f) 2
    Assert-Bytes $f
    $jsonFiles = @(Get-ChildItem -LiteralPath $f.backup -Recurse -File -Filter 'Source-check.json')
    $evidence = Get-Content -LiteralPath $jsonFiles[0].FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    $records = @($evidence.context_sources | Where-Object { $_.path -eq $relative })
    Assert-That ($records.Count -eq 1 -and $records[0].status -eq 'omitted_size_limit') 'Oversized addon was silently omitted or exceeded the evidence limit.'
    Assert-That ($null -eq $records[0].PSObject.Properties['copy']) 'Oversized source was copied despite the limit.'
}

$failures = @($checks | Where-Object { -not $_.passed })
$report = [ordered]@{
    powershell_version = $PSVersionTable.PSVersion.ToString()
    os = [Environment]::OSVersion.ToString()
    installer_sha256 = Get-Sha $InstallerScript
    wrapper_sha256 = Get-Sha $WrapperScript
    test_script_sha256 = Get-Sha $PSCommandPath
    fixture_root = $sessionRoot
    native_windows_integration = $true
    live_gameplay_executed = $false
    third_party_mod_sources_used = $false
    checks_passed = $checks.Count - $failures.Count
    checks_total = $checks.Count
    checks = $checks.ToArray()
}
Write-Json ([IO.Path]::GetFullPath($ReportPath)) $report
Write-Host ('Report: ' + $ReportPath)
if ($failures.Count -gt 0) { exit 1 }
exit 0
