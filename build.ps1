param([switch]$SkipTests, [switch]$NoPublish)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
# Stage in an ASCII path: NMake/MSVC installations differ in Unicode path support.
$stage = Join-Path $env:LOCALAPPDATA 'CppToolboxBuild'
New-Item -ItemType Directory -Force $stage | Out-Null
foreach ($item in @('src','assets','tests','CMakeLists.txt')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $item) -Destination $stage -Recurse -Force
}
$vsRoots = @("$env:ProgramFiles\Microsoft Visual Studio\18\Insiders", "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools", "$env:ProgramFiles\Microsoft Visual Studio\2022\Community")
$vcvars = $vsRoots | ForEach-Object { Join-Path $_ 'VC\Auxiliary\Build\vcvars64.bat' } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (!$vcvars) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $install = & $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($install) { $vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat' }
    }
}
if (!$vcvars) { throw 'Visual Studio C++ desktop tools are required.' }
# The batch file contains only ASCII paths and has no deletion or move commands.
$envScript = Join-Path $stage 'environment.cmd'
[IO.File]::WriteAllText($envScript,"@echo off`r`ncall `"$vcvars`" >nul`r`nset`r`n",[Text.Encoding]::ASCII)
$lines = & cmd.exe /d /c $envScript
foreach ($line in $lines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process') }
}
& cmake -S $stage -B "$stage\out" -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
if ($LASTEXITCODE) { throw 'CMake configure failed' }
& cmake --build "$stage\out" --parallel
if ($LASTEXITCODE) { throw 'C++ compilation failed' }
if (!$SkipTests) {
    & ctest --test-dir "$stage\out" --output-on-failure
    if ($LASTEXITCODE) { throw 'Core tests failed' }
}
if ($NoPublish) { Write-Output "Validated staged EXE: $stage\out\CppToolbox.exe"; return }
New-Item -ItemType Directory -Force (Join-Path $projectRoot 'dist') | Out-Null
Copy-Item -LiteralPath "$stage\out\CppToolbox.exe" -Destination (Join-Path $projectRoot 'dist\CppToolbox.exe') -Force
Write-Output "Built: $projectRoot\dist\CppToolbox.exe"
