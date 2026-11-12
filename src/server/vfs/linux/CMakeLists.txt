find_package(Qt6 REQUIRED Core)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(FUSE3 REQUIRED IMPORTED_TARGET fuse3)

# Target
# The plugin name must match pluginFileName("vfs", "linux") in libcommonserver/vfs/plugin.cpp.
add_library("${APPLICATION_EXECUTABLE}_vfs_linux" SHARED
    vfs_linux.h vfs_linux.cpp
    fusesession.h fusesession.cpp
    nodemap.h nodemap.cpp
    placeholdermetadata.h placeholdermetadata.cpp
)

set_target_properties("${APPLICATION_EXECUTABLE}_vfs_linux" PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${BIN_OUTPUT_DIRECTORY}
    RUNTIME_OUTPUT_DIRECTORY ${BIN_OUTPUT_DIRECTORY}
    PREFIX ""
    AUTOMOC TRUE
)

# Link
target_link_libraries("${APPLICATION_EXECUTABLE}_vfs_linux"
    ${libcommonserver_NAME}
    Qt6::Core
    PkgConfig::FUSE3
    Threads::Threads
)

# Install
# Keep this path aligned with PLUGINDIR defined in the root CMakeLists.txt.
set(vfs_installdir "${CMAKE_INSTALL_FULL_LIBDIR}/${APPLICATION_SHORTNAME}/plugins")
install(TARGETS "${APPLICATION_EXECUTABLE}_vfs_linux"
    LIBRARY DESTINATION "${vfs_installdir}"
    RUNTIME DESTINATION "${vfs_installdir}"
)
