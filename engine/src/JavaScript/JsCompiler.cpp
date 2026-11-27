// okay::js compiler: syntax tree -> our own stack bytecode (JsInternal.hpp, Op).
// Written from ECMA-262: 9.1 environment records (resolved at compile time into
// frame slots, captured cells and global lookups), 10.2.11
// FunctionDeclarationInstantiation (parameters, then hoisted var and function
// declarations), 14.2 blocks and lexical declarations (let/const with a
// "not initialized yet" state), 14.7 loops (a fresh binding per iteration for
// captured let variables, 14.7.4.4 CreatePerIterationEnvironment), 14.15 try
// statements (a finally block is entered with a completion kind and leaves by
// the same kind), 13.15.5 destructuring, 15.7 class definitions and 27.5 / 27.7
// generators and async functions.
//
// The design (our own): every function gets a slot array; a variable a nested
// function uses lives in a Cell in its slot, others live directly in the slot.
// The operand stack is empty between statements, so a catch or a finally only
// has to restore the frame's stack height. Iterators, switch values and the
// completion of a finally live in hidden slots.
#include "JsInternal.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace okay::js {

namespace {

struct CompileFail {};

enum VarKind : std::uint8_t { VK_Var, VK_Let, VK_Const, VK_Hidden, VK_Param, VK_Func, VK_Class, VK_CatchParam,
                               VK_Self /* a named function expression's own name: the body's declarations shadow it */ };

struct Var {
    std::u16string name;
    int slot;
    bool captured;
    VarKind kind;
    bool initKnown;   ///< reads after this point in the same function can skip the not-initialized check
};

struct Scope {
    std::vector<Var> vars;
    bool fnScope = false;
    bool inSwitch = false;
};

enum CtlKind : std::uint8_t { CK_Loop, CK_Switch, CK_Label, CK_Try, CK_Finally };

struct Ctl {
    CtlKind kind;
    std::vector<std::u16string> labels;
    std::vector<int> breaks, continues;
    int iterSlot = -1;      ///< a for-of loop: the slot of its iterator record
    int finIndex = -1;      ///< CK_Finally: index in FS::finals
};

struct FinExit {
    int id;
    int target;     ///< ctl index
    bool isContinue;
};

struct FinallyInfo {
    int kindSlot, valueSlot;
    std::vector<int> entryJumps;   ///< jumps to the finally block
    std::vector<FinExit> exits;
    bool hasReturn = false;
};

struct UpName {
    std::u16string name;
    const void* owner;
    int slot;
};

struct FS {
    FS* parent = nullptr;
    FuncInfo* fi = nullptr;
    Proto* p = nullptr;
    std::vector<std::unique_ptr<Scope>> scopes;
    int nslots = 0;
    int depth = 0, maxDepth = 0;
    std::vector<Ctl> ctl;
    std::vector<FinallyInfo> finals;
    std::vector<UpName> upNames;
    bool isScript = false;
    bool strict = true;      ///< false only for sloppy page scripts (Runtime::sloppyScripts) outside "use strict" and classes
    int thisCell = -1;       ///< slot of the captured %this cell, if any
    int exportSlot = -1;
    std::vector<std::u16string> pendingLabels;
};

struct Compiler {
    Runtime& rt;
    SyntaxError& err;
    std::string chunk;
    FS* fs = nullptr;
    int line = 0;

    Compiler(Runtime& r, SyntaxError& e, const std::string& c) : rt(r), err(e), chunk(c) {}

    [[noreturn]] void Fail(Node* at, const std::string& m) {
        err.line = at ? at->line : line;
        err.column = at ? at->col : 0;
        err.message = m;
        throw CompileFail{};
    }

    // ---------------------------------------------------------------- emission
    static int Effect(int op, int a) {
        switch (op) {
            case OP_Undef: case OP_Null: case OP_True: case OP_False: case OP_PushEmpty: case OP_Const: case OP_Int:
            case OP_Dup: case OP_GetLoc: case OP_GetLocChk: case OP_GetCell: case OP_GetCellChk: case OP_GetUp:
            case OP_GetUpChk: case OP_GetGlobal: case OP_TypeofGlobal: case OP_NewObject: case OP_LoadThis:
            case OP_LoadThisChk: case OP_LoadNewTarget: case OP_LoadFunc: case OP_LoadHome: case OP_Closure:
            case OP_Arguments: case OP_RestArgs: case OP_DupX1: case OP_DupX2: case OP_GetPropKeep: case OP_TemplateObj:
                return 1;
            case OP_Dup2: return 2;
            case OP_Pop: case OP_PutLoc: case OP_PutCell: case OP_PutUp: case OP_InitLex: case OP_SetProp:
            case OP_GetElem: case OP_In: case OP_InstanceOf: case OP_Add: case OP_Sub: case OP_Mul: case OP_Div:
            case OP_Mod: case OP_Pow: case OP_BitAnd: case OP_BitOr: case OP_BitXor: case OP_Shl: case OP_Sar:
            case OP_Shr: case OP_Eq: case OP_Ne: case OP_StrictEq: case OP_StrictNe: case OP_Lt: case OP_Le:
            case OP_Gt: case OP_Ge: case OP_JumpF: case OP_JumpT: case OP_ArrPush: case OP_ArrSpread:
            case OP_DefFieldK: case OP_CopyProps: case OP_DelElem: case OP_RunFields: case OP_StoreThis:
            case OP_IterClose: case OP_IterStep: case OP_Throw: case OP_LtJumpF: case OP_Return:
            case OP_Class: case OP_IterSend: case OP_SetFieldInit: case OP_NewRegExp: case OP_SetProtoLit:
                return op == OP_LtJumpF ? -2 : op == OP_Class ? 1 : -1;
            case OP_SetElem: case OP_DefField: case OP_DefGetter: case OP_DefSetter: case OP_DefMethod:
            case OP_SuperGet:
                return -2;
            case OP_SuperSet: return -3;
            case OP_Call: return -(a + 1);
            case OP_CallSpread: return -2;
            case OP_New: return -(a + 1);
            case OP_NewSpread: return -2;
            case OP_SuperCall: return -a + 1;
            case OP_SuperCallSpread: return 0;
            case OP_NewArray: return -a + 1;
            case OP_RestObject: return -a;
            case OP_GetElemKeep: return 0;
            default: return 0;
        }
    }

    int Emit(int op, int a = 0, int b = 0) {
        Instr in;
        in.op = (std::uint16_t)op;
        in.a = a;
        in.b = (std::uint16_t)b;
        fs->p->code.push_back(in);
        fs->p->lines.push_back(line);
        fs->depth += Effect(op, a);
        if (fs->depth > fs->maxDepth) fs->maxDepth = fs->depth;
        if (fs->depth < 0) fs->depth = 0;
        return (int)fs->p->code.size() - 1;
    }
    int Here() const { return (int)fs->p->code.size(); }
    void Patch(int at) { fs->p->code[(std::size_t)at].a = Here(); }
    void PatchTo(int at, int target) { fs->p->code[(std::size_t)at].a = target; }
    int EmitJump(int op) { return Emit(op, -1); }

    int Const(const Value& v) {
        auto& c = fs->p->consts;
        for (std::size_t i = 0; i < c.size(); ++i)
            if (c[i].t == v.t && (v.IsStr() ? c[i].p == v.p : v.IsNum() ? (c[i].n == v.n && std::signbit(c[i].n) == std::signbit(v.n)) : false))
                return (int)i;
        c.push_back(v);
        return (int)c.size() - 1;
    }
    int AtomConst(const std::u16string& s) { return Const(Value::Str(rt.Intern(s))); }
    int AtomConst(const char* s) { return Const(Value::Str(rt.Intern(s))); }
    void EmitNum(double d) {
        if (d == std::floor(d) && d >= -1000000000.0 && d <= 1000000000.0 && !(d == 0 && std::signbit(d))) Emit(OP_Int, (int)d);
        else Emit(OP_Const, Const(Value::Num(d)));
    }
    void EmitStr(const std::u16string& s) { Emit(OP_Const, AtomConst(s)); }

    // ---------------------------------------------------------------- scopes and variables
    Scope* CurScope() { return fs->scopes.back().get(); }
    void PushScope(bool fnScope = false) {
        fs->scopes.push_back(std::make_unique<Scope>());
        fs->scopes.back()->fnScope = fnScope;
    }
    void PopScope() { fs->scopes.pop_back(); }
    int NewSlot() { return fs->nslots++; }

    bool IsCaptured(const std::u16string& name) const { return fs->fi && fs->fi->innerRefs.count(name) > 0; }

    Var* FindIn(Scope* s, const std::u16string& n) {
        for (auto& v : s->vars)
            if (v.name == n) return &v;
        return nullptr;
    }

    Var* Declare(const std::u16string& name, VarKind kind, Node* at, Scope* sc = nullptr) {
        Scope* s = sc ? sc : CurScope();
        if (Var* ex = FindIn(s, name)) {
            bool bothVar = (ex->kind == VK_Var || ex->kind == VK_Func || ex->kind == VK_Param) &&
                           (kind == VK_Var || kind == VK_Func);
            if (bothVar && !(ex->kind == VK_Param && kind == VK_Func)) return ex;
            if (ex->kind == VK_Param && kind == VK_Func) { ex->kind = VK_Func; return ex; }
            if (ex->kind == VK_Hidden) { ex->kind = kind; return ex; }
            if (ex->kind == VK_Self) {   // 15.2.5: the name lives in an outer scope, so a body declaration may shadow it
                ex->kind = kind;
                ex->initKnown = !(kind == VK_Let || kind == VK_Const || kind == VK_Class);
                return ex;
            }
            Fail(at, "'" + Utf16To8(name) + "' is already declared in this scope");
        }
        Var v;
        v.name = name;
        v.slot = NewSlot();
        v.captured = IsCaptured(name);
        v.kind = kind;
        v.initKnown = !(kind == VK_Let || kind == VK_Const || kind == VK_Class);
        s->vars.push_back(v);
        return &s->vars.back();
    }

