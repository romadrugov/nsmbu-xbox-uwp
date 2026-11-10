# Public R2.4 wiring test: no Nintendo/game content, only synthetic PPC.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference = $false
$dir = $PSScriptRoot
$root = (Resolve-Path (Join-Path $dir '../..')).Path
& (Join-Path $dir 'test-r2-production-dispatch.ps1')
if ($LASTEXITCODE -ne 0) { throw 'R2.1 host runtime prerequisite failed' }
$out = Join-Path $env:RUNNER_TEMP 'r2-real-production-dispatch'
foreach ($obj in @('core.obj','hle.obj','write_watch.obj','hooks.obj','code_000.obj','table.obj')) {
    if (-not (Test-Path (Join-Path $out $obj))) { throw "R2.1 object missing: $obj" }
}
$clangOptions = @((Get-Command clang-cl.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1),'C:\Program Files\LLVM\bin\clang-cl.exe')
$clang = $clangOptions | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $clang) { throw 'clang-cl missing' }
$cxx = @('/nologo','/c','/O2','/MD','/EHsc','/clang:-std=c++20',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_APP','/DNSMBU_XBOX_UWP=1',
    "/I$(Join-Path $root 'runtime/src')","/I$(Join-Path $root 'runtime/include')")
foreach ($name in @('r24_private_dispatch.cpp','r24_public_host_main.cpp')) {
    $obj = Join-Path $out ($name -replace '.cpp$', '.obj')
    & $clang @cxx "/Fo$obj" (Join-Path $dir $name)
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $obj)) { throw "Compile failed: $name" }
}
$publicSelected = Join-Path $out 'r24_public_selected.obj'
$selectedArgs = @('/nologo','/c','/O2','/MD','/clang:-std=c11',
    "/I$(Join-Path $root 'runtime/include')", "/Fo$publicSelected",
    (Join-Path $dir 'r24_public_selected.c'))
& $clang @selectedArgs
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $publicSelected)) {
    throw 'R24 synthetic selected leaf compilation failed'
}
$binding = Join-Path $out 'r24_public_only_binding.obj'
$c = @('/nologo','/c','/O2','/MD','/clang:-std=c11',
    "/I$(Join-Path $root 'runtime/include')","/Fo$binding",
    (Join-Path $dir 'r24_public_only_binding.c'))
& $clang @c
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $binding)) { throw 'R24 synthetic binding compile failed' }
$sdkRoot = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Windows Kits/10'
$sdk = Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory |
    Where-Object { $_.Name -match '^10\.0\.\d+\.0$' } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $sdk) { throw 'Windows SDK missing' }
$onecore = Join-Path $sdkRoot "Lib/$($sdk.Name)/um/x64/OneCore.lib"
$vcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
if (-not $vcpkgRoot -or -not (Test-Path (Join-Path $vcpkgRoot 'vcpkg.exe'))) { $vcpkgRoot = 'C:\vcpkg' }
$libs = @(Get-ChildItem (Join-Path $vcpkgRoot 'installed/x64-uwp/lib') -Filter '*.lib' -File)
if ($libs.Count -ne 1) { throw 'zlib not available from R2.1 prerequisite' }
$objects = @('core.obj','hle.obj','write_watch.obj','hooks.obj',
             'code_000.obj','table.obj','r24_private_dispatch.obj',
             'r24_public_host_main.obj','r24_public_selected.obj','r24_public_only_binding.obj') |
    ForEach-Object { Join-Path $out $_ }
$exe = Join-Path $out 'r24_public_wiring_host.exe'
& $clang /nologo @objects $onecore $libs[0].FullName "/Fe:$exe" '/link' '/SUBSYSTEM:CONSOLE'
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $exe)) { throw 'R24 synthetic binding host link failed' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'R24 synthetic binding host execution FAILED' }
Write-Host 'R2.4 PUBLIC WIRING PASS: production guest dispatch registers synthetic function, CPU witness matches.'
Write-Host 'No Nintendo/game code was compiled or used in this test.'
"R2.4 public dispatch wiring **PASS** on Windows host: production dispatch::set, guest call and known synthetic CPU result. Actual NSMBU function remains private/not executed." >> $env:GITHUB_STEP_SUMMARY
