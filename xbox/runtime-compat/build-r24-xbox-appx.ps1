# R2.4 independent signed AppX: synthetic-only public mode or externally staged private COFF.
[CmdletBinding()]
param([string]$PrivateInputDirectory = '')
# R2.4 signed Xbox public runtime diagnostic package. No private games/assets.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference = $false
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$dir = $PSScriptRoot
$objects = Join-Path $dir 'r2_uwp_generated'

# Reuse the R2.2 verified production UWP objects, public 3-instruction
# recomp-generated table, and same-API-family write-watch behavior test.
& (Join-Path $dir 'test-r2-uwp-link.ps1')
if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw 'R2.2 UWP prerequisite failed' }
foreach ($name in @('core.obj','hle.obj','uwp_watch.obj','hooks.obj','code_000.obj','table.obj')) {
    if (-not (Test-Path (Join-Path $objects $name))) { throw "R2.2 object missing: $name" }
}

$clangOptions = @((Get-Command clang-cl.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1),'C:\Program Files\LLVM\bin\clang-cl.exe')
$clang = $clangOptions | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $clang) { throw 'clang-cl unavailable' }
$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -property installationPath | Select-Object -First 1
if (-not $vs) { throw 'Visual Studio unavailable' }
$msbuild = Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
$msvc = Get-ChildItem (Join-Path $vs 'VC/Tools/MSVC') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$dumpbin = Join-Path $msvc.FullName 'bin/Hostx64/x64/dumpbin.exe'
$sdkRoot = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Windows Kits/10'
$sdk = Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory | Where-Object { $_.Name -match '^10\.0\.\d+\.0$' } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $sdk) { throw 'Windows SDK missing' }
$vcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
if (-not $vcpkgRoot -or -not (Test-Path (Join-Path $vcpkgRoot 'vcpkg.exe'))) { $vcpkgRoot = 'C:\vcpkg' }
$libs = @(Get-ChildItem (Join-Path $vcpkgRoot 'installed/x64-uwp/lib') -Filter '*.lib' -File)
if ($libs.Count -ne 1) { throw 'UWP zlib archive is missing or ambiguous' }
$zlibLib = $libs[0].FullName

$bridge = Join-Path $objects 'r23bridge.obj'
$flags = @('/nologo','/c','/O2','/MD','/EHsc','/clang:-std=c++20',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_APP','/DNSMBU_XBOX_UWP=1',
    '/D_CRT_SECURE_NO_WARNINGS',
    "/I$(Join-Path $root 'runtime/src')","/I$(Join-Path $root 'runtime/include')",
    "/Fo$bridge",(Join-Path $dir 'r23_xbox_device_bridge.cpp'))
& $clang @flags
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $bridge)) { throw 'R2.4 real HLE bridge compilation failed' }
Write-Host 'R2.4 HLE bridge COMPILE PASS'

$msbuildArgs = @((Join-Path $dir 'NSMBUR24XboxProbe.vcxproj'),'/t:Build','/m','/v:minimal',
    '/p:Configuration=Release','/p:Platform=x64',
    "/p:WindowsTargetPlatformVersion=$($sdk.Name)","/p:R2ZlibLib=$zlibLib",
    '/p:GenerateAppxPackageOnBuild=false','/p:AppxPackageSigningEnabled=false')
& $msbuild @msbuildArgs
if ($LASTEXITCODE -ne 0) { throw 'R2.4 Xbox UWP build failed' }
$exe = Get-ChildItem $dir -Filter 'NSMBUR24XboxProbe.exe' -File -Recurse | Select-Object -First 1
if (-not $exe) { throw 'R2.4 executable missing' }
$headers = & $dumpbin /headers $exe.FullName | Out-String
if ($LASTEXITCODE -ne 0 -or $headers -notmatch 'machine \(x64\)' -or $headers -notmatch 'App Container' -or $headers -notmatch 'Windows GUI') {
    throw 'R2.4 executable lacks expected x64 AppContainer GUI flags'
}
$deps = & $dumpbin /dependents $exe.FullName | Out-String
if ($LASTEXITCODE -ne 0) { throw 'R2.4 PE dependency audit failed' }
if ($deps -match '(?im)^\s+(?:USER32|MSVCP140|VCRUNTIME140)\.dll\s*$') { throw 'Desktop CRT/UI dependency found' }
Write-Host "R2.4 x64 GUI AppContainer EXE: $($exe.Length) bytes"
Write-Host $deps

