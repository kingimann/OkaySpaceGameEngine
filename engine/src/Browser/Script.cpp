// Browser (page engine): JavaScript in web pages (see Script.hpp). Written from the WHATWG DOM standard (2.1-2.10 events and
// their dispatch: capture, target and bubble phases, the canceled and stop-propagation flags; 4.2-4.9 nodes, the tree's
// pre-insert / remove steps and their hierarchy checks; 4.9 DOMTokenList for classList) and the HTML standard (8.6 timers,
// 8.1.8.1 event handler attributes and properties, 13.3 fragment serialization, 4.12.1.1 script types), on our own
// JavaScript engine's public API (okay/JavaScript/JavaScript.hpp) and its library helpers (JsInternal.hpp).
#include "okay/Browser/Script.hpp"

#include "okay/Browser/Css.hpp"
#include "okay/Browser/Forms.hpp"
#include "okay/Browser/Url.hpp"
#include "okay/Core/StringUtil.hpp"
#include "../JavaScript/JsInternal.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace okay::browser {

namespace js = okay::js;

namespace {

constexpr std::uint32_t kWindowSlot = 0xFFFFFFFFu;   // the listeners of window

using okay::str::LowerAscii;
using okay::str::UpperAscii;

bool IsSpaceChar(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r'; }

std::vector<std::string> Tokens(const std::string& s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && IsSpaceChar(s[i])) ++i;
        const std::size_t b = i;
        while (i < s.size() && !IsSpaceChar(s[i])) ++i;
        if (i > b) out.push_back(s.substr(b, i - b));
    }
    return out;
}

std::string Joined(const std::vector<std::string>& t) {
    std::string out;
    for (const std::string& s : t) { if (!out.empty()) out += ' '; out += s; }
    return out;
}

void SetAttr(Node& n, const std::string& name, const std::string& value) {
    for (auto& a : n.attrs) if (a.first == name) { a.second = value; return; }
    if (n.attrs.size() < 256) n.attrs.emplace_back(name, value);
}

bool RemoveAttr(Node& n, const std::string& name) {
    for (std::size_t i = 0; i < n.attrs.size(); ++i)
        if (n.attrs[i].first == name) { n.attrs.erase(n.attrs.begin() + (std::ptrdiff_t)i); return true; }
    return false;
}

std::string AttrOr(const Node& n, const char* name) {
    const std::string* v = n.Attr(name);
    return v ? *v : std::string();
}

// "backgroundColor" -> "background-color" (CSSOM 6.7.1's camel-cased attribute names).
std::string CssName(const std::string& camel) {
    std::string out;
    for (char c : camel) {
        if (c >= 'A' && c <= 'Z') { out += '-'; out += (char)(c + 32); }
        else out += c;
    }
    return out;
}

// Preorder walk below `root` (root itself when `self`), with an explicit stack; stops when f returns true.
template <class F>
void Walk(Node* root, bool self, F&& f) {
    std::vector<Node*> todo;
    if (self) todo.push_back(root);
    else for (std::size_t k = root->children.size(); k-- > 0;) todo.push_back(root->children[k].get());
    while (!todo.empty()) {
        Node* n = todo.back();
        todo.pop_back();
        if (f(n)) return;
        for (std::size_t k = n->children.size(); k-- > 0;) todo.push_back(n->children[k].get());
    }
}

std::size_t CountNodes(Node* root) {
    std::size_t c = 0;
    Walk(root, true, [&](Node*) { ++c; return false; });
    return c;
}

// What a node's text costs (its characters, name and attributes): the page's text is held to kMaxPageTextBytes.
std::size_t BytesOf(const Node& n) {
    std::size_t b = n.data.size() + n.name.size();
    for (const auto& a : n.attrs) b += a.first.size() + a.second.size();
    return b;
}

std::size_t TreeBytes(Node* root) {
    std::size_t b = 0;
    Walk(root, true, [&](Node* n) { b += BytesOf(*n); return false; });
    return b;
}

int DepthOf(const Node* n) {
    int d = 0;
    for (const Node* p = n->parent; p; p = p->parent) ++d;
    return d;
}

int HeightOf(Node* n, std::size_t& visited) {   // levels below n (0: no children)
    int h = 0;
    std::vector<std::pair<Node*, int>> todo{{n, 0}};
    while (!todo.empty()) {
        auto [x, d] = todo.back();
        todo.pop_back();
        h = std::max(h, d);
        ++visited;
        for (auto& c : x->children) todo.push_back({c.get(), d + 1});
    }
    return h;
}

} // namespace

bool IsJavaScriptType(const std::string& typeIn) {
    std::string t = LowerAscii(typeIn);
    const std::size_t semi = t.find(';');
    if (semi != std::string::npos) t.resize(semi);
    while (!t.empty() && IsSpaceChar(t.back())) t.pop_back();
    std::size_t b = 0;
    while (b < t.size() && IsSpaceChar(t[b])) ++b;
    t = t.substr(b);
    if (t.empty()) return true;
    static const char* const kJs[] = {"text/javascript", "application/javascript", "application/ecmascript", "application/x-ecmascript",
                                      "application/x-javascript", "text/ecmascript", "text/javascript1.0", "text/javascript1.1",
                                      "text/javascript1.2", "text/javascript1.3", "text/javascript1.4", "text/javascript1.5",
                                      "text/jscript", "text/livescript", "text/x-ecmascript", "text/x-javascript"};
    for (const char* k : kJs) if (t == k) return true;
    return false;
}

struct PageScripts::Impl {
    HtmlDocument& doc;
    FormState& forms;
    std::string url;
    Hooks hooks;
    std::uint64_t budget = kBudgetPerCall;
    int depth = 0;   // host entries in progress (nested: an event a script fired with click())
    bool loaded = false;

    // nodes the scripts have seen: slot -> node, its generation (a freed slot's old wrappers no longer match) and wrapper
    std::vector<Node*> nodes;
    std::vector<std::uint32_t> gens;
    std::vector<js::HostObj*> wraps;   // weak: cleared when the collector frees the wrapper
    std::vector<std::uint32_t> freeSlots;
    std::unordered_map<const Node*, std::uint32_t> slotOf;
    std::vector<std::unique_ptr<Node>> removed;   // roots of trees taken out of the page or made and not added
    std::size_t nodeCount = 0;
    std::size_t textBytes = 0;   // characters, names and attributes of every node (in the page or removed), and form values

    struct Listener { std::string type; int fn = -1; bool capture = false; bool once = false; };
    std::unordered_map<std::uint32_t, std::vector<Listener>> listeners;
    std::unordered_map<std::uint32_t, std::vector<std::pair<std::string, int>>> onProps;   // el.onclick = fn
    struct AttrHandler { std::string source; int fn = -1; };
    std::unordered_map<std::uint32_t, std::vector<std::pair<std::string, AttrHandler>>> attrHandlers;   // onclick="..."

    struct Dispatching { js::Object* ev = nullptr; bool stop = false, stopNow = false, prevented = false, cancelable = true; };
    std::vector<Dispatching> dispatching;

    struct Timer { int id = 0; double due = 0, interval = 0; bool repeat = false, frame = false; int fn = -1; std::vector<int> args; };
    std::vector<Timer> timers;
    int nextTimer = 1;
    double now = 0;

    std::vector<ConsoleLine> console;
    std::uint64_t consoleCount = 0;
    ConsoleLine::Level level = ConsoleLine::Log;

    std::uint32_t kNode = 0, kStyle = 0, kClassList = 0, kLocation = 0;
    js::Object *nodeProto = nullptr, *documentProto = nullptr, *styleProto = nullptr, *classListProto = nullptr,
               *eventProto = nullptr, *locationProto = nullptr;
    js::Symbol* ownerKey = nullptr;   // style / classList objects keep their element's wrapper alive through it
    int locationRef = -1;
    int inlineCount = 0;

    js::Runtime rt;   // last: destroyed first, while the tables its onHostFree reads still exist

    Impl(HtmlDocument& d, FormState& f, const std::string& u, Hooks h) : doc(d), forms(f), url(u), hooks(std::move(h)) {}
    ~Impl() { rt.onHostFree = nullptr; rt.printSink = nullptr; }

    // ---- console
    void Log(ConsoleLine::Level lv, const std::string& text) {
        if (console.size() >= kMaxConsoleLines) console.erase(console.begin(), console.begin() + (std::ptrdiff_t)(kMaxConsoleLines / 5));
        ConsoleLine l;
        l.level = lv;
        l.text = text.size() > 4096 ? text.substr(0, 4096) + "..." : text;
        console.push_back(std::move(l));
        ++consoleCount;
    }

    // ---- host entries: one budget each
    struct Entry {
        Impl& I;
        explicit Entry(Impl& i) : I(i) {
            if (I.depth++ == 0) { I.rt.SetBudget(I.budget); I.level = ConsoleLine::Log; }
        }
        ~Entry() { --I.depth; }
    };
    bool CallFn(const js::Value& fn, const js::Value& thisv, const std::vector<js::Value>& args, js::Value* out = nullptr) {
        std::string err;
        if (rt.CallHost(fn, thisv, args, out, &err)) return true;
        Log(ConsoleLine::Error, err);
        return false;
    }