    struct Res {
        int kind;   // 0 local, 1 upvalue, 2 global
        int index;
        Var* var;
    };

    int UpIndex(FS* f, const std::u16string& name, FS* owner, Var* v) {
        for (std::size_t i = 0; i < f->upNames.size(); ++i)
            if (f->upNames[i].name == name && f->upNames[i].owner == owner && f->upNames[i].slot == v->slot) return (int)i;
        UpDesc d;
        if (f->parent == owner) {
            d.fromLocal = true;
            d.index = (std::uint32_t)v->slot;
            if (!v->captured) Fail(nullptr, "internal error: a captured variable was not boxed ('" + Utf16To8(name) + "')");
        } else {
            d.fromLocal = false;
            d.index = (std::uint32_t)UpIndex(f->parent, name, owner, v);
        }
        f->p->ups.push_back(d);
        f->upNames.push_back(UpName{name, owner, v->slot});
        return (int)f->upNames.size() - 1;
    }

    Res Resolve(const std::u16string& name) {
        for (FS* f = fs; f; f = f->parent) {
            for (auto it = f->scopes.rbegin(); it != f->scopes.rend(); ++it)
                if (Var* v = FindIn(it->get(), name)) {
                    if (f == fs) return Res{0, v->slot, v};
                    return Res{1, UpIndex(fs, name, f, v), v};
                }
        }
        return Res{2, AtomConst(name), nullptr};
    }

    static bool NeedsCheck(const Var* v) { return v && !v->initKnown; }

    void EmitLoad(const std::u16string& name, Node* at) {
        Res r = Resolve(name);
        int nameK = AtomConst(name);
        if (r.kind == 0) {
            if (r.var->captured) Emit(NeedsCheck(r.var) ? OP_GetCellChk : OP_GetCell, r.index, nameK);
            else Emit(NeedsCheck(r.var) ? OP_GetLocChk : OP_GetLoc, r.index, nameK);
        } else if (r.kind == 1) {
            Emit(r.var->kind == VK_Let || r.var->kind == VK_Const || r.var->kind == VK_Class || r.var->kind == VK_Hidden
                     ? OP_GetUpChk : OP_GetUp, r.index, nameK);
        } else {
            if (name == u"undefined") { Emit(OP_Undef); return; }
            if (name == u"NaN") { Emit(OP_Const, Const(Value::Num(NAN))); return; }
            if (name == u"Infinity") { Emit(OP_Const, Const(Value::Num(INFINITY))); return; }
            (void)at;
            Emit(OP_GetGlobal, r.index);
        }
    }

    // Store the top of the stack into name. keep: leave the value on the stack.
    void EmitStore(const std::u16string& name, bool keep, Node* at, bool init = false) {
        Res r = Resolve(name);
        if (r.kind != 2 && !init && (r.var->kind == VK_Const)) {
            Emit(OP_ThrowConst, AtomConst(u"assignment to the constant '" + name + u"'"), (int)ErrorType::TypeError);
            if (!keep) Emit(OP_Pop);
            return;
        }
        if (r.kind != 2 && !init && r.var->kind == VK_Class && r.var->name == name && r.var->initKnown == false) {
            // assigning to a class name inside its own body
        }
        if (r.kind == 0) {
            if (r.var->captured) Emit(keep ? OP_SetCell : OP_PutCell, r.index);
            else Emit(keep ? OP_SetLoc : OP_PutLoc, r.index);
        } else if (r.kind == 1) Emit(keep ? OP_SetUp : OP_PutUp, r.index);
        else {
            if (init) {   // a top-level lexical declaration
                if (keep) Emit(OP_Dup);
                Emit(OP_InitLex, r.index);
                return;
            }
            Emit(OP_SetGlobal, r.index, fs->strict ? 0 : 1);   // b = 1: an undeclared name becomes a global (sloppy)
            if (!keep) Emit(OP_Pop);
        }
        (void)at;
    }

    // ---------------------------------------------------------------- hoisting
    void CollectVarNames(Node* n, std::vector<std::pair<std::u16string, Node*>>& out) {
        if (!n) return;
        switch (n->k) {
            case NK::VarDecl:
                if (n->op == DK_Var)
                    for (Node* d : n->list) {
                        std::vector<std::u16string> names;
                        PatternNames(d->a, names);
                        for (auto& s : names) out.push_back({s, d});
                    }
                return;
            case NK::Block: for (Node* s : n->list) CollectVarNames(s, out); return;
            case NK::If: CollectVarNames(n->b, out); CollectVarNames(n->c, out); return;
            case NK::For: CollectVarNames(n->a, out); CollectVarNames(n->d, out); return;
            case NK::ForIn: case NK::ForOf: CollectVarNames(n->a, out); CollectVarNames(n->d, out); return;
            case NK::While: case NK::DoWhile: CollectVarNames(n->b, out); return;
            case NK::Try: CollectVarNames(n->a, out); CollectVarNames(n->c, out); CollectVarNames(n->d, out); return;
            case NK::Switch: for (Node* c : n->list) for (Node* s : c->list) CollectVarNames(s, out); return;
            case NK::Labeled: CollectVarNames(n->a, out); return;
            case NK::ExportDefault: return;
            default: return;
        }
    }
    static void PatternNames(Node* p, std::vector<std::u16string>& out) {
        if (!p) return;
        switch (p->k) {
            case NK::Ident: out.push_back(p->str); break;
            case NK::AssignPat: PatternNames(p->a, out); break;
            case NK::RestElem: PatternNames(p->a, out); break;
            case NK::ArrayPat: for (Node* e : p->list) PatternNames(e, out); break;
            case NK::ObjectPat: for (Node* e : p->list) PatternNames(e->k == NK::PatProp ? e->b : e, out); break;
            default: break;
        }
    }

    // Declare the lexical names of a statement list (let/const/class/function) in the current scope.
    void DeclareLexicals(const std::vector<Node*>& list, bool topLevelScript) {
        for (Node* s : list) {
            Node* d = s;
            if (d->k == NK::ExportDefault) {
                if (!d->b) continue;
                d = d->b;
                if (d->k == NK::Class) {   // export default class Name {}
                    if (topLevelScript) continue;
                    Declare(d->str, VK_Class, d);
                    continue;
                }
            }
            if (d->k == NK::VarDecl && d->op != DK_Var) {
                if (topLevelScript) continue;
                for (Node* dc : d->list) {
                    std::vector<std::u16string> names;
                    PatternNames(dc->a, names);
                    for (auto& nm : names) Declare(nm, d->op == DK_Let ? VK_Let : VK_Const, dc);
                }
            } else if (d->k == NK::ClassDecl) {
                if (topLevelScript) continue;
                Declare(d->a->str, VK_Class, d);
            } else if (d->k == NK::FuncDecl && !CurScope()->fnScope) {
                if (topLevelScript) continue;
                Declare(d->a->fn->name, VK_Func, d);
            }
        }
    }

    // Cells for captured lexical variables of the current scope, and the not-initialized marks.
    void InitLexicalSlots() {
        for (Var& v : CurScope()->vars) {
            if (v.kind == VK_Let || v.kind == VK_Const || v.kind == VK_Class) {
                if (v.captured) Emit(OP_NewCell, v.slot, 1);
                else if (fs->ctl.size() > 0 || true) { Emit(OP_PushEmpty); Emit(OP_PutLoc, v.slot); }
            } else if (v.kind == VK_Func && v.captured) Emit(OP_NewCell, v.slot, 0);
        }
    }

    void HoistFunctions(const std::vector<Node*>& list) {
        for (Node* s : list) {
            Node* d = s;
            if (d->k == NK::ExportDefault && d->b && d->b->k == NK::FuncDecl) d = d->b;
            if (d->k != NK::FuncDecl) continue;
            line = d->line;
            EmitClosure(d->a);
            if (fs->isScript && CurScope()->fnScope) {
                Emit(OP_DeclFunc, AtomConst(d->a->fn->name));
            } else EmitStore(d->a->fn->name, false, d, true);
        }
    }

