# Configures and builds. Output: build\<Config>\version.dll
# Usage:  .\build.ps1 [-Config Debug]
param(
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

cmake -S $root -B "$root\build" -G "Visual Studio 18 2026" -A x64
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
cmake --build "$root\build" --config $Config
if ($LASTEXITCODE -ne 0) { throw "build failed" }

Write-Host "build: $root\build\$Config\version.dll"
