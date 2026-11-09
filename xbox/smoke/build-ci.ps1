$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# This workflow builds ONLY the standalone open-source smoke test.
# No Nintendo game data is read, copied, downloaded or uploaded.
& py -3 (Join-Path $PSScriptRoot 'make_assets.py')
if ($LASTEXITCODE -ne 0) { throw 'PNG asset generation failed' }

$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = (& $vswhere -latest -products '*' -property installationPath | Select-Object -First 1)
if (-not $vs) { throw 'Visual Studio not found' }
$msbuild = Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
if (-not (Test-Path $msbuild)) { throw "Missing MSBuild: $msbuild" }

$sdkRoot = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Windows Kits/10'
$versions = @(Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory |
    Where-Object { $_.Name -match '^10\.0\.\d+\.0$' } |
    Sort-Object { [version]$_.Name } -Descending)
if ($versions.Count -eq 0) { throw 'Windows SDK missing' }
$sdk = $versions[0].Name
Write-Host "Building with SDK $sdk and VS $vs"

$project = Join-Path $PSScriptRoot 'NSMBUXboxSmoke.vcxproj'
$buildArgs = @(
    $project, '/t:Build', '/m', '/v:minimal',
    '/p:Configuration=Release', '/p:Platform=x64',
    "/p:WindowsTargetPlatformVersion=$sdk",
    '/p:GenerateAppxPackageOnBuild=false',
    '/p:AppxPackageSigningEnabled=false'
)
& $msbuild @buildArgs
if ($LASTEXITCODE -ne 0) { throw "UWP build failed: $LASTEXITCODE" }

$exe = Get-ChildItem $PSScriptRoot -Filter 'NSMBUXboxSmoke.exe' -File -Recurse |
    Select-Object -First 1
if (-not $exe) { throw 'UWP executable missing after build' }
Write-Host "Compiled: $($exe.FullName) ($($exe.Length) bytes)"

# Record the exact DLL names imported by the PE. Missing _APP DLLs can
# prevent Xbox UWP activation before application code and logs start.
$toolsetRoot = Join-Path $vs 'VC/Tools/MSVC'
$msvcVersions = @(Get-ChildItem $toolsetRoot -Directory | Sort-Object { [version]$_.Name } -Descending)
if ($msvcVersions.Count -eq 0) { throw 'MSVC toolset missing' }
$dumpbin = Join-Path $msvcVersions[0].FullName 'bin/Hostx64/x64/dumpbin.exe'
if (-not (Test-Path $dumpbin)) { throw "No dumpbin: $dumpbin" }
Write-Host "==== PE import dependencies ===="
& $dumpbin /dependents $exe.FullName
if ($LASTEXITCODE -ne 0) { throw 'dumpbin failed' }
Write-Host "==== Generated Windows metadata and resources ===="
Get-ChildItem (Join-Path $PSScriptRoot 'x64') -Recurse -File |
  Where-Object { $_.Extension -in @('.winmd', '.pri') } |
  ForEach-Object { Write-Host "$($_.FullName) ($($_.Length) bytes)" }


# A graphical Xbox UWP app must not be linked as a Windows console executable.
# IMAGE_OPTIONAL_HEADER64.Subsystem is 68 bytes from the PE optional header.
$peBytes = [IO.File]::ReadAllBytes($exe.FullName)
$peOffset = [BitConverter]::ToInt32($peBytes, 0x3c)
if ($peBytes[$peOffset] -ne 80 -or $peBytes[$peOffset + 1] -ne 69) {
    throw 'Invalid PE signature'
}
$subsystem = [BitConverter]::ToUInt16($peBytes, $peOffset + 24 + 68)
Write-Host "PE subsystem: $subsystem (2 = GUI; 3 = console)"
if ($subsystem -ne 2) { throw 'Unexpected console subsystem in Xbox graphical application' }


