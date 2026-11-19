#requires -Version 5.1
[CmdletBinding()]
param([string]$CaptureScript, [string]$Output)
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT' -or $PSVersionTable.PSVersion.Major -ne 5) { throw 'This integration test requires Windows PowerShell 5.1.' }
$CaptureScript = (Resolve-Path -LiteralPath $CaptureScript).Path
$Output = [IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Path $Output -Force | Out-Null
. $CaptureScript -DefinitionsOnly
$checks = New-Object 'System.Collections.Generic.List[object]'
function Check-Windows {
    param([string]$Name, [bool]$Passed)
    $checks.Add([pscustomobject]@{name=$Name;passed=$Passed})
    if (-not $Passed) { throw ('FAILED: ' + $Name) }
    Write-Host ('PASS: ' + $Name)
}
function Set-TestCapture {
    param([string]$Name)
    $script:CaptureRoot = Join-Path $Output $Name
    New-Item -ItemType Directory -Path $script:CaptureRoot -Force | Out-Null
    $script:CopiedDumpBytes=0L
    $script:Copied=New-Object 'System.Collections.Generic.List[object]'
    $script:Omitted=New-Object 'System.Collections.Generic.List[object]'
}
$complete=$false
$procdumpHash=$null
try {
    Set-TestCapture 'setup'
    $procdump = Get-ProcDump
    $procdumpHash = (Get-FileHash -LiteralPath $procdump -Algorithm SHA256).Hash.ToLowerInvariant()
    Check-Windows 'Signed ProcDump matches the observed 12.02 binary' ($procdumpHash -ceq '22c2a2b09ade73a342a18dab838354385f73fc98e324b9903fd61fff949c5124')
    # Only the disposable CI runner accepts the tool license automatically.
    # The delivered user's launcher continues to show Microsoft's license normally.
    $license = Start-Process -FilePath $procdump -ArgumentList '-accepteula -?' -NoNewWindow -PassThru -RedirectStandardOutput (Join-Path $script:CaptureRoot 'license-output.txt') -RedirectStandardError (Join-Path $script:CaptureRoot 'license-error.txt')
    $licenseHandle=$license.Handle
    $license.WaitForExit()
    $sharedLog=Join-Path $script:CaptureRoot 'held-writer.txt'
    $writer=[IO.File]::Open($sharedLog,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite)
    try {
        $heldText='[20:00:00]Dump 1 complete: 1 MB written in 0.0 seconds'
        $heldBytes=[Text.Encoding]::Unicode.GetBytes($heldText)
        $writer.Write($heldBytes,0,$heldBytes.Length)
        $writer.Flush()
        Check-Windows 'ProcDump text remains readable while another writer holds the file' ((Read-ProcDumpText -Path $sharedLog) -ceq $heldText)
        Complete-Capture
        $heldZip=[IO.Compression.ZipFile]::OpenRead($script:CaptureRoot+'.zip')
        try {
            $heldEntry=$heldZip.GetEntry('held-writer.txt')
            $reader=New-Object IO.StreamReader($heldEntry.Open(),[Text.Encoding]::Unicode)
            try { $savedText=$reader.ReadToEnd() } finally { $reader.Dispose() }
            Check-Windows 'Evidence ZIP retains log held by a writer' ($savedText -ceq $heldText)
        } finally { $heldZip.Dispose() }
    } finally { $writer.Dispose() }
    Set-TestCapture 'probe'
    Test-ProcDumpCapture -ProcDump $procdump
    $probe=Get-Content -Raw -LiteralPath (Join-Path $script:CaptureRoot 'procdump-self-test/result.json') | ConvertFrom-Json
    Check-Windows 'Real native cmd self-test accepts one completed dump and exit 1' ($probe.passed -and $probe.procdump_exit_code -eq 1 -and $probe.dumps_with_mdmp_header -eq 1)
    $fixtureSource=@'
#include <windows.h>
#include <intrin.h>
#include <fstream>
#include <string>
int main() {
    wchar_t executable[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) return 98;
    std::wstring folder(executable);
    folder.resize(folder.find_last_of(L"\\/") + 1);
    std::ifstream input(folder + L"scenario.txt");
    std::string mode;
    std::getline(input, mode);
    Sleep(2500);
    if (mode == "failfast") __fastfail(7);
    return mode == "nonzero" ? 23 : 0;
}
'@
    $fixture=Join-Path $Output 'fixture.exe'
    $cpp=Join-Path $Output 'fixture.cpp'
    [IO.File]::WriteAllText($cpp,$fixtureSource)
    $compileArgs=@('/nologo','/O2','/EHsc','/MT',('/Fe:'+$fixture),('/Fo:'+(Join-Path $Output 'fixture.obj')),$cpp)
    $compileCommand=($compileArgs | ForEach-Object { Quote-NativeArgument -Value $_ }) -join ' '
    $compiler=Start-Process -FilePath (Get-Command cl.exe).Source -ArgumentList $compileCommand -WorkingDirectory $Output -NoNewWindow -PassThru -RedirectStandardOutput (Join-Path $Output 'compiler-output.txt') -RedirectStandardError (Join-Path $Output 'compiler-error.txt')
    $compilerHandle=$compiler.Handle
    $compiler.WaitForExit()
    Check-Windows 'Native fixture compiled successfully' ($compiler.ExitCode -eq 0 -and (Test-Path -LiteralPath $fixture))
    foreach($scenario in @('normal','nonzero','failfast')) {
        Set-TestCapture $scenario
        $app=Join-Path $script:CaptureRoot 'fixture app'
        New-Item -ItemType Directory -Path $app -Force | Out-Null
        Copy-Item -LiteralPath $fixture -Destination (Join-Path $app 'openmw.exe')
        [IO.File]::WriteAllText((Join-Path $app 'scenario.txt'),$scenario)
        Start-MonitoredOpenMW -Exe (Join-Path $app 'openmw.exe') -ProcDump $procdump
        $result=Get-Content -Raw -LiteralPath (Join-Path $script:CaptureRoot 'process-result.json') | ConvertFrom-Json
        Check-Windows ($scenario+' monitor verifies actual dumps and observes game exit') ($result.capture_completed -and $result.capture_evidence.verified -and $result.game_still_alive -eq $false)
        if($scenario -eq 'normal') { Check-Windows 'Normal child exit remains zero' ($result.game_exit_code -eq 0) }
        if($scenario -eq 'nonzero') { Check-Windows 'Child exit 23 stays separate from ProcDump dump count' ($result.game_exit_code -eq 23 -and $result.procdump_exit_code -ne 23) }
        if($scenario -eq 'failfast') {
            $trace=Read-ProcDumpText -Path (Join-Path $script:CaptureRoot 'procdump-output.txt')
            Check-Windows 'Native fast-fail captures exact C0000409 instead of a fallback return' ($result.game_exit_code -eq -1073740791 -and $trace -match '(?i)c0000409')
        }
    }
    Set-TestCapture 'missing-executable'
    $rejected=$false
    try { Start-MonitoredOpenMW -Exe (Join-Path $script:CaptureRoot 'missing.exe') -ProcDump $procdump } catch { $rejected=$true }
    $failure=Get-Content -Raw -LiteralPath (Join-Path $script:CaptureRoot 'process-result.json') | ConvertFrom-Json
    Check-Windows 'Actual launch failure is rejected without claiming capture' ($rejected -and -not $failure.capture_completed -and -not $failure.capture_evidence.verified)
    $complete=$true
} finally {
    Write-JsonFile -Path (Join-Path $Output 'windows-test-results.json') -Value ([pscustomobject]@{
        completed=$complete; checks=@($checks.ToArray()); powershell=$PSVersionTable.PSVersion.ToString(); os=[Environment]::OSVersion.VersionString;
        source_sha256=(Get-FileHash -LiteralPath $CaptureScript -Algorithm SHA256).Hash.ToLowerInvariant(); procdump_sha256=$procdumpHash;
        scope='Actual signed ProcDump and shipped capture functions on Windows PowerShell 5.1; synthetic normal/nonzero/fail-fast processes, not OpenMW gameplay.'
    })
}
