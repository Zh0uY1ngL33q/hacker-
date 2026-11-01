# verify-build.ps1 -- run on the BUILD machine, not on the analysis box.
#
# Captures everything needed to classify a failure without a second round trip:
#   <out>/env.log         toolchain versions actually used
#   <out>/configure.log   full cmake output
#   <out>/build.log       full compiler output (keeps going after the first error)
#   <out>/summary.txt     error codes grouped, plus the first occurrence of each
#
# Usage (from an "x64 Native Tools Command Prompt for VS", or plain PowerShell if
# cmake/ninja are on PATH):
#   powershell -ExecutionPolicy Bypass -File tools\build\verify-build.ps1
#   ... -Source G:\hacker\solstice\solstice-src\solstice-master-main\solstice-master-main
#
# ASCII only on purpose: non-ASCII text in a .ps1 breaks under the GBK code page.

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
if (-not (Test-Path $Source)) {
    Write-Host "source not found: $Source" -ForegroundColor Red
    exit 2
}

if ($Fresh -and (Test-Path $Out)) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Force -Path $Out | Out-Null

# ---------------------------------------------------------------- environment
$envLog = Join-Path $Out "env.log"
"=== verify-build environment ===" | Out-File -Encoding utf8 $envLog
"date        : $(Get-Date -Format o)" | Out-File -Append -Encoding utf8 $envLog
"host        : $env:COMPUTERNAME  $([System.Environment]::OSVersion.VersionString)" | Out-File -Append -Encoding utf8 $envLog
"source      : $Source" | Out-File -Append -Encoding utf8 $envLog
"build type  : $BuildType" | Out-File -Append -Encoding utf8 $envLog

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue)
if ($cmake) {
    "cmake       : $($cmake.Source)" | Out-File -Append -Encoding utf8 $envLog
    (& cmake --version | Select-Object -First 1) | Out-File -Append -Encoding utf8 $envLog
} else {
    "cmake       : NOT FOUND" | Out-File -Append -Encoding utf8 $envLog
}
$ninja = (Get-Command ninja -ErrorAction SilentlyContinue)
"ninja       : $(if ($ninja) { $ninja.Source } else { 'NOT FOUND' })" | Out-File -Append -Encoding utf8 $envLog
$rc = (Get-Command rc.exe -ErrorAction SilentlyContinue)
"rc.exe      : $(if ($rc) { $rc.Source } else { 'NOT FOUND (Windows SDK not on PATH)' })" | Out-File -Append -Encoding utf8 $envLog
"cl.exe      : $(if (Get-Command cl.exe -ErrorAction SilentlyContinue) { (Get-Command cl.exe).Source } else { 'NOT FOUND (open the x64 Native Tools prompt)' })" | Out-File -Append -Encoding utf8 $envLog

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    (& $vswhere -latest -products * -property installationPath) | Out-File -Append -Encoding utf8 $envLog
}
"=== git ===" | Out-File -Append -Encoding utf8 $envLog
(& git -C $repoRoot rev-parse HEAD) | Out-File -Append -Encoding utf8 $envLog
(& git -C $repoRoot status --porcelain --untracked-files=no) | Out-File -Append -Encoding utf8 $envLog

Get-Content $envLog | Write-Host

# ------------------------------------------------------------------ configure
$cfgLog = Join-Path $Out "configure.log"
Write-Host "`n=== configure ===" -ForegroundColor Cyan
$cfgArgs = @("-S", $Source, "-B", $Out, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=$BuildType")
& cmake @cfgArgs 2>&1 | Tee-Object -FilePath $cfgLog
$cfgExit = $LASTEXITCODE
"[configure exit code: $cfgExit]" | Out-File -Append -Encoding utf8 $cfgLog
if ($cfgExit -ne 0) {
    Write-Host "configure FAILED (exit $cfgExit) -- see $cfgLog" -ForegroundColor Red
    exit 3
}

# ---------------------------------------------------------------------- build
$buildLog = Join-Path $Out "build.log"
Write-Host "`n=== build ===" -ForegroundColor Cyan
# -k 0 keeps ninja going after the first error so one run shows every failure.
& cmake --build $Out -- -k 0 2>&1 | Tee-Object -FilePath $buildLog
$buildExit = $LASTEXITCODE
"[build exit code: $buildExit]" | Out-File -Append -Encoding utf8 $buildLog

# -------------------------------------------------------------------- summary
$summary = Join-Path $Out "summary.txt"
$lines = Get-Content $buildLog
$codes = @{}
foreach ($l in $lines) {
    foreach ($m in [regex]::Matches($l, "error\s+((?:C|LNK|MSB)\d{3,5})")) {
        $c = $m.Groups[1].Value
        if ($codes.ContainsKey($c)) { $codes[$c]++ } else { $codes[$c] = 1 }
    }
}
"=== build failure summary ===" | Out-File -Encoding utf8 $summary
"build exit code: $buildExit" | Out-File -Append -Encoding utf8 $summary
"distinct error codes: $($codes.Count)" | Out-File -Append -Encoding utf8 $summary
$codes.GetEnumerator() | Sort-Object -Property Value -Descending |
    ForEach-Object { "  $($_.Value) x $($_.Key)" } | Out-File -Append -Encoding utf8 $summary

"`n-- first occurrence per code --" | Out-File -Append -Encoding utf8 $summary
foreach ($c in $codes.Keys) {
    $first = $lines | Where-Object { $_ -match "error\s+$c" } | Select-Object -First 1
    "  $c : $first" | Out-File -Append -Encoding utf8 $summary
}

"`n-- files that failed --" | Out-File -Append -Encoding utf8 $summary
$files = $lines | ForEach-Object {
    if ($_ -match "^(.*\.(cpp|hpp|h))\(\d+") { $matches[1] }
} | Sort-Object -Unique
$files | ForEach-Object { "  $_" } | Out-File -Append -Encoding utf8 $summary

Get-Content $summary | Write-Host
Write-Host "`nartifacts in $Out" -ForegroundColor Green
Write-Host "send back: env.log, configure.log, build.log, summary.txt" -ForegroundColor Yellow
exit $buildExit