    // ---- nodes and wrappers
    std::uint32_t SlotFor(Node* n) {
        auto it = slotOf.find(n);
        if (it != slotOf.end()) return it->second;
        std::uint32_t s;
        if (!freeSlots.empty()) { s = freeSlots.back(); freeSlots.pop_back(); nodes[s] = n; }
        else { s = (std::uint32_t)nodes.size(); nodes.push_back(n); gens.push_back(0); wraps.push_back(nullptr); }
        slotOf[n] = s;
        return s;
    }
    js::Value Wrap(Node* n) {
        if (!n) return js::Value::Null();
        const std::uint32_t s = SlotFor(n);
        if (wraps[s]) return js::Value::Obj(wraps[s]);
        js::HostObj* h = rt.NewHost(kNode, n->kind == NodeKind::Document ? documentProto : nodeProto);
        h->id = s;
        h->u32 = gens[s];
        wraps[s] = h;
        return js::Value::Obj(h);
    }
    Node* NodeOf(const js::HostObj* h) const {
        if (h->id >= nodes.size() || !nodes[(std::size_t)h->id] || gens[(std::size_t)h->id] != h->u32) return nullptr;
        return nodes[(std::size_t)h->id];
    }
    Node* NodeArg(const js::Value& v) const {
        if (!v.IsObj() || v.O()->cls != js::Cls::Host) return nullptr;
        const js::HostObj* h = static_cast<const js::HostObj*>(v.O());
        return h->hostClass == kNode ? NodeOf(h) : nullptr;
    }
    // A style or classList object of element slot `s` (its wrapper is kept alive by it).
    js::Value Part(std::uint32_t cls, js::Object* proto, Node* el) {
        const js::Value owner = Wrap(el);
        js::HostObj* h = rt.NewHost(cls, proto);
        h->id = slotOf[el];
        h->u32 = gens[h->id];
        rt.DefineData(h, ownerKey, owner, 0);
        return js::Value::Obj(h);
    }
    js::Value Array(const std::vector<Node*>& list) {
        rt.Charge(list.size());
        js::ArrayObj* a = rt.NewArray(list.size());
        rt.Keep(js::Value::Obj(a));
        for (std::size_t i = 0; i < list.size(); ++i) rt.DefineIndex(a, (std::uint32_t)i, Wrap(list[i]));
        return js::Value::Obj(a);
    }
    void Changed() { if (hooks.changed) hooks.changed(); }
    bool InPage(const Node* n) const {
        while (n->parent) n = n->parent;
        return n == doc.root.get();
    }
    void CheckRoom(js::Runtime& r, std::size_t more) {
        if (nodeCount + more > kMaxNodes) r.ThrowRange("the page has too many nodes (" + std::to_string(kMaxNodes) + ")");
    }
    // The page's text grows by `add` and loses `drop` bytes; more than kMaxPageTextBytes is refused before anything changes.
    void Account(js::Runtime& r, std::size_t add, std::size_t drop) {
        const std::size_t after = textBytes - std::min(drop, textBytes) + add;
        if (add > drop && after > kMaxPageTextBytes) r.ThrowRange("the page's text would be too big (" + std::to_string(kMaxPageTextBytes >> 20) + " MB)");
        textBytes = after;
        r.Charge(add / 64);
    }
    void SetAttrC(js::Runtime& r, Node& n, const std::string& name, const std::string& value) {
        const std::string* old = n.Attr(name.c_str());
        Account(r, name.size() + value.size(), old ? name.size() + old->size() : 0);
        SetAttr(n, name, value);
    }
    void SetData(js::Runtime& r, Node& n, const std::string& text) {
        Account(r, text.size(), n.data.size());
        n.data = text;
    }
    Node* NewNode(js::Runtime& r, NodeKind kind) {
        CheckRoom(r, 1);
        std::unique_ptr<Node> n(new Node());
        n->kind = kind;
        Node* p = n.get();
        removed.push_back(std::move(n));
        ++nodeCount;
        return p;
    }

    // Take `n` out of where it is (its parent, or the removed list) and hand it over.
    std::unique_ptr<Node> Take(Node* n) {
        if (Node* p = n->parent) {
            rt.Charge(p->children.size() / 8);
            for (std::size_t i = 0; i < p->children.size(); ++i)
                if (p->children[i].get() == n) {
                    std::unique_ptr<Node> out = std::move(p->children[i]);
                    p->children.erase(p->children.begin() + (std::ptrdiff_t)i);
                    out->parent = nullptr;
                    return out;
                }
            return nullptr;
        }
        for (std::size_t i = removed.size(); i-- > 0;)
            if (removed[i].get() == n) {
                std::unique_ptr<Node> out = std::move(removed[i]);
                removed[i] = std::move(removed.back());
                removed.pop_back();
                return out;
            }
        return nullptr;
    }
    // Remove `n` from its parent; it goes to the removed list (freed later, FreeRemoved).
    void Remove(Node* n) {
        if (!n->parent) return;
        std::unique_ptr<Node> t = Take(n);
        if (t) removed.push_back(std::move(t));
        Changed();
    }
    // The DOM standard's pre-insert validity checks (4.2.3) and insert: `child` goes into `parent` before `before` (null: last).
    void Insert(js::Runtime& r, Node* parent, Node* child, Node* before) {
        if (parent->kind != NodeKind::Element && parent->kind != NodeKind::Document)
            r.ThrowError(js::ErrorType::Error, "HierarchyRequestError: this node can not have children");
        if (child->kind == NodeKind::Document) r.ThrowError(js::ErrorType::Error, "HierarchyRequestError: a document can not be inserted");
        for (const Node* p = parent; p; p = p->parent)
            if (p == child) r.ThrowError(js::ErrorType::Error, "HierarchyRequestError: a node can not be put inside itself");
        if (before && before->parent != parent) r.ThrowError(js::ErrorType::Error, "NotFoundError: the reference node is not a child of this node");
        if (before == child) return;
        std::size_t visited = 0;
        const int height = HeightOf(child, visited);
        r.Charge(visited);
        if (DepthOf(parent) + 1 + height > HtmlLimits().maxDepth)
            r.ThrowRange("the tree would be too deep (" + std::to_string(HtmlLimits().maxDepth) + " levels)");
        std::unique_ptr<Node> t = Take(child);
        if (!t) r.ThrowError(js::ErrorType::Error, "NotFoundError: the node is not in this page");
        t->parent = parent;
        std::size_t at = parent->children.size();
        if (before) for (std::size_t i = 0; i < parent->children.size(); ++i) if (parent->children[i].get() == before) { at = i; break; }
        parent->children.insert(parent->children.begin() + (std::ptrdiff_t)at, std::move(t));
        Changed();
    }
    void RemoveChildren(Node* el) {
        rt.Charge(el->children.size());
        while (!el->children.empty()) {
            std::unique_ptr<Node> t = std::move(el->children.back());
            el->children.pop_back();
            t->parent = nullptr;
            removed.push_back(std::move(t));
        }
    }
    void SetText(js::Runtime& r, Node* el, const std::string& text) {
        if (el->kind == NodeKind::Text || el->kind == NodeKind::Comment) { SetData(r, *el, text); Changed(); return; }
        if (el->kind != NodeKind::Element) return;
        if (text.size() > HtmlLimits().maxTextBytes) r.ThrowRange("the text is too long");
        Account(r, text.size(), 0);
        RemoveChildren(el);
        if (!text.empty()) {
            Node* t = NewNode(r, NodeKind::Text);
            t->data = text;
            Insert(r, el, t, nullptr);
        }
        Changed();
    }
    void SetInnerHtml(js::Runtime& r, Node* el, const std::string& html) {
        if (el->kind != NodeKind::Element) return;
        HtmlLimits lim;
        lim.maxDepth = std::max(1, lim.maxDepth - DepthOf(el) - 3);   // html > body > ... stays within the page's limit
        r.Charge(html.size() / 16);
        HtmlDocument frag = ParseHtml("<!DOCTYPE html><body>" + html, lim);
        Node* body = const_cast<Node*>(frag.Body());
        std::size_t count = 0, bytes = 0;
        if (body) for (auto& c : body->children) { count += CountNodes(c.get()); bytes += TreeBytes(c.get()); }
        r.Charge(count);
        CheckRoom(r, count);
        Account(r, bytes, 0);
        RemoveChildren(el);
        if (body)
            for (auto& c : body->children) {
                c->parent = el;
                el->children.push_back(std::move(c));
            }
        nodeCount += count;
        Changed();
    }
    std::string ValueOf(const Node& el) const {
        if (KindOf(el) == ControlKind::Select) {
            const int i = forms.SelectedIndex(el);
            const auto opts = forms.Options(el);
            return i >= 0 && (std::size_t)i < opts.size() ? opts[(std::size_t)i].first : std::string();
        }
        return forms.Value(el);
    }
    void SetValue(js::Runtime& r, const Node& el, const std::string& v) {
        Account(r, std::min(v.size(), FormState::kMaxValueBytes), ValueOf(el).size());
        if (KindOf(el) == ControlKind::Select) {
            const auto opts = forms.Options(el);
            for (std::size_t i = 0; i < opts.size(); ++i) if (opts[i].first == v) { forms.Select(el, (int)i); break; }
        } else {
            forms.SetValue(el, v);
        }
        Changed();
    }
    Node* TitleElement() {
        Node* found = nullptr;
        Walk(doc.root.get(), false, [&](Node* n) { if (n->IsElement("title")) { found = n; return true; } return false; });
        return found;
    }
    std::string Title() {
        Node* t = TitleElement();
        if (!t) return doc.title;
        std::string out;
        bool space = false;
        for (char c : t->TextContent()) {
            if (IsSpaceChar(c)) { space = !out.empty(); continue; }
            if (space) out += ' ';
            space = false;
            out += c;
        }
        return out;
    }
    void SetTitle(js::Runtime& r, const std::string& t) {
        Node* el = TitleElement();
        if (!el) {
            Node* head = nullptr;
            if (const Node* h = doc.Html()) for (auto& c : h->children) if (c->IsElement("head")) head = c.get();
            if (head) {
                el = NewNode(r, NodeKind::Element);
                el->name = "title";
                Insert(r, head, el, nullptr);
            }
        }
        if (el) SetText(r, el, t);
        doc.title = t;
        if (hooks.title) hooks.title(t);
    }

