#!/usr/bin/env bash

#
# Infomaniak kDrive - Desktop
# Copyright (C) 2023-2026 Infomaniak Network SA
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

# Installs the Linux build dependencies of kDrive Desktop, builds the Linux VFS (Lite Sync) plugin together with the
# unit test binaries and runs the test suites that cover the phase 1 work.
#
# Usage: infomaniak-build-tools/linux/build-and-test-linux-vfs.sh [options]
#
# Qt is installed by the Conan recipe through the Qt online installer, which requires authentication. Log in once with
# the Qt online installer so that ~/.local/share/Qt/qtaccount.ini exists, or export QT_INSTALLER_JWT_TOKEN. Exporting
# QT_ACCOUNT_EMAIL and QT_ACCOUNT_PASSWORD is also supported locally and enables the recipe's credentials mode.

set -euo pipefail

program_name="$(basename "$0")"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

build_type="Debug"
build_dir="$repo_root/build-linux-vfs"
jobs="$(nproc 2>/dev/null || echo 2)"
install_system_deps=1
refresh_conan=0

log() { printf '\033[0;32m[%s]\033[0m %s\n' "$program_name" "$*"; }
warn() { printf '\033[0;33m[%s]\033[0m %s\n' "$program_name" "$*" >&2; }
die() {
    printf '\033[0;31m[%s] Error:\033[0m %s\n' "$program_name" "$*" >&2
    exit 1
}

usage() {
    cat <<EOF
$program_name [options]

Install the Linux dependencies, build kDrive Desktop with BUILD_LINUX_VFS=ON and run the unit tests.

Options:
  -h, --help              Show this help text.
  -t, --build-type TYPE   Debug (default), Release or RelWithDebInfo.
  -b, --build-dir DIR     Build directory. Defaults to '$build_dir'.
  -j, --jobs N            Number of parallel build jobs. Defaults to $(nproc 2>/dev/null || echo 2).
      --skip-system-deps  Skip the apt-get installation of system packages (assumes they are already installed).
      --update            Ask Conan to check the remotes for newer dependency revisions.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h | --help)
            usage
            exit 0
            ;;
        -t | --build-type)
            [[ $# -ge 2 ]] || die "Missing value for $1."
            build_type="$2"
            shift 2
            ;;
        -b | --build-dir)
            [[ $# -ge 2 ]] || die "Missing value for $1."
            build_dir="$2"
            shift 2
            ;;
        -j | --jobs)
            [[ $# -ge 2 ]] || die "Missing value for $1."
            jobs="$2"
            shift 2
            ;;
        --skip-system-deps)
            install_system_deps=0
            shift
            ;;
        --update)
            refresh_conan=1
            shift
            ;;
        *)
            die "Unknown option: $1 (use --help)."
            ;;
    esac
done

case "${build_type,,}" in
    debug) build_type="Debug" ;;
    release) build_type="Release" ;;
    relwithdebinfo) build_type="RelWithDebInfo" ;;
    *) die "Invalid build type '$build_type'. Expected Debug, Release or RelWithDebInfo." ;;
esac

[[ "$(uname -s)" == "Linux" ]] || die "This script only supports Linux."
[[ -f "$repo_root/conanfile.py" && -d "$repo_root/infomaniak-build-tools/conan" ]] ||
    die "Run this script from a kDrive Desktop checkout."

