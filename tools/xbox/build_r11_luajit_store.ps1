# Build actual upstream pinned LuaJIT 2.1 under the exact x64 Store MSVC CRT
# used by the game. No Lua API stubs and no desktop precompiled DLL.
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$LuaJitRoot,
  [Parameter(Mandatory=$true)][string]$OutputDir
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$src = (Resolve-Path -LiteralPath $LuaJitRoot).Path
$out = [IO.Path]::GetFullPath($OutputDir)
if (Test-Path -LiteralPath $out) { throw 'LuaJIT private build path must not already exist' }
if (-not (Test-Path (Join-Path $src 'src/msvcbuild.bat')) -or
    -not (Test-Path (Join-Path $src 'src/ljamalg.c'))) {
  throw 'Expected pinned LuaJIT source submodule is incomplete'
}
$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -property installationPath | Select-Object -First 1
if (-not $vs) { throw 'Visual Studio not found' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
if (-not (Test-Path $vcvars)) { throw 'Store SDK toolchain not found' }
$vcCommand = '"' + $vcvars + '" x64 store >nul 2>&1 && set'
& cmd.exe /d /c $vcCommand | ForEach-Object {
  if ($_ -match '^([^=]+)=(.*)$') {
    Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] -ErrorAction SilentlyContinue
  }
}
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64' -or
    -not (Get-Command 'cl.exe' -ErrorAction SilentlyContinue)) {
  throw 'MSVC x64 WindowsStore tools unavailable'
}
Copy-Item -LiteralPath $src -Destination $out -Recurse
# A copied Git submodule has a .git file pointing outside this isolated
# source tree. LuaJIT's version generator would try that stale path.
$versionStamp = (& git -C $src show -s --format=%ct HEAD | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $versionStamp -notmatch '^\d{9,12}
$bat = Join-Path $buildDir 'msvcbuild.bat'
$text = [IO.File]::ReadAllText($bat)
# UWP requires dynamic CRT; xbox forbids JIT memory execution. The interpreter
# remains fully real and script APIs must resolve from the compiled LuaJIT.
$anchor = ' /nologo /c /O2 /W3 '
if (-not $text.Contains($anchor)) { throw 'Pinned LuaJIT build command layout changed' }
$text = $text.Replace($anchor,
  ' /nologo /c /O2 /W3 /MD /DLUAJIT_DISABLE_FFI /DLUAJIT_DISABLE_JIT ')
# LuaJIT's msvcbuild.bat generates minilua.exe + buildvm.exe first.
# They must run on the desktop CI runner, NOT use the UWP app CRT.
# msvcbuild wraps both host tool invocations in nested setlocal/endlocal.
# Switch to desktop x64 MSVC only inside those blocks; outside them the
# compiler remains x64 Store and the produced archive is AppContainer-ready.
$hostLabel = "`r`n:SETHOSTVARS`r`n"
if (-not $text.Contains($hostLabel)) { throw 'Pinned LuaJIT host-tool scope changed' }
$text = $text.Replace($hostLabel,
  ("`r`n:SETHOSTVARS`r`n@call `"" + $vcvars +
   "`" x64 >nul`r`n@if errorlevel 1 goto :BAD`r`n"))
# Do not compile a standalone UWP luajit.exe: it is not part of the game
# and its non-AppContainer entrypoint is irrelevant to the static library.
$cliAnchor = '%LJCOMPILE% luajit.c'
$cleanupAnchor = '@del *.obj *.manifest minilua.exe buildvm.exe'
if (-not $text.Contains($cliAnchor) -or -not $text.Contains($cleanupAnchor)) {
  throw 'LuaJIT standalone CLI phase layout changed'
}
$text = $text.Replace($cliAnchor, "@goto :R11_TARGET_LIB_READY`r`n" + $cliAnchor)
$text = $text.Replace($cleanupAnchor, ":R11_TARGET_LIB_READY`r`n" + $cleanupAnchor)
[IO.File]::WriteAllText($bat, $text, [Text.Encoding]::ASCII)
# LuaJIT's msvcbuild.bat may exit 0 even after jumping to :BAD; verify actual
# produced lib and exported API symbols explicitly.
$cmd = 'cd /d "' + $buildDir + '" && call msvcbuild.bat static'
& cmd.exe /d /c $cmd
if ($LASTEXITCODE -ne 0) { throw 'Pinned LuaJIT MSVC Store static build command failed' }
$lib = Join-Path $buildDir 'lua51.lib'
if (-not (Test-Path $lib) -or (Get-Item $lib).Length -lt 100000) {
  throw 'No real nontrivial LuaJIT static library produced'
}
$dumpbin = Join-Path $vs 'VC/Tools/MSVC'
$tools = @(Get-ChildItem -LiteralPath $dumpbin -Directory |
  Where-Object { $_.Name -match '^\d+\.\d+\.\d+' } |
  Sort-Object { [version]$_.Name } -Descending)
if (!$tools.Count) { throw 'MSVC dumpbin directory missing' }
$exe = Join-Path $tools[0].FullName 'bin/Hostx64/x64/dumpbin.exe'
$symbols = (& $exe /linkermember:1 $lib | Out-String)
if ($LASTEXITCODE -ne 0 -or
    $symbols -notmatch '\blua_pushcclosure\b' -or
    $symbols -notmatch '\bluaL_loadbuffer\b') {
  throw 'LuaJIT archive does not export mandatory real Lua APIs'
}
Write-Host "R11 REAL UWP LuaJIT static compiled: $lib"
Write-Host "R11 LuaJIT SHA256: $((Get-FileHash -LiteralPath $lib -Algorithm SHA256).Hash)"
Write-Host 'This real LuaJIT has JIT and FFI disabled; script interpreter is included.'
Write-Host 'Xbox execution and mods behavior are NOT yet verified.'
) {
  throw 'Cannot pin LuaJIT rolling version to source commit'
}
$copiedGit = Join-Path $out '.git'
if (Test-Path -LiteralPath $copiedGit) {
  Remove-Item -LiteralPath $copiedGit -Force -Recurse
}
[IO.File]::WriteAllText((Join-Path $out '.relver'), "$versionStamp`n")
$buildDir = Join-Path $out 'src'
$bat = Join-Path $buildDir 'msvcbuild.bat'
$text = [IO.File]::ReadAllText($bat)
# UWP requires dynamic CRT; xbox forbids JIT memory execution. The interpreter
# remains fully real and script APIs must resolve from the compiled LuaJIT.
$anchor = ' /nologo /c /O2 /W3 '
if (-not $text.Contains($anchor)) { throw 'Pinned LuaJIT build command layout changed' }
$text = $text.Replace($anchor,
  ' /nologo /c /O2 /W3 /MD /DLUAJIT_DISABLE_FFI /DLUAJIT_DISABLE_JIT ')
# The UWP /MD CRT's host-side LuaJIT generators also need WindowsApp
# imports (QueryPerformanceCounter, GetCurrentThreadId, RoInitialize).
# This affects BOTH temporary host generators and the static Lua library.
$oldLink = '@set LJLINK=link /nologo'
if (-not $text.Contains($oldLink)) { throw 'Pinned LuaJIT link setting changed' }
$text = $text.Replace($oldLink,
  '@set LJLINK=link /nologo WindowsApp.lib runtimeobject.lib')
[IO.File]::WriteAllText($bat, $text, [Text.Encoding]::ASCII)
# LuaJIT's msvcbuild.bat may exit 0 even after jumping to :BAD; verify actual
# produced lib and exported API symbols explicitly.
$cmd = 'cd /d "' + $buildDir + '" && call msvcbuild.bat static'
& cmd.exe /d /c $cmd
if ($LASTEXITCODE -ne 0) { throw 'Pinned LuaJIT MSVC Store static build command failed' }
$lib = Join-Path $buildDir 'lua51.lib'
if (-not (Test-Path $lib) -or (Get-Item $lib).Length -lt 100000) {
  throw 'No real nontrivial LuaJIT static library produced'
}
$dumpbin = Join-Path $vs 'VC/Tools/MSVC'
$tools = @(Get-ChildItem -LiteralPath $dumpbin -Directory |
  Where-Object { $_.Name -match '^\d+\.\d+\.\d+' } |
  Sort-Object { [version]$_.Name } -Descending)
if (!$tools.Count) { throw 'MSVC dumpbin directory missing' }
$exe = Join-Path $tools[0].FullName 'bin/Hostx64/x64/dumpbin.exe'
$symbols = (& $exe /linkermember:1 $lib | Out-String)
if ($LASTEXITCODE -ne 0 -or
    $symbols -notmatch '\blua_pushcclosure\b' -or
    $symbols -notmatch '\bluaL_loadbuffer\b') {
  throw 'LuaJIT archive does not export mandatory real Lua APIs'
}
Write-Host "R11 REAL UWP LuaJIT static compiled: $lib"
Write-Host "R11 LuaJIT SHA256: $((Get-FileHash -LiteralPath $lib -Algorithm SHA256).Hash)"
Write-Host 'This real LuaJIT has JIT and FFI disabled; script interpreter is included.'
Write-Host 'Xbox execution and mods behavior are NOT yet verified.'
