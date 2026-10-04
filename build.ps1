# Builds OStimAutoAlign.dll and packages it as a Vortex/MO2 archive in dist\.
# Nothing is copied into the game's Data folder: install the zip with the mod manager.
#   .\build.ps1          configure (first run only) + build + package
#   .\build.ps1 -Clean   wipe the build folder first
param([switch]$Clean)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# Load the MSVC x64 environment (cl, link, cmake, ninja) from VS Build Tools.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

# vcpkg: $env:VCPKG_ROOT, else a checkout next to this folder (..\vcpkg).
if (-not $env:VCPKG_ROOT) {
    if (-not (Test-Path "$PSScriptRoot\..\vcpkg\scripts\buildsystems\vcpkg.cmake")) { throw 'Set VCPKG_ROOT to a vcpkg checkout (or clone vcpkg into ..\vcpkg).' }
    $env:VCPKG_ROOT = (Resolve-Path "$PSScriptRoot\..\vcpkg").Path
}

# CommonLibSSE-NG is a git submodule: fetch it if it isn't there yet (its own openvr submodule isn't needed).
if (-not (Test-Path extern\CommonLibSSE-NG\CMakeLists.txt)) {
    git submodule update --init
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}

if ($Clean -and (Test-Path build)) { Remove-Item build -Recurse -Force }

# Temp files into build\tmp: the PDB records the linker's temp file path, and the default TEMP is in the user profile.
New-Item -ItemType Directory -Force build\tmp | Out-Null
$env:TEMP = $env:TMP = (Resolve-Path build\tmp).Path

if (-not (Test-Path build\build.ninja)) {
    cmake --preset release
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
cmake --build --preset release
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# Package: SKSE\Plugins\OStimAutoAlign.dll (+ .pdb for crash-log symbols) and the readme.
$version = (Select-String -Path CMakeLists.txt -Pattern 'VERSION (\d+\.\d+\.\d+)' | Select-Object -First 1).Matches[0].Groups[1].Value
$stage = Join-Path $PSScriptRoot 'build\package'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force "$stage\SKSE\Plugins" | Out-Null
Copy-Item build\OStimAutoAlign.dll, build\OStimAutoAlign.pdb "$stage\SKSE\Plugins\"
Copy-Item README.md "$stage\OStimAutoAlign_README.md"
Copy-Item data\SKSE "$stage" -Recurse -Force
New-Item -ItemType Directory -Force dist | Out-Null
$zip = Join-Path $PSScriptRoot "dist\OStim Auto Align $version.zip"
if (Test-Path $zip) { Remove-Item $zip }
# Not Compress-Archive: Windows PowerShell 5.1 writes '\' into entry names.
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::Open($zip, 'Create')
try {
    Get-ChildItem $stage -Recurse -File | ForEach-Object {
        $entry = $_.FullName.Substring($stage.Length + 1).Replace('\', '/')
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $_.FullName, $entry) | Out-Null
    }
} finally {
    $archive.Dispose()
}
Write-Host "Packaged $zip"
