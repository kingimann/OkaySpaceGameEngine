#pragma once
// The sentences the canvas shows when a wire is refused, in everyday words ("This plug needs a number, but you
// gave it text."). One place, so a test can read every one of them and check the words. No ImGui.
// Written for this project.
#include "okay/VisualScript/VsTypes.hpp"

#include <string>
#include <vector>

namespace okay::editor::vsed {

/// The everyday name of a pin type, with its article: "a number", "some text", "a true-or-false value".
inline const char* VsPlainTypeWithArticle(okay::vs::PinType t) {
    using okay::vs::PinType;
    switch (t) {
        case PinType::Number: return "a number";
        case PinType::Bool:   return "a true-or-false value";
        case PinType::Text:   return "text";
        case PinType::Vec3:   return "a position";
        case PinType::Object: return "an object";
        case PinType::List:   return "a list";
        case PinType::Map:    return "a table of names and values";
        case PinType::Color:  return "a colour";
        default:              return "anything";
    }
}

/// "This plug needs a number, but you gave it text." plus, when there is one, the block that fixes it.
inline std::string VsPlainWireRefusal(okay::vs::PinType from, okay::vs::PinType to) {
    using okay::vs::PinType;
    std::string s = std::string("This plug needs ") + VsPlainTypeWithArticle(to) + ", but you gave it "
                  + VsPlainTypeWithArticle(from) + ".";
    if (from == PinType::Text && to == PinType::Number) s += " Put a Text To Number block in between.";
    else if ((from == PinType::Number || from == PinType::Bool) && to == PinType::Text) s += " Put a Number To Text block in between.";
    else if (from == PinType::Vec3 && to == PinType::Number) s += " A position has three numbers. Use Get Position Left-Right, Up-Down or Forward-Back.";
    else if (from == PinType::Number && to == PinType::Vec3) s += " Use Make Position to join three numbers.";
    else if (from == PinType::Object && to == PinType::Vec3) s += " Use Get Position and put this object in its Target.";
    else if (from == PinType::Object && to == PinType::Number) s += " Use Get Distance To, or Get Position.";
    else if (to == PinType::Object) s += " Use Find Object, or drag one in from the Hierarchy.";
    return s;
}

inline const char* VsPlainRefuseEventIntoEvent() { return "A when-this-happens block starts a chain. Plug this into a step, not into another start block."; }
inline const char* VsPlainRefuseLoop() { return "That would go round in a circle: this value already depends on the block you are plugging it into."; }
inline const char* VsPlainRefuseFlowOnValue() { return "This wire carries the order things happen in (the white triangle). Drop it on another triangle, not on a coloured circle."; }
inline const char* VsPlainRefuseValueOnFlow() { return "This wire carries a value. Drop it on a coloured circle, not on a white triangle."; }

/// Every refusal sentence, for the test that reads them all.
inline std::vector<std::string> VsAllRefusalTexts() {
    using okay::vs::PinType;
    std::vector<std::string> v = {VsPlainRefuseEventIntoEvent(), VsPlainRefuseLoop(), VsPlainRefuseFlowOnValue(), VsPlainRefuseValueOnFlow()};
    const PinType all[] = {PinType::Number, PinType::Bool, PinType::Text, PinType::Vec3, PinType::Object,
                           PinType::List, PinType::Map, PinType::Color};
    for (PinType a : all) for (PinType b : all) if (a != b) v.push_back(VsPlainWireRefusal(a, b));
    return v;
}

} // namespace okay::editor::vsed