    std::vector<Node*> Query(js::Runtime& r, Node* scope, const std::string& selText, bool all) {
        std::vector<Selector> sels;
        int spec = 0;
        if (!ParseSelectorList(selText, sels, spec)) r.ThrowError(js::ErrorType::SyntaxError, "'" + selText + "' is not a selector we read");
        std::vector<Node*> out;
        Walk(scope, false, [&](Node* n) {
            if (n->kind != NodeKind::Element) return false;
            r.Charge(1);
            for (const Selector& s : sels)
                if (Matches(s, *n)) { out.push_back(n); return !all; }
            return false;
        });
        return out;
    }

    // ---- events
    js::Value HandlerFor(std::uint32_t slot, Node* n, const std::string& type) {
        auto op = onProps.find(slot);
        if (op != onProps.end()) for (auto& p : op->second) if (p.first == type) return rt.GetRef(p.second);
        if (!n || n->kind != NodeKind::Element) return js::Value();
        const std::string* src = n->Attr(("on" + type).c_str());
        if (!src) return js::Value();
        auto& list = attrHandlers[slot];
        for (auto& p : list)
            if (p.first == type) {
                if (p.second.source == *src) return p.second.fn >= 0 ? rt.GetRef(p.second.fn) : js::Value();
                if (p.second.fn >= 0) rt.Unref(p.second.fn);
                list.erase(std::find_if(list.begin(), list.end(), [&](const auto& q) { return q.first == type; }));
                break;
            }
        // HTML 8.1.8.1 "getting the current value of the event handler": the attribute's text is the body of a function of `event`
        AttrHandler h;
        h.source = *src;
        js::Value fn;
        std::string err;
        if (rt.Run("export default function (event) {\n" + *src + "\n}", "on" + type + " attribute", &err, &fn) && rt.IsCallable(fn))
            h.fn = rt.Ref(fn);
        else Log(ConsoleLine::Error, err);
        list.emplace_back(type, h);
        return h.fn >= 0 ? rt.GetRef(h.fn) : js::Value();
    }

    // DOM 2.9 "dispatch": the path is the target, its ancestors, then window when the target is in the page.
    bool DispatchEvent(Node* target, const std::string& type, bool bubbles, bool cancelable) {
        Entry entry(*this);
        std::vector<Node*> path;
        for (Node* p = target; p; p = p->parent) path.push_back(p);
        const bool toWindow = !path.empty() && path.back() == doc.root.get();
        js::Object* ev = rt.NewObject(eventProto);
        const int evRef = rt.Ref(js::Value::Obj(ev));
        auto put = [&](const char* k, const js::Value& v) { rt.DefineData(ev, rt.Intern(k), v); };
        put("type", js::Value::Str(rt.NewStringUtf8(type)));
        put("bubbles", js::Value::Bool(bubbles));
        put("cancelable", js::Value::Bool(cancelable));
        put("defaultPrevented", js::Value::Bool(false));
        put("isTrusted", js::Value::Bool(true));
        put("timeStamp", js::Value::Num(now));
        put("target", target ? Wrap(target) : js::Value::Obj(rt.global));
        Dispatching d;
        d.ev = ev;
        d.cancelable = cancelable;
        dispatching.push_back(d);
        auto state = [&]() -> Dispatching& { return dispatching.back(); };
        // one stop on the path: (node or window, phase)
        auto invoke = [&](Node* n, int phase) {
            if (state().stop) return;
            const std::uint32_t slot = n ? SlotFor(n) : kWindowSlot;
            const js::Value cur = n ? Wrap(n) : js::Value::Obj(rt.global);
            const int curRef = rt.Ref(cur);
            put("currentTarget", cur);
            put("eventPhase", js::Value::Num(phase));
            const js::Value evv = js::Value::Obj(ev);
            // the event handler (on<type>) first, then the listeners in the order they were added
            if (phase != 1) {
                js::Value h;
                if (n) h = HandlerFor(slot, n, type);
                else {
                    h = rt.GetGlobal(("on" + type).c_str());
                    if (!rt.IsCallable(h) && type == "load")   // <body onload="...">: window's handler (HTML 8.1.8.2)
                        if (const Node* b = doc.Body()) { const std::uint32_t bs = SlotFor(const_cast<Node*>(b)); h = HandlerFor(bs, const_cast<Node*>(b), type); }
                }
                if (rt.IsCallable(h)) {
                    js::Value ret;
                    if (CallFn(h, cur, {evv}, &ret) && ret.IsBool() && !ret.b && cancelable) state().prevented = true;
                }
            }
            auto it = listeners.find(slot);
            if (it != listeners.end() && !state().stopNow) {
                const std::vector<Listener> list = it->second;   // a listener may add or remove listeners
                for (const Listener& l : list) {
                    if (state().stopNow) break;
                    if (l.type != type) continue;
                    if (phase == 1 && !l.capture) continue;
                    if (phase == 3 && l.capture) continue;
                    // still there? (an earlier listener may have removed it)
                    auto& live = listeners[slot];
                    auto at = std::find_if(live.begin(), live.end(), [&](const Listener& x) { return x.fn == l.fn && x.type == l.type && x.capture == l.capture; });
                    if (at == live.end()) continue;
                    const js::Value fn = rt.GetRef(l.fn);
                    rt.Keep(fn);
                    const int fnRef = rt.Ref(fn);
                    if (l.once) { rt.Unref(at->fn); live.erase(at); }
                    if (rt.IsCallable(fn)) CallFn(fn, cur, {evv});
                    else if (fn.IsObj()) {   // an object with handleEvent (DOM 2.7 "inner invoke")
                        const js::Value he = rt.GetStr(fn, "handleEvent");
                        if (rt.IsCallable(he)) CallFn(he, fn, {evv});
                    }
                    rt.Unref(fnRef);
                }
            }
            rt.Unref(curRef);
            if (state().stopNow) state().stop = true;
        };
        // capture: window down to the target's parent; target; bubble: parent up to window
        if (toWindow) invoke(nullptr, 1);
        for (std::size_t i = path.size(); i-- > 1;) invoke(path[i], 1);
        if (target) invoke(target, 2);
        else invoke(nullptr, 2);
        if (bubbles && target) {
            for (std::size_t i = 1; i < path.size(); ++i) invoke(path[i], 3);
            if (toWindow) invoke(nullptr, 3);
        }
        const bool prevented = dispatching.back().prevented;
        dispatching.pop_back();
        put("eventPhase", js::Value::Num(0));
        put("currentTarget", js::Value::Null());
        rt.Unref(evRef);
        return prevented;
    }

    Dispatching* StateOf(const js::Value& ev) {
        for (std::size_t i = dispatching.size(); i-- > 0;)
            if (ev.IsObj() && dispatching[i].ev == ev.O()) return &dispatching[i];
        return nullptr;
    }

