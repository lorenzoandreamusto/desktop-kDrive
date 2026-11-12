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

#include "fusesession.h"

#include "io/iohelper.h"
#include "libcommonserver/utility/utility.h"
#include "log/log.h"
#include "placeholdermetadata.h"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>

#include <dirent.h>
#include <fcntl.h>
#include <linux/magic.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <sys/xattr.h>
#include <unistd.h>

// Recent kernel headers expose FUSE_SUPER_MAGIC, but the older linux-libc-dev
// shipped by some distributions (for example Ubuntu 22.04 with Linux 5.15)
// does not. The value is part of the stable kernel ABI.
#ifndef FUSE_SUPER_MAGIC
#define FUSE_SUPER_MAGIC 0x65735546
#endif

namespace KDC {

FuseSession::FuseSession(SyncPath sourceRoot, SyncPath mountPoint, log4cplus::Logger logger, HydrationHandler hydrationHandler) :
    _sourceRoot(std::move(sourceRoot)),
    _mountPoint(std::move(mountPoint)),
    _logger(logger),
    _hydrationHandler(std::move(hydrationHandler)) {}

FuseSession::~FuseSession() {
    stop();
}

void FuseSession::cleanupStaleMountPoint() {
    struct statfs statfsBuffer {};
    if (statfs(_mountPoint.c_str(), &statfsBuffer) != 0) {
        return;
    }
    if (statfsBuffer.f_type != FUSE_SUPER_MAGIC) {
        return;
    }

    // A previous server instance was killed while the filesystem was mounted: detach it before mounting again.
    LOG_WARN(_logger, "Removing a stale FUSE mount at " << _mountPoint.string());
    if (umount2(_mountPoint.c_str(), MNT_DETACH) != 0) {
        LOG_WARN(_logger, "Unable to remove the stale FUSE mount: " << std::strerror(errno));
    }
}

FuseSession *FuseSession::self() {
    return static_cast<FuseSession *>(fuse_get_context()->private_data);
}

struct fuse_operations FuseSession::buildOperations() {
    struct fuse_operations operations {};

    operations.getattr = cbGetattr;
    operations.readdir = cbReaddir;
    operations.open = cbOpen;
    operations.read = cbRead;
    operations.write = cbWrite;
    operations.release = cbRelease;
    operations.flush = cbFlush;
    operations.fsync = cbFsync;
    operations.create = cbCreate;
    operations.truncate = cbTruncate;
    operations.unlink = cbUnlink;
    operations.rename = cbRename;
    operations.mkdir = cbMkdir;
    operations.rmdir = cbRmdir;
    operations.readlink = cbReadlink;
    operations.symlink = cbSymlink;
    operations.link = cbLink;
    operations.chmod = cbChmod;
    operations.chown = cbChown;
    operations.utimens = cbUtimens;
    operations.statfs = cbStatfs;
    operations.access = cbAccess;
    operations.getxattr = cbGetxattr;
    operations.setxattr = cbSetxattr;
    operations.listxattr = cbListxattr;
    operations.removexattr = cbRemovexattr;

