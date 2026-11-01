# verify-build.ps1 -- run this on the BUILD machine.
#
# Captures everything needed to classify a failure without a second round trip:
#   <Out>\env.log         toolchain versions actually used, plus git state
#   <Out>\configure.log   full cmake output (including dependency downloads)
#   <Out>\build.log       full compiler output, keeps going after the first error
#   <Out>\summary.txt     error codes grouped, first occurrence, failing files
#   <Out>\run.log         everything the script itself printed
#
# Designed to survive being dropped anywhere: it finds the source tree by walking
# up from its own location, and if that fails it looks next to itself and one
# level down. Every early failure is written to a log and the window is kept open
# so the message can actually be read.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File verify-build.ps1
#   powershell -ExecutionPolicy Bypass -File verify-build.ps1 -Source G:\hacker\solstice\solstice-src\solstice-master-main\solstice-master-main
#   ... -NoPause        (for automation; default keeps the window open)
#
# ASCII only on purpose: non-ASCII text in a .ps1 breaks under the GBK code page.

param(
    [string]$Source = "",
    [string]$Out = "",
    [string]$BuildType = "RelWithDebInfo",
    [switch]$Fresh,
    [switch]$NoPause
)

$ErrorActionPreference = "Continue"
$script:logPath = Join-Path $PSScriptRoot "verify-build-error.log"

function Say([string]$text, [string]$color = "Gray") {
    Write-Host $text -ForegroundColor $color
    Add-Content -Path $script:logPath -Value $text -Encoding utf8
}

function Fail([string]$text, [int]$code) {
    Say ""
    Say "ERROR: $text" "Red"
    Say ""
    Say "This message was also written to:" "Yellow"
    Say "  $script:logPath" "Yellow"
    if (-not $NoPause) { Write-Host ""; Write-Host "Press Enter to close..." -ForegroundColor Yellow; $null = Read-Host }
    exit $code
}

Remove-Item $script:logPath -ErrorAction SilentlyContinue
Say "verify-build.ps1 started $(Get-Date -Format o)"
Say "script location : $PSScriptRoot"

# ------------------------------------------------------- locate the source tree
function Test-SourceTree([string]$p) {
    if (-not $p) { return $false }
    $cl = Join-Path $p "CMakeLists.txt"
    if (-not (Test-Path $cl)) { return $false }
    return (Select-String -Path $cl -Pattern "project\(Solstice\)" -Quiet)
}

if ($Source) {
    if (-not (Test-SourceTree $Source)) { Fail "the -Source directory does not look like the Solstice project (no CMakeLists.txt with project(Solstice)): $Source" 2 }
} else {
    $candidates = New-Object System.Collections.ArrayList
    $dir = $PSScriptRoot
    for ($i = 0; $i -lt 6 -and $dir; $i++) {
        [void]$candidates.Add((Join-Path $dir "solstice\solstice-src\solstice-master-main\solstice-master-main"))
        [void]$candidates.Add((Join-Path $dir "solstice\solstice-src\solstice-master-main"))
        $dir = Split-Path -Parent $dir
    }
    [void]$candidates.Add((Join-Path $PSScriptRoot "..\solstice\solstice-src\solstice-master-main\solstice-master-main"))
    [void]$candidates.Add((Join-Path $PSScriptRoot "..\..\solstice\solstice-src\solstice-master-main\solstice-master-main"))

    foreach ($c in $candidates) {
        $full = [System.IO.Path]::GetFullPath($c)
        if (Test-SourceTree $full) { $Source = $full; break }
    }
    # last resort: a shallow search under the script's own directory
    if (-not $Source) {
        $hit = Get-ChildItem -Path $PSScriptRoot -Filter "CMakeLists.txt" -Recurse -Depth 5 -ErrorAction SilentlyContinue |
               Where-Object { Select-String -Path $_.FullName -Pattern "project\(Solstice\)" -Quiet } |
               Select-Object -First 1
        if ($hit) { $Source = $hit.DirectoryName }
    }
    if (-not $Source) {
        Say ""
        Say "Looked for solstice\solstice-src\solstice-master-main\solstice-master-main starting from:" "Yellow"
        Say "  $PSScriptRoot"
        Fail "could not find the Solstice source tree. Pass it explicitly: -Source <path to the folder containing CMakeLists.txt>" 2
    }
}
Say "source tree     : $Source"

