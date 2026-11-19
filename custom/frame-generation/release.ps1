# Builds Release and assembles dist\openmw-fg-<Version>.zip plus a
# dist\openmw-fg-<Version>-src.zip source archive (tracked files at HEAD).
# Usage:  .\release.ps1 -Version 0.2.0 [-Tag]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [switch]$Tag # also create git tag v<Version>
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

& "$root\build.ps1"

$staging = "$root\dist\staging"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Force $staging | Out-Null

Copy-Item "$root\build\Release\version.dll" $staging
Copy-Item "$root\openmw_fg.ini" $staging
Copy-Item "$root\LICENSE" "$staging\LICENSE.txt"
(Get-Content "$root\packaging\README.txt" -Raw) -replace '\{VERSION\}', $Version |
    Set-Content "$staging\README.txt" -NoNewline

$zip = "$root\dist\openmw-fg-$Version.zip"
Compress-Archive -Path "$staging\*" -DestinationPath $zip -Force
Remove-Item $staging -Recurse -Force

# source zip: tracked files at HEAD, unzips into openmw-fg-<Version>/
$srcZip = "$root\dist\openmw-fg-$Version-src.zip"
git -C $root archive --format=zip --prefix "openmw-fg-$Version/" -o $srcZip HEAD
if ($LASTEXITCODE -ne 0) { throw "git archive failed" }

if ($Tag) { git -C $root tag "v$Version" }

Write-Host "release: $zip"
Get-Item $zip, $srcZip | Select-Object Name, Length, LastWriteTime
