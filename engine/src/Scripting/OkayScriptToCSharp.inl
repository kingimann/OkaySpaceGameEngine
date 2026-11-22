// The OkayScript -> C# converter (docs/scripting/migration_plan.md, stage 3).
//
// Included inside OkayScriptVM.cpp's private namespace, because it walks the old VM's own
// syntax tree (the node types above) rather than the text. Our own design: two passes
// over the tree, the first only working out what kind of value each variable, parameter
// and function holds (a number, text, true/false, a Vector3), the second writing the C#.
// The old language has one number type (a float), so every number becomes a float; text
// and true/false stay as they are. A built-in with a C# binding of the very same meaning
// becomes that call (Mathf.Sin, Debug.Log for text); every other built-in is called by
// name through OkayScript.Call / Number / Text / Bool, which runs the old built-in on the
// same object, so the converted script does what the old one did. Those calls are the
// converter's to-do list (OkayScriptConversion::bridged).

enum class CK { Unknown, Float, Bool, String, Vec3, List, Void, Object };

const char* CKName(CK k) {
    switch (k) {
        case CK::Float: return "float"; case CK::Bool: return "bool"; case CK::String: return "string";
        case CK::Vec3: return "Vector3"; case CK::List: return "List<float>"; case CK::Void: return "void";
        case CK::Object: return "GameObject";
        default: return "float";
    }
}

class CSharpConverter {
public:
    CSharpConverter(std::unordered_map<std::string, FunctionDecl>& funcs, std::vector<StmtPtr>& top, std::string source)
        : funcs_(funcs), top_(top), source_(std::move(source)) {}

    std::string Run(const std::string& className, std::vector<std::string>& notes, std::vector<std::string>& bridged) {
        bare_ = funcs_.empty() && !top_.empty();
        // Pass 1 works out the kinds (twice, so a use before a declaration still counts);
        // pass 2 writes the file with them.
        for (int pass = 0; pass < 3; ++pass) {
            writing_ = pass == 2;
            out_.clear();
            notes_.clear();
            bridged_.clear();
            Emit(className);
        }
        notes = notes_;
        for (const auto& b : bridged_) bridged.push_back(b);
        return out_;
    }

private:
    std::unordered_map<std::string, FunctionDecl>& funcs_;
    std::vector<StmtPtr>& top_;
    std::string source_;
    bool bare_ = false;
    bool writing_ = false;
    std::string out_;
    std::vector<std::string> notes_;
    std::set<std::string> bridged_;
    std::map<std::string, CK> globals_;             ///< script-wide variables (fields)
    std::vector<std::string> globalOrder_;
    std::map<std::string, CK> funcRet_;             ///< what each function returns
    std::map<std::string, std::vector<CK>> params_; ///< each function's parameter kinds
    std::vector<std::map<std::string, CK>> locals_; ///< the function being written: its scopes
    std::string fn_;                                ///< its name
    std::string dtName_;                            ///< update(dt): the parameter that is Time.deltaTime
    std::map<std::string, std::string> objType_;    ///< variables holding an engine object: its type (GameObject, Rigidbody)
    std::string TypeText(CK k, const std::string& var) const {
        if (k == CK::Object) { auto it = objType_.find(var); return it != objType_.end() && !it->second.empty() ? it->second : "GameObject"; }
        return k == CK::Unknown ? "float" : CKName(k);
    }
    static bool IsCollisionHandler(const std::string& n) {
        for (const char* m : {"OnCollisionEnter", "OnCollisionStay", "OnCollisionExit", "OnTriggerEnter", "OnTriggerStay", "OnTriggerExit"})
            if (n == m || n == std::string(m) + "2D") return true;
        return false;
    }
    int indent_ = 0;

    void Note(const std::string& n) {
        if (std::find(notes_.begin(), notes_.end(), n) == notes_.end()) notes_.push_back(n);
    }
    static CK Join(CK a, CK b) { return a == CK::Unknown ? b : a; }

    // ---- variables ----
    CK* FindVar(const std::string& n) {
        for (auto it = locals_.rbegin(); it != locals_.rend(); ++it) {
            auto f = it->find(n);
            if (f != it->end()) return &f->second;
        }
        auto g = globals_.find(n);
        return g == globals_.end() ? nullptr : &g->second;
    }
    void Global(const std::string& n, CK k) {
        auto it = globals_.find(n);
        if (it == globals_.end()) { globals_[n] = k; globalOrder_.push_back(n); }
        else it->second = Join(it->second, k);
    }
    static CK FromKind(unsigned char k) {
        switch (k & 0x0F) { case KInt: case KFloat: return CK::Float; case KBool: return CK::Bool; case KString: return CK::String; default: return CK::Unknown; }
    }

    // ---- names ----
    static bool IsLifecycle(const std::string& n) { return n == "start" || n == "update"; }
    static std::string MethodName(const std::string& n) {
        if (n == "start") return "Start";
        if (n == "update") return "Update";
        return n;
    }
    // C# words that can't be names: such a variable gets an @ in front, as C# allows.
    static std::string Safe(const std::string& n) {
        static const std::set<std::string> kw = {"base", "object", "event", "params", "ref", "out", "in", "is", "as", "lock",
                                                 "operator", "checked", "fixed", "internal", "decimal", "double", "long", "char",
                                                 "byte", "delegate", "implicit", "explicit", "namespace", "using", "struct",
                                                 "interface", "sealed", "default", "goto", "typeof", "sizeof", "virtual"};
        return kw.count(n) ? n + "_" : n;
    }

