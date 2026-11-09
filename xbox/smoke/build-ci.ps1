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

$stage = Join-Path $env:RUNNER_TEMP 'nsmbu-smoke-stage'
$out = Join-Path $PSScriptRoot 'output'
New-Item -ItemType Directory -Path $stage,$out -Force | Out-Null
Copy-Item $exe.FullName (Join-Path $stage 'NSMBUXboxSmoke.exe')
Copy-Item (Join-Path $PSScriptRoot 'Package.appxmanifest') (Join-Path $stage 'AppxManifest.xml')
Copy-Item (Join-Path $PSScriptRoot 'Assets') (Join-Path $stage 'Assets') -Recurse

# C++/CX needs the OneCore CRT, not the Win32 desktop runtime.
$redistRoot = Join-Path $vs 'VC/Redist/MSVC'
$crtNames = @(
    'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll',
    'msvcp140_atomic_wait.dll', 'vccorlib140.dll',
    'vcruntime140.dll', 'vcruntime140_1.dll'
)
$crtFolder = $null
if (Test-Path $redistRoot) {
    foreach ($candidate in (Get-ChildItem $redistRoot -Directory | Sort-Object Name -Descending)) {
        $possible = Join-Path $candidate.FullName 'onecore/x64/Microsoft.VC143.CRT'
        $missing = @($crtNames | Where-Object { -not (Test-Path (Join-Path $possible $_)) })
        if ($missing.Count -eq 0) { $crtFolder = $possible; break }
    }
}
if (-not $crtFolder) { throw 'Complete OneCore CRT redistributable not found' }
foreach ($name in $crtNames) {
    Copy-Item (Join-Path $crtFolder $name) (Join-Path $stage $name)
}
Write-Host "OneCore CRT: $crtFolder"

$tools = Join-Path $sdkRoot "bin/$sdk/x64"
$makeappx = Join-Path $tools 'makeappx.exe'
$signtool = Join-Path $tools 'signtool.exe'
if (-not (Test-Path $makeappx) -or -not (Test-Path $signtool)) {
    throw 'Windows SDK app packaging tools missing'
}

$appx = Join-Path $out 'NSMBUXboxSmoke_0.1.0.0_x64.appx'
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
foreach ($file in @('NSMBUXboxSmoke.exe', 'AppxManifest.xml', 'Assets/Square150x150Logo.png')) {
    if (-not (Test-Path (Join-Path $unpack $file))) { throw "Appx missing $file" }
}
Write-Host "APPX: $appx"
Write-Host "SHA256: $((Get-FileHash $appx -Algorithm SHA256).Hash)"
Write-Host "Public developer certificate: $cer"
