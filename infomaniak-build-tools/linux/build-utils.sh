#! /bin/bash

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

function get_host_arch() {
    case "$(uname -m)" in
    x86_64) architecture="amd64" ;;
    arm64)  architecture="arm64" ;;
    aarch64)  architecture="arm64" ;;
    *) echo "Unsupported architecture: $(uname -m)" >&2; exit 1 ;;
    esac

    echo $architecture
}

function get_default_src_dir() {
  if [[ -n "$KDRIVE_SRC_DIR" ]]; then
     echo "$KDRIVE_SRC_DIR"
  elif [[ -d "$HOME/Projects/desktop-kDrive" ]]; then
     echo "$HOME/Projects/desktop-kDrive"
  else
     echo "$PWD"
    fi
}

function get_host_os() {
    uname -s
}

function get_podman_machine_name() {
    echo "${KDRIVE_PODMAN_MACHINE:-build_kdrive}"
}

# Podman runs natively on Linux; only macOS needs a Podman machine.
function start_podman_machine() {
    if [[ "$(get_host_os)" != "Darwin" ]]; then
        return 0
    fi

    local machine_name
    machine_name="$(get_podman_machine_name)"

    if ! podman machine inspect "$machine_name" >/dev/null 2>&1; then
        echo "Podman machine '$machine_name' does not exist, initializing it..." >&2
        podman machine init "$machine_name"
    fi

    # 'podman machine start' fails when the machine is already running.
    podman machine start "$machine_name" >/dev/null 2>&1 || true

    if ! podman info >/dev/null 2>&1; then
        echo "Failed to start the Podman machine '$machine_name'." >&2
        return 1
    fi
}

function stop_podman_machine() {
    if [[ "$(get_host_os)" != "Darwin" ]]; then
        return 0
    fi

    podman machine stop "$(get_podman_machine_name)" >/dev/null 2>&1 || true
}
