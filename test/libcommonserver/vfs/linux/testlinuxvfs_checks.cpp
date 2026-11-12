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

#include "testlinuxvfs_checks.h"

#include "io/iohelper.h"
#include "server/vfs/linux/nodemap.h"
#include "server/vfs/linux/placeholdermetadata.h"

#include <string>
#include <string_view>

namespace KDC::TestLinuxVfs {

namespace {

void expect(const bool condition, const std::string &message, std::vector<std::string> &failures) {
    if (!condition) {
        failures.push_back(message);
    }
}

void expectEqual(const int64_t expected, const int64_t actual, const std::string &name, std::vector<std::string> &failures) {
    if (expected != actual) {
        failures.push_back(name + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
    }
}

} // namespace

void checkPlaceholderWriteAndRead(const SyncPath &filePath, std::vector<std::string> &failures) {
    constexpr int64_t size = 4096;
    constexpr time_t modificationTime = 1700000000;

    if (!PlaceholderMetadata::write(filePath, size, modificationTime, "remote-node-id", false)) {
        failures.emplace_back("write failed");
        return;
    }

    PlaceholderInfo info;
    if (!PlaceholderMetadata::read(filePath, info)) {
        failures.emplace_back("read after write failed");
        return;
    }

    expect(info.isPlaceholder, "file is not marked as a placeholder", failures);
    expect(!info.isHydrated, "placeholder is unexpectedly hydrated", failures);
    expect(!info.isSyncing, "placeholder is unexpectedly syncing", failures);
    expectEqual(size, info.size, "placeholder size", failures);
    expectEqual(static_cast<int64_t>(modificationTime), static_cast<int64_t>(info.modificationTime),
                "placeholder modification time", failures);
    expect(info.nodeId == "remote-node-id", "placeholder node id", failures);
    expect(info.pinState == PinState::OnlineOnly, "placeholder pin state is not OnlineOnly", failures);

    const auto readXAttr = [&](const std::string_view name, std::string &value) {
        IoError ioError = IoError::Success;
        return IoHelper::getXAttrValue(filePath, name, value, ioError) && ioError == IoError::Success;
    };

    std::string rawStatus;
    std::string rawSize;
    std::string rawModificationTime;
    std::string rawNodeId;
    std::string rawPinState;
    expect(readXAttr(litesync_attrs::status, rawStatus) && rawStatus == std::string(litesync_attrs::statusOnline),
           "xattr status is not O", failures);
    expect(readXAttr(litesync_attrs::size, rawSize) && rawSize == "4096", "xattr size is not 4096", failures);
    expect(readXAttr(litesync_attrs::modificationTime, rawModificationTime) && rawModificationTime == "1700000000",
           "xattr modification time is wrong", failures);
    expect(readXAttr(litesync_attrs::nodeId, rawNodeId) && rawNodeId == "remote-node-id", "xattr node id is wrong", failures);
    expect(readXAttr(litesync_attrs::pinState, rawPinState) && rawPinState == std::string(litesync_attrs::pinStateUnpinned),
           "xattr pin state is not U", failures);
}

void checkPlaceholderStatusTransitions(const SyncPath &filePath, std::vector<std::string> &failures) {
    if (!PlaceholderMetadata::write(filePath, 1024, 1700000000, "node", true)) {
        failures.emplace_back("initial write failed");
        return;
    }

    PlaceholderInfo info;
    if (!PlaceholderMetadata::read(filePath, info)) {
        failures.emplace_back("initial read failed");
        return;
    }
    expect(info.isHydrated, "initial placeholder is not hydrated", failures);

    if (!PlaceholderMetadata::setStatus(filePath, false, true, 42)) {
        failures.emplace_back("setStatus(H42) failed");
        return;
    }
    if (!PlaceholderMetadata::read(filePath, info)) {
        failures.emplace_back("read after setStatus(H42) failed");
        return;
    }
    expect(!info.isHydrated, "H42 placeholder is unexpectedly hydrated", failures);
    expect(info.isSyncing, "H42 placeholder is not syncing", failures);
    expectEqual(42, info.progress, "H42 progress", failures);

    if (!PlaceholderMetadata::setStatus(filePath, false)) {
        failures.emplace_back("setStatus(O) failed");
        return;
    }
    if (!PlaceholderMetadata::read(filePath, info)) {
        failures.emplace_back("read after setStatus(O) failed");
        return;
    }
    expect(!info.isHydrated, "O placeholder is unexpectedly hydrated", failures);
    expect(!info.isSyncing, "O placeholder is unexpectedly syncing", failures);
    expectEqual(0, info.progress, "O progress", failures);
}

void checkPlaceholderPinState(const SyncPath &filePath, std::vector<std::string> &failures) {
    if (!PlaceholderMetadata::write(filePath, 0, 0, "", true)) {
        failures.emplace_back("write failed");
        return;
    }

    PlaceholderInfo info;
    if (!PlaceholderMetadata::read(filePath, info)) {
        failures.emplace_back("initial read failed");
        return;
    }
    expect(info.pinState == PinState::AlwaysLocal, "initial pin state is not AlwaysLocal", failures);

    if (!PlaceholderMetadata::setPinState(filePath, PinState::OnlineOnly)) {
        failures.emplace_back("setPinState(OnlineOnly) failed");
        return;
    }
    if (!PlaceholderMetadata::read(filePath, info)) {
        failures.emplace_back("read after setPinState(OnlineOnly) failed");
        return;
    }
    expect(info.pinState == PinState::OnlineOnly, "pin state is not OnlineOnly", failures);

    if (!PlaceholderMetadata::setPinState(filePath, PinState::Unspecified)) {
        failures.emplace_back("setPinState(Unspecified) failed");
        return;
    }
    if (!PlaceholderMetadata::read(filePath, info)) {
        failures.emplace_back("read after setPinState(Unspecified) failed");
        return;
    }
    expect(info.pinState == PinState::Unspecified, "pin state is not Unspecified", failures);
}

void checkPlaceholderRemove(const SyncPath &filePath, std::vector<std::string> &failures) {
    if (!PlaceholderMetadata::write(filePath, 10, 1700000000, "node", false)) {
        failures.emplace_back("write failed");
        return;
    }
    if (!PlaceholderMetadata::remove(filePath)) {
        failures.emplace_back("remove failed");
        return;
    }

    PlaceholderInfo info;
    if (!PlaceholderMetadata::read(filePath, info)) {
        failures.emplace_back("read after remove failed");
        return;
    }
    expect(!info.isPlaceholder, "file remains a placeholder after remove", failures);
    expect(info.isHydrated, "file is not hydrated after remove", failures);
}

void checkRegularFileIsNotPlaceholder(const SyncPath &filePath, std::vector<std::string> &failures) {
    PlaceholderInfo info;
    if (!PlaceholderMetadata::read(filePath, info)) {
        failures.emplace_back("read of regular file failed");
        return;
    }
    expect(!info.isPlaceholder, "regular file is marked as placeholder", failures);
    expect(info.isHydrated, "regular file is not hydrated", failures);
    expectEqual(0, info.size, "regular file size", failures);
}

void checkNodeMapStableInodes(std::vector<std::string> &failures) {
    NodeMap nodeMap;

    expectEqual(NodeMap::rootInode, nodeMap.inodeFor({}), "root inode", failures);
    const uint64_t inode = nodeMap.inodeFor("/sync/folder/file.txt");
    expect(inode > NodeMap::rootInode, "file inode is not greater than root", failures);
    expectEqual(static_cast<int64_t>(inode), static_cast<int64_t>(nodeMap.inodeFor("/sync/folder/file.txt")), "inode stability",
                failures);
    expectEqual(1, static_cast<int64_t>(nodeMap.size()), "node map size", failures);

    const auto path = nodeMap.pathFor(inode);
    expect(path.has_value(), "path lookup failed", failures);
    expect(path.has_value() && path.value() == SyncPath("/sync/folder/file.txt"), "path lookup returned the wrong path",
           failures);
}

void checkNodeMapRename(std::vector<std::string> &failures) {
    NodeMap nodeMap;
    const uint64_t inode = nodeMap.inodeFor("/sync/file.txt");

    nodeMap.rename("/sync/file.txt", "/sync/renamed.txt");

    const auto path = nodeMap.pathFor(inode);
    expect(path.has_value(), "renamed path lookup failed", failures);
    expect(path.has_value() && path.value() == SyncPath("/sync/renamed.txt"), "renamed path is wrong", failures);
    expectEqual(static_cast<int64_t>(inode), static_cast<int64_t>(nodeMap.inodeFor("/sync/renamed.txt")), "renamed inode",
                failures);
}

void checkNodeMapRenameDirectory(std::vector<std::string> &failures) {
    NodeMap nodeMap;
    const uint64_t directoryInode = nodeMap.inodeFor("/sync/dir");
    const uint64_t childInode = nodeMap.inodeFor("/sync/dir/file.txt");
    const uint64_t otherInode = nodeMap.inodeFor("/sync/other.txt");

    nodeMap.rename("/sync/dir", "/sync/moved");

    const auto directoryPath = nodeMap.pathFor(directoryInode);
    expect(directoryPath.has_value(), "directory path lookup failed", failures);
    expect(directoryPath.has_value() && directoryPath.value() == SyncPath("/sync/moved"), "directory path is wrong", failures);

    const auto childPath = nodeMap.pathFor(childInode);
    expect(childPath.has_value(), "child path lookup failed", failures);
    expect(childPath.has_value() && childPath.value() == SyncPath("/sync/moved/file.txt"), "child path is wrong", failures);
    expectEqual(static_cast<int64_t>(childInode), static_cast<int64_t>(nodeMap.inodeFor("/sync/moved/file.txt")), "child inode",
                failures);

    const auto otherPath = nodeMap.pathFor(otherInode);
    expect(otherPath.has_value(), "other path lookup failed", failures);
    expect(otherPath.has_value() && otherPath.value() == SyncPath("/sync/other.txt"), "other path is wrong", failures);

    nodeMap.erase("/sync/moved");
    expectEqual(1, static_cast<int64_t>(nodeMap.size()), "node map size after subtree erase", failures);
    expect(nodeMap.pathFor(otherInode).has_value(), "erase removed an unrelated entry", failures);
}

void checkNodeMapEraseAndClear(std::vector<std::string> &failures) {
    NodeMap nodeMap;
    nodeMap.inodeFor("/sync/file.txt");
    nodeMap.erase("/sync/file.txt");
    expectEqual(0, static_cast<int64_t>(nodeMap.size()), "node map size after erase", failures);

    const uint64_t first = nodeMap.inodeFor("/sync/first.txt");
    nodeMap.clear();
    expectEqual(0, static_cast<int64_t>(nodeMap.size()), "node map size after clear", failures);
    expect(first > NodeMap::rootInode, "first inode is not greater than root", failures);
    expectEqual(NodeMap::rootInode + 1, nodeMap.inodeFor("/sync/second.txt"), "inode counter after clear", failures);
}

} // namespace KDC::TestLinuxVfs
