cmake_minimum_required(VERSION 3.21)
project(appimagetool)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

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
if("$ENV{GITHUB_RUN_NUMBER}" STREQUAL "")
    set(BUILD_NUMBER "<local dev build>")
else()
    set(BUILD_NUMBER "$ENV{GITHUB_RUN_NUMBER}")
endif()
mark_as_advanced(BUILD_NUMBER)

# get current date
execute_process(
    COMMAND env LC_ALL=C date -u "+%Y-%m-%d %H:%M:%S %Z"
    OUTPUT_VARIABLE DATE
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# for distribution purposes, we need fully static builds
# all of our dependencies are available as static packages on Alpine Linux, which is great!
# you can use ci/build-in-docker.sh to build release-able AppImages of this tool
# by default, static builds are off allow for working on this tool on any distribution
option(BUILD_STATIC OFF)

if(BUILD_STATIC)
    # since this project does not expose any libraries, we can safely set the linker flag globally
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -no-pie")

    # test if compiling a simple binary works with these flags
    unset(STATIC_BUILDS_WORKING CACHE)
    include(CheckCSourceCompiles)
    check_c_source_compiles("
        int main() { return 0; }
    " STATIC_BUILDS_WORKING)

    if(NOT STATIC_BUILDS_WORKING)
        message(FATAL_ERROR "static builds do not work on this system")
    endif()
endif()

# required for appimagetool's signing
# we're using CMake's pkg-config integration directly, since we want to link those statically
find_package(PkgConfig REQUIRED)

# note: the following code is a workaround (well, hack) around CMake's limited pkg-config IMPORTED_TARGET support
# out of the box, CMake does not provide PkgConfig::<target> targets for static build variants of the libraries we use
# we can either use this workaround, which will
# see https://gitlab.kitware.com/cmake/cmake/-/issues/21714 for the corresponding feature request and
# https://gitlab.kitware.com/cmake/cmake/-/issues/21714#note_1074454 for more information on the workaround
if(BUILD_STATIC)
    # again, since this project does not expose any libraries, we can safely set this option globally
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    list(APPEND PKG_CONFIG_EXECUTABLE "--static")
endif()

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
pkg_check_modules(libcurl REQUIRED libcurl IMPORTED_TARGET)

# Alpine Linux does not ship an argp.h as part of the standard compiler toolchain
# Non-Linux OSes like FreeBSD do not have this header too
find_file(ARGP_H argp.h HINTS /usr/include /usr/local/include)

if(NOT ARGP_H)
    message(FATAL_ERROR "argp.h not installed, if on Alpine Linux please apk add argp-standalone")
endif()

get_filename_component(ARGP_INCLUDE_DIR "${ARGP_H}" DIRECTORY)

add_subdirectory(src)
