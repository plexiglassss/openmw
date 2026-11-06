# Native Windows PowerShell 5.1 tests for the authored source collector.
# Fixtures contain only newly authored dummy bytes, never game/mod sources.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$CollectorScript,
    [Parameter(Mandatory = $true)][string]$TestRoot,
    [Parameter(Mandatory = $true)][string]$ReportPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$utf8 = New-Object System.Text.UTF8Encoding($false)
$checks = New-Object System.Collections.Generic.List[object]
$archiveChecks = New-Object System.Collections.Generic.List[object]
$caseNumber = 0
$invokeNumber = 0
$sessionRoot = Join-Path ([IO.Path]::GetFullPath($TestRoot)) ('session-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($sessionRoot)
$CollectorScript = [IO.Path]::GetFullPath($CollectorScript)
$WrapperScript = Join-Path ([IO.Path]::GetDirectoryName($CollectorScript)) 'COLLECT_SOURCES.cmd'
$powershellExe = Join-Path $PSHOME 'powershell.exe'
$requiredPaths = @(
    'scripts/proximityTool/ui/mainMenu.lua',
    'scripts/proximityTool/ui/retainedParts.lua',
    'scripts/ZerkishHotkeysImproved/zhi_hotbarhud.lua',
    'scripts/AttendMe/hud.lua',
    'scripts/gravity_enforcement_act/vendor.lua'
)
$componentIds = @('proximity_tool', 'zhi', 'attend_me', 'gravity_enforcement_act')

function Assert-That([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Write-Text([string]$Path, [string]$Value) {
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))
    [IO.File]::WriteAllText($Path, $Value, $utf8)
}
function Write-Json([string]$Path, $Value) {
    Write-Text $Path ($Value | ConvertTo-Json -Depth 30)
}
function Get-Sha([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Get-InputState([string]$Path) {
    $state = @{}
    foreach ($file in @(Get-ChildItem -LiteralPath $Path -File -Recurse)) {
        $relative = $file.FullName.Substring($Path.Length).TrimStart('\')
        $state[$relative] = [ordered]@{sha256 = Get-Sha $file.FullName; length = $file.Length; last_write_ticks = $file.LastWriteTimeUtc.Ticks}
    }
    return $state
}
function Assert-Unchanged([string]$Path, $Before) {
    $after = Get-InputState $Path
    Assert-That ($after.Count -eq $Before.Count) 'Collector added or removed an input file.'
    foreach ($key in $Before.Keys) {
        Assert-That ($after.ContainsKey($key)) ('Input disappeared: ' + $key)
        Assert-That ($after[$key].sha256 -eq $Before[$key].sha256) ('Input bytes changed: ' + $key)
        Assert-That ($after[$key].last_write_ticks -eq $Before[$key].last_write_ticks) ('Input write timestamp changed: ' + $key)
    }
}
function ConvertTo-NativeArgument([string]$Value) {
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}
function Invoke-Script($Fixture, [string[]]$Parameters) {
    $script:invokeNumber++
    $arguments = @('-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $CollectorScript) + $Parameters
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $powershellExe
    $start.Arguments = (($arguments | ForEach-Object { ConvertTo-NativeArgument $_ }) -join ' ')
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $start
    [void]$process.Start()
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(60000)) {
        $process.Kill()
        throw 'Isolated collector test child exceeded 60 seconds.'
    }
    $stdout = $stdoutTask.Result
    $stderr = $stderrTask.Result
    $exitCode = $process.ExitCode
    $process.Dispose()
    $prefix = Join-Path $Fixture.root ('invoke-{0:D2}' -f $script:invokeNumber)
    Write-Text ($prefix + '.stdout.txt') $stdout
    Write-Text ($prefix + '.stderr.txt') $stderr
    Write-Json ($prefix + '.json') @{arguments = $arguments; exit_code = $exitCode}
    return [pscustomobject]@{code = $exitCode; stdout = $stdout; stderr = $stderr; output_root = $Fixture.output}
}
function Assert-Exit($Result, [int]$Expected) {
    Assert-That ($Result.code -eq $Expected) ("Expected exit $Expected, got $($Result.code).`n$($Result.stdout)`n$($Result.stderr)")
}
function Check([string]$Name, [scriptblock]$Body) {
    try {
        & $Body
        $checks.Add([ordered]@{name = $Name; passed = $true})
        Write-Host ('PASS: ' + $Name)
    }
    catch {
        $checks.Add([ordered]@{name = $Name; passed = $false; error = $_.Exception.Message; location = $_.ScriptStackTrace})
        Write-Host ('FAIL: ' + $Name + ': ' + $_.Exception.Message)
    }
}

function New-Fixture([string]$Name) {
    $script:caseNumber++
    $root = Join-Path $sessionRoot ('{0:D2}-{1}' -f $script:caseNumber, $Name)
    $unicode = 'Unicode-' + [char]0x00E9 + [char]0x96EA
    $inputRoot = Join-Path $root 'Inputs [fixture]'
    $providers = @((Join-Path $inputRoot 'Provider first'), (Join-Path $inputRoot ('Provider [last] ' + $unicode)))
    $configs = @((Join-Path $inputRoot 'Engine config\openmw.cfg'), (Join-Path $inputRoot ('User config ' + $unicode + '\openmw.cfg')))
    Write-Text $configs[0] "# Authored collector config fixture`r`nencoding=win1252`r`n"
    Write-Text $configs[1] ("# Authored collector config fixture`r`n" + 'data="' + $providers[1] + '"' + "`r`n")
    foreach ($relative in $requiredPaths) {
        Write-Text (Join-Path $providers[0] $relative) ("-- Authored collector earlier fixture: $relative`nreturn { fixture = 'earlier' }`n")
        Write-Text (Join-Path $providers[1] $relative) ("-- Authored collector latest fixture: $relative`nreturn { fixture = 'latest' }`n")
    }
    $backup = Join-Path $inputRoot 'Backups [fixture]'
    foreach ($id in $componentIds) {
        $receipt = Join-Path $backup ($id + '\20260101-120000-000-11111111\receipt.json')
        Write-Json $receipt @{schema_version=1; component=$id; state='installed'; operation='install'; files=@(); fixture='old'}
        [IO.File]::SetLastWriteTimeUtc($receipt, [datetime]'2026-01-01T12:00:00Z')
        $receipt = Join-Path $backup ($id + '\20260102-120000-000-22222222\receipt.json')
        Write-Json $receipt @{schema_version=1; component=$id; state='installed'; operation='install'; files=@(); fixture='latest'}
        [IO.File]::SetLastWriteTimeUtc($receipt, [datetime]'2026-01-02T12:00:00Z')
    }
    $lines = @()
    foreach ($config in $configs) { $lines += ('[12:00:00.000 *] Loading config file: ' + $config) }
    $lines += '[12:00:00.001 I] OpenMW version 0.52.0'
    $lines += '[12:00:00.002 I] Revision: 0123456789'
    foreach ($provider in $providers) { $lines += ('[12:00:00.003 I] Adding data directory ' + $provider) }
    $log = Join-Path $inputRoot 'Logs [fixture]\openmw.log'
    Write-Text $log ($lines -join "`r`n")
    return [pscustomobject]@{
        root=$root; inputs=$inputRoot; providers=$providers; configs=$configs; backup=$backup
        log=$log; lines=$lines; output=(Join-Path $root ('Output [fixture] ' + $unicode))
    }
}
function Invoke-Collector($Fixture, [string[]]$Extra = @()) {
    return Invoke-Script $Fixture (@('-LogPath', $Fixture.log, '-BackupRoot', $Fixture.backup,
        '-OutputRoot', $Fixture.output) + $Extra)
}
function Read-Capture($Result) {
    Assert-Exit $Result 0
    $jsonMatch = [regex]::Match($Result.stdout, '(?m)^SOURCE_CHECK_JSON=(.+)\r?$')
    $zipMatch = [regex]::Match($Result.stdout, '(?m)^SOURCE_CHECK_ZIP=(.+)\r?$')
    Assert-That $jsonMatch.Success ('Missing SOURCE_CHECK_JSON output. ' + $Result.stdout)
    Assert-That $zipMatch.Success 'Missing SOURCE_CHECK_ZIP output.'
    # Windows PowerShell 5.1's redirected legacy console encoding cannot round-
    # trip every Unicode path. Retain the unique ASCII run name from both output
    # markers, and resolve it under the exact literal OutputRoot passed above.
    $name = [regex]::Match($jsonMatch.Groups[1].Value.TrimEnd("`r"), '(SourceCheck-\d{8}-\d{6}-\d{3}-[0-9a-f]{8})[\\/]Source-check\.json$')
    Assert-That $name.Success 'JSON marker lacks the unique source-check run name.'
    Assert-That ($zipMatch.Groups[1].Value.TrimEnd("`r").EndsWith($name.Groups[1].Value + '.zip', [StringComparison]::Ordinal)) 'ZIP/JSON stdout markers identify different runs.'
    $folder = Join-Path $Result.output_root $name.Groups[1].Value
    $jsonPath = Join-Path $folder 'Source-check.json'
    $zipPath = $folder + '.zip'
    Assert-That (Test-Path -LiteralPath $jsonPath -PathType Leaf) 'Source-check JSON is absent.'
    Assert-That (Test-Path -LiteralPath $zipPath -PathType Leaf) 'Source-check ZIP is absent.'
    $document = Get-Content -LiteralPath $jsonPath -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert-That ($document.output_directory -eq $folder) 'The copied JSON does not identify the exact requested literal output directory.'
    return [pscustomobject]@{json=$document; json_path=$jsonPath; zip_path=$zipPath; folder=[IO.Path]::GetDirectoryName($jsonPath)}
}
function Assert-CapturedHashes($Capture, $Rows) {
    foreach ($row in @($Rows | Where-Object { $_.status -eq 'copied' })) {
        $actual = Join-Path $Capture.folder $row.captured_path
        Assert-That (Test-Path -LiteralPath $actual -PathType Leaf) ('Copied file absent: ' + $row.captured_path)
        $hash = Get-Sha $actual
        Assert-That ($hash -eq $row.before.sha256 -and $hash -eq $row.copied.sha256 -and $hash -eq $row.after.sha256) ('Snapshot hashes differ: ' + $row.path)
        Assert-That ((Get-Item -LiteralPath $actual).Length -eq $row.copied.size_bytes) ('Snapshot size differs: ' + $row.path)
    }
}
function Assert-ZipMatches($Capture, $Rows) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($Capture.zip_path)
    try {
        $relativeFiles = @('Source-check.json') + @($Rows | Where-Object { $_.status -eq 'copied' } | ForEach-Object { $_.captured_path })
        foreach ($relative in $relativeFiles) {
            $normal = $relative.Replace('\', '/')
            $entries = @($zip.Entries | Where-Object { $entryName = $_.FullName.Replace('\', '/'); $entryName -eq $normal -or $entryName.EndsWith('/' + $normal, [StringComparison]::Ordinal) })
            Assert-That ($entries.Count -eq 1) ('Expected one ZIP entry for ' + $relative + '; actual names: ' + (($zip.Entries | ForEach-Object { $_.FullName }) -join ', '))
            $stream = $entries[0].Open()
            $sha = [Security.Cryptography.SHA256]::Create()
            try { $hash = ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
            finally { $sha.Dispose(); $stream.Dispose() }
            Assert-That ($hash -eq (Get-Sha (Join-Path $Capture.folder $relative))) ('ZIP byte mismatch for ' + $relative)
        }
        $archiveChecks.Add([ordered]@{zip_name=[IO.Path]::GetFileName($Capture.zip_path); entries_checked=$relativeFiles.Count;
            total_entries=$zip.Entries.Count; backslash_entry_count=@($zip.Entries | Where-Object { $_.FullName.Contains('\') }).Count})
    }
    finally { $zip.Dispose() }
}

Check 'Native Windows PowerShell 5.1 and collector AST' {
    Assert-That ($env:OS -eq 'Windows_NT') 'This harness requires native Windows.'
    Assert-That ($PSVersionTable.PSVersion.Major -eq 5 -and $PSVersionTable.PSVersion.Minor -eq 1) 'This run must use Windows PowerShell 5.1.'
    $tokens = $null; $errors = $null
    [void][Management.Automation.Language.Parser]::ParseFile($CollectorScript, [ref]$tokens, [ref]$errors)
    Assert-That (@($errors).Count -eq 0) (($errors | Out-String))
}
Check 'Required helpers use last logged loose provider and preserve literal Unicode inputs' {
    $f = New-Fixture 'literal-and-vfs'
    $before = Get-InputState $f.inputs
    $capture = Read-Capture (Invoke-Collector $f)
    $rows = @($capture.json.required_helpers)
    Assert-That ($rows.Count -eq 5) 'Exactly five required helpers were not recorded.'
    foreach ($relative in $requiredPaths) {
        $row = @($rows | Where-Object { $_.path -ieq $relative })
        Assert-That ($row.Count -eq 1) ('Required helper missing or duplicated: ' + $relative)
        Assert-That ($row[0].status -eq 'copied') ('Required helper not copied: ' + $relative)
        Assert-That ([IO.Path]::GetFullPath($row[0].provider.path) -eq [IO.Path]::GetFullPath((Join-Path $f.providers[1] $relative))) ('Wrong VFS winner: ' + $relative)
        Assert-That ($row[0].provider.data_directory_index -eq 1) 'Provider index is not last logged index.'
        Assert-That ($row[0].matches -eq 'other') 'Authored dummy was incorrectly classified as a real mod revision.'
    }
    Assert-CapturedHashes $capture $rows
    Assert-ZipMatches $capture $rows
    Assert-Unchanged $f.inputs $before
}
Check 'Earlier loose provider remains eligible when the later provider lacks a helper' {
    $f = New-Fixture 'fallback-provider'
    Remove-Item -LiteralPath (Join-Path $f.providers[1] $requiredPaths[0])
    $before = Get-InputState $f.inputs
    $capture = Read-Capture (Invoke-Collector $f)
    $row = @($capture.json.required_helpers | Where-Object { $_.path -ieq $requiredPaths[0] })[0]
    Assert-That ($row.status -eq 'copied' -and $row.provider.data_directory_index -eq 0) 'Earlier loose provider was not selected.'
    Assert-CapturedHashes $capture $capture.json.required_helpers
    Assert-Unchanged $f.inputs $before
}
Check 'Missing required provider is explicit and does not discard the other four helpers' {
    $f = New-Fixture 'missing-required'
    foreach ($provider in $f.providers) { Remove-Item -LiteralPath (Join-Path $provider $requiredPaths[0]) }
    $before = Get-InputState $f.inputs
    $capture = Read-Capture (Invoke-Collector $f)
    $rows = @($capture.json.required_helpers)
    $missing = @($rows | Where-Object { $_.path -ieq $requiredPaths[0] })[0]
    Assert-That ($rows.Count -eq 5 -and $missing.status -eq 'missing_loose_provider') 'Missing helper status was not preserved.'
    Assert-That (@($rows | Where-Object { $_.status -eq 'copied' }).Count -eq 4) 'Unaffected required helpers were dropped.'
    Assert-CapturedHashes $capture $rows
    Assert-Unchanged $f.inputs $before
}
Check 'Oversized required helper reports a gap without replacing its VFS winner' {
    $f = New-Fixture 'oversize-required'
    [IO.File]::WriteAllBytes((Join-Path $f.providers[1] $requiredPaths[0]), (New-Object byte[] (2 * 1024 * 1024 + 1)))
    $before = Get-InputState $f.inputs
    $capture = Read-Capture (Invoke-Collector $f)
    $rows = @($capture.json.required_helpers)
    $row = @($rows | Where-Object { $_.path -ieq $requiredPaths[0] })[0]
    Assert-That ($row.status -eq 'oversize' -and $row.provider.data_directory_index -eq 1) 'Oversized winning helper was silently replaced or not reported.'
    Assert-That (@($rows | Where-Object { $_.status -eq 'copied' }).Count -eq 4) 'Unaffected required helpers were dropped.'
    Assert-Unchanged $f.inputs $before
}
Check 'Locked required helper reports unavailable without replacing its VFS winner' {
    $f = New-Fixture 'locked-required'
    $before = Get-InputState $f.inputs
    $held = [IO.File]::Open((Join-Path $f.providers[1] $requiredPaths[0]), [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try { $capture = Read-Capture (Invoke-Collector $f) }
    finally { $held.Dispose() }
    $row = @($capture.json.required_helpers | Where-Object { $_.path -ieq $requiredPaths[0] })[0]
    Assert-That ($row.status -eq 'unavailable' -and $row.provider.data_directory_index -eq 1) 'Locked winning helper was silently replaced or not reported.'
    Assert-That (@($capture.json.required_helpers | Where-Object { $_.status -eq 'copied' }).Count -eq 4) 'Unaffected required helpers were dropped.'
    Assert-Unchanged $f.inputs $before
}

function Add-OptionalFixtures($Fixture) {
    $paths = @('scripts/SkillPerks/npc.lua', 'scripts/SkillPerks/creature.lua',
        'scripts/SkillPerks/nested/shared.lua', 'scripts/SaneMagic/daesumm_pn.lua',
        'scripts/SaneMagic/npc.lua', 'scripts/SaneMagic/daesumm_c.lua', 'scripts/SaneMagic/nested/magic.lua',
        'scripts/dynamicsounds/creature.lua', 'scripts/cmc/creature.lua',
        'scripts/omw_achievements/handlers/actorhandler.lua', 'scripts/tr_spells/tractor.lua',
        'scripts/bufftimers/player.lua', 'scripts/hbfs/actor.lua')
    foreach ($relative in $paths) {
        Write-Text (Join-Path $Fixture.providers[0] $relative) ("-- Authored optional earlier fixture: $relative`nreturn { fixture = 'earlier' }`n")
        Write-Text (Join-Path $Fixture.providers[1] $relative) ("-- Authored optional latest fixture: $relative`nreturn { fixture = 'latest' }`n")
    }
    Write-Text (Join-Path $Fixture.providers[1] 'scripts/UnrelatedFixture/do-not-copy.lua') "-- Unrelated authored fixture; outside the permitted module scope.`n"
    Write-Text (Join-Path $Fixture.providers[1] 'scripts/SkillPerks/do-not-copy.txt') "Authored text fixture; excluded extension.`n"
    return $paths
}
function Write-Reference($Fixture) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $orderSha = ([BitConverter]::ToString($sha.ComputeHash($utf8.GetBytes(($Fixture.providers -join "`n"))))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
    $reference = [ordered]@{
        engine_revision='0123456789'; log_sha256=Get-Sha $Fixture.log; data_order_sha256=$orderSha
        content_config_paths=$Fixture.configs
        config_files=@($Fixture.configs | ForEach-Object { @{path=$_; sha256=Get-Sha $_} })
    }
    $path = Join-Path $Fixture.inputs 'Reference [fixture].json'
    Write-Json $path $reference
    return $path
}
function Invoke-Wrapper($Fixture) {
    $script:invokeNumber++
    $package = Join-Path $Fixture.root ('Launcher [fixture] ' + [char]0x00E9 + [char]0x96EA)
    [void][IO.Directory]::CreateDirectory($package)
    $wrapper = Join-Path $package 'COLLECT_SOURCES.cmd'
    [IO.File]::Copy($WrapperScript, $wrapper, $true)
    [IO.File]::Copy($CollectorScript, (Join-Path $package 'Collect-Sources.ps1'), $true)
    $command = @($wrapper, '-LogPath', $Fixture.log, '-BackupRoot', $Fixture.backup, '-OutputRoot', $Fixture.output)
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $env:ComSpec
    $start.Arguments = '/d /s /c "' + (($command | ForEach-Object { ConvertTo-NativeArgument $_ }) -join ' ') + '"'
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true; $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $start
    [void]$process.Start()
    $process.StandardInput.Close()
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(60000)) { $process.Kill(); throw 'CMD wrapper child exceeded 60 seconds.' }
    $stdout = $stdoutTask.Result; $stderr = $stderrTask.Result; $exitCode = $process.ExitCode
    $process.Dispose()
    $prefix = Join-Path $Fixture.root ('invoke-{0:D2}' -f $script:invokeNumber)
    Write-Text ($prefix + '.stdout.txt') $stdout
    Write-Text ($prefix + '.stderr.txt') $stderr
    Write-Json ($prefix + '.json') @{arguments=$command; exit_code=$exitCode; via='cmd.exe'}
    return [pscustomobject]@{code=$exitCode; stdout=$stdout; stderr=$stderr; output_root=$Fixture.output}
}
Check 'Optional module union and six fixed actor entries select current loose winners' {
    $f = New-Fixture 'optional-union'
    $expected = @(Add-OptionalFixtures $f)
    $reference = Write-Reference $f
    $before = Get-InputState $f.inputs
    $capture = Read-Capture (Invoke-Collector $f @('-ReferencePath', $reference))
    $rows = @($capture.json.optional_sources)
    Assert-That ($rows.Count -eq $expected.Count) 'Optional selection escaped or omitted the fixed/module source scope.'
    foreach ($path in $expected) {
        $row = @($rows | Where-Object { $_.path -ieq $path })
        Assert-That ($row.Count -eq 1 -and $row[0].status -eq 'copied') ('Missing/duplicate optional source: ' + $path)
        Assert-That ($row[0].provider.data_directory_index -eq 1) ('Wrong optional VFS winner: ' + $path)
    }
    Assert-That ($capture.json.optional_scan.files_copied -eq $expected.Count) 'Optional copy count differs.'
    Assert-That ($capture.json.status -eq 'completed') 'Complete stable requested source scope was not reported complete.'
    Assert-That ($capture.json.collector_source.copied.sha256 -eq (Get-Sha $CollectorScript)) 'Collector did not bind its own executed source bytes.'
    Assert-CapturedHashes $capture $rows
    Assert-ZipMatches $capture ($rows + @($capture.json.required_helpers))
    Assert-Unchanged $f.inputs $before
}
Check 'Zero optional byte budget preserves all five required helper copies' {
    $f = New-Fixture 'optional-byte-budget'
    [void](Add-OptionalFixtures $f)
    $before = Get-InputState $f.inputs
    $capture = Read-Capture (Invoke-Collector $f @('-OptionalByteLimit', '0'))
    Assert-That (@($capture.json.required_helpers | Where-Object { $_.status -eq 'copied' }).Count -eq 5) 'Optional budget discarded required helpers.'
    Assert-That (@($capture.json.optional_sources | Where-Object { $_.status -ne 'budget_exceeded' }).Count -eq 0) 'Optional byte budget was not explicit for every present source.'
    Assert-That ($capture.json.optional_scan.bytes_copied -eq 0 -and $capture.json.optional_scan.files_copied -eq 0) 'Zero optional budget still copied optional source bytes.'
    Assert-CapturedHashes $capture $capture.json.required_helpers
    Assert-Unchanged $f.inputs $before
}
Check 'Optional file limit preserves required files and prioritises measured actor entry points' {
    $f = New-Fixture 'optional-file-budget'
    [void](Add-OptionalFixtures $f)
    $before = Get-InputState $f.inputs
    $capture = Read-Capture (Invoke-Collector $f @('-OptionalFileLimit', '1'))
    Assert-That (@($capture.json.required_helpers | Where-Object { $_.status -eq 'copied' }).Count -eq 5) 'Optional file budget discarded required helpers.'
    $copied = @($capture.json.optional_sources | Where-Object { $_.status -eq 'copied' })
    Assert-That ($copied.Count -eq 1 -and $copied[0].path -ieq 'scripts/SkillPerks/npc.lua') 'Actor entry point was not first within the optional budget.'
    Assert-That (@($capture.json.optional_sources | Where-Object { $_.status -eq 'budget_exceeded' }).Count -eq 12) 'Optional file budget gaps were not recorded.'
    Assert-Unchanged $f.inputs $before
}
Check 'Latest receipt per fixed component is captured as data with no target traversal' {
    $f = New-Fixture 'receipt-selection'
    $secret = Join-Path $f.inputs 'Unrelated private fixture\never-capture-me.lua'
    Write-Text $secret '-- A unique authored sentinel outside the permitted source scope.'
    $receipt = Join-Path $f.backup 'proximity_tool\20260102-120000-000-22222222\receipt.json'
    Write-Json $receipt @{component='proximity_tool'; state='installed'; operation='install'; fixture='latest';
        files=@(@{path='../outside.lua'; target=$secret; original_sha256='not-a-real-hash'; installed_sha256='not-a-real-hash'})}
    $before = Get-InputState $f.inputs
    $capture = Read-Capture (Invoke-Collector $f)
    $rows = @($capture.json.receipts)
    Assert-That ($rows.Count -eq 4) 'Receipt collection did not preserve four fixed components.'
    foreach ($row in $rows) {
        Assert-That ($row.status -eq 'copied' -and $row.parse_status -eq 'valid') ('Receipt was not captured as valid data: ' + $row.component)
        $raw = Get-Content -LiteralPath (Join-Path $capture.folder $row.captured_path) -Raw -Encoding UTF8 | ConvertFrom-Json
        Assert-That ($raw.fixture -eq 'latest') 'Older receipt was selected.'
    }
    $captured = @(Get-ChildItem -LiteralPath $capture.folder -Recurse -File)
    Assert-That (@($captured | Where-Object { $_.Name -eq 'never-capture-me.lua' -or $_.Name -eq 'outside.lua' }).Count -eq 0) 'Receipt target was followed into collected files.'
    Assert-ZipMatches $capture $rows
    Assert-Unchanged $f.inputs $before
}
Check 'Malformed latest receipt remains captured with parse failure rather than falling back' {
    $f = New-Fixture 'malformed-receipt'
    $receipt = Join-Path $f.backup 'proximity_tool\20260102-120000-000-22222222\receipt.json'
    Write-Text $receipt '{ this is an intentionally malformed authored receipt'
    $before = Get-InputState $f.inputs
    $capture = Read-Capture (Invoke-Collector $f)
    $row = @($capture.json.receipts | Where-Object { $_.component -eq 'proximity_tool' })[0]
    Assert-That ($row.status -eq 'copied' -and $row.parse_status -eq 'malformed') 'Malformed latest receipt was not preserved and identified.'
    Assert-That ((Get-Sha (Join-Path $capture.folder $row.captured_path)) -eq (Get-Sha $receipt)) 'Malformed latest receipt was replaced with an older record.'
    Assert-Unchanged $f.inputs $before
}
Check 'Missing and semantically invalid receipts remain explicit independent gaps' {
    $f = New-Fixture 'invalid-receipts'
    Remove-Item -LiteralPath (Join-Path $f.backup 'attend_me') -Recurse
    Write-Json (Join-Path $f.backup 'zhi\20260102-120000-000-22222222\receipt.json') @{component='wrong-component'; state='installed'; operation='install'}
    Write-Json (Join-Path $f.backup 'gravity_enforcement_act\20260102-120000-000-22222222\receipt.json') @{component='gravity_enforcement_act'; state='invented-state'; operation='install'}
    $before = Get-InputState $f.inputs
    $capture = Read-Capture (Invoke-Collector $f)
    Assert-That ((@($capture.json.receipts | Where-Object { $_.component -eq 'attend_me' })[0]).status -eq 'missing_receipt') 'Absent component receipt was not recorded.'
    Assert-That ((@($capture.json.receipts | Where-Object { $_.component -eq 'zhi' })[0]).parse_status -eq 'component_mismatch') 'Receipt component mismatch was not recorded.'
    Assert-That ((@($capture.json.receipts | Where-Object { $_.component -eq 'gravity_enforcement_act' })[0]).parse_status -eq 'invalid_state') 'Receipt state mismatch was not recorded.'
    Assert-Unchanged $f.inputs $before
}
Check 'Matching reference binds configuration bytes, paths, directory order and logged revision' {
    $f = New-Fixture 'reference-match'
    $reference = Write-Reference $f
    $before = Get-InputState $f.inputs
    $capture = Read-Capture (Invoke-Collector $f @('-ReferencePath', $reference))
    $guard = $capture.json.reference_guard
    Assert-That ($guard.status -eq 'captured_context_match' -and $guard.last_log_sha256_match) 'Matching reference was not recognised.'
    Assert-That ($guard.data_order_match -and $guard.content_config_paths_match -and $guard.captured_config_bytes_match -and $guard.engine_revision_match) 'One or more provenance dimensions were omitted.'
    Assert-That ($capture.json.last_log_provenance.log_stable_during_collection) 'Unchanging log was not recognised as stable.'
    Assert-Unchanged $f.inputs $before
}
Check 'Reference drift is reported while source evidence remains available' {
    $f = New-Fixture 'reference-drift'
    $reference = Write-Reference $f
    Write-Text $f.configs[1] "# Authored changed configuration fixture.`n"
    $before = Get-InputState $f.inputs
    $capture = Read-Capture (Invoke-Collector $f @('-ReferencePath', $reference))
    $guard = $capture.json.reference_guard
    Assert-That ($guard.status -eq 'captured_context_differs' -and -not $guard.captured_config_bytes_match) 'Changed configuration was not reported.'
    Assert-That (@($capture.json.required_helpers | Where-Object { $_.status -eq 'copied' }).Count -eq 5) 'Reference mismatch discarded current source evidence.'
    Assert-Unchanged $f.inputs $before
}
Check 'Missing logged configuration prevents a complete status while retaining all source evidence' {
    $f = New-Fixture 'missing-config'
    [void](Add-OptionalFixtures $f)
    $reference = Write-Reference $f
    Remove-Item -LiteralPath $f.configs[0]
    $before = Get-InputState $f.inputs
    $capture = Read-Capture (Invoke-Collector $f @('-ReferencePath', $reference))
    Assert-That ($capture.json.status -eq 'completed_with_gaps') 'Missing logged configuration was incorrectly reported complete.'
    $row = @($capture.json.config | Where-Object { $_.source_path -eq $f.configs[0] })[0]
    Assert-That ($row.status -eq 'unavailable') 'Missing logged configuration evidence was omitted.'
    Assert-That (@($capture.json.required_helpers | Where-Object { $_.status -eq 'copied' }).Count -eq 5) 'Configuration gap discarded required helpers.'
    Assert-Unchanged $f.inputs $before
}
Check 'Running game-name fixture is observed without being stopped or claiming settled completion' {
    $f = New-Fixture 'running-process'
    [void](Add-OptionalFixtures $f)
    $reference = Write-Reference $f
    $stub = Join-Path $f.inputs 'Authored process fixture\openmw.exe'
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($stub))
    # Add-Type resolves its output path using wildcard semantics. Compile into a
    # simple staging path and then copy literally into the adversarial fixture.
    $stagedStub = Join-Path $sessionRoot 'AuthoredProcessStub.exe'
    Add-Type -TypeDefinition 'public static class SourceCollectorFixtureWait { public static void Main() { System.Threading.Thread.Sleep(120000); } }' -Language CSharp -OutputAssembly $stagedStub -OutputType ConsoleApplication
    [IO.File]::Copy($stagedStub, $stub, $false)
    $before = Get-InputState $f.inputs
    $process = New-Object Diagnostics.Process
    $process.StartInfo.FileName = $stub
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    [void]$process.Start()
    try {
        $capture = Read-Capture (Invoke-Collector $f @('-ReferencePath', $reference))
        Assert-That (-not $process.HasExited) 'Collector stopped the already-running process.'
        Assert-That ($capture.json.status -eq 'completed_with_gaps') 'Running-process source state was incorrectly reported settled.'
        Assert-That (@($capture.json.running_game_processes | Where-Object { $_.pid -eq $process.Id }).Count -eq 1) 'Running fixture PID was not recorded.'
    }
    finally { if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }; $process.Dispose() }
    Assert-Unchanged $f.inputs $before
}
Check 'Multiple-startup log is recorded as ambiguous and does not resolve a mixed winner' {
    $f = New-Fixture 'ambiguous-log'
    Write-Text $f.log (($f.lines + '[13:00:00.000 I] OpenMW version 0.52.0' + ('[13:00:00.001 I] Adding data directory ' + $f.providers[0])) -join "`r`n")
    $before = Get-InputState $f.inputs
    $capture = Read-Capture (Invoke-Collector $f)
    Assert-That $capture.json.last_log_provenance.ambiguous_log 'Concatenated startup log was not recognised.'
    Assert-That (@($capture.json.required_helpers | Where-Object { $_.status -eq 'copied' }).Count -eq 0) 'Ambiguous log was used to claim current source winners.'
    Assert-Unchanged $f.inputs $before
}
Check 'Repeated collection creates distinct outputs without overwriting earlier evidence' {
    $f = New-Fixture 'unique-output'
    $before = Get-InputState $f.inputs
    $first = Read-Capture (Invoke-Collector $f)
    $firstHash = Get-Sha $first.zip_path
    $second = Read-Capture (Invoke-Collector $f)
    Assert-That ($first.folder -ne $second.folder -and $first.zip_path -ne $second.zip_path) 'Repeated collection reused an output path.'
    Assert-That ((Get-Sha $first.zip_path) -eq $firstHash) 'Repeated collection changed an earlier evidence ZIP.'
    Assert-Unchanged $f.inputs $before
}
Check 'CMD launcher forwards literal Unicode paths and returns success' {
    $f = New-Fixture 'wrapper-success'
    $before = Get-InputState $f.inputs
    $capture = Read-Capture (Invoke-Wrapper $f)
    Assert-That (@($capture.json.required_helpers | Where-Object { $_.status -eq 'copied' }).Count -eq 5) 'CMD launcher did not pass explicit source/output arguments.'
    Assert-Unchanged $f.inputs $before
}
Check 'CMD launcher propagates fatal output error without modifying source inputs' {
    $f = New-Fixture 'wrapper-failure'
    Write-Text $f.output 'Authored obstruction: the requested output root is a file.'
    $before = Get-InputState $f.inputs
    $obstructionHash = Get-Sha $f.output
    Assert-Exit (Invoke-Wrapper $f) 1
    Assert-That ((Get-Sha $f.output) -eq $obstructionHash) 'Collector changed the obstructing output file.'
    Assert-Unchanged $f.inputs $before
}

$passed = @($checks | Where-Object { $_.passed }).Count
$failed = @($checks | Where-Object { -not $_.passed }).Count
$report = [ordered]@{
    schema_version=1
    purpose='Native Windows PowerShell 5.1 read-only collector verification with authored dummy inputs only'
    utc=[datetime]::UtcNow.ToString('o')
    github_sha=$env:GITHUB_SHA
    github_run_id=$env:GITHUB_RUN_ID
    os=$env:OS
    powershell_version=$PSVersionTable.PSVersion.ToString()
    collector_sha256=Get-Sha $CollectorScript
    wrapper_sha256=Get-Sha $WrapperScript
    test_sha256=Get-Sha $PSCommandPath
    total=$checks.Count
    passed=$passed
    failed=$failed
    checks=@($checks.ToArray())
    archive_checks=@($archiveChecks.ToArray())
}
Write-Json ([IO.Path]::GetFullPath($ReportPath)) $report
Write-Host ("Collector checks: $passed passed; $failed failed.")
if ($failed -gt 0) { exit 1 }
exit 0
