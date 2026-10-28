#include "output.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <system_error>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace renderer {

Output::Output(fs::path destination) : destination_(std::move(destination)) {}

Output::~Output() {
    // Explicit cleanup by the caller reports failures; this only covers exceptions.
    cleanup();
}

bool Output::create() {
    if (!directory_.empty()) {
        native_error_ = EINVAL;
        return false;
    }
    std::error_code ec;
    const auto status = fs::symlink_status(destination_, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        native_error_ = ec.value();
        return false;
    }
    if (fs::exists(status)) {
        collision_ = true;
        native_error_ = EEXIST;
        return false;
    }
    auto pattern = (destination_.parent_path() / ".gxps-job-XXXXXX").u8string();
    if (!g_mkdtemp_full(pattern.data(), 0700)) {
        native_error_ = errno;
        return false;
    }
    directory_ = fs::u8path(pattern);
    temporary_ = directory_ / "output.pdf";
    fd_ = g_open(temporary_.u8string().c_str(), O_WRONLY | O_CREAT | O_EXCL
#ifdef _WIN32
                 | O_BINARY
#endif
                 , 0600);
    if (fd_ == -1) {
        native_error_ = errno;
        return false;
    }
    return true;
}

bool Output::write(const unsigned char* data, std::size_t length) {
    if (fd_ < 0 || finished_) {
        native_error_ = EBADF;
        return false;
    }
    while (length) {
        const auto count = static_cast<unsigned int>(std::min(length, static_cast<std::size_t>(INT_MAX)));
#ifdef _WIN32
        const auto written = _write(fd_, data, count);
#else
        const auto written = ::write(fd_, data, count);
#endif
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            native_error_ = written < 0 ? errno : EIO;
            return false;
        }
        data += written;
        length -= static_cast<std::size_t>(written);
        bytes_ += static_cast<std::uint64_t>(written);
    }
    return true;
}

bool Output::finish() {
    if (fd_ < 0 || finished_) {
        native_error_ = EBADF;
        return false;
    }
#ifdef _WIN32
    const int sync_result = _commit(fd_);
#else
    const int sync_result = fsync(fd_);
#endif
    const int sync_error = errno;
#ifdef _WIN32
    const int close_result = _close(fd_);
#else
    const int close_result = close(fd_);
#endif
    fd_ = -1;
    if (sync_result != 0 || close_result != 0) {
        native_error_ = sync_result != 0 ? sync_error : errno;
        return false;
    }
    finished_ = true;
    return true;
}

bool Output::publish() {
    if (!finished_ || published_) {
        native_error_ = EINVAL;
        return false;
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary_.c_str(), destination_.c_str(), MOVEFILE_WRITE_THROUGH)) {
        native_error_ = static_cast<int>(GetLastError());
        collision_ = native_error_ == ERROR_ALREADY_EXISTS || native_error_ == ERROR_FILE_EXISTS;
        return false;
    }
#else
    // A hard link is an atomic no-replace operation on the same filesystem.
    if (link(temporary_.c_str(), destination_.c_str()) != 0) {
        native_error_ = errno;
        collision_ = errno == EEXIST;
        return false;
    }
#endif
    published_ = true;
    return true;
}

bool Output::cleanup() {
    bool ok = true;
    if (fd_ >= 0) {
#ifdef _WIN32
        ok = _close(fd_) == 0;
#else
        ok = close(fd_) == 0;
#endif
        if (!ok) {
            native_error_ = errno;
        }
        fd_ = -1;
    }
    std::error_code ec;
    if (!temporary_.empty()) {
        fs::remove(temporary_, ec);
        if (ec) {
            native_error_ = ec.value();
            ok = false;
        }
    }
    if (!directory_.empty()) {
        ec.clear();
        fs::remove(directory_, ec);
        if (ec) {
            native_error_ = ec.value();
            ok = false;
        }
    }
    return ok;
}

}
