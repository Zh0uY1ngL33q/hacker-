# verify-build.ps1 -- run this on the BUILD machine.
#
# v2: the first run stopped at configure because a plain PowerShell session has
# neither cl.exe nor rc.exe on PATH and no ninja. This version sets the MSVC
# environment up itself (vswhere -> vcvars64.bat) and takes Ninja from the
# Visual Studio install, so it can be launched from any shell.
#
# Outputs, all under -Out (default <repo>\build-verify):
#   env.log         what was found and how (toolchain, ninja, ld, git state)
#   configure.log   full cmake configure output
#   build.log       full compiler output; ninja -k 0 so it does not stop at #1
#   summary.txt     error codes grouped + first occurrence + failing files
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\build\verify-build.ps1
#   ... -Source <dir> -Out <dir> -BuildType Release -Fresh
#
# ASCII only: non-ASCII text in a .ps1/.bat breaks under the GBK code page.

param(
    [string]$Source = "",
    [string]$Out = "",
    [string]$BuildType = "RelWithDebInfo",
    [switch]$Fresh
)

$ErrorActionPreference = "Continue"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if (-not $Source) {
    $Source = Join-Path $repoRoot "solstice\solstice-src\solstice-master-main\solstice-master-main"
}
if (-not $Out) {
    $Out = Join-Path $repoRoot "build-verify"
}
$resolvedSource = Resolve-Path $Source -ErrorAction SilentlyContinue
if (-not $resolvedSource) {
    Write-Host "source not found: $Source" -ForegroundColor Red
    exit 2
}
$Source = $resolvedSource.Path
if ($Fresh -and (Test-Path $Out)) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path $Out).Path

$envLog = Join-Path $Out "env.log"
function Log($text) {
    $text | Tee-Object -FilePath $envLog -Append
}

"=== verify-build environment ===" | Set-Content -Encoding utf8 $envLog
Log "date        : $(Get-Date -Format o)"
Log "host        : $env:COMPUTERNAME  $([System.Environment]::OSVersion.VersionString)"
Log "source      : $Source"
Log "out         : $Out"
Log "build type  : $BuildType"

# ---------------------------------------------------------------- Visual Studio
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = $null
if (Test-Path $vswhere) {
    $vsPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath) | Select-Object -First 1
    if (-not $vsPath) {
        # fall back to any instance so the message can say what is actually there
        $vsPath = (& $vswhere -latest -products * -property installationPath) | Select-Object -First 1
        Log "VS          : instance present but WITHOUT the C++ workload: $vsPath"
    } else {
        Log "VS          : $vsPath"
    }
} else {
    Log "VS          : vswhere not found (no Visual Studio installer?)"
}

