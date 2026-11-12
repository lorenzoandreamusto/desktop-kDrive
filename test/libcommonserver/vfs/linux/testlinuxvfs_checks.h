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

#include <string>
#include <vector>

namespace KDC::TestLinuxVfs {

/// Checks shared by the CppUnit suite and the standalone Linux VFS harness.
/// A check appends a human-readable message to `failures` for every failed assertion.
void checkPlaceholderWriteAndRead(const SyncPath &filePath, std::vector<std::string> &failures);
void checkPlaceholderStatusTransitions(const SyncPath &filePath, std::vector<std::string> &failures);
void checkPlaceholderPinState(const SyncPath &filePath, std::vector<std::string> &failures);
void checkPlaceholderRemove(const SyncPath &filePath, std::vector<std::string> &failures);
void checkRegularFileIsNotPlaceholder(const SyncPath &filePath, std::vector<std::string> &failures);

void checkNodeMapStableInodes(std::vector<std::string> &failures);
void checkNodeMapRename(std::vector<std::string> &failures);
void checkNodeMapRenameDirectory(std::vector<std::string> &failures);
void checkNodeMapEraseAndClear(std::vector<std::string> &failures);

} // namespace KDC::TestLinuxVfs
