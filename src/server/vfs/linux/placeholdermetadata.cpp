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

#include "placeholdermetadata.h"

#include "io/iohelper.h"
#include "log/log.h"

#include <algorithm>
#include <exception>

namespace KDC {

namespace {

ExitInfo xattrErrorToExitInfo(const IoError ioError) {
    switch (ioError) {
        case IoError::NoSuchFileOrDirectory:
            return {ExitCode::SystemError, ExitCause::NotFound};
        case IoError::AccessDenied:
        case IoError::AttrNotFound:
            return {ExitCode::SystemError, ExitCause::FileAccessError};
        default:
            return {ExitCode::SystemError};
    }
}

bool readInt64(const SyncPath &path, const std::string_view &attributeName, int64_t &value) {
    std::string rawValue;
    IoError ioError = IoError::Success;
    if (!IoHelper::getXAttrValue(path, attributeName, rawValue, ioError) || ioError != IoError::Success) {
        return false;
    }

    try {
        value = std::stoll(rawValue);
    } catch (const std::exception &) {
        return false;
    }
    return true;
}

} // namespace

ExitInfo PlaceholderMetadata::read(const SyncPath &path, PlaceholderInfo &info) {
    info = {};

    std::string status;
    IoError ioError = IoError::Success;
    if (!IoHelper::getXAttrValue(path, litesync_attrs::status, status, ioError)) {
        return xattrErrorToExitInfo(ioError);
    }

    if (ioError == IoError::AttrNotFound) {
        // No status attribute: this is a regular, fully hydrated file.
        return ExitCode::Ok;
    }
    if (ioError != IoError::Success) {
        return xattrErrorToExitInfo(ioError);
    }

    info.isPlaceholder = true;
    if (status == litesync_attrs::statusOffline) {
        info.isHydrated = true;
    } else if (status == litesync_attrs::statusOnline) {
        info.isHydrated = false;
    } else if (status.size() > 1 && status.front() == litesync_attrs::statusHydrating.front()) {
        info.isHydrated = false;
        info.isSyncing = true;

        try {
            const int progress = std::stoi(status.substr(1));
            info.progress = static_cast<int16_t>(std::clamp(progress, 0, 100));
        } catch (const std::exception &) {
            info.progress = 0;
        }
    } else {
        // Unknown value: keep the content visible rather than hiding it.
        info.isHydrated = true;
    }

    int64_t size = 0;
    if (readInt64(path, litesync_attrs::size, size)) {
        info.size = size;
    }

    int64_t modificationTime = 0;
    if (readInt64(path, litesync_attrs::modificationTime, modificationTime)) {
        info.modificationTime = static_cast<time_t>(modificationTime);
    }

    std::string nodeId;
    if (IoHelper::getXAttrValue(path, litesync_attrs::nodeId, nodeId, ioError) && ioError == IoError::Success) {
        info.nodeId = nodeId;
    }

    std::string pinState;
    if (IoHelper::getXAttrValue(path, litesync_attrs::pinState, pinState, ioError) && ioError == IoError::Success) {
        if (pinState == litesync_attrs::pinStatePinned) {
            info.pinState = PinState::AlwaysLocal;
        } else if (pinState == litesync_attrs::pinStateUnpinned) {
            info.pinState = PinState::OnlineOnly;
        } else {
            info.pinState = PinState::Unspecified;
        }
    }

    return ExitCode::Ok;
}

ExitInfo PlaceholderMetadata::write(const SyncPath &path, const int64_t size, const time_t modificationTime, const NodeId &nodeId,
                                    const bool hydrated) {
    if (const ExitInfo exitInfo = setStatus(path, hydrated); !exitInfo) {
        return exitInfo;
    }
    if (const ExitInfo exitInfo = setSize(path, size); !exitInfo) {
        return exitInfo;
    }
    if (const ExitInfo exitInfo = setModificationTime(path, modificationTime); !exitInfo) {
        return exitInfo;
    }

    if (!nodeId.empty()) {
        IoError ioError = IoError::Success;
        if (!IoHelper::setXAttrValue(path, litesync_attrs::nodeId, nodeId, ioError) || ioError != IoError::Success) {
            return xattrErrorToExitInfo(ioError);
        }
    }

    return setPinState(path, hydrated ? PinState::AlwaysLocal : PinState::OnlineOnly);
}

ExitInfo PlaceholderMetadata::setStatus(const SyncPath &path, const bool hydrated, const bool syncing, const int16_t progress) {
    const std::string status = IoHelper::statusXAttr(syncing, progress, hydrated);

    IoError ioError = IoError::Success;
    if (!IoHelper::setXAttrValue(path, litesync_attrs::status, status, ioError) || ioError != IoError::Success) {
        return xattrErrorToExitInfo(ioError);
    }
    return ExitCode::Ok;
}

ExitInfo PlaceholderMetadata::setSize(const SyncPath &path, const int64_t size) {
    IoError ioError = IoError::Success;
    const std::string value = std::to_string(size);
    if (!IoHelper::setXAttrValue(path, litesync_attrs::size, value, ioError) || ioError != IoError::Success) {
        return xattrErrorToExitInfo(ioError);
    }
    return ExitCode::Ok;
}

ExitInfo PlaceholderMetadata::setModificationTime(const SyncPath &path, const time_t modificationTime) {
    IoError ioError = IoError::Success;
    const std::string value = std::to_string(static_cast<int64_t>(modificationTime));
    if (!IoHelper::setXAttrValue(path, litesync_attrs::modificationTime, value, ioError) || ioError != IoError::Success) {
        return xattrErrorToExitInfo(ioError);
    }
    return ExitCode::Ok;
}

ExitInfo PlaceholderMetadata::setNodeId(const SyncPath &path, const NodeId &nodeId) {
    IoError ioError = IoError::Success;
    if (!IoHelper::setXAttrValue(path, litesync_attrs::nodeId, nodeId, ioError) || ioError != IoError::Success) {
        return xattrErrorToExitInfo(ioError);
    }
    return ExitCode::Ok;
}

ExitInfo PlaceholderMetadata::setPinState(const SyncPath &path, const PinState pinState) {
    IoError ioError = IoError::Success;

    switch (pinState) {
        case PinState::AlwaysLocal:
            if (!IoHelper::setXAttrValue(path, litesync_attrs::pinState, litesync_attrs::pinStatePinned, ioError) ||
                ioError != IoError::Success) {
                return xattrErrorToExitInfo(ioError);
            }
            break;
        case PinState::OnlineOnly:
            if (!IoHelper::setXAttrValue(path, litesync_attrs::pinState, litesync_attrs::pinStateUnpinned, ioError) ||
                ioError != IoError::Success) {
                return xattrErrorToExitInfo(ioError);
            }
            break;
        case PinState::Unspecified:
            if (!IoHelper::removeXAttrs(path, {litesync_attrs::pinState}, ioError) || ioError != IoError::Success) {
                return xattrErrorToExitInfo(ioError);
            }
            break;
        default:
            break;
    }

    return ExitCode::Ok;
}

ExitInfo PlaceholderMetadata::remove(const SyncPath &path) {
    IoError ioError = IoError::Success;
    if (!IoHelper::removeLiteSyncXAttrs(path, ioError) || ioError != IoError::Success) {
        return xattrErrorToExitInfo(ioError);
    }
    return ExitCode::Ok;
}

} // namespace KDC
