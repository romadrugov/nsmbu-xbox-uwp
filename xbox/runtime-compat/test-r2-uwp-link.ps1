# R2.2: AppContainer PE link-only, no private game data, never executed.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference = $false
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$dir = $PSScriptRoot
$build = Join-Path $dir 'r2_uwp_generated'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$clangOptions = @((Get-Command clang-cl.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1),'C:\Program Files\LLVM\bin\clang-cl.exe')
$clang = $clangOptions | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $clang) { throw 'clang-cl not available' }
$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -property installationPath | Select-Object -First 1
if (-not $vs) { throw 'Visual Studio missing' }
$msbuild = Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
$msvc = Get-ChildItem (Join-Path $vs 'VC/Tools/MSVC') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$dumpbin = Join-Path $msvc.FullName 'bin/Hostx64/x64/dumpbin.exe'
$sdkRoot = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Windows Kits/10'
$sdk = Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory | Where-Object { $_.Name -match '^10\.0\.\d+\.0$' } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $sdk) { throw 'Windows SDK missing' }
$vcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
if (-not $vcpkgRoot -or -not (Test-Path (Join-Path $vcpkgRoot 'vcpkg.exe'))) { $vcpkgRoot = 'C:\vcpkg' }
& (Join-Path $vcpkgRoot 'vcpkg.exe') install zlib:x64-uwp --disable-metrics
if ($LASTEXITCODE -ne 0) { throw 'zlib:x64-uwp install failed' }
$libs = @(Get-ChildItem (Join-Path $vcpkgRoot 'installed/x64-uwp/lib') -Filter '*.lib' -File)
if ($libs.Count -ne 1) { throw "Expected one zlib lib, found $($libs.Count)" }
$zlibLib = $libs[0].FullName
$zlibInclude = Join-Path $vcpkgRoot 'installed/x64-uwp/include'
Write-Host "UWP zlib archive: $zlibLib"
$cxx = @('/nologo','/c','/O1','/MD','/EHsc','/clang:-std=c++20',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_APP','/DNSMBU_XBOX_UWP=1',
    '/D_CRT_SECURE_NO_WARNINGS',
    "/I$(Join-Path $root 'runtime/src')",
    "/I$(Join-Path $root 'runtime/include')",
    "/I$zlibInclude")
$modules = @(
    @('runtime/src/core/core.cpp','core.obj'),
    @('runtime/src/hle/coreinit_mem.cpp','hle.obj'),
    @('xbox/runtime-compat/r2_uwp_write_watch.cpp','uwp_watch.obj'),
    @('xbox/runtime-compat/r2_uwp_link_bridge.cpp','bridge.obj'),
    @('xbox/runtime-compat/r2_public_guest_hooks.cpp','hooks.obj')
)
foreach ($entry in $modules) {
    $out = Join-Path $build $entry[1]
    & $clang @cxx "/Fo$out" (Join-Path $root $entry[0])
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $out)) { throw "R2.2 UWP compilation failed: $($entry[0])" }
    Write-Host "R2.2 UWP compiled: $($entry[0])"
}
$fixture = Join-Path $env:RUNNER_TEMP 'r2-uwp-public-fixture'
& py -3 (Join-Path $root 'recomp/android_fixture.py') --output $fixture
if ($LASTEXITCODE -ne 0) { throw 'Public PowerPC recompilation failed' }
$gen = Join-Path $fixture 'gen'
$cflags = @('/nologo','/c','/O2','/MD','/clang:-std=c11','/DWINAPI_FAMILY=WINAPI_FAMILY_APP',
    "/I$gen","/I$(Join-Path $root 'runtime/include')","/I$(Join-Path $root 'runtime/src')")
foreach ($file in @('code_000.c','table.c')) {
    $src = Join-Path $gen $file
    if (-not (Test-Path $src)) { throw "Generated $file missing" }
    $out = Join-Path $build $file.Replace('.c','.obj')
    & $clang @cflags "/Fo$out" $src
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $out)) { throw "R2.2 generated PPC compilation failed: $file" }
    Write-Host "R2.2 PPC compiled: $file"
}
$symbols = & $dumpbin /symbols (Join-Path $build 'core.obj') | Out-String
if ($LASTEXITCODE -ne 0 -or $symbols -notmatch 'hle_find' -or $symbols -notmatch 'register_host') { throw 'Real HLE/dispatcher absent in COFF' }
& py -3 (Join-Path $root 'xbox/smoke/make_assets.py')
if ($LASTEXITCODE -ne 0) { throw 'Public logo generation failed' }
$args = @((Join-Path $dir 'NSMBUR2UwpLinkProbe.vcxproj'),'/t:Build','/m','/v:minimal',
    '/p:Configuration=Release','/p:Platform=x64',
    "/p:WindowsTargetPlatformVersion=$($sdk.Name)","/p:R2ZlibLib=$zlibLib",
    '/p:GenerateAppxPackageOnBuild=false','/p:AppxPackageSigningEnabled=false')
& $msbuild @args
if ($LASTEXITCODE -ne 0) { throw 'R2.2 MSBuild UWP AppContainer link failed' }
$exe = Get-ChildItem $dir -Filter 'NSMBUR2UwpLinkProbe.exe' -File -Recurse | Select-Object -First 1
if (-not $exe) { throw 'No R2.2 PE produced' }
$headers = & $dumpbin /headers $exe.FullName | Out-String
if ($LASTEXITCODE -ne 0 -or $headers -notmatch 'machine \(x64\)' -or $headers -notmatch 'App Container' -or $headers -notmatch 'Windows GUI') {
    throw 'R2.2 PE lacks x64 GUI AppContainer flags'
}
$deps = & $dumpbin /dependents $exe.FullName | Out-String
if ($LASTEXITCODE -ne 0) { throw 'R2.2 PE imports unreadable' }
if ($deps -match '(?im)^\s+(?:USER32|MSVCP140|VCRUNTIME140)\.dll\s*$') { throw 'Desktop CRT/UI dependency in AppContainer' }
Write-Host "R2.2 PASS: Windows Store AppContainer x64 GUI PE ($($exe.Length) bytes)"
Write-Host $deps
Write-Host 'Build/link only; no UWP activation, packaging, Xbox execution or private code.'
"R2.2 AppContainer x64 UWP **LINK PASS**, public recompilation and real core/HLE; UWP GetWriteWatch adapter. PE import audit passed. No Xbox activation." >> $env:GITHUB_STEP_SUMMARY
