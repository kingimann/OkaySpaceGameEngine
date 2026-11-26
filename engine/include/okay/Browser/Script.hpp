#pragma once
// Browser (page engine): JavaScript in web pages. One PageScripts runs the scripts of one shown page on our own JavaScript
// engine (okay::js, engine/include/okay/JavaScript/JavaScript.hpp) and gives them a DOM binding, written from the WHATWG
// DOM standard (sections 2 events, 4 nodes, 4.2.10 HTMLCollection), the HTML standard (7.4 history, 8.4 dialogs, 8.6 timers,
// 8.4.3 document.write, 12 web storage, 13.3 serializing fragments), the Fetch standard (5 the fetch() method, Headers,
// Response), XMLHttpRequest (the WHATWG living standard), UI Events (keyboard, mouse and focus events) and CSSOM
// (getComputedStyle):
//   * document: getElementById, querySelector / querySelectorAll (our CSS selector subset, Css.hpp; a static list),
//     getElementsByTagName / getElementsByClassName (live HTMLCollections), createElement, createTextNode, body, head,
//     documentElement, title, cookie, activeElement, currentScript, write / writeln, addEventListener;
//   * elements and text: tagName, id, className, classList, style, dataset (read and write), textContent, innerText,
//     innerHTML (scripts in it do not run, as in the standard), value and checked (Forms.hpp), attributes, parent / child /
//     sibling links, children and childNodes (live), appendChild, insertBefore, removeChild, remove, append, click(),
//     focus(), blur(), addEventListener / removeEventListener, on<type> properties and attributes;
//   * events: capture, target and bubble phases; preventDefault, stopPropagation, stopImmediatePropagation; the tab fires
//     click, input, change, submit, keydown / keyup / keypress, mousedown / mouseup / mousemove / mouseover / mouseout,
//     focus / blur / focusin / focusout, popstate, DOMContentLoaded and load;
//   * window (the global object): timers, requestAnimationFrame, location, history (pushState / replaceState / back /
//     forward / go / state / length), localStorage / sessionStorage, fetch() and XMLHttpRequest, getComputedStyle,
//     alert / confirm / prompt, console.
// Scripts added by a script run: an inline one at once when it is put into the page, a src one when its file arrives (the
// tab fetches it). A <style> added or changed by a script is applied at the next layout.
// What scripts can NOT do: read or write files (the engine has no file functions and none are bound), reach the network
// on their own (fetch and XMLHttpRequest go through the tab, which applies its own rules: in the launcher only the page's
// own site; Set-Cookie answers are never shown to the page), wait for a dialog (alert / confirm / prompt do not stop the
// script: the tab shows the dialog, confirm answers false and prompt null, as the standard allows when the browser does
// not show the dialog modally), or run forever: every entry from the host (one script, one event, the timers of one frame,
// one network answer) has an instruction budget and the heap a size limit; a script that runs past them is stopped with a
// line in the console, and the page stays up.
// Nodes a script removes are kept until the next layout no longer points at them, so the host never holds a freed node.
// Standard library and okay::js only.
#include "okay/Browser/Css.hpp"
#include "okay/Browser/Html.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace okay::browser {

class FormState;
class StorageArea;

/// A fetch() or XMLHttpRequest a page asked for; the tab checks it, adds cookies and hands it to the host.
struct ScriptRequest {
    int id = 0;
    std::string method = "GET";
    std::string url;   ///< absolute
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};
/// The answer for a ScriptRequest. `ok` false: a network error (`error` says why; the page sees a TypeError / onerror).
struct ScriptResponse {
    bool ok = false;
    int status = 0;
    std::string statusText;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    std::string url;   ///< the final address after redirects ("" = the request's)
    std::string error;
};
/// A key event (UI Events KeyboardEvent): key is "a", "Enter", "ArrowLeft" ...; code is the physical key ("KeyA").
struct KeyInfo {
    std::string key, code;
    int keyCode = 0;
    bool shift = false, ctrl = false, alt = false, meta = false, repeat = false;
};
/// A mouse event (UI Events MouseEvent): page view coordinates in CSS px, button 0 left / 1 middle / 2 right.
struct MouseInfo {
    float x = 0, y = 0;
    int button = 0, buttons = 0, detail = 1;
    bool shift = false, ctrl = false, alt = false, meta = false;
    const Node* related = nullptr;   ///< mouseover: the element left; mouseout: the element entered
};
/// alert / confirm / prompt: the tab shows it; the script did not wait for it.
struct PageDialog {
    enum Kind { Alert, Confirm, Prompt } kind = Alert;
    std::string message, defaultText;
};

/// One line of a page's console.
struct ConsoleLine {
    enum Level { Log, Warn, Error } level = Log;
    std::string text;
};

