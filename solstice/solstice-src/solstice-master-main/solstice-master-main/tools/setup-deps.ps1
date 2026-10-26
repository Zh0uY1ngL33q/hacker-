<#
.SYNOPSIS
    Pre-stages the external dependencies used by CMakeLists.txt.

.DESCRIPTION
    CMakeLists.txt can fetch everything itself over HTTP, no git required. This
    script exists for the offline case: run it on a machine with internet access,
    copy the resulting folder over, then configure with -DSOLSTICE_DEPS_DIR.

    Upstream these dependencies were referenced only through SOURCE_DIR
    "D:/dev/deps/<name>" -- one developer's private directory, which also made
    the project unbuildable anywhere else.

    Folder names matter: CMakeLists.txt refers to them literally.

        spdlog-1.9.2              gabime/spdlog            tag v1.9.2
        magic_enum-0.9.5          Neargye/magic_enum       tag v0.9.5
        glm-1.0.1                 g-truc/glm               tag 1.0.1
        json-3.11.3               nlohmann/json            tag v3.11.3
        DirectXTK-main            microsoft/DirectXTK      branch main
        cryptopp-cmake-master     ASSEMBLED, see below

    GitHub strips the leading "v" from tags, so tag v1.9.2 extracts to
    "spdlog-1.9.2" -- the names above are the real extraction roots.

    cryptopp-cmake-master is the odd one out. noloader/cryptopp-cmake is not a
    library; its master branch is 7 files and 15 KB with no .cpp at all. It is a
    CMakeLists.txt designed to be dropped into a Crypto++ source checkout. This
    script therefore builds the folder as Crypto++ 8.7.0 sources plus the
    wrapper's CMakeLists.txt and cryptopp-config.cmake, which is the only layout
    that can actually compile.

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

# github.com/<owner>/<repo>/archive/... redirects; zips are served directly by
# codeload, which also mirrors what CMakeLists.txt uses.
$simpleDeps = @(
    @{ Name = 'spdlog-1.9.2';   Url = 'https://codeload.github.com/gabime/spdlog/zip/refs/tags/v1.9.2' }
    @{ Name = 'magic_enum-0.9.5'; Url = 'https://codeload.github.com/Neargye/magic_enum/zip/refs/tags/v0.9.5' }
    @{ Name = 'glm-1.0.1';      Url = 'https://codeload.github.com/g-truc/glm/zip/refs/tags/1.0.1' }
    @{ Name = 'json-3.11.3';    Url = 'https://codeload.github.com/nlohmann/json/zip/refs/tags/v3.11.3' }
    @{ Name = 'DirectXTK-main'; Url = 'https://codeload.github.com/microsoft/DirectXTK/zip/refs/heads/main' }
)

$cryptoFolder  = 'cryptopp-cmake-master'
$cryptoSource  = 'https://codeload.github.com/weidai11/cryptopp/zip/refs/tags/CRYPTOPP_8_7_0'
$cryptoWrapper = 'https://codeload.github.com/noloader/cryptopp-cmake/zip/refs/heads/master'

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("solstice-deps-" + [guid]::NewGuid())
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null

$failed = @()

function Get-Archive {
    param([string] $Url, [string] $Name, [string] $Into)
    $zip = Join-Path $tempRoot "$Name.zip"
    Invoke-WebRequest -Uri $Url -OutFile $zip -UseBasicParsing
    Expand-Archive -Path $zip -DestinationPath $Into -Force
    Remove-Item $zip -Force
}

function Get-SingleRoot {
    param([string] $Dir)
    $roots = @(Get-ChildItem $Dir -Directory)
    if ($roots.Count -ne 1) {
        throw "expected exactly one top-level directory in $Dir, found $($roots.Count)"
    }
    return $roots[0].FullName
}

Write-Host "destination: $Dest" -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path $Dest | Out-Null