$stage = Join-Path $env:RUNNER_TEMP 'nsmbu-smoke-stage'
$out = Join-Path $PSScriptRoot 'output'
New-Item -ItemType Directory -Path $stage,$out -Force | Out-Null
Copy-Item $exe.FullName (Join-Path $stage 'NSMBUXboxSmoke.exe')
Copy-Item (Join-Path $PSScriptRoot 'Package.appxmanifest') (Join-Path $stage 'AppxManifest.xml')
Copy-Item (Join-Path $PSScriptRoot 'Assets') (Join-Path $stage 'Assets') -Recurse

# MSVC UWP images import VCRUNTIME140_APP / VCRUNTIME140_1_APP /
# VCCORLIB140_APP. These live in the Microsoft.VCLibs framework package.
# Do NOT stage desktop CRT DLLs (without _APP suffix): they are not substitutes.
$metadata = @('NSMBUXboxSmoke.winmd', 'resources.pri')
foreach ($name in $metadata) {
    $src = Join-Path $exe.DirectoryName $name
    if (-not (Test-Path $src)) { throw "Missing UWP activation metadata $src" }
    Copy-Item $src (Join-Path $stage $name)
    Write-Host "Bundled WinRT metadata: $name"
}

$vclibsRelative = 'Microsoft SDKs/Windows Kits/10/ExtensionSDKs/Microsoft.VCLibs/14.0/Appx/Retail/x64/Microsoft.VCLibs.x64.14.00.appx'
$vclibsCandidates = @(
    (Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) $vclibsRelative),
    (Join-Path ([Environment]::GetFolderPath('ProgramFiles')) $vclibsRelative)
)
$vclibs = $vclibsCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vclibs) { throw "Missing UWP VCLibs framework appx. Install C++ Universal Windows Platform Tools; searched $($vclibsCandidates -join ', ')" }
Copy-Item $vclibs (Join-Path $out 'Microsoft.VCLibs.x64.14.00.appx')
Write-Host "UWP framework dependency: $vclibs"

$tools = Join-Path $sdkRoot "bin/$sdk/x64"
$makeappx = Join-Path $tools 'makeappx.exe'
$signtool = Join-Path $tools 'signtool.exe'
if (-not (Test-Path $makeappx) -or -not (Test-Path $signtool)) {
    throw 'Windows SDK app packaging tools missing'
}

$appx = Join-Path $out 'NSMBUXboxSmoke_0.1.4.0_x64.appx'
$cer = Join-Path $out 'NSMBUXboxSmoke.cer'
$pfx = Join-Path $env:RUNNER_TEMP 'nsmbu-smoke-private.pfx'
$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=NSMBUXbox' -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 2048 -KeyExportPolicy Exportable -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(1)
$password = [Convert]::ToBase64String([Security.Cryptography.RandomNumberGenerator]::GetBytes(24))
$secure = ConvertTo-SecureString $password -AsPlainText -Force
Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $secure | Out-Null
Export-Certificate -Cert $cert -FilePath $cer | Out-Null

& $makeappx pack /d $stage /p $appx /o
if ($LASTEXITCODE -ne 0) { throw 'makeappx pack failed' }
& $signtool sign /fd SHA256 /f $pfx /p $password $appx
if ($LASTEXITCODE -ne 0) { throw 'signtool sign failed' }

$unpack = Join-Path $env:RUNNER_TEMP 'nsmbu-smoke-unpacked'
& $makeappx unpack /p $appx /d $unpack /o
if ($LASTEXITCODE -ne 0) { throw 'makeappx validation failed' }
foreach ($file in @('NSMBUXboxSmoke.exe', 'AppxManifest.xml', 'NSMBUXboxSmoke.winmd', 'resources.pri', 'Assets/Square150x150Logo.png')) {
    if (-not (Test-Path (Join-Path $unpack $file))) { throw "Appx missing $file" }
}
Write-Host "APPX: $appx"
Write-Host "SHA256: $((Get-FileHash $appx -Algorithm SHA256).Hash)"
Write-Host "Public developer certificate: $cer"
