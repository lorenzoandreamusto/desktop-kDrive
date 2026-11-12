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

program_name="$(basename "$0")"
host_os="$(get_host_os)"

image="${KDRIVE_LINUX_BUILD_IMAGE:-localhost/kdrive-desktop-linux:latest}"
base_image="ghcr.io/infomaniak/kdrive-desktop-linux:latest"

function display_help {
  echo "$program_name [-h] [--image image] [--base-image image]"
  echo "  Build the Linux AppImage build image for desktop-kDrive."
  echo "where:"
  echo "-h  Show this help text."
  echo "--image  Set the tag of the image to build. Defaults to '$image'."
  echo "--base-image  Set the published image to extend. Defaults to '$base_image'."
}

while :
do
    case "$1" in
      -h | --help)
          display_help
          exit 0
          ;;
      --image)
          image="$2"
          shift 2
          ;;
      --base-image)
          base_image="$2"
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

set -e

if ! command -v podman >/dev/null 2>&1; then
    echo "Podman is not installed or not in PATH." >&2
    exit 1
fi

if [[ "$host_os" == "Darwin" ]]; then
    start_podman_machine
    trap stop_podman_machine EXIT
fi

architecture=$(get_host_arch)

echo "Building container image '${image}' from '${base_image}' for architecture ${architecture} ..."

podman build \
    --platform "linux/${architecture}" \
    --build-arg "BASE_IMAGE=${base_image}" \
    --tag "$image" \
    --file "$script_directory_path/Containerfile" \
    "$script_directory_path"

if [[ "$host_os" == "Darwin" ]]; then
    stop_podman_machine
    trap - EXIT
fi

echo
echo "Successfully built '${image}'."
echo "Run the AppImage build with:"
echo "  KDRIVE_LINUX_BUILD_IMAGE='${image}' '$script_directory_path/build-release-via-podman.sh'"
