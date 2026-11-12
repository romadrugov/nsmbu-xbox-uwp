# R4 signed Windows Store / Xbox UWP preflight package (NOT gameplay).
# Same safe dev certificate method as previously tested xbox/smoke/build-ci.ps1.
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$BuildRoot,
  [Parameter(Mandatory=$true)][string]$OutputDir
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.TrimEnd('\','/')
$build = (Resolve-Path $BuildRoot).Path.TrimEnd('\','/')
$out = [IO.Path]::GetFullPath($OutputDir).TrimEnd('\','/')
if ($out.Equals($root,[StringComparison]::OrdinalIgnoreCase) -or
    $out.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)) {
  throw 'R4 output may not be inside the public checkout'
}
if (-not (Test-Path $out)) { New-Item -ItemType Directory $out -Force | Out-Null }
if (@(Get-ChildItem $out -Force -ErrorAction SilentlyContinue).Count -ne 0) {
  throw 'R4 package output must be empty'
}
$executables = @(Get-ChildItem $build -Filter 'NSMBUBootPreflight.exe' -File -Recurse)
if ($executables.Count -ne 1) { throw 'Expected exactly one R4 executable' }
$exe = $executables[0]
$winmd = Join-Path $exe.DirectoryName 'NSMBUBootPreflight.winmd'
$pri = Join-Path $exe.DirectoryName 'resources.pri'
if (-not (Test-Path $winmd) -or -not (Test-Path $pri)) {
  throw 'R4 WinRT metadata or PRI missing'
}
$peBytes = [IO.File]::ReadAllBytes($exe.FullName)
if ($peBytes.Length -lt 256) { throw 'Truncated PE' }
$peOffset = [BitConverter]::ToInt32($peBytes,0x3c)
if ($peOffset -lt 0 -or $peOffset + 96 -ge $peBytes.Length -or
    $peBytes[$peOffset] -ne 80 -or $peBytes[$peOffset+1] -ne 69) {
  throw 'Invalid R4 PE image'
}
$machine = [BitConverter]::ToUInt16($peBytes,$peOffset+4)
$subsystem = [BitConverter]::ToUInt16($peBytes,$peOffset+24+68)
if ($machine -ne 0x8664 -or $subsystem -ne 2) {
  throw 'R4 boot probe must be x64 Windows GUI executable'
}
$sdkRoot = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Windows Kits/10'
$versions = @(Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory |
  Where-Object { $_.Name -match '^10\.0\.\d+\.0$' } |
  Sort-Object { [version]$_.Name } -Descending)
if ($versions.Count -eq 0) { throw 'Windows SDK missing' }
$sdk = $versions[0].Name
$tools = Join-Path $sdkRoot "bin/$sdk/x64"
$makeappx = Join-Path $tools 'makeappx.exe'
$signtool = Join-Path $tools 'signtool.exe'
if (-not (Test-Path $makeappx) -or -not (Test-Path $signtool)) {
  throw 'AppX packing tools unavailable'
}
$stage = Join-Path $out 'stage'
New-Item -ItemType Directory $stage -Force | Out-Null
Copy-Item $exe.FullName (Join-Path $stage 'NSMBUBootPreflight.exe')
Copy-Item $winmd (Join-Path $stage 'NSMBUBootPreflight.winmd')
Copy-Item $pri (Join-Path $stage 'resources.pri')
Copy-Item (Join-Path $root 'xbox/runtime-compat/PackageBootPreflight.appxmanifest') (Join-Path $stage 'AppxManifest.xml')
Copy-Item (Join-Path $root 'xbox/smoke/Assets') (Join-Path $stage 'Assets') -Recurse
$vclibsRel = 'Microsoft SDKs/Windows Kits/10/ExtensionSDKs/Microsoft.VCLibs/14.0/Appx/Retail/x64/Microsoft.VCLibs.x64.14.00.appx'
$vclibs = @(
  (Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) $vclibsRel),
  (Join-Path ([Environment]::GetFolderPath('ProgramFiles')) $vclibsRel)
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vclibs) { throw 'Microsoft.VCLibs UWP framework dependency missing' }
Copy-Item $vclibs (Join-Path $out 'Microsoft.VCLibs.x64.14.00.appx')
$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=NSMBUXbox' -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 2048 -KeyExportPolicy Exportable -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(1)
$password = [Convert]::ToBase64String([Security.Cryptography.RandomNumberGenerator]::GetBytes(24))
$secure = ConvertTo-SecureString $password -AsPlainText -Force
$pfx = Join-Path $env:RUNNER_TEMP 'nsmbu-r4-signing.pfx'
$cer = Join-Path $out 'NSMBUBootPreflight.cer'
Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $secure | Out-Null
Export-Certificate -Cert $cert -FilePath $cer | Out-Null
$appx = Join-Path $out 'NSMBUBootPreflight_R4_x64.appx'
& $makeappx pack /d $stage /p $appx /o
if ($LASTEXITCODE -ne 0) { throw 'R4 AppX packing failed' }
& $signtool sign /fd SHA256 /f $pfx /p $password $appx
if ($LASTEXITCODE -ne 0) { throw 'R4 AppX signing failed' }
$unpacked = Join-Path $out 'verify'
& $makeappx unpack /p $appx /d $unpacked /o
if ($LASTEXITCODE -ne 0) { throw 'R4 AppX verification unpack failed' }
foreach ($rel in @('NSMBUBootPreflight.exe','NSMBUBootPreflight.winmd',
                   'resources.pri','AppxManifest.xml','Assets/Square150x150Logo.png')) {
  if (-not (Test-Path (Join-Path $unpacked $rel))) {
    throw "R4 AppX missing required file: $rel"
  }
}
Remove-Item $stage -Recurse -Force
Remove-Item $unpacked -Recurse -Force
Write-Host 'R4 SIGNED BOOT PREFLIGHT APPX PASS (not gameplay)'
Write-Host "AppX bytes: $((Get-Item $appx).Length)"
Write-Host "AppX SHA256: $((Get-FileHash $appx -Algorithm SHA256).Hash)"
