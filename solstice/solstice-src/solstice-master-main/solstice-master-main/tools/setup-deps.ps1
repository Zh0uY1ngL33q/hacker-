<#
.SYNOPSIS
    Populates the optional local dependency directory used by CMakeLists.txt.

.DESCRIPTION
    CMakeLists.txt can fetch its six external dependencies itself (CPM, via git).
    This script is for the cases where that is undesirable: building offline, or
    pinning to a local copy instead of re-downloading on every clean configure.

    Upstream these dependencies were referenced only through SOURCE_DIR
    "D:/dev/deps/<name>" -- one developer's private directory -- so a fresh clone
    could not configure at all. They are now optional: any folder this script
    creates is auto-detected through SOLSTICE_DEPS_DIR.

    Folder names matter: CMakeLists.txt refers to them literally
    (spdlog-1.9.2, magic_enum-0.9.5, glm-1.0.1, json-3.11.3, DirectXTK-main,
    cryptopp-cmake-master). The archives below are the GitHub source snapshots
    whose extracted root directory has exactly those names -- GitHub strips the
    leading "v" from tags, so tag v1.9.2 extracts to spdlog-1.9.2.

.PARAMETER Dest
    Where to place the dependencies. Defaults to D:\dev\deps.

.PARAMETER Force
    Re-download and overwrite dependencies that already exist.

.EXAMPLE
    .\setup-deps.ps1
    .\setup-deps.ps1 -Dest E:\deps
    cmake -S . -B out/build/x64-Release -G Ninja -DSOLSTICE_DEPS_DIR=E:/deps
#>
param(
    [string] $Dest = 'D:\dev\deps',
    [switch] $Force
)

$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'   # Invoke-WebRequest is far faster without it

# Name is both the extracted folder name and what CMakeLists.txt looks for.
$deps = @(
    @{ Name = 'spdlog-1.9.2';          Url = 'https://github.com/gabime/spdlog/archive/refs/tags/v1.9.2.zip' }
    @{ Name = 'magic_enum-0.9.5';      Url = 'https://github.com/Neargye/magic_enum/archive/refs/tags/v0.9.5.zip' }
    @{ Name = 'glm-1.0.1';             Url = 'https://github.com/g-truc/glm/archive/refs/tags/1.0.1.zip' }
    @{ Name = 'json-3.11.3';           Url = 'https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.zip' }
    @{ Name = 'DirectXTK-main';        Url = 'https://github.com/microsoft/DirectXTK/archive/refs/heads/main.zip' }
    @{ Name = 'cryptopp-cmake-master'; Url = 'https://github.com/noloader/cryptopp-cmake/archive/refs/heads/master.zip' }
)

Write-Host "destination: $Dest" -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path $Dest | Out-Null

$failed = @()

foreach ($d in $deps) {
    $target = Join-Path $Dest $d.Name

    if ((Test-Path $target) -and -not $Force) {
        Write-Host "  skip    $($d.Name)  (already present)" -ForegroundColor DarkGray
        continue
    }

    $zip = Join-Path ([System.IO.Path]::GetTempPath()) "$($d.Name).zip"
    try {
        Write-Host "  get     $($d.Name)" -ForegroundColor Yellow
        Invoke-WebRequest -Uri $d.Url -OutFile $zip -UseBasicParsing

        if ($Force -and (Test-Path $target)) { Remove-Item $target -Recurse -Force }
        Expand-Archive -Path $zip -DestinationPath $Dest -Force

        # The archive's root folder name is the contract with CMakeLists.txt.
        # Fail loudly rather than letting CMake report a missing directory later.
        if (-not (Test-Path $target)) {
            $found = Get-ChildItem $Dest -Directory |
                     Where-Object { $_.Name -like "$($d.Name.Split('-')[0])*" } |
                     Select-Object -ExpandProperty Name
            throw "extracted folder is not '$($d.Name)'. Candidates: $($found -join ', ')"
        }
        Write-Host "  ok      $($d.Name)" -ForegroundColor Green
    }
    catch {
        $failed += $d.Name
        Write-Host "  FAIL    $($d.Name): $($_.Exception.Message)" -ForegroundColor Red
    }
    finally {
        if (Test-Path $zip) { Remove-Item $zip -Force -ErrorAction SilentlyContinue }
    }
}

Write-Host ""
if ($failed.Count -gt 0) {
    Write-Host "failed: $($failed -join ', ')" -ForegroundColor Red
    Write-Host "Configure will still work: CMakeLists.txt falls back to downloading" -ForegroundColor DarkGray
    Write-Host "these from GitHub with CPM." -ForegroundColor DarkGray
    exit 1
}

$forward = $Dest -replace '\\', '/'
Write-Host "all dependencies present under $Dest" -ForegroundColor Green
Write-Host ""
Write-Host "Point CMake at them with:" -ForegroundColor Cyan
Write-Host "  cmake -S . -B out/build/x64-Release -G Ninja -DSOLSTICE_DEPS_DIR=$forward" -ForegroundColor White
Write-Host ""
Write-Host "note: cryptopp-cmake downloads the Crypto++ sources during configure," -ForegroundColor DarkGray
Write-Host "      so the first cmake run still needs network access." -ForegroundColor DarkGray
