#include "EngineProcess.hpp"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {

// Writing to a pipe whose reader died raises SIGPIPE. Ignore it so write fails with EPIPE.
void IgnoreSigpipeOnce() {
    static std::once_flag flag;
    std::call_once(flag, [] { std::signal(SIGPIPE, SIG_IGN); });
}

void CloseIfOpen(int &fd) {
    if(fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

} // namespace

EngineProcess::EngineProcess(const std::string &path) {
    IgnoreSigpipeOnce();

    int in[2];
    int out[2];
    int err[2];
    if(pipe2(in, O_CLOEXEC) != 0) {
        throw std::runtime_error(std::string("pipe2 failed: ") + std::strerror(errno));
    }
    if(pipe2(out, O_CLOEXEC) != 0) {
        ::close(in[0]);
        ::close(in[1]);
        throw std::runtime_error(std::string("pipe2 failed: ") + std::strerror(errno));
    }
    if(pipe2(err, O_CLOEXEC) != 0) {
        ::close(in[0]);
        ::close(in[1]);
        ::close(out[0]);
        ::close(out[1]);
        throw std::runtime_error(std::string("pipe2 failed: ") + std::strerror(errno));
    }

    const char *cpath = path.c_str();

    pid_t pid = ::fork();
    if(pid < 0) {
        const int e = errno;
        for(int fd : {in[0], in[1], out[0], out[1], err[0], err[1]}) {
            ::close(fd);
        }
        throw std::runtime_error(std::string("fork failed: ") + std::strerror(e));
    }

    if(pid == 0) {
        if(in[0] == STDIN_FILENO) {
            ::fcntl(in[0], F_SETFD, 0);
        } else {
            ::dup2(in[0], STDIN_FILENO);
        }
        if(out[1] == STDOUT_FILENO) {
            ::fcntl(out[1], F_SETFD, 0);
        } else {
            ::dup2(out[1], STDOUT_FILENO);
        }

        ::execlp(cpath, cpath, static_cast<char *>(nullptr));
        const int e = errno;
        ssize_t ignored = ::write(err[1], &e, sizeof(e));
        (void)ignored;
        ::_exit(127);
    }

    // Parent
    ::close(in[0]);
    ::close(out[1]);
    ::close(err[1]);

    int childErrno = 0;
    ssize_t n = 0;
    do {
        n = ::read(err[0], &childErrno, sizeof(childErrno));
    } while(n < 0 && errno == EINTR);
    ::close(err[0]);

    if(n == static_cast<ssize_t>(sizeof(childErrno))) {
        ::waitpid(pid, nullptr, 0);
        ::close(in[1]);
        ::close(out[0]);
        throw std::runtime_error("failed to launch '" + path + "': " + std::strerror(childErrno));
    }

    pid_ = pid;
    toEngine_ = in[1];
    fromEngine_ = out[0];
}

EngineProcess::~EngineProcess() {
    shutdown();
    CloseIfOpen(fromEngine_);
}

void EngineProcess::writeLine(const std::string &line) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    if(toEngine_ < 0) {
        throw std::runtime_error("engine stdin is closed");
    }

    const std::string data = line + '\n';
    size_t off = 0;
    while(off < data.size()) {
        ssize_t n = ::write(toEngine_, data.data() + off, data.size() - off);
        if(n < 0) {
            if(errno == EINTR) {
                continue;
            }
            throw std::runtime_error(std::string("write to engine failed: ") + std::strerror(errno));
        }
        off += static_cast<size_t>(n);
    }
}

bool EngineProcess::readLine(std::string &out) {
    if(fromEngine_ < 0) {
        return false;
    }

    for(;;) {
        const std::string::size_type pos = buffer_.find('\n');
        if(pos != std::string::npos) {
            out = buffer_.substr(0, pos);
            buffer_.erase(0, pos + 1);
            if(!out.empty() && out.back() == '\r') {
                out.pop_back();
            }
            return true;
        }

        char chunk[4096];
        const ssize_t n = ::read(fromEngine_, chunk, sizeof(chunk));
        if(n > 0) {
            buffer_.append(chunk, static_cast<size_t>(n));
            continue;
        }
        if(n < 0 && errno == EINTR) {
            continue;
        }

        if(n == 0 && !buffer_.empty()) {
            out = std::move(buffer_);
            buffer_.clear();
            return true;
        }
        return false;
    }
}

void EngineProcess::shutdown() {
    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        CloseIfOpen(toEngine_);
    }
    if(pid_ <= 0) {
        return;
    }

    for(int i = 0; i < 50; ++i) {
        const pid_t r = ::waitpid(pid_, nullptr, WNOHANG);
        if(r == pid_ || (r < 0 && errno != EINTR)) {
            pid_ = -1;
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ::kill(pid_, SIGKILL);
    ::waitpid(pid_, nullptr, 0);
    pid_ = -1;
}