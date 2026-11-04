#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$CaptureScript,
    [string]$TestRoot = (Join-Path ([IO.Path]::GetTempPath()) ('OpenMW capture tests ' + [guid]::NewGuid().ToString('N'))),
    [string]$ReportPath
)
$ErrorActionPreference='Stop'
$CaptureScript=(Resolve-Path -LiteralPath $CaptureScript).Path
[void][IO.Directory]::CreateDirectory($TestRoot)
if (!$ReportPath) { $ReportPath=Join-Path $TestRoot 'test-report.json' }
$checks=New-Object 'System.Collections.Generic.List[object]'
function Assert-True($Condition,[string]$Message) { if (!$Condition) { throw $Message } }
function Check([string]$Name,[scriptblock]$Body) {
    try { & $Body; $checks.Add([pscustomobject]@{ name=$Name; passed=$true }); Write-Host ('PASS '+$Name) }
    catch { $checks.Add([pscustomobject]@{ name=$Name; passed=$false; error=$_.Exception.ToString() }); Write-Warning ('FAIL '+$Name+': '+$_.Exception.Message) }
}
. $CaptureScript -HelpersOnly
Check 'PowerShell AST parses without errors' {
    $tokens=$null; $parseErrors=$null
    [void][Management.Automation.Language.Parser]::ParseFile($CaptureScript,[ref]$tokens,[ref]$parseErrors)
    Assert-True ($parseErrors.Count -eq 0) ($parseErrors | Out-String)
}
Check 'Windows argument quoting covers blanks spaces quotes and trailing slashes' {
    Assert-True ((ConvertTo-WindowsArgument '') -ceq '""') 'Empty argument was lost.'
    Assert-True ((ConvertTo-WindowsArgument 'plain') -ceq 'plain') 'Plain argument changed.'
    Assert-True ((ConvertTo-WindowsArgument 'two words') -ceq '"two words"') 'Spaced argument was not quoted.'
    Assert-True ((ConvertTo-WindowsArgument 'a"b') -ceq '"a\"b"') 'Embedded quote was not escaped.'
    Assert-True ((ConvertTo-WindowsArgument 'two words\') -ceq '"two words\\"') 'Trailing slash before closing quote was not doubled.'
}
Check 'Log parsing retains space-containing scripts and raw registration order' {
    $fixture=@'
[01:02:03 I] Loading config file: C:\Game folder\openmw.cfg
[01:02:03 I] Loading settings file: C:\Profile\settings.cfg
[01:02:03 I] Loading shader settings file: C:\Profile\shaders.yaml
[01:02:03 I] Adding data directory C:\A
[01:02:03 I] Adding data directory C:\B
[01:02:03 I] Adding data directory C:\A
[01:02:03 V] #890 PLAYER : scripts/completionist - tr patch/quests_tr.lua
[01:02:03 I] OpenMW version 0.52.0
[01:02:03 I] Revision: test123
[01:02:03 I] OSG stats will be written to: "C:\\Captures\\Run one\\openmw-stats.log"
'@
    $ctx=Get-LogContext $fixture
    Assert-True ($ctx.registered_scripts -contains 'scripts/completionist - tr patch/quests_tr.lua') 'Script name with spaces was missed.'
    Assert-True (($ctx.data_directories -join '|') -ceq 'C:\A|C:\B|C:\A') 'Registration order was changed.'
    Assert-True ($ctx.stats_output_paths[0] -ceq 'C:\Captures\Run one\openmw-stats.log') 'C++ quoted log path was not decoded.'
}
$fixtureDir=Join-Path $TestRoot ('source spaces '+[char]0x03A9)
[void][IO.Directory]::CreateDirectory($fixtureDir)
$fixtureCfg=Join-Path $fixtureDir 'settings.cfg'
[IO.File]::WriteAllText($fixtureCfg,"[Video]`r`nframerate limit = 144`r`n# unicode $([char]0x03A9)`r`n",$script:Utf8)
$cfgHash=(Get-FileHash -LiteralPath $fixtureCfg).Hash
$unitRun=Join-Path $TestRoot 'unit capture'
[void][IO.Directory]::CreateDirectory($unitRun)
Check 'Configuration snapshots preserve exact bytes and ignore missing files' {
    Save-ConfigSnapshot @($fixtureCfg,(Join-Path $fixtureDir 'missing.cfg')) 'before' $unitRun
    $row=@($script:Snapshots | Where-Object { $_.source_path -eq $fixtureCfg })[-1]
    Assert-True ($row.status -eq 'copied') 'Configuration was not copied.'
    Assert-True ((Get-FileHash -LiteralPath (Join-Path $unitRun $row.captured_path)).Hash -eq $cfgHash) 'Copy bytes differ.'
    Assert-True ((Get-FileHash -LiteralPath $fixtureCfg).Hash -eq $cfgHash) 'Original configuration changed.'
}
Check 'Optional collection failures are visible without aborting evidence packaging' {
    $oldWarningCount=$script:Warnings.Count
    Invoke-OptionalCaptureStage 'Test-only optional failure' { throw 'intentional fixture read failure' }
    Assert-True ($script:Warnings.Count -eq ($oldWarningCount+1)) 'Optional failure was not recorded.'
}
Check 'Missing identity is explicit rather than a fabricated hash' {
    $id=Get-FileIdentity (Join-Path $fixtureDir 'absent.dll')
    Assert-True ($id.status -eq 'unavailable') 'Missing file not marked unavailable.'
}
$timingPath=Join-Path $unitRun 'script-timing.csv'
[IO.File]::WriteAllText($timingPath, "thread,frame,category,source,callback,calls,total_us,self_us,max_us,first_us,last_us`n0,1,lua,`"scripts/a with space.lua`",onUpdate,1,100,80,100,0,100`n0,2,lua,`"scripts/a with space.lua`",onUpdate,1,100,80,100,100,200`n1,2,lua,`"scripts/small.lua`",onUpdate,1,30,30,30,100,130`n",$script:Utf8)
Check 'Timing source ranking parses CSV and retains script paths with spaces' {
    $rank=Get-TopTimedScripts $timingPath
    Assert-True ($rank.status -eq 'complete') ('Unexpected parse status: '+$rank.status+' '+$rank.error)
    Assert-True ($rank.rows_read -eq 3) 'Wrong timing row count.'
    Assert-True ($rank.top_scripts[0].vfs_path -ceq 'scripts/a with space.lua') 'Wrong highest-cost source.'
    Assert-True ($rank.top_scripts[0].self_us -eq 160) 'Self time accumulation wrong.'
}
Check 'Invalid or absent timing data is explicitly incomplete' {
    Assert-True ((Get-TopTimedScripts (Join-Path $fixtureDir 'absent.csv')).status -eq 'file_missing') 'Missing CSV not marked.'
    $bad=Join-Path $fixtureDir 'bad.csv'; [IO.File]::WriteAllText($bad,"not,the,header`n")
    Assert-True ((Get-TopTimedScripts $bad).status -eq 'parse_failed') 'Invalid CSV not rejected.'
}
Check 'Native trace validation rejects empty files and accepts real record markers' {
    Assert-True ((Test-NativeTrace $timingPath 'timing').valid_markers) 'Valid CSV rejected.'
    $osg=Join-Path $unitRun 'openmw-stats.log'
    [IO.File]::WriteAllText($osg,"Stats Viewer FrameNumber 12`n    Frame duration 0.016`nStats Camera FrameNumber 12`n    GPU draw time taken 0.002`nStats Viewer FrameNumber 13`n    Frame duration 0.017`n",$script:Utf8)
    $result=Test-NativeTrace $osg 'osg'
    Assert-True ($result.valid_markers -and $result.first_observed_frame -eq 12 -and $result.last_observed_frame -eq 13) 'Valid OSG range not recognized.'
    $empty=Join-Path $fixtureDir 'empty.log';[IO.File]::WriteAllText($empty,'')
    Assert-True (!(Test-NativeTrace $empty 'osg').valid_markers) 'Empty OSG file accepted.'
    Assert-True (!(Test-NativeTrace (Join-Path $fixtureDir 'absent-timing.csv') 'timing').valid_markers) 'Missing timing accepted.'
}
Check 'ZIP contains exact captured data and leaves originals unchanged' {
    [void][IO.Directory]::CreateDirectory((Join-Path $unitRun 'empty folder'))
    $zip=New-CaptureArchive $unitRun
    $archive=[IO.Compression.ZipFile]::OpenRead($zip)
    try {
        foreach ($entry in $archive.Entries) {
            if ($entry.Name.Length -eq 0) { continue }
            Assert-True ($entry.FullName -notmatch '\\') 'ZIP member uses a platform-dependent backslash.'
            $original=Join-Path $unitRun $entry.FullName
            Assert-True ($entry.Length -eq (Get-Item -LiteralPath $original).Length) 'ZIP length mismatch.'
            $stream=$entry.Open(); $sha=[Security.Cryptography.SHA256]::Create()
            try { $hash=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','') }
            finally { $stream.Dispose(); $sha.Dispose() }
            Assert-True ($hash -eq (Get-FileHash -LiteralPath $original).Hash) ('ZIP bytes differ: '+$entry.FullName)
        }
    } finally { $archive.Dispose() }
    Assert-True ((Get-FileHash -LiteralPath $fixtureCfg).Hash -eq $cfgHash) 'Source file changed during packaging.'
}

$integrationRun=$false
if ($env:OS -eq 'Windows_NT') {
    $integrationRun=$true
    $install=Join-Path $TestRoot ('engine spaces '+[char]0x03A9)
    $profile=Join-Path $TestRoot ('profile spaces '+[char]0x00E9)
    $data=Join-Path $TestRoot 'data provider'
    $out=Join-Path $TestRoot 'captures'
    foreach ($dir in @($install,$profile,$data,(Join-Path $install 'resources\shaders'),(Join-Path $data 'scripts'))) {
        [void][IO.Directory]::CreateDirectory($dir)
    }
    [IO.File]::WriteAllText((Join-Path $install 'openmw.cfg'),('config="'+$profile+'"'+"`nresources=resources`n"),$script:Utf8)
    [IO.File]::WriteAllText((Join-Path $profile 'openmw.cfg'),('data="'+$data+'"'+"`ncontent=Morrowind.esm`n"),$script:Utf8)
    [IO.File]::WriteAllText((Join-Path $profile 'settings.cfg'),"[Lua]`nlua profiler = false`nlua debug = false`n[Video]`nframerate limit = 144`n",$script:Utf8)
    [IO.File]::WriteAllText((Join-Path $profile 'shaders.yaml'),"shaders: []`n",$script:Utf8)
    [IO.File]::WriteAllText((Join-Path $install 'openmw_fg.ini'),"mode=async`n",$script:Utf8)
    [IO.File]::WriteAllText((Join-Path $install 'resources\shaders\test.glsl'),'void main() {}',$script:Utf8)
    [IO.File]::WriteAllText((Join-Path $install 'defaults.bin'),'Fixture only, not a real engine defaults file.',$script:Utf8)
    [IO.File]::WriteAllText((Join-Path $data 'scripts\a with space.lua'),'return {}',$script:Utf8)
    [IO.File]::WriteAllText((Join-Path $profile 'openmw.log'),("Loading config file: "+(Join-Path $install 'openmw.cfg')+"`nLoading config file: "+(Join-Path $profile 'openmw.cfg')+"`nLogs dir: "+$profile+"`n"),$script:Utf8)
    $stub=@'
using System;
using System.IO;
using System.Text;
using System.Threading;
public static class CaptureStubGame {
  public static int Main(string[] args) {
    string profile=Environment.GetEnvironmentVariable("OMW_CAPTURE_TEST_PROFILE");
    string data=Environment.GetEnvironmentVariable("OMW_CAPTURE_TEST_DATA");
    string mode=Environment.GetEnvironmentVariable("OMW_CAPTURE_TEST_MODE");
    string stats=Environment.GetEnvironmentVariable("OPENMW_OSG_STATS_FILE");
    string timing=Environment.GetEnvironmentVariable("OPENMW_TIMING_FILE");
    string root=AppDomain.CurrentDomain.BaseDirectory;
    File.WriteAllLines(Path.Combine(profile,"received-arguments.txt"),args,new UTF8Encoding(false));
    var log=new StringBuilder();
    log.AppendLine("Loading config file: "+Path.Combine(root,"openmw.cfg"));
    log.AppendLine("Loading config file: "+Path.Combine(profile,"openmw.cfg"));
    log.AppendLine("Loading settings file: "+Path.Combine(profile,"settings.cfg").Replace('\\','/'));
    log.AppendLine("Loading shader settings file: "+Path.Combine(profile,"shaders.yaml").Replace('\\','/'));
    log.AppendLine("Logs dir: "+profile);
    log.AppendLine("User data dir: "+profile);
    log.AppendLine("Adding data directory "+data);
    log.AppendLine("#890 PLAYER : scripts/completionist - tr patch/quests_tr.lua");
    log.AppendLine("OpenMW version 0.52.0 fixture");
    log.AppendLine("Revision: stub-for-collector-tests");
    if(mode!="missing") {
      log.AppendLine("OSG stats will be written to: \""+stats.Replace("\\","\\\\")+"\"");
      var s=new StringBuilder();
      for(int i=0;i<12;i++) s.AppendLine("Stats Viewer FrameNumber "+i+"\n    Frame duration 0.0166667\n    script_time_taken 0.001\nStats Camera FrameNumber "+i+"\n    GPU draw time taken 0.004");
      File.WriteAllText(stats,s.ToString(),new UTF8Encoding(false));
      if(!String.IsNullOrEmpty(timing)) {
        var t=new StringBuilder("thread,frame,category,source,callback,calls,total_us,self_us,max_us,first_us,last_us\n");
        for(int i=0;i<12;i++)t.AppendLine("0,"+i+",lua,\"scripts/a with space.lua\",onUpdate,1,100,80,100,0,100");
        File.WriteAllText(timing,t.ToString(),new UTF8Encoding(false));
      }
    }
    File.WriteAllText(Path.Combine(profile,"openmw.log"),log.ToString(),new UTF8Encoding(false));
    Console.WriteLine(log.ToString()); Console.Error.WriteLine("stub stderr captured");
    Thread.Sleep(1400);
    return mode=="abnormal" ? 7 : 0;
  }
}
'@
    $stubExe=Join-Path $install 'openmw.exe'
    Add-Type -TypeDefinition $stub -OutputAssembly $stubExe -OutputType ConsoleApplication
    $sourceFiles=@((Join-Path $install 'openmw.cfg'),(Join-Path $profile 'openmw.cfg'),(Join-Path $profile 'settings.cfg'),
        (Join-Path $profile 'shaders.yaml'),(Join-Path $install 'openmw_fg.ini'),(Join-Path $install 'defaults.bin'),
        (Join-Path $install 'resources\shaders\test.glsl'),(Join-Path $data 'scripts\a with space.lua'),$stubExe)
    $sourceHashes=@{};foreach($path in $sourceFiles){$sourceHashes[$path]=(Get-FileHash -LiteralPath $path).Hash}
    $envNames=@('OPENMW_TIMING_FILE','OPENMW_OSG_STATS_FILE','OPENMW_OSG_STATS_LIST','OPENMW_CELL_ID_INDEX',
        'OPENMW_WORLD_CELL_INDEX','OPENMW_WORLD_CELL_CANDIDATES','OMW_CAPTURE_TEST_PROFILE','OMW_CAPTURE_TEST_DATA','OMW_CAPTURE_TEST_MODE')
    $saved=@{};foreach($name in $envNames){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
    try {
        $env:OPENMW_TIMING_FILE='prior timing output';$env:OPENMW_OSG_STATS_FILE='prior stats output';$env:OPENMW_OSG_STATS_LIST='prior-list'
        $env:OPENMW_CELL_ID_INDEX='0';$env:OPENMW_WORLD_CELL_INDEX='0';$env:OPENMW_WORLD_CELL_CANDIDATES='0'
        $env:OMW_CAPTURE_TEST_PROFILE=$profile;$env:OMW_CAPTURE_TEST_DATA=$data
        foreach($case in @('normal','abnormal','missing')) {
            $env:OMW_CAPTURE_TEST_MODE=$case
            $script:LastCase=$case
            Check ('Actual collector with stub child: '+$case) {
                $argv=@('two words',('unicode '+[char]0x03A9),'quote"inside','tail slash\','semi; & dollar$ literal')
                & $CaptureScript -OpenMWExe $stubExe -UserConfigDir $profile -OutputRoot $out -RunLabel $script:LastCase -NoPrompt -GameArguments $argv
                $run=Get-ChildItem -LiteralPath $out -Directory | Where-Object { $_.Name -like ('*-'+$script:LastCase) } | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
                Assert-True ($null -ne $run) 'Run directory not produced.'
                $session=Get-Content -LiteralPath (Join-Path $run.FullName 'session.json') -Raw -Encoding UTF8 | ConvertFrom-Json
                $expectedExit=0;if($script:LastCase -eq 'abnormal'){$expectedExit=7}
                Assert-True ($null -ne $session.exit_code -and $session.exit_code -eq $expectedExit) ('Exit code not preserved: '+$session.exit_code)
                Assert-True ($session.process_exit_observed -eq $true) 'Child exit was not observed.'
                Assert-True ($session.declared_fg_at_start -eq 'Unknown') 'Unknown FG was guessed.'
                $actualArgs=[IO.File]::ReadAllLines((Join-Path $profile 'received-arguments.txt'))
                Assert-True (($actualArgs -join '|') -ceq ($argv -join '|')) 'Actual child argument round trip failed.'
                Assert-True ($env:OPENMW_TIMING_FILE -ceq 'prior timing output') 'Timing environment not restored.'
                Assert-True ($env:OPENMW_OSG_STATS_FILE -ceq 'prior stats output') 'Stats environment not restored.'
                Assert-True ($env:OPENMW_OSG_STATS_LIST -ceq 'prior-list') 'Stats-list environment not restored.'
                Assert-True ($env:OPENMW_CELL_ID_INDEX -ceq '0' -and $env:OPENMW_WORLD_CELL_INDEX -ceq '0' -and $env:OPENMW_WORLD_CELL_CANDIDATES -ceq '0') 'Optimization override changed.'
                foreach($path in $sourceFiles){Assert-True ((Get-FileHash -LiteralPath $path).Hash -eq $sourceHashes[$path]) ('Installed fixture changed: '+$path)}
                $zip=$run.FullName+'.zip';Assert-True (Test-Path -LiteralPath $zip) 'ZIP not produced.'
                $archive=[IO.Compression.ZipFile]::OpenRead($zip)
                try {
                    Assert-True (@($archive.Entries | Where-Object {$_.FullName -match '\.(exe|dll|bin|ess|dmp)$'}).Count -eq 0) 'Private binary/save was copied.'
                    foreach($entry in $archive.Entries){
                        if ($entry.Name.Length -eq 0) { continue }
                        $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
                        try{$hash=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','')}finally{$stream.Dispose();$sha.Dispose()}
                        Assert-True ($hash -eq (Get-FileHash -LiteralPath (Join-Path $run.FullName $entry.FullName)).Hash) ('ZIP readback mismatch: '+$entry.FullName)
                    }
                } finally {$archive.Dispose()}
                if($script:LastCase -eq 'missing') {
                    Assert-True (@($session.warnings | Where-Object {$_ -match 'incomplete|too small|missing'}).Count -gt 0) 'Missing outputs were not reported.'
                } else {
                    Assert-True ($session.fresh_log_source -ceq (Join-Path $profile 'openmw.log')) ('Fresh log path mismatch. Expected: '+(Join-Path $profile 'openmw.log')+'; actual: '+$session.fresh_log_source)
                    Assert-True (@($session.active_configs_missing_before_snapshot).Count -eq 0) 'Mixed slash spelling incorrectly marked captured settings as missing.'
                    $ids=Get-Content -LiteralPath (Join-Path $run.FullName 'targeted-script-identities.json') -Raw -Encoding UTF8 | ConvertFrom-Json
                    $selected=@($ids.targets | Where-Object {$_.vfs_path -eq 'scripts/a with space.lua'})[0]
                    Assert-True ($selected.source_and_copy_match -eq $true) 'Measured source was not copied/hash-bound.'
                }
            }
        }
    } finally {foreach($name in $envNames){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}
}
$report=[ordered]@{powershell_version=$PSVersionTable.PSVersion.ToString(); os=[Environment]::OSVersion.VersionString;
    capture_script=$CaptureScript; capture_sha256=(Get-FileHash -LiteralPath $CaptureScript).Hash.ToLowerInvariant();
    windows_stub_integration_executed=$integrationRun; live_gameplay_executed=$false; checks=$checks.ToArray()}
[IO.File]::WriteAllText($ReportPath,(ConvertTo-Json -InputObject $report -Depth 12),$script:Utf8)
if(@($checks | Where-Object {!$_.passed}).Count -gt 0){exit 1}
Write-Host ('Report: '+$ReportPath)
