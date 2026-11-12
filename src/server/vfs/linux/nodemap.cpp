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

#include "nodemap.h"

#include <vector>

namespace KDC {

std::string NodeMap::normalize(const SyncPath &path) {
    if (path.empty()) {
        return {};
    }
    return path.lexically_normal().generic_string();
}

uint64_t NodeMap::inodeFor(const SyncPath &path) {
    const std::string key = normalize(path);
    if (key.empty() || key == ".") {
        return rootInode;
    }

    const std::lock_guard<std::mutex> lock(_mutex);

    if (const auto it = _pathToInode.find(key); it != _pathToInode.end()) {
        return it->second;
    }

    const uint64_t inode = _nextInode++;
    _pathToInode.emplace(key, inode);
    _inodeToPath.emplace(inode, key);
    return inode;
}

std::optional<SyncPath> NodeMap::pathFor(const uint64_t inode) const {
    const std::lock_guard<std::mutex> lock(_mutex);
    if (const auto it = _inodeToPath.find(inode); it != _inodeToPath.end()) {
        return SyncPath(it->second);
    }
    return std::nullopt;
}

void NodeMap::erase(const SyncPath &path) {
    const std::string key = normalize(path);
    if (key.empty() || key == ".") {
        return;
    }

    const std::lock_guard<std::mutex> lock(_mutex);

    const std::string prefix = key + "/";
    for (auto it = _pathToInode.begin(); it != _pathToInode.end();) {
        if (it->first != key && it->first.rfind(prefix, 0) != 0) {
            ++it;
            continue;
        }

        _inodeToPath.erase(it->second);
        it = _pathToInode.erase(it);
    }
}

void NodeMap::rename(const SyncPath &from, const SyncPath &to) {
    const std::string fromKey = normalize(from);
    const std::string toKey = normalize(to);
    if (fromKey.empty() || toKey.empty() || fromKey == toKey) {
        return;
    }

    const std::lock_guard<std::mutex> lock(_mutex);

    const std::string prefix = fromKey + "/";
    std::vector<std::pair<std::string, uint64_t>> movedEntries;
    for (auto it = _pathToInode.begin(); it != _pathToInode.end();) {
        if (it->first != fromKey && it->first.rfind(prefix, 0) != 0) {
            ++it;
            continue;
        }

        const std::string movedKey = toKey + it->first.substr(fromKey.size());
        movedEntries.emplace_back(movedKey, it->second);
        _inodeToPath[it->second] = movedKey;
        it = _pathToInode.erase(it);
    }

    for (const auto &[key, inode]: movedEntries) {
        _pathToInode[key] = inode;
    }
}

void NodeMap::clear() {
    const std::lock_guard<std::mutex> lock(_mutex);
    _pathToInode.clear();
    _inodeToPath.clear();
    _nextInode = rootInode + 1;
}

size_t NodeMap::size() const {
    const std::lock_guard<std::mutex> lock(_mutex);
    return _pathToInode.size();
}

} // namespace KDC
