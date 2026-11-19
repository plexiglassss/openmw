#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Optimized', 'LegacyLayout')][string]$Variant = 'Optimized',
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
    throw 'The comparison capture script is missing. Extract the complete engine package first.'
}
if ([string]::IsNullOrWhiteSpace($OpenMWExe)) {
    $OpenMWExe = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\openmw.exe'))
}
$parameters = @{
    OpenMWExe = $OpenMWExe
    RunLabel = ('town-' + $Variant.ToLowerInvariant())
    Mode = 'FrameOnly'
    DeclaredFG = $DeclaredFG
    GameArguments = $GameArguments
    NoGpu = $NoGpu
    NoPrompt = $NoPrompt
}
if (-not [string]::IsNullOrWhiteSpace($UserConfigDir)) { $parameters.UserConfigDir = $UserConfigDir }
if (-not [string]::IsNullOrWhiteSpace($OutputRoot)) { $parameters.OutputRoot = $OutputRoot }
$previous = [Environment]::GetEnvironmentVariable('OPENMW_LUA_UI_LEGACY_LAYOUT', 'Process')
try {
    $env:OPENMW_LUA_UI_LEGACY_LAYOUT = if ($Variant -eq 'LegacyLayout') { '1' } else { '0' }
    Write-Host ('Comparison: ' + $Variant + '; FrameOnly recording')
    Write-Host 'Use the same save, location, camera direction and settings for both runs.'
    Write-Host 'After loading settles, stand still for one minute, then repeat the same short route.'
    Write-Host 'Quit normally. Repeat the other variant, reversing the order for a second pair if possible.'
    Write-Host 'LegacyLayout changes only the new UI property-read optimization.'
    & $collector @parameters
} finally {
    [Environment]::SetEnvironmentVariable('OPENMW_LUA_UI_LEGACY_LAYOUT', $previous, 'Process')
}
