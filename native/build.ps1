[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectDir = $PSScriptRoot
$buildDir = Join-Path $projectDir 'build'
$outputDir = Join-Path $projectDir 'out'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer was not found. Install Visual Studio 2022 C++ Build Tools first.'
}

$installationPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath

if (-not $installationPath) {
    throw 'MSVC x64 compiler tools were not found.'
}

$bundledCmake = Join-Path $installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$cmake = if (Test-Path -LiteralPath $bundledCmake) {
    $bundledCmake
} else {
    (Get-Command cmake.exe -ErrorAction Stop).Source
}
$cmakeBin = Split-Path -Parent $cmake
$env:Path = "$cmakeBin;$env:Path"

& $cmake -S $projectDir -B $buildDir -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake --build $buildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake --install $buildDir --config $Configuration --prefix $outputDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "WinCal executable: $(Join-Path $outputDir 'WinCal.exe')"
