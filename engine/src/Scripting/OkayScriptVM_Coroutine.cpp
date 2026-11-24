#include "OkayScriptVMInternal.hpp"

#include <condition_variable>   // lint-allow: win32-thread
#include <mutex>   // lint-allow: win32-thread
#include <thread>   // lint-allow: win32-thread

namespace okay {
namespace script_vm {

struct Runtime::Coroutine {
        int id = 0;
        std::string fn; std::vector<Value> args;
        std::vector<std::string> argNames;   ///< parallel to args; empty = positional
        std::thread th;   // lint-allow: win32-thread
        std::mutex m; std::condition_variable cv;   // lint-allow: win32-thread
        bool coTurn = false, started = false, done = false, abort = false;
        float wait = 0.0f; bool nextFrame = false; int waitFor = 0;   // waitFor: a coroutine id
        Value cond; bool condWhile = false; bool real = false;   // WaitUntil/WaitWhile lambda; real = ignore time scale
        long long yieldFrame = -1;
        std::vector<Environment> frames;
        std::size_t baseDepth = 0;
    };

Runtime::Runtime() { InitEngineRuntime(*this); }
Runtime::~Runtime() = default;


Runtime::Coroutine* Runtime::FindCo(int id) { for (auto& c : coroutines) if (c->id == id) return c.get(); return nullptr; }
void Runtime::Resume(Coroutine* co) {
        if (co->done) return;
        Coroutine* prev = currentCo; currentCo = co;
        co->baseDepth = scopes.size();
        for (auto& f : co->frames) scopes.push_back(std::move(f));
        co->frames.clear();
        if (!co->started) {
            co->started = true;
            co->th = std::thread([this, co] {   // lint-allow: win32-thread
                { std::unique_lock<std::mutex> lk(co->m); co->cv.wait(lk, [co] { return co->coTurn; }); }   // lint-allow: win32-thread
                try { if (!co->abort) Call(co->fn, co->args, co->argNames.empty() ? nullptr : &co->argNames); }
                catch (const CoroutineAbort&) {}
                catch (const std::exception& e) { Log::Error("OkayScript coroutine ", co->fn, "(): ", e.what()); }
                catch (...) { Log::Error("OkayScript coroutine ", co->fn, "(): error"); }
                std::lock_guard<std::mutex> lk(co->m);   // lint-allow: win32-thread
                co->done = true; co->coTurn = false; co->cv.notify_all();
            });
        }
        {
            std::unique_lock<std::mutex> lk(co->m);   // lint-allow: win32-thread
            co->coTurn = true; co->cv.notify_all();
            co->cv.wait(lk, [co] { return !co->coTurn; });
        }
        if (co->done && scopes.size() > co->baseDepth) scopes.resize(co->baseDepth);   // unwound by an error
        currentCo = prev;
}
    // Called on the coroutine's own thread by `yield return`.
void Runtime::YieldCoroutine(float wait, bool nextFrame, int waitFor, Value cond, bool condWhile, bool real) {
        Coroutine* co = currentCo;
        if (!co) throw ScriptError("'yield' only works inside a coroutine started with StartCoroutine()");
        co->wait = wait; co->nextFrame = nextFrame; co->waitFor = waitFor; co->yieldFrame = frame;
        co->cond = std::move(cond); co->condWhile = condWhile; co->real = real;
        for (std::size_t i = co->baseDepth; i < scopes.size(); ++i) co->frames.push_back(std::move(scopes[i]));
        scopes.resize(co->baseDepth);
        std::unique_lock<std::mutex> lk(co->m);   // lint-allow: win32-thread
        co->coTurn = false; co->cv.notify_all();
        co->cv.wait(lk, [co] { return co->coTurn; });
        if (co->abort) throw CoroutineAbort{};
}
    // Names from the call that started a coroutine (StartCoroutine(Blink(delay: 0.2))).
    // Set only while that builtin runs, on the caller's thread.
int Runtime::StartCoroutine(const std::string& fn, std::vector<Value> args, std::vector<std::string> argNames) {
        if (!functions.count(fn)) throw ScriptError("StartCoroutine: no function named '" + fn + "'");
        auto co = std::make_unique<Coroutine>();
        co->id = nextCoId++; co->fn = fn; co->args = std::move(args); co->argNames = std::move(argNames);
        Coroutine* raw = co.get();
        coroutines.push_back(std::move(co));
        Resume(raw);   // runs until its first yield
        return raw->id;
}
void Runtime::StopCoroutine(Coroutine* co) {
        if (co->done) return;
        if (!co->started) { co->done = true; return; }
        co->abort = true;
        Resume(co);
}
void Runtime::StopCoroutines(const std::string& fn) {   // "" = all
        for (std::size_t i = 0; i < coroutines.size(); ++i)
            if (fn.empty() || coroutines[i]->fn == fn) StopCoroutine(coroutines[i].get());
        ReapCoroutines();
}
void Runtime::ReapCoroutines() {
        if (currentCo) return;   // never join from inside a coroutine
        for (auto it = coroutines.begin(); it != coroutines.end();) {
            if ((*it)->done) { if ((*it)->th.joinable()) (*it)->th.join(); it = coroutines.erase(it); }
            else ++it;
        }
}
void Runtime::TickCoroutines(float dt) {
        for (std::size_t i = 0; i < coroutines.size(); ++i) {   // index loop: the list can grow
            Coroutine* co = coroutines[i].get();
            if (co->done || co->yieldFrame >= frame) continue;   // yielded this frame: wait for the next
            if (co->waitFor) { Coroutine* w = FindCo(co->waitFor); if (w && !w->done) continue; co->waitFor = 0; Resume(co); continue; }
            if (co->nextFrame) { Resume(co); continue; }
            if (IsLambda(co->cond)) {   // WaitUntil(() => c) resumes when c is true; WaitWhile when it is false
                std::vector<Value> none;
                if (CallLambda(co->cond, none).AsBool() != co->condWhile) Resume(co);
                continue;
            }
            co->wait -= co->real ? Time::UnscaledDeltaTime() : dt;
            if (co->wait <= 0.0f) Resume(co);
        }
        ReapCoroutines();
}

} // namespace script_vm
} // namespace okay
