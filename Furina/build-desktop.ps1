param([string]$QtRoot='D:\PC\EXE\QtCreator')
$ErrorActionPreference='Stop'
$kit=Join-Path $QtRoot '6.11.1\mingw_64';$compiler=Join-Path $QtRoot 'Tools\mingw1310_64\bin'
$cmake=Join-Path $QtRoot 'Tools\CMake_64\bin\cmake.exe';$ninja=Join-Path $QtRoot 'Tools\Ninja\ninja.exe'
$env:PATH="$kit\bin;$compiler;"+$env:PATH
& $cmake -S $PSScriptRoot -B (Join-Path $PSScriptRoot 'build-desktop') -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_PREFIX_PATH=$kit" '-DCMAKE_BUILD_TYPE=Release'
if($LASTEXITCODE){throw 'CMake configure failed'}
& $cmake --build (Join-Path $PSScriptRoot 'build-desktop') --parallel 4
if($LASTEXITCODE){throw 'Build failed'}
& (Join-Path $QtRoot 'Tools\CMake_64\bin\ctest.exe') --test-dir (Join-Path $PSScriptRoot 'build-desktop') --output-on-failure
if($LASTEXITCODE){throw 'Desktop simulation tests failed'}
Write-Host 'Desktop simulation built. This executable is not an Android APK.'
