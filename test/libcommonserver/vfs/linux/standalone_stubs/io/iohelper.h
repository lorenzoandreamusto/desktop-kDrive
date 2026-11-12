#pragma once
#include "libcommon/utility/types.h"
#include <cerrno>
#include <string>
#include <string_view>
#include <sys/xattr.h>
#include <vector>
namespace KDC {
enum class IoError { Success, AttrNotFound, NoSuchFileOrDirectory, AccessDenied, ResultOutOfRange, Unknown };
namespace litesync_attrs {
static constexpr std::string_view status("user.kDrive.status");
static constexpr std::string_view pinState("user.kDrive.pinstate");
static constexpr std::string_view size("user.kDrive.size");
static constexpr std::string_view modificationTime("user.kDrive.mtime");
static constexpr std::string_view nodeId("user.kDrive.nodeid");
static constexpr std::string_view statusOnline("O");
static constexpr std::string_view statusOffline("F");
static constexpr std::string_view statusHydrating("H");
static constexpr std::string_view pinStatePinned("P");
static constexpr std::string_view pinStateUnpinned("U");
}
inline IoError errnoToIoError(const int error) {
    switch (error) {
        case ENODATA: return IoError::AttrNotFound;
        case ENOENT: return IoError::NoSuchFileOrDirectory;
        case EACCES:
        case EPERM: return IoError::AccessDenied;
        case ERANGE: return IoError::ResultOutOfRange;
        default: return IoError::Unknown;
    }
}
class IoHelper {
    public:
        static bool getXAttrValue(const SyncPath &path, const std::string_view attrName, std::string &value, IoError &ioError) {
            value.clear();
            ioError = IoError::Success;
            const std::string name(attrName);
            const ssize_t length = lgetxattr(path.c_str(), name.c_str(), nullptr, 0);
            if (length < 0) {
                const int error = errno;
                ioError = errnoToIoError(error);
                return error == ENODATA || error == ENOENT;
            }
            value.resize(static_cast<size_t>(length));
            if (length > 0 && lgetxattr(path.c_str(), name.c_str(), value.data(), value.size()) != length) {
                ioError = errnoToIoError(errno);
                return false;
            }
            return true;
        }
        static bool setXAttrValue(const SyncPath &path, const std::string_view attrName, const std::string_view value,
                                  IoError &ioError) {
            ioError = IoError::Success;
            const std::string name(attrName);
            if (lsetxattr(path.c_str(), name.c_str(), value.data(), value.size(), 0) < 0) {
                ioError = errnoToIoError(errno);
                return false;
            }
            return true;
        }
        static bool removeXAttrs(const SyncPath &path, const std::vector<std::string_view> &attrNames, IoError &ioError) {
            ioError = IoError::Success;
            for (const auto attrName: attrNames) {
                const std::string name(attrName);
                if (lremovexattr(path.c_str(), name.c_str()) < 0) {
                    const int error = errno;
                    if (error == ENODATA) continue;
                    ioError = errnoToIoError(error);
                    return false;
                }
            }
            return true;
        }
        static bool removeLiteSyncXAttrs(const SyncPath &path, IoError &ioError) {
            return removeXAttrs(path, {litesync_attrs::status, litesync_attrs::pinState, litesync_attrs::size,
                                       litesync_attrs::modificationTime, litesync_attrs::nodeId},
                                ioError);
        }
        static bool checkIfFileIsDehydrated(const SyncPath &path, bool &isDehydrated, IoError &ioError) {
            isDehydrated = false;
            std::string status;
            if (!getXAttrValue(path, litesync_attrs::status, status, ioError) || ioError != IoError::Success) return false;
            isDehydrated = status == litesync_attrs::statusOnline;
            return true;
        }
        static std::string statusXAttr(const bool isSyncing, const int16_t progress, const bool isHydrated) {
            return (isHydrated ? std::string(litesync_attrs::statusOffline)
                               : (isSyncing ? std::string(litesync_attrs::statusHydrating) + std::to_string(progress)
                                            : std::string(litesync_attrs::statusOnline)));
        }
};
}
