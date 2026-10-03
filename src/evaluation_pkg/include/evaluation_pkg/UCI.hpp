#ifndef H_UCI
#define H_UCI

#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

//These functions are implementations for this UCI protocol following this documentation:
// https://gist.github.com/DOBRO/2592c6dad754ba67e6dcaec8c90165bf


// Pure UCI protocol layer: builds command lines, parses engine output.
// No process / pipe / thread code in here, and no dependency on boardState.
// Moves are plain long-algebraic strings ("e2e4", "e7e8q"); StockfishNode
// converts to/from the board's own move type.
namespace uci {


// GUI -> Engine: builders. Each returns one command line WITHOUT the trailing
// '\n'; the transport layer appends it when writing to the engine.

std::string cmdUci();
std::string cmdDebug(bool on);
std::string cmdIsReady();
std::string cmdSetOption(const std::string& name,
                         const std::optional<std::string>& value = std::nullopt);
std::string cmdUciNewGame();
std::string cmdPosition(const std::optional<std::string>& fen,   // nullopt -> "startpos"
                        const std::vector<std::string>& moves = {});
std::string cmdStop();
std::string cmdPonderHit();
std::string cmdQuit();

// One struct replaces the 12 go* functions. Unset fields are simply omitted.
struct GoParams {
    std::vector<std::string> searchMoves;
    bool ponder   = false;
    bool infinite = false;
    std::optional<int>      wtime, btime, winc, binc, movesToGo;  // ms / count
    std::optional<int>      depth, mate, moveTime;
    std::optional<uint64_t> nodes;
};
std::string cmdGo(const GoParams& p);


// Engine -> GUI: message types. Each is one kind of line Stockfish can send.


// "uciok" - engine finished the uci handshake.
struct UciOk {};

// "readyok" - answer to "isready".
struct ReadyOk {};

// "id name <x>" / "id author <x>"
struct IdLine {
    enum class Field { Name, Author } field = Field::Name;
    std::string value;
};

// "option name <id> type <t> [default <x>] [min <x>] [max <x>] [var <x>]..."
struct Option {
    enum class Type { Check, Spin, Combo, Button, String } type = Type::String;
    std::string              name;           // may contain spaces ("Clear Hash")
    std::string              defaultValue;
    std::optional<int>       min, max;       // spin
    std::vector<std::string> vars;           // combo
};

struct Score {
    enum class Kind  { Cp, Mate } kind = Kind::Cp;   // Mate: moves, negative = being mated
    enum class Bound { Exact, Lower, Upper } bound = Bound::Exact;
    int value = 0;
};

// "info ..." - any subset of these fields may be present on a given line.
struct Info {
    std::optional<int>         depth, seldepth, multipv, time, nps, hashfull, tbhits, currMoveNumber;
    std::optional<uint64_t>    nodes;
    std::optional<Score>       score;
    std::optional<std::string> currMove;
    std::optional<std::string> text;         // "info string ..." (rest of line)
    std::vector<std::string>   pv;
};

// "bestmove <move1> [ponder <move2>]"
struct BestMove {
    std::string                move;         // "(none)" if there are no legal moves
    std::optional<std::string> ponder;
};

// Anything else (startup banner, non-UCI output, malformed lines). Keeps the
// raw text so the node can log it; normally just ignored.
struct Unknown {
    std::string raw;
};

using Message = std::variant<UciOk, ReadyOk, IdLine, Option, Info, BestMove, Unknown>;

// Parse one line of engine output (no trailing newline). Never fails:
// unrecognised or malformed input yields Unknown.
Message parseLine(const std::string& line);

// Helper for std::visit with lambdas:
//   std::visit(uci::overloaded{ [&](const uci::Info& i){...}, ... }, msg);
template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;  // needed pre-C++20

}  // namespace uci

#endif