    void AddListener(js::Runtime& r, std::uint32_t slot, js::CallArgs& a) {
        if (!a[1].IsObj()) return;   // null / undefined: nothing (DOM 2.7)
        Listener l;
        l.type = r.ToUtf8(a[0]);
        if (a[2].IsObj()) {
            l.capture = r.ToBoolean(r.GetStr(a[2], "capture"));
            l.once = r.ToBoolean(r.GetStr(a[2], "once"));
        } else l.capture = r.ToBoolean(a[2]);
        auto& list = listeners[slot];
        for (const Listener& x : list)
            if (x.type == l.type && x.capture == l.capture && js::StrictEquals(r.GetRef(x.fn), a[1])) return;   // added once
        if (list.size() >= 1000) r.ThrowRange("too many listeners on one node");
        l.fn = r.Ref(a[1]);
        list.push_back(l);
    }
    void RemoveListener(js::Runtime& r, std::uint32_t slot, js::CallArgs& a) {
        auto it = listeners.find(slot);
        if (it == listeners.end()) return;
        const std::string type = r.ToUtf8(a[0]);
        const bool capture = a[2].IsObj() ? r.ToBoolean(r.GetStr(a[2], "capture")) : r.ToBoolean(a[2]);
        auto& list = it->second;
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i].type == type && list[i].capture == capture && js::StrictEquals(r.GetRef(list[i].fn), a[1])) {
                r.Unref(list[i].fn);
                list.erase(list.begin() + (std::ptrdiff_t)i);
                return;
            }
    }
    void SetOnProp(js::Runtime& r, std::uint32_t slot, const std::string& type, const js::Value& v) {
        auto& list = onProps[slot];
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i].first == type) { r.Unref(list[i].second); list.erase(list.begin() + (std::ptrdiff_t)i); break; }
        if (r.IsCallable(v)) list.emplace_back(type, r.Ref(v));
    }
    void DropSlotRefs(std::uint32_t slot) {
        auto l = listeners.find(slot);
        if (l != listeners.end()) { for (auto& x : l->second) rt.Unref(x.fn); listeners.erase(l); }
        auto o = onProps.find(slot);
        if (o != onProps.end()) { for (auto& x : o->second) rt.Unref(x.second); onProps.erase(o); }
        auto h = attrHandlers.find(slot);
        if (h != attrHandlers.end()) { for (auto& x : h->second) if (x.second.fn >= 0) rt.Unref(x.second.fn); attrHandlers.erase(h); }
    }

    // ---- timers (HTML 8.6)
    int AddTimer(js::Runtime& r, js::CallArgs& a, bool repeat, bool frame) {
        if (!r.IsCallable(a[0])) r.ThrowType("the timer callback is not a function (code strings are not run)");
        if (timers.size() >= kMaxTimers) r.ThrowRange("too many timers (" + std::to_string(kMaxTimers) + ")");
        Timer t;
        t.id = nextTimer++;
        if (nextTimer > 2000000000) nextTimer = 1;
        double delay = frame ? 0.0 : r.ToNumber(a[1]);
        if (!(delay >= 0.0)) delay = 0.0;
        delay = std::min(delay, 2147483647.0);
        t.interval = repeat ? std::max(delay, 1.0) : delay;
        t.due = now + delay;
        t.repeat = repeat;
        t.frame = frame;
        t.fn = r.Ref(a[0]);
        if (!frame) for (int i = 2; i < a.argc && i < 34; ++i) t.args.push_back(r.Ref(a.argv[i]));
        timers.push_back(std::move(t));
        return timers.back().id;
    }
    void DropTimer(std::size_t i) {
        rt.Unref(timers[i].fn);
        for (int r : timers[i].args) rt.Unref(r);
        timers.erase(timers.begin() + (std::ptrdiff_t)i);
    }
    void ClearTimer(int id) {
        for (std::size_t i = 0; i < timers.size(); ++i) if (timers[i].id == id) { DropTimer(i); return; }
    }
    void RunTimers() {
        Entry entry(*this);
        // the timers due now, earliest first; timers they add wait for the next tick
        std::vector<std::pair<double, int>> due;
        for (const Timer& t : timers) if (t.due <= now) due.emplace_back(t.due, t.id);
        std::sort(due.begin(), due.end());
        if (due.size() > (std::size_t)kMaxTimersPerTick) due.resize((std::size_t)kMaxTimersPerTick);
        for (const auto& d : due) {
            std::size_t i = 0;
            while (i < timers.size() && timers[i].id != d.second) ++i;
            if (i == timers.size()) continue;   // cleared by an earlier callback
            const js::Value fn = rt.GetRef(timers[i].fn);
            std::vector<js::Value> args;
            for (int r : timers[i].args) args.push_back(rt.GetRef(r));
            if (timers[i].frame) args.push_back(js::Value::Num(now));
            const int keep = rt.Ref(fn);
            if (timers[i].repeat) timers[i].due = std::max(timers[i].due + timers[i].interval, now);
            else {   // a one-shot timer is gone before its callback runs (it may set a new one)
                std::vector<int> argRefs = timers[i].args;
                timers[i].args.clear();
                DropTimer(i);
                CallFn(fn, js::Value::Obj(rt.global), args);
                for (int r : argRefs) rt.Unref(r);
                rt.Unref(keep);
                continue;
            }
            CallFn(fn, js::Value::Obj(rt.global), args);
            rt.Unref(keep);
        }
    }

};

void Install(PageScripts::Impl& I);

