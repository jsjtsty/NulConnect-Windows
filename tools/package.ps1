<#
.SYNOPSIS
    Builds the Release configuration and packages it as an Inno Setup
    installer and a portable zip in dist\.
.DESCRIPTION
    Requires Visual Studio (MSBuild) and Inno Setup 6 (ISCC.exe on PATH, or
    installed in the default location). Use -SkipBuild to package what is
    already in build\x64\Release.
#>
param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot -Parent
$Release = Join-Path $Root "build\x64\Release"
$Dist = Join-Path $Root "dist"

$header = Get-Content (Join-Path $Root "NulConnect\src\app\Resource.h") -Raw
if ($header -notmatch '#define NC_VERSION_STRING "([^"]+)"') { throw "NC_VERSION_STRING not found in Resource.h" }
$Version = $Matches[1]

if (-not $SkipBuild) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $msbuild = & $vswhere -latest -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
    if (-not $msbuild) { throw "MSBuild was not found." }
    & $msbuild (Join-Path $Root "NulConnect.slnx") /p:Configuration=Release /p:Platform=x64 /v:m /nologo
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }
}

$files = "NulConnect.exe", "reatrust.dll", "wintun.dll", "nulconnect-helper.exe"
foreach ($name in $files) {
    if (-not (Test-Path (Join-Path $Release $name))) { throw "$name is missing from $Release" }
}

New-Item -ItemType Directory -Force $Dist | Out-Null

# Portable zip: the same files without the installer. The privileged helper is
# then installed by the app on first use (one administrator prompt).
$staging = Join-Path $Dist "NulConnect-$Version-win-x64"
if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
New-Item -ItemType Directory $staging | Out-Null
foreach ($name in $files) { Copy-Item (Join-Path $Release $name) $staging }
Copy-Item (Join-Path $Root "LICENSE") $staging
$zip = "$staging.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $staging -DestinationPath $zip
Remove-Item -Recurse -Force $staging

# Installer.
$iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe"))
    $iscc = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $iscc) { throw "Inno Setup 6 (ISCC.exe) was not found." }
& $iscc "/DAppVersion=$Version" "/DSourceDir=$Release" "/DOutputDir=$Dist" (Join-Path $Root "installer\NulConnect.iss")
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed." }

Get-ChildItem $Dist -File | Select-Object Name, @{n = "MB"; e = { [math]::Round($_.Length / 1MB, 1) } }