    // ---------------------------------------------------------------- functions
    Proto* CompileFunction(FuncInfo* fi, bool namedExpr) {
        FS f;
        f.parent = fs;
        f.fi = fi;
        f.strict = !rt.sloppyScripts || (fs && fs->strict) || classDepth > 0 || HasUseStrict(fi->exprBody ? nullptr : fi->body);
        Proto* p = new Proto();
        protos.push_back(p);   // linked into the heap by CompileScript's caller
        f.p = p;
        p->chunk = chunk;
        p->line = fi->line;
        p->length = fi->length;
        if (!fi->name.empty()) p->name = rt.Intern(fi->name);
        if (fi->arrow) p->flags |= PF_Arrow;
        if (fi->generator) p->flags |= PF_Generator;
        if (fi->async) p->flags |= PF_Async;
        if (fi->method || fi->getter || fi->setter || fi->fieldInit) p->flags |= PF_Method;
        if (fi->classCtor) p->flags |= PF_ClassCtor;
        if (fi->derived) p->flags |= PF_Derived;
        FS* saved = fs;
        fs = &f;
        int savedLine = line;
        line = fi->line;
        PushScope(true);
        // parameters: simple names sit in their argument slots
        p->nparams = (std::uint32_t)fi->params.size();
        bool hasRest = !fi->params.empty() && fi->params.back()->k == NK::RestElem;
        if (hasRest) p->nparams -= 1;
        if (hasRest || fi->usesArguments) p->flags |= PF_NeedsExtra;
        if (fi->classCtor && fi->derived && !fi->body) p->flags |= PF_NeedsExtra;   // constructor(...args) { super(...args); }
        std::vector<int> paramSlots;
        for (std::size_t i = 0; i < p->nparams; ++i) paramSlots.push_back(NewSlot());
        if (fi->simpleParams) {
            for (std::size_t i = 0; i < p->nparams; ++i) {
                Var v;
                v.name = fi->params[i]->str;
                v.slot = paramSlots[i];
                v.captured = IsCaptured(v.name);
                v.kind = VK_Param;
                v.initKnown = true;
                CurScope()->vars.push_back(v);
            }
        }
        if (fi->async) Emit(OP_AsyncStart);
        // hidden names arrows and nested code use
        if (!fi->arrow) {
            if (fi->innerRefs.count(u"%this")) {
                Var* v = Declare(u"%this", VK_Hidden, nullptr);
                v->initKnown = true;
                if (fi->derived) Emit(OP_NewCell, v->slot, 1);
                else {
                    Emit(OP_LoadThis);
                    Emit(OP_PutLoc, v->slot);
                    Emit(OP_NewCell, v->slot, 2);
                }
                f.thisCell = v->slot;
                if (!fi->derived) v->kind = VK_Hidden;
            }
            if (fi->innerRefs.count(u"%home")) {
                Var* v = Declare(u"%home", VK_Hidden, nullptr);
                v->initKnown = true;
                Emit(OP_LoadHome);
                Emit(OP_PutLoc, v->slot);
                Emit(OP_NewCell, v->slot, 2);
            }
            if (fi->innerRefs.count(u"%newtarget")) {
                Var* v = Declare(u"%newtarget", VK_Hidden, nullptr);
                v->initKnown = true;
                Emit(OP_LoadNewTarget);
                Emit(OP_PutLoc, v->slot);
                Emit(OP_NewCell, v->slot, 2);
            }
            if (fi->usesArguments) {
                Var* v = Declare(u"arguments", VK_Var, nullptr);
                Emit(OP_Arguments);
                if (v->captured) { Emit(OP_PutLoc, v->slot); Emit(OP_NewCell, v->slot, 2); }
                else Emit(OP_PutLoc, v->slot);
            }
            if (namedExpr && !fi->name.empty() && !FindIn(CurScope(), fi->name)) {
                Var* v = Declare(fi->name, VK_Self, nullptr);
                Emit(OP_LoadFunc);
                Emit(OP_PutLoc, v->slot);
                if (v->captured) Emit(OP_NewCell, v->slot, 2);
            }
        }
        // boxed simple parameters
        if (fi->simpleParams)
            for (Var& v : CurScope()->vars)
                if (v.kind == VK_Param && v.captured) Emit(OP_NewCell, v.slot, 2);
        // complex parameters: defaults and patterns, left to right
        if (!fi->simpleParams) {
            std::vector<std::u16string> names;
            for (Node* pn : fi->params) PatternNames(pn, names);
            for (auto& nm : names) {
                Var* v = Declare(nm, VK_Param, fi->params.empty() ? nullptr : fi->params[0]);
                if (v->captured) Emit(OP_NewCell, v->slot, 0);
            }
            for (std::size_t i = 0; i < fi->params.size(); ++i) {
                Node* pn = fi->params[i];
                line = pn->line;
                if (pn->k == NK::RestElem) {
                    Emit(OP_RestArgs, (int)p->nparams);
                    BindPattern(pn->a, 1);
                    continue;
                }
                Emit(OP_GetLoc, paramSlots[i]);
                BindPattern(pn, 1);
            }
        }
        // var declarations
        std::vector<std::pair<std::u16string, Node*>> vars;
        if (!fi->exprBody && fi->body) CollectVarNames(fi->body, vars);
        if (!fi->exprBody && fi->body)
            for (Node* s : fi->body->list)
                if (s->k == NK::FuncDecl) vars.push_back({s->a->fn->name, s});
        for (auto& vn : vars) {
            if (Var* ex = FindIn(CurScope(), vn.first)) {
                if (ex->kind == VK_Hidden) ex->kind = VK_Var;
                if (ex->kind == VK_Self) {   // `var f` inside `function f`: a new binding that starts undefined
                    ex->kind = VK_Var;
                    Emit(OP_Undef);
                    Emit(ex->captured ? OP_PutCell : OP_PutLoc, ex->slot);
                }
                continue;
            }
            Var* v = Declare(vn.first, VK_Var, vn.second);
            if (v->captured) Emit(OP_NewCell, v->slot, 0);
        }
        if (fi->generator) Emit(OP_GenStart);
        // class constructors: field initializers (a base class runs them first)
        if (fi->classCtor && !fi->derived && fi->hasFields) {
            Emit(OP_LoadThis);
            Emit(OP_RunFields);
        }
        if (fi->fieldInit) {
            for (Node* field : fi->body->list) {
                line = field->line;
                if (field->k == NK::ExprStmt) {   // a static { } block (an arrow called in place)
                    Expr(field->a);
                    Emit(OP_Pop);
                    continue;
                }
                Emit(OP_LoadThis);
                if (field->a) Expr(field->a);
                else Emit(OP_Undef);
                Emit(OP_DefFieldK, AtomConst(field->str));
                Emit(OP_Pop);
            }
            Emit(OP_ReturnUndef);
        } else if (fi->classCtor && !fi->body) {
            // default constructor: constructor(...args) { super(...args); }
            if (fi->derived) {
                Emit(OP_RestArgs, 0, 1);
                Emit(OP_SuperCallSpread);
                AfterSuperCall();
                Emit(OP_Pop);
            }
            Emit(OP_ReturnUndef);
        } else if (fi->exprBody) {
            line = fi->body->line;
            Expr(fi->body);
            EmitReturn();
        } else {
            DeclareLexicals(fi->body->list, false);
            InitLexicalSlots();
            HoistFunctions(fi->body->list);
            for (Node* s : fi->body->list) Stmt(s);
            Emit(OP_ReturnUndef);
        }
        PopScope();
        p->nslots = (std::uint32_t)f.nslots;
        p->maxStack = (std::uint32_t)f.maxDepth + 8;
        fs = saved;
        line = savedLine;
        return p;
    }

    void AfterSuperCall() {
        // [thisObj] -> this binding, the captured %this, then instance fields
        Emit(OP_Dup);
        Emit(OP_StoreThis);
        if (fs->thisCell >= 0) {
            Emit(OP_Dup);
            Emit(OP_PutCell, fs->thisCell);
        }
        if (fs->fi && fs->fi->hasFields) {
            Emit(OP_Dup);
            Emit(OP_RunFields);
        }
    }

    void EmitClosure(Node* fnNode) {
        Proto* child = CompileFunction(fnNode->fn, (fnNode->flags & NF_Computed) != 0);
        fs->p->protos.push_back(child);
        Emit(OP_Closure, (int)fs->p->protos.size() - 1);
    }

    // ---------------------------------------------------------------- control transfer
    // break / continue to ctl[target], running finally blocks, closing iterators and leaving try handlers on the way.
    void EmitJumpOut(int target, bool isContinue) {
        int stop = isContinue ? target + 1 : target;
        for (int i = (int)fs->ctl.size() - 1; i >= stop; --i) {
            Ctl& c = fs->ctl[(std::size_t)i];
            if (c.kind == CK_Try) Emit(OP_TryEnd);
            else if (c.kind == CK_Loop && c.iterSlot >= 0) {   // leaving a for-of: close its iterator
                Emit(OP_GetLoc, c.iterSlot);
                Emit(OP_IterClose);
            } else if (c.kind == CK_Finally) {
                FinallyInfo& fin = fs->finals[(std::size_t)c.finIndex];
                int id = 3 + (int)fin.exits.size();
                fin.exits.push_back(FinExit{id, target, isContinue});
                Emit(OP_Int, id);
                Emit(OP_PutLoc, fin.kindSlot);
                fin.entryJumps.push_back(EmitJump(OP_Jump));
                return;
            }
        }
        int j = EmitJump(OP_Jump);
        if (isContinue) fs->ctl[(std::size_t)target].continues.push_back(j);
        else fs->ctl[(std::size_t)target].breaks.push_back(j);
    }

    // return with the value on the stack
    void EmitReturn() {
        for (int i = (int)fs->ctl.size() - 1; i >= 0; --i) {
            Ctl& c = fs->ctl[(std::size_t)i];
            if (c.kind == CK_Try) Emit(OP_TryEnd);
            else if (c.kind == CK_Loop && c.iterSlot >= 0) {
                Emit(OP_GetLoc, c.iterSlot);
                Emit(OP_IterClose);
            } else if (c.kind == CK_Finally) {
                FinallyInfo& fin = fs->finals[(std::size_t)c.finIndex];
                fin.hasReturn = true;
                Emit(OP_PutLoc, fin.valueSlot);
                Emit(OP_Int, 2);
                Emit(OP_PutLoc, fin.kindSlot);
                fin.entryJumps.push_back(EmitJump(OP_Jump));
                return;
            }
        }
        Emit(OP_Return);
    }