$stage = Join-Path $env:RUNNER_TEMP 'nsmbu-r24-stage'
$out = Join-Path $dir 'output-r24'
New-Item -ItemType Directory -Path $stage,$out -Force | Out-Null
Copy-Item $exe.FullName (Join-Path $stage 'NSMBUR24XboxProbe.exe')
Copy-Item (Join-Path $dir 'PackageR24.appxmanifest') (Join-Path $stage 'AppxManifest.xml')
Copy-Item (Join-Path $root 'xbox/smoke/Assets') (Join-Path $stage 'Assets') -Recurse -Force
foreach ($name in @('NSMBUR24XboxProbe.winmd','resources.pri')) {
    $source = Join-Path $exe.DirectoryName $name
    if (-not (Test-Path $source)) { throw "R2.4 missing activation metadata: $source" }
    Copy-Item $source (Join-Path $stage $name)
}

$vclibsRelative = 'Microsoft SDKs/Windows Kits/10/ExtensionSDKs/Microsoft.VCLibs/14.0/Appx/Retail/x64/Microsoft.VCLibs.x64.14.00.appx'
$vclibs = @(
    (Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) $vclibsRelative),
    (Join-Path ([Environment]::GetFolderPath('ProgramFiles')) $vclibsRelative)
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vclibs) { throw 'UWP VCLibs dependency missing' }
Copy-Item $vclibs (Join-Path $out 'Microsoft.VCLibs.x64.14.00.appx')
$sdkTools = Join-Path $sdkRoot "bin/$($sdk.Name)/x64"
$makeappx = Join-Path $sdkTools 'makeappx.exe'
$signtool = Join-Path $sdkTools 'signtool.exe'
if (-not (Test-Path $makeappx) -or -not (Test-Path $signtool)) { throw 'SDK AppX signing tools missing' }

# CI private signing key never leaves RUNNER_TEMP; only public .cer uploaded.
$appx = Join-Path $out 'NSMBUR24XboxProbe_0.8.4.0_x64.appx'
$cer = Join-Path $out 'NSMBUR24XboxProbe.cer'
$pfx = Join-Path $env:RUNNER_TEMP 'nsmbu-r24-private.pfx'
$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=NSMBUXbox' -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 2048 -KeyExportPolicy Exportable -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(1)
$password = [Convert]::ToBase64String([Security.Cryptography.RandomNumberGenerator]::GetBytes(24))
$secure = ConvertTo-SecureString $password -AsPlainText -Force
Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $secure | Out-Null
Export-Certificate -Cert $cert -FilePath $cer | Out-Null
& $makeappx pack /d $stage /p $appx /o
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $appx)) { throw 'R2.4 makeappx pack failed' }
& $signtool sign /fd SHA256 /f $pfx /p $password $appx
if ($LASTEXITCODE -ne 0) { throw 'R2.4 signtool signing failed' }

$unpacked = Join-Path $env:RUNNER_TEMP 'nsmbu-r24-unpacked'
& $makeappx unpack /p $appx /d $unpacked /o
if ($LASTEXITCODE -ne 0) { throw 'R2.4 AppX unpack validation failed' }
foreach ($name in @('NSMBUR24XboxProbe.exe','AppxManifest.xml','NSMBUR24XboxProbe.winmd','resources.pri','Assets/Square150x150Logo.png')) {
    if (-not (Test-Path (Join-Path $unpacked $name))) { throw "R2.4 AppX is missing $name" }
}
$sha = (Get-FileHash $appx -Algorithm SHA256).Hash
Write-Host 'R2.4 SIGNED APPX PACKAGE: PASS'
Write-Host "APPX: $appx"
Write-Host "SHA256: $sha"
Write-Host "Public developer certificate: $cer"
"R2.4 signed AppX **PACKAGE PASS**. Independent x64 AppContainer identity, authentic core/HLE + public fixture. SHA256: $sha. **No physical Xbox execution verified.**" >> $env:GITHUB_STEP_SUMMARY
