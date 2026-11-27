// OkayUI's contexts: CreateContext, DestroyContext, GetCurrentContext, SetCurrentContext (OkayUICompat.hpp), over the variable lists
// of OkayUI_ContextState.hpp. Written by us for OkaySpace from Dear ImGui's documented behaviour of multiple contexts (its header:
// CreateContext keeps the current context when there is one, SetCurrentContext picks the one later calls act on, DestroyContext(null)
// destroys the current one); no code from any other project.
//
// One set of live variables exists. The context that owns them is "live"; every other context keeps its values in stored copies.
// SetCurrentContext(x) exchanges: the live values go into the live context's copies, x's copies come into the live variables.
// "Current" is what GetCurrentContext answers; it can be null (SetCurrentContext(null)) while some context still owns the live values.
// The program starts with one context, the default one, live and current, so code that never calls CreateContext works as before.
// The first CreateContext hands out that default context; later ones make new contexts that start from the start-up values.
#include "OkayUI_ContextState.hpp"
#include "okay/UI/OkayUICompat.hpp"
#include "okay/UI/OkayUIInternals.hpp"

#include <algorithm>
#include <vector>

namespace OkayUI {
namespace ctxstate {
namespace {
struct Registry {
    std::vector<VarOps> vars;
    std::vector<void*> startup;   // each variable's start-up value
};
Registry& Reg() { static Registry* r = new Registry(); return *r; }   // never destroyed: static destructors may still switch contexts

struct Rec {
    Context handle;
    std::vector<void*> stored;   // one per variable; while this context is live they hold stale values (kept for the next switch)
    bool claimed = false;        // the default context: handed out by CreateContext
};
Rec& DefaultRec() { static Rec* d = new Rec(); return *d; }
std::vector<Rec*>& All() { static std::vector<Rec*>* a = new std::vector<Rec*>{&DefaultRec()}; return *a; }
Rec* g_live = nullptr;      // null means the default context
Rec* g_current = nullptr;   // what GetCurrentContext answers (null: none)
bool g_currentSet = false;  // false: the default context is current (program start)

Rec* Live() { return g_live ? g_live : &DefaultRec(); }
Rec* Current() { return g_currentSet ? g_current : &DefaultRec(); }
void SetCurrent(Rec* r) { g_current = r; g_currentSet = true; }

void FillStored(Rec& r) {   // stored copies from the start-up values, for a context that has none yet
    Registry& reg = Reg();
    for (std::size_t i = r.stored.size(); i < reg.vars.size(); ++i) r.stored.push_back(reg.vars[i].fromStored(reg.startup[i]));
}
void ResetStored(Rec& r) {
    Registry& reg = Reg();
    for (std::size_t i = 0; i < r.stored.size(); ++i) reg.vars[i].destroy(r.stored[i]);
    r.stored.clear();
    FillStored(r);
}
void MakeLive(Rec* to) {
    Rec* from = Live();
    if (from == to) return;
    Registry& reg = Reg();
    FillStored(*from); FillStored(*to);
    for (std::size_t i = 0; i < reg.vars.size(); ++i) {
        reg.vars[i].exchange(reg.vars[i].global, from->stored[i]);   // the live values into `from`'s copies
        reg.vars[i].exchange(reg.vars[i].global, to->stored[i]);     // `to`'s copies into the live values
    }
    g_live = to;
}
Rec* Find(Context* c) {
    if (!c) return nullptr;
    for (Rec* r : All()) if (&r->handle == c) return r;
    return nullptr;
}
} // namespace

void Register(std::initializer_list<VarOps> vars) {
    Registry& reg = Reg();
    for (const VarOps& v : vars) { reg.vars.push_back(v); reg.startup.push_back(v.fromGlobal(v.global)); }
}
int RegisteredCount() { return (int)Reg().vars.size(); }
const char* RegisteredName(int i) { return i >= 0 && i < RegisteredCount() ? Reg().vars[(std::size_t)i].name : ""; }
Context* LiveHandle() { return &Live()->handle; }

} // namespace ctxstate

using namespace ctxstate;

Context* CreateContext() {
    Rec& d = DefaultRec();
    if (!d.claimed && (Current() == &d || Current() == nullptr)) {   // the first one: the context the program started with
        d.claimed = true;
        if (Live() != &d) MakeLive(&d);
        SetCurrent(&d);
        return RefreshContext();
    }
    Rec* r = new Rec();
    FillStored(*r);
    All().push_back(r);
    if (!Current()) { MakeLive(r); SetCurrent(r); RefreshContext(); }
    return &r->handle;
}

void DestroyContext(Context* ctx) {
    Rec* r = ctx ? Find(ctx) : Current();
    if (!r) return;
    Rec& d = DefaultRec();
    if (r == &d) {
        if (Live() == &d) ResetState(); else ResetStored(d);
        d.claimed = false;
        if (Current() == &d) SetCurrent(nullptr);
        return;
    }
    if (Live() == r) MakeLive(&d);   // its values move into its copies, which are freed below
    if (Current() == r) SetCurrent(nullptr);
    for (std::size_t i = 0; i < r->stored.size(); ++i) Reg().vars[i].destroy(r->stored[i]);
    std::vector<Rec*>& all = All();
    all.erase(std::remove(all.begin(), all.end(), r), all.end());
    delete r;
}

Context* GetCurrentContext() {
    Rec* c = Current();
    if (!c) return nullptr;
    if (Live() != c) MakeLive(c);
    return RefreshContext();
}

void SetCurrentContext(Context* ctx) {
    if (!ctx) { SetCurrent(nullptr); return; }
    Rec* r = Find(ctx);
    if (!r) return;   // a handle OkayUI did not make is ignored
    MakeLive(r);
    SetCurrent(r);
}

} // namespace OkayUI