// ============================================================ the binding
namespace {

using Impl = PageScripts::Impl;
Impl& Of(js::Runtime& rt) { return *static_cast<Impl*>(rt.userData); }
js::Value Str(js::Runtime& rt, const std::string& s) { return js::Value::Str(rt.NewStringUtf8(s)); }

Node* ThisNode(js::Runtime& rt, js::CallArgs& a) {
    Node* n = Of(rt).NodeArg(a.thisv);
    if (!n) rt.ThrowType("Illegal invocation: 'this' is not a node of this page");
    return n;
}
Node* ThisElement(js::Runtime& rt, js::CallArgs& a) {
    Node* n = ThisNode(rt, a);
    if (n->kind != NodeKind::Element) rt.ThrowType("this node is not an element");
    return n;
}
Node* ArgNode(js::Runtime& rt, const js::Value& v) {
    Node* n = Of(rt).NodeArg(v);
    if (!n) rt.ThrowType("the argument is not a node of this page");
    return n;
}
// A string argument becomes a text node (ParentNode.append / prepend, 4.2.6 "convert nodes into a node").
Node* NodeOrText(js::Runtime& rt, const js::Value& v) {
    if (Node* n = Of(rt).NodeArg(v)) return n;
    Node* t = Of(rt).NewNode(rt, NodeKind::Text);
    Of(rt).SetData(rt, *t, rt.ToUtf8(v));
    return t;
}
Node* PartNode(js::Runtime& rt, js::HostObj* h) { return Of(rt).NodeOf(h); }

// The event handler properties and attributes we know (HTML 8.1.8.2): el.onclick = fn, <button onclick="...">.
bool IsHandlerName(const std::string& k) {
    static const char* const kTypes[] = {"click", "dblclick", "input", "change", "submit", "reset", "load", "error", "keydown",
                                         "keyup", "keypress", "mousedown", "mouseup", "mouseover", "mouseout", "mousemove",
                                         "focus", "blur", "scroll", "resize", "contextmenu", "wheel", "pointerdown", "pointerup", nullptr};
    if (k.size() < 3 || k[0] != 'o' || k[1] != 'n') return false;
    for (const char* const* t = kTypes; *t; ++t) if (k.compare(2, std::string::npos, *t) == 0) return true;
    return false;
}

const char* const kReflected[] = {"href", "src", "alt", "name", "type", "placeholder", "rel", "action", "method", "target",
                                  "lang", "dir", "role", "width", "height", "min", "max", "step", "pattern", "for", nullptr};

bool NodeGet(js::Runtime& rt, js::HostObj* self, js::String* keyS, js::Value& out) {
    Impl& I = Of(rt);
    Node* n = I.NodeOf(self);
    if (!n) return false;
    const std::string k = js::Utf16To8(keyS->s);
    const bool el = n->kind == NodeKind::Element;
    const bool isDoc = n->kind == NodeKind::Document;
    if (k == "nodeType") {
        out = js::Value::Num(el ? 1 : n->kind == NodeKind::Text ? 3 : n->kind == NodeKind::Comment ? 8 : isDoc ? 9 : 10);
        return true;
    }
    if (k == "nodeName") {
        out = Str(rt, el ? UpperAscii(n->name) : n->kind == NodeKind::Text ? "#text" : n->kind == NodeKind::Comment ? "#comment" : isDoc ? "#document" : n->name);
        return true;
    }
    if (k == "parentNode") { out = I.Wrap(n->parent); return true; }
    if (k == "parentElement") { out = n->parent && n->parent->kind == NodeKind::Element ? I.Wrap(n->parent) : js::Value::Null(); return true; }
    if (k == "firstChild") { out = n->children.empty() ? js::Value::Null() : I.Wrap(n->children.front().get()); return true; }
    if (k == "lastChild") { out = n->children.empty() ? js::Value::Null() : I.Wrap(n->children.back().get()); return true; }
    if (k == "nextSibling" || k == "previousSibling" || k == "nextElementSibling" || k == "previousElementSibling") {
        out = js::Value::Null();
        if (!n->parent) return true;
        const auto& sib = n->parent->children;
        rt.Charge(sib.size() / 8);
        std::size_t i = 0;
        while (i < sib.size() && sib[i].get() != n) ++i;
        const bool next = k[0] == 'n', elOnly = k.find("Element") != std::string::npos;
        if (next) { for (std::size_t j = i + 1; j < sib.size(); ++j) if (!elOnly || sib[j]->kind == NodeKind::Element) { out = I.Wrap(sib[j].get()); break; } }
        else { for (std::size_t j = i; j-- > 0;) if (!elOnly || sib[j]->kind == NodeKind::Element) { out = I.Wrap(sib[j].get()); break; } }
        return true;
    }
    if (k == "childNodes" || k == "children") {
        std::vector<Node*> list;
        for (auto& c : n->children) if (k == "childNodes" || c->kind == NodeKind::Element) list.push_back(c.get());
        out = I.Array(list);
        return true;
    }
    if (k == "firstElementChild" || k == "lastElementChild") {
        out = js::Value::Null();
        if (k[0] == 'f') { for (auto& c : n->children) if (c->kind == NodeKind::Element) { out = I.Wrap(c.get()); break; } }
        else for (std::size_t j = n->children.size(); j-- > 0;) if (n->children[j]->kind == NodeKind::Element) { out = I.Wrap(n->children[j].get()); break; }
        return true;
    }
    if (k == "childElementCount") {
        double c = 0;
        for (auto& ch : n->children) if (ch->kind == NodeKind::Element) ++c;
        out = js::Value::Num(c);
        return true;
    }
    if (k == "isConnected") { out = js::Value::Bool(I.InPage(n)); return true; }
    if (k == "ownerDocument") { out = isDoc ? js::Value::Null() : I.Wrap(I.doc.root.get()); return true; }
    if (k == "textContent") {
        const std::string text = isDoc ? std::string() : el ? n->TextContent() : n->data;
        rt.Charge(text.size() / 16);
        out = isDoc ? js::Value::Null() : Str(rt, text);
        return true;
    }
    if (k == "nodeValue" || k == "data") {
        if (el || isDoc) { if (k == "data") return false; out = js::Value::Null(); return true; }
        out = Str(rt, n->data);
        return true;
    }
    if (k == "length" && (n->kind == NodeKind::Text || n->kind == NodeKind::Comment)) { out = js::Value::Num((double)js::Utf8To16(n->data).size()); return true; }
    if (isDoc) {
        auto child = [&](const char* tag) -> Node* {
            if (const Node* h = I.doc.Html()) for (auto& c : h->children) if (c->IsElement(tag)) return c.get();
            return nullptr;
        };
        if (k == "documentElement") { out = I.Wrap(const_cast<Node*>(I.doc.Html())); return true; }
        if (k == "body") { out = I.Wrap(child("body")); return true; }
        if (k == "head") { out = I.Wrap(child("head")); return true; }
        if (k == "title") { out = Str(rt, I.Title()); return true; }
        if (k == "readyState") { out = Str(rt, I.loaded ? "complete" : "interactive"); return true; }
        if (k == "URL" || k == "documentURI") { out = Str(rt, I.url); return true; }
        if (k == "location") { out = rt.GetRef(I.locationRef); return true; }
        if (k == "defaultView") { out = js::Value::Obj(rt.global); return true; }
        if (k == "activeElement") { out = I.Wrap(child("body")); return true; }
        return false;
    }
    if (!el) return false;
    if (k == "tagName") { out = Str(rt, UpperAscii(n->name)); return true; }
    if (k == "localName") { out = Str(rt, n->name); return true; }
    if (k == "id") { out = Str(rt, AttrOr(*n, "id")); return true; }
    if (k == "className") { out = Str(rt, AttrOr(*n, "class")); return true; }
    if (k == "title") { out = Str(rt, AttrOr(*n, "title")); return true; }
    if (k == "htmlFor") { out = Str(rt, AttrOr(*n, "for")); return true; }
    if (k == "innerText" || k == "innerHTML" || k == "outerHTML") {
        const std::string text = k == "innerText" ? n->TextContent() : k == "innerHTML" ? SerializeChildren(*n) : SerializeNode(*n);
        rt.Charge(text.size() / 16);
        out = Str(rt, text);
        return true;
    }
    if (k == "value") { out = Str(rt, I.ValueOf(*n)); return true; }
    if (k == "checked") { out = js::Value::Bool(I.forms.Checked(*n)); return true; }
    if (k == "disabled" || k == "hidden" || k == "required" || k == "readOnly" || k == "multiple" || k == "selected") {
        out = js::Value::Bool(n->Attr(LowerAscii(k).c_str()) != nullptr);
        return true;
    }
    if (k == "selectedIndex") { out = js::Value::Num(I.forms.SelectedIndex(*n)); return true; }
    if (k == "style") { out = I.Part(I.kStyle, I.styleProto, n); return true; }
    if (k == "classList") { out = I.Part(I.kClassList, I.classListProto, n); return true; }
    if (IsHandlerName(k)) {
        auto op = I.onProps.find(I.SlotFor(n));
        out = js::Value::Null();
        if (op != I.onProps.end()) for (auto& p : op->second) if (p.first == k.substr(2)) { out = rt.GetRef(p.second); break; }
        return true;
    }
    if (k == "dataset") {   // a snapshot of the data-* attributes (writing to it does not change them yet)
        js::Object* o = rt.NewObject();
        for (const auto& a : n->attrs)
            if (a.first.compare(0, 5, "data-") == 0) {
                std::string camel;
                for (std::size_t i = 5; i < a.first.size(); ++i) {
                    if (a.first[i] == '-' && i + 1 < a.first.size() && a.first[i + 1] >= 'a' && a.first[i + 1] <= 'z') { camel += (char)(a.first[++i] - 32); continue; }
                    camel += a.first[i];
                }
                rt.DefineData(o, rt.Intern(js::Utf8To16(camel)), Str(rt, a.second));
            }
        out = js::Value::Obj(o);
        return true;
    }
    for (const char* const* r = kReflected; *r; ++r)
        if (k == *r) {
            std::string v = AttrOr(*n, *r);
            if ((k == "href" || k == "src" || k == "action") && !v.empty()) v = ResolveUrl(I.url, v);
            out = Str(rt, v);
            return true;
        }
    return false;
}

bool NodeSet(js::Runtime& rt, js::HostObj* self, js::String* keyS, const js::Value& v) {
    Impl& I = Of(rt);
    Node* n = I.NodeOf(self);
    if (!n) return false;
    const std::string k = js::Utf16To8(keyS->s);
    const bool el = n->kind == NodeKind::Element;
    if (k == "textContent" || k == "innerText" || ((k == "nodeValue" || k == "data") && !el)) {
        if (n->kind == NodeKind::Document) return true;
        I.SetText(rt, n, v.IsNullish() ? std::string() : rt.ToUtf8(v));
        return true;
    }
    if (n->kind == NodeKind::Document) {
        if (k == "title") { I.SetTitle(rt, rt.ToUtf8(v)); return true; }
        return false;
    }
    if (!el) return false;
    auto attr = [&](const char* name) { I.SetAttrC(rt, *n, name, rt.ToUtf8(v)); I.Changed(); return true; };
    if (k == "id") return attr("id");
    if (k == "className") return attr("class");
    if (k == "title") return attr("title");
    if (k == "htmlFor") return attr("for");
    if (k == "innerHTML") { I.SetInnerHtml(rt, n, v.IsNullish() ? std::string() : rt.ToUtf8(v)); return true; }
    if (k == "value") { I.SetValue(rt, *n, rt.ToUtf8(v)); return true; }
    if (k == "checked") { I.forms.SetChecked(*n, rt.ToBoolean(v)); I.Changed(); return true; }
    if (k == "selectedIndex") { I.forms.Select(*n, (int)std::max(-1.0, std::min(1e6, rt.ToIntegerOrInfinity(v)))); I.Changed(); return true; }
    if (k == "disabled" || k == "hidden" || k == "required" || k == "readOnly" || k == "multiple" || k == "selected") {
        if (rt.ToBoolean(v)) I.SetAttrC(rt, *n, LowerAscii(k), std::string());
        else RemoveAttr(*n, LowerAscii(k));
        I.Changed();
        return true;
    }
    if (IsHandlerName(k)) { I.SetOnProp(rt, I.SlotFor(n), k.substr(2), v); return true; }
    if (k == "style" || k == "classList") {   // el.style = "color: red" sets the attribute (PutForwards)
        return attr(k == "style" ? "style" : "class");
    }
    for (const char* const* r = kReflected; *r; ++r) if (k == *r) return attr(*r);
    return false;
}

std::string NodeText(js::Runtime& rt, js::HostObj* self) {
    Node* n = Of(rt).NodeOf(self);
    if (!n) return "[object Node]";
    if (n->kind == NodeKind::Document) return "[object HTMLDocument]";
    if (n->kind == NodeKind::Text) return "[object Text]";
    return "[object HTML" + UpperAscii(n->name) + "Element]";
}

// ---- element.style: the declarations of the style attribute (CSSOM 6.7, reduced to reading and writing them)
bool StyleGet(js::Runtime& rt, js::HostObj* self, js::String* keyS, js::Value& out) {
    Node* n = PartNode(rt, self);
    if (!n) return false;
    const std::string k = js::Utf16To8(keyS->s);
    if (k == "cssText") { out = Str(rt, AttrOr(*n, "style")); return true; }
    if (k == "getPropertyValue" || k == "setProperty" || k == "removeProperty") return false;
    const std::string prop = CssName(k);
    for (const Declaration& d : ParseDeclarations(AttrOr(*n, "style"))) if (d.property == prop) { out = Str(rt, d.value); return true; }
    out = Str(rt, std::string());
    return true;
}
void WriteStyle(js::Runtime& rt, Node* n, const std::string& prop, const std::string& value) {
    std::string css;
    bool done = false;
    for (const Declaration& d : ParseDeclarations(AttrOr(*n, "style"))) {
        if (d.property == prop) {
            if (!value.empty() && !done) css += prop + ": " + value + "; ";
            done = true;
            continue;
        }
        css += d.property + ": " + d.value + (d.important ? " !important" : "") + "; ";
    }
    if (!done && !value.empty()) css += prop + ": " + value + "; ";
    while (!css.empty() && css.back() == ' ') css.pop_back();
    Of(rt).SetAttrC(rt, *n, "style", css);
    Of(rt).Changed();
}
bool StyleSet(js::Runtime& rt, js::HostObj* self, js::String* keyS, const js::Value& v) {
    Node* n = PartNode(rt, self);
    if (!n) return false;
    const std::string k = js::Utf16To8(keyS->s);
    std::string value = v.IsNullish() ? std::string() : rt.ToUtf8(v);
    if (value.find_first_of(";{}") != std::string::npos) return true;   // one value only; never another declaration
    if (k == "cssText") { Of(rt).SetAttrC(rt, *n, "style", rt.ToUtf8(v)); Of(rt).Changed(); return true; }
    WriteStyle(rt, n, CssName(k), value);
    return true;
}

// ---- element.classList (DOM 4.9 DOMTokenList over the class attribute)
bool ClassListGet(js::Runtime& rt, js::HostObj* self, js::String* keyS, js::Value& out) {
    Node* n = PartNode(rt, self);
    if (!n) return false;
    const std::string k = js::Utf16To8(keyS->s);
    const std::vector<std::string> t = Tokens(AttrOr(*n, "class"));
    if (k == "length") { out = js::Value::Num((double)t.size()); return true; }
    if (k == "value") { out = Str(rt, AttrOr(*n, "class")); return true; }
    if (!k.empty() && k[0] >= '0' && k[0] <= '9') {
        const std::size_t i = (std::size_t)std::strtoul(k.c_str(), nullptr, 10);
        out = i < t.size() ? Str(rt, t[i]) : js::Value();
        return true;
    }
    return false;
}

std::string ClassListText(js::Runtime& rt, js::HostObj* self) {
    Node* n = PartNode(rt, self);
    return n ? AttrOr(*n, "class") : std::string();
}

std::string LocationText(js::Runtime& rt, js::HostObj*) { return Of(rt).url; }
bool LocationGet(js::Runtime& rt, js::HostObj*, js::String* keyS, js::Value& out) {
    const std::string k = js::Utf16To8(keyS->s);
    const std::string& u = Of(rt).url;
    const Url p = ParseUrl(u);
    if (k == "href") out = Str(rt, u);
    else if (k == "protocol") out = Str(rt, p.scheme + ":");
    else if (k == "host") out = Str(rt, p.authority);
    else if (k == "hostname") out = Str(rt, HostOf(u));
    else if (k == "pathname") out = Str(rt, p.path.empty() ? "/" : p.path);
    else if (k == "search") out = Str(rt, p.hasQuery && !p.query.empty() ? "?" + p.query : "");
    else if (k == "hash") out = Str(rt, p.hasFragment && !p.fragment.empty() ? "#" + p.fragment : "");
    else if (k == "origin") out = Str(rt, OriginOf(u));
    else return false;
    return true;
}
void AskToGo(js::Runtime& rt, const std::string& where) {
    Impl& I = Of(rt);
    if (I.hooks.navigate) I.hooks.navigate(where);
}
bool LocationSet(js::Runtime& rt, js::HostObj*, js::String* keyS, const js::Value& v) {
    const std::string k = js::Utf16To8(keyS->s);
    if (k == "href") { AskToGo(rt, rt.ToUtf8(v)); return true; }
    if (k == "hash") { AskToGo(rt, "#" + rt.ToUtf8(v)); return true; }
    return true;   // the other parts are not written (one change at a time: href)
}

using js::Value;
using js::CallArgs;
using js::Runtime;

} // namespace

