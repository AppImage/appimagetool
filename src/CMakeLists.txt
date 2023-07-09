add_executable(appimagetool
    appimagetool.cpp
    appimagetool_fetch_runtime.cpp
    appimagetool_fetch_runtime.h
    appimagetool_sign.c
    appimagetool_sign.h
    digest.c
    elf.c
    hexlify.c
    light_byteswap.h
    light_elf.h
    md5.c
    md5.h
    util.h
)

# Ensure that the C compiler, not the C++ compiler, is used for compiling C files
set_source_files_properties(digest.c elf.c hexlify.c md5.c appimagetool_sign.c PROPERTIES LANGUAGE C)
set(CMAKE_C_COMPILER gcc)

# trick: list libraries on which imported static ones depend on in the PUBLIC section
# CMake then adds them after the PRIVATE ones in the linker command
target_link_libraries(appimagetool
    ${CMAKE_DL_LIBS}
    PkgConfig::libglib
    PkgConfig::libgio
    PkgConfig::libgcrypt
    PkgConfig::libgpgme
    PkgConfig::libcurl
)


target_compile_definitions(appimagetool
    PRIVATE -D_FILE_OFFSET_BITS=64
    PRIVATE -DGIT_VERSION="${GIT_VERSION}"
    PRIVATE -DBUILD_NUMBER="${BUILD_NUMBER}"
    PRIVATE -DBUILD_DATE="${DATE}"
)

target_include_directories(appimagetool
    PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/>
    INTERFACE $<INSTALL_INTERFACE:include/>
)

install(
    TARGETS appimagetool
    RUNTIME DESTINATION bin
)

# install additional resources
install(
    FILES "${PROJECT_SOURCE_DIR}/resources/appimagetool.desktop"
    DESTINATION share/applications
)
install(
    FILES "${PROJECT_SOURCE_DIR}/resources/appimagetool.png"
    DESTINATION share/icons/hicolor/128x128/apps
)
install(
    FILES "${PROJECT_SOURCE_DIR}/resources/appimagetool.appdata.xml"
    DESTINATION share/metainfo
)

