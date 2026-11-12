- [kDrive files](#kdrive-files)
- [Installation Requirements](#installation-requirements)
    - [Packages](#packages)
    - [CPPUnit](#cppunit)
    - [libzip](#libzip)
    - [Conan](#conan)
- [Build in Debug](#build-in-debug)
    - [Using CLion](#using-clion)
        - [Prerequisites](#prerequisites)
        - [CMake Parameters](#cmake-parameters)
    - [Using Qt Creator](#using-qt-creator)
        - [Configuration](#configuration)
        - [Debugging](#debugging)
- [Build in Release](#build-in-release)
    - [Podman prerequisites](#podman-prerequisites)
    - [Podman image](#podman-image)
    - [Building](#building)
        - [Qt account](#qt-account)

# kDrive files

The directory `~/Projects` will be used for the installation in this documentation.  
If you wish to have the sources elsewhere, feel free to use the path you want.

```bash
cd ~/Projects
git clone https://github.com/Infomaniak/desktop-kDrive.git
cd desktop-kDrive && git submodule update --init --recursive
```

# Installation Requirements

These requirements will only apply if you want to build from a Linux device  
Currently, the build for Linux release is created from a podman container

You will need cmake and clang to compile the libraries and the kDrive project  
This documentation was made for Ubuntu 22.04 LTS

We are migrating the dependency management from manual to using conan.

## Packages

If not already installed, you will need **git**, **cmake** and **clang** (clang-18 or higher) packages.

```bash
sudo apt update && sudo apt install -y git cmake clang
```

Ensure that CLANG is installed with at least version 18 by running the following command :

```bash
clang --version
```

If the version is lower than 18, you can install the latest version with the following commands :

```bash
wget https://apt.llvm.org/llvm.sh
chmod u+x llvm.sh
apt install lsb-release wget software-properties-common gnupg
sudo ./llvm.sh 18
sudo update-alternatives --install /usr/bin/clang clang /usr/lib/llvm-18/bin/clang 100
sudo update-alternatives --install /usr/bin/clang++ clang++ /usr/lib/llvm-18/bin/clang++ 100
```

Check the version again with `clang --version` to ensure that the version is now 18 or higher.


## CPPUnit

```bash
sudo apt-get install -y autotools-dev
sudo apt-get install -y automake
sudo apt-get install -y libtool m4 automake
cd ~/Projects
git clone git://anongit.freedesktop.org/git/libreoffice/cppunit
cd cppunit
./autogen.sh
./configure
make
sudo make install
```

If the server does not reply to the `git clone` command, you can download the source from https://www.freedesktop.org/wiki/Software/cppunit/.

You can also download cppunit version 1.15.1 using the ["Wayback Machine"](https://web.archive.org/) here: https://web.archive.org/web/20231118010938/http://dev-www.libreoffice.org/src/cppunit-1.15.1.tar.gz

## libzip

First, install the `dev` version of zlib:

```bash
sudo apt-get install zlib1g-dev
```

Clone and install libzip

```bash
cd ~/Projects
git clone https://github.com/nih-at/libzip.git
cd libzip
git checkout tags/v1.10.1
mkdir build && cd build
cmake ..
make
sudo make install
```

---

## Conan
The recommended way to install Conan is via **pip** within a Python virtual environment (Python 3.6 or newer). This approach ensures isolation and compatibility with your project’s dependencies.

> **Tip:** Other installation methods (system packages, pipx, installer scripts, etc.) are also supported. See [Conan Downloads](https://conan.io/downloads) for the full list of options.
### Prerequisites
- **Python 3.6+**
- **pip** (upgrade to the latest version):
  ```bash
  pip install --upgrade pip
  ```

### 1. Create and Activate a Virtual Environment
1. Create a virtual environment in `./.venv`:
   ```bash
   python3 -m venv .venv
   ```
2. Activate the virtual environment:
   ```bash
   source .venv/bin/activate
   ```

### 2. Install Conan

With the virtual environment activated, install Conan:
```bash
pip install conan
```

Verify the installation:
```bash
conan --version
```
You should see an output similar to:
```
Conan version 2.x.x
```

---

### 3. Configure a Conan Profile
1. Auto-generate the default profile:
   ```bash
   conan profile detect
   ```
   This creates `~/.conan2/profiles/default`.

2. Open `~/.conan2/profiles/default` and customize the settings under the `[settings]` section. For example, to target Linux with C++20:
   ```ini
    [settings]
    arch=x86_64
    build_type=Release
    compiler=gcc
    compiler.cppstd=20
    compiler.libcxx=libstdc++11
    compiler.version=11.4
    os=Linux
   ```

3. **Linux only**: The Sentry Conan recipe validates that the required system packages are already installed in order to avoid building `c-ares` and `libcurl` from source.

    On Debian/Ubuntu-based systems, the recipe checks for:
    - `libc-ares-dev` (c-ares development library)
    - `libcurl4-openssl-dev` (libcurl with OpenSSL support)

    Install them manually before running `conan install`:
    ```bash
    sudo apt update && sudo apt install -y libc-ares-dev libcurl4-openssl-dev
    ```
    If these packages are missing, the Conan recipe fails with an explicit error and asks you to install them manually.

4. **Linux only**: The keychain Conan recipe validates that the `glib` and `libsecret` development packages are already installed. With the custom recipe, they are no longer provided by Conan, and upstream finds them through `pkg-config` at configure time.

    On Debian/Ubuntu-based systems, the recipe checks for:
    - `libglib2.0-dev` (glib development library)
    - `libsecret-1-dev` (libsecret development library)

    Install them manually before running `conan install`:
    ```bash
    sudo apt update && sudo apt install -y libglib2.0-dev libsecret-1-dev
    ```
    If these packages are missing, the Conan recipe fails with an explicit error and asks you to install them manually.


---

### 4. Configure CMake Toolchain Injection
The project requires additional CMake variables for a correct build. To inject these, create a file named `debug_vars.cmake` in your profiles directory (`~/.conan2/profiles`), and then reference it in the profile under `[conf]`:

1. Create or open `~/.conan2/profiles/debug_vars.cmake` and add the cache entries, for example:
   ```cmake
   set(APPLICATION_CLIENT_EXECUTABLE "kdrive_client")
   set(KDRIVE_THEME_DIR "$ENV{HOME}/Projects/desktop-kDrive/infomaniak")
   set(BUILD_UNIT_TESTS "ON")      # Set to "OFF" to skip tests
   set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/Projects/CLion-build-debug/bin")
   ```

2. In your profile (`~/.conan2/profiles/default`), add under a new `[conf]` section:
   ```ini
   [conf]
   tools.cmake.cmaketoolchain:user_toolchain+={{profile_dir}}/debug_vars.cmake
   ```
### 5. Configure the Release Profile

To **build a release version** using the script `./infomaniak-build-tools/linux/build-release-amd64.sh`, you must create a profile named `infomaniak_release`.
This profile must not contain a `tools.cmake.cmaketoolchain:user_toolchain` entry and must have the `build_type` set to `Release` or `RelWithDebInfo`.

> :information_source: This step is only required for the `build-release-amd64.sh` script, as the correct profile is already configured in the container used by `build-release-via-podman.sh`.
---

### 6. Install Project Dependencies

**From the repository root**, run the provided build script, specifying the desired configuration (`Debug` or `Release`) and the folder where the app will be builded.
```bash
./infomaniak-build-tools/conan/build_dependencies.sh [Debug|Release] [--output-dir=<output_dir>]
```

> **Note:** Currently only **xxHash**, **log4cplus**, **Qt**, **OpenSSL**, **zlib**, **SQLite**, **Sentry**, **Poco** and **keychain** are managed via this Conan-based workflow. Note that **keychain** links the system `glib` and `libsecret` packages instead of Conan-provided ones (see the Linux-only note above). Additional dependencies will be added in future updates.

---
# Build in Debug

## Linking dependencies

```bash
sudo apt-get install -y libgl1-mesa-dev \
   libsecret-1-dev
```
In order for CMake to be able to find all dependencies, you might need to define `LD_LIBRARY_PATH=/usr/local/lib` in your environment variables.

## Using CLion

### Prerequisites

Install the following libraries:

```bash
sudo apt update && sudo apt install -y mesa-utils freeglut3-dev \
                    pkg-config \
                    libglib2.0-dev libsecret-1-0 \
                    libsecret-1-dev libglib2.0-dev
```


### Using Conan - CMakeUserPresets

After you followed the [Conan](#conan) section, a file named `CMakeUserPresets.json` has been created at the root of the project.
_Sometimes, CLion need to reload the CMake project to detect the presets. To do so, go, in the top bar, to `Tools` > `CMake`  and click `Reload CMake Project`._

Now you can select the `conan-[debug|release]` profile in the CMake configuration.


### Classical Way

#### CMake Parameters

CMake options:

```
-DCMAKE_BUILD_TYPE:STRING=Debug
-DAPPLICATION_CLIENT_EXECUTABLE=kdrive_client
-DKDRIVE_THEME_DIR=/home/<user>/Projects/desktop-kDrive/infomaniak
-DCMAKE_INSTALL_PREFIX=/home/<user>/Projects/CLion-build-debug/bin
-DBUILD_UNIT_TESTS:BOOL=ON
-DQT_DEBUG_FIND_PACKAGE=ON
-DCMAKE_TOOLCHAIN_FILE=/home/<user>/Projects/CLion-build-debug/conan_toolchain.cmake
```

## Using Qt Creator

### Configuration

Open the kDrive project in your IDE   
In the project build settings, paste the following lines in the Initial Configuration Batch Edit (replace `<user>`)

```
-GUnix Makefiles
-DCMAKE_BUILD_TYPE:STRING=Debug
-DCMAKE_PROJECT_INCLUDE_BEFORE:PATH=%{IDE:ResourcePath}/package-manager/auto-setup.cmake
-DCMAKE_C_COMPILER:STRING=%{Compiler:Executable:C}
-DCMAKE_CXX_COMPILER:STRING=%{Compiler:Executable:Cxx}
-DAPPLICATION_CLIENT_EXECUTABLE=kdrive
-DKDRIVE_THEME_DIR=/home/<user>/Projects/desktop-kDrive/infomaniak
-DCMAKE_INSTALL_PREFIX=/home/<user>/Projects/build-desktop-kDrive-Desktop_Qt_6_2_3_GCC_64bit-Debug/bin
-DBUILD_TESTING=OFF
```

### Debugging

The configuration and database files are stored in the `~/.config/kDrive` directory.  
The log files will be generated in the `/tmp/kDrive-logdir` directory.

# Build in Release

The release AppImage is generated in a Podman container so that the build does not depend on the host distribution.

## Podman prerequisites

- Podman installed. Podman runs natively on Linux; on macOS the wrapper uses a Podman machine named
  `build_kdrive` (override it with `KDRIVE_PODMAN_MACHINE`).
- Git submodules initialized:
  ```bash
  git submodule update --init --recursive
  ```
- About 20 GB of free disk space and enough memory for the C++ build. Compilation parallelism (Conan and
  CMake) can be limited with `KDRIVE_BUILD_JOBS` (defaults to `nproc`), for example `KDRIVE_BUILD_JOBS=4`.

## Podman image

The published build image already contains the complete toolchain (Conan, the Qt Online Installer,
linuxdeploy and its plugins, appimagetool and sentry-cli):

```bash
podman pull ghcr.io/infomaniak/kdrive-desktop-linux:latest
```

That image is the one used by CI. The current sources also need `libfuse3-dev` (Lite Sync plugin) and
`libc-ares-dev` (Sentry recipe), which the published image does not provide. For local builds, build the
small derived image:

```bash
./infomaniak-build-tools/linux/build-podman-image.sh
```

The script builds `infomaniak-build-tools/linux/Containerfile` and tags the result
`localhost/kdrive-desktop-linux:latest`.

## Building

Start a Linux build with the script located in `infomaniak-build-tools/linux/build-release-via-podman.sh`.
It mounts the git directory to `/src`, the build directory to `/build` and the install directory to
`/install`, then runs `build-release-appimage.sh` inside the container.

```bash
./infomaniak-build-tools/linux/build-release-via-podman.sh
```

- Local builds use `localhost/kdrive-desktop-linux:latest` by default; CI keeps using the published image.
  Override the image with `--image` or `KDRIVE_LINUX_BUILD_IMAGE`, for example:
  ```bash
  KDRIVE_LINUX_BUILD_IMAGE=ghcr.io/infomaniak/kdrive-desktop-linux:latest \
      ./infomaniak-build-tools/linux/build-release-via-podman.sh
  ```
- The architecture (`amd64` or `arm64`) is the host architecture and is passed to Podman with
  `--platform linux/<arch>`.
- The Conan cache is persisted in `~/.conan2_linux` and mounted under `/root/.conan2`.
- `APPIMAGE_EXTRACT_AND_RUN=1` is set because a rootless container does not provide a usable FUSE mount
  for `appimagetool`.
- `QT_ACCOUNT_EMAIL`, `QT_ACCOUNT_PASSWORD` and `QT_INSTALLER_JWT_TOKEN` are forwarded to the container.

### Qt account

The Qt Online Installer needs a Qt account. For local builds, either:

- place `qtaccount.ini` in `~/.conan2_linux/.local/share/Qt/qtaccount.ini`, which is mounted into the
  container:
  ```bash
  mkdir -p ~/.conan2_linux/.local/share/Qt
  cp ~/.local/share/Qt/qtaccount.ini ~/.conan2_linux/.local/share/Qt/
  ```
- let the installer prompt for credentials in the terminal (an interactive shell is required).

CI uses `QT_ACCOUNT_EMAIL` and `QT_ACCOUNT_PASSWORD`.

If you do not want to build through Podman, use the `build-release-appimage.sh` script directly from a
compatible Linux environment.

The generated AppImage file will be located in the `build-linux-[arch]/install` directory, for example
`build-linux-amd64/install/kDrive-<version>-amd64.AppImage`.

## Lite Sync on Linux (POC)

Lite Sync on Linux is a proof of concept guarded by the CMake option
`BUILD_LINUX_VFS`, which defaults to `OFF`. It requires FUSE 3:

```bash
sudo apt update && sudo apt install -y libfuse3-dev
cmake -B build-linux-vfs -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_UNIT_TESTS=ON -DBUILD_LINUX_VFS=ON
cmake --build build-linux-vfs --parallel
```

The helper script `infomaniak-build-tools/linux/build-and-test-linux-vfs.sh`
installs the system packages, configures the build and runs the Linux VFS unit
tests. To build and run the same targets manually after the Conan dependencies
are available:

```bash
cmake --build build-linux-vfs --parallel \
    --target kDrive_vfs_linux kDrive_test_common kDrive_test_common_server
./infomaniak-build-tools/run-test.sh build-linux-vfs/bin kDrive_test_common_server
```

A real mount also needs `/dev/fuse` and `fusermount3`; containers and
GitHub-hosted runners usually do not provide them, so mount integration remains
a manual test on an Ubuntu desktop. The hydrate-on-open path is implemented with
the existing direct-download jobs; the plugin blocks the FUSE open until the
placeholder is hydrated or the timeout expires.

The real metadata and NodeMap checks can also be run without Qt/Poco or a FUSE
mount:

```bash
./infomaniak-build-tools/linux/run-linux-vfs-standalone-tests.sh
# With sanitizers:
KDRIVE_STANDALONE_SANITIZERS=address,undefined \
    ./infomaniak-build-tools/linux/run-linux-vfs-standalone-tests.sh
```

The harness compiles the real `nodemap.cpp` and `placeholdermetadata.cpp` with
minimal Qt/Poco-free stubs and real Linux xattr syscalls, and runs the same
checks as `kDrive_test_common_server`. Last run output: nine `[PASS]` groups for
placeholder metadata, status/progress, pin state, removal, regular-file
classification, stable inodes, rename, directory rename and erase/clear, ending
with `All standalone Linux VFS checks passed`. Exit code was 0, including under
AddressSanitizer and UBSan.

The same isolated harness is available through a dedicated CMake entry point and
CTest, without Qt, Poco, log4cplus or FUSE:

```bash
cmake -S test/libcommonserver/vfs/linux -B /tmp/kdrive-linux-vfs-cmake \
    -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/kdrive-linux-vfs-cmake \
    --target kDrive_test_linuxvfs_standalone --parallel
ctest --test-dir /tmp/kdrive-linux-vfs-cmake --output-on-failure
```

The script above invokes this exact CMake target/CTest path; it does not contain a
parallel direct-g++ build. This was run from a clean build directory; configure and
build succeeded and CTest reported `100% tests passed out of 1`. The built binary
printed the nine `[PASS]` groups and exited with code 0.

Build-system state: both submodules were initialized successfully with
`git submodule update --init --recursive`. A root configure with
`-DBUILD_UNIT_TESTS=ON -DBUILD_LINUX_VFS=ON -DBUILD_GUI=OFF` now stops only at
`find_package(Qt6)`: Qt6 is not installed. The apt attempts were blocked by the
container restriction and missing package indices:

```text
$ sudo -n apt-get update
sudo: The "no new privileges" flag is set, which prevents sudo from running as root.
$ sudo -n apt-get install -y libfuse3-dev pkg-config qt6-base-dev \
      libpoco-dev liblog4cplus-dev libcppunit-dev
sudo: The "no new privileges" flag is set, which prevents sudo from running as root.
$ apt-get download libfuse3-dev
E: Unable to locate package libfuse3-dev
$ apt-cache policy libfuse3-dev qt6-base-dev libpoco-dev \
      liblog4cplus-dev libcppunit-dev
# no candidate for any of these packages; pkg-config is installed
```

The plugin target still lists all phase 3–6 sources and links `PkgConfig::FUSE3`
and `Threads::Threads`; `kDrive_test_common_server` compiles the real node-map and
placeholder-metadata sources on Linux. The full plugin build, the CppUnit binary and
the FUSE mount still need an Ubuntu/Conan environment with Qt6, libfuse3,
`/dev/fuse` and a kDrive account.

The release scripts (`build-release-appimage.sh` and `build-release-amd64.sh`) now enable
`BUILD_LINUX_VFS=ON` and package the plugin, `libfuse3.so*`, `fusermount3` and
`litesync-exclude-linux.lst`. The Linux build image must provide `libfuse3-dev` (and
`pkg-config`); the scripts fail with an explicit error instead of silently shipping a
client without the plugin. An AppImage cannot preserve the setuid bit of a bundled
`fusermount3`, so the target host must permit the user to mount FUSE filesystems. This is
still a POC packaging constraint, not a production-ready FUSE installation.
