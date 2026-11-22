#pragma once
// Every sentence the Visual Script window says when something is wrong or refused, in one place, in plain words
// ("block", "number", "text", "true or false", "plug"). tests/test_vs_plain_words.cpp checks that none of them
// uses a programming word (the list in VisualScriptGlossary.hpp). The engine's checker words its findings its own
// way; VsPlainIssue rewrites the ones a beginner will meet and leaves the rest as they are.
// No ImGui, no threads.
#include "okay/VisualScript/VsPinType.hpp"
#include <string>

namespace okay::editor::vsed {

/// What a plug carries, with its article ("a number", "text", "true or false"), for a sentence.
inline const char* VsPlainKind(okay::vs::PinType t) {
    using okay::vs::PinType;
    switch (t) {
        case PinType::Number: return "a number";
        case PinType::Bool:   return "true or false";
        case PinType::Text:   return "text";
        case PinType::Vec3:   return "a position";
        case PinType::Object: return "an object";
        case PinType::List:   return "a list";
        case PinType::Map:    return "a table";
        case PinType::Color:  return "a color";
        default:              return "anything";
    }
}

/// The engine's type name (what its checker prints) as a plain kind; "" when not one of the nine.
inline std::string VsPlainKindOfName(const std::string& n) {
    using okay::vs::PinType;
    if (n == "Number") return VsPlainKind(PinType::Number);
    if (n == "Bool") return VsPlainKind(PinType::Bool);
    if (n == "Text") return VsPlainKind(PinType::Text);
    if (n == "Vector3") return VsPlainKind(PinType::Vec3);
    if (n == "Object") return VsPlainKind(PinType::Object);
    if (n == "List") return VsPlainKind(PinType::List);
    if (n == "Map") return VsPlainKind(PinType::Map);
    if (n == "Color") return VsPlainKind(PinType::Color);
    if (n == "Any") return VsPlainKind(PinType::Any);
    return {};
}

/// A wire between two plugs that do not carry the same thing.
inline std::string VsPlainMismatch(okay::vs::PinType from, okay::vs::PinType to) {
    return std::string("This plug needs ") + VsPlainKind(to) + ", but you gave it " + VsPlainKind(from) + ".";
}

/// A short tip for the same pair when there is no one-click fix.
inline std::string VsPlainMismatchTip(okay::vs::PinType from, okay::vs::PinType to) {
    using okay::vs::PinType;
    if (from == PinType::Vec3 && to == PinType::Number) return "A position is three numbers. Use a block that picks one of them.";
    if (from == PinType::Number && to == PinType::Vec3) return "Use a block that makes a position from three numbers.";
    if (from == PinType::Object && to == PinType::Number) return "An object is not a number. Use a block that measures it, like the distance to it.";
    if (from == PinType::Object && to == PinType::Vec3) return "An object is not a position. Use a block that gives the position of the object.";
    if (to == PinType::Object) return "This plug wants an object. Use the Self block, a Find block, or drop an object on it.";
    if (from == PinType::Color && to == PinType::Number) return "A color is not a number. Use a block that picks one part of the color.";
    if (from == PinType::Number && to == PinType::Color) return "Use a block that makes a color from numbers.";
    if (from == PinType::Text && to == PinType::Bool) return "Compare the text with something to get true or false.";
    return {};
}

inline const char* VsPlainStartIntoStart() { return "A starting block begins a chain. Plug this into a step, not into another starting block."; }
inline const char* VsPlainWouldLoop() { return "That would go round in a circle: this block already depends on the one you are plugging into."; }
inline const char* VsPlainFlowToValue() { return "This plug carries the order things happen in. Drop it on another white triangle, not on a colored circle."; }
inline const char* VsPlainValueToFlow() { return "This plug carries a value. Drop it on a colored circle, not on a white triangle."; }
inline const char* VsPlainDoesNotFitWire() { return "That block does not fit on this wire."; }
inline const char* VsPlainNotConnected() { return "This block is not connected to anything yet."; }
inline const char* VsPlainNothingAfterStart() { return "Nothing is plugged into this starting block yet."; }
inline const char* VsPlainFixIt() { return "Fix it for me"; }

/// Rewrites a finding of the engine's graph checker in plain words. Findings it does not know come back unchanged.
inline std::string VsPlainIssue(const std::string& m) {
    auto has = [&](const char* s) { return m.find(s) != std::string::npos; };
    if (has("nothing runs this node")) return VsPlainNotConnected();
    if (has("this event does nothing")) return VsPlainNothingAfterStart();
    // "a Text from Foo is wired into an input that needs a Number - use To Number"
    if (const auto at = m.find(" is wired into an input that needs a "); at != std::string::npos && m.rfind("a ", 0) == 0) {
        const auto fromEnd = m.find(' ', 2);
        std::string fromName = fromEnd == std::string::npos ? std::string() : m.substr(2, fromEnd - 2);
        std::string toName = m.substr(at + 37);
        const auto dash = toName.find(" - ");
        if (dash != std::string::npos) toName.erase(dash);
        const std::string f = VsPlainKindOfName(fromName), t = VsPlainKindOfName(toName);
        if (!f.empty() && !t.empty()) return "This plug needs " + t + ", but you gave it " + f + ".";
    }
    if (m.rfind("Required: ", 0) == 0) return "This block still needs something filled in: " + m.substr(10);
    if (m.rfind("variable '", 0) == 0 && has("is read but this graph never sets it")) {
        const auto q = m.find('\'', 10);
        const std::string name = q == std::string::npos ? std::string("it") : m.substr(10, q - 10);
        return "'" + name + "' is used here, but nothing gives it a value. Add a block that sets it.";
    }
    if (has("isn't a key name OkaySpace knows")) return "The game does not know that key name. Use one letter or number, space, enter, escape, or an arrow.";
    if (m == "no prefab file is set") return "This block needs a saved object to create. Pick one.";
    return m;
}

} // namespace okay::editor::vsed
