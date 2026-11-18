#pragma once
// Quick blocks: one-block ready-made actions for beginners ("Move With Keys", "Collect On Touch", ...).
// A quick block is ONE node in the editor and in the saved file (its type token plus the values typed on it).
// When a graph is loaded, ExpandQuickBlocks turns each block into the ordinary low-level nodes and wires it stands
// for, using the data table below and one generic expander. Nothing here runs by itself: the low-level nodes do the
// work, so a block can never behave differently from the nodes it is built from.
// Written for this project (no outside code). Block text language, one line each:
//   N a Type args   a low-level node (a..z are local names)         D a pin value   a typed value on a pin
//   X a pin b       a flow wire                                      W c inPin p outPin   a data wire
//   E entry a       register node a as an event entry               IN a            where incoming flow enters
//   OUT a pin       which flow pin of which node is the block's "Then do"
// Field placeholders: {0} the field as a word, {v0} as a number or quoted text, {q0} always quoted.
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay::vs {

struct QbField { const char* name; const char* def; };

struct QuickBlock {
    const char* type;          // the saved token
    const char* title;         // plain display name
    const char* group;         // Movement, Game rules, World, Feedback, Flow
    const char* description;   // one sentence
    const char* example;       // a tiny example
    bool flowIn;               // has a flow input (put it after an event); false: it starts on its own event
    bool thenOut;              // has a "Then do" flow output
    std::vector<QbField> fields;
    const char* body;
};

inline const std::vector<QuickBlock>& QuickBlocks() {
    static const std::vector<QuickBlock> all = {
        // ---- Movement
        {"QbMoveWithKeys", "Move With Keys", "Movement",
         "Slides this object while you hold the four keys, all the time.", "Hold the right arrow to go right.", false, false,
         {{"Left key", "left"}, {"Right key", "right"}, {"Up key", "up"}, {"Down key", "down"}, {"Speed", "5"}},
         "N u OnUpdate\nE OnUpdate u\nN a Key {1}\nN b Key {0}\nN c Sub\nW c 0 a 0\nW c 1 b 0\nN d Mul\nD d 1 {v4}\nW d 0 c 0\nN e DeltaTime\n"
         "N f Mul\nW f 0 d 0\nW f 1 e 0\nN g Key {2}\nN h Key {3}\nN i Sub\nW i 0 g 0\nW i 1 h 0\nN j Mul\nD j 1 {v4}\n"
         "W j 0 i 0\nN k Mul\nW k 0 j 0\nW k 1 e 0\nN t Translate\nW t 0 f 0\nW t 1 k 0\nD t 2 0\nX u 0 t\nIN u\n"},
        {"QbMoveToward", "Move To Spot", "Movement",
         "Walks this object toward a spot, all the time.", "Walk to the spot 5 steps right.", false, false,
         {{"Right", "5"}, {"Up", "0"}, {"Forward", "0"}, {"Speed", "2"}},
         "N u OnUpdate\nE OnUpdate u\nN a GlMoveToPosition\nD a 0 {v0}\nD a 1 {v1}\nD a 2 {v2}\nD a 3 {v3}\nX u 0 a\nIN u\n"},
        {"QbFollowObject", "Follow Object", "Movement",
         "Walks toward the object with this name, all the time.", "A ghost follows the object named Player.", false, false,
         {{"Who", "Player"}, {"Speed", "3"}},
         "N u OnUpdate\nE OnUpdate u\nN a MoveTowards {0}\nD a 0 {v1}\nX u 0 a\nIN u\n"},
        {"QbPatrol", "Walk Back And Forth", "Movement",
         "Swings this object left and right of where it started, all the time.", "A guard walks 3 steps each way, or a platform slides.", false, false,
         {{"Distance", "3"}, {"Speed", "1.5"}},
         "N a OnUpdate\nE OnUpdate a\nN b GlMoveBackAndForth\nD b 0 {v0}\nD b 1 {v1}\nX a 0 b\nIN a\n"},
        {"QbJumpOnKey", "Jump On Key", "Movement",
         "Jumps up when you press the key.", "Press Space to jump.", false, true,
         {{"Key", "space"}, {"Strength", "7"}},
         "N a OnKey {0}\nE OnKey:{0} a\nN b AddImpulse\nD b 0 0\nD b 1 {v1}\nD b 2 0\nX a 0 b\nIN a\nOUT b 0\n"},
        {"QbTurnToFace", "Turn To Face", "Movement",
         "Turns this object to look at the object with this name.", "A turret turns to face the Player.", true, true,
         {{"Who", "Player"}},
         "N a LookAt {0}\nIN a\nOUT a 0\n"},
        // ---- Game rules
        {"QbAddToScore", "Add To Score", "Game rules",
         "Adds points to the score.", "Add 1 point for each coin.", true, true,
         {{"Points", "1"}},
         "N a ScoreAdd\nD a 0 {v0}\nIN a\nOUT a 0\n"},
        {"QbShowScore", "Show Score", "Game rules",
         "Writes the score on this object's text, all the time.", "Show Score: 3 on the screen.", false, false,
         {{"Label", "Score: "}},
         "N a OnUpdate\nE OnUpdate a\nN s ScoreGet\nN x GlShowNumberInText\nD x 0 {v0}\nW x 1 s 0\nD x 2 0\nX a 0 x\nIN a\n"},
        {"QbLoseHealth", "Lose Health", "Game rules",
         "Takes health away from this object.", "A spike takes 10 health.", true, true,
         {{"Amount", "10"}},
         "N a Damage\nD a 0 {v0}\nIN a\nOUT a 0\n"},
        {"QbGameOverAtZeroHealth", "Game Over When Health Is 0", "Game rules",
         "Ends the game and says You lose when this object has no health left.", "The hero dies and the game is over.", false, false,
         {},
         "N a OnDeath\nE OnDeath a\nN b Lose\nX a 0 b\nIN a\n"},
        {"QbGameOverOnTouch", "Game Over When Touched", "Game rules",
         "Ends the game and says You lose when something touches this object.", "Touch the enemy and the game is over.", false, false,
         {},
         "N a OnTrigger\nE OnTrigger a\nN b Lose\nX a 0 b\nIN a\n"},
        {"QbWinAtScore", "Win When Score Reaches", "Game rules",
         "Ends the game and says You win when the score gets this high.", "Win at 5 coins.", false, false,
         {{"Score", "5"}},
         "N a OnUpdate\nE OnUpdate a\nN s ScoreGet\nN c Compare >=\nW c 0 s 0\nD c 1 {v0}\nN b Branch\nW b 0 c 0\nX a 0 b\n"
         "N w Win\nX b 0 w\nIN a\n"},
        {"QbEverySeconds", "Do Every Few Seconds", "Game rules",
         "Does the next thing again and again, after this many seconds.", "Do something every 2 seconds.", false, true,
         {{"Seconds", "2"}},
         "N a OnTimer {0}\nE OnTimer:{0} a\nIN a\nOUT a 0\n"},
        {"QbRespawnAtCheckpoint", "Go Back To Checkpoint", "Game rules",
         "Puts this object back at the last saved checkpoint.", "After falling, return to the flag.", true, true,
         {},
         "N a CheckpointLoad\nIN a\nOUT a 0\n"},
        // ---- Things in the world
        {"QbCollectOnTouch", "Collect Coin On Touch", "World",
         "When touched, like a coin: adds points, plays this object's sound and hides it.", "A coin that gives 1 point.", false, false,
         {{"Points", "1"}},
         "N a OnTrigger\nE OnTrigger a\nN b ScoreAdd\nD b 0 {v0}\nX a 0 b\nN p PlaySound\nX b 0 p\nN c SetActive self\nD c 0 0\nX p 0 c\nIN a\n"},
        {"QbOpenDoorWhenTouched", "Open Door When Touched", "World",
         "When touched, this object slides up out of the way.", "A door that opens when you walk into it.", false, false,
         {{"Lift", "2"}},
         "N a OnTrigger\nE OnTrigger a\nN b Translate\nD b 0 0\nD b 1 {v0}\nD b 2 0\nX a 0 b\nIN a\n"},
        {"QbSpinningObject", "Spinning Object", "World",
         "Turns this object around, all the time.", "A fan that spins.", false, false,
         {{"Speed", "90"}},
         "N a OnUpdate\nE OnUpdate a\nN b Spin\nD b 0 {v0}\nX a 0 b\nIN a\n"},
        {"QbShootOnKey", "Shoot On Key", "World",
         "Fires a copy of a saved object forward when you press the key.", "Press Space to fire a bullet.", false, true,
         {{"Key", "space"}, {"What", "Assets/bullet.okayprefab"}, {"Speed", "10"}},
         "N a OnKey {0}\nE OnKey:{0} a\nN b SpawnProjectile {1}\nD b 0 {v2}\nX a 0 b\nIN a\nOUT b 0\n"},
        {"QbSpawnEvery", "Spawn Copy Every Few Seconds", "World",
         "Makes a new copy of a saved object again and again.", "A cannon that makes a ball every 2 seconds.", false, false,
         {{"What", "Assets/ball.okayprefab"}, {"Seconds", "2"}},
         "N a OnUpdate\nE OnUpdate a\nN b SpawnEvery {0}\nD b 0 {v1}\nX a 0 b\nIN a\n"},
        {"QbSpawnCopyAt", "Spawn Copy At", "World",
         "Makes a copy of a saved object at a spot.", "Drop a coin at 2 steps up.", true, true,
         {{"What", "Assets/coin.okayprefab"}, {"Right", "0"}, {"Up", "0"}, {"Forward", "0"}},
         "N a Spawn {0}\nD a 0 {v1}\nD a 1 {v2}\nD a 2 {v3}\nIN a\nOUT a 0\n"},
        {"QbRemoveAfter", "Remove After A While", "World",
         "Takes this object away after some seconds.", "A bullet disappears after 3 seconds.", false, false,
         {{"Seconds", "3"}},
         "N a OnStart\nE OnStart a\nN b DestroyAfter\nD b 0 {v0}\nX a 0 b\nIN a\n"},
        // ---- Feedback
        {"QbShowText", "Show Text On Screen", "Feedback",
         "Shows a message on this object's text for a few seconds.", "Show Hello! for 2 seconds.", true, true,
         {{"Text", "Hello!"}, {"Seconds", "2"}},
         "N a SetText self\nD a 0 {v0}\nN w Delay {1}\nX a 0 w\nN c SetText self\nD c 0 \"\"\nX w 0 c\nIN a\nOUT a 0\n"},
        {"QbPlaySoundOnKey", "Play Sound On Key", "Feedback",
         "Plays this object's sound when you press the key.", "Press Space to hear a beep.", false, true,
         {{"Key", "space"}},
         "N a OnKey {0}\nE OnKey:{0} a\nN b PlaySound\nX a 0 b\nIN a\nOUT b 0\n"},
        {"QbFlashColour", "Flash Colour", "Feedback",
         "Turns this object a colour for a moment, then back to white.", "A hit flashes red.", true, true,
         {{"Red", "1"}, {"Green", "0"}, {"Blue", "0"}, {"Seconds", "0.2"}},
         "N a SetColor\nD a 0 {v0}\nD a 1 {v1}\nD a 2 {v2}\nD a 3 1\nN w Delay {3}\nX a 0 w\nN c SetColor\nD c 0 1\nD c 1 1\nD c 2 1\nD c 3 1\nX w 0 c\nIN a\nOUT a 0\n"},
        {"QbFollowWithCamera", "Follow With Camera", "Feedback",
         "Makes the camera follow the object with this name.", "The camera follows the Player.", true, true,
         {{"Who", "Player"}, {"Smoothness", "5"}},
         "N f FindObject {0}\nN a SetCameraTarget\nW a 0 f 0\nD a 1 {v1}\nIN a\nOUT a 0\n"},
        // ---- Flow
        {"QbWaitThenDo", "Wait Then Do", "Flow",
         "Waits, then does the next thing.", "Wait 2 seconds, then open the door.", true, true,
         {{"Seconds", "1"}},
         "N a Delay {0}\nIN a\nOUT a 0\n"},
        {"QbRepeat", "Repeat Times", "Flow",
         "Does the next thing again and again, this many times.", "Spawn 5 coins.", true, true,
         {{"Times", "3"}},
         "N a Repeat {0}\nIN a\nOUT a 0\n"},
        {"QbDoForever", "Do Forever", "Flow",
         "Does the next thing on every frame, all the time.", "Spin all the time.", false, true,
         {},
         "N a OnUpdate\nE OnUpdate a\nIN a\nOUT a 0\n"},
        {"QbIfKeyThenDo", "If Key Pressed Do", "Flow",
         "Does the next thing only while the key is held down.", "While Shift is held, run.", true, true,
         {{"Key", "space"}},
         "N k Key {0}\nN b Branch\nW b 0 k 0\nIN b\nOUT b 0\n"},
        {"QbWhenTouchedDo", "When Touched Do", "Flow",
         "Does the next thing when something touches this object.", "When touched, add a point.", false, true,
         {},
         "N a OnTrigger\nE OnTrigger a\nIN a\nOUT a 0\n"},
    };
    return all;
}

inline const QuickBlock* FindQuickBlock(const std::string& type) {
    static const std::unordered_map<std::string, const QuickBlock*> idx = [] {
        std::unordered_map<std::string, const QuickBlock*> m;
        for (const QuickBlock& b : QuickBlocks()) m[b.type] = &b;
        return m;
    }();
    auto it = idx.find(type);
    return it == idx.end() ? nullptr : it->second;
}

namespace qb_detail {
inline std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
inline std::string Unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        std::string out;
        for (std::size_t i = 1; i + 1 < s.size(); ++i) {
            if (s[i] == '\\' && i + 2 < s.size()) ++i;
            out += s[i];
        }
        return out;
    }
    return s;
}
inline std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) { if (c == '"' || c == '\\') out += '\\'; out += c; }
    return out + "\"";
}
inline bool IsNumberOrBool(const std::string& v) {
    if (v == "true" || v == "false") return true;
    if (v.empty()) return false;
    char* end = nullptr;
    std::strtod(v.c_str(), &end);
    return end && *end == '\0';
}
inline std::string Literal(const std::string& v) { return IsNumberOrBool(v) ? v : Quote(v); }
inline std::string Word(const std::string& v) {
    bool plain = !v.empty();
    for (char c : v) if (std::isspace((unsigned char)c) || c == '"' || c == '#') plain = false;
    return plain ? v : Quote(v);
}
// Splits off the first `n` space-separated words; `rest` gets the text after them.
inline std::vector<std::string> Words(const std::string& line, std::size_t n, std::string* rest) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (out.size() < n) {
        while (i < line.size() && line[i] == ' ') ++i;
        if (i >= line.size()) break;
        std::size_t j = i;
        while (j < line.size() && line[j] != ' ') ++j;
        out.push_back(line.substr(i, j - i));
        i = j;
    }
    while (i < line.size() && line[i] == ' ') ++i;
    if (rest) *rest = i < line.size() ? line.substr(i) : std::string();
    return out;
}
inline std::string Fill(const std::string& tmpl, const std::vector<std::string>& f) {
    std::string out;
    for (std::size_t i = 0; i < tmpl.size(); ++i) {
        if (tmpl[i] != '{') { out += tmpl[i]; continue; }
        const std::size_t e = tmpl.find('}', i);
        if (e == std::string::npos) { out += tmpl[i]; continue; }
        std::string key = tmpl.substr(i + 1, e - i - 1);
        char mode = 'w';
        if (!key.empty() && (key[0] == 'v' || key[0] == 'q')) { mode = key[0]; key = key.substr(1); }
        const std::size_t k = (std::size_t)std::atoi(key.c_str());
        const std::string& v = k < f.size() ? f[k] : std::string();
        out += mode == 'v' ? Literal(v) : mode == 'q' ? Quote(v) : Word(v);
        i = e;
    }
    return out;
}
} // namespace qb_detail

