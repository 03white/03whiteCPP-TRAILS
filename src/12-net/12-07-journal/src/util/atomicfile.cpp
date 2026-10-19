#include "util/atomicfile.h"

#include <algorithm>
#include <cerrno>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace util {
namespace {

std::filesystem::path makeTmpPath(const std::filesystem::path& target) {
    static std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream oss;
    oss << target.filename().string() << ".tmp." << std::hex << rng();
    return target.parent_path() / oss.str();
}

#ifdef _WIN32

[[noreturn]] void throwLastError(const char* what) {
    DWORD err = ::GetLastError();
    throw std::system_error(
        static_cast<int>(err), std::system_category(), what);
}

void writeAtomicImpl(const std::filesystem::path& target,
                     std::string_view data) {
    namespace fs = std::filesystem;

    fs::path dir = target.parent_path();
    if (dir.empty())
        dir = ".";

    fs::path tmp = makeTmpPath(target);

    HANDLE h = ::CreateFileW(tmp.wstring().c_str(),
                             GENERIC_WRITE,
                             0,
                             nullptr,
                             CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL,
                             nullptr);
    if (h == INVALID_HANDLE_VALUE)
        throwLastError("CreateFileW(tmp)");

    auto cleanup = [&] {
        ::CloseHandle(h);
        ::DeleteFileW(tmp.wstring().c_str());
    };

    const char* p = data.data();
    std::size_t left = data.size();
    while (left > 0) {
        DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(left, 1u << 30));
        DWORD written = 0;
        if (!::WriteFile(h, p, chunk, &written, nullptr)) {
            cleanup();
            throwLastError("WriteFile");
        }
        if (written == 0) {
            cleanup();
            throw std::runtime_error("WriteFile wrote 0 bytes");
        }
        p += written;
        left -= written;
    }

    if (!::FlushFileBuffers(h)) {
        cleanup();
        throwLastError("FlushFileBuffers");
    }

    if (!::CloseHandle(h)) {
        ::DeleteFileW(tmp.wstring().c_str());
        throwLastError("CloseHandle");
    }
    h = INVALID_HANDLE_VALUE;

    if (!::MoveFileExW(tmp.wstring().c_str(),
                       target.wstring().c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ::DeleteFileW(tmp.wstring().c_str());
        throwLastError("MoveFileExW");
    }
}

#else // POSIX

void writeAtomicImpl(const std::filesystem::path& target,
                     std::string_view data) {
    namespace fs = std::filesystem;

    fs::path dir = target.parent_path();
    if (dir.empty())
        dir = ".";

    fs::path tmp = makeTmpPath(target);

    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "open(tmp)");
    }

    auto cleanup = [&] {
        ::close(fd);
        ::unlink(tmp.c_str());
    };

    const char* p = data.data();
    std::size_t left = data.size();
    while (left > 0) {
        ssize_t n = ::write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            cleanup();
            throw std::system_error(
                errno, std::generic_category(), "write(tmp)");
        }
        p += n;
        left -= static_cast<std::size_t>(n);
    }

#ifdef __APPLE__
    if (::fcntl(fd, F_FULLFSYNC) != 0) {
        cleanup();
        throw std::system_error(errno, std::generic_category(), "F_FULLFSYNC");
    }
#else
    if (::fsync(fd) != 0) {
        cleanup();
        throw std::system_error(errno, std::generic_category(), "fsync(tmp)");
    }
#endif

    if (::close(fd) != 0) {
        ::unlink(tmp.c_str());
        throw std::system_error(errno, std::generic_category(), "close(tmp)");
    }
    fd = -1;

    if (::rename(tmp.c_str(), target.c_str()) != 0) {
        ::unlink(tmp.c_str());
        throw std::system_error(errno, std::generic_category(), "rename");
    }

    // 让 rename 本身也持久化
    int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
        ::fsync(dfd);
        ::close(dfd);
    }
}

#endif // _WIN32

} // namespace

void writeAtomic(const std::filesystem::path& target, std::string_view data) {
    writeAtomicImpl(target, data);
}

} // namespace util
