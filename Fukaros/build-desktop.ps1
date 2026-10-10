param([string]$QtRoot='D:\PC\EXE\QtCreator',[switch]$Package,[string]$PlatformTools)
$ErrorActionPreference='Stop'
$kit=Join-Path $QtRoot '6.11.1\mingw_64'
$compiler=Join-Path $QtRoot 'Tools\mingw1310_64\bin'
$cmake=Join-Path $QtRoot 'Tools\CMake_64\bin\cmake.exe'
$ninja=Join-Path $QtRoot 'Tools\Ninja\ninja.exe'
$env:PATH="$kit\bin;$compiler;"+$env:PATH
& $cmake -S $PSScriptRoot -B (Join-Path $PSScriptRoot 'build-desktop') -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_PREFIX_PATH=$kit" '-DCMAKE_BUILD_TYPE=Release'
if($LASTEXITCODE){throw 'CMake configure failed'}
& $cmake --build (Join-Path $PSScriptRoot 'build-desktop') --parallel 4
if($LASTEXITCODE){throw 'Build failed'}
& (Join-Path $QtRoot 'Tools\CMake_64\bin\ctest.exe') --test-dir (Join-Path $PSScriptRoot 'build-desktop') --output-on-failure
if($LASTEXITCODE){throw 'Tests failed'}
if($Package){
 $dest=Join-Path $PSScriptRoot 'bin';New-Item -ItemType Directory -Path $dest -Force | Out-Null
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'build-desktop\Fukaros.exe') -Destination $dest
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'build-desktop\virtual_display_helper.exe') -Destination $dest
 $driverDest=Join-Path $dest 'virtual-display';New-Item -ItemType Directory -Path $driverDest -Force | Out-Null
 Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'core\virtual-display\driver') -File | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $driverDest -Force}
 & (Join-Path $kit 'bin\windeployqt.exe') --release --no-translations (Join-Path $dest 'Fukaros.exe')
 if($LASTEXITCODE){throw 'Deployment failed'}
 foreach($dll in @('avcodec-61.dll','avutil-59.dll','swscale-8.dll','swresample-5.dll')){Copy-Item -LiteralPath (Join-Path $kit ('bin\'+$dll)) -Destination $dest}
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'core\vendor\ffmpeg\COPYING.LGPLv2.1') -Destination (Join-Path $dest 'FFmpeg-LGPL-2.1.txt')
}

if($Package){
 if(!$PlatformTools -and $env:ANDROID_SDK_ROOT){$PlatformTools=Join-Path $env:ANDROID_SDK_ROOT 'platform-tools'}
 if($PlatformTools){
  $adbDir=Join-Path $dest 'adb';New-Item -ItemType Directory -Path $adbDir -Force | Out-Null
  foreach($file in @('adb.exe','AdbWinApi.dll','AdbWinUsbApi.dll','NOTICE.txt')){Copy-Item -LiteralPath (Join-Path $PlatformTools $file) -Destination $adbDir}
 }
 $licenseDir=Join-Path $dest 'licenses';New-Item -ItemType Directory -Path $licenseDir -Force | Out-Null
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\docs\THIRD_PARTY.md') -Destination (Join-Path $licenseDir 'THIRD_PARTY.md')
 foreach($license in @('COPYING.LGPLv2.1','LICENSE.md')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('core\vendor\ffmpeg\'+$license)) -Destination $licenseDir}
 Copy-Item -Path (Join-Path $PSScriptRoot '..\docs\licenses\*') -Destination $licenseDir
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\docs\FFmpeg-runtime.json') -Destination $licenseDir
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\README.md') -Destination (Join-Path $dest 'README.md')
}
