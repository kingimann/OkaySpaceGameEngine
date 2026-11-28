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

namespace part {
int AddFromPart(const char* name);  // inline_part.cpp
int CounterFromPart();
std::string GreetingFromPart();
}  // namespace part
