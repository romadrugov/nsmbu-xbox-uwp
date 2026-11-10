# R2.1 public integration test. Compiles actual runtime core/HLE/write-watch,
# generates the synthetic three-instruction guest with the real recompiler,
# and executes a Windows host test. It never builds or deploys an Xbox AppX.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference = $false

$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path $env:RUNNER_TEMP 'nsmbu-r2-production-dispatch'
New-Item -ItemType Directory -Path $out -Force | Out-Null

$clangOptions = @(
    (Get-Command clang-cl.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1),
    'C:\Program Files\LLVM\bin\clang-cl.exe'
)
$clang = $clangOptions | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $clang) { throw 'clang-cl missing' }
$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -property installationPath | Select-Object -First 1
if (-not $vs) { throw 'Visual Studio toolchain missing' }
$msvc = Get-ChildItem (Join-Path $vs 'VC/Tools/MSVC') -Directory |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $msvc) { throw 'MSVC toolset missing' }
$dumpbin = Join-Path $msvc.FullName 'bin/Hostx64/x64/dumpbin.exe'

$sdkRoot = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Windows Kits/10'
$sdk = Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory |
    Where-Object { $_.Name -match '^10\.0\.\d+\.0$' } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $sdk) { throw 'Windows SDK missing' }
$onecore = Join-Path $sdkRoot "Lib/$($sdk.Name)/um/x64/OneCore.lib"
if (-not (Test-Path $onecore)) { throw "UWP OneCore.lib missing: $onecore" }

$vcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
if (-not $vcpkgRoot -or -not (Test-Path (Join-Path $vcpkgRoot 'vcpkg.exe'))) {
    $vcpkgRoot = 'C:\vcpkg'
}
$vcpkg = Join-Path $vcpkgRoot 'vcpkg.exe'
if (-not (Test-Path $vcpkg)) { throw 'vcpkg missing' }
& $vcpkg install zlib:x64-uwp --disable-metrics
if ($LASTEXITCODE -ne 0) { throw 'Could not prepare zlib:x64-uwp' }
$zlibInclude = Join-Path $vcpkgRoot 'installed/x64-uwp/include'
$zlibLibDir = Join-Path $vcpkgRoot 'installed/x64-uwp/lib'
$zlibCandidates = @(Get-ChildItem $zlibLibDir -File -Filter '*zlib*.lib' -ErrorAction Stop)
Write-Host "UWP zlib library candidates: $($zlibCandidates.Name -join ', ')"
if (-not (Test-Path (Join-Path $zlibInclude 'zlib.h')) -or $zlibCandidates.Count -eq 0) {
    throw 'UWP zlib headers/library missing'
}
$zlibLib = ($zlibCandidates | Select-Object -First 1).FullName
if (-not (Test-Path $zlibLib)) { throw 'No UWP zlib static archive chosen' }

# Original upstream modules; no test-only HLE registry or dispatcher shims.
$cxxOptions = @('/nologo','/c','/O1','/MD','/EHsc','/clang:-std=c++20',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_APP','/DNSMBU_XBOX_UWP=1',
    '/D_CRT_SECURE_NO_WARNINGS',
    "/I$(Join-Path $root 'runtime/src')",
    "/I$(Join-Path $root 'runtime/include')",
    "/I$zlibInclude")
$cxxSources = @(
    @('runtime/src/core/core.cpp', 'core.obj'),
    @('runtime/src/hle/coreinit_mem.cpp', 'coreinit_mem.obj'),
    @('runtime/src/core/write_watch.cpp', 'write_watch.obj'),
    @('xbox/runtime-compat/r2_real_dispatch_probe.cpp', 'probe.obj'),
    @('xbox/runtime-compat/r2_public_guest_hooks.cpp', 'guest_hooks.obj')
)
$objects = @()
foreach ($entry in $cxxSources) {
    $source = Join-Path $root $entry[0]
    $object = Join-Path $out $entry[1]
    & $clang @cxxOptions "/Fo$object" $source
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $object)) {
        throw "clang-cl compile failed: $($entry[0])"
    }
    Write-Host "R2 COMPILE PASS: $($entry[0])"
    $objects += $object
}

