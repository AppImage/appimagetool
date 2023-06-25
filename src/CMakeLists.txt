add_executable(appimagetool
    appimagetool.c
    appimagetool_sign.c
    binreloc.c
    hexlify.c
    elf.c
    digest.c
    md5.c
)

# trick: list libraries on which imported static ones depend on in the PUBLIC section
# CMake then adds them after the PRIVATE ones in the linker command
target_link_libraries(appimagetool
    ${CMAKE_DL_LIBS}
    PkgConfig::libglib
    PkgConfig::libgio
    PkgConfig::libgcrypt
    PkgConfig::libgpgme
)

target_compile_definitions(appimagetool
    PRIVATE -D_FILE_OFFSET_BITS=64
    PRIVATE -DGIT_VERSION="${GIT_VERSION}"
    PRIVATE -DBUILD_NUMBER="${BUILD_NUMBER}"
    PRIVATE -DBUILD_DATE="${DATE}"
    PRIVATE -DENABLE_BINRELOC
)

target_include_directories(appimagetool
    PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/>
    INTERFACE $<INSTALL_INTERFACE:include/>
)

install(
    TARGETS appimagetool
    RUNTIME DESTINATION bin
    COMPONENT appimagetool
)

# install additional resources
install(
    FILES "${PROJECT_SOURCE_DIR}/resources/appimagetool.desktop"
    DESTINATION share/applications
    COMPONENT appimagetool
    EXCLUDE_FROM_ALL
)
install(
    FILES "${PROJECT_SOURCE_DIR}/resources/appimagetool.png"
    DESTINATION share/icons/hicolor/128x128/apps
    COMPONENT appimagetool
    EXCLUDE_FROM_ALL
)
install(
    FILES "${PROJECT_SOURCE_DIR}/resources/usr/share/metainfo/appimagetool.appdata.xml"
    DESTINATION share/metainfo
    COMPONENT appimagetool
    EXCLUDE_FROM_ALL
)