    // ---- built-ins with a C# binding of the same meaning ----
    // name -> (C# call, result kind). Only exact equivalents: anything else is bridged.
    struct Mapped { const char* cs; CK ret; std::size_t args; };
    static const Mapped* MapBuiltin(const std::string& n, std::size_t argc) {
        static const std::map<std::string, Mapped> m = {
            {"sin", {"Mathf.Sin", CK::Float, 1}}, {"cos", {"Mathf.Cos", CK::Float, 1}}, {"abs", {"Mathf.Abs", CK::Float, 1}},
            {"floor", {"Mathf.Floor", CK::Float, 1}}, {"min", {"Mathf.Min", CK::Float, 2}}, {"max", {"Mathf.Max", CK::Float, 2}},
            {"time", {"Time.time", CK::Float, 0}},
        };
        auto it = m.find(n);
        return it != m.end() && it->second.args == argc ? &it->second : nullptr;
    }
    // What a bridged built-in gives, when where it's used doesn't say.
    static CK BuiltinResult(const std::string& n) {
        static const std::set<std::string> bools = {"key", "key_down", "key_up", "action", "action_down", "action_up", "action_held",
            "action_double_tap", "action_hold_done", "action_buffered", "action_consume", "action_hold", "gamepad_connected",
            "mouse_down", "mouse_pressed", "gesture", "remembered", "hidden", "exists"};
        static const std::set<std::string> texts = {"to_fixed", "pad_button_name", "action_label", "input_button_name",
            "editor_selection_name", "editor_object_name", "substr", "replace", "swipe_direction", "input_device", "pad_info",
            "txt", "str", "upper", "lower", "trim", "name", "action_conflicts", "rebind", "pad_setup_prompt", "pad_raw", "pad_touch",
            "pad_gyro", "slider_value_text", "okaysave", "save", "pad_event"};
        if (bools.count(n) || n.rfind("is_", 0) == 0 || n.rfind("has_", 0) == 0) return CK::Bool;
        if (texts.count(n)) return CK::String;
        return CK::Float;
    }
    static bool IsApiRoot(const std::string& n) {
        static const std::set<std::string> roots = {"Mathf", "Time", "Vector3", "Debug", "Input", "Random", "ForceMode", "Quaternion"};
        return roots.count(n) > 0;
    }

