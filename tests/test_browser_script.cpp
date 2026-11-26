// Browser (page engine), round 2: JavaScript in web pages (okay/Browser/Script.hpp) on our own engine okay::js. The DOM
// binding (finding, reading and changing elements, innerHTML, classList, style, form values), events (click / input /
// submit through the tab, capture and bubble order, preventDefault, stopPropagation, once, on... attributes and properties),
// timers on the page's clock, the console, <script src> through the tab's loader in page order, location changes by the
// link rules, the budget that stops a hanging page, no file or network functions, and the removed-node bookkeeping.
#include "test_framework.hpp"
#include "okay/Browser/Browser.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace okay::browser;

namespace {

const FixedMeasure kMeasure(0.5f);

// The element with id `id` (HtmlDocument::FindAnchor: the first element with that id), or null.
const Node* ById(const HtmlDocument& doc, const std::string& id) { return doc.FindAnchor(id); }

bool ConsoleHas(const std::vector<ConsoleLine>& lines, const std::string& part, ConsoleLine::Level level = ConsoleLine::Log) {
    for (const ConsoleLine& l : lines) if (l.level == level && l.text.find(part) != std::string::npos) return true;
    return false;
}

std::string Dump(const std::vector<ConsoleLine>& lines) {
    std::string s;
    for (const ConsoleLine& l : lines) s += "  [" + std::to_string((int)l.level) + "] " + l.text + "\n";
    return s;
}

// A page and its scripts without a tab (the tests that need the collector stressed or the budget changed first).
struct Page {
    HtmlDocument doc;
    FormState forms;
    int changes = 0;
    std::string title, nav;
    std::unique_ptr<PageScripts> js;
    explicit Page(const std::string& html, bool stress = false) {
        doc = ParseHtml(html);
        forms.Reset(doc);
        PageScripts::Hooks h;
        h.changed = [this]() { ++changes; };
        h.title = [this](const std::string& t) { title = t; };
        h.navigate = [this](const std::string& u) { nav = u; };
        js.reset(new PageScripts(doc, forms, "https://site.test/page.html", h));
        js->SetGcStress(stress);
    }
    bool Run(const std::string& src) { return js->RunScript(src, "test.js"); }
    const Node& El(const char* id) const {
        const Node* n = ById(doc, id);
        if (!n) { std::cerr << "no #" << id << "\n"; std::abort(); }
        return *n;
    }
    // Evaluate an expression and return it as text (through a global the test reads back with console.log).
    std::string Eval(const std::string& expr) {
        js->ClearConsole();
        Run("console.log(String(" + expr + "))");
        return js->Console().empty() ? std::string("<no output>") : js->Console().back().text;
    }
};

} // namespace