/// How many nodes and wires a block stands for once expanded (for the budget notes in the tests).
struct QbSize { int nodes = 0, wires = 0; };

/// Turns every quick block in a saved graph into the low-level nodes and wires it stands for. Graphs without a
/// quick block come back unchanged. `outIds` (optional) is not used by the runtime.
inline std::string ExpandQuickBlocks(const std::string& text) {
    using namespace qb_detail;
    if (text.find("node ") == std::string::npos || text.find(" Qb") == std::string::npos) return text;
    struct Inst { const QuickBlock* b; std::vector<std::string> fields; int base; };
    std::unordered_map<int, Inst> blocks;
    std::vector<std::string> lines;
    { std::istringstream in(text); std::string l; while (std::getline(in, l)) lines.push_back(l); }
    int counter = 0;
    for (const std::string& l : lines) {
        if (l.compare(0, 5, "node ") != 0) continue;
        std::string rest;
        const auto w = Words(l, 3, &rest);
        if (w.size() < 3) continue;
        const QuickBlock* b = FindQuickBlock(w[2]);
        if (!b) continue;
        Inst inst{b, {}, 1000000 + 100 * counter++};
        for (const QbField& f : b->fields) inst.fields.push_back(f.def);
        blocks[std::atoi(w[1].c_str())] = inst;
    }
    if (blocks.empty()) return text;
    // Typed field values: "default <id> <pin> <value>".
    for (const std::string& l : lines) {
        if (l.compare(0, 8, "default ") != 0) continue;
        std::string rest;
        const auto w = Words(l, 3, &rest);
        if (w.size() < 3) continue;
        auto it = blocks.find(std::atoi(w[1].c_str()));
        if (it == blocks.end()) continue;
        const std::size_t pin = (std::size_t)std::atoi(w[2].c_str());
        if (pin < it->second.fields.size()) it->second.fields[pin] = Unquote(Trim(rest));
    }
    // Where flow enters and leaves each block (found while building its lines).
    struct Ends { int in = -1; int out = -1; int outPin = 0; };
    std::unordered_map<int, Ends> ends;
    std::string extra;
    for (auto& kv : blocks) {
        Inst& inst = kv.second;
        auto idOf = [&](const std::string& name) { return inst.base + (name.empty() ? 0 : (name[0] - 'a' + 1)); };
        Ends e;
        std::istringstream body(inst.b->body);
        std::string l;
        while (std::getline(body, l)) {
            l = Fill(l, inst.fields);
            std::string rest;
            const auto w = Words(l, 1, &rest);
            if (w.empty()) continue;
            const std::string& k = w[0];
            if (k == "N") {
                std::string args;
                const auto n = Words(rest, 2, &args);
                extra += "node " + std::to_string(idOf(n[0])) + " " + n[1] + (args.empty() ? "" : " " + args) + "\n";
            } else if (k == "D") {
                std::string val;
                const auto n = Words(rest, 2, &val);
                extra += "default " + std::to_string(idOf(n[0])) + " " + n[1] + " " + val + "\n";
            } else if (k == "X") {
                const auto n = Words(rest, 3, nullptr);
                extra += "exec " + std::to_string(idOf(n[0])) + " " + n[1] + " " + std::to_string(idOf(n[2])) + "\n";
            } else if (k == "W") {
                const auto n = Words(rest, 4, nullptr);
                extra += "data " + std::to_string(idOf(n[0])) + " " + n[1] + " " + std::to_string(idOf(n[2])) + " " + n[3] + "\n";
            } else if (k == "E") {
                std::string name;
                const auto n = Words(rest, 2, &name);
                extra += "entry " + Word(n[0]) + " " + std::to_string(idOf(n[1])) + "\n";
            } else if (k == "IN") {
                e.in = idOf(Trim(rest));
            } else if (k == "OUT") {
                const auto n = Words(rest, 2, nullptr);
                e.out = idOf(n[0]);
                e.outPin = n.size() > 1 ? std::atoi(n[1].c_str()) : 0;
            }
        }
        ends[kv.first] = e;
    }
    std::string out;
    for (const std::string& l : lines) {
        std::string rest;
        const auto w = Words(l, 4, &rest);
        auto isBlock = [&](const std::string& s) { return blocks.count(std::atoi(s.c_str())) > 0; };
        if (w.size() >= 3 && w[0] == "node" && isBlock(w[1]) && FindQuickBlock(w[2])) continue;
        if (w.size() >= 2 && (w[0] == "default" || w[0] == "pos") && isBlock(w[1])) continue;
        if (w.size() >= 4 && w[0] == "exec") {
            std::string from = w[1], to = w[3], pin = w[2];
            if (isBlock(to)) { const int in = ends[std::atoi(to.c_str())].in; if (in < 0) continue; to = std::to_string(in); }
            if (isBlock(from)) {
                const Ends& e = ends[std::atoi(from.c_str())];
                if (e.out < 0 || std::atoi(pin.c_str()) != 0) continue;
                from = std::to_string(e.out); pin = std::to_string(e.outPin);
            }
            out += "exec " + from + " " + pin + " " + to + "\n";
            continue;
        }
        if (w.size() >= 3 && w[0] == "entry") {   // the block registers its own entries
            const std::size_t sp = l.find_last_of(' ');
            if (sp != std::string::npos && isBlock(l.substr(sp + 1))) continue;
        }
        if (w.size() >= 4 && w[0] == "data" && (isBlock(w[1]) || isBlock(w[3]))) continue;   // a block takes typed fields only
        out += l + "\n";
    }
    return out + extra;
}

} // namespace okay::vs