    int FindBreakTarget(const std::u16string& label, Node* at) {
        for (int i = (int)fs->ctl.size() - 1; i >= 0; --i) {
            Ctl& c = fs->ctl[(std::size_t)i];
            if (label.empty()) {
                if (c.kind == CK_Loop || c.kind == CK_Switch) return i;
            } else if (std::find(c.labels.begin(), c.labels.end(), label) != c.labels.end()) return i;
        }
        Fail(at, "nothing to break out of here");
    }
    int FindContinueTarget(const std::u16string& label, Node* at) {
        for (int i = (int)fs->ctl.size() - 1; i >= 0; --i) {
            Ctl& c = fs->ctl[(std::size_t)i];
            if (c.kind != CK_Loop) continue;
            if (label.empty() || std::find(c.labels.begin(), c.labels.end(), label) != c.labels.end()) return i;
        }
        Fail(at, label.empty() ? "'continue' is only allowed inside a loop" : "'continue' needs the label of a loop");
    }

    Ctl& PushCtl(CtlKind k) {
        Ctl c;
        c.kind = k;
        if (k == CK_Loop || k == CK_Switch || k == CK_Label) {
            c.labels = std::move(fs->pendingLabels);
            fs->pendingLabels.clear();
        }
        fs->ctl.push_back(std::move(c));
        return fs->ctl.back();
    }
    void PopCtl(int continueTarget, int breakTarget) {
        Ctl c = std::move(fs->ctl.back());
        fs->ctl.pop_back();
        for (int j : c.continues) PatchTo(j, continueTarget);
        for (int j : c.breaks) PatchTo(j, breakTarget);
    }

    // ---------------------------------------------------------------- statements
    void Stmt(Node* n) {
        line = n->line;
        if (n->k != NK::Labeled && !fs->pendingLabels.empty() && n->k != NK::For && n->k != NK::ForIn &&
            n->k != NK::ForOf && n->k != NK::While && n->k != NK::DoWhile && n->k != NK::Switch) {
            // a label on a non-loop statement: break label leaves it
            PushCtl(CK_Label);
            StmtInner(n);
            PopCtl(Here(), Here());
            return;
        }
        StmtInner(n);
    }

    void StmtInner(Node* n) {
        switch (n->k) {
            case NK::Empty: case NK::Debugger: return;
            case NK::ExprStmt: ExprStatement(n->a); return;
            case NK::VarDecl: VarDeclStmt(n); return;
            case NK::FuncDecl: return;   // hoisted
            case NK::ClassDecl: {
                Expr(n->a);
                EmitStore(n->a->str, false, n, true);
                MarkInit(n->a->str);
                return;
            }
            case NK::ExportDefault: {
                if (n->b && n->b->k == NK::FuncDecl) {   // export default function name() {} : already declared
                    EmitLoad(n->b->a->fn->name, n);
                } else if (n->b && n->b->k == NK::Class) {
                    Expr(n->a);
                    if (fs->isScript) { Emit(OP_Dup); EmitStore(n->a->str, false, n, true); }
                    else { Emit(OP_Dup); EmitStore(n->a->str, false, n, true); }
                } else Expr(n->a);
                if (fs->exportSlot < 0) Fail(n, "'export default' is only allowed at the top level");
                Emit(OP_PutLoc, fs->exportSlot);
                return;
            }
            case NK::Return:
                if (n->a) Expr(n->a);
                else Emit(OP_Undef);
                if (fs->fi->async && !fs->fi->generator && n->a) { /* an async function's return value resolves its promise */ }
                EmitReturn();
                return;
            case NK::If: {
                CondJumpFalse(n->a);
                int jf = lastCondJump;
                Stmt(n->b);
                if (n->c) {
                    int je = EmitJump(OP_Jump);
                    Patch(jf);
                    Stmt(n->c);
                    Patch(je);
                } else Patch(jf);
                return;
            }
            case NK::Block: BlockStmt(n->list); return;
            case NK::While: {
                PushCtl(CK_Loop);
                int top = Here();
                CondJumpFalse(n->a);
                int jf = lastCondJump;
                Stmt(n->b);
                Emit(OP_Jump, top);
                Patch(jf);
                PopCtl(top, Here());
                return;
            }
            case NK::DoWhile: {
                PushCtl(CK_Loop);
                int top = Here();
                Stmt(n->b);
                int cont = Here();
                Expr(n->a);
                Emit(OP_JumpT, top);
                PopCtl(cont, Here());
                return;
            }
            case NK::For: ForStmt(n); return;
            case NK::ForIn: case NK::ForOf: ForInOf(n); return;
            case NK::Break: {
                int t = FindBreakTarget(n->str, n);
                EmitJumpOut(t, false);
                return;
            }
            case NK::Continue: {
                int t = FindContinueTarget(n->str, n);
                EmitJumpOut(t, true);
                return;
            }
            case NK::Throw:
                Expr(n->a);
                Emit(OP_Throw);
                return;
            case NK::Try: TryStmt(n); return;
            case NK::Switch: SwitchStmt(n); return;
            case NK::Labeled: {
                fs->pendingLabels.push_back(n->str);
                Stmt(n->a);
                fs->pendingLabels.clear();
                return;
            }
            default: Fail(n, "internal error: unknown statement");
        }
    }

    void MarkInit(const std::u16string& name) {
        for (auto it = fs->scopes.rbegin(); it != fs->scopes.rend(); ++it)
            if (Var* v = FindIn(it->get(), name)) {
                if (!(*it)->inSwitch) v->initKnown = true;
                return;
            }
    }

    void ExprStatement(Node* e) {
        line = e->line;
        // i++ / ++i / i-- on a plain local: one instruction
        if (e->k == NK::Update && e->a->k == NK::Ident) {
            Res r = Resolve(e->a->str);
            if (r.kind == 0 && !r.var->captured && r.var->initKnown && r.var->kind != VK_Const) {
                Emit(e->op == P_Inc ? OP_IncLoc : OP_DecLoc, r.index);
                return;
            }
        }
        Expr(e);
        Emit(OP_Pop);
    }

    void BlockStmt(const std::vector<Node*>& list) {
        bool needScope = false;
        for (Node* s : list)
            if ((s->k == NK::VarDecl && s->op != DK_Var) || s->k == NK::ClassDecl || s->k == NK::FuncDecl) needScope = true;
        if (needScope) {
            PushScope();
            DeclareLexicals(list, false);
            InitLexicalSlots();
            HoistFunctions(list);
        }
        for (Node* s : list) Stmt(s);
        if (needScope) PopScope();
    }

    void VarDeclStmt(Node* n) {
        for (Node* d : n->list) {
            line = d->line;
            if (!d->b) {
                if (n->op == DK_Var) continue;
                Emit(OP_Undef);
            } else {
                Expr(d->b);
                if (d->a->k == NK::Ident && (d->b->k == NK::Func || d->b->k == NK::Class)) { /* named at parse time */ }
            }
            BindPattern(d->a, n->op == DK_Var ? 2 : 1);
        }
    }

    // Pattern binding: the value is on the stack and is consumed.
    // mode 0 = assignment (references), 1 = initialize a let/const/param binding, 2 = var (assignment to a declared var)
    void BindPattern(Node* p, int mode) {
        line = p->line;
        switch (p->k) {
            case NK::Ident:
                if (mode == 1) {
                    if (fs->isScript && fs->scopes.size() == 1) EmitStore(p->str, false, p, true);
                    else {
                        Res r = Resolve(p->str);
                        if (r.kind == 0) Emit(r.var->captured ? OP_PutCell : OP_PutLoc, r.index);
                        else EmitStore(p->str, false, p, true);
                        MarkInit(p->str);
                    }
                } else if (mode == 2 && fs->isScript && fs->scopes.size() == 1) {
                    Emit(OP_SetGlobal, AtomConst(p->str));
                    Emit(OP_Pop);
                } else EmitStore(p->str, false, p);
                return;
            case NK::Member:
                Expr(p->a);
                Emit(OP_Swap);
                Emit(OP_SetProp, AtomConst(p->str));
                Emit(OP_Pop);
                return;
            case NK::Index: {
                int tmp = NewSlot();
                Emit(OP_PutLoc, tmp);
                Expr(p->a);
                Expr(p->b);
                Emit(OP_ToKey);
                Emit(OP_GetLoc, tmp);
                Emit(OP_SetElem);
                Emit(OP_Pop);
                return;
            }
            case NK::AssignPat: {
                int j = Emit(OP_JumpNotUndefKeep, -1);
                Expr(p->b);
                Patch(j);
                BindPattern(p->a, mode);
                return;
            }
            case NK::ArrayPat: {
                int it = NewSlot();
                Emit(OP_GetIter);
                Emit(OP_PutLoc, it);
                for (Node* e : p->list) {
                    if (e->k == NK::Hole) {
                        Emit(OP_GetLoc, it);
                        Emit(OP_IterStep);
                        continue;
                    }
                    if (e->k == NK::RestElem) {
                        Emit(OP_GetLoc, it);
                        Emit(OP_IterRest);
                        BindPattern(e->a, mode);
                        continue;
                    }
                    Emit(OP_GetLoc, it);
                    Emit(OP_IterValue);
                    BindPattern(e, mode);
                }
                Emit(OP_GetLoc, it);
                Emit(OP_IterClose);
                return;
            }
            case NK::ObjectPat: {
                Emit(OP_CheckCoercible);
                std::vector<int> keySlots;   // computed keys kept for ...rest
                std::vector<std::u16string> plainKeys;
                for (Node* e : p->list) {
                    if (e->k == NK::RestElem) {
                        Emit(OP_Dup);
                        int nk = 0;
                        for (auto& k : plainKeys) { EmitStr(k); ++nk; }
                        for (int s : keySlots) { Emit(OP_GetLoc, s); ++nk; }
                        Emit(OP_RestObject, nk);
                        BindPattern(e->a, mode);
                        continue;
                    }
                    Emit(OP_Dup);
                    if (e->flags & NF_Computed) {
                        Expr(e->a);
                        Emit(OP_ToKey);
                        int s = NewSlot();
                        Emit(OP_SetLoc, s);
                        keySlots.push_back(s);
                        Emit(OP_GetElem);
                    } else {
                        plainKeys.push_back(e->str);
                        Emit(OP_GetProp, AtomConst(e->str));
                    }
                    BindPattern(e->b, mode);
                }
                Emit(OP_Pop);
                return;
            }
            default: Fail(p, "this can not be assigned to");
        }
    }

