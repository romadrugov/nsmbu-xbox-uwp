# Build UWP Mesa Dozen using pinned Smash4 Xbox patches. Public sources only.
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$MesaRoot,
  [Parameter(Mandatory=$true)][string]$SmashSource
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$mesa = [IO.Path]::GetFullPath($MesaRoot).TrimEnd('\','/')
$smash = (Resolve-Path $SmashSource).Path
if (Test-Path $mesa) { throw "Expected a fresh Mesa checkout: $mesa" }
$patch = Join-Path $smash 'patches/mesa-dozen-xbox.patch'
$stubs = Join-Path $smash 'src/mesa_sysstubs.c'
$compat = Join-Path $smash 'src/mesa_uwp_compat.h'
foreach ($p in @($patch,$stubs,$compat)) {
  if (-not (Test-Path $p)) { throw "Missing Smash4 UWP reference: $p" }
}
& git clone --filter=blob:none --depth 1 --branch mesa-26.2.6 https://gitlab.freedesktop.org/mesa/mesa.git $mesa
if ($LASTEXITCODE -ne 0) { throw 'Official Mesa 26.2.6 clone failed' }
Write-Host "Mesa source commit: $(& git -C $mesa rev-parse HEAD)"
& git -C $mesa apply --check $patch
if ($LASTEXITCODE -ne 0) { throw 'Smash4 Xbox patch does not apply to the pinned Mesa source' }
& git -C $mesa apply $patch
if ($LASTEXITCODE -ne 0) { throw 'Mesa Xbox patch application failed' }

$uwp = Join-Path $mesa 'uwp'
New-Item -ItemType Directory -Path $uwp | Out-Null
Copy-Item $stubs (Join-Path $uwp 'mesa_sysstubs.c')
Copy-Item $compat (Join-Path $uwp 'mesa_uwp_compat.h')
$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -property installationPath | Select-Object -First 1
if (-not $vs) { throw 'Visual Studio missing' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
if (-not (Test-Path $vcvars)) { throw 'vcvarsall.bat missing' }
# x64 store selects the AppContainer runtime, not desktop MSVC.
$cmdline = '"' + $vcvars + '" x64 store >nul 2>&1 && set'
& cmd.exe /d /c $cmdline | ForEach-Object {
  if ($_ -match '^([^=]+)=(.*)$') {
    Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] -ErrorAction SilentlyContinue
  }
}
if (-not $env:VSCMD_ARG_TGT_ARCH -or $env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
  throw 'VS x64 Store environment failed'
}
$kit = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Windows Kits/10'
$d3d12Lib = @(Get-ChildItem (Join-Path $kit 'Lib') -Directory |
  Sort-Object { [version]$_.Name } -Descending |
  ForEach-Object { Join-Path $_.FullName 'um/x64/d3d12.lib' } |
  Where-Object { Test-Path $_ } | Select-Object -First 1)
if ($d3d12Lib.Count -ne 1) { throw 'SDK d3d12.lib missing' }
$env:LIB = "$(Split-Path $d3d12Lib[0] -Parent);$env:LIB"
& py -3 -m pip install --disable-pip-version-check 'meson>=1.5,<2' ninja mako packaging ply pyyaml
if ($LASTEXITCODE -ne 0) { throw 'Meson Python tools unavailable' }
if (-not (Get-Command clang-cl -ErrorAction SilentlyContinue) -or
    -not (Get-Command llvm-lib -ErrorAction SilentlyContinue)) {
  throw 'clang-cl/llvm-lib not found in VS environment PATH'
}
$stubsObj = Join-Path $uwp 'mesa_sysstubs.obj'
& clang-cl /c /O2 /MD /nologo "/Fo$stubsObj" (Join-Path $uwp 'mesa_sysstubs.c')
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $stubsObj)) {
  throw 'Mesa UWP import stubs compilation failed'
}
$cross = Join-Path $uwp 'cross-uwp.ini'
$compatUnix = ((Join-Path $uwp 'mesa_uwp_compat.h') -replace '\\', '/')
$stubsUnix = ($stubsObj -replace '\\', '/')
$crossText = @"
[binaries]
c = 'clang-cl'
cpp = 'clang-cl'
ar = 'llvm-lib'
windres = 'rc'
pkg-config = 'false'

[built-in options]
c_args = ['-DNOMINMAX', '-Wno-error', '/FI$compatUnix']
cpp_args = ['-DNOMINMAX', '-Wno-error', '/FI$compatUnix']
c_link_args = ['$stubsUnix', 'WindowsApp.lib']
cpp_link_args = ['$stubsUnix', 'WindowsApp.lib']
b_vscrt = 'md'

[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
"@
$crossText | Set-Content -LiteralPath $cross -Encoding ascii
$build = Join-Path $mesa 'build-uwp'
$mesonArgs = @(
  'setup',$build,"--cross-file=$cross",'--buildtype=release',
  '-Dvulkan-drivers=microsoft-experimental','-Dgallium-drivers=',
  '-Dopengl=false','-Dgles1=disabled','-Dgles2=disabled',
  '-Degl=disabled','-Dglx=disabled','-Dplatforms=windows',
  '-Dllvm=disabled','-Dzlib=disabled','-Dzstd=disabled',
  '-Dexpat=disabled','-Dbuild-tests=false',
  '-Dmicrosoft-clc=disabled','-Dspirv-to-dxil=false',
  '-Dvideo-codecs=','-Dshader-cache=disabled'
)
& py -3 -m mesonbuild.mesonmain @mesonArgs
if ($LASTEXITCODE -ne 0) { throw 'Mesa Dozen UWP meson configure failed' }
& ninja -C $build
if ($LASTEXITCODE -ne 0) { throw 'Mesa Dozen UWP compilation failed' }
$dll = Join-Path $build 'src/microsoft/vulkan/vulkan_dzn.dll'
if (-not (Test-Path $dll)) { throw 'Compiled vulkan_dzn.dll absent' }
Write-Host "R9 REAL UWP DOZEN BUILT: $dll"
Write-Host "Dozen SHA256: $((Get-FileHash $dll -Algorithm SHA256).Hash)"
