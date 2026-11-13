#pragma once
// Declared (typed) visual-script variables and struct types, and the value
// literals their defaults are written in.
//
//   decl <name> <Type> <object|scene|global> [<literal>]
//   struct <Name> <field>:<Type>[=<literal>] ...
//
// Types: Number, Bool, Text, Vector3, Color, Object, List<T>, Map<T>, Any, or a
// struct's name. A literal is `1.5`, `true`, `"text"`, `(1, 2, 3)`, `#ff8800`
// (or #rrggbbaa), `[a, b]`, `{key: v}`, `@Name` (an object, by name).
// Lists, maps and structs are references (two variables can hold the same one);
// a declared default is deep-copied for every object, scene and run.
#include "okay/VisualScript/VsPinType.hpp"
#include "okay/VisualScript/VsValue.hpp"
#include <string>
#include <vector>

namespace okay::vs {

/// A name made of letters, digits and _, not starting with a digit.
bool VsValidIdent(const std::string& s);

/// The display name of a pin type ("Number", "Vector3"...). VsTypes.cpp.
const char* PinTypeName(PinType t);

enum class VarScope { Object, Scene, Global };
const char* VarScopeName(VarScope s);   ///< "object" / "scene" / "global"
bool VarScopeFromName(const std::string& s, VarScope& out);

/// A variable's declared type. For List / Map, `elem` is the element type (Any
/// = untyped). A struct is a Map whose `structName` is set.
struct VarType {
    PinType base = PinType::Any;
    PinType elem = PinType::Any;
    std::string structName;
    bool operator==(const VarType& o) const { return base == o.base && elem == o.elem && structName == o.structName; }
    bool IsStruct() const { return !structName.empty(); }
};

struct StructField { std::string name; VarType type; VsValue def; };
struct StructDef { std::string name; std::vector<StructField> fields; };

struct VarDecl {
    std::string name;
    VarType type;
    VarScope scope = VarScope::Object;
    VsValue value;           ///< the default (the Inspector edits this)
    bool legacy = false;     ///< came from an old `var name value` line
    /// Back-compat spelling for code that read the old exposed values.
    VsValue& Value() { return value; }
};

/// "Number", "Vector3", "List<Vector3>", "Enemy"...
std::string VarTypeText(const VarType& t);
/// Parse a type name. `structs` resolves struct names. False with `err` when unknown.
bool ParseVarType(const std::string& text, const std::vector<StructDef>& structs, VarType& out, std::string* err = nullptr);
const StructDef* FindStruct(const std::vector<StructDef>& structs, const std::string& name);

/// A value as literal text (see above). Lists / maps nest to `depth` levels; deeper
/// parts print as `[]` / `{}` so a self-containing list can't run away.
std::string VsToLiteral(const VsValue& v, int depth = 0);
/// Parse literal text into a value. False with `err` on bad input; limits (nesting
/// 16, 100000 items, 1 MB) make hostile text fail rather than hang.
bool VsParseLiteral(const std::string& text, VsValue& out, std::string* err = nullptr);
/// A short human reading of any value for watch panels: lists show their length and
/// first items, maps their keys, long text is cut. Never recurses past 4 levels.
std::string VsDescribe(const VsValue& v, int maxItems = 6);

/// The value a variable of this type starts with (0, false, "", (0,0,0), white,
/// empty list / map, a struct with each field's default).
VsValue VsDefaultFor(const VarType& t, const std::vector<StructDef>& structs);
/// An independent copy: lists, maps and structs are copied all the way down.
VsValue VsDeepCopy(const VsValue& v, int depth = 0);
/// Make `v` fit type `t`, converting where that means something ("3" -> 3, a
/// vector -> a colour) and, for a list / map / struct, converting every element in
/// place (the list stays the same list). False with `err` saying what's wrong and
/// where ("item 2: expected Number, got Vector3"); `v` may be partly converted then.
bool VsCoerce(const VarType& t, VsValue& v, const std::vector<StructDef>& structs, std::string* err = nullptr);
/// Parse literal text and fit it to `t` (a bare word that isn't a literal counts as text).
bool VsParseTyped(const VarType& t, const std::string& text, const std::vector<StructDef>& structs, VsValue& out, std::string* err = nullptr);
/// The type a value has, as a word for messages ("Number", "Vector3", "List"...).
const char* VsKindName(const VsValue& v);
/// A total order over any two values (kind first, then content) so sorting a mixed
/// list is well defined: Number < Bool < Text < ... ; NaN sorts first.
int VsCompare(const VsValue& a, const VsValue& b);

} // namespace okay::vs
