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

# Configures, builds and runs the isolated Linux VFS CMake harness.
#
# This script intentionally uses only the CMake entry point at
# test/libcommonserver/vfs/linux: no direct g++ invocation is kept here, so the
# documented and executed test paths cannot diverge.
#
# Usage:
#   infomaniak-build-tools/linux/run-linux-vfs-standalone-tests.sh
#   KDRIVE_STANDALONE_SANITIZERS=address,undefined \
#       infomaniak-build-tools/linux/run-linux-vfs-standalone-tests.sh

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
harness_dir="$repo_root/test/libcommonserver/vfs/linux"
build_dir="${KDRIVE_STANDALONE_BUILD_DIR:-${TMPDIR:-/tmp}/kdrive-linux-vfs-standalone}"
jobs="${JOBS:-$(nproc 2>/dev/null || echo 2)}"

if [[ -z "$build_dir" || "$build_dir" == "/" ]]; then
    echo "Error: refusing to use '$build_dir' as the standalone build directory." >&2
    exit 1
fi

resolve_tool() {
    local variable_name="$1"
    local executable_name="$2"
    local configured_value="${!variable_name:-}"

    if [[ -n "$configured_value" ]]; then
        if [[ ! -x "$configured_value" ]]; then
            echo "Error: $variable_name='$configured_value' is not executable." >&2
            exit 1
        fi
        printf '%s\n' "$configured_value"
        return
    fi

    if command -v "$executable_name" >/dev/null 2>&1; then
        command -v "$executable_name"
        return
    fi

    # The Cloud worktree ships CMake in a Python build environment; use it only
    # as a local fallback when the normal PATH does not provide the tool.
    if [[ -x "$HOME/buildenv/bin/$executable_name" ]]; then
        printf '%s\n' "$HOME/buildenv/bin/$executable_name"
        return
    fi

    echo "Error: '$executable_name' was not found. Install CMake/CTest or set $variable_name explicitly." >&2
    exit 1
}

cmake_bin="$(resolve_tool CMAKE cmake)"
ctest_bin="$(resolve_tool CTEST ctest)"

cmake_args=(
    -S "$harness_dir"
    -B "$build_dir"
    -DCMAKE_BUILD_TYPE=Debug
)

if [[ -n "${KDRIVE_STANDALONE_SANITIZERS:-}" ]]; then
    sanitizer_flags="-fsanitize=${KDRIVE_STANDALONE_SANITIZERS} -fno-omit-frame-pointer"
    cmake_args+=(
        "-DCMAKE_CXX_FLAGS=$sanitizer_flags"
        "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=${KDRIVE_STANDALONE_SANITIZERS}"
    )
fi

rm -rf "$build_dir"
mkdir -p "$build_dir"

"$cmake_bin" "${cmake_args[@]}"
"$cmake_bin" --build "$build_dir" --target kDrive_test_linuxvfs_standalone --parallel "$jobs"
"$ctest_bin" --test-dir "$build_dir" --output-on-failure

# CTest intentionally hides passing test output; run the CMake-built binary once
# more so the real [PASS] assertions are visible in the script output.
"$build_dir/kDrive_test_linuxvfs_standalone"