class PageScripts {
public:
    /// What a script changes outside the tree, handed to the tab.
    struct Hooks {
        std::function<void()> changed;                           ///< the tree, an attribute or a form value changed: lay out again
        std::function<void(const std::string&)> title;           ///< document.title was set
        std::function<void(const std::string&)> navigate;        ///< location.href was set (as written; the tab resolves and checks it)
        // ---- round 3 (each may be empty: the feature is then off in the page)
        /// fetch() / XMLHttpRequest: the tab checks `req` (its rules, cookies) and queues it; false and `why` to refuse it.
        std::function<bool(ScriptRequest& req, std::string& why)> request;
        std::function<std::string()> getCookie;                  ///< document.cookie (no HttpOnly cookies)
        std::function<void(const std::string&)> setCookie;       ///< document.cookie = "..."
        /// The page origin's localStorage (`local`) or sessionStorage area, or null (storage off). Asked each time.
        std::function<StorageArea*(bool local)> storage;
        std::function<void()> storageChanged;                    ///< a localStorage write (the host saves the store)
        std::size_t storageQuota = 5u << 20;
        /// history.pushState / replaceState: `url` is absolute and of the page's origin; false refuses it.
        std::function<bool(const std::string& url, const std::string& stateJson, bool replace)> pushState;
        std::function<void(int delta)> traverse;                 ///< history.back / forward / go (done after the script)
        std::function<int()> historyLength;
        std::function<void(const Node* el)> focus;               ///< el.focus() / el.blur() (null): the tab moves the focus
        std::function<void(const Node& el, const std::string& url)> loadScript;   ///< a <script src> a script added (absolute)
        std::function<void()> styles;                            ///< a <style> or <link rel=stylesheet> changed
        std::function<void(const PageDialog&)> dialog;           ///< alert / confirm / prompt
        /// getComputedStyle(el): the tab computes the style with its sheets; false when it can not.
        std::function<bool(const Node& el, ComputedStyle& out)> computedStyle;
    };

    static constexpr std::uint64_t kBudgetPerCall = 20'000'000;   ///< instructions one host entry may use (about 0.1-0.3 s)
    static constexpr std::size_t kMemoryLimit = 64u << 20;        ///< the page's JavaScript heap
    static constexpr std::size_t kMaxConsoleLines = 500;          ///< the oldest lines go first
    static constexpr std::size_t kMaxTimers = 1000;               ///< pending timers at once; more are refused
    static constexpr std::size_t kMaxNodes = 200000;              ///< nodes a page may have (parsed and made) at once
    static constexpr int kMaxTimersPerTick = 200;                 ///< timers run in one Tick (the rest wait for the next)
    static constexpr std::size_t kMaxPageTextBytes = 64u << 20;   ///< text, names, attributes and form values of the page's nodes
    static constexpr std::size_t kMaxRequests = 64;               ///< fetch / XMLHttpRequest waiting at once; more are refused
    static constexpr std::size_t kMaxRequestBodyBytes = 16u << 20;///< a request body a page may send
    static constexpr std::size_t kMaxStateBytes = 640u << 10;     ///< a history state (as JSON)
    static constexpr std::size_t kMaxDialogs = 3;                 ///< dialogs one host entry may open (the rest go to the console)
    static constexpr int kMaxEventDepth = 32;                     ///< events dispatched inside each other (click() in a click ...)

    /// `doc` and `forms` belong to the tab and must outlive this; `pageUrl` is the page's address (location.href).
    PageScripts(HtmlDocument& doc, FormState& forms, const std::string& pageUrl, Hooks hooks);
    ~PageScripts();
    PageScripts(const PageScripts&) = delete;
    PageScripts& operator=(const PageScripts&) = delete;

    /// Run one script (`source`; `chunk` names it in error lines: its address, or "inline script N"). A syntax or run
    /// error is written to the console and false is returned; the page goes on.
    /// `element` is the <script> it came from (document.currentScript), or null. `parser`: the page's own script (one the
    /// tab read from the page or one document.write wrote): document.write puts its text right after `element`. Any other
    /// script's document.write is ignored with a line in the console (the standard would wipe the page).
    bool RunScript(const std::string& source, const std::string& chunk, const Node* element = nullptr, bool parser = true);
    /// The <script> elements document.write put into the page during the last RunScript, in order (the tab runs them next).
    std::vector<const Node*> TakeWrittenScripts();
    /// The page's scripts have run: fire DOMContentLoaded (document) and load (window).
    void PageLoaded();
    /// The parts of PageLoaded: DOMContentLoaded once the parser's scripts ran; load once the async ones ran too.
    void ContentLoaded();
    void Loaded();
    /// A script the tab is about to run (parser script, written script or one a script added) is marked started, so it is
    /// never run twice.
    void MarkStarted(const Node& script);
    bool Started(const Node& script) const;

