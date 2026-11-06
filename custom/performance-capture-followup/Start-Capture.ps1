#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$OpenMWExe,
    [string]$UserConfigDir,
    [string]$OutputRoot,
    [string]$RunLabel = 'baseline',
    [ValidateSet('Detailed', 'FrameOnly')][string]$Mode = 'Detailed',
    [ValidateSet('Unknown', 'On', 'Off')][string]$DeclaredFG = 'Unknown',
    [string[]]$GameArguments = @(),
    [switch]$NoGpu,
    [switch]$NoPrompt,
    [switch]$HelpersOnly
)

# The collector changes only three environment variables in this PowerShell
# process and its child game. It never edits installed files or game settings.
$ErrorActionPreference = 'Stop'
$script:Utf8 = New-Object System.Text.UTF8Encoding($false)
$script:Warnings = New-Object 'System.Collections.Generic.List[string]'
$script:Snapshots = New-Object 'System.Collections.Generic.List[object]'

function Write-JsonFile([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, (ConvertTo-Json -InputObject $Value -Depth 16), $script:Utf8)
}
function Add-CaptureWarning([string]$Text) {
    $script:Warnings.Add($Text)
    Write-Warning $Text
}
function Invoke-OptionalCaptureStage([string]$Name, [scriptblock]$Action) {
    try { & $Action }
    catch { Add-CaptureWarning ($Name+' did not complete: '+$_.Exception.Message+'; collected raw evidence will still be packaged.') }
}
function Get-PathComparisonKey([string]$Path) {
    try { return [IO.Path]::GetFullPath($Path).Replace('/','\').TrimEnd([char]92) }
    catch { return $Path.Replace('/','\').TrimEnd([char]92) }
}
function ConvertTo-WindowsArgument([AllowEmptyString()][string]$Value) {
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $b = New-Object Text.StringBuilder
    [void]$b.Append('"')
    $slashes = 0
    foreach ($c in $Value.ToCharArray()) {
        if ($c -eq [char]92) { $slashes++; continue }
        if ($c -eq [char]34) {
            [void]$b.Append([char]92, (2 * $slashes + 1))
            [void]$b.Append([char]34)
        } else {
            [void]$b.Append([char]92, $slashes)
            [void]$b.Append($c)
        }
        $slashes = 0
    }
    [void]$b.Append([char]92, (2 * $slashes))
    [void]$b.Append('"')
    return $b.ToString()
}
function ConvertTo-CsvCell($Value) {
    return '"' + ([string]$Value).Replace('"', '""') + '"'
}
function Read-SharedText([string]$Path) {
    $stream = $null; $reader = $null
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
            [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        $reader = New-Object IO.StreamReader($stream, [Text.Encoding]::UTF8, $true)
        return $reader.ReadToEnd()
    } finally {
        if ($reader) { $reader.Dispose() } elseif ($stream) { $stream.Dispose() }
    }
}
function Get-LogContext([string]$Text) {
    $out = [ordered]@{
        config_paths = @(); settings_paths = @(); shader_settings_paths = @()
        logs_dirs = @(); user_data_dirs = @(); data_directories = @()
        bsa_archives = @(); registered_scripts = @(); skipped_config_messages = @(); stats_output_paths = @()
        engine_revision = $null; engine_version = $null
    }
    $rules = [ordered]@{
        'Loading config file:\s*(.+)$' = 'config_paths'
        'Loading settings file:\s*(.+)$' = 'settings_paths'
        'Loading shader settings file:\s*(.+)$' = 'shader_settings_paths'
        'Logs dir:\s*(.+)$' = 'logs_dirs'
        'User data dir:\s*(.+)$' = 'user_data_dirs'
        'Adding data directory\s+(.+)$' = 'data_directories'
        'Adding BSA archive\s+(.+)$' = 'bsa_archives'
    }
    foreach ($line in ($Text -split '\r?\n')) {
        foreach ($pattern in $rules.Keys) {
            if ($line -match $pattern) {
                $value = $Matches[1].Trim().Trim([char]34)
                $out[$rules[$pattern]] += $value
                break
            }
        }
        if ($line -match '\bRevision:\s*(\S+)') { $out.engine_revision = $Matches[1] }
        if ($line -match '\bOpenMW version\s+(.+)$') { $out.engine_version = $Matches[1] }
        if ($line -match 'OSG stats will be written to:\s*(.+)$') {
            $value = $Matches[1].Trim()
            if ($value.StartsWith('"') -and $value.EndsWith('"')) {
                $value = $value.Substring(1,$value.Length-2)
                $value = [regex]::Replace($value, '\\(["\\])', '$1')
            }
            $out.stats_output_paths += $value
        }
        if ($line -match '#\d+\s+[^:]+:\s*(scripts/.+?\.lua)\s*$') {
            $out.registered_scripts += $Matches[1]
        }
        if ($line -match 'Skipping previous configs except') { $out.skipped_config_messages += $line }
    }
    foreach ($key in @('config_paths','settings_paths','shader_settings_paths','logs_dirs','user_data_dirs',
            'registered_scripts','stats_output_paths')) {
        $out[$key] = @($out[$key] | Select-Object -Unique)
    }
    return [pscustomobject]$out
}
function Get-FileIdentity([string]$Path, [switch]$MetadataOnly) {
    try {
        $item = Get-Item -LiteralPath $Path -ErrorAction Stop
        if ($item.PSIsContainer) { throw 'Expected a file.' }
        $sha = $null
        if (!$MetadataOnly) { $sha = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
        return [pscustomobject]@{ path=$item.FullName; size_bytes=$item.Length;
            last_write_utc=$item.LastWriteTimeUtc.ToString('o'); sha256=$sha; status='read' }
    } catch {
        return [pscustomobject]@{ path=$Path; status='unavailable'; error=$_.Exception.Message }
    }
}
function Save-ConfigSnapshot([string[]]$Paths, [string]$Phase, [string]$RunDir) {
    $destDir = Join-Path $RunDir ('config\' + $Phase)
    [void][IO.Directory]::CreateDirectory($destDir)
    foreach ($path in @($Paths | Where-Object { $_ } | Select-Object -Unique)) {
        # Copy text configuration only. defaults.bin, DLLs, saves and Lua storage
        # are identity/size records, never copied by this collector.
        $name = [IO.Path]::GetFileName($path)
        if ($name -notin @('openmw.cfg','settings.cfg','shaders.yaml','openmw_fg.ini')) { continue }
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        try {
            $source = Get-Item -LiteralPath $path
            if ($source.Length -gt 16MB) { throw 'Configuration exceeds the 16 MiB copy limit.' }
            $relative = 'config/' + $Phase + '/' + ('{0:D4}_{1}' -f $script:Snapshots.Count, $name)
            $dest = Join-Path $RunDir ($relative.Replace('/', [IO.Path]::DirectorySeparatorChar))
            Copy-Item -LiteralPath $path -Destination $dest
            $identity = Get-FileIdentity $dest
            $script:Snapshots.Add([pscustomobject]@{ phase=$Phase; source_path=$source.FullName;
                captured_path=$relative; source_last_write_utc=$source.LastWriteTimeUtc.ToString('o');
                sha256=$identity.sha256; size_bytes=$identity.size_bytes; status='copied' })
        } catch {
            $script:Snapshots.Add([pscustomobject]@{ phase=$Phase; source_path=$path;
                status='failed'; error=$_.Exception.Message })
            Add-CaptureWarning ('Cannot copy configuration: ' + $path + ': ' + $_.Exception.Message)
        }
    }
}
function Get-ConfigCandidates($Context, [string]$ExeDir, [string]$ConfigDir) {
    $paths = @()
    foreach ($dir in @($ExeDir, $ConfigDir)) {
        if ($dir) { foreach ($name in @('openmw.cfg','settings.cfg','shaders.yaml','openmw_fg.ini')) {
            $paths += Join-Path $dir $name
        } }
    }
    if ($Context) {
        $paths += @($Context.config_paths) + @($Context.settings_paths) + @($Context.shader_settings_paths)
        foreach ($path in @($Context.config_paths)) {
            $dir = Split-Path -Parent $path
            foreach ($name in @('settings.cfg','shaders.yaml')) { $paths += Join-Path $dir $name }
        }
    }
    return @($paths | Where-Object { $_ } | Select-Object -Unique)
}
function Resolve-GameExecutable([string]$Requested, [string]$ConfigDir, [switch]$NonInteractive) {
    if ($Requested) {
        if (Test-Path -LiteralPath $Requested -PathType Container) { $Requested = Join-Path $Requested 'openmw.exe' }
        $found = (Resolve-Path -LiteralPath $Requested -ErrorAction Stop).Path
        if ([IO.Path]::GetFileName($found) -ine 'openmw.exe') { throw 'Select openmw.exe, not the launcher.' }
        return $found
    }
    $candidates = @(Join-Path $PSScriptRoot 'openmw.exe')
    $oldLog = Join-Path $ConfigDir 'openmw.log'
    if (Test-Path -LiteralPath $oldLog -PathType Leaf) {
        try {
            $context = Get-LogContext (Read-SharedText $oldLog)
            foreach ($cfg in @($context.config_paths)) { $candidates += Join-Path (Split-Path -Parent $cfg) 'openmw.exe' }
        } catch { }
    }
    # Observed in the user's 4 November 18:26 launch log. A missing path is not
    # created; the user selects the current executable if none of these exists.
    $candidates += 'F:\Modding\OpenMW 0.52 - latest custom build\openmw.exe'
    foreach ($path in @($candidates | Select-Object -Unique)) {
        if (Test-Path -LiteralPath $path -PathType Leaf) { return (Resolve-Path -LiteralPath $path).Path }
    }
    if ($NonInteractive) { throw 'No OpenMW installation found. Pass -OpenMWExe with the current executable path.' }
    Add-Type -AssemblyName System.Windows.Forms
    $picker = New-Object Windows.Forms.OpenFileDialog
    $picker.Title = 'Select your current OpenMW engine (openmw.exe)'
    $picker.Filter = 'OpenMW engine (openmw.exe)|openmw.exe'
    try {
        if ($picker.ShowDialog() -ne [Windows.Forms.DialogResult]::OK) { throw 'No OpenMW executable selected.' }
        return $picker.FileName
    } finally { $picker.Dispose() }
}
function Initialize-CaptureNative {
    if ('OpenMWCaptureNative' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class OpenMWCaptureNative {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)] public static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int key);
    public static uint ForegroundProcess() {
        uint p; GetWindowThreadProcessId(GetForegroundWindow(), out p); return p;
    }
    public static bool KeyDown(int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; }
}
'@
}
function Find-NvidiaSmi {
    $command = Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    foreach ($path in @((Join-Path $env:WINDIR 'System32\nvidia-smi.exe'),
            (Join-Path $env:ProgramFiles 'NVIDIA Corporation\NVSMI\nvidia-smi.exe'))) {
        if (Test-Path -LiteralPath $path -PathType Leaf) { return $path }
    }
    return $null
}
function Get-TopTimedScripts([string]$TimingPath) {
    $result=[ordered]@{ status='file_missing'; rows_read=0; top_scripts=@(); basis='Cumulative Lua callback self_us; elapsed wall time, not CPU time. Ranking is a source-collection aid, not root-cause attribution.' }
    if (!(Test-Path -LiteralPath $TimingPath -PathType Leaf)) { return [pscustomobject]$result }
    if ((Get-Item -LiteralPath $TimingPath).Length -gt 256MB) {
        $result.status='file_over_256MiB_budget'; return [pscustomobject]$result
    }
    $parser=$null; $watch=[Diagnostics.Stopwatch]::StartNew(); $totals=@{}
    try {
        Add-Type -AssemblyName Microsoft.VisualBasic
        $parser=New-Object Microsoft.VisualBasic.FileIO.TextFieldParser($TimingPath)
        $parser.SetDelimiters(','); $parser.HasFieldsEnclosedInQuotes=$true
        $header=$parser.ReadFields()
        if (($header -join ',') -ne 'thread,frame,category,source,callback,calls,total_us,self_us,max_us,first_us,last_us') {
            throw 'Unexpected timing CSV header.'
        }
        $result.status='complete'
        while (!$parser.EndOfData) {
            if (($result.rows_read % 256) -eq 0 -and $watch.Elapsed.TotalSeconds -gt 15) { $result.status='time_budget_reached'; break }
            $fields=$parser.ReadFields(); $result.rows_read++
            if ($fields.Count -ne 11) { throw 'Unexpected timing CSV column count.' }
            if ($fields[2] -ne 'lua' -or $fields[3] -notmatch '^scripts/.+\.lua$') { continue }
            $path=$fields[3]
            if ($path -match '(^|/)\.\.(/|$)|[:\\]') { continue }
            $value=0.0
            if ([double]::TryParse($fields[7],[Globalization.NumberStyles]::Float,[cultureinfo]::InvariantCulture,[ref]$value) -and $value -ge 0 -and ![double]::IsInfinity($value)) {
                if (!$totals.ContainsKey($path)) { $totals[$path]=0.0 }
                $totals[$path]+=$value
            }
        }
        $result.top_scripts=@($totals.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 16 | ForEach-Object { [pscustomobject]@{vfs_path=$_.Key; self_us=$_.Value} })
    } catch { $result.status='parse_failed'; $result.error=$_.Exception.Message }
    finally { if ($parser) { $parser.Dispose() } }
    $result.elapsed_seconds=$watch.Elapsed.TotalSeconds
    return [pscustomobject]$result
}
function Get-TargetedScriptIdentities($Context, [string]$RunDir) {
    $timed = Get-TopTimedScripts (Join-Path $RunDir 'script-timing.csv')
    Write-JsonFile (Join-Path $RunDir 'script-source-selection.json') $timed
    $targets = @(
        'scripts/quest_guider_lite/player.lua', 'scripts/quest_guider_lite/main.lua',
        'scripts/quest_guider_lite/initializerfrommenu.lua',
        'scripts/advanced_world_map/player.lua', 'scripts/advanced_world_map/global.lua',
        'scripts/advanced_world_map/datarebuildservice.lua', 'scripts/advanced_world_map_tracking/player.lua',
        'scripts/sunsdusk/sd_p.lua', 'scripts/sunsdusk/sd_g.lua', 'scripts/sunsdusk/sharedray_v1.lua',
        'scripts/hudmarkers/hudm_p.lua', 'scripts/hudmarkers/hudm_g.lua',
        'scripts/craftingframework/cf_p.lua', 'scripts/craftingframework/cf_g.lua',
        'scripts/inventoryextender/player.lua', 'scripts/inventoryextender/api.lua',
        'scripts/omw/settings/player.lua', 'scripts/omw/settings/global.lua',
        'scripts/heft/player.lua', 'scripts/armsarmorcompare/ie_tooltip.lua',
        'scripts/inventoryextender/ui/windows/inventory.lua', 'scripts/inventoryextender/ui/templates/base.lua',
        'scripts/craftingframework/parserecipes.lua', 'scripts/omw/settings/menu.lua', 'scripts/omw/settings/common.lua',
        'scripts/advanced_world_map/mapdatahandler.lua', 'scripts/completionist/main.lua',
        'scripts/dynamicsounds/global.lua', 'scripts/npcpersonality/npc.lua',
        'scripts/dynamicactors/global.lua', 'scripts/dynamicactors/player.lua'
    )
    $targets=@(@($timed.top_scripts | ForEach-Object { $_.vfs_path }) + $targets | Select-Object -Unique)
    $rows = @(); $copiedBytes=0L; $watch = [Diagnostics.Stopwatch]::StartNew()
    [void][IO.Directory]::CreateDirectory((Join-Path $RunDir 'targeted-scripts'))
    $dirs = @($Context.data_directories)
    foreach ($relative in $targets) {
        $record = [ordered]@{ vfs_path=$relative; status='no_loose_provider_found'; provider=$null; identity=$null }
        for ($i=$dirs.Count-1; $i -ge 0; $i--) {
            if ($watch.Elapsed.TotalSeconds -gt 30) { $record.status='lookup_time_budget_reached'; break }
            $path = Join-Path $dirs[$i] ($relative.Replace('/', [IO.Path]::DirectorySeparatorChar))
            if (Test-Path -LiteralPath $path -PathType Leaf) {
                $record.status = 'last_logged_loose_provider'
                $record.provider = $dirs[$i]
                $record.identity = Get-FileIdentity $path
                $record.copied_path=$null
                if ($record.identity.status -eq 'read' -and $record.identity.size_bytes -le 2MB -and ($copiedBytes+$record.identity.size_bytes) -le 32MB) {
                    $copyName='targeted-scripts/{0:D3}_{1}' -f $rows.Count,[IO.Path]::GetFileName($path)
                    $copyPath=Join-Path $RunDir $copyName
                    Copy-Item -LiteralPath $path -Destination $copyPath
                    $copyId=Get-FileIdentity $copyPath
                    $record.copied_path=$copyName; $record.copied_sha256=$copyId.sha256
                    $record.source_and_copy_match=($record.identity.sha256 -eq $copyId.sha256)
                    $copiedBytes+=$copyId.size_bytes
                }
                break
            }
        }
        $rows += [pscustomobject]$record
    }
    Write-JsonFile (Join-Path $RunDir 'targeted-script-identities.json') ([ordered]@{
        method='Read after game exit; reverse the data-directory registration order from this run. BSA files are registered before loose files by this engine. No archive contents are parsed.'
        limits='Targeted sample, not a full VFS audit. Missing loose providers may be supplied by archives. Post-run hashes cannot prove files were unchanged during play.'
        elapsed_seconds=$watch.Elapsed.TotalSeconds; copied_bytes=$copiedBytes; targets=$rows
    })
}
#region Fixed follow-up evidence
# Fixed follow-up evidence is collected only after the game exits. It has its
# own per-file bounds; timing-ranked selection and its budgets are unchanged.
function Get-FollowupMetadata([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.PSIsContainer) { throw 'Expected a file.' }
    return [ordered]@{ path=$item.FullName; size_bytes=[long]$item.Length;
        last_write_utc=$item.LastWriteTimeUtc.ToString('o') }
}
function Get-FollowupIdentity([string]$Path, [long]$Limit) {
    $meta = Get-FollowupMetadata $Path
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
        return $meta
    } finally {
        if ($algorithm) { $algorithm.Dispose() }
        if ($stream) { $stream.Dispose() }
    }
}
function Copy-FollowupEvidence([string]$Source, [string]$Relative, [long]$Limit, [string]$RunDir) {
    $record = [ordered]@{ source_path=$Source; captured_path=$null; status='unavailable';
        before=$null; copied=$null; after=$null; error=$null }
    $inputStream = $null; $outputStream = $null
    $destination = Join-Path $RunDir $Relative.Replace('/', [IO.Path]::DirectorySeparatorChar)
    try {
        $metadata = Get-FollowupMetadata $Source
        if ($metadata.size_bytes -gt $Limit) {
            $record.status='oversize'; $record.before=$metadata; return $record
        }
        $record.before = Get-FollowupIdentity $Source $Limit
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
        $inputStream.Dispose(); $inputStream=$null
        $outputStream.Dispose(); $outputStream=$null
        $record.captured_path=$Relative
        $record.copied=Get-FollowupIdentity $destination $Limit
        $record.after=Get-FollowupIdentity $Source $Limit
        $stable = $record.before.sha256 -eq $record.copied.sha256 -and
            $record.copied.sha256 -eq $record.after.sha256 -and
            $record.before.size_bytes -eq $record.before.bytes_hashed -and
            $record.before.size_bytes -eq $record.copied.size_bytes -and
            $record.copied.size_bytes -eq $record.after.size_bytes -and
            $record.after.size_bytes -eq $record.after.bytes_hashed -and
            $record.before.last_write_utc -eq $record.after.last_write_utc
        $record.status=if ($stable) { 'copied' } else { 'unstable' }
    } catch {
        $record.error=$_.Exception.Message
        if ($null -ne $record.before) { $record.status='unstable' }
        if ([IO.File]::Exists($destination)) {
            if ($outputStream) { $outputStream.Dispose(); $outputStream=$null }
            [IO.File]::Delete($destination)
            $record.captured_path=$null
        }
    } finally {
        if ($inputStream) { $inputStream.Dispose() }
        if ($outputStream) { $outputStream.Dispose() }
    }
    return $record
}
function Get-FixedFollowupSources($Context, [string]$RunDir) {
    $targets = @(
        'scripts/proximityTool/ui/mainMenu.lua', 'scripts/proximityTool/ui/retainedParts.lua',
        'scripts/ZerkishHotkeysImproved/zhi_hotbarhud.lua', 'scripts/AttendMe/hud.lua',
        'scripts/gravity_enforcement_act/vendor.lua',
        'scripts/SkillPerks/shared/stagger.lua', 'scripts/SkillPerks/shared/timed_effects.lua',
        'scripts/SkillPerks/shared/magic_target.lua',
        'scripts/SaneMagic/npc.lua', 'scripts/SaneMagic/daesumm_pn.lua', 'scripts/SaneMagic/daesumm_c.lua',
        'scripts/dynamicsounds/creature.lua', 'scripts/cmc/creature.lua',
        'scripts/omw_achievements/handlers/actorhandler.lua', 'scripts/tr_spells/tractor.lua',
        'scripts/bufftimers/player.lua', 'scripts/hbfs/actor.lua', 'scripts/makemorvisag/player.lua'
    )
    $rows=@(); $dirs=@($Context.data_directories)
    foreach ($relative in $targets) {
        $record=[ordered]@{ vfs_path=$relative; provider=$null; status='missing_loose_provider';
            source_path=$null; captured_path=$null; before=$null; copied=$null; after=$null; error=$null }
        for ($i=$dirs.Count-1; $i -ge 0; $i--) {
            try {
                $path=Join-Path $dirs[$i] $relative.Replace('/', [IO.Path]::DirectorySeparatorChar)
                if (!(Test-Path -LiteralPath $path -PathType Leaf -ErrorAction Stop)) { continue }
                $record.provider=[ordered]@{ path=$path; data_directory=$dirs[$i]; data_directory_index=$i }
                $copy=Copy-FollowupEvidence $path ('fixed-scripts/'+$relative) 2097152 $RunDir
                foreach ($key in $copy.Keys) { $record[$key]=$copy[$key] }
            } catch { $record.status='unavailable'; $record.error=$_.Exception.Message }
            # An inaccessible/oversize last provider must not silently fall back.
            break
        }
        $rows += [pscustomobject]$record
    }
    Write-JsonFile (Join-Path $RunDir 'fixed-script-identities.json') ([ordered]@{
        method='Fixed post-exit follow-up paths; last logged loose provider wins. Independent of timing-ranked source selection.'
        limits='18 fixed files, 2 MiB each (36 MiB maximum). No archives are parsed. Post-run hashes do not prove files were unchanged during play.'
        data_directory_count=$dirs.Count; targets=$rows
    })
}
function Get-FixedInstallReceipts([string]$RunDir, [string]$BackupRoot) {
    if (!$BackupRoot) { $BackupRoot=Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'OpenMW Responsiveness Backups' }
    $rows=@()
    foreach ($component in @('skillperks_actor','sane_magic_actor','attend_me','proximity_tool','zhi','gravity_enforcement_act')) {
        $record=[ordered]@{ component=$component; status='missing_receipt'; source_path=$null;
            captured_path=$null; before=$null; copied=$null; after=$null; error=$null;
            parse_status='not_read'; receipt_state=$null; operation=$null; issues=@() }
        try {
            $root=Join-Path $BackupRoot $component
            if (Test-Path -LiteralPath $root -PathType Container -ErrorAction Stop) {
                if (((Get-Item -LiteralPath $root -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Reparse component directory skipped.' }
                $directories=@(Get-ChildItem -LiteralPath $root -Directory -Force | Where-Object { $_.Name -match '^\d{8}-\d{6}-\d{3}-[0-9a-f]+$' } | Sort-Object Name -Descending)
                if ($directories.Count -gt 4096) {
                    $record.issues += 'Only the newest 4096 named receipt directories were inspected.'
                    $directories=@($directories | Select-Object -First 4096)
                }
                $candidates=@()
                foreach ($directory in $directories) {
                    if (($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { continue }
                    $path=Join-Path $directory.FullName 'receipt.json'
                    if (!(Test-Path -LiteralPath $path -PathType Leaf -ErrorAction Stop)) { continue }
                    $item=Get-Item -LiteralPath $path -Force
                    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { continue }
                    $candidates += $item
                }
                if ($candidates.Count -gt 0) {
                    $selected=$candidates | Sort-Object -Property LastWriteTimeUtc, FullName -Descending | Select-Object -First 1
                    $copy=Copy-FollowupEvidence $selected.FullName ('install-receipts/'+$component+'/receipt.json') 262144 $RunDir
                    foreach ($key in $copy.Keys) { $record[$key]=$copy[$key] }
                    if ($copy.status -eq 'copied') {
                        try {
                            $parsed=[IO.File]::ReadAllText((Join-Path $RunDir $copy.captured_path),[Text.Encoding]::UTF8) | ConvertFrom-Json
                            $fields=@($parsed.PSObject.Properties.Name)
                            if ('component' -notin $fields -or $parsed.component -ne $component) { $record.parse_status='component_mismatch' }
                            elseif ('state' -notin $fields -or 'operation' -notin $fields -or
                                $parsed.state -notin @('prepared','installed','failed_rolled_back','rollback_incomplete','restored') -or
                                $parsed.operation -notin @('install','restore')) { $record.parse_status='invalid_state' }
                            else { $record.parse_status='valid'; $record.receipt_state=$parsed.state; $record.operation=$parsed.operation }
                        } catch { $record.parse_status='malformed'; $record.error=$_.Exception.Message }
                    }
                }
            }
        } catch { $record.status='unavailable'; $record.error=$_.Exception.Message }
        $rows += [pscustomobject]$record
    }
    Write-JsonFile (Join-Path $RunDir 'install-receipt-identities.json') ([ordered]@{
        backup_root=$BackupRoot; selection='Latest receipt LastWriteTimeUtc, then full path, for six fixed components.'
        limits='256 KiB per receipt. Only receipt.json is read; backup originals and paths declared inside receipts are never followed. Custom backup roots are not discovered.'
        significance='Optional historical claims; missing or malformed receipts do not invalidate a gameplay trace or prove the current source bytes.'
        receipts=$rows
    })
}

#endregion Fixed follow-up evidence

function Get-ResourceIdentities([string]$ExeDir, [string]$RunDir) {
    $resourceRoot = Join-Path $ExeDir 'resources'
    $rows = @(); $bytes = 0L; $status = 'complete'; $watch = [Diagnostics.Stopwatch]::StartNew()
    if (Test-Path -LiteralPath $resourceRoot -PathType Container) {
        foreach ($file in Get-ChildItem -LiteralPath $resourceRoot -File -Recurse -ErrorAction SilentlyContinue) {
            if ($rows.Count -ge 5000 -or ($bytes + $file.Length) -gt 256MB -or $watch.Elapsed.TotalSeconds -gt 30) {
                $status = 'budget_reached'; break
            }
            $rows += Get-FileIdentity $file.FullName
            $bytes += $file.Length
        }
    } else { $status = 'directory_missing' }
    Write-JsonFile (Join-Path $RunDir 'runtime-resource-identities.json') ([ordered]@{
        root=$resourceRoot; status=$status; hashed_bytes=$bytes; elapsed_seconds=$watch.Elapsed.TotalSeconds
        scope='Installed engine resources directory only, after exit. Custom resources= overrides and mod VFS winners require separate reconciliation from captured config/log.'
        files=$rows
    })
}
function Test-NativeTrace([string]$Path, [ValidateSet('osg','timing')][string]$Kind) {
    $result=[ordered]@{ path=$Path; kind=$Kind; valid_markers=$false; status='missing';
        scope='Bounded header/data/tail validation, not proof that every frame was recorded or that presentation/input was responsive.' }
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { return [pscustomobject]$result }
    $parser=$null
    try {
        $result.size_bytes=(Get-Item -LiteralPath $Path).Length
        if ($Kind -eq 'timing') {
            Add-Type -AssemblyName Microsoft.VisualBasic
            $parser=New-Object Microsoft.VisualBasic.FileIO.TextFieldParser($Path)
            $parser.SetDelimiters(','); $parser.HasFieldsEnclosedInQuotes=$true
            $header=$parser.ReadFields()
            if (($header -join ',') -ne 'thread,frame,category,source,callback,calls,total_us,self_us,max_us,first_us,last_us') { throw 'Unexpected timing CSV header.' }
            if ($parser.EndOfData) { throw 'Timing CSV contains only a header.' }
            $row=$parser.ReadFields()
            if ($row.Count -ne 11) { throw 'First timing row has the wrong column count.' }
            $frame=0L
            if (![long]::TryParse($row[1],[ref]$frame)) { throw 'First timing row has an invalid frame number.' }
            $result.first_record_frame=$frame
        } else {
            $head=(@(Get-Content -LiteralPath $Path -TotalCount 200) -join "`n")
            $tail=(@(Get-Content -LiteralPath $Path -Tail 200) -join "`n")
            $first=[regex]::Match($head,'(?m)^Stats Viewer FrameNumber (\d+)\s*$')
            $last=[regex]::Matches($tail,'(?m)^Stats Viewer FrameNumber (\d+)\s*$')
            if (!$first.Success -or $last.Count -eq 0) { throw 'No OSG viewer-frame records in the sampled header/tail.' }
            $result.first_observed_frame=[long]$first.Groups[1].Value
            $result.last_observed_frame=[long]$last[$last.Count-1].Groups[1].Value
            $result.sampled_frame_marker_count=$last.Count
        }
        $result.valid_markers=$true; $result.status='expected_records_present'
    } catch { $result.status='invalid_or_empty'; $result.error=$_.Exception.Message }
    finally { if ($parser) { $parser.Dispose() } }
    return [pscustomobject]$result
}
function New-CaptureArchive([string]$RunDir) {
    $RunDir=[IO.Path]::GetFullPath($RunDir).TrimEnd([IO.Path]::DirectorySeparatorChar)
    $manifest = @()
    foreach ($file in Get-ChildItem -LiteralPath $RunDir -Recurse -File) {
        if ($file.FullName -eq (Join-Path $RunDir 'capture-files.json')) { continue }
        $identity = Get-FileIdentity $file.FullName
        $manifest += [pscustomobject]@{ path=$file.FullName.Substring($RunDir.Length+1).Replace('\','/');
            size_bytes=$file.Length; sha256=$identity.sha256; status=$identity.status }
    }
    Write-JsonFile (Join-Path $RunDir 'capture-files.json') $manifest
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = $RunDir + '.zip'
    # .NET Framework CreateFromDirectory uses platform separators in entry names.
    # Write explicit portable '/' names so both Windows and Unix readers resolve
    # exactly the names recorded in capture-files.json.
    $created=[IO.Compression.ZipFile]::Open($zip,[IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in Get-ChildItem -LiteralPath $RunDir -Recurse -File) {
            $name=$file.FullName.Substring($RunDir.Length+1).Replace('\','/')
            $entry=$created.CreateEntry($name,[IO.Compression.CompressionLevel]::Optimal)
            $inputStream=$null; $outputStream=$null
            try {
                $inputStream=[IO.File]::OpenRead($file.FullName)
                $outputStream=$entry.Open()
                $inputStream.CopyTo($outputStream)
            } finally {
                if ($outputStream) { $outputStream.Dispose() }
                if ($inputStream) { $inputStream.Dispose() }
            }
        }
    } finally { $created.Dispose() }
    $archive = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $fileEntries=@($archive.Entries | Where-Object { $_.Name.Length -gt 0 })
        if ($fileEntries.Count -ne $manifest.Count + 1) { throw 'ZIP file count did not match the capture.' }
        foreach ($item in $manifest) {
            $entry = $archive.GetEntry($item.path)
            if (!$entry -or $entry.Length -ne $item.size_bytes) { throw ('ZIP member missing or wrong size: ' + $item.path) }
        }
    } finally { $archive.Dispose() }
    return $zip
}

if ($HelpersOnly) { return }
if ($env:OS -ne 'Windows_NT') { throw 'Run this collector on the Windows PC where you play OpenMW.' }
if (Get-Process openmw,openmw-launcher -ErrorAction SilentlyContinue) {
    throw 'Close OpenMW and its launcher, then start the capture. No process has been stopped.'
}
if (!$UserConfigDir) { $UserConfigDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games\OpenMW' }
if (!$OutputRoot) { $OutputRoot = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'OpenMW Responsiveness Captures' }
$exe = Resolve-GameExecutable $OpenMWExe $UserConfigDir -NonInteractive:$NoPrompt
$exeDir = Split-Path -Parent $exe
$safeLabel = [regex]::Replace($RunLabel, '[^A-Za-z0-9_-]', '_')
if ($safeLabel.Length -gt 48) { $safeLabel = $safeLabel.Substring(0,48) }
$runDir = [IO.Path]::GetFullPath((Join-Path $OutputRoot ('Run-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + $safeLabel)))
[void][IO.Directory]::CreateDirectory($runDir)
[void][IO.Directory]::CreateDirectory((Join-Path $runDir 'logs'))
Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $runDir 'collector.ps1')
$oldLog = Join-Path $UserConfigDir 'openmw.log'
$oldContext = $null
if (Test-Path -LiteralPath $oldLog -PathType Leaf) {
    try { $oldContext = Get-LogContext (Read-SharedText $oldLog) } catch { Add-CaptureWarning $_.Exception.Message }
}
$beforePaths = @(Get-ConfigCandidates $oldContext $exeDir $UserConfigDir)
Save-ConfigSnapshot $beforePaths 'before' $runDir
$coreFiles = @('openmw.exe','lua51.dll','version.dll','defaults.bin')
$beforeIds = @($coreFiles | ForEach-Object { Get-FileIdentity (Join-Path $exeDir $_) })
Write-JsonFile (Join-Path $runDir 'core-identities-before.json') $beforeIds
$storageDirs = @($UserConfigDir)
if ($oldContext) { $storageDirs += @($oldContext.logs_dirs) }
$storageBefore = @()
foreach ($dir in @($storageDirs | Select-Object -Unique)) {
    foreach ($name in @('player_storage.bin','global_storage.bin')) { $storageBefore += Get-FileIdentity (Join-Path $dir $name) -MetadataOnly }
}
Write-JsonFile (Join-Path $runDir 'lua-storage-metadata-before.json') $storageBefore
$system = [ordered]@{ os_version=[Environment]::OSVersion.VersionString; cpu_count=[Environment]::ProcessorCount }
try { $system.cpu = @(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors) } catch { $system.cpu_error=$_.Exception.Message }
try { $system.display = @(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,CurrentRefreshRate,CurrentHorizontalResolution,CurrentVerticalResolution) } catch { $system.display_error=$_.Exception.Message }
try { $system.memory = Get-CimInstance Win32_OperatingSystem | Select-Object TotalVisibleMemorySize,FreePhysicalMemory,LastBootUpTime } catch { $system.memory_error=$_.Exception.Message }
Write-JsonFile (Join-Path $runDir 'system.json') $system
$observedEnv = [ordered]@{}
foreach ($name in @('OPENMW_TIMING_FILE','OPENMW_OSG_STATS_FILE','OPENMW_OSG_STATS_LIST','OPENMW_CELL_ID_INDEX',
        'OPENMW_WORLD_CELL_INDEX','OPENMW_WORLD_CELL_CANDIDATES','OPENMW_POSTLOAD_HANDOFF','OPENMW_DEBUG_LEVEL',
        'OPENMW_DEBUG_OPENGL','OPENMW_OPTIMIZE','OPENMW_DECOMPRESS_TEXTURES','OPENMW_DONT_PRECOMPILE',
        'OPENMW_PHYSICS_FPS','OSG_THREADING','OSG_NUM_DATABASE_THREADS','SDL_VIDEODRIVER')) {
    $observedEnv[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
Write-JsonFile (Join-Path $runDir 'inherited-environment.json') $observedEnv
$changedNames = @('OPENMW_TIMING_FILE','OPENMW_OSG_STATS_FILE','OPENMW_OSG_STATS_LIST')
$game = $null; $gpu = $null; $samples = $null; $markers = $null
$startUtc = $null; $exitCode = $null; $captureError = $null; $normalProcessExit = $false
$nativeAvailable = $true
try { Initialize-CaptureNative } catch { $nativeAvailable=$false; Add-CaptureWarning ('Window/marker API unavailable: '+$_.Exception.Message) }
$session = [ordered]@{ format_version=1; label=$RunLabel; mode=$Mode; executable=$exe; arguments=$GameArguments;
    declared_fg_at_start=$DeclaredFG; declaration_is_measurement=$false;
    version_dll_present=(Test-Path -LiteralPath (Join-Path $exeDir 'version.dll'));
    fg_dll_presence_does_not_prove_interpolation=$true; config_dir_hint=$UserConfigDir; output_directory=$runDir;
    prior_log_context=$oldContext; collection_started_utc=[DateTime]::UtcNow.ToString('o');
    observer_interval_ms=1000; optional_marker_poll_ms=100; reminder_minutes=8;
    limits='Engine-frame times and script scopes are not input-to-photon latency. Menus, loading redraws, hidden/minimized time and Lua/render overlap must be separated. The timing recorder has overhead. Native recording lasts until the game exits; the collector never forces game termination.' }
Write-JsonFile (Join-Path $runDir 'session.json') $session
Write-Host ''
Write-Host ('OpenMW: ' + $exe)
Write-Host ('Capture: ' + $runDir)
Write-Host 'Use your current settings and FG state. Load a save; wait until responsive.'
Write-Host 'Spend about five minutes standing/turning, walking, using a door, dialogue, inventory,'
Write-Host 'journal and Options (close it too), then repeat the walking route once.'
Write-Host 'Then quit normally. Optional markers: Ctrl+Alt+1 still, 2 walk, 3 door, 4 dialogue,'
Write-Host '5 inventory, 6 sluggish moment, 7 FG declared OFF, 8 FG declared ON.'
Write-Host 'Markers only label the trace; they do not change FG. Skip any key combination used by a mod.'
Write-Host ''
try {
    $env:OPENMW_TIMING_FILE = if ($Mode -eq 'Detailed') { Join-Path $runDir 'script-timing.csv' } else { $null }
    $env:OPENMW_OSG_STATS_FILE = Join-Path $runDir 'openmw-stats.log'
    $env:OPENMW_OSG_STATS_LIST = 'times;resource'
    Write-JsonFile (Join-Path $runDir 'capture-environment.json') ([ordered]@{
        OPENMW_TIMING_FILE=$env:OPENMW_TIMING_FILE; OPENMW_OSG_STATS_FILE=$env:OPENMW_OSG_STATS_FILE;
        OPENMW_OSG_STATS_LIST=$env:OPENMW_OSG_STATS_LIST; all_other_environment_preserved=$true })
    $samples = New-Object IO.StreamWriter((Join-Path $runDir 'process-window-samples.csv'), $false, $script:Utf8)
    $markers = New-Object IO.StreamWriter((Join-Path $runDir 'action-markers.csv'), $false, $script:Utf8)
    $samples.AutoFlush=$true; $markers.AutoFlush=$true
    $samples.WriteLine('utc,elapsed_ms,process_id,cpu_total_ms,working_set_bytes,private_bytes,window_handle,visible,minimized,foreground,declared_fg')
    $markers.WriteLine('utc,elapsed_ms,marker,declared_fg,game_foreground')
    $startUtc = [DateTime]::UtcNow
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $startOptions = @{ FilePath=$exe; WorkingDirectory=$exeDir; PassThru=$true;
        RedirectStandardError=(Join-Path $runDir 'logs\process-stderr.txt');
        RedirectStandardOutput=(Join-Path $runDir 'logs\process-stdout.txt') }
    if ($GameArguments.Count -gt 0) { $startOptions.ArgumentList = (($GameArguments | ForEach-Object { ConvertTo-WindowsArgument $_ }) -join ' ') }
    $game = Start-Process @startOptions
    $null = $game.Handle # Keep the process handle valid for ExitCode after a short-lived child exits.
    $session.process_id = $game.Id; $session.launch_utc=$startUtc.ToString('o')
    if (!$NoGpu) {
        $smi = Find-NvidiaSmi
        if ($smi) {
            try {
                $gpu = Start-Process -FilePath $smi -ArgumentList @('--query-gpu=timestamp,index,utilization.gpu,memory.used,memory.total,clocks.current.graphics,power.draw','--format=csv','--loop-ms=1000') -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $runDir 'gpu-samples.csv') -RedirectStandardError (Join-Path $runDir 'logs\gpu-sampler-stderr.txt')
                $session.gpu_sampler = @{ path=$smi; process_id=$gpu.Id; interval_ms=1000 }
            } catch { Add-CaptureWarning ('Optional GPU sampling failed: '+$_.Exception.Message) }
        } else { $session.gpu_sampler='nvidia-smi not found; skipped' }
    } else { $session.gpu_sampler='disabled by capture argument' }
    Write-JsonFile (Join-Path $runDir 'session.json') $session
    $keyState = @{}; $nextSample=0.0; $reminded=$false
    $labels = @('stationary_start','walking_turning_start','door_transition_start','dialogue_start',
        'inventory_start','sluggishness_observed','fg_interpolation_declared_off','fg_interpolation_declared_on')
    while (!$game.HasExited) {
        $elapsed = $clock.Elapsed.TotalMilliseconds
        $foreground = $null
        if ($nativeAvailable) { $foreground = ([OpenMWCaptureNative]::ForegroundProcess() -eq $game.Id) }
        if ($elapsed -ge $nextSample) {
            try {
                $game.Refresh()
                $handle=$game.MainWindowHandle; $visible=$null; $minimized=$null
                if ($nativeAvailable -and $handle -ne [IntPtr]::Zero) {
                    $visible=[OpenMWCaptureNative]::IsWindowVisible($handle); $minimized=[OpenMWCaptureNative]::IsIconic($handle)
                }
                $values = @([DateTime]::UtcNow.ToString('o'),$elapsed.ToString('F3',[cultureinfo]::InvariantCulture),
                    $game.Id,$game.TotalProcessorTime.TotalMilliseconds.ToString('F3',[cultureinfo]::InvariantCulture),
                    $game.WorkingSet64,$game.PrivateMemorySize64,$handle.ToInt64(),$visible,$minimized,$foreground,$DeclaredFG)
                $samples.WriteLine((($values | ForEach-Object { ConvertTo-CsvCell $_ }) -join ','))
            } catch { if (!$game.HasExited) { Add-CaptureWarning ('Process sample failed: '+$_.Exception.Message) } }
            $nextSample=$elapsed+1000.0
        }
        if ($nativeAvailable) {
            $chord = [OpenMWCaptureNative]::KeyDown(0x11) -and [OpenMWCaptureNative]::KeyDown(0x12)
            for ($i=1; $i -le 8; $i++) {
                $down = $chord -and [OpenMWCaptureNative]::KeyDown(0x30+$i)
                if ($down -and !$keyState[$i] -and $foreground) {
                    if ($i -eq 7) { $DeclaredFG='Off' }; if ($i -eq 8) { $DeclaredFG='On' }
                    $values=@([DateTime]::UtcNow.ToString('o'),$elapsed.ToString('F3',[cultureinfo]::InvariantCulture),$labels[$i-1],$DeclaredFG,$foreground)
                    $markers.WriteLine((($values | ForEach-Object { ConvertTo-CsvCell $_ }) -join ','))
                }
                $keyState[$i]=$down
            }
        }
        if (!$reminded -and $clock.Elapsed.TotalMinutes -ge 8) {
            Write-Host 'Capture reminder: once you have reproduced the issue, quit OpenMW normally to finish the ZIP.'
            $reminded=$true
        }
        Start-Sleep -Milliseconds 100
    }
    $game.WaitForExit()
    $exitCode = $game.ExitCode
    $normalProcessExit=$true
} catch {
    $captureError=$_.Exception.ToString()
    Add-CaptureWarning ('Capture interrupted: '+$_.Exception.Message)
} finally {
    if ($gpu) {
        try {
            if (!$gpu.HasExited) {
                $gpu.Kill(); $gpu.WaitForExit()
                $session.gpu_sampler_status='stopped_by_collector_after_capture'
            } else {
                $gpu.WaitForExit(); $session.gpu_sampler_exit_code=$gpu.ExitCode
                $session.gpu_sampler_status='exited_before_capture_finished'
                Add-CaptureWarning ('Optional GPU sampler exited early; code '+$gpu.ExitCode+'. Inspect gpu-sampler-stderr.txt. GPU coverage may be incomplete.')
            }
        } catch { Add-CaptureWarning ('GPU sampler cleanup: '+$_.Exception.Message) }
        $gpu.Dispose()
    }
    if ($samples) { $samples.Dispose() }; if ($markers) { $markers.Dispose() }
    foreach ($name in $changedNames) { [Environment]::SetEnvironmentVariable($name, $observedEnv[$name], 'Process') }
}
$session.end_utc=[DateTime]::UtcNow.ToString('o'); $session.exit_code=$exitCode
$session.process_exit_observed=$normalProcessExit; $session.error=$captureError
if (!$normalProcessExit) { Add-CaptureWarning 'The game did not finish under this collector. Any trace may be partial; the game has not been terminated.' }
if ($normalProcessExit -and $exitCode -ne 0) { Add-CaptureWarning ('OpenMW exited abnormally with code '+$exitCode+'. No memory dump or save was copied.') }

# Locate the current run's log, including configured log directories exposed in
# stdout/stderr. Never substitute an older log for a missing current log.
$logCandidates=@(Join-Path $UserConfigDir 'openmw.log')
if ($oldContext) { foreach ($dir in @($oldContext.logs_dirs)) { $logCandidates += Join-Path $dir 'openmw.log' } }
foreach ($name in @('process-stdout.txt','process-stderr.txt')) {
    $path=Join-Path (Join-Path $runDir 'logs') $name
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        try {
            $consoleContext=Get-LogContext (Read-SharedText $path)
            foreach ($dir in @($consoleContext.logs_dirs)) { $logCandidates += Join-Path $dir 'openmw.log' }
        } catch { }
    }
}
$freshLog=$null; $newContext=$null
foreach ($path in @($logCandidates | Select-Object -Unique)) {
    try {
        if (!$startUtc -or !(Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        $item=Get-Item -LiteralPath $path
        if ($item.LastWriteTimeUtc -lt $startUtc.AddSeconds(-2)) { continue }
        $text=Read-SharedText $path
        $context=Get-LogContext $text
        if (!$context.engine_version) { continue }
        # Presence of the unique output path binds this log to the launched capture.
        $expectedStats = Get-PathComparisonKey (Join-Path $runDir 'openmw-stats.log')
        $boundStats = @($context.stats_output_paths | Where-Object { (Get-PathComparisonKey $_) -ieq $expectedStats })
        if ($boundStats.Count -eq 0) { continue }
        Copy-Item -LiteralPath $path -Destination (Join-Path $runDir 'logs\openmw.log')
        $freshLog=$path; $newContext=$context
        break
    } catch { Add-CaptureWarning ('Cannot collect log candidate '+$path+': '+$_.Exception.Message) }
}
if (!$freshLog) { Add-CaptureWarning 'No log bound to this run was found. The ZIP is incomplete; raw stdout/stderr and timing files are retained.' }
$session.fresh_log_source=$freshLog
if ($newContext) {
    Write-JsonFile (Join-Path $runDir 'runtime-context.json') $newContext
    $afterPaths = @(Get-ConfigCandidates $newContext $exeDir $UserConfigDir)
    Save-ConfigSnapshot $afterPaths 'after' $runDir
    $beforeCaptured=@($script:Snapshots | Where-Object { $_.phase -eq 'before' -and $_.status -eq 'copied' } | ForEach-Object { Get-PathComparisonKey $_.source_path })
    $activeTexts=@(@($newContext.config_paths)+@($newContext.settings_paths)+@($newContext.shader_settings_paths) |
        Where-Object { [IO.Path]::GetFileName($_) -ne 'defaults.bin' } | Select-Object -Unique)
    $session.active_configs_missing_before_snapshot=@($activeTexts | Where-Object { (Get-PathComparisonKey $_) -notin $beforeCaptured })
    if ($session.active_configs_missing_before_snapshot.Count -gt 0) {
        Add-CaptureWarning ('Some active configuration files were discovered only in this launch; their before snapshots are missing: '+($session.active_configs_missing_before_snapshot -join '; '))
    }
    Invoke-OptionalCaptureStage 'Fixed follow-up Lua source collection' { Get-FixedFollowupSources $newContext $runDir }
    Invoke-OptionalCaptureStage 'Targeted Lua source collection' { Get-TargetedScriptIdentities $newContext $runDir }
    $storageAfter=@()
    foreach ($dir in @($newContext.logs_dirs | Select-Object -Unique)) {
        foreach ($name in @('player_storage.bin','global_storage.bin')) { $storageAfter += Get-FileIdentity (Join-Path $dir $name) -MetadataOnly }
    }
    Write-JsonFile (Join-Path $runDir 'lua-storage-metadata-after.json') $storageAfter
} else { Save-ConfigSnapshot $beforePaths 'after' $runDir }
$fgLogCandidates=@((Join-Path $exeDir 'openmw_fg.log'),(Join-Path $UserConfigDir 'openmw_fg.log'),
    (Join-Path $env:USERPROFILE 'Documents\My Games\OpenMW\openmw_fg.log'))
$fgIndex=0
foreach ($path in @($fgLogCandidates | Select-Object -Unique)) {
    try {
        if ($startUtc -and (Test-Path -LiteralPath $path -PathType Leaf) -and (Get-Item -LiteralPath $path).LastWriteTimeUtc -ge $startUtc.AddSeconds(-2)) {
            Copy-Item -LiteralPath $path -Destination (Join-Path $runDir ('logs\fg-{0}-openmw_fg.log' -f $fgIndex))
            $fgIndex++
        }
    } catch { Add-CaptureWarning ('Cannot collect optional FG log '+$path+': '+$_.Exception.Message) }
}
$afterIds=@($coreFiles | ForEach-Object { Get-FileIdentity (Join-Path $exeDir $_) })
foreach ($dll in Get-ChildItem -LiteralPath $exeDir -Filter '*.dll' -File) {
    if ($dll.Name -notin $coreFiles) { $afterIds += Get-FileIdentity $dll.FullName }
}
Write-JsonFile (Join-Path $runDir 'runtime-identities-after.json') $afterIds
Invoke-OptionalCaptureStage 'Optional install receipt collection' { Get-FixedInstallReceipts $runDir }
Invoke-OptionalCaptureStage 'Runtime resource hashing' { Get-ResourceIdentities $exeDir $runDir }
Write-JsonFile (Join-Path $runDir 'config-snapshots.json') $script:Snapshots.ToArray()
$traceChecks=@(Test-NativeTrace (Join-Path $runDir 'openmw-stats.log') 'osg')
if ($Mode -eq 'Detailed') { $traceChecks+=Test-NativeTrace (Join-Path $runDir 'script-timing.csv') 'timing' }
foreach ($check in $traceChecks) {
    if (!$check.valid_markers) { Add-CaptureWarning (([IO.Path]::GetFileName($check.path))+' is missing or invalid; this capture is incomplete. '+$check.error) }
}
Write-JsonFile (Join-Path $runDir 'trace-validation.json') $traceChecks
if (!$NoPrompt -and $normalProcessExit) {
    Write-Host 'Optional: note where it felt sluggish, whether this was a first or warm launch, and any FG toggle.'
    $note=Read-Host 'Note (Enter to skip)'
    [IO.File]::WriteAllText((Join-Path $runDir 'user-note.txt'),$note,$script:Utf8)
}
$session.warnings=$script:Warnings.ToArray()
$session.capture_status=if (!$normalProcessExit -or $exitCode -ne 0 -or !$freshLog -or @($traceChecks | Where-Object {!$_.valid_markers}).Count -gt 0) {
    'incomplete'
} elseif ($script:Warnings.Count -gt 0) { 'captured_with_warnings' } else { 'captured' }
Write-JsonFile (Join-Path $runDir 'session.json') $session
Write-JsonFile (Join-Path $runDir 'warnings.json') $script:Warnings.ToArray()
try {
    Write-Host 'Packaging the capture; the raw folder will also remain available.'
    $zip=New-CaptureArchive $runDir
    Write-Host ('Send this ZIP: '+$zip)
    if (!$NoPrompt) { Start-Process explorer.exe -ArgumentList ('/select,'+(ConvertTo-WindowsArgument $zip)) }
} catch {
    Write-Warning ('ZIP creation failed. Keep/send the run folder: '+$runDir+'; '+$_.Exception.Message)
}
if ($game) { $game.Dispose() }
