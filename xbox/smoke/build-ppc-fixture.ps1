# Build a tiny, PUBLIC synthetic PowerPC guest with the upstream
# nsmbu-recomp pipeline, then compile the resulting C with clang-cl for
# the Xbox UWP (x64) executable linked by MSVC/C++-CX.
# NO Nintendo RPX, assets, game dumps or secrets are loaded or uploaded.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$fixtureRoot = Join-Path $env:RUNNER_TEMP 'nsmbu-xbox-public-ppc-fixture'
if (Test-Path $fixtureRoot) {
    Remove-Item $fixtureRoot -Recurse -Force
}
& py -3 (Join-Path $repoRoot 'recomp/android_fixture.py') --output $fixtureRoot
if ($LASTEXITCODE -ne 0) { throw 'Upstream synthetic PPC -> C recompiler failed' }

$generated = Join-Path $fixtureRoot 'gen'
$funcs = Join-Path $generated 'funcs.h'
if (-not (Test-Path $funcs)) { throw 'Upstream fixture funcs.h missing' }
$clangOptions = @(
    (Get-Command clang-cl.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1),
    'C:\Program Files\LLVM\bin\clang-cl.exe',
    (Join-Path $env:ProgramFiles 'Microsoft Visual Studio/2022/Enterprise/VC/Tools/Llvm/x64/bin/clang-cl.exe')
)
$clang = $clangOptions | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $clang) { throw 'clang-cl not available; musttail guest source cannot safely use MSVC' }
Write-Host "PPC fixture compiler: $clang"
& $clang --version
if ($LASTEXITCODE -ne 0) { throw 'clang-cl version query failed' }

$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = (& $vswhere -latest -products '*' -property installationPath | Select-Object -First 1)
if (-not $vs) { throw 'Visual Studio toolchain missing for lib.exe' }
$msvcRoot = Join-Path $vs 'VC/Tools/MSVC'
$msvc = Get-ChildItem $msvcRoot -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $msvc) { throw 'MSVC toolset missing' }
$libExe = Join-Path $msvc.FullName 'bin/Hostx64/x64/lib.exe'
if (-not (Test-Path $libExe)) { throw "No MSVC librarian: $libExe" }

$output = Join-Path $PSScriptRoot 'generated'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$objects = @()
# Compile generated translated functions only. The generated harness,
# table, and import table are desktop test metadata, not part of UWP.
$sources = @(Get-ChildItem $generated -Filter '*.c' -File |
    Where-Object { $_.Name -notin @('harness.c','table.c','imports.c') })
if ($sources.Count -lt 1) { throw 'No PowerPC-generated C function source files' }
$sources += (Get-Item (Join-Path $PSScriptRoot 'ppc_fixture_bridge.c'))
foreach ($file in $sources) {
    $obj = Join-Path $output ($file.BaseName + '.obj')
    & $clang /nologo /c /O2 /MD /clang:-std=c11 `
        "/I$generated" "/I$(Join-Path $repoRoot 'runtime/include')" `
        "/Fo$obj" $file.FullName
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $obj)) {
        throw "clang-cl failed compiling $($file.Name)"
    }
    $objects += $obj
    Write-Host "Compiled generated PPC fixture object: $($file.Name)"
}
$library = Join-Path $output 'guest_fixture.lib'
& $libExe /nologo "/OUT:$library" @objects
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $library)) {
    throw 'PPC generated fixture library failed'
}
Write-Host "PPC generated fixture library ready: $library (contains $($objects.Count) objects)"
