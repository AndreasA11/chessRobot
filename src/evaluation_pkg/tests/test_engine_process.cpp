// Tests for EngineProcess. No ROS and no Stockfish needed: the "engines" are tiny
// shell scripts, `cat`, and `sleep`. One test uses the real Stockfish if installed.
#include <gtest/gtest.h>

#include <atomic>
#include <cerrno>
#include <map>
#include <thread>

#include <sys/wait.h>

#include "EngineProcess.hpp"
#include "test_helpers.hpp"

using namespace std::chrono_literals;
using testutil::TempDir;

namespace {
double secondsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}
}  // namespace

class EngineProcessTest : public ::testing::Test {
protected:
    // Writes `body` as a shell script and returns its path.
    std::string script(const std::string& body) {
        return dir_.writeScript("child_" + std::to_string(counter_++) + ".sh", "#!/bin/sh\n" + body + "\n");
    }
    TempDir dir_;
    int counter_ = 0;
};

// ---------------------------------------------------------------------------
// Launching
// ---------------------------------------------------------------------------
TEST_F(EngineProcessTest, ThrowsWhenExecutableMissing) {
    try {
        EngineProcess e("/nonexistent/engine");
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& ex) {
        EXPECT_NE(std::string(ex.what()).find("/nonexistent/engine"), std::string::npos)
            << "message should name the path, got: " << ex.what();
    }
}

TEST_F(EngineProcessTest, ThrowsWhenFileIsNotExecutable) {
    const std::string path = dir_.writeFile("plain.txt", "#!/bin/sh\necho hi\n");   // no chmod
    EXPECT_THROW(EngineProcess e(path), std::runtime_error);
}

TEST_F(EngineProcessTest, FindsExecutableOnPath) {
    EngineProcess e("cat");   // no '/', so it must be looked up on PATH
    e.writeLine("ping");
    std::string line;
    ASSERT_TRUE(e.readLine(line));
    EXPECT_EQ(line, "ping");
}

TEST_F(EngineProcessTest, WorksWhenStdinIsClosedInTheParent) {
    // Some launchers (daemons, certain test runners) start us with fd 0 closed. pipe2 then
    // returns 0 for the child's stdin pipe, and dup2(0, 0) is a no-op that leaves O_CLOEXEC
    // set, so exec would silently close the child's stdin.
    const int saved = dup(STDIN_FILENO);
    ASSERT_GE(saved, 0);
    close(STDIN_FILENO);
    {
        EngineProcess e("cat");
        e.writeLine("still works");
        std::string line;
        EXPECT_TRUE(e.readLine(line));
        EXPECT_EQ(line, "still works");
    }
    dup2(saved, STDIN_FILENO);
    close(saved);
}

// ---------------------------------------------------------------------------
// Writing and reading (cat echoes stdin to stdout, so it tests both pipes)
// ---------------------------------------------------------------------------
TEST_F(EngineProcessTest, EchoRoundTrip) {
    EngineProcess e("cat");
    e.writeLine("position fen 8/8/8/8/8/8/8/K6k w - - 0 1");   // spaces and slashes survive
    std::string line;
    ASSERT_TRUE(e.readLine(line));
    EXPECT_EQ(line, "position fen 8/8/8/8/8/8/8/K6k w - - 0 1");
}

TEST_F(EngineProcessTest, PreservesLineOrder) {
    EngineProcess e("cat");
    for (int i = 0; i < 100; ++i) e.writeLine("line " + std::to_string(i));
    for (int i = 0; i < 100; ++i) {
        std::string line;
        ASSERT_TRUE(e.readLine(line));
        EXPECT_EQ(line, "line " + std::to_string(i));
    }
}

TEST_F(EngineProcessTest, ChildReadsNothingUntilLineIsComplete) {
    // The child echoes only after it has read a full line, so this also proves that
    // writeLine appends the '\n' itself.
    EngineProcess e(script("read x; echo \"got:$x\""));
    e.writeLine("abc");
    std::string line;
    ASSERT_TRUE(e.readLine(line));
    EXPECT_EQ(line, "got:abc");
}

// ---------------------------------------------------------------------------
// readLine framing: the pipe is a byte stream, not a stream of lines
// ---------------------------------------------------------------------------
TEST_F(EngineProcessTest, StripsCarriageReturn) {
    EngineProcess e(script("printf 'abc\\r\\n'"));
    std::string line;
    ASSERT_TRUE(e.readLine(line));
    EXPECT_EQ(line, "abc");
}

TEST_F(EngineProcessTest, JoinsALineSplitAcrossReads) {
    EngineProcess e(script("printf 'ab'; sleep 0.2; printf 'cd\\n'"));
    std::string line;
    ASSERT_TRUE(e.readLine(line));
    EXPECT_EQ(line, "abcd");
}

