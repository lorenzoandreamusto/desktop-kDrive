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

#include "libcommonserver/vfs/plugin.h"
#include "libcommonserver/vfs/vfs.h"

#include <QObject>

namespace KDC {

/// Linux VFS skeleton.
///
/// This plugin only advertises the Linux VFS mode and rejects activation: the FUSE
/// implementation lands in the next POC phase. Keeping the plugin loadable lets the
/// plugin discovery, mode selection and persistence paths be validated independently.
class VfsLinux : public Vfs {
        Q_OBJECT
        Q_INTERFACES(KDC::Vfs)

    public:
        explicit VfsLinux(const VfsSetupParams &vfsSetupParams, QObject *parent = nullptr);

        VirtualFileMode mode() const override { return VirtualFileMode::Linux; }

        bool showPinStateActions() const override { return false; }

        ExitInfo updateMetadata(const SyncPath &filePath, time_t creationTime, time_t modtime, int64_t size,
                                const NodeId &fileId) override;

        ExitInfo createPlaceholder(const SyncPath &relativeLocalPath, const SyncFileItem &item) override;
        ExitInfo dehydratePlaceholder(const SyncPath &path) override;
        ExitInfo convertToPlaceholder(const SyncPath &path, const SyncFileItem &item) override;
        ExitInfo updateFetchStatus(const SyncPath &tmpPath, const SyncPath &path, int64_t received, bool &canceled,
                                   bool &finished) override;
        ExitInfo updateFetchStatus(const SyncPath &path, const std::string &status) override;
        ExitInfo forceStatus(const SyncPath &path, const VfsStatus &vfsStatus) override;

        ExitInfo isDehydratedPlaceholder(const SyncPath &filePath, bool &isDehydrated) override;

        ExitInfo setPinState(const SyncPath &fileRelativePath, PinState state) override;
        PinState pinState(const SyncPath &fileRelativePath) override;
        ExitInfo status(const SyncPath &filePath, VfsStatus &vfsStatus) override;
        ExitInfo setThumbnail(const SyncPath &filePath, const QPixmap &pixmap) override;
        ExitInfo setAppExcludeList() override;
        ExitInfo getFetchingAppList(QHash<QString, QString> &appTable) override;
        ExitInfo getFetchingAppList(AppTable &appTable) override;

        bool isExcluded(const SyncPath &filePath) override;
        bool fileStatusChanged(const SyncPath &systemFileName, SyncFileStatus fileStatus) override;
        void clearFileAttributes(const SyncPath &path) override;

        void dehydrate(const SyncPath &path) override;
        void hydrate(const SyncPath &path) override;
        void cancelHydrate(const SyncPath &path, const ExitInfo &exitInfo = ExitCode::Unknown) override;

    protected:
        ExitInfo startImpl(bool &installationDone, bool &activationDone, bool &connectionDone) override;
        void stopImpl(bool unregister) override;

    private:
        void exclude(const SyncPath &path) override;
};

class LinuxVfsPluginFactory : public QObject, public DefaultPluginFactory<VfsLinux> {
        Q_OBJECT
        Q_PLUGIN_METADATA(IID "org.kdrive.PluginFactory" FILE "../vfspluginmetadata.json")
        Q_INTERFACES(KDC::PluginFactory)
};

} // namespace KDC
