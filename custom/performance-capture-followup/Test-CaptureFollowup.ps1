# Native Windows PowerShell checks. All fixture files are authored dummy data.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$CaptureScript,
    [Parameter(Mandatory=$true)][string]$InstallWrapper,
    [Parameter(Mandatory=$true)][string]$TestRoot,
    [Parameter(Mandatory=$true)][string]$ReportPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$CaptureScript=[IO.Path]::GetFullPath($CaptureScript)
$InstallWrapper=[IO.Path]::GetFullPath($InstallWrapper)
$utf8=New-Object Text.UTF8Encoding($false)
$checks=New-Object 'System.Collections.Generic.List[object]'
$sessionRoot=Join-Path ([IO.Path]::GetFullPath($TestRoot)) ('session-'+[Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($sessionRoot)
$expectedPaths=@(
    'scripts/proximityTool/ui/mainMenu.lua','scripts/proximityTool/ui/retainedParts.lua',
    'scripts/ZerkishHotkeysImproved/zhi_hotbarhud.lua','scripts/AttendMe/hud.lua',
    'scripts/gravity_enforcement_act/vendor.lua','scripts/SkillPerks/shared/stagger.lua',
    'scripts/SkillPerks/shared/timed_effects.lua','scripts/SkillPerks/shared/magic_target.lua',
    'scripts/SaneMagic/npc.lua','scripts/SaneMagic/daesumm_pn.lua','scripts/SaneMagic/daesumm_c.lua',
    'scripts/dynamicsounds/creature.lua','scripts/cmc/creature.lua',
    'scripts/omw_achievements/handlers/actorhandler.lua','scripts/tr_spells/tractor.lua',
    'scripts/bufftimers/player.lua','scripts/hbfs/actor.lua','scripts/makemorvisag/player.lua'
)
$receiptIds=@('skillperks_actor','sane_magic_actor','attend_me','proximity_tool','zhi','gravity_enforcement_act')
$componentDirs=@('01_SkillPerks','02_Sane_Magic','03_Attend_Me')
function Assert-True([bool]$Value,[string]$Message) { if (!$Value) { throw $Message } }
function Write-Text([string]$Path,[string]$Value) {
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))
    [IO.File]::WriteAllText($Path,$Value,$utf8)
}
function Write-Json([string]$Path,$Value) { Write-Text $Path (ConvertTo-Json -InputObject $Value -Depth 25) }
function Read-Json([string]$Path) { return ([IO.File]::ReadAllText($Path,[Text.Encoding]::UTF8) | ConvertFrom-Json) }
function Sha([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Text-Sha([string]$Text) {
    $a=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($a.ComputeHash($utf8.GetBytes($Text)))).Replace('-','').ToLowerInvariant() }
    finally { $a.Dispose() }
}
function Check([string]$Name,[scriptblock]$Body) {
    try { & $Body; $checks.Add([ordered]@{name=$Name;passed=$true}); Write-Host ('PASS: '+$Name) }
    catch { $checks.Add([ordered]@{name=$Name;passed=$false;error=$_.Exception.Message;location=$_.ScriptStackTrace}); Write-Host ('FAIL: '+$Name+': '+$_.Exception.Message) }
}
function New-Fixture([string]$Name) {
    $root=Join-Path $sessionRoot ($Name+' [literal] '+[char]0x00e9+[char]0x96ea)
    $inputRoot=Join-Path $root 'inputs'
    $low=Join-Path $inputRoot 'first provider'; $high=Join-Path $inputRoot 'last provider'
    $output=Join-Path $root 'capture output'; $backup=Join-Path $inputRoot 'backups'
    foreach ($path in @($low,$high,$output,$backup)) { [void][IO.Directory]::CreateDirectory($path) }
    return [pscustomobject]@{root=$root;inputs=$inputRoot;low=$low;high=$high;output=$output;backup=$backup;
        context=[pscustomobject]@{data_directories=@($low,$high)}}
}
function Populate-Sources($Fixture) {
    foreach ($path in $expectedPaths) { Write-Text (Join-Path $Fixture.low $path) ('-- authored dummy '+$path) }
}
function Source-Rows($Fixture) {
    Get-FixedFollowupSources $Fixture.context $Fixture.output
    return (Read-Json (Join-Path $Fixture.output 'fixed-script-identities.json')).targets
}
function Receipt-Rows($Fixture) {
    Get-FixedInstallReceipts $Fixture.output $Fixture.backup
    return (Read-Json (Join-Path $Fixture.output 'install-receipt-identities.json')).receipts
}
function New-Receipt($Fixture,[string]$Component,[string]$Stamp,$Value) {
    $path=Join-Path $Fixture.backup ($Component+'/'+$Stamp+'/receipt.json')
    Write-Json $path $Value
    return $path
}
function Snapshot-Inputs([string]$Root) {
    $result=@{}
    foreach ($file in @(Get-ChildItem -LiteralPath $Root -Recurse -File)) {
        $result[$file.FullName]=[ordered]@{sha256=(Sha $file.FullName);ticks=$file.LastWriteTimeUtc.Ticks}
    }
    return $result
}
function Assert-InputsUnchanged([string]$Root,$Before) {
    $after=Snapshot-Inputs $Root
    Assert-True ($after.Count -eq $Before.Count) 'Input file count changed.'
    foreach ($path in $Before.Keys) {
        Assert-True ($after.ContainsKey($path)) 'Input file disappeared.'
        Assert-True ($after[$path].sha256 -eq $Before[$path].sha256 -and $after[$path].ticks -eq $Before[$path].ticks) 'Input bytes or timestamp changed.'
    }
}
function Assert-Copy($Row,[string]$Output) {
    Assert-True ($Row.status -eq 'copied') ('Expected copied, got '+$Row.status)
    $hash=Sha (Join-Path $Output $Row.captured_path)
    Assert-True ($Row.before.sha256 -eq $hash -and $Row.copied.sha256 -eq $hash -and $Row.after.sha256 -eq $hash) 'Before/copied/after hash disagreement.'
    Assert-True ($Row.before.size_bytes -eq $Row.after.size_bytes -and $Row.before.last_write_utc -eq $Row.after.last_write_utc) 'Metadata disagreement.'
}