int main() {
    RUN_SUITE("browser script");

    // ---- script types (HTML 4.12.1.1)
    CHECK(IsJavaScriptType("") && IsJavaScriptType("text/javascript") && IsJavaScriptType(" Text/JavaScript; charset=utf-8"));
    CHECK(IsJavaScriptType("application/ecmascript"));
    CHECK(!IsJavaScriptType("module") && !IsJavaScriptType("application/json") && !IsJavaScriptType("text/template"));

    // ---- finding and changing elements, with the collector run at every safe point
    {
        Page p("<div id=box class='a b'><p id=t>old</p><ul id=list><li>1</li><li class=x>2</li></ul></div>"
               "<input id=f value=start><span id=s style='color: red'>s</span>",
               true);
        CHECK(p.Run("document.getElementById('t').textContent = 'new text'"));
        CHECK(p.El("t").TextContent() == "new text");
        CHECK(p.changes > 0);
        CHECK(p.Eval("document.querySelectorAll('#list li').length") == "2");
        CHECK(p.Eval("document.querySelector('li.x').textContent") == "2");
        CHECK(p.Eval("document.querySelector('nothing')") == "null");
        CHECK(p.Eval("document.getElementById('t').tagName") == "P");
        CHECK(p.Eval("document.getElementById('t') === document.querySelector('#t')") == "true");   // one wrapper per node
        CHECK(p.Eval("document.getElementById('t').parentNode.id") == "box");
        CHECK(p.Eval("document.body.children.length") == "3");
        CHECK(p.Run("const li = document.createElement('li'); li.textContent = 'three'; li.className = 'new';"
                    "document.getElementById('list').appendChild(li);"));
        CHECK(p.Eval("document.querySelectorAll('li').length") == "3");
        CHECK(p.Eval("document.getElementById('list').lastChild.textContent") == "three");
        CHECK(p.Run("document.getElementById('list').innerHTML = '<li>a &amp; b</li><li><b>c</b></li>'"));
        CHECK(p.Eval("document.getElementById('list').innerHTML") == "<li>a &amp; b</li><li><b>c</b></li>");
        CHECK(p.Eval("document.getElementById('list').children[1].firstChild.tagName") == "B");
        CHECK(p.Eval("document.getElementById('t').outerHTML") == "<p id=\"t\">new text</p>");
        CHECK(p.Run("const b = document.getElementById('box'); b.classList.add('c'); b.classList.remove('a'); b.classList.toggle('d');"));
        CHECK(*p.El("box").Attr("class") == "b c d");
        CHECK(p.Eval("document.getElementById('box').classList.contains('c')") == "true");
        CHECK(p.Run("const s = document.getElementById('s'); s.style.backgroundColor = 'blue'; s.style.color = '';"));
        CHECK(*p.El("s").Attr("style") == "background-color: blue;");
        CHECK(p.Eval("document.getElementById('s').style.backgroundColor") == "blue");
        CHECK(p.Run("document.getElementById('s').setAttribute('data-n', '5')"));
        CHECK(p.Eval("document.getElementById('s').dataset.n") == "5");
        CHECK(p.Eval("document.getElementById('s').getAttribute('DATA-N')") == "5");
        // form values come from the page's form state
        CHECK(p.Eval("document.getElementById('f').value") == "start");
        CHECK(p.Run("document.getElementById('f').value = 'typed'"));
        CHECK(p.forms.Value(p.El("f")) == "typed");
        // removing, inserting before, contains, closest, matches
        CHECK(p.Run("const t = document.getElementById('t'); t.remove();"));
        CHECK(ById(p.doc, "t") == nullptr);
        CHECK(p.Run("const n = document.createElement('em'); n.id = 'first'; const box = document.getElementById('box');"
                    "box.insertBefore(n, box.firstChild);"));
        CHECK(p.El("box").children.front()->name == "em");
        CHECK(p.Eval("document.body.contains(document.getElementById('first'))") == "true");
        CHECK(p.Eval("document.getElementById('first').closest('div').id") == "box");
        CHECK(p.Eval("document.getElementById('first').matches('#box > em')") == "true");
        CHECK(p.Eval("document.title = 'Script title'") == "Script title");
        CHECK(p.title == "Script title");
        CHECK(p.Eval("document.title") == "Script title");
        // hierarchy errors are JavaScript errors, not crashes
        CHECK(p.Eval("(() => { try { const b = document.getElementById('box'); b.firstChild.appendChild(b); return 'no'; } catch (e) { return e.message; } })()")
                  .find("HierarchyRequestError") != std::string::npos);
        CHECK(p.Eval("(() => { try { document.createElement('a b'); return 'no'; } catch (e) { return 'threw'; } })()") == "threw");
        CHECK(p.Eval("(() => { try { document.querySelector('[[['); return 'no'; } catch (e) { return e.name; } })()") == "SyntaxError");
    }

    // ---- events: capture, target, bubble; preventDefault; stopPropagation; once; handlers
    {
        Page p("<div id=outer><a id=link href='/x'><span id=in>go</span></a></div><button id=b onclick='log.push(\"attr\"); return false'>b</button>", true);
        CHECK(p.Run("var log = [];"
                    "document.getElementById('outer').addEventListener('click', e => log.push('outer-capture:' + e.eventPhase), true);"
                    "document.getElementById('outer').addEventListener('click', e => log.push('outer:' + e.target.id));"
                    "document.getElementById('link').addEventListener('click', e => { log.push('link'); e.preventDefault(); });"
                    "document.getElementById('in').addEventListener('click', function (e) { log.push('in:' + (this === e.currentTarget)); });"
                    "document.addEventListener('click', () => log.push('doc'));"
                    "window.addEventListener('click', () => log.push('win'));"));
        CHECK(p.js->Dispatch(p.El("in"), "click"));   // the link's listener cancelled it
        CHECK(p.Eval("log.join(',')") == "outer-capture:1,in:true,link,outer:in,doc,win");
        CHECK(p.Run("log = []; document.getElementById('link').addEventListener('click', e => e.stopPropagation());"));
        p.js->Dispatch(p.El("in"), "click");
        CHECK(p.Eval("log.join(',')") == "outer-capture:1,in:true,link");
        // once, and removeEventListener
        CHECK(p.Run("log = []; const f = () => log.push('f'); const o = document.getElementById('outer');"
                    "o.addEventListener('ping', () => log.push('once'), {once: true}); o.addEventListener('ping', f); o.removeEventListener('ping', f);"));
        p.js->Dispatch(p.El("outer"), "ping");
        p.js->Dispatch(p.El("outer"), "ping");
        CHECK(p.Eval("log.join(',')") == "once");
        // an on...="" attribute is a function of `event`; returning false cancels
        CHECK(p.Run("log = [];"));
        CHECK(p.js->Dispatch(p.El("b"), "click"));
        CHECK(p.Eval("log.join(',')") == "attr,doc,win");   // it bubbles on to the document and window listeners
        // an on... property replaces the attribute's handler
        CHECK(p.Run("log = []; document.getElementById('b').onclick = () => log.push('prop');"));
        CHECK(!p.js->Dispatch(p.El("b"), "click"));
        CHECK(p.Eval("log.join(',')") == "prop,doc,win");
        // element.click() from a script runs the listeners at once
        CHECK(p.Run("log = []; document.getElementById('b').click();"));
        CHECK(p.Eval("log.join(',')") == "prop,doc,win");
        // a listener that throws: the error goes to the console and the others still run
        CHECK(p.Run("log = []; const o2 = document.getElementById('outer'); o2.addEventListener('boom', () => { throw new Error('bad listener'); });"
                    "o2.addEventListener('boom', () => log.push('after'));"));
        p.js->Dispatch(p.El("outer"), "boom");
        CHECK(ConsoleHas(p.js->Console(), "bad listener", ConsoleLine::Error));
        CHECK(p.Eval("log.join(',')") == "after");
    }

    // ---- timers on the page's clock
    {
        Page p("<p id=t>0</p>", true);
        CHECK(p.Run("var n = 0, order = [];"
                    "setTimeout(() => order.push('b'), 20); setTimeout((x) => order.push(x), 10, 'a');"
                    "const id = setTimeout(() => order.push('never'), 5); clearTimeout(id);"
                    "const iv = setInterval(() => { n++; document.getElementById('t').textContent = String(n); if (n == 3) clearInterval(iv); }, 100);"
                    "requestAnimationFrame(ts => order.push('frame'));"));
        CHECK(p.js->PendingTimers() == 4);
        p.js->Tick(0);
        CHECK(p.Eval("order.join(',')") == "frame");
        p.js->Tick(15);
        CHECK(p.Eval("order.join(',')") == "frame,a");
        p.js->Tick(10);
        CHECK(p.Eval("order.join(',')") == "frame,a,b");
        for (int i = 0; i < 10; ++i) p.js->Tick(100);
        CHECK(p.El("t").TextContent() == "3");
        CHECK(p.js->PendingTimers() == 0);
        CHECK(p.js->NextTimerIn() < 0);
        CHECK(p.Eval("(() => { try { setTimeout('alert(1)', 1); return 'ran'; } catch (e) { return e.name; } })()") == "TypeError");
    }

    // ---- console levels, and errors with where they happened
    {
        Page p("<p>x</p>");
        CHECK(p.Run("console.log('hello', 1 + 1); console.warn('careful'); console.error('broken', {a: 1});"));
        CHECK(ConsoleHas(p.js->Console(), "hello 2"));
        CHECK(ConsoleHas(p.js->Console(), "careful", ConsoleLine::Warn));
        CHECK(ConsoleHas(p.js->Console(), "broken", ConsoleLine::Error));
        CHECK(!p.js->RunScript("let a = 1;\nnotDefined();", "https://site.test/app.js"));
        CHECK(ConsoleHas(p.js->Console(), "app.js:2", ConsoleLine::Error));
        CHECK(ConsoleHas(p.js->Console(), "ReferenceError", ConsoleLine::Error));
        CHECK(!p.js->RunScript("let = = 3", ""));
        CHECK(ConsoleHas(p.js->Console(), "SyntaxError", ConsoleLine::Error));
        if (!ConsoleHas(p.js->Console(), "SyntaxError", ConsoleLine::Error)) std::cerr << Dump(p.js->Console());
    }

    // ---- what a page can not do: no files, no network, no endless loops
    {
        Page p("<p id=t>x</p>");
        CHECK(p.Eval("[typeof require, typeof fetch, typeof XMLHttpRequest, typeof process, typeof importScripts, typeof WebSocket].join()") ==
              "undefined,undefined,undefined,undefined,undefined,undefined");
        CHECK(p.Eval("(() => { try { eval('1'); return 'ran'; } catch (e) { return e.name; } })()") == "EvalError");
        p.js->SetBudget(200000);
        CHECK(!p.Run("while (true) {}"));
        CHECK(ConsoleHas(p.js->Console(), "budget", ConsoleLine::Error));
        CHECK(p.Run("document.getElementById('t').textContent = 'still works'"));   // the next entry has a fresh budget
        CHECK(p.El("t").TextContent() == "still works");
        // a hanging listener and a hanging timer are stopped too
        CHECK(p.Run("document.getElementById('t').addEventListener('click', () => { for (;;) {} });"
                    "setInterval(() => { for (;;) {} }, 10);"));
        p.js->ClearConsole();
        p.js->Dispatch(p.El("t"), "click");
        CHECK(ConsoleHas(p.js->Console(), "budget", ConsoleLine::Error));
        p.js->ClearConsole();
        p.js->Tick(20);
        CHECK(ConsoleHas(p.js->Console(), "budget", ConsoleLine::Error));
        // work the binding does for one instruction is charged too: an endless loop over a big list is stopped as well
        CHECK(p.Run("for (let i = 0; i < 2000; i++) document.body.appendChild(document.createElement('i'));"));
        p.js->ClearConsole();
        CHECK(!p.Run("for (;;) document.body.childNodes;"));
        CHECK(ConsoleHas(p.js->Console(), "budget", ConsoleLine::Error));
        // hostile trees: too deep, too many nodes, too much text
        p.js->SetBudget(PageScripts::kBudgetPerCall);
        CHECK(p.Eval("(() => { const big = 'x'.repeat(1 << 20); try { for (let i = 0; i < 100; i++) { const e = document.createElement('p');"
                     " e.textContent = big; document.body.appendChild(e); } return 'no'; } catch (x) { return x.name; } })()") == "RangeError");
        CHECK(p.Eval("(() => { let e = document.body; try { for (let i = 0; i < 1000; i++) { const c = document.createElement('div'); e.appendChild(c); e = c; } return 'no'; }"
                     " catch (x) { return x.name; } })()") == "RangeError");
        CHECK(p.Eval("(() => { try { for (let i = 0; i < 300000; i++) document.createElement('i'); return 'no'; } catch (x) { return x.name; } })()") == "RangeError");
        CHECK(p.Eval("(() => { try { for (let i = 0; i < 2000; i++) setTimeout(() => 0, 1e9); return 'no'; } catch (x) { return x.name; } })()") == "RangeError");
    }

    // ---- removed nodes are freed once no script holds them (and kept while one does)
    {
        Page p("<div id=d></div>");
        CHECK(p.Run("var keep = null; const d = document.getElementById('d');"
                    "for (let i = 0; i < 500; i++) { d.innerHTML = '<p>' + i + '</p><p>x</p>'; if (i == 7) keep = d.firstChild; }"));
        CHECK(p.js->RemovedCount() > 900);
        int forgot = 0;
        p.js->FreeRemoved([&](const Node&) { ++forgot; });
        CHECK(forgot > 900);
        CHECK(p.js->RemovedCount() == 2);   // the tree that holds `keep`: <p>7</p> and its text
        CHECK(p.Eval("keep.textContent") == "7");
        CHECK(p.Run("keep = null"));
        p.js->FreeRemoved(nullptr, true);
        CHECK(p.js->RemovedCount() == 0);
        CHECK(p.El("d").TextContent() == "499x");
    }

    // ---- through the tab: scripts in page order, <script src> through the loader, events, re-layout, submit, location
    {
        BrowserTab t;
        CHECK(t.Scripting());
        t.Open("https://site.test/app.html");
        t.TakeFetches();
        t.DeliverPage("https://site.test/app.html", true, 200,
                      "<title>App</title><noscript><p>Turn on JavaScript</p></noscript>"
                      "<p id=out>waiting</p><button id=btn>Count</button><a id=lnk href='/other.html'>other</a>"
                      "<form id=f action='/send'><input id=q name=q><input id=go type=submit></form>"
                      "<script>var order = ['inline1']; document.getElementById('out').textContent = 'ran';</script>"
                      "<script src='/lib.js'></script>"
                      "<script src='https://evil.test/x.js'></script>"
                      "<script type=module>order.push('module')</script>"
                      "<script>order.push('inline2');"
                      "document.addEventListener('DOMContentLoaded', () => order.push('dcl')); window.onload = () => order.push('load');"
                      "let clicks = 0; document.getElementById('btn').addEventListener('click', () => {"
                      "  clicks++; document.getElementById('out').textContent = 'clicked ' + clicks; });"
                      "document.getElementById('lnk').addEventListener('click', e => e.preventDefault());"
                      "document.getElementById('q').addEventListener('input', e => { document.title = 'typing ' + e.target.value; });"
                      "document.getElementById('f').addEventListener('submit', e => { if (document.getElementById('q').value == 'stop') e.preventDefault(); });"
                      "</script>");
        CHECK(t.GetState() == BrowserTab::State::Ready);
        const HtmlDocument* doc = t.Document();
        CHECK(doc && ById(*doc, "out")->TextContent() == "ran");
        CHECK(doc->root->TextContent().find("Turn on JavaScript") == std::string::npos);   // <noscript> is not shown
        // the src file is fetched like an image (same origin only); the scripts after it wait for it
        const std::vector<std::string> f = t.TakeFetches();
        CHECK(std::find(f.begin(), f.end(), "https://site.test/lib.js") != f.end());
        CHECK(std::find(f.begin(), f.end(), "https://evil.test/x.js") == f.end());
        CHECK(t.KindOfFetch("https://site.test/lib.js") == BrowserTab::FetchKind::Script);
        CHECK(t.Scripts() != nullptr);
        CHECK(ConsoleHas(t.ConsoleLines(), "") == false);
        t.DeliverScript("https://site.test/lib.js", true, "order.push('lib'); console.log('lib loaded');");
        CHECK(ConsoleHas(t.ConsoleLines(), "lib loaded"));
        CHECK(ConsoleHas(t.ConsoleLines(), "evil.test/x.js", ConsoleLine::Warn));
        t.Scripts()->ClearConsole();
        t.Scripts()->RunScript("console.log(order.join(','))", "check");
        CHECK(ConsoleHas(t.ConsoleLines(), "inline1,lib,inline2,dcl,load"));
        if (!ConsoleHas(t.ConsoleLines(), "inline1,lib,inline2,dcl,load")) std::cerr << Dump(t.ConsoleLines());
        // a click runs the listener and the page is laid out again with the new text
        const LayoutResult& before = t.Layout(500, kMeasure);
        CHECK(DumpLayout(before).find("'ran'") != std::string::npos);
        CHECK(!t.Click(*ById(*doc, "btn")));
        CHECK(!t.Click(*ById(*doc, "btn")));
        const LayoutResult& after = t.Layout(500, kMeasure);
        CHECK(DumpLayout(after).find("clicked 2") != std::string::npos);
        // the page cancels the link's click: the host does not follow it
        CHECK(t.Click(*ById(*doc, "lnk")));
        // typing fires input
        const Node& q = *ById(*doc, "q");
        t.Forms().SetValue(q, "abc");
        t.ControlInput(q);
        CHECK(t.Title() == "typing abc");
        // a cancelled submit sends nothing; an allowed one loads the answer
        std::string note;
        t.Forms().SetValue(q, "stop");
        CHECK(!t.SubmitForm(*ById(*doc, "f"), nullptr, note));
        CHECK(t.GetState() == BrowserTab::State::Ready);
        t.Forms().SetValue(q, "go");
        CHECK(t.SubmitForm(*ById(*doc, "f"), nullptr, note));
        CHECK(t.GetState() == BrowserTab::State::Loading);
        CHECK(t.Url() == "https://site.test/send?q=go");
    }

    // ---- location.href: followed at the next Tick by the link rules
    {
        BrowserTab t;
        t.Open("https://site.test/a.html");
        t.TakeFetches();
        t.DeliverPage("https://site.test/a.html", true, 200, "<script>setTimeout(() => { location.href = 'b.html'; }, 50);</script>");
        t.Tick(10);
        CHECK(t.GetState() == BrowserTab::State::Ready);
        t.Tick(50);
        CHECK(t.GetState() == BrowserTab::State::Loading);
        CHECK(t.Url() == "https://site.test/b.html");
        t.TakeFetches();
        // another site (launcher tab) and javascript: are not followed
        t.DeliverPage("https://site.test/b.html", true, 200, "<script>location.href = 'https://evil.test/'</script>");
        t.Tick(16);
        CHECK(t.GetState() == BrowserTab::State::Ready && t.Url() == "https://site.test/b.html");
        t.Navigate("https://site.test/c.html");
        t.TakeFetches();
        t.DeliverPage("https://site.test/c.html", true, 200, "<script>location.href = 'javascript:alert(1)'</script>");
        t.Tick(16);
        CHECK(t.GetState() == BrowserTab::State::Ready && t.Url() == "https://site.test/c.html");
        CHECK(ConsoleHas(t.ConsoleLines(), "not followed", ConsoleLine::Warn));
        // a #fragment scrolls the page instead of loading it
        t.Navigate("https://site.test/d.html");
        t.TakeFetches();
        t.DeliverPage("https://site.test/d.html", true, 200, "<p style='height: 900px'>top</p><h2 id=end>end</h2><script>location.hash = 'end'</script>");
        t.Layout(400, kMeasure);
        t.PendingScroll();
        t.Tick(16);
        CHECK(t.GetState() == BrowserTab::State::Ready && t.Url() == "https://site.test/d.html");
        CHECK(t.PendingScroll() > 800.0f);
    }

    // ---- scripting off: nothing runs and <noscript> is shown; a page without scripts gets no engine
    {
        BrowserTab t;
        t.SetScripting(false);
        t.Open("https://site.test/a.html");
        t.TakeFetches();
        t.DeliverPage("https://site.test/a.html", true, 200, "<p id=p>static</p><noscript>no js</noscript><script>document.getElementById('p').textContent='js'</script>");
        CHECK(t.Scripts() == nullptr);
        CHECK(ById(*t.Document(), "p")->TextContent() == "static");
        CHECK(t.Document()->root->TextContent().find("no js") != std::string::npos);
        BrowserTab plain;
        plain.Open("https://site.test/plain.html");
        plain.TakeFetches();
        plain.DeliverPage("https://site.test/plain.html", true, 200, "<p>no scripts here</p>");
        CHECK(plain.Scripts() == nullptr && plain.ConsoleLines().empty());
    }

    // ---- a page that keeps replacing its content: the tab frees what was removed after each layout
    {
        BrowserTab t;
        t.Open("https://site.test/clock.html");
        t.TakeFetches();
        t.DeliverPage("https://site.test/clock.html", true, 200,
                      "<p id=c>0</p><script>let n = 0; setInterval(() => { document.getElementById('c').innerHTML = '<b>' + (++n) + '</b>'; }, 10);</script>");
        for (int i = 0; i < 300; ++i) { t.Tick(10); t.Layout(400, kMeasure); }
        CHECK(ById(*t.Document(), "c")->TextContent() == "300");
        CHECK(t.Scripts()->RemovedCount() < 2 * PageScripts::kFreeAfter * 2);
    }

    TEST_MAIN_RESULT();
}
