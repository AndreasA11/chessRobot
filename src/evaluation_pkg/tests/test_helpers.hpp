#ifndef H_TEST_HELPERS
#define H_TEST_HELPERS

#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace testutil {

// A temporary directory that is cleaned up when destroyed
class TempDir {
public:
    TempDir() {
        std::string tmpl = (std::filesystem::temp_directory_path() / "uci_test_XXXXXX").string();
        if(!mkdtemp(tmpl.data())) {
            throw std::runtime_error("mkdtemp failed");
        }
        path_ = tmpl;
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;

    const std::string &path() const { return path_; }

    // Writes a non-executable file and returns its path
    std::string writeFile(const std::string &name, const std::string &contents) const {
        const std::string full = path_ + "/" + name;
        std::ofstream out(full, std::ios::binary);
        out << contents;
        return full;
    }

    // Writes an executable shell script and returns its path
    std::string writeScript(const std::string &name, const std::string &contents) const {
        const std::string full = writeFile(name, contents);
        chmod(full.c_str(), 0755);
        return full;
    }

private:
    std::string path_; // Path to temporary directory
};

inline std::string readFile(const std::string &path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

inline std::vector<std::string> readLines(const std::string &path) {
    std::vector<std::string> lines{};
    std::istringstream in(readFile(path));
    for(std::string l; std::getline(in, l);) {
        lines.push_back(l);
    }
    return lines;
}

// Polls pred every 5 ms until true or timeout passes
inline bool waitFor(const std::function<bool()> &pred, std::chrono::milliseconds timeout) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + timeout;
    while(std::chrono::steady_clock::now() < deadline) {
        if(pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

// Finds real Stockfish executable on system
inline std::string findStockfish() {
    if(const char *env = std::getenv("STOCKFISH_PATH")) {
        if(access(env, X_OK) == 0) {
            return env;
        }
    }
    for(const char *p : {"/usr/games/stockfish", "/usr/bin/stockfish", "/usr/local/bin/stockfish"}) {
        if(access(p, X_OK) == 0) {
            return p;
        }
    }
    return "";
}

} // namespace testutil

#endif
