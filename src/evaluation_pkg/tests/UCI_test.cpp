#include "UCI.hpp"

#include <gtest/gtest.h>

using namespace uci;

namespace {

template <class T>
const T &As(const Message &m) {
    EXPECT_TRUE(std::holds_alternative<T>(m)) << "message has a different type than expected";
    return std::get<T>(m);
}

} // namespace

//GUI TO ENGINE BUILDERS TESTS

TEST(UciBuilders, SimpleCommands) {
    EXPECT_EQ(cmdUci(), "uci");
    EXPECT_EQ(cmdIsReady(), "isready");
    EXPECT_EQ(cmdUciNewGame(), "ucinewgame");
    EXPECT_EQ(cmdStop(), "stop");
    EXPECT_EQ(cmdPonderHit(), "ponderhit");
    EXPECT_EQ(cmdQuit(), "quit");
    EXPECT_EQ(cmdDebug(true), "debug on");
    EXPECT_EQ(cmdDebug(false), "debug off");
}

TEST(UciBuilders, PositionStartpos) {
    EXPECT_EQ(cmdPosition(std::nullopt), "position startpos");
    EXPECT_EQ(cmdPosition(std::nullopt, {"e2e4", "e7e5"}), "position startpos moves e2e4 e7e5");
}

TEST(UciBuilders, PositionFen) {
    EXPECT_EQ(cmdPosition(std::string("8/8/8/8/8/8/8/K6k w - - 0 1")),
              "position fen 8/8/8/8/8/8/8/K6k w - - 0 1");
    EXPECT_EQ(cmdPosition(std::string("8/8/8/8/8/8/8/K6k w - - 0 1"), {"a1a2"}),
              "position fen 8/8/8/8/8/8/8/K6k w - - 0 1 moves a1a2");
}

TEST(UciBuilders, SetOption) {
    EXPECT_EQ(cmdSetOption("Clear Hash"), "setoption name Clear Hash");
    EXPECT_EQ(cmdSetOption("Hash", "128"), "setoption name Hash value 128");
    EXPECT_EQ(cmdSetOption("Skill Level", "5"), "setoption name Skill Level value 5");
}

TEST(UciBuilders, GoOmitsUnsetFieldsAndPutsSearchMovesLast) {
    GoParams g;
    g.depth = 12;
    g.wtime = 1000;
    g.searchMoves = {"e2e4", "d2d4"};
    EXPECT_EQ(cmdGo(g), "go wtime 1000 depth 12 searchmoves e2e4 d2d4");

    GoParams inf;
    inf.infinite = true;
    EXPECT_EQ(cmdGo(inf), "go infinite");

    GoParams mt;
    mt.moveTime = 250;
    EXPECT_EQ(cmdGo(mt), "go movetime 250");

    EXPECT_EQ(cmdGo(GoParams{}), "go");
}

//ENGINE TO GUI PARSING TESTS

TEST(UciParse, UciOkAndReadyOk) {
    EXPECT_TRUE(std::holds_alternative<UciOk>(parseLine("uciok")));
    EXPECT_TRUE(std::holds_alternative<ReadyOk>(parseLine("readyok")));
    EXPECT_TRUE(std::holds_alternative<ReadyOk>(parseLine("readyok\r")));
}

TEST(UciParse, UnknownForEmptyOrNonUciLines) {
    EXPECT_TRUE(std::holds_alternative<Unknown>(parseLine("")));
    EXPECT_TRUE(std::holds_alternative<Unknown>(parseLine("   ")));
    EXPECT_TRUE(std::holds_alternative<Unknown>(parseLine("Stockfish 17 by the Stockfish developers")));
    EXPECT_EQ(As<Unknown>(parseLine("hello world")).raw, "hello world");
}

TEST(UciParse, IdLines) {
    const IdLine name = As<IdLine>(parseLine("id name Stockfish 17.1"));
    EXPECT_EQ(name.field, IdLine::Field::Name);
    EXPECT_EQ(name.value, "Stockfish 17.1");

    const IdLine author = As<IdLine>(parseLine("id author the Stockfish developers (see AUTHORS file)"));
    EXPECT_EQ(author.field, IdLine::Field::Author);
    EXPECT_EQ(author.value, "the Stockfish developers (see AUTHORS file)");

    EXPECT_TRUE(std::holds_alternative<Unknown>(parseLine("id")));
    EXPECT_TRUE(std::holds_alternative<Unknown>(parseLine("id colour blue")));
}

TEST(UciParse, SpinOption) {
    const Option o = As<Option>(parseLine("option name Hash type spin default 16 min 1 max 33554432"));
    EXPECT_EQ(o.name, "Hash");
    EXPECT_EQ(o.type, Option::Type::Spin);
    EXPECT_EQ(o.defaultValue, "16");
    ASSERT_TRUE(o.min.has_value());
    ASSERT_TRUE(o.max.has_value());
    EXPECT_EQ(*o.min, 1);
    EXPECT_EQ(*o.max, 33554432);
}