    void ForStmt(Node* n) {
        PushScope();
        std::vector<int> perIter;
        if (n->a) {
            if (n->a->k == NK::VarDecl) {
                if (n->a->op != DK_Var) {
                    for (Node* d : n->a->list) {
                        std::vector<std::u16string> names;
                        PatternNames(d->a, names);
                        for (auto& nm : names) {
                            Var* v = Declare(nm, n->a->op == DK_Let ? VK_Let : VK_Const, d);
                            if (v->captured) {
                                Emit(OP_NewCell, v->slot, 1);
                                if (n->a->op == DK_Let) perIter.push_back(v->slot);
                            }
                        }
                    }
                }
                VarDeclStmt(n->a);
            } else ExprStatement(n->a->a);
        }
        for (int s : perIter) Emit(OP_CopyCell, s);
        PushCtl(CK_Loop);
        int top = Here();
        int jf = -1;
        if (n->b) {
            CondJumpFalse(n->b);
            jf = lastCondJump;
        }
        Stmt(n->d);
        int cont = Here();
        for (int s : perIter) Emit(OP_CopyCell, s);
        if (n->c) ExprStatement(n->c);
        Emit(OP_Jump, top);
        if (jf >= 0) Patch(jf);
        PopCtl(cont, Here());
        PopScope();
    }

    void ForInOf(Node* n) {
        bool of = n->k == NK::ForOf;
        int it = NewSlot();
        // the iterated value is evaluated with the loop's let names in their not-initialized state (14.7.5.6); we
        // evaluate it before the per-iteration scope
        const bool isAwait = (n->flags & NF_Await) != 0;
        Expr(n->b);
        Emit(isAwait ? OP_GetAsyncIter : of ? OP_GetIter : OP_ForInPrep);
        Emit(OP_PutLoc, it);
        Ctl& loop = PushCtl(CK_Loop);
        if (of) loop.iterSlot = it;
        int top = Here();
        Emit(OP_GetLoc, it);
        int jdone;
        if (isAwait) {
            // 14.7.5.7 ForIn/OfBodyEvaluation with iteratorKind async: Await(next()), then the value (a sync
            // iterator's value is awaited too, as CreateAsyncFromSyncIterator does)
            Emit(OP_AsyncIterNext);
            Emit(OP_Await);
            Patch(Emit(OP_ResumeCheck, -1));
            Emit(OP_GetLoc, it);
            jdone = Emit(OP_AsyncIterValue, -1);   // [value fromSync]
            int jskip = Emit(OP_JumpF, -1);
            Emit(OP_Await);
            Patch(Emit(OP_ResumeCheck, -1));
            Patch(jskip);
        } else jdone = Emit(of ? OP_IterNext : OP_ForInNext, -1);
        PushScope();
        Node* head = n->a;
        if (head->k == NK::VarDecl) {
            Node* d = head->list[0];
            if (head->op != DK_Var) {
                std::vector<std::u16string> names;
                PatternNames(d->a, names);
                for (auto& nm : names) {
                    Var* v = Declare(nm, head->op == DK_Let ? VK_Let : VK_Const, d);
                    if (v->captured) Emit(OP_NewCell, v->slot, 1);
                }
            }
            BindPattern(d->a, head->op == DK_Var ? 2 : 1);
        } else BindPattern(head, 0);
        Stmt(n->d);
        PopScope();
        Emit(OP_Jump, top);
        Patch(jdone);
        PopCtl(top, Here());
    }

    void TryStmt(Node* n) {
        bool hasCatch = (n->flags & NF_Computed) != 0;
        int finIdx = -1;
        if (n->d) {
            FinallyInfo fi;
            fi.kindSlot = NewSlot();
            fi.valueSlot = NewSlot();
            fs->finals.push_back(fi);
            finIdx = (int)fs->finals.size() - 1;
            Ctl& c = PushCtl(CK_Finally);
            c.finIndex = finIdx;
            Emit(OP_TryBegin, -1);
            PushCtl(CK_Try);
        }
        int finHandler = n->d ? Here() - 1 : -1;
        int catchHandler = -1;
        if (hasCatch) {
            catchHandler = Emit(OP_TryBegin, -1);
            PushCtl(CK_Try);
        }
        BlockStmt(n->a->list);
        if (hasCatch) {
            Emit(OP_TryEnd);
            fs->ctl.pop_back();
            int jover = EmitJump(OP_Jump);
            Patch(catchHandler);
            fs->depth = 1;   // the exception
            PushScope();
            if (n->b) {
                std::vector<std::u16string> names;
                PatternNames(n->b, names);
                for (auto& nm : names) {
                    Var* v = Declare(nm, VK_CatchParam, n->b);
                    if (v->captured) Emit(OP_NewCell, v->slot, 0);
                }
                BindPattern(n->b, 1);
            } else Emit(OP_Pop);
            BlockStmt(n->c->list);
            PopScope();
            Patch(jover);
        }
        if (n->d) {
            Emit(OP_TryEnd);
            fs->ctl.pop_back();   // CK_Try of the finally handler
            FinallyInfo* fin = &fs->finals[(std::size_t)finIdx];
            Emit(OP_Int, 0);
            Emit(OP_PutLoc, fin->kindSlot);
            int jfin = EmitJump(OP_Jump);
            Patch(finHandler);
            fs->depth = 1;
            Emit(OP_PutLoc, fin->valueSlot);
            Emit(OP_Int, 1);
            Emit(OP_PutLoc, fin->kindSlot);
            Patch(jfin);
            fs->ctl.pop_back();   // CK_Finally: the finally block runs outside the try
            fin = &fs->finals[(std::size_t)finIdx];
            for (int j : fin->entryJumps) Patch(j);
            int kindSlot = fin->kindSlot, valueSlot = fin->valueSlot;
            BlockStmt(n->d->list);
            // leave the way we came in
            Emit(OP_GetLoc, kindSlot);
            Emit(OP_Int, 1);
            Emit(OP_StrictEq);
            int j1 = EmitJump(OP_JumpF);
            Emit(OP_GetLoc, valueSlot);
            Emit(OP_Throw);
            Patch(j1);
            fin = &fs->finals[(std::size_t)finIdx];
            if (fin->hasReturn) {
                Emit(OP_GetLoc, kindSlot);
                Emit(OP_Int, 2);
                Emit(OP_StrictEq);
                int j2 = EmitJump(OP_JumpF);
                Emit(OP_GetLoc, valueSlot);
                EmitReturn();
                Patch(j2);
            }
            std::vector<FinExit> exits = fs->finals[(std::size_t)finIdx].exits;
            for (const FinExit& e : exits) {
                Emit(OP_GetLoc, kindSlot);
                Emit(OP_Int, e.id);
                Emit(OP_StrictEq);
                int j3 = EmitJump(OP_JumpF);
                EmitJumpOut(e.target, e.isContinue);
                Patch(j3);
            }
        }
    }

    void SwitchStmt(Node* n) {
        int disc = NewSlot();
        Expr(n->a);
        Emit(OP_PutLoc, disc);
        PushScope();
        CurScope()->inSwitch = true;
        std::vector<Node*> all;
        for (Node* c : n->list) for (Node* s : c->list) all.push_back(s);
        DeclareLexicals(all, false);
        InitLexicalSlots();
        HoistFunctions(all);
        PushCtl(CK_Switch);
        std::vector<int> jumps(n->list.size(), -1);
        int defaultIdx = -1;
        for (std::size_t i = 0; i < n->list.size(); ++i) {
            Node* c = n->list[i];
            if (!c->a) { defaultIdx = (int)i; continue; }
            line = c->line;
            Emit(OP_GetLoc, disc);
            Expr(c->a);
            Emit(OP_StrictEq);
            jumps[i] = EmitJump(OP_JumpT);
        }
        int jdefault = EmitJump(OP_Jump);
        for (std::size_t i = 0; i < n->list.size(); ++i) {
            if ((int)i == defaultIdx) Patch(jdefault);
            else Patch(jumps[i]);
            for (Node* s : n->list[i]->list) Stmt(s);
        }
        if (defaultIdx < 0) Patch(jdefault);
        PopCtl(Here(), Here());
        PopScope();
    }

