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

script_directory_path="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
source "$script_directory_path/build-utils.sh"

git_dir="$(get_default_src_dir)"
program_name="$(basename "$0")"
host_os="$(get_host_os)"

published_image="ghcr.io/infomaniak/kdrive-desktop-linux:latest"
local_image="localhost/kdrive-desktop-linux:latest"
image=""

function display_help {
  echo "$program_name [-h] [-d git-directory] [--image image]"
  echo "  Build the Linux release AppImage for desktop-kDrive via Podman."
  echo "where:"
  echo "-h  Show this help text."
  echo "-d  Set the git directory path that will be mapped to the image '/src' folder. Defaults to '$git_dir'."
  echo "--image  Set the container image to use. Defaults to '$local_image' for local builds"
  echo "         and to '$published_image' in CI. Can also be set with KDRIVE_LINUX_BUILD_IMAGE."
}

while :
do
    case "$1" in
      -h | --help)
          display_help
          exit 0
          ;;
      -d | --git-dir)
          git_dir="$2"
          shift 2
          ;;
      --image)
          image="$2"
          shift 2
          ;;
      --) # End of all options
          shift
          break
          ;;
      -*)
          echo "Error: Unknown option: $1" >&2
          exit 1
          ;;
      *)  # No more options
          break
          ;;
    esac
done

if [[ -z "$image" ]]; then
    image="${KDRIVE_LINUX_BUILD_IMAGE:-}"
fi

if [[ -z "$image" ]]; then
    if [[ "${GITHUB_ACTIONS:-}" == "true" || "${KDRIVE_TEST_CI_RUNNING_ON_CI:-}" == "true" ]]; then
        image="$published_image"
    else
        image="$local_image"
    fi
fi

if ! command -v podman >/dev/null 2>&1; then
    echo "Podman is not installed or not in PATH." >&2
    exit 1
fi

build_dir="$PWD/build-linux"
client_dir="$build_dir/client"
install_dir="$build_dir/install"

conan_persistent_folder="$HOME/.conan2_linux"
conan_cache_folder="$conan_persistent_folder/p"
local_recipes_index="$conan_persistent_folder/.local_recipes_index"

rm -Rf "$build_dir"
mkdir -p "$build_dir"
mkdir -p "$client_dir"
mkdir -p "$install_dir"
mkdir -p "$conan_cache_folder"
mkdir -p "$local_recipes_index"
qt_account_mount=()
if [[ "${GITHUB_ACTIONS:-}" != "true" && "${KDRIVE_TEST_CI_RUNNING_ON_CI:-}" != "true" ]]; then
    mkdir -p "$conan_persistent_folder/.local/share/Qt"
    qt_account_mount=(--volume "$conan_persistent_folder/.local/share/Qt:/root/.local/share/Qt")
fi

if [ ! -d "$git_dir" ]; then
    echo "Git directory does not exist: '$git_dir'"
    exit 1
fi

echo "Using git directory '${git_dir}'."
echo "Using temporary build folder '${build_dir}'."
architecture=$(get_host_arch)
echo "Using container image '${image}'."
echo "Building desktop-kDrive AppImage for architecture ${architecture} via Podman ..."
echo

set -ex

# Podman runs natively on Linux; macOS needs the Podman machine to be started.
if [[ "$host_os" == "Darwin" ]]; then
    start_podman_machine
    trap stop_podman_machine EXIT
fi

