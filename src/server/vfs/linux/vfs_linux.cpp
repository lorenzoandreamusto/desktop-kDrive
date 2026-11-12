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

#include "vfs_linux.h"

#include "libcommonserver/log/log.h"

namespace KDC {

VfsLinux::VfsLinux(const VfsSetupParams &vfsSetupParams, QObject *parent) :
    Vfs(vfsSetupParams, parent) {}

ExitInfo VfsLinux::startImpl([[maybe_unused]] bool &installationDone, [[maybe_unused]] bool &activationDone,
                             [[maybe_unused]] bool &connectionDone) {
    LOG_WARN(logger(), "Linux VFS is not implemented yet: Lite Sync cannot be activated");
    return {ExitCode::SystemError, ExitCause::UnableToStartVfs};
}

void VfsLinux::stopImpl([[maybe_unused]] bool unregister) {
    // Nothing to stop yet.
}

ExitInfo VfsLinux::updateMetadata([[maybe_unused]] const SyncPath &filePath, [[maybe_unused]] time_t creationTime,
                                  [[maybe_unused]] time_t modtime, [[maybe_unused]] int64_t size,
                                  [[maybe_unused]] const NodeId &fileId) {
    return ExitCode::Ok;
}

ExitInfo VfsLinux::createPlaceholder([[maybe_unused]] const SyncPath &relativeLocalPath,
                                     [[maybe_unused]] const SyncFileItem &item) {
    return ExitCode::Ok;
}

ExitInfo VfsLinux::dehydratePlaceholder([[maybe_unused]] const SyncPath &path) {
    return ExitCode::Ok;
}

ExitInfo VfsLinux::convertToPlaceholder([[maybe_unused]] const SyncPath &path, [[maybe_unused]] const SyncFileItem &item) {
    return ExitCode::Ok;
}

ExitInfo VfsLinux::updateFetchStatus([[maybe_unused]] const SyncPath &tmpPath, [[maybe_unused]] const SyncPath &path,
                                     [[maybe_unused]] int64_t received, [[maybe_unused]] bool &canceled,
                                     [[maybe_unused]] bool &finished) {
    return ExitCode::Ok;
}

ExitInfo VfsLinux::updateFetchStatus([[maybe_unused]] const SyncPath &path, [[maybe_unused]] const std::string &status) {
    return ExitCode::Ok;
}

ExitInfo VfsLinux::forceStatus([[maybe_unused]] const SyncPath &path, [[maybe_unused]] const VfsStatus &vfsStatus) {
    return ExitCode::Ok;
}

ExitInfo VfsLinux::isDehydratedPlaceholder([[maybe_unused]] const SyncPath &filePath, bool &isDehydrated) {
    isDehydrated = false;
    return ExitCode::Ok;
}

ExitInfo VfsLinux::setPinState([[maybe_unused]] const SyncPath &fileRelativePath, [[maybe_unused]] PinState state) {
    return ExitCode::Ok;
}

PinState VfsLinux::pinState([[maybe_unused]] const SyncPath &fileRelativePath) {
    return PinState::AlwaysLocal;
}

ExitInfo VfsLinux::status([[maybe_unused]] const SyncPath &filePath, VfsStatus &vfsStatus) {
    vfsStatus.isPlaceholder = false;
    vfsStatus.isHydrated = true;
    return ExitCode::Ok;
}

ExitInfo VfsLinux::setThumbnail([[maybe_unused]] const SyncPath &filePath, [[maybe_unused]] const QPixmap &pixmap) {
    return ExitCode::Ok;
}

ExitInfo VfsLinux::setAppExcludeList() {
    return ExitCode::Ok;
}

ExitInfo VfsLinux::getFetchingAppList([[maybe_unused]] QHash<QString, QString> &appTable) {
    return ExitCode::Ok;
}

ExitInfo VfsLinux::getFetchingAppList([[maybe_unused]] AppTable &appTable) {
    return ExitCode::Ok;
}

void VfsLinux::exclude([[maybe_unused]] const SyncPath &path) {
    // Application exclusions are not handled on Linux yet.
}

bool VfsLinux::isExcluded([[maybe_unused]] const SyncPath &filePath) {
    return false;
}

bool VfsLinux::fileStatusChanged([[maybe_unused]] const SyncPath &systemFileName, [[maybe_unused]] SyncFileStatus fileStatus) {
    // No file manager integration on Linux yet.
    return true;
}

void VfsLinux::clearFileAttributes([[maybe_unused]] const SyncPath &path) {
    // No placeholder attributes to clear yet.
}

void VfsLinux::dehydrate([[maybe_unused]] const SyncPath &path) {
    // Not implemented yet.
}

void VfsLinux::hydrate([[maybe_unused]] const SyncPath &path) {
    // Not implemented yet.
}

void VfsLinux::cancelHydrate([[maybe_unused]] const SyncPath &path, [[maybe_unused]] const ExitInfo &exitInfo) {
    // Not implemented yet.
}

} // namespace KDC