    // ---------------------------------------------------------------- expressions
    int lastCondJump = -1;
    // Compile a condition and a jump taken when it is false (fused for < on numbers).
    void CondJumpFalse(Node* c) {
        line = c->line;
        if (c->k == NK::Binary && c->op == P_Lt) {
            Expr(c->a);
            Expr(c->b);
            lastCondJump = Emit(OP_LtJumpF, -1);
            return;
        }
        if (c->k == NK::Unary && c->op == P_Bang) {
            Expr(c->a);
            lastCondJump = Emit(OP_JumpT, -1);
            return;
        }
        Expr(c);
        lastCondJump = Emit(OP_JumpF, -1);
    }

    std::vector<int> chainJumps;   // jumps to the end of the innermost optional chain
    std::vector<std::vector<int>*> chainStack;

    void Expr(Node* n) {
        line = n->line;
        switch (n->k) {
            case NK::Num: EmitNum(n->num); return;
            case NK::Str: EmitStr(n->str); return;
            case NK::BigInt: {   // a constant BigInt (immutable, so one value serves every evaluation)
                Value b;
                if (!StringToBigInt(rt, n->str, b, false)) Fail(n, "bad BigInt literal");
                fs->p->consts.push_back(b);
                Emit(OP_Const, (int)fs->p->consts.size() - 1);
                return;
            }
            case NK::Regex:   // 13.2.7.3: a new RegExp object each time the literal is evaluated
                EmitStr(n->str);
                EmitStr(n->a->str);
                Emit(OP_NewRegExp);
                return;
            case NK::True: Emit(OP_True); return;
            case NK::False: Emit(OP_False); return;
            case NK::Null: Emit(OP_Null); return;
            case NK::This: EmitThis(); return;
            case NK::Ident: EmitLoad(n->str, n); return;
            case NK::NewTarget:
                if (fs->fi->arrow) EmitLoad(u"%newtarget", n);
                else Emit(OP_LoadNewTarget);
                return;
            case NK::Template: {
                bool first = true;
                for (std::size_t i = 0; i < n->list.size(); ++i) {
                    Node* part = n->list[i];
                    if (i % 2 == 0) {
                        if (part->str.empty() && !(first && n->list.size() == 1)) continue;
                        EmitStr(part->str);
                    } else {
                        Expr(part);
                        Emit(OP_ToStr);
                    }
                    if (!first) Emit(OP_Add);
                    first = false;
                }
                if (first) EmitStr(u"");
                return;
            }
            case NK::ImportCall:
                Expr(n->a);
                if (n->b) {
                    Expr(n->b);
                    Emit(OP_Pop);
                }
                Emit(OP_ImportCall);
                return;
            case NK::TaggedTemplate: {
                // [cache, cooked..., raw...] in a row of constants; OP_TemplateObj builds the object once (the
                // cache slot keeps it), so each evaluation of this site gets the same object (13.2.8.4)
                auto& c = fs->p->consts;
                int at = (int)c.size();
                c.push_back(Value());
                for (Node* q : n->list) c.push_back(q->k == NK::Hole ? Value() : Value::Str(rt.Intern(q->str)));
                for (Node* q : n->b->list) c.push_back(Value::Str(rt.Intern(q->str)));
                Emit(OP_TemplateObj, at, (int)n->list.size());
                return;
            }
            case NK::Array: ArrayLiteral(n); return;
            case NK::Object: ObjectLiteral(n); return;
            case NK::Func: EmitClosure(n); return;
            case NK::Class: ClassExpr(n); return;
            case NK::Unary: UnaryExpr(n); return;
            case NK::Update: UpdateExpr(n); return;
            case NK::Binary: {
                Expr(n->a);
                Expr(n->b);
                static const struct { int p; int op; } map[] = {
                    {P_Plus, OP_Add}, {P_Minus, OP_Sub}, {P_Star, OP_Mul}, {P_Slash, OP_Div}, {P_Percent, OP_Mod},
                    {P_Star2, OP_Pow}, {P_Amp, OP_BitAnd}, {P_Bar, OP_BitOr}, {P_Caret, OP_BitXor}, {P_Shl, OP_Shl},
                    {P_Sar, OP_Sar}, {P_Shr, OP_Shr}, {P_Eq2, OP_Eq}, {P_Ne, OP_Ne}, {P_Eq3, OP_StrictEq},
                    {P_Ne2, OP_StrictNe}, {P_Lt, OP_Lt}, {P_Le, OP_Le}, {P_Gt, OP_Gt}, {P_Ge, OP_Ge},
                    {1000, OP_In}, {1001, OP_InstanceOf},
                };
                for (auto& m : map)
                    if (m.p == n->op) { Emit(m.op); return; }
                Fail(n, "internal error: unknown operator");
            }
            case NK::Logical: {
                Expr(n->a);
                int j = EmitJump(n->op == P_And ? OP_JumpFKeep : n->op == P_Or ? OP_JumpTKeep : OP_JumpNotNullishKeep);
                Expr(n->b);
                Patch(j);
                return;
            }
            case NK::Cond: {
                CondJumpFalse(n->a);
                int jf = lastCondJump;
                int d0 = fs->depth;
                Expr(n->b);
                int je = EmitJump(OP_Jump);
                Patch(jf);
                fs->depth = d0;
                Expr(n->c);
                Patch(je);
                return;
            }
            case NK::Assign: AssignExpr(n); return;
            case NK::Seq:
                for (std::size_t i = 0; i < n->list.size(); ++i) {
                    Expr(n->list[i]);
                    if (i + 1 < n->list.size()) Emit(OP_Pop);
                }
                return;
            case NK::Member: {
                if (n->a->k == NK::Super) {
                    EmitThis();
                    EmitHome();
                    EmitStr(n->str);
                    Emit(OP_SuperGet);
                    return;
                }
                Expr(n->a);
                if (n->flags & NF_Optional) ChainJump(OP_JumpNullish);
                Emit(OP_GetProp, AtomConst(n->str));
                return;
            }
            case NK::Index: {
                if (n->a->k == NK::Super) {
                    EmitThis();
                    EmitHome();
                    Expr(n->b);
                    Emit(OP_ToKey);
                    Emit(OP_SuperGet);
                    return;
                }
                Expr(n->a);
                if (n->flags & NF_Optional) ChainJump(OP_JumpNullish);
                Expr(n->b);
                Emit(OP_GetElem);
                return;
            }
            case NK::OptChain: {
                std::vector<int> jumps;
                chainStack.push_back(&jumps);
                Expr(n->a);
                chainStack.pop_back();
                for (int j : jumps) Patch(j);
                return;
            }
            case NK::Call: CallExpr(n); return;
            case NK::New: {
                Expr(n->a);
                Emit(OP_PushEmpty);
                bool spread = std::any_of(n->list.begin(), n->list.end(), [](Node* x) { return x->k == NK::Spread; });
                if (spread) {
                    SpreadArgs(n->list);
                    Emit(OP_NewSpread);
                } else {
                    for (Node* a : n->list) Expr(a);
                    Emit(OP_New, (int)n->list.size(), CalleeName(n->a));
                }
                return;
            }
            case NK::Yield: YieldExpr(n); return;
            case NK::Await:
                Expr(n->a);
                Emit(OP_Await);
                {
                    int j = Emit(OP_ResumeCheck, -1);
                    Patch(j);
                }
                return;
            case NK::Spread: Fail(n, "'...' is not allowed here");
            case NK::Super: Fail(n, "'super' must be followed by '.', '[' or '('");
            default: Fail(n, "internal error: unknown expression");
        }
    }

    void ChainJump(int op) {
        if (chainStack.empty()) Fail(nullptr, "internal error: ?. outside a chain");
        chainStack.back()->push_back(Emit(op, -1));
    }

    void EmitThis() {
        if (fs->fi->arrow) {
            Res r = Resolve(u"%this");
            if (r.kind == 2) { Emit(OP_Undef); return; }   // top-level arrows: this is undefined
            EmitLoad(u"%this", nullptr);
            return;
        }
        if (fs->fi->derived) {
            Emit(OP_LoadThisChk);
            return;
        }
        Emit(OP_LoadThis);
    }
    void EmitHome() {
        if (fs->fi->arrow) EmitLoad(u"%home", nullptr);
        else Emit(OP_LoadHome);
    }

    void SpreadArgs(const std::vector<Node*>& args) {
        Emit(OP_NewArray, 0);
        for (Node* a : args) {
            if (a->k == NK::Spread) {
                Expr(a->a);
                Emit(OP_ArrSpread);
            } else {
                Expr(a);
                Emit(OP_ArrPush);
            }
        }
    }

