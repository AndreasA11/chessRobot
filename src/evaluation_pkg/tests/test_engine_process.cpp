#include "EngineProcess.hpp"
#include "test_helpers.hpp"

#include <gtest/gtest.h>
#include <sys/wait.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <map>
#include <string>
#include <thread>
#include <vector>

using testutil::TempDir;

namespace {

double SecondsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

class EngineProcessTest : public ::testing::Test {
protected:
    std::string script(const std::string &body) {
        return dir_.writeScript("child_" + std::to_string(counter_++) + ".sh", "#!/bin/sh\n" + body + "\n");
    }

    TempDir dir_;
    int counter_ = 0;
};

//LAUNCHING TESTS

TEST_F(EngineProcessTest, ThrowsWhenExecutableMissing) {
    try {
        EngineProcess e("/nonexistent/engine");
        FAIL() << "expected std::runtime_error";
    } catch(const std::runtime_error &ex) {
        EXPECT_NE(std::string(ex.what()).find("/nonexistent/engine"), std::string::npos)
            << "message should name the path, got: " << ex.what();
    }
}

TEST_F(EngineProcessTest, ThrowsWhenFileIsNotExecutable) {
    const std::string path = dir_.writeFile("plain.txt", "#!/bin/sh\necho hi\n");
    EXPECT_THROW(EngineProcess e(path), std::runtime_error);
}

TEST_F(EngineProcessTest, FindsExecutableOnPath) {
    EngineProcess e("cat");
    e.writeLine("ping");
    std::string line;
    ASSERT_TRUE(e.readLine(line));
    EXPECT_EQ(line, "ping");
}

TEST_F(EngineProcessTest, WorksWhenStdinIsClosedInTheParent) {
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

//WRITING AND READING TESTS

TEST_F(EngineProcessTest, EchoRoundTrip) {
    EngineProcess e("cat");
    e.writeLine("position fen 8/8/8/8/8/8/8/K6k w - - 0 1");
    std::string line;
    ASSERT_TRUE(e.readLine(line));
    EXPECT_EQ(line, "position fen 8/8/8/8/8/8/8/K6k w - - 0 1");
}

TEST_F(EngineProcessTest, PreservesLineOrder) {
    EngineProcess e("cat");
    for(int i = 0; i < 100; ++i) {
        e.writeLine("line " + std::to_string(i));
    }
    for(int i = 0; i < 100; ++i) {
        std::string line;
        ASSERT_TRUE(e.readLine(line));
        EXPECT_EQ(line, "line " + std::to_string(i));
    }
}

TEST_F(EngineProcessTest, ChildReadsNothingUntilLineIsComplete) {
    EngineProcess e(script("read x; echo \"got:$x\""));
    e.writeLine("abc");
    std::string line;
    ASSERT_TRUE(e.readLine(line));
    EXPECT_EQ(line, "got:abc");
}

//READLINE FRAMING TESTS

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
    ASSERT_TRUE(e.readLine(line));
    EXPECT_EQ(line, "one");
    ASSERT_TRUE(e.readLine(line));
    EXPECT_EQ(line, "two");
    ASSERT_TRUE(e.readLine(line));
    EXPECT_EQ(line, "three");
    EXPECT_FALSE(e.readLine(line));
}

TEST_F(EngineProcessTest, PreservesEmptyLines) {
    EngineProcess e(script("printf 'a\\n\\nb\\n'"));
    std::string line;
    ASSERT_TRUE(e.readLine(line));
    EXPECT_EQ(line, "a");
    ASSERT_TRUE(e.readLine(line));
    EXPECT_EQ(line, "");
    ASSERT_TRUE(e.readLine(line));
    EXPECT_EQ(line, "b");
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

//FAILURE BEHAVIOUR TESTS

TEST_F(EngineProcessTest, WriteToExitedChildEventuallyThrowsInsteadOfKillingUs) {
    EngineProcess e(script("exit 0"));
    std::string line;
    ASSERT_FALSE(e.readLine(line));
    const bool threw = testutil::waitFor(
        [&] {
            try {
                e.writeLine("hello?");
                return false;
            } catch(const std::runtime_error &) {
                return true;
            }
        },
        std::chrono::milliseconds(1000));
    EXPECT_TRUE(threw);
}

TEST_F(EngineProcessTest, WriteAfterShutdownThrows) {
    EngineProcess e("cat");
    e.shutdown();
    EXPECT_THROW(e.writeLine("x"), std::runtime_error);
}

//SHUTDOWN AND CLEANUP TESTS

TEST_F(EngineProcessTest, ShutdownIsIdempotent) {
    EngineProcess e("cat");
    e.shutdown();
    EXPECT_NO_THROW(e.shutdown());
}

TEST_F(EngineProcessTest, ShutdownEndsAWellBehavedChildQuickly) {
    EngineProcess e("cat");
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    e.shutdown();
    EXPECT_LT(SecondsSince(t0), 0.3);
    std::string line;
    EXPECT_FALSE(e.readLine(line));
}

TEST_F(EngineProcessTest, ShutdownKillsAChildThatIgnoresStdin) {
    EngineProcess e(script("exec sleep 30"));
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    e.shutdown();
    EXPECT_LT(SecondsSince(t0), 2.0);
}

TEST_F(EngineProcessTest, ShutdownWakesAReaderBlockedInReadLine) {
    EngineProcess e(script("exec sleep 30"));
    std::atomic<bool> returned{false};
    std::atomic<bool> gotLine{true};
    std::thread reader([&] {
        std::string line;
        gotLine = e.readLine(line);
        returned = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(returned) << "readLine should still be blocked";

    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    e.shutdown();
    EXPECT_TRUE(testutil::waitFor([&] { return returned.load(); }, std::chrono::milliseconds(2000)));
    EXPECT_LT(SecondsSince(t0), 2.5);
    reader.join();
    EXPECT_FALSE(gotLine);
}

TEST_F(EngineProcessTest, DestructorReapsTheChild) {
    {
        EngineProcess e("cat");
    }
    errno = 0;
    EXPECT_EQ(waitpid(-1, nullptr, WNOHANG), -1);
    EXPECT_EQ(errno, ECHILD);
}

TEST_F(EngineProcessTest, DestructorDoesNotHangOnAChildThatIgnoresStdin) {
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    {
        EngineProcess e(script("exec sleep 30"));
    }
    EXPECT_LT(SecondsSince(t0), 2.0);
    errno = 0;
    EXPECT_EQ(waitpid(-1, nullptr, WNOHANG), -1);
    EXPECT_EQ(errno, ECHILD);
}

TEST_F(EngineProcessTest, SecondEngineDoesNotInheritFirstEnginesPipes) {
    EngineProcess a("cat");
    EngineProcess b("cat");
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    a.shutdown();
    EXPECT_LT(SecondsSince(t0), 0.3) << "pipe fds are leaking into later children";
    b.writeLine("still alive");
    std::string line;
    ASSERT_TRUE(b.readLine(line));
    EXPECT_EQ(line, "still alive");
}

//THREADING TESTS

TEST_F(EngineProcessTest, ConcurrentWritesNeverInterleaveWithinALine) {
    constexpr int kThreads = 4;
    constexpr int kLinesPerThread = 40;
    constexpr int kLineLen = 20000;
    EngineProcess e("cat");

    std::vector<std::string> received{};
    std::thread reader([&] {
        std::string line;
        for(int i = 0; i < kThreads * kLinesPerThread && e.readLine(line); ++i) {
            received.push_back(line);
        }
    });

    std::vector<std::thread> writers{};
    for(int t = 0; t < kThreads; ++t) {
        writers.emplace_back([&, t] {
            const std::string line(kLineLen, static_cast<char>('A' + t));
            for(int i = 0; i < kLinesPerThread; ++i) {
                e.writeLine(line);
            }
        });
    }
    for(std::thread &w : writers) {
        w.join();
    }
    reader.join();

    ASSERT_EQ(received.size(), static_cast<size_t>(kThreads * kLinesPerThread));
    std::map<char, int> perThread{};
    for(const std::string &l : received) {
        ASSERT_EQ(l.size(), static_cast<size_t>(kLineLen)) << "a line was cut or merged";
        ASSERT_EQ(l.find_first_not_of(l[0]), std::string::npos) << "two writers' bytes were mixed";
        ++perThread[l[0]];
    }
    for(int t = 0; t < kThreads; ++t) {
        EXPECT_EQ(perThread[static_cast<char>('A' + t)], kLinesPerThread);
    }
}

//REAL STOCKFISH TESTS

TEST_F(EngineProcessTest, RealStockfishCompletesUciHandshake) {
    const std::string sf = testutil::findStockfish();
    if(sf.empty()) {
        GTEST_SKIP() << "Stockfish not found (set STOCKFISH_PATH)";
    }

    EngineProcess e(sf);
    e.writeLine("uci");
    bool sawName = false;
    bool sawUciOk = false;
    std::string line;
    while(e.readLine(line)) {
        if(line.rfind("id name", 0) == 0) {
            sawName = true;
        }
        if(line == "uciok") {
            sawUciOk = true;
            break;
        }
    }
    EXPECT_TRUE(sawName);
    EXPECT_TRUE(sawUciOk);

    e.writeLine("isready");
    ASSERT_TRUE(e.readLine(line));
    while(line != "readyok" && e.readLine(line)) {}
    EXPECT_EQ(line, "readyok");
}