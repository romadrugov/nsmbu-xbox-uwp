# R12: privately package the real R11 WindowsStore x64 game executable
# with the exact R9-tested Mesa Dozen/DXIL binaries. NEVER include a game dump.
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$GameExe,
  [Parameter(Mandatory=$true)][string]$R9ProbeAppx,
  [Parameter(Mandatory=$true)][string]$VCLibsAppx,
  [Parameter(Mandatory=$true)][string]$OutputDir
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$exe = (Resolve-Path -LiteralPath $GameExe).Path
$probe = (Resolve-Path -LiteralPath $R9ProbeAppx).Path
$vclibs = (Resolve-Path -LiteralPath $VCLibsAppx).Path
$out = [IO.Path]::GetFullPath($OutputDir)
if (Test-Path -LiteralPath $out) { throw 'R12 package output folder must not exist yet' }
if ($out.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) {
  throw 'R12 package output must stay outside the public source checkout'
}
if ([IO.Path]::GetFileName($exe) -cne 'NSMBUFullRuntimeR11.exe' -or
    (Get-Item -LiteralPath $exe).Length -lt 200000) {
  throw 'Expected genuine, fully linked R11 UWP executable, not a probe'
}
$pe = [IO.File]::ReadAllBytes($exe)
$off = [BitConverter]::ToInt32($pe, 0x3c)
if ($off -lt 64 -or $off + 0x100 -ge $pe.Length -or
    [BitConverter]::ToUInt32($pe, $off) -ne 0x00004550 -or
    [BitConverter]::ToUInt16($pe, $off + 4) -ne 0x8664 -or
    [BitConverter]::ToUInt16($pe, $off + 24 + 68) -ne 2) {
  throw 'R11 EXE must be an x64 Windows GUI PE'
}
if (([BitConverter]::ToUInt16($pe, $off + 24 + 70) -band 0x1000) -eq 0) {
  throw 'R11 EXE lacks the required /APPCONTAINER bit'
}
$kit = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Windows Kits/10'
$versions = @(Get-ChildItem -LiteralPath (Join-Path $kit 'Bin') -Directory |
  Where-Object { $_.Name -match '^10\.0\.\d+\.0$' } |
  Sort-Object { [version]$_.Name } -Descending)
if ($versions.Count -eq 0) { throw 'Windows SDK packaging tools unavailable' }
$sdk = $versions[0].Name
$makeappx = Join-Path $kit "Bin/$sdk/x64/makeappx.exe"
$signtool = Join-Path $kit "Bin/$sdk/x64/signtool.exe"
if (-not (Test-Path $makeappx) -or -not (Test-Path $signtool)) {
  throw 'Missing makeappx.exe or signtool.exe'
}
New-Item -ItemType Directory -Path $out | Out-Null
$prior = Join-Path $out 'r9-unpacked'
& $makeappx unpack /p $probe /d $prior /o
if ($LASTEXITCODE -ne 0) { throw 'Cannot unpack Series S-tested R9 Dozen AppX' }
foreach ($name in @('vulkan_dzn.dll','dxil.dll')) {
  if (-not (Test-Path (Join-Path $prior $name))) {
    throw "R9 artifact lacks real driver: $name"
  }
}
$stage = Join-Path $out 'stage'
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $exe -Destination (Join-Path $stage 'NSMBUFullRuntimeR11.exe')
Copy-Item -LiteralPath (Join-Path $prior 'vulkan_dzn.dll') -Destination $stage
Copy-Item -LiteralPath (Join-Path $prior 'dxil.dll') -Destination $stage
$manifestSrc = Join-Path $root 'xbox/r12/PackageFullRuntime.appxmanifest'
[xml]$manifest = Get-Content -LiteralPath $manifestSrc -Raw
$identity = $manifest.Package.Identity
if ($identity.Name -cne 'Romadrugov.NSMBUFullRuntimeR12' -or
    $identity.Publisher -cne 'CN=NSMBUXboxR12' -or
    $manifest.Package.Applications.Application.Executable -cne 'NSMBUFullRuntimeR11.exe') {
  throw 'R12 package identity or executable changed unexpectedly'
}
Copy-Item -LiteralPath $manifestSrc -Destination (Join-Path $stage 'AppxManifest.xml')
& py -3 (Join-Path $root 'xbox/smoke/make_assets.py')
if ($LASTEXITCODE -ne 0) { throw 'Cannot generate copyright-free tiles' }
Copy-Item -LiteralPath (Join-Path $root 'xbox/smoke/Assets') -Destination (Join-Path $stage 'Assets') -Recurse
$allowed = @('NSMBUFullRuntimeR11.exe','vulkan_dzn.dll','dxil.dll',
  'AppxManifest.xml','Assets\StoreLogo.png','Assets\Square150x150Logo.png',
  'Assets\Square44x44Logo.png','Assets\SplashScreen.png')
