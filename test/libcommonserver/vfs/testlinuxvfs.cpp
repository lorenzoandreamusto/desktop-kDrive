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

#include "testlinuxvfs.h"

#include "server/vfs/linux/nodemap.h"
#include "server/vfs/linux/placeholdermetadata.h"

#include <fstream>

namespace KDC {

void TestPlaceholderMetadata::setUp() {
    TestBase::start();
    _tempDir = std::make_unique<LocalTemporaryDirectory>("TestPlaceholderMetadata");
    _filePath = _tempDir->path() / "placeholder.bin";

    std::ofstream file(_filePath);
    file << "content";
}

void TestPlaceholderMetadata::tearDown() {
    _tempDir.reset();
    TestBase::stop();
}

void TestPlaceholderMetadata::testWriteAndRead() {
    constexpr int64_t size = 4096;
    constexpr time_t modificationTime = 1700000000;

    CPPUNIT_ASSERT(PlaceholderMetadata::write(_filePath, size, modificationTime, "remote-node-id", false));

    PlaceholderInfo info;
    CPPUNIT_ASSERT(PlaceholderMetadata::read(_filePath, info));
    CPPUNIT_ASSERT(info.isPlaceholder);
    CPPUNIT_ASSERT(!info.isHydrated);
    CPPUNIT_ASSERT(!info.isSyncing);
    CPPUNIT_ASSERT_EQUAL(size, info.size);
    CPPUNIT_ASSERT_EQUAL(modificationTime, info.modificationTime);
    CPPUNIT_ASSERT(info.nodeId == "remote-node-id");
    CPPUNIT_ASSERT(info.pinState == PinState::OnlineOnly);
}

void TestPlaceholderMetadata::testStatusTransitions() {
    CPPUNIT_ASSERT(PlaceholderMetadata::write(_filePath, 1024, 1700000000, "node", true));

    PlaceholderInfo info;
    CPPUNIT_ASSERT(PlaceholderMetadata::read(_filePath, info));
    CPPUNIT_ASSERT(info.isHydrated);

    CPPUNIT_ASSERT(PlaceholderMetadata::setStatus(_filePath, false, true, 42));
    CPPUNIT_ASSERT(PlaceholderMetadata::read(_filePath, info));
    CPPUNIT_ASSERT(!info.isHydrated);
    CPPUNIT_ASSERT(info.isSyncing);
    CPPUNIT_ASSERT_EQUAL(static_cast<int16_t>(42), info.progress);

    CPPUNIT_ASSERT(PlaceholderMetadata::setStatus(_filePath, false));
    CPPUNIT_ASSERT(PlaceholderMetadata::read(_filePath, info));
    CPPUNIT_ASSERT(!info.isHydrated);
    CPPUNIT_ASSERT(!info.isSyncing);
    CPPUNIT_ASSERT_EQUAL(static_cast<int16_t>(0), info.progress);
}

void TestPlaceholderMetadata::testPinState() {
    CPPUNIT_ASSERT(PlaceholderMetadata::write(_filePath, 0, 0, "", true));

    PlaceholderInfo info;
    CPPUNIT_ASSERT(PlaceholderMetadata::read(_filePath, info));
    CPPUNIT_ASSERT(info.pinState == PinState::AlwaysLocal);

    CPPUNIT_ASSERT(PlaceholderMetadata::setPinState(_filePath, PinState::OnlineOnly));
    CPPUNIT_ASSERT(PlaceholderMetadata::read(_filePath, info));
    CPPUNIT_ASSERT(info.pinState == PinState::OnlineOnly);

    CPPUNIT_ASSERT(PlaceholderMetadata::setPinState(_filePath, PinState::Unspecified));
    CPPUNIT_ASSERT(PlaceholderMetadata::read(_filePath, info));
    CPPUNIT_ASSERT(info.pinState == PinState::Unspecified);
}

void TestPlaceholderMetadata::testRemove() {
    CPPUNIT_ASSERT(PlaceholderMetadata::write(_filePath, 10, 1700000000, "node", false));
    CPPUNIT_ASSERT(PlaceholderMetadata::remove(_filePath));

    PlaceholderInfo info;
    CPPUNIT_ASSERT(PlaceholderMetadata::read(_filePath, info));
    CPPUNIT_ASSERT(!info.isPlaceholder);
    CPPUNIT_ASSERT(info.isHydrated);
}

void TestPlaceholderMetadata::testRegularFileIsNotPlaceholder() {
    PlaceholderInfo info;
    CPPUNIT_ASSERT(PlaceholderMetadata::read(_filePath, info));
    CPPUNIT_ASSERT(!info.isPlaceholder);
    CPPUNIT_ASSERT(info.isHydrated);
    CPPUNIT_ASSERT_EQUAL(static_cast<int64_t>(0), info.size);
}

void TestNodeMap::testStableInodes() {
    NodeMap nodeMap;

    CPPUNIT_ASSERT_EQUAL(NodeMap::rootInode, nodeMap.inodeFor({}));
    const uint64_t inode = nodeMap.inodeFor("/sync/folder/file.txt");
    CPPUNIT_ASSERT(inode > NodeMap::rootInode);
    CPPUNIT_ASSERT_EQUAL(inode, nodeMap.inodeFor("/sync/folder/file.txt"));
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), nodeMap.size());

    const auto path = nodeMap.pathFor(inode);
    CPPUNIT_ASSERT(path.has_value());
    CPPUNIT_ASSERT(path.value() == SyncPath("/sync/folder/file.txt"));
}

void TestNodeMap::testRename() {
    NodeMap nodeMap;
    const uint64_t inode = nodeMap.inodeFor("/sync/file.txt");

    nodeMap.rename("/sync/file.txt", "/sync/renamed.txt");

    const auto path = nodeMap.pathFor(inode);
    CPPUNIT_ASSERT(path.has_value());
    CPPUNIT_ASSERT(path.value() == SyncPath("/sync/renamed.txt"));
    CPPUNIT_ASSERT_EQUAL(inode, nodeMap.inodeFor("/sync/renamed.txt"));
}

void TestNodeMap::testEraseAndClear() {
    NodeMap nodeMap;
    nodeMap.inodeFor("/sync/file.txt");
    nodeMap.erase("/sync/file.txt");
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(0), nodeMap.size());

    const uint64_t first = nodeMap.inodeFor("/sync/first.txt");
    nodeMap.clear();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(0), nodeMap.size());
    CPPUNIT_ASSERT(first > NodeMap::rootInode);
    CPPUNIT_ASSERT_EQUAL(NodeMap::rootInode + 1, nodeMap.inodeFor("/sync/second.txt"));
}

} // namespace KDC
