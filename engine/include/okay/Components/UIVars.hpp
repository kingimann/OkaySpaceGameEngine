#pragma once
// Reading and writing the variables a piece of UI is bound to, and a small condition language over them.
//
// The data-driven parts of the UI library (UIBind, UIVisibleIf, UIListBind) share this: a variable name is looked up
// the same way UIBarBind looks it up (a visual script on the same object, the scene's visual-script variables,
// the Actions variables, a saved Prefs value), so one name means one value everywhere.
//
// Condition language (UIVisibleIf.condition):
//     hp < 30            score >= 10 && lives > 0        !gameOver        state == "menu"        (a || b) && c
// A name is a variable. A bare number is a number, "quoted text" is text, true / false are 1 / 0. Two sides that are both
// numbers compare as numbers, otherwise as text. A lone name is true when it is a number other than 0 or text that is not
// empty, "0" or "false". A name nobody set is false (and equal to nothing).
// Written for this engine from the plain idea of an infix boolean expression; no other project's code was used.
#include <functional>
#include <map>
#include <string>

namespace okay {

class GameObject;

namespace uivars {

/// A value read from a variable: a number, or text.
struct Value {
    bool found = false;
    bool isNumber = false;
    float number = 0.0f;
    std::string text;       ///< always set when found (a number is written out)
};

/// Look `name` up (see the file comment). `go` may be null (then only the global pools are searched).
Value Read(const GameObject* go, const std::string& name);
bool ReadNumber(const GameObject* go, const std::string& name, float& out);

/// Write a number into the variable `name`: the place that already holds it (a visual script on `go`, the scene's
/// visual-script variables), else the Actions variables. Creates it when nobody has it.
void WriteNumber(GameObject* go, const std::string& name, float v);
/// Write text into `name`: the text variables (the ones a UITextBind shows).
void WriteText(GameObject* go, const std::string& name, const std::string& text);

/// The text of a condition's answer. `error` (when given) receives why a condition could not be read, empty when fine.
bool Evaluate(const GameObject* go, const std::string& condition, std::string* error = nullptr);

/// Evaluate with the variables supplied by `lookup` instead of the real pools (tests, tools).
bool EvaluateWith(const std::function<Value(const std::string&)>& lookup, const std::string& condition,
                  std::string* error = nullptr);

/// Fill the `{key}` and `{key|default}` holes of `format` from `fields` ("{{" and "}}" are literal braces).
std::string FillTemplate(const std::string& format, const std::map<std::string, std::string>& fields);

/// A number as shown on screen: 5 -> "5", 2.5 -> "2.5" (up to 3 decimals, trailing zeros dropped).
std::string FormatNumber(float f);

} // namespace uivars
} // namespace okay
