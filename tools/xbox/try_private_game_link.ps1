# First private real-game UWP LINK ATTEMPT, not a playable game.
# Requires Windows x64 VS2022 C++/UWP SDK, LLVM clang-cl and a user-local
# 95-object recompilation archive. Never upload Nintendo-derived binaries.
#
# .\tools\xbox\try_private_game_link.ps1 -PrivateArchive D:\private\nsmbu_usa_v0_win.lib -OutputDir D:\private\first-link
# Public CI may use -PublicFixture; it NEVER uses Nintendo game files.
[CmdletBinding(DefaultParameterSetName='Private')]
param(
  [Parameter(Mandatory=$true,ParameterSetName='Private')][string]$PrivateArchive,
  [Parameter(Mandatory=$true,ParameterSetName='Public')][switch]$PublicFixture,
  [Parameter(Mandatory=$true)][string]$OutputDir
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.TrimEnd('\','/')
$dest = [IO.Path]::GetFullPath($OutputDir).TrimEnd('\','/')
function InCheckout([string]$path) {
  $p = [IO.Path]::GetFullPath($path).TrimEnd('\','/')
  return $p.Equals($root,[StringComparison]::OrdinalIgnoreCase) -or $p.StartsWith($root + '\',[StringComparison]::OrdinalIgnoreCase)
}
if (InCheckout $dest) { throw 'OutputDir must be OUTSIDE public GitHub checkout' }
if (-not $PublicFixture -and $env:GITHUB_ACTIONS -eq 'true') {
  throw 'Private game code on public GitHub Actions is forbidden. Use private Windows machine.'
}
if (Test-Path $dest) {
  if (Get-ChildItem $dest -Force | Select-Object -First 1) { throw 'OutputDir must be empty/new to prevent stale private builds' }
} else { New-Item -ItemType Directory $dest -Force | Out-Null }
$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Requires VS2022 Windows C++ tools' }
$vs = (& $vswhere -latest -products '*' -property installationPath | Select-Object -First 1)
if (-not $vs) { throw 'Visual Studio not found' }
$msbuild = Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
$msvc = Get-ChildItem (Join-Path $vs 'VC/Tools/MSVC') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $msvc) { throw 'MSVC toolset missing' }
$clang = @((Get-Command clang-cl.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1),'C:\Program Files\LLVM\bin\clang-cl.exe') | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $clang) { throw 'clang-cl required for recompiled PPC ABI' }
$sdkRoot = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Windows Kits/10'
$versions = @(Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory | Where-Object { $_.Name -match '^10\.0\.\d+\.0$' } | Sort-Object { [version]$_.Name } -Descending)
if (-not $versions) { throw 'Windows SDK missing' }
$sdk = $versions[0].Name
$mode = 'USA_V0_PRIVATE_COFF_ARCHIVE'
$entry = '0x02AA2E50u'
$funcs = '84231u'
$imports = '2821u'
if ($PublicFixture) {
  if (-not $env:RUNNER_TEMP) { throw 'PublicFixture requires CI RUNNER_TEMP' }
  & (Join-Path $root 'xbox/smoke/build-ppc-fixture.ps1')
  if ($LASTEXITCODE -ne 0) { throw 'Public synthetic fixture compile failed' }
  $archive = Join-Path $root 'xbox/smoke/generated/guest_fixture.lib'
  $mode = 'PUBLIC_SYNTHETIC_PPC'
  $entry = '0x02000000u'
  $funcs = '1u'
  $imports = '0u'
} else {
  $archive = (Resolve-Path $PrivateArchive).Path
  if (InCheckout $archive) { throw 'Private game archive must be outside public Git checkout' }
  if ((Get-Item $archive).Length -lt 1000) { throw 'Archive unexpectedly small' }
}
if (-not (Test-Path $archive)) { throw "Missing archive: $archive" }

$bridge = Join-Path $dest 'private_game_table_probe.obj'
$flags = @('/nologo','/c','/O1','/MD','/clang:-std=c11','/DWINAPI_FAMILY=WINAPI_FAMILY_APP',
  "/DNSMBU_EXPECT_ENTRY=$entry","/DNSMBU_EXPECT_FUNCS=$funcs","/DNSMBU_EXPECT_IMPORTS=$imports",
  "/I$(Join-Path $root 'runtime/include')","/Fo$bridge",
  (Join-Path $root 'xbox/runtime-compat/private_game_table_probe.c'))
& $clang @flags *> (Join-Path $dest 'bridge.log')
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $bridge)) { throw 'Bridge compiler failed; inspect only LOCAL bridge.log' }

$compatOut = Join-Path $dest 'compat'
$compatInt = Join-Path $dest 'compat-int'
$appOut = Join-Path $dest 'app'
$appInt = Join-Path $dest 'app-int'
foreach ($p in @($compatOut,$compatInt,$appOut,$appInt)) { New-Item -ItemType Directory $p -Force | Out-Null }
$common = @('/t:Build','/m','/v:minimal','/p:Configuration=Release','/p:Platform=x64',
 "/p:WindowsTargetPlatformVersion=$sdk",'/p:GenerateAppxPackageOnBuild=false','/p:AppxPackageSigningEnabled=false')
$compat = Join-Path $root 'xbox/runtime-compat/NSMBURuntimeCompat.vcxproj'
& $msbuild $compat @common "/p:OutDir=$compatOut/" "/p:IntDir=$compatInt/" *> (Join-Path $dest 'compat.log')
if ($LASTEXITCODE -ne 0) { throw 'UWP runtime compatibility compile failed; read only LOCAL compat.log' }
$compatLib = Join-Path $compatOut 'NSMBURuntimeCompat.lib'
if (-not (Test-Path $compatLib)) {
  $matches = @(Get-ChildItem $dest,(Join-Path $root 'xbox/runtime-compat') -Recurse -File -Filter 'NSMBURuntimeCompat.lib' -ErrorAction SilentlyContinue)
  if ($matches.Count -gt 0) { $compatLib = $matches[0].FullName }
}
if (-not (Test-Path $compatLib)) {
  Write-Host 'UWP compatibility MSBuild log (last 35 lines):'
  Get-Content (Join-Path $dest 'compat.log') -Tail 35 | ForEach-Object { Write-Host $_ }
  throw 'Runtime compat library not found despite successful MSBuild exit'
}
Write-Host "UWP compatibility library: $compatLib"
& py -3 (Join-Path $root 'xbox/smoke/make_assets.py')
if ($LASTEXITCODE -ne 0) { throw 'Public logo generation failed' }
$project = Join-Path $root 'xbox/runtime-compat/NSMBURuntimeLinkProbe.vcxproj'
$linkLog = Join-Path $dest 'private-link.log'
$extra = @("/p:OutDir=$appOut/","/p:IntDir=$appInt/",
 "/p:NSMBUCompatArchive=$compatLib",
 "/p:NSMBUPrivateArchive=$archive",
 "/p:NSMBUPrivateBridgeObj=$bridge")
& $msbuild $project @common @extra *> $linkLog
$linkExit = $LASTEXITCODE
# MSBuild may place the EXE in OutDir/ProjectName rather than directly in OutDir.
# Only accept a unique output inside the fresh private output directory.
$executables = @(Get-ChildItem -LiteralPath $appOut -Filter 'NSMBURuntimeLinkProbe.exe' -Recurse -File -ErrorAction SilentlyContinue)
$exe = if ($executables.Count -eq 1) { $executables[0].FullName } else { $null }
$ok = ($linkExit -eq 0) -and ($null -ne $exe)
$report = [ordered]@{
  schema = 'nsmbu-private-first-link-v1'
  input = $mode
  linked_uwp_x64_executable = $ok
  game_code_executed = $false
  rendered_real_game_frame = $false
  playable = $false
  private_output_only = $true
}
$report | ConvertTo-Json | Set-Content (Join-Path $dest 'link-summary.json') -Encoding UTF8
if (-not $ok) {
  $errors = @(Get-Content $linkLog | Where-Object { $_ -match 'error LNK\d+|fatal error LNK\d+|error C\d+|error MSB\d+' })
  Write-Host "UWP archive link FAILED: MSBuild exit=$linkExit, found EXEs=$($executables.Count), diagnostics=$($errors.Count)."
  $errors | Select-Object -First 8 | ForEach-Object { Write-Host $_ }
  if ($PublicFixture) {
    # Public code fixture only: its MSBuild output contains no game-derived symbols.
    # Give CI a useful error when MSBuild returned an error without an LNK diagnostic.
    Write-Host 'PUBLIC fixture link log (last 35 lines):'
    Get-Content $linkLog -Tail 35 | ForEach-Object { Write-Host $_ }
  }
  Write-Host "Full log LOCAL ONLY: $linkLog"
  Write-Host 'Share only metadata from link-summary.json; no private source, .lib, .obj or full logs.'
  exit 1
}
Write-Host "UWP ARCHIVE LINK PASS ($mode): $exe"
Write-Host 'Not a playable game: no game execution, GPU renderer or AppX deployment.'
Write-Host "Safe summary: $(Join-Path $dest 'link-summary.json')"
