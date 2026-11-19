#requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackageRoot,
    [Parameter(Mandatory = $true)][string]$EvidenceRoot,
    [switch]$NegativeControl,
    [ValidateRange(5, 60)][int]$TimeoutSeconds = 30
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'This check must execute the real Windows engine on Windows.' }

$package = (Resolve-Path -LiteralPath $PackageRoot).Path
$evidence = [IO.Path]::GetFullPath($EvidenceRoot)
if (Test-Path -LiteralPath $evidence) { throw 'Use a fresh startup-evidence directory.' }
if ($evidence.StartsWith($package.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or $evidence -eq $package) {
    throw 'Startup evidence must be outside the input package.'
}

function Get-Identity([string]$Path) {
    $file = Get-Item -LiteralPath $Path
    return [ordered]@{ bytes = $file.Length; sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
}

function Normalize-LoggedPath([string]$Path) {
    # std::filesystem's quoted output doubles Windows backslashes.
    return $Path.Trim().Trim('"').Replace('\\', '\').Replace('\', '/').ToLowerInvariant()
}

$required = @('openmw.exe', 'openmw-launcher.exe', 'openmw.cfg', 'lua51.dll',
              'resources/lua_libs/content.lua', 'defaults.bin', 'SDL2.dll')
$identities = [ordered]@{}
foreach ($name in $required) {
    $path = Join-Path $package $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing required package file: $name" }
    $identities[$name] = Get-Identity $path
}
foreach ($name in @('version.dll', 'openmw_fg.ini')) {
    if (Test-Path -LiteralPath (Join-Path $package $name)) { throw "Optional frame generation must remain separate: $name" }
}

$temporary = Join-Path ([IO.Path]::GetTempPath()) ('openmw-package-startup-' + [guid]::NewGuid().ToString('N'))
$copy = Join-Path $temporary 'engine'
$cases = [Collections.Generic.List[object]]::new()
$report = [ordered]@{
    result = 'FAIL'
    package_root = $package
    packaged_files = $identities
    powershell_version = $PSVersionTable.PSVersion.ToString()
    timeout_seconds_per_launch = $TimeoutSeconds
    negative_control_requested = [bool]$NegativeControl
    environment = @{ OPENMW_DISABLE_CRASH_CATCHER = '1'; SDL_VIDEODRIVER = 'dummy' }
    cases = $cases
    input_files_unchanged = $false
    scope = 'Actual engine configuration/defaults startup without game assets; not a gameplay or Lua UI test'
}
New-Item -ItemType Directory -Path $evidence, $copy -Force | Out-Null

function Invoke-Startup([string]$Name, [bool]$ExpectMissingBootstrap) {
    $config = Join-Path $temporary "$Name-config"
    $data = Join-Path $temporary "$Name-data"
    New-Item -ItemType Directory -Path $config, $data | Out-Null
    $arguments = @('--replace=config', '--config', $config, '--user-data', $data, '--no-sound', '--no-grab')
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $copy 'openmw.exe'
    $start.WorkingDirectory = $copy
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $start.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
    $start.Environment['OPENMW_DISABLE_CRASH_CATCHER'] = '1'
    # The bundled SDL 2.32.10 restricts message-box fallback to this driver.
    # The dummy driver has no message-box callback, so the intentional fatal
    # error is logged instead of opening a blocking native Windows dialog.
    $start.Environment['SDL_VIDEODRIVER'] = 'dummy'
    foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $row = [ordered]@{
        name = $Name; arguments = $arguments; expected_missing_bootstrap = $ExpectMissingBootstrap
        result = 'FAIL'; timed_out = $false; output_complete = $false
    }
    $cases.Add($row)
    try {
        if (-not $process.Start()) { throw "Could not start the packaged engine: $Name" }
        $row['process_id'] = $process.Id
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $row['timed_out'] = -not $process.WaitForExit($TimeoutSeconds * 1000)
        if ($row['timed_out']) {
            # Kill only this test process and any children it created.
            $process.Kill($true)
            if (-not $process.WaitForExit(5000)) { throw "Timed-out engine did not terminate: $Name" }
        }
        $row['exit_code'] = $process.ExitCode
        $row['elapsed_ms'] = $watch.ElapsedMilliseconds
        if (-not $stdoutTask.Wait(5000) -or -not $stderrTask.Wait(5000)) { throw "Incomplete engine output: $Name" }
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $evidence "$Name.stdout.txt"), $stdout, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $evidence "$Name.stderr.txt"), $stderr, [Text.UTF8Encoding]::new($false))
        $row['output_complete'] = $true
        $log = ''
        $logPath = Join-Path $config 'openmw.log'
        if (Test-Path -LiteralPath $logPath -PathType Leaf) {
            Copy-Item -LiteralPath $logPath -Destination (Join-Path $evidence "$Name.openmw.log")
            $log = [IO.File]::ReadAllText($logPath)
        }
        $text = $stdout + "`n" + $stderr + "`n" + $log
        $loaded = @($text -split "`r?`n" | ForEach-Object {
            if ($_ -match 'Loading config file:\s*(.+)$') { Normalize-LoggedPath $Matches[1] }
        } | Sort-Object -Unique)
        $bootstrap = Normalize-LoggedPath (Join-Path $copy 'openmw.cfg')
        $noContent = 'No content file given (esm/esp, nor omwgame/omwaddon). Aborting...'
        $missingBootstrap = 'Fatal error: No config dirs! ConfigurationManager::readConfiguration must be called first.'
        $row['loaded_config_paths'] = $loaded
        $row['exact_no_content_message'] = $text.Contains($noContent)
        $row['exact_missing_bootstrap_error'] = $text.Contains($missingBootstrap)
        $row['packaged_bootstrap_loaded'] = $loaded -contains $bootstrap
        if ($row['timed_out']) { throw "Engine exceeded the startup timeout: $Name" }
        if ($ExpectMissingBootstrap) {
            if ($process.ExitCode -ne 1 -or -not $row['exact_missing_bootstrap_error'] -or
                $row['packaged_bootstrap_loaded'] -or $row['exact_no_content_message']) {
                throw 'Missing-bootstrap control did not reproduce the exact configuration failure.'
            }
            Write-Host 'PASS: missing-bootstrap control exited 1 with the exact No config dirs fatal error.'
        } else {
            if ($process.ExitCode -ne 0 -or -not $row['exact_no_content_message'] -or
                -not $row['packaged_bootstrap_loaded'] -or $loaded.Count -ne 1 -or
                $text -match 'No config dirs!|Neither local config nor global config are available|No default settings file found|Make sure the defaults\.bin|Fatal error:') {
                throw "Packaged engine did not pass configuration/defaults startup: $Name"
            }
            Write-Host "PASS: $Name exited 0 after loading its packaged bootstrap and reaching the exact no-content message."
        }
        $row['result'] = 'PASS'
    } finally {
        $watch.Stop()
        $process.Dispose()
    }
}

