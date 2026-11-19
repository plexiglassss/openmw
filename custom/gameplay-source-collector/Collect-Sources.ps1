#requires -Version 5.1
# OpenMW Gameplay Source Check v1.0. Read-only collection of a fixed source scope.
# Never launches the game, changes configuration, installs a mod, or reads saves/storage.
[CmdletBinding()]
param(
    [string]$LogPath = '',
    [string]$BackupRoot = '',
    [string]$OutputRoot = '',
    [string]$ReferencePath = '',
    [ValidateRange(0, 33554432)][long]$OptionalByteLimit = 33554432,
    [ValidateRange(1, 2048)][int]$OptionalFileLimit = 2048
)
Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'
$script:Utf8 = New-Object Text.UTF8Encoding($false)
$script:Warnings = New-Object 'System.Collections.Generic.List[string]'
$script:DataDirs = @()
$script:RunDir = $null
$started = [DateTime]::UtcNow

# These are the five files in Gameplay Responsiveness Fixes v1.0. Hashes identify
# the prepared originals/candidates; they do not assume that installation succeeded.
$required = @(
    [ordered]@{ component='proximity_tool'; path='scripts/proximityTool/ui/mainMenu.lua'; original_sha256='7581968841ee78296f4317cc5588d5e03512631d1228f6f59485a609f4fd005f'; candidate_sha256='74e5f3dad3503063223faedcad9ab1c7ed356997f9a22a60a82928328212f45b' },
    [ordered]@{ component='proximity_tool'; path='scripts/proximityTool/ui/retainedParts.lua'; original_sha256='51e5dfa15fc163106ff6f5b294a5ee28108eeaece9aa94380a75fa2fbde72f78'; candidate_sha256='d14c597f8e8c9970f8fe9167867d9802fb471648686eefd11c95a9ff1e70150d' },
    [ordered]@{ component='zhi'; path='scripts/ZerkishHotkeysImproved/zhi_hotbarhud.lua'; original_sha256='7a3a0564cbb0deafea74d8561e0f8af11375b2fa75485c52b38caaa0793b69a1'; candidate_sha256='cb7b369ee84c0941c8f0aaacd0474f6952cdb4f015341289bb36d3c4a3e151a9' },
    [ordered]@{ component='attend_me'; path='scripts/AttendMe/hud.lua'; original_sha256='e1bdbfc42302f396b8ab3a4a8d3cad5f1acb52ecd356b273a3058236da427531'; candidate_sha256='36ee215e37bcb3ab6ec13c91392bd08eab615ebb871ae6ecff091c5a0684893a' },
    [ordered]@{ component='gravity_enforcement_act'; path='scripts/gravity_enforcement_act/vendor.lua'; original_sha256='7d64c7caa08527db3f6d90aacd4b9be422779e55ad3a8e48f1b782d1e61a4adb'; candidate_sha256='d01d66fdb4d71430b37fb233bf476a5c4e1dfc34b1198ca4d52742a654c903e7' }
)
$moduleRoots = @('scripts/SkillPerks', 'scripts/SaneMagic')
$actorEntries = @('scripts/SkillPerks/npc.lua', 'scripts/SkillPerks/creature.lua',
    'scripts/SaneMagic/daesumm_pn.lua', 'scripts/SaneMagic/npc.lua', 'scripts/SaneMagic/daesumm_c.lua')
$extraOptional = @('scripts/dynamicsounds/creature.lua', 'scripts/cmc/creature.lua',
    'scripts/omw_achievements/handlers/actorhandler.lua', 'scripts/tr_spells/tractor.lua',
    'scripts/bufftimers/player.lua', 'scripts/hbfs/actor.lua')
$componentIds = @('proximity_tool', 'zhi', 'attend_me', 'gravity_enforcement_act')