TEST(UciParse, OptionNamesMayContainSpaces) {
    const Option button = As<Option>(parseLine("option name Clear Hash type button"));
    EXPECT_EQ(button.name, "Clear Hash");
    EXPECT_EQ(button.type, Option::Type::Button);

    const Option str = As<Option>(parseLine("option name Debug Log File type string default <empty>"));
    EXPECT_EQ(str.name, "Debug Log File");
    EXPECT_EQ(str.type, Option::Type::String);
    EXPECT_EQ(str.defaultValue, "<empty>");
}

TEST(UciParse, CheckAndComboOptions) {
    const Option check = As<Option>(parseLine("option name Ponder type check default false"));
    EXPECT_EQ(check.type, Option::Type::Check);
    EXPECT_EQ(check.defaultValue, "false");

    const Option combo = As<Option>(parseLine("option name Style type combo default Normal var Solid var Normal var Risky"));
    EXPECT_EQ(combo.type, Option::Type::Combo);
    ASSERT_EQ(combo.vars.size(), 3u);
    EXPECT_EQ(combo.vars[0], "Solid");
    EXPECT_EQ(combo.vars[2], "Risky");
}

TEST(UciParse, MalformedOptionIsUnknown) {
    EXPECT_TRUE(std::holds_alternative<Unknown>(parseLine("option")));
    EXPECT_TRUE(std::holds_alternative<Unknown>(parseLine("option name Hash")));
    EXPECT_TRUE(std::holds_alternative<Unknown>(parseLine("option name Hash type banana")));
}

TEST(UciParse, FullInfoLine) {
    const Info i = As<Info>(parseLine(
        "info depth 12 seldepth 18 multipv 1 score cp 34 nodes 123456 nps 987654 hashfull 12 tbhits 0 time 125 pv e2e4 e7e5 g1f3"));
    EXPECT_EQ(i.depth, 12);
    EXPECT_EQ(i.seldepth, 18);
    EXPECT_EQ(i.multipv, 1);
    ASSERT_TRUE(i.score.has_value());
    EXPECT_EQ(i.score->kind, Score::Kind::Cp);
    EXPECT_EQ(i.score->value, 34);
    EXPECT_EQ(i.score->bound, Score::Bound::Exact);
    EXPECT_EQ(i.nodes, 123456u);
    EXPECT_EQ(i.nps, 987654);
    EXPECT_EQ(i.time, 125);
    ASSERT_EQ(i.pv.size(), 3u);
    EXPECT_EQ(i.pv[0], "e2e4");
    EXPECT_EQ(i.pv[2], "g1f3");
}

TEST(UciParse, MateScoreWithBoundAndWdl) {
    const Info i = As<Info>(parseLine("info depth 20 score mate -3 lowerbound wdl 0 0 1000 nodes 5 pv a1a2"));
    ASSERT_TRUE(i.score.has_value());
    EXPECT_EQ(i.score->kind, Score::Kind::Mate);
    EXPECT_EQ(i.score->value, -3);
    EXPECT_EQ(i.score->bound, Score::Bound::Lower);
    EXPECT_EQ(i.nodes, 5u);
    EXPECT_EQ(i.pv.size(), 1u);
}

TEST(UciParse, UpperBoundScore) {
    const Info i = As<Info>(parseLine("info depth 5 score cp -20 upperbound"));
    ASSERT_TRUE(i.score.has_value());
    EXPECT_EQ(i.score->bound, Score::Bound::Upper);
}

TEST(UciParse, InfoStringTakesTheRestOfTheLine) {
    const Info i = As<Info>(parseLine("info string NNUE evaluation using nn-1111.nnue"));
    ASSERT_TRUE(i.text.has_value());
    EXPECT_EQ(*i.text, "NNUE evaluation using nn-1111.nnue");
}

TEST(UciParse, InfoCurrMove) {
    const Info i = As<Info>(parseLine("info currmove e2e4 currmovenumber 1"));
    EXPECT_EQ(i.currMove, "e2e4");
    EXPECT_EQ(i.currMoveNumber, 1);
}

TEST(UciParse, TruncatedInfoLinesDoNotCrashOrInventValues) {
    const Info a = As<Info>(parseLine("info depth"));
    EXPECT_FALSE(a.depth.has_value());
    const Info b = As<Info>(parseLine("info score cp"));
    EXPECT_FALSE(b.score.has_value());
    const Info c = As<Info>(parseLine("info depth notanumber nodes 7"));
    EXPECT_FALSE(c.depth.has_value());
    EXPECT_EQ(c.nodes, 7u);
}

TEST(UciParse, BestMove) {
    const BestMove withPonder = As<BestMove>(parseLine("bestmove e2e4 ponder e7e5"));
    EXPECT_EQ(withPonder.move, "e2e4");
    EXPECT_EQ(withPonder.ponder, "e7e5");

    const BestMove none = As<BestMove>(parseLine("bestmove (none)"));
    EXPECT_EQ(none.move, "(none)");
    EXPECT_FALSE(none.ponder.has_value());

    const BestMove promo = As<BestMove>(parseLine("bestmove e7e8q"));
    EXPECT_EQ(promo.move, "e7e8q");

    EXPECT_TRUE(std::holds_alternative<Unknown>(parseLine("bestmove")));
}