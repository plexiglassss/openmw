#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Combined', 'Inherited', 'PreviousFrameUpdates')][string]$Variant = 'Combined',
    [string]$OpenMWExe,
    [string]$UserConfigDir,
    [string]$OutputRoot,
    [ValidateSet('Unknown', 'On', 'Off')][string]$DeclaredFG = 'Unknown',
    [string[]]$GameArguments = @(),
    [switch]$NoGpu,
    [switch]$NoPrompt
)

$ErrorActionPreference = 'Stop'
$collector = Join-Path $PSScriptRoot 'capture\Start-Capture.ps1'
if (-not (Test-Path -LiteralPath $collector -PathType Leaf)) {
    throw 'The capture script is missing. Extract the complete engine package first.'
}
if ([string]::IsNullOrWhiteSpace($OpenMWExe)) {
    $OpenMWExe = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\openmw.exe'))
}
$parameters = @{
    OpenMWExe = $OpenMWExe
    RunLabel = ('combined-' + $Variant.ToLowerInvariant())
    Mode = 'FrameOnly'
    DeclaredFG = $DeclaredFG
    GameArguments = $GameArguments
    NoGpu = $NoGpu
    NoPrompt = $NoPrompt
}
if (-not [string]::IsNullOrWhiteSpace($UserConfigDir)) { $parameters.UserConfigDir = $UserConfigDir }
if (-not [string]::IsNullOrWhiteSpace($OutputRoot)) { $parameters.OutputRoot = $OutputRoot }
$overrides = @{
    OPENMW_COMBINED_PERFORMANCE = '1'
    OPENMW_NATIVE_JOBS = $(if ($Variant -eq 'Inherited') { '0' } else { '1' })
    OPENMW_TOWN_FRAME_UPDATES = $(if ($Variant -eq 'Combined') { '1' } else { '0' })
    OPENMW_LUA_CELL_QUERY_CACHE = '1'
    OPENMW_LUA_UI_LEGACY_UPDATES = '0'
    OPENMW_LUA_UI_LEGACY_LAYOUT = '0'
}
$previous = @{}
foreach ($name in $overrides.Keys) {
    $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    foreach ($name in $overrides.Keys) {
        [Environment]::SetEnvironmentVariable($name, $overrides[$name], 'Process')
    }
    Write-Host ('Engine mode: ' + $Variant + '; native jobs ' + $(if ($Variant -eq 'Inherited') { 'disabled' } else { 'enabled' }) + '; FrameOnly recording')
    Write-Host 'Load your usual town save, let loading settle, then play your usual short route.'
    Write-Host 'Keep the four mods enabled for this combined-candidate check.'
    Write-Host 'Quit normally when finished; the capture ZIP is created automatically.'
    if ($Variant -eq 'Inherited') {
        Write-Host 'Inherited mode uses the same executable with native jobs and the new generic query APIs disabled.'
        Write-Host 'The cumulative Sane Magic queries remain available. The three new adapters use their prior paths.'
    }
    if ($Variant -eq 'PreviousFrameUpdates') {
        Write-Host 'Comparison mode keeps native jobs enabled and uses the previous actor and HUD update paths.'
    }
    & $collector @parameters
} finally {
    foreach ($name in $overrides.Keys) {
        [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process')
    }
}
