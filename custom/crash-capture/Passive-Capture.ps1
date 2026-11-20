#requires -Version 5.1
# Loaded by OpenMW-CrashCapture.ps1 and the Windows integration harness.
. (Join-Path $PSScriptRoot 'Manage-WER.ps1') -LoadDefinitionsOnly

function Get-PassiveDumpFolder {
    if (-not $env:LOCALAPPDATA) { throw 'Windows LOCALAPPDATA is unavailable.' }
    return Join-Path $env:LOCALAPPDATA 'OpenMW Crash Captures\Dumps'
}
function Invoke-WerPermissionHelper {
    param([ValidateSet('Enable','Restore')][string]$Action, [string]$Folder)
    $helper = Join-Path $PSScriptRoot 'Manage-WER.ps1'
    $powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$helper,'-SetupMode',$Action)
    if ($Folder) { $arguments += @('-SetupDumpFolder',$Folder) }
    $line = ($arguments | ForEach-Object { Quote-NativeArgument $_ }) -join ' '
    Write-Host 'Windows will request permission to update crash capture for openmw.exe only.'
    $helperProcess = Start-Process -FilePath $powershell -ArgumentList $line -Verb RunAs -Wait -PassThru
    try { if ($helperProcess.ExitCode -ne 0) { throw 'Crash-capture setup/restore did not complete. The game was not started and the original backup was retained.' } }
    finally { $helperProcess.Dispose() }
}
function Assert-WerDumpsPreserved {
    param([string]$Folder)
    # Windows rotates its own dedicated folder after three dumps. Preserve every existing
    # file there before deliberately crashing the probe; refuse if a copy was omitted.
    foreach ($file in @(Get-ChildItem -LiteralPath $Folder -Filter '*.dmp' -File -ErrorAction Stop)) {
        $copied = @($script:Copied | Where-Object { $_.source.path -ieq $file.FullName })
        if ($copied.Count -eq 0) { throw ('A previous passive dump could not be preserved before rotation: ' + $file.FullName + '. Keep that file safely, then move it out of this Dumps folder and retry.') }
    }
}
function Read-CaptureMinidump {
    param([string]$Path, [uint64]$MemoryAddress = 0, [int]$MemoryBytes = 0)
    $stream = $null; $reader = $null
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        $reader = New-Object IO.BinaryReader $stream
        if ($stream.Length -lt 32 -or $reader.ReadUInt32() -ne 0x504D444D) { throw 'Not a minidump.' }
        $version = $reader.ReadUInt32(); $count = $reader.ReadUInt32(); $directory = $reader.ReadUInt32()
        $checksum = $reader.ReadUInt32(); $timestamp = $reader.ReadUInt32(); $flags = $reader.ReadUInt64()
        if ($count -gt 4096 -or ([long]$directory + 12L * $count) -gt $stream.Length) { throw 'Invalid minidump directory.' }
        $entries = @()
        for ($i = 0; $i -lt $count; $i++) {
            $stream.Position = [long]$directory + 12L * $i
            $type = $reader.ReadUInt32(); $size = $reader.ReadUInt32(); $offset = $reader.ReadUInt32()
            if ([long]$offset + $size -gt $stream.Length) { throw 'Truncated minidump stream.' }
            $entries += [pscustomobject]@{ type=$type; size=$size; offset=$offset }
        }
        $processId = $null; $exceptionCode = $null; $exceptionParameter = $null; $memory = $null
        foreach ($entry in $entries) {
            if ($entry.type -eq 15 -and $entry.size -ge 12) {
                $stream.Position = [long]$entry.offset + 4
                $miscFlags = $reader.ReadUInt32(); $value = $reader.ReadUInt32()
                if (($miscFlags -band 1) -ne 0) { $processId = $value }
            }
            if ($entry.type -eq 6 -and $entry.size -ge 48) {
                $stream.Position = [long]$entry.offset + 8; $exceptionCode = $reader.ReadUInt32()
                $stream.Position = [long]$entry.offset + 32; $parameterCount = $reader.ReadUInt32()
                if ($parameterCount -gt 0) { $stream.Position = [long]$entry.offset + 40; $exceptionParameter = $reader.ReadUInt64() }
            }
            if ($MemoryBytes -gt 0 -and $MemoryBytes -le 4096 -and $entry.type -in @(5,9)) {
                $stream.Position = $entry.offset
                if ($entry.type -eq 5) { $ranges = [long]$reader.ReadUInt32(); $rangesStart = [long]$entry.offset + 4; $dataOffset = 0L }
                else { $ranges = [long]$reader.ReadUInt64(); $dataOffset = [long]$reader.ReadUInt64(); $rangesStart = [long]$entry.offset + 16 }
                if ($ranges -lt 0 -or $ranges -gt 10000000 -or $rangesStart + 16L * $ranges -gt [long]$entry.offset + $entry.size) { throw 'Invalid minidump memory list.' }
                for ($j = 0; $j -lt $ranges; $j++) {
                    $stream.Position = $rangesStart + 16L * $j
                    $address = $reader.ReadUInt64()
                    if ($entry.type -eq 5) { $bytes = [long]$reader.ReadUInt32(); $dataOffset = [long]$reader.ReadUInt32() }
                    else { $bytes = [long]$reader.ReadUInt64() }
                    if ($bytes -lt 0 -or $dataOffset -lt 0 -or $dataOffset + $bytes -gt $stream.Length) { throw 'Truncated minidump memory data.' }
                    if ($MemoryAddress -ge $address -and ($MemoryAddress - $address) -le $bytes -and ($MemoryAddress - $address + $MemoryBytes) -le $bytes) {
                        $stream.Position = $dataOffset + [long]($MemoryAddress - $address)
                        $memory = [Text.Encoding]::ASCII.GetString($reader.ReadBytes($MemoryBytes))
                    }
                    if ($entry.type -eq 9) { $dataOffset += $bytes }
                    if ($null -ne $memory) { break }
                }
            }
        }
        return [pscustomobject]@{ path=$Path; bytes=$stream.Length; process_id=$processId; exception_code=$exceptionCode; exception_parameter=$exceptionParameter; flags=$flags; timestamp=$timestamp; memory_text=$memory }
    } finally { if ($null -ne $reader) { $reader.Dispose() }; if ($null -ne $stream) { $stream.Dispose() } }
}
function Find-PassiveDump {
    param([string]$Folder, [int]$ProcessId, [DateTime]$StartedUtc)
    foreach ($file in @(Get-ChildItem -LiteralPath $Folder -Filter ('openmw.exe.' + $ProcessId + '*.dmp') -File -ErrorAction SilentlyContinue)) {
        if ($file.LastWriteTimeUtc -lt $StartedUtc.AddSeconds(-2)) { continue }
        try {
            $dump = Read-CaptureMinidump $file.FullName
            $epoch = [DateTime]::SpecifyKind([DateTime]'1970-01-01', [DateTimeKind]::Utc)
            if ($dump.process_id -eq $ProcessId -and $epoch.AddSeconds($dump.timestamp) -ge $StartedUtc.AddSeconds(-2) -and $null -ne $dump.exception_code) { return $dump }
        } catch { $script:LastDumpReadError = $_.Exception.Message }
    }
    return $null
}
function Start-PassiveProcess {
    param([string]$Exe, [string[]]$Arguments = @())
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $Exe
    $info.WorkingDirectory = Split-Path -Parent $Exe
    $info.UseShellExecute = $false
    $info.Arguments = ($Arguments | ForEach-Object { Quote-NativeArgument $_ }) -join ' '
    # Exact engine source verified: nonzero bypasses its custom exception/freeze monitor.
    # This changes only the child, never this process or persisted environment values.
    $info.EnvironmentVariables['OPENMW_DISABLE_CRASH_CATCHER'] = '1'
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $info
    if (-not $process.Start()) { $process.Dispose(); throw 'Could not start the selected process.' }
    $null = $process.Handle # Hold a handle immediately, including for a very short-lived process.
    return $process
}
function Wait-PassiveResult {
    param($Process, [string]$Folder, [DateTime]$StartedUtc, [int]$TimeoutSeconds = 0, [int]$DumpWaitSeconds = 30)
    $gamePid = $Process.Id
    if ($TimeoutSeconds -gt 0) {
        if (-not $Process.WaitForExit($TimeoutSeconds * 1000)) { throw 'Synthetic capture check timed out.' }
    } else { $Process.WaitForExit() }
    $exitCode = $Process.ExitCode
    $dump = Find-PassiveDump -Folder $Folder -ProcessId $gamePid -StartedUtc $StartedUtc
    # Wait for WER only after an NTSTATUS-style fatal exit, never during gameplay.
    if ($exitCode -lt 0 -and $null -eq $dump) {
        $deadline = [DateTime]::UtcNow.AddSeconds($DumpWaitSeconds)
        while ($null -eq $dump -and [DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 500
            $dump = Find-PassiveDump -Folder $Folder -ProcessId $gamePid -StartedUtc $StartedUtc
        }
    }
    $status = 'normal_exit_no_dump'
    if ($null -ne $dump) { $status = 'crash_dump_captured' }
    elseif ($exitCode -ne 0) { $status = 'abnormal_exit_no_dump' }
    return [pscustomobject]@{
        process_id=$gamePid; game_exit_code=$exitCode; started_utc=$StartedUtc.ToString('o'); ended_utc=[DateTime]::UtcNow.ToString('o')
        capture_status=$status; dump=$dump; debugger_attached_by_tool=$false
        note='Normal exits do not need a dump. Nonzero exits without a dump may be handled errors or forced termination; they do not prove a native crash. Hangs and forced exits are not captured by WER.'
    }
}
function Test-PassiveCapture {
    param([string]$Folder)
    $probeSource = Join-Path $PSScriptRoot 'Capture-Probe.exe'
    $hashFile = Join-Path $PSScriptRoot 'Capture-Probe.sha256'
    if (-not (Test-Path -LiteralPath $probeSource) -or -not (Test-Path -LiteralPath $hashFile)) { throw 'The capture tool is incomplete. Extract the whole v1.4 ZIP before running it.' }
    $expected = (Get-Content -LiteralPath $hashFile -Raw).Trim()
    if ($expected -notmatch '^[0-9a-fA-F]{64}$' -or (Get-FileHash $probeSource -Algorithm SHA256).Hash -ine $expected) { throw 'Capture test executable hash did not match. Re-extract the complete tool.' }
    $probeRoot = Join-Path $script:CaptureRoot 'synthetic-probe'
    New-Item -ItemType Directory -Path $probeRoot -Force | Out-Null
    $probeExe = Join-Path $probeRoot 'openmw.exe'
    Copy-Item -LiteralPath $probeSource -Destination $probeExe
    $reportPath = Join-Path $probeRoot 'probe-process.json'
    $start = [DateTime]::UtcNow
    $process = $null
    try {
        $process = Start-PassiveProcess -Exe $probeExe -Arguments @('fastfail', $reportPath)
        $result = Wait-PassiveResult -Process $process -Folder $Folder -StartedUtc $start -TimeoutSeconds 60
        Write-JsonFile (Join-Path $probeRoot 'probe-result.json') $result
        if ($null -eq $result.dump) { throw 'Windows did not produce a dump for the local test. OpenMW was not started; send this evidence ZIP. No debugger was installed or changed.' }
        $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
        $marker = 'OpenMW-passive-capture-probe-stack-reference-v1.4'
        $details = Read-CaptureMinidump $result.dump.path -MemoryAddress $report.heap_address -MemoryBytes $marker.Length
        Write-JsonFile (Join-Path $probeRoot 'probe-dump-check.json') $details
        if ($report.debugger_present -or $report.elevated -or $report.catcher_environment -cne '1' -or $details.process_id -ne $report.pid -or $details.exception_code -ne 0xC0000409L -or $details.exception_parameter -ne 7 -or ($details.flags -band 0x1965) -ne 0x1965 -or $details.memory_text -cne $marker) {
            throw 'The passive capture test did not meet its checks. The game was not started; send this evidence ZIP.'
        }
        Copy-Item -LiteralPath $result.dump.path -Destination (Join-Path $probeRoot 'synthetic-fastfail.dmp')
        # The source file is ours (PID/time verified) and already preserved, but leave it in place.
        Write-Host 'Crash-only collection check passed. Starting OpenMW normally, without a debugger.'
    } finally {
        if ($null -ne $process) {
            if (-not $process.HasExited) { $process.Kill(); $null = $process.WaitForExit(5000) }
            $process.Dispose()
        }
        # Keep identity/source evidence but never leave a misleading second openmw.exe in the evidence ZIP.
        if (Test-Path -LiteralPath $probeExe) { Remove-Item -LiteralPath $probeExe -Force }
    }
}
function Start-PassiveOpenMW {
    param([string]$Exe, [string]$Folder)
    $start = [DateTime]::UtcNow
    $process = Start-PassiveProcess -Exe $Exe
    try {
        Write-JsonFile (Join-Path $script:CaptureRoot 'game-process.json') ([pscustomobject]@{ process_id=$process.Id; executable=$Exe; started_utc=$start.ToString('o'); child_environment=@{OPENMW_DISABLE_CRASH_CATCHER='1'}; debugger_attached_by_tool=$false })
        Write-Host 'Play normally. Leave this window open; evidence will be packaged after the game exits.'
        $result = Wait-PassiveResult -Process $process -Folder $Folder -StartedUtc $start
        Write-JsonFile (Join-Path $script:CaptureRoot 'process-result.json') $result
        if ($null -ne $result.dump) {
            $dest = Join-Path $script:CaptureRoot 'new-dumps'
            New-Item -ItemType Directory -Path $dest -Force | Out-Null
            # Do not apply the old-evidence size limit to the new crash we are trying to collect.
            Copy-Item -LiteralPath $result.dump.path -Destination (Join-Path $dest ([IO.Path]::GetFileName($result.dump.path)))
            Write-Host 'Windows captured a crash dump. Packaging evidence...'
        } elseif ($result.game_exit_code -eq 0) { Write-Host 'OpenMW exited normally; no crash dump was needed.' }
        else { Write-Host ('OpenMW exited with code ' + $result.game_exit_code + ', without a Windows crash dump. Logs and events will still be packaged.') -ForegroundColor Yellow }
    } finally { $process.Dispose() } # Never terminate the game from cleanup.
}
