// Browser (page engine): JavaScript in web pages (see Script.hpp). Written from the WHATWG DOM standard (2.1-2.10 events and
// their dispatch: capture, target and bubble phases, the canceled and stop-propagation flags; 4.2-4.9 nodes, the tree's
// pre-insert / remove steps and their hierarchy checks; 4.9 DOMTokenList for classList) and the HTML standard (8.6 timers,
// 8.1.8.1 event handler attributes and properties, 13.3 fragment serialization, 4.12.1.1 script types), on our own
// JavaScript engine's public API (okay/JavaScript/JavaScript.hpp) and its library helpers (JsInternal.hpp).
// Round 3 adds, from the same standards and the ones Script.hpp names: HTMLCollection (DOM 4.2.10) for children /
// childNodes / getElementsBy*, DOMStringMap (HTML 3.2.6.6) for dataset, Storage (HTML 12.2), History (HTML 7.4.2),
// document.write for parser scripts (HTML 8.4.3), "prepare the script element" for scripts a script adds (HTML 4.12.1.1),
// key / mouse / focus event attributes (UI Events 5.2-5.6), getComputedStyle (CSSOM 9), simple dialogs (HTML 8.8.1), and
// fetch() / Headers / Response (Fetch 5.2-5.5) and XMLHttpRequest, written in JavaScript below (kPrelude) on one native
// request function the tab answers.
#include "okay/Browser/Script.hpp"

#include "okay/Browser/Css.hpp"
#include "okay/Browser/Forms.hpp"
#include "okay/Browser/Storage.hpp"
#include "okay/Browser/Url.hpp"
#include "okay/Core/StringUtil.hpp"
#include "../JavaScript/JsInternal.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

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

bool IsJavaScriptGlobal(const std::string& name) {
    // ECMAScript's global objects and functions (ECMA-262 section 19) and the two HTML adds that pages treat as language
    static const std::unordered_set<std::string> kLanguage = {
        "Intl", "WeakRef", "FinalizationRegistry", "Proxy", "Reflect", "BigInt", "Symbol", "WeakMap", "WeakSet", "Map",
        "Set", "Promise", "globalThis", "Date", "decodeURIComponent", "encodeURIComponent", "decodeURI", "encodeURI",
        "escape", "unescape", "parseInt", "parseFloat", "isNaN", "isFinite", "ArrayBuffer", "SharedArrayBuffer",
        "DataView", "Atomics", "Uint8Array", "Uint8ClampedArray", "Int8Array", "Uint16Array", "Int16Array",
        "Uint32Array", "Int32Array", "Float32Array", "Float64Array", "BigInt64Array", "BigUint64Array",
        "AggregateError", "Iterator", "Object", "Array", "String", "Number", "Math", "JSON", "RegExp", "Error",
        "structuredClone", "queueMicrotask"};
    return kLanguage.count(name) != 0;
}