# --------------------------------------------------------------------- outputs
if (-not $Out) { $Out = Join-Path $PSScriptRoot "build-verify" }
if ($Fresh -and (Test-Path $Out)) { Remove-Item -Recurse -Force $Out -ErrorAction SilentlyContinue }
try { New-Item -ItemType Directory -Force -Path $Out | Out-Null } catch { Fail "cannot create output directory: $Out" 2 }
Say "output dir      : $Out"
$envLog = Join-Path $Out "env.log"
$cfgLog = Join-Path $Out "configure.log"
$buildLog = Join-Path $Out "build.log"
$summary = Join-Path $Out "summary.txt"
$runLog = Join-Path $Out "run.log"

# ------------------------------------------------------------- tool discovery
function Find-Tool([string]$name, [string[]]$extra) {
    $c = Get-Command $name -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    foreach ($p in $extra) { if (Test-Path $p) { return $p } }
    return $null
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsRoot = $null
if (Test-Path $vswhere) {
    # Prefer an instance that actually has the C++ toolset; fall back to any
    # instance so the log can still say what is installed.
    $vsRoot = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath) 2>$null
    if (-not $vsRoot) { $vsRoot = (& $vswhere -latest -products * -property installationPath) 2>$null }
    if ($vsRoot) { $vsRoot = @($vsRoot)[0] }
}

