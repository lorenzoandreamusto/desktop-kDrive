#pragma once
#include <cstdint>
#include <filesystem>
#include <ostream>
#include <string>
namespace KDC {
using SyncDbId = int64_t;
enum class VirtualFileMode { Off, Win, Mac, Linux, EnumEnd };
using SyncPath = std::filesystem::path;
using NodeId = std::string;
enum class ExitCode { Ok, SystemError, LogicError, Unknown };
enum class ExitCause { Unknown, NotFound, FileAccessError, FileExists, NotPlaceHolder, UnableToStartVfs, OperationCanceled, InvalidArgument };
struct ExitInfo {
    ExitCode code{ExitCode::Unknown};
    ExitCause cause{ExitCause::Unknown};
    ExitInfo() = default;
    ExitInfo(ExitCode c, ExitCause ca = ExitCause::Unknown) : code(c), cause(ca) {}
    explicit operator bool() const { return code == ExitCode::Ok; }
};
enum class PinState { Inherited, AlwaysLocal, OnlineOnly, Unspecified, Unknown, EnumEnd };
}
namespace KDC { inline std::wostream &operator<<(std::wostream &os, const ExitInfo &) { return os << L"ExitInfo"; } }
