# Builds an unsigned MSIX for Microsoft Store submission (the Store signs it).
# Usage: pwsh packaging/msix/pack.ps1 [-ExePath <path\to\tinta.exe>] [-OutDir <dir>]
param(
    [string]$ExePath = "$PSScriptRoot\..\..\build\Release\tinta.exe",
    [string]$OutDir = "$PSScriptRoot\out"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $ExePath)) { throw "tinta.exe not found at $ExePath — build Release first" }

# Version from CMakeLists project() VERSION, padded to 4 parts (Store requires x.y.z.0)
$cmake = Get-Content "$PSScriptRoot\..\..\CMakeLists.txt" -Raw
if ($cmake -notmatch 'project\([^)]*VERSION\s+(\d+\.\d+\.\d+)') { throw "VERSION not found in CMakeLists.txt" }
$version = "$($Matches[1]).0"

$staging = Join-Path $OutDir "staging"
if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
New-Item -ItemType Directory -Force "$staging\Assets" | Out-Null

Copy-Item $ExePath $staging
Copy-Item "$PSScriptRoot\Assets\*" "$staging\Assets"
(Get-Content "$PSScriptRoot\AppxManifest.xml" -Raw) -replace 'TINTA_MSIX_VERSION', $version |
    Set-Content "$staging\AppxManifest.xml" -Encoding utf8

$sdkBin = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.*\x64\makeappx.exe" |
    Sort-Object FullName | Select-Object -Last 1 | ForEach-Object DirectoryName
if (-not $sdkBin) { throw "makeappx.exe not found — install the Windows 10/11 SDK" }
$makeappx = Join-Path $sdkBin "makeappx.exe"
$makepri = Join-Path $sdkBin "makepri.exe"

# resources.pri maps the manifest's unqualified asset names to the scale-qualified files
$priconfig = Join-Path $OutDir "priconfig.xml"
& $makepri createconfig /cf $priconfig /dq lang-en-US /o
if ($LASTEXITCODE -ne 0) { throw "makepri createconfig failed" }
& $makepri new /pr $staging /cf $priconfig /of "$staging\resources.pri" /mn "$staging\AppxManifest.xml" /o
if ($LASTEXITCODE -ne 0) { throw "makepri new failed" }

$msix = Join-Path $OutDir "tinta-$version.msix"
if (Test-Path $msix) { Remove-Item -Force $msix }
& $makeappx pack /d $staging /p $msix /o
if ($LASTEXITCODE -ne 0) { throw "makeappx failed with exit code $LASTEXITCODE" }

Write-Host "Packed: $msix"