try {
    Get-ChildItem -LiteralPath $package -Force | Copy-Item -Destination $copy -Recurse -Force
    foreach ($name in $required) {
        if ((Get-Identity (Join-Path $copy $name)).sha256 -ne $identities[$name].sha256) {
            throw "Temporary engine copy differs from its package: $name"
        }
    }
    Invoke-Startup 'complete-package' $false
    if ($NegativeControl) {
        $bootstrap = Join-Path $copy 'openmw.cfg'
        $heldBootstrap = Join-Path $copy 'openmw.cfg.startup-control'
        Move-Item -LiteralPath $bootstrap -Destination $heldBootstrap
        try {
            Invoke-Startup 'missing-bootstrap' $true
        } finally {
            Move-Item -LiteralPath $heldBootstrap -Destination $bootstrap
        }
        if ((Get-Identity $bootstrap).sha256 -ne $identities['openmw.cfg'].sha256) {
            throw 'The negative control did not restore the exact bootstrap bytes.'
        }
        Invoke-Startup 'restored-bootstrap' $false
    }
    foreach ($name in $required) {
        if ((Get-Identity (Join-Path $package $name)).sha256 -ne $identities[$name].sha256) {
            throw "Input package changed during startup verification: $name"
        }
    }
    $report['input_files_unchanged'] = $true
    $report['result'] = 'PASS'
} catch {
    $report['error'] = $_.Exception.Message
    throw
} finally {
    $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $evidence 'startup-report.json') -Encoding utf8
    Remove-Item -LiteralPath $temporary -Recurse -Force
}
