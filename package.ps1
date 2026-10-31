# Builds Commandos2-ModernPatch and assembles a release zip:
#   out\Commandos2-ModernPatch-<version>.zip
#     ddraw.dll                                  -> game folder (next to Comm2.exe)
#     Commandos2-ModernPatch.ini                 -> game folder
#     Commandos2-ModernPatch\cnc-ddraw\...       -> bundled cnc-ddraw (MIT, see THIRD_PARTY.md)
#
# Usage:  powershell -ExecutionPolicy Bypass -File package.ps1
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
[Environment]::CurrentDirectory = $PSScriptRoot

$name       = 'Commandos2-ModernPatch'
$cncVersion = 'v7.1.0.0'
$cncUrl     = "https://github.com/FunkyFr3sh/cnc-ddraw/releases/download/$cncVersion/cnc-ddraw.zip"
$cncSha256  = '0B13AB89A64C9918189B1DADD449EF6ED3CB3B7B19CABD96D8ADBD95505BB908'

# 1. Build
cmd /c "`"$PSScriptRoot\build.bat`""
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
$version = (Select-String -Path src\ddraw_proxy.cpp -Pattern '#define MOD_VERSION "(.+)"').Matches[0].Groups[1].Value

# 2. cnc-ddraw (cached in .cache)
New-Item -ItemType Directory -Force .cache | Out-Null
$cncZip = ".cache\cnc-ddraw-$cncVersion.zip"
if (-not (Test-Path $cncZip)) {
    Write-Host "Downloading cnc-ddraw $cncVersion"
    Invoke-WebRequest -Uri $cncUrl -OutFile $cncZip -UseBasicParsing
}
$hash = (Get-FileHash $cncZip -Algorithm SHA256).Hash
if ($hash -ne $cncSha256) { Remove-Item $cncZip; throw "cnc-ddraw checksum mismatch: $hash" }

# 3. Assemble
$stage = "out\stage"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
$cncDir = "$stage\$name\cnc-ddraw"
New-Item -ItemType Directory -Force $cncDir | Out-Null
Expand-Archive -Path $cncZip -DestinationPath "$stage\cnc-tmp"
Copy-Item "$stage\cnc-tmp\ddraw.dll" $cncDir
Copy-Item -Recurse "$stage\cnc-tmp\Shaders" $cncDir
# cnc-ddraw's own ddraw.ini + the tested Commandos 2 section
$ini = (Get-Content "$stage\cnc-tmp\ddraw.ini" -Raw) + "`r`n`r`n" + (Get-Content dist\cnc-ddraw\comm2-section.ini -Raw)
Set-Content -Path "$cncDir\ddraw.ini" -Value $ini -Encoding ascii -NoNewline
Remove-Item -Recurse -Force "$stage\cnc-tmp"

Copy-Item build\ddraw.dll $stage
Copy-Item "dist\$name.ini" $stage
Copy-Item README.md, LICENSE, THIRD_PARTY.md "$stage\$name\"

$zip = "out\$name-$version.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Remove-Item -Recurse -Force $stage
Write-Host "Created $zip"