$vcvars = $null
if ($vsPath) { $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat" }
if (-not $vcvars -or -not (Test-Path $vcvars)) {
    Log "vcvars64    : NOT FOUND -> the C++ desktop workload is not installed."
    Log ""
    Log "ACTION: Visual Studio Installer -> Modify -> tick 'Desktop development with C++'."
    Write-Host "`nC++ toolchain missing. See $envLog" -ForegroundColor Red
    exit 2
}
Log "vcvars64    : $vcvars"

# ------------------------------------------------------------------ ninja/cmake
function Find-Tool($name, $extra) {
    $c = Get-Command $name -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    foreach ($p in $extra) { if ($p -and (Test-Path $p)) { return $p } }
    return $null
}

# vswhere already gave us the install root; deriving it from vcvars64.bat needs
# four Split-Path hops (file -> Build -> Auxiliary -> VC -> <VS>) and is exactly
# the kind of off-by-one that silently points at <VS>\VC.
$vsRoot = $vsPath
$cmake = Find-Tool "cmake" @((Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"))
$ninja = Find-Tool "ninja" @((Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"))
Log "cmake       : $(if ($cmake) { $cmake } else { 'NOT FOUND' })"
Log "ninja       : $(if ($ninja) { $ninja } else { 'NOT FOUND' })"

if (-not $cmake) { Write-Host "cmake not found" -ForegroundColor Red; exit 2 }
if (-not $ninja) {
    Log ""
    Log "ACTION: ninja not found. In the VS Installer add 'C++ CMake tools for Windows',"
    Log "        which ships <VS>\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    Write-Host "`nninja missing. See $envLog" -ForegroundColor Red
    exit 2
}
(& $cmake --version | Select-Object -First 1) | ForEach-Object { Log "cmake ver   : $_" }

# ld is a hard requirement of CMakeLists.txt (ld -r -b binary for resources)
$ld = Find-Tool "ld" @("C:\msys64\mingw64\bin\ld.exe")
Log "ld          : $(if ($ld) { $ld } else { 'NOT FOUND  (configure will FATAL_ERROR: install MSYS2 binutils)' })"

Log "=== git ==="
(& git -C $repoRoot rev-parse HEAD) 2>&1 | ForEach-Object { Log $_ }
(& git -C $repoRoot status --porcelain --untracked-files=no) 2>&1 | ForEach-Object { Log $_ }

# A cache left by an earlier failed run makes cmake refuse to reconfigure
# ("generator does not match"), which would look like a source problem.
if (Test-Path (Join-Path $Out "CMakeCache.txt")) {
    Log "cache       : removing stale CMakeCache.txt + CMakeFiles"
    Remove-Item -Force (Join-Path $Out "CMakeCache.txt") -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force (Join-Path $Out "CMakeFiles") -ErrorAction SilentlyContinue
}

# ----------------------------------------------------------------- run helpers
# Both steps go through a generated .bat: vcvars64 sets the environment and the
# redirection keeps native stderr out of PowerShell's ErrorRecord machinery, so
# the log contains compiler output instead of CategoryInfo blocks.
# The .bat is invoked directly (not through Start-Process cmd /c) so paths with
# spaces need no quoting games and $LASTEXITCODE carries the real status.
function Invoke-Bat($batPath) {
    & $batPath | Out-Null
    return $LASTEXITCODE
}

$vcvarsLog = Join-Path $Out "vcvars.log"
$cfgBat = Join-Path $Out "_configure.bat"
$cfgLog = Join-Path $Out "configure.log"
@"
@echo off
call "$vcvars" > "$vcvarsLog" 2>&1
if errorlevel 1 exit /b 900
where cl >> "$vcvarsLog" 2>&1
where rc >> "$vcvarsLog" 2>&1
cl 2>&1 | findstr /C:"Version" >> "$vcvarsLog"
"$cmake" -S "$Source" -B "$Out" -G Ninja -DCMAKE_BUILD_TYPE=$BuildType -DCMAKE_MAKE_PROGRAM="$ninja" > "$cfgLog" 2>&1
exit /b %ERRORLEVEL%
"@ | Set-Content -Encoding ascii $cfgBat

Write-Host "`n=== configure ===" -ForegroundColor Cyan
$cfgExit = Invoke-Bat $cfgBat
"`n[configure exit code: $cfgExit]" | Add-Content -Encoding utf8 $cfgLog
Get-Content $cfgLog | Select-Object -Last 25 | Write-Host
if ($cfgExit -ne 0) {
    Write-Host "`nconfigure FAILED (exit $cfgExit) -- full log: $cfgLog" -ForegroundColor Red
    Write-Host "send back: env.log, configure.log" -ForegroundColor Yellow
    exit 3
}

# ---------------------------------------------------------------------- build
$buildBat = Join-Path $Out "_build.bat"
$buildLog = Join-Path $Out "build.log"
@"
@echo off
call "$vcvars" > nul 2>&1
if errorlevel 1 exit /b 900
"$cmake" --build "$Out" -- -k 0 > "$buildLog" 2>&1
exit /b %ERRORLEVEL%
"@ | Set-Content -Encoding ascii $buildBat
Write-Host "`n=== build ===" -ForegroundColor Cyan
$buildExit = Invoke-Bat $buildBat
"`n[build exit code: $buildExit]" | Add-Content -Encoding utf8 $buildLog
Get-Content $buildLog | Select-Object -Last 25 | Write-Host

# -------------------------------------------------------------------- summary
$summary = Join-Path $Out "summary.txt"
$lines = @(Get-Content $buildLog -ErrorAction SilentlyContinue)
$codes = @{}
foreach ($l in $lines) {
    foreach ($m in [regex]::Matches($l, "error\s+((?:C|LNK|MSB)\d{3,5})")) {
        $c = $m.Groups[1].Value
        if ($codes.ContainsKey($c)) { $codes[$c]++ } else { $codes[$c] = 1 }
    }
}
"=== build failure summary ===" | Set-Content -Encoding utf8 $summary
"build exit code: $buildExit" | Add-Content -Encoding utf8 $summary
"distinct error codes: $($codes.Count)" | Add-Content -Encoding utf8 $summary
$codes.GetEnumerator() | Sort-Object -Property Value -Descending |
    ForEach-Object { "  $($_.Value) x $($_.Key)" } | Add-Content -Encoding utf8 $summary

"" | Add-Content -Encoding utf8 $summary
"-- first occurrence per code --" | Add-Content -Encoding utf8 $summary
foreach ($c in $codes.Keys) {
    $first = $lines | Where-Object { $_ -match "error\s+$c" } | Select-Object -First 1
    "  $c : $first" | Add-Content -Encoding utf8 $summary
}

"" | Add-Content -Encoding utf8 $summary
"-- files that failed --" | Add-Content -Encoding utf8 $summary
$lines | ForEach-Object {
    if ($_ -match "^(.*\.(cpp|hpp|h))\(\d+") { $matches[1] }
} | Sort-Object -Unique | ForEach-Object { "  $_" } | Add-Content -Encoding utf8 $summary

"" | Add-Content -Encoding utf8 $summary
"-- produced binaries --" | Add-Content -Encoding utf8 $summary
Get-ChildItem -Path $Out -Recurse -Filter *.dll -ErrorAction SilentlyContinue |
    ForEach-Object { "  $($_.FullName)  $([math]::Round($_.Length/1KB)) KB" } |
    Add-Content -Encoding utf8 $summary

Get-Content $summary | Write-Host
Write-Host "`nartifacts in $Out" -ForegroundColor Green
Write-Host "send back: env.log, configure.log, build.log, summary.txt" -ForegroundColor Yellow
exit $buildExit
