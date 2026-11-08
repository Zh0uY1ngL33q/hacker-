# SPDX-License-Identifier: MIT
#
# SPDX-FileCopyrightText: Copyright (c) 2019-2023 Lars Melchior and contributors

set(CPM_DOWNLOAD_VERSION 0.38.7)
set(CPM_HASH_SUM "83e5eb71b2bbb8b1f2ad38f1950287a057624e385c238f6087f94cdfc44af9c5")

# Solstice full-integration build bootstrap fix (Batch "Solstice Full Integration").
#
# The original bootstrap downloaded CPM.cmake from GitHub at configure time and
# then `include()`d it. When that download fails (offline build machine, proxy,
# TLS interception, blocked github.com) `file(DOWNLOAD)` writes an EMPTY file and
# returns success-ish, so `include()` silently defines nothing. CMakeLists.txt:63
# then aborts with the misleading "Unknown CMake command \"CPMAddPackage\"" --
# the real fault is the missing CPM bootstrap, not a runtime/SDK problem.
#
# Fix: ship the pinned upstream v0.38.7 CPM.cmake IN the repository and include
# it directly. The vendored file is byte-identical to the release the downloader
# asked for (same CPM_DOWNLOAD_VERSION + CPM_HASH_SUM pin), so NOTHING about the
# dependency versions, CPM version or CPMAddPackage call order changes. The
# network download is kept only as a fallback for trees that lack the vendored
# file. A post-include command check turns any future corruption into an
# explicit, actionable configure error instead of an opaque "Unknown command".
#
# NOTE: the vendored copy is CMake code, not C++ runtime code. It is never
# compiled into Solstice.dll and cannot affect game behaviour.

set(CPM_VENDORED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/CPM_${CPM_DOWNLOAD_VERSION}.cmake")

if (EXISTS "${CPM_VENDORED_LOCATION}")
    # Verify the pin at configure time: a stale/edited vendored copy must not be
    # silently trusted.
    file(SHA256 "${CPM_VENDORED_LOCATION}" _cpm_vendored_hash)
    if (NOT _cpm_vendored_hash STREQUAL CPM_HASH_SUM)
        message(FATAL_ERROR
                "Vendored CPM.cmake hash mismatch.\n"
                "  expected ${CPM_HASH_SUM}\n"
                "  got      ${_cpm_vendored_hash}\n"
                "  file     ${CPM_VENDORED_LOCATION}\n"
                "Restore the exact upstream v${CPM_DOWNLOAD_VERSION} release file.")
    endif ()
    set(CPM_EFFECTIVE_LOCATION "${CPM_VENDORED_LOCATION}")
    message(STATUS "CPM v${CPM_DOWNLOAD_VERSION}: using verified vendored copy (offline-safe)")
else ()
    # Fallback for a tree without the vendored copy: original downloader logic,
    # with an explicit STATUS check so a failed download is reported here.
    if (CPM_SOURCE_CACHE)
        set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
    elseif (DEFINED ENV{CPM_SOURCE_CACHE})
        set(CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
    else ()
        set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
    endif ()

    # Expand relative path. This is important if the provided path contains a tilde (~)
    get_filename_component(CPM_DOWNLOAD_LOCATION ${CPM_DOWNLOAD_LOCATION} ABSOLUTE)

    if (NOT EXISTS ${CPM_DOWNLOAD_LOCATION})
        file(DOWNLOAD
             https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
             ${CPM_DOWNLOAD_LOCATION} EXPECTED_HASH SHA256=${CPM_HASH_SUM}
             STATUS _cpm_download_status TLS_VERIFY ON)
        list(GET _cpm_download_status 0 _cpm_download_code)
        if (NOT _cpm_download_code EQUAL 0)
            file(REMOVE "${CPM_DOWNLOAD_LOCATION}")
            message(FATAL_ERROR
                    "CPM v${CPM_DOWNLOAD_VERSION} bootstrap download failed and no vendored copy is present.\n"
                    "  status: ${_cpm_download_status}\n"
                    "  Add cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake from the upstream release, or set CPM_SOURCE_CACHE.")
        endif ()
    endif ()
    set(CPM_EFFECTIVE_LOCATION "${CPM_DOWNLOAD_LOCATION}")
    message(STATUS "CPM v${CPM_DOWNLOAD_VERSION}: using downloaded copy ${CPM_DOWNLOAD_LOCATION}")
endif ()

include("${CPM_EFFECTIVE_LOCATION}")

# Guard: if the included file did not define CPMAddPackage (empty/truncated
# download, wrong file), fail HERE with a clear message rather than letting the
# first CPMAddPackage call in CMakeLists.txt report "Unknown CMake command".
if (NOT COMMAND CPMAddPackage)
    message(FATAL_ERROR
            "CPM v${CPM_DOWNLOAD_VERSION} was included from ${CPM_EFFECTIVE_LOCATION} but CPMAddPackage is not defined.\n"
            "The file is empty, truncated, or not a CPM release. Verify its SHA256 against ${CPM_HASH_SUM}.")
endif ()
