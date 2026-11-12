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
#include "nodemap.h"

#include <log4cplus/logger.h>

#include <atomic>
#include <ctime>
#include <optional>
#include <string>
#include <thread>

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>

namespace KDC {

/// Owns a FUSE session that exposes the sync folder at a separate mount point.
///
/// The sync engine keeps working on the real folder: the mount is a read-through view that
/// reports the logical size of dehydrated placeholders, so applications no longer see an
/// empty file. Hydration on open is implemented by the next phase.
class FuseSession {
    public:
        FuseSession(SyncPath sourceRoot, SyncPath mountPoint, log4cplus::Logger logger);
        ~FuseSession();

        FuseSession(const FuseSession &) = delete;
        FuseSession &operator=(const FuseSession &) = delete;

        bool start(std::string &error);
        void stop();

        [[nodiscard]] bool isMounted() const { return _mounted.load(); }

    private:
        static FuseSession *self();
        static struct fuse_operations buildOperations();

        static int cbGetattr(const char *path, struct stat *statBuffer, struct fuse_file_info *fileInfo);
        static int cbReaddir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset,
                             struct fuse_file_info *fileInfo, enum fuse_readdir_flags flags);
        static int cbOpen(const char *path, struct fuse_file_info *fileInfo);
        static int cbRead(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fileInfo);
        static int cbWrite(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fileInfo);
        static int cbRelease(const char *path, struct fuse_file_info *fileInfo);
        static int cbFlush(const char *path, struct fuse_file_info *fileInfo);
        static int cbFsync(const char *path, int datasync, struct fuse_file_info *fileInfo);
        static int cbCreate(const char *path, mode_t mode, struct fuse_file_info *fileInfo);
        static int cbTruncate(const char *path, off_t size, struct fuse_file_info *fileInfo);
        static int cbUnlink(const char *path);
        static int cbRename(const char *from, const char *to, unsigned int flags);
        static int cbMkdir(const char *path, mode_t mode);
        static int cbRmdir(const char *path);
        static int cbReadlink(const char *path, char *buffer, size_t size);
        static int cbSymlink(const char *target, const char *linkPath);
        static int cbLink(const char *from, const char *to);
        static int cbChmod(const char *path, mode_t mode, struct fuse_file_info *fileInfo);
        static int cbChown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fileInfo);
        static int cbUtimens(const char *path, const struct timespec times[2], struct fuse_file_info *fileInfo);
        static int cbStatfs(const char *path, struct statvfs *stat);
        static int cbAccess(const char *path, int mask);
        static int cbGetxattr(const char *path, const char *name, char *value, size_t size);
        static int cbSetxattr(const char *path, const char *name, const char *value, size_t size, int flags);
        static int cbListxattr(const char *path, char *list, size_t size);
        static int cbRemovexattr(const char *path, const char *name);

        int doGetattr(const char *path, struct stat *statBuffer);
        int doReaddir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset);
        int doOpen(const char *path, struct fuse_file_info *fileInfo);
        int doRead(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fileInfo);
        int doWrite(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fileInfo);
        int doRelease(struct fuse_file_info *fileInfo);
        int doFlush(struct fuse_file_info *fileInfo);
        int doFsync(struct fuse_file_info *fileInfo, int datasync);
        int doCreate(const char *path, mode_t mode, struct fuse_file_info *fileInfo);
        int doTruncate(const char *path, off_t size);
        int doUnlink(const char *path);
        int doRename(const char *from, const char *to);
        int doMkdir(const char *path, mode_t mode);
        int doRmdir(const char *path);
        int doReadlink(const char *path, char *buffer, size_t size);
        int doSymlink(const char *target, const char *linkPath);
        int doLink(const char *from, const char *to);
        int doChmod(const char *path, mode_t mode);
        int doChown(const char *path, uid_t uid, gid_t gid);
        int doUtimens(const char *path, const struct timespec times[2]);
        int doStatfs(const char *path, struct statvfs *stat);
        int doAccess(const char *path, int mask);
        int doGetxattr(const char *path, const char *name, char *value, size_t size);
        int doSetxattr(const char *path, const char *name, const char *value, size_t size, int flags);
        int doListxattr(const char *path, char *list, size_t size);
        int doRemovexattr(const char *path, const char *name);

        [[nodiscard]] std::optional<SyncPath> resolve(const char *path) const;

        SyncPath _sourceRoot;
        SyncPath _mountPoint;
        log4cplus::Logger _logger;
        NodeMap _nodeMap;

        struct fuse *_fuse{nullptr};
        std::thread _loopThread;
        std::atomic<bool> _mounted{false};
        std::atomic<bool> _stopping{false};
};

} // namespace KDC
