param([switch]$Test, [switch]$X86)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $root 'src'
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$sources = @(Get-ChildItem -Path $src -Filter '*.cpp' | ForEach-Object FullName)
$common = @('-std=c++11', '-O2', '-DUNICODE', '-D_UNICODE', '-Wall', '-Wextra', '-I', $src)
$libs = @('-lwinhttp', '-lcrypt32', '-lws2_32', '-lshell32', '-luser32', '-lgdi32')
if ($X86) { $common += @('-m32', '-DYDISK_NO_DLLEXPORT') }
$dll = if ($X86) { Join-Path $build 'ydisk_commander.wfx' } else { Join-Path $build 'ydisk_commander.wfx64' }
$linker = @()
if ($X86) {
    $def = Join-Path $src 'ydisk_commander.def'
    # Verify that the .def stdcall aliases match the decorated symbols the
    # compiler actually emits (catches wrong @N suffixes before link time).
    $obj = Join-Path $build 'plugin32_check.o'
    & g++ @common -c (Join-Path $src 'plugin.cpp') -o $obj
    if ($LASTEXITCODE -ne 0) { throw 'x86 compile check failed' }
    $nm = & nm -g --defined-only $obj |
        ForEach-Object { ($_ -split '\s+')[-1] } | Where-Object { $_ -like '_Fs*' } | Sort-Object
    Remove-Item -Force $obj
    $aliases = Get-Content $def |
        ForEach-Object { if ($_ -match '=\s*(\S+)$') { $Matches[1] } } | Sort-Object
    $diff = Compare-Object $nm $aliases
    if ($diff) {
        $diff | ForEach-Object { Write-Output ("def mismatch: " + $_.InputObject + " " + $_.SideIndicator) }
        throw 'ydisk_commander.def does not match the decorated x86 symbols'
    }
    $linker = @($def)
}
& g++ @common -shared -o $dll @sources @linker @libs
if ($LASTEXITCODE -ne 0) { throw 'DLL build failed' }
Write-Output "Built: $dll"
if ($Test) {
    if ($X86) { throw 'Smoke test executable is only configured for the native x64 toolchain' }
    $exe = Join-Path $build 'smoke.exe'
    & g++ @common -o $exe (Join-Path $root 'tests\smoke.cpp') @sources @libs
    if ($LASTEXITCODE -ne 0) { throw 'Smoke test build failed' }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw 'Smoke tests failed' }
}