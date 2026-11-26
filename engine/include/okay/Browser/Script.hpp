#pragma once
// Browser (page engine): JavaScript in web pages. One PageScripts runs the scripts of one shown page on our own JavaScript
// engine (okay::js, engine/include/okay/JavaScript/JavaScript.hpp) and gives them a first DOM binding, written from the WHATWG
// DOM standard (sections 2 events, 4 nodes) and the HTML standard (8.6 timers, 13.3 serializing fragments):
//   * document: getElementById, querySelector / querySelectorAll (our CSS selector subset, Css.hpp), getElementsByTagName,
//     getElementsByClassName, createElement, createTextNode, body, head, documentElement, title, addEventListener;
//   * elements and text: tagName, id, className, classList, style (written into the style attribute), textContent, innerText,
//     innerHTML (read and write; scripts in it do not run, as in the standard), value and checked (the page's form state,
//     Forms.hpp), get/set/has/removeAttribute, parent / child / sibling links, children, appendChild, insertBefore,
//     removeChild, remove, append, click(), addEventListener / removeEventListener, on<type> properties and attributes;
//   * events: capture, target and bubble phases through the ancestors, document and window; preventDefault, stopPropagation,
//     stopImmediatePropagation; the tab fires click, input, change, submit, DOMContentLoaded and load;
//   * window (the global object): setTimeout / setInterval / clearTimeout / clearInterval, requestAnimationFrame,
//     location.href (setting it asks the tab to go there, by the tab's own rules), console.log / info / warn / error / debug.
// What scripts can NOT do: read or write files (the engine has no file functions and none are bound), reach the network
// (no fetch, no XMLHttpRequest: a page's <script src> files come through the tab's own loader, same rules as its images),
// or run forever: every entry from the host (one script, one event, the timers of one frame) has an instruction budget and
// the heap a size limit; a script that runs past them is stopped with a line in the console, and the page stays up.
// Nodes a script removes are kept until the next layout no longer points at them, so the host never holds a freed node.
// Standard library and okay::js only.
#include "okay/Browser/Html.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace okay::browser {

class FormState;

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
    };

    static constexpr std::uint64_t kBudgetPerCall = 20'000'000;   ///< instructions one host entry may use (about 0.1-0.3 s)
    static constexpr std::size_t kMemoryLimit = 64u << 20;        ///< the page's JavaScript heap
    static constexpr std::size_t kMaxConsoleLines = 500;          ///< the oldest lines go first
    static constexpr std::size_t kMaxTimers = 1000;               ///< pending timers at once; more are refused
    static constexpr std::size_t kMaxNodes = 200000;              ///< nodes a page may have (parsed and made) at once
    static constexpr int kMaxTimersPerTick = 200;                 ///< timers run in one Tick (the rest wait for the next)
    static constexpr std::size_t kMaxPageTextBytes = 64u << 20;   ///< text, names, attributes and form values of the page's nodes

    /// `doc` and `forms` belong to the tab and must outlive this; `pageUrl` is the page's address (location.href).
    PageScripts(HtmlDocument& doc, FormState& forms, const std::string& pageUrl, Hooks hooks);
    ~PageScripts();
    PageScripts(const PageScripts&) = delete;
    PageScripts& operator=(const PageScripts&) = delete;

    /// Run one script (`source`; `chunk` names it in error lines: its address, or "inline script N"). A syntax or run
    /// error is written to the console and false is returned; the page goes on.
    bool RunScript(const std::string& source, const std::string& chunk);
    /// The page's scripts have run: fire DOMContentLoaded (document) and load (window).
    void PageLoaded();
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
bool IsClassicScriptType(const std::string& type);

} // namespace okay::browser
