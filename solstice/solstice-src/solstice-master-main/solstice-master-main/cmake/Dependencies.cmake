# External dependency resolution.
#
# Included from CMakeLists.txt, and also runnable on its own in script mode
# (`cmake -P cmake/Dependencies.cmake`) so the resolution logic can be tested
# without a compiler, a toolchain, or the rest of the project.
#
# ---------------------------------------------------------------------------
# Why this file exists at all
# ---------------------------------------------------------------------------
# Upstream, CMakeLists.txt had six bare CPMAddPackage calls whose only argument
# was SOURCE_DIR "D:/dev/deps/<name>" -- one developer's private working
# directory. CPM's SOURCE_DIR is *exclusive*: setting it disables downloading
# altogether, and a missing directory is a hard error rather than a fallback. So
# a fresh clone could not configure, on any machine but that one.
#
# ---------------------------------------------------------------------------
# Why the downloads do not go through CPM
# ---------------------------------------------------------------------------
# CPM's normal path is `git clone` (GIT_REPOSITORY), and the build machine may
# not have git installed at all. The archives are therefore fetched with
# file(DOWNLOAD), which only needs the HTTPS support built into CMake itself,
# and CPM is then handed a plain SOURCE_DIR.
#
# This also pins what the folder names are. GitHub archive URLs extract to
# "<repo>-<tag>" with the leading "v" stripped, so tag v1.9.2 yields
# "spdlog-1.9.2" -- the exact folder names CMakeLists.txt refers to.
#
# ---------------------------------------------------------------------------
# Resolution order, per dependency
# ---------------------------------------------------------------------------
#   1. <SOLSTICE_DEPS_DIR>/<local-folder>    pre-staged, no network
#   2. <SOLSTICE_DEPS_CACHE>/<local-folder>  downloaded once, then reused
#
# tools/setup-deps.ps1 produces layout (1).

set(SOLSTICE_DEPS_DIR "" CACHE PATH
        "Optional directory holding pre-staged checkouts of the external dependencies")

set(SOLSTICE_DEPS_CACHE "${CMAKE_BINARY_DIR}/_deps-local" CACHE PATH
        "Where dependency archives are downloaded and extracted")

# Convenience: machines that already have the original layout keep working
# without passing a flag.
if (NOT SOLSTICE_DEPS_DIR AND EXISTS "D:/dev/deps")
    set(SOLSTICE_DEPS_DIR "D:/dev/deps")
    message(STATUS "dependencies: using the existing directory D:/dev/deps")
endif ()

# solstice_download(<out-var> <name> <folder> <url>)
#
# Ensures <SOLSTICE_DEPS_CACHE>/<folder> exists, downloading and extracting the
# archive if it does not, and sets <out-var> to it.
function(solstice_download out_var name folder url)
    set(dir "${SOLSTICE_DEPS_CACHE}/${folder}")

    if (NOT EXISTS "${dir}")
        message(STATUS "dependency ${name}: downloading ${url}")
        file(MAKE_DIRECTORY "${SOLSTICE_DEPS_CACHE}")
        set(archive "${SOLSTICE_DEPS_CACHE}/${folder}.zip")

        file(DOWNLOAD "${url}" "${archive}"
                STATUS download_status
                TLS_VERIFY ON)

        list(GET download_status 0 download_code)
        list(GET download_status 1 download_message)
        if (NOT download_code EQUAL 0)
            file(REMOVE "${archive}")
            message(FATAL_ERROR
                    "Could not download ${name}.\n"
                    "  url:    ${url}\n"
                    "  reason: ${download_message}\n"
                    "If this machine has no internet access, pre-stage the dependencies\n"
                    "elsewhere with tools/setup-deps.ps1 and pass\n"
                    "  -DSOLSTICE_DEPS_DIR=<directory>")
        endif ()

        message(STATUS "dependency ${name}: extracting into ${SOLSTICE_DEPS_CACHE}")
        file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${SOLSTICE_DEPS_CACHE}")
        file(REMOVE "${archive}")

        if (NOT EXISTS "${dir}")
            file(GLOB extracted LIST_DIRECTORIES true "${SOLSTICE_DEPS_CACHE}/*")
            message(FATAL_ERROR
                    "Extracted ${name} but '${dir}' is missing.\n"
                    "The archive's root directory did not match '${folder}'.\n"
                    "Contents of ${SOLSTICE_DEPS_CACHE}: ${extracted}")
        endif ()
    endif ()

    set(${out_var} "${dir}" PARENT_SCOPE)
endfunction()