try {
    foreach ($d in $simpleDeps) {
        $target = Join-Path $Dest $d.Name

        if ((Test-Path $target) -and -not $Force) {
            Write-Host "  skip    $($d.Name)  (already present)" -ForegroundColor DarkGray
            continue
        }

        try {
            Write-Host "  get     $($d.Name)" -ForegroundColor Yellow
            $stage = Join-Path $tempRoot $d.Name
            New-Item -ItemType Directory -Force -Path $stage | Out-Null
            Get-Archive -Url $d.Url -Name $d.Name -Into $stage

            $root = Get-SingleRoot $stage
            if ((Split-Path $root -Leaf) -ne $d.Name) {
                throw "extracted folder is '$(Split-Path $root -Leaf)', expected '$($d.Name)'"
            }

            if ($Force -and (Test-Path $target)) { Remove-Item $target -Recurse -Force }
            Move-Item $root $target
            Write-Host "  ok      $($d.Name)" -ForegroundColor Green
        }
        catch {
            $failed += $d.Name
            Write-Host "  FAIL    $($d.Name): $($_.Exception.Message)" -ForegroundColor Red
        }
    }

    # ---------------------------------------------------------------- cryptopp
    $cryptoTarget = Join-Path $Dest $cryptoFolder
    if ((Test-Path (Join-Path $cryptoTarget 'cryptlib.cpp')) -and -not $Force) {
        Write-Host "  skip    $cryptoFolder  (already assembled)" -ForegroundColor DarkGray
    }
    else {
        try {
            Write-Host "  get     $cryptoFolder  (Crypto++ 8.7.0 sources)" -ForegroundColor Yellow
            $srcStage = Join-Path $tempRoot 'cryptopp-src'
            New-Item -ItemType Directory -Force -Path $srcStage | Out-Null
            Get-Archive -Url $cryptoSource -Name 'cryptopp' -Into $srcStage
            $srcRoot = Get-SingleRoot $srcStage

            Write-Host "  get     $cryptoFolder  (CMake wrapper)" -ForegroundColor Yellow
            $wrapStage = Join-Path $tempRoot 'cryptopp-wrapper'
            New-Item -ItemType Directory -Force -Path $wrapStage | Out-Null
            Get-Archive -Url $cryptoWrapper -Name 'cryptopp-wrapper' -Into $wrapStage
            $wrapRoot = Get-SingleRoot $wrapStage

            if (-not (Test-Path (Join-Path $wrapRoot 'CMakeLists.txt'))) {
                throw "the wrapper archive has no CMakeLists.txt"
            }

            if ($Force -and (Test-Path $cryptoTarget)) { Remove-Item $cryptoTarget -Recurse -Force }
            if (Test-Path $cryptoTarget) { Remove-Item $cryptoTarget -Recurse -Force }
            Move-Item $srcRoot $cryptoTarget

            # Crypto++ ships a GNUmakefile and no root CMakeLists.txt, so this
            # cannot overwrite anything it needs.
            Copy-Item (Join-Path $wrapRoot 'CMakeLists.txt')        $cryptoTarget -Force
            Copy-Item (Join-Path $wrapRoot 'cryptopp-config.cmake') $cryptoTarget -Force

            if (-not (Test-Path (Join-Path $cryptoTarget 'cryptlib.cpp'))) {
                throw "assembled folder does not contain cryptlib.cpp"
            }
            Write-Host "  ok      $cryptoFolder  (sources + wrapper)" -ForegroundColor Green
        }
        catch {
            $failed += $cryptoFolder
            Write-Host "  FAIL    ${cryptoFolder}: $($_.Exception.Message)" -ForegroundColor Red
        }
    }
}
finally {
    Remove-Item $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ""
if ($failed.Count -gt 0) {
    Write-Host "failed: $($failed -join ', ')" -ForegroundColor Red
    Write-Host "Configure still works without this folder: CMakeLists.txt downloads" -ForegroundColor DarkGray
    Write-Host "the same archives over HTTP itself (no git required)." -ForegroundColor DarkGray
    exit 1
}

Write-Host "all dependencies present under $Dest" -ForegroundColor Green
Write-Host ""
Write-Host "Point CMake at them with:" -ForegroundColor Cyan
Write-Host ("  cmake -S . -B out/build/x64-Release -G Ninja -DSOLSTICE_DEPS_DIR=" + ($Dest -replace '\\', '/')) -ForegroundColor White
