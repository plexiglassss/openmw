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
    Set-TestCapture 'probe'
    Test-ProcDumpCapture -ProcDump $procdump
    $probe=Get-Content -Raw -LiteralPath (Join-Path $script:CaptureRoot 'procdump-self-test/result.json') | ConvertFrom-Json
    Check-Windows 'Real native cmd self-test accepts one completed dump and exit 1' ($probe.passed -and $probe.procdump_exit_code -eq 1 -and $probe.dumps_with_mdmp_header -eq 1)
    $fixtureSource=@'
using System;
using System.IO;
using System.Threading;
using System.Runtime.InteropServices;
public static class CaptureFixture {
    [DllImport("kernel32.dll")]
    private static extern void RaiseFailFastException(IntPtr record, IntPtr context, uint flags);
    public static int Main() {
        string mode=File.ReadAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"scenario.txt")).Trim();
        Thread.Sleep(2500);
        if(mode=="failfast") { RaiseFailFastException(IntPtr.Zero,IntPtr.Zero,0); return 99; }
        return mode=="nonzero" ? 23 : 0;
    }
}
'@
    $fixture=Join-Path $Output 'fixture.exe'
    Add-Type -TypeDefinition $fixtureSource -Language CSharp -OutputAssembly $fixture -OutputType ConsoleApplication
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
        if($scenario -eq 'failfast') { Check-Windows 'Native fail-fast remains an abnormal child exit' ($null -ne $result.game_exit_code -and $result.game_exit_code -ne 0) }
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
