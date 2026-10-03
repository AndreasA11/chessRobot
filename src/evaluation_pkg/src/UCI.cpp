#include "UCI.hpp"

#include <charconv>
#include <sstream>
#include <unordered_set>

namespace uci {

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
std::vector<std::string> tokenize(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

// Join tokens[from, to) with single spaces.
std::string join(const std::vector<std::string>& t, size_t from, size_t to) {
    std::string out;
    for (size_t i = from; i < to && i < t.size(); ++i) {
        if (!out.empty()) out += ' ';
        out += t[i];
    }
    return out;
}

template <class T>
std::optional<T> toNumber(const std::string& s) {
    T value{};
    const char* first = s.data();
    const char* last  = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc() || ptr != last) return std::nullopt;
    return value;
}

// ---------------------------------------------------------------------------
// id
// ---------------------------------------------------------------------------
Message parseId(const std::vector<std::string>& t, const std::string& line) {
    if (t.size() < 3) {return Unknown{line};}
    IdLine id;
    if (t[1] == "name") {       id.field = IdLine::Field::Name;}
    else if (t[1] == "author") {id.field = IdLine::Field::Author;}
    else {                      return Unknown{line};}
    id.value = join(t, 2, t.size());
    return id;
}

// ---------------------------------------------------------------------------
// option name <id> type <t> [default <x>] [min <x>] [max <x>] [var <x>]...
// <id>, <x> and var values may contain spaces, so values run until the next
// keyword.
// ---------------------------------------------------------------------------
bool isOptionKeyword(const std::string& s) {
    return s == "name" || s == "type" || s == "default" || s == "min" ||
           s == "max"  || s == "var";
}

Message parseOption(const std::vector<std::string>& t, const std::string& line) {
    if (t.size() < 2 || t[1] != "name") return Unknown{line};

    Option opt;
    std::string typeStr;
    std::string minStr, maxStr;
    bool haveType = false;

    size_t i = 1;
    while (i < t.size()) {
        const std::string& key = t[i];
        if (!isOptionKeyword(key)) { ++i; continue; }

        size_t j = i + 1;
        while (j < t.size() && !isOptionKeyword(t[j])) ++j;
        std::string value = join(t, i + 1, j);

        if (key == "name")         opt.name = value;
        else if (key == "type")  { typeStr = value; haveType = true; }
        else if (key == "default") opt.defaultValue = value;
        else if (key == "min")     minStr = value;
        else if (key == "max")     maxStr = value;
        else if (key == "var")     opt.vars.push_back(value);
        i = j;
    }

    if (opt.name.empty() || !haveType) return Unknown{line};

    if      (typeStr == "check")  opt.type = Option::Type::Check;
    else if (typeStr == "spin")   opt.type = Option::Type::Spin;
    else if (typeStr == "combo")  opt.type = Option::Type::Combo;
    else if (typeStr == "button") opt.type = Option::Type::Button;
    else if (typeStr == "string") opt.type = Option::Type::String;
    else return Unknown{line};

    if (!minStr.empty()) opt.min = toNumber<int>(minStr);
    if (!maxStr.empty()) opt.max = toNumber<int>(maxStr);
    return opt;
}

// ---------------------------------------------------------------------------
// info ...
// ---------------------------------------------------------------------------
bool isInfoKeyword(const std::string& s) {
    static const std::unordered_set<std::string> keywords = {
        "depth", "seldepth", "time", "nodes", "pv", "multipv", "score",
        "currmove", "currmovenumber", "hashfull", "nps", "tbhits", "sbhits",
        "cpuload", "string", "refutation", "currline", "wdl"};
    return keywords.count(s) > 0;
}

Message parseInfo(const std::vector<std::string>& t) {
    Info info;
    size_t i = 1;  // t[0] == "info"

    // Reads the single-token integer after t[i] into dst (if it parses).
    auto readInt = [&](std::optional<int>& dst) {
        if (i + 1 < t.size()) dst = toNumber<int>(t[i + 1]);
        i += 2;
    };

    while (i < t.size()) {
        const std::string& key = t[i];

        if      (key == "depth")           readInt(info.depth);
        else if (key == "seldepth")        readInt(info.seldepth);
        else if (key == "multipv")         readInt(info.multipv);
        else if (key == "time")            readInt(info.time);
        else if (key == "nps")             readInt(info.nps);
        else if (key == "hashfull")        readInt(info.hashfull);
        else if (key == "tbhits")          readInt(info.tbhits);
        else if (key == "currmovenumber")  readInt(info.currMoveNumber);
        else if (key == "nodes") {
            if (i + 1 < t.size()) info.nodes = toNumber<uint64_t>(t[i + 1]);
            i += 2;
        }
        else if (key == "currmove") {
            if (i + 1 < t.size()) info.currMove = t[i + 1];
            i += 2;
        }
        else if (key == "score") {
            // score (cp <x> | mate <y>) [lowerbound | upperbound]
            ++i;
            if (i + 1 < t.size() && (t[i] == "cp" || t[i] == "mate")) {
                Score s;
                s.kind = (t[i] == "cp") ? Score::Kind::Cp : Score::Kind::Mate;
                auto v = toNumber<int>(t[i + 1]);
                i += 2;
                if (v) {
                    s.value = *v;
                    if (i < t.size() && t[i] == "lowerbound") { s.bound = Score::Bound::Lower; ++i; }
                    else if (i < t.size() && t[i] == "upperbound") { s.bound = Score::Bound::Upper; ++i; }
                    info.score = s;
                }
            }
        }
        else if (key == "pv") {
            ++i;
            while (i < t.size() && !isInfoKeyword(t[i])) info.pv.push_back(t[i++]);
        }
        else if (key == "string") {
            info.text = join(t, i + 1, t.size());   // rest of line
            break;
        }
        else if (key == "refutation" || key == "currline") {
            break;                                   // not exposed; runs to end of line
        }
        else if (key == "sbhits" || key == "cpuload") {
            i += 2;                                  // known, but not stored
        }
        else if (key == "wdl") {
            i += 4;                                  // "wdl <w> <d> <l>", not stored
        }
        else {
            ++i;                                     // unknown token: skip
        }
    }
    return info;
}

// ---------------------------------------------------------------------------
// bestmove <move1> [ponder <move2>]
// ---------------------------------------------------------------------------
Message parseBestMove(const std::vector<std::string>& t, const std::string& line) {
    if (t.size() < 2) return Unknown{line};
    BestMove bm;
    bm.move = t[1];
    if (t.size() >= 4 && t[2] == "ponder") bm.ponder = t[3];
    return bm;
}

}  // namespace

