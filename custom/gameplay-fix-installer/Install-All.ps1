[CmdletBinding()]
param([string]$LogPath = '', [string]$BackupRoot = '', [switch]$CheckOnly)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$components = @('01_Proximity_Tool', '02_ZHI', '03_Attend_Me', '04_Gravity_Enforcement_Act')
$results = @()
$exitCode = 0
foreach ($component in $components) {
    $root = Join-Path $PSScriptRoot $component
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $root 'Install-Fix.ps1'), '-PackageRoot', $root)
    if ($LogPath) { $arguments += @('-LogPath', $LogPath) }
    if ($BackupRoot) { $arguments += @('-BackupRoot', $BackupRoot) }
    if ($CheckOnly) { $arguments += '-CheckOnly' }
    & (Join-Path $PSHOME 'powershell.exe') @arguments
    $code = $LASTEXITCODE
    $status = 'Installed / already installed'
    if ($CheckOnly) { $status = 'Check passed / already installed' }
    if ($code -eq 2) { $status = 'Skipped: source or environment check did not match'; if ($exitCode -eq 0) { $exitCode = 2 } }
    elseif ($code -ne 0) { $status = 'Stopped: inspect the message and backup receipt'; $exitCode = 1 }
    $results += [pscustomobject]@{ Component = $component; Result = $status; ExitCode = $code }
}
Write-Host ''
Write-Host 'RESULTS'
$results | Format-Table -AutoSize -Wrap | Out-Host
Write-Host 'Only rows reporting success were applied. Keep the backup receipts.'
Write-Host 'If any component was skipped, send its Source-check ZIP back for review.'
exit $exitCode