# solstice_resolve_dependency(<out-var> <name> <local-folder> <url>)
#
# Resolves a dependency to a directory containing a CMakeLists.txt, acquiring it
# if necessary. This only sets a variable; the CPMAddPackage call stays in
# CMakeLists.txt so that package registration happens at directory scope.
function(solstice_resolve_dependency out_var name folder url)
    if (SOLSTICE_DEPS_DIR AND EXISTS "${SOLSTICE_DEPS_DIR}/${folder}/CMakeLists.txt")
        message(STATUS "dependency ${name}: pre-staged at ${SOLSTICE_DEPS_DIR}/${folder}")
        set(${out_var} "${SOLSTICE_DEPS_DIR}/${folder}" PARENT_SCOPE)
        return()
    endif ()

    if (EXISTS "${SOLSTICE_DEPS_CACHE}/${folder}/CMakeLists.txt")
        message(STATUS "dependency ${name}: cached at ${SOLSTICE_DEPS_CACHE}/${folder}")
        set(${out_var} "${SOLSTICE_DEPS_CACHE}/${folder}" PARENT_SCOPE)
        return()
    endif ()

    solstice_download(dir "${name}" "${folder}" "${url}")

    # Fail here, with an explanation, rather than letting CMake report a missing
    # CMakeLists.txt somewhere less obvious. The folder name is the contract.
    if (NOT EXISTS "${dir}/CMakeLists.txt")
        message(FATAL_ERROR
                "Extracted ${name} into '${dir}' but it has no CMakeLists.txt.\n"
                "The archive's root directory probably did not match '${folder}'.")
    endif ()

    set(${out_var} "${dir}" PARENT_SCOPE)
endfunction()

solstice_resolve_dependency(SPDLOG_DIR spdlog spdlog-1.9.2
        "https://codeload.github.com/gabime/spdlog/zip/refs/tags/v1.9.2")
solstice_resolve_dependency(MAGIC_ENUM_DIR magic_enum magic_enum-0.9.5
        "https://codeload.github.com/Neargye/magic_enum/zip/refs/tags/v0.9.5")
solstice_resolve_dependency(GLM_DIR glm glm-1.0.1
        "https://codeload.github.com/g-truc/glm/zip/refs/tags/1.0.1")
solstice_resolve_dependency(NLOHMANN_JSON_DIR nlohmann_json json-3.11.3
        "https://codeload.github.com/nlohmann/json/zip/refs/tags/v3.11.3")
solstice_resolve_dependency(DIRECTXTK_DIR DirectXTK DirectXTK-main
        "https://codeload.github.com/microsoft/DirectXTK/zip/refs/heads/main")

# ---------------------------------------------------------------------------
# Crypto++
# ---------------------------------------------------------------------------
# The folder name is misleading: "cryptopp-cmake-master" is not
# noloader/cryptopp-cmake unpacked. That repository's master branch is 7 files,
# 15 KB, with no .cpp at all -- it is a CMakeLists.txt meant to be dropped *into*
# a Crypto++ source checkout. Its build globs ${CRYPTOPP_PROJECT_DIR}/*.cpp and
# explicitly names cryptlib.cpp / cpu.cpp / integer.cpp, so on its own it fails
# at configure time with "Cannot find source file".
#
# The folder therefore has to be assembled from two archives: Crypto++ itself,
# plus the wrapper's CMakeLists.txt and cryptopp-config.cmake. A pre-staged copy
# is assumed to be already assembled (checked via cryptlib.cpp).
function(solstice_resolve_cryptopp out_var)
    if (SOLSTICE_DEPS_DIR AND EXISTS "${SOLSTICE_DEPS_DIR}/cryptopp-cmake-master/cryptlib.cpp")
        message(STATUS "dependency cryptopp: pre-staged at ${SOLSTICE_DEPS_DIR}/cryptopp-cmake-master")
        set(${out_var} "${SOLSTICE_DEPS_DIR}/cryptopp-cmake-master" PARENT_SCOPE)
        return()
    endif ()

    solstice_download(sources_dir cryptopp cryptopp-CRYPTOPP_8_7_0
            "https://codeload.github.com/weidai11/cryptopp/zip/refs/tags/CRYPTOPP_8_7_0")
    solstice_download(wrapper_dir cryptopp-cmake-wrapper cryptopp-cmake-master
            "https://codeload.github.com/noloader/cryptopp-cmake/zip/refs/heads/master")

    # Crypto++ 8.7.0 ships a GNUmakefile and no CMakeLists.txt at its root, so
    # installing the wrapper cannot overwrite anything it needs.
    if (NOT EXISTS "${sources_dir}/CMakeLists.txt")
        message(STATUS "dependency cryptopp: installing the CMake wrapper into ${sources_dir}")
        file(COPY "${wrapper_dir}/CMakeLists.txt" "${wrapper_dir}/cryptopp-config.cmake"
                DESTINATION "${sources_dir}")
    endif ()

    if (NOT EXISTS "${sources_dir}/CMakeLists.txt")
        message(FATAL_ERROR
                "Could not assemble Crypto++: '${sources_dir}/CMakeLists.txt' is missing.")
    endif ()

    set(${out_var} "${sources_dir}" PARENT_SCOPE)
endfunction()

solstice_resolve_cryptopp(CRYPTOPP_DIR)