$envBefore=@{}; foreach ($key in @('OPENMW_OSG_STATS_FILE','OPENMW_TIMING_FILE','OPENMW_OSG_STATS_LIST')) { $envBefore[$key]=[Environment]::GetEnvironmentVariable($key,'Process') }
. $CaptureScript -HelpersOnly
Check 'Native syntax and HelpersOnly preserve process environment' {
    Assert-True ($PSVersionTable.PSVersion.Major -eq 5 -and $PSVersionTable.PSVersion.Minor -eq 1) 'Not Windows PowerShell 5.1.'
    foreach ($path in @($CaptureScript,$InstallWrapper,$PSCommandPath)) {
        $tokens=$null;$errors=$null
        [void][Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
        Assert-True ($errors.Count -eq 0) ('Syntax errors in '+$path)
    }
    foreach ($key in $envBefore.Keys) { Assert-True ([Environment]::GetEnvironmentVariable($key,'Process') -eq $envBefore[$key]) ('Environment changed: '+$key) }
}
Check 'Removing only additions exactly reconstructs released v1.0 collector' {
    $text=[IO.File]::ReadAllText($CaptureScript,[Text.Encoding]::UTF8)
    $performanceObservationBlock="        'OPENMW_LUA_UI_LEGACY_LAYOUT','OPENMW_LUA_UI_LEGACY_UPDATES',`n        'OPENMW_COMBINED_PERFORMANCE','OPENMW_NATIVE_JOBS','OPENMW_TOWN_FRAME_UPDATES','OPENMW_SPARSE_UI_UPDATES','OPENMW_SHARED_ACTIVE_SPELL_READS','OPENMW_LUA_CELL_QUERY_CACHE',`n"
    Assert-True ([regex]::Matches($text,[regex]::Escape($performanceObservationBlock)).Count -eq 1) 'Expected exactly one reviewed performance mode observation block.'
    $original=$text.Replace($performanceObservationBlock,'')
    $original=[regex]::Replace($original,'(?s)#region Fixed follow-up evidence\n.*?#endregion Fixed follow-up evidence\n\n','')
    $original=$original.Replace("    Invoke-OptionalCaptureStage 'Fixed follow-up Lua source collection' { Get-FixedFollowupSources `$newContext `$runDir }`n",'')
    $original=$original.Replace("Invoke-OptionalCaptureStage 'Optional install receipt collection' { Get-FixedInstallReceipts `$runDir }`n",'')
    Assert-True ((Text-Sha $original) -eq '72aa1ad2f78babf8d8f7c93c6127482b8332010e48f5abe44c2a6acfe77f66b3') 'Unexpected launch/recording/old helper change.'
    Assert-True ((Sha $InstallWrapper) -eq 'ff9eacfa9607c093157c45697ec8b87b23ceab231c2773903d68904791351cd9') 'Install-All wrapper differs from approved bytes.'
}
Check 'All 18 fixed targets use last loose providers with literal paths and stable copies' {
    $f=New-Fixture 'all targets'; Populate-Sources $f
    Write-Text (Join-Path $f.high $expectedPaths[0]) '-- last provider wins'
    $before=Snapshot-Inputs $f.inputs; $rows=@(Source-Rows $f)
    Assert-True ($rows.Count -eq 18) 'Wrong fixed target count.'
    Assert-True (@(Compare-Object ($expectedPaths | Sort-Object) ($rows.vfs_path | Sort-Object)).Count -eq 0) 'Fixed target census differs.'
    foreach ($row in $rows) { Assert-Copy $row $f.output }
    Assert-True ($rows[0].provider.data_directory_index -eq 1 -and $rows[0].provider.data_directory -eq $f.high) 'Override provider not selected.'
    Assert-True ($rows[1].provider.data_directory_index -eq 0) 'Lower existing provider not selected.'
    Assert-InputsUnchanged $f.inputs $before
    $script:archiveFixture=$f
}
Check 'Fixed source pass survives timing CSV larger than 256 MiB' {
    $f=New-Fixture 'timing budget'; Populate-Sources $f
    $timing=Join-Path $f.output 'script-timing.csv'
    $s=[IO.File]::Open($timing,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write); try { $s.SetLength(268435457) } finally { $s.Dispose() }
    Get-TargetedScriptIdentities $f.context $f.output
    Assert-True ((Read-Json (Join-Path $f.output 'script-source-selection.json')).status -eq 'file_over_256MiB_budget') 'Ranking budget fixture did not trigger.'
    $rows=@(Source-Rows $f)
    Assert-True (@($rows | Where-Object {$_.status -eq 'copied'}).Count -eq 18) 'Fixed helpers were lost to ranking budget.'
    [IO.File]::Delete($timing)
}
Check 'Oversize last source is reported without lower-provider fallback' {
    $f=New-Fixture 'oversize source'; Populate-Sources $f
    $p=Join-Path $f.high $expectedPaths[0]; Write-Text $p ''
    $s=[IO.File]::OpenWrite($p); try { $s.SetLength(2097153) } finally { $s.Dispose() }
    $rows=@(Source-Rows $f)
    Assert-True ($rows[0].status -eq 'oversize' -and $rows[0].provider.data_directory_index -eq 1 -and !$rows[0].captured_path) 'Oversize winner fell back or copied.'
    Assert-True (@($rows | Where-Object {$_.status -eq 'copied'}).Count -eq 17) 'An oversize file stopped other targets.'
}
Check 'Locked last source is reported without lower-provider fallback' {
    $f=New-Fixture 'locked source'; Populate-Sources $f
    $p=Join-Path $f.high $expectedPaths[0]; Write-Text $p '-- locked winning bytes'
    $s=[IO.File]::Open($p,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
    try { $rows=@(Source-Rows $f) } finally { $s.Dispose() }
    Assert-True ($rows[0].status -eq 'unavailable' -and $rows[0].provider.data_directory_index -eq 1 -and !$rows[0].captured_path) 'Locked winner fell back or copied.'
    Assert-True (@($rows | Where-Object {$_.status -eq 'copied'}).Count -eq 17) 'A locked file stopped other targets.'
}
Check 'A source exactly at 2 MiB is captured and hash verified' {
    $f=New-Fixture 'exact source limit'; $p=Join-Path $f.low $expectedPaths[0]; Write-Text $p ''
    $s=[IO.File]::OpenWrite($p); try { $s.SetLength(2097152) } finally { $s.Dispose() }
    $rows=@(Source-Rows $f); Assert-Copy $rows[0] $f.output
    Assert-True ($rows[0].copied.size_bytes -eq 2097152) 'Exact limit copy wrong size.'
}
Check 'Missing sources and receipts remain explicit optional records' {
    $f=New-Fixture 'missing'; $count=$script:Warnings.Count
    $sources=@(Source-Rows $f); $receipts=@(Receipt-Rows $f)
    Assert-True ($sources.Count -eq 18 -and @($sources | Where-Object {$_.status -ne 'missing_loose_provider'}).Count -eq 0) 'Missing source statuses wrong.'
    Assert-True ($receipts.Count -eq 6 -and @($receipts | Where-Object {$_.status -ne 'missing_receipt'}).Count -eq 0) 'Missing receipt statuses wrong.'
    Assert-True ($script:Warnings.Count -eq $count) 'Expected absence became a capture warning.'
}
Check 'All six receipt IDs select latest LastWriteTimeUtc without following targets' {
    $f=$script:archiveFixture
    $secret=Join-Path $f.inputs 'do not read.bin'; Write-Text $secret 'sentinel not collected'
    foreach ($id in $receiptIds) {
        $old=New-Receipt $f $id '20261105-235959-999-aaaa' @{component=$id;state='restored';operation='restore'}
        $new=New-Receipt $f $id '20261105-010000-001-bbbb' @{component=$id;state='installed';operation='install';files=@(@{target=$secret;backup='../originals/do-not-follow'})}
        [IO.File]::SetLastWriteTimeUtc($old,[datetime]'2026-01-01'); [IO.File]::SetLastWriteTimeUtc($new,[datetime]'2026-01-02')
        Write-Text (Join-Path ([IO.Path]::GetDirectoryName($new)) 'originals/dummy.lua') '-- never copied'
    }
    Write-Text (Join-Path $f.backup 'unrelated/20261105-010000-001-bbbb/receipt.json') '{"secret":true}'
    $before=Snapshot-Inputs $f.inputs
    $locked=[IO.File]::Open($secret,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
    try { $rows=@(Receipt-Rows $f) } finally { $locked.Dispose() }
    Assert-True ($rows.Count -eq 6) 'Wrong receipt component count.'
    foreach ($row in $rows) {
        Assert-Copy $row $f.output
        Assert-True ($row.parse_status -eq 'valid' -and $row.receipt_state -eq 'installed' -and $row.source_path.Contains('010000-001-bbbb')) 'Receipt selection or parse wrong.'
    }
    Assert-True (@(Get-ChildItem -LiteralPath (Join-Path $f.output 'install-receipts') -Recurse -File).Count -eq 6) 'Receipt collection copied extra files.'
    Assert-InputsUnchanged $f.inputs $before
}
Check 'Malformed and wrong-component receipts retain raw evidence with parse gaps' {
    $f=New-Fixture 'bad receipts'
    $p=New-Receipt $f $receiptIds[0] '20261105-010000-001-aaaa' @{}; Write-Text $p '{broken'
    [void](New-Receipt $f $receiptIds[1] '20261105-010000-001-aaaa' @{component='unrelated';state='installed';operation='install'})
    [void](New-Receipt $f $receiptIds[2] '20261105-010000-001-aaaa' @{component=$receiptIds[2];state='invented';operation='install'})
    $rows=@(Receipt-Rows $f)
    foreach ($i in 0..2) { Assert-Copy $rows[$i] $f.output }
    Assert-True ($rows[0].parse_status -eq 'malformed' -and $rows[1].parse_status -eq 'component_mismatch' -and $rows[2].parse_status -eq 'invalid_state') 'Receipt gap classification wrong.'
}
Check 'Oversize and locked latest receipts do not fall back to older receipts' {
    $f=New-Fixture 'bounded receipts'
    foreach ($id in $receiptIds[0..1]) {
        $p=New-Receipt $f $id '20261105-010000-001-aaaa' @{component=$id;state='installed';operation='install'}
        [IO.File]::SetLastWriteTimeUtc($p,[datetime]'2026-01-01')
    }
    $big=New-Receipt $f $receiptIds[0] '20261105-020000-001-bbbb' @{}
    $s=[IO.File]::OpenWrite($big); try { $s.SetLength(262145) } finally { $s.Dispose() }
    $locked=New-Receipt $f $receiptIds[1] '20261105-020000-001-bbbb' @{component=$receiptIds[1];state='installed';operation='install'}
    $s=[IO.File]::Open($locked,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
    try { $rows=@(Receipt-Rows $f) } finally { $s.Dispose() }
    Assert-True ($rows[0].status -eq 'oversize' -and !$rows[0].captured_path) 'Oversize receipt copied or fell back.'
    Assert-True ($rows[1].status -eq 'unavailable' -and !$rows[1].captured_path) 'Locked receipt copied or fell back.'
}
Check 'Archive includes fixed sources and receipts with exact recorded bytes' {
    $f=$script:archiveFixture; $zip=New-CaptureArchive $f.output
    $manifest=Read-Json (Join-Path $f.output 'capture-files.json')
    $archive=[IO.Compression.ZipFile]::OpenRead($zip)
    try {
        foreach ($item in $manifest) {
            $entry=$archive.GetEntry($item.path); Assert-True ($null -ne $entry) ('Missing ZIP entry '+$item.path)
            $stream=$entry.Open(); $a=[Security.Cryptography.SHA256]::Create()
            try { $hash=([BitConverter]::ToString($a.ComputeHash($stream))).Replace('-','').ToLowerInvariant() } finally { $a.Dispose();$stream.Dispose() }
            Assert-True ($hash -eq $item.sha256) ('Archive hash mismatch '+$item.path)
        }
        Assert-True (@($manifest | Where-Object {$_.path.StartsWith('fixed-scripts/')}).Count -eq 18) 'Missing fixed sources in archive.'
        Assert-True (@($manifest | Where-Object {$_.path.StartsWith('install-receipts/')}).Count -eq 6) 'Missing receipt files in archive.'
    } finally { $archive.Dispose() }
}

function Invoke-WrapperCase([string]$Name,[int[]]$Codes,[switch]$CheckOnly) {
    $f=New-Fixture $Name; $wrapper=Join-Path $f.root 'bundle with spaces/Install-All.ps1'
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($wrapper)); [IO.File]::Copy($InstallWrapper,$wrapper)
    $stub=@'
param([string]$PackageRoot,[string]$LogPath,[string]$BackupRoot,[switch]$CheckOnly)
$ErrorActionPreference='Stop'
$config=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'behavior.json')) | ConvertFrom-Json
$received=[ordered]@{package_root=$PackageRoot;log_path=$LogPath;backup_root=$BackupRoot;check_only=[bool]$CheckOnly}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'invoked.json'),(ConvertTo-Json $received),(New-Object Text.UTF8Encoding($false)))
exit [int]$config.code
'@
    for ($i=0;$i -lt 3;$i++) {
        $dir=Join-Path ([IO.Path]::GetDirectoryName($wrapper)) $componentDirs[$i]
        Write-Text (Join-Path $dir 'Install-Fix.ps1') $stub; Write-Json (Join-Path $dir 'behavior.json') @{code=$Codes[$i]}
    }
    $log=Join-Path $f.inputs 'log [fixture] file.log'; $backup=Join-Path $f.inputs 'backup [fixture] folder'
    $args=@('-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$wrapper,'-LogPath',$log,'-BackupRoot',$backup)
    if ($CheckOnly) { $args += '-CheckOnly' }
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=Join-Path $PSHOME 'powershell.exe'; $info.Arguments=($args | ForEach-Object {ConvertTo-WindowsArgument $_}) -join ' '
    $info.UseShellExecute=$false; $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true; $info.CreateNoWindow=$true
    $process=New-Object Diagnostics.Process; $process.StartInfo=$info; [void]$process.Start()
    $stdoutTask=$process.StandardOutput.ReadToEndAsync();$stderrTask=$process.StandardError.ReadToEndAsync()
    if (!$process.WaitForExit(60000)) { $process.Kill(); throw 'Dummy wrapper exceeded 60 seconds.' }
    $stdout=$stdoutTask.Result;$stderr=$stderrTask.Result;$code=$process.ExitCode;$process.Dispose()
    Write-Text (Join-Path $f.root 'invoke.stdout.txt') $stdout; Write-Text (Join-Path $f.root 'invoke.stderr.txt') $stderr
    Write-Json (Join-Path $f.root 'invoke.json') @{code=$code;args=$args}
    foreach ($component in $componentDirs) {
        $root=Join-Path ([IO.Path]::GetDirectoryName($wrapper)) $component
        $received=Read-Json (Join-Path $root 'invoked.json')
        Assert-True ($received.package_root -eq $root -and $received.log_path -eq $log -and $received.backup_root -eq $backup -and $received.check_only -eq [bool]$CheckOnly) 'Wrapper did not forward exact parameters.'
    }
    return [pscustomobject]@{code=$code;stdout=$stdout;stderr=$stderr}
}
Check 'Combined wrapper forwards literal paths and all three successful components' {
    $r=Invoke-WrapperCase 'wrapper success' @(0,0,0)
    Assert-True ($r.code -eq 0 -and $r.stdout.Contains('Successful rows are installed or already present.')) 'All-success wrapper failed.'
}
Check 'Combined CheckOnly wrapper forwards switch and reports no installation' {
    $r=Invoke-WrapperCase 'wrapper check only' @(0,0,0) -CheckOnly
    Assert-True ($r.code -eq 0 -and $r.stdout.Contains('Successful rows passed source and environment checks; no installation was performed.')) 'CheckOnly summary incorrect.'
    Assert-True (!$r.stdout.Contains('Successful rows are installed or already present.')) 'CheckOnly falsely reported installation.'
}
Check 'Combined wrapper returns guard exit 2 while checking remaining components' {
    $r=Invoke-WrapperCase 'wrapper guard' @(0,2,0)
    Assert-True ($r.code -eq 2 -and $r.stdout.Contains('Skipped:')) 'Guard result was not preserved.'
}
Check 'Combined wrapper returns failure exit 1 even alongside a guard skip' {
    $r=Invoke-WrapperCase 'wrapper failure' @(2,1,0)
    Assert-True ($r.code -eq 1 -and $r.stdout.Contains('Stopped:')) 'Failure result was not preserved.'
}
$failed=@($checks | Where-Object {!$_.passed}).Count
$report=[ordered]@{status=$(if ($failed -eq 0) {'passed'} else {'failed'});passed=$checks.Count-$failed;failed=$failed;total=$checks.Count;
    powershell_version=$PSVersionTable.PSVersion.ToString();os_version=[Environment]::OSVersion.VersionString;
    collector_sha256=(Sha $CaptureScript);install_wrapper_sha256=(Sha $InstallWrapper);harness_sha256=(Sha $PSCommandPath);
    scope='Changed post-exit collection functions on authored synthetic inputs plus combined wrapper orchestration. No native game launch, gameplay, engine, or real mod files were tested.';
    checks=$checks.ToArray()}
Write-Json $ReportPath $report
Write-Host ($report.passed.ToString()+'/'+$report.total.ToString()+' passed')
if ($failed -gt 0) { exit 1 }
exit 0
