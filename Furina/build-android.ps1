param(
 [Parameter(Mandatory=$true)][string]$Sdk,
 [Parameter(Mandatory=$true)][string]$Ndk,
 [Parameter(Mandatory=$true)][string]$Jdk,
 [string]$QtRoot='D:\PC\EXE\QtCreator',
 [ValidateSet('arm64-v8a','armeabi-v7a','x86','x86_64')][string]$Abi='arm64-v8a',
 [ValidateSet('Debug','Release')][string]$Configuration='Debug'
)
$ErrorActionPreference='Stop'
$kits=@{'arm64-v8a'='android_arm64_v8a';'armeabi-v7a'='android_armv7';'x86'='android_x86';'x86_64'='android_x86_64'}
$kit=Join-Path $QtRoot (Join-Path '6.11.1' $kits[$Abi]);$hostKit=Join-Path $QtRoot '6.11.1\mingw_64'
$cmake=Join-Path $QtRoot 'Tools\CMake_64\bin\cmake.exe';$ninja=Join-Path $QtRoot 'Tools\Ninja\ninja.exe'
$build=Join-Path $PSScriptRoot "build-$Abi"
foreach($path in @((Join-Path $Sdk 'platforms\android-36\android.jar'),(Join-Path $Sdk 'build-tools\36.0.0\aapt2.exe'),(Join-Path $Ndk 'build\cmake\android.toolchain.cmake'),(Join-Path $Jdk 'bin\java.exe'),(Join-Path $Jdk 'bin\jlink.exe'),(Join-Path $kit 'lib\cmake\Qt6\qt.toolchain.cmake'))){if(!(Test-Path -LiteralPath $path)){throw "Required file missing: $path"}}
$env:GRADLE_OPTS='-Dorg.gradle.daemon=false'
$env:JAVA_HOME=$Jdk;$env:ANDROID_SDK_ROOT=$Sdk;$env:ANDROID_NDK_ROOT=$Ndk
$env:GRADLE_USER_HOME=Join-Path $PSScriptRoot 'build-tool-cache\gradle'
$env:ANDROID_USER_HOME=Join-Path $PSScriptRoot 'build-tool-cache\android'
$env:PATH="$Jdk\bin;$hostKit\bin;"+(Join-Path $QtRoot 'Tools\Ninja')+';'+$env:PATH
& $cmake -S $PSScriptRoot -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$kit/lib/cmake/Qt6/qt.toolchain.cmake" "-DQT_HOST_PATH=$hostKit" "-DANDROID_SDK_ROOT=$Sdk" "-DANDROID_NDK_ROOT=$Ndk" "-DANDROID_ABI=$Abi" '-DANDROID_PLATFORM=android-28' "-DCMAKE_BUILD_TYPE=$Configuration"
if($LASTEXITCODE){throw 'Android configure failed'}
& $cmake --build $build --target apk --parallel 4
if($LASTEXITCODE){throw 'Android APK build failed'}
Get-ChildItem -LiteralPath $build -Recurse -Filter '*.apk' | Select-Object FullName
