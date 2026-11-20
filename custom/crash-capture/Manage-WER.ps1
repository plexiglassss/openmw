#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Enable','Restore')][string]$SetupMode = 'Enable',
    [string]$SetupDumpFolder,
    [switch]$LoadDefinitionsOnly
)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$script:WerAppKey = 'SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\openmw.exe'
$script:WerBackupKey = 'SOFTWARE\OpenMWCrashCapture\WERBackup-v1'
$script:WerValueNames = @('DumpFolder','DumpCount','DumpType','CustomDumpFlags')

function Test-CaptureAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        $principal = New-Object Security.Principal.WindowsPrincipal $identity
        return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    } finally { $identity.Dispose() }
}
function Open-WerRegistry {
    return [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
}
function Get-WerDesiredValues {
    param([string]$Folder)
    return @(
        [pscustomobject]@{ name='DumpFolder'; exists=$true; kind='ExpandString'; data=$Folder },
        [pscustomobject]@{ name='DumpCount'; exists=$true; kind='DWord'; data=3 },
        [pscustomobject]@{ name='DumpType'; exists=$true; kind='DWord'; data=0 },
        [pscustomobject]@{ name='CustomDumpFlags'; exists=$true; kind='DWord'; data=0x1965 }
    )
}
function Get-WerValueState {
    param($Key, [string]$Name)
    if ($null -eq $Key -or $Name -notin $Key.GetValueNames()) {
        return [pscustomobject]@{ name=$Name; exists=$false; kind=$null; data=$null }
    }
    return [pscustomobject]@{
        name=$Name; exists=$true; kind=$Key.GetValueKind($Name).ToString()
        data=$Key.GetValue($Name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
    }
}
function Test-WerValueEqual {
    param($Left, $Right)
    if ($Left.exists -ne $Right.exists) { return $false }
    if (-not $Left.exists) { return $true }
    if ($Left.kind -cne $Right.kind) { return $false }
    return (ConvertTo-Json -InputObject $Left.data -Compress) -ceq (ConvertTo-Json -InputObject $Right.data -Compress)
}
function Set-WerValueState {
    param($Key, $State)
    if (-not $State.exists) { $Key.DeleteValue($State.name, $false); return }
    $value = $State.data
    switch ($State.kind) {
        'DWord' { $value = [int]$value }
        'QWord' { $value = [long]$value }
        'Binary' { $value = [byte[]]$value }
        'None' { $value = [byte[]]$value }
        'MultiString' { $value = [string[]]$value }
        'String' { $value = [string]$value }
        'ExpandString' { $value = [string]$value }
        default { throw ('Unsupported original registry value kind: ' + $State.kind) }
    }
    $Key.SetValue($State.name, $value, [Enum]::Parse([Microsoft.Win32.RegistryValueKind], $State.kind))
}
function Read-WerBackup {
    param($Base)
    $key = $Base.OpenSubKey($script:WerBackupKey, $false)
    if ($null -eq $key) { return $null }
    try {
        $raw = $key.GetValue('StateJson', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        if (-not $raw) { throw 'The WER backup is incomplete. No capture settings were changed.' }
        $state = $raw | ConvertFrom-Json
        if ($state.schema -ne 1 -or $state.application -cne $script:WerAppKey) { throw 'Unexpected WER backup format. No settings were changed.' }
        foreach ($list in @('original','desired')) {
            $rows = @($state.$list)
            if ($rows.Count -ne 4 -or (@($rows.name | Sort-Object -Unique).Count -ne 4)) { throw 'Invalid WER backup values.' }
            foreach ($name in $script:WerValueNames) { if ($name -notin $rows.name) { throw 'Invalid WER backup value names.' } }
        }
        return $state
    } finally { $key.Dispose() }
}
function Test-WerConfigured {
    param([string]$Folder)
    $base = Open-WerRegistry
    $app = $null
    try {
        $backup = Read-WerBackup $base
        if ($null -eq $backup) { return $false }
        $app = $base.OpenSubKey($script:WerAppKey, $false)
        foreach ($desired in @(Get-WerDesiredValues $Folder)) {
            if (-not (Test-WerValueEqual (Get-WerValueState $app $desired.name) $desired)) { return $false }
            $savedDesired = @($backup.desired | Where-Object { $_.name -ceq $desired.name })[0]
            if (-not (Test-WerValueEqual $savedDesired $desired)) { return $false }
        }
        return $true
    } finally { if ($null -ne $app) { $app.Dispose() }; $base.Dispose() }
}
function Invoke-WerConfiguration {
    param([ValidateSet('Enable','Restore')][string]$Action, [string]$Folder)
    if (-not (Test-CaptureAdministrator)) { throw 'Windows administrator permission is required to change per-application WER settings.' }
    if (-not [Environment]::Is64BitProcess) { throw 'Use 64-bit Windows PowerShell.' }
    if ($Action -eq 'Enable') {
        if (-not $Folder -or -not [IO.Path]::IsPathRooted($Folder) -or $Folder.StartsWith('\\')) { throw 'Dump folder must be an absolute path on a local drive.' }
        $Folder = [IO.Path]::GetFullPath($Folder).TrimEnd('\')
        # The unelevated launcher creates the directory. Do not create/chmod user paths as administrator.
        if (-not [IO.Directory]::Exists($Folder)) { throw 'The unelevated launcher must create the dump folder first.' }
    }
    $mutex = New-Object Threading.Mutex($false, 'Global\OpenMWCrashCapture-WER-v1')
    $owned = $false
    $base = $null; $app = $null; $backupKey = $null
    try {
        try { $owned = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $owned = $true }
        if (-not $owned) { throw 'Another capture setup or restore is in progress.' }
        $base = Open-WerRegistry
        $state = Read-WerBackup $base
        $app = $base.OpenSubKey($script:WerAppKey, $true)
        if ($null -eq $state) {
            if ($Action -eq 'Restore') { Write-Host 'No saved capture configuration to restore.'; return }
            $state = [pscustomobject]@{
                schema=1; application=$script:WerAppKey; original_key_exists=($null -ne $app)
                created_utc=[DateTime]::UtcNow.ToString('o')
                original=@($script:WerValueNames | ForEach-Object { Get-WerValueState $app $_ })
                desired=@(Get-WerDesiredValues $Folder)
            }
            $backupKey = $base.CreateSubKey($script:WerBackupKey)
            $backupKey.SetValue('StateJson', (ConvertTo-Json -InputObject $state -Depth 10 -Compress), [Microsoft.Win32.RegistryValueKind]::String)
            $backupKey.Flush()
            $backupKey.Dispose(); $backupKey = $null
        }
        if ($Action -eq 'Enable') {
            foreach ($desired in @(Get-WerDesiredValues $Folder)) {
                $saved = @($state.desired | Where-Object { $_.name -ceq $desired.name })[0]
                if (-not (Test-WerValueEqual $saved $desired)) { throw 'Capture was configured for another folder/user. Restore that configuration first; the original backup was kept.' }
            }
        }
        # Check all values before making any change. Allow interrupted enable/restore states.
        foreach ($name in $script:WerValueNames) {
            $current = Get-WerValueState $app $name
            $original = @($state.original | Where-Object { $_.name -ceq $name })[0]
            $desired = @($state.desired | Where-Object { $_.name -ceq $name })[0]
            if (-not (Test-WerValueEqual $current $original) -and -not (Test-WerValueEqual $current $desired)) {
                throw ('WER value ' + $name + ' was changed by something else. Nothing was overwritten; the original backup was kept.')
            }
        }
        if ($null -eq $app) { $app = $base.CreateSubKey($script:WerAppKey) }
        $target = $state.desired
        if ($Action -eq 'Restore') { $target = $state.original }
        foreach ($value in $target) { Set-WerValueState $app $value }
        $app.Flush()
        foreach ($value in $target) {
            if (-not (Test-WerValueEqual (Get-WerValueState $app $value.name) $value)) { throw 'WER settings read-back failed. Recovery backup was kept.' }
        }
        if ($Action -eq 'Restore') {
            $deleteEmptyApp = (-not $state.original_key_exists -and $app.ValueCount -eq 0 -and $app.SubKeyCount -eq 0)
            $app.Dispose(); $app = $null
            if ($deleteEmptyApp) { $base.DeleteSubKey($script:WerAppKey, $false) }
            $base.DeleteSubKeyTree($script:WerBackupKey, $false)
            Write-Host 'Previous OpenMW crash-dump settings restored. Existing dumps were kept.'
        } else { Write-Host 'Passive crash capture enabled for openmw.exe. The game will run without a debugger.' }
    } finally {
        if ($null -ne $backupKey) { $backupKey.Dispose() }
        if ($null -ne $app) { $app.Dispose() }
        if ($null -ne $base) { $base.Dispose() }
        if ($owned) { $mutex.ReleaseMutex() }
        $mutex.Dispose()
    }
}
if ($LoadDefinitionsOnly) { return }
try { Invoke-WerConfiguration -Action $SetupMode -Folder $SetupDumpFolder }
catch { Write-Host $_.Exception.Message -ForegroundColor Red; exit 1 }