    // ---- output ----
    void Line(const std::string& s) { if (writing_) out_ += (s.empty() ? std::string() : std::string((std::size_t)indent_ * 4, ' ') + s) + "\n"; }
    static std::string Quote(const std::string& s) {
        std::string o = "\"";
        for (char c : s) {
            switch (c) {
                case '"': o += "\\\""; break; case '\\': o += "\\\\"; break; case '\n': o += "\\n"; break;
                case '\t': o += "\\t"; break; case '\r': o += "\\r"; break; default: o += c;
            }
        }
        return o + "\"";
    }
    static std::string Num(double v) {
        if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e9) return std::to_string((long long)v);
        char b[64];
        std::snprintf(b, sizeof(b), "%.9g", v);
        std::string s = b;
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos) s += ".0";
        return s + "f";
    }

    // ---- expressions ----
    struct E { std::string s; CK k = CK::Unknown; bool intLit = false; std::string obj; };
    // `e` joined into text: true / false as the old language writes them (C# writes True / False).
    static std::string TextOf(const E& e) {
        if (e.k == CK::Bool) return "(" + e.s + " ? \"true\" : \"false\")";
        return e.s;
    }

    // `x` as a condition: a number is true when it isn't 0, text when it isn't empty.
    E Cond(const Expr* x) {
        E e = Ex(x, CK::Bool);
        if (e.k == CK::Bool) return e;
        if (e.k == CK::String) return {"(" + e.s + ") != \"\"", CK::Bool};
        if (e.k == CK::Vec3) return {"true", CK::Bool};
        return {"(" + e.s + ") != 0", CK::Bool};
    }
    // `x` where `want` is needed: numbers and text turn into what's wanted.
    E As(const Expr* x, CK want) {
        if (want == CK::Bool) return Cond(x);
        E e = Ex(x, want);
        if (want == CK::String && e.k != CK::String && e.k != CK::Unknown) return {"\"\" + " + (e.k == CK::Bool ? TextOf(e) : Paren(e)), CK::String};
        if (want == CK::Float && e.k == CK::Bool) return {"(" + e.s + " ? 1 : 0)", CK::Float};
        return e;
    }
    static std::string Paren(const E& e) {
        for (char c : e.s) if (c == ' ' || c == '?') return "(" + e.s + ")";
        return e.s;
    }
    std::string Args(const std::vector<ExprPtr>& args, const std::vector<CK>* kinds = nullptr) {
        std::string s;
        for (std::size_t i = 0; i < args.size(); ++i) {
            CK want = kinds && i < kinds->size() ? (*kinds)[i] : CK::Unknown;
            s += (i ? ", " : "") + (want == CK::Unknown ? Ex(args[i].get(), CK::Unknown).s : As(args[i].get(), want).s);
        }
        return s;
    }
    E Bridge(const std::string& name, const std::vector<ExprPtr>& args, CK want) {
        bridged_.insert(name);
        if (args.size() > 8) Note(name + "() is called with more than 8 values: OkayScript.Call takes up to 8, so the rest are left out");
        CK k = want == CK::Unknown ? BuiltinResult(name) : want;
        const char* fn = k == CK::Void ? "Call" : k == CK::Bool ? "Bool" : k == CK::String ? "Text" : k == CK::Object ? "Object" : "Number";
        std::string s = std::string("OkayScript.") + fn + "(" + Quote(name);
        for (std::size_t i = 0; i < args.size() && i < 8; ++i) s += ", " + Ex(args[i].get(), CK::Unknown).s;
        return {s + ")", k == CK::Void ? CK::Void : k};
    }

    // A dotted name of the old language (transform.position.x, Time.deltaTime, this.hp).
    E Dotted(std::string n) {
        if (n.rfind("this.", 0) == 0) n = n.substr(5);
        const auto dot = n.find('.');
        if (dot == std::string::npos) {
            if (n == "null") return {"null", CK::Object, false, "null"};
            if (!dtName_.empty() && n == dtName_) return {"Time.deltaTime", CK::Float};
            if (CK* k = FindVar(n)) { E e{Safe(n), *k}; if (*k == CK::Object) e.obj = objType_[n]; return e; }
            if (n == "gameObject" || n == "transform") return {n, CK::Unknown};
            Global(n, CK::Unknown);   // read before anything set it: an old global that starts as 0
            Note("'" + n + "' is read before the script sets it: it is a field starting at 0");
            return {Safe(n), CK::Float};
        }
        const std::string root = n.substr(0, dot), last = n.substr(n.rfind('.') + 1);
        CK k = CK::Unknown;
        if (last == "x" || last == "y" || last == "z" || last == "deltaTime" || last == "time" || last == "magnitude") k = CK::Float;
        else if (last == "position" || last == "localPosition" || last == "localScale" || last == "eulerAngles" || last == "forward" ||
                 last == "up" || last == "right") k = CK::Vec3;
        else if (last == "name" || last == "tag") k = CK::String;
        if (last == "gameObject") k = CK::Object;
        if (root != "transform" && root != "gameObject" && !IsApiRoot(root) && !FindVar(root))
            Note("'" + n + "' was copied as it is: check it reads the same in C#");
        return {n, k};
    }

    E Ex(const Expr* x, CK want) {
        if (!x) return {"0", CK::Float};
        if (auto* n = dynamic_cast<const NumberExpr*>(x)) {
            E e{Num(n->v), CK::Float};
            e.intLit = e.s.find('.') == std::string::npos && e.s.find('e') == std::string::npos;
            return e;
        }
        if (auto* s = dynamic_cast<const StringExpr*>(x)) return {Quote(s->v), CK::String};
        if (auto* b = dynamic_cast<const BoolExpr*>(x)) return {b->v ? "true" : "false", CK::Bool};
        if (auto* is = dynamic_cast<const InterpStringExpr*>(x)) {
            std::string s = "$\"";
            for (const auto& seg : is->segs) {
                if (!seg.isExpr) {
                    std::string q = Quote(seg.lit);
                    q = q.substr(1, q.size() - 2);
                    for (char c : q) s += c == '{' ? std::string("{{") : c == '}' ? std::string("}}") : std::string(1, c);
                } else {
                    E part = Ex(seg.expr.get(), CK::Unknown);
                    s += "{" + (part.k == CK::Bool ? TextOf(part) : part.s) + "}";
                }
            }
            return {s + "\"", CK::String};
        }
        if (auto* v = dynamic_cast<const VarExpr*>(x)) return Dotted(v->n);
        if (auto* a = dynamic_cast<const AssignExpr*>(x)) return Assign(a->n, a->value.get());
        if (auto* u = dynamic_cast<const UnaryExpr*>(x)) {
            if (u->op == Tok::Not) return {"!" + Paren(Cond(u->e.get())), CK::Bool};
            E e = Ex(u->e.get(), CK::Float);
            return {"-" + Paren(e), e.k == CK::Vec3 ? CK::Vec3 : CK::Float};
        }
        if (auto* l = dynamic_cast<const LogicalExpr*>(x))
            return {Cond(l->l.get()).s + (l->op == Tok::And ? " && " : " || ") + Cond(l->r.get()).s, CK::Bool};
        if (auto* c = dynamic_cast<const CoalesceExpr*>(x)) return Ex(c->l.get(), want);   // numbers / text are never null here
        if (auto* b = dynamic_cast<const BinaryExpr*>(x)) return Binary(*b);
        if (auto* m = dynamic_cast<const MemberExpr*>(x)) {
            E o = Ex(m->obj.get(), CK::Unknown);
            CK k = (m->name == "x" || m->name == "y" || m->name == "z" || m->name == "magnitude" || m->name == "Length" || m->name == "Count") ? CK::Float
                 : m->name == "normalized" ? CK::Vec3 : CK::Unknown;
            if (m->name == "length" && o.k == CK::String) return {Paren(o) + ".Length", CK::Float};
            return {Paren(o) + "." + m->name, k};
        }
        if (auto* m = dynamic_cast<const MemberAssignExpr*>(x)) {
            E o = Ex(m->obj.get(), CK::Unknown);
            return {Paren(o) + "." + m->name + " = " + Ex(m->value.get(), CK::Unknown).s, CK::Unknown};
        }
        if (auto* c = dynamic_cast<const CastExpr*>(x)) {
            if (c->type == "string") return As(c->e.get(), CK::String);
            if (c->type == "bool") return Cond(c->e.get());
            if (c->type == "int" || c->type == "long") return {"(int)" + Paren(As(c->e.get(), CK::Float)), CK::Float};
            return As(c->e.get(), CK::Float);
        }
        if (auto* m = dynamic_cast<const MethodCallExpr*>(x)) {
            E o = Ex(m->obj.get(), CK::Unknown);
            if (m->name == "ToString" && m->args.empty()) return {"\"\" + " + Paren(o), CK::String};
            if (o.k == CK::Object) {   // an engine object's own method (other.CompareTag("Coin"))
                CK k = m->name == "CompareTag" ? CK::Bool : CK::Unknown;
                return {Paren(o) + "." + m->name + "(" + Args(m->args) + ")", k};
            }
            Note("." + m->name + "(...) was copied as it is: check it exists in C#");
            return {Paren(o) + "." + m->name + "(" + Args(m->args) + ")", CK::Unknown};
        }
        if (auto* n = dynamic_cast<const NewExpr*>(x)) {
            if (n->type == "Vector3") return {"new Vector3(" + Args(n->args, &kFloats) + ")", CK::Vec3};
            Note("new " + n->type + "(...) was copied as it is");
            return {"new " + n->type + "(" + Args(n->args) + ")", CK::Unknown};
        }
        if (auto* c = dynamic_cast<const CallExpr*>(x)) return Call(*c, want);
        if (auto* t = dynamic_cast<const TernaryExpr*>(x)) {
            E a = Ex(t->a.get(), want), b = Ex(t->b.get(), want);
            CK k = a.k == b.k ? a.k : (a.k == CK::String || b.k == CK::String) ? CK::String : Join(a.k, b.k);
            if (k == CK::String) { a = As(t->a.get(), CK::String); b = As(t->b.get(), CK::String); }
            return {Cond(t->cond.get()).s + " ? " + a.s + " : " + b.s, k};
        }
        if (auto* a = dynamic_cast<const ArrayExpr*>(x)) {
            Note("A list [...] became a List<float>: check its items are numbers");
            return {"new List<float> { " + Args(a->elems, nullptr) + " }", CK::List};
        }
        if (auto* i = dynamic_cast<const IndexExpr*>(x)) {
            E a = Ex(i->arr.get(), CK::Unknown);
            return {Paren(a) + "[(int)" + Paren(As(i->index.get(), CK::Float)) + "]", CK::Float};
        }
        if (auto* i = dynamic_cast<const IndexAssignExpr*>(x)) {
            E a = Ex(i->arr.get(), CK::Unknown);
            return {Paren(a) + "[(int)" + Paren(As(i->index.get(), CK::Float)) + "] = " + As(i->value.get(), CK::Float).s, CK::Float};
        }
        if (auto* i = dynamic_cast<const IndexCompoundAssignExpr*>(x)) {
            E a = Ex(i->arr.get(), CK::Unknown);
            return {Paren(a) + "[(int)" + Paren(As(i->index.get(), CK::Float)) + "] " + OpText(i->op) + "= " + As(i->value.get(), CK::Float).s, CK::Float};
        }
        if (auto* ev = dynamic_cast<const EventOrCompoundExpr*>(x)) {
            if (auto* v = dynamic_cast<const VarExpr*>(ev->rhs.get()); v && funcs_.count(v->n) && !FindVar(v->n)) {
                Note("'" + ev->name + (ev->remove ? " -= " : " += ") + v->n + "' subscribes a function to an event: not converted (write a C# event)");
                return {"/* " + ev->name + (ev->remove ? " -= " : " += ") + v->n + " */", CK::Void};
            }
            return Compound(ev->name, ev->bin, ev->rhs.get());
        }
        if (dynamic_cast<const LambdaExpr*>(x)) {
            Note("A lambda (function value) isn't converted yet: rewrite it as a method");
            return {"null /* lambda */", CK::Unknown};
        }
        if (auto* b = dynamic_cast<const BorrowedExpr*>(x)) return Ex(b->e, want);
        Note("A part of an expression couldn't be converted");
        return {"0 /* ? */", CK::Float};
    }
    static const std::vector<CK> kFloats;

    static const char* OpText(Tok op) {
        switch (op) {
            case Tok::Plus: return "+"; case Tok::Minus: return "-"; case Tok::Star: return "*"; case Tok::Slash: return "/";
            case Tok::Percent: return "%"; case Tok::Eq: return "=="; case Tok::Ne: return "!="; case Tok::Lt: return "<";
            case Tok::Gt: return ">"; case Tok::Le: return "<="; case Tok::Ge: return ">="; default: return "?";
        }
    }
    E Binary(const BinaryExpr& b) {
        E l = Ex(b.l.get(), CK::Unknown), r = Ex(b.r.get(), CK::Unknown);
        const std::string op = OpText(b.op);
        if (b.op == Tok::Plus && (l.k == CK::String || r.k == CK::String)) {
            // "a" + 1 + 2 stays text from the left, as in C#; a number first joins as a number in both languages
            // an old built-in whose result is joined into text gives text (get("name") may hold either)
            if (l.k == CK::String && r.s.rfind("OkayScript.Number(", 0) == 0) r = Ex(b.r.get(), CK::String);
            if (r.k == CK::String && l.s.rfind("OkayScript.Number(", 0) == 0) l = Ex(b.l.get(), CK::String);
            if (l.k == CK::Bool) l.s = TextOf(l);
            if (r.k == CK::Bool) r.s = TextOf(r);
            return {l.s + " + " + ParenIfLow(r), CK::String};
        }
        if (b.op == Tok::Eq || b.op == Tok::Ne || b.op == Tok::Lt || b.op == Tok::Gt || b.op == Tok::Le || b.op == Tok::Ge) {
            if ((b.op == Tok::Eq || b.op == Tok::Ne) && l.k != r.k && l.k != CK::Unknown && r.k != CK::Unknown) {
                // the old language compares text with a number as text
                if (l.k == CK::String) r = As(b.r.get(), CK::String);
                else if (r.k == CK::String) l = As(b.l.get(), CK::String);
                else if (l.k == CK::Bool) r = Cond(b.r.get());
                else if (r.k == CK::Bool) l = Cond(b.l.get());
            }
            return {ParenIfLow(l) + " " + op + " " + ParenIfLow(r), CK::Bool};
        }
        if (l.k == CK::Bool) l = As(b.l.get(), CK::Float);
        if (r.k == CK::Bool) r = As(b.r.get(), CK::Float);
        if ((b.op == Tok::Slash || b.op == Tok::Percent) && l.intLit && r.intLit) l.s += "f";   // 1 / 2 is 0.5 in the old language
        CK k = (l.k == CK::Vec3 || r.k == CK::Vec3) ? CK::Vec3 : CK::Float;
        E e{ParenIfLow(l) + " " + op + " " + ParenIfLow(r), k};
        return e;
    }
    static std::string ParenIfLow(const E& e) {
        // wrap anything with an operator in it (a ternary, a comparison, a sum) so it reads the same
        int depth = 0;
        bool inStr = false;
        for (std::size_t i = 0; i < e.s.size(); ++i) {
            char c = e.s[i];
            if (c == '"' && (i == 0 || e.s[i - 1] != '\\')) inStr = !inStr;
            if (inStr) continue;
            if (c == '(' || c == '[') ++depth;
            else if (c == ')' || c == ']') --depth;
            else if (depth == 0 && c == ' ') return "(" + e.s + ")";
        }
        return e.s;
    }
    // name = value (a new name inside a function is a new global, as in the old language)
    E Assign(const std::string& rawName, const Expr* value) {
        std::string n = rawName.rfind("this.", 0) == 0 ? rawName.substr(5) : rawName;
        // x = x + v  ->  x += v / x++
        if (auto* b = dynamic_cast<const BinaryExpr*>(value)) {
            auto* lv = dynamic_cast<const VarExpr*>(b->l.get());
            if (lv && lv->n == rawName && (b->op == Tok::Plus || b->op == Tok::Minus || b->op == Tok::Star || b->op == Tok::Slash))
                return Compound(rawName, b->op, b->r.get());
        }
        if (n.find('.') != std::string::npos) return DottedAssign(n, value);
        CK* k = FindVar(n);
        if (!k) { Global(n, Ex(value, CK::Unknown).k); k = FindVar(n); }
        E v = As(value, *k);
        if (*k == CK::Unknown) *k = v.k;
        return {Safe(n) + " = " + v.s, *k};
    }
    // transform.position.x = v: a Vector3 property can't be changed a part at a time in C#
    E DottedAssign(const std::string& n, const Expr* value) {
        const std::string last = n.substr(n.rfind('.') + 1), base = n.substr(0, n.rfind('.'));
        if ((last == "x" || last == "y" || last == "z") && base.find('.') != std::string::npos) {
            E v = As(value, CK::Float);
            std::string parts[3] = {base + ".x", base + ".y", base + ".z"};
            parts[last[0] - 'x'] = v.s;
            return {base + " = new Vector3(" + parts[0] + ", " + parts[1] + ", " + parts[2] + ")", CK::Vec3};
        }
        E d = Dotted(n);
        return {d.s + " = " + As(value, d.k).s, d.k};
    }
    E Compound(const std::string& rawName, Tok op, const Expr* rhs) {
        std::string n = rawName.rfind("this.", 0) == 0 ? rawName.substr(5) : rawName;
        if (n.find('.') != std::string::npos) {
            const std::string last = n.substr(n.rfind('.') + 1), base = n.substr(0, n.rfind('.'));
            if ((last == "x" || last == "y" || last == "z") && base.find('.') != std::string::npos) {
                E v = As(rhs, CK::Float);
                std::string parts[3] = {base + ".x", base + ".y", base + ".z"};
                parts[last[0] - 'x'] = n + " " + OpText(op) + " " + ParenIfLow(v);
                return {base + " = new Vector3(" + parts[0] + ", " + parts[1] + ", " + parts[2] + ")", CK::Vec3};
            }
            E d = Dotted(n);
            return {d.s + " " + OpText(op) + "= " + As(rhs, d.k == CK::Vec3 ? CK::Unknown : d.k).s, d.k};
        }
        CK* k = FindVar(n);
        if (!k) { Global(n, Ex(rhs, CK::Unknown).k == CK::String ? CK::String : CK::Float); k = FindVar(n); }
        if (*k == CK::Unknown) *k = CK::Float;
        auto* num = dynamic_cast<const NumberExpr*>(rhs);
        if (num && num->v == 1.0 && (op == Tok::Plus || op == Tok::Minus) && *k == CK::Float) return {Safe(n) + (op == Tok::Plus ? "++" : "--"), CK::Float};
        E v = As(rhs, *k == CK::String && op == Tok::Plus ? CK::Unknown : (*k == CK::Vec3 ? CK::Unknown : CK::Float));
        return {Safe(n) + " " + OpText(op) + "= " + v.s, *k};
    }
    E Call(const CallExpr& c, CK want) {
        const std::string& n = c.name;
        if (auto f = funcs_.find(n); f != funcs_.end() && n.find('.') == std::string::npos) {   // the script's own function
            std::vector<CK>& ps = params_[n];
            if (ps.size() < c.args.size()) ps.resize(c.args.size(), CK::Unknown);
            for (std::size_t i = 0; i < c.args.size(); ++i)
                if (ps[i] == CK::Unknown && !writing_) ps[i] = Ex(c.args[i].get(), CK::Unknown).k;
            std::vector<CK> kinds = ps;
            for (auto& k : kinds) if (k == CK::Unknown) k = CK::Float;
            CK r = funcRet_.count(n) ? funcRet_[n] : CK::Unknown;
            return {MethodName(n) + "(" + Args(c.args, &kinds) + ")", r == CK::Unknown ? CK::Float : r};
        }
        if (n == "GetComponent" && c.args.size() == 1) {   // GetComponent<Rigidbody>() parsed as GetComponent("Rigidbody")
            if (auto* t = dynamic_cast<const StringExpr*>(c.args[0].get()); t && okay::csharp::IsEngineComponent(t->v))
                return {"GetComponent<" + t->v + ">()", CK::Object, false, t->v};
        }
        if ((n == "Vector3" || n == "new Vector3") && (c.args.size() == 3 || c.args.size() == 0))
            return {"new Vector3(" + Args(c.args, &kFloats) + ")", CK::Vec3};
        if (n == "Destroy" && c.args.size() == 1) {
            E a = Ex(c.args[0].get(), CK::Unknown);
            if (a.k == CK::Object && (a.obj.empty() || a.obj == "GameObject")) return {"Destroy(" + a.s + ")", CK::Void};
        }
        if (auto dot = n.find('.'); dot != std::string::npos) {   // rb.AddTorque(...) on an object variable
            const std::string root = n.substr(0, dot);
            if (CK* k = FindVar(root); k && *k == CK::Object) {
                const std::string m = n.substr(n.rfind('.') + 1);
                return {Safe(root) + n.substr(dot) + "(" + Args(c.args) + ")", m == "CompareTag" ? CK::Bool : CK::Unknown};
            }
        }
        if (n == "print" && c.args.size() == 1) {
            E a = Ex(c.args[0].get(), CK::Unknown);
            if (a.k == CK::String) return {"Debug.Log(" + a.s + ")", CK::Void};   // text prints the same in both
        }
        if (const Mapped* m = MapBuiltin(n, c.args.size())) {
            if (m->args == 0) return {m->cs, m->ret};
            return {std::string(m->cs) + "(" + Args(c.args, &kFloats) + ")", m->ret};
        }
        if (n == "GameObject.Find" || n == "GameObject.FindIncludingInactive" || n == "GameObject.FindWithTag") {
            if (n == "GameObject.FindIncludingInactive") return {n + "(" + Args(c.args) + ")", CK::Object, false, "GameObject"};
            E b = Bridge(n, c.args, CK::Object);   // the old Find skips switched-off objects and reads "Parent/Child"
            b.obj = "GameObject";
            return b;
        }
        if (auto dot = n.find('.'); dot != std::string::npos && IsApiRoot(n.substr(0, dot)) && n != "Debug.Log") {
            Note(n + "(...) was copied as it is: check it means the same in C#");
            return {n + "(" + Args(c.args) + ")", want};
        }
        return Bridge(n, c.args, want);
    }

    // ---- statements ----
    void Body(const std::vector<StmtPtr>& body) {
        ++indent_;
        locals_.emplace_back();
        for (const auto& s : body) St(s.get());
        locals_.pop_back();
        --indent_;
    }
    void Block(const std::string& head, const std::vector<StmtPtr>& body) {
        Line(head);
        Line("{");
        Body(body);
        Line("}");
    }
    std::string Simple(const Stmt* s) {   // a for loop's start: a declaration or an expression, no ';'
        if (auto* v = dynamic_cast<const VarDeclStmt*>(s)) {
            CK k = FromKind(v->kind);
            E init = v->init ? (k == CK::Unknown ? Ex(v->init.get(), CK::Unknown) : As(v->init.get(), k)) : E{"0", CK::Float};
            k = Join(k, init.k);
            if (k == CK::Unknown) k = CK::Float;
            locals_.back()[v->n] = k;
            if (k == CK::Object && !init.obj.empty() && init.obj != "null") objType_[v->n] = init.obj;
            return TypeText(k, v->n) + " " + Safe(v->n) + " = " + init.s;
        }
        if (auto* e = dynamic_cast<const ExprStmt*>(s)) return Ex(e->e.get(), CK::Void).s;
        return "";
    }
    void St(const Stmt* s) {
        if (!s) return;
        if (auto* e = dynamic_cast<const ExprStmt*>(s)) {
            E x = Ex(e->e.get(), CK::Void);
            if (x.s.rfind("/*", 0) == 0) { Line(x.s); return; }
            // C# only allows calls, assignments and ++ / -- as statements
            const bool ok = dynamic_cast<const CallExpr*>(e->e.get()) || dynamic_cast<const AssignExpr*>(e->e.get()) ||
                            dynamic_cast<const MemberAssignExpr*>(e->e.get()) || dynamic_cast<const IndexAssignExpr*>(e->e.get()) ||
                            dynamic_cast<const IndexCompoundAssignExpr*>(e->e.get()) || dynamic_cast<const EventOrCompoundExpr*>(e->e.get()) ||
                            dynamic_cast<const MethodCallExpr*>(e->e.get());
            const auto* ce = dynamic_cast<const CallExpr*>(e->e.get());
            const bool callValue = ce && !funcs_.count(ce->name) && MapBuiltin(ce->name, ce->args.size()) != nullptr;   // e.g. sin(x); alone
            if (!ok || callValue) { Line("// " + x.s + ";  (a value nobody uses: the old script did nothing with it)"); return; }
            Line(x.s + ";");
            return;
        }
        if (auto* v = dynamic_cast<const VarDeclStmt*>(s)) { Line(Simple(v) + ";"); return; }
        if (auto* r = dynamic_cast<const ReturnStmt*>(s)) {
            if (!r->e) { Line("return;"); return; }
            CK want = funcRet_.count(fn_) ? funcRet_[fn_] : CK::Unknown;
            E x = want == CK::Unknown ? Ex(r->e.get(), CK::Unknown) : As(r->e.get(), want);
            if (!writing_) funcRet_[fn_] = Join(want, x.k == CK::Void ? CK::Unknown : x.k);
            Line("return " + x.s + ";");
            return;
        }
        if (dynamic_cast<const BreakStmt*>(s)) { Line("break;"); return; }
        if (dynamic_cast<const ContinueStmt*>(s)) { Line("continue;"); return; }
        if (auto* b = dynamic_cast<const BlockStmt*>(s)) { Line("{"); Body(b->body); Line("}"); return; }
        if (auto* m = dynamic_cast<const MultiStmt*>(s)) { for (const auto& x : m->list) St(x.get()); return; }
        if (auto* i = dynamic_cast<const IfStmt*>(s)) {
            Block("if (" + Cond(i->cond.get()).s + ")", i->thenB);
            if (!i->elseB.empty()) {
                if (i->elseB.size() == 1 && dynamic_cast<const IfStmt*>(i->elseB[0].get())) {
                    // else if: written as else { if ... } to keep this simple and exact
                }
                Block("else", i->elseB);
            }
            return;
        }
        if (auto* w = dynamic_cast<const WhileStmt*>(s)) { Block("while (" + Cond(w->cond.get()).s + ")", w->body); return; }
        if (auto* d = dynamic_cast<const DoWhileStmt*>(s)) {
            Line("do"); Line("{"); Body(d->body); Line("} while (" + Cond(d->cond.get()).s + ");");
            return;
        }
        if (auto* f = dynamic_cast<const ForStmt*>(s)) {
            locals_.emplace_back();
            std::string init = f->init ? Simple(f->init.get()) : "";
            std::string cond = f->cond ? Cond(f->cond.get()).s : "";
            std::string step = f->step ? Ex(f->step.get(), CK::Void).s : "";
            Block("for (" + init + "; " + cond + "; " + step + ")", f->body);
            locals_.pop_back();
            return;
        }
        if (auto* f = dynamic_cast<const ForEachStmt*>(s)) {
            E coll = Ex(f->iterable.get(), CK::Unknown);
            locals_.emplace_back();
            locals_.back()[f->var] = CK::Float;
            if (coll.k != CK::List) Note("foreach over '" + coll.s + "' needs a List or array in C#: check its type");
            Block("foreach (var " + Safe(f->var) + " in " + coll.s + ")", f->body);
            locals_.pop_back();
            return;
        }
        if (auto* sw = dynamic_cast<const SwitchStmt*>(s)) {
            E subj = Ex(sw->subject.get(), CK::Unknown);
            Line("switch (" + (subj.k == CK::Float ? "(int)" + Paren(subj) : subj.s) + ")");
            Line("{");
            ++indent_;
            for (const auto& c : sw->cases) {
                if (c.isDefault) Line("default:");
                else {
                    E m = Ex(c.match.get(), subj.k);
                    Line("case " + m.s + ":");
                }
                Line("{");
                Body(c.body);
                bool ends = !c.body.empty() && (dynamic_cast<const BreakStmt*>(c.body.back().get()) ||
                                                dynamic_cast<const ReturnStmt*>(c.body.back().get()));
                if (!ends) { ++indent_; Line("break;"); --indent_; }
                Line("}");
            }
            --indent_;
            Line("}");
            return;
        }
        if (auto* t = dynamic_cast<const TryStmt*>(s)) {
            Block("try", t->body);
            if (t->hasCatch) {
                Line(t->catchVar.empty() ? "catch" : "catch (Exception " + Safe(t->catchVar) + "_error)");
                Line("{");
                ++indent_;
                locals_.emplace_back();
                if (!t->catchVar.empty()) {
                    locals_.back()[t->catchVar] = CK::String;
                    Line("string " + Safe(t->catchVar) + " = " + Safe(t->catchVar) + "_error.Message;");
                }
                for (const auto& x : t->catchB) St(x.get());
                locals_.pop_back();
                --indent_;
                Line("}");
            } else Line("finally { }");
            return;
        }
        if (auto* t = dynamic_cast<const ThrowStmt*>(s)) { Line("throw new Exception(" + As(t->e.get(), CK::String).s + ");"); return; }
        if (dynamic_cast<const YieldStmt*>(s)) {
            Note("A coroutine (yield) isn't converted yet: rewrite it with IEnumerator and StartCoroutine");
            Line("// yield ... (not converted)");
            return;
        }
        if (dynamic_cast<const FunctionStmt*>(s)) { Note("A function inside a function isn't converted"); return; }
        Note("A statement couldn't be converted");
        Line("// (a statement that couldn't be converted)");
    }

    // ---- the file ----
    // Where a function is written in the source (to keep the old order).
    std::size_t SourcePos(const std::string& name) const {
        for (const char* pre : {"function ", "void ", "float ", "int ", "bool ", "string ", "var "}) {
            std::size_t at = 0;
            while ((at = source_.find(pre + name, at)) != std::string::npos) {
                std::size_t after = at + std::strlen(pre) + name.size();
                while (after < source_.size() && source_[after] == ' ') ++after;
                if (after < source_.size() && source_[after] == '(') return at;
                at = after;
            }
        }
        return std::string::npos;
    }

    void Emit(const std::string& className) {
        indent_ = 0;
        locals_.clear();
        dtName_.clear();
        // work out the method bodies first (into nothing) so the fields' kinds are known
        std::string body;
        {
            std::string saved;
            std::swap(saved, out_);
            indent_ = 1;
            if (bare_) {
                Line("// Runs every frame (the old script was just these lines).");
                fn_ = "update";
                locals_.emplace_back();
                Block("void Update()", top_);
                locals_.pop_back();
            } else {
                // top-level lines other than plain declarations run once, before Start
                std::vector<const Stmt*> once;
                for (const auto& s : top_) {
                    if (dynamic_cast<const FunctionStmt*>(s.get())) continue;
                    if (auto* v = dynamic_cast<const VarDeclStmt*>(s.get())) {
                        CK k = Join(FromKind(v->kind), v->init ? Ex(v->init.get(), CK::Unknown).k : CK::Unknown);
                        Global(v->n, k);
                        if (v->init && !IsFixed(v->init.get())) once.push_back(s.get());
                        continue;
                    }
                    once.push_back(s.get());
                }
                if (!once.empty() && !funcs_.count("Awake")) {
                    Line("// The old script's top-level lines: they run once, before Start.");
                    Line("void Awake()");
                    Line("{");
                    ++indent_;
                    fn_ = "";
                    locals_.emplace_back();
                    for (const Stmt* s : once) {
                        if (auto* v = dynamic_cast<const VarDeclStmt*>(s)) {
                            CK k = globals_[v->n];
                            Line(Safe(v->n) + " = " + (k == CK::Unknown ? Ex(v->init.get(), CK::Unknown) : As(v->init.get(), k)).s + ";");
                        } else St(s);
                    }
                    locals_.pop_back();
                    --indent_;
                    Line("}");
                    Line("");
                }
                std::vector<std::pair<std::size_t, std::string>> order;
                for (const auto& f : funcs_) order.push_back({SourcePos(f.first), f.first});
                std::sort(order.begin(), order.end());
                for (const auto& o : order) {
                    const std::string& name = o.second;
                    const FunctionDecl& d = funcs_.at(name);
                    fn_ = name;
                    dtName_.clear();
                    std::vector<CK>& ps = params_[name];
                    if (ps.size() < d.params.size()) ps.resize(d.params.size(), CK::Unknown);
                    locals_.emplace_back();
                    std::string head;
                    if (IsLifecycle(name)) {
                        head = "void " + MethodName(name) + "()";
                        if (name == "update" && !d.params.empty()) dtName_ = d.params[0];
                        if (d.params.size() > (name == "update" ? 1u : 0u))
                            Note(name + "() takes values the engine doesn't give: they start at 0");
                    } else {
                        CK r = d.returnKind ? FromKind(d.returnKind) : (funcRet_.count(name) ? funcRet_[name] : CK::Void);
                        if (d.returnKind) funcRet_[name] = r;
                        head = "public " + std::string(r == CK::Unknown ? "float" : CKName(r)) + " " + name + "(";
                        for (std::size_t i = 0; i < d.params.size(); ++i) {
                            CK k = i < d.paramKinds.size() && d.paramKinds[i] ? FromKind(d.paramKinds[i]) : ps[i];
                            if (IsCollisionHandler(name)) { k = CK::Object; objType_[d.params[i]] = "GameObject"; }   // the other object
                            if (k == CK::Unknown) k = CK::Float;
                            locals_.back()[d.params[i]] = k;
                            head += (i ? ", " : "") + TypeText(k, d.params[i]) + " " + Safe(d.params[i]);
                        }
                        head += ")";
                    }
                    Line(head);
                    Line("{");
                    Body(d.body);
                    if (!IsLifecycle(name) && funcRet_.count(name) && funcRet_[name] != CK::Void && funcRet_[name] != CK::Unknown) {
                        // the old language gave 0 when a function ran off its end
                        if (d.body.empty() || !dynamic_cast<const ReturnStmt*>(d.body.back().get())) {
                            ++indent_;
                            Line(std::string("return ") + (funcRet_[name] == CK::String ? "\"\"" : funcRet_[name] == CK::Bool ? "false" :
                                                           funcRet_[name] == CK::Vec3 ? "Vector3.zero" : "0") + ";");
                            --indent_;
                        }
                    }
                    Line("}");
                    Line("");
                    locals_.pop_back();
                }
            }
            std::swap(saved, out_);
            body = saved;
        }
        indent_ = 0;
        Line("using UnityEngine;");
        Line("using System.Collections.Generic;");
        Line("");
        Line("// Converted from OkayScript by the Script Editor (File > Convert to C#).");
        Line("public class " + className + " : MonoBehaviour");
        Line("{");
        ++indent_;
        for (const std::string& g : globalOrder_) {
            CK k = globals_[g];
            if (k == CK::Unknown) k = CK::Float;
            std::string init;
            if (!bare_)
                for (const auto& s : top_)
                    if (auto* v = dynamic_cast<const VarDeclStmt*>(s.get()); v && v->n == g && v->init && IsFixed(v->init.get()))
                        init = " = " + As(v->init.get(), k).s;
            if (init.empty() && k == CK::String) init = " = \"\"";
            if (init.empty() && k == CK::List) init = " = new List<float>()";
            Line("public " + TypeText(k, g) + " " + Safe(g) + init + ";");
        }
        if (!globalOrder_.empty()) Line("");
        --indent_;
        if (writing_) out_ += body;
        // drop the blank line before the closing brace
        while (writing_ && out_.size() >= 2 && out_[out_.size() - 1] == '\n' && out_[out_.size() - 2] == '\n') out_.pop_back();
        Line("}");
    }
    // A value C# accepts as a field's starting value: literals and arithmetic on them.
    static bool IsFixed(const Expr* x) {
        if (dynamic_cast<const NumberExpr*>(x) || dynamic_cast<const StringExpr*>(x) || dynamic_cast<const BoolExpr*>(x)) return true;
        if (auto* u = dynamic_cast<const UnaryExpr*>(x)) return IsFixed(u->e.get());
        if (auto* b = dynamic_cast<const BinaryExpr*>(x)) return IsFixed(b->l.get()) && IsFixed(b->r.get());
        return false;
    }
};
const std::vector<CK> CSharpConverter::kFloats = {CK::Float, CK::Float, CK::Float, CK::Float};