    void CallExpr(Node* n) {
        Node* callee = n->a;
        bool spread = std::any_of(n->list.begin(), n->list.end(), [](Node* x) { return x->k == NK::Spread; });
        if (callee->k == NK::Super) {
            if (spread) {
                SpreadArgs(n->list);
                Emit(OP_SuperCallSpread);
            } else {
                for (Node* a : n->list) Expr(a);
                Emit(OP_SuperCall, (int)n->list.size());
            }
            AfterSuperCall();
            return;
        }
        if (callee->k == NK::Member && callee->a->k != NK::Super) {
            Expr(callee->a);
            if (callee->flags & NF_Optional) ChainJump(OP_JumpNullish);
            Emit(OP_GetPropKeep, AtomConst(callee->str));
        } else if (callee->k == NK::Index && callee->a->k != NK::Super) {
            Expr(callee->a);
            if (callee->flags & NF_Optional) ChainJump(OP_JumpNullish);
            Expr(callee->b);
            Emit(OP_GetElemKeep);
        } else if ((callee->k == NK::Member || callee->k == NK::Index) && callee->a->k == NK::Super) {
            Expr(callee);
            EmitThis();
        } else {
            Expr(callee);
            Emit(OP_Undef);
        }
        if (n->flags & NF_Optional) ChainJump(OP_JumpNullishCallee);
        if (spread) {
            SpreadArgs(n->list);
            Emit(OP_CallSpread);
        } else {
            for (Node* a : n->list) Expr(a);
            Emit(OP_Call, (int)n->list.size(), CalleeName(callee));
        }
    }

    // a constant index + 1 for the callee's text in "x is not a function" (0 = none)
    int CalleeName(Node* c) {
        std::u16string t;
        if (c->k == NK::Ident) t = c->str;
        else if (c->k == NK::Member) {
            if (c->a->k == NK::Ident) t = c->a->str + u"." + c->str;
            else if (c->a->k == NK::This) t = u"this." + c->str;
            else t = c->str;
        } else return 0;
        int k = AtomConst(t);
        return k < 65000 ? k + 1 : 0;
    }

    void UnaryExpr(Node* n) {
        if (n->op == 2000 && n->a->k == NK::Ident) {   // typeof name: no error for an undeclared name
            Res r = Resolve(n->a->str);
            if (r.kind == 2) {
                Emit(OP_TypeofGlobal, r.index);
                return;
            }
        }
        if (n->op == 2002) {   // delete
            Node* t = n->a;
            if (t->k == NK::OptChain && (t->a->k == NK::Member || t->a->k == NK::Index)) {
                // 13.5.1.2 delete a?.b: when the chain stops early (a is nullish) the result is true
                Node* m = t->a;
                std::vector<int> jumps;
                chainStack.push_back(&jumps);
                Expr(m->a);
                if (m->flags & NF_Optional) ChainJump(OP_JumpNullish);
                chainStack.pop_back();
                if (m->k == NK::Member) Emit(OP_DelProp, AtomConst(m->str));
                else {
                    Expr(m->b);
                    Emit(OP_DelElem);
                }
                int jend = EmitJump(OP_Jump);
                for (int j : jumps) Patch(j);
                Emit(OP_Pop);   // the undefined a short-circuit leaves
                Emit(OP_True);
                Patch(jend);
                return;
            }
            if (t->k == NK::OptChain) t = t->a;
            if (t->k == NK::Member && t->a->k != NK::Super) {
                Expr(t->a);
                Emit(OP_DelProp, AtomConst(t->str));
                return;
            }
            if (t->k == NK::Index && t->a->k != NK::Super) {
                Expr(t->a);
                Expr(t->b);
                Emit(OP_DelElem);
                return;
            }
            if (t->k == NK::Member || t->k == NK::Index) {
                Emit(OP_ThrowConst, AtomConst(u"'delete super.x' is not allowed"), (int)ErrorType::ReferenceError);
                Emit(OP_True);
                return;
            }
            Expr(n->a);
            Emit(OP_Pop);
            Emit(OP_True);
            return;
        }
        Expr(n->a);
        switch (n->op) {
            case P_Bang: Emit(OP_Not); return;
            case P_Tilde: Emit(OP_BitNot); return;
            case P_Plus: Emit(OP_Pos); return;
            case P_Minus: Emit(OP_Neg); return;
            case 2000: Emit(OP_Typeof); return;
            case 2001: Emit(OP_Void); return;
            default: Fail(n, "internal error: unknown unary operator");
        }
    }

    void UpdateExpr(Node* n) {
        bool prefix = (n->flags & NF_Prefix) != 0;
        int incOp = n->op == P_Inc ? OP_Inc : OP_Dec;
        Node* t = n->a;
        if (t->k == NK::Ident) {
            EmitLoad(t->str, t);
            Emit(OP_ToNum);
            if (!prefix) Emit(OP_Dup);
            Emit(incOp);
            EmitStore(t->str, true, t);
            if (!prefix) Emit(OP_Pop);
            return;
        }
        if (t->k == NK::Member) {
            if (t->a->k == NK::Super) Fail(t, "++ on super properties is not supported");
            Expr(t->a);
            Emit(OP_Dup);
            Emit(OP_GetProp, AtomConst(t->str));
            Emit(OP_ToNum);
            if (!prefix) Emit(OP_DupX1);
            Emit(incOp);
            Emit(OP_SetProp, AtomConst(t->str));
            if (!prefix) Emit(OP_Pop);
            return;
        }
        if (t->k == NK::Index) {
            if (t->a->k == NK::Super) Fail(t, "++ on super properties is not supported");
            Expr(t->a);
            Expr(t->b);
            Emit(OP_ToKey);
            Emit(OP_Dup2);
            Emit(OP_GetElem);
            Emit(OP_ToNum);
            if (!prefix) Emit(OP_DupX2);
            Emit(incOp);
            Emit(OP_SetElem);
            if (!prefix) Emit(OP_Pop);
            return;
        }
        Fail(n, "++ and -- need a variable or a property");
    }

    static int CompoundOp(int p) {
        switch (p) {
            case P_PlusEq: return OP_Add;
            case P_MinusEq: return OP_Sub;
            case P_StarEq: return OP_Mul;
            case P_SlashEq: return OP_Div;
            case P_PercentEq: return OP_Mod;
            case P_Star2Eq: return OP_Pow;
            case P_ShlEq: return OP_Shl;
            case P_SarEq: return OP_Sar;
            case P_ShrEq: return OP_Shr;
            case P_AmpEq: return OP_BitAnd;
            case P_BarEq: return OP_BitOr;
            case P_CaretEq: return OP_BitXor;
            default: return -1;
        }
    }

    void AssignExpr(Node* n) {
        Node* t = n->a;
        if (n->op == P_Assign) {
            if (t->k == NK::Ident) {
                Expr(n->b);
                EmitStore(t->str, true, t);
                return;
            }
            if (t->k == NK::Member) {
                if (t->a->k == NK::Super) {
                    EmitThis();
                    EmitHome();
                    EmitStr(t->str);
                    Expr(n->b);
                    Emit(OP_SuperSet);
                    return;
                }
                Expr(t->a);
                Expr(n->b);
                Emit(OP_SetProp, AtomConst(t->str));
                return;
            }
            if (t->k == NK::Index) {
                if (t->a->k == NK::Super) {
                    EmitThis();
                    EmitHome();
                    Expr(t->b);
                    Emit(OP_ToKey);
                    Expr(n->b);
                    Emit(OP_SuperSet);
                    return;
                }
                Expr(t->a);
                Expr(t->b);
                Emit(OP_ToKey);
                Expr(n->b);
                Emit(OP_SetElem);
                return;
            }
            // destructuring assignment: the value of the expression is the right side
            Expr(n->b);
            Emit(OP_Dup);
            BindPattern(t, 0);
            return;
        }
        bool logical = n->op == P_AndEq || n->op == P_OrEq || n->op == P_NullishEq;
        int jop = n->op == P_AndEq ? OP_JumpFKeep : n->op == P_OrEq ? OP_JumpTKeep : OP_JumpNotNullishKeep;
        if (t->k == NK::Ident) {
            EmitLoad(t->str, t);
            if (logical) {
                int j = EmitJump(jop);
                Expr(n->b);
                EmitStore(t->str, true, t);
                Patch(j);
                return;
            }
            Expr(n->b);
            Emit(CompoundOp(n->op));
            EmitStore(t->str, true, t);
            return;
        }
        if (t->a->k == NK::Super) Fail(t, "compound assignment to super properties is not supported");
        if (t->k == NK::Member) {
            Expr(t->a);
            Emit(OP_Dup);
            Emit(OP_GetProp, AtomConst(t->str));
            if (logical) {
                // [o v]
                int j = EmitJump(jop);   // keep v when short-circuiting
                Emit(OP_Pop);            // (JumpXKeep popped v on fall-through already)
                fs->depth += 1;
                Expr(n->b);
                Emit(OP_SetProp, AtomConst(t->str));
                int je = EmitJump(OP_Jump);
                Patch(j);
                Emit(OP_Swap);
                Emit(OP_Pop);
                Patch(je);
                return;
            }
            Expr(n->b);
            Emit(CompoundOp(n->op));
            Emit(OP_SetProp, AtomConst(t->str));
            return;
        }
        // Index
        Expr(t->a);
        Expr(t->b);
        Emit(OP_ToKey);
        Emit(OP_Dup2);
        Emit(OP_GetElem);
        if (logical) {
            int j = EmitJump(jop);
            Emit(OP_Pop);
            fs->depth += 1;
            Expr(n->b);
            Emit(OP_SetElem);
            int je = EmitJump(OP_Jump);
            Patch(j);
            // [o k v] -> [v]
            Emit(OP_DupX2);
            Emit(OP_Pop);
            Emit(OP_Pop);
            Emit(OP_Pop);
            Patch(je);
            return;
        }
        Expr(n->b);
        Emit(CompoundOp(n->op));
        Emit(OP_SetElem);
    }

