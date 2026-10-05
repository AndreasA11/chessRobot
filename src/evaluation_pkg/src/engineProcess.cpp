#include "EngineProcess.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <stdexcept>
#include <thread>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {


void closeIfOpen(int& fd) {
    if (fd >= 0) { ::close(fd); fd = -1; }
}

}  // namespace

EngineProcess::EngineProcess(const std::string& path) {
    // Writing to a pipe whose reader died raises SIGPIPE, which would kill the whole
    // ROS node. Ignore it so write() just fails with EPIPE and we throw instead.
    static std::once_flag flag;
    std::call_once(flag, [] { std::signal(SIGPIPE, SIG_IGN); });

    // in:  parent writes in[1] -> child stdin (in[0])
    // out: child stdout (out[1]) -> parent reads out[0]
    // err: child reports an exec() failure to the parent; closes silently on success
    // All ends are O_CLOEXEC, so a successful exec closes the unused ones for us
    // (dup2 clears the flag on the fds that become stdin/stdout).
    int in[2], out[2], err[2];
    if (pipe(in) != 0)
        throw std::runtime_error(std::string("pipe failed: ") + std::strerror(errno));
    if (pipe(out) != 0) {
        ::close(in[0]); ::close(in[1]);
        throw std::runtime_error(std::string("pipe failed: ") + std::strerror(errno));
    }
    if (pipe(err) != 0) {
        ::close(in[0]); ::close(in[1]); ::close(out[0]); ::close(out[1]);
        throw std::runtime_error(std::string("pipe failed: ") + std::strerror(errno));
    }

    const char* cpath = path.c_str();   // resolved before fork: no allocation in the child

    pid_t pid = ::fork();
    if (pid < 0) {
        const int e = errno;
        for (int fd : {in[0], in[1], out[0], out[1], err[0], err[1]}) ::close(fd);
        throw std::runtime_error(std::string("fork failed: ") + std::strerror(e));
    }

    if (pid == 0) {
        // Child: only async-signal-safe calls from here until exec.
        ::dup2(in[0], STDIN_FILENO);
        ::dup2(out[1], STDOUT_FILENO);
        // stderr stays inherited so engine errors show up in the node's console.
        ::execlp(cpath, cpath, static_cast<char*>(nullptr));
        const int e = errno;
        ssize_t ignored = ::write(err[1], &e, sizeof e);
        (void)ignored;
        ::_exit(127);
    }

    // Parent.
    ::close(in[0]);
    ::close(out[1]);
    ::close(err[1]);

    int childErrno = 0;
    ssize_t n;
    do { n = ::read(err[0], &childErrno, sizeof childErrno); } while (n < 0 && errno == EINTR);
    ::close(err[0]);

    if (n == static_cast<ssize_t>(sizeof childErrno)) {   // exec failed
        ::waitpid(pid, nullptr, 0);
        ::close(in[1]);
        ::close(out[0]);
        throw std::runtime_error("failed to launch '" + path + "': " + std::strerror(childErrno));
    }

    enginePid_  = pid;
    toEngine_   = in[1];
    fromEngine_ = out[0];
}

EngineProcess::~EngineProcess() {
    shutdown();
    closeIfOpen(fromEngine_);
}

void EngineProcess::writeLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    if (toEngine_ < 0) {throw std::runtime_error("engine stdin is closed");} 

    const std::string data = line + '\n';
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::write(toEngine_, data.data() + off, data.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error(std::string("write to engine failed: ") + std::strerror(errno));
        }
        off += static_cast<size_t>(n);
    }
}

bool EngineProcess::readLine(std::string& out) {
    if (fromEngine_ < 0) return false;

    while (true) {
        const auto pos = buffer_.find('\n');
        if (pos != std::string::npos) {
            out = buffer_.substr(0, pos);
            buffer_.erase(0, pos + 1);
            if (!out.empty() && out.back() == '\r') out.pop_back();
            return true;
        }

        char chunk[4096];
        const ssize_t n = ::read(fromEngine_, chunk, sizeo(chunk));
        if (n > 0) { 
            buffer_.append(chunk, static_cast<size_t>(n)); 
            continue; 
        }
        if (n < 0 && errno == EINTR) continue;

        // EOF or error: hand back a trailing unterminated line once, then report end.
        if (n == 0 && !buffer_.empty()) {
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
        closeIfOpen(toEngine_);                 // EOF on stdin makes Stockfish quit
    }
    if (enginePid_ <= 0) return;

    for (int i = 0; i < 50; ++i) {              // up to ~500 ms for a clean exit
        const pid_t r = ::waitpid(enginePid_, nullptr, WNOHANG);
        if (r == enginePid_ || (r < 0 && errno != EINTR)) { enginePid_ = -1; return; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ::kill(enginePid_, SIGKILL);
    ::waitpid(enginePid_, nullptr, 0);
    enginePid_ = -1;
}