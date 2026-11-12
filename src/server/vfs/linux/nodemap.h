/*
 * Infomaniak kDrive - Desktop
 * Copyright (C) 2023-2026 Infomaniak Network SA
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "libcommon/utility/types.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace KDC {

/// Thread-safe mapping between the paths exposed by the FUSE mount and stable inodes.
///
/// FUSE expects an inode to remain stable for as long as a node exists; the sync folder
/// itself can be renamed or replaced by the sync engine, so the plugin cannot rely on the
/// underlying filesystem inode numbers.
class NodeMap {
    public:
        static constexpr uint64_t rootInode = 1;

        uint64_t inodeFor(const SyncPath &path);
        [[nodiscard]] std::optional<SyncPath> pathFor(uint64_t inode) const;
        void erase(const SyncPath &path);
        void rename(const SyncPath &from, const SyncPath &to);
        void clear();
        [[nodiscard]] size_t size() const;

    private:
        static std::string normalize(const SyncPath &path);

        mutable std::mutex _mutex;
        std::unordered_map<std::string, uint64_t> _pathToInode;
        std::unordered_map<uint64_t, std::string> _inodeToPath;
        uint64_t _nextInode{rootInode + 1};
};

} // namespace KDC