    return operations;
}

bool FuseSession::start(std::string &error) {
    if (_fuse) {
        return true;
    }

    cleanupStaleMountPoint();

    std::error_code ec;
    std::filesystem::create_directories(_mountPoint, ec);
    if (ec) {
        error = "Unable to create the FUSE mount point: " + ec.message();
        return false;
    }

    if (!std::filesystem::is_directory(_sourceRoot)) {
        error = "The sync folder does not exist";
        return false;
    }

    struct fuse_args args = FUSE_ARGS_INIT(0, nullptr);
    fuse_opt_add_arg(&args, "kdrive-vfs");
    fuse_opt_add_arg(&args, "-o");
    fuse_opt_add_arg(&args, "fsname=kDrive");
    fuse_opt_add_arg(&args, "-o");
    fuse_opt_add_arg(&args, "default_permissions");

    const struct fuse_operations operations = buildOperations();
    _fuse = fuse_new(&args, &operations, sizeof(operations), this);
    fuse_opt_free_args(&args);

    if (!_fuse) {
        error = "fuse_new failed";
        return false;
    }

    if (fuse_mount(_fuse, _mountPoint.c_str()) != 0) {
        error = "fuse_mount failed";
        fuse_destroy(_fuse);
        _fuse = nullptr;
        return false;
    }

    _stopping = false;
    _mounted = true;

    try {
        _loopThread = std::thread([this]() {
            // Multi-threaded loop: a blocking open on a placeholder must not freeze the whole mount, and
            // concurrent opens of the same node need to reach the single-flight hydration state.
            const int result = fuse_loop_mt(_fuse, 0);
            if (result != 0 && !_stopping.load()) {
                LOG_ERROR(_logger, "FUSE loop stopped with error " << result);
            }
            _mounted = false;
        });
    } catch (const std::exception &exception) {
        error = std::string("Unable to start the FUSE thread: ") + exception.what();
        fuse_unmount(_fuse);
        fuse_destroy(_fuse);
        _fuse = nullptr;
        _mounted = false;
        return false;
    }

    return true;
}

void FuseSession::stop() {
    _stopping = true;

    if (_fuse) {
        if (_loopThread.joinable()) {
            fuse_exit(_fuse);
            _loopThread.join();
        }
        fuse_unmount(_fuse);
        fuse_destroy(_fuse);
        _fuse = nullptr;
    }

    _mounted = false;

    std::error_code ec;
    std::filesystem::remove(_mountPoint, ec);
}

std::optional<SyncPath> FuseSession::resolve(const char *path) const {
    if (!path || path[0] != '/') {
        return std::nullopt;
    }

    const std::filesystem::path relativePath(path + 1);
    for (const auto &part: relativePath) {
        if (part == "..") {
            return std::nullopt;
        }
    }

    if (relativePath.empty()) {
        return _sourceRoot;
    }
    return _sourceRoot / relativePath;
}

int FuseSession::cbGetattr(const char *path, struct stat *statBuffer, struct fuse_file_info *) {
    FuseSession *session = self();
    return session ? session->doGetattr(path, statBuffer) : -ENOENT;
}

int FuseSession::cbReaddir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *,
                           enum fuse_readdir_flags) {
    FuseSession *session = self();
    return session ? session->doReaddir(path, buffer, filler, offset) : -ENOENT;
}

int FuseSession::cbOpen(const char *path, struct fuse_file_info *fileInfo) {
    FuseSession *session = self();
    return session ? session->doOpen(path, fileInfo) : -ENOENT;
}

int FuseSession::cbRead(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fileInfo) {
    FuseSession *session = self();
    return session ? session->doRead(path, buffer, size, offset, fileInfo) : -ENOENT;
}

int FuseSession::cbWrite(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fileInfo) {
    FuseSession *session = self();
    return session ? session->doWrite(path, buffer, size, offset, fileInfo) : -ENOENT;
}

int FuseSession::cbRelease(const char *, struct fuse_file_info *fileInfo) {
    FuseSession *session = self();
    return session ? session->doRelease(fileInfo) : 0;
}

int FuseSession::cbFlush(const char *, struct fuse_file_info *fileInfo) {
    FuseSession *session = self();
    return session ? session->doFlush(fileInfo) : 0;
}

int FuseSession::cbFsync(const char *, int datasync, struct fuse_file_info *fileInfo) {
    FuseSession *session = self();
    return session ? session->doFsync(fileInfo, datasync) : 0;
}

int FuseSession::cbCreate(const char *path, mode_t mode, struct fuse_file_info *fileInfo) {
    FuseSession *session = self();
    return session ? session->doCreate(path, mode, fileInfo) : -ENOENT;
}

int FuseSession::cbTruncate(const char *path, off_t size, struct fuse_file_info *) {
    FuseSession *session = self();
    return session ? session->doTruncate(path, size) : -ENOENT;
}

int FuseSession::cbUnlink(const char *path) {
    FuseSession *session = self();
    return session ? session->doUnlink(path) : -ENOENT;
}

int FuseSession::cbRename(const char *from, const char *to, unsigned int) {
    FuseSession *session = self();
    return session ? session->doRename(from, to) : -ENOENT;
}

int FuseSession::cbMkdir(const char *path, mode_t mode) {
    FuseSession *session = self();
    return session ? session->doMkdir(path, mode) : -ENOENT;
}

int FuseSession::cbRmdir(const char *path) {
    FuseSession *session = self();
    return session ? session->doRmdir(path) : -ENOENT;
}