std::string MissingWebApi(const std::string& line) {
    // Names from the HTML, DOM, CSSOM, Fetch, URL, Encoding and Web IDL standards' global interfaces and functions, and the
    // language's own (IsJavaScriptGlobal).
    static const std::unordered_set<std::string> kWeb = {
        "navigator", "screen", "performance", "crypto", "indexedDB", "caches", "visualViewport", "speechSynthesis",
        "trustedTypes", "devicePixelRatio", "innerWidth", "innerHeight", "outerWidth", "outerHeight", "scrollX",
        "scrollY", "pageXOffset", "pageYOffset", "scrollTo", "scrollBy", "scroll", "open", "postMessage", "atob",
        "btoa", "matchMedia", "getSelection", "requestIdleCallback", "cancelIdleCallback", "customElements",
        "createImageBitmap", "isSecureContext", "origin", "self", "top", "parent", "frames", "opener", "CSS",
        "IntersectionObserver", "ResizeObserver", "MutationObserver", "PerformanceObserver", "ReportingObserver",
        "CustomEvent", "Event", "EventTarget", "KeyboardEvent", "MouseEvent", "PointerEvent", "FocusEvent",
        "InputEvent", "UIEvent", "MessageChannel", "MessagePort", "BroadcastChannel", "Worker", "SharedWorker",
        "WebSocket", "EventSource", "URL", "URLSearchParams", "TextEncoder", "TextDecoder", "AbortController",
        "AbortSignal", "Blob", "File", "FileReader", "FormData", "DOMParser", "XMLSerializer", "Image", "Audio",
        "Option", "Node", "Element", "HTMLElement", "Document", "DocumentFragment", "ShadowRoot", "Range", "Selection",
        "NodeFilter", "NodeList", "HTMLCollection", "DOMRect", "DOMMatrix", "DOMException", "ImageData", "Path2D",
        "OffscreenCanvas", "CanvasRenderingContext2D", "WebGLRenderingContext", "CSSStyleSheet", "Request",
        "Notification", "AudioContext", "MediaSource", "ClipboardItem", "HTMLIFrameElement", "HTMLImageElement",
        "HTMLScriptElement", "HTMLAnchorElement", "SVGElement"};
    auto known = [](const std::string& n) { return kWeb.count(n) != 0 || IsJavaScriptGlobal(n); };
    static const std::unordered_set<std::string> kRoots = {
        "window", "self", "globalThis", "document", "navigator", "location", "history", "screen", "performance", "console",
        "localStorage", "sessionStorage", "crypto", "customElements", "CSS"};
    // DOM and CSSOM members that are called on elements, documents and events.
    static const std::unordered_set<std::string> kMembers = {
        "closest", "matches", "getBoundingClientRect", "getClientRects", "insertAdjacentHTML", "insertAdjacentElement",
        "insertAdjacentText", "replaceWith", "replaceChildren", "before", "after", "prepend", "toggleAttribute", "attachShadow",
        "animate", "scrollIntoView", "scrollTo", "scrollBy", "requestFullscreen", "dispatchEvent", "contains", "getRootNode",
        "compareDocumentPosition", "hasAttribute", "getAttributeNames", "setAttributeNS", "getAttributeNS", "cloneNode",
        "isEqualNode", "normalize", "createDocumentFragment", "createElementNS", "createComment", "createEvent", "createRange",
        "createTreeWalker", "importNode", "adoptNode", "elementFromPoint", "elementsFromPoint", "hasFocus", "getElementsByName",
        "querySelectorAll", "querySelector", "setPointerCapture", "releasePointerCapture", "checkVisibility", "showModal",
        "showPopover", "hidePopover", "togglePopover", "reportValidity", "checkValidity", "setCustomValidity", "select",
        "setSelectionRange", "getContext", "toDataURL", "toBlob", "canPlayType", "getPropertyValue", "setProperty",
        "removeProperty", "composedPath", "initEvent", "initCustomEvent", "namedItem", "sendBeacon"};
    auto quoted = [&](const char* tail, std::string& name) {
        const std::size_t at = line.find(tail);
        if (at == std::string::npos || at == 0 || line[at - 1] != '\'') return false;
        const std::size_t open = line.rfind('\'', at - 2);
        if (open == std::string::npos) return false;
        name = line.substr(open + 1, at - 1 - open - 1);
        return !name.empty() && name.size() < 200;
    };
    std::string name;
    if (quoted(" is not defined", name)) return known(name) ? name : std::string();
    if (quoted(" is undefined, not a function", name) || quoted(" is undefined, not a constructor", name)) {
        const std::size_t dot = name.find('.'), last = name.rfind('.');
        if (dot == std::string::npos) return known(name) ? name : std::string();
        std::string root = name.substr(0, dot);
        if (root == "window" || root == "self" || root == "globalThis") {   // window.X is the global X
            const std::string rest = name.substr(dot + 1);
            if (rest.find('.') == std::string::npos) return known(rest) ? rest : std::string();
        }
        if (kRoots.count(root) || IsJavaScriptGlobal(root)) return name;
        if (kMembers.count(name.substr(last + 1))) return "*." + name.substr(last + 1);   // "*": the object's type is not known here
    }
    return std::string();
}

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
    bool loaded = false, contentLoaded = false;

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

    std::uint32_t kNode = 0, kStyle = 0, kClassList = 0, kLocation = 0, kCollection = 0, kStorage = 0, kDataset = 0;
    js::Object *nodeProto = nullptr, *documentProto = nullptr, *styleProto = nullptr, *classListProto = nullptr,
               *eventProto = nullptr, *locationProto = nullptr, *collectionProto = nullptr, *storageProto = nullptr,
               *datasetProto = nullptr, *computedProto = nullptr, *historyProto = nullptr;
    js::Symbol* filterKey = nullptr;   // a collection's tag or class names

    // ---- round 3
    std::unordered_map<int, int> requests;   // fetch / XMLHttpRequest id -> its callback (a host ref)
    int nextRequest = 1;
    const Node* currentScript = nullptr;
    bool parserScript = false;               // the running script may document.write
    std::string writeBuffer;
    std::vector<const Node*> written;        // <script> elements document.write put in, not taken by the tab yet
    std::unordered_set<const Node*> started; // scripts that ran or are on their way ("already started", HTML 4.12.1)
    const Node* focused = nullptr;
    std::string historyState;                // JSON ("" = null)
    int historyStateRef = -1;                // its parsed value (history.state is the same object each time)
    int storageRef[2] = {-1, -1};            // the Storage objects (session, local)
    int dialogs = 0;                         // opened in this host entry
    std::uint64_t treeVersion = 0;           // goes up with every change (live collections cache their list per version)
    struct CollectionCache { std::uint64_t version = ~0ull; std::vector<Node*> list; };
    std::unordered_map<const js::HostObj*, CollectionCache> collections;
    js::Symbol* ownerKey = nullptr;   // style / classList objects keep their element's wrapper alive through it
    int locationRef = -1;
    int inlineCount = 0;

    // ---- round 4: the interface objects (DOM 4.4-4.11, HTML 3.2.8, UI Events), MutationObserver (DOM 4.3), the view
    js::Object *elementProto = nullptr, *htmlElementProto = nullptr, *svgElementProto = nullptr, *characterDataProto = nullptr,
               *textProto = nullptr, *commentProto = nullptr, *doctypeProto = nullptr, *uiEventProto = nullptr,
               *keyboardEventProto = nullptr, *mouseEventProto = nullptr, *focusEventProto = nullptr, *moProto = nullptr;
    std::unordered_map<std::string, js::Object*> tagProtos;   // "img" -> HTMLImageElement.prototype ...
    struct MoReg {   // one observe() call: the options of MutationObserverInit (DOM 4.3.1)
        int observer = -1;
        bool childList = false, attributes = false, characterData = false, subtree = false, attrOld = false, cdOld = false;
        std::vector<std::string> filter;
    };
    std::unordered_map<const Node*, std::vector<MoReg>> moRegs;   // the nodes' registered observer lists
    struct Mo { int callback = -1, self = -1; std::vector<int> records; bool live = false; };
    std::vector<Mo> mos;                                          // index = the observer's id (in its object under moKey)
    std::size_t moLive = 0;
    bool moScheduled = false;                                     // a delivery microtask is queued
    int moDeliverRef = -1;
    std::uint32_t kObserver = 0;
    // An observer the page can no longer reach and that observes nothing (its object was collected): let it go.
    void ReleaseObserver(std::size_t id) {
        if (id >= mos.size() || !mos[id].live) return;
        for (auto it = moRegs.begin(); it != moRegs.end();) {
            auto& list = it->second;
            list.erase(std::remove_if(list.begin(), list.end(), [id](const MoReg& r) { return r.observer == (int)id; }), list.end());
            if (list.empty()) it = moRegs.erase(it);
            else ++it;
        }
        for (int ref : mos[id].records) rt.Unref(ref);
        if (mos[id].callback >= 0) rt.Unref(mos[id].callback);
        mos[id] = Mo();
        if (moLive) --moLive;
    }
    PageView lastView;
    bool viewKnown = false;
    int viewHookRef = -1;                                         // the prelude's update steps (matchMedia, observers)
    std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
    double timeOrigin = 0;                                        // ms since 1970 when the page's scripts started

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
        if (lv == ConsoleLine::Error) {   // name what our engine lacks (MissingWebApi), for the console and the site check
            const std::string api = MissingWebApi(text);
            if (!api.empty()) Log(ConsoleLine::Warn, "not implemented: " + api);
        }
    }

    // ---- host entries: one budget each
    struct Entry {
        Impl& I;
        explicit Entry(Impl& i) : I(i) {
            if (I.depth++ == 0) { I.rt.SetBudget(I.budget); I.level = ConsoleLine::Log; I.dialogs = 0; }
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
    // The prototype a node's wrapper gets: its interface (DOM 4.4-4.11; HTML 3.2.8 "element interfaces"), so instanceof works.
    js::Object* ProtoFor(const Node& n) const {
        switch (n.kind) {
        case NodeKind::Document: return documentProto;
        case NodeKind::Text: return textProto ? textProto : nodeProto;
        case NodeKind::Comment: return commentProto ? commentProto : nodeProto;
        case NodeKind::Doctype: return doctypeProto ? doctypeProto : nodeProto;
        default: break;
        }
        auto it = tagProtos.find(n.name);
        if (it != tagProtos.end()) return it->second;
        return htmlElementProto ? htmlElementProto : nodeProto;
    }
    js::Value Wrap(Node* n) {
        if (!n) return js::Value::Null();
        const std::uint32_t s = SlotFor(n);
        if (wraps[s]) return js::Value::Obj(wraps[s]);
        js::HostObj* h = rt.NewHost(kNode, ProtoFor(*n));
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
    void Changed() { ++treeVersion; if (hooks.changed) hooks.changed(); }
    void StylesChanged() { if (hooks.styles) hooks.styles(); }
    static bool IsStyleNode(const Node* n) {
        if (!n || n->kind != NodeKind::Element) return false;
        if (n->name == "style") return true;
        if (n->name != "link") return false;
        const std::string* rel = n->Attr("rel");
        return rel && LowerAscii(*rel).find("stylesheet") != std::string::npos;
    }
    // Does the tree at `n` hold a <style> or a style sheet link? (charged: one step per node)
    bool HasStyleNode(js::Runtime& r, Node* n) {
        bool found = false;
        std::size_t seen = 0;
        Walk(n, true, [&](Node* x) { ++seen; if (IsStyleNode(x)) { found = true; return true; } return false; });
        r.Charge(seen);
        return found;
    }
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
        if (moRegs.empty()) { SetAttr(n, name, value); return; }
        const bool had = old != nullptr;
        const std::string was = had ? *old : std::string();
        SetAttr(n, name, value);
        QueueMutation(1, &n, name, had ? &was : nullptr, {}, {}, nullptr, nullptr);
    }
    void SetData(js::Runtime& r, Node& n, const std::string& text) {
        Account(r, text.size(), n.data.size());
        if (!moRegs.empty() && (n.kind == NodeKind::Text || n.kind == NodeKind::Comment)) {
            const std::string was = n.data;
            n.data = text;
            QueueMutation(2, &n, std::string(), &was, {}, {}, nullptr, nullptr);
            return;
        }
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

    // ---- MutationObserver (DOM 4.3 "queue a mutation record"): type 0 childList, 1 attributes, 2 characterData
    struct Interest { int obs; bool old; };
    void InterestedIn(const Node* target, int type, const std::string& attr, std::vector<Interest>& out) {
        for (const Node* n = target; n; n = n->parent) {
            auto it = moRegs.find(n);
            if (it == moRegs.end()) continue;
            for (const MoReg& r : it->second) {
                if (n != target && !r.subtree) continue;
                if (type == 0 && !r.childList) continue;
                if (type == 1 && (!r.attributes || (!r.filter.empty() && std::find(r.filter.begin(), r.filter.end(), attr) == r.filter.end()))) continue;
                if (type == 2 && !r.characterData) continue;
                const bool old = type == 1 ? r.attrOld : type == 2 ? r.cdOld : false;
                auto f = std::find_if(out.begin(), out.end(), [&](const Interest& x) { return x.obs == r.observer; });
                if (f != out.end()) f->old = f->old || old;
                else out.push_back({r.observer, old});
            }
        }
    }
    static void SiblingsOf(const Node* n, Node*& prev, Node*& next) {
        prev = next = nullptr;
        const Node* p = n->parent;
        if (!p) return;
        for (std::size_t i = 0; i < p->children.size(); ++i)
            if (p->children[i].get() == n) {
                if (i > 0) prev = p->children[i - 1].get();
                if (i + 1 < p->children.size()) next = p->children[i + 1].get();
                return;
            }
    }
    void QueueMutation(int type, Node* target, const std::string& attr, const std::string* oldValue, const std::vector<Node*>& added,
                       const std::vector<Node*>& gone, Node* prev, Node* next) {
        if (moRegs.empty() || !target) return;
        std::vector<Interest> who;
        InterestedIn(target, type, attr, who);
        if (who.empty()) return;
        static const char* const kTypes[] = {"childList", "attributes", "characterData"};
        for (const Interest& w : who) {
            if (w.obs < 0 || (std::size_t)w.obs >= mos.size() || !mos[(std::size_t)w.obs].live) continue;
            if (mos[(std::size_t)w.obs].records.size() >= kMaxMutationRecords) continue;
            js::Object* rec = rt.NewObject();
            const int ref = rt.Ref(js::Value::Obj(rec));
            mos[(std::size_t)w.obs].records.push_back(ref);
            auto put = [&](const char* k, const js::Value& v) { rt.DefineData(rec, rt.Intern(k), v); };
            put("type", js::Value::Str(rt.NewStringAscii(kTypes[type])));
            put("target", Wrap(target));
            put("addedNodes", Array(added));
            put("removedNodes", Array(gone));
            put("previousSibling", Wrap(prev));
            put("nextSibling", Wrap(next));
            put("attributeName", type == 1 ? js::Value::Str(rt.NewStringUtf8(attr)) : js::Value::Null());
            put("attributeNamespace", js::Value::Null());
            put("oldValue", w.old && oldValue ? js::Value::Str(rt.NewStringUtf8(*oldValue)) : js::Value::Null());
        }
        if (!moScheduled && moDeliverRef >= 0) {   // "queue a mutation observer microtask"
            moScheduled = true;
            js::Job j;
            j.kind = 0;
            j.a = rt.GetRef(moDeliverRef);
            rt.vm->jobs.push_back(j);
        }
    }
    void ChildRemoved(Node* n) {   // before `n` leaves its parent
        if (moRegs.empty() || !n->parent) return;
        Node *prev, *next;
        SiblingsOf(n, prev, next);
        QueueMutation(0, n->parent, std::string(), nullptr, {}, {n}, prev, next);
    }
    void ChildAdded(Node* n) {     // after `n` was put into its parent
        if (moRegs.empty() || !n->parent) return;
        Node *prev, *next;
        SiblingsOf(n, prev, next);
        QueueMutation(0, n->parent, std::string(), nullptr, {n}, {}, prev, next);
    }
    bool RemoveAttrC(Node& n, const std::string& name) {
        const std::string* old = n.Attr(name.c_str());
        if (!old) return false;
        const std::string was = *old;
        RemoveAttr(n, name);
        QueueMutation(1, &n, name, &was, {}, {}, nullptr, nullptr);
        return true;
    }
    // The box of `n` in the last layout (Hooks::box), in view coordinates when `view` (CSSOM View 6.1 getBoundingClientRect).
    bool BoxOf(const Node* n, PageRect& r, bool view) {
        if (!n || n->kind != NodeKind::Element || !hooks.box || !InPage(n) || !hooks.box(*n, r)) return false;
        if (view && !r.fixed && hooks.view) { const PageView v = hooks.view(); r.x -= v.scrollX; r.y -= v.scrollY; }
        return true;
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
        ChildRemoved(n);
        const bool styles = InPage(n) && HasStyleNode(rt, n);
        std::unique_ptr<Node> t = Take(n);
        if (t) removed.push_back(std::move(t));
        Changed();
        if (styles) StylesChanged();
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
        ChildRemoved(child);
        std::unique_ptr<Node> t = Take(child);
        if (!t) r.ThrowError(js::ErrorType::Error, "NotFoundError: the node is not in this page");
        t->parent = parent;
        std::size_t at = parent->children.size();
        if (before) for (std::size_t i = 0; i < parent->children.size(); ++i) if (parent->children[i].get() == before) { at = i; break; }
        parent->children.insert(parent->children.begin() + (std::ptrdiff_t)at, std::move(t));
        ChildAdded(child);
        Changed();
        AfterInsert(r, child);
    }
    // HTML 4.12.1.1 "prepare the script element" for scripts a script put into the page: an inline one runs now, a src one
    // is fetched by the tab and runs when it arrives. A <style> or style sheet link tells the tab to read the sheets again.
    void AfterInsert(js::Runtime& r, Node* child) {
        if (!InPage(child)) return;
        std::vector<Node*> scripts;
        bool styles = false;
        std::size_t seen = 0;
        Walk(child, true, [&](Node* n) {
            ++seen;
            if (n->kind == NodeKind::Element) {
                if (n->name == "script" && !started.count(n)) scripts.push_back(n);
                else if (IsStyleNode(n)) styles = true;
            }
            return false;
        });
        r.Charge(seen);
        if (styles) StylesChanged();
        for (Node* sc : scripts) {
            if (started.count(sc) || !InPage(sc)) continue;
            const std::string* type = sc->Attr("type");
            if (!IsJavaScriptType(type ? *type : std::string())) continue;
            if (const std::string* src = sc->Attr("src")) {
                started.insert(sc);
                if (hooks.loadScript && !src->empty()) hooks.loadScript(*sc, WithoutFragment(ResolveUrl(url, *src)));
                continue;
            }
            const std::string text = sc->TextContent();
            if (text.empty()) continue;   // an empty inline script is not started: text added later runs it (not done yet)
            started.insert(sc);
            const Node* prev = currentScript;
            const bool prevParser = parserScript;
            currentScript = sc;
            parserScript = false;
            std::string err;
            if (!rt.Run(text, "inline script " + std::to_string(++inlineCount), &err)) Log(ConsoleLine::Error, err);
            currentScript = prev;
            parserScript = prevParser;
        }
    }
    void RemoveChildren(Node* el) {
        rt.Charge(el->children.size());
        if (!moRegs.empty() && !el->children.empty()) {
            std::vector<Node*> gone;
            for (auto& c : el->children) gone.push_back(c.get());
            QueueMutation(0, el, std::string(), nullptr, {}, gone, nullptr, nullptr);
        }
        while (!el->children.empty()) {
            std::unique_ptr<Node> t = std::move(el->children.back());
            el->children.pop_back();
            t->parent = nullptr;
            removed.push_back(std::move(t));
        }
    }
    void SetText(js::Runtime& r, Node* el, const std::string& text) {
        if (IsStyleNode(el) || (el->parent && IsStyleNode(el->parent))) StylesChanged();
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
        bool styles = IsStyleNode(el);
        if (body)
            for (auto& c : body->children) {
                // scripts put in by innerHTML never run (HTML 8.4.3: they are marked "already started")
                Walk(c.get(), true, [&](Node* n) { if (n->IsElement("script")) started.insert(n); else if (IsStyleNode(n)) styles = true; return false; });
                c->parent = el;
                el->children.push_back(std::move(c));
            }
        if (!moRegs.empty() && !el->children.empty()) {
            std::vector<Node*> added;
            for (auto& c : el->children) added.push_back(c.get());
            QueueMutation(0, el, std::string(), nullptr, added, {}, nullptr, nullptr);
        }
        nodeCount += count;
        Changed();
        if (styles && InPage(el)) StylesChanged();
    }

    // ---- document.write (HTML 8.4.3, for the page's own scripts): the text is read when the script ends and its nodes go
    // right after the script element; <script> elements in it are handed to the tab to run next.
    void FlushWrite(const Node* element) {
        std::string text;
        text.swap(writeBuffer);
        if (text.empty() || !element) return;
        Node* el = const_cast<Node*>(element);
        Node* parent = el->parent;
        if (!parent || !InPage(el)) return;
        HtmlLimits lim;
        lim.maxDepth = std::max(1, lim.maxDepth - DepthOf(parent) - 3);
        HtmlDocument frag = ParseHtml("<!DOCTYPE html><body>" + text, lim);
        Node* body = const_cast<Node*>(frag.Body());
        if (!body) return;
        std::size_t count = 0, bytes = 0;
        for (auto& c : body->children) { count += CountNodes(c.get()); bytes += TreeBytes(c.get()); }
        if (nodeCount + count > kMaxNodes || textBytes + bytes > kMaxPageTextBytes) {
            Log(ConsoleLine::Error, "document.write: the page would be too big; the text was not added");
            return;
        }
        std::size_t at = 0;
        while (at < parent->children.size() && parent->children[at].get() != el) ++at;
        ++at;
        bool styles = false;
        for (auto& c : body->children) {
            Walk(c.get(), true, [&](Node* n) {
                if (n->IsElement("script") && !started.count(n)) {
                    const std::string* type = n->Attr("type");
                    started.insert(n);
                    if (IsJavaScriptType(type ? *type : std::string())) written.push_back(n);
                } else if (IsStyleNode(n)) styles = true;
                return false;
            });
            c->parent = parent;
            Node* put = c.get();
            parent->children.insert(parent->children.begin() + (std::ptrdiff_t)at++, std::move(c));
            try { ChildAdded(put); }   // outside a script: a full heap drops the record, nothing else
            catch (const js::JsThrow&) { rt.exception = js::Value(); }
            catch (const js::JsTerminate&) {}
        }
        nodeCount += count;
        textBytes += bytes;
        Changed();
        if (styles) StylesChanged();
    }

    // ---- live collections (DOM 4.2.10): kind 0 children, 1 childNodes, 2 by tag name, 3 by class names
    std::vector<Node*>& CollectionList(js::Runtime& r, js::HostObj* h) {
        static std::vector<Node*> none;
        none.clear();
        Node* root = NodeOf(h);
        if (!root) return none;
        if (collections.size() > 64 && !collections.count(h)) collections.clear();   // a small cache: loops use one or two
        CollectionCache& c = collections[h];
        if (c.version == treeVersion) return c.list;
        c.list.clear();
        c.version = treeVersion;
        const int kind = h->i32;
        if (kind <= 1) {
            r.Charge(root->children.size() / 4 + 1);
            for (auto& ch : root->children) if (kind == 1 || ch->kind == NodeKind::Element) c.list.push_back(ch.get());
            return c.list;
        }
        std::string filter;
        if (const js::Prop* pr = h->FindRaw(filterKey)) if (pr->v.IsStr()) filter = js::Utf16To8(pr->v.S()->s);
        const std::vector<std::string> want = kind == 3 ? Tokens(filter) : std::vector<std::string>();
        if (kind == 3 && want.empty()) return c.list;
        std::size_t seen = 0;
        Walk(root, false, [&](Node* n) {
            ++seen;
            if (n->kind != NodeKind::Element) return false;
            if (kind == 2) { if (filter == "*" || n->name == filter) c.list.push_back(n); return false; }
            const std::vector<std::string> has = Tokens(AttrOr(*n, "class"));
            for (const std::string& w : want) if (std::find(has.begin(), has.end(), w) == has.end()) return false;
            c.list.push_back(n);
            return false;
        });
        r.Charge(seen);
        return c.list;
    }
    js::Value Collection(Node* root, int kind, const std::string& filter) {
        const js::Value owner = Wrap(root);
        js::HostObj* h = rt.NewHost(kCollection, collectionProto);
        h->id = slotOf[root];
        h->u32 = gens[h->id];
        h->i32 = kind;
        rt.DefineData(h, ownerKey, owner, 0);
        if (kind >= 2) rt.DefineData(h, filterKey, js::Value::Str(rt.NewStringUtf8(filter)), 0);
        return js::Value::Obj(h);
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
    // `given`: an event a script made (new Event(...), dispatchEvent): it is sent as it is (isTrusted false) and its own type,
    // bubbles and cancelable are used. Otherwise a new event of interface `proto` (null: Event) is made.
    bool DispatchEvent(Node* target, const std::string& type, bool bubbles, bool cancelable,
                       const std::function<void(js::Object*)>& extra = nullptr, js::Object* given = nullptr, js::Object* proto = nullptr) {
        if ((int)dispatching.size() >= kMaxEventDepth) {
            Log(ConsoleLine::Warn, "the event '" + type + "' was not sent: too many events inside each other");
            return false;
        }
        Entry entry(*this);
        std::vector<Node*> path;
        for (Node* p = target; p; p = p->parent) path.push_back(p);
        const bool toWindow = !path.empty() && path.back() == doc.root.get();
        js::Object* ev = given ? given : rt.NewObject(proto ? proto : eventProto);
        const int evRef = rt.Ref(js::Value::Obj(ev));
        auto put = [&](const char* k, const js::Value& v) { rt.DefineData(ev, rt.Intern(k), v); };
        if (!given) {
            put("type", js::Value::Str(rt.NewStringUtf8(type)));
            put("bubbles", js::Value::Bool(bubbles));
            put("cancelable", js::Value::Bool(cancelable));
            put("defaultPrevented", js::Value::Bool(false));
            put("isTrusted", js::Value::Bool(true));
            put("timeStamp", js::Value::Num(now));
        }
        put("target", target ? Wrap(target) : js::Value::Obj(rt.global));
        if (extra) {
            try { extra(ev); }
            catch (const js::JsThrow&) { rt.exception = js::Value(); rt.Unref(evRef); return false; }
            catch (const js::JsTerminate& t) { Log(ConsoleLine::Error, t.why); rt.Unref(evRef); return false; }
        }
        Dispatching d;
        d.ev = ev;
        d.cancelable = cancelable;
        if (given) d.prevented = rt.ToBoolean(rt.GetStr(js::Value::Obj(ev), "defaultPrevented"));
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

    // ---- focus (UI Events 5.2.4: blur, focusout at the old element; focus, focusin at the new one)
    void FocusMoved(const Node* from, const Node* to) {
        focused = to;
        auto related = [this](const Node* other) {
            return [this, other](js::Object* ev) { rt.DefineData(ev, rt.Intern("relatedTarget"), Wrap(const_cast<Node*>(other))); };
        };
        if (from && InPage(from)) {
            DispatchEvent(const_cast<Node*>(from), "blur", false, false, related(to), nullptr, focusEventProto);
            DispatchEvent(const_cast<Node*>(from), "focusout", true, false, related(to), nullptr, focusEventProto);
        }
        if (to && InPage(to) && focused == to) {
            DispatchEvent(const_cast<Node*>(to), "focus", false, false, related(from), nullptr, focusEventProto);
            DispatchEvent(const_cast<Node*>(to), "focusin", true, false, related(from), nullptr, focusEventProto);
        }
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
    if (k == "childNodes" || k == "children") { out = I.Collection(n, k == "children" ? 0 : 1, std::string()); return true; }
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
        if (k == "activeElement") {
            out = I.focused && I.InPage(I.focused) ? I.Wrap(const_cast<Node*>(I.focused)) : I.Wrap(child("body"));
            return true;
        }
        if (k == "cookie") { out = Str(rt, I.hooks.getCookie ? I.hooks.getCookie() : std::string()); return true; }
        if (k == "currentScript") { out = I.Wrap(const_cast<Node*>(I.currentScript)); return true; }
        // ---- round 4 (HTML 3.1.3 document metadata, 6.2 page visibility; CSSOM View 7 scrollingElement)
        if (k == "hidden") { out = js::Value::Bool(false); return true; }
        if (k == "visibilityState") { out = Str(rt, "visible"); return true; }
        if (k == "scrollingElement") { out = I.Wrap(const_cast<Node*>(I.doc.Html())); return true; }
        if (k == "referrer") { out = Str(rt, std::string()); return true; }
        if (k == "characterSet" || k == "charset" || k == "inputEncoding") { out = Str(rt, "UTF-8"); return true; }
        if (k == "compatMode") { out = Str(rt, I.doc.quirks ? "BackCompat" : "CSS1Compat"); return true; }
        if (k == "contentType") { out = Str(rt, "text/html"); return true; }
        if (k == "domain") { out = Str(rt, HostOf(I.url)); return true; }
        if (k == "dir") { out = Str(rt, I.doc.Html() ? AttrOr(*I.doc.Html(), "dir") : std::string()); return true; }
        if (k == "images" || k == "scripts" || k == "forms" || k == "embeds" || k == "links") {   // HTML 3.1.3 (live collections)
            static const std::pair<const char*, const char*> kTags[] = {{"images", "img"}, {"scripts", "script"}, {"forms", "form"}, {"embeds", "embed"}, {"links", "a"}};
            for (const auto& t : kTags) if (k == t.first) out = I.Collection(n, 2, t.second);
            return true;
        }
        return false;
    }
    if (!el) return false;
    // ---- round 4: the element's box in the last layout (CSSOM View 7.1 offset*, 7.2 client* and scroll*)
    if (k.size() > 6 && (k.compare(0, 6, "offset") == 0 || k.compare(0, 6, "client") == 0 || k.compare(0, 6, "scroll") == 0)) {
        const bool root = n == I.doc.Html() || n == I.doc.Body();
        PageView v;
        if (I.hooks.view) v = I.hooks.view();
        PageRect b;
        const bool has = I.BoxOf(n, b, false);
        double val = -1;
        if (k == "offsetParent") {
            const Node* body = I.doc.Body();
            out = !has || b.fixed || root || !body ? js::Value::Null() : I.Wrap(const_cast<Node*>(body));
            return true;
        }
        if (k == "offsetWidth") val = has ? b.w : 0;
        else if (k == "offsetHeight") val = has ? b.h : 0;
        else if (k == "offsetTop") val = has ? b.y : 0;
        else if (k == "offsetLeft") val = has ? b.x : 0;
        else if (k == "clientWidth") val = n == I.doc.Html() ? v.width : has ? std::max(0.0f, b.w - b.border[1] - b.border[3]) : 0;
        else if (k == "clientHeight") val = n == I.doc.Html() ? v.height : has ? std::max(0.0f, b.h - b.border[0] - b.border[2]) : 0;
        else if (k == "clientTop") val = has ? b.border[0] : 0;
        else if (k == "clientLeft") val = has ? b.border[3] : 0;
        else if (k == "scrollWidth") val = root ? std::max(v.width, v.pageWidth) : has ? std::max(0.0f, b.w - b.border[1] - b.border[3]) : 0;
        else if (k == "scrollHeight") val = root ? std::max(v.height, v.pageHeight) : has ? std::max(0.0f, b.h - b.border[0] - b.border[2]) : 0;
        else if (k == "scrollTop") val = root ? v.scrollY : 0;
        else if (k == "scrollLeft") val = root ? v.scrollX : 0;
        if (val >= 0) { out = js::Value::Num(std::round(val)); return true; }   // the offset* and client* values are whole numbers
    }
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
    if (k == "dataset") { out = I.Part(I.kDataset, I.datasetProto, n); return true; }
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
        if (k == "cookie") {
            const std::string text = rt.ToUtf8(v);
            rt.Charge(text.size() / 16);
            if (I.hooks.setCookie && text.size() <= 8192) I.hooks.setCookie(text);
            return true;
        }
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
        else I.RemoveAttrC(*n, LowerAscii(k));
        I.Changed();
        return true;
    }
    if (IsHandlerName(k)) { I.SetOnProp(rt, I.SlotFor(n), k.substr(2), v); return true; }
    if (k == "style" || k == "classList") {   // el.style = "color: red" sets the attribute (PutForwards)
        return attr(k == "style" ? "style" : "class");
    }
    if ((k == "scrollTop" || k == "scrollLeft") && (n == I.doc.Html() || n == I.doc.Body())) {   // the page's own scroll
        if (I.hooks.scrollTo && I.hooks.view) {
            const PageView pv = I.hooks.view();
            const double to = rt.ToNumber(v);
            if (std::isfinite(to)) I.hooks.scrollTo(k == "scrollLeft" ? (float)to : pv.scrollX, k == "scrollTop" ? (float)to : pv.scrollY);
        }
        return true;
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

// ---- element.dataset (HTML 3.2.6.6 DOMStringMap): fooBar <-> the data-foo-bar attribute
std::string DataAttrName(const std::string& camel) {
    std::string out = "data-";
    for (char c : camel) {
        if (c >= 'A' && c <= 'Z') { out += '-'; out += (char)(c + 32); }
        else out += c;
    }
    return out;
}
std::string DataPropName(const std::string& attr) {   // "data-foo-bar" -> "fooBar"
    std::string camel;
    for (std::size_t i = 5; i < attr.size(); ++i) {
        if (attr[i] == '-' && i + 1 < attr.size() && attr[i + 1] >= 'a' && attr[i + 1] <= 'z') { camel += (char)(attr[++i] - 32); continue; }
        camel += attr[i];
    }
    return camel;
}
bool DatasetGet(js::Runtime& rt, js::HostObj* self, js::String* keyS, js::Value& out) {
    Node* n = PartNode(rt, self);
    if (!n) return false;
    const std::string* v = n->Attr(DataAttrName(js::Utf16To8(keyS->s)).c_str());
    if (!v) return false;
    out = Str(rt, *v);
    return true;
}
bool DatasetSet(js::Runtime& rt, js::HostObj* self, js::String* keyS, const js::Value& v) {
    Node* n = PartNode(rt, self);
    if (!n) return false;
    const std::string k = js::Utf16To8(keyS->s);
    for (std::size_t i = 0; i + 1 < k.size(); ++i)   // HTML 3.2.6.6: "-" followed by a lower-case letter is a SyntaxError
        if (k[i] == '-' && k[i + 1] >= 'a' && k[i + 1] <= 'z') rt.ThrowError(js::ErrorType::SyntaxError, "'" + k + "' is not a dataset name");
    const std::string name = DataAttrName(k);
    if (name.size() > HtmlLimits().maxNameBytes || name.find_first_of(" \t\n\f\r/>=\"'") != std::string::npos)
        rt.ThrowError(js::ErrorType::Error, "InvalidCharacterError: '" + name + "' is not an attribute name");
    std::string value = rt.ToUtf8(v);
    if (value.size() > HtmlLimits().maxAttrValueBytes) value.resize(HtmlLimits().maxAttrValueBytes);
    Of(rt).SetAttrC(rt, *n, name, value);
    Of(rt).Changed();
    return true;
}
std::vector<std::string> DatasetKeys(js::Runtime& rt, js::HostObj* self) {
    std::vector<std::string> out;
    if (Node* n = PartNode(rt, self))
        for (const auto& a : n->attrs) if (a.first.compare(0, 5, "data-") == 0) out.push_back(DataPropName(a.first));
    return out;
}

// ---- HTMLCollection / NodeList (live)
bool IsIndexKey(const std::string& k, std::size_t& out) {
    if (k.empty() || k.size() > 9 || (k.size() > 1 && k[0] == '0')) return false;
    std::size_t v = 0;
    for (char c : k) { if (c < '0' || c > '9') return false; v = v * 10 + (std::size_t)(c - '0'); }
    out = v;
    return true;
}
bool CollectionGet(js::Runtime& rt, js::HostObj* self, js::String* keyS, js::Value& out) {
    const std::string k = js::Utf16To8(keyS->s);
    std::size_t i = 0;
    if (k == "length") { out = js::Value::Num((double)Of(rt).CollectionList(rt, self).size()); return true; }
    if (IsIndexKey(k, i)) {
        std::vector<Node*>& list = Of(rt).CollectionList(rt, self);
        if (i >= list.size()) return false;
        out = Of(rt).Wrap(list[i]);
        return true;
    }
    return false;
}
std::vector<std::string> CollectionKeys(js::Runtime& rt, js::HostObj* self) {
    std::vector<std::string> out;
    const std::size_t n = Of(rt).CollectionList(rt, self).size();
    for (std::size_t i = 0; i < n; ++i) out.push_back(std::to_string(i));
    return out;
}
js::HostObj* ThisCollection(js::Runtime& rt, js::CallArgs& a) {
    if (!a.thisv.IsObj() || a.thisv.O()->cls != js::Cls::Host || static_cast<js::HostObj*>(a.thisv.O())->hostClass != Of(rt).kCollection)
        rt.ThrowType("Illegal invocation: 'this' is not a collection");
    return static_cast<js::HostObj*>(a.thisv.O());
}

// ---- localStorage / sessionStorage (HTML 12.2.1): i32 1 = local, 0 = session
const char* const kStorageMethods[] = {"getItem", "setItem", "removeItem", "clear", "key", nullptr};
bool IsStorageMethod(const std::string& k) {
    for (const char* const* m = kStorageMethods; *m; ++m) if (k == *m) return true;
    return false;
}
StorageArea* AreaOf(js::Runtime& rt, const js::HostObj* self) {
    Impl& I = Of(rt);
    return I.hooks.storage ? I.hooks.storage(self->i32 == 1) : nullptr;
}
void ThrowDom(js::Runtime& rt, const char* name, const std::string& msg) {
    js::Object* e = rt.NewError(js::ErrorType::Error, msg);
    rt.DefineData(e, rt.Intern("name"), Str(rt, name), js::kHidden);
    rt.Throw(js::Value::Obj(e));
}
void StorageWrite(js::Runtime& rt, const js::HostObj* self, const std::string& key, const std::string& value) {
    StorageArea* area = AreaOf(rt, self);
    if (!area) ThrowDom(rt, "SecurityError", "storage is not available in this page");
    rt.Charge((key.size() + value.size()) / 64);
    std::string old;
    if (area->Get(key, old) && old == value) return;
    if (!area->Set(key, value, Of(rt).hooks.storageQuota))
        ThrowDom(rt, "QuotaExceededError", "the site's storage is full (" + std::to_string(Of(rt).hooks.storageQuota >> 20) + " MB)");
    if (self->i32 == 1 && Of(rt).hooks.storageChanged) Of(rt).hooks.storageChanged();
}
bool StorageGet(js::Runtime& rt, js::HostObj* self, js::String* keyS, js::Value& out) {
    const std::string k = js::Utf16To8(keyS->s);
    StorageArea* area = AreaOf(rt, self);
    if (!area) return false;
    if (k == "length") { out = js::Value::Num((double)area->Length()); return true; }
    if (IsStorageMethod(k)) return false;
    std::string v;
    if (!area->Get(k, v)) return false;
    out = Str(rt, v);
    return true;
}
bool StorageSet(js::Runtime& rt, js::HostObj* self, js::String* keyS, const js::Value& v) {
    const std::string k = js::Utf16To8(keyS->s);
    if (IsStorageMethod(k) || k == "length") return false;
    StorageWrite(rt, self, k, rt.ToUtf8(v));
    return true;
}
std::vector<std::string> StorageKeys(js::Runtime& rt, js::HostObj* self) {
    StorageArea* area = AreaOf(rt, self);
    return area ? area->Keys() : std::vector<std::string>();
}
js::HostObj* ThisStorage(js::Runtime& rt, js::CallArgs& a) {
    if (!a.thisv.IsObj() || a.thisv.O()->cls != js::Cls::Host || static_cast<js::HostObj*>(a.thisv.O())->hostClass != Of(rt).kStorage)
        rt.ThrowType("Illegal invocation: 'this' is not a Storage");
    return static_cast<js::HostObj*>(a.thisv.O());
}

// ---- getComputedStyle (CSSOM 9): the values of a ComputedStyle as CSS text
std::string ColorText(const Color& c) {
    if (c.a == 255) return "rgb(" + std::to_string(c.r) + ", " + std::to_string(c.g) + ", " + std::to_string(c.b) + ")";
    char a[16];
    std::snprintf(a, sizeof a, "%g", std::round(c.a / 255.0 * 1000.0) / 1000.0);
    return "rgba(" + std::to_string(c.r) + ", " + std::to_string(c.g) + ", " + std::to_string(c.b) + ", " + a + ")";
}
std::string PxText(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%gpx", std::round(v * 100.0f) / 100.0f);
    return b;
}
std::string LengthText(const Length& l) {
    switch (l.unit) {
    case Length::Px: return PxText(l.value);
    case Length::Percent: { char b[32]; std::snprintf(b, sizeof b, "%g%%", l.value); return b; }
    case Length::None: return "none";
    case Length::Calc: { char b[64]; std::snprintf(b, sizeof b, "calc(%g%% + %gpx)", l.pct, l.value); return b; }
    default: return "auto";
    }
}
std::vector<std::pair<std::string, std::string>> StyleTexts(const ComputedStyle& st) {
    static const char* const kDisplay[] = {"inline", "block", "list-item", "none", "table", "table-row", "table-cell", "table-row-group",
                                           "inline-block", "flow-root", "flex", "inline-flex", "grid", "inline-grid"};
    static const char* const kAlign[] = {"left", "right", "center", "justify"};
    static const char* const kWhite[] = {"normal", "pre", "nowrap", "pre-wrap", "pre-line"};
    static const char* const kPos[] = {"static", "relative", "absolute", "fixed", "sticky"};
    static const char* const kOverflow[] = {"visible", "hidden", "clip", "scroll", "auto"};
    static const char* const kFloat[] = {"none", "left", "right"};
    static const char* const kSides[] = {"top", "right", "bottom", "left"};
    std::vector<std::pair<std::string, std::string>> o;
    o.emplace_back("display", kDisplay[(int)st.display]);
    o.emplace_back("color", ColorText(st.color));
    o.emplace_back("background-color", ColorText(st.background));
    o.emplace_back("font-size", PxText(st.fontSize));
    o.emplace_back("font-weight", std::to_string(st.weight));
    o.emplace_back("font-style", st.italic ? "italic" : "normal");
    o.emplace_back("font-family", st.family);
    o.emplace_back("visibility", st.visible ? "visible" : "hidden");
    { char b[32]; std::snprintf(b, sizeof b, "%g", std::round(st.opacity * 1000.0f) / 1000.0f); o.emplace_back("opacity", b); }
    o.emplace_back("position", kPos[(int)st.position]);
    o.emplace_back("float", kFloat[(int)st.floating]);
    o.emplace_back("text-align", kAlign[(int)st.textAlign]);
    o.emplace_back("white-space", kWhite[(int)st.whiteSpace]);
    o.emplace_back("overflow-x", kOverflow[(int)st.overflowX]);
    o.emplace_back("overflow-y", kOverflow[(int)st.overflowY]);
    o.emplace_back("width", LengthText(st.width));
    o.emplace_back("height", LengthText(st.height));
    o.emplace_back("line-height", st.lineHeight < 0 ? std::string("normal") : PxText(st.lineHeight));
    o.emplace_back("z-index", st.zAuto ? std::string("auto") : std::to_string(st.zIndex));
    for (int i = 0; i < 4; ++i) {
        o.emplace_back(std::string("margin-") + kSides[i], LengthText(st.margin[i]));
        o.emplace_back(std::string("padding-") + kSides[i], LengthText(st.padding[i]));
        o.emplace_back(std::string("border-") + kSides[i] + "-width", PxText(st.border[i]));
        o.emplace_back(std::string("border-") + kSides[i] + "-color", ColorText(st.borderColor[i]));
        o.emplace_back(kSides[i], LengthText(st.inset[i]));
    }
    return o;
}
std::string CamelName(const std::string& css) {   // "background-color" -> "backgroundColor"
    std::string out;
    for (std::size_t i = 0; i < css.size(); ++i) {
        if (css[i] == '-' && i + 1 < css.size()) { out += (char)(css[i + 1] >= 'a' && css[i + 1] <= 'z' ? css[i + 1] - 32 : css[i + 1]); ++i; }
        else out += css[i];
    }
    return out;
}

// ---- history (HTML 7.4.2)
std::string JsonOf(js::Runtime& rt, const js::Value& v) {
    const js::Value json = rt.GetGlobal("JSON");
    const js::Value fn = json.IsObj() ? rt.GetStr(json, "stringify") : js::Value();
    if (!rt.IsCallable(fn)) return "null";
    const js::Value r = rt.Call(fn, json, {v});
    return r.IsStr() ? rt.ToUtf8(r) : std::string("null");
}
js::Value ParseJson(js::Runtime& rt, const std::string& text) {
    if (text.empty()) return js::Value::Null();
    const js::Value json = rt.GetGlobal("JSON");
    const js::Value fn = json.IsObj() ? rt.GetStr(json, "parse") : js::Value();
    if (!rt.IsCallable(fn)) return js::Value::Null();
    return rt.Call(fn, json, {Str(rt, text)});
}
void PushState(js::Runtime& rt, js::CallArgs& a, bool replace) {
    Impl& I = Of(rt);
    const std::string state = JsonOf(rt, a[0]);
    if (state.size() > PageScripts::kMaxStateBytes) ThrowDom(rt, "DataCloneError", "the state is too big");
    std::string to = I.url;
    if (a.argc > 2 && !a[2].IsNullish()) to = ResolveUrl(I.url, rt.ToUtf8(a[2]));
    if (!SameOrigin(to, I.url)) ThrowDom(rt, "SecurityError", "a history entry can not be made for " + to + " from this page");
    if (!I.hooks.pushState || !I.hooks.pushState(to, state == "null" ? std::string() : state, replace))
        ThrowDom(rt, "SecurityError", "history entries can not be made here");
    I.url = to;
    I.historyState = state == "null" ? std::string() : state;
    if (I.historyStateRef >= 0) { rt.Unref(I.historyStateRef); I.historyStateRef = -1; }
}

using js::Value;
using js::CallArgs;
using js::Runtime;

// fetch(), Headers, Response and XMLHttpRequest on the one native request function (Fetch standard 5.2-5.5; XMLHttpRequest
// living standard 3.5-3.6). Run once per page before its own scripts; it removes its helper from the global object.
const char* const kPrelude = R"JS(
(function (ok) {
  const request = ok.request, cancel = ok.abort, domError = ok.domError;
  delete globalThis.__okay;
  const LIST = Symbol('list'), BODY = Symbol('body'), TAKE = Symbol('take');
  class Headers {
    constructor(init) {
      this[LIST] = [];
      if (init == null) return;
      if (init instanceof Headers) init = init[LIST];
      if (typeof init[Symbol.iterator] === 'function') { for (const p of init) this.append(p[0], p[1]); }
      else for (const k of Object.keys(init)) this.append(k, init[k]);
    }
    append(n, v) { this[LIST].push([String(n).toLowerCase(), String(v)]); }
    set(n, v) { n = String(n).toLowerCase(); this.delete(n); this[LIST].push([n, String(v)]); }
    get(n) { n = String(n).toLowerCase(); const f = this[LIST].filter(p => p[0] === n); return f.length ? f.map(p => p[1]).join(', ') : null; }
    has(n) { n = String(n).toLowerCase(); return this[LIST].some(p => p[0] === n); }
    delete(n) { n = String(n).toLowerCase(); this[LIST] = this[LIST].filter(p => p[0] !== n); }
    forEach(cb, self) { for (const p of this[LIST]) cb.call(self, p[1], p[0], this); }
    entries() { return this[LIST].map(p => [p[0], p[1]])[Symbol.iterator](); }
    keys() { return this[LIST].map(p => p[0])[Symbol.iterator](); }
    values() { return this[LIST].map(p => p[1])[Symbol.iterator](); }
    [Symbol.iterator]() { return this.entries(); }
  }
  class Response {
    constructor(body, init) {
      init = init || {};
      this.status = init.status === undefined ? 200 : init.status | 0;
      this.statusText = init.statusText === undefined ? '' : String(init.statusText);
      this.ok = this.status >= 200 && this.status < 300;
      this.headers = new Headers(init.headers);
      this.url = '';
      this.type = 'basic';
      this.redirected = false;
      this.bodyUsed = false;
      this[BODY] = body == null ? '' : String(body);
    }
    [TAKE]() {
      if (this.bodyUsed) return Promise.reject(new TypeError('the body was already read'));
      this.bodyUsed = true;
      return Promise.resolve(this[BODY]);
    }
    text() { return this[TAKE](); }
    json() { return this[TAKE]().then(t => JSON.parse(t)); }
    clone() {
      if (this.bodyUsed) throw new TypeError('the body was already read');
      const r = new Response(this[BODY], { status: this.status, statusText: this.statusText, headers: this.headers });
      r.url = this.url; r.redirected = this.redirected;
      return r;
    }
  }
  function fetch(input, init) {
    return new Promise((resolve, reject) => {
      init = init || {};
      const isReq = input !== null && typeof input === 'object' && 'url' in input;
      const url = isReq ? String(input.url) : String(input);
      const method = String(init.method || (isReq && input.method) || 'GET').toUpperCase();
      const headers = new Headers(init.headers || (isReq ? input.headers : undefined));
      const body = init.body == null ? '' : String(init.body);
      if (body && (method === 'GET' || method === 'HEAD')) throw new TypeError('a GET or HEAD request has no body');
      if (init.body != null && typeof init.body === 'string' && !headers.has('content-type')) headers.set('content-type', 'text/plain;charset=UTF-8');
      request(method, url, headers[LIST], body, (good, status, statusText, list, text, finalUrl, why) => {
        if (!good) { reject(new TypeError('Failed to fetch: ' + why)); return; }
        const r = new Response(text, { status: status, statusText: statusText, headers: list });
        r.url = finalUrl;
        resolve(r);
      });
    });
  }
  const EV = Symbol('ev'), SEND = Symbol('send');
  function fire(x, type) {
    const ev = { type: type, target: x, currentTarget: x, lengthComputable: false, loaded: 0, total: 0,
                 preventDefault() {}, stopPropagation() {}, stopImmediatePropagation() {} };
    const run = f => { try { f.call(x, ev); } catch (e) { console.error(e && e.stack ? e.stack : String(e)); } };
    if (typeof x['on' + type] === 'function') run(x['on' + type]);
    for (const f of (x[EV][type] || []).slice()) run(f);
  }
  class XMLHttpRequest {
    constructor() {
      this[EV] = {}; this[SEND] = null;
      this.readyState = 0; this.status = 0; this.statusText = ''; this.responseText = ''; this.response = '';
      this.responseType = ''; this.responseURL = ''; this.timeout = 0; this.withCredentials = false;
      this.onreadystatechange = null; this.onload = null; this.onerror = null; this.onloadend = null; this.onloadstart = null;
      this.onabort = null; this.onprogress = null; this.ontimeout = null;
      this.upload = { addEventListener() {}, removeEventListener() {} };
    }
    addEventListener(type, f) { if (typeof f !== 'function') return; const l = this[EV][type] || (this[EV][type] = []); if (l.indexOf(f) < 0) l.push(f); }
    removeEventListener(type, f) { const l = this[EV][type]; if (l) { const i = l.indexOf(f); if (i >= 0) l.splice(i, 1); } }
    open(method, url, async) {
      if (arguments.length > 2 && !async) throw domError('InvalidAccessError', 'synchronous requests are not supported');
      if (this[SEND] && this[SEND].id) cancel(this[SEND].id);
      this[SEND] = { method: String(method).toUpperCase(), url: String(url), headers: [], sent: false, id: 0, answer: [] };
      this.readyState = 1; this.status = 0; this.statusText = ''; this.responseText = '';
      this.response = this.responseType === 'json' ? null : '';
      fire(this, 'readystatechange');
    }
    setRequestHeader(n, v) {
      if (this.readyState !== 1 || this[SEND].sent) throw domError('InvalidStateError', 'open() first, and before send()');
      this[SEND].headers.push([String(n), String(v)]);
    }
    send(body) {
      const s = this[SEND];
      if (this.readyState !== 1 || s.sent) throw domError('InvalidStateError', 'open() first, and send() once');
      s.sent = true;
      body = (body == null || s.method === 'GET' || s.method === 'HEAD') ? '' : String(body);
      fire(this, 'loadstart');
      const fail = () => {
        if (this[SEND] !== s) return;
        this.readyState = 4; this.status = 0; s.id = 0;
        fire(this, 'readystatechange'); fire(this, 'error'); fire(this, 'loadend');
      };
      try {
        s.id = request(s.method, s.url, s.headers, body, (good, status, statusText, list, text, finalUrl) => {
          if (this[SEND] !== s) return;
          s.id = 0;
          if (!good) { fail(); return; }
          s.answer = list;
          this.status = status; this.statusText = statusText; this.responseURL = finalUrl;
          this.readyState = 2; fire(this, 'readystatechange');
          this.readyState = 3; fire(this, 'readystatechange');
          this.responseText = text;
          if (this.responseType === 'json') { try { this.response = JSON.parse(text); } catch (e) { this.response = null; } }
          else this.response = text;
          this.readyState = 4; fire(this, 'readystatechange');
          fire(this, 'load'); fire(this, 'loadend');
        });
      } catch (e) { Promise.resolve().then(fail); }
    }
    abort() {
      const s = this[SEND];
      if (!s || !s.sent || this.readyState === 4) { this.readyState = 0; return; }
      if (s.id) cancel(s.id);
      this[SEND] = null;
      this.readyState = 4; this.status = 0;
      fire(this, 'readystatechange'); fire(this, 'abort'); fire(this, 'loadend');
      this.readyState = 0;
    }
    getResponseHeader(n) {
      const s = this[SEND];
      if (!s || this.readyState < 2) return null;
      n = String(n).toLowerCase();
      const f = s.answer.filter(p => p[0].toLowerCase() === n);
      return f.length ? f.map(p => p[1]).join(', ') : null;
    }
    getAllResponseHeaders() {
      const s = this[SEND];
      if (!s || this.readyState < 2) return '';
      return s.answer.map(p => p[0].toLowerCase() + ': ' + p[1] + '\r\n').join('');
    }
    overrideMimeType() {}
  }
  XMLHttpRequest.UNSENT = 0; XMLHttpRequest.OPENED = 1; XMLHttpRequest.HEADERS_RECEIVED = 2; XMLHttpRequest.LOADING = 3; XMLHttpRequest.DONE = 4;
  globalThis.Headers = Headers;
  globalThis.Response = Response;
  globalThis.fetch = fetch;
  globalThis.XMLHttpRequest = XMLHttpRequest;
})(globalThis.__okay);
)JS";

} // namespace

// ============================================================ round 4: interfaces, events, observers, the view
namespace {

// Web IDL 3.7.3: an interface's prototype object has @@toStringTag, so Object.prototype.toString says "[object HTMLDivElement]".
void ClassString(Runtime& r, js::Object* proto, const char* name) {
    r.DefineData(proto, r.symToStringTag, Value::Str(r.NewStringUtf8(name)), js::kConfigurable);
}

// The interface objects of the DOM and HTML standards (DOM 4.4-4.11, HTML 3.2.8 and its element index): one constructor each,
// whose prototype the page's wrappers get (Impl::ProtoFor), so `el instanceof HTMLElement` and the like work. Elements are
// not made with these (HTML 3.2.8: "Illegal constructor"); document.createElement makes them. slots[0]: the interface name.
Value IllegalCtor(Runtime& rt, CallArgs& a) {
    rt.ThrowType("Illegal constructor: '" + rt.ToUtf8(a.callee->slots[0]) + "' is not made with new (use document.createElement)");
}
js::Object* Interface(Runtime& r, const char* name, js::Object* parentProto, js::NativeFn fn = IllegalCtor, int length = 0) {
    js::Object* proto = r.NewObject(parentProto);
    r.Ref(Value::Obj(proto));
    js::NativeFunc* f = js::DefCtor(r, name, fn, length, proto);
    f->slots.push_back(Value::Str(r.NewStringUtf8(name)));
    ClassString(r, proto, name);
    return proto;
}
// The node a constructor made gets the prototype new.target names (a class that extends the interface), else its interface's.
js::Object* ProtoOfNew(Runtime& rt, CallArgs& a, js::Object* fallback) {
    if (!a.newTarget) rt.ThrowType("Failed to construct: use 'new'");
    const Value p = rt.GetStr(Value::Obj(a.newTarget), "prototype");
    return p.IsObj() ? p.O() : fallback;
}
Value WrapNew(Runtime& rt, CallArgs& a, Node* n, js::Object* fallback) {
    Impl& I = Of(rt);
    const Value w = I.Wrap(n);
    js::Object* proto = ProtoOfNew(rt, a, fallback);
    if (proto != w.O()->proto) rt.SetPrototype(w.O(), proto);
    return w;
}

// new Event(type, init) and the event interfaces (DOM 2.2, 2.4 CustomEvent; UI Events 3 and 5): slots[1] says which:
// 0 Event, 1 CustomEvent (detail), 2 another interface (the init dictionary's members are copied).
Value EventCtor(Runtime& rt, CallArgs& a) {
    Impl& I = Of(rt);
    js::Object* proto = ProtoOfNew(rt, a, I.eventProto);
    if (a.argc < 1) rt.ThrowType("Failed to construct '" + rt.ToUtf8(a.callee->slots[0]) + "': 1 argument required");
    const int kind = (int)a.callee->slots[1].n;
    js::Object* ev = rt.NewObject(proto);
    rt.Keep(Value::Obj(ev));
    const Value init = a[1];
    auto flag = [&](const char* k) { return init.IsObj() && rt.ToBoolean(rt.GetStr(init, k)); };
    auto put = [&](const char* k, const Value& v) { rt.DefineData(ev, rt.Intern(k), v); };
    put("type", Value::Str(rt.ToString(a[0])));
    put("bubbles", Value::Bool(flag("bubbles")));
    put("cancelable", Value::Bool(flag("cancelable")));
    put("composed", Value::Bool(flag("composed")));
    put("defaultPrevented", Value::Bool(false));
    put("isTrusted", Value::Bool(false));
    put("timeStamp", Value::Num(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - I.startedAt).count()));
    put("target", Value::Null());
    put("currentTarget", Value::Null());
    put("srcElement", Value::Null());
    put("eventPhase", Value::Num(0));
    put("returnValue", Value::Bool(true));
    put("cancelBubble", Value::Bool(false));
    if (kind == 1) put("detail", init.IsObj() ? rt.GetStr(init, "detail") : Value::Null());
    if (kind == 1 && rt.GetStr(Value::Obj(ev), "detail").IsUndef()) put("detail", Value::Null());
    if (kind == 2) {
        for (const char* k : {"ctrlKey", "shiftKey", "altKey", "metaKey", "repeat", "isComposing"}) put(k, Value::Bool(false));
        for (const char* k : {"detail", "button", "buttons", "clientX", "clientY", "screenX", "screenY", "pageX", "pageY", "keyCode",
                              "charCode", "which", "location", "deltaX", "deltaY", "deltaZ", "deltaMode"}) put(k, Value::Num(0));
        put("key", Str(rt, std::string()));
        put("code", Str(rt, std::string()));
        put("view", Value::Null());
        put("relatedTarget", Value::Null());
        if (init.IsObj()) {
            js::Object* o = init.O();
            for (js::GcObj* key : rt.OwnKeys(o)) {
                js::Prop p;
                if (!rt.GetOwnProperty(o, key, p) || !(p.flags & js::kEnumerable)) continue;
                const std::string name = js::KeyText(rt, key);
                if (name == "bubbles" || name == "cancelable" || name == "composed") continue;
                rt.DefineData(ev, key, rt.Get(init, key));
            }
        }
    }
    return Value::Obj(ev);
}

// atob / btoa (HTML 8.3 "forgiving-base64").
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// The page's MutationObserver `this` (a host object of class kObserver; h->id is its index in mos).
int ThisObserver(Runtime& rt, CallArgs& a) {
    Impl& I = Of(rt);
    const js::HostObj* h = a.thisv.IsObj() && a.thisv.O()->cls == js::Cls::Host ? static_cast<const js::HostObj*>(a.thisv.O()) : nullptr;
    if (!h || h->hostClass != I.kObserver || h->id >= I.mos.size() || !I.mos[(std::size_t)h->id].live)
        rt.ThrowType("Illegal invocation: 'this' is not a MutationObserver");
    return (int)h->id;
}
// The records of observer `id` as an array (they are handed over: the observer's list is emptied).
Value TakeRecordsOf(Runtime& rt, int id) {
    Impl& I = Of(rt);
    Impl::Mo& mo = I.mos[(std::size_t)id];
    js::ArrayObj* arr = rt.NewArray(mo.records.size());
    rt.Keep(Value::Obj(arr));
    std::uint32_t k = 0;
    for (int ref : mo.records) { rt.DefineIndex(arr, k++, rt.GetRef(ref)); rt.Unref(ref); }
    mo.records.clear();
    return Value::Obj(arr);
}
void DropObserverRegs(Impl& I, int id) {
    for (auto it = I.moRegs.begin(); it != I.moRegs.end();) {
        auto& list = it->second;
        list.erase(std::remove_if(list.begin(), list.end(), [id](const Impl::MoReg& r) { return r.observer == id; }), list.end());
        if (list.empty()) it = I.moRegs.erase(it);
        else ++it;
    }
}

// Fragments: insertAdjacentHTML's markup read as the children of <body>, each top node put in the removed list (Insert takes it).
std::vector<Node*> ParseFragment(Runtime& rt, Node* context, const std::string& html) {
    Impl& I = Of(rt);
    HtmlLimits lim;
    lim.maxDepth = std::max(1, lim.maxDepth - DepthOf(context) - 3);
    rt.Charge(html.size() / 16);
    HtmlDocument frag = ParseHtml("<!DOCTYPE html><body>" + html, lim);
    Node* body = const_cast<Node*>(frag.Body());
    std::vector<Node*> out;
    if (!body) return out;
    std::size_t count = 0, bytes = 0;
    for (auto& c : body->children) { count += CountNodes(c.get()); bytes += TreeBytes(c.get()); }
    rt.Charge(count);
    I.CheckRoom(rt, count);
    I.Account(rt, bytes, 0);
    for (auto& c : body->children) {
        Walk(c.get(), true, [&](Node* n) { if (n->IsElement("script")) I.started.insert(n); return false; });   // as innerHTML
        c->parent = nullptr;
        out.push_back(c.get());
        I.removed.push_back(std::move(c));
    }
    I.nodeCount += count;
    return out;
}

// The page's view and the size of the window (CSSOM View 4, 5).
PageView ViewOf(Impl& I) { return I.hooks.view ? I.hooks.view() : PageView(); }
void ScrollPage(Runtime& rt, CallArgs& a, bool by) {
    Impl& I = Of(rt);
    if (!I.hooks.scrollTo) return;
    const PageView v = ViewOf(I);
    double x = by ? 0 : v.scrollX, y = by ? 0 : v.scrollY;
    if (a[0].IsObj()) {   // ScrollToOptions { left, top, behavior }
        const Value l = rt.GetStr(a[0], "left"), t = rt.GetStr(a[0], "top");
        if (!l.IsUndef()) x = rt.ToNumber(l);
        if (!t.IsUndef()) y = rt.ToNumber(t);
    } else if (a.argc >= 2) { x = rt.ToNumber(a[0]); y = rt.ToNumber(a[1]); }
    if (by) { x += v.scrollX; y += v.scrollY; }
    if (!std::isfinite(x)) x = v.scrollX;
    if (!std::isfinite(y)) y = v.scrollY;
    I.hooks.scrollTo((float)x, (float)y);
}

// The page's own JavaScript for the APIs that are easier there: URL / URLSearchParams (WHATWG URL 6), navigator (HTML 8.9.1),
// performance (High Resolution Time 2, User Timing 3), matchMedia (CSSOM View 4.2), requestIdleCallback (Cooperative
// Scheduling of Background Tasks), DOMRect (Geometry Interfaces 1), IntersectionObserver (Intersection Observer, 3.2.8 "run
// the update intersection observations steps"), ResizeObserver (Resize Observer 1, 3.4), getBoundingClientRect (CSSOM View
// 6.1), the Event methods of DOM 2.2 that need no native help, document.fonts (CSS Font Loading 3, reduced: no fonts are
// waited for), CSS.escape (CSSOM 2.1.1) and postMessage to the page itself (HTML 9.3.3, same window only).
const char* const kPrelude4 = R"JS(
(function (ok) {
  delete globalThis.__okay4;
  const LIST = Symbol('list'), OWNER = Symbol('owner'), PARTS = Symbol('parts'), SP = Symbol('sp');
  // ---- URLSearchParams (WHATWG URL 6.2) over the urlencoded parser and serializer (5.1, 5.2)
  function parseQuery(q) {
    const out = [];
    q = String(q);
    if (q.charAt(0) === '?') q = q.slice(1);
    for (const part of q.split('&')) {
      if (!part) continue;
      const i = part.indexOf('=');
      out.push([ok.formDecode(i < 0 ? part : part.slice(0, i)), ok.formDecode(i < 0 ? '' : part.slice(i + 1))]);
    }
    return out;
  }
  function changed(sp) {
    const u = sp[OWNER];
    if (!u) return;
    const q = sp.toString();
    u[PARTS].search = q ? '?' + q : '';
  }
  class URLSearchParams {
    constructor(init) {
      this[LIST] = []; this[OWNER] = null;
      if (init == null) return;
      if (typeof init === 'object' || typeof init === 'function') {
        if (init instanceof URLSearchParams) { this[LIST] = init[LIST].map(p => [p[0], p[1]]); return; }
        if (typeof init[Symbol.iterator] === 'function') {
          for (const p of init) {
            const a = Array.from(p);
            if (a.length !== 2) throw new TypeError("Failed to construct 'URLSearchParams': each pair needs a name and a value");
            this[LIST].push([String(a[0]), String(a[1])]);
          }
          return;
        }
        for (const k of Object.keys(init)) this[LIST].push([k, String(init[k])]);
        return;
      }
      this[LIST] = parseQuery(init);
    }
    get size() { return this[LIST].length; }
    append(n, v) { this[LIST].push([String(n), String(v)]); changed(this); }
    delete(n, v) {
      n = String(n);
      this[LIST] = this[LIST].filter(p => !(p[0] === n && (v === undefined || p[1] === String(v))));
      changed(this);
    }
    get(n) { n = String(n); for (const p of this[LIST]) if (p[0] === n) return p[1]; return null; }
    getAll(n) { n = String(n); return this[LIST].filter(p => p[0] === n).map(p => p[1]); }
    has(n, v) { n = String(n); return this[LIST].some(p => p[0] === n && (v === undefined || p[1] === String(v))); }
    set(n, v) {
      n = String(n); v = String(v);
      const out = [];
      let done = false;
      for (const p of this[LIST]) {
        if (p[0] !== n) { out.push(p); continue; }
        if (!done) { out.push([n, v]); done = true; }
      }
      if (!done) out.push([n, v]);
      this[LIST] = out;
      changed(this);
    }
    sort() {   // stable, by the names' code units
      const l = this[LIST].map((p, i) => [p, i]);
      l.sort((a, b) => a[0][0] < b[0][0] ? -1 : a[0][0] > b[0][0] ? 1 : a[1] - b[1]);
      this[LIST] = l.map(x => x[0]);
      changed(this);
    }
    toString() { return this[LIST].map(p => ok.formEncode(p[0]) + '=' + ok.formEncode(p[1])).join('&'); }
    forEach(cb, self) { for (const p of this[LIST].slice()) cb.call(self, p[1], p[0], this); }
    entries() { return this[LIST].map(p => [p[0], p[1]])[Symbol.iterator](); }
    keys() { return this[LIST].map(p => p[0])[Symbol.iterator](); }
    values() { return this[LIST].map(p => p[1])[Symbol.iterator](); }
    [Symbol.iterator]() { return this.entries(); }
  }
  // ---- URL (WHATWG URL 6.1) on the native parser (Url.hpp ParseWebUrl)
  function parse(input, base) {
    const p = ok.parseUrl(String(input), base === undefined ? null : String(base));
    if (!p) return null;
    return { protocol: p[0], username: p[1], password: p[2], hostname: p[3], port: p[4], pathname: p[5], search: p[6],
             hash: p[7], authority: p[8], special: p[9] };
  }
  function hrefOf(p) {
    let s = p.protocol;
    if (p.authority) {
      s += '//';
      if (p.username || p.password) s += p.username + (p.password ? ':' + p.password : '') + '@';
      s += p.hostname + (p.port ? ':' + p.port : '');
    }
    return s + p.pathname + p.search + p.hash;
  }
  function reparse(u, text) {   // a setter: the address with one part changed is read again; an invalid one changes nothing
    const p = parse(text);
    if (!p) return;
    u[PARTS] = p;
    if (u[SP]) u[SP][LIST] = parseQuery(p.search);
  }
  class URL {
    constructor(url, base) {
      const p = parse(url, base);
      if (!p) throw new TypeError("Failed to construct 'URL': Invalid URL");
      this[PARTS] = p; this[SP] = null;
    }
    static canParse(url, base) { return parse(url, base) !== null; }
    static parse(url, base) { try { return new URL(url, base); } catch (e) { return null; } }
    static createObjectURL() { return 'blob:' + location.origin + '/' + (++URL._n); }
    static revokeObjectURL() {}
    get href() { return hrefOf(this[PARTS]); }
    set href(v) { const p = parse(v); if (!p) throw new TypeError("Failed to set 'href' on 'URL': Invalid URL"); reparse(this, v); }
    get origin() { return ok.originOf(hrefOf(this[PARTS])); }
    get protocol() { return this[PARTS].protocol; }
    set protocol(v) { v = String(v).replace(/:.*$/, ''); const p = this[PARTS]; reparse(this, hrefOf(Object.assign({}, p, { protocol: v + ':' }))); }
    get username() { return this[PARTS].username; }
    set username(v) { const p = this[PARTS]; if (p.authority) reparse(this, hrefOf(Object.assign({}, p, { username: String(v) }))); }
    get password() { return this[PARTS].password; }
    set password(v) { const p = this[PARTS]; if (p.authority) reparse(this, hrefOf(Object.assign({}, p, { password: String(v) }))); }
    get host() { const p = this[PARTS]; return p.hostname + (p.port ? ':' + p.port : ''); }
    set host(v) { const p = this[PARTS]; if (!p.authority) return; v = String(v); const i = v.lastIndexOf(':'); reparse(this, hrefOf(Object.assign({}, p, i > 0 && v.indexOf(']', i) < 0 ? { hostname: v.slice(0, i), port: v.slice(i + 1) } : { hostname: v }))); }
    get hostname() { return this[PARTS].hostname; }
    set hostname(v) { const p = this[PARTS]; if (p.authority) reparse(this, hrefOf(Object.assign({}, p, { hostname: String(v) }))); }
    get port() { return this[PARTS].port; }
    set port(v) { const p = this[PARTS]; if (p.authority) reparse(this, hrefOf(Object.assign({}, p, { port: String(v) }))); }
    get pathname() { return this[PARTS].pathname; }
    set pathname(v) { const p = this[PARTS]; v = String(v); if (p.special && v.charAt(0) !== '/') v = '/' + v; reparse(this, hrefOf(Object.assign({}, p, { pathname: v }))); }
    get search() { return this[PARTS].search; }
    set search(v) { const p = this[PARTS]; v = String(v); if (v && v.charAt(0) !== '?') v = '?' + v; reparse(this, hrefOf(Object.assign({}, p, { search: v === '?' ? '' : v }))); if (this[SP]) this[SP][LIST] = parseQuery(this[PARTS].search); }
    get searchParams() {
      if (!this[SP]) { const sp = new URLSearchParams(this[PARTS].search); sp[OWNER] = this; this[SP] = sp; }
      return this[SP];
    }
    get hash() { return this[PARTS].hash; }
    set hash(v) { const p = this[PARTS]; v = String(v); if (v && v.charAt(0) !== '#') v = '#' + v; reparse(this, hrefOf(Object.assign({}, p, { hash: v === '#' ? '' : v }))); }
    toString() { return this.href; }
    toJSON() { return this.href; }
  }
  URL._n = 0;
  globalThis.URL = URL;
  globalThis.URLSearchParams = URLSearchParams;
  globalThis.webkitURL = URL;

  // ---- navigator (HTML 8.9.1); no service worker, so feature tests take the plain path
  const lang = ok.language;
  const langs = [lang];
  if (lang.indexOf('-') > 0) langs.push(lang.slice(0, lang.indexOf('-')));
  const navigator = {
    userAgent: ok.userAgent, appCodeName: 'Mozilla', appName: 'Netscape', appVersion: ok.userAgent.replace(/^Mozilla\//, ''),
    product: 'Gecko', productSub: '20100101', vendor: '', vendorSub: '', platform: ok.platform, oscpu: ok.platform,
    language: lang, languages: Object.freeze(langs), onLine: true, cookieEnabled: ok.cookies, hardwareConcurrency: 4,
    maxTouchPoints: 0, doNotTrack: null, webdriver: false, pdfViewerEnabled: false,
    javaEnabled() { return false; },
    sendBeacon(url, data) {   // HTML 8.9.1.6 / Beacon: a POST the page does not wait for
      try { fetch(url, { method: 'POST', body: data == null ? '' : String(data) }).catch(() => {}); return true; } catch (e) { return false; }
    },
  };
  globalThis.navigator = navigator;
  globalThis.clientInformation = navigator;

  // ---- performance (High Resolution Time 2; User Timing 3: marks and measures)
  const entries = [];
  function addEntry(e) { entries.push(e); if (entries.length > 1000) entries.shift(); return e; }
  function markTime(name) {
    for (let i = entries.length - 1; i >= 0; --i) if (entries[i].entryType === 'mark' && entries[i].name === name) return entries[i].startTime;
    const e = new Error("The mark '" + name + "' does not exist."); e.name = 'SyntaxError'; throw e;
  }
  const origin = ok.timeOrigin();
  const performance = {
    timeOrigin: origin,
    now() { return ok.now(); },
    mark(name, o) {
      const t = o && o.startTime !== undefined ? +o.startTime : ok.now();
      return addEntry({ name: String(name), entryType: 'mark', startTime: t, duration: 0, detail: o && o.detail !== undefined ? o.detail : null });
    },
    measure(name, a, b) {
      let start = 0, end = ok.now();
      if (a && typeof a === 'object') {
        if (a.start !== undefined) start = typeof a.start === 'string' ? markTime(a.start) : +a.start;
        if (a.end !== undefined) end = typeof a.end === 'string' ? markTime(a.end) : +a.end;
        else if (a.duration !== undefined) end = start + +a.duration;
      } else {
        if (a !== undefined) start = markTime(String(a));
        if (b !== undefined) end = markTime(String(b));
      }
      return addEntry({ name: String(name), entryType: 'measure', startTime: start, duration: end - start, detail: a && a.detail !== undefined ? a.detail : null });
    },
    getEntries() { return entries.slice(); },
    getEntriesByType(t) { return entries.filter(e => e.entryType === t); },
    getEntriesByName(n, t) { return entries.filter(e => e.name === n && (t === undefined || e.entryType === t)); },
    clearMarks(n) { for (let i = entries.length - 1; i >= 0; --i) if (entries[i].entryType === 'mark' && (n === undefined || entries[i].name === n)) entries.splice(i, 1); },
    clearMeasures(n) { for (let i = entries.length - 1; i >= 0; --i) if (entries[i].entryType === 'measure' && (n === undefined || entries[i].name === n)) entries.splice(i, 1); },
    clearResourceTimings() {}, setResourceTimingBufferSize() {},
    addEventListener() {}, removeEventListener() {},
    timing: { navigationStart: origin, fetchStart: origin, requestStart: origin, responseStart: origin, responseEnd: origin,
              domLoading: origin, domInteractive: origin, domContentLoadedEventStart: 0, domContentLoadedEventEnd: 0,
              domComplete: 0, loadEventStart: 0, loadEventEnd: 0 },
    navigation: { type: 0, redirectCount: 0 },
    toJSON() { return { timeOrigin: origin }; },
  };
  globalThis.performance = performance;
  class PerformanceObserver {   // no entries are reported to it (none of the entry types it is asked for are made here)
    constructor(cb) { if (typeof cb !== 'function') throw new TypeError("Failed to construct 'PerformanceObserver': the callback is not a function"); }
    observe() {} disconnect() {} takeRecords() { return []; }
  }
  PerformanceObserver.supportedEntryTypes = Object.freeze(['mark', 'measure']);
  globalThis.PerformanceObserver = PerformanceObserver;

  // ---- requestIdleCallback (Cooperative Scheduling of Background Tasks): soon, with up to 50 ms "left"
  globalThis.requestIdleCallback = function (cb, opts) {
    if (typeof cb !== 'function') throw new TypeError("requestIdleCallback: the callback is not a function");
    const end = ok.now() + 50;
    return setTimeout(() => cb({ didTimeout: false, timeRemaining() { return Math.max(0, end - ok.now()); } }), 1);
  };
  globalThis.cancelIdleCallback = function (id) { clearTimeout(id); };

  // ---- DOMRect (Geometry Interfaces 1, 6)
  class DOMRectReadOnly {
    constructor(x, y, w, h) { this._x = +x || 0; this._y = +y || 0; this._w = +w || 0; this._h = +h || 0; }
    get x() { return this._x; } get y() { return this._y; } get width() { return this._w; } get height() { return this._h; }
    get top() { return Math.min(this._y, this._y + this._h); } get bottom() { return Math.max(this._y, this._y + this._h); }
    get left() { return Math.min(this._x, this._x + this._w); } get right() { return Math.max(this._x, this._x + this._w); }
    toJSON() { return { x: this.x, y: this.y, width: this.width, height: this.height, top: this.top, right: this.right, bottom: this.bottom, left: this.left }; }
    static fromRect(r) { r = r || {}; return new this(r.x, r.y, r.width, r.height); }
  }
  class DOMRect extends DOMRectReadOnly {
    get x() { return this._x; } set x(v) { this._x = +v; }
    get y() { return this._y; } set y(v) { this._y = +v; }
    get width() { return this._w; } set width(v) { this._w = +v; }
    get height() { return this._h; } set height(v) { this._h = +v; }
  }
  globalThis.DOMRectReadOnly = DOMRectReadOnly;
  globalThis.DOMRect = DOMRect;
  function rectOf(el) { const r = ok.rect(el); return r ? new DOMRect(r[0], r[1], r[2], r[3]) : new DOMRect(0, 0, 0, 0); }
  Element.prototype.getBoundingClientRect = function () { return rectOf(this); };
  Element.prototype.getClientRects = function () { const r = ok.rect(this); return r ? [new DOMRect(r[0], r[1], r[2], r[3])] : []; };
  Element.prototype.checkVisibility = function () { return ok.rect(this) !== null; };

  // ---- Event methods (DOM 2.2) and document.createEvent (DOM 4.5)
  Event.prototype.composedPath = function () {
    const out = [];
    if (!this.currentTarget) return out;
    for (let n = this.target; n && n !== globalThis; n = n.parentNode) out.push(n);
    if (this.target && (this.target === globalThis || (this.target.ownerDocument || this.target).defaultView)) out.push(globalThis);
    return out;
  };
  Event.prototype.initEvent = function (type, bubbles, cancelable) {
    this.type = String(type); this.bubbles = !!bubbles; this.cancelable = !!cancelable;
  };
  CustomEvent.prototype.initCustomEvent = function (type, bubbles, cancelable, detail) {
    this.initEvent(type, bubbles, cancelable); this.detail = detail === undefined ? null : detail;
  };
  Document.prototype.createEvent = function (name) {
    const n = String(name).toLowerCase();
    if (n === 'customevent') return new CustomEvent('');
    const known = { event: Event, events: Event, htmlevents: Event, uievent: UIEvent, uievents: UIEvent, mouseevent: MouseEvent,
                    mouseevents: MouseEvent, keyboardevent: KeyboardEvent, focusevent: FocusEvent, messageevent: MessageEvent };
    const C = known[n];
    if (!C) { const e = new Error("The interface '" + name + "' is not supported."); e.name = 'NotSupportedError'; throw e; }
    return new C('');
  };
  // ---- document.fonts (CSS Font Loading 3): the fonts the page names are loaded by the browser on its own, nothing waits
  const fonts = { status: 'loaded', size: 0, ready: Promise.resolve(), onloading: null, onloadingdone: null, onloadingerror: null,
                  load() { return Promise.resolve([]); }, check() { return true; }, add() { return fonts; }, delete() { return false; },
                  clear() {}, has() { return false; }, forEach() {}, addEventListener() {}, removeEventListener() {},
                  values() { return [][Symbol.iterator](); }, [Symbol.iterator]() { return [][Symbol.iterator](); } };
  fonts.ready = Promise.resolve(fonts);
  Object.defineProperty(Document.prototype, 'fonts', { value: fonts, configurable: true });
  class FontFace {
    constructor(family, source) { this.family = String(family); this.status = 'unloaded'; this.loaded = Promise.resolve(this); }
    load() { this.status = 'loaded'; return Promise.resolve(this); }
  }
  globalThis.FontFace = FontFace;

  // ---- CSS.escape (CSSOM 2.1.1 "serialize an identifier") and CSS.supports (no feature is promised)
  globalThis.CSS = {
    escape(v) {
      const s = String(v);
      let out = '';
      for (let i = 0; i < s.length; ++i) {
        const c = s.charCodeAt(i);
        if (c === 0) out += '�';
        else if ((c >= 1 && c <= 0x1f) || c === 0x7f || (c >= 0x30 && c <= 0x39 && (i === 0 || (i === 1 && s.charCodeAt(0) === 0x2d))))
          out += '\\' + c.toString(16) + ' ';
        else if (i === 0 && c === 0x2d && s.length === 1) out += '\\-';
        else if (c >= 0x80 || c === 0x2d || c === 0x5f || (c >= 0x30 && c <= 0x39) || (c >= 0x41 && c <= 0x5a) || (c >= 0x61 && c <= 0x7a)) out += s.charAt(i);
        else out += '\\' + s.charAt(i);
      }
      return out;
    },
    supports() { return false; },
  };

  // ---- matchMedia (CSSOM View 4.2): MediaQueryList with change listeners, checked again in the update steps
  const lists = [];
  const MQ = Symbol('mq');
  function MediaQueryList() { throw new TypeError('Illegal constructor'); }
  MediaQueryList.prototype.addListener = function (f) { this.addEventListener('change', f); };
  MediaQueryList.prototype.removeListener = function (f) { this.removeEventListener('change', f); };
  MediaQueryList.prototype.addEventListener = function (type, f) {
    if (type !== 'change' || typeof f !== 'function' && !(f && typeof f.handleEvent === 'function')) return;
    if (this[MQ].indexOf(f) < 0) this[MQ].push(f);
    if (lists.indexOf(this) < 0 && lists.length < 1000) lists.push(this);
  };
  MediaQueryList.prototype.removeEventListener = function (type, f) { const i = this[MQ].indexOf(f); if (i >= 0) this[MQ].splice(i, 1); };
  MediaQueryList.prototype.dispatchEvent = function (e) { fireChange(this, e); return true; };
  function fireChange(l, e) {
    const call = f => { try { if (typeof f === 'function') f.call(l, e); else f.handleEvent(e); } catch (x) { console.error(x && x.stack ? x.stack : String(x)); } };
    if (typeof l.onchange === 'function') call(l.onchange);
    for (const f of l[MQ].slice()) call(f);
  }
  globalThis.MediaQueryList = MediaQueryList;
  globalThis.matchMedia = function (q) {
    if (arguments.length < 1) throw new TypeError("matchMedia: 1 argument required");
    const l = Object.create(MediaQueryList.prototype);
    l.media = String(q); l.matches = ok.matchMedia(l.media); l.onchange = null; l[MQ] = [];
    if (lists.length < 200) lists.push(l);   // the first ones are checked even without listeners (onchange set later)
    return l;
  };

  // ---- IntersectionObserver (Intersection Observer, 2 and 3.2.8): the root is the view (or an element's box), clipping by
  // scrolling boxes between them is not looked at; rootMargin in px or % of the root
  const ioList = [], roList = [];
  const T = Symbol('targets'), CB = Symbol('cb'), Q = Symbol('queue');
  function margins(text) {
    const parts = String(text || '0px').trim().split(/\s+/).slice(0, 4);
    const v = parts.map(p => { const n = parseFloat(p); return { n: isFinite(n) ? n : 0, pct: /%$/.test(p) }; });
    while (v.length < 4) v.push(v[v.length === 3 ? 1 : v.length === 2 ? 0 : 0]);
    return v;   // top, right, bottom, left
  }
  function rootBox(o) {
    let r;
    if (o.root && o.root.nodeType === 1) { const b = ok.rect(o.root); r = b ? [b[0], b[1], b[2], b[3]] : null; }
    else { const v = ok.view(); r = [0, 0, v[0], v[1]]; }
    if (!r) return null;
    const m = o._m, px = (k, size) => m[k].pct ? m[k].n * size / 100 : m[k].n;
    const top = px(0, r[3]), right = px(1, r[2]), bottom = px(2, r[3]), left = px(3, r[2]);
    return [r[0] - left, r[1] - top, r[2] + left + right, r[3] + top + bottom];
  }
  function ioUpdate() {
    const time = ok.now();
    for (const o of ioList.slice()) {
      const root = rootBox(o);
      for (const t of o[T]) {
        const b = ok.rect(t.el);
        let inter = null, hit = false;
        if (b && root) {
          const x0 = Math.max(b[0], root[0]), y0 = Math.max(b[1], root[1]);
          const x1 = Math.min(b[0] + b[2], root[0] + root[2]), y1 = Math.min(b[1] + b[3], root[1] + root[3]);
          if (x1 >= x0 && y1 >= y0) { hit = true; inter = [x0, y0, x1 - x0, y1 - y0]; }
        }
        const area = b ? b[2] * b[3] : 0;
        const ratio = !hit ? 0 : area > 0 ? (inter[2] * inter[3]) / area : 1;
        let index = 0;
        if (hit) { index = 0; for (const th of o.thresholds) if (ratio >= th) index++; }
        if (index === t.index && hit === t.hit) continue;
        t.index = index; t.hit = hit;
        o[Q].push({ time: time, target: t.el, isIntersecting: hit, isVisible: false, intersectionRatio: ratio,
                    boundingClientRect: b ? new DOMRect(b[0], b[1], b[2], b[3]) : new DOMRect(0, 0, 0, 0),
                    intersectionRect: inter ? new DOMRect(inter[0], inter[1], inter[2], inter[3]) : new DOMRect(0, 0, 0, 0),
                    rootBounds: root ? new DOMRect(root[0], root[1], root[2], root[3]) : null });
      }
      if (o[Q].length) {
        const q = o[Q]; o[Q] = [];
        try { o[CB].call(o, q, o); } catch (x) { console.error(x && x.stack ? x.stack : String(x)); }
      }
    }
  }
  class IntersectionObserver {
    constructor(cb, opts) {
      if (typeof cb !== 'function') throw new TypeError("Failed to construct 'IntersectionObserver': the callback is not a function");
      opts = opts || {};
      this[CB] = cb; this[T] = []; this[Q] = [];
      this.root = opts.root || null;
      this.rootMargin = opts.rootMargin === undefined ? '0px 0px 0px 0px' : String(opts.rootMargin);
      this._m = margins(this.rootMargin);
      const th = opts.threshold === undefined ? [0] : [].concat(opts.threshold).map(Number);
      for (const v of th) if (!(v >= 0 && v <= 1)) throw new RangeError("Failed to construct 'IntersectionObserver': a threshold is not between 0 and 1");
      this.thresholds = Object.freeze(th.sort((a, b) => a - b));
      this.delay = 0; this.trackVisibility = false;
    }
    observe(el) {
      if (!el || el.nodeType !== 1) throw new TypeError("IntersectionObserver.observe: the target is not an element");
      if (this[T].some(t => t.el === el)) return;
      this[T].push({ el: el, index: -1, hit: false });
      if (ioList.indexOf(this) < 0) ioList.push(this);
      soon();
    }
    unobserve(el) { this[T] = this[T].filter(t => t.el !== el); if (!this[T].length) this.disconnect(); }
    disconnect() { this[T] = []; const i = ioList.indexOf(this); if (i >= 0) ioList.splice(i, 1); }
    takeRecords() { const q = this[Q]; this[Q] = []; return q; }
  }
  globalThis.IntersectionObserver = IntersectionObserver;
  globalThis.IntersectionObserverEntry = function IntersectionObserverEntry() { throw new TypeError('Illegal constructor'); };
  globalThis.IntersectionObserverEntry.prototype.intersectionRatio = 0;

  // ---- ResizeObserver (Resize Observer 1, 3.4): border box from the layout, the content box is it less the borders
  function roUpdate() {
    for (const o of roList.slice()) {
      const q = [];
      for (const t of o[T]) {
        const b = ok.rect(t.el);
        const bw = b ? b[2] : 0, bh = b ? b[3] : 0;
        const cw = b ? Math.max(0, bw - b[5] - b[7]) : 0, ch = b ? Math.max(0, bh - b[4] - b[6]) : 0;
        const w = t.box === 'border-box' ? bw : cw, h = t.box === 'border-box' ? bh : ch;
        if (w === t.w && h === t.h) continue;
        t.w = w; t.h = h;
        const size = (i, bl) => Object.freeze([{ inlineSize: i, blockSize: bl }]);
        q.push({ target: t.el, contentRect: new DOMRectReadOnly(0, 0, cw, ch), borderBoxSize: size(bw, bh), contentBoxSize: size(cw, ch),
                 devicePixelContentBoxSize: size(cw, ch) });
      }
      if (q.length) { try { o[CB].call(o, q, o); } catch (x) { console.error(x && x.stack ? x.stack : String(x)); } }
    }
  }
  class ResizeObserver {
    constructor(cb) {
      if (typeof cb !== 'function') throw new TypeError("Failed to construct 'ResizeObserver': the callback is not a function");
      this[CB] = cb; this[T] = [];
    }
    observe(el, opts) {
      if (!el || el.nodeType !== 1) throw new TypeError("ResizeObserver.observe: the target is not an element");
      const box = opts && opts.box ? String(opts.box) : 'content-box';
      const old = this[T].find(t => t.el === el);
      if (old) { old.box = box; return; }
      this[T].push({ el: el, box: box, w: -1, h: -1 });
      if (roList.indexOf(this) < 0) roList.push(this);
      soon();
    }
    unobserve(el) { this[T] = this[T].filter(t => t.el !== el); }
    disconnect() { this[T] = []; const i = roList.indexOf(this); if (i >= 0) roList.splice(i, 1); }
  }
  globalThis.ResizeObserver = ResizeObserver;

  // ---- EventTarget (DOM 2.7) for objects a script makes itself (nodes and window keep their own listener lists)
  const LS = Symbol('listeners');
  class EventTarget {
    constructor() { this[LS] = {}; }
    addEventListener(t, f, o) {
      if (!f) return;
      const l = this[LS][t] || (this[LS][t] = []);
      if (!l.some(x => x.f === f)) l.push({ f: f, once: !!(o && typeof o === 'object' && o.once) });
    }
    removeEventListener(t, f) { const l = this[LS][t]; if (l) { const i = l.findIndex(x => x.f === f); if (i >= 0) l.splice(i, 1); } }
    dispatchEvent(e) {
      e.target = this; e.currentTarget = this; e.eventPhase = 2;
      const h = this['on' + e.type];
      const all = (this[LS][e.type] || []).slice();
      if (typeof h === 'function') all.unshift({ f: h, once: false });
      for (const x of all) {
        if (x.once) this.removeEventListener(e.type, x.f);
        try { if (typeof x.f === 'function') x.f.call(this, e); else x.f.handleEvent(e); } catch (err) { console.error(err && err.stack ? err.stack : String(err)); }
      }
      e.currentTarget = null; e.eventPhase = 0;
      return !e.defaultPrevented;
    }
  }
  globalThis.EventTarget = EventTarget;

  // ---- the update steps: after a layout or a scroll (PageScripts::ViewChanged), and once soon after observe()
  let pending = false;
  function soon() { if (!pending) { pending = true; requestAnimationFrame(() => { pending = false; update(); }); } }
  function update() {
    for (const l of lists.slice()) {
      const m = ok.matchMedia(l.media);
      if (m === l.matches) continue;
      l.matches = m;
      const e = new Event('change'); e.matches = m; e.media = l.media;
      fireChange(l, e);
    }
    ioUpdate();
    roUpdate();
  }
  ok.onView(update);

  // ---- postMessage to this window (HTML 9.3.3): a message event soon after, with the data as given (not cloned)
  globalThis.postMessage = function (data, target) {
    setTimeout(() => {
      const e = new MessageEvent('message');
      e.data = data; e.origin = location.origin; e.source = globalThis; e.ports = []; e.lastEventId = '';
      dispatchEvent(e);
    }, 0);
  };
  globalThis.getSelection = function () {
    return { rangeCount: 0, isCollapsed: true, type: 'None', anchorNode: null, focusNode: null, toString() { return ''; },
             removeAllRanges() {}, addRange() {}, collapse() {}, empty() {}, getRangeAt() { const e = new Error('no range'); e.name = 'IndexSizeError'; throw e; } };
  };
  Document.prototype.getSelection = function () { return getSelection(); };
  Document.prototype.hasFocus = function () { return true; };
})(globalThis.__okay4);
)JS";

} // namespace

void InstallRound4(PageScripts::Impl& I) {
    js::Runtime& r = I.rt;
    js::Object* g = r.global;
    // ---- interfaces: Node > Element > HTMLElement > HTML...Element; Node > CharacterData > Text / Comment; Node > Document
    js::NativeFunc* nodeCtor = js::DefCtor(r, "Node", IllegalCtor, 0, I.nodeProto);
    nodeCtor->slots.push_back(Str(r, "Node"));
    {   // DOM 4.4 nodeType constants
        static const std::pair<const char*, int> kTypes[] = {{"ELEMENT_NODE", 1}, {"ATTRIBUTE_NODE", 2}, {"TEXT_NODE", 3},
            {"CDATA_SECTION_NODE", 4}, {"PROCESSING_INSTRUCTION_NODE", 7}, {"COMMENT_NODE", 8}, {"DOCUMENT_NODE", 9},
            {"DOCUMENT_TYPE_NODE", 10}, {"DOCUMENT_FRAGMENT_NODE", 11}, {"DOCUMENT_POSITION_DISCONNECTED", 1},
            {"DOCUMENT_POSITION_PRECEDING", 2}, {"DOCUMENT_POSITION_FOLLOWING", 4}, {"DOCUMENT_POSITION_CONTAINS", 8},
            {"DOCUMENT_POSITION_CONTAINED_BY", 16}};
        for (const auto& t : kTypes) { js::DefValue(r, nodeCtor, t.first, Value::Num(t.second), 0); js::DefValue(r, I.nodeProto, t.first, Value::Num(t.second), 0); }
    }
    js::NativeFunc* docCtor = js::DefCtor(r, "Document", IllegalCtor, 0, I.documentProto);
    docCtor->slots.push_back(Str(r, "Document"));
    ClassString(r, I.nodeProto, "Node");
    ClassString(r, I.documentProto, "HTMLDocument");
    ClassString(r, I.eventProto, "Event");
    js::DefValue(r, g, "HTMLDocument", Value::Obj(docCtor));
    I.elementProto = Interface(r, "Element", I.nodeProto);
    I.htmlElementProto = Interface(r, "HTMLElement", I.elementProto);
    I.svgElementProto = Interface(r, "SVGElement", I.elementProto);
    I.characterDataProto = Interface(r, "CharacterData", I.nodeProto);
    I.doctypeProto = Interface(r, "DocumentType", I.nodeProto);
    I.textProto = Interface(r, "Text", I.characterDataProto, [](Runtime& rt, CallArgs& a) -> Value {   // DOM 4.11 new Text(data)
        Node* n = Of(rt).NewNode(rt, NodeKind::Text);
        Of(rt).SetData(rt, *n, a.argc > 0 && !a[0].IsUndef() ? rt.ToUtf8(a[0]) : std::string());
        return WrapNew(rt, a, n, Of(rt).textProto);
    });
    I.commentProto = Interface(r, "Comment", I.characterDataProto, [](Runtime& rt, CallArgs& a) -> Value {
        Node* n = Of(rt).NewNode(rt, NodeKind::Comment);
        Of(rt).SetData(rt, *n, a.argc > 0 && !a[0].IsUndef() ? rt.ToUtf8(a[0]) : std::string());
        return WrapNew(rt, a, n, Of(rt).commentProto);
    });
    {
        struct Tag { const char* iface; const char* tags; };
        static const Tag kTags[] = {
            {"HTMLAnchorElement", "a"}, {"HTMLAreaElement", "area"}, {"HTMLBaseElement", "base"}, {"HTMLBodyElement", "body"},
            {"HTMLBRElement", "br"}, {"HTMLButtonElement", "button"}, {"HTMLCanvasElement", "canvas"}, {"HTMLDataElement", "data"},
            {"HTMLDetailsElement", "details"}, {"HTMLDialogElement", "dialog"}, {"HTMLDivElement", "div"}, {"HTMLDListElement", "dl"},
            {"HTMLEmbedElement", "embed"}, {"HTMLFieldSetElement", "fieldset"}, {"HTMLFormElement", "form"},
            {"HTMLHeadingElement", "h1 h2 h3 h4 h5 h6"}, {"HTMLHeadElement", "head"}, {"HTMLHRElement", "hr"},
            {"HTMLHtmlElement", "html"}, {"HTMLIFrameElement", "iframe"}, {"HTMLImageElement", "img"}, {"HTMLInputElement", "input"},
            {"HTMLLabelElement", "label"}, {"HTMLLegendElement", "legend"}, {"HTMLLIElement", "li"}, {"HTMLLinkElement", "link"},
            {"HTMLMapElement", "map"}, {"HTMLMetaElement", "meta"}, {"HTMLMeterElement", "meter"}, {"HTMLObjectElement", "object"},
            {"HTMLOListElement", "ol"}, {"HTMLOptGroupElement", "optgroup"}, {"HTMLOptionElement", "option"},
            {"HTMLOutputElement", "output"}, {"HTMLParagraphElement", "p"}, {"HTMLPictureElement", "picture"},
            {"HTMLPreElement", "pre"}, {"HTMLProgressElement", "progress"}, {"HTMLQuoteElement", "q blockquote"},
            {"HTMLScriptElement", "script"}, {"HTMLSelectElement", "select"}, {"HTMLSlotElement", "slot"},
            {"HTMLSourceElement", "source"}, {"HTMLSpanElement", "span"}, {"HTMLStyleElement", "style"},
            {"HTMLTableCaptionElement", "caption"}, {"HTMLTableCellElement", "td th"}, {"HTMLTableColElement", "col colgroup"},
            {"HTMLTableElement", "table"}, {"HTMLTableRowElement", "tr"}, {"HTMLTableSectionElement", "thead tbody tfoot"},
            {"HTMLTemplateElement", "template"}, {"HTMLTextAreaElement", "textarea"}, {"HTMLTimeElement", "time"},
            {"HTMLTitleElement", "title"}, {"HTMLTrackElement", "track"}, {"HTMLUListElement", "ul"}};
        for (const Tag& t : kTags) {
            js::Object* proto = Interface(r, t.iface, I.htmlElementProto);
            for (const std::string& tag : Tokens(t.tags)) I.tagProtos[tag] = proto;
        }
        js::Object* media = Interface(r, "HTMLMediaElement", I.htmlElementProto);
        I.tagProtos["audio"] = Interface(r, "HTMLAudioElement", media);
        I.tagProtos["video"] = Interface(r, "HTMLVideoElement", media);
        I.tagProtos["svg"] = Interface(r, "SVGSVGElement", I.svgElementProto);
        for (const char* t : {"path", "g", "circle", "rect", "line", "polyline", "polygon", "ellipse", "use", "symbol", "defs", "tspan",
                              "clippath", "mask", "lineargradient", "radialgradient", "stop", "foreignobject", "pattern", "filter"})
            I.tagProtos[t] = I.svgElementProto;
    }
    // new Image(width, height) / new Audio(src) / new Option(text, value, defaultSelected, selected) (HTML 4.8.3, 4.8.10, 4.10.10)
    struct Named { const char* name; const char* tag; };
    for (const Named& nm : {Named{"Image", "img"}, Named{"Audio", "audio"}, Named{"Option", "option"}}) {
        js::NativeFunc* f = r.NewNative([](Runtime& rt, CallArgs& a) -> Value {
            Impl& P = Of(rt);
            if (!a.newTarget) rt.ThrowType("Failed to construct: use 'new'");
            const std::string tag = rt.ToUtf8(a.callee->slots[0]);
            Node* n = P.NewNode(rt, NodeKind::Element);
            n->name = tag;
            if (tag == "img") {
                if (a.argc > 0 && !a[0].IsUndef()) P.SetAttrC(rt, *n, "width", std::to_string((long)rt.ToIntegerOrInfinity(a[0])));
                if (a.argc > 1 && !a[1].IsUndef()) P.SetAttrC(rt, *n, "height", std::to_string((long)rt.ToIntegerOrInfinity(a[1])));
            } else if (tag == "audio") {
                P.SetAttrC(rt, *n, "preload", "auto");
                if (a.argc > 0 && !a[0].IsUndef()) P.SetAttrC(rt, *n, "src", rt.ToUtf8(a[0]));
            } else {
                if (a.argc > 0 && !a[0].IsUndef()) {
                    Node* t = P.NewNode(rt, NodeKind::Text);
                    P.SetData(rt, *t, rt.ToUtf8(a[0]));
                    P.Insert(rt, n, t, nullptr);
                }
                if (a.argc > 1 && !a[1].IsUndef()) P.SetAttrC(rt, *n, "value", rt.ToUtf8(a[1]));
                if (a.argc > 2 && rt.ToBoolean(a[2])) P.SetAttrC(rt, *n, "selected", std::string());
            }
            auto it = P.tagProtos.find(tag);
            return WrapNew(rt, a, n, it != P.tagProtos.end() ? it->second : P.htmlElementProto);
        }, nm.name, 0, {Str(r, nm.tag)});
        f->ctor = true;
        r.MakeLazyProps(f);
        const Value proto = Value::Obj(I.tagProtos[nm.tag]);
        f->AddRaw(r.Intern("prototype"), proto, 0);
        js::DefValue(r, g, nm.name, Value::Obj(f));
    }

    // ---- events: Event, CustomEvent and the UI Events interfaces; dispatchEvent on nodes and window (DOM 2.9)
    js::NativeFunc* evCtor = js::DefCtor(r, "Event", EventCtor, 1, I.eventProto);
    evCtor->slots.push_back(Str(r, "Event"));
    evCtor->slots.push_back(Value::Num(0));
    for (const auto& c : {std::make_pair("NONE", 0), std::make_pair("CAPTURING_PHASE", 1), std::make_pair("AT_TARGET", 2), std::make_pair("BUBBLING_PHASE", 3)}) {
        js::DefValue(r, evCtor, c.first, Value::Num(c.second), 0);
        js::DefValue(r, I.eventProto, c.first, Value::Num(c.second), 0);
    }
    auto eventIface = [&](const char* name, js::Object* parent, int kind) {
        js::Object* proto = r.NewObject(parent);
        r.Ref(Value::Obj(proto));
        js::NativeFunc* f = js::DefCtor(r, name, EventCtor, 1, proto);
        f->slots.push_back(Str(r, name));
        f->slots.push_back(Value::Num(kind));
        ClassString(r, proto, name);
        return proto;
    };
    eventIface("CustomEvent", I.eventProto, 1);
    I.uiEventProto = eventIface("UIEvent", I.eventProto, 2);
    I.keyboardEventProto = eventIface("KeyboardEvent", I.uiEventProto, 2);
    I.mouseEventProto = eventIface("MouseEvent", I.uiEventProto, 2);
    I.focusEventProto = eventIface("FocusEvent", I.uiEventProto, 2);
    eventIface("InputEvent", I.uiEventProto, 2);
    eventIface("CompositionEvent", I.uiEventProto, 2);
    js::Object* pointer = eventIface("PointerEvent", I.mouseEventProto, 2);
    (void)pointer;
    eventIface("WheelEvent", I.mouseEventProto, 2);
    eventIface("DragEvent", I.mouseEventProto, 2);
    for (const char* n : {"MessageEvent", "ErrorEvent", "ProgressEvent", "PopStateEvent", "HashChangeEvent", "PageTransitionEvent",
                          "StorageEvent", "SubmitEvent", "AnimationEvent", "TransitionEvent", "ClipboardEvent", "PromiseRejectionEvent"})
        eventIface(n, I.eventProto, 2);
    js::DefNative(r, I.nodeProto, "dispatchEvent", [](Runtime& rt, CallArgs& a) -> Value {
        Node* t = ThisNode(rt, a);
        Impl& P = Of(rt);
        if (!a[0].IsObj() || !rt.InstanceOf(a[0], rt.GetGlobal("Event")))
            rt.ThrowType("Failed to execute 'dispatchEvent': the argument is not an Event");
        js::Object* ev = a[0].O();
        for (const Impl::Dispatching& d : P.dispatching)
            if (d.ev == ev) ThrowDom(rt, "InvalidStateError", "the event is already being dispatched");
        const std::string type = rt.ToUtf8(rt.GetStr(a[0], "type"));
        const bool bubbles = rt.ToBoolean(rt.GetStr(a[0], "bubbles")), cancelable = rt.ToBoolean(rt.GetStr(a[0], "cancelable"));
        return Value::Bool(!P.DispatchEvent(t, type, bubbles, cancelable, nullptr, ev));
    }, 1);
    js::DefNative(r, g, "dispatchEvent", [](Runtime& rt, CallArgs& a) -> Value {
        Impl& P = Of(rt);
        if (!a[0].IsObj() || !rt.InstanceOf(a[0], rt.GetGlobal("Event")))
            rt.ThrowType("Failed to execute 'dispatchEvent': the argument is not an Event");
        js::Object* ev = a[0].O();
        for (const Impl::Dispatching& d : P.dispatching)
            if (d.ev == ev) ThrowDom(rt, "InvalidStateError", "the event is already being dispatched");
        const std::string type = rt.ToUtf8(rt.GetStr(a[0], "type"));
        const bool cancelable = rt.ToBoolean(rt.GetStr(a[0], "cancelable"));
        return Value::Bool(!P.DispatchEvent(nullptr, type, false, cancelable, nullptr, ev));
    }, 1);
    // ---- MutationObserver (DOM 4.3)
    I.kObserver = r.AddHostClass({"MutationObserver", nullptr, nullptr, nullptr, nullptr});
    I.moProto = r.NewObject(r.objectProto);
    r.Ref(Value::Obj(I.moProto));
    js::DefCtor(r, "MutationObserver", [](Runtime& rt, CallArgs& a) -> Value {
        Impl& P = Of(rt);
        js::Object* proto = ProtoOfNew(rt, a, P.moProto);
        if (!rt.IsCallable(a[0])) rt.ThrowType("Failed to construct 'MutationObserver': the callback is not a function");
        if (P.moLive >= PageScripts::kMaxObservers) rt.ThrowRange("too many MutationObservers (" + std::to_string(PageScripts::kMaxObservers) + ")");
        js::HostObj* o = rt.NewHost(P.kObserver, proto);
        rt.Keep(Value::Obj(o));
        std::size_t id = 0;
        while (id < P.mos.size() && P.mos[id].live) ++id;
        if (id == P.mos.size()) P.mos.emplace_back();
        o->id = id;
        Impl::Mo& mo = P.mos[id];
        mo.live = true;
        mo.callback = rt.Ref(a[0]);
        mo.self = -1;   // held only while it observes something (observe() takes the ref)
        ++P.moLive;
        return Value::Obj(o);
    }, 1, I.moProto);
    js::DefNative(r, I.moProto, "observe", [](Runtime& rt, CallArgs& a) -> Value {
        Impl& P = Of(rt);
        const int id = ThisObserver(rt, a);
        Node* target = ArgNode(rt, a[0]);
        Impl::MoReg reg;
        reg.observer = id;
        const Value o = a[1];
        auto has = [&](const char* k) { return o.IsObj() && !rt.GetStr(o, k).IsUndef(); };
        auto flag = [&](const char* k) { return o.IsObj() && rt.ToBoolean(rt.GetStr(o, k)); };
        reg.childList = flag("childList");
        reg.subtree = flag("subtree");
        reg.attrOld = flag("attributeOldValue");
        reg.cdOld = flag("characterDataOldValue");
        reg.attributes = has("attributes") ? flag("attributes") : (reg.attrOld || has("attributeFilter"));
        reg.characterData = has("characterData") ? flag("characterData") : reg.cdOld;
        if (has("attributeFilter")) {
            const Value f = rt.GetStr(o, "attributeFilter");
            const std::uint64_t n = std::min<std::uint64_t>(rt.LengthOf(f), 256);
            for (std::uint32_t i = 0; i < n; ++i) reg.filter.push_back(LowerAscii(rt.ToUtf8(rt.GetIndex(f, i))));
        }
        // DOM 4.3.1 observe() steps 3 to 6
        if (!reg.childList && !reg.attributes && !reg.characterData)
            rt.ThrowType("Failed to execute 'observe': one of childList, attributes and characterData must be true");
        if (reg.attrOld && !reg.attributes) rt.ThrowType("Failed to execute 'observe': attributeOldValue needs attributes");
        if (!reg.filter.empty() && !reg.attributes) rt.ThrowType("Failed to execute 'observe': attributeFilter needs attributes");
        if (reg.cdOld && !reg.characterData) rt.ThrowType("Failed to execute 'observe': characterDataOldValue needs characterData");
        auto& list = P.moRegs[target];
        bool replaced = false;
        for (Impl::MoReg& x : list) if (x.observer == id) { x = reg; replaced = true; }
        if (!replaced) {
            if (list.size() >= 1000) rt.ThrowRange("too many observers on one node");
            list.push_back(reg);
        }
        Impl::Mo& mo = P.mos[(std::size_t)id];
        if (mo.self < 0) mo.self = rt.Ref(a.thisv);
        return Value();
    }, 2);
    js::DefNative(r, I.moProto, "disconnect", [](Runtime& rt, CallArgs& a) -> Value {
        Impl& P = Of(rt);
        const int id = ThisObserver(rt, a);
        DropObserverRegs(P, id);
        Impl::Mo& mo = P.mos[(std::size_t)id];
        for (int ref : mo.records) rt.Unref(ref);
        mo.records.clear();
        if (mo.self >= 0) { rt.Unref(mo.self); mo.self = -1; }   // now only the page's own variables hold it (onHostFree lets it go)
        return Value();
    }, 0);
    js::DefNative(r, I.moProto, "takeRecords", [](Runtime& rt, CallArgs& a) -> Value { return TakeRecordsOf(rt, ThisObserver(rt, a)); }, 0);
    // the delivery microtask (DOM 4.3 "notify mutation observers"): each observer with records gets them, in creation order
    js::NativeFunc* deliver = r.NewNative([](Runtime& rt, CallArgs&) -> Value {
        Impl& P = Of(rt);
        P.moScheduled = false;
        for (std::size_t id = 0; id < P.mos.size(); ++id) {
            if (!P.mos[id].live || P.mos[id].records.empty()) continue;
            const Value records = TakeRecordsOf(rt, (int)id);
            const Value cb = rt.GetRef(P.mos[id].callback);
            const Value self = P.mos[id].self >= 0 ? rt.GetRef(P.mos[id].self) : Value::Undef();
            rt.Keep(cb);
            rt.Keep(self);
            try { rt.Call(cb, self, {records, self}); }
            catch (const js::JsThrow&) { P.Log(ConsoleLine::Error, rt.ExceptionText(rt.exception)); rt.exception = Value(); }
        }
        return Value();
    }, "deliverMutations", 0);
    I.moDeliverRef = r.Ref(Value::Obj(deliver));

    // ---- more Element and ChildNode methods (DOM 4.2.6-4.2.8, 4.9; HTML 3.2.6 / 8.4 insertAdjacentHTML; CSSOM View 6.1)
    // before / after / replaceWith (ChildNode): slots[0] 0 before, 1 after, 2 replaceWith
    for (const auto& cn : {std::make_pair("before", 0), std::make_pair("after", 1), std::make_pair("replaceWith", 2)}) {
        js::NativeFunc* f = js::DefNative(r, I.nodeProto, cn.first, [](Runtime& rt, CallArgs& a) -> Value {
            Node* self = ThisNode(rt, a);
            Node* parent = self->parent;
            if (!parent) return Value();
            const int op = (int)a.callee->slots[0].n;
            std::vector<Node*> nodes;
            for (int i = 0; i < a.argc; ++i) nodes.push_back(NodeOrText(rt, a.argv[i]));
            // the "viable" sibling: the first one after `self` that is not being moved (DOM 4.2.8)
            Node* next = nullptr;
            if (op != 0) {
                bool after = false;
                for (auto& c : parent->children) {
                    if (c.get() == self) { after = true; continue; }
                    if (after && std::find(nodes.begin(), nodes.end(), c.get()) == nodes.end()) { next = c.get(); break; }
                }
            }
            Impl& P = Of(rt);
            for (Node* n : nodes) if (n != self) P.Insert(rt, parent, n, op == 0 ? self : next);
            if (op == 2 && std::find(nodes.begin(), nodes.end(), self) == nodes.end()) P.Remove(self);
            return Value();
        }, 0);
        f->slots.push_back(Value::Num(cn.second));
    }
    js::DefNative(r, I.nodeProto, "replaceChildren", [](Runtime& rt, CallArgs& a) -> Value {
        Node* p = ThisNode(rt, a);
        std::vector<Node*> nodes;
        for (int i = 0; i < a.argc; ++i) nodes.push_back(NodeOrText(rt, a.argv[i]));
        Impl& P = Of(rt);
        std::vector<Node*> old;
        for (auto& c : p->children) if (std::find(nodes.begin(), nodes.end(), c.get()) == nodes.end()) old.push_back(c.get());
        for (Node* o : old) P.Remove(o);
        for (Node* n : nodes) P.Insert(rt, p, n, nullptr);
        return Value();
    }, 0);
    // insertAdjacentHTML / insertAdjacentElement / insertAdjacentText: slots[0] 0 html, 1 element, 2 text
    for (const auto& ia : {std::make_pair("insertAdjacentHTML", 0), std::make_pair("insertAdjacentElement", 1), std::make_pair("insertAdjacentText", 2)}) {
        js::NativeFunc* f = js::DefNative(r, I.nodeProto, ia.first, [](Runtime& rt, CallArgs& a) -> Value {
            Node* el = ThisElement(rt, a);
            Impl& P = Of(rt);
            const std::string where = LowerAscii(rt.ToUtf8(a[0]));
            const int kind = (int)a.callee->slots[0].n;
            Node *parent = nullptr, *before = nullptr;
            if (where == "beforebegin") { parent = el->parent; before = el; }
            else if (where == "afterbegin") { parent = el; before = el->children.empty() ? nullptr : el->children.front().get(); }
            else if (where == "beforeend") { parent = el; }
            else if (where == "afterend") {
                parent = el->parent;
                Node *prev, *next;
                Impl::SiblingsOf(el, prev, next);
                before = next;
            } else ThrowDom(rt, "SyntaxError", "'" + where + "' is not one of beforebegin, afterbegin, beforeend and afterend");
            if (!parent || parent->kind == NodeKind::Document) {
                if (kind == 1) return Value::Null();
                if (!parent) return Value();
                ThrowDom(rt, "NoModificationAllowedError", "the element has no parent element");
            }
            if (kind == 1) { Node* n = ArgNode(rt, a[1]); P.Insert(rt, parent, n, before); return a[1]; }
            if (kind == 2) {
                Node* t = P.NewNode(rt, NodeKind::Text);
                P.SetData(rt, *t, rt.ToUtf8(a[1]));
                P.Insert(rt, parent, t, before);
                return Value();
            }
            for (Node* n : ParseFragment(rt, parent, rt.ToUtf8(a[1]))) P.Insert(rt, parent, n, before);
            return Value();
        }, 2);
        f->slots.push_back(Value::Num(ia.second));
    }
    js::DefNative(r, I.nodeProto, "toggleAttribute", [](Runtime& rt, CallArgs& a) -> Value {
        Node* n = ThisElement(rt, a);
        Impl& P = Of(rt);
        const std::string name = LowerAscii(rt.ToUtf8(a[0]));
        if (name.empty() || name.size() > HtmlLimits().maxNameBytes || name.find_first_of(" \t\n\f\r/>=\"'") != std::string::npos)
            rt.ThrowError(js::ErrorType::Error, "InvalidCharacterError: '" + name + "' is not an attribute name");
        const bool has = n->Attr(name.c_str()) != nullptr;
        const bool want = a.argc > 1 && !a[1].IsUndef() ? rt.ToBoolean(a[1]) : !has;
        if (want && !has) { P.SetAttrC(rt, *n, name, std::string()); P.Changed(); }
        if (!want && has) { P.RemoveAttrC(*n, name); P.Changed(); }
        return Value::Bool(want);
    }, 1);
    js::DefNative(r, I.nodeProto, "getAttributeNames", [](Runtime& rt, CallArgs& a) -> Value {
        Node* n = ThisElement(rt, a);
        js::ArrayObj* arr = rt.NewArray(n->attrs.size());
        rt.Keep(Value::Obj(arr));
        std::uint32_t i = 0;
        for (const auto& at : n->attrs) rt.DefineIndex(arr, i++, Str(rt, at.first));
        return Value::Obj(arr);
    }, 0);
    js::DefNative(r, I.nodeProto, "getRootNode", [](Runtime& rt, CallArgs& a) -> Value {
        Node* n = ThisNode(rt, a);
        while (n->parent) n = n->parent;
        return Of(rt).Wrap(n);
    }, 0);
    js::DefNative(r, I.nodeProto, "scrollIntoView", [](Runtime& rt, CallArgs& a) -> Value {   // CSSOM View 6.1: its top to the view's top
        Impl& P = Of(rt);
        PageRect b;
        if (P.BoxOf(ThisNode(rt, a), b, false) && !b.fixed && P.hooks.scrollTo) {
            const PageView v = ViewOf(P);
            const bool end = a[0].IsBool() && !a[0].b;   // scrollIntoView(false): its bottom to the view's bottom
            P.hooks.scrollTo(v.scrollX, end ? b.y + b.h - v.height : b.y);
        }
        return Value();
    }, 0);
    for (const char* n : {"scroll", "scrollTo", "scrollBy"})   // an element's own scrolling is not offered yet: these do nothing
        js::DefNative(r, I.nodeProto, n, [](Runtime&, CallArgs&) -> Value { return Value(); }, 0);

    // ---- window: the view (CSSOM View 4) and scrolling
    struct ViewGetter { const char* name; int what; };
    for (const ViewGetter& vg : {ViewGetter{"innerWidth", 0}, ViewGetter{"innerHeight", 1}, ViewGetter{"outerWidth", 0},
                                 ViewGetter{"outerHeight", 1}, ViewGetter{"scrollX", 2}, ViewGetter{"scrollY", 3},
                                 ViewGetter{"pageXOffset", 2}, ViewGetter{"pageYOffset", 3}}) {
        js::NativeFunc* get = r.NewNative([](Runtime& rt, CallArgs& a) -> Value {
            const PageView v = ViewOf(Of(rt));
            const int w = (int)a.callee->slots[0].n;
            return Value::Num(w == 0 ? std::round(v.width) : w == 1 ? std::round(v.height) : w == 2 ? v.scrollX : v.scrollY);
        }, vg.name, 0, {Value::Num(vg.what)});
        js::Prop* p = g->AddRaw(r.Intern(vg.name), Value(), js::kConfigurable | js::kEnumerable | js::kAccessor);
        p->get = get;
    }
    js::DefValue(r, g, "devicePixelRatio", Value::Num(1), js::kDefault);
    js::DefValue(r, g, "screenX", Value::Num(0), js::kDefault);
    js::DefValue(r, g, "screenY", Value::Num(0), js::kDefault);
    js::DefValue(r, g, "screenLeft", Value::Num(0), js::kDefault);
    js::DefValue(r, g, "screenTop", Value::Num(0), js::kDefault);
    js::DefValue(r, g, "name", Str(r, std::string()), js::kDefault);
    js::DefValue(r, g, "closed", Value::Bool(false), js::kDefault);
    js::DefValue(r, g, "length", Value::Num(0), js::kDefault);
    js::DefValue(r, g, "opener", Value::Null(), js::kDefault);
    js::DefValue(r, g, "frameElement", Value::Null(), js::kDefault);
    for (const char* n : {"top", "parent", "frames"}) js::DefValue(r, g, n, Value::Obj(g), js::kDefault);
    js::DefValue(r, g, "isSecureContext", Value::Bool(ParseUrl(I.url).scheme == "https"), js::kDefault);
    js::DefValue(r, g, "origin", Str(r, OriginOf(I.url).empty() ? std::string("null") : OriginOf(I.url)), js::kDefault);
    {   // screen (CSSOM View 4.3): the view's size, so a page learns nothing else about the machine
        const PageView v = ViewOf(I);
        js::Object* scr = r.NewObject();
        for (const char* n : {"width", "availWidth"}) js::DefValue(r, scr, n, Value::Num(std::round(v.width)), js::kDefault);
        for (const char* n : {"height", "availHeight"}) js::DefValue(r, scr, n, Value::Num(std::round(v.height)), js::kDefault);
        for (const char* n : {"availLeft", "availTop"}) js::DefValue(r, scr, n, Value::Num(0), js::kDefault);
        for (const char* n : {"colorDepth", "pixelDepth"}) js::DefValue(r, scr, n, Value::Num(24), js::kDefault);
        js::Object* orient = r.NewObject();
        js::DefValue(r, orient, "type", Str(r, v.width >= v.height ? "landscape-primary" : "portrait-primary"), js::kDefault);
        js::DefValue(r, orient, "angle", Value::Num(0), js::kDefault);
        js::DefNative(r, orient, "addEventListener", [](Runtime&, CallArgs&) -> Value { return Value(); }, 2);
        js::DefNative(r, orient, "removeEventListener", [](Runtime&, CallArgs&) -> Value { return Value(); }, 2);
        js::DefValue(r, scr, "orientation", Value::Obj(orient), js::kDefault);
        js::DefValue(r, g, "screen", Value::Obj(scr), js::kDefault);
    }
    for (const auto& sc : {std::make_pair("scrollTo", 0), std::make_pair("scroll", 0), std::make_pair("scrollBy", 1)}) {
        js::NativeFunc* f = js::DefNative(r, g, sc.first, [](Runtime& rt, CallArgs& a) -> Value { ScrollPage(rt, a, a.callee->slots[0].n != 0); return Value(); }, 2);
        f->slots.push_back(Value::Num(sc.second));
    }
    // atob / btoa (HTML 8.3)
    js::DefNative(r, g, "btoa", [](Runtime& rt, CallArgs& a) -> Value {
        const std::u16string s = rt.ToU16(a[0]);
        rt.Charge(s.size() / 16);
        std::string out;
        out.reserve((s.size() + 2) / 3 * 4);
        for (std::size_t i = 0; i < s.size(); i += 3) {
            unsigned v = 0;
            int n = 0;
            for (int k = 0; k < 3; ++k) {
                v <<= 8;
                if (i + k < s.size()) {
                    if (s[i + k] > 0xFF) ThrowDom(rt, "InvalidCharacterError", "btoa: the string has characters outside Latin-1");
                    v |= s[i + k];
                    ++n;
                }
            }
            for (int k = 0; k < 4; ++k) out += k <= n ? kB64[(v >> (18 - 6 * k)) & 63] : '=';
        }
        return Str(rt, out);
    }, 1);
    js::DefNative(r, g, "atob", [](Runtime& rt, CallArgs& a) -> Value {
        std::string in;
        for (const char16_t c : rt.ToU16(a[0])) if (!(c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r')) in += c < 128 ? (char)c : '\x01';
        rt.Charge(in.size() / 16);
        if (in.size() % 4 == 0 && !in.empty()) {   // the forgiving-base64 decode: up to two '=' at the end of a whole group
            if (in.back() == '=') in.pop_back();
            if (in.back() == '=') in.pop_back();
        }
        if (in.size() % 4 == 1) ThrowDom(rt, "InvalidCharacterError", "atob: the string is not base64");
        std::u16string out;
        unsigned v = 0;
        int bits = 0;
        for (const char c : in) {
            const char* p = c ? std::strchr(kB64, c) : nullptr;
            if (!p) ThrowDom(rt, "InvalidCharacterError", "atob: the string is not base64");
            v = (v << 6) | (unsigned)(p - kB64);
            bits += 6;
            if (bits >= 8) { bits -= 8; out += (char16_t)((v >> bits) & 0xFF); }
        }
        return Value::Str(rt.NewString(std::move(out)));
    }, 1);

    // ---- the natives the round-4 prelude uses (__okay4, removed by it)
    js::Object* ok = r.NewObject();
    js::DefValue(r, g, "__okay4", Value::Obj(ok), js::kDefault);
    js::DefNative(r, ok, "parseUrl", [](Runtime& rt, CallArgs& a) -> Value {
        const std::string input = rt.ToUtf8(a[0]);
        const std::string base = a[1].IsNullish() ? std::string() : rt.ToUtf8(a[1]);
        rt.Charge((input.size() + base.size()) / 16 + 1);
        WebUrl w;
        if (!ParseWebUrl(input, a[1].IsNullish() ? nullptr : &base, w)) return Value::Null();
        js::ArrayObj* arr = rt.NewArray(10);
        rt.Keep(Value::Obj(arr));
        const std::string* parts[] = {&w.protocol, &w.username, &w.password, &w.hostname, &w.port, &w.pathname, &w.search, &w.hash};
        std::uint32_t i = 0;
        for (const std::string* p : parts) rt.DefineIndex(arr, i++, Str(rt, *p));
        rt.DefineIndex(arr, i++, Value::Bool(w.hasAuthority));
        rt.DefineIndex(arr, i++, Value::Bool(w.special));
        return Value::Obj(arr);
    }, 2);
    js::DefNative(r, ok, "originOf", [](Runtime& rt, CallArgs& a) -> Value {
        WebUrl w;
        const std::string s = rt.ToUtf8(a[0]);
        return Str(rt, ParseWebUrl(s, nullptr, w) ? w.Origin() : std::string("null"));
    }, 1);
    js::DefNative(r, ok, "formDecode", [](Runtime& rt, CallArgs& a) -> Value { return Str(rt, FormUrlDecode(rt.ToUtf8(a[0]))); }, 1);
    js::DefNative(r, ok, "formEncode", [](Runtime& rt, CallArgs& a) -> Value { return Str(rt, FormUrlEncode(rt.ToUtf8(a[0]))); }, 1);
    js::DefNative(r, ok, "rect", [](Runtime& rt, CallArgs& a) -> Value {   // [x, y, w, h, border top, right, bottom, left] in the view
        Node* n = Of(rt).NodeArg(a[0]);
        PageRect b;
        if (!n || !Of(rt).BoxOf(n, b, true)) return Value::Null();
        js::ArrayObj* arr = rt.NewArray(8);
        rt.Keep(Value::Obj(arr));
        const float vals[8] = {b.x, b.y, b.w, b.h, b.border[0], b.border[1], b.border[2], b.border[3]};
        for (std::uint32_t i = 0; i < 8; ++i) rt.DefineIndex(arr, i, Value::Num(vals[i]));
        return Value::Obj(arr);
    }, 1);
    js::DefNative(r, ok, "view", [](Runtime& rt, CallArgs&) -> Value {
        const PageView v = ViewOf(Of(rt));
        js::ArrayObj* arr = rt.NewArray(4);
        rt.Keep(Value::Obj(arr));
        const float vals[4] = {v.width, v.height, v.scrollX, v.scrollY};
        for (std::uint32_t i = 0; i < 4; ++i) rt.DefineIndex(arr, i, Value::Num(vals[i]));
        return Value::Obj(arr);
    }, 0);
    js::DefNative(r, ok, "matchMedia", [](Runtime& rt, CallArgs& a) -> Value {
        Impl& P = Of(rt);
        std::string q = rt.ToUtf8(a[0]);
        if (q.size() > 4096) q.resize(4096);
        rt.Charge(q.size() / 8 + 10);
        if (P.hooks.matchMedia) return Value::Bool(P.hooks.matchMedia(q));
        MediaEnv env;
        const PageView v = ViewOf(P);
        env.width = v.width;
        env.height = v.height;
        return Value::Bool(MediaMatches(ParseMediaQuery(q), env));
    }, 1);
    js::DefNative(r, ok, "now", [](Runtime& rt, CallArgs&) -> Value {   // High Resolution Time 2: ms since the page's scripts started
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Of(rt).startedAt).count();
        return Value::Num(std::floor(ms * 10.0) / 10.0);   // 0.1 ms steps (the standard coarsens the clock; 5.3)
    }, 0);
    js::DefNative(r, ok, "timeOrigin", [](Runtime& rt, CallArgs&) -> Value { return Value::Num(Of(rt).timeOrigin); }, 0);
    js::DefNative(r, ok, "onView", [](Runtime& rt, CallArgs& a) -> Value {
        Impl& P = Of(rt);
        if (rt.IsCallable(a[0]) && P.viewHookRef < 0) P.viewHookRef = rt.Ref(a[0]);
        return Value();
    }, 1);
    {
        const std::string ua = I.hooks.userAgent.empty() ? std::string(PageScripts::kDefaultUserAgent) : I.hooks.userAgent;
        js::DefValue(r, ok, "userAgent", Str(r, ua), js::kDefault);
        js::DefValue(r, ok, "language", Str(r, I.hooks.language.empty() ? std::string("en-US") : I.hooks.language), js::kDefault);
#if defined(_WIN32)
        js::DefValue(r, ok, "platform", Str(r, "Win32"), js::kDefault);
#elif defined(__APPLE__)
        js::DefValue(r, ok, "platform", Str(r, "MacIntel"), js::kDefault);
#else
        js::DefValue(r, ok, "platform", Str(r, "Linux x86_64"), js::kDefault);
#endif
        js::DefValue(r, ok, "cookies", Value::Bool((bool)I.hooks.getCookie), js::kDefault);
    }
    I.timeOrigin = (double)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void Install(PageScripts::Impl& I) {
    js::Runtime& r = I.rt;
    r.userData = &I;
    r.memoryLimit = PageScripts::kMemoryLimit;
    r.onHostFree = [&I](js::HostObj* h) {
        if (h->hostClass == I.kNode && h->id < I.wraps.size() && I.wraps[(std::size_t)h->id] == h) I.wraps[(std::size_t)h->id] = nullptr;
        if (h->hostClass == I.kCollection) I.collections.erase(h);
        if (h->hostClass == I.kObserver && I.kObserver != 0) I.ReleaseObserver((std::size_t)h->id);
    };
    r.printSink = [&I](const std::string& s) { I.Log(I.level, s); };
    I.kNode = r.AddHostClass({"Node", NodeGet, NodeSet, NodeText, nullptr});
    I.kStyle = r.AddHostClass({"CSSStyleDeclaration", StyleGet, StyleSet, nullptr, nullptr});
    I.kClassList = r.AddHostClass({"DOMTokenList", ClassListGet, nullptr, ClassListText, nullptr});
    I.kLocation = r.AddHostClass({"Location", LocationGet, LocationSet, LocationText, nullptr});
    I.kCollection = r.AddHostClass({"HTMLCollection", CollectionGet, nullptr, nullptr, CollectionKeys});
    I.kStorage = r.AddHostClass({"Storage", StorageGet, StorageSet, nullptr, StorageKeys});
    I.kDataset = r.AddHostClass({"DOMStringMap", DatasetGet, DatasetSet, nullptr, DatasetKeys});
    I.ownerKey = r.NewSymbol(r.Intern("owner"));
    r.Ref(Value::Sym(I.ownerKey));
    I.filterKey = r.NewSymbol(r.Intern("filter"));
    r.Ref(Value::Sym(I.filterKey));
    auto proto = [&](js::Object* parent) { js::Object* o = r.NewObject(parent); r.Ref(Value::Obj(o)); return o; };
    I.nodeProto = proto(r.objectProto);
    I.documentProto = proto(I.nodeProto);
    I.styleProto = proto(r.objectProto);
    I.classListProto = proto(r.objectProto);
    I.eventProto = proto(r.objectProto);
    I.locationProto = proto(r.objectProto);
    I.collectionProto = proto(r.objectProto);
    I.storageProto = proto(r.objectProto);
    I.datasetProto = proto(r.objectProto);
    I.computedProto = proto(r.objectProto);
    I.historyProto = proto(r.objectProto);

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
        if (Of(rt).RemoveAttrC(*ThisElement(rt, a), LowerAscii(rt.ToUtf8(a[0])))) Of(rt).Changed();
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
        return Of(rt).Collection(ThisNode(rt, a), 2, LowerAscii(rt.ToUtf8(a[0])));
    }, 1);
    js::DefNative(r, I.nodeProto, "getElementsByClassName", [](Runtime& rt, CallArgs& a) -> Value {
        return Of(rt).Collection(ThisNode(rt, a), 3, rt.ToUtf8(a[0]));
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
    // focus() / blur() (HTML 6.6.6): the tab moves its focus (and fires the focus events back here)
    js::DefNative(r, I.nodeProto, "focus", [](Runtime& rt, CallArgs& a) -> Value {
        Node* n = ThisNode(rt, a);
        Impl& P = Of(rt);
        if (n->kind != NodeKind::Element || !P.InPage(n) || n == P.focused) return Value();
        if (P.hooks.focus) P.hooks.focus(n);
        else P.FocusMoved(P.focused, n);
        return Value();
    }, 0);
    js::DefNative(r, I.nodeProto, "blur", [](Runtime& rt, CallArgs& a) -> Value {
        Node* n = ThisNode(rt, a);
        Impl& P = Of(rt);
        if (n != P.focused) return Value();
        if (P.hooks.focus) P.hooks.focus(nullptr);
        else P.FocusMoved(P.focused, nullptr);
        return Value();
    }, 0);
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
    // document.write / writeln (HTML 8.4.3): only the page's own scripts while it is read; slots[0]: 1 for writeln
    for (int ln = 0; ln < 2; ++ln) {
        js::NativeFunc* f = js::DefNative(r, I.documentProto, ln ? "writeln" : "write", [](Runtime& rt, CallArgs& a) -> Value {
            Impl& P = Of(rt);
            std::string text;
            for (int i = 0; i < a.argc; ++i) text += rt.ToUtf8(a.argv[i]);
            if (a.callee->slots[0].n != 0) text += '\n';
            if (!P.parserScript || !P.currentScript || P.loaded) {
                P.Log(ConsoleLine::Warn, "document.write was ignored: it only works while the page is read (it would replace the page)");
                return Value();
            }
            if (P.writeBuffer.size() + text.size() + P.textBytes > PageScripts::kMaxPageTextBytes)
                rt.ThrowRange("document.write: the page's text would be too big (" + std::to_string(PageScripts::kMaxPageTextBytes >> 20) + " MB)");
            rt.Charge(text.size() / 64 + 1);
            P.writeBuffer += text;
            return Value();
        }, 1);
        f->slots.push_back(Value::Num(ln));
    }

    // ---- live collections: item, namedItem, forEach and iteration (Array.prototype's own iterators work on them)
    js::DefNative(r, I.collectionProto, "item", [](Runtime& rt, CallArgs& a) -> Value {
        std::vector<Node*>& list = Of(rt).CollectionList(rt, ThisCollection(rt, a));
        const double i = rt.ToIntegerOrInfinity(a[0]);
        return i >= 0 && i < (double)list.size() ? Of(rt).Wrap(list[(std::size_t)i]) : Value::Null();
    }, 1);
    js::DefNative(r, I.collectionProto, "namedItem", [](Runtime& rt, CallArgs& a) -> Value {
        const std::string name = rt.ToUtf8(a[0]);
        for (Node* n : Of(rt).CollectionList(rt, ThisCollection(rt, a)))
            if (n->kind == NodeKind::Element && (AttrOr(*n, "id") == name || AttrOr(*n, "name") == name)) return Of(rt).Wrap(n);
        return Value::Null();
    }, 1);
    {
        const Value arrayProto = Value::Obj(r.arrayProto);
        for (const char* m : {"forEach", "keys", "values", "entries"}) {
            const Value fn = r.GetStr(arrayProto, m);
            if (r.IsCallable(fn)) js::DefValue(r, I.collectionProto, m, fn);
        }
        const Value it = r.Get(arrayProto, I.rt.symIterator);
        if (r.IsCallable(it)) r.DefineData(I.collectionProto, r.symIterator, it, js::kHidden);
    }

    // ---- Storage methods
    js::DefNative(r, I.storageProto, "getItem", [](Runtime& rt, CallArgs& a) -> Value {
        StorageArea* area = AreaOf(rt, ThisStorage(rt, a));
        std::string v;
        return area && area->Get(rt.ToUtf8(a[0]), v) ? Str(rt, v) : Value::Null();
    }, 1);
    js::DefNative(r, I.storageProto, "setItem", [](Runtime& rt, CallArgs& a) -> Value {
        StorageWrite(rt, ThisStorage(rt, a), rt.ToUtf8(a[0]), rt.ToUtf8(a[1]));
        return Value();
    }, 2);
    js::DefNative(r, I.storageProto, "removeItem", [](Runtime& rt, CallArgs& a) -> Value {
        js::HostObj* self = ThisStorage(rt, a);
        StorageArea* area = AreaOf(rt, self);
        if (area && area->Remove(rt.ToUtf8(a[0])) && self->i32 == 1 && Of(rt).hooks.storageChanged) Of(rt).hooks.storageChanged();
        return Value();
    }, 1);
    js::DefNative(r, I.storageProto, "clear", [](Runtime& rt, CallArgs& a) -> Value {
        js::HostObj* self = ThisStorage(rt, a);
        StorageArea* area = AreaOf(rt, self);
        if (area && area->Length()) { area->Clear(); if (self->i32 == 1 && Of(rt).hooks.storageChanged) Of(rt).hooks.storageChanged(); }
        return Value();
    }, 0);
    js::DefNative(r, I.storageProto, "key", [](Runtime& rt, CallArgs& a) -> Value {
        StorageArea* area = AreaOf(rt, ThisStorage(rt, a));
        const double i = rt.ToIntegerOrInfinity(a[0]);
        std::string k;
        return area && i >= 0 && i < 4e9 && area->Key((std::size_t)i, k) ? Str(rt, k) : Value::Null();
    }, 1);

    // ---- getComputedStyle's answer: getPropertyValue reads the property of that name
    js::DefNative(r, I.computedProto, "getPropertyValue", [](Runtime& rt, CallArgs& a) -> Value {
        if (!a.thisv.IsObj()) rt.ThrowType("Illegal invocation");
        const Value v = rt.Get(a.thisv, rt.Intern(js::Utf8To16(LowerAscii(rt.ToUtf8(a[0])))));
        return v.IsStr() ? v : Str(rt, std::string());
    }, 1);

    // ---- history (HTML 7.4.2)
    js::DefNative(r, I.historyProto, "pushState", [](Runtime& rt, CallArgs& a) -> Value { PushState(rt, a, false); return Value(); }, 2);
    js::DefNative(r, I.historyProto, "replaceState", [](Runtime& rt, CallArgs& a) -> Value { PushState(rt, a, true); return Value(); }, 2);
    struct Step { const char* name; int delta; };
    for (const Step& st : {Step{"back", -1}, Step{"forward", 1}, Step{"go", 0}}) {
        js::NativeFunc* f = js::DefNative(r, I.historyProto, st.name, [](Runtime& rt, CallArgs& a) -> Value {
            int delta = (int)a.callee->slots[0].n;
            if (delta == 0) {
                const double d = rt.ToIntegerOrInfinity(a[0]);
                delta = d > 1000 ? 1000 : d < -1000 ? -1000 : (int)d;
                if (delta == 0) { Impl& P = Of(rt); if (P.hooks.navigate) P.hooks.navigate(P.url); return Value(); }   // go(0): reload
            }
            if (Of(rt).hooks.traverse) Of(rt).hooks.traverse(delta);
            return Value();
        }, st.delta == 0 ? 1 : 0);
        f->slots.push_back(Value::Num(st.delta));
    }
    js::DefGetter(r, I.historyProto, "length", [](Runtime& rt, CallArgs&) -> Value {
        return Value::Num(Of(rt).hooks.historyLength ? std::max(1, Of(rt).hooks.historyLength()) : 1);
    });
    js::DefGetter(r, I.historyProto, "state", [](Runtime& rt, CallArgs&) -> Value {
        Impl& P = Of(rt);
        if (P.historyState.empty()) return Value::Null();
        if (P.historyStateRef < 0) P.historyStateRef = rt.Ref(ParseJson(rt, P.historyState));
        return rt.GetRef(P.historyStateRef);
    });
    js::DefValue(r, I.historyProto, "scrollRestoration", Str(r, "auto"), js::kDefault);

    // ---- events (DOM 2.2): preventDefault, stopPropagation, stopImmediatePropagation
    js::DefNative(r, I.eventProto, "preventDefault", [](Runtime& rt, CallArgs& a) -> Value {
        if (Impl::Dispatching* d = Of(rt).StateOf(a.thisv)) {
            if (d->cancelable) { d->prevented = true; rt.DefineData(d->ev, rt.Intern("defaultPrevented"), Value::Bool(true)); }
        } else if (a.thisv.IsObj() && rt.ToBoolean(rt.GetStr(a.thisv, "cancelable"))) {   // an event an EventTarget object sends
            rt.DefineData(a.thisv.O(), rt.Intern("defaultPrevented"), Value::Bool(true));
        }
        return Value();
    }, 0);
    js::DefNative(r, I.eventProto, "stopPropagation", [](Runtime& rt, CallArgs& a) -> Value {
        if (Impl::Dispatching* d = Of(rt).StateOf(a.thisv)) d->stop = true;
        else if (a.thisv.IsObj()) rt.DefineData(a.thisv.O(), rt.Intern("cancelBubble"), Value::Bool(true));
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
    // alert / confirm / prompt (HTML 8.8.1): the tab shows the dialog; the script does not wait for it, so confirm answers
    // false and prompt null (what the standard has them return when the browser does not show the dialog). Past
    // kMaxDialogs in one host entry, or with no tab to show them, the message goes to the console.
    struct DialogFn { const char* name; int kind; };
    for (const DialogFn& df : {DialogFn{"alert", PageDialog::Alert}, DialogFn{"confirm", PageDialog::Confirm}, DialogFn{"prompt", PageDialog::Prompt}}) {
        js::NativeFunc* f = js::DefNative(r, g, df.name, [](Runtime& rt, CallArgs& a) -> Value {
            Impl& P = Of(rt);
            PageDialog d;
            d.kind = (PageDialog::Kind)(int)a.callee->slots[0].n;
            d.message = a.argc > 0 ? rt.ToUtf8(a[0]) : std::string();
            if (d.message.size() > 4096) d.message.resize(4096);
            if (d.kind == PageDialog::Prompt && a.argc > 1) { d.defaultText = rt.ToUtf8(a[1]); if (d.defaultText.size() > 4096) d.defaultText.resize(4096); }
            static const char* const kNames[] = {"alert", "confirm", "prompt"};
            if (P.hooks.dialog && P.dialogs < (int)PageScripts::kMaxDialogs) { ++P.dialogs; P.hooks.dialog(d); }
            else P.Log(ConsoleLine::Warn, std::string(kNames[(int)d.kind]) + ": " + d.message);
            return d.kind == PageDialog::Alert ? Value() : d.kind == PageDialog::Confirm ? Value::Bool(false) : Value::Null();
        }, 1);
        f->slots.push_back(Value::Num(df.kind));
    }

    // history, localStorage / sessionStorage (null when the tab keeps none), getComputedStyle
    js::Object* hist = r.NewObject(I.historyProto);
    js::DefValue(r, g, "history", Value::Obj(hist), js::kEnumerable);
    for (int local = 0; local < 2; ++local) {
        js::HostObj* st = r.NewHost(I.kStorage, I.storageProto);
        st->i32 = local;
        I.storageRef[local] = r.Ref(Value::Obj(st));
    }
    js::DefGetter(r, g, "localStorage", [](Runtime& rt, CallArgs&) -> Value {
        Impl& P = Of(rt);
        return P.hooks.storage && P.hooks.storage(true) ? rt.GetRef(P.storageRef[1]) : Value::Null();
    });
    js::DefGetter(r, g, "sessionStorage", [](Runtime& rt, CallArgs&) -> Value {
        Impl& P = Of(rt);
        return P.hooks.storage && P.hooks.storage(false) ? rt.GetRef(P.storageRef[0]) : Value::Null();
    });
    js::DefNative(r, g, "getComputedStyle", [](Runtime& rt, CallArgs& a) -> Value {
        Impl& P = Of(rt);
        Node* n = ArgNode(rt, a[0]);
        if (n->kind != NodeKind::Element) rt.ThrowType("getComputedStyle needs an element");
        rt.Charge(2000);
        js::Object* o = rt.NewObject(P.computedProto);
        rt.Keep(Value::Obj(o));
        ComputedStyle st;
        if (P.InPage(n) && P.hooks.computedStyle && P.hooks.computedStyle(*n, st))
            for (const auto& kv : StyleTexts(st)) {
                const Value v = Str(rt, kv.second);
                rt.DefineData(o, rt.Intern(js::Utf8To16(kv.first)), v);
                if (kv.first.find('-') != std::string::npos) rt.DefineData(o, rt.Intern(js::Utf8To16(CamelName(kv.first))), v);
            }
        return Value::Obj(o);
    }, 1);

    // the native side of fetch / XMLHttpRequest (kPrelude): request(method, url, [[name, value]...], body, callback) -> id
    js::Object* ok = r.NewObject();
    js::DefValue(r, g, "__okay", Value::Obj(ok), js::kDefault);
    js::DefNative(r, ok, "request", [](Runtime& rt, CallArgs& a) -> Value {
        Impl& P = Of(rt);
        if (!rt.IsCallable(a[4])) rt.ThrowType("request: no callback");
        if (P.requests.size() >= PageScripts::kMaxRequests)
            rt.ThrowType("too many requests at once (" + std::to_string(PageScripts::kMaxRequests) + ")");
        ScriptRequest req;
        req.method = UpperAscii(rt.ToUtf8(a[0]));
        if (req.method.empty() || req.method.size() > 16 || req.method.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ-") != std::string::npos ||
            req.method == "CONNECT" || req.method == "TRACE" || req.method == "TRACK")
            rt.ThrowType("'" + req.method + "' is not a method a page may use");
        const std::string target = rt.ToUtf8(a[1]);
        req.url = WithoutFragment(ResolveUrl(P.url, target));
        req.body = a[3].IsNullish() ? std::string() : rt.ToUtf8(a[3]);
        if (req.body.size() > PageScripts::kMaxRequestBodyBytes) rt.ThrowType("the request body is too big");
        rt.Charge(req.body.size() / 64);
        // the page's headers, without the ones only the browser sets (Fetch 2.2.2 "forbidden request-header")
        if (a[2].IsObj()) {
            const std::uint64_t n = std::min<std::uint64_t>(rt.LengthOf(a[2]), 100);
            for (std::uint32_t i = 0; i < n; ++i) {
                const Value pair = rt.GetIndex(a[2], i);
                if (!pair.IsObj()) continue;
                const std::string name = rt.ToUtf8(rt.GetIndex(pair, 0)), value = rt.ToUtf8(rt.GetIndex(pair, 1));
                const std::string low = LowerAscii(name);
                static const char* const kForbidden[] = {"accept-charset", "accept-encoding", "access-control-request-headers",
                    "access-control-request-method", "connection", "content-length", "cookie", "cookie2", "date", "dnt", "expect",
                    "host", "keep-alive", "origin", "referer", "set-cookie", "te", "trailer", "transfer-encoding", "upgrade", "via"};
                bool bad = low.compare(0, 6, "proxy-") == 0 || low.compare(0, 4, "sec-") == 0 || name.empty() || name.size() > 256 ||
                           value.size() > 8192 || name.find_first_of(" \t\r\n:") != std::string::npos ||
                           value.find_first_of("\r\n") != std::string::npos;
                for (const char* f : kForbidden) if (low == f) bad = true;
                if (!bad) req.headers.emplace_back(name, value);
            }
        }
        req.id = P.nextRequest++;
        if (P.nextRequest > 2000000000) P.nextRequest = 1;
        std::string why;
        if (!P.hooks.request || !P.hooks.request(req, why))
            rt.ThrowType(why.empty() ? "network requests are off in this page" : why);
        P.requests[req.id] = rt.Ref(a[4]);
        return Value::Num(req.id);
    }, 5);
    js::DefNative(r, ok, "abort", [](Runtime& rt, CallArgs& a) -> Value {
        Impl& P = Of(rt);
        const double id = rt.ToNumber(a[0]);
        auto it = id >= 1 && id <= 2e9 ? P.requests.find((int)id) : P.requests.end();
        if (it != P.requests.end()) { rt.Unref(it->second); P.requests.erase(it); }
        return Value();
    }, 1);
    js::DefNative(r, ok, "domError", [](Runtime& rt, CallArgs& a) -> Value {
        js::Object* e = rt.NewError(js::ErrorType::Error, rt.ToUtf8(a[1]));
        rt.DefineData(e, rt.Intern("name"), Str(rt, rt.ToUtf8(a[0])), js::kHidden);
        return Value::Obj(e);
    }, 2);
    InstallRound4(I);
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
    Impl::Entry entry(*m);
    std::string err;
    if (!m->rt.Run(kPrelude, "browser prelude", &err)) m->Log(ConsoleLine::Error, err);
    if (!m->rt.Run(kPrelude4, "browser prelude 4", &err)) m->Log(ConsoleLine::Error, err);
}

PageScripts::~PageScripts() = default;

bool PageScripts::RunScript(const std::string& source, const std::string& chunk, const Node* element, bool parser) {
    Impl& I = *m;
    Impl::Entry entry(I);
    const Node* prevScript = I.currentScript;
    const bool prevParser = I.parserScript;
    std::string prevBuffer;
    prevBuffer.swap(I.writeBuffer);
    I.currentScript = element;
    I.parserScript = parser && element != nullptr;
    if (element) I.started.insert(element);
    std::string err;
    const bool ok = I.rt.Run(source, chunk.empty() ? "inline script " + std::to_string(++I.inlineCount) : chunk, &err);
    if (!ok) { I.Log(ConsoleLine::Error, err); I.writeBuffer.clear(); }
    else if (I.parserScript) I.FlushWrite(element);
    I.writeBuffer.swap(prevBuffer);
    I.currentScript = prevScript;
    I.parserScript = prevParser;
    return ok;
}

std::vector<const Node*> PageScripts::TakeWrittenScripts() {
    std::vector<const Node*> out;
    out.swap(m->written);
    return out;
}

void PageScripts::MarkStarted(const Node& script) { m->started.insert(&script); }
bool PageScripts::Started(const Node& script) const { return m->started.count(&script) != 0; }

void PageScripts::PageLoaded() {
    ContentLoaded();
    Loaded();
}

void PageScripts::ContentLoaded() {
    if (m->loaded || m->contentLoaded) return;
    m->contentLoaded = true;
    m->DispatchEvent(m->doc.root.get(), "DOMContentLoaded", true, false);
}

void PageScripts::Loaded() {
    if (m->loaded) return;
    ContentLoaded();
    m->loaded = true;
    m->DispatchEvent(nullptr, "load", false, false);
}

void PageScripts::ElementEvent(const Node& el, const std::string& type) {
    m->DispatchEvent(const_cast<Node*>(&el), type, false, false);
}

void PageScripts::DeliverResponse(int id, const ScriptResponse& r) {
    Impl& I = *m;
    auto it = I.requests.find(id);
    if (it == I.requests.end()) return;   // aborted, or not ours
    const int cbRef = it->second;
    I.requests.erase(it);
    Impl::Entry entry(I);
    js::Runtime& rt = I.rt;
    try {
        const bool tooBig = r.body.size() > kMaxResponseBytes;
        const bool good = r.ok && !tooBig;
        js::ArrayObj* headers = rt.NewArray();
        rt.Keep(js::Value::Obj(headers));
        std::uint32_t n = 0;
        if (good)
            for (const auto& h : r.headers) {
                const std::string low = LowerAscii(h.first);
                if (low == "set-cookie" || low == "set-cookie2") continue;   // Fetch 2.2.2: never shown to the page
                if (n >= 200) break;
                js::ArrayObj* pair = rt.NewArray(2);
                rt.Keep(js::Value::Obj(pair));
                rt.DefineIndex(pair, 0, js::Value::Str(rt.NewStringUtf8(h.first)));
                rt.DefineIndex(pair, 1, js::Value::Str(rt.NewStringUtf8(h.second)));
                rt.DefineIndex(headers, n++, js::Value::Obj(pair));
            }
        rt.Charge(good ? r.body.size() / 64 : 0);
        std::vector<js::Value> args;
        args.push_back(js::Value::Bool(good));
        args.push_back(js::Value::Num(good ? std::max(0, std::min(r.status, 999)) : 0));
        args.push_back(js::Value::Str(rt.NewStringUtf8(good ? r.statusText.substr(0, 256) : std::string())));
        args.push_back(js::Value::Obj(headers));
        args.push_back(js::Value::Str(rt.NewStringUtf8(good ? r.body : std::string())));
        args.push_back(js::Value::Str(rt.NewStringUtf8(r.url)));
        args.push_back(js::Value::Str(rt.NewStringUtf8(tooBig ? "the answer is too big" : r.error.empty() ? "the request failed" : r.error)));
        for (const js::Value& v : args) rt.Keep(v);
        const js::Value cb = rt.GetRef(cbRef);
        I.CallFn(cb, js::Value(), args);
    } catch (const js::JsTerminate& t) {
        I.Log(ConsoleLine::Error, t.why);
    } catch (const js::JsThrow&) {
        I.Log(ConsoleLine::Error, rt.ExceptionText(rt.exception));
        rt.exception = js::Value();
    }
    rt.Unref(cbRef);
}

std::size_t PageScripts::PendingRequests() const { return m->requests.size(); }

namespace {
// UI Events KeyboardEvent.keyCode for the keys that have a well-known one (letters and digits: their upper-case code).
int LegacyKeyCode(const std::string& key) {
    if (key.size() == 1) {
        const char c = key[0];
        if (c >= 'a' && c <= 'z') return c - 32;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ') return c;
        return 0;
    }
    static const std::pair<const char*, int> kCodes[] = {{"Enter", 13}, {"Tab", 9}, {"Escape", 27}, {"Backspace", 8}, {"Delete", 46},
        {"ArrowLeft", 37}, {"ArrowUp", 38}, {"ArrowRight", 39}, {"ArrowDown", 40}, {"Home", 36}, {"End", 35}, {"PageUp", 33},
        {"PageDown", 34}, {"Shift", 16}, {"Control", 17}, {"Alt", 18}, {"Insert", 45}, {"F1", 112}, {"F5", 116}, {"F12", 123}};
    for (const auto& k : kCodes) if (key == k.first) return k.second;
    return 0;
}
} // namespace

bool PageScripts::DispatchKey(const Node* target, const std::string& type, const KeyInfo& k) {
    Impl& I = *m;
    Node* t = const_cast<Node*>(target);
    if (!t || !I.InPage(t)) t = const_cast<Node*>(I.doc.Body());
    if (!t) return false;
    return I.DispatchEvent(t, type, true, true, [&I, &k, &type](js::Object* ev) {
        js::Runtime& rt = I.rt;
        auto put = [&](const char* name, const js::Value& v) { rt.DefineData(ev, rt.Intern(name), v); };
        const int code = k.keyCode ? k.keyCode : LegacyKeyCode(k.key);
        int charCode = 0;
        if (type == "keypress" && !k.key.empty()) { const std::u16string u = js::Utf8To16(k.key); if (u.size() == 1) charCode = u[0]; }
        put("key", js::Value::Str(rt.NewStringUtf8(k.key)));
        put("code", js::Value::Str(rt.NewStringUtf8(k.code)));
        put("keyCode", js::Value::Num(type == "keypress" ? charCode : code));
        put("which", js::Value::Num(type == "keypress" ? charCode : code));
        put("charCode", js::Value::Num(charCode));
        put("shiftKey", js::Value::Bool(k.shift));
        put("ctrlKey", js::Value::Bool(k.ctrl));
        put("altKey", js::Value::Bool(k.alt));
        put("metaKey", js::Value::Bool(k.meta));
        put("repeat", js::Value::Bool(k.repeat));
        put("isComposing", js::Value::Bool(false));
        put("location", js::Value::Num(0));
    }, nullptr, I.keyboardEventProto);
}

bool PageScripts::DispatchMouse(const Node& target, const std::string& type, const MouseInfo& mi) {
    Impl& I = *m;
    Node* t = const_cast<Node*>(&target);
    if (!I.InPage(t)) return false;
    const bool bubbles = type != "mouseenter" && type != "mouseleave";   // UI Events 5.3.3: those two neither bubble nor cancel
    const bool prevented = I.DispatchEvent(t, type, bubbles, bubbles, [&I, &mi](js::Object* ev) {
        js::Runtime& rt = I.rt;
        auto put = [&](const char* name, const js::Value& v) { rt.DefineData(ev, rt.Intern(name), v); };
        for (const char* n : {"clientX", "pageX", "screenX", "x", "offsetX"}) put(n, js::Value::Num(mi.x));
        for (const char* n : {"clientY", "pageY", "screenY", "y", "offsetY"}) put(n, js::Value::Num(mi.y));
        put("button", js::Value::Num(mi.button));
        put("buttons", js::Value::Num(mi.buttons));
        put("detail", js::Value::Num(mi.detail));
        put("shiftKey", js::Value::Bool(mi.shift));
        put("ctrlKey", js::Value::Bool(mi.ctrl));
        put("altKey", js::Value::Bool(mi.alt));
        put("metaKey", js::Value::Bool(mi.meta));
        put("relatedTarget", mi.related && I.InPage(mi.related) ? I.Wrap(const_cast<Node*>(mi.related)) : js::Value::Null());
    }, nullptr, I.mouseEventProto);
    return prevented;
}

void PageScripts::FocusChanged(const Node* from, const Node* to) { m->FocusMoved(from, to); }

void PageScripts::SetUrl(const std::string& url) { m->url = url; }

void PageScripts::SetHistoryState(const std::string& stateJson) {
    m->historyState = stateJson;
    if (m->historyStateRef >= 0) { m->rt.Unref(m->historyStateRef); m->historyStateRef = -1; }
}

void PageScripts::PopState(const std::string& stateJson) {
    SetHistoryState(stateJson);
    Impl& I = *m;
    I.DispatchEvent(nullptr, "popstate", false, false, [&I](js::Object* ev) {
        js::Value st = js::Value::Null();
        if (!I.historyState.empty()) {
            if (I.historyStateRef < 0) I.historyStateRef = I.rt.Ref(ParseJson(I.rt, I.historyState));
            st = I.rt.GetRef(I.historyStateRef);
        }
        I.rt.DefineData(ev, I.rt.Intern("state"), st);
    });
}

bool PageScripts::Dispatch(const Node& target, const std::string& type) {
    Node* t = const_cast<Node*>(&target);   // the tab's own tree: the view hands it back as const
    const bool cancelable = type == "click" || type == "submit";
    return m->DispatchEvent(t, type, true, cancelable);
}

void PageScripts::ViewChanged() {
    Impl& I = *m;
    if (!I.hooks.view) return;
    const PageView v = I.hooks.view();
    Impl::Entry entry(I);
    if (I.viewKnown) {   // CSSOM View 13.1 resize at window, 13.2 scroll at the document (it bubbles to window)
        if (v.width != I.lastView.width || v.height != I.lastView.height) I.DispatchEvent(nullptr, "resize", false, false);
        if (v.scrollX != I.lastView.scrollX || v.scrollY != I.lastView.scrollY) I.DispatchEvent(I.doc.root.get(), "scroll", true, false);
    }
    I.lastView = v;
    I.viewKnown = true;
    if (I.viewHookRef >= 0) {
        const js::Value fn = I.rt.GetRef(I.viewHookRef);
        I.CallFn(fn, js::Value::Obj(I.rt.global), {});
    }
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
            I.started.erase(n);
            I.written.erase(std::remove(I.written.begin(), I.written.end(), n), I.written.end());
            if (I.focused == n) I.focused = nullptr;
            if (I.currentScript == n) I.currentScript = nullptr;
            I.moRegs.erase(n);
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