# ------------------------------------------------------- MSVC environment
# A plain console has neither cl.exe nor rc.exe on PATH, so cmake cannot pick a
# compiler and configure dies with "CMAKE_C_COMPILER not set". Reproduce what a
# developer prompt does: run vcvars64.bat once and import the variables it sets.
$vcvars = $null
if ($vsRoot) { $vcvars = Join-Path $vsRoot "VC\Auxiliary\Build\vcvars64.bat" }
$vcvarsLoaded = $false
$vcvarsLog = Join-Path $Out "vcvars.log"
if ($vcvars -and (Test-Path $vcvars)) {
    try {
        $dump = & cmd.exe /c "call `"$vcvars`" > nul 2>&1 && set" 2>&1
        foreach ($line in $dump) {
            $eq = $line.IndexOf("=")
            if ($eq -gt 0) {
                $k = $line.Substring(0, $eq)
                $v = $line.Substring($eq + 1)
                Set-Item -Path "env:$k" -Value $v -ErrorAction SilentlyContinue
            }
        }
        $vcvarsLoaded = $true
        & cmd.exe /c "where cl & where rc & cl" > $vcvarsLog 2>&1
    } catch {
        "vcvars import failed: $_" | Out-File -Encoding utf8 $vcvarsLog
    }
} else {
    "vcvars64.bat not found under: $vsRoot" | Out-File -Encoding utf8 $vcvarsLog
}

$cmakeExtra = @()
$ninjaExtra = @()
if ($vsRoot) {
    $cmakeExtra += (Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")
    $ninjaExtra += (Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe")
}
$cmakeExtra += "${env:ProgramFiles}\CMake\bin\cmake.exe"
$ninjaExtra += "C:\msys64\mingw64\bin\ninja.exe"

$cmake = Find-Tool "cmake.exe" $cmakeExtra
$ninja = Find-Tool "ninja.exe" $ninjaExtra

# ------------------------------------------------------------------ environment
"" | Out-File -Encoding utf8 $envLog
"=== verify-build environment ===" | Out-File -Append -Encoding utf8 $envLog
"date        : $(Get-Date -Format o)" | Out-File -Append -Encoding utf8 $envLog
"host        : $env:COMPUTERNAME  $([System.Environment]::OSVersion.VersionString)" | Out-File -Append -Encoding utf8 $envLog
"script      : $PSScriptRoot" | Out-File -Append -Encoding utf8 $envLog
"source      : $Source" | Out-File -Append -Encoding utf8 $envLog
"build type  : $BuildType" | Out-File -Append -Encoding utf8 $envLog
"cmake       : $(if ($cmake) { $cmake } else { 'NOT FOUND' })" | Out-File -Append -Encoding utf8 $envLog
"ninja       : $(if ($ninja) { $ninja } else { 'NOT FOUND' })" | Out-File -Append -Encoding utf8 $envLog
$clCmd = Get-Command cl.exe -ErrorAction SilentlyContinue
"cl.exe      : $(if ($clCmd) { $clCmd.Source } else { 'NOT ON PATH (open the x64 Native Tools prompt, or let the script find VS)' })" | Out-File -Append -Encoding utf8 $envLog
$rcCmd = Get-Command rc.exe -ErrorAction SilentlyContinue
"rc.exe      : $(if ($rcCmd) { $rcCmd.Source } else { 'NOT ON PATH (Windows SDK bin not in PATH)' })" | Out-File -Append -Encoding utf8 $envLog
"vs install  : $(if ($vsRoot) { $vsRoot } else { 'vswhere found nothing' })" | Out-File -Append -Encoding utf8 $envLog
"vcvars64    : $(if ($vcvarsLoaded) { "loaded ($vcvars)" } elseif ($vcvars) { "present but failed to load ($vcvars)" } else { 'NOT FOUND (C++ workload missing?)' })" | Out-File -Append -Encoding utf8 $envLog

if ($cmake) { (& $cmake --version | Select-Object -First 1) | Out-File -Append -Encoding utf8 $envLog }
if ($ninja) { (& $ninja --version) | Out-File -Append -Encoding utf8 $envLog }

# Walk up from the source tree instead of assuming a fixed depth: the checkout
# root is not always the same number of levels above src/.
$repoRoot = $null
$probe = $Source
for ($i = 0; $i -lt 8 -and $probe; $i++) {
    if (Test-Path (Join-Path $probe ".git")) { $repoRoot = $probe; break }
    $probe = Split-Path -Parent $probe
}
if ($repoRoot) {
    "=== git ===" | Out-File -Append -Encoding utf8 $envLog
    "repo        : $repoRoot" | Out-File -Append -Encoding utf8 $envLog
    (& git -C $repoRoot rev-parse HEAD 2>&1) | Out-File -Append -Encoding utf8 $envLog
    "dirty (tracked):" | Out-File -Append -Encoding utf8 $envLog
    (& git -C $repoRoot status --porcelain --untracked-files=no 2>&1) | Out-File -Append -Encoding utf8 $envLog
} else {
    "git         : no .git above $Source (not a worktree)" | Out-File -Append -Encoding utf8 $envLog
}

Get-Content $envLog | ForEach-Object { Write-Host $_; Add-Content -Path $runLog -Value $_ -Encoding utf8 }

if (-not $cmake) { Fail "cmake.exe not found. Install CMake or run this from a VS developer prompt." 4 }

# ------------------------------------------------------------------ configure
Say ""
Say "=== configure ===" "Cyan"
$cfgArgs = @("-S", $Source, "-B", $Out, "-DCMAKE_BUILD_TYPE=$BuildType")
# Reuse the dependency cache the existing build tree already populated, otherwise
# a fresh build dir re-downloads ~12 MB of archives and fails outright on an
# offline machine.
foreach ($d in @("out\build\x64-Release\_deps-local", "out\build\x64-Debug\_deps-local")) {
    $candidate = Join-Path $Source $d
    if (Test-Path $candidate) {
        Say "reusing dependency cache: $candidate" "Yellow"
        $cfgArgs += "-DSOLSTICE_DEPS_CACHE=$candidate"
        break
    }
}
if ($ninja) { $cfgArgs += @("-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=$ninja") }

# Native stderr arrives as ErrorRecord objects. Writing those straight into a log
# emits the whole "CategoryInfo / FullyQualifiedErrorId" block and buries the
# actual compiler message, so unwrap to the message before logging.
function Invoke-Logged([string]$exe, [string[]]$exeArgs, [string]$logPath) {
    $lines = @(& $exe @exeArgs 2>&1 | ForEach-Object {
        if ($_ -is [System.Management.Automation.ErrorRecord]) { $_.Exception.Message } else { "$_" }
    })
    $lines | Set-Content -Encoding utf8 $logPath
    if ($lines.Count -gt 0) { $lines | Add-Content -Path $runLog -Encoding utf8 }
    $lines | ForEach-Object { Write-Host $_ }
    return $LASTEXITCODE
}

$cfgExit = Invoke-Logged $cmake $cfgArgs $cfgLog
"[configure exit code: $cfgExit]" | Out-File -Append -Encoding utf8 $cfgLog
if ($cfgExit -ne 0) {
    Say ""
    Say "configure FAILED (exit $cfgExit). Full output:" "Red"
    Say "  $cfgLog" "Yellow"
    if (-not $NoPause) { Write-Host ""; Write-Host "Press Enter to close..." -ForegroundColor Yellow; $null = Read-Host }
    exit 3
}

# ---------------------------------------------------------------------- build
Say ""
Say "=== build ===" "Cyan"
# -k 0 keeps ninja going after the first error so a single run shows everything.
$buildExit = Invoke-Logged $cmake @("--build", $Out, "--", "-k", "0") $buildLog
"[build exit code: $buildExit]" | Out-File -Append -Encoding utf8 $buildLog
if (-not $ninja) {
    Say "note: ninja was not found, so the generator was chosen by cmake; failure output may differ from a Ninja build." "Yellow"
}

# -------------------------------------------------------------------- summary
$lines = Get-Content $buildLog -ErrorAction SilentlyContinue
$codes = @{}
foreach ($l in $lines) {
    foreach ($m in [regex]::Matches($l, "error\s+((?:C|LNK|MSB)\d{3,5})")) {
        $c = $m.Groups[1].Value
        if ($codes.ContainsKey($c)) { $codes[$c]++ } else { $codes[$c] = 1 }
    }
}
"" | Out-File -Encoding utf8 $summary
"=== build failure summary ===" | Out-File -Append -Encoding utf8 $summary
"build exit code: $buildExit" | Out-File -Append -Encoding utf8 $summary
"distinct error codes: $($codes.Count)" | Out-File -Append -Encoding utf8 $summary
$codes.GetEnumerator() | Sort-Object -Property Value -Descending |
    ForEach-Object { "  $($_.Value) x $($_.Key)" } | Out-File -Append -Encoding utf8 $summary

"" | Out-File -Append -Encoding utf8 $summary
"-- first occurrence per code --" | Out-File -Append -Encoding utf8 $summary
foreach ($c in $codes.Keys) {
    $first = $lines | Where-Object { $_ -match "error\s+$c" } | Select-Object -First 1
    "  $c : $first" | Out-File -Append -Encoding utf8 $summary
}

"" | Out-File -Append -Encoding utf8 $summary
"-- files that failed --" | Out-File -Append -Encoding utf8 $summary
$files = $lines | ForEach-Object { if ($_ -match "^(.*\.(cpp|hpp|h|c))\(\d+") { $matches[1] } } | Sort-Object -Unique
$files | ForEach-Object { "  $_" } | Out-File -Append -Encoding utf8 $summary

Get-Content $summary | ForEach-Object { Write-Host $_; Add-Content -Path $runLog -Value $_ -Encoding utf8 }

Say ""
if ($buildExit -eq 0) {
    Say "BUILD OK. dll should be under: $Out" "Green"
} else {
    Say "BUILD FAILED (exit $buildExit)." "Red"
}
Say "artifacts: $Out" "Green"
Say "send back: env.log, configure.log, build.log, summary.txt" "Yellow"
if (-not $NoPause) { Write-Host ""; Write-Host "Press Enter to close..." -ForegroundColor Yellow; $null = Read-Host }
exit $buildExit