function Write-Json([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, (ConvertTo-Json -InputObject $Value -Depth 24), $script:Utf8)
}
function Add-Warning([string]$Message) {
    $script:Warnings.Add($Message)
    Write-Warning $Message
}
function Get-Field($Value, [string]$Name, $Default = $null) {
    if ($null -eq $Value) { return $Default }
    if ($Value -is [Collections.IDictionary]) {
        if ($Value.Contains($Name)) { return $Value[$Name] }
    } else {
        $property = $Value.PSObject.Properties[$Name]
        if ($null -ne $property) { return $property.Value }
    }
    return $Default
}
function Get-TextSha([string]$Value) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($script:Utf8.GetBytes($Value)))).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}
function Get-FullLiteralPath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or -not [IO.Path]::IsPathRooted($Path) -or
        $Path -match '[\x00-\x1f*?"<>|]') { throw ('Invalid absolute filesystem path: ' + $Path) }
    return [IO.Path]::GetFullPath($Path)
}
function Get-PathKey([string]$Path) {
    return (Get-FullLiteralPath $Path).Replace('/', '\').TrimEnd([char]92).ToLowerInvariant()
}
function Join-Source([string]$Root, [string]$Relative) {
    if ($Relative -notmatch '^scripts/.+\.lua$' -or $Relative -match '[\x00-\x1f\\:*?"<>|]' -or
        $Relative -match '(^|/)\.\.?(/|$)' -or $Relative -match '//') { throw ('Invalid virtual script path: ' + $Relative) }
    $base = (Get-FullLiteralPath $Root).TrimEnd([char]92, [char]47)
    $path = [IO.Path]::GetFullPath((Join-Path $base $Relative.Replace('/', [IO.Path]::DirectorySeparatorChar)))
    if (-not $path.StartsWith(($base + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase)) {
        throw ('Virtual path escapes its data directory: ' + $Relative)
    }
    return $path
}
function Get-Metadata([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.PSIsContainer) { throw 'Expected a file.' }
    return [ordered]@{ path=$item.FullName; size_bytes=[long]$item.Length;
        last_write_utc=$item.LastWriteTimeUtc.ToString('o') }
}
function Get-BoundedIdentity([string]$Path, [long]$Limit) {
    $meta = Get-Metadata $Path
    if ($meta.size_bytes -gt $Limit) { throw 'File exceeds its byte limit.' }
    $stream = $null; $algorithm = $null
    try {
        $stream = [IO.File]::Open($meta.path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
            [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        $algorithm = [Security.Cryptography.SHA256]::Create()
        $buffer = New-Object byte[] 65536
        [long]$total = 0
        while (($count = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $total += $count
            if ($total -gt $Limit) { throw 'File grew beyond its byte limit.' }
            [void]$algorithm.TransformBlock($buffer, 0, $count, $buffer, 0)
        }
        [void]$algorithm.TransformFinalBlock([byte[]]@(), 0, 0)
        $meta.sha256 = ([BitConverter]::ToString($algorithm.Hash)).Replace('-', '').ToLowerInvariant()
        $meta.bytes_hashed = $total
        $meta.status = 'read'
        return $meta
    } finally {
        if ($algorithm) { $algorithm.Dispose() }
        if ($stream) { $stream.Dispose() }
    }
}
function Copy-Evidence([string]$Source, [string]$Relative, [long]$Limit) {
    $record = [ordered]@{ source_path=$Source; captured_path=$null; status='unavailable';
        before=$null; copied=$null; after=$null; error=$null }
    $inputStream = $null; $outputStream = $null
    $destination = Join-Path $script:RunDir $Relative.Replace('/', [IO.Path]::DirectorySeparatorChar)
    try {
        $metadata = Get-Metadata $Source
        if ($metadata.size_bytes -gt $Limit) {
            $record.status = 'oversize'; $record.before = $metadata; return $record
        }
        $record.before = Get-BoundedIdentity $Source $Limit
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
        $inputStream = [IO.File]::Open($Source, [IO.FileMode]::Open, [IO.FileAccess]::Read,
            [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        $outputStream = [IO.File]::Open($destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        $buffer = New-Object byte[] 65536
        [long]$total = 0
        while (($count = $inputStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $total += $count
            if ($total -gt $Limit) { throw 'File grew beyond its copy limit.' }
            $outputStream.Write($buffer, 0, $count)
        }
        $inputStream.Dispose(); $inputStream = $null
        $outputStream.Dispose(); $outputStream = $null
        $record.captured_path = $Relative
        $record.copied = Get-BoundedIdentity $destination $Limit
        $record.after = Get-BoundedIdentity $Source $Limit
        $stable = $record.before.sha256 -eq $record.copied.sha256 -and
            $record.copied.sha256 -eq $record.after.sha256 -and
            $record.before.size_bytes -eq $record.before.bytes_hashed -and
            $record.before.size_bytes -eq $record.copied.size_bytes -and
            $record.copied.size_bytes -eq $record.after.size_bytes -and
            $record.after.size_bytes -eq $record.after.bytes_hashed -and
            $record.before.last_write_utc -eq $record.after.last_write_utc
        $record.status = if ($stable) { 'copied' } else { 'unstable' }
    } catch {
        $record.error = $_.Exception.Message
        if ($null -ne $record.before) { $record.status = 'unstable' }
        if ([IO.File]::Exists($destination)) {
            # Only an incomplete file created in this new output directory is removed.
            if ($outputStream) { $outputStream.Dispose(); $outputStream = $null }
            [IO.File]::Delete($destination)
            $record.captured_path = $null
        }
    } finally {
        if ($inputStream) { $inputStream.Dispose() }
        if ($outputStream) { $outputStream.Dispose() }
    }
    return $record
}
function Get-LogContext([string]$Text) {
    $context = [ordered]@{ config_paths=@(); settings_paths=@(); shader_settings_paths=@();
        data_directories=@(); bsa_archives=@(); engine_revision=$null; engine_version=$null;
        rejected_paths=@(); startup_markers=0 }
    $rules = [ordered]@{
        'Loading config file:\s*(.+)$'='config_paths'
        'Loading settings file:\s*(.+)$'='settings_paths'
        'Loading shader settings file:\s*(.+)$'='shader_settings_paths'
        'Adding data directory\s+(.+)$'='data_directories'
        'Adding BSA archive\s+(.+)$'='bsa_archives'
    }
    foreach ($line in ($Text -split '\r?\n')) {
        foreach ($pattern in $rules.Keys) {
            if ($line -match $pattern) {
                $value = $Matches[1].Trim().Trim([char]34)
                try { [void](Get-FullLiteralPath $value); $context[$rules[$pattern]] += $value }
                catch { $context.rejected_paths += [ordered]@{kind=$rules[$pattern]; value=$value; error=$_.Exception.Message} }
                break
            }
        }
        if ($line -match '\bRevision:\s*(\S+)') { $context.engine_revision = $Matches[1] }
        if ($line -match '\bOpenMW version\s+(.+)$') {
            $context.engine_version = $Matches[1]; $context.startup_markers++
        }
    }
    foreach ($key in @('config_paths', 'settings_paths', 'shader_settings_paths')) {
        $context[$key] = @($context[$key] | Select-Object -Unique)
    }
    return $context
}
function Find-Winner([string]$Relative) {
    $errors = @()
    for ($index = $script:DataDirs.Count - 1; $index -ge 0; $index--) {
        try {
            $path = Join-Source $script:DataDirs[$index] $Relative
            if (Test-Path -LiteralPath $path -PathType Leaf -ErrorAction Stop) {
                return [ordered]@{ path=$path; data_directory=$script:DataDirs[$index];
                    data_directory_index=$index; preceding_check_errors=$errors }
            }
        } catch { $errors += [ordered]@{data_directory_index=$index; error=$_.Exception.Message} }
    }
    return [ordered]@{ path=$null; data_directory=$null; data_directory_index=$null; preceding_check_errors=$errors }
}
function Collect-Script([string]$Relative, [string]$Lane, [long]$Limit) {
    $provider = Find-Winner $Relative
    $record = [ordered]@{ path=$Relative; provider=$provider; status='missing_loose_provider';
        captured_path=$null; before=$null; copied=$null; after=$null; error=$null }
    if ($null -ne $provider.path) {
        $copy = Copy-Evidence $provider.path ($Lane + '/' + $Relative) $Limit
        foreach ($key in @('status', 'captured_path', 'before', 'copied', 'after', 'error')) { $record[$key] = $copy[$key] }
    }
    return $record
}
function Discover-OptionalSources {
    $paths = @{}; $issues = New-Object 'System.Collections.Generic.List[object]'
    [int]$entries = 0; [int]$folders = 0; $halted = $false
    for ($index = 0; $index -lt $script:DataDirs.Count -and -not $halted; $index++) {
        foreach ($virtualRoot in $moduleRoots) {
            $root = Join-Path $script:DataDirs[$index] $virtualRoot.Replace('/', [IO.Path]::DirectorySeparatorChar)
            try { if (-not (Test-Path -LiteralPath $root -PathType Container -ErrorAction Stop)) { continue } }
            catch { $issues.Add(@{status='directory_unavailable'; path=$root; error=$_.Exception.Message}); continue }
            $pending = New-Object 'System.Collections.Generic.Queue[object]'
            $pending.Enqueue([pscustomobject]@{path=$root; relative=$virtualRoot; depth=0})
            while ($pending.Count -gt 0 -and -not $halted) {
                $next = $pending.Dequeue(); $folders++
                if ($folders -gt 4096) { $issues.Add(@{status='directory_budget_exceeded'; limit=4096}); $halted=$true; break }
                try {
                    $directory = Get-Item -LiteralPath $next.path -Force
                    if (($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                        $issues.Add(@{status='reparse_directory_skipped'; path=$next.path}); continue
                    }
                    foreach ($childPath in [IO.Directory]::EnumerateFileSystemEntries($next.path)) {
                        $entries++
                        if ($entries -gt 20000) { $issues.Add(@{status='entry_budget_exceeded'; limit=20000}); $halted=$true; break }
                        $item = Get-Item -LiteralPath $childPath -Force -ErrorAction Stop
                        $relative = $next.relative + '/' + $item.Name
                        if ($item.PSIsContainer) {
                            if ($next.depth -ge 24) { $issues.Add(@{status='depth_limit'; path=$item.FullName}); continue }
                            $pending.Enqueue([pscustomobject]@{path=$item.FullName; relative=$relative; depth=($next.depth+1)})
                        } elseif ($item.Extension -ieq '.lua') {
                            try { [void](Join-Source $script:DataDirs[$index] $relative); $paths[$relative.ToLowerInvariant()]=$relative }
                            catch { $issues.Add(@{status='invalid_virtual_path'; path=$relative; error=$_.Exception.Message}) }
                        }
                    }
                } catch { $issues.Add(@{status='directory_unavailable'; path=$next.path; error=$_.Exception.Message}) }
            }
            if ($halted) { break }
        }
    }
    # The measured entry points come first and get a missing-provider record even
    # when no module folder was found. Other files are a case-insensitive union.
    $ordered = New-Object 'System.Collections.Generic.List[string]'; $seen = @{}
    foreach ($path in @($actorEntries) + @($paths.Values | Sort-Object) + @($extraOptional)) {
        if (-not $seen.ContainsKey($path.ToLowerInvariant())) { $ordered.Add($path); $seen[$path.ToLowerInvariant()]=$true }
    }
    return [ordered]@{paths=@($ordered.ToArray()); folders_visited=$folders; entries_visited=$entries;
        discovered_unique_files=$paths.Count; enumeration_complete=(-not $halted -and $issues.Count -eq 0); issues=@($issues.ToArray())}
}
function Collect-Receipt([string]$Component) {
    $record = [ordered]@{ component=$Component; status='missing_receipt'; source_path=$null;
        captured_path=$null; before=$null; copied=$null; after=$null; error=$null;
        parse_status='not_read'; receipt_state=$null; operation=$null; declared_files=@(); selection='latest receipt LastWriteTimeUtc, then path'; issues=@() }
    $root = Join-Path $BackupRoot $Component
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { return $record }
    try {
        $candidates = New-Object 'System.Collections.Generic.List[object]'
        $directories = @(Get-ChildItem -LiteralPath $root -Directory -Force | Where-Object { $_.Name -match '^\d{8}-\d{6}-\d{3}-[0-9a-f]+$' } | Sort-Object Name -Descending)
        if ($directories.Count -gt 4096) {
            $record.issues += 'Receipt directory count exceeds 4096; only the newest named directories were inspected.'
            $directories = @($directories | Select-Object -First 4096)
        }
        foreach ($directory in $directories) {
            if (($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { $record.issues += ('Reparse directory skipped: ' + $directory.Name); continue }
            $path = Join-Path $directory.FullName 'receipt.json'
            if (Test-Path -LiteralPath $path -PathType Leaf) { $candidates.Add((Get-Item -LiteralPath $path -Force)) }
        }
        if ($candidates.Count -eq 0) { return $record }
        $selected = $candidates | Sort-Object -Property LastWriteTimeUtc, FullName -Descending | Select-Object -First 1
        $copy = Copy-Evidence $selected.FullName ('receipts/' + $Component + '/receipt.json') 262144
        foreach ($key in @('source_path', 'status', 'captured_path', 'before', 'copied', 'after', 'error')) { $record[$key]=$copy[$key] }
        if ($copy.status -ne 'copied') { return $record }
        try {
            $parsed = [IO.File]::ReadAllText((Join-Path $script:RunDir $copy.captured_path), [Text.Encoding]::UTF8) | ConvertFrom-Json
            if ((Get-Field $parsed 'component') -ne $Component) { $record.parse_status='component_mismatch'; return $record }
            $state = Get-Field $parsed 'state'
            $operation = Get-Field $parsed 'operation'
            if ($state -notin @('prepared','installed','failed_rolled_back','rollback_incomplete','restored') -or $operation -notin @('install','restore')) {
                $record.parse_status='invalid_state'; return $record
            }
            $record.receipt_state = $state; $record.operation = $operation
            $record.parse_status = 'valid'
            foreach ($file in @(Get-Field $parsed 'files' @())) {
                # Report receipt claims only. Never open, copy or hash a receipt's target path.
                $record.declared_files += [ordered]@{path=(Get-Field $file 'path');
                    original_sha256=(Get-Field $file 'original_sha256'); installed_sha256=(Get-Field $file 'installed_sha256');
                    reported_target=(Get-Field $file 'target')}
            }
        } catch { $record.parse_status='malformed'; $record.error=$_.Exception.Message }
    } catch { $record.status='unavailable'; $record.error=$_.Exception.Message }
    return $record
}
function Compare-Reference($Reference, $Context, $Configs, [string]$LogSha) {
    $result = [ordered]@{status='reference_unavailable'; data_order_match=$false; content_config_paths_match=$false;
        captured_config_bytes_match=$false; engine_revision_match=$false; last_log_sha256_match=$false;
        config_checks=@(); note='Even a matching reference binds current copies to that captured context; it does not prove the bytes used during the earlier gameplay run.'}
    if ($null -eq $Reference) { return $result }
    try {
        $result.data_order_match = (Get-TextSha ($Context.data_directories -join "`n")) -eq (Get-Field $Reference 'data_order_sha256')
        $result.engine_revision_match = $Context.engine_revision -eq (Get-Field $Reference 'engine_revision')
        $result.last_log_sha256_match = $LogSha -eq (Get-Field $Reference 'log_sha256')
        $expectedPaths = @((Get-Field $Reference 'content_config_paths' @()) | ForEach-Object { Get-PathKey $_ })
        $actualPaths = @($Context.config_paths | ForEach-Object { Get-PathKey $_ })
        $result.content_config_paths_match = $expectedPaths.Count -gt 0 -and
            (($expectedPaths -join "`n") -ceq ($actualPaths -join "`n"))
        $allMatch = $true
        foreach ($expected in @(Get-Field $Reference 'config_files' @())) {
            $key = Get-PathKey (Get-Field $expected 'path')
            $actual = @($Configs | Where-Object { (Get-PathKey $_.source_path) -eq $key })
            $match = $actual.Count -eq 1 -and $actual[0].status -eq 'copied' -and
                (Get-Field $actual[0] 'stable_at_collection_end' $false) -and
                $actual[0].copied.sha256 -eq (Get-Field $expected 'sha256')
            $result.config_checks += [ordered]@{path=(Get-Field $expected 'path'); expected_sha256=(Get-Field $expected 'sha256'); matches=$match}
            if (-not $match) { $allMatch=$false }
        }
        $result.captured_config_bytes_match = $allMatch -and $result.config_checks.Count -gt 0
        $matches = $result.data_order_match -and $result.content_config_paths_match -and
            $result.captured_config_bytes_match -and $result.engine_revision_match
        $result.status = if ($matches) { 'captured_context_match' } else { 'captured_context_differs' }
    } catch { $result.status='reference_invalid'; $result.error=$_.Exception.Message }
    return $result
}

try {
    if ($env:OS -ne 'Windows_NT') { throw 'Run this source check on the Windows OpenMW computer.' }
    $documents = [Environment]::GetFolderPath('MyDocuments')
    if (-not $LogPath) { $LogPath = Join-Path $documents 'My Games\OpenMW\openmw.log' }
    if (-not $BackupRoot) { $BackupRoot = Join-Path $documents 'OpenMW Responsiveness Backups' }
    if (-not $OutputRoot) { $OutputRoot = Join-Path $documents 'OpenMW Responsiveness Captures\SourceChecks' }
    if (-not $ReferencePath) { $ReferencePath = Join-Path $PSScriptRoot 'Capture-Reference.json' }
    $LogPath = Get-FullLiteralPath $LogPath; $BackupRoot = Get-FullLiteralPath $BackupRoot
    $OutputRoot = Get-FullLiteralPath $OutputRoot; $ReferencePath = Get-FullLiteralPath $ReferencePath
    $stamp = (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
    $script:RunDir = Join-Path $OutputRoot ('SourceCheck-' + $stamp)
    [void][IO.Directory]::CreateDirectory($script:RunDir)
    $running = @(Get-Process -Name 'openmw', 'openmw-launcher' -ErrorAction SilentlyContinue | ForEach-Object {
        [ordered]@{name=$_.ProcessName; pid=$_.Id}
    })
    if ($running.Count -gt 0) { Add-Warning 'OpenMW or its launcher is running. Files may change; this collection cannot establish a settled post-run source state.' }
    Write-Host 'Collecting current source files. The game will not be launched or changed.'
    $logCopy = Copy-Evidence $LogPath 'logs/openmw.log' 33554432
    $context = Get-LogContext ''
    if ($logCopy.captured_path) {
        $context = Get-LogContext ([IO.File]::ReadAllText((Join-Path $script:RunDir $logCopy.captured_path), [Text.Encoding]::UTF8))
    } else { Add-Warning 'The log could not be copied. Missing last-log source resolution will be recorded.' }
    # A concatenated multi-startup log is ambiguous. Collect evidence, but do not
    # combine two different runs into a falsely asserted winning directory order.
    $ambiguousLog = $context.startup_markers -gt 1 -or $context.data_directories.Count -gt 20000
    if ($ambiguousLog) { Add-Warning 'The log contains multiple startups or too many data directories; source resolution was not attempted.' }
    if (-not $ambiguousLog) { $script:DataDirs = @($context.data_directories) }

    $requiredRecords = @()
    foreach ($target in $required) {
        $record = Collect-Script $target.path 'required' 2097152
        $record.component=$target.component; $record.original_sha256=$target.original_sha256; $record.candidate_sha256=$target.candidate_sha256
        $record.matches='unverified'
        if ($record.status -eq 'copied' -and $record.provider.preceding_check_errors.Count -eq 0) {
            $record.matches = if ($record.copied.sha256 -eq $target.candidate_sha256) { 'candidate' }
                elseif ($record.copied.sha256 -eq $target.original_sha256) { 'original' } else { 'other' }
        }
        $requiredRecords += $record
    }

    $configPaths = @($context.config_paths) + @($context.settings_paths) + @($context.shader_settings_paths)
    # Include the FG settings only beside an actually logged configuration path.
    foreach ($path in @($context.config_paths)) {
        $fgPath = Join-Path ([IO.Path]::GetDirectoryName($path)) 'openmw_fg.ini'
        if (Test-Path -LiteralPath $fgPath -PathType Leaf) { $configPaths += $fgPath }
    }
    $configRecords = @(); $configSeen = @{}
    foreach ($path in $configPaths) {
        $name = [IO.Path]::GetFileName($path)
        if ($name -notin @('openmw.cfg','settings.cfg','shaders.yaml','openmw_fg.ini')) { continue }
        $key = Get-PathKey $path
        if ($configSeen.ContainsKey($key)) { continue }
        $configSeen[$key]=$true
        if ($configRecords.Count -ge 32) { Add-Warning 'More than 32 configuration paths were logged; remaining copies were skipped.'; break }
        $record = Copy-Evidence $path ('config/{0:D2}_{1}' -f $configRecords.Count, $name) 4194304
        $configRecords += $record
    }

    $optionalScan = Discover-OptionalSources
    $optionalRecords = @(); [long]$optionalBytes = 0; [int]$optionalCopies = 0
    foreach ($path in $optionalScan.paths) {
        $provider = Find-Winner $path
        $record = [ordered]@{path=$path; provider=$provider; status='missing_loose_provider';
            captured_path=$null; before=$null; copied=$null; after=$null; error=$null}
        if ($null -ne $provider.path) {
            try {
                $meta = Get-Metadata $provider.path; $record.before=$meta
                if ($meta.size_bytes -gt 2097152) { $record.status='oversize' }
                elseif ($optionalCopies -ge $OptionalFileLimit -or $optionalBytes + $meta.size_bytes -gt $OptionalByteLimit) { $record.status='budget_exceeded' }
                else {
                    $remaining = [Math]::Min([long]2097152, ($OptionalByteLimit - $optionalBytes))
                    $copy = Copy-Evidence $provider.path ('optional/' + $path) $remaining
                    foreach ($key in @('status','captured_path','before','copied','after','error')) { $record[$key]=$copy[$key] }
                    if ($record.captured_path) {
                        $optionalBytes += (Get-Item -LiteralPath (Join-Path $script:RunDir $record.captured_path)).Length
                        $optionalCopies++
                    }
                }
            } catch { $record.status='unavailable'; $record.error=$_.Exception.Message }
        }
        $optionalRecords += $record
    }
    $optionalScan.Remove('paths')
    $optionalScan.bytes_copied=$optionalBytes; $optionalScan.files_copied=$optionalCopies
    $optionalScan.byte_limit=$OptionalByteLimit; $optionalScan.file_limit=$OptionalFileLimit
    $optionalScan.module_roots=$moduleRoots
    $optionalScan.additional_fixed_paths=$extraOptional

    $receiptRecords = @()
    foreach ($component in $componentIds) { $receiptRecords += Collect-Receipt $component }
    $reference = $null; $referenceCopy = $null
    if (Test-Path -LiteralPath $ReferencePath -PathType Leaf) {
        $referenceCopy = Copy-Evidence $ReferencePath 'Capture-Reference.json' 1048576
        if ($referenceCopy.status -eq 'copied') {
            try { $reference = [IO.File]::ReadAllText((Join-Path $script:RunDir $referenceCopy.captured_path), [Text.Encoding]::UTF8) | ConvertFrom-Json }
            catch { Add-Warning ('Capture reference could not be parsed: ' + $_.Exception.Message) }
        }
    }
    $logSha = if ($null -ne $logCopy.copied) { $logCopy.copied.sha256 } else { '' }
    foreach ($record in $configRecords) {
        $record.at_collection_end=$null; $record.stable_at_collection_end=$false
        if ($record.status -eq 'copied') {
            try {
                $record.at_collection_end = Get-BoundedIdentity $record.source_path 4194304
                $record.stable_at_collection_end = $record.at_collection_end.sha256 -eq $record.copied.sha256 -and
                    $record.at_collection_end.last_write_utc -eq $record.after.last_write_utc
            } catch { $record.end_check_error=$_.Exception.Message }
        }
    }
    $guard = Compare-Reference $reference $context $configRecords $logSha
    $logEnd = $null
    try { $logEnd = Get-BoundedIdentity $LogPath 33554432 } catch { Add-Warning ('Log end check failed: ' + $_.Exception.Message) }
    $logStable = $logCopy.status -eq 'copied' -and $null -ne $logEnd -and $logEnd.sha256 -eq $logSha
    $logAge = $null
    if ($null -ne $logCopy.before) { $logAge = ([DateTime]::UtcNow - [DateTime]::Parse($logCopy.before.last_write_utc)).TotalHours }
    $components = @()
    foreach ($component in $componentIds) {
        $files = @($requiredRecords | Where-Object {$_.component -eq $component})
        $classes = @($files | ForEach-Object {$_.matches} | Select-Object -Unique)
        $state = if ($classes -contains 'unverified') { 'incomplete' } elseif ($classes.Count -eq 1) { $classes[0] } else { 'mixed' }
        $components += [ordered]@{component=$component; current_file_classification=$state}
    }
    $collectorCopy = Copy-Evidence $PSCommandPath 'collector.ps1' 1048576
    $complete = @($requiredRecords | Where-Object {$_.status -ne 'copied' -or $_.provider.preceding_check_errors.Count -gt 0}).Count -eq 0 -and
        @($optionalRecords | Where-Object {$_.status -ne 'copied' -or $_.provider.preceding_check_errors.Count -gt 0}).Count -eq 0 -and
        $configRecords.Count -gt 0 -and @($configRecords | Where-Object {$_.status -ne 'copied' -or -not $_.stable_at_collection_end}).Count -eq 0 -and
        $optionalScan.enumeration_complete -and $logStable -and -not $ambiguousLog -and $context.rejected_paths.Count -eq 0 -and
        $script:DataDirs.Count -gt 0 -and $running.Count -eq 0 -and $guard.status -eq 'captured_context_match'
    $report = [ordered]@{
        format_version=1; collector='OpenMW Gameplay Source Check v1.0'; started_utc=$started.ToString('o'); finished_utc=[DateTime]::UtcNow.ToString('o')
        status=$(if ($complete) {'completed'} else {'completed_with_gaps'});
        completion_scope='Current source copies and matching captured context; not proof of earlier-run installed bytes or performance validation.'
        game_was_launched=$false; game_files_were_written=$false
        output_directory=$script:RunDir; collector_source=$collectorCopy; log=$logCopy; log_at_end=$logEnd; running_game_processes=$running
        last_log_provenance=[ordered]@{basis='last_logged_loose_provider'; log_age_hours=$logAge; log_stable_during_collection=$logStable;
            ambiguous_log=$ambiguousLog; data_order_sha256=(Get-TextSha ($context.data_directories -join "`n"));
            note='Provider paths use the last log directory order. Archives are not opened. Missing loose files may be archive-only or absent. Current copies do not prove the bytes loaded in an earlier session.'}
        reference_guard=$guard; reference=$referenceCopy; context=$context; config=$configRecords
        required_helpers=$requiredRecords; components=$components; optional_scan=$optionalScan; optional_sources=$optionalRecords; receipts=$receiptRecords
        limits=[ordered]@{required_files=5; required_bytes_per_file=2097152; optional_bytes_per_file=2097152; optional_total_bytes=$OptionalByteLimit;
            optional_files=$OptionalFileLimit; optional_directory_entries=20000; optional_directories=4096; optional_depth=24;
            log_bytes=33554432; config_bytes_per_file=4194304; config_files=32; receipt_bytes_per_file=262144; receipt_components=4}
        warnings=@($script:Warnings.ToArray())
    }
    $reportPath = Join-Path $script:RunDir 'Source-check.json'
    Write-Json $reportPath $report
    $summary = @(
        'OpenMW Gameplay Source Check v1.0',
        ('Collection time (UTC): ' + $report.finished_utc),
        'Source resolution: last logged loose provider; archives are not inspected.',
        ('Reference context: ' + $guard.status),
        'This is a current source snapshot, not proof of the files loaded in the earlier gameplay run.',
        ('Required helpers copied: ' + @($requiredRecords | Where-Object {$_.status -eq 'copied'}).Count + '/5'),
        ('Optional Lua files copied: ' + $optionalCopies + '; bytes: ' + $optionalBytes),
        'No game/configuration/mod file was changed. No saves, Lua storage or backup mod originals were copied.',
        'See Source-check.json for per-file hashes, missing files, receipt state and provenance limits.'
    ) -join "`r`n"
    [IO.File]::WriteAllText((Join-Path $script:RunDir 'Readme.txt'), $summary, $script:Utf8)
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zipPath = $script:RunDir + '.zip'
    [IO.Compression.ZipFile]::CreateFromDirectory($script:RunDir, $zipPath, [IO.Compression.CompressionLevel]::Optimal, $true)
    Write-Host ('SOURCE_CHECK_JSON=' + $reportPath)
    Write-Host ('SOURCE_CHECK_ZIP=' + $zipPath)
    Write-Host 'Finished. Upload the SourceCheck ZIP; no new gameplay recording is needed.'
    exit 0
} catch {
    $message = $_.Exception.Message
    if ($script:RunDir -and [IO.Directory]::Exists($script:RunDir)) {
        try { Write-Json (Join-Path $script:RunDir 'Collection-error.json') ([ordered]@{error=$message; started_utc=$started.ToString('o'); time_utc=[DateTime]::UtcNow.ToString('o')}) } catch { }
    }
    Write-Error ('Source check could not finish: ' + $message) -ErrorAction Continue
    exit 1
}
