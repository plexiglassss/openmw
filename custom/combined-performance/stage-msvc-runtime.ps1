# Stage the Visual Studio redistributable CRT beside the public executables.
param([Parameter(Mandatory)][string]$PackageRoot)
$ErrorActionPreference = 'Stop'
$toolset = (Resolve-Path -LiteralPath $env:VCToolsRedistDir).Path
$crt = Join-Path $toolset 'x64/Microsoft.VC143.CRT'
if (-not (Test-Path -LiteralPath "$crt/vcruntime140.dll")) { throw "Missing x64 CRT in $toolset" }
Get-ChildItem -LiteralPath $crt -Filter '*.dll' -File | Copy-Item -Destination $PackageRoot

$family = Join-Path $env:VCINSTALLDIR 'Redist/MSVC'
$bootstrap = @(
    (Join-Path $toolset 'vc_redist.x64.exe'),
    (Join-Path $family 'v143/vc_redist.x64.exe')
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $bootstrap) {
    $bootstrap = Join-Path $env:RUNNER_TEMP 'vc_redist.x64.exe'
    Invoke-WebRequest -Uri 'https://aka.ms/vs/17/release/vc_redist.x64.exe' -OutFile $bootstrap
}
$signature = Get-AuthenticodeSignature -LiteralPath $bootstrap
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
    throw 'Microsoft redistributable signature verification failed.'
}
$redist = New-Item -ItemType Directory -Force -Path (Join-Path $PackageRoot 'redist')
Copy-Item -LiteralPath $bootstrap -Destination (Join-Path $redist 'vc_redist.x64.exe') -Force