int FuseSession::cbReadlink(const char *path, char *buffer, size_t size) {
    FuseSession *session = self();
    return session ? session->doReadlink(path, buffer, size) : -ENOENT;
}

int FuseSession::cbSymlink(const char *target, const char *linkPath) {
    FuseSession *session = self();
    return session ? session->doSymlink(target, linkPath) : -ENOENT;
}

int FuseSession::cbLink(const char *from, const char *to) {
    FuseSession *session = self();
    return session ? session->doLink(from, to) : -ENOENT;
}

int FuseSession::cbChmod(const char *path, mode_t mode, struct fuse_file_info *) {
    FuseSession *session = self();
    return session ? session->doChmod(path, mode) : -ENOENT;
}

int FuseSession::cbChown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *) {
    FuseSession *session = self();
    return session ? session->doChown(path, uid, gid) : -ENOENT;
}

int FuseSession::cbUtimens(const char *path, const struct timespec times[2], struct fuse_file_info *) {
    FuseSession *session = self();
    return session ? session->doUtimens(path, times) : -ENOENT;
}

int FuseSession::cbStatfs(const char *path, struct statvfs *stat) {
    FuseSession *session = self();
    return session ? session->doStatfs(path, stat) : -ENOENT;
}

int FuseSession::cbAccess(const char *path, int mask) {
    FuseSession *session = self();
    return session ? session->doAccess(path, mask) : -ENOENT;
}

int FuseSession::cbGetxattr(const char *path, const char *name, char *value, size_t size) {
    FuseSession *session = self();
    return session ? session->doGetxattr(path, name, value, size) : -ENOENT;
}

int FuseSession::cbSetxattr(const char *path, const char *name, const char *value, size_t size, int flags) {
    FuseSession *session = self();
    return session ? session->doSetxattr(path, name, value, size, flags) : -ENOENT;
}

int FuseSession::cbListxattr(const char *path, char *list, size_t size) {
    FuseSession *session = self();
    return session ? session->doListxattr(path, list, size) : -ENOENT;
}

int FuseSession::cbRemovexattr(const char *path, const char *name) {
    FuseSession *session = self();
    return session ? session->doRemovexattr(path, name) : -ENOENT;
}

int FuseSession::doGetattr(const char *path, struct stat *statBuffer) {
    const auto realPath = resolve(path);
    if (!realPath) {
        return -ENOENT;
    }

    struct stat fileStat {};
    if (lstat(realPath->c_str(), &fileStat) != 0) {
        return -errno;
    }

    if (S_ISREG(fileStat.st_mode)) {
        PlaceholderInfo info;
        if (const ExitInfo exitInfo = PlaceholderMetadata::read(*realPath, info);
            exitInfo && info.isPlaceholder && !info.isHydrated) {
            fileStat.st_size = static_cast<off_t>(info.size);
            fileStat.st_blocks = 0;
        }
    }

    fileStat.st_ino = (path[0] == '/' && path[1] == '\0') ? NodeMap::rootInode : _nodeMap.inodeFor(*realPath);
    *statBuffer = fileStat;
    return 0;
}

int FuseSession::doReaddir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t) {
    const auto realPath = resolve(path);
    if (!realPath) {
        return -ENOENT;
    }

    DIR *directory = opendir(realPath->c_str());
    if (!directory) {
        return -errno;
    }

    while (const struct dirent *entry = readdir(directory)) {
        struct stat fileStat {};
        const bool hasStat = fstatat(dirfd(directory), entry->d_name, &fileStat, AT_SYMLINK_NOFOLLOW) == 0;
        if (hasStat) {
            fileStat.st_ino = _nodeMap.inodeFor(*realPath / entry->d_name);
        }

        if (filler(buffer, entry->d_name, hasStat ? &fileStat : nullptr, 0, static_cast<enum fuse_fill_dir_flags>(0)) != 0) {
            break;
        }
    }

    closedir(directory);
    return 0;
}

