#ifndef H_ENGINEPROCESS
#define H_ENGINEPROCESS

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

// Owns the Stockfish child process and its stdin/stdout pipes.
class EngineProcess {
public:
    explicit EngineProcess(const std::string &path);
    ~EngineProcess();

    EngineProcess(const EngineProcess &) = delete;
    EngineProcess &operator=(const EngineProcess &) = delete;

    //PUBLIC FUNCTIONS
    // Appends newline and writes a command line to the engine process
    void writeLine(const std::string &line);

    // Reads one complete line from the engine output, stripping trailing newlines
    bool readLine(std::string &out);

    // Shuts down child process cleanly, falling back to SIGKILL if necessary
    void shutdown();

private:
    //PRIVATE VARIABLES
    std::mutex writeMutex_; // Mutex protecting pipe writes
    std::string buffer_; // Buffer for read engine output
    pid_t pid_ = -1; // Process ID of child engine process
    int toEngine_ = -1; // Write end of parent to engine pipe
    int fromEngine_ = -1; // Read end of engine to parent pipe
};

#endif