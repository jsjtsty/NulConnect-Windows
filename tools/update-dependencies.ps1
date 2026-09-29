# Downloads the prebuilt dependencies into lib\ (which is not committed).
#
#   libreatrust        GitHub release of jsjtsty/libreatrust
#   nulconnect-helper  GitHub release of jsjtsty/nulconnect-helper
#   Wintun             www.wintun.net (SHA-256 pinned)
#   WebView2 SDK       NuGet package Microsoft.Web.WebView2 (SHA-256 pinned)
#   nlohmann/json      GitHub release of nlohmann/json (SHA-256 pinned)
#
# Each dependency records its version in lib\<name>\.version; one that is
# already at the requested version is skipped unless -Force is given.
#
# Usage: powershell -ExecutionPolicy Bypass -File tools\update-dependencies.ps1
#            [-LibreatrustVersion v0.3.4] [-HelperVersion v0.3.2] [-Force]
# LIBREATRUST_VERSION / NULCONNECT_HELPER_VERSION override the defaults too.
param(
    [string]$LibreatrustVersion = $(if ($env:LIBREATRUST_VERSION) { $env:LIBREATRUST_VERSION } else { "v0.3.4" }),
    [string]$HelperVersion = $(if ($env:NULCONNECT_HELPER_VERSION) { $env:NULCONNECT_HELPER_VERSION } else { "v0.3.2" }),
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$WintunVersion = "0.14.1"
$WintunSha256 = "07C256185D6EE3652E09FA55C0B673E2624B565E02C4B9091C79CA7D2F24EF51"
$WebView2Version = "1.0.4191.47"
$WebView2Sha256 = "F492BBF547D0DA329553B6727435B677579B1E9F91CC9E4A1AD029366D5F23D0"
$JsonVersion = "v3.12.0"
$JsonSha256 = "AAF127C04CB31C406E5B04A63F1AE89369FCCDE6D8FA7CDDA1ED4F32DFC5DE63"

$Lib = [IO.Path]::GetFullPath("$PSScriptRoot\..\lib")
$Work = Join-Path ([IO.Path]::GetTempPath()) ("nulconnect-deps-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $Lib, $Work | Out-Null

function Test-Current([string]$Name, [string]$Version) {
    if ($Force) { return $false }
    $stamp = Join-Path $Lib "$Name\.version"
    return (Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq $Version)
}

function Get-File([string]$Url, [string]$Sha256) {
    $file = Join-Path $Work ([Guid]::NewGuid().ToString("N"))
    Write-Host "  $Url"
    Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $file
    if ($Sha256) {
        $actual = (Get-FileHash -Algorithm SHA256 $file).Hash
        if ($actual -ne $Sha256) { throw "SHA-256 mismatch for ${Url}: $actual" }
    }
    return $file
}

function Expand-To([string]$Archive) {
    $dir = Join-Path $Work ([Guid]::NewGuid().ToString("N"))
    # Expand-Archive insists on a .zip extension (NuGet packages are zips).
    $zip = "$Archive.zip"
    Move-Item $Archive $zip
    Expand-Archive -Path $zip -DestinationPath $dir
    return $dir
}

# Replaces lib\<Name> with the staged directory and stamps its version.
function Install-Dir([string]$Name, [string]$Version, [string]$Staged) {
    $target = Join-Path $Lib $Name
    if (Test-Path $target) { Remove-Item -Recurse -Force $target }
    Move-Item $Staged $target
    Set-Content -Path (Join-Path $target ".version") -Value $Version -Encoding ASCII
    Write-Host "  -> lib\$Name ($Version)"
}

try {
    foreach ($dep in @(
            @{ Repo = "libreatrust"; Name = "libreatrust-windows-x86_64"; Version = $LibreatrustVersion },
            @{ Repo = "nulconnect-helper"; Name = "nulconnect-helper-windows-x86_64"; Version = $HelperVersion })) {
        if (Test-Current $dep.Name $dep.Version) { continue }
        Write-Host "$($dep.Repo) $($dep.Version)"
        $zip = Get-File "https://github.com/jsjtsty/$($dep.Repo)/releases/download/$($dep.Version)/$($dep.Name).zip"
        $dir = Expand-To $zip
        Install-Dir $dep.Name $dep.Version (Join-Path $dir $dep.Name)
    }

    if (-not (Test-Current "wintun" $WintunVersion)) {
        Write-Host "Wintun $WintunVersion"
        $dir = Expand-To (Get-File "https://www.wintun.net/builds/wintun-$WintunVersion.zip" $WintunSha256)
        Install-Dir "wintun" $WintunVersion (Join-Path $dir "wintun")
    }

    if (-not (Test-Current "webview2" $WebView2Version)) {
        Write-Host "WebView2 SDK $WebView2Version"
        $dir = Expand-To (Get-File "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$WebView2Version" $WebView2Sha256)
        $staged = Join-Path $Work "webview2"
        New-Item -ItemType Directory -Force "$staged\include", "$staged\x64" | Out-Null
        Copy-Item "$dir\build\native\include\WebView2.h", "$dir\build\native\include\WebView2EnvironmentOptions.h" "$staged\include"
        Copy-Item "$dir\build\native\x64\*" "$staged\x64"
        Copy-Item "$dir\LICENSE.txt" $staged
        Install-Dir "webview2" $WebView2Version $staged
    }

    if (-not (Test-Current "nlohmann" $JsonVersion)) {
        Write-Host "nlohmann/json $JsonVersion"
        $staged = Join-Path $Work "nlohmann"
        New-Item -ItemType Directory -Force "$staged\nlohmann" | Out-Null
        Move-Item (Get-File "https://github.com/nlohmann/json/releases/download/$JsonVersion/json.hpp" $JsonSha256) "$staged\nlohmann\json.hpp"
        Move-Item (Get-File "https://raw.githubusercontent.com/nlohmann/json/$JsonVersion/LICENSE.MIT") "$staged\LICENSE.MIT"
        Install-Dir "nlohmann" $JsonVersion $staged
    }

    Write-Host "Dependencies are up to date in $Lib"
}
finally {
    Remove-Item -Recurse -Force $Work -ErrorAction SilentlyContinue
}
