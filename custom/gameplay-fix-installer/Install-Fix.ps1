# OpenMW responsiveness helper installer. Windows PowerShell 5.1.
# Copies prepared files only. Does not patch source, edit configuration, or stop processes.
[CmdletBinding()]
param(
    [string]$PackageRoot = $PSScriptRoot,
    [string]$LogPath = '',
    [string]$BackupRoot = '',
    [string]$RestoreBackup = '',
    [switch]$RestoreLatest,
    [switch]$CheckOnly
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$utf8 = New-Object System.Text.UTF8Encoding($false)
$manifest = $null
$dataDirs = @()
$configPaths = @()
$receiptPath = $null

function Stop-Guard([string]$Message) { throw ('GUARD: ' + $Message) }
function Get-Sha([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
}
function Get-TextSha([string]$Value) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($utf8.GetBytes($Value)))).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}
function Write-Json([string]$Path, $Value) {
    $temporary = $Path + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
    try {
        [IO.File]::WriteAllText($temporary, ($Value | ConvertTo-Json -Depth 20), $utf8)
        if (Test-Path -LiteralPath $Path -PathType Leaf) { [IO.File]::Replace($temporary, $Path, $null) }
        else { [IO.File]::Move($temporary, $Path) }
    }
    finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue } }
}
function Join-Safe([string]$Root, [string]$Relative) {
    if ([string]::IsNullOrWhiteSpace($Relative) -or $Relative -notmatch '^scripts[/\\]' -or
        [IO.Path]::IsPathRooted($Relative) -or $Relative -match '(^|[/\\])\.\.([/\\]|$)' -or
        $Relative -match '[:*?"<>|]' -or $Relative -match '[\r\n]') {
        Stop-Guard "Invalid relative payload path: $Relative"
    }
    $base = [IO.Path]::GetFullPath($Root).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $path = [IO.Path]::GetFullPath((Join-Path $base $Relative))
    if (-not $path.StartsWith(($base + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase)) {
        Stop-Guard "Payload path escapes its root: $Relative"
    }
    return $path
}
function Find-Winner([string]$Relative) {
    for ($index = $dataDirs.Count - 1; $index -ge 0; $index--) {
        $path = Join-Safe $dataDirs[$index] $Relative
        if (Test-Path -LiteralPath $path -PathType Leaf) { return $path }
    }
    return $null
}
function Assert-GameClosed {
    $running = @(Get-Process -Name 'openmw', 'openmw-launcher' -ErrorAction SilentlyContinue)
    if ($running.Count -gt 0) { Stop-Guard 'Close OpenMW and its launcher, then run this installer again.' }
}
function Write-SourceEvidence([string]$Reason) {
    if ($null -eq $manifest -or $manifest.id -notmatch '^[a-z0-9_-]+$') { return }
    $stamp = (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + ([Guid]::NewGuid().ToString('N').Substring(0, 6))
    $folder = Join-Path (Join-Path $BackupRoot $manifest.id) ('Source-check-' + $stamp)
    [void][IO.Directory]::CreateDirectory($folder)
    $evidence = [ordered]@{
        component = $manifest.id; reason = $Reason; time_utc = [DateTime]::UtcNow.ToString('o')
        note = 'Providers below are resolved from the supplied log. If configuration differs, they are not asserted to be the next launch winners.'
        log_path = $LogPath; data_directories = @($dataDirs); config = @(); files = @()
    }
    foreach ($config in $configPaths) {
        if (Test-Path -LiteralPath $config -PathType Leaf) {
            $destination = Join-Path $folder ('config-' + $evidence.config.Count + '.cfg')
            [IO.File]::Copy($config, $destination, $false)
            $evidence.config += @{ source = $config; sha256 = Get-Sha $destination; copy = [IO.Path]::GetFileName($destination) }
        }
    }
    foreach ($file in $manifest.files) {
        $source = Find-Winner $file.path
        $record = [ordered]@{ path = $file.path; expected_sha256 = $file.expected_sha256; installed_sha256 = $file.installed_sha256; source = $source }
        if ($null -ne $source) {
            $destination = Join-Safe (Join-Path $folder 'current-files') $file.path
            [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
            [IO.File]::Copy($source, $destination, $false)
            $record.sha256 = Get-Sha $destination
        }
        $evidence.files += $record
    }
    Write-Json (Join-Path $folder 'Source-check.json') $evidence
    $zip = $folder + '.zip'
    Compress-Archive -LiteralPath $folder -DestinationPath $zip -CompressionLevel Optimal
    Write-Host ('Source-check ZIP (send this back if blocked): ' + $zip)
}
function Invoke-Transaction($Items, $Receipt) {
    $staged = New-Object System.Collections.Generic.List[object]
    $applied = New-Object System.Collections.Generic.List[object]
    try {
        # Stage every file before replacing any target. Temporary files stay on the target volume.
        foreach ($item in $Items) {
            if ((Get-Sha $item.target) -ne $item.before) { Stop-Guard ('File changed during preflight: ' + $item.target) }
            $temporary = $item.target + '.omw-' + [Guid]::NewGuid().ToString('N') + '.tmp'
            $entry = @{ item = $item; temporary = $temporary }
            $staged.Add($entry)
            [IO.File]::Copy($item.source, $temporary, $false)
            if ((Get-Sha $temporary) -ne $item.after) { Stop-Guard ('Staged file failed verification: ' + $item.target) }
        }
        Assert-GameClosed
        foreach ($entry in $staged) {
            $item = $entry.item
            if ((Get-Sha $item.target) -ne $item.before) { Stop-Guard ('File changed before replacement: ' + $item.target) }
            [IO.File]::Replace($entry.temporary, $item.target, $null)
            $applied.Add($item)
            if ((Get-Sha $item.target) -ne $item.after) { throw ('Installed file failed verification: ' + $item.target) }
        }
        $Receipt.state = 'installed'
        if ($Receipt.operation -eq 'restore') { $Receipt.state = 'restored' }
        # Receipt finalization is part of the protected transaction: a failed write rolls targets back.
        Write-Json $receiptPath $Receipt
    }
    catch {
        $originalError = $_.Exception.Message
        $failures = @()
        for ($index = $applied.Count - 1; $index -ge 0; $index--) {
            $item = $applied[$index]
            try {
                # Do not overwrite an unrelated concurrent edit during recovery.
                $current = Get-Sha $item.target
                if ($current -eq $item.before) { continue }
                if ($current -ne $item.after) { throw 'Target changed after replacement; original backup retained.' }
                $recovery = $item.target + '.omw-' + [Guid]::NewGuid().ToString('N') + '.tmp'
                try {
                    [IO.File]::Copy($item.rollback, $recovery, $false)
                    if ((Get-Sha $recovery) -ne $item.before) { throw 'Rollback copy failed hash verification.' }
                    [IO.File]::Replace($recovery, $item.target, $null)
                    if ((Get-Sha $item.target) -ne $item.before) { throw 'Rollback target failed hash verification.' }
                }
                finally { if (Test-Path -LiteralPath $recovery) { Remove-Item -LiteralPath $recovery -Force -ErrorAction SilentlyContinue } }
            }
            catch { $failures += ($item.target + ': ' + $_.Exception.Message) }
        }
        $Receipt.state = 'failed_rolled_back'
        $Receipt.last_attempt = 'failed_rolled_back'
        # A failed restore can be retried after its transaction returned to the installed bytes.
        if ($Receipt.operation -eq 'restore') { $Receipt.state = 'installed' }
        if ($failures.Count -gt 0) { $Receipt.state = 'rollback_incomplete' }
        $Receipt.error = $originalError
        $Receipt.rollback_errors = $failures
        Write-Json $receiptPath $Receipt
        if ($failures.Count -gt 0) { throw ($originalError + ' Recovery needs attention; see ' + $receiptPath) }
        throw $originalError
    }
    finally {
        foreach ($entry in $staged) {
            if (Test-Path -LiteralPath $entry.temporary) { Remove-Item -LiteralPath $entry.temporary -Force -ErrorAction SilentlyContinue }
        }
    }
}

try {
    if ($PSVersionTable.PSVersion -lt [Version]'5.1') { Stop-Guard 'Windows PowerShell 5.1 or newer is required.' }
    if ($env:OS -ne 'Windows_NT') { Stop-Guard 'Use this installer on the Windows OpenMW computer.' }
    if ([string]::IsNullOrEmpty($BackupRoot)) { $BackupRoot = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'OpenMW Responsiveness Backups' }
    $PackageRoot = [IO.Path]::GetFullPath($PackageRoot)
    $BackupRoot = [IO.Path]::GetFullPath($BackupRoot)
    $manifest = Get-Content -LiteralPath (Join-Path $PackageRoot 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($manifest.id -notmatch '^[a-z0-9_-]+$') { Stop-Guard 'Invalid component identity.' }
    if (@($manifest.files).Count -eq 0) { Stop-Guard 'The package contains no files.' }
    $paths = @{}
    foreach ($file in $manifest.files) {
        [void](Join-Safe (Join-Path $PackageRoot 'Data Files') $file.path)
        if ($file.expected_sha256 -notmatch '^[a-f0-9]{64}$' -or $file.installed_sha256 -notmatch '^[a-f0-9]{64}$') { Stop-Guard 'Invalid file hash in the package.' }
        $key = $file.path.Replace('\', '/').ToLowerInvariant()
        if ($paths.ContainsKey($key)) { Stop-Guard 'The package repeats a target file.' }
        $paths[$key] = $true
    }
    Assert-GameClosed
    Write-Host ('Component: ' + $manifest.name)

    if ($RestoreLatest -or -not [string]::IsNullOrEmpty($RestoreBackup)) {
        if ($CheckOnly) { Stop-Guard 'CheckOnly and restoration are separate modes.' }
        if ($RestoreLatest -and -not [string]::IsNullOrEmpty($RestoreBackup)) { Stop-Guard 'Choose RestoreLatest or RestoreBackup.' }
        if ($RestoreLatest) {
            $componentRoot = Join-Path $BackupRoot $manifest.id
            $candidates = @()
            if (Test-Path -LiteralPath $componentRoot -PathType Container) {
                foreach ($directory in Get-ChildItem -LiteralPath $componentRoot -Directory) {
                    $path = Join-Path $directory.FullName 'receipt.json'
                    if (Test-Path -LiteralPath $path -PathType Leaf) {
                        $candidate = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
                        if ($candidate.component -eq $manifest.id -and $candidate.state -eq 'installed') {
                            $candidates += $directory
                        }
                    }
                }
            }
            if ($candidates.Count -eq 0) { Stop-Guard 'No installed backup for this component was found.' }
            $RestoreBackup = ($candidates | Sort-Object Name -Descending | Select-Object -First 1).FullName
        }
        $RestoreBackup = [IO.Path]::GetFullPath($RestoreBackup)
        $receiptPath = Join-Path $RestoreBackup 'receipt.json'
        $saved = Get-Content -LiteralPath $receiptPath -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($saved.component -ne $manifest.id -or $saved.state -ne 'installed' -or @($saved.files).Count -ne @($manifest.files).Count) { Stop-Guard 'This is not an installed backup for this component.' }
        $items = @()
        foreach ($file in $manifest.files) {
            $entries = @($saved.files | Where-Object { $_.path -eq $file.path })
            if ($entries.Count -ne 1) { Stop-Guard 'Backup file list does not match the package.' }
            $entry = $entries[0]
            $original = Join-Safe (Join-Path $RestoreBackup 'originals') $file.path
            if ($entry.original_sha256 -ne $file.expected_sha256 -or $entry.installed_sha256 -ne $file.installed_sha256 -or
                (Get-Sha $original) -ne $file.expected_sha256 -or (Get-Sha $entry.target) -ne $file.installed_sha256) {
                Stop-Guard ('Restoration would overwrite a later change, or the backup differs: ' + $file.path)
            }
            $rollback = Join-Safe (Join-Path $RestoreBackup 'restore-before') $file.path
            $items += @{ path = $file.path; target = $entry.target; source = $original; rollback = $rollback; before = $file.installed_sha256; after = $file.expected_sha256 }
        }
        foreach ($item in $items) {
            [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($item.rollback))
            [IO.File]::Copy($item.target, $item.rollback, $true)
            if ((Get-Sha $item.rollback) -ne $item.before) { Stop-Guard 'The current file changed during restoration preflight.' }
        }
        $receipt = @{ component = $manifest.id; operation = 'restore'; state = 'restoring'; files = $saved.files; restored_utc = [DateTime]::UtcNow.ToString('o'); error = $null; rollback_errors = @() }
        Invoke-Transaction $items $receipt
        Write-Host ('RESTORED: ' + $manifest.name)
        Write-Host ('Receipt: ' + $receiptPath)
        exit 0
    }

    if ([string]::IsNullOrEmpty($LogPath)) { $LogPath = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games\OpenMW\openmw.log' }
    $LogPath = [IO.Path]::GetFullPath($LogPath)
    if (-not (Test-Path -LiteralPath $LogPath -PathType Leaf)) { Stop-Guard ('OpenMW log not found: ' + $LogPath) }
    $lines = @(Get-Content -LiteralPath $LogPath -Encoding UTF8)
    $revisions = @()
    foreach ($line in $lines) {
        if ($line -match '^\[[^\]]+\]\s+Adding data directory (.+)$') { $dataDirs += $Matches[1].Trim() }
        if ($line -match '^\[[^\]]+\]\s+Loading config file:\s*(.+)$') { $configPaths += $Matches[1].Trim() }
        if ($line -match '^\[[^\]]+\]\s+Revision:\s*(\S+)') { $revisions += $Matches[1] }
    }
    if ($dataDirs.Count -eq 0 -or $configPaths.Count -eq 0 -or $revisions.Count -ne 1) { Stop-Guard 'The log does not contain one complete supported startup.' }
    if ($revisions[0] -ne $manifest.expected_revision) { Stop-Guard 'The logged engine revision differs from the reviewed capture.' }
    if ((Get-TextSha ($dataDirs -join "`n")) -ne $manifest.expected_data_order_sha256) { Stop-Guard 'The logged data-directory order differs from the reviewed capture.' }
    $configHashes = @()
    foreach ($config in $configPaths) { $configHashes += Get-Sha $config }
    if ((($configHashes | Sort-Object) -join ',') -ne ((@($manifest.expected_config_sha256) | Sort-Object) -join ',')) { Stop-Guard 'The current openmw.cfg files differ from the reviewed capture. Nothing was installed.' }

    $plans = @()
    foreach ($file in $manifest.files) {
        $payload = Join-Safe (Join-Path $PackageRoot 'Data Files') $file.path
        if (-not (Test-Path -LiteralPath $payload -PathType Leaf) -or (Get-Sha $payload) -ne $file.installed_sha256) { Stop-Guard ('Package payload differs: ' + $file.path) }
        $target = Find-Winner $file.path
        if ($null -eq $target) { Stop-Guard ('No loose-file provider was found for ' + $file.path) }
        $current = Get-Sha $target
        if ($current -ne $file.expected_sha256 -and $current -ne $file.installed_sha256) { Stop-Guard ('Current mod file differs from the reviewed source: ' + $target) }
        $plans += @{ path = $file.path; target = $target; payload = $payload; current = $current; expected = $file.expected_sha256; installed = $file.installed_sha256 }
        Write-Host ('  ' + $target)
    }
    $already = @($plans | Where-Object { $_.current -eq $_.installed })
    if ($already.Count -eq $plans.Count) { Write-Host 'ALREADY INSTALLED: every target has the delivered bytes.'; exit 0 }
    if ($already.Count -gt 0) { Stop-Guard 'This multi-file component is only partly installed. Restore it before applying the complete pair.' }
    if ($CheckOnly) { Write-Host 'CHECK PASSED: sources, payloads, logged engine revision, configuration and logged order match.'; exit 0 }

    $stamp = (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + ([Guid]::NewGuid().ToString('N').Substring(0, 6))
    $backup = Join-Path (Join-Path $BackupRoot $manifest.id) $stamp
    [void][IO.Directory]::CreateDirectory($backup)
    $receiptPath = Join-Path $backup 'receipt.json'
    $receipt = @{ component = $manifest.id; name = $manifest.name; operation = 'install'; state = 'prepared'; time_utc = [DateTime]::UtcNow.ToString('o'); log_path = $LogPath; log_sha256 = Get-Sha $LogPath; manifest_sha256 = Get-Sha (Join-Path $PackageRoot 'manifest.json'); files = @(); error = $null; rollback_errors = @() }
    $items = @()
    foreach ($plan in $plans) {
        $original = Join-Safe (Join-Path $backup 'originals') $plan.path
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($original))
        [IO.File]::Copy($plan.target, $original, $false)
        if ((Get-Sha $original) -ne $plan.expected) { Stop-Guard ('File changed while making its backup: ' + $plan.target) }
        $receipt.files += @{ path = $plan.path; target = $plan.target; original_sha256 = $plan.expected; installed_sha256 = $plan.installed }
        $items += @{ path = $plan.path; target = $plan.target; source = $plan.payload; rollback = $original; before = $plan.expected; after = $plan.installed }
    }
    Write-Json $receiptPath $receipt
    Invoke-Transaction $items $receipt
    Write-Host ('INSTALLED: ' + $manifest.name)
    Write-Host ('Backup and receipt: ' + $backup)
    Write-Host 'Use RESTORE.cmd in this component folder to undo it.'
    exit 0
}
catch {
    $message = $_.Exception.Message
    Write-Host ('STOPPED: ' + $message) -ForegroundColor Yellow
    if ($null -ne $receiptPath) { Write-Host ('Receipt / recovery location: ' + $receiptPath) }
    if (-not $RestoreLatest -and [string]::IsNullOrEmpty($RestoreBackup) -and $null -ne $manifest -and -not [string]::IsNullOrEmpty($BackupRoot)) {
        try { Write-SourceEvidence $message }
        catch { Write-Host ('Source-check ZIP could not be made: ' + $_.Exception.Message) }
    }
    if ($message.StartsWith('GUARD: ')) { exit 2 }
    exit 1
}