int FuseSession::doOpen(const char *path, struct fuse_file_info *fileInfo) {
    const auto realPath = resolve(path);
    if (!realPath || !fileInfo) {
        return -ENOENT;
    }

    bool isDehydrated = false;
    IoError ioError = IoError::Success;
    if (IoHelper::checkIfFileIsDehydrated(*realPath, isDehydrated, ioError) && isDehydrated) {
        if (!_hydrationHandler) {
            return -EIO;
        }

        const ExitInfo hydrationExitInfo = _hydrationHandler(*realPath);
        if (!hydrationExitInfo) {
            LOGW_WARN(_logger, L"Unable to hydrate " << Utility::formatSyncPath(*realPath) << L": " << hydrationExitInfo);
            return -EIO;
        }

        // The file is expected to be hydrated now; never expose a partially hydrated file to the caller.
        if (!IoHelper::checkIfFileIsDehydrated(*realPath, isDehydrated, ioError) || isDehydrated) {
            return -EIO;
        }
    }

    const int fileDescriptor = open(realPath->c_str(), fileInfo->flags);
    if (fileDescriptor < 0) {
        return -errno;
    }

    fileInfo->fh = static_cast<uint64_t>(fileDescriptor);
    return 0;
}

int FuseSession::doRead(const char *, char *buffer, size_t size, off_t offset, struct fuse_file_info *fileInfo) {
    if (!fileInfo) {
        return -EINVAL;
    }

    const ssize_t bytesRead = pread(static_cast<int>(fileInfo->fh), buffer, size, offset);
    return bytesRead < 0 ? -errno : static_cast<int>(bytesRead);
}

int FuseSession::doWrite(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fileInfo) {
    if (!fileInfo) {
        return -EINVAL;
    }

    const ssize_t bytesWritten = pwrite(static_cast<int>(fileInfo->fh), buffer, size, offset);
    if (bytesWritten < 0) {
        return -errno;
    }

    if (const auto realPath = resolve(path)) {
        (void) PlaceholderMetadata::setStatus(*realPath, true);

        struct stat fileStat {};
        if (fstat(static_cast<int>(fileInfo->fh), &fileStat) == 0) {
            (void) PlaceholderMetadata::setSize(*realPath, static_cast<int64_t>(fileStat.st_size));
        }
    }

    return static_cast<int>(bytesWritten);
}

int FuseSession::doRelease(struct fuse_file_info *fileInfo) {
    if (fileInfo) {
        close(static_cast<int>(fileInfo->fh));
    }
    return 0;
}

int FuseSession::doFlush(struct fuse_file_info *) {
    return 0;
}

int FuseSession::doFsync(struct fuse_file_info *fileInfo, int) {
    if (!fileInfo) {
        return -EINVAL;
    }
    return fsync(static_cast<int>(fileInfo->fh)) == 0 ? 0 : -errno;
}

int FuseSession::doCreate(const char *path, mode_t mode, struct fuse_file_info *fileInfo) {
    const auto realPath = resolve(path);
    if (!realPath || !fileInfo) {
        return -ENOENT;
    }

    const int fileDescriptor = open(realPath->c_str(), O_CREAT | O_WRONLY | O_TRUNC | fileInfo->flags, mode);
    if (fileDescriptor < 0) {
        return -errno;
    }

    fileInfo->fh = static_cast<uint64_t>(fileDescriptor);
    (void) PlaceholderMetadata::write(*realPath, 0, std::time(nullptr), {}, true);
    return 0;
}

int FuseSession::doTruncate(const char *path, off_t size) {
    const auto realPath = resolve(path);
    if (!realPath) {
        return -ENOENT;
    }

    if (truncate(realPath->c_str(), size) != 0) {
        return -errno;
    }

    (void) PlaceholderMetadata::setSize(*realPath, static_cast<int64_t>(size));
    (void) PlaceholderMetadata::setStatus(*realPath, true);
    return 0;
}

int FuseSession::doUnlink(const char *path) {
    const auto realPath = resolve(path);
    if (!realPath) {
        return -ENOENT;
    }

    if (unlink(realPath->c_str()) != 0) {
        return -errno;
    }

    _nodeMap.erase(*realPath);
    return 0;
}

int FuseSession::doRename(const char *from, const char *to) {
    const auto fromPath = resolve(from);
    const auto toPath = resolve(to);
    if (!fromPath || !toPath) {
        return -ENOENT;
    }
    if (*fromPath == *toPath) {
        return 0;
    }

    if (rename(fromPath->c_str(), toPath->c_str()) != 0) {
        return -errno;
    }

    // POSIX rename replaces an existing destination: drop its previous inode assignment first.
    _nodeMap.erase(*toPath);
    _nodeMap.rename(*fromPath, *toPath);
    return 0;
}

