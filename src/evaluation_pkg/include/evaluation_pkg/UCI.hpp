#ifndef H_UCI
#define H_UCI

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// Pure UCI protocol layer: builds command lines and parses engine output.
namespace uci {

// GUI -> Engine: builders
std::string cmdUci();
std::string cmdDebug(bool on);
std::string cmdIsReady();
std::string cmdSetOption(const std::string &name,
                         const std::optional<std::string> &value = std::nullopt);
std::string cmdUciNewGame();
std::string cmdPosition(const std::optional<std::string> &fen,
                        const std::vector<std::string> &moves = {});
std::string cmdStop();
std::string cmdPonderHit();
std::string cmdQuit();

struct GoParams {
    std::vector<std::string> searchMoves{};
    std::optional<uint64_t> nodes;
    std::optional<int> wtime;
    std::optional<int> btime;
    std::optional<int> winc;
    std::optional<int> binc;
    std::optional<int> movesToGo;
    std::optional<int> depth;
    std::optional<int> mate;
    std::optional<int> moveTime;
    bool ponder = false;
    bool infinite = false;
};

std::string cmdGo(const GoParams &p);

// Engine -> GUI: message types
struct UciOk {};

struct ReadyOk {};

struct IdLine {
    enum class Field {
        Name,
        Author
    } field = Field::Name;
    std::string value;
};

struct Option {
    enum class Type {
        Check,
        Spin,
        Combo,
        Button,
        String
    } type = Type::String;
    std::vector<std::string> vars{};
    std::string name;
    std::string defaultValue;
    std::optional<int> min;
    std::optional<int> max;
};

struct Score {
    enum class Kind {
        Cp,
        Mate
    } kind = Kind::Cp;
    enum class Bound {
        Exact,
        Lower,
        Upper
    } bound = Bound::Exact;
    int value = 0;
};

struct Info {
    std::vector<std::string> pv{};
    std::optional<std::string> currMove;
    std::optional<std::string> text;
    std::optional<uint64_t> nodes;
    std::optional<Score> score;
    std::optional<int> depth;
    std::optional<int> seldepth;
    std::optional<int> multipv;
    std::optional<int> time;
    std::optional<int> nps;
    std::optional<int> hashfull;
    std::optional<int> tbhits;
    std::optional<int> currMoveNumber;
};

struct BestMove {
    std::string move;
    std::optional<std::string> ponder;
};

struct Unknown {
    std::string raw;
};

using Message = std::variant<UciOk, ReadyOk, IdLine, Option, Info, BestMove, Unknown>;

Message parseLine(const std::string &line);

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

} // namespace uci

#endif