void Install(PageScripts::Impl& I) {
    js::Runtime& r = I.rt;
    r.userData = &I;
    r.memoryLimit = PageScripts::kMemoryLimit;
    r.onHostFree = [&I](js::HostObj* h) {
        if (h->hostClass == I.kNode && h->id < I.wraps.size() && I.wraps[(std::size_t)h->id] == h) I.wraps[(std::size_t)h->id] = nullptr;
    };
    r.printSink = [&I](const std::string& s) { I.Log(I.level, s); };
    I.kNode = r.AddHostClass({"Node", NodeGet, NodeSet, NodeText, nullptr});
    I.kStyle = r.AddHostClass({"CSSStyleDeclaration", StyleGet, StyleSet, nullptr, nullptr});
    I.kClassList = r.AddHostClass({"DOMTokenList", ClassListGet, nullptr, ClassListText, nullptr});
    I.kLocation = r.AddHostClass({"Location", LocationGet, LocationSet, LocationText, nullptr});
    I.ownerKey = r.NewSymbol(r.Intern("owner"));
    r.Ref(Value::Sym(I.ownerKey));
    auto proto = [&](js::Object* parent) { js::Object* o = r.NewObject(parent); r.Ref(Value::Obj(o)); return o; };
    I.nodeProto = proto(r.objectProto);
    I.documentProto = proto(I.nodeProto);
    I.styleProto = proto(r.objectProto);
    I.classListProto = proto(r.objectProto);
    I.eventProto = proto(r.objectProto);
    I.locationProto = proto(r.objectProto);

    // ---- Node, Element and ParentNode methods (DOM 4.4, 4.5, 4.9)
    js::DefNative(r, I.nodeProto, "appendChild", [](Runtime& rt, CallArgs& a) -> Value {
        Node* p = ThisNode(rt, a);
        Of(rt).Insert(rt, p, ArgNode(rt, a[0]), nullptr);
        return a[0];
    }, 1);
    js::DefNative(r, I.nodeProto, "insertBefore", [](Runtime& rt, CallArgs& a) -> Value {
        Node* p = ThisNode(rt, a);
        Node* before = a[1].IsNullish() ? nullptr : ArgNode(rt, a[1]);
        Of(rt).Insert(rt, p, ArgNode(rt, a[0]), before);
        return a[0];
    }, 2);
    js::DefNative(r, I.nodeProto, "removeChild", [](Runtime& rt, CallArgs& a) -> Value {
        Node* p = ThisNode(rt, a);
        Node* c = ArgNode(rt, a[0]);
        if (c->parent != p) rt.ThrowError(js::ErrorType::Error, "NotFoundError: the node is not a child of this node");
        Of(rt).Remove(c);
        return a[0];
    }, 1);
    js::DefNative(r, I.nodeProto, "replaceChild", [](Runtime& rt, CallArgs& a) -> Value {
        Node* p = ThisNode(rt, a);
        Node* nw = ArgNode(rt, a[0]);
        Node* old = ArgNode(rt, a[1]);
        if (old->parent != p) rt.ThrowError(js::ErrorType::Error, "NotFoundError: the node is not a child of this node");
        if (nw != old) { Of(rt).Insert(rt, p, nw, old); Of(rt).Remove(old); }
        return a[1];
    }, 2);
    js::DefNative(r, I.nodeProto, "remove", [](Runtime& rt, CallArgs& a) -> Value { Of(rt).Remove(ThisNode(rt, a)); return Value(); }, 0);
    js::DefNative(r, I.nodeProto, "append", [](Runtime& rt, CallArgs& a) -> Value {
        Node* p = ThisNode(rt, a);
        for (int i = 0; i < a.argc; ++i) Of(rt).Insert(rt, p, NodeOrText(rt, a.argv[i]), nullptr);
        return Value();
    }, 0);
    js::DefNative(r, I.nodeProto, "prepend", [](Runtime& rt, CallArgs& a) -> Value {
        Node* p = ThisNode(rt, a);
        Node* first = p->children.empty() ? nullptr : p->children.front().get();
        for (int i = 0; i < a.argc; ++i) Of(rt).Insert(rt, p, NodeOrText(rt, a.argv[i]), first);
        return Value();
    }, 0);
    js::DefNative(r, I.nodeProto, "hasChildNodes", [](Runtime& rt, CallArgs& a) -> Value { return Value::Bool(!ThisNode(rt, a)->children.empty()); }, 0);
    js::DefNative(r, I.nodeProto, "contains", [](Runtime& rt, CallArgs& a) -> Value {
        Node* p = ThisNode(rt, a);
        Node* c = Of(rt).NodeArg(a[0]);
        for (; c; c = c->parent) if (c == p) return Value::Bool(true);
        return Value::Bool(false);
    }, 1);
    js::DefNative(r, I.nodeProto, "getAttribute", [](Runtime& rt, CallArgs& a) -> Value {
        const std::string* v = ThisElement(rt, a)->Attr(LowerAscii(rt.ToUtf8(a[0])).c_str());
        return v ? Str(rt, *v) : Value::Null();
    }, 1);
    js::DefNative(r, I.nodeProto, "hasAttribute", [](Runtime& rt, CallArgs& a) -> Value {
        return Value::Bool(ThisElement(rt, a)->Attr(LowerAscii(rt.ToUtf8(a[0])).c_str()) != nullptr);
    }, 1);
    js::DefNative(r, I.nodeProto, "setAttribute", [](Runtime& rt, CallArgs& a) -> Value {
        Node* n = ThisElement(rt, a);
        const std::string name = LowerAscii(rt.ToUtf8(a[0]));
        if (name.empty() || name.size() > HtmlLimits().maxNameBytes || name.find_first_of(" \t\n\f\r/>=\"'") != std::string::npos)
            rt.ThrowError(js::ErrorType::Error, "InvalidCharacterError: '" + name + "' is not an attribute name");
        std::string value = rt.ToUtf8(a[1]);
        if (value.size() > HtmlLimits().maxAttrValueBytes) value.resize(HtmlLimits().maxAttrValueBytes);
        Of(rt).SetAttrC(rt, *n, name, value);
        Of(rt).Changed();
        return Value();
    }, 2);
    js::DefNative(r, I.nodeProto, "removeAttribute", [](Runtime& rt, CallArgs& a) -> Value {
        if (RemoveAttr(*ThisElement(rt, a), LowerAscii(rt.ToUtf8(a[0])))) Of(rt).Changed();
        return Value();
    }, 1);
    js::DefNative(r, I.nodeProto, "querySelector", [](Runtime& rt, CallArgs& a) -> Value {
        const std::vector<Node*> f = Of(rt).Query(rt, ThisNode(rt, a), rt.ToUtf8(a[0]), false);
        return f.empty() ? Value::Null() : Of(rt).Wrap(f[0]);
    }, 1);
    js::DefNative(r, I.nodeProto, "querySelectorAll", [](Runtime& rt, CallArgs& a) -> Value {
        return Of(rt).Array(Of(rt).Query(rt, ThisNode(rt, a), rt.ToUtf8(a[0]), true));
    }, 1);
    js::DefNative(r, I.nodeProto, "getElementsByTagName", [](Runtime& rt, CallArgs& a) -> Value {
        const std::string tag = LowerAscii(rt.ToUtf8(a[0]));
        std::vector<Node*> out;
        Walk(ThisNode(rt, a), false, [&](Node* n) { rt.Charge(1); if (n->kind == NodeKind::Element && (tag == "*" || n->name == tag)) out.push_back(n); return false; });
        return Of(rt).Array(out);
    }, 1);
    js::DefNative(r, I.nodeProto, "getElementsByClassName", [](Runtime& rt, CallArgs& a) -> Value {
        const std::vector<std::string> want = Tokens(rt.ToUtf8(a[0]));
        std::vector<Node*> out;
        if (!want.empty())
            Walk(ThisNode(rt, a), false, [&](Node* n) {
                rt.Charge(1);
                if (n->kind != NodeKind::Element) return false;
                const std::vector<std::string> has = Tokens(AttrOr(*n, "class"));
                for (const std::string& w : want) if (std::find(has.begin(), has.end(), w) == has.end()) return false;
                out.push_back(n);
                return false;
            });
        return Of(rt).Array(out);
    }, 1);
    js::DefNative(r, I.nodeProto, "matches", [](Runtime& rt, CallArgs& a) -> Value {
        Node* n = ThisElement(rt, a);
        std::vector<Selector> sels;
        int spec = 0;
        const std::string text = rt.ToUtf8(a[0]);
        if (!ParseSelectorList(text, sels, spec)) rt.ThrowError(js::ErrorType::SyntaxError, "'" + text + "' is not a selector we read");
        for (const Selector& s : sels) if (Matches(s, *n)) return Value::Bool(true);
        return Value::Bool(false);
    }, 1);
    js::DefNative(r, I.nodeProto, "closest", [](Runtime& rt, CallArgs& a) -> Value {
        Node* n = ThisElement(rt, a);
        std::vector<Selector> sels;
        int spec = 0;
        const std::string text = rt.ToUtf8(a[0]);
        if (!ParseSelectorList(text, sels, spec)) rt.ThrowError(js::ErrorType::SyntaxError, "'" + text + "' is not a selector we read");
        for (; n && n->kind == NodeKind::Element; n = n->parent)
            for (const Selector& s : sels) if (Matches(s, *n)) return Of(rt).Wrap(n);
        return Value::Null();
    }, 1);
    js::DefNative(r, I.nodeProto, "addEventListener", [](Runtime& rt, CallArgs& a) -> Value {
        Of(rt).AddListener(rt, Of(rt).SlotFor(ThisNode(rt, a)), a);
        return Value();
    }, 2);
    js::DefNative(r, I.nodeProto, "removeEventListener", [](Runtime& rt, CallArgs& a) -> Value {
        Of(rt).RemoveListener(rt, Of(rt).SlotFor(ThisNode(rt, a)), a);
        return Value();
    }, 2);
    js::DefNative(r, I.nodeProto, "click", [](Runtime& rt, CallArgs& a) -> Value {   // HTML 6.4.2: a synthetic click (it bubbles)
        Of(rt).DispatchEvent(ThisNode(rt, a), "click", true, true);
        return Value();
    }, 0);
    js::DefNative(r, I.nodeProto, "focus", [](Runtime&, CallArgs&) -> Value { return Value(); }, 0);
    js::DefNative(r, I.nodeProto, "blur", [](Runtime&, CallArgs&) -> Value { return Value(); }, 0);
    js::DefNative(r, I.nodeProto, "cloneNode", [](Runtime& rt, CallArgs& a) -> Value {   // DOM 4.4 "clone a node"
        Node* src = ThisNode(rt, a);
        if (src->kind == NodeKind::Document) rt.ThrowError(js::ErrorType::Error, "NotSupportedError: a document is not cloned");
        const bool deep = rt.ToBoolean(a[0]);
        Impl& P = Of(rt);
        P.CheckRoom(rt, deep ? CountNodes(src) : 1);
        P.Account(rt, deep ? TreeBytes(src) : BytesOf(*src), 0);
        Node* top = P.NewNode(rt, src->kind);
        std::vector<std::pair<const Node*, Node*>> todo{{src, top}};
        while (!todo.empty()) {
            auto [from, to] = todo.back();
            todo.pop_back();
            to->name = from->name;
            to->data = from->data;
            to->attrs = from->attrs;
            if (!deep) continue;
            for (auto& c : from->children) {
                std::unique_ptr<Node> nc(new Node());
                nc->kind = c->kind;
                nc->parent = to;
                Node* p = nc.get();
                to->children.push_back(std::move(nc));
                ++P.nodeCount;
                todo.push_back({c.get(), p});
            }
        }
        return P.Wrap(top);
    }, 1);

    // ---- document (DOM 4.5, HTML 3.1)
    js::DefNative(r, I.documentProto, "getElementById", [](Runtime& rt, CallArgs& a) -> Value {
        const std::string id = rt.ToUtf8(a[0]);
        Node* found = nullptr;
        Walk(Of(rt).doc.root.get(), false, [&](Node* n) {
            rt.Charge(1);
            if (n->kind == NodeKind::Element) if (const std::string* v = n->Attr("id")) if (*v == id) { found = n; return true; }
            return false;
        });
        return Of(rt).Wrap(found);
    }, 1);
    js::DefNative(r, I.documentProto, "createElement", [](Runtime& rt, CallArgs& a) -> Value {
        const std::string tag = LowerAscii(rt.ToUtf8(a[0]));
        if (tag.empty() || tag.size() > HtmlLimits().maxNameBytes || tag.find_first_of(" \t\n\f\r/<>=\"'") != std::string::npos)
            rt.ThrowError(js::ErrorType::Error, "InvalidCharacterError: '" + tag + "' is not an element name");
        Node* n = Of(rt).NewNode(rt, NodeKind::Element);
        n->name = tag;
        return Of(rt).Wrap(n);
    }, 1);
    js::DefNative(r, I.documentProto, "createTextNode", [](Runtime& rt, CallArgs& a) -> Value {
        Node* n = Of(rt).NewNode(rt, NodeKind::Text);
        Of(rt).SetData(rt, *n, rt.ToUtf8(a[0]));
        return Of(rt).Wrap(n);
    }, 1);
    js::DefNative(r, I.documentProto, "createComment", [](Runtime& rt, CallArgs& a) -> Value {
        Node* n = Of(rt).NewNode(rt, NodeKind::Comment);
        Of(rt).SetData(rt, *n, rt.ToUtf8(a[0]));
        return Of(rt).Wrap(n);
    }, 1);

    // ---- events (DOM 2.2): preventDefault, stopPropagation, stopImmediatePropagation
    js::DefNative(r, I.eventProto, "preventDefault", [](Runtime& rt, CallArgs& a) -> Value {
        if (Impl::Dispatching* d = Of(rt).StateOf(a.thisv))
            if (d->cancelable) { d->prevented = true; rt.DefineData(d->ev, rt.Intern("defaultPrevented"), Value::Bool(true)); }
        return Value();
    }, 0);
    js::DefNative(r, I.eventProto, "stopPropagation", [](Runtime& rt, CallArgs& a) -> Value {
        if (Impl::Dispatching* d = Of(rt).StateOf(a.thisv)) d->stop = true;
        return Value();
    }, 0);
    js::DefNative(r, I.eventProto, "stopImmediatePropagation", [](Runtime& rt, CallArgs& a) -> Value {
        if (Impl::Dispatching* d = Of(rt).StateOf(a.thisv)) d->stop = d->stopNow = true;
        return Value();
    }, 0);

    // ---- style and classList methods
    js::DefNative(r, I.styleProto, "getPropertyValue", [](Runtime& rt, CallArgs& a) -> Value {
        Node* n = a.thisv.IsObj() && a.thisv.O()->cls == js::Cls::Host ? PartNode(rt, static_cast<js::HostObj*>(a.thisv.O())) : nullptr;
        if (!n) rt.ThrowType("Illegal invocation");
        const std::string prop = LowerAscii(rt.ToUtf8(a[0]));
        for (const Declaration& d : ParseDeclarations(AttrOr(*n, "style"))) if (d.property == prop) return Str(rt, d.value);
        return Str(rt, std::string());
    }, 1);
    js::DefNative(r, I.styleProto, "setProperty", [](Runtime& rt, CallArgs& a) -> Value {
        Node* n = a.thisv.IsObj() && a.thisv.O()->cls == js::Cls::Host ? PartNode(rt, static_cast<js::HostObj*>(a.thisv.O())) : nullptr;
        if (!n) rt.ThrowType("Illegal invocation");
        const std::string value = a[1].IsNullish() ? std::string() : rt.ToUtf8(a[1]);
        if (value.find_first_of(";{}") == std::string::npos) WriteStyle(rt, n, LowerAscii(rt.ToUtf8(a[0])), value);
        return Value();
    }, 2);
    js::DefNative(r, I.styleProto, "removeProperty", [](Runtime& rt, CallArgs& a) -> Value {
        Node* n = a.thisv.IsObj() && a.thisv.O()->cls == js::Cls::Host ? PartNode(rt, static_cast<js::HostObj*>(a.thisv.O())) : nullptr;
        if (!n) rt.ThrowType("Illegal invocation");
        WriteStyle(rt, n, LowerAscii(rt.ToUtf8(a[0])), std::string());
        return Value();
    }, 1);
    // add / remove / toggle / contains / replace: slots[0] says which
    struct ClassOp { const char* name; int op; int len; };
    for (const ClassOp& c : {ClassOp{"add", 0, 1}, ClassOp{"remove", 1, 1}, ClassOp{"toggle", 2, 1}, ClassOp{"contains", 3, 1}, ClassOp{"replace", 4, 2}}) {
        js::NativeFunc* f = js::DefNative(r, I.classListProto, c.name, [](Runtime& rt, CallArgs& a) -> Value {
            Node* n = a.thisv.IsObj() && a.thisv.O()->cls == js::Cls::Host ? PartNode(rt, static_cast<js::HostObj*>(a.thisv.O())) : nullptr;
            if (!n) rt.ThrowType("Illegal invocation");
            const int op = (int)a.callee->slots[0].n;
            std::vector<std::string> t = Tokens(AttrOr(*n, "class"));
            auto has = [&](const std::string& s) { return std::find(t.begin(), t.end(), s) != t.end(); };
            auto check = [&](const std::string& s) {
                if (s.empty()) rt.ThrowError(js::ErrorType::SyntaxError, "a class name can not be empty");
                if (s.find_first_of(" \t\n\f\r") != std::string::npos) rt.ThrowError(js::ErrorType::Error, "InvalidCharacterError: a class name has no spaces");
            };
            Value result;
            if (op == 0) { for (int i = 0; i < a.argc; ++i) { const std::string s = rt.ToUtf8(a.argv[i]); check(s); if (!has(s)) t.push_back(s); } }
            else if (op == 1) { for (int i = 0; i < a.argc; ++i) { const std::string s = rt.ToUtf8(a.argv[i]); check(s); t.erase(std::remove(t.begin(), t.end(), s), t.end()); } }
            else if (op == 2) {
                const std::string s = rt.ToUtf8(a[0]);
                check(s);
                const bool on = a.argc > 1 && !a[1].IsUndef() ? rt.ToBoolean(a[1]) : !has(s);
                if (on && !has(s)) t.push_back(s);
                if (!on) t.erase(std::remove(t.begin(), t.end(), s), t.end());
                result = Value::Bool(on);
            } else if (op == 3) return Value::Bool(has(rt.ToUtf8(a[0])));
            else {
                const std::string from = rt.ToUtf8(a[0]), to = rt.ToUtf8(a[1]);
                check(from);
                check(to);
                if (!has(from)) return Value::Bool(false);
                for (std::string& s : t) if (s == from) s = to;
                std::vector<std::string> uniq;
                for (const std::string& s : t) if (std::find(uniq.begin(), uniq.end(), s) == uniq.end()) uniq.push_back(s);
                t.swap(uniq);
                result = Value::Bool(true);
            }
            Of(rt).SetAttrC(rt, *n, "class", Joined(t));
            Of(rt).Changed();
            return result;
        }, c.len);
        f->slots.push_back(Value::Num(c.op));
    }

    // ---- location
    for (const char* m : {"assign", "replace"})
        js::DefNative(r, I.locationProto, m, [](Runtime& rt, CallArgs& a) -> Value { AskToGo(rt, rt.ToUtf8(a[0])); return Value(); }, 1);
    js::DefNative(r, I.locationProto, "reload", [](Runtime& rt, CallArgs&) -> Value { AskToGo(rt, Of(rt).url); return Value(); }, 0);
    js::DefNative(r, I.locationProto, "toString", [](Runtime& rt, CallArgs&) -> Value { return Str(rt, Of(rt).url); }, 0);
    js::HostObj* loc = r.NewHost(I.kLocation, I.locationProto);
    I.locationRef = r.Ref(Value::Obj(loc));

    // ---- window: the global object
    js::Object* g = r.global;
    js::DefValue(r, g, "window", Value::Obj(g), js::kDefault);
    js::DefValue(r, g, "self", Value::Obj(g), js::kDefault);
    js::DefValue(r, g, "document", I.Wrap(I.doc.root.get()), js::kEnumerable);
    js::DefValue(r, g, "location", Value::Obj(loc), js::kEnumerable);
    js::DefNative(r, g, "setTimeout", [](Runtime& rt, CallArgs& a) -> Value { return Value::Num(Of(rt).AddTimer(rt, a, false, false)); }, 2);
    js::DefNative(r, g, "setInterval", [](Runtime& rt, CallArgs& a) -> Value { return Value::Num(Of(rt).AddTimer(rt, a, true, false)); }, 2);
    js::DefNative(r, g, "requestAnimationFrame", [](Runtime& rt, CallArgs& a) -> Value { return Value::Num(Of(rt).AddTimer(rt, a, false, true)); }, 1);
    for (const char* n : {"clearTimeout", "clearInterval", "cancelAnimationFrame"})
        js::DefNative(r, g, n, [](Runtime& rt, CallArgs& a) -> Value {
            const double id = rt.ToNumber(a[0]);
            if (id >= 1 && id <= 2e9) Of(rt).ClearTimer((int)id);
            return Value();
        }, 1);
    js::DefNative(r, g, "addEventListener", [](Runtime& rt, CallArgs& a) -> Value { Of(rt).AddListener(rt, kWindowSlot, a); return Value(); }, 2);
    js::DefNative(r, g, "removeEventListener", [](Runtime& rt, CallArgs& a) -> Value { Of(rt).RemoveListener(rt, kWindowSlot, a); return Value(); }, 2);
    js::DefNative(r, g, "alert", [](Runtime& rt, CallArgs& a) -> Value {   // no dialogs yet: the message goes to the console
        Of(rt).Log(ConsoleLine::Warn, "alert: " + rt.ToUtf8(a[0]));
        return Value();
    }, 1);
    // console.warn / error: the engine's console (its formatting) with the I.level of the line
    const Value con = r.GetGlobal("console");
    if (con.IsObj()) {
        struct LevelFn { const char* name; ConsoleLine::Level level; };
        for (const LevelFn& lf : {LevelFn{"warn", ConsoleLine::Warn}, LevelFn{"error", ConsoleLine::Error}}) {
            const Value orig = r.GetStr(con, lf.name);
            if (!r.IsCallable(orig)) continue;
            // (DefNative would add a second "warn" behind the engine's own; this replaces it)
            js::NativeFunc* f = r.NewNative([](Runtime& rt, CallArgs& a) -> Value {
                Impl& P = Of(rt);
                const ConsoleLine::Level before = P.level;
                P.level = (ConsoleLine::Level)(int)a.callee->slots[1].n;
                rt.Call(a.callee->slots[0], a.thisv, a.argv, a.argc);
                P.level = before;
                return Value();
            }, lf.name, 0);
            r.DefineData(con.O(), r.Intern(lf.name), Value::Obj(f), js::kHidden);
            f->slots.push_back(orig);
            f->slots.push_back(Value::Num((double)lf.level));
        }
    }
}