    void ArrayLiteral(Node* n) {
        bool simple = std::none_of(n->list.begin(), n->list.end(), [](Node* x) { return x->k == NK::Spread || x->k == NK::Hole; });
        if (simple && n->list.size() < 4096) {
            for (Node* e : n->list) Expr(e);
            Emit(OP_NewArray, (int)n->list.size());
            return;
        }
        Emit(OP_NewArray, 0);
        for (Node* e : n->list) {
            if (e->k == NK::Hole) Emit(OP_ArrHole);
            else if (e->k == NK::Spread) {
                Expr(e->a);
                Emit(OP_ArrSpread);
            } else {
                Expr(e);
                Emit(OP_ArrPush);
            }
        }
    }

    void PushKey(Node* p) {
        if (p->flags & NF_Computed) {
            Expr(p->a);
            Emit(OP_ToKey);
        } else EmitStr(p->str);
    }

    void ObjectLiteral(Node* n) {
        Emit(OP_NewObject);
        bool sawProto = false;
        for (Node* p : n->list) {
            line = p->line;
            switch (p->k) {
                case NK::PropSpread:
                    Expr(p->a);
                    Emit(OP_CopyProps);
                    break;
                case NK::PropGet: case NK::PropSet:
                    PushKey(p);
                    EmitClosure(p->b);
                    Emit(p->k == NK::PropGet ? OP_DefGetter : OP_DefSetter, 0, 1);
                    break;
                case NK::PropInit:
                    if (p->flags & NF_CoverInit) Fail(p, "'{ name = value }' is only allowed when destructuring");
                    if (p->flags & NF_Method) {
                        PushKey(p);
                        EmitClosure(p->b);
                        Emit(OP_DefMethod, 0, 1);
                    } else if (p->flags & NF_Computed) {
                        PushKey(p);
                        Expr(p->b);
                        if (p->b->k == NK::Func || p->b->k == NK::Class) Emit(OP_SetName);
                        Emit(OP_DefField);
                    } else if (p->str == u"__proto__" && !(p->flags & NF_Shorthand)) {
                        // B.3.1: `__proto__: value` sets the new object's prototype when the value is an object
                        // or null (anything else is ignored); naming it twice is an early SyntaxError.
                        if (sawProto) Fail(p, "'__proto__' can be set only once in an object literal");
                        sawProto = true;
                        Expr(p->b);
                        Emit(OP_SetProtoLit);
                    } else {
                        Expr(p->b);
                        Emit(OP_DefFieldK, AtomConst(p->str));
                    }
                    break;
                default: Fail(p, "internal error: unknown property");
            }
        }
    }

    // 11.2.1 directive prologue: the leading expression statements that are bare string literals
    static bool HasUseStrict(const Node* body) {
        if (!body) return false;
        for (const Node* s : body->list) {
            if (s->k != NK::ExprStmt || !s->a || s->a->k != NK::Str || (s->a->flags & NF_Paren)) return false;
            if (s->a->str == u"use strict") return true;
        }
        return false;
    }
    int classDepth = 0;   ///< inside a class body (class code is strict, 11.2.2)

    void ClassExpr(Node* n) {
        ++classDepth;
        struct Leave { int& d; ~Leave() { --d; } } leave{classDepth};
        ClassInfo* ci = n->ci;
        bool named = !n->str.empty();
        if (named) {
            PushScope();
            Var* v = Declare(n->str, VK_Const, n);
            if (v->captured) Emit(OP_NewCell, v->slot, 1);
            else { Emit(OP_PushEmpty); Emit(OP_PutLoc, v->slot); }
        }
        if (ci->heritage) Expr(ci->heritage);
        else Emit(OP_PushEmpty);
        Proto* ctor = CompileFunction(ci->ctor, false);
        fs->p->protos.push_back(ctor);
        Emit(OP_Class, (int)fs->p->protos.size() - 1, ci->heritage ? 1 : 0);   // [heritage] -> [ctor proto]
        for (Node* m : n->list) {
            line = m->line;
            bool isStatic = (m->flags & NF_Static) != 0;
            if (isStatic) { Emit(OP_Dup2); Emit(OP_Pop); }
            else Emit(OP_Dup);
            PushKey(m);
            EmitClosure(m->b);
            Emit(m->op == 1 ? OP_DefGetter : m->op == 2 ? OP_DefSetter : OP_DefMethod, 0, 0);
            Emit(OP_Pop);
        }
        if (ci->instFields) {
            Proto* f = CompileFunction(ci->instFields, false);
            fs->p->protos.push_back(f);
            Emit(OP_Closure, (int)fs->p->protos.size() - 1);
            Emit(OP_SetFieldInit);
        }
        Emit(OP_Pop);   // the prototype
        if (named) {
            Emit(OP_Dup);
            Res r = Resolve(n->str);
            Emit(r.var->captured ? OP_PutCell : OP_PutLoc, r.index);
            r.var->initKnown = true;
        }
        if (ci->staticFields) {
            Emit(OP_Dup);
            Proto* f = CompileFunction(ci->staticFields, false);
            fs->p->protos.push_back(f);
            Emit(OP_Closure, (int)fs->p->protos.size() - 1);
            Emit(OP_Swap);
            Emit(OP_SetHome);   // [fn ctor]: super in static fields and blocks looks up from the class (15.7.14 step 31)
            Emit(OP_Call, 0);
            Emit(OP_Pop);
        }
        if (named) PopScope();
    }

    void YieldExpr(Node* n) {
        if (n->flags & NF_Delegate) {
            int it = NewSlot(), recv = NewSlot();
            Expr(n->a);
            Emit(OP_GetIter);
            Emit(OP_PutLoc, it);
            Emit(OP_Undef);
            Emit(OP_PutLoc, recv);
            int top = Here();
            Emit(OP_GetLoc, it);
            Emit(OP_GetLoc, recv);
            Emit(OP_IterSend);   // [result object]
            Emit(OP_Dup);
            Emit(OP_GetProp, AtomConst("done"));
            int jdone = EmitJump(OP_JumpT);
            Emit(OP_GetProp, AtomConst("value"));
            Emit(OP_Yield);
            int jnext = Emit(OP_ResumeCheck, -1);
            // return(v) on the outer generator: close the inner one, then return
            Emit(OP_GetLoc, it);
            Emit(OP_IterClose);
            EmitReturn();
            Patch(jnext);
            Emit(OP_PutLoc, recv);
            Emit(OP_Jump, top);
            Patch(jdone);
            fs->depth = fs->depth < 1 ? 1 : fs->depth;
            Emit(OP_GetProp, AtomConst("value"));
            return;
        }
        if (n->a) Expr(n->a);
        else Emit(OP_Undef);
        Emit(OP_Yield);
        int j = Emit(OP_ResumeCheck, -1);
        EmitReturn();
        Patch(j);
        fs->depth += 1;
    }

    std::vector<Proto*> protos;

    Proto* Script(Ast& ast) {
        FS f;
        f.fi = ast.script;
        f.isScript = true;
        f.strict = !rt.sloppyScripts || HasUseStrict(ast.script->body);
        Proto* p = new Proto();
        protos.push_back(p);
        f.p = p;
        p->chunk = chunk;
        p->line = 1;
        p->flags = PF_Script;
        p->name = rt.Intern("script");
        fs = &f;
        PushScope(true);
        f.exportSlot = NewSlot();
        Node* body = ast.script->body;
        // top-level var and function names are global object properties; let / const / class are global lexicals
        std::vector<std::pair<std::u16string, Node*>> vars;
        CollectVarNames(body, vars);
        for (auto& v : vars) Emit(OP_DeclVar, AtomConst(v.first));
        for (Node* s : body->list) {
            Node* d = s;
            if (d->k == NK::ExportDefault) d = d->b ? d->b : nullptr;
            if (!d) continue;
            if (d->k == NK::VarDecl && d->op != DK_Var)
                for (Node* dc : d->list) {
                    std::vector<std::u16string> names;
                    PatternNames(dc->a, names);
                    for (auto& nm : names) Emit(OP_DeclLex, AtomConst(nm), d->op == DK_Const ? 1 : 0);
                }
            else if (d->k == NK::ClassDecl) Emit(OP_DeclLex, AtomConst(d->a->str), 0);
            else if (d->k == NK::Class) Emit(OP_DeclLex, AtomConst(d->str), 0);
        }
        HoistFunctions(body->list);
        for (Node* s : body->list) Stmt(s);
        Emit(OP_GetLoc, f.exportSlot);
        Emit(OP_Return);
        PopScope();
        p->nslots = (std::uint32_t)f.nslots;
        p->maxStack = (std::uint32_t)f.maxDepth + 8;
        fs = nullptr;
        return p;
    }
};

} // namespace

Proto* CompileScript(Runtime& rt, Ast& ast, const std::string& chunk, SyntaxError& err) {
    Compiler c(rt, err, chunk);
    Proto* p = nullptr;
    try {
        p = c.Script(ast);
    } catch (const CompileFail&) {
        for (Proto* x : c.protos) delete x;
        return nullptr;
    } catch (...) {   // a memory stop while interning names: free what was made, let Runtime::Compile report it
        for (Proto* x : c.protos) delete x;
        throw;
    }
    for (Proto* x : c.protos) rt.Adopt(x);
    return p;
}

} // namespace okay::js