# Reuse the actual recompilation generator and its table.c. No private RPX.
$fixture = Join-Path $out 'authored-ppc-fixture'
& py -3 (Join-Path $root 'recomp/android_fixture.py') --output $fixture
if ($LASTEXITCODE -ne 0) { throw 'Public PPC recompilation failed' }
$generated = Join-Path $fixture 'gen'
if (-not (Test-Path (Join-Path $generated 'table.c'))) { throw 'Public generated table.c missing' }
$generatedSources = @(Get-ChildItem $generated -File -Filter '*.c' |
    Where-Object { $_.Name -notin @('harness.c','imports.c') })
if ($generatedSources.Count -lt 2) { throw 'Missing actual generated PPC function body or table.c' }
$cOptions = @('/nologo','/c','/O2','/MD','/clang:-std=c11',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_APP',
    "/I$generated",
    "/I$(Join-Path $root 'runtime/include')",
    "/I$(Join-Path $root 'runtime/src')")
foreach ($source in $generatedSources) {
    $object = Join-Path $out ($source.BaseName + '.obj')
    & $clang @cOptions "/Fo$object" $source.FullName
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $object)) {
        throw "Public recompilation output failed to compile: $($source.Name)"
    }
    Write-Host "R2 RECOMP COMPILE PASS: $($source.Name)"
    $objects += $object
}

# Real core.cpp links more than HLE alone: OneCore supplies APP-family
# allocation/protection; zlib:x64-uwp handles the RPX loader dependency;
# real write_watch.cpp resolves wwatch::init/hint; generated table.c
# supplies g_recomp_* with no fabricated symbols.
$core = Join-Path $out 'core.obj'
$symbols = & $dumpbin /symbols $core | Out-String
if ($LASTEXITCODE -ne 0 -or $symbols -notmatch 'hle_find' -or $symbols -notmatch 'register_host') {
    throw 'Production core.obj HLE/dispatch symbols missing'
}
$exe = Join-Path $out 'r2_production_dispatch.exe'
& $clang /nologo @objects $onecore $zlibLib "/Fe:$exe" '/link' '/SUBSYSTEM:CONSOLE'
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $exe)) {
    throw 'R2 production core/HLE/recompiled guest link FAILED; inspect preceding unresolved symbols'
}
Write-Host "R2 LINK PASS: original core.cpp, coreinit_mem.cpp, write_watch.cpp, public generated guest; $exe"
& $dumpbin /dependents $exe
if ($LASTEXITCODE -ne 0) { throw 'PE dependency inspection failed' }
& $exe
if ($LASTEXITCODE -ne 0) { throw "R2 behavior test FAILED with exit code $LASTEXITCODE" }
Write-Host 'R2 EXECUTION PASS: genuine registry + generated guest (42) + HLE heap lifecycle.'
"## R2.1 production dispatch host test\n\n**PASS:** unchanged real runtime core/HLE/write-watch linked to public recomp-generated guest, Windows host test executed (guest = 42, HLE + heap roundtrip). No actual Xbox UWP activation or private game code was tested." >> $env:GITHUB_STEP_SUMMARY
 } | Select-Object -First 1)
if (-not $zlibLib) { $zlibLib = $zlibCandidates | Select-Object -First 1 }
$zlibLib = $zlibLib.FullName

# Original upstream modules; no test-only HLE registry or dispatcher shims.
$cxxOptions = @('/nologo','/c','/O1','/MD','/EHsc','/clang:-std=c++20',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_APP','/DNSMBU_XBOX_UWP=1',
    '/D_CRT_SECURE_NO_WARNINGS',
    "/I$(Join-Path $root 'runtime/src')",
    "/I$(Join-Path $root 'runtime/include')",
    "/I$zlibInclude")