// ============================================================ PageScripts
PageScripts::PageScripts(HtmlDocument& doc, FormState& forms, const std::string& pageUrl, Hooks hooks)
    : m(new Impl(doc, forms, pageUrl, std::move(hooks))) {
    if (doc.root) { m->nodeCount = CountNodes(doc.root.get()); m->textBytes = TreeBytes(doc.root.get()); }
    Install(*m);
}

PageScripts::~PageScripts() = default;

bool PageScripts::RunScript(const std::string& source, const std::string& chunk) {
    Impl::Entry entry(*m);
    std::string err;
    if (m->rt.Run(source, chunk.empty() ? "inline script " + std::to_string(++m->inlineCount) : chunk, &err)) return true;
    m->Log(ConsoleLine::Error, err);
    return false;
}

void PageScripts::PageLoaded() {
    if (m->loaded) return;
    m->DispatchEvent(m->doc.root.get(), "DOMContentLoaded", true, false);
    m->loaded = true;
    m->DispatchEvent(nullptr, "load", false, false);
}

bool PageScripts::Dispatch(const Node& target, const std::string& type) {
    Node* t = const_cast<Node*>(&target);   // the tab's own tree: the view hands it back as const
    const bool cancelable = type == "click" || type == "submit";
    return m->DispatchEvent(t, type, true, cancelable);
}

