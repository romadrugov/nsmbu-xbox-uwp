# Build/sign separate UWP Vulkan/Dozen app. R7D remains unmodified.
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$VulkanHeadersRoot,
  [Parameter(Mandatory=$true)][string]$OutputDir,
  [string]$DozenDll = '',
  [string]$DxilDll = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.TrimEnd('\','/')
$out = [IO.Path]::GetFullPath($OutputDir).TrimEnd('\','/')
$vk = (Resolve-Path $VulkanHeadersRoot).Path
if (-not (Test-Path (Join-Path $vk 'include/vulkan/vulkan.h'))) {
  throw 'Pinned Khronos Vulkan headers are missing'
}
if ($out.Equals($root,[StringComparison]::OrdinalIgnoreCase) -or
    $out.StartsWith($root+'\', [StringComparison]::OrdinalIgnoreCase)) {
  throw 'R9 output must stay outside public checkout'
}
if (([bool]$DozenDll) -ne ([bool]$DxilDll)) {
  throw 'Both packaged Dozen and compatible DXIL are required together'
}
if (-not (Test-Path $out)) { New-Item -ItemType Directory -Path $out | Out-Null }
if (@(Get-ChildItem $out -Force -ErrorAction SilentlyContinue).Count -ne 0) {
  throw 'R9 output dir must be empty'
}
if ($DozenDll) {
  $DozenDll = (Resolve-Path $DozenDll).Path
  $DxilDll = (Resolve-Path $DxilDll).Path
}
$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -property installationPath | Select-Object -First 1
if (-not $vs) { throw 'Visual Studio missing' }
$msbuild = Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
$kit = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Windows Kits/10'
$sdk = @(Get-ChildItem (Join-Path $kit 'Include') -Directory |
  Where-Object { $_.Name -match '^10\.0\.\d+\.0$' } |
  Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1)
if ($sdk.Count -ne 1) { throw 'Windows SDK unavailable' }
$version = $sdk[0].Name
$makeappx = Join-Path $kit "bin/$version/x64/makeappx.exe"
$signtool = Join-Path $kit "bin/$version/x64/signtool.exe"
if (-not (Test-Path $makeappx) -or -not (Test-Path $signtool)) {
  throw 'AppX SDK signing tools missing'
}
$build = Join-Path $out 'build'
New-Item -ItemType Directory -Path $build | Out-Null
& py -3 (Join-Path $root 'xbox/smoke/make_assets.py')
if ($LASTEXITCODE -ne 0) { throw 'Xbox icon generation failed' }
$proj = Join-Path $root 'xbox/r9/NSMBUDozenProbe.vcxproj'
$arguments = @(
  $proj, '/t:Build','/m','/v:minimal',
  '/p:Configuration=Release','/p:Platform=x64',
  "/p:WindowsTargetPlatformVersion=$version",
  "/p:VulkanHeadersRoot=$vk", "/p:OutDir=$build\",
  '/p:GenerateAppxPackageOnBuild=false',
  '/p:AppxPackageSigningEnabled=false'
)
& $msbuild @arguments
if ($LASTEXITCODE -ne 0) { throw 'R9 UWP Vulkan application build failed' }
foreach ($file in @('NSMBUDozenProbe.exe','NSMBUDozenProbe.winmd','resources.pri')) {
  if (-not (Test-Path (Join-Path $build $file))) { throw "Missing R9 output: $file" }
}
$exe = Join-Path $build 'NSMBUDozenProbe.exe'
$bytes = [IO.File]::ReadAllBytes($exe)
$pe = [BitConverter]::ToInt32($bytes,0x3c)
if ($bytes[$pe] -ne 80 -or $bytes[$pe+1] -ne 69 -or
    [BitConverter]::ToUInt16($bytes,$pe+4) -ne 0x8664 -or
    [BitConverter]::ToUInt16($bytes,$pe+24+68) -ne 2) {
  throw 'R9 executable is not x64 Windows GUI'
}
$dllCharacteristics = [BitConverter]::ToUInt16($bytes,$pe+24+70)
if (($dllCharacteristics -band 0x1000) -eq 0) {
  throw 'R9 EXE lacks /APPCONTAINER bit'
}
$stage = Join-Path $out 'stage'
New-Item -ItemType Directory -Path $stage | Out-Null
foreach ($file in @('NSMBUDozenProbe.exe','NSMBUDozenProbe.winmd','resources.pri')) {
  Copy-Item (Join-Path $build $file) (Join-Path $stage $file)
}
Copy-Item (Join-Path $root 'xbox/r9/PackageDozenProbe.appxmanifest') (Join-Path $stage 'AppxManifest.xml')
Copy-Item (Join-Path $root 'xbox/smoke/Assets') (Join-Path $stage 'Assets') -Recurse
if ($DozenDll) {
  Copy-Item $DozenDll (Join-Path $stage 'vulkan_dzn.dll')
  Copy-Item $DxilDll (Join-Path $stage 'dxil.dll')
  $msvc = @(Get-ChildItem (Join-Path $vs 'VC/Tools/MSVC') -Directory |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1)
  $dumpbin = Join-Path $msvc[0].FullName 'bin/Hostx64/x64/dumpbin.exe'
  $deps = & $dumpbin /dependents $DozenDll | Out-String
  Write-Host "===== Dozen DLL dependencies ====="
  Write-Host $deps
  if ($deps -match '(?im)^\s*(vcruntime140|ucrtbase|msvcp140)\.dll\s*$') {
    throw 'Dozen imports desktop CRT instead of UWP _APP CRT'
  }
}
$vclibsRelative = 'Microsoft SDKs/Windows Kits/10/ExtensionSDKs/Microsoft.VCLibs/14.0/Appx/Retail/x64/Microsoft.VCLibs.x64.14.00.appx'
$vclibs = @(
  (Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) $vclibsRelative),
  (Join-Path ([Environment]::GetFolderPath('ProgramFiles')) $vclibsRelative)
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vclibs) { throw 'UWP VCLibs package missing' }
Copy-Item $vclibs (Join-Path $out 'Microsoft.VCLibs.x64.14.00.appx')
$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=NSMBUXbox' -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 2048 -KeyExportPolicy Exportable -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(1)
$password = [Convert]::ToBase64String([Security.Cryptography.RandomNumberGenerator]::GetBytes(24))
$secure = ConvertTo-SecureString $password -AsPlainText -Force
$pfx = Join-Path $env:RUNNER_TEMP 'r9-uwp-signing.pfx'
Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $secure | Out-Null
Export-Certificate -Cert $cert -FilePath (Join-Path $out 'NSMBUDozenProbe.cer') | Out-Null
$appx = Join-Path $out 'NSMBUDozenProbe_R9_x64.appx'
& $makeappx pack /d $stage /p $appx /o
if ($LASTEXITCODE -ne 0) { throw 'R9 makeappx failed' }
& $signtool sign /fd SHA256 /f $pfx /p $password $appx
if ($LASTEXITCODE -ne 0) { throw 'R9 signtool failed' }
$verify = Join-Path $out 'verify'
& $makeappx unpack /p $appx /d $verify /o
if ($LASTEXITCODE -ne 0) { throw 'R9 unpack verification failed' }
foreach ($file in @('NSMBUDozenProbe.exe','NSMBUDozenProbe.winmd','resources.pri','AppxManifest.xml','Assets/Square150x150Logo.png')) {
  if (-not (Test-Path (Join-Path $verify $file))) {
    throw "R9 AppX missing: $file"
  }
}
if ($DozenDll -and ((-not (Test-Path (Join-Path $verify 'vulkan_dzn.dll'))) -or
                   (-not (Test-Path (Join-Path $verify 'dxil.dll'))))) {
  throw 'R9 Dozen/DXIL absent from signed AppX'
}
Remove-Item -LiteralPath $stage -Recurse -Force
Remove-Item -LiteralPath $verify -Recurse -Force
Write-Host 'R9 SIGNED APPX PASS: independent Vulkan/Dozen GPU gateway'
Write-Host "Packaged driver: $([bool]$DozenDll)"
Write-Host "SHA256: $((Get-FileHash $appx -Algorithm SHA256).Hash)"
Write-Host 'Synthetic frame only; Nintendo assets/game code never packaged.'