TEST_F(EngineProcessTest, SplitsSeveralLinesArrivingTogether) {
    EngineProcess e(script("printf 'one\\ntwo\\nthree\\n'"));
    std::string line;
    ASSERT_TRUE(e.readLine(line)); EXPECT_EQ(line, "one");
    ASSERT_TRUE(e.readLine(line)); EXPECT_EQ(line, "two");
    ASSERT_TRUE(e.readLine(line)); EXPECT_EQ(line, "three");
    EXPECT_FALSE(e.readLine(line));
}

TEST_F(EngineProcessTest, PreservesEmptyLines) {
    EngineProcess e(script("printf 'a\\n\\nb\\n'"));
    std::string line;
    ASSERT_TRUE(e.readLine(line)); EXPECT_EQ(line, "a");
    ASSERT_TRUE(e.readLine(line)); EXPECT_EQ(line, "");
    ASSERT_TRUE(e.readLine(line)); EXPECT_EQ(line, "b");
}

TEST_F(EngineProcessTest, HandlesLineLongerThanOneReadChunk) {
    EngineProcess e(script("head -c 20000 /dev/zero | tr '\\0' 'x'; printf '\\nend\\n'"));
    std::string line;
    ASSERT_TRUE(e.readLine(line));
    EXPECT_EQ(line.size(), 20000u);
    EXPECT_EQ(line.find_first_not_of('x'), std::string::npos);
    ASSERT_TRUE(e.readLine(line));
    EXPECT_EQ(line, "end");
}

TEST_F(EngineProcessTest, ReturnsFalseOnEofAndKeepsReturningFalse) {
    EngineProcess e(script("exit 0"));
    std::string line;
    EXPECT_FALSE(e.readLine(line));
    EXPECT_FALSE(e.readLine(line));
}

TEST_F(EngineProcessTest, ReturnsTrailingUnterminatedLineThenEof) {
    EngineProcess e(script("printf 'tail'"));
    std::string line;
    ASSERT_TRUE(e.readLine(line));
    EXPECT_EQ(line, "tail");
    EXPECT_FALSE(e.readLine(line));
}

// ---------------------------------------------------------------------------
// Failure behaviour
// ---------------------------------------------------------------------------
TEST_F(EngineProcessTest, WriteToExitedChildEventuallyThrowsInsteadOfKillingUs) {
    // Writing to a pipe nobody reads raises SIGPIPE, whose default action kills the
    // whole process. If the test binary dies here, SIGPIPE isn't being ignored.
    //
    // Note the "eventually": EOF on the child's stdout can arrive a moment before the
    // kernel has closed its stdin, so the very first write after EOF may still succeed
    // (the bytes just sit in the pipe buffer). EOF from readLine is the reliable signal
    // that the engine is gone; a failed write is only a secondary one.
    EngineProcess e(script("exit 0"));
    std::string line;
    ASSERT_FALSE(e.readLine(line));   // EOF: the child is exiting or gone
    const bool threw = testutil::waitFor(
        [&] {
            try { e.writeLine("hello?"); return false; }
            catch (const std::runtime_error&) { return true; }
        },
        1000ms);
    EXPECT_TRUE(threw);
}

