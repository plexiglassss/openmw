#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Collect', 'Run')][string]$Mode = 'Collect',
    [string]$EnginePath,
    [string]$OutputRoot,
    [switch]$DefinitionsOnly
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function Write-JsonFile {
    param([string]$Path, $Value)
    ConvertTo-Json -InputObject $Value -Depth 12 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Add-CaptureNote {
    param([string]$Text)
    Add-Content -LiteralPath (Join-Path $script:CaptureRoot 'collector-notes.txt') -Value ((Get-Date -Format o) + ' ' + $Text) -Encoding UTF8
}

function Get-FileIdentity {
    param([string]$Path, [switch]$Hash)
    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    $row = [ordered]@{
        path = $item.FullName
        bytes = $item.Length
        modified_utc = $item.LastWriteTimeUtc.ToString('o')
        sha256 = $null
    }
    if ($Hash) { $row.sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    return [pscustomobject]$row
}

function Copy-EvidenceFile {
    param([string]$Source, [string]$Destination)
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { return }
    $row = $null
    try {
        $row = Get-FileIdentity -Path $Source
        # Large existing full-memory dumps are indexed, never deleted or silently omitted.
        # Each before/after snapshot gets its own allowance so old dumps cannot crowd out new evidence.
        $isDump = [IO.Path]::GetExtension($Source) -ieq '.dmp'
        if ($row.bytes -gt 512MB -or ($isDump -and ($script:CopiedDumpBytes + $row.bytes) -gt 1GB)) {
            $script:Omitted.Add([pscustomobject]@{ file = $row; reason = 'Size limit: 512 MiB per copied file / 1 GiB existing dumps per snapshot'; })
            Add-CaptureNote ('NOT COPIED (size limit): ' + $Source)
            return
        }
        $parent = Split-Path -Parent $Destination
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
        Copy-Item -LiteralPath $Source -Destination $Destination -ErrorAction Stop
        if ($isDump) { $script:CopiedDumpBytes += $row.bytes }
        $copied = Get-FileIdentity -Path $Destination -Hash
        $script:Copied.Add([pscustomobject]@{ source = $row; saved = $copied; })
    } catch {
        $script:Omitted.Add([pscustomobject]@{ path = $Source; reason = $_.Exception.Message; })
        Add-CaptureNote ('Could not copy ' + $Source + ': ' + $_.Exception.Message)
    }
}

function Get-OpenMWLocations {
    param([string]$Exe)
    $dirs = New-Object 'System.Collections.Generic.List[string]'
    $documents = [Environment]::GetFolderPath('MyDocuments')
    if ($documents) { $dirs.Add((Join-Path $documents 'My Games\OpenMW')) }
    if ($env:USERPROFILE) {
        $dirs.Add((Join-Path $env:USERPROFILE 'Documents\My Games\OpenMW'))
        $dirs.Add($env:USERPROFILE)
    }
    if ($env:TEMP) { $dirs.Add($env:TEMP) }
    if ($Exe) { $dirs.Add((Split-Path -Parent $Exe)) }
    if ($env:LOCALAPPDATA) { $dirs.Add((Join-Path $env:LOCALAPPDATA 'CrashDumps')) }
    return @($dirs | Select-Object -Unique)
}

function Save-OpenMWSnapshot {
    param([string]$Label, [string]$Exe)
    $dest = Join-Path $script:CaptureRoot $Label
    New-Item -ItemType Directory -Path $dest -Force | Out-Null
    $locations = @(Get-OpenMWLocations -Exe $Exe)
    Write-JsonFile -Path (Join-Path $dest 'searched-locations.json') -Value $locations
    $index = 0
    foreach ($dir in $locations) {
        $index++
        if (-not (Test-Path -LiteralPath $dir -PathType Container)) { continue }
        $target = Join-Path $dest ('location-' + $index)
        foreach ($name in @('openmw.log', 'openmw.cfg', 'settings.cfg', 'openmw-crash.dmp', 'openmw-freeze.dmp', 'openmw-crash.log')) {
            Copy-EvidenceFile -Source (Join-Path $dir $name) -Destination (Join-Path $target $name)
        }
        # WER names include the process ID; search only this directory, never the whole drive.
        try {
            foreach ($file in @(Get-ChildItem -LiteralPath $dir -Filter 'openmw*.dmp' -File -ErrorAction Stop)) {
                if ($file.Name -in @('openmw-crash.dmp', 'openmw-freeze.dmp')) { continue }
                Copy-EvidenceFile -Source $file.FullName -Destination (Join-Path $target $file.Name)
            }
        } catch { Add-CaptureNote ('Dump listing failed for ' + $dir + ': ' + $_.Exception.Message) }
    }
}

function Export-RelevantEvents {
    param([string]$Label)
    $dest = Join-Path $script:CaptureRoot $Label
    New-Item -ItemType Directory -Path $dest -Force | Out-Null
    $start = (Get-Date).AddDays(-2)
    $queries = @(
        @{ name = 'application'; filter = @{ LogName = 'Application'; Id = @(1000,1001,1002); StartTime = $start }; openmwOnly = $true },
        @{ name = 'display-hardware'; filter = @{ LogName = 'System'; ProviderName = @('Display','nvlddmkm','Microsoft-Windows-WHEA-Logger','Microsoft-Windows-Resource-Exhaustion-Detector'); StartTime = $start }; openmwOnly = $false }
    )
    foreach ($query in $queries) {
        try {
            # Filter in Event Log, not after an arbitrary cap of unrelated applications.
            $events = @(Get-WinEvent -FilterHashtable $query.filter -ErrorAction Stop | Where-Object {
                (-not $query.openmwOnly) -or ($_.ToXml() -match '(?i)openmw(?:\.exe)?')
            } | Select-Object -First 200)
            $rows = @($events | ForEach-Object {
                [pscustomobject]@{ time = $_.TimeCreated.ToString('o'); id = $_.Id; provider = $_.ProviderName; record_id = $_.RecordId; message = $_.Message; xml = $_.ToXml() }
            })
            Write-JsonFile -Path (Join-Path $dest ($query.name + '.json')) -Value $rows
        } catch {
            # NoMatchingEventsFound is also recorded; an empty file is never called proof of no crash.
            Add-CaptureNote ('Event query ' + $query.name + ': ' + $_.Exception.Message)
        }
    }
    $bases = @()
    if ($env:LOCALAPPDATA) { $bases += (Join-Path $env:LOCALAPPDATA 'Microsoft\Windows\WER') }
    if ($env:ProgramData) { $bases += (Join-Path $env:ProgramData 'Microsoft\Windows\WER') }
    $reportIndex = 0
    foreach ($base in $bases) {
        foreach ($queue in @('ReportArchive','ReportQueue')) {
            $path = Join-Path $base $queue
            if (-not (Test-Path -LiteralPath $path)) { continue }
            try {
                $reports = @(Get-ChildItem -LiteralPath $path -Directory -ErrorAction Stop | Where-Object {
                    $_.Name -match '(?i)openmw' -and $_.LastWriteTime -ge $start
                } | Sort-Object LastWriteTime -Descending | Select-Object -First 20)
                foreach ($report in $reports) {
                    $reportIndex++
                    foreach ($file in @(Get-ChildItem -LiteralPath $report.FullName -File -ErrorAction Stop | Where-Object { $_.Extension -in @('.wer','.dmp') })) {
                        Copy-EvidenceFile -Source $file.FullName -Destination (Join-Path (Join-Path $dest ('wer-' + $reportIndex)) $file.Name)
                    }
                }
            } catch { Add-CaptureNote ('WER read failed for ' + $path + ': ' + $_.Exception.Message) }
        }
    }
}

function Save-EngineIdentity {
    param([string]$Exe)
    if (-not $Exe -or -not (Test-Path -LiteralPath $Exe -PathType Leaf)) { return }
    $dir = Split-Path -Parent $Exe
    $rows = New-Object 'System.Collections.Generic.List[object]'
    foreach ($file in @(Get-ChildItem -LiteralPath $dir -File | Where-Object { $_.Extension -in @('.dll','.exe') })) {
        try {
            $identity = Get-FileIdentity -Path $file.FullName -Hash
            $rows.Add([pscustomobject]@{ file = $identity; product = $file.VersionInfo.ProductName; version = $file.VersionInfo.FileVersion; })
        } catch { Add-CaptureNote ('Cannot identify ' + $file.FullName + ': ' + $_.Exception.Message) }
    }
    Write-JsonFile -Path (Join-Path $script:CaptureRoot 'engine-files.json') -Value @($rows.ToArray())
    # Preserve the selected installation's Lua runtime for matching-image stack unwinding.
    # Its bytes can differ from the engine artifact even when openmw.exe is identical.
    $luaRuntime = Join-Path $dir 'lua51.dll'
    if (Test-Path -LiteralPath $luaRuntime -PathType Leaf) {
        Copy-EvidenceFile -Source $luaRuntime -Destination (Join-Path $script:CaptureRoot 'binaries\lua51.dll')
    } else {
        Add-CaptureNote ('Lua runtime not found beside the selected engine: ' + $luaRuntime)
    }
    $version = Join-Path $dir 'resources\version'
    Copy-EvidenceFile -Source $version -Destination (Join-Path $script:CaptureRoot 'engine-resource-version.txt')
}

function Assert-OriginalEvidencePreserved {
    foreach ($entry in $script:Omitted) {
        $path = $null
        if ($entry.PSObject.Properties['file']) { $path = $entry.file.path }
        elseif ($entry.PSObject.Properties['path']) { $path = $entry.path }
        if ($path -and [IO.Path]::GetFileName($path) -in @('openmw.log','openmw-crash.dmp','openmw-freeze.dmp')) {
            throw ('OpenMW was not launched because existing evidence could not be preserved: ' + $path + '. Send the collector ZIP first; the original is still in place.')
        }
    }
}

function Assert-CaptureFreeSpace {
    param([long]$RequiredBytes = 4GB)
    $drive = New-Object IO.DriveInfo ([IO.Path]::GetPathRoot($script:CaptureRoot))
    $free = $drive.AvailableFreeSpace
    Write-JsonFile -Path (Join-Path $script:CaptureRoot 'free-space.json') -Value ([pscustomobject]@{
        free_bytes = $free; minimum_before_capture = $RequiredBytes;
        note = 'A minimum headroom check, not an upper bound on dump or ZIP size. New ProcDump dumps are retained in full.'
    })
    if ($free -lt $RequiredBytes) { throw 'Less than 4 GiB is available on the evidence drive. OpenMW was not launched; free some space or choose another local output folder.' }
}

function Quote-NativeArgument {
    param([AllowEmptyString()][string]$Value)
    # Windows CommandLineToArgvW / CRT escaping, including spaces and trailing backslashes.
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}

function Get-ProcDumpArguments {
    param([string]$DumpFolder, [string]$Exe)
    # -x starts precisely this executable under observation; never attach by openmw.exe name
    # because OpenMW also starts an openmw.exe --crash-monitor child.
    return @('-mm','-e','-t','-n','2','-x',$DumpFolder,$Exe)
}

function Test-MicrosoftProcDump {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid' -or $null -eq $signature.SignerCertificate) { return $false }
    if ($signature.SignerCertificate.Subject -notmatch '(?:^|,\s*)O=Microsoft Corporation(?:,|$)') { return $false }
    $info = (Get-Item -LiteralPath $Path).VersionInfo
    return ($info.FileDescription -match '(?i)ProcDump' -or $info.ProductName -match '(?i)ProcDump')
}

function Get-ProcDump {
    $toolDir = Join-Path $PSScriptRoot 'Microsoft-ProcDump'
    $exe = Join-Path $toolDir 'procdump64.exe'
    if (-not (Test-MicrosoftProcDump -Path $exe)) {
        Write-Host 'Downloading Microsoft ProcDump from download.sysinternals.com...'
        New-Item -ItemType Directory -Path $toolDir -Force | Out-Null
        $archive = Join-Path $toolDir 'Procdump.zip'
        $temp = Join-Path $toolDir ('download-' + [guid]::NewGuid().ToString('N'))
        $oldTls = [Net.ServicePointManager]::SecurityProtocol
        try {
            [Net.ServicePointManager]::SecurityProtocol = $oldTls -bor [Net.SecurityProtocolType]::Tls12
            Invoke-WebRequest -UseBasicParsing -Uri 'https://download.sysinternals.com/files/Procdump.zip' -OutFile $archive -TimeoutSec 90
            Expand-Archive -LiteralPath $archive -DestinationPath $temp
            $downloaded = Join-Path $temp 'procdump64.exe'
            if (-not (Test-MicrosoftProcDump -Path $downloaded)) { throw 'ProcDump signature or product identity did not validate. Nothing was executed.' }
            Copy-Item -LiteralPath $downloaded -Destination $exe -Force
            if (Test-Path -LiteralPath (Join-Path $temp 'Eula.txt')) { Copy-Item -LiteralPath (Join-Path $temp 'Eula.txt') -Destination $toolDir -Force }
        } finally {
            [Net.ServicePointManager]::SecurityProtocol = $oldTls
            if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
        }
    }
    if (-not (Test-MicrosoftProcDump -Path $exe)) { throw 'Microsoft ProcDump could not be verified.' }
    Write-JsonFile -Path (Join-Path $script:CaptureRoot 'procdump-identity.json') -Value (Get-FileIdentity -Path $exe -Hash)
    return $exe
}

function Test-MinidumpHeader {
    param([string]$Path)
    $stream = $null
    try {
        $stream = [IO.File]::OpenRead($Path)
        if ($stream.Length -lt 32) { return $false }
        $magic = New-Object byte[] 4
        if ($stream.Read($magic,0,4) -ne 4) { return $false }
        return ([Text.Encoding]::ASCII.GetString($magic) -ceq 'MDMP')
    } catch { return $false } finally { if ($null -ne $stream) { $stream.Dispose() } }
}

function Read-ProcDumpText {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    # ProcDump or its child may still hold the redirected output handle after exit.
    # Read a shared snapshot; dump/completion counts and game lifetime are verified separately.
    $stream = $null
    $copy = $null
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        $copy = New-Object IO.MemoryStream
        $stream.CopyTo($copy)
        $bytes = $copy.ToArray()
    } finally {
        if ($null -ne $copy) { $copy.Dispose() }
        if ($null -ne $stream) { $stream.Dispose() }
    }
    if ($bytes.Length -ge 2 -and $bytes[0] -eq 255 -and $bytes[1] -eq 254) {
        return [Text.Encoding]::Unicode.GetString($bytes).TrimStart([char]0xFEFF)
    }
    if ($bytes.Length -ge 2 -and $bytes[0] -eq 254 -and $bytes[1] -eq 255) {
        return [Text.Encoding]::BigEndianUnicode.GetString($bytes).TrimStart([char]0xFEFF)
    }
    # Redirected ProcDump output can be UTF-16 without a byte-order mark.
    if ($bytes.Length -ge 4 -and $bytes[1] -eq 0 -and $bytes[3] -eq 0) {
        return [Text.Encoding]::Unicode.GetString($bytes)
    }
    if ($bytes.Length -ge 4 -and $bytes[0] -eq 0 -and $bytes[2] -eq 0) {
        return [Text.Encoding]::BigEndianUnicode.GetString($bytes)
    }
    return [Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xFEFF)
}

function Get-ProcDumpCaptureResult {
    param($ExitCode, [string]$DumpFolder, [string]$OutputPath, [string]$ErrorPath, [int]$MaximumDumps)
    $stdout = Read-ProcDumpText -Path $OutputPath
    $stderr = Read-ProcDumpText -Path $ErrorPath
    $dumpFiles = @(Get-ChildItem -LiteralPath $DumpFolder -Filter '*.dmp' -File)
    $valid = @($dumpFiles | Where-Object { Test-MinidumpHeader -Path $_.FullName })
    $completed = @([regex]::Matches($stdout, '(?m)^\s*\[[^\]\r\n]+\]\s*Dump\s+([1-9][0-9]*)\s+complete:') | ForEach-Object { [int]$_.Groups[1].Value })
    $distinctCompleted = @($completed | Sort-Object -Unique)
    $logError = [regex]::IsMatch(($stdout + "`n" + $stderr), '(?im)^\s*(?:\[\d{2}:\d{2}:\d{2}\]\s*(?:Dump\s+\d+\s+(?:error|failed)\b|Error\s+(?:writing|creating|opening)\b|Failed\s+(?:to\s+)?(?:write|create|open)\b|Access is denied\b)|Error\s+(?:(?:creating (?:process|dump file)|writing dump|opening process|debugging process)\b|(?:opening [^\r\n]+ \(\d+\)|launching [^\r\n]+):)|Exception in MiniDumpWriteDump Callback:)')
    $ordinalSequence = (($distinctCompleted -join ',') -ceq ((1..([Math]::Max(1,$distinctCompleted.Count))) -join ','))
    # ProcDump's successful return is the number of dumps written, not conventional zero.
    # Require independent file and completed-write evidence; a positive exit alone is insufficient.
    $verified = ($null -ne $ExitCode -and $ExitCode -ge 1 -and $ExitCode -le $MaximumDumps -and
        $ExitCode -eq $valid.Count -and $ExitCode -eq $completed.Count -and
        $completed.Count -eq $distinctCompleted.Count -and $ordinalSequence -and
        $dumpFiles.Count -eq $valid.Count -and -not $logError)
    return [pscustomobject]@{
        verified = $verified; procdump_exit_code = $ExitCode; maximum_dumps = $MaximumDumps;
        dump_files = $dumpFiles.Count; dumps_with_mdmp_header = $valid.Count; completed_dump_count = $completed.Count;
        completed_dump_numbers = $completed; explicit_log_error = $logError;
        note = 'Success requires matching positive exit/dump/completed-write counts. The exit code belongs to ProcDump, not the game. MDMP header checks are not full dump-integrity validation.'
    }
}

function Test-ProcDumpCapture {
    param([string]$ProcDump)
    $dest = Join-Path $script:CaptureRoot 'procdump-self-test'
    New-Item -ItemType Directory -Path $dest -Force | Out-Null
    $cmdExe = Join-Path $env:SystemRoot 'System32\cmd.exe'
    # Microsoft's current support page lists Windows 11+ clients. Do not infer Windows 10
    # compatibility from a valid signature: first exercise this exact binary on this PC.
    # This short-lived, empty cmd process does not run or change the game.
    $argv = @('-mm','-e','-t','-n','1','-x',$dest,$cmdExe,'/d','/c','exit','0')
    $command = ($argv | ForEach-Object { Quote-NativeArgument -Value $_ }) -join ' '
    Write-Host 'Checking ProcDump capture on this PC first. Accept Microsoft''s license if shown.'
    $probe = Start-Process -FilePath $ProcDump -ArgumentList $command -WorkingDirectory $dest -NoNewWindow -PassThru -RedirectStandardOutput (Join-Path $dest 'output.txt') -RedirectStandardError (Join-Path $dest 'errors.txt')
    # Windows PowerShell can otherwise lose ExitCode after a redirected child exits.
    # Retain the native handle immediately, before waiting (PowerShell issue #20400).
    $probeHandle = $probe.Handle
    if ($null -eq $probeHandle -or $probeHandle -eq [IntPtr]::Zero) {
        throw 'Could not retain the ProcDump test process handle. OpenMW was not launched; partial evidence was kept.'
    }
    $probe.WaitForExit()
    $probeExit = $probe.ExitCode
    $capture = Get-ProcDumpCaptureResult -ExitCode $probeExit -DumpFolder $dest -OutputPath (Join-Path $dest 'output.txt') -ErrorPath (Join-Path $dest 'errors.txt') -MaximumDumps 1
    $testExitObserved = (Read-ProcDumpText -Path (Join-Path $dest 'output.txt')) -match 'Process Exit:\s*PID\s+\d+,\s*Exit Code\s+0x00000000\b'
    $ok = ($capture.verified -and $testExitObserved)
    Write-JsonFile -Path (Join-Path $dest 'result.json') -Value ([pscustomobject]@{
        arguments = $argv; procdump_exit_code = $probeExit; dumps_with_mdmp_header = $capture.dumps_with_mdmp_header; passed = $ok; test_process_exit_zero_observed = $testExitObserved; capture_evidence = $capture;
        scope = 'Local native-process launch and termination-dump smoke test, with minimum size/MDMP-header checks only. This does not establish operating-system vendor support, complete dump integrity or every game-crash path.'
    })
    if ($null -eq $probeExit) { throw 'The ProcDump test exit code was unavailable. OpenMW was not launched; send the evidence ZIP so capture status can be checked.' }
    if (-not $ok) { throw 'ProcDump could not pass its capture check on this PC. OpenMW was not launched. Send the evidence ZIP, which includes the failure details.' }
}

function Save-ProcessDetails {
    param($ProcessRow, [System.Diagnostics.Process]$Process)
    Write-JsonFile -Path (Join-Path $script:CaptureRoot 'game-process.json') -Value ([pscustomobject]@{
        pid = $ProcessRow.ProcessId; parent_pid = $ProcessRow.ParentProcessId; executable = $ProcessRow.ExecutablePath; command_line = $ProcessRow.CommandLine
    })
    try {
        $modules = @($Process.Modules | ForEach-Object {
            [pscustomobject]@{ name = $_.ModuleName; path = $_.FileName; base_address = ('0x{0:X}' -f $_.BaseAddress.ToInt64()); bytes = $_.ModuleMemorySize; version = $_.FileVersionInfo.FileVersion }
        })
        Write-JsonFile -Path (Join-Path $script:CaptureRoot 'loaded-modules-snapshot.json') -Value $modules
    } catch { Add-CaptureNote ('Module snapshot unavailable: ' + $_.Exception.Message) }
}

function Start-MonitoredOpenMW {
    param([string]$Exe, [string]$ProcDump)
    $dumps = Join-Path $script:CaptureRoot 'new-dumps'
    New-Item -ItemType Directory -Path $dumps -Force | Out-Null
    $argv = @(Get-ProcDumpArguments -DumpFolder $dumps -Exe $Exe)
    Write-JsonFile -Path (Join-Path $script:CaptureRoot 'capture-command.json') -Value ([pscustomobject]@{ executable = $ProcDump; arguments = $argv; working_directory = (Split-Path -Parent $Exe) })
    $command = ($argv | ForEach-Object { Quote-NativeArgument -Value $_ }) -join ' '
    Write-Host 'Starting OpenMW through ProcDump. Accept Microsoft''s license if it appears.'
    Write-Host 'Leave this window open. After the game closes, evidence will be packaged here.'
    Write-Host 'The ProcDump log records whether it successfully started monitoring.'
    $monitor = Start-Process -FilePath $ProcDump -ArgumentList $command -WorkingDirectory (Split-Path -Parent $Exe) -NoNewWindow -PassThru -RedirectStandardOutput (Join-Path $script:CaptureRoot 'procdump-output.txt') -RedirectStandardError (Join-Path $script:CaptureRoot 'procdump-errors.txt')
    # Keep this handle through termination too; the game process is tracked separately.
    $monitorHandle = $monitor.Handle
    if ($null -eq $monitorHandle -or $monitorHandle -eq [IntPtr]::Zero) {
        throw 'Could not retain the ProcDump monitor handle. The game may have started; monitoring is unconfirmed and partial evidence was kept.'
    }
    $game = $null
    $row = $null
    $started = Get-Date
    $lastCheck = $started.AddMinutes(-1)
    $moduleRecorded = $false
    while (-not $monitor.WaitForExit(1000)) {
        if (((Get-Date) - $lastCheck).TotalSeconds -ge 5) {
            $lastCheck = Get-Date
            if ($null -eq $game) {
                try {
                    $children = @(Get-CimInstance Win32_Process -Filter ('ParentProcessId = ' + $monitor.Id) | Where-Object {
                        $_.Name -ieq 'openmw.exe' -and $_.CommandLine -notmatch '(?i)--crash-monitor'
                    })
                    if ($children.Count -eq 1) {
                        $row = $children[0]
                        $game = Get-Process -Id $row.ProcessId -ErrorAction Stop
                        $null = $game.Handle # Retain a handle so ExitCode can be queried after termination.
                        Save-ProcessDetails -ProcessRow $row -Process $game
                        Write-Host ('Game process observed: ' + $game.Id + '. Play until the crash or exit normally.')
                    }
                } catch { Add-CaptureNote ('Process discovery: ' + $_.Exception.Message) }
            } elseif (-not $moduleRecorded -and ((Get-Date) - $started).TotalSeconds -ge 30) {
                Save-ProcessDetails -ProcessRow $row -Process $game
                $moduleRecorded = $true
            }
        }
    }
    $monitor.WaitForExit()
    # A dump cap can end ProcDump just before a crashing child finishes terminating.
    # Wait briefly for that independently tracked process; never kill it or assume exit.
    if ($null -ne $game) {
        try { $null = $game.WaitForExit(10000) }
        catch { Add-CaptureNote ('Final game-exit wait: ' + $_.Exception.Message) }
    }
    $gameExit = $null
    if ($null -ne $game) {
        try { if ($game.HasExited) { $gameExit = $game.ExitCode } } catch { Add-CaptureNote ('Game exit-code read: ' + $_.Exception.Message) }
    }
    $gameStillAlive = $null
    if ($null -ne $game) {
        try { $gameStillAlive = -not $game.HasExited } catch { Add-CaptureNote ('Final game-state read: ' + $_.Exception.Message) }
    }
    $monitorExit = $monitor.ExitCode
    $capture = Get-ProcDumpCaptureResult -ExitCode $monitorExit -DumpFolder $dumps -OutputPath (Join-Path $script:CaptureRoot 'procdump-output.txt') -ErrorPath (Join-Path $script:CaptureRoot 'procdump-errors.txt') -MaximumDumps 2
    $captureProduced = $capture.verified
    $gameExitConfirmed = ($gameStillAlive -eq $false)
    $status = 'capture_failed'
    if ($captureProduced) {
        if ($gameExitConfirmed) { $status = 'dump_captured_game_exit_confirmed' }
        else { $status = 'dump_captured_game_exit_unconfirmed' }
    }
    if ($gameStillAlive -eq $true) { $status = 'monitor_ended_game_alive' }
    Write-JsonFile -Path (Join-Path $script:CaptureRoot 'process-result.json') -Value ([pscustomobject]@{
        procdump_exit_code = $monitorExit; game_exit_code = $gameExit; started_local = $started.ToString('o'); ended_local = (Get-Date).ToString('o');
        dumps_with_mdmp_header = $capture.dumps_with_mdmp_header; capture_evidence = $capture; capture_status = $status; capture_completed = ($captureProduced -and $gameExitConfirmed); game_still_alive = $gameStillAlive;
        note = 'ProcDump exit code is not the game exit code. A termination dump is also generated after a normal exit and is not by itself proof of a crash.'
    })
    if ($gameStillAlive -eq $true) { Add-CaptureNote 'ProcDump ended while the game was still alive. Monitoring did not cover the rest of the session.' }
    if (-not $captureProduced -or $gameStillAlive -eq $true) { throw 'Monitoring was incomplete or produced no game dump with a recognizable header. Partial evidence was retained; see process-result.json and the ProcDump logs.' }
    if (-not $gameExitConfirmed) { Add-CaptureNote 'A dump was produced, but the game lifetime could not be independently observed. Consult the ProcDump logs; game state is unknown, not assumed exited.' }
}

function Complete-Capture {
    Write-JsonFile -Path (Join-Path $script:CaptureRoot 'copied-evidence.json') -Value @($script:Copied.ToArray())
    Write-JsonFile -Path (Join-Path $script:CaptureRoot 'omitted-evidence.json') -Value @($script:Omitted.ToArray())
    $entries = @(Get-ChildItem -LiteralPath $script:CaptureRoot -Recurse -File | ForEach-Object {
        $filePath = $_.FullName
        try { Get-FileIdentity -Path $filePath -Hash }
        catch {
            $identity = Get-FileIdentity -Path $filePath
            $identity | Add-Member -NotePropertyName hash_unavailable -NotePropertyValue $_.Exception.Message
            $identity
        }
    })
    Write-JsonFile -Path (Join-Path $script:CaptureRoot 'file-manifest.json') -Value $entries
    $zipPath = $script:CaptureRoot + '.zip'
    # ZipFile handles ZIP64; Compress-Archive on Windows PowerShell has a 2 GiB file limit.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    # A failed/incomplete session may leave redirected logs open for writing.
    # Zip a shared-read snapshot instead of failing to return all evidence.
    $archive = $null
    try {
        $archive = [IO.Compression.ZipFile]::Open($zipPath, [IO.Compression.ZipArchiveMode]::Create)
        foreach ($file in @(Get-ChildItem -LiteralPath $script:CaptureRoot -Recurse -File)) {
            $name = $file.FullName.Substring($script:CaptureRoot.Length).TrimStart([char[]]'\/').Replace('\','/')
            $entry = $archive.CreateEntry($name, [IO.Compression.CompressionLevel]::Optimal)
            $sourceStream = $null
            $entryStream = $null
            try {
                $sourceStream = [IO.File]::Open($file.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
                $entryStream = $entry.Open()
                $sourceStream.CopyTo($entryStream)
            } finally {
                if ($null -ne $entryStream) { $entryStream.Dispose() }
                if ($null -ne $sourceStream) { $sourceStream.Dispose() }
            }
        }
    } finally { if ($null -ne $archive) { $archive.Dispose() } }
    Write-Host ''
    Write-Host ('Evidence saved: ' + $zipPath) -ForegroundColor Green
    Write-Host 'Upload this ZIP to the chat. The original files have not been moved.'
    if ($script:Omitted.Count -gt 0) { Write-Host 'Some files could not be included; see omitted-evidence.json.' -ForegroundColor Yellow }
    try { Start-Process -FilePath 'explorer.exe' -ArgumentList ('/select,' + (Quote-NativeArgument -Value $zipPath)) | Out-Null }
    catch { Write-Host ('The ZIP is ready, but Explorer could not open it: ' + $_.Exception.Message) -ForegroundColor Yellow }
}

if ($DefinitionsOnly) { return }
if ($env:OS -ne 'Windows_NT') { throw 'This capture launcher must run on the Windows PC running OpenMW.' }
if (-not [Environment]::Is64BitProcess) { throw 'Run this tool using 64-bit Windows PowerShell.' }
if (-not $OutputRoot) { $OutputRoot = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'OpenMW Crash Captures' }
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$script:CaptureRoot = Join-Path $OutputRoot ('OpenMW-' + $Mode + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,6))
New-Item -ItemType Directory -Path $script:CaptureRoot -Force | Out-Null
$script:CopiedDumpBytes = 0L
$script:Copied = New-Object 'System.Collections.Generic.List[object]'
$script:Omitted = New-Object 'System.Collections.Generic.List[object]'
$failed = $false
try {
    $knownExe = 'F:\Modding\OpenMW 0.52 RC1 - latest custom build\openmw.exe'
    if (-not $EnginePath -and (Test-Path -LiteralPath $knownExe -PathType Leaf)) { $EnginePath = $knownExe }
    if ($Mode -eq 'Run') {
        Add-Type -AssemblyName System.Windows.Forms
        $picker = New-Object System.Windows.Forms.OpenFileDialog
        $picker.Title = 'Choose openmw.exe from the build you are testing'
        $picker.Filter = 'OpenMW game (openmw.exe)|openmw.exe'
        if ($EnginePath) { $picker.FileName = $EnginePath; $picker.InitialDirectory = Split-Path -Parent $EnginePath }
        if ($picker.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { throw 'No game selected; capture was cancelled.' }
        $EnginePath = $picker.FileName
        if ([IO.Path]::GetFileName($EnginePath) -ine 'openmw.exe') { throw 'Select the game openmw.exe, not the launcher.' }
        $existing = @(Get-CimInstance Win32_Process -Filter "Name = 'openmw.exe'")
        if ($existing.Count -gt 0) { throw 'OpenMW or its crash monitor is already running. Close it normally, then run this launcher again.' }
    }
    $environmentRows = @{}
    foreach ($name in @('OPENMW_DISABLE_CRASH_CATCHER','OPENMW_FULL_MEMDUMP','OPENMW_DEBUG_LEVEL')) {
        $environmentRows[$name] = @{}
        foreach ($scope in @('Process','User','Machine')) { $environmentRows[$name][$scope] = [Environment]::GetEnvironmentVariable($name,$scope) }
    }
    Write-JsonFile -Path (Join-Path $script:CaptureRoot 'session.json') -Value ([pscustomobject]@{
        tool_version = '1.3'; mode = $Mode; local_time = (Get-Date).ToString('o'); utc_time = [DateTime]::UtcNow.ToString('o');
        timezone = [TimeZoneInfo]::Local.Id; os = [Environment]::OSVersion.VersionString; powershell = $PSVersionTable.PSVersion.ToString();
        engine_selected = $EnginePath; environment = $environmentRows;
        limitations = 'Only documented OpenMW locations and relevant Windows events are searched. No mod conflict diagnosis or live gameplay acceptance is implied. DMPs may contain game memory and local paths; nothing is uploaded automatically.'
    })
    Write-Host 'OpenMW Crash Capture v1.3'
    Write-Host 'Collecting existing OpenMW logs, crash dumps and Windows crash events...'
    Save-OpenMWSnapshot -Label 'before' -Exe $EnginePath
    Export-RelevantEvents -Label 'events-before'
    Save-EngineIdentity -Exe $EnginePath
    if ($Mode -eq 'Run') {
        Assert-OriginalEvidencePreserved
        Assert-CaptureFreeSpace
        $procDump = Get-ProcDump
        Test-ProcDumpCapture -ProcDump $procDump
        try { Start-MonitoredOpenMW -Exe $EnginePath -ProcDump $procDump } finally {
            $script:CopiedDumpBytes = 0L
            Save-OpenMWSnapshot -Label 'after' -Exe $EnginePath
            Export-RelevantEvents -Label 'events-after'
        }
    }
} catch {
    $failed = $true
    Add-CaptureNote ('FAILED: ' + $_.Exception.ToString())
    Write-Host ('Capture did not finish normally: ' + $_.Exception.Message) -ForegroundColor Red
} finally {
    try { Complete-Capture } catch {
        $failed = $true
        Write-Host ('Packaging failed: ' + $_.Exception.Message) -ForegroundColor Red
        Write-Host ('Unpacked evidence is still here: ' + $script:CaptureRoot)
    }
}
if ($failed) { exit 1 }