if ! podman image exists "$image"; then
    if [[ "$image" == localhost/* ]]; then
        echo "Container image '$image' not found." >&2
        echo "Build it first with '$script_directory_path/build-podman-image.sh'," >&2
        echo "or use the published image with '--image $published_image'." >&2
        exit 1
    fi

    podman pull --platform "linux/${architecture}" "$image"
fi

inode_max_limit=100000
# Probe the limit in a subshell so that the shell option tracing from 'set -x'
# does not end up in the captured output.
if ! ( set +x; ulimit -n "$inode_max_limit" ) 2>/dev/null; then
    echo "Failed to set the max limit of open inodes with '$inode_max_limit'."
    inode_max_limit=$(ulimit -n)
    echo "Current limit: '$inode_max_limit'."
fi

# Keep an interactive terminal when available so that the Qt Online Installer
# can prompt for credentials on local builds.
podman_terminal_flags=()
if [ -t 0 ]; then
    podman_terminal_flags+=(-i)
fi
if [ -t 1 ]; then
    podman_terminal_flags+=(-t)
fi

# On SELinux-enabled hosts the bind mounts need to be readable by the container.
podman_security_flags=()
if [[ "$host_os" == "Linux" ]] && command -v getenforce >/dev/null 2>&1 && [[ "$(getenforce 2>/dev/null)" == "Enforcing" ]]; then
    podman_security_flags+=(--security-opt label=disable)
fi

# Only needed if a step really has to use FUSE inside the container; the build
# itself extracts appimagetool instead (see APPIMAGE_EXTRACT_AND_RUN below).
if [[ "${KDRIVE_PODMAN_PRIVILEGED:-0}" == "1" ]]; then
    podman_security_flags+=(--privileged)
fi

qt_account_ini="$conan_persistent_folder/.local/share/Qt/qtaccount.ini"
if [[ "${GITHUB_ACTIONS:-}" != "true" && "${KDRIVE_TEST_CI_RUNNING_ON_CI:-}" != "true" ]] \
    && [ ! -f "$qt_account_ini" ] && [ -z "${QT_INSTALLER_JWT_TOKEN:-}" ] && [ ! -t 0 ]; then
    echo "Warning: no Qt account file found at '$qt_account_ini' and no interactive terminal." >&2
    echo "The Qt Online Installer may fail to authenticate. See infomaniak-build-tools/linux/Readme.md." >&2
fi

podman run --rm \
    "${podman_terminal_flags[@]}" \
    "${podman_security_flags[@]}" \
    --ulimit nofile=$inode_max_limit:$inode_max_limit \
    --volume "$git_dir:/src" \
    --volume "$build_dir:/build" \
    --volume "$install_dir:/install" \
    --volume "$conan_cache_folder:/root/.conan2/p" \
    --volume "$local_recipes_index:/root/.conan2/.local_recipes_index/" \
    "${qt_account_mount[@]}" \
    --workdir "/src" \
    --env APPLICATION_SERVER_URL="$APPLICATION_SERVER_URL" \
    --env GITHUB_ACTIONS="${GITHUB_ACTIONS:-}" \
    --env KDRIVE_TEST_CI_RUNNING_ON_CI="${KDRIVE_TEST_CI_RUNNING_ON_CI:-}" \
    --env QT_ACCOUNT_EMAIL \
    --env QT_ACCOUNT_PASSWORD \
    --env QT_INSTALLER_JWT_TOKEN \
    --env KDRIVE_BUILD_JOBS \
    --env KDRIVE_VERSION_BUILD="$(date +%Y%m%d)" \
    --env APPIMAGE_EXTRACT_AND_RUN=1 \
    --platform "linux/${architecture}" \
    "$image" /bin/bash -c "/src/infomaniak-build-tools/linux/build-release-appimage.sh"

if [[ "$host_os" == "Darwin" ]]; then
    stop_podman_machine
    trap - EXIT
fi

appimage_path="$install_dir/kDrive-${architecture}.AppImage"
if [ ! -f "$appimage_path" ]; then
    echo "The AppImage was not created at '$appimage_path'." >&2
    exit 1
fi

version=$(grep "KDRIVE_VERSION_FULL" "$build_dir/client/version.h" | awk '{print $3}')
mv "$appimage_path" "$install_dir/kDrive-$version-${architecture}.AppImage"

if [ -f "$install_dir/kDriveRecoveryUpdater-${architecture}.AppImage" ]; then
    mv "$install_dir/kDriveRecoveryUpdater-${architecture}.AppImage" "$install_dir/kDriveRecoveryUpdater-$version-${architecture}.AppImage"
fi

rm -Rf "$build_dir-${architecture}"
mv "$build_dir" "$build_dir-${architecture}"

set -

echo
echo "Successfully built '${build_dir}-${architecture}/install/kDrive-${version}-${architecture}.AppImage'."
