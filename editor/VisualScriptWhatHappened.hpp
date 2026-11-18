#pragma once
// Play feedback: the short "what happened" line under a block that just ran ("Moved 3 steps right"). A pure
// function: the block's type, its typed text, the values on its plugs (live ones when a wire feeds them, else what
// is typed in) and how many times it ran. The canvas gets those from the running graph's debug trace (execCount
// and lastOut already exist there), so the runner needed no new hook. Plain words only.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace okay::editor::vsed {

/// "3", "-2.5", "0.2": a number as a short word (no trailing zeros). Anything else comes back as it was.
inline std::string VsShortNumber(const std::string& v) {
    if (v.empty()) return v;
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (end == v.c_str() || *end) return v;
    char b[40];
    std::snprintf(b, sizeof b, "%.3g", d);
    return b;
}

inline bool VsLooksFalse(const std::string& v) { return v == "false" || v == "0" || v == "0.0"; }

/// One short sentence for what block `type` just did. `in` holds the value on each input plug, by plug number
/// (missing ones are empty); `arg` is the text typed on the block. `times` is how often it has run.
inline std::string VsWhatHappened(const std::string& type, const std::string& arg, const std::vector<std::string>& in, unsigned times) {
    auto v = [&](std::size_t i) { return i < in.size() ? VsShortNumber(in[i]) : std::string(); };
    auto num = [&](std::size_t i) { const std::string s = v(i); return s.empty() ? std::string("some") : s; };
    auto sign = [&](std::size_t i) { return !v(i).empty() && v(i)[0] == '-'; };
    auto abs = [&](std::size_t i) { std::string s = v(i); if (!s.empty() && s[0] == '-') s.erase(0, 1); return s.empty() ? std::string("some") : s; };
    std::string out;
    if (type == "GlMoveSideways") out = "Moved " + abs(0) + " steps " + (sign(0) ? "left" : "right");
    else if (type == "GlMoveForward") out = "Moved " + abs(0) + " steps " + (sign(0) ? "back" : "forward");
    else if (type == "GlJump") out = "Jumped";
    else if (type == "Translate") out = "Moved by " + num(0) + ", " + num(1) + ", " + num(2);
    else if (type == "SetPosition" || type == "Teleport") out = "Jumped to " + num(0) + ", " + num(1) + ", " + num(2);
    else if (type == "Rotate") out = "Turned " + num(0) + " degrees";
    else if (type == "MoveTowards") out = "Moved toward " + (arg.empty() ? std::string("the target") : arg);
    else if (type == "Chase") out = "Chased the target";
    else if (type == "ScoreAdd") out = "Added " + num(0) + " to the score";
    else if (type == "Collect") out = "Collected " + num(0);
    else if (type == "SetActive") out = VsLooksFalse(in.empty() ? std::string("1") : in[0]) ? "Hid it" : "Showed it";
    else if (type == "SetText") out = "Showed the text \"" + (in.empty() ? std::string() : in[0]) + "\"";
    else if (type == "Print") out = "Wrote \"" + (in.size() > 1 && !in[1].empty() ? VsShortNumber(in[1]) : (in.empty() ? std::string() : in[0])) + "\"";
    else if (type == "PlaySound") out = "Played a sound";
    else if (type == "Lose") out = "The game is over: you lose";
    else if (type == "Win") out = "You win";
    else if (type == "Damage") out = "Took away " + num(0) + " health";
    else if (type == "Heal") out = "Gave " + num(0) + " health";
    else if (type == "Destroy") out = "Removed an object";
    else if (type == "Spawn") out = "Made a new object";
    else if (type == "SetVar") out = "Set " + (arg.empty() ? std::string("a value") : arg) + " to " + (in.empty() ? std::string("a new value") : VsShortNumber(in[0]));
    else if (type == "Delay" || type == "Countdown") out = "Waited " + (arg.empty() ? num(0) : VsShortNumber(arg)) + " seconds";
    else if (type == "DoorOpen") out = "Opened the door";
    else if (type == "AddForce") out = "Pushed it";
    else if (type == "LoadScene") out = "Went to " + (arg.empty() ? std::string("another scene") : arg);
    else if (type.rfind("On", 0) == 0 && type.size() > 2 && type[2] >= 'A' && type[2] <= 'Z') out = "This just happened";
    else out = "Ran";
    if (times > 1) out += " (" + std::to_string(times) + " times)";
    return out;
}

} // namespace okay::editor::vsed
