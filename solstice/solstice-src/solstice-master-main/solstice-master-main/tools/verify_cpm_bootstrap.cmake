# Offline verification of the Solstice CPM bootstrap fix.
# Run from the repository root:  cmake -P tools/verify_cpm_bootstrap.cmake
# It does NOT need a compiler, a generator, or network access: it only proves
# that including cmake/CPM.cmake defines CPMAddPackage, which is the exact
# command that CMakeLists.txt:63 failed to find ("Unknown CMake command").
get_filename_component(_repo_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${_repo_root}/cmake/CPM.cmake")
if (COMMAND CPMAddPackage)
    message(STATUS "VERIFY_PASS: CPMAddPackage is defined (CPM v${CPM_DOWNLOAD_VERSION})")
else ()
    message(FATAL_ERROR "VERIFY_FAIL: CPMAddPackage missing after bootstrap include")
endif ()