foreach ($item in (Get-ChildItem -LiteralPath $stage -Recurse -File)) {
  $relative = $item.FullName.Substring($stage.Length + 1)
  if ($allowed -cnotcontains $relative) { throw "Unexpected/unsafe package file: $relative" }
}
foreach ($name in @('StoreLogo.png','Square150x150Logo.png','Square44x44Logo.png','SplashScreen.png')) {
  if (-not (Test-Path (Join-Path $stage "Assets/$name"))) { throw "Missing R12 tile: $name" }
}
Copy-Item -LiteralPath $vclibs -Destination (Join-Path $out 'Microsoft.VCLibs.x64.14.00.appx')
$certParams = @{
  Type = 'CodeSigningCert'; Subject = 'CN=NSMBUXboxR12'
  CertStoreLocation = 'Cert:\CurrentUser\My'; KeyAlgorithm = 'RSA'
  KeyLength = 2048; KeyExportPolicy = 'Exportable'; HashAlgorithm = 'SHA256'
  NotAfter = (Get-Date).AddYears(1)
}
$cert = New-SelfSignedCertificate @certParams
$password = [Convert]::ToBase64String([Security.Cryptography.RandomNumberGenerator]::GetBytes(24))
$secure = ConvertTo-SecureString $password -AsPlainText -Force
$pfx = Join-Path $env:RUNNER_TEMP 'r12-ephemeral-signing.pfx'
Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $secure | Out-Null
Export-Certificate -Cert $cert -FilePath (Join-Path $out 'NSMBUFullRuntimeR12.cer') | Out-Null
$appx = Join-Path $out 'NSMBUFullRuntime_R12_0.1.0.0_x64.appx'
& $makeappx pack /d $stage /p $appx /o
if ($LASTEXITCODE -ne 0) { throw 'R12 AppX packaging failed' }
& $signtool sign /fd SHA256 /f $pfx /p $password $appx
if ($LASTEXITCODE -ne 0) { throw 'R12 AppX signing failed' }
# Trust only this ephemeral test certificate on the disposable CI worker,
# so WinVerifyTrust can validate the complete signed AppX chain.
# Users will separately install the exported .cer on their own console.
Import-Certificate -FilePath (Join-Path $out 'NSMBUFullRuntimeR12.cer') -CertStoreLocation 'Cert:\CurrentUser\Root' | Out-Null
& $signtool verify /pa $appx
if ($LASTEXITCODE -ne 0) { throw 'R12 AppX Authenticode verification failed' }
$verify = Join-Path $out 'verified'
& $makeappx unpack /p $appx /d $verify /o
if ($LASTEXITCODE -ne 0) { throw 'R12 signed AppX unpack failed' }
foreach ($name in @('NSMBUFullRuntimeR11.exe','vulkan_dzn.dll','dxil.dll',
                    'AppxManifest.xml','Assets/Square150x150Logo.png')) {
  if (-not (Test-Path (Join-Path $verify $name))) { throw "R12 unpack missing $name" }
}
if ((Get-FileHash $exe -Algorithm SHA256).Hash -cne
    (Get-FileHash (Join-Path $verify 'NSMBUFullRuntimeR11.exe') -Algorithm SHA256).Hash) {
  throw 'Packaged executable differs from successful private link'
}
$report = @(
  'R12 Xbox UWP signed install package PASS (CI only, no hardware boot proven)'
  "R11 EXE SHA256=$((Get-FileHash $exe -Algorithm SHA256).Hash)"
  "Dozen SHA256=$((Get-FileHash (Join-Path $prior 'vulkan_dzn.dll') -Algorithm SHA256).Hash)"
  "DXIL SHA256=$((Get-FileHash (Join-Path $prior 'dxil.dll') -Algorithm SHA256).Hash)"
  "AppX SHA256=$((Get-FileHash $appx -Algorithm SHA256).Hash)"
  "AppX bytes=$((Get-Item $appx).Length)"
  'Dozen/DXIL origin: Series S-tested R9 open-source artifact run 43433286834'
  'Compiled owner game code is inside private EXE; no original game dump included.'
  'Full code/content/meta must be placed in app LocalState/game to play.'
  'App activation, memory, actual game frame NOT yet tested.'
)
$report | Set-Content -LiteralPath (Join-Path $out 'R12-build-report.txt') -Encoding utf8
Remove-Item -LiteralPath $prior,$stage,$verify -Force -Recurse
Write-Host 'R12 SIGNED PRIVATE GAME APPX PASS. Device startup remains unverified.'
Write-Host "R12 AppX SHA256: $((Get-FileHash $appx -Algorithm SHA256).Hash)"