TEST_F(EngineProcessTest, WriteAfterShutdownThrows) {
    EngineProcess e("cat");
    e.shutdown();
    EXPECT_THROW(e.writeLine("x"), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Shutdown and cleanup
// ---------------------------------------------------------------------------
TEST_F(EngineProcessTest, ShutdownIsIdempotent) {
    EngineProcess e("cat");
    e.shutdown();
    EXPECT_NO_THROW(e.shutdown());
}

TEST_F(EngineProcessTest, ShutdownEndsAWellBehavedChildQuickly) {
    // cat exits when its stdin closes, so shutdown must not need the SIGKILL fallback.
    EngineProcess e("cat");
    const auto t0 = std::chrono::steady_clock::now();
    e.shutdown();
    EXPECT_LT(secondsSince(t0), 0.3);
    std::string line;
    EXPECT_FALSE(e.readLine(line));
}

TEST_F(EngineProcessTest, ShutdownKillsAChildThatIgnoresStdin) {
    // `exec` matters: without it the shell is killed but its `sleep` child survives and
    // keeps our read pipe open forever.
    EngineProcess e(script("exec sleep 30"));
    const auto t0 = std::chrono::steady_clock::now();
    e.shutdown();
    EXPECT_LT(secondsSince(t0), 2.0);
}

TEST_F(EngineProcessTest, ShutdownWakesAReaderBlockedInReadLine) {
    EngineProcess e(script("exec sleep 30"));
    std::atomic<bool> returned{false};
    std::atomic<bool> gotLine{true};
    std::thread reader([&] {
        std::string line;
        gotLine = e.readLine(line);   // blocks: the child never prints anything
        returned = true;
    });
    std::this_thread::sleep_for(100ms);
    EXPECT_FALSE(returned) << "readLine should still be blocked";

    const auto t0 = std::chrono::steady_clock::now();
    e.shutdown();
    EXPECT_TRUE(testutil::waitFor([&] { return returned.load(); }, 2000ms));
    EXPECT_LT(secondsSince(t0), 2.5);
    reader.join();
    EXPECT_FALSE(gotLine);
}

TEST_F(EngineProcessTest, DestructorReapsTheChild) {
    { EngineProcess e("cat"); }
    // If the child was left as a zombie, waitpid(-1) would hand it to us here.
    errno = 0;
    EXPECT_EQ(waitpid(-1, nullptr, WNOHANG), -1);
    EXPECT_EQ(errno, ECHILD);
}

TEST_F(EngineProcessTest, DestructorDoesNotHangOnAChildThatIgnoresStdin) {
    const auto t0 = std::chrono::steady_clock::now();
    { EngineProcess e(script("exec sleep 30")); }
    EXPECT_LT(secondsSince(t0), 2.0);
    errno = 0;
    EXPECT_EQ(waitpid(-1, nullptr, WNOHANG), -1);
    EXPECT_EQ(errno, ECHILD);
}

TEST_F(EngineProcessTest, SecondEngineDoesNotInheritFirstEnginesPipes) {
    // If B inherited A's pipe ends, closing our copy of A's stdin pipe wouldn't deliver
    // EOF to A (B still holds one), and A's shutdown would stall until the SIGKILL.
    EngineProcess a("cat");
    EngineProcess b("cat");
    const auto t0 = std::chrono::steady_clock::now();
    a.shutdown();
    EXPECT_LT(secondsSince(t0), 0.3) << "pipe fds are leaking into later children";
    b.writeLine("still alive");
    std::string line;
    ASSERT_TRUE(b.readLine(line));
    EXPECT_EQ(line, "still alive");
}

// ---------------------------------------------------------------------------
// Threading
// ---------------------------------------------------------------------------
TEST_F(EngineProcessTest, ConcurrentWritesNeverInterleaveWithinALine) {
    // Lines are longer than PIPE_BUF (4096), so the kernel does NOT make a single write()
    // atomic. Only the lock in writeLine keeps two threads' bytes apart.
    constexpr int kThreads = 4, kLinesPerThread = 40, kLineLen = 20000;
    EngineProcess e("cat");

    std::vector<std::string> received;
    std::thread reader([&] {          // must drain concurrently or cat's output pipe fills up
        std::string line;
        for (int i = 0; i < kThreads * kLinesPerThread && e.readLine(line); ++i)
            received.push_back(line);
    });

    std::vector<std::thread> writers;
    for (int t = 0; t < kThreads; ++t)
        writers.emplace_back([&, t] {
            const std::string line(kLineLen, static_cast<char>('A' + t));
            for (int i = 0; i < kLinesPerThread; ++i) e.writeLine(line);
        });
    for (auto& w : writers) w.join();
    reader.join();

    ASSERT_EQ(received.size(), static_cast<size_t>(kThreads * kLinesPerThread));
    std::map<char, int> perThread;
    for (const auto& l : received) {
        ASSERT_EQ(l.size(), static_cast<size_t>(kLineLen)) << "a line was cut or merged";
        ASSERT_EQ(l.find_first_not_of(l[0]), std::string::npos) << "two writers' bytes were mixed";
        ++perThread[l[0]];
    }
    for (int t = 0; t < kThreads; ++t) EXPECT_EQ(perThread[static_cast<char>('A' + t)], kLinesPerThread);
}

// ---------------------------------------------------------------------------
// Real Stockfish (skipped if not installed; set STOCKFISH_PATH to point at it)
// ---------------------------------------------------------------------------
TEST_F(EngineProcessTest, RealStockfishCompletesUciHandshake) {
    const std::string sf = testutil::findStockfish();
    if (sf.empty()) GTEST_SKIP() << "Stockfish not found (set STOCKFISH_PATH)";

    EngineProcess e(sf);
    e.writeLine("uci");
    bool sawName = false, sawUciOk = false;
    std::string line;
    while (e.readLine(line)) {
        if (line.rfind("id name", 0) == 0) sawName = true;
        if (line == "uciok") { sawUciOk = true; break; }
    }
    EXPECT_TRUE(sawName);
    EXPECT_TRUE(sawUciOk);

    e.writeLine("isready");
    ASSERT_TRUE(e.readLine(line));
    // Stockfish may print option/info lines first; skip to readyok.
    while (line != "readyok" && e.readLine(line)) {}
    EXPECT_EQ(line, "readyok");
}