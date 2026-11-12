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

#include "fusesession.h"
#include "placeholdermetadata.h"

#include "io/iohelper.h"
#include "libcommon/utility/utility.h"
#include "libcommonserver/log/log.h"
#include "libcommonserver/utility/utility.h"

#include <QStandardPaths>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace KDC {

namespace {

ExitInfo ioErrorToExitInfo(const IoError ioError) {
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

bool setFileTimes(const SyncPath &path, const time_t modificationTime) {
    const struct timespec times[2] = {{modificationTime, 0}, {modificationTime, 0}};
    return utimensat(AT_FDCWD, path.c_str(), times, AT_SYMLINK_NOFOLLOW) == 0;
}

} // namespace

VfsLinux::VfsLinux(const VfsSetupParams &vfsSetupParams, QObject *parent) :
    Vfs(vfsSetupParams, parent) {}

VfsLinux::~VfsLinux() = default;

bool VfsLinux::isRegistered() const {
    return _session && _session->isMounted();
}

ExitInfo VfsLinux::startImpl(bool &installationDone, bool &activationDone, bool &connectionDone) {
    if (_session) {
        return ExitCode::Ok;
    }

    if (access("/dev/fuse", R_OK | W_OK) != 0) {
        LOG_WARN(logger(), "FUSE device is not available or not accessible: " << std::strerror(errno));
        return {ExitCode::SystemError, ExitCause::UnableToStartVfs};
    }

    const QString runtimeDirectory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const SyncPath runtimePath =
            runtimeDirectory.isEmpty() ? std::filesystem::temp_directory_path() : SyncPath(runtimeDirectory.toStdString());
    _mountPoint = runtimePath / "kDrive" / ("vfs-" + std::to_string(_vfsSetupParams.syncDbId));

    std::error_code ec;
    std::filesystem::create_directories(_mountPoint, ec);
    if (ec) {
        // The runtime directory is not always available (for example in a session without logind):
        // fall back to the temporary directory.
        _mountPoint = std::filesystem::temp_directory_path() / "kDrive" / ("vfs-" + std::to_string(_vfsSetupParams.syncDbId));
        ec.clear();
        std::filesystem::create_directories(_mountPoint, ec);
        if (ec) {
            LOG_WARN(logger(), "Unable to create the FUSE mount point: " << ec.message());
            return {ExitCode::SystemError, ExitCause::UnableToStartVfs};
        }
    }

    _session = std::make_unique<FuseSession>(_vfsSetupParams.localPath, _mountPoint, logger(),
                                             [this](const SyncPath &path) { return hydrateAndWait(path); });

    std::string error;
    if (!_session->start(error)) {
        LOG_WARN(logger(), "Unable to start the Linux VFS: " << error);
        _session.reset();
        std::filesystem::remove(_mountPoint, ec);
        return {ExitCode::SystemError, ExitCause::UnableToStartVfs};
    }

    installationDone = true;
    activationDone = true;
    connectionDone = true;

    LOG_INFO(logger(), "Linux VFS mounted at " << _mountPoint.string() << " for sync " << _vfsSetupParams.syncDbId);
    return ExitCode::Ok;
}

void VfsLinux::stopImpl([[maybe_unused]] bool unregister) {
    // Unblock FUSE open calls waiting for a hydration before stopping the loop thread.
    {
        const std::scoped_lock lock(_hydrationMutex);
        for (auto &[_, state]: _hydrations) {
            const std::scoped_lock stateLock(state->mutex);
            state->completed = true;
            state->exitInfo = {ExitCode::SystemError, ExitCause::OperationCanceled};
        }
        for (auto &[_, state]: _hydrations) {
            state->cv.notify_all();
        }
    }

    if (_session) {
        _session->stop();
        _session.reset();
    }

    std::error_code ec;
    std::filesystem::remove(_mountPoint, ec);
}

SyncPath VfsLinux::absolutePath(const SyncPath &path) const {
    return path.is_absolute() ? path : _vfsSetupParams.localPath / path;
}

ExitInfo VfsLinux::updateMetadata([[maybe_unused]] const SyncPath &filePath, [[maybe_unused]] time_t creationTime,
                                  const time_t modtime, const int64_t size, const NodeId &fileId) {
    const SyncPath fullPath = absolutePath(filePath);
    if (const ExitInfo exitInfo = checkIfPathIsValid(fullPath, true); !exitInfo) {
        return exitInfo;
    }

    PlaceholderInfo info;
    if (const ExitInfo exitInfo = PlaceholderMetadata::read(fullPath, info); !exitInfo) {
        return exitInfo;
    }

    if (!info.isPlaceholder) {
        return ExitCode::Ok;
    }

    if (!info.isHydrated && std::filesystem::is_regular_file(fullPath)) {
        if (truncate(fullPath.c_str(), 0) != 0) {
            LOG_WARN(logger(), "Unable to truncate placeholder " << fullPath.string() << ": " << std::strerror(errno));
            return {ExitCode::SystemError, ExitCause::FileAccessError};
        }
    }

    if (const ExitInfo exitInfo = PlaceholderMetadata::setSize(fullPath, size); !exitInfo) {
        return exitInfo;
    }
    if (const ExitInfo exitInfo = PlaceholderMetadata::setModificationTime(fullPath, modtime); !exitInfo) {
        return exitInfo;
    }
    if (!fileId.empty()) {
        if (const ExitInfo exitInfo = PlaceholderMetadata::setNodeId(fullPath, fileId); !exitInfo) {
            return exitInfo;
        }
    }

    if (!setFileTimes(fullPath, modtime)) {
        LOG_WARN(logger(), "Unable to update placeholder times " << fullPath.string() << ": " << std::strerror(errno));
    }

    return ExitCode::Ok;
}

ExitInfo VfsLinux::createPlaceholder(const SyncPath &relativeLocalPath, const SyncFileItem &item) {
    if (relativeLocalPath.empty()) {
        return {ExitCode::LogicError, ExitCause::InvalidArgument};
    }

    const SyncPath fullPath = absolutePath(relativeLocalPath);
    if (const ExitInfo exitInfo = checkIfPathIsValid(fullPath, false); !exitInfo) {
        return exitInfo;
    }
    if (const ExitInfo exitInfo = checkIfPathIsValid(fullPath.parent_path(), true); !exitInfo) {
        return exitInfo;
    }

    if (item.type() == NodeType::Directory) {
        std::error_code ec;
        if (!std::filesystem::create_directory(fullPath, ec) && ec) {
            LOG_WARN(logger(), "Unable to create placeholder directory " << fullPath.string() << ": " << ec.message());
            return {ExitCode::SystemError, ExitCause::FileAccessError};
        }
    } else {
        const int fileDescriptor = open(fullPath.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (fileDescriptor < 0) {
            if (errno == EEXIST) {
                return {ExitCode::SystemError, ExitCause::FileExists};
            }
            LOG_WARN(logger(), "Unable to create placeholder " << fullPath.string() << ": " << std::strerror(errno));
            return handleVfsError(fullPath);
        }
        close(fileDescriptor);
    }

    const time_t modificationTime = static_cast<time_t>(item.modTime());
    if (const ExitInfo exitInfo =
                PlaceholderMetadata::write(fullPath, item.size(), modificationTime, item.remoteNodeId().value_or(""), false);
        !exitInfo) {
        return exitInfo;
    }

    if (!setFileTimes(fullPath, modificationTime)) {
        LOG_WARN(logger(), "Unable to set placeholder times " << fullPath.string() << ": " << std::strerror(errno));
    }

    return ExitCode::Ok;
}

ExitInfo VfsLinux::dehydratePlaceholder(const SyncPath &path) {
    const SyncPath fullPath = absolutePath(path);
    if (const ExitInfo exitInfo = checkIfPathIsValid(fullPath, true); !exitInfo) {
        return exitInfo;
    }

    PlaceholderInfo info;
    if (const ExitInfo exitInfo = PlaceholderMetadata::read(fullPath, info); !exitInfo) {
        return exitInfo;
    }

    if (!info.isPlaceholder) {
        return {ExitCode::SystemError, ExitCause::NotPlaceHolder};
    }

    if (info.isHydrated && std::filesystem::is_regular_file(fullPath) && truncate(fullPath.c_str(), 0) != 0) {
        LOG_WARN(logger(), "Unable to dehydrate " << fullPath.string() << ": " << std::strerror(errno));
        return {ExitCode::SystemError, ExitCause::FileAccessError};
    }

    return PlaceholderMetadata::setStatus(fullPath, false);
}

ExitInfo VfsLinux::convertToPlaceholder(const SyncPath &path, const SyncFileItem &item) {
    if (path.empty()) {
        return {ExitCode::LogicError, ExitCause::InvalidArgument};
    }

    const SyncPath fullPath = absolutePath(path);
    if (const ExitInfo exitInfo = checkIfPathIsValid(fullPath, true); !exitInfo) {
        return exitInfo;
    }

    const bool hydrated = !item.dehydrated();
    if (!hydrated && std::filesystem::is_regular_file(fullPath) && truncate(fullPath.c_str(), 0) != 0) {
        LOG_WARN(logger(), "Unable to truncate " << fullPath.string() << ": " << std::strerror(errno));
        return {ExitCode::SystemError, ExitCause::FileAccessError};
    }

    return PlaceholderMetadata::write(fullPath, item.size(), static_cast<time_t>(item.modTime()),
                                      item.remoteNodeId().value_or(""), hydrated);
}

ExitInfo VfsLinux::updateFetchStatus(const SyncPath &tmpPath, const SyncPath &path, [[maybe_unused]] int64_t received,
                                     bool &canceled, bool &finished) {
    (void) tmpPath;
    canceled = false;
    finished = false;

    const SyncPath fullPath = absolutePath(path);
    if (const ExitInfo exitInfo = checkIfPathIsValid(fullPath, true); !exitInfo) {
        return exitInfo;
    }

    PlaceholderInfo info;
    if (const ExitInfo exitInfo = PlaceholderMetadata::read(fullPath, info); !exitInfo) {
        return exitInfo;
    }

    int16_t progress = 0;
    if (info.size > 0) {
        progress = static_cast<int16_t>(std::clamp<int64_t>(received * 100 / info.size, 0, 100));
    }

    return PlaceholderMetadata::setStatus(fullPath, false, true, progress);
}

ExitInfo VfsLinux::updateFetchStatus(const SyncPath &path, const std::string &status) {
    const SyncPath fullPath = absolutePath(path);
    if (const ExitInfo exitInfo = checkIfPathIsValid(fullPath, true); !exitInfo) {
        return exitInfo;
    }

    IoError ioError = IoError::Success;
    if (!IoHelper::setXAttrValue(fullPath, litesync_attrs::status, status, ioError) || ioError != IoError::Success) {
        return ioErrorToExitInfo(ioError);
    }
    return ExitCode::Ok;
}

ExitInfo VfsLinux::forceStatus(const SyncPath &path, const VfsStatus &vfsStatus) {
    const SyncPath fullPath = absolutePath(path);
    if (const ExitInfo exitInfo = checkIfPathIsValid(fullPath, true); !exitInfo) {
        return exitInfo;
    }

    if (const ExitInfo exitInfo =
                PlaceholderMetadata::setStatus(fullPath, vfsStatus.isHydrated, vfsStatus.isSyncing, vfsStatus.progress);
        !exitInfo) {
        return exitInfo;
    }

    if (!vfsStatus.isHydrated) {
        return PlaceholderMetadata::setPinState(fullPath, PinState::OnlineOnly);
    }

    completeHydration(fullPath, ExitCode::Ok);
    return ExitCode::Ok;
}

ExitInfo VfsLinux::isDehydratedPlaceholder(const SyncPath &filePath, bool &isDehydrated) {
    const SyncPath fullPath = absolutePath(filePath);

    IoError ioError = IoError::Success;
    if (!IoHelper::checkIfFileIsDehydrated(fullPath, isDehydrated, ioError)) {
        return ioErrorToExitInfo(ioError);
    }
    if (ioError == IoError::NoSuchFileOrDirectory) {
        return {ExitCode::SystemError, ExitCause::NotFound};
    }
    if (ioError == IoError::AccessDenied) {
        return {ExitCode::SystemError, ExitCause::FileAccessError};
    }
    return ExitCode::Ok;
}

ExitInfo VfsLinux::setPinState(const SyncPath &fileRelativePath, const PinState state) {
    const SyncPath fullPath = absolutePath(fileRelativePath);
    if (const ExitInfo exitInfo = checkIfPathIsValid(fullPath, true); !exitInfo) {
        return exitInfo;
    }

    return PlaceholderMetadata::setPinState(fullPath, state);
}

PinState VfsLinux::pinState(const SyncPath &fileRelativePath) {
    const SyncPath fullPath = absolutePath(fileRelativePath);

    PlaceholderInfo info;
    if (const ExitInfo exitInfo = PlaceholderMetadata::read(fullPath, info); !exitInfo || !info.isPlaceholder) {
        return PinState::AlwaysLocal;
    }

    return info.pinState;
}

ExitInfo VfsLinux::status(const SyncPath &filePath, VfsStatus &vfsStatus) {
    const SyncPath fullPath = absolutePath(filePath);

    std::error_code ec;
    if (!std::filesystem::exists(fullPath, ec)) {
        return {ExitCode::SystemError, ExitCause::NotFound};
    }

    PlaceholderInfo info;
    if (const ExitInfo exitInfo = PlaceholderMetadata::read(fullPath, info); !exitInfo) {
        return exitInfo;
    }

    vfsStatus.isPlaceholder = info.isPlaceholder;
    vfsStatus.isHydrated = info.isHydrated;
    vfsStatus.isSyncing = info.isSyncing;
    vfsStatus.progress = info.progress;

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

void VfsLinux::clearFileAttributes(const SyncPath &path) {
    const SyncPath fullPath = absolutePath(path);
    if (const ExitInfo exitInfo = PlaceholderMetadata::remove(fullPath); !exitInfo) {
        LOGW_WARN(logger(), L"Unable to clear the placeholder attributes: " << Utility::formatSyncPath(fullPath));
    }
}

void VfsLinux::convertDirContentToPlaceholder(const QString &filePath, const bool isHydratedIn) {
    const SyncPath rootPath = QStr2Path(filePath);
    std::error_code ec;
    std::filesystem::recursive_directory_iterator iterator(rootPath, ec);
    const std::filesystem::recursive_directory_iterator end;
    if (ec) {
        LOGW_WARN(logger(), L"Unable to iterate the sync folder: " << Utility::formatStdError(rootPath, ec));
        return;
    }

    for (; iterator != end; iterator.increment(ec)) {
        if (ec) {
            LOGW_WARN(logger(), L"Unable to iterate the sync folder: " << Utility::formatStdError(rootPath, ec));
            return;
        }

        PlaceholderInfo info;
        if (const ExitInfo readExitInfo = PlaceholderMetadata::read(iterator->path(), info); readExitInfo && info.isPlaceholder) {
            continue;
        }

        std::error_code typeError;
        const bool isDirectory = iterator->is_directory(typeError);
        if (typeError) {
            continue;
        }
        const bool isRegularFile = !isDirectory && iterator->is_regular_file(typeError);
        if (typeError || (!isDirectory && !isRegularFile)) {
            continue;
        }

        struct stat fileStat {};
        if (stat(iterator->path().c_str(), &fileStat) != 0) {
            continue;
        }

        if (isRegularFile && !isHydratedIn && truncate(iterator->path().c_str(), 0) != 0) {
            LOGW_WARN(logger(),
                      L"Unable to truncate "
                              << Utility::formatSyncPath(iterator->path()) << L": "
                              << Utility::formatStdError(iterator->path(), std::error_code(errno, std::system_category())));
            continue;
        }

        const int64_t size = isDirectory ? 0 : static_cast<int64_t>(fileStat.st_size);
        if (const ExitInfo writeExitInfo =
                    PlaceholderMetadata::write(iterator->path(), size, fileStat.st_mtime, "", isHydratedIn);
            !writeExitInfo) {
            LOGW_WARN(logger(), L"Unable to convert " << Utility::formatSyncPath(iterator->path()) << L" to a placeholder: "
                                                      << writeExitInfo);
        }
    }
}

void VfsLinux::dehydrate(const SyncPath &path) {
    if (const ExitInfo exitInfo = dehydratePlaceholder(path); !exitInfo) {
        LOGW_WARN(logger(),
                  L"Unable to dehydrate placeholder " << Utility::formatSyncPath(absolutePath(path)) << L": " << exitInfo);
    }
}

bool VfsLinux::hydrateAllPlaceholders() {
    std::error_code ec;
    std::filesystem::recursive_directory_iterator iterator(_vfsSetupParams.localPath, ec);
    const std::filesystem::recursive_directory_iterator end;
    if (ec) {
        LOGW_WARN(logger(), L"Unable to iterate the sync folder: " << Utility::formatStdError(_vfsSetupParams.localPath, ec));
        return false;
    }

    for (; iterator != end; iterator.increment(ec)) {
        if (ec) {
            LOGW_WARN(logger(), L"Unable to iterate the sync folder: " << Utility::formatStdError(_vfsSetupParams.localPath, ec));
            return false;
        }

        std::error_code typeError;
        if (!iterator->is_regular_file(typeError) || typeError) {
            continue;
        }

        PlaceholderInfo info;
        if (const ExitInfo exitInfo = PlaceholderMetadata::read(iterator->path(), info); !exitInfo) {
            return false;
        }
        if (!info.isPlaceholder || info.isHydrated) {
            continue;
        }

        if (const ExitInfo exitInfo = hydrateAndWait(iterator->path()); !exitInfo) {
            LOGW_WARN(logger(), L"Unable to hydrate " << Utility::formatSyncPath(iterator->path()) << L": " << exitInfo);
            return false;
        }
    }

    return true;
}

ExitInfo VfsLinux::hydrateAndWait(const SyncPath &path) {
    const SyncPath fullPath = absolutePath(path);

    PlaceholderInfo info;
    if (const ExitInfo exitInfo = PlaceholderMetadata::read(fullPath, info); !exitInfo) {
        return exitInfo;
    }
    if (!info.isPlaceholder || info.isHydrated) {
        return ExitCode::Ok;
    }

    const std::string key = fullPath.lexically_normal().native();
    std::shared_ptr<HydrationState> state;
    bool startRequest = false;
    {
        const std::scoped_lock lock(_hydrationMutex);
        auto &entry = _hydrations[key];
        if (!entry) {
            entry = std::make_shared<HydrationState>();
        }
        state = entry;
        if (!state->started) {
            state->started = true;
            startRequest = true;
        }
    }

    if (startRequest) {
        (void) PlaceholderMetadata::setStatus(fullPath, false, true, 0);

        ExitInfo requestExitInfo{ExitCode::LogicError, ExitCause::Unknown};
        if (_hydrationRequest) {
            requestExitInfo =
                    _hydrationRequest(_vfsSetupParams.syncDbId, CommonUtility::relativePath(_vfsSetupParams.localPath, fullPath),
                                      fullPath, fullPath.parent_path());
        }
        if (!requestExitInfo) {
            completeHydration(fullPath, requestExitInfo);
        }
    }

    ExitInfo result;
    {
        std::unique_lock lock(state->mutex);
        if (!state->cv.wait_for(lock, std::chrono::minutes(10), [&state] { return state->completed; })) {
            state->completed = true;
            state->exitInfo = {ExitCode::SystemError, ExitCause::Unknown};
        }
        result = state->exitInfo;
    }

    {
        const std::scoped_lock lock(_hydrationMutex);
        const auto it = _hydrations.find(key);
        if (it != _hydrations.end() && it->second == state && state->completed) {
            _hydrations.erase(it);
        }
    }

    return result;
}

void VfsLinux::completeHydration(const SyncPath &absolutePath, const ExitInfo &exitInfo) {
    const std::string key = absolutePath.lexically_normal().native();
    std::shared_ptr<HydrationState> state;
    {
        const std::scoped_lock lock(_hydrationMutex);
        const auto it = _hydrations.find(key);
        if (it == _hydrations.end()) {
            return;
        }
        state = it->second;
    }

    {
        const std::scoped_lock lock(state->mutex);
        state->completed = true;
        state->exitInfo = exitInfo;
    }
    state->cv.notify_all();
}

void VfsLinux::hydrate(const SyncPath &path) {
    if (const ExitInfo exitInfo = hydrateAndWait(path); !exitInfo) {
        LOGW_WARN(logger(), L"Unable to hydrate " << Utility::formatSyncPath(absolutePath(path)) << L": " << exitInfo);
    }
}

void VfsLinux::cancelHydrate(const SyncPath &path, const ExitInfo &exitInfo) {
    const SyncPath fullPath = absolutePath(path);
    (void) PlaceholderMetadata::setStatus(fullPath, false);
    completeHydration(fullPath, exitInfo);
}

} // namespace KDC
