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

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;

namespace {

int failures = 0;

template<typename CheckFunction>
void runCheck(const char *name, CheckFunction checkFunction) {
    std::vector<std::string> checkFailures;
    checkFunction(checkFailures);

    if (checkFailures.empty()) {
        std::cout << "[PASS] " << name << '\n';
        return;
    }

    ++failures;
    std::cerr << "[FAIL] " << name << '\n';
    for (const auto &failure: checkFailures) {
        std::cerr << "       " << failure << '\n';
    }
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / ("kdrive-linux-vfs-standalone-" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root);

    const KDC::SyncPath filePath = root / "placeholder.bin";
    {
        std::ofstream file(filePath);
        file << "content";
    }

    runCheck("placeholder metadata write/read",
             [&](std::vector<std::string> &result) { KDC::TestLinuxVfs::checkPlaceholderWriteAndRead(filePath, result); });
    runCheck("placeholder status transitions",
             [&](std::vector<std::string> &result) { KDC::TestLinuxVfs::checkPlaceholderStatusTransitions(filePath, result); });
    runCheck("placeholder pin state",
             [&](std::vector<std::string> &result) { KDC::TestLinuxVfs::checkPlaceholderPinState(filePath, result); });
    runCheck("placeholder remove",
             [&](std::vector<std::string> &result) { KDC::TestLinuxVfs::checkPlaceholderRemove(filePath, result); });
    runCheck("regular file is not a placeholder",
             [&](std::vector<std::string> &result) { KDC::TestLinuxVfs::checkRegularFileIsNotPlaceholder(filePath, result); });
    runCheck("node map stable inodes", KDC::TestLinuxVfs::checkNodeMapStableInodes);
    runCheck("node map rename", KDC::TestLinuxVfs::checkNodeMapRename);
    runCheck("node map directory rename", KDC::TestLinuxVfs::checkNodeMapRenameDirectory);
    runCheck("node map erase and clear", KDC::TestLinuxVfs::checkNodeMapEraseAndClear);

    fs::remove_all(root);

    if (failures != 0) {
        std::cerr << failures << " Linux VFS check group(s) failed\n";
        return 1;
    }

    std::cout << "All standalone Linux VFS checks passed\n";
    return 0;
}