    // ---- round 3: what the tab passes on
    /// The answer for fetch / XMLHttpRequest `id` (ScriptRequest::id): the page's promise or onload / onerror runs.
    void DeliverResponse(int id, const ScriptResponse& r);
    std::size_t PendingRequests() const;
    static constexpr std::size_t kMaxResponseBytes = 16u << 20;   ///< a longer answer is a network error for the page
    /// A plain event at `el` that does not bubble and can not be cancelled (a <script>'s load / error).
    void ElementEvent(const Node& el, const std::string& type);
    /// A key event at `target` (the focused element, or the body): bubbles and may be cancelled; true when it was.
    bool DispatchKey(const Node* target, const std::string& type, const KeyInfo& k);
    /// A mouse event at `target`; true when it was cancelled.
    bool DispatchMouse(const Node& target, const std::string& type, const MouseInfo& m);
    /// The focus moved (the person or a script): blur and focusout at `from`, focus and focusin at `to` (either may be null).
    void FocusChanged(const Node* from, const Node* to);
    /// The page's address changed without a load (history.pushState, back / forward between its own entries).
    void SetUrl(const std::string& url);
    /// history.state (JSON text, "" = null).
    void SetHistoryState(const std::string& stateJson);
    /// Back / forward between this page's own entries: popstate with the entry's state.
    void PopState(const std::string& stateJson);
    /// Fire event `type` ("click", "input", "change", "submit") at `target` the way a person's action does: it bubbles
    /// (input and change do too) and may be cancelled. True when a listener called preventDefault() (the tab then skips
    /// what the action would do: follow the link, send the form ...).
    bool Dispatch(const Node& target, const std::string& type);
    /// Move the page's clock on by `ms` and run the timers that are due (and the animation frame callbacks).
    void Tick(double ms);
    /// Milliseconds until the next timer is due (-1: none). The host may sleep that long.
    double NextTimerIn() const;
    std::size_t PendingTimers() const;

    const std::vector<ConsoleLine>& Console() const;
    void ClearConsole();
    /// A line from the browser itself (a script that was not loaded ...).
    void AddConsole(ConsoleLine::Level level, const std::string& text);
    /// Every line written so far (also the ones dropped from Console()); a host that shows the console compares it.
    std::uint64_t ConsoleCount() const;

    /// Free the removed nodes no script can reach any more. Call only when nothing outside (a layout, the focus) points at
    /// a removed node: `forget` is called for each node before it is freed (the tab drops it from its form state). It runs
    /// the JavaScript collector first, so without `force` it waits until kFreeAfter trees were removed.
    void FreeRemoved(const std::function<void(const Node&)>& forget, bool force = false);
    static constexpr std::size_t kFreeAfter = 64;
    /// Nodes removed from the page and not freed yet.
    std::size_t RemovedCount() const;
    /// True when `n` is in the page's tree (not removed, not made and never added).
    bool Connected(const Node* n) const;

    /// Change the budget per host entry (tests).
    void SetBudget(std::uint64_t instructions);
    /// Collect at every safe point (tests: finds a value the binding forgot to keep).
    void SetGcStress(bool on);

    struct Impl;   ///< engine/src/Browser/Script.cpp
private:
    std::unique_ptr<Impl> m;
};

/// Is `type` (a <script type="..."> value, "" when there is none) a classic script we run? (HTML 4.12.1.1: the JavaScript
/// MIME type essences, ignoring case and parameters.) "module", JSON and template types are not run.
bool IsJavaScriptType(const std::string& type);

/// The web API a console error line of a page's script says is missing, or "" (Site check, tools/sitecheck). Read from our
/// JavaScript engine's own wording: a standard web or JavaScript global that is not defined ("'IntersectionObserver' is not
/// defined"), or a call of something that is undefined ("'document.fonts.load' is undefined, not a function") on one of the
/// page's host objects (window, document, navigator ...), a built-in (Object, Array ...) or a DOM method name ("*.closest": the object's type is not known). A name the page
/// itself was meant to define (a script that did not load) is not counted. PageScripts adds "not implemented: X" to the console
/// (a warning) after such an error line, so the page's console and the site check say what to build.
std::string MissingWebApi(const std::string& errorLine);
/// Is `name` one of the JavaScript language's own globals (Object, Date, Map, decodeURIComponent, typed arrays ...; ECMA-262
/// section 19)? A missing one is a gap in our JavaScript engine rather than in the DOM binding.
bool IsJavaScriptGlobal(const std::string& name);

} // namespace okay::browser
