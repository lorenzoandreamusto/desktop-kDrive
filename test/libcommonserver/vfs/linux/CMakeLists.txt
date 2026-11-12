# Standalone Linux VFS test harness.
#
# This is deliberately a separate CMake entry point: it builds the real NodeMap
# and PlaceholderMetadata implementations against minimal type/IoHelper stubs,
# without Qt, Poco, log4cplus or libfuse3. The CppUnit target still compiles
# testlinuxvfs_checks.cpp through test/libcommonserver/CMakeLists.txt.

cmake_minimum_required(VERSION 3.20)

project(kDriveLinuxVfsStandalone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")
set(src_dir "${repo_root}/src")
set(stubs_dir "${CMAKE_CURRENT_SOURCE_DIR}/standalone_stubs")

add_executable(kDrive_test_linuxvfs_standalone
    testlinuxvfs_standalone.cpp
    testlinuxvfs_checks.cpp
    "${src_dir}/server/vfs/linux/nodemap.cpp"
    "${src_dir}/server/vfs/linux/placeholdermetadata.cpp"
)

target_include_directories(kDrive_test_linuxvfs_standalone PRIVATE
    "${stubs_dir}"
    "${src_dir}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_options(kDrive_test_linuxvfs_standalone PRIVATE
    -Wall
    -Wextra
)

enable_testing()

add_test(NAME linuxvfs_standalone
    COMMAND kDrive_test_linuxvfs_standalone
)
