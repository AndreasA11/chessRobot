#ifndef H_ENGINE_PROCESS
#define H_ENGINE_PROCESS

#include <mutex>
#include <string>
#include <sys/types.h>
#include <vector>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
// Owns the Stockfish child process and its stdin/stdout pipes (POSIX).
// Knows nothing about UCI or ROS: it just moves lines in and out.
//
// Threading: writeLine() may be called from any thread. readLine() must only
// be called from one thread (the reader thread).

class EngineProcess {
    public:
    EngineProcess(const std::string& path);
    ~EngineProcess();

    EngineProcess(const EngineProcess&) = delete;
    EngineProcess& operator=(const EngineProcess&) = delete;

     // Appends '\n' and writes the whole line. Throws std::runtime_error on failure
    // (e.g. engine died, or shutdown() already closed stdin).
    //Takes in once complete command to send
    void writeLine(const std::string& line);
 
    // Blocks until a full line arrives. Strips "\n" / "\r\n". Returns false on EOF,
    // i.e. the engine exited (or was shut down) and everything was consumed.

    // Reads one complete line from the engine at a time.
    // Call repeatedly to process all Stockfish output:
    //
    // std::string line;
    // while (engine.readLine(line)) {
    //     // process line
    // }
    bool readLine(std::string& out);
 
    // Closes engine stdin (Stockfish exits on EOF), waits up to ~500 ms for it to
    // exit, then SIGKILLs and reaps it. Safe to call more than once. The read end
    // stays open so a blocked readLine() wakes up with EOF.
    void shutdown();

    private:
    int toEngine_ = -1;//write end of parent -> engine pipe
    int fromEngine_ = -1;//read end of engine -> parent pipe
    pid_t enginePid_ = -1;
    std::string buffer_;
    std::mutex  writeMutex_;
    
};


#endif