// Itanium C++ ABI mangled names of types, functions, constructors and data.
// Moved out of parser.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "ast.hpp"

#include <set>

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
    case Type::Kind::Pointer:
        if (!t.memberOf.empty() && t.inner) {
            // A pointer to member: M <class type> <member type> ([mangle.pointer-to-member-type]).
            Type cls;
            cls.kind = Type::Kind::Struct;
            cls.tag = t.memberOf;
            return "M" + mangleType(cls) + std::string(t.inner->cnst ? "K" : "") + mangleType(*t.inner);
        }
        return t.inner ? "P" + std::string(t.inner->cnst ? "K" : "") + mangleType(*t.inner) : "Pv";
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
    if (part.compare(0, 10, "operator\"\"") == 0) return "li" + std::to_string(part.size() - 10) + part.substr(10);
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

// ---- Type information objects (RTTI), Itanium C++ ABI 2.9.5 ----
// Written from the Itanium C++ ABI section 2.9.5 (RTTI layout) and ISO/IEC 14882 [expr.typeid]. The
// object for type T is _ZTI<T>: three 8-byte words, (1) the type_info vtable slot of the ABI, left zero
// (nothing calls through it: our <typeinfo> has no virtual functions), (2) the address of the name
// _ZTS<T>, the mangled type without "_Z" ("i", "3Foo"), as std::type_info::name() returns it with
// g++, (3) for a class with a base class (single inheritance only), the base's _ZTI, which plays the
// part of __si_class_type_info::__base_type, else zero. Each object and name is a weak symbol, so every
// file may define the ones it uses. A vtable's second word holds its class's _ZTI.
std::string typeInfoSym(const Type& t) {
    const Type* u = &t;
    if (u->kind == Type::Kind::Reference && u->inner) u = u->inner.get();  // [expr.typeid] p4-5
    auto c = cloneType(*u);
    c->cnst = false;  // only the top-level const is dropped (const char * stays PKc)
    return "_ZTI" + mangleType(*c);
}

void makeTypeInfo(const Type& t, std::vector<std::unique_ptr<Decl>>& out, std::set<std::string>& done,
                  int line, int col, const std::string& file) {
    const Type* u = &t;
    if (u->kind == Type::Kind::Reference && u->inner) u = u->inner.get();
    std::string sym = typeInfoSym(*u);
    if (!done.insert(sym).second) return;
    std::string base;
    // The bases after the primary one ([class.mi]): their type_info and their offset in the
    // object (the subobject member "__mb:<tag>"), for dynamic_cast to and across them.
    std::vector<std::string> more;
    if (u->kind == Type::Kind::Struct && !u->tag.empty()) {
        auto it = canonStructs().find(u->tag);
        if (it != canonStructs().end() && it->second && !it->second->base.empty()) {
            Type b;
            b.kind = Type::Kind::Struct;
            b.tag = it->second->base;
            base = typeInfoSym(b);
            makeTypeInfo(b, out, done, line, col, file);
        }
        if (it != canonStructs().end() && it->second) {
            std::vector<std::string> tags;
            for (const Param& p : it->second->params)
                if (p.name.compare(0, 5, "__mb:") == 0 && p.type) tags.push_back(p.type->tag);
            for (const std::string& tag : tags) {
                const int off = memberOffset(u->tag, "__mb:" + tag);
                if (off < 0) continue;
                Type b;
                b.kind = Type::Kind::Struct;
                b.tag = tag;
                makeTypeInfo(b, out, done, line, col, file);
                more.push_back(typeInfoSym(b));
                more.push_back("\x01#" + std::to_string(off));
            }
        }
    }
    std::string nameText = sym.substr(4);
    auto nameDecl = std::make_unique<Decl>();
    nameDecl->name = "_ZTS" + nameText;
    nameDecl->line = line;
    nameDecl->col = col;
    nameDecl->file = file;
    nameDecl->isInline = true;
    auto arr = std::make_unique<Type>();
    arr->kind = Type::Kind::Array;
    arr->inner = std::make_unique<Type>();
    arr->inner->kind = Type::Kind::Char;
    arr->inner->cnst = true;
    arr->bound = std::make_unique<Expr>();
    arr->bound->kind = Expr::Kind::Int;
    arr->bound->ival = nameText.size() + 1;
    nameDecl->type = std::move(arr);
    nameDecl->init = std::make_unique<Expr>();
    nameDecl->init->kind = Expr::Kind::String;
    nameDecl->init->text = nameText;
    out.push_back(std::move(nameDecl));
    auto ti = std::make_unique<Decl>();
    ti->name = sym;
    ti->line = line;
    ti->col = col;
    ti->file = file;
    ti->isInline = true;
    auto words = std::make_unique<Type>();
    words->kind = Type::Kind::Array;
    words->inner = std::make_unique<Type>();
    words->inner->kind = Type::Kind::Pointer;
    words->inner->inner = std::make_unique<Type>();
    words->inner->inner->kind = Type::Kind::Void;
    words->bound = std::make_unique<Expr>();
    words->bound->kind = Expr::Kind::Int;
    // 0 unused, 1 the name, 2 the primary base's type_info or 0, 3 how many other bases, then each
    // other base's type_info and offset (lib/eh.c __okay_dyncast_ptr).
    words->bound->ival = 4 + more.size();
    ti->type = std::move(words);
    ti->vt = {"", "_ZTS" + nameText, base, "\x01#" + std::to_string(more.size() / 2)};
    for (std::string& m : more) ti->vt.push_back(std::move(m));
    out.push_back(std::move(ti));
}


}  // namespace compiler
}  // namespace okaycc
