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

# Compiles and runs the real NodeMap and PlaceholderMetadata checks with g++.
# The standalone harness uses minimal Qt/Poco-free stubs and real Linux xattr
# syscalls, so it works without the full Conan/Qt dependency set or /dev/fuse.
#
# Usage:
#   infomaniak-build-tools/linux/run-linux-vfs-standalone-tests.sh
#   KDRIVE_STANDALONE_SANITIZERS=address,undefined \
#       infomaniak-build-tools/linux/run-linux-vfs-standalone-tests.sh

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${KDRIVE_STANDALONE_BUILD_DIR:-${TMPDIR:-/tmp}/kdrive-linux-vfs-standalone}"
cxx="${CXX:-g++}"
binary="$build_dir/kDrive_test_linuxvfs_standalone"
standalone_dir="$repo_root/test/libcommonserver/vfs/linux"

flags=(
    -std=c++20
    -O1
    -g
    -Wall
    -Wextra
    -I"$standalone_dir/standalone_stubs"
    -I"$repo_root/src"
)

if [[ -n "${KDRIVE_STANDALONE_SANITIZERS:-}" ]]; then
    flags+=("-fsanitize=${KDRIVE_STANDALONE_SANITIZERS}" -fno-omit-frame-pointer)
fi

mkdir -p "$build_dir"

"$cxx" "${flags[@]}" \
    "$repo_root/src/server/vfs/linux/nodemap.cpp" \
    "$repo_root/src/server/vfs/linux/placeholdermetadata.cpp" \
    "$standalone_dir/testlinuxvfs_checks.cpp" \
    "$standalone_dir/testlinuxvfs_standalone.cpp" \
    -o "$binary"

"$binary"
