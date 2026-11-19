[CmdletBinding()]
param(
    [string]$SettingsPath = '',
    [string]$BackupRoot = '',
    [string]$RestoreBackup = '',
    [switch]$CheckOnly
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'SettingsTools.ps1')

try {
    if ($PSVersionTable.PSVersion -lt [Version]'5.1') { throw 'Windows PowerShell 5.1 or newer is required.' }
    if (@(Get-Process -Name openmw,openmw-launcher -ErrorAction SilentlyContinue).Count -gt 0) {
        throw 'Close OpenMW and its launcher, then run this again.'
    }
    if ($RestoreBackup -ne '' -and $CheckOnly) { throw 'Choose restoration or CheckOnly.' }
    if ($RestoreBackup -ne '') {
        $receipt = Get-Content -LiteralPath (Join-Path $RestoreBackup 'receipt.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($receipt.component -ne 'gameplay-settings-20261106') { throw 'This backup belongs to another change.' }
        $SettingsPath = $receipt.path
        [byte[]]$original = [IO.File]::ReadAllBytes((Join-Path $RestoreBackup 'settings.cfg'))
        if ((Get-BytesSha $original) -ne $receipt.before_sha256) { throw 'The backup has changed; restoration stopped.' }
        $currentSha = Get-BytesSha ([IO.File]::ReadAllBytes($SettingsPath))
        if ($currentSha -eq $receipt.before_sha256) { Write-Host 'Settings are already restored.'; exit 0 }
        if ($currentSha -ne $receipt.after_sha256) {
            throw 'Settings changed since this preset was applied. To keep those edits, set inactive script cache duration = 0 and compute scene bounds = primitives manually.'
        }
        Write-SettingsAtomically $SettingsPath $original $currentSha
        Write-Host ('RESTORED: ' + $SettingsPath)
        exit 0
    }
    if ($SettingsPath -eq '') { $SettingsPath = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games\OpenMW\settings.cfg' }
    $SettingsPath = [IO.Path]::GetFullPath($SettingsPath)
    if (-not (Test-Path -LiteralPath $SettingsPath -PathType Leaf)) { throw ('settings.cfg not found: ' + $SettingsPath) }
    [byte[]]$before = [IO.File]::ReadAllBytes($SettingsPath)
    [byte[]]$after = Convert-GameplaySettings $before
    $beforeSha = Get-BytesSha $before
    $afterSha = Get-BytesSha $after
    Write-Host '[Shadows] compute scene bounds = bounds'
    Write-Host '[Lua] inactive script cache duration = 30'
    Write-Host '[Lua] inactive script cache memory threshold = 2147483648'
    Write-Host 'The Lua cache settings take effect with the accompanying gameplay engine build, after restart.'
    if ($beforeSha -eq $afterSha) { Write-Host 'ALREADY APPLIED: these settings are unchanged.'; exit 0 }
    if ($CheckOnly) { Write-Host ('READY: ' + $SettingsPath); exit 0 }
    if ($BackupRoot -eq '') { $BackupRoot = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'OpenMW Responsiveness Backups\gameplay-settings-20261106' }
    $backup = Join-Path $BackupRoot ((Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,6))
    [void][IO.Directory]::CreateDirectory($backup)
    [IO.File]::WriteAllBytes((Join-Path $backup 'settings.cfg'), $before)
    $receipt = [ordered]@{
        component = 'gameplay-settings-20261106'; path = $SettingsPath;
        before_sha256 = $beforeSha; after_sha256 = $afterSha;
        prepared_utc = [DateTime]::UtcNow.ToString('o')
    }
    $utf8 = New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText((Join-Path $backup 'receipt.json'), ($receipt | ConvertTo-Json), $utf8)
    if ((Get-BytesSha ([IO.File]::ReadAllBytes((Join-Path $backup 'settings.cfg')))) -ne $beforeSha) { throw 'The settings backup failed verification.' }
    Write-SettingsAtomically $SettingsPath $after $beforeSha
    if ((Get-BytesSha ([IO.File]::ReadAllBytes($SettingsPath))) -ne $afterSha) { throw ('Settings readback differs. Original backup: ' + $backup) }
    Write-Host ('APPLIED: ' + $SettingsPath)
    Write-Host ('Backup: ' + $backup)
    Write-Host 'Restart the game to use the new settings.'
    exit 0
}
catch { Write-Error $_ -ErrorAction Continue; exit 1 }
