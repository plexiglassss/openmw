[CmdletBinding()]
param([string]$ToolsRoot = $PSScriptRoot)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $ToolsRoot 'SettingsTools.ps1')
$count = 0
function Assert-True([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw ('FAIL: ' + $Name) }
    $script:count++
}
function Assert-Throws([scriptblock]$Action, [string]$Name) {
    $threw = $false
    try { & $Action | Out-Null } catch { $threw = $true }
    Assert-True $threw $Name
}
$utf8 = New-Object Text.UTF8Encoding($false)
$original = "# Keep these comments`r`n[Shadows]`r`nenable shadows = true`r`ncompute scene bounds = primitives  # custom`r`nshadow map resolution = 1024`r`n`r`n[Lua]`r`nlua profiler = false`r`nmemory limit = 8589934592`r`n`r`n[Unrelated]`r`nname = Morrowind - caf" + [char]233 + "`r`nvalue = 12"
[byte[]]$bytes = $utf8.GetBytes($original)
[byte[]]$converted = Convert-GameplaySettings $bytes
$updated = $utf8.GetString($converted)
$stripped = $updated.Replace('compute scene bounds = bounds  # custom', 'compute scene bounds = primitives  # custom')
$stripped = $stripped.Replace("inactive script cache duration = 30`r`n", '').Replace("inactive script cache memory threshold = 2147483648`r`n", '')
Assert-True ($stripped -ceq $original) 'all unrelated bytes, comments, CRLF, Unicode and missing final newline preserved'
Assert-True ((Get-BytesSha (Convert-GameplaySettings $converted)) -eq (Get-BytesSha $converted)) 'idempotence'
Assert-True ($updated.Contains('memory limit = 8589934592')) 'existing profiler-only memory setting preserved'

$lf = $utf8.GetBytes("[Lua]`nlua num threads = 1`n[Shadows]`n compute scene bounds = primitives`n")
$lfText = $utf8.GetString((Convert-GameplaySettings $lf))
Assert-True (-not $lfText.Contains("`r")) 'LF endings preserved'
Assert-True ($lfText.Contains(" compute scene bounds = bounds`n")) 'existing indentation preserved'
[byte[]]$bomInput = [byte[]](@(239,187,191) + $bytes)
[byte[]]$bomOutput = Convert-GameplaySettings $bomInput
Assert-True ($bomOutput[0] -eq 239 -and $bomOutput[1] -eq 187 -and $bomOutput[2] -eq 191) 'UTF-8 BOM preserved'
Assert-True ((Get-BytesSha ([byte[]]$bomOutput[3..($bomOutput.Length - 1)])) -eq (Get-BytesSha $converted)) 'BOM does not change text'

$missing = $utf8.GetString((Convert-GameplaySettings ($utf8.GetBytes("[Camera]`nviewing distance = 40927"))))
Assert-True ($missing.StartsWith("[Camera]`nviewing distance = 40927`n")) 'missing sections appended without losing previous line'
Assert-True ($missing.Contains('[Shadows]') -and $missing.Contains('[Lua]')) 'missing target sections created'
Assert-Throws { Convert-GameplaySettings ($utf8.GetBytes("[Lua]`n[Lua]`n")) } 'duplicate section rejected'
Assert-Throws { Convert-GameplaySettings ($utf8.GetBytes("[Shadows]`ncompute scene bounds = none`ncompute scene bounds = primitives`n")) } 'duplicate target key rejected'
Assert-Throws { Convert-GameplaySettings ([byte[]]@(255,254,91,0,76,0)) } 'UTF-16 rejected'
Assert-Throws { Convert-GameplaySettings ([byte[]]@(91,76,117,97,93,0)) } 'NUL bytes rejected'

$temporary = Join-Path ([IO.Path]::GetTempPath()) ('openmw-settings-test-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($temporary)
try {
    $target = Join-Path $temporary 'settings.cfg'
    $backupRoot = Join-Path $temporary 'backups'
    $hostExe = (Get-Process -Id $PID).Path
    [IO.File]::WriteAllBytes($target, $bytes)
    & $hostExe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ToolsRoot 'Apply-Settings.ps1') -SettingsPath $target -BackupRoot $backupRoot -CheckOnly
    Assert-True ($LASTEXITCODE -eq 0) 'CheckOnly succeeds'
    Assert-True ((Get-BytesSha ([IO.File]::ReadAllBytes($target))) -eq (Get-BytesSha $bytes)) 'CheckOnly leaves target byte-identical'
    Assert-True (-not (Test-Path -LiteralPath $backupRoot)) 'CheckOnly creates no backup'
    & $hostExe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ToolsRoot 'Apply-Settings.ps1') -SettingsPath $target -BackupRoot $backupRoot
    Assert-True ($LASTEXITCODE -eq 0) 'apply succeeds'
    Assert-True ((Get-BytesSha ([IO.File]::ReadAllBytes($target))) -eq (Get-BytesSha $converted)) 'applied bytes match prepared settings'
    $backups = @(Get-ChildItem -LiteralPath $backupRoot -Directory)
    Assert-True ($backups.Count -eq 1) 'one backup created'
    $backup = $backups[0].FullName
    Assert-True ((Get-BytesSha ([IO.File]::ReadAllBytes((Join-Path $backup 'settings.cfg')))) -eq (Get-BytesSha $bytes)) 'original bytes backed up'
    & $hostExe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ToolsRoot 'Apply-Settings.ps1') -SettingsPath $target -BackupRoot $backupRoot
    Assert-True ($LASTEXITCODE -eq 0) 'reapplication succeeds'
    Assert-True (@(Get-ChildItem -LiteralPath $backupRoot -Directory).Count -eq 1) 'reapplication makes no duplicate backup'

    [IO.File]::AppendAllText($target, "`r`n# later edit", $utf8)
    $laterSha = Get-BytesSha ([IO.File]::ReadAllBytes($target))
    & $hostExe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ToolsRoot 'Apply-Settings.ps1') -RestoreBackup $backup
    Assert-True ($LASTEXITCODE -ne 0) 'restore rejects a later edit'
    Assert-True ((Get-BytesSha ([IO.File]::ReadAllBytes($target))) -eq $laterSha) 'rejected restore preserves later edit'
    [IO.File]::WriteAllBytes($target, $converted)
    & $hostExe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ToolsRoot 'Apply-Settings.ps1') -RestoreBackup $backup
    Assert-True ($LASTEXITCODE -eq 0) 'restore succeeds'
    Assert-True ((Get-BytesSha ([IO.File]::ReadAllBytes($target))) -eq (Get-BytesSha $bytes)) 'restore recovers original exact bytes'
    Assert-Throws { Write-SettingsAtomically $target $converted ('0' * 64) } 'concurrent-edit guard rejects a mismatched preimage'
    Assert-True ((Get-BytesSha ([IO.File]::ReadAllBytes($target))) -eq (Get-BytesSha $bytes)) 'guard failure preserves file'
    Assert-True (@(Get-ChildItem -LiteralPath $temporary -Filter '*.tmp').Count -eq 0) 'temporary stages cleaned'
}
finally { Remove-Item -LiteralPath $temporary -Recurse -Force }
Write-Host ('PASS: ' + $count + ' settings checks (' + $PSVersionTable.PSVersion + ').')
