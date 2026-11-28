// Round 36: inline functions with static locals, included by two objects of one program (link_inline_static.cpp and
// inline_part.cpp). [dcl.inline] 6: a static local variable in an inline function with external linkage is one object
// in the whole program, so what one file puts there the other sees. okay-cl gives each such object (and its guard) a
// COMDAT section of its own, named the same in both objects, and the linker keeps one.
#pragma once
#include <string>
#include <vector>

struct Registry {
    // A member function written in its class is inline: its static local is one list for the program.
    static std::vector<std::string>& Names() {
        static std::vector<std::string> names;
        return names;
    }
    static int& Made() {
        static int made = 100;  // a constant initializer: the object's bytes are the value
        return made;
    }
};

inline int& Counter() {
    static int count = 0;
    return count;
}

// A dynamic initializer under a guard: it must run once in the program, from whichever file calls first.
inline std::string& Greeting() {
    static std::string text = std::string("hello") + " there";
    return text;
}

// Round 37: inline variables ([dcl.inline] 6: one object in the program), at namespace scope and as static members
// written in the class, and a static local of a lambda written in an inline function (the lambda is the same in every
// file, [expr.prim.lambda.closure] 2 and [basic.def.odr] 13, so its operator()'s static local is one object too).
inline int g_inlineCount = 5;
namespace part {
inline float g_scale = 1.5f;
inline const char* g_label = "start";
}  // namespace part
struct Settings {
    static inline bool visible = true;
    static inline int levels[3] = {1, 2, 3};
};
inline int LambdaTicks() {
    auto tick = [] {
        static int n = 0;
        return ++n;
    };
    return tick();
}

namespace part {
int InlineVarsFromPart();  // inline_part.cpp
int AddFromPart(const char* name);  // inline_part.cpp
int CounterFromPart();
std::string GreetingFromPart();
}  // namespace part
