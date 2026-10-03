#include "UCI.hpp"
#include <cassert>
#include <iostream>
using namespace uci;
template <class T> const T& as(const Message& m){ assert(std::holds_alternative<T>(m)); return std::get<T>(m); }
int main(){
    // builders
    assert(cmdPosition(std::nullopt) == "position startpos");
    assert(cmdPosition(std::nullopt,{"e2e4","e7e5"}) == "position startpos moves e2e4 e7e5");
    assert(cmdPosition(std::string("8/8/8/8/8/8/8/K6k w - - 0 1")) == "position fen 8/8/8/8/8/8/8/K6k w - - 0 1");
    assert(cmdSetOption("Clear Hash") == "setoption name Clear Hash");
    assert(cmdSetOption("Hash","128") == "setoption name Hash value 128");
    GoParams g; g.depth=12; g.wtime=1000; g.searchMoves={"e2e4","d2d4"};
    assert(cmdGo(g) == "go wtime 1000 depth 12 searchmoves e2e4 d2d4");
    GoParams inf; inf.infinite=true; assert(cmdGo(inf)=="go infinite");
    // simple
    assert(std::holds_alternative<UciOk>(parseLine("uciok")));
    assert(std::holds_alternative<ReadyOk>(parseLine("readyok\r")));
    assert(std::holds_alternative<Unknown>(parseLine("")));
    assert(std::holds_alternative<Unknown>(parseLine("Stockfish 17 by the Stockfish developers")));
    // id
    auto id = as<IdLine>(parseLine("id name Stockfish 17.1"));
    assert(id.field==IdLine::Field::Name && id.value=="Stockfish 17.1");
    assert(as<IdLine>(parseLine("id author the Stockfish developers (see AUTHORS file)")).field==IdLine::Field::Author);
    // options
    auto o1 = as<Option>(parseLine("option name Hash type spin default 16 min 1 max 33554432"));
    assert(o1.name=="Hash" && o1.type==Option::Type::Spin && o1.defaultValue=="16" && *o1.min==1 && *o1.max==33554432);
    auto o2 = as<Option>(parseLine("option name Clear Hash type button"));
    assert(o2.name=="Clear Hash" && o2.type==Option::Type::Button);
    auto o3 = as<Option>(parseLine("option name Style type combo default Normal var Solid var Normal var Risky"));
    assert(o3.vars.size()==3 && o3.vars[2]=="Risky");
    auto o4 = as<Option>(parseLine("option name Debug Log File type string default <empty>"));
    assert(o4.name=="Debug Log File" && o4.defaultValue=="<empty>");
    auto o5 = as<Option>(parseLine("option name Ponder type check default false"));
    assert(o5.type==Option::Type::Check && o5.defaultValue=="false");
    // info
    auto i1 = as<Info>(parseLine("info depth 12 seldepth 18 multipv 1 score cp 34 nodes 123456 nps 987654 hashfull 12 tbhits 0 time 125 pv e2e4 e7e5 g1f3"));
    assert(*i1.depth==12 && *i1.seldepth==18 && *i1.multipv==1 && i1.score->value==34 && *i1.nodes==123456
        && *i1.nps==987654 && *i1.time==125 && i1.pv.size()==3 && i1.pv[2]=="g1f3");
    auto i2 = as<Info>(parseLine("info depth 20 score mate -3 lowerbound wdl 0 0 1000 nodes 5 pv a1a2"));
    assert(i2.score->kind==Score::Kind::Mate && i2.score->value==-3 && i2.score->bound==Score::Bound::Lower && *i2.nodes==5 && i2.pv.size()==1);
    auto i3 = as<Info>(parseLine("info string NNUE evaluation using nn-1111.nnue"));
    assert(*i3.text=="NNUE evaluation using nn-1111.nnue");
    auto i4 = as<Info>(parseLine("info currmove e2e4 currmovenumber 1"));
    assert(*i4.currMove=="e2e4" && *i4.currMoveNumber==1);
    as<Info>(parseLine("info depth")); // truncated line must not crash
    as<Info>(parseLine("info score cp"));
    // bestmove
    auto b1 = as<BestMove>(parseLine("bestmove e2e4 ponder e7e5"));
    assert(b1.move=="e2e4" && *b1.ponder=="e7e5");
    auto b2 = as<BestMove>(parseLine("bestmove (none)"));
    assert(b2.move=="(none)" && !b2.ponder);
    assert(std::holds_alternative<Unknown>(parseLine("bestmove")));
    std::cout<<"all ok\n";
}