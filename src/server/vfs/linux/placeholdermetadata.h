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

#include <ctime>
#include <string>

namespace KDC {

/// Metadata attached to a Linux placeholder through user.* extended attributes.
struct PlaceholderInfo {
        bool isPlaceholder{false};
        bool isHydrated{true};
        bool isSyncing{false};
        int16_t progress{0};
        int64_t size{0};
        time_t modificationTime{0};
        NodeId nodeId;
        PinState pinState{PinState::Unspecified};
};

/// Reads and writes the placeholder metadata stored in extended attributes.
///
/// The status vocabulary mirrors macOS:
/// - O: online, the content is not available locally;
/// - F: offline, the content is available locally;
/// - H<progress>: hydration in progress, <progress> being a percentage.
class PlaceholderMetadata {
    public:
        static ExitInfo read(const SyncPath &path, PlaceholderInfo &info);
        static ExitInfo write(const SyncPath &path, int64_t size, time_t modificationTime, const NodeId &nodeId, bool hydrated);
        static ExitInfo setStatus(const SyncPath &path, bool hydrated, bool syncing = false, int16_t progress = 0);
        static ExitInfo setSize(const SyncPath &path, int64_t size);
        static ExitInfo setModificationTime(const SyncPath &path, time_t modificationTime);
        static ExitInfo setNodeId(const SyncPath &path, const NodeId &nodeId);
        static ExitInfo setPinState(const SyncPath &path, PinState pinState);
        static ExitInfo remove(const SyncPath &path);
};

} // namespace KDC
