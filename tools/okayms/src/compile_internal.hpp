#pragma once
// The C and C++ compiler's shared pieces: tokens, types, the syntax tree, the tables of structs, globals and
// functions, and the declarations of the functions one part of the compiler calls in another.
// Moved out of compile.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "okayms/compile.hpp"
#include "okayms/unwind.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>

namespace okayms {
namespace compiler {
enum Tok {
    TEnd, TIdent, TNumber, TString,
    TLParen, TRParen, TLBrace, TRBrace, TSemi, TComma, TStar, TAmp,
    TPlus, TMinus, TSlash, TPercent, TAssign, TEq, TNe, TLt, TGt, TLe, TGe,
    TPlusPlus, TMinusMinus, TPlusEq, TMinusEq, TStarEq, TSlashEq, TPercentEq, TQuestion,
    TAndAnd, TOrOr, TNot, TLBrack, TRBrack, TDot, TArrow, TEllipsis, TColon, TColonColon,
    TInt, TVoid, TChar, TIf, TElse, TWhile, TFor, TDo, TSwitch, TCase, TDefault, TBreak, TContinue, TReturn,
    TStruct, TUnion, TEnum, TTypedef, TUnsigned, TSigned, TLong, TShort, TDeclspec,
    TSizeof, TConst, TStatic, TGoto, TFloat, TDouble, TNamespace, TTilde,
    // Bit operators. A '>>' is two adjacent TGt tokens (so A<B<int>> still closes twice) and the parser
    // joins them into TShr / TShrEq where an operator is expected.
    TPipe, TCaret, TShl, TShr, TAndEq, TOrEq, TXorEq, TShlEq, TShrEq
};

struct Token {
    Tok kind = TEnd;
    std::string text;
    uint64_t number = 0;
    // 0 for an integer, 5 for a float literal, 6 for a double literal.
    int real = 0;
    // Type of an integer literal ([lex.icc]): 0 int, 1 unsigned int, 2 long long, 3 unsigned long long.
    int ity = 0;
    // Line of the preprocessed text, counted from 1.
    int line = 0;
    // A '>' written directly after another '>' (no space), used to read '>>' and '>>='.
    bool adj = false;
    // The word `inline` stood in front of this token (the word itself is dropped): the function that follows is an inline
    // function, one copy per file.
    bool inl = false;
    // Namespace names in front of this name were dropped (std::filesystem::path is read as path): it names something in
    // a namespace, so a local variable of the same name does not hide it.
    bool nsq = false;
    // The namespace names that were dropped in front of it ("okay::crypto" for okay::crypto::Sha256): used to choose
    // between classes of one name in different namespaces.
    std::string qual;
    // `thread_local` was written here: the token is `static` and the variable gets storage per thread.
    bool tls = false;
};

// The names of the enumerations read so far (Type::enum_id indexes it).
inline std::vector<std::string>& enum_names() {
    static std::vector<std::string> names;
    return names;
}

struct Type;
// The pointer-to-member types read so far (Type::memptr indexes it): the class, then the member's type.
std::vector<std::pair<Type, Type>>& memptr_types();
// The pointer-to-member-function types read so far (Type::memfn indexes it; Round 31), for their decorated names.
struct MemFnType;
std::vector<MemFnType>& memfn_types();

struct Type {
    int pointers = 0;
    // 0 void, 1 int, 2 char, 3 struct or union, 4 short
    int base = 1;
    int array_len = -1;
    // Dimensions after the first. int a[2][3][4][5] stores 3, 4, 5 here.
    std::vector<int> more;
    int struct_id = -1;
    int func_id = -1;
    // A class template argument pattern while a template's parameter types are read (index into
    // the pattern table), or -2 for a name that cannot be deduced (typename T::type).
    int tpat = -1;
    int bits = -1;
    int bit_off = 0;
    bool is_unsigned = false;
    bool is_const = false;
    // A parameter or return type written `const T&` or `const T*`: the target is const. Only decorated names and
    // overload choice look at it.
    bool pconst = false;
    bool is_ref = false;
    // `T&&` with a class (or template) T: an rvalue reference; is_ref is set too. It binds to a temporary or to the
    // result of std::move, and takes part in the choice of a move constructor or a move assignment.
    bool rref = false;
    // bool: one byte holding 0 or 1 (base 2, unsigned). A conversion to it compares with zero.
    bool is_bool = false;
    // long long, unsigned long long, __int64: a base 1 integer that is 8 bytes wide and lives in rax.
    bool wide = false;
    // An enumeration (its values are int here): its index in enum_names(), or -1. The decorated name of the type is
    // W4 name @@ (the public MSVC rule), so a template instance for an enumeration is not the one for int.
    int enum_id = -1;
    bool is_enum() const { return enum_id >= 0; }
    // A function parameter pack (Rest... rest) while a template's parameter types are read.
    bool pack = false;
    // A placeholder return type ([dcl.spec.auto]): 1 `auto`, 2 `decltype(auto)`; 0 a written type. The function's
    // body is read once to learn the type of its first return (parse_function_body), then the type is replaced.
    int deduce = 0;
    // A pointer to a data member ([dcl.mptr], `T C::*`; Round 30): its index in memptr_types(), or -1. The value is an
    // int: the member's offset in C, and -1 for a null member pointer (Microsoft's representation for a class without
    // virtual bases, from Round 31; Round 30 kept the offset plus one and 0 for null).
    int memptr = -1;
    // A pointer to a member function ([dcl.mptr], `R (C::*)(P...)`; Round 31): its index in memfn_types(), or -1. The
    // type is also a function pointer (pointers 1, func_id) whose first parameter is `C*`: the value is the address of
    // the function, or of a thunk that makes the virtual call (Microsoft's representation for a class with one base or
    // none), and 0 for null.
    int memfn = -1;
    bool pointer() const { return pointers > 0; }
    bool is_array() const { return array_len >= 0; }
    bool is_func() const { return func_id >= 0; }
};

// A template argument: a type or a value. In a pattern (a specialization or a function template's
// parameter type) a value that is a template parameter has marker set to the parameter's index.
struct TArg {
    bool is_value = false;
    long long value = 0;
    Type type{};
    int marker = -1;
    // A parameter pack (typename... Ts): the types it stands for ([temp.variadic]).
    bool is_pack = false;
    std::vector<Type> pack;
    // In a specialization's pattern, a pack argument ending in `Rest...`: the index of the
    // specialization's own pack parameter Rest, which takes the arguments left over; else -1.
    int rest = -1;
};

struct FuncType {
    Type ret{};
    std::vector<Type> params;
    bool vararg = false;
};

struct MemFnType {
    Type cls{};
    Type ret{};
    std::vector<Type> params;  // without the object parameter
    bool is_const = false;     // R (C::*)(P...) const
};

struct Field {
    std::string name;
    Type type{};
    int offset = 0;
    // A default member initializer ([class.mem]): the token where its expression starts, or -1.
    // `T x{};` has no expression and gives zero (init_zero).
    int init_at = -1;
    // `T x{a, b};`: init_at is the first argument, and the list ends at the '}'.
    bool init_braced = false;
    bool init_zero = false;
    // `T x = {a, b};`: init_at is the '{' itself; an array or aggregate member is filled element by element.
    bool init_aggr = false;
};

struct Node;

// A member function. The first parameter of the compiled function is an
// implicit pointer to the object, named "this".
struct Method {
    std::string name;
    // Internal function name ("C::f/1") and the MSVC decorated symbol.
    std::string qname;
    std::string symbol;
    Type ret{};
    std::vector<Type> params;
    std::vector<std::string> param_names;
    bool is_virtual = false;
    bool pure = false;
    bool is_const = false;
    bool has_body = false;
    // Defined outside its class (`T C::f() {}`): one definition for the program. A member defined inside the class body is
    // an inline function: every file that includes the class gets its own copy (internal symbol).
    bool out_of_class = false;
    int slot = -1;
    // 'A' private, 'I' protected, 'Q' public: the MSVC access letters.
    char access = 'Q';
    // The class that declared the function.
    int owner = -1;
    // Token index of the body's '{' (or of the ':' that starts a constructor's
    // initializer list) until the body has been compiled.
    size_t body_at = 0;
    // Where the body's '{' was, kept after the body is compiled (body_at is then cleared).
    size_t tok_body = 0;
    // Template parameter names of an out-of-class definition (template <typename U> U S<U>::f()).
    std::vector<std::string> ool_names;
    // A constructor or destructor the compiler made because a member or base
    // needs one. It has no tokens.
    bool implicit = false;
    // The implicit copy constructor: copies the base and each member.
    bool implicit_copy = false;
    // A static member function: no this, decorated with S/C/K instead of the Q/A/I letters.
    bool is_static = false;
    // The decorated spelling of a member function template instance's name ("$add@H"), else empty.
    std::string tsym;
    // Default arguments ([dcl.fct.default]): defaults[i] is the expression for parameter i, or an empty Block.
    std::vector<Node> defaults;
    // Round 29, multiple inheritance ([class.mi]): a function reached through a base that is not at offset 0 of the
    // class whose list holds this entry. A direct call passes the object's address plus this_adj, as a pointer to
    // this_sid (-1: the owner); a virtual call reads the table pointer at vt_adj (the base subobject whose table holds
    // the slot) and passes that address, as a pointer to vt_sid (-1: the owner). Both are 0 for the first base.
    int this_adj = 0;
    int this_sid = -1;
    int vt_adj = 0;
    int vt_sid = -1;
};

// A static data member, or a constant (static const integral, an enumerator) of a class.
struct StaticMem {
    Type type{};
    std::string symbol;  // the global that holds it (not for a constant)
    bool has_const = false;
    long long cval = 0;
};

struct StructDef {
    std::string name;
    // Declared (`class X;`, or named after `struct`) and not yet defined: only pointers and references to it can be
    // made ([basic.def], [class.name]). The definition fills this same entry.
    bool incomplete = false;
    // The name in decorated symbols when it differs from name (a class template instance Box<H>
    // is ?$Box@H there).
    std::string sym_name;
    const std::string& symname() const { return sym_name.empty() ? name : sym_name; }
    std::vector<Field> fields;
    int size = 0;
    int align = 1;
    bool is_union = false;
    bool is_class = false;
    // True for a class, or any struct with a base class or member functions.
    bool cxx = false;
    // The first base ([class.derived]): with several, the first one with virtual functions (it shares the class's
    // address and its table is the class's own, as in the Microsoft x64 layout), else the first one written.
    int base = -1;
    // Every direct base in declaration order (Round 29): its class, its offset in this class, and where its fields
    // are in `fields` (the bases' fields come first, then this class's own from base_fields on).
    struct BaseInfo {
        int sid = -1;
        int off = 0;
        size_t f0 = 0;
        size_t f1 = 0;
    };
    std::vector<BaseInfo> bases;
    int bases_size = 0;  // the end of the last base: this class's own fields start here (before alignment)
    // The tables of the bases that are not at offset 0 and have virtual functions (one per such base subobject, the
    // ones nested in them included): where the table pointer is, the subobject's class, the slots (an override in this
    // class is an adjustor thunk), the destructor's slot (-1: none) and the table's symbol.
    struct SecTable {
        int off = 0;
        int sid = -1;
        std::vector<std::string> slots;
        int dtor_slot = -1;
        std::string sym;
    };
    std::vector<SecTable> sec_tables;
    size_t base_fields = 0;
    bool poly = false;
    std::vector<Method> methods;
    // Symbol of the function in each virtual slot. Empty for a pure virtual.
    std::vector<std::string> vtable;
    std::string vtable_sym;
    // typedef and using names declared inside the class (typename C::name).
    std::map<std::string, Type> tdefs;
    // Static data members and class-scope constants, by name (ISO/IEC 14882 [class.static.data]).
    std::map<std::string, StaticMem> statics;
    // A lambda's closure class: the captured variables kept by reference. The field holds the address; a use of
    // the name reads through it.
    std::set<std::string> ref_caps;
    std::set<std::string> ref_members;  // `T& name;` members: also in ref_caps (the field holds an address)
    // The class template and the arguments of an instance, for deduction through Node<T>.
    std::string tmpl;
    std::vector<TArg> targs;
    // Anonymous struct and union members (`union { struct { DWORD Low; LONG High; }; long long Quad; }`,
    // [class.union.anon] and the Microsoft anonymous struct): the hidden fields that hold them. Their members are
    // named as members of this class (lookup goes through the hidden field).
    std::vector<std::string> anon_fields;
    // The class this one is nested in (-1 at namespace scope). Classes are kept under their plain names, so two nested
    // classes of one name (Outer1::Entry and Outer2::Entry) are told apart by it: a name used inside a class finds the
    // class nested there first (find_struct), and members of a nested class are decorated with the outer class too.
    int parent = -1;
    // The namespace the class was declared in ("okay::crypto"; empty at global scope). Classes are kept by their plain
    // names; when two at namespace scope share one, the later one is decorated Name@ns (ns_dup) and lookups prefer the
    // one of the namespace written in front of the name, then the one of the namespace being read.
    std::string ns;
    bool ns_dup = false;
    // A local class written in a member function of a class: that class, whose static members its member functions
    // name by their plain names ([class.local], [basic.lookup.unqual] 8). -1 otherwise.
    int local_in = -1;
    // A class defined in a function body (Round 30): its member functions name the function's static locals.
    bool local_fn = false;
    // A lambda's closure class that captured `this` ([expr.prim.lambda.capture] 10): the class of the member function the
    // lambda was written in. The pointer is the closure's field "__this"; names of that class's members used in the
    // body are read through it.
    int lam_outer = -1;
    // Any lambda's closure class written in a member function: that class. Its static members are found from the body
    // without a capture ([expr.prim.lambda.closure] 13, names are looked up in the enclosing scopes).
    int lam_scope = -1;
};

struct Global {
    std::string name;
    Type type{};
    bool bss = true;
    bool is_static = false;
    uint32_t init = 0;
    std::vector<uint8_t> init_bytes;
    // A virtual function table: read-only data, one ADDR64 slot per entry.
    bool vtable = false;
    std::vector<std::string> slots;
    // Pointers in the initial data (a string literal's address): offset in the data and the
    // symbol it points to, filled by an ADDR64 relocation.
    std::vector<std::pair<uint32_t, std::string>> addr_relocs;
    // Some of its value is stored at startup (__okay_init) or on first use (a static local): its bytes are not its
    // value, so even a const one is never read as a compile-time constant.
    bool dynamic = false;
    // A virtual table: the type information object of its class, whose address is in the slot just before the first
    // function (the table pointer of an object points at the first function). Empty for none.
    std::string rtti;
    // A virtual table: the offset of the subobject that points at it inside the whole object, kept in the slot before
    // the type information (Round 29; dynamic_cast finds the whole object with it). 0 for a class's own table.
    long long top_off = 0;
    // thread_local ([basic.stc.thread]): kept in the .tls$ section, one copy per thread made by the Windows loader from
    // the image's TLS template; reached through the thread's TLS array (gs:[0x58]) and _tls_index.
    bool tls = false;
    // `extern T name;` ([basic.def] 2.3): declared here, defined in another file; no storage, an undefined symbol. A
    // definition later in the same file takes its place.
    bool external = false;
};

inline Type peel_array(const Type& type) {
    Type elem = type;
    if (elem.array_len >= 0) {
        if (elem.more.empty()) elem.array_len = -1;
        else {
            elem.array_len = elem.more.front();
            elem.more.erase(elem.more.begin());
        }
        return elem;
    }
    if (elem.pointers > 0) --elem.pointers;
    return elem;
}

struct Node {
    enum Kind {
        Block, Return, If, While, For, Do, Switch, Case, Break, Continue, ExprStmt,
        IntLit, StringLit, Name, Unary, Binary, Assign, Call, Index, Field,
        Ternary, Goto, Label
    } kind = Block;
    std::string text;
    uint64_t number = 0;
    Tok op = TEnd;
    Type type{};
    int32_t offset = 0;
    bool imported = false;
    // Line of the preprocessed text where a statement starts. 0 is unknown.
    int line = 0;
    // A name read by the parser: the parser's count of declared locals (Local::seq) at that point, or -1. Of two locals
    // of one name the code generator takes one declared before the name was read ([basic.scope.pdecl], Round 30).
    int vis = -1;
    std::vector<Node> kids;
};

// A part of a static initializer that is not a constant: stored at run time (Parser::dyn_leaf_stmts).
struct DynLeaf {
    int at = 0;
    Type type{};
    Node value{};
};

struct Local {
    std::string name;
    Type type{};
    int32_t offset = 0;
    bool is_static = false;
    bool is_const = false;
    std::string symbol;
    int reg = 0;
    // 0 is the parameter scope. A nested block uses a higher number.
    int scope = 0;
    // `const int k = 8;` / `constexpr int k = 8;` in a function: an integer constant ([expr.const]), usable as an array
    // length or template argument; cval is its value in the type's width.
    bool has_cval = false;
    uint64_t cval = 0;
    // When it was declared: the parser's running count of declared locals (place_local), 0 for one made otherwise.
    // A name read before it (Node::vis) does not mean it (Round 30).
    int seq = 0;
};

struct FuncDecl {
    std::string name;
    Type ret{};
    std::vector<Type> params;
    std::vector<std::string> param_names;
    bool imported = false;
    bool exported = false;
    bool defined = false;
    bool is_static = false;
    bool vararg = false;
    bool sret = false;
    // Default arguments, as for Method::defaults.
    std::vector<Node> defaults;
    std::string symbol;
    int func_id = -1;
    int sret_off = 0;
    int param_base = 0;
    Node body;
    std::vector<Local> locals;
    // Which parameters are references. In params they are pointers (that is what is passed);
    // the body sees them as references again.
    std::vector<bool> param_ref;
    // The function returns a reference: ret is then a pointer and a call is dereferenced.
    bool ret_ref = false;
    int nparams = 0;
    int max_args = 0;
    // First and last line of the body (the braces), for the line table.
    int line = 0;
    int end_line = 0;
    // An adjustor thunk (Round 29): no body; the code subtracts thunk_adj from the register that holds `this` (rcx, or
    // rdx when the target returns a class through memory) and jumps to thunk_target.
    std::string thunk_target;
    int thunk_adj = 0;
    bool thunk_rdx = false;
    // A virtual call thunk (Round 31, the value of &C::f for a virtual f): the code reads the table pointer at
    // thunk_vptr in the object (rcx, or rdx as above) and jumps to the function in slot thunk_vslot. -1: not one.
    int thunk_vslot = -1;
    int thunk_vptr = 0;
};

// Where the lines of the preprocessed text came from, for the line table.
struct LineSource {
    bool enabled = false;
    const std::vector<std::pair<int, int>>* origin = nullptr;
    const std::vector<std::string>* files = nullptr;
};


// ---- Functions one part of the compiler calls in another (the definitions are in the named file) ----

struct Gen;

// preprocess.cpp
// A ' at i of s is a digit separator (50'000'000, 0x7F'FF; [lex.icon] 1, [lex.ppnumber]), not the start of a character
// literal: it stands between two hex digits inside a number that starts with a decimal digit.
inline bool is_digit_separator(const std::string& s, size_t i) {
    if (i == 0 || i + 1 >= s.size() || s[i] != '\'') return false;
    if (!std::isxdigit(static_cast<unsigned char>(s[i - 1])) || !std::isxdigit(static_cast<unsigned char>(s[i + 1])))
        return false;
    size_t start = i;
    while (start > 0 && (std::isalnum(static_cast<unsigned char>(s[start - 1])) || s[start - 1] == '\'' ||
                         s[start - 1] == '_'))
        --start;
    return std::isdigit(static_cast<unsigned char>(s[start])) != 0;
}
std::string strip_comments(const std::string& in);
bool read_file(const std::string& path, std::string* text);

// lex.cpp
std::vector<Token> lex(const std::string& text, std::string* error);
bool is_assign_op(Tok op);
bool is_compound_op(Tok op);

// mangle.cpp
std::string cxx_type(const Type& type, const std::vector<StructDef>* structs = nullptr);
std::string cxx_method_symbol(const std::vector<StructDef>& structs, const StructDef& owner, const Method& m);
std::string sdd_symbol(const StructDef& owner, const Method& m);
std::string cxx_symbol(const std::string& ns, const std::string& name, const Type& ret, const std::vector<Type>& params,
                       const std::vector<StructDef>* structs = nullptr);

// const_fold.cpp
uint64_t const_norm(uint64_t v, const Type& t);
Type const_common(const Type& a, const Type& b);
double lit_double(const Node& n);
bool const_convert(const Node& lit, const Type& to, uint64_t* bits);
bool fold_fp_binary(Tok op, const Node& l, const Node& r, Node* out);
bool fold_int_binary(Tok op, const Node& l, const Node& r, Node* out, std::string* err);

// parse_call.cpp
bool has_effects(const Node& node);

// optimize.cpp
bool address_taken(const Node& node, const std::string& name);
bool modified_in_place(const Node& node, const std::string& name);
void prop_node(Node* node, std::map<std::string, uint64_t>* known, const std::set<std::string>* shadowed,
               const std::map<std::string, Type>* ltypes);
void inline_node(Node* node, const std::vector<FuncDecl>& funcs);
void walk_inline(Node* node, FuncDecl* caller, const std::vector<FuncDecl>& funcs, Gen& gen, int* seq);

// emit.cpp
bool emit_functions(std::vector<FuncDecl>& funcs, const std::vector<StructDef>& structs,
                    const std::vector<Global>& globals, const std::vector<FuncType>& func_types, bool optimize,
                    int opt_level, const LineSource& line_source, std::string* error, CompileOutput* out);

}  // namespace compiler
}  // namespace okayms
