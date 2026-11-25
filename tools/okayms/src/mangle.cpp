// MSVC decorated names of functions, methods and types.
// Moved out of compile.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "compile_internal.hpp"

namespace okayms {
namespace compiler {
// MSVC decorated names, from the public decoration rules: '?' name '@'
// namespace '@@YA' return-type argument-types '@Z'. H is int, X is void,
// A in front of a type is a reference. Written here, not copied.
std::string cxx_type(const Type& type, const std::vector<StructDef>* structs) {
    if (type.is_ref) {
        // On x64 a reference is "AEA" (the E is the 64-bit pointer mark).
        Type inner = type;
        inner.is_ref = false;
        inner.rref = false;
        if (type.rref && type.pointers == 0) return std::string("$$QEA") + cxx_type(inner, structs);  // T&&
        return std::string(type.pconst && type.pointers == 0 ? "AEB" : "AEA") + cxx_type(inner, structs);
    }
    if (type.pointers > 0) {
        Type inner = type;
        --inner.pointers;
        return std::string(type.pconst && type.pointers == 1 ? "PEB" : "PEA") + cxx_type(inner, structs);
    }
    if (type.base == 0) return "X";
    if (type.base == 2) return type.is_bool ? "_N" : type.is_unsigned ? "E" : "D";
    if (type.base == 4) return type.is_unsigned ? "G" : "F";
    if (type.base == 5) return "M";
    if (type.base == 6) return "N";
    if (type.base == 3 && structs != nullptr && type.struct_id >= 0 &&
        type.struct_id < static_cast<int>(structs->size())) {
        const StructDef& def = (*structs)[static_cast<size_t>(type.struct_id)];
        // U is struct, T is union, V is class; then the name and "@@".
        return std::string(def.is_union ? "T" : def.is_class ? "V" : "U") + def.symname() + "@@";
    }
    if (type.base == 1 && type.enum_id >= 0 && type.enum_id < static_cast<int>(enum_names().size()))
        return "W4" + enum_names()[static_cast<size_t>(type.enum_id)] + "@@";  // an enumeration of int
    if (type.wide) return type.is_unsigned ? "_K" : "_J";
    return type.is_unsigned ? "I" : "H";
}

// The name part of a decorated name: "f@" for a function f, and the operator code for an operator function
// ("??H" for operator+, from the public MSVC decoration table: ?4 = ?5 >> ?6 << ?7 ! ?8 == ?9 != ?A [] ?B
// conversion ?C -> ?D * ?E ++ ?F -- ?G - ?H + ?I & ?K / ?L % ?M < ?N <= ?O > ?P >= ?Q , ?R () ?S ~ ?T ^ ?U |
// ?V && ?W || ?X *= ?Y += ?Z -= ?_0 /= ?_1 %= ?_2 >>= ?_3 <<= ?_4 &= ?_5 |= ?_6 ^=). The result of an operator
// is "?" and the code, with no '@' after it.
std::string name_part(const std::string& name) {
    if (name.compare(0, 8, "operator") != 0 || name.size() == 8) return name + "@";
    static const char* const table[][2] = {
        {"=", "?4"}, {">>", "?5"}, {"<<", "?6"}, {"!", "?7"}, {"==", "?8"}, {"!=", "?9"}, {"[]", "?A"},
        {"->", "?C"}, {"*", "?D"}, {"++", "?E"}, {"--", "?F"}, {"-", "?G"}, {"+", "?H"}, {"&", "?I"},
        {"/", "?K"}, {"%", "?L"}, {"<", "?M"}, {"<=", "?N"}, {">", "?O"}, {">=", "?P"}, {",", "?Q"},
        {"()", "?R"}, {"~", "?S"}, {"^", "?T"}, {"|", "?U"}, {"&&", "?V"}, {"||", "?W"}, {"*=", "?X"},
        {"+=", "?Y"}, {"-=", "?Z"}, {"/=", "?_0"}, {"%=", "?_1"}, {">>=", "?_2"}, {"<<=", "?_3"},
        {"&=", "?_4"}, {"|=", "?_5"}, {"^=", "?_6"}};
    const std::string rest = name.substr(8);
    if (rest.compare(0, 5, " conv") == 0) return "?B";
    for (const auto& entry : table)
        if (rest == entry[0]) return entry[1];
    return name + "@";
}

// ?name@Class@@ access-letter this-qualifiers calling-convention return
// parameters @Z. The access letter is Q/A/I for public/private/protected
// and U/E/M for the same three when the function is virtual. "EAA" is a
// 64-bit non-const this pointer and the one calling convention; "EBA" is a
// const this pointer.
std::string cxx_method_symbol(const std::vector<StructDef>& structs, const StructDef& owner, const Method& m) {
    char letter = m.access;
    if (m.is_virtual) letter = m.access == 'A' ? 'E' : m.access == 'I' ? 'M' : 'U';
    if (m.is_static) {
        // ?g@P@@SAHH@Z: S, C or K for a public, private or protected static member, then the
        // calling convention, with no this qualifier.
        std::string st = "?" + (m.tsym.empty() ? name_part(m.name) : m.tsym + "@") + owner.symname() + "@@";
        st += m.access == 'A' ? 'C' : m.access == 'I' ? 'K' : 'S';
        st += "A";
        if (m.ret.base == 3 && m.ret.pointers == 0 && !m.ret.is_ref) st += "?A";
        st += cxx_type(m.ret, &structs);
        if (m.params.empty()) st += "X";
        else {
            for (const Type& param : m.params) st += cxx_type(param, &structs);
            st += "@";
        }
        return st + "Z";
    }
    if (m.name == "<ctor>" || m.name == "<dtor>") {
        // ??0 is a constructor and ??1 a destructor. There is no return type: '@' stands where it
        // would be.
        // A constructor template instance is ??$?0H@S@@...: the template arguments follow ??$?0.
        std::string special = (m.name == "<ctor>" && !m.tsym.empty() ? "?" + m.tsym + "@" :
                               std::string(m.name == "<ctor>" ? "??0" : "??1")) + owner.symname() + "@@";
        special += letter;
        special += "EAA@";
        if (m.params.empty()) special += "X";
        else {
            for (const Type& param : m.params) special += cxx_type(param, &structs);
            special += "@";
        }
        return special + "Z";
    }
    std::string out = "?" + (m.tsym.empty() ? name_part(m.name) : m.tsym + "@") + owner.symname() + "@@";
    out += letter;
    out += m.is_const ? "EBA" : "EAA";
    if (m.ret.base == 3 && m.ret.pointers == 0 && !m.ret.is_ref) out += "?A";
    out += cxx_type(m.ret, &structs);
    if (m.params.empty()) out += "X";
    else {
        for (const Type& param : m.params) out += cxx_type(param, &structs);
        out += "@";
    }
    out += "Z";
    return out;
}

// The scalar deleting destructor, ??_G: the function a virtual destructor's table slot holds. It
// takes this and a flag word, runs the destructor, frees the memory when the flag is non-zero and
// returns this. Written from the public MSVC decoration description.
std::string sdd_symbol(const StructDef& owner, const Method& m) {
    const char letter = m.access == 'A' ? 'E' : m.access == 'I' ? 'M' : 'U';
    return "??_G" + owner.symname() + "@@" + letter + "EAAPEAXI@Z";
}

std::string cxx_symbol(const std::string& ns, const std::string& name, const Type& ret, const std::vector<Type>& params,
                       const std::vector<StructDef>* structs) {
    std::string out = "?" + name_part(name) + (ns.empty() ? "" : ns + "@") + "@YA";
    if (ret.base == 3 && ret.pointers == 0 && !ret.is_ref) out += "?A";
    out += cxx_type(ret, structs);
    if (params.empty()) out += "X";
    else {
        for (const Type& param : params) out += cxx_type(param, structs);
        out += "@";
    }
    out += "Z";
    return out;
}


}  // namespace compiler
}  // namespace okayms
