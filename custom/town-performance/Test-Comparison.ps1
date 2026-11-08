#requires -Version 5.1
# Executes the real comparison wrapper against a recording stub. No game is launched.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ComparisonScript,
    [Parameter(Mandatory = $true)][string]$TestRoot,
    [Parameter(Mandatory = $true)][string]$ReportPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ComparisonScript = [IO.Path]::GetFullPath($ComparisonScript)
$ReportPath = [IO.Path]::GetFullPath($ReportPath)
$utf8 = New-Object Text.UTF8Encoding($false)
$checks = New-Object 'System.Collections.Generic.List[object]'
$sessionRoot = Join-Path ([IO.Path]::GetFullPath($TestRoot)) ('comparison ' + [Guid]::NewGuid().ToString('N') + ' [literal] ' + [char]0x00e9)
$toolRoot = Join-Path $sessionRoot 'tools\town-performance'
$collectorRoot = Join-Path $toolRoot 'capture'
$copiedScript = Join-Path $toolRoot 'Start-Comparison.ps1'
$collectorScript = Join-Path $collectorRoot 'Start-Capture.ps1'
$environmentNames = @('OPENMW_LUA_UI_LEGACY_LAYOUT', 'OPENMW_COMPARISON_FIXTURE_RESULT', 'OPENMW_COMPARISON_FIXTURE_THROW')
$environmentBefore = @{}
foreach ($name in $environmentNames) {
    $environmentBefore[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

function Assert-True([bool]$Value, [string]$Message) {
    if (-not $Value) { throw $Message }
}

function Check([string]$Name, [scriptblock]$Body) {
    try {
        & $Body
        $checks.Add([ordered]@{ name = $Name; passed = $true })
        Write-Host ('PASS: ' + $Name)
    } catch {
        $checks.Add([ordered]@{ name = $Name; passed = $false; error = $_.Exception.Message; location = $_.ScriptStackTrace })
        Write-Host ('FAIL: ' + $Name + ': ' + $_.Exception.Message)
    }
}

function Invoke-Fixture([string]$Name, $InitialEnvironment, [hashtable]$Parameters, [bool]$ShouldThrow) {
    $resultPath = Join-Path $sessionRoot ($Name + '.json')
    [Environment]::SetEnvironmentVariable('OPENMW_LUA_UI_LEGACY_LAYOUT', $InitialEnvironment, 'Process')
    [Environment]::SetEnvironmentVariable('OPENMW_COMPARISON_FIXTURE_RESULT', $resultPath, 'Process')
    [Environment]::SetEnvironmentVariable('OPENMW_COMPARISON_FIXTURE_THROW', $(if ($ShouldThrow) { '1' } else { '0' }), 'Process')

    $caught = $null
    try { & $copiedScript @Parameters | Out-Null } catch { $caught = $_ }
    $restored = [Environment]::GetEnvironmentVariable('OPENMW_LUA_UI_LEGACY_LAYOUT', 'Process')
    Assert-True ([object]::Equals($InitialEnvironment, $restored)) 'The process layout flag was not restored exactly.'
    if ($ShouldThrow) {
        Assert-True ($null -ne $caught) 'The collector failure did not propagate.'
        Assert-True ($caught.Exception.Message -ceq 'comparison-fixture-failure') 'The wrapper propagated an unexpected exception.'
    } else {
        Assert-True ($null -eq $caught) ('The successful collector threw: ' + [string]$caught)
    }
    Assert-True ([IO.File]::Exists($resultPath)) 'The capture stub was not invoked.'
    $actual = [IO.File]::ReadAllText($resultPath, [Text.Encoding]::UTF8) | ConvertFrom-Json
    $expectedVariant = if ($Parameters.ContainsKey('Variant')) { $Parameters.Variant } else { 'Optimized' }
    $expectedFlag = if ($expectedVariant -eq 'LegacyLayout') { '1' } else { '0' }
    Assert-True ($actual.mode -ceq 'FrameOnly') 'The wrapper did not enforce FrameOnly capture.'
    Assert-True ($actual.legacyLayout -ceq $expectedFlag) 'The collector observed the wrong layout variant.'
    Assert-True ($actual.runLabel -ceq ('town-' + $expectedVariant.ToLowerInvariant())) 'The variant run label changed.'

    $expectedExe = if ($Parameters.ContainsKey('OpenMWExe') -and -not [string]::IsNullOrWhiteSpace($Parameters.OpenMWExe)) {
        $Parameters.OpenMWExe
    } else {
        [IO.Path]::GetFullPath((Join-Path $toolRoot '..\..\openmw.exe'))
    }
    Assert-True ($actual.openMWExe -ceq $expectedExe) 'The executable path was not preserved or correctly defaulted.'
    foreach ($option in @('UserConfigDir', 'OutputRoot')) {
        $expectedBound = $Parameters.ContainsKey($option) -and -not [string]::IsNullOrWhiteSpace($Parameters[$option])
        Assert-True ([bool]$actual.('has' + $option) -eq $expectedBound) ($option + ' binding did not preserve optional-argument semantics.')
        if ($expectedBound) {
            Assert-True ($actual.$option -ceq $Parameters[$option]) ($option + ' changed during forwarding.')
        }
    }
    $expectedFG = if ($Parameters.ContainsKey('DeclaredFG')) { $Parameters.DeclaredFG } else { 'Unknown' }
    Assert-True ($actual.declaredFG -ceq $expectedFG) 'DeclaredFG changed during forwarding.'
    foreach ($option in @('NoGpu', 'NoPrompt')) {
        $expectedSwitch = $Parameters.ContainsKey($option) -and [bool]$Parameters[$option]
        Assert-True ([bool]$actual.$option -eq $expectedSwitch) ($option + ' changed during forwarding.')
    }
    $expectedArguments = @()
    if ($Parameters.ContainsKey('GameArguments')) { $expectedArguments = @($Parameters.GameArguments) }
    $actualArguments = @($actual.gameArguments)
    Assert-True ($actualArguments.Count -eq $expectedArguments.Count) 'The number of forwarded game arguments changed.'
    for ($i = 0; $i -lt $expectedArguments.Count; ++$i) {
        Assert-True ($actualArguments[$i] -ceq $expectedArguments[$i]) ('Game argument ' + $i + ' changed during forwarding.')
    }
}

try {
    [void][IO.Directory]::CreateDirectory($collectorRoot)
    [IO.File]::Copy($ComparisonScript, $copiedScript)
    $stub = @'
[CmdletBinding()]
param(
    [string]$OpenMWExe,
    [string]$RunLabel,
    [string]$Mode,
    [string]$UserConfigDir,
    [string]$OutputRoot,
    [string]$DeclaredFG,
    [string[]]$GameArguments = @(),
    [switch]$NoGpu,
    [switch]$NoPrompt
)
$record = [ordered]@{
    openMWExe = $OpenMWExe
    runLabel = $RunLabel
    mode = $Mode
    userConfigDir = $UserConfigDir
    outputRoot = $OutputRoot
    hasUserConfigDir = $PSBoundParameters.ContainsKey('UserConfigDir')
    hasOutputRoot = $PSBoundParameters.ContainsKey('OutputRoot')
    declaredFG = $DeclaredFG
    gameArguments = @($GameArguments)
    noGpu = [bool]$NoGpu
    noPrompt = [bool]$NoPrompt
    legacyLayout = [Environment]::GetEnvironmentVariable('OPENMW_LUA_UI_LEGACY_LAYOUT', 'Process')
}
$encoding = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText($env:OPENMW_COMPARISON_FIXTURE_RESULT, (ConvertTo-Json -InputObject $record -Depth 6), $encoding)
if ($env:OPENMW_COMPARISON_FIXTURE_THROW -eq '1') { throw 'comparison-fixture-failure' }
'@
    [IO.File]::WriteAllText($collectorScript, $stub, $utf8)

    Check 'Windows PowerShell 5.1 and native script syntax' {
        Assert-True ($PSVersionTable.PSVersion.Major -eq 5 -and $PSVersionTable.PSVersion.Minor -eq 1) 'This fixture must run under Windows PowerShell 5.1.'
        foreach ($path in @($ComparisonScript, $collectorScript, $PSCommandPath)) {
            $tokens = $null
            $errors = $null
            [void][Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
            Assert-True ($errors.Count -eq 0) ('PowerShell syntax errors in ' + $path)
        }
    }

    foreach ($variant in @('Optimized', 'LegacyLayout')) {
        foreach ($initial in @([pscustomobject]@{ label = 'unset'; value = $null }, [pscustomobject]@{ label = 'existing-one'; value = '1' })) {
            foreach ($shouldThrow in @($false, $true)) {
                $outcome = if ($shouldThrow) { 'failure' } else { 'success' }
                $caseName = $variant + '-' + $initial.label + '-' + $outcome
                Check $caseName {
                    $parameters = @{
                        Variant = $variant
                        OpenMWExe = Join-Path $sessionRoot 'explicit engine [copy]\openmw.exe'
                        UserConfigDir = Join-Path $sessionRoot 'user configuration [copy]'
                        OutputRoot = Join-Path $sessionRoot 'capture output [copy]'
                        DeclaredFG = 'Off'
                        GameArguments = @('--skip-menu', '--load-savegame', 'Saves\Town walk [1].omwsave', 'literal $dollar; & "quoted" value')
                        NoGpu = $true
                        NoPrompt = $true
                    }
                    Invoke-Fixture $caseName $initial.value $parameters $shouldThrow
                }
            }
        }
    }
    Check 'Default variant and paths restore an existing custom environment value' {
        Invoke-Fixture 'defaults' 'custom-existing-value' @{} $false
    }
    Check 'Whitespace optional paths and false switches preserve collector defaults' {
        Invoke-Fixture 'blank-options' '0' @{
            Variant = 'LegacyLayout'; OpenMWExe = ' '; UserConfigDir = ' '; OutputRoot = ''
            DeclaredFG = 'On'; GameArguments = @(); NoGpu = $false; NoPrompt = $false
        } $false
    }
    Check 'A missing collector fails without changing the environment' {
        [IO.File]::Delete($collectorScript)
        [Environment]::SetEnvironmentVariable('OPENMW_LUA_UI_LEGACY_LAYOUT', '1', 'Process')
        $caught = $null
        try { & $copiedScript -Variant Optimized | Out-Null } catch { $caught = $_ }
        Assert-True ($null -ne $caught) 'A missing collector did not fail.'
        Assert-True ($caught.Exception.Message -like '*comparison capture script is missing*') 'Missing-collector failure had an unexpected cause.'
        Assert-True ([Environment]::GetEnvironmentVariable('OPENMW_LUA_UI_LEGACY_LAYOUT', 'Process') -ceq '1') 'Missing-collector failure changed the environment.'
    }
} finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable($name, $environmentBefore[$name], 'Process')
    }
    if ([IO.Directory]::Exists($sessionRoot)) { [IO.Directory]::Delete($sessionRoot, $true) }
}

$failures = @($checks | Where-Object { -not $_.passed })
$report = [ordered]@{
    schema = 1
    status = $(if ($failures.Count -eq 0) { 'PASS' } else { 'FAIL' })
    powershell_version = $PSVersionTable.PSVersion.ToString()
    comparison_script_sha256 = (Get-FileHash -LiteralPath $ComparisonScript -Algorithm SHA256).Hash.ToLowerInvariant()
    scope = 'Real comparison wrapper with a stub capture script; no engine, GPU, or in-game performance test.'
    check_count = $checks.Count
    failure_count = $failures.Count
    checks = $checks.ToArray()
}
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($ReportPath))
[IO.File]::WriteAllText($ReportPath, (ConvertTo-Json -InputObject $report -Depth 8), $utf8)
if ($failures.Count -ne 0) { throw ('Comparison wrapper checks failed: ' + $failures.Count) }
Write-Host ('Comparison wrapper checks passed: ' + $checks.Count)
