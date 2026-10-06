#pragma once
// Small utilities shared by the EngineProcess and StockfishNode tests.
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace testutil {

// A temp directory that is deleted when the object goes out of scope.
class TempDir {
public:
    TempDir() {
        std::string tmpl = (std::filesystem::temp_directory_path() / "uci_test_XXXXXX").string();
        if (!mkdtemp(tmpl.data())) throw std::runtime_error("mkdtemp failed");
        path_ = tmpl;
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path_, ec); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::string& path() const { return path_; }

    // Writes a file and returns its full path (not executable).
    std::string writeFile(const std::string& name, const std::string& contents) const {
        const std::string full = path_ + "/" + name;
        std::ofstream out(full, std::ios::binary);
        out << contents;
        return full;
    }
    // Same, but chmod 755 so it can be exec'd. The file is closed before returning,
    // which avoids "text file busy" when it is launched straight away.
    std::string writeScript(const std::string& name, const std::string& contents) const {
        const std::string full = writeFile(name, contents);
        chmod(full.c_str(), 0755);
        return full;
    }
private:
    std::string path_;
};

inline std::string readFile(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

inline std::vector<std::string> readLines(const std::string& path) {
    std::vector<std::string> lines;
    std::istringstream in(readFile(path));
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    return lines;
}

// Polls `pred` every 5 ms until it is true or `timeout` passes.
inline bool waitFor(const std::function<bool()>& pred, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

// Finds a real Stockfish: $STOCKFISH_PATH first, then the usual install locations.
// Returns "" if none is found (tests that need it call GTEST_SKIP()).
inline std::string findStockfish() {
    if (const char* env = std::getenv("STOCKFISH_PATH"))
        if (access(env, X_OK) == 0) return env;
    for (const char* p : {"/usr/games/stockfish", "/usr/bin/stockfish", "/usr/local/bin/stockfish"})
        if (access(p, X_OK) == 0) return p;
    return "";
}

}  // namespace testutil
