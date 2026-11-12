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
  [Parameter(Mandatory=$true)][string]$OutputDir,
  [switch]$BuildBootShell
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
  # Never accept private game inputs in public CI. The explicit owner-private
  # workflow must check repository visibility before setting this opt-in.
  if ($env:GITHUB_REPOSITORY -ne 'romadrugov/mkwii-xbox-build' -or
      $env:NSMBU_PRIVATE_CI_OPT_IN -ne 'YES') {
    throw 'Private game code is forbidden outside explicitly opted-in private CI.'
  }
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

# Use the unchanged upstream Espresso floating-point implementation rather
# than replacing two game-required PowerPC operations with placeholder stubs.
# Both public fixture and private paths compile this same source with clang-cl.
$mathObj = Join-Path $dest 'espresso_fp.obj'
$mathArgs = @('/nologo','/c','/O1','/MD','/clang:-std=c11',
  '/DWINAPI_FAMILY=WINAPI_FAMILY_APP',
  "/I$(Join-Path $root 'runtime/include')","/Fo$mathObj",
  (Join-Path $root 'runtime/src/core/espresso_fp.c'))
& $clang @mathArgs *> (Join-Path $dest 'espresso-fp.log')
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $mathObj)) { throw 'Espresso FP compile failed; inspect only LOCAL espresso-fp.log' }

# Native first-boot subset: the already tested table dispatcher plus an
# explicit fail-fast diagnostic runtime. The same author-created code links
# both PUBLIC fixture and PRIVATE full game, without executing game code.
$headlessObj = Join-Path $dest 'headless_dispatch.obj'
$bootObj = Join-Path $dest 'bootfirst_runtime.obj'
foreach ($unit in @(
    @{ Source = 'runtime/src/core/headless_dispatch.c'; Out = $headlessObj; Log = 'headless-dispatch.log' },
    @{ Source = 'xbox/runtime-compat/bootfirst_runtime.c'; Out = $bootObj; Log = 'bootfirst-runtime.log' }
)) {
  $args = @('/nologo','/c','/O1','/MD','/clang:-std=c11',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_APP',
    "/I$(Join-Path $root 'runtime/include')",
    "/I$(Join-Path $root 'runtime/src')",
    "/Fo$($unit.Out)",
    (Join-Path $root $unit.Source))
  & $clang @args *> (Join-Path $dest $unit.Log)
  if ($LASTEXITCODE -ne 0 -or -not (Test-Path $unit.Out)) {
    throw "Boot-first source compile failed: $($unit.Source); private log: $($unit.Log)"
  }
}

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
 "/p:NSMBUPrivateBridgeObj=$bridge",
 "/p:NSMBUMathObject=$mathObj",
 "/p:NSMBUHeadlessDispatchObj=$headlessObj",
 "/p:NSMBUBootFirstRuntimeObj=$bootObj")
& $msbuild $project @common @extra *> $linkLog
$linkExit = $LASTEXITCODE
# MSBuild may place the EXE in OutDir/ProjectName rather than directly in OutDir.
# Only accept a unique output inside the fresh private output directory.
$executables = @(Get-ChildItem -LiteralPath $appOut -Filter 'NSMBURuntimeLinkProbe.exe' -Recurse -File -ErrorAction SilentlyContinue)
$exe = if ($executables.Count -eq 1) { $executables[0].FullName } else { $null }
$ok = ($linkExit -eq 0) -and ($null -ne $exe)
# Optional R4: compile a REAL activatable C++/CX WinRT app that validates
# the 4-GiB guest VM and native PPC entrypoint dispatch table on Xbox.
# This step does NOT run the game's entrypoint or embed RPX sections.
$bootOk = (-not $BuildBootShell)
if ($BuildBootShell -and $ok) {
  $bootOut = Join-Path $dest 'boot-app'
  $bootInt = Join-Path $dest 'boot-int'
  foreach ($p in @($bootOut, $bootInt)) {
    New-Item -ItemType Directory -Path $p -Force | Out-Null
  }
  $bootProject = Join-Path $root 'xbox/runtime-compat/NSMBUBootPreflight.vcxproj'
  $bootArgs = @(
    "/p:OutDir=$bootOut/",
    "/p:IntDir=$bootInt/",
    "/p:NSMBUR4Archive=$archive",
    "/p:NSMBUR4BridgeObj=$bridge",
    "/p:NSMBUR4MathObj=$mathObj",
    "/p:NSMBUR4DispatchObj=$headlessObj",
    "/p:NSMBUR4RuntimeObj=$bootObj"
  )
  & $msbuild $bootProject @common @bootArgs *> (Join-Path $dest 'boot-link.log')
  $bootExit = $LASTEXITCODE
  $bootExecutables = @(Get-ChildItem $bootOut -Recurse -File -Filter 'NSMBUBootPreflight.exe' -ErrorAction SilentlyContinue)
  $bootOk = ($bootExit -eq 0 -and $bootExecutables.Count -eq 1)
  if (-not $bootOk) {
    Write-Host "R4 WinRT boot-shell link failed: msbuild exit=$bootExit, EXEs=$($bootExecutables.Count)"
    if ($PublicFixture) {
      Write-Host 'PUBLIC fixture R4 MSBuild log (last 40 lines):'
      Get-Content (Join-Path $dest 'boot-link.log') -Tail 40 | ForEach-Object { Write-Host $_ }
    }
  } else {
    Write-Host "R4 WinRT boot-shell linked: $($bootExecutables[0].Length) bytes"
  }
}
$report = [ordered]@{
  schema = 'nsmbu-private-first-link-v1'
  input = $mode
  linked_uwp_x64_executable = $ok
  r4_boot_shell_requested = [bool]$BuildBootShell
  r4_boot_shell_linked = [bool]$bootOk
  game_code_executed = $false
  rendered_real_game_frame = $false
  playable = $false
  private_output_only = $true
}
$report | ConvertTo-Json | Set-Content (Join-Path $dest 'link-summary.json') -Encoding UTF8
if (-not $ok -or -not $bootOk) {
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
if ($BuildBootShell) { Write-Host 'R4 BOOT SHELL LINK PASS (WinRT App, not yet a signed AppX)' }
Write-Host 'Not a playable game: no game execution, GPU renderer or AppX deployment.'
Write-Host "Safe summary: $(Join-Path $dest 'link-summary.json')"