// ===========================================================================
// GUI -> Engine
// ===========================================================================
std::string cmdUci()        { return "uci"; }
std::string cmdIsReady()    { return "isready"; }
std::string cmdUciNewGame() { return "ucinewgame"; }
std::string cmdStop()       { return "stop"; }
std::string cmdPonderHit()  { return "ponderhit"; }
std::string cmdQuit()       { return "quit"; }

std::string cmdDebug(bool on) { return on ? "debug on" : "debug off"; }

std::string cmdSetOption(const std::string& name, const std::optional<std::string>& value) {
    std::string cmd = "setoption name " + name;
    if (value) cmd += " value " + *value;   // buttons have no value
    return cmd;
}

std::string cmdPosition(const std::optional<std::string>& fen,
                        const std::vector<std::string>& moves) {
    std::string cmd = "position ";
    cmd += fen ? "fen " + *fen : "startpos";
    if (!moves.empty()) {
        cmd += " moves";
        for (const auto& m : moves) cmd += " " + m;
    }
    return cmd;
}

std::string cmdGo(const GoParams& p) {
    std::string cmd = "go";
    auto add = [&](const char* key, const auto& opt) {
        if (opt) cmd += std::string(" ") + key + " " + std::to_string(*opt);
    };

    if (p.ponder)   cmd += " ponder";
    if (p.infinite) cmd += " infinite";
    add("wtime", p.wtime);
    add("btime", p.btime);
    add("winc", p.winc);
    add("binc", p.binc);
    add("movestogo", p.movesToGo);
    add("depth", p.depth);
    add("nodes", p.nodes);
    add("mate", p.mate);
    add("movetime", p.moveTime);

    // searchmoves swallows every following move token, so it goes last.
    if (!p.searchMoves.empty()) {
        cmd += " searchmoves";
        for (const auto& m : p.searchMoves) cmd += " " + m;
    }
    return cmd;
}

// ===========================================================================
// Engine -> GUI
// ===========================================================================
Message parseLine(const std::string& line) {
    const auto t = tokenize(line);
    if (t.empty()) return Unknown{line};

    const std::string& head = t[0];
    if (head == "uciok")    return UciOk{};
    if (head == "readyok")  return ReadyOk{};
    if (head == "id")       return parseId(t, line);
    if (head == "option")   return parseOption(t, line);
    if (head == "info")     return parseInfo(t);
    if (head == "bestmove") return parseBestMove(t, line);
    return Unknown{line};
}

}  // namespace uci