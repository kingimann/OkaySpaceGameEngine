// Itanium C++ ABI mangled names of types, functions, constructors and data.
// Moved out of parser.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "ast.hpp"

namespace okaycc {
namespace compiler {
std::string mangleTag(const std::string& qualified) { return mangleNested(qualified); }

static void dropConst(Type& t) {
    t.cnst = false;
    if (t.inner) dropConst(*t.inner);
}
// The type with every const dropped: how a thrown object and a handler are matched (a handler for
// const char* takes a thrown char*, [except.handle]/3 qualification conversion).
std::string mangleTypeNoCv(const Type& t) {
    auto c = cloneType(t);
    dropConst(*c);
    return mangleType(*c);
}

std::string mangleType(const Type& t) {
    switch (t.kind) {
    case Type::Kind::Void: return "v";
    case Type::Kind::Bool: return "b";
    case Type::Kind::Char: return t.unsign ? "h" : "c";
    case Type::Kind::Short: return t.unsign ? "t" : "s";
    case Type::Kind::Int: return t.unsign ? "j" : "i";
    case Type::Kind::Long: return t.unsign ? "m" : "l";
    case Type::Kind::Float: return "f";
    case Type::Kind::Double: return "d";
    // A const pointee is K<type> ([mangle.qualified-type]); top-level const is not mangled.
    case Type::Kind::Pointer: return t.inner ? "P" + std::string(t.inner->cnst ? "K" : "") + mangleType(*t.inner) : "Pv";
    case Type::Kind::Reference:
        return t.inner ? (t.rref ? "O" : "R") + std::string(t.inner->cnst ? "K" : "") + mangleType(*t.inner) : "Rv";
    case Type::Kind::Struct:
    case Type::Kind::Union:
        if (t.tag.empty()) return "i";
        if (t.tag.find("::") == std::string::npos) return mangleTag(t.tag);
        return "N" + mangleTag(t.tag) + "E";
    // An array type is A <length> _ <element>, or A_ <element> with no length ([mangle.array-type]).
    case Type::Kind::Array:
        return "A" + std::string(t.unsized || !t.bound ? "" : (t.bound->kind == Expr::Kind::Int ? std::to_string(t.bound->ival) : "")) +
               "_" + (t.inner ? mangleType(*t.inner) : "i");
    // A function type is F <return type> <parameter types> E, v for no parameters
    // ([mangle.function-type]).
    case Type::Kind::Function: {
        std::string out = "F" + (t.inner ? std::string(t.inner->cnst ? "K" : "") + mangleType(*t.inner) : "v");
        if (t.params.empty()) out += "v";
        for (const Param& p : t.params) out += p.type ? std::string(p.type->cnst ? "K" : "") + mangleType(*p.type) : "i";
        return out + "E";
    }
    default: return "i";
    }
}

// One source-level name part. A class template instance is spelled "Box<i,Pc>" where each
// argument is already mangled; it becomes <name> I <args> E (Itanium C++ ABI 5.1.5).
// Itanium C++ ABI 5.1.5 operator names (<operator-name>).
std::string operatorCode(const std::string& op) {
    static const char* const kTable[][2] = {
        {"+", "pl"}, {"-", "mi"}, {"*", "ml"}, {"/", "dv"}, {"%", "rm"}, {"&", "an"}, {"|", "or"}, {"^", "eo"},
        {"=", "aS"}, {"+=", "pL"}, {"-=", "mI"}, {"*=", "mL"}, {"/=", "dV"}, {"%=", "rM"}, {"&=", "aN"},
        {"|=", "oR"}, {"^=", "eO"}, {"<<", "ls"}, {">>", "rs"}, {"<<=", "lS"}, {">>=", "rS"}, {"==", "eq"},
        {"!=", "ne"}, {"<", "lt"}, {">", "gt"}, {"<=", "le"}, {">=", "ge"}, {"!", "nt"}, {"&&", "aa"},
        {"||", "oo"}, {"++", "pp"}, {"--", "mm"}, {",", "cm"}, {"->", "pt"}, {"()", "cl"}, {"[]", "ix"},
        {"~", "co"},
    };
    for (const auto& row : kTable)
        if (op == row[0]) return row[1];
    return "nw";
}

std::string manglePart(const std::string& part) {
    if (part.compare(0, 9, "operator ") == 0) return "cv" + part.substr(9);  // conversion function
    if (part.compare(0, 8, "operator") == 0 && part.size() > 8) return operatorCode(part.substr(8));
    std::size_t lt = part.find('<');
    if (lt == std::string::npos || part.back() != '>') return std::to_string(part.size()) + part;
    std::string name = part.substr(0, lt);
    std::string args = part.substr(lt + 1, part.size() - lt - 2);
    std::string joined;
    for (char c : args)
        if (c != ',') joined += c;
    return std::to_string(name.size()) + name + "I" + joined + "E";
}

std::string mangleNested(const std::string& qualified) {
    std::string out;
    std::size_t i = 0;
    while (i < qualified.size()) {
        std::size_t j = qualified.find("::", i);
        std::string part = qualified.substr(i, j == std::string::npos ? std::string::npos : j - i);
        out += manglePart(part);
        if (j == std::string::npos) break;
        i = j + 2;
    }
    return out;
}

std::string mangleFunc(const std::string& qualified, const Type& fn) {
    std::string params;
    if (fn.params.empty()) params = "v";
    else {
        for (const Param& p : fn.params)
            params += p.type ? mangleType(*p.type) : "i";
    }
    return "_ZN" + mangleNested(qualified) + "E" + params;
}

// Itanium C++ ABI 5.1.4: a constructor is <nested-name> C1 and a destructor D1 (complete
// object) or D0 (deleting). "ctor" is the two letters to put after the class name.
std::string mangleSpecial(const std::string& cls, const char* kind, const Type& fn) {
    std::string params;
    if (fn.params.empty()) params = "v";
    else {
        for (const Param& p : fn.params) params += p.type ? mangleType(*p.type) : "i";
    }
    return "_ZN" + mangleNested(cls) + kind + "E" + params;
}

std::string mangleData(const std::string& qualified) { return "_ZN" + mangleNested(qualified) + "E"; }


}  // namespace compiler
}  // namespace okaycc