if [[ "$build_dir" != /* ]]; then
    build_dir="$repo_root/$build_dir"
fi
conan_dir="$build_dir/conan"

if [[ $install_system_deps -eq 1 ]]; then
    if [[ $EUID -eq 0 ]]; then
        sudo_cmd=()
    elif command -v sudo >/dev/null 2>&1 && sudo -v; then
        sudo_cmd=(sudo)
    else
        die "Root privileges are required to install the system packages. Run as root or install sudo."
    fi
else
    sudo_cmd=()
fi

run_as_root() {
    if [[ ${#sudo_cmd[@]} -gt 0 ]]; then
        "${sudo_cmd[@]}" "$@"
    else
        "$@"
    fi
}

# Ubuntu 24.04 ships libzip 1.7.3, but src/libcommon/CMakeLists.txt requires libzip >= 1.10.1.
libzip_version="1.10.1"

version_at_least() {
    [[ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -n 1)" == "$2" ]]
}

install_system_dependencies() {
    command -v apt-get >/dev/null 2>&1 || die "apt-get was not found. This script supports Debian/Ubuntu systems."

    log "Installing system packages..."
    run_as_root apt-get update

    # libcppunit-dev, libmount-dev, libglib2.0-dev and libsecret-1-dev are required by the tests and by the Conan
    # recipes. libc-ares-dev and libcurl4-openssl-dev let the Sentry recipe use the system libraries. The xcb packages
    # are needed by the Qt online installer, which is a Qt application itself.
    local packages=(
        build-essential
        cmake
        ninja-build
        pkg-config
        git
        python3-venv
        python3-pip
        curl
        ca-certificates
        clang
        libcppunit-dev
        libmount-dev
        libglib2.0-dev
        libsecret-1-dev
        libc-ares-dev
        libcurl4-openssl-dev
        zlib1g-dev
        libssl-dev
        xauth
        xvfb
        libxkbcommon-x11-0
        libxcb1
        libxcb-cursor0
        libxcb-icccm4
        libxcb-image0
        libxcb-keysyms1
        libxcb-randr0
        libxcb-render-util0
        libxcb-shape0
        libxcb-sync1
        libxcb-util1
        libxcb-xfixes0
        libxcb-xinerama0
        libxcb-xkb1
        libxcomposite1
        libxdamage1
        libxext6
        libxfixes3
        libxrandr2
        libxrender1
        libxi6
        libxtst6
        libfontconfig1
        libfreetype6
        libnss3
        libasound2
    )

    run_as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${packages[@]}"

    # The test CMake files link against /usr/local/lib/libcppunit.so, while libcppunit-dev installs the library in
    # /usr/lib/x86_64-linux-gnu.
    local cppunit_library
    cppunit_library="$(dpkg -L libcppunit-dev 2>/dev/null | grep -E '/libcppunit\.so$' | head -n 1 || true)"
    if [[ -n "$cppunit_library" && ! -e /usr/local/lib/libcppunit.so ]]; then
        run_as_root mkdir -p /usr/local/lib
        run_as_root ln -s "$cppunit_library" /usr/local/lib/libcppunit.so
    fi
    [[ -e /usr/local/lib/libcppunit.so ]] ||
        die "CPPUnit was not found at /usr/local/lib/libcppunit.so. Install it with prefix /usr/local."

    local current_libzip
    current_libzip="$(pkg-config --modversion libzip 2>/dev/null || true)"
    if [[ -n "$current_libzip" ]] && version_at_least "$current_libzip" "$libzip_version"; then
        log "libzip $current_libzip is already available."
        return
    fi

    log "Building libzip $libzip_version into /usr/local..."
    local tmp_dir
    tmp_dir="$(mktemp -d)"
    curl -fsSL "https://github.com/nih-at/libzip/archive/refs/tags/v${libzip_version}.tar.gz" -o "$tmp_dir/libzip.tar.gz"
    tar -xzf "$tmp_dir/libzip.tar.gz" -C "$tmp_dir"
    cmake -S "$tmp_dir/libzip-$libzip_version" -B "$tmp_dir/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DBUILD_DOC=OFF \
        -DBUILD_EXAMPLES=OFF \
        -DBUILD_REGRESS=OFF \
        -DBUILD_OSSFUZZ=OFF \
        -DENABLE_BZIP2=OFF \
        -DENABLE_LZMA=OFF \
        -DENABLE_ZSTD=OFF \
        -DENABLE_OPENSSL=OFF
    cmake --build "$tmp_dir/build" --parallel "$jobs"
    run_as_root cmake --install "$tmp_dir/build"
    run_as_root ldconfig
    rm -rf "$tmp_dir"
}

install_conan() {
    local venv_dir="${KDRIVE_BUILD_VENV:-$HOME/.cache/kdrive-build/venv}"
    if [[ ! -x "$venv_dir/bin/conan" ]]; then
        log "Creating the Conan virtual environment in $venv_dir..."
        python3 -m venv "$venv_dir"
        "$venv_dir/bin/pip" install --upgrade pip
        "$venv_dir/bin/pip" install 'conan>=2,<3'
    fi
    export PATH="$venv_dir/bin:$PATH"

    if ! conan profile show >/dev/null 2>&1; then
        conan profile detect --force
    fi
}

check_qt_login() {
    local qt_ini="${XDG_DATA_HOME:-$HOME/.local/share}/Qt/qtaccount.ini"
    if [[ -f "$qt_ini" || -n "${QT_INSTALLER_JWT_TOKEN:-}" ]]; then
        return
    fi
    if [[ -n "${QT_ACCOUNT_EMAIL:-}" && -n "${QT_ACCOUNT_PASSWORD:-}" ]]; then
        # The Qt Conan recipe always uses the credentials login type in CI mode, with --no-save-account.
        export KDRIVE_TEST_CI_RUNNING_ON_CI=true
        return
    fi
    die "Qt authentication is required: log in once with the Qt online installer so that '$qt_ini' exists, export
QT_INSTALLER_JWT_TOKEN, or export QT_ACCOUNT_EMAIL and QT_ACCOUNT_PASSWORD."
}

install_dependencies() {
    if [[ -f "$repo_root/.gitmodules" ]]; then
        log "Initializing the git submodules..."
        git -C "$repo_root" submodule update --init --recursive
    fi

    install_conan
    check_qt_login

    log "Installing the Conan dependencies ($build_type)..."
    local conan_args=("$build_type" "--output-dir=$conan_dir")
    if [[ $refresh_conan -eq 1 ]]; then
        conan_args+=(--update)
    fi

    local conan_runner=()
    if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]] && command -v xvfb-run >/dev/null 2>&1; then
        conan_runner=(xvfb-run -a)
    fi

    "${conan_runner[@]}" bash "$repo_root/infomaniak-build-tools/conan/build_dependencies.sh" "${conan_args[@]}"
}

configure_and_build() {
    local conan_toolchain
    conan_toolchain="$(find "$conan_dir" -name conan_toolchain.cmake -print -quit)"
    [[ -n "$conan_toolchain" ]] || die "Conan toolchain file not found in $conan_dir."

    local conan_env_dir
    conan_env_dir="$(dirname "$conan_toolchain")"

    # shellcheck disable=SC1091
    source "$repo_root/infomaniak-build-tools/conan/common-utils.sh"
    QTDIR="$(find_qt_conan_path "$conan_dir")"
    export QTDIR
    # shellcheck disable=SC1091
    source "$conan_env_dir/conanbuild.sh"

    export QMAKE="$QTDIR/bin/qmake"
    export PATH="$QTDIR/bin:$QTDIR/libexec:$PATH"
    export LD_LIBRARY_PATH="$QTDIR/lib:${LD_LIBRARY_PATH:-}"
    export PKG_CONFIG_PATH="$QTDIR/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

    local generator_args=()
    if command -v ninja >/dev/null 2>&1; then
        generator_args=(-G Ninja)
    fi

    mkdir -p "$build_dir"

    log "Configuring the build with BUILD_LINUX_VFS=ON..."
    cmake -S "$repo_root" -B "$build_dir" \
        "${generator_args[@]}" \
        -DQT_FEATURE_neon=OFF \
        -DCMAKE_BUILD_TYPE="$build_type" \
        -DCMAKE_PREFIX_PATH="/usr/local;$repo_root" \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DBIN_INSTALL_DIR="$build_dir/bin" \
        -DKDRIVE_THEME_DIR="$repo_root/infomaniak" \
        -DBUILD_UNIT_TESTS=ON \
        -DBUILD_LINUX_VFS=ON \
        -DBUILD_GUI=OFF \
        -DBUILD_EXTENSIONS=OFF \
        -DBUILD_EXTENSIONS_ICONS=OFF \
        -DBUILD_DOCS=OFF \
        -DCONAN_DEP_DIR="$conan_dir/dependencies" \
        -DCMAKE_TOOLCHAIN_FILE="$conan_toolchain"

    log "Building the Linux VFS plugin and the test binaries..."
    cmake --build "$build_dir" --parallel "$jobs" --target \
        kDrive_vfs_linux \
        kDrive_test_common \
        kDrive_test_common_server

    cp "$repo_root/sync-exclude-linux.lst" "$build_dir/bin/sync-exclude.lst"
    cp "$repo_root/sync-folder-rules-linux.csv" "$build_dir/bin/sync-folder-rules.csv"
    if [[ -f "$repo_root/litesync-exclude.lst" ]]; then
        cp "$repo_root/litesync-exclude.lst" "$build_dir/bin/litesync-exclude.lst"
    fi
}

run_tests() {
    local conan_toolchain
    conan_toolchain="$(find "$conan_dir" -name conan_toolchain.cmake -print -quit)"
    local conan_env_dir
    conan_env_dir="$(dirname "$conan_toolchain")"

    # shellcheck disable=SC1091
    source "$conan_env_dir/conanrun.sh"
    export LD_LIBRARY_PATH="$build_dir/bin:${LD_LIBRARY_PATH:-}"
    export KDRIVE_DEBUG=0

    local xdg_data_home
    xdg_data_home="$(mktemp -d)"
    cleanup() {
        chmod -R u+rwX "$xdg_data_home" 2>/dev/null || true
        rm -rf "$xdg_data_home"
    }
    trap cleanup EXIT
    export XDG_DATA_HOME="$xdg_data_home"

    chmod +x "$repo_root/infomaniak-build-tools/run-test.sh"

    local failures=0
    local test_binary
    for test_binary in kDrive_test_common kDrive_test_common_server; do
        if ! "$repo_root/infomaniak-build-tools/run-test.sh" "$build_dir/bin" "$test_binary"; then
            warn "$test_binary failed."
            failures=$((failures + 1))
        fi
    done

    [[ $failures -eq 0 ]] || die "$failures test suite(s) failed. See the output above."
    log "All unit tests passed."
}

if [[ $install_system_deps -eq 1 ]]; then
    install_system_dependencies
else
    log "Skipping the system package installation."
    [[ -e /usr/local/lib/libcppunit.so ]] ||
        warn "CPPUnit is not available at /usr/local/lib/libcppunit.so; the test binaries may fail to link."
fi

mkdir -p "$build_dir"
install_dependencies
configure_and_build
run_tests

log "Done. Build directory: $build_dir"
