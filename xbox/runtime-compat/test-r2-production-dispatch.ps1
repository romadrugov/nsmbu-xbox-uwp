# Public R2.1: genuine runtime core/HLE/write-watch plus authored PPC fixture.
# Windows host execution only; no Nintendo material, no Xbox AppX changes.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference = $false

$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path $env:RUNNER_TEMP 'r2-real-production-dispatch'
New-Item -ItemType Directory -Path $out -Force | Out-Null

$clangOptions = @(
    (Get-Command clang-cl.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1),
    'C:\Program Files\LLVM\bin\clang-cl.exe'
)
$clang = $clangOptions | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $clang) { throw 'clang-cl not available' }
$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -property installationPath | Select-Object -First 1
if (-not $vs) { throw 'VS2022 not found' }
$msvc = Get-ChildItem (Join-Path $vs 'VC/Tools/MSVC') -Directory |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $msvc) { throw 'MSVC v143 missing' }
$dumpbin = Join-Path $msvc.FullName 'bin/Hostx64/x64/dumpbin.exe'

$sdkRoot = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Windows Kits/10'
$sdk = Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory |
    Where-Object { $_.Name -match '^10\.0\.\d+\.0$' } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $sdk) { throw 'Windows SDK missing' }
$onecore = Join-Path $sdkRoot "Lib/$($sdk.Name)/um/x64/OneCore.lib"
if (-not (Test-Path $onecore)) { throw "OneCore.lib missing: $onecore" }

$vcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
if (-not $vcpkgRoot -or -not (Test-Path (Join-Path $vcpkgRoot 'vcpkg.exe'))) {
    $vcpkgRoot = 'C:\vcpkg'
}
$vcpkg = Join-Path $vcpkgRoot 'vcpkg.exe'
if (-not (Test-Path $vcpkg)) { throw 'vcpkg not installed' }
& $vcpkg install zlib:x64-uwp --disable-metrics
if ($LASTEXITCODE -ne 0) { throw 'zlib:x64-uwp install failed' }
$zlibInclude = Join-Path $vcpkgRoot 'installed/x64-uwp/include'
$zlibDir = Join-Path $vcpkgRoot 'installed/x64-uwp/lib'
$zlibLibs = @(Get-ChildItem $zlibDir -Filter '*zlib*.lib' -File)
Write-Host "zlib library candidates: $($zlibLibs.Name -join ', ')"
if (-not (Test-Path (Join-Path $zlibInclude 'zlib.h')) -or $zlibLibs.Count -eq 0) {
    throw 'zlib:x64-uwp headers or library missing'
}
$zlibLib = $zlibLibs[0].FullName

$common = @('/nologo','/c','/O1','/MD','/EHsc','/clang:-std=c++20',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_APP','/DNSMBU_XBOX_UWP=1',
    '/D_CRT_SECURE_NO_WARNINGS',
    "/I$(Join-Path $root 'runtime/src')",
    "/I$(Join-Path $root 'runtime/include')",
    "/I$zlibInclude")
$modules = @(
    @('runtime/src/core/core.cpp','core.obj'),
    @('runtime/src/hle/coreinit_mem.cpp','hle.obj'),
    @('runtime/src/core/write_watch.cpp','write_watch.obj'),
    @('xbox/runtime-compat/r2_real_dispatch_probe.cpp','probe.obj'),
    @('xbox/runtime-compat/r2_public_guest_hooks.cpp','hooks.obj')
)
$objects = @()
foreach ($entry in $modules) {
    $obj = Join-Path $out $entry[1]
    & $clang @common "/Fo$obj" (Join-Path $root $entry[0])
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $obj)) {
        throw "Upstream compilation failed: $($entry[0])"
    }
    Write-Host "R2 compiled $($entry[0])"
    $objects += $obj
}

$fixture = Join-Path $out 'public-ppc-fixture'
& py -3 (Join-Path $root 'recomp/android_fixture.py') --output $fixture
if ($LASTEXITCODE -ne 0) { throw 'Recompiler public fixture failed' }
$gen = Join-Path $fixture 'gen'
if (-not (Test-Path (Join-Path $gen 'table.c'))) { throw 'Missing recomp-generated table.c' }
$generated = @(Get-ChildItem $gen -Filter '*.c' -File |
    Where-Object { $_.Name -notin @('harness.c','imports.c') })
if ($generated.Count -lt 2) { throw 'Expected table.c and generated C guest body' }
$cflags = @('/nologo','/c','/O2','/MD','/clang:-std=c11',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_APP',
    "/I$gen",
    "/I$(Join-Path $root 'runtime/include')",
    "/I$(Join-Path $root 'runtime/src')")
foreach ($source in $generated) {
    $obj = Join-Path $out ($source.BaseName + '.obj')
    & $clang @cflags "/Fo$obj" $source.FullName
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $obj)) {
        throw "Generated fixture C compilation failed: $($source.Name)"
    }
    Write-Host "R2 fixture compiled $($source.Name)"
    $objects += $obj
}

$symbols = & $dumpbin /symbols (Join-Path $out 'core.obj') | Out-String
if ($LASTEXITCODE -ne 0 -or $symbols -notmatch 'hle_find' -or $symbols -notmatch 'register_host') {
    throw 'Real core.cpp HLE registry/dispatcher symbols missing'
}
# UWP-family imports, but a desktop host executable rather than activatable AppX.
$exe = Join-Path $out 'r2_real_dispatch.exe'
& $clang /nologo @objects $onecore $zlibLib "/Fe:$exe" '/link' '/SUBSYSTEM:CONSOLE'
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $exe)) {
    throw 'R2.1 link failed: inspect unresolved imports above'
}
Write-Host 'R2.1 LINK PASS: production core/HLE/write-watch + generated guest.'
& $dumpbin /dependents $exe
if ($LASTEXITCODE -ne 0) { throw 'PE import audit failed' }
& $exe
if ($LASTEXITCODE -ne 0) { throw "R2.1 guest + HLE behavior FAILED: $LASTEXITCODE" }
Write-Host 'R2.1 EXECUTION PASS: guest 42 and production-dispatched Cafe OS heap lifecycle.'
"R2.1 PASS: production registry, dispatch, HLE, write-watch and genuine public generated guest on Windows host. Xbox UWP activation is still unverified." >> $env:GITHUB_STEP_SUMMARY
