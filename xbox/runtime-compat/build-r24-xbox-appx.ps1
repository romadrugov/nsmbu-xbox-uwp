# R2.4: prepare an independent signed UWP diagnostic AppX.
# Without PrivateInputDirectory, use ONLY public synthetic code for CI.
# With PrivateInputDirectory, use the TWO user-supplied local COFF objects.
# The private inputs must remain outside any public Git checkout.
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

$bridge = Join-Path $objects 'r24bridge.obj'
$flags = @('/nologo','/c','/O2','/MD','/EHsc','/clang:-std=c++20',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_APP','/DNSMBU_XBOX_UWP=1',
    '/D_CRT_SECURE_NO_WARNINGS',
    "/I$(Join-Path $root 'runtime/src')","/I$(Join-Path $root 'runtime/include')",
    "/Fo$bridge",(Join-Path $dir 'r24_xbox_device_bridge.cpp'))
& $clang @flags
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $bridge)) { throw 'R2.4 real HLE bridge compilation failed' }
Write-Host 'R2.4 HLE bridge COMPILE PASS'

# Compile R2.4 public dispatch bridge without compiling/copying game code.
$r24Bridge = Join-Path $objects 'r24bridge.obj'
$r24Flags = @('/nologo','/c','/O2','/MD','/EHsc','/clang:-std=c++20',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_APP','/DNSMBU_XBOX_UWP=1',
    '/D_CRT_SECURE_NO_WARNINGS',
    "/I$(Join-Path $root 'runtime/src')", "/I$(Join-Path $root 'runtime/include')",
    "/Fo$r24Bridge", (Join-Path $dir 'r24_private_dispatch.cpp'))
& $clang @r24Flags
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $r24Bridge)) { throw 'R2.4 dispatch bridge compilation failed' }

# The compiler sees only a static C ABI; proprietary function bodies are
# NEVER added to the public repo or emitted in public CI logs.
if ($PrivateInputDirectory) {
    $privateDir = (Resolve-Path $PrivateInputDirectory -ErrorAction Stop).Path
    $selectedObj = Join-Path $privateDir 'r24_selected.obj'
    $bindingObj = Join-Path $privateDir 'r24_private_binding.obj'
    Write-Host 'R2.4 PRIVATE mode: using externally staged COFF inputs'
} else {
    $selectedObj = Join-Path $objects 'r24_public_selected.obj'
    $bindingObj = Join-Path $objects 'r24_public_only_binding.obj'
    $publicCFlags = @('/nologo','/c','/O2','/MD','/clang:-std=c11',
        '/DWINAPI_FAMILY=WINAPI_FAMILY_APP',
        "/I$(Join-Path $root 'runtime/include')")
    foreach ($name in @('r24_public_selected.c','r24_public_only_binding.c')) {
        $dst = Join-Path $objects ($name -replace '\.c

$msbuildArgs = @((Join-Path $dir 'NSMBUR24XboxProbe.vcxproj'),'/t:Build','/m','/v:minimal',
    '/p:Configuration=Release','/p:Platform=x64',
    "/p:WindowsTargetPlatformVersion=$($sdk.Name)","/p:R2ZlibLib=$zlibLib",
    '/p:GenerateAppxPackageOnBuild=false','/p:AppxPackageSigningEnabled=false',
    "/p:R24SelectedObj=$selectedObj", "/p:R24BindingObj=$bindingObj")
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
"R2.4 signed AppX **PACKAGE PASS**. Independent x64 AppContainer identity, authentic core/HLE + public fixture. SHA256: $sha. **No physical Xbox execution verified.** Private game content is not in the public repository." >> $env:GITHUB_STEP_SUMMARY
, '.obj')
        & $clang @publicCFlags "/Fo$dst" (Join-Path $dir $name)
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $dst)) { throw "Public R2.4 synthetic compilation failed: $name" }
    }
    Write-Host 'R2.4 PUBLIC mode: only synthetic selected code is bound'
}
foreach ($file in @($selectedObj, $bindingObj)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing R2.4 object $file" }
    if ((Get-Item -LiteralPath $file).Length -gt 262144) { throw "R2.4 object exceeds strict bounded leaf size: $file" }
    $machine = & $dumpbin /headers $file | Out-String
    if ($LASTEXITCODE -ne 0 -or $machine -notmatch '(?i)8664 machine \(x64\)') {
        throw "R2.4 input is not Windows AMD64 COFF: $file"
    }
}
$selectedSymbols = & $dumpbin /symbols $selectedObj | Out-String
if ($LASTEXITCODE -ne 0 -or $selectedSymbols -notmatch 'nsmbu_r24_private_function') {
    throw 'R2.4 selected object lacks expected exported C ABI symbol'
}
$bindingSymbols = & $dumpbin /symbols $bindingObj | Out-String
foreach ($symbol in @('g_r24_private_function','g_r24_guest_address','g_r24_expected_r3',
                     'g_r24_expected_r4','g_r24_expected_lr')) {
    if ($LASTEXITCODE -ne 0 -or $bindingSymbols -notmatch $symbol) {
        throw "R2.4 binding object missing C ABI symbol: $symbol"
    }
}
Write-Host 'R2.4 two x64 COFF objects and ABI symbols: PASS'


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
