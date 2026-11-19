param(
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$build = Join-Path $root "build-fixed"
$out = Join-Path $root "dist-fixed"

if (Test-Path $build) { Remove-Item $build -Recurse -Force }
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force $out | Out-Null

# Let CMake choose the installed Visual Studio generator instead of requiring
# one specific VS release name. -A x64 keeps the DLL compatible with OpenMW x64.
cmake -S $root -B $build -A x64
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
cmake --build $build --config $Config
if ($LASTEXITCODE -ne 0) { throw "build failed" }

$dll = Join-Path $build "$Config\version.dll"
if (!(Test-Path $dll)) { throw "version.dll not found at $dll" }
Copy-Item $dll $out
Copy-Item (Join-Path $root "openmw_fg.ini") $out
Copy-Item (Join-Path $root "PATCH_NOTES.txt") $out

$zip = Join-Path $root "OpenMW Frame Generation 0.2.0 - Overlay and Save-Load Fix.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$out\*" -DestinationPath $zip -Force

Write-Host ""
Write-Host "Build complete:"
Write-Host "  $dll"
Write-Host "Test package:"
Write-Host "  $zip"
