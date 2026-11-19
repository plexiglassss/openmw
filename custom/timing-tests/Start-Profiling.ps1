param(
    [string]$OpenMWExe = (Join-Path $PSScriptRoot 'openmw.exe'),
    [string]$UserConfigDir = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games\OpenMW'),
    [switch]$WithoutTiming,
    [ValidateSet('Indexed', 'Linear')]
    [string]$CellLookup = 'Indexed'
)
$ErrorActionPreference = 'Stop'
if (Get-Process openmw,openmw-launcher -ErrorAction SilentlyContinue) {
    throw 'Close OpenMW and its launcher before starting this capture.'
}
$exe = (Resolve-Path -LiteralPath $OpenMWExe).Path
$runDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) ('OpenMW Profiling\Run-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $runDir | Out-Null
$names = @('OPENMW_TIMING_FILE','OPENMW_OSG_STATS_FILE','OPENMW_OSG_STATS_LIST','OPENMW_CELL_ID_INDEX')
$old = @{}
foreach ($name in $names) { $old[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:OPENMW_TIMING_FILE = if ($WithoutTiming) { $null } else { Join-Path $runDir 'script-timing.csv' }
    $env:OPENMW_OSG_STATS_FILE = Join-Path $runDir 'openmw-stats.log'
    $env:OPENMW_OSG_STATS_LIST = 'times;resource'
    $env:OPENMW_CELL_ID_INDEX = if ($CellLookup -eq 'Linear') { '0' } else { '1' }
    $start = Get-Date
    @("Executable: $exe", "Start: $($start.ToString('o'))", "Timing: $(!$WithoutTiming)") | Set-Content (Join-Path $runDir 'capture.txt')
    "Cell lookup: $CellLookup" | Add-Content (Join-Path $runDir 'capture.txt')
    foreach ($file in @('openmw.exe','lua51.dll','version.dll')) {
        $path = Join-Path (Split-Path $exe) $file
        if (Test-Path -LiteralPath $path) { Get-FileHash -LiteralPath $path -Algorithm SHA256 | Format-List | Out-File (Join-Path $runDir 'binary-hashes.txt') -Append }
    }
    foreach ($file in @('settings.cfg','openmw.cfg')) {
        $path = Join-Path $UserConfigDir $file
        if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination $runDir }
    }
    Write-Host "Capture folder: $runDir"
    Write-Host 'Reproduce the test, then quit the game normally. This window will collect the log.'
    $process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru -Wait -RedirectStandardError (Join-Path $runDir 'process-stderr.txt')
    "Exit: $((Get-Date).ToString('o')); code: $($process.ExitCode)" | Add-Content (Join-Path $runDir 'capture.txt')
    $log = Join-Path $UserConfigDir 'openmw.log'
    if ((Test-Path -LiteralPath $log) -and (Get-Item -LiteralPath $log).LastWriteTime -ge $start) {
        Copy-Item -LiteralPath $log -Destination $runDir
    } else { Write-Warning 'No fresh log at the selected user-config path; copy the actual log manually.' }
    # Collect only fresh OpenMW crash dumps after an abnormal process exit.
    if ($process.ExitCode -ne 0) {
        $dumpRoots = @($UserConfigDir, $env:USERPROFILE, $env:TEMP, (Split-Path $exe)) | Select-Object -Unique
        foreach ($root in $dumpRoots) {
            if (!$root -or !(Test-Path -LiteralPath $root)) { continue }
            foreach ($dump in Get-ChildItem -LiteralPath $root -Filter 'openmw*.dmp' -File -ErrorAction SilentlyContinue) {
                if ($dump.LastWriteTime -ge $start) {
                    Copy-Item -LiteralPath $dump.FullName -Destination (Join-Path $runDir ('dump-' + [guid]::NewGuid().ToString('N') + '-' + $dump.Name))
                }
            }
        }
        Write-Warning 'OpenMW returned an abnormal exit code. Fresh OpenMW dumps, if found, were copied into the capture.'
    }
    $required = @('openmw-stats.log')
    if (!$WithoutTiming) { $required += 'script-timing.csv' }
    foreach ($file in $required) {
        $path = Join-Path $runDir $file
        if (!(Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -lt 200) {
            Write-Warning "$file is missing or too small. Do not treat this capture as complete."
        }
    }
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $old[$name], 'Process') }
    explorer.exe $runDir
}
