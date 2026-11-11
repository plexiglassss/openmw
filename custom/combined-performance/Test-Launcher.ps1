#requires -Version 5.1
[CmdletBinding()]
param([string]$ReportPath = 'combined-launcher-tests.json')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$utf8 = New-Object System.Text.UTF8Encoding($false)
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('OpenMW-combined-launcher-' + [Guid]::NewGuid().ToString('N'))
$tool = Join-Path $fixture 'tools\combined-performance'
$capture = Join-Path $tool 'capture'
$names = @('OPENMW_COMBINED_PERFORMANCE', 'OPENMW_NATIVE_JOBS', 'OPENMW_TOWN_FRAME_UPDATES', 'OPENMW_SPARSE_UI_UPDATES', 'OPENMW_SHARED_ACTIVE_SPELL_READS', 'OPENMW_LUA_UI_LEGACY_UPDATES', 'OPENMW_LUA_UI_LEGACY_LAYOUT', 'OPENMW_LUA_CELL_QUERY_CACHE')
$original = @{}
foreach ($name in $names) { $original[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
$cases = @()
function Assert-Equal($Expected, $Actual, [string]$Message) {
    if ($Expected -cne $Actual) { throw ('ASSERT: ' + $Message + '; expected [' + $Expected + '], got [' + $Actual + ']') }
}
try {
    New-Item -ItemType Directory -Path $capture -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Start-Combined.ps1') -Destination $tool
    $stub = @'
[CmdletBinding()]
param([string]$OpenMWExe, [string]$RunLabel, [string]$Mode, [string]$DeclaredFG,
      [string[]]$GameArguments, [switch]$NoGpu, [switch]$NoPrompt,
      [string]$UserConfigDir, [string]$OutputRoot)
$row = @{
    executable = $OpenMWExe; label = $RunLabel; mode = $Mode; fg = $DeclaredFG;
    arguments = @($GameArguments); no_gpu = [bool]$NoGpu; no_prompt = [bool]$NoPrompt;
    config = $UserConfigDir; output = $OutputRoot;
    combined = $env:OPENMW_COMBINED_PERFORMANCE; native_jobs = $env:OPENMW_NATIVE_JOBS; town_updates = $env:OPENMW_TOWN_FRAME_UPDATES;
    spell_reads = $env:OPENMW_SHARED_ACTIVE_SPELL_READS; sparse = $env:OPENMW_SPARSE_UI_UPDATES; updates = $env:OPENMW_LUA_UI_LEGACY_UPDATES;
    layout = $env:OPENMW_LUA_UI_LEGACY_LAYOUT; cell = $env:OPENMW_LUA_CELL_QUERY_CACHE
}
$row | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'observed.json') -Encoding UTF8
'@
    [IO.File]::WriteAllText((Join-Path $capture 'Start-Capture.ps1'), $stub, $utf8)
    foreach ($variant in @('Combined', 'Inherited', 'PreviousFrameUpdates', 'PreviousHud', 'PreviousSpellReads')) {
        foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, 'previous-value', 'Process') }
        & (Join-Path $tool 'Start-Combined.ps1') -Variant $variant -UserConfigDir 'fixture-config' -OutputRoot 'fixture-output' -DeclaredFG Off -GameArguments @('--start', 'fixture-cell') -NoGpu -NoPrompt
        $row = Get-Content -LiteralPath (Join-Path $capture 'observed.json') -Raw | ConvertFrom-Json
        Assert-Equal ([IO.Path]::GetFullPath((Join-Path $fixture 'openmw.exe'))) $row.executable 'packaged executable resolution'
        Assert-Equal ('combined-' + $variant.ToLowerInvariant()) $row.label 'run label'
        Assert-Equal 'FrameOnly' $row.mode 'recording mode'
        Assert-Equal 'Off' $row.fg 'frame-generation declaration'
        Assert-Equal '--start,fixture-cell' ($row.arguments -join ',') 'game arguments'
        Assert-Equal 'fixture-config' $row.config 'config forwarding'
        Assert-Equal 'fixture-output' $row.output 'capture destination'
        Assert-Equal $true $row.no_gpu 'GPU flag'
        Assert-Equal $true $row.no_prompt 'prompt flag'
        Assert-Equal '1' $row.combined 'preserved cumulative performance mode'
        Assert-Equal $(if ($variant -eq 'Inherited') { '0' } else { '1' }) $row.native_jobs 'native job mode'
        Assert-Equal $(if ($variant -in @('Combined','PreviousHud','PreviousSpellReads')) { '1' } else { '0' }) $row.town_updates 'town frame updates'
        Assert-Equal $(if ($variant -in @('PreviousSpellReads','Inherited')) { '0' } else { '1' }) $row.spell_reads 'shared spell reads'
        Assert-Equal $(if ($variant -in @('Combined','PreviousSpellReads')) { '1' } else { '0' }) $row.sparse 'sparse HUD updates'
        Assert-Equal '0' $row.updates 'native UI fallback reset'
        Assert-Equal '0' $row.layout 'prior property-reader mode'
        Assert-Equal '1' $row.cell 'cell cache mode'
        foreach ($name in $names) {
            Assert-Equal 'previous-value' ([Environment]::GetEnvironmentVariable($name, 'Process')) ('restored ' + $name)
        }
        $cases += @{ name = $variant; result = 'PASS'; environment_restored = $true; observation = $row }
    }
    [IO.File]::WriteAllText((Join-Path $capture 'Start-Capture.ps1'), "throw 'fixture collector failure'", $utf8)
    $failed = $false
    try { & (Join-Path $tool 'Start-Combined.ps1') -Variant Combined }
    catch { $failed = $_.Exception.Message -eq 'fixture collector failure' }
    Assert-Equal $true $failed 'collector error propagation'
    foreach ($name in $names) {
        Assert-Equal 'previous-value' ([Environment]::GetEnvironmentVariable($name, 'Process')) ('failure restores ' + $name)
    }
    $cases += @{ name = 'CollectorFailure'; result = 'PASS'; environment_restored = $true }
    $report = @{ result = 'PASS'; powershell_version = $PSVersionTable.PSVersion.ToString(); cases = $cases; scope = 'Prepared launcher with controlled capture service; no gameplay execution' }
    [IO.File]::WriteAllText([IO.Path]::GetFullPath($ReportPath), ($report | ConvertTo-Json -Depth 8), $utf8)
    Write-Host 'PASS: combined/inherited modes, arguments, recording labels and failure restoration.'
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $original[$name], 'Process') }
    if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture -Force -Recurse }
}
