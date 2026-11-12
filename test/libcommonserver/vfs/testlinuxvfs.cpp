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

#include "linux/testlinuxvfs_checks.h"

#include <fstream>
#include <string>
#include <vector>

namespace KDC {

namespace {

void assertChecks(const std::vector<std::string> &failures) {
    if (failures.empty()) {
        return;
    }

    std::string message = "Linux VFS check failed:";
    for (const auto &failure: failures) {
        message += "\n- " + failure;
    }
    CPPUNIT_FAIL(message);
}

} // namespace

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
    std::vector<std::string> failures;
    TestLinuxVfs::checkPlaceholderWriteAndRead(_filePath, failures);
    assertChecks(failures);
}

void TestPlaceholderMetadata::testStatusTransitions() {
    std::vector<std::string> failures;
    TestLinuxVfs::checkPlaceholderStatusTransitions(_filePath, failures);
    assertChecks(failures);
}

void TestPlaceholderMetadata::testPinState() {
    std::vector<std::string> failures;
    TestLinuxVfs::checkPlaceholderPinState(_filePath, failures);
    assertChecks(failures);
}

void TestPlaceholderMetadata::testRemove() {
    std::vector<std::string> failures;
    TestLinuxVfs::checkPlaceholderRemove(_filePath, failures);
    assertChecks(failures);
}

void TestPlaceholderMetadata::testRegularFileIsNotPlaceholder() {
    std::vector<std::string> failures;
    TestLinuxVfs::checkRegularFileIsNotPlaceholder(_filePath, failures);
    assertChecks(failures);
}

void TestNodeMap::testStableInodes() {
    std::vector<std::string> failures;
    TestLinuxVfs::checkNodeMapStableInodes(failures);
    assertChecks(failures);
}

void TestNodeMap::testRename() {
    std::vector<std::string> failures;
    TestLinuxVfs::checkNodeMapRename(failures);
    assertChecks(failures);
}

void TestNodeMap::testRenameDirectory() {
    std::vector<std::string> failures;
    TestLinuxVfs::checkNodeMapRenameDirectory(failures);
    assertChecks(failures);
}

void TestNodeMap::testEraseAndClear() {
    std::vector<std::string> failures;
    TestLinuxVfs::checkNodeMapEraseAndClear(failures);
    assertChecks(failures);
}

} // namespace KDC