$cxxSources = @(
    @('runtime/src/core/core.cpp', 'core.obj'),
    @('runtime/src/hle/coreinit_mem.cpp', 'coreinit_mem.obj'),
    @('runtime/src/core/write_watch.cpp', 'write_watch.obj'),
    @('xbox/runtime-compat/r2_real_dispatch_probe.cpp', 'probe.obj'),
    @('xbox/runtime-compat/r2_public_guest_hooks.cpp', 'guest_hooks.obj')
)
$objects = @()
foreach ($entry in $cxxSources) {
    $source = Join-Path $root $entry[0]
    $object = Join-Path $out $entry[1]
    & $clang @cxxOptions "/Fo$object" $source
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $object)) {
        throw "clang-cl compile failed: $($entry[0])"
    }
    Write-Host "R2 COMPILE PASS: $($entry[0])"
    $objects += $object
}

# Reuse the actual recompilation generator and its table.c. No private RPX.
$fixture = Join-Path $out 'authored-ppc-fixture'
& py -3 (Join-Path $root 'recomp/android_fixture.py') --output $fixture
if ($LASTEXITCODE -ne 0) { throw 'Public PPC recompilation failed' }
$generated = Join-Path $fixture 'gen'
if (-not (Test-Path (Join-Path $generated 'table.c'))) { throw 'Public generated table.c missing' }
$generatedSources = @(Get-ChildItem $generated -File -Filter '*.c' |
    Where-Object { $_.Name -notin @('harness.c','imports.c') })
if ($generatedSources.Count -lt 2) { throw 'Missing actual generated PPC function body or table.c' }
$cOptions = @('/nologo','/c','/O2','/MD','/clang:-std=c11',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_APP',
    "/I$generated",
    "/I$(Join-Path $root 'runtime/include')",
    "/I$(Join-Path $root 'runtime/src')")
foreach ($source in $generatedSources) {
    $object = Join-Path $out ($source.BaseName + '.obj')
    & $clang @cOptions "/Fo$object" $source.FullName
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $object)) {
        throw "Public recompilation output failed to compile: $($source.Name)"
    }
    Write-Host "R2 RECOMP COMPILE PASS: $($source.Name)"
    $objects += $object
}

# Real core.cpp links more than HLE alone: OneCore supplies APP-family
# allocation/protection; zlib:x64-uwp handles the RPX loader dependency;
# real write_watch.cpp resolves wwatch::init/hint; generated table.c
# supplies g_recomp_* with no fabricated symbols.
$core = Join-Path $out 'core.obj'
$symbols = & $dumpbin /symbols $core | Out-String
if ($LASTEXITCODE -ne 0 -or $symbols -notmatch 'hle_find' -or $symbols -notmatch 'register_host') {
    throw 'Production core.obj HLE/dispatch symbols missing'
}
$exe = Join-Path $out 'r2_production_dispatch.exe'
& $clang /nologo @objects $onecore $zlibLib "/Fe:$exe" '/link' '/SUBSYSTEM:CONSOLE'
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $exe)) {
    throw 'R2 production core/HLE/recompiled guest link FAILED; inspect preceding unresolved symbols'
}
Write-Host "R2 LINK PASS: original core.cpp, coreinit_mem.cpp, write_watch.cpp, public generated guest; $exe"
& $dumpbin /dependents $exe
if ($LASTEXITCODE -ne 0) { throw 'PE dependency inspection failed' }
& $exe
if ($LASTEXITCODE -ne 0) { throw "R2 behavior test FAILED with exit code $LASTEXITCODE" }
Write-Host 'R2 EXECUTION PASS: genuine registry + generated guest (42) + HLE heap lifecycle.'
"## R2.1 production dispatch host test\n\n**PASS:** unchanged real runtime core/HLE/write-watch linked to public recomp-generated guest, Windows host test executed (guest = 42, HLE + heap roundtrip). No actual Xbox UWP activation or private game code was tested." >> $env:GITHUB_STEP_SUMMARY
