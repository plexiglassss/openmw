#requires -Version 5.1
param([string]$Output)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
New-Item -ItemType Directory -Path $Output -Force | Out-Null
$Output = (Resolve-Path $Output).Path
. (Join-Path $PSScriptRoot 'OpenMW-CrashCapture.ps1') -DefinitionsOnly
$checks = New-Object 'System.Collections.Generic.List[object]'
function Check([string]$Name, [bool]$Condition, $Detail) {
    $checks.Add([pscustomobject]@{ name=$Name; passed=$Condition; detail=$Detail })
    Write-Host ($Name + ': ' + $Condition)
    if (-not $Condition) { throw ('Failed: ' + $Name) }
}
$initialEnvironment = [Environment]::GetEnvironmentVariable('OPENMW_DISABLE_CRASH_CATCHER','Process')
$folder = Join-Path $Output 'dumps'
New-Item -ItemType Directory -Path $folder -Force | Out-Null
$base = $null; $app = $null; $configured = $false; $testUser = $null
try {
    foreach ($scriptFile in @(Get-ChildItem $PSScriptRoot -Filter '*.ps1')) {
        $tokens = $null; $parseErrors = $null
        $null = [Management.Automation.Language.Parser]::ParseFile($scriptFile.FullName, [ref]$tokens, [ref]$parseErrors)
        Check ('PS5 parse ' + $scriptFile.Name) ($parseErrors.Count -eq 0) @($parseErrors | ForEach-Object { $_.Message })
    }
    $base = Open-WerRegistry
    Check 'Runner has no tool backup' ($null -eq (Read-WerBackup $base)) 'Refuse to overwrite an unrelated setup.'
    $app = $base.OpenSubKey($script:WerAppKey, $false)
    Check 'Disposable runner has no openmw.exe WER override' ($null -eq $app) $null
    $app = $base.CreateSubKey($script:WerAppKey)
    # Deliberately non-default kinds/raw values to exercise exact restoration.
    $app.SetValue('DumpFolder', '%TEMP%\prior-openmw-dumps', [Microsoft.Win32.RegistryValueKind]::ExpandString)
    $app.SetValue('DumpCount', 9, [Microsoft.Win32.RegistryValueKind]::DWord)
    $app.SetValue('UnrelatedValue', 'leave me alone', [Microsoft.Win32.RegistryValueKind]::String)
    $sub = $app.CreateSubKey('UnrelatedChild'); $sub.SetValue('Keep', 'yes'); $sub.Dispose()
    $original = @($script:WerValueNames | ForEach-Object { Get-WerValueState $app $_ })
    $app.Dispose(); $app = $null
    Invoke-WerConfiguration -Action Enable -Folder $folder
    $configured = $true
    Check 'WER configured in Registry64' (Test-WerConfigured $folder) (Get-WerDesiredValues $folder)
    $backupBefore = Read-WerBackup $base | ConvertTo-Json -Depth 10 -Compress
    Invoke-WerConfiguration -Action Enable -Folder $folder
    Check 'Repeat enable keeps original backup' ($backupBefore -ceq (Read-WerBackup $base | ConvertTo-Json -Depth 10 -Compress)) $null
    $probe = Join-Path $Output 'Capture-Probe.exe'
    & cl.exe /nologo /O2 /MT /EHsc /W4 (Join-Path $PSScriptRoot 'Capture-Probe.cpp') "/Fe:$probe" "/Fo:$Output\Capture-Probe.obj" /link advapi32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Native fixture compilation failed.' }
    $exe = Join-Path $Output 'openmw.exe'
    Copy-Item $probe $exe
    (Get-FileHash $probe -Algorithm SHA256).Hash.ToLowerInvariant() | Set-Content (Join-Path $Output 'Capture-Probe.sha256') -Encoding ASCII
    $timings = @()
    foreach ($caseName in @('normal','nonzero','handled','fastfail','access')) {
        $reportPath = Join-Path $Output ($caseName + '-process.json')
        $started = [DateTime]::UtcNow
        $process = Start-PassiveProcess -Exe $exe -Arguments @($caseName, $reportPath)
        try {
            $result = Wait-PassiveResult -Process $process -Folder $folder -StartedUtc $started -TimeoutSeconds 60
            Write-JsonFile (Join-Path $Output ($caseName + '-result.json')) $result
            $report = Get-Content $reportPath -Raw | ConvertFrom-Json
            Check ($caseName + ': no debugger and correct child environment') (-not $report.debugger_present -and $report.catcher_environment -ceq '1') $report
            if ($caseName -in @('normal','handled')) { Check ($caseName + ': zero exit without dump succeeds') ($result.game_exit_code -eq 0 -and $null -eq $result.dump -and $result.capture_status -eq 'normal_exit_no_dump') $result }
            if ($caseName -eq 'nonzero') { Check 'Handled failure is not called a native crash' ($result.game_exit_code -eq 17 -and $null -eq $result.dump -and $result.capture_status -eq 'abnormal_exit_no_dump') $result }
            if ($caseName -eq 'handled') {
                Check '100000 handled exceptions cause no dump' ($report.handled_count -eq 100000 -and $null -eq $result.dump) $report
                $timings += [pscustomobject]@{ mode='passive'; seconds=$report.loop_seconds }
            }
            if ($caseName -in @('fastfail','access')) {
                Check ($caseName + ': WER dump matches process') ($null -ne $result.dump -and $result.dump.process_id -eq $report.pid) $result
                $marker = 'OpenMW-passive-capture-probe-stack-reference-v1.4'
                $details = Read-CaptureMinidump $result.dump.path -MemoryAddress $report.heap_address -MemoryBytes $marker.Length
                Write-JsonFile (Join-Path $Output ($caseName + '-dump-check.json')) $details
                Check ($caseName + ': requested dump flags and stack-referenced heap') (($details.flags -band 0x1965) -eq 0x1965 -and $details.memory_text -ceq $marker) $details
                if ($caseName -eq 'fastfail') { Check 'Original-crash exception class and subcode' ($details.exception_code -eq 0xC0000409L -and $details.exception_parameter -eq 7) $details }
                else { Check 'Access violation captured' ($details.exception_code -eq 0xC0000005L) $details }
                Check 'Stale timestamp dump rejected' ($null -eq (Find-PassiveDump -Folder $folder -ProcessId $report.pid -StartedUtc ([DateTime]::UtcNow.AddMinutes(1)))) $null
                Check 'Wrong PID dump rejected' ($null -eq (Find-PassiveDump -Folder $folder -ProcessId ($report.pid + 1) -StartedUtc $started)) $null
            }
        } finally {
            if (-not $process.HasExited) { $process.Kill(); $null = $process.WaitForExit(5000) }
            $process.Dispose()
        }
    }
    # Baseline is the same native workload launched directly; report measurements, no FPS extrapolation.
    $directReport = Join-Path $Output 'direct-process.json'
    $p = Start-Process -FilePath $exe -ArgumentList ('handled ' + (Quote-NativeArgument $directReport)) -Wait -PassThru
    $p.Dispose()
    $direct = Get-Content $directReport -Raw | ConvertFrom-Json
    $timings += [pscustomobject]@{ mode='direct'; seconds=$direct.loop_seconds }
    Write-JsonFile (Join-Path $Output 'handled-exception-timings.json') $timings
    Check 'Parent environment untouched' ([Environment]::GetEnvironmentVariable('OPENMW_DISABLE_CRASH_CATCHER','Process') -ceq $initialEnvironment) $initialEnvironment
    # Exercise the exact shipped preflight with a real non-administrator token, including
    # binary hash verification, no-debugger/privilege checks, dump validation and cleanup.
    Copy-Item $probe (Join-Path $PSScriptRoot 'Capture-Probe.exe')
    Copy-Item (Join-Path $Output 'Capture-Probe.sha256') (Join-Path $PSScriptRoot 'Capture-Probe.sha256')
    $testUser = 'OMWCapture_' + [guid]::NewGuid().ToString('N').Substring(0,8)
    $testPassword = ConvertTo-SecureString ('Cpt!7' + [guid]::NewGuid().ToString('N') + 'zQ') -AsPlainText -Force
    $account = New-LocalUser -Name $testUser -Password $testPassword -AccountNeverExpires -PasswordNeverExpires
    $sid = $account.SID.Value
    & icacls.exe $Output /grant ('*' + $sid + ':(OI)(CI)M') /T /Q | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Could not grant the disposable test account access to its evidence folder.' }
    & icacls.exe $PSScriptRoot /grant ('*' + $sid + ':(OI)(CI)RX') /T /Q | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Could not grant the disposable test account read access to the tool.' }
    $standardRoot = Join-Path $Output ('standard account ' + [char]0x00E9)
    New-Item -ItemType Directory -Path $standardRoot -Force | Out-Null
    $childScript = Join-Path $Output 'standard-account-check.ps1'
    $childText = @'
param([string]$Tool, [string]$Destination, [string]$Dumps)
$ErrorActionPreference = 'Stop'
. $Tool -DefinitionsOnly
$script:CaptureRoot = $Destination
$script:Copied = New-Object 'System.Collections.Generic.List[object]'
$script:Omitted = New-Object 'System.Collections.Generic.List[object]'
$script:CopiedDumpBytes = 0L
try {
    Test-PassiveCapture -Folder $Dumps
    Complete-Capture
    Write-JsonFile (Join-Path $Destination 'standard-account-result.json') @{ passed=$true; elevated=(Test-CaptureAdministrator) }
} catch { $_ | Out-String | Set-Content (Join-Path $Destination 'standard-account-failure.txt'); exit 1 }
'@
    $childText | Set-Content -LiteralPath $childScript -Encoding UTF8
    $credential = New-Object Management.Automation.PSCredential ('.\' + $testUser), $testPassword
    $psExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $argsLine = (@('-NoProfile','-ExecutionPolicy','Bypass','-File',$childScript,'-Tool',(Join-Path $PSScriptRoot 'OpenMW-CrashCapture.ps1'),'-Destination',$standardRoot,'-Dumps',$folder) | ForEach-Object { Quote-NativeArgument $_ }) -join ' '
    $child = Start-Process -FilePath $psExe -ArgumentList $argsLine -Credential $credential -LoadUserProfile -PassThru -WorkingDirectory $Output
    try {
        $null = $child.Handle
        if (-not $child.WaitForExit(120000)) { $child.Kill(); throw 'Standard-account preflight timed out.' }
        Check 'Production preflight succeeds as standard user' ($child.ExitCode -eq 0) $child.ExitCode
        $standardResult = Get-Content (Join-Path $standardRoot 'standard-account-result.json') -Raw | ConvertFrom-Json
        Check 'Production preflight confirms non-elevated process' ($standardResult.passed -and -not $standardResult.elevated) $standardResult
        Check 'Probe executable removed after validation' (-not (Test-Path (Join-Path $standardRoot 'synthetic-probe\openmw.exe'))) $null
        $zip = [IO.Compression.ZipFile]::OpenRead($standardRoot + '.zip')
        try { Check 'Production ZIP contains validated synthetic dump and manifest' ('synthetic-probe/synthetic-fastfail.dmp' -in $zip.Entries.FullName -and 'file-manifest.json' -in $zip.Entries.FullName) @($zip.Entries.FullName) }
        finally { $zip.Dispose() }
    } finally { $child.Dispose() }
    $guard = New-Object Threading.Mutex($false, 'Global\OpenMWCrashCapture-Session-v1')
    $null = $guard.WaitOne(0)
    try {
        $helperArgs = (@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'Manage-WER.ps1'),'-SetupMode','Restore') | ForEach-Object { Quote-NativeArgument $_ }) -join ' '
        $helper = Start-Process -FilePath $psExe -ArgumentList $helperArgs -PassThru -RedirectStandardOutput (Join-Path $Output 'restore-session-guard.txt')
        try { $null = $helper.Handle; $helper.WaitForExit(); Check 'Separate restore process refuses active capture session' ($helper.ExitCode -ne 0 -and (Test-WerConfigured $folder)) $helper.ExitCode }
        finally { $helper.Dispose() }
    } finally { $guard.ReleaseMutex(); $guard.Dispose() }
    $app = $base.OpenSubKey($script:WerAppKey, $true)
    $app.SetValue('DumpCount', 77, [Microsoft.Win32.RegistryValueKind]::DWord)
    $conflict = $false
    try { Invoke-WerConfiguration -Action Restore } catch { $conflict = $_.Exception.Message -like '*changed by something else*' }
    Check 'Restore conflict refuses all writes and keeps backup' ($conflict -and $app.GetValue('DumpCount') -eq 77 -and $app.GetValue('DumpType') -eq 0 -and $null -ne (Read-WerBackup $base)) $null
    $app.SetValue('DumpCount', 3, [Microsoft.Win32.RegistryValueKind]::DWord)
    # Simulate interrupted configuration by restoring one value, then resume enable.
    Set-WerValueState $app $original[0]
    $app.Dispose(); $app = $null
    Invoke-WerConfiguration -Action Enable -Folder $folder
    Check 'Interrupted enable resumes without replacing backup' ((Test-WerConfigured $folder) -and $backupBefore -ceq (Read-WerBackup $base | ConvertTo-Json -Depth 10 -Compress)) $null
    Invoke-WerConfiguration -Action Restore
    $configured = $false
    $app = $base.OpenSubKey($script:WerAppKey, $true)
    foreach ($value in $original) { Check ('Exact original restored: ' + $value.name) (Test-WerValueEqual (Get-WerValueState $app $value.name) $value) (Get-WerValueState $app $value.name) }
    $sub = $app.OpenSubKey('UnrelatedChild')
    Check 'Unrelated value and child preserved' ($app.GetValue('UnrelatedValue') -ceq 'leave me alone' -and $sub.GetValue('Keep') -ceq 'yes') $null
    $sub.Dispose(); $app.Dispose(); $app = $null
    Check 'Successful restore removes backup' ($null -eq (Read-WerBackup $base)) $null
    $base.DeleteSubKeyTree($script:WerAppKey, $false) # Disposable fixture only; original absence asserted above.
    Invoke-WerConfiguration -Action Enable -Folder $folder
    $configured = $true
    Invoke-WerConfiguration -Action Restore
    $configured = $false
    $app = $base.OpenSubKey($script:WerAppKey, $false)
    Check 'Originally absent empty key removed after restore' ($null -eq $app) $null
    Check 'Restore without backup is safe' $true (Invoke-WerConfiguration -Action Restore)
    $roundTrips = @(
        [pscustomobject]@{ name='DumpFolder'; exists=$true; kind='String'; data='literal %TEMP%' },
        [pscustomobject]@{ name='DumpCount'; exists=$true; kind='QWord'; data=9223372036854775000L },
        [pscustomobject]@{ name='DumpType'; exists=$true; kind='Binary'; data=[byte[]]@(0,128,255) },
        [pscustomobject]@{ name='CustomDumpFlags'; exists=$true; kind='MultiString'; data=[string[]]@('one','two') }
    )
    $app = $base.CreateSubKey($script:WerAppKey)
    foreach ($value in $roundTrips) { Set-WerValueState $app $value }
    $app.Dispose(); $app = $null
    Invoke-WerConfiguration -Action Enable -Folder $folder; $configured = $true
    Invoke-WerConfiguration -Action Restore; $configured = $false
    $app = $base.OpenSubKey($script:WerAppKey, $true)
    foreach ($value in $roundTrips) { Check ('Raw registry kind roundtrip: ' + $value.kind) (Test-WerValueEqual (Get-WerValueState $app $value.name) $value) (Get-WerValueState $app $value.name) }
    $noneValue = [pscustomobject]@{ name='DumpType'; exists=$true; kind='None'; data=[byte[]]@(17,18) }
    Set-WerValueState $app $noneValue
    $app.Dispose(); $app = $null
    Invoke-WerConfiguration -Action Enable -Folder $folder; $configured = $true
    Invoke-WerConfiguration -Action Restore; $configured = $false
    $app = $base.OpenSubKey($script:WerAppKey, $false)
    Check 'Raw registry kind roundtrip: None' (Test-WerValueEqual (Get-WerValueState $app 'DumpType') $noneValue) (Get-WerValueState $app 'DumpType')
    $app.Dispose(); $app = $null
    $base.DeleteSubKeyTree($script:WerAppKey, $false)
} catch {
    $_ | Out-String | Set-Content (Join-Path $Output 'failure.txt') -Encoding UTF8
    $diagnosticDumps = @()
    foreach ($file in @(Get-ChildItem $folder -Filter '*.dmp' -File)) {
        try { $diagnosticDumps += Read-CaptureMinidump $file.FullName }
        catch { $diagnosticDumps += [pscustomobject]@{ path=$file.FullName; bytes=$file.Length; error=$_.Exception.ToString() } }
    }
    Write-JsonFile (Join-Path $Output 'passive-debug.json') ([pscustomobject]@{
        dumps=$diagnosticDumps
        wer=@(Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting' -ErrorAction SilentlyContinue | Select-Object Disabled,DontShowUI)
        policy=@(Get-ItemProperty 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\Windows Error Reporting' -ErrorAction SilentlyContinue | Select-Object Disabled,DontShowUI)
        debugger=@(Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\AeDebug' -ErrorAction SilentlyContinue | Select-Object Auto,Debugger)
        service=@(Get-Service WerSvc -ErrorAction SilentlyContinue | Select-Object Status,StartType)
        events=@(Get-WinEvent -FilterHashtable @{LogName='Application';Id=@(1000,1001);StartTime=(Get-Date).AddMinutes(-10)} -ErrorAction SilentlyContinue | Where-Object { $_.Message -match 'openmw.exe' } | Select-Object TimeCreated,Id,Message)
    })
    throw
} finally {
    if ($null -ne $app) { $app.Dispose() }
    if ($configured) { try { Invoke-WerConfiguration -Action Restore } catch { $_ | Out-String | Set-Content (Join-Path $Output 'restore-failure.txt') } }
    if ($null -ne $base) { $base.Dispose() }
    if ($testUser) { Remove-LocalUser -Name $testUser -ErrorAction SilentlyContinue }
    $sources = @(Get-ChildItem $PSScriptRoot -File | ForEach-Object { [pscustomobject]@{ name=$_.Name; sha256=(Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() } })
    Write-JsonFile (Join-Path $Output 'windows-results.json') ([pscustomobject]@{ powershell=$PSVersionTable.PSVersion.ToString(); os=[Environment]::OSVersion.VersionString; elevated_host=(Test-CaptureAdministrator); checks=@($checks.ToArray()); source_hashes=$sources; limitation='CI tests run in an elevated disposable host; user-facing launcher refuses elevated game launch. Interactive UAC and OpenMW FPS require user-PC validation.' })
}