void PageScripts::Tick(double ms) {
    if (ms > 0 && ms < 1e9) m->now += ms;
    if (m->timers.empty()) return;
    m->RunTimers();
}

double PageScripts::NextTimerIn() const {
    double best = -1;
    for (const Impl::Timer& t : m->timers) {
        const double in = std::max(0.0, t.due - m->now);
        if (best < 0 || in < best) best = in;
    }
    return best;
}

std::size_t PageScripts::PendingTimers() const { return m->timers.size(); }
const std::vector<ConsoleLine>& PageScripts::Console() const { return m->console; }
void PageScripts::ClearConsole() { m->console.clear(); }
void PageScripts::AddConsole(ConsoleLine::Level level, const std::string& text) { m->Log(level, text); }
std::uint64_t PageScripts::ConsoleCount() const { return m->consoleCount; }
void PageScripts::SetBudget(std::uint64_t instructions) { m->budget = instructions; }
void PageScripts::SetGcStress(bool on) { m->rt.gcStress = on; }

std::size_t PageScripts::RemovedCount() const {
    std::size_t c = 0;
    for (const auto& r : m->removed) c += CountNodes(r.get());
    return c;
}

bool PageScripts::Connected(const Node* n) const { return n && m->InPage(n); }

void PageScripts::FreeRemoved(const std::function<void(const Node&)>& forget, bool force) {
    Impl& I = *m;
    if (I.removed.empty() || I.depth > 0 || (!force && I.removed.size() < kFreeAfter)) return;
    I.rt.Collect();   // wrappers nothing reaches are freed now (onHostFree clears them)
    for (std::size_t i = I.removed.size(); i-- > 0;) {
        Node* root = I.removed[i].get();
        bool held = false;
        Walk(root, true, [&](Node* n) {
            auto it = I.slotOf.find(n);
            if (it != I.slotOf.end() && I.wraps[it->second]) { held = true; return true; }
            return false;
        });
        if (held) continue;
        Walk(root, true, [&](Node* n) {
            if (forget) forget(*n);
            auto it = I.slotOf.find(n);
            if (it != I.slotOf.end()) {
                const std::uint32_t s = it->second;
                I.DropSlotRefs(s);
                I.nodes[s] = nullptr;
                ++I.gens[s];
                I.freeSlots.push_back(s);
                I.slotOf.erase(it);
            }
            --I.nodeCount;
            I.textBytes -= std::min(I.textBytes, BytesOf(*n));
            return false;
        });
        I.removed[i] = std::move(I.removed.back());
        I.removed.pop_back();
    }
}

} // namespace okay::browser
