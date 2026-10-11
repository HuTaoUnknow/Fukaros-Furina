param([Parameter(Mandatory=$true)][string]$Iscc)
$ErrorActionPreference='Stop'
$packageDir=Join-Path $PSScriptRoot 'bin'
if(!(Test-Path (Join-Path $packageDir 'Fukaros.exe'))){throw 'Run build-desktop.ps1 -Package first'}
$outputDir=Join-Path $PSScriptRoot 'dist'
& $Iscc "/DPackageDir=$packageDir" "/DOutputDir=$outputDir" (Join-Path $PSScriptRoot 'packaging\Fukaros.iss')
if($LASTEXITCODE){throw 'Installer compilation failed'}
Get-Item (Join-Path $outputDir 'Fukaros-0.4.3-windows-x64-setup.exe')
