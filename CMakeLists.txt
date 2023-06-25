cmake_minimum_required(VERSION 3.21)
project(appimagetool)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# determine Git version based on distance to tag
# FIXME: right now we have no tags, so we just use the current Git commit's hash
execute_process(
    #COMMAND git describe --tags HEAD
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# set version and build number
set(VERSION 1-alpha)
mark_as_advanced(VERSION)
if("$ENV{TRAVIS_BUILD_NUMBER}" STREQUAL "")
    set(BUILD_NUMBER "<local dev build>")
else()
    set(BUILD_NUMBER "$ENV{TRAVIS_BUILD_NUMBER}")
endif()
mark_as_advanced(BUILD_NUMBER)

# get current date
execute_process(
    COMMAND env LC_ALL=C date -u "+%Y-%m-%d %H:%M:%S %Z"
    OUTPUT_VARIABLE DATE
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# required for appimagetool's signing
# we're using CMake's pkg-config integration directly, since we want to link those statically
find_package(PkgConfig REQUIRED)

# note: the names of the targets we create with pkg_check_modules should differ from what you pass to `-l`
# this allows us to catch problems in our own CMake setup
# otherwise, CMake may just pass `-l<name>` if a corresponding CMake target is not found
# this is especially an issue with libcairo, where the library is called libcairo
# therefore, all libs imported this way have been prefixed with lib
pkg_check_modules(libgpgme REQUIRED gpgme IMPORTED_TARGET)
pkg_check_modules(libgcrypt REQUIRED libgcrypt IMPORTED_TARGET)
# TODO: in the long term, we plan to get rid of GLib/GIO by moving to C++
pkg_check_modules(libglib REQUIRED glib-2.0 IMPORTED_TARGET)
pkg_check_modules(libgio REQUIRED gio-2.0 IMPORTED_TARGET)

# Alpine Linux does not ship an argp.h as part of the standard compiler toolchain
unset(ARGP_H_FOUND CACHE)
include(CheckCSourceCompiles)
check_c_source_compiles("
    #include <argp.h>
    int main() { return 0; }
" ARGP_H_FOUND)

if(NOT ARGP_H_FOUND)
    message(FATAL_ERROR "argp.h not installed, if on Alpine Linux please apk add argp-standalone")
endif()

add_subdirectory(src)
