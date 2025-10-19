#include "../../include/io/fd_manager.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <mutex>

namespace modern_coro {

FdContext::FdContext(int fd) : fd_(fd) {
    init();
}

FdContext::~FdContext() {
    close();
}

bool FdContext::init() {
    if (is_init_) {
        return true;
    }

    struct stat fd_stat;
    if (fstat(fd_, &fd_stat) == -1) {
        is_init_ = false;
        is_socket_ = false;
        return false;
    }

    if (S_ISSOCK(fd_stat.st_mode)) {
        is_socket_ = true;
    }

    // 获取当前文件描述符的标志
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags != -1) {
        sys_nonblock_ = (flags & O_NONBLOCK) != 0;
    }

    is_init_ = true;
    return true;
}

void FdContext::set_timeout(int type, uint64_t timeout_ms) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (type == SO_RCVTIMEO) {
        recv_timeout_ = timeout_ms;
    } else if (type == SO_SNDTIMEO) {
        send_timeout_ = timeout_ms;
    }
}

uint64_t FdContext::get_timeout(int type) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (type == SO_RCVTIMEO) {
        return recv_timeout_;
    } else if (type == SO_SNDTIMEO) {
        return send_timeout_;
    }
    return static_cast<uint64_t>(-1);
}

void FdContext::close() {
    is_closed_.store(true);
}

FdManager& FdManager::instance() {
    static FdManager instance;
    return instance;
}

std::shared_ptr<FdContext> FdManager::get(int fd, bool auto_create) {
    if (fd < 0) {
        return nullptr;
    }

    std::shared_lock<std::shared_mutex> read_lock(mutex_);
    if (static_cast<size_t>(fd) < fd_contexts_.size() && fd_contexts_[fd]) {
        return fd_contexts_[fd];
    }
    read_lock.unlock();

    if (!auto_create) {
        return nullptr;
    }

    std::unique_lock<std::shared_mutex> write_lock(mutex_);
    if (static_cast<size_t>(fd) >= fd_contexts_.size()) {
        fd_contexts_.resize(fd + 1);
    }

    if (!fd_contexts_[fd]) {
        fd_contexts_[fd] = std::make_shared<FdContext>(fd);
    }

    return fd_contexts_[fd];
}

void FdManager::del(int fd) {
    if (fd < 0) {
        return;
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (static_cast<size_t>(fd) < fd_contexts_.size()) {
        fd_contexts_[fd].reset();
    }
}

} // namespace modern_coro