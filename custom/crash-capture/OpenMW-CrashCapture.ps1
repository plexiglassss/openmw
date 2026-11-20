#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Collect', 'Run', 'Restore')][string]$Mode = 'Collect',
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
    if ($env:LOCALAPPDATA) { $dirs.Add((Join-Path $env:LOCALAPPDATA 'CrashDumps')); $dirs.Add((Get-PassiveDumpFolder)) }
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
        note = 'A minimum headroom check, not an upper bound on dump or ZIP size. New Windows crash dumps are retained in full.'
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

. (Join-Path $PSScriptRoot 'Passive-Capture.ps1')

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
if ($Mode -eq 'Restore') {
    try { Invoke-WerPermissionHelper -Action Restore; exit 0 } catch { Write-Host $_.Exception.Message -ForegroundColor Red; exit 1 }
}
if ($Mode -eq 'Run' -and (Test-CaptureAdministrator)) { throw 'Run the capture launcher normally, not as administrator. Only the separate setup prompt needs administrator permission.' }
$sessionMutex = New-Object Threading.Mutex($false, 'Local\OpenMWCrashCapture-Session-v1')
$sessionOwned = $false
try { $sessionOwned = $sessionMutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $sessionOwned = $true }
if (-not $sessionOwned) { $sessionMutex.Dispose(); throw 'Another OpenMW capture session is already running.' }
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
        tool_version = '1.4'; mode = $Mode; local_time = (Get-Date).ToString('o'); utc_time = [DateTime]::UtcNow.ToString('o');
        timezone = [TimeZoneInfo]::Local.Id; os = [Environment]::OSVersion.VersionString; powershell = $PSVersionTable.PSVersion.ToString();
        engine_selected = $EnginePath; environment = $environmentRows;
        limitations = 'Only documented OpenMW locations and relevant Windows events are searched. No mod conflict diagnosis or live gameplay acceptance is implied. DMPs may contain game memory and local paths; nothing is uploaded automatically.'
    })
    Write-Host 'OpenMW Crash Capture v1.4 - passive Windows collection'
    Write-Host 'Collecting existing OpenMW logs, crash dumps and Windows crash events...'
    Save-OpenMWSnapshot -Label 'before' -Exe $EnginePath
    Export-RelevantEvents -Label 'events-before'
    Save-EngineIdentity -Exe $EnginePath
    if ($Mode -eq 'Run') {
        Assert-OriginalEvidencePreserved
        Assert-CaptureFreeSpace
        $dumpFolder = Get-PassiveDumpFolder
        New-Item -ItemType Directory -Path $dumpFolder -Force | Out-Null
        Assert-WerDumpsPreserved -Folder $dumpFolder
        $dumpDrive = New-Object IO.DriveInfo ([IO.Path]::GetPathRoot($dumpFolder))
        if ($dumpDrive.AvailableFreeSpace -lt 4GB) { throw 'Less than 4 GiB is available on the Windows dump drive. Free some space before capture.' }
        if (-not (Test-WerConfigured -Folder $dumpFolder)) { Invoke-WerPermissionHelper -Action Enable -Folder $dumpFolder }
        if (-not (Test-WerConfigured -Folder $dumpFolder)) { throw 'Windows capture configuration read-back failed. OpenMW was not launched.' }
        Write-Host 'Checking crash-only collection with a small synthetic test (the game has not started yet)...'
        Test-PassiveCapture -Folder $dumpFolder
        try { Start-PassiveOpenMW -Exe $EnginePath -Folder $dumpFolder } finally {
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
if ($sessionOwned) { $sessionMutex.ReleaseMutex() }; $sessionMutex.Dispose()
if ($failed) { exit 1 }