int FuseSession::doMkdir(const char *path, mode_t mode) {
    const auto realPath = resolve(path);
    if (!realPath) {
        return -ENOENT;
    }

    return mkdir(realPath->c_str(), mode) == 0 ? 0 : -errno;
}

int FuseSession::doRmdir(const char *path) {
    const auto realPath = resolve(path);
    if (!realPath) {
        return -ENOENT;
    }

    if (rmdir(realPath->c_str()) != 0) {
        return -errno;
    }

    _nodeMap.erase(*realPath);
    return 0;
}

int FuseSession::doReadlink(const char *path, char *buffer, size_t size) {
    const auto realPath = resolve(path);
    if (!realPath) {
        return -ENOENT;
    }

    const ssize_t length = readlink(realPath->c_str(), buffer, size > 0 ? size - 1 : 0);
    if (length < 0) {
        return -errno;
    }

    if (size > 0) {
        buffer[length] = '\0';
    }
    return 0;
}

int FuseSession::doSymlink(const char *target, const char *linkPath) {
    const auto realPath = resolve(linkPath);
    if (!realPath) {
        return -ENOENT;
    }

    return symlink(target, realPath->c_str()) == 0 ? 0 : -errno;
}

int FuseSession::doLink(const char *from, const char *to) {
    const auto fromPath = resolve(from);
    const auto toPath = resolve(to);
    if (!fromPath || !toPath) {
        return -ENOENT;
    }

    return link(fromPath->c_str(), toPath->c_str()) == 0 ? 0 : -errno;
}

int FuseSession::doChmod(const char *path, mode_t mode) {
    const auto realPath = resolve(path);
    if (!realPath) {
        return -ENOENT;
    }

    return chmod(realPath->c_str(), mode) == 0 ? 0 : -errno;
}

int FuseSession::doChown(const char *path, uid_t uid, gid_t gid) {
    const auto realPath = resolve(path);
    if (!realPath) {
        return -ENOENT;
    }

    return lchown(realPath->c_str(), uid, gid) == 0 ? 0 : -errno;
}

int FuseSession::doUtimens(const char *path, const struct timespec times[2]) {
    const auto realPath = resolve(path);
    if (!realPath) {
        return -ENOENT;
    }

    return utimensat(AT_FDCWD, realPath->c_str(), times, AT_SYMLINK_NOFOLLOW) == 0 ? 0 : -errno;
}

int FuseSession::doStatfs(const char *path, struct statvfs *stat) {
    const auto realPath = resolve(path);
    if (!realPath) {
        return -ENOENT;
    }

    return statvfs(realPath->c_str(), stat) == 0 ? 0 : -errno;
}

int FuseSession::doAccess(const char *path, int mask) {
    const auto realPath = resolve(path);
    if (!realPath) {
        return -ENOENT;
    }

    return access(realPath->c_str(), mask) == 0 ? 0 : -errno;
}

int FuseSession::doGetxattr(const char *path, const char *name, char *value, size_t size) {
    const auto realPath = resolve(path);
    if (!realPath) {
        return -ENOENT;
    }

    const ssize_t length = lgetxattr(realPath->c_str(), name, value, size);
    return length < 0 ? -errno : static_cast<int>(length);
}

int FuseSession::doSetxattr(const char *path, const char *name, const char *value, size_t size, int flags) {
    const auto realPath = resolve(path);
    if (!realPath) {
        return -ENOENT;
    }

    return lsetxattr(realPath->c_str(), name, value, size, flags) == 0 ? 0 : -errno;
}

int FuseSession::doListxattr(const char *path, char *list, size_t size) {
    const auto realPath = resolve(path);
    if (!realPath) {
        return -ENOENT;
    }

    const ssize_t length = llistxattr(realPath->c_str(), list, size);
    return length < 0 ? -errno : static_cast<int>(length);
}

int FuseSession::doRemovexattr(const char *path, const char *name) {
    const auto realPath = resolve(path);
    if (!realPath) {
        return -ENOENT;
    }

    return lremovexattr(realPath->c_str(), name) == 0 ? 0 : -errno;
}

} // namespace KDC
