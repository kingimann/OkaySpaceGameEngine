// Browser (page engine), round 2: JavaScript in web pages (okay/Browser/Script.hpp) on our own engine okay::js. The DOM
// binding (finding, reading and changing elements, innerHTML, classList, style, form values), events (click / input /
// submit through the tab, capture and bubble order, preventDefault, stopPropagation, once, on... attributes and properties),
// timers on the page's clock, the console, <script src> through the tab's loader in page order, location changes by the
// link rules, the budget that stops a hanging page, no file or network functions, and the removed-node bookkeeping.
#include "test_framework.hpp"
#include "okay/Browser/Browser.hpp"
#include "okay/Core/StringUtil.hpp"

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

    // ---- what a page can not do: no files, no network of its own (fetch / XMLHttpRequest only through the tab, round 3), no
    // endless loops
    {
        Page p("<p id=t>x</p>");
        CHECK(p.Eval("[typeof require, typeof process, typeof importScripts, typeof WebSocket, typeof __okay].join()") ==
              "undefined,undefined,undefined,undefined,undefined");
        CHECK(p.Eval("[typeof fetch, typeof XMLHttpRequest].join()") == "function,function");
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


    // ==================================================================== round 3
    // ---- cookies (RFC 6265): dates, domains, paths, Secure, HttpOnly, SameSite, expiry, limits, saving
    {
        std::int64_t d = 0;
        CHECK(ParseCookieDate("Wed, 21 Oct 2015 07:28:00 GMT", d) && d == 1445412480);
        CHECK(ParseCookieDate("Sunday, 06-Nov-94 08:49:37 GMT", d) && d == 784111777);   // RFC 850 form, two-digit year
        CHECK(ParseCookieDate("Sun Nov  6 08:49:37 1994", d) && d == 784111777);         // asctime form
        CHECK(!ParseCookieDate("yesterday", d) && !ParseCookieDate("Wed, 32 Oct 2015 07:28:00 GMT", d));
        CHECK(DomainMatches("a.site.test", "site.test") && DomainMatches("site.test", "site.test"));
        CHECK(!DomainMatches("evilsite.test", "site.test") && !DomainMatches("site.test", "a.site.test"));
        CHECK(SiteOf("a.b.site.test") == "site.test" && SiteOf("x.example.co.uk") == "example.co.uk" && SiteOf("10.0.0.1") == "10.0.0.1");

        const std::int64_t now = 1800000000;
        CookieStore c;
        CHECK(c.Set("https://a.site.test/dir/page", "id=1", now));
        CHECK(c.Header("https://a.site.test/dir/x", now) == "id=1");
        CHECK(c.Header("https://a.site.test/", now) == "");                       // default path: /dir
        CHECK(c.Header("https://b.site.test/dir/x", now) == "");                  // host-only
        CHECK(c.Set("https://a.site.test/", "wide=2; Domain=.site.test; Path=/", now));
        CHECK(c.Header("https://b.site.test/q", now) == "wide=2");
        CHECK(!c.Set("https://a.site.test/", "bad=1; Domain=test", now));        // one label
        CHECK(!c.Set("https://shop.example.co.uk/", "bad=1; Domain=co.uk", now)); // a shared suffix
        CHECK(!c.Set("https://a.site.test/", "bad=1; Domain=other.test", now));  // not this host's domain
        CHECK(c.Header("https://a.site.test/dir/page", now) == "id=1; wide=2");    // longer path first
        // expiry: Max-Age wins over Expires; a past date or Max-Age=0 removes
        CHECK(c.Set("https://a.site.test/", "p=1; Expires=Wed, 21 Oct 2015 07:28:00 GMT; Max-Age=100", now));
        CHECK(c.Header("https://a.site.test/", now).find("p=1") != std::string::npos);
        CHECK(c.Header("https://a.site.test/", now + 101).find("p=1") == std::string::npos);
        CHECK(c.Set("https://a.site.test/", "wide=; Domain=site.test; Path=/; Expires=Wed, 21 Oct 2015 07:28:00 GMT", now));
        CHECK(c.Header("https://b.site.test/", now) == "");
        // Secure: not from http, not sent over http, and http may not overwrite it
        CHECK(!c.Set("http://a.site.test/", "s=1; Secure", now));
        CHECK(c.Set("https://a.site.test/", "s=1; Secure; Path=/", now));
        CHECK(c.Header("http://a.site.test/", now).find("s=1") == std::string::npos);
        CHECK(!c.Set("http://a.site.test/", "s=2; Path=/", now));
        CHECK(c.Header("https://a.site.test/", now).find("s=1") != std::string::npos);
        // HttpOnly: hidden from scripts, and a script may not set or overwrite one
        CHECK(c.Set("https://a.site.test/", "h=1; HttpOnly; Path=/", now));
        CookieStore::Context script;
        script.fromScript = true;
        CHECK(c.Header("https://a.site.test/", now).find("h=1") != std::string::npos);
        CHECK(c.Header("https://a.site.test/", now, script).find("h=1") == std::string::npos);
        CHECK(!c.Set("https://a.site.test/", "h=2; Path=/", now, script));
        CHECK(!c.Set("https://a.site.test/", "j=2; HttpOnly", now, script));
        // SameSite: Strict only same-site; Lax (and no attribute) also on a cross-site top-level GET; None must be Secure
        CookieStore ss;
        CHECK(ss.Set("https://site.test/", "st=1; SameSite=Strict", now));
        CHECK(ss.Set("https://site.test/", "lx=1; SameSite=Lax", now));
        CHECK(ss.Set("https://site.test/", "df=1", now));
        CHECK(!ss.Set("https://site.test/", "nn=1; SameSite=None", now));
        CHECK(ss.Set("https://site.test/", "nn=1; SameSite=None; Secure", now));
        CookieStore::Context cross;
        cross.sameSite = false;
        CHECK(ss.Header("https://site.test/", now, cross) == "nn=1");
        cross.topLevel = true;
        CHECK(ss.Header("https://site.test/", now, cross) == "lx=1; df=1; nn=1");
        cross.safeMethod = false;
        CHECK(ss.Header("https://site.test/", now, cross) == "nn=1");
        CHECK(ss.Header("https://site.test/", now) == "st=1; lx=1; df=1; nn=1");
        // limits: a too-big cookie is refused; one host keeps at most kMaxPerDomain (the oldest goes)
        CHECK(!ss.Set("https://site.test/", "big=" + std::string(CookieStore::kMaxCookieBytes, 'x'), now));
        CookieStore many;
        for (int i = 0; i < 60; ++i) many.Set("https://m.test/", "c" + std::to_string(i) + "=v", now + i);
        CHECK(many.Count() == CookieStore::kMaxPerDomain);
        CHECK(many.Header("https://m.test/", now + 100).find("c0=") == std::string::npos);
        CHECK(many.Header("https://m.test/", now + 100).find("c59=v") != std::string::npos);
        // saving keeps the persistent cookies that are still valid
        CookieStore keep;
        keep.Set("https://k.test/", "session=1", now);
        keep.Set("https://k.test/", "long=2; Max-Age=1000; HttpOnly; SameSite=Strict", now);
        keep.Set("https://k.test/", "short=3; Max-Age=10", now);
        CHECK(keep.TakeChanged() && !keep.TakeChanged());
        const std::string saved = keep.ToJson(now + 20);
        CookieStore back;
        CHECK(back.FromJson(saved, now + 30));
        CHECK(back.Count() == 1 && back.All()[0].name == "long" && back.All()[0].httpOnly && back.All()[0].sameSite == Cookie::Strict);
        CHECK(!back.FromJson("{not json", now) && back.Count() == 0);
    }

    // ---- web storage areas: order, quota, saving
    {
        WebStorage ws;
        StorageArea& a = ws.Area("https://site.test");
        CHECK(a.Set("b", "2", ws.quota) && a.Set("a", "1", ws.quota) && a.Set("b", "3", ws.quota));
        std::string v;
        CHECK(a.Length() == 2 && a.Key(0, v) && v == "b" && a.Get("b", v) && v == "3");
        CHECK(!a.Set("big", std::string(WebStorage::kMaxBytesPerOrigin, 'x'), ws.quota) && a.Length() == 2);
        CHECK(a.Remove("b") && !a.Get("b", v) && a.Length() == 1);
        ws.Area("https://other.test").Set("o", "x", ws.quota);
        WebStorage back;
        CHECK(back.FromJson(ws.ToJson()));
        CHECK(back.Origins() == 2 && back.Find("https://site.test") && back.Find("https://site.test")->Get("a", v) && v == "1");
        CHECK(!back.FromJson("[1,2]") && back.Origins() == 2);
    }

    // ---- through the tab: fetch() and XMLHttpRequest go through the tab's loader, with cookies; document.cookie
    {
        CookieStore jar;
        BrowserTab t;
        t.SetCookieStore(&jar);
        t.Open("https://site.test/app.html");
        t.TakeFetches();
        jar.Set("https://site.test/", "sid=abc; HttpOnly; Path=/", 0x7fffffff);
        CHECK(t.CookieHeader("https://site.test/app.html") == "sid=abc");
        t.ReceiveCookies("https://site.test/app.html", {"theme=dark; Path=/"});
        t.DeliverPage("https://site.test/app.html", true, 200,
                      "<p id=out>-</p><script>"
                      "var log = [];"
                      "log.push('cookie ' + document.cookie);"
                      "document.cookie = 'lang=en; path=/';"
                      "fetch('/api/data.json', {headers: {'X-Test': '1'}}).then(r => { log.push('status ' + r.status + ' ' + r.ok + ' ' + r.headers.get('content-type') + ' ' + r.headers.get('set-cookie')); return r.json(); })"
                      "  .then(j => { log.push('json ' + j.n); document.getElementById('out').textContent = 'n=' + j.n; });"
                      "fetch('https://evil.test/steal').then(() => log.push('evil ok'), e => log.push('evil ' + e.name));"
                      "fetch('/missing').then(r => log.push('missing ' + r.status + ' ' + r.ok));"
                      "fetch('/down').catch(e => log.push('down ' + e.name));"
                      "fetch('/post', {method: 'POST', body: 'a=1'}).then(r => r.text()).then(t => log.push('post ' + t));"
                      "var x = new XMLHttpRequest(); var states = [];"
                      "x.onreadystatechange = () => states.push(x.readyState);"
                      "x.onload = () => log.push('xhr ' + x.status + ' ' + x.responseText + ' ' + x.getResponseHeader('X-Kind') + ' ' + states.join(''));"
                      "x.open('GET', 'xhr.txt'); x.setRequestHeader('X-A', 'b'); x.send();"
                      "var y = new XMLHttpRequest(); y.responseType = 'json';"
                      "y.addEventListener('load', () => log.push('xhrjson ' + y.response.k));"
                      "y.open('GET', '/j'); y.send();"
                      "var z = new XMLHttpRequest(); z.onerror = () => log.push('xhrerr ' + z.status); z.open('GET', 'https://evil.test/'); z.send();"
                      "</script>");
        CHECK(jar.Header("https://site.test/", 0x7fffffff).find("lang=en") != std::string::npos);
        std::vector<ScriptRequest> reqs = t.TakeScriptRequests();
        CHECK(reqs.size() == 6);
        CHECK(t.TakeScriptRequests().empty());
        auto find = [&](const std::string& url) -> const ScriptRequest* {
            for (const ScriptRequest& r : reqs) if (r.url == url) return &r;
            return nullptr;
        };
        auto header = [](const ScriptRequest& r, const std::string& name) {
            for (const auto& h : r.headers) if (okay::str::EqualsNoCaseAscii(h.first, name)) return h.second;
            return std::string("<none>");
        };
        const ScriptRequest* api = find("https://site.test/api/data.json");
        CHECK(api && api->method == "GET" && header(*api, "X-Test") == "1");
        CHECK(api && header(*api, "Cookie").find("sid=abc") != std::string::npos && header(*api, "Cookie").find("lang=en") != std::string::npos);
        const ScriptRequest* post = find("https://site.test/post");
        CHECK(post && post->method == "POST" && post->body == "a=1");
        const ScriptRequest* xr = find("https://site.test/xhr.txt");
        CHECK(xr && header(*xr, "X-A") == "b");
        CHECK(!find("https://evil.test/steal") && !find("https://evil.test/"));
        auto answer = [&](const ScriptRequest* r, int status, const std::string& body, std::vector<std::pair<std::string, std::string>> hs = {}) {
            if (!r) return;
            ScriptResponse a;
            a.ok = true;
            a.status = status;
            a.body = body;
            a.headers = std::move(hs);
            t.DeliverScriptResponse(r->id, a);
        };
        answer(api, 200, "{\"n\": 5}", {{"Content-Type", "application/json"}, {"Set-Cookie", "fromapi=1; Path=/"}});
        answer(find("https://site.test/missing"), 404, "no");
        if (const ScriptRequest* down = find("https://site.test/down")) { ScriptResponse e; e.ok = false; e.error = "no route"; t.DeliverScriptResponse(down->id, e); }
        answer(post, 201, "made");
        answer(xr, 200, "hello", {{"X-Kind", "text"}});
        answer(find("https://site.test/j"), 200, "{\"k\": \"v\"}");
        t.DeliverScriptResponse(999999, ScriptResponse());   // no such request: ignored
        CHECK(jar.Header("https://site.test/", 0x7fffffff).find("fromapi=1") != std::string::npos);
        const std::string logText = (t.Scripts()->ClearConsole(), t.Scripts()->RunScript("console.log(log.join('|'))", "c"), t.ConsoleLines().back().text);
        CHECK(logText.find("cookie theme=dark") != std::string::npos && logText.find("sid=") == std::string::npos);
        CHECK(logText.find("status 200 true application/json null") != std::string::npos);
        CHECK(logText.find("json 5") != std::string::npos);
        CHECK(logText.find("evil TypeError") != std::string::npos && logText.find("evil ok") == std::string::npos);
        CHECK(logText.find("missing 404 false") != std::string::npos);
        CHECK(logText.find("down TypeError") != std::string::npos);
        CHECK(logText.find("post made") != std::string::npos);
        CHECK(logText.find("xhr 200 hello text 1234") != std::string::npos);
        CHECK(logText.find("xhrjson v") != std::string::npos);
        CHECK(logText.find("xhrerr 0") != std::string::npos);
        if (logText.find("xhrjson v") == std::string::npos) std::cerr << "  log: " << logText << "\n" << Dump(t.ConsoleLines());
        CHECK(ById(*t.Document(), "out")->TextContent() == "n=5");
        // too many requests at once are refused; an answer for a page that is gone is ignored
        t.Scripts()->RunScript("var refused = 0; for (let i = 0; i < 100; i++) fetch('/n' + i).catch(() => refused++);", "many");
        CHECK(t.TakeScriptRequests().size() == PageScripts::kMaxRequests);
        t.Navigate("https://site.test/next.html");
        t.DeliverScriptResponse(reqs[0].id, ScriptResponse());
        CHECK(t.TakeScriptRequests().empty());
    }

    // ---- the browser program (any site): another site's answer is read only when it allows it (CORS), with no cookies;
    // a redirect to another site is checked the same way; SameSite cookies on page loads follow who started the load
    {
        CookieStore jar;
        BrowserTab t;
        t.SetAnyOrigin(true);
        t.SetCookieStore(&jar);
        jar.Set("https://api.other.test/", "secret=1; SameSite=None; Secure", 0x7fffffff);
        jar.Set("https://site.test/", "strict=1; SameSite=Strict", 0x7fffffff);
        jar.Set("https://site.test/", "lax=1", 0x7fffffff);
        t.Open("https://site.test/a.html");
        CHECK(t.CookieHeader("https://site.test/a.html") == "strict=1; lax=1");   // typed: the person started it
        t.TakeFetches();
        t.DeliverPage("https://site.test/a.html", true, 200,
                      "<a id=out href='https://evil.test/x'>x</a><script>var log = [];"
                      "fetch('https://api.other.test/open').then(r => r.text()).then(t => log.push('open ' + t), e => log.push('open ' + e.name));"
                      "fetch('https://api.other.test/closed').then(r => r.text()).then(t => log.push('closed ' + t), e => log.push('closed ' + e.name));"
                      "fetch('https://api.other.test/put', {method: 'PUT'}).catch(e => log.push('put ' + e.name));"
                      "fetch('/moved').then(r => r.text()).then(t => log.push('moved ' + t), e => log.push('moved ' + e.name));</script>");
        std::vector<ScriptRequest> reqs = t.TakeScriptRequests();
        CHECK(reqs.size() == 3);
        for (const ScriptRequest& r : reqs) {
            bool cookie = false, origin = false;
            for (const auto& h : r.headers) { if (h.first == "Cookie") cookie = true; if (h.first == "Origin" && h.second == "https://site.test") origin = true; }
            if (r.url.find("other.test") != std::string::npos) CHECK(!cookie && origin);
            ScriptResponse a;
            a.ok = true;
            a.status = 200;
            a.body = "data";
            if (r.url == "https://api.other.test/open") a.headers = {{"Access-Control-Allow-Origin", "https://site.test"}};
            if (r.url == "https://site.test/moved") a.url = "https://api.other.test/landing";
            t.DeliverScriptResponse(r.id, a);
        }
        t.Scripts()->RunScript("console.log(log.sort().join('|'))", "c");
        CHECK(ConsoleHas(t.ConsoleLines(), "closed TypeError|moved TypeError|open data|put TypeError"));
        if (!ConsoleHas(t.ConsoleLines(), "closed TypeError|moved TypeError|open data|put TypeError")) std::cerr << Dump(t.ConsoleLines());
        // a link from another site's page: Strict cookies stay home, Lax ones go on a GET
        t.Navigate("https://evil.test/x");
        t.TakeFetches();
        t.DeliverPage("https://evil.test/x", true, 200, "<a href='https://site.test/b.html'>b</a>");
        CHECK(t.Activate("https://site.test/b.html").kind == BrowserTab::Action::Navigated);
        CHECK(t.CookieHeader("https://site.test/b.html") == "lax=1");
    }

    // ---- localStorage (per profile, saved) and sessionStorage (per tab)
    {
        WebStorage local;
        BrowserTab t;
        t.SetLocalStorage(&local);
        t.Open("https://site.test/a.html");
        t.TakeFetches();
        t.DeliverPage("https://site.test/a.html", true, 200,
                      "<script>localStorage.setItem('k', 'v'); localStorage.n = 5; sessionStorage.setItem('s', '1');"
                      "var r = [localStorage.getItem('k'), localStorage.n, localStorage.length, localStorage.key(0), String(localStorage.getItem('none')),"
                      "         sessionStorage.s, 'getItem' in localStorage];"
                      "try { localStorage.setItem('huge', 'x'.repeat(6 * 1024 * 1024)); r.push('stored'); } catch (e) { r.push(e.name); }"
                      "console.log(r.join(','));</script>");
        CHECK(ConsoleHas(t.ConsoleLines(), "v,5,2,k,null,1,true,QuotaExceededError"));
        if (!ConsoleHas(t.ConsoleLines(), "v,5,2,k,null,1,true,QuotaExceededError")) std::cerr << Dump(t.ConsoleLines());
        std::string v;
        CHECK(local.Find("https://site.test") && local.Find("https://site.test")->Get("n", v) && v == "5");
        CHECK(local.TakeChanged());
        CHECK(!local.Find("https://site.test")->Get("s", v));   // sessionStorage is not in the profile
        // another tab of the profile sees localStorage, not the first tab's sessionStorage; another site sees neither
        BrowserTab t2;
        t2.SetLocalStorage(&local);
        t2.Open("https://site.test/b.html");
        t2.TakeFetches();
        t2.DeliverPage("https://site.test/b.html", true, 200,
                       "<script>localStorage.removeItem('n'); console.log(localStorage.k + ' ' + sessionStorage.getItem('s') + ' ' + localStorage.length);</script>");
        CHECK(ConsoleHas(t2.ConsoleLines(), "v null 1"));
        t2.Navigate("https://other.test/c.html");
        BrowserTab t3;
        t3.SetLocalStorage(&local);
        t3.Open("https://other.test/c.html");
        t3.TakeFetches();
        t3.DeliverPage("https://other.test/c.html", true, 200, "<script>console.log('other ' + localStorage.getItem('k') + ' ' + localStorage.length)</script>");
        CHECK(ConsoleHas(t3.ConsoleLines(), "other null 0"));
        // no store: localStorage is null (the page's code that checks for it goes on)
        BrowserTab none;
        none.Open("https://site.test/a.html");
        none.TakeFetches();
        none.DeliverPage("https://site.test/a.html", true, 200, "<script>console.log('ls ' + window.localStorage)</script>");
        CHECK(ConsoleHas(none.ConsoleLines(), "ls null"));
    }

    // ---- history: pushState / replaceState / back / forward within the page, popstate, no load
    {
        BrowserTab t;
        t.Open("https://site.test/app/");
        t.TakeFetches();
        t.DeliverPage("https://site.test/app/", true, 200,
                      "<script>var pops = []; addEventListener('popstate', e => pops.push(JSON.stringify(e.state) + '@' + location.pathname));"
                      "history.pushState({n: 1}, '', 'one'); history.pushState({n: 2}, '', '/app/two?x=1');"
                      "history.replaceState({n: 22}, '');"
                      "var bad; try { history.pushState(null, '', 'https://evil.test/'); } catch (e) { bad = e.name; }"
                      "console.log(history.length + ' ' + location.href + ' ' + history.state.n + ' ' + bad);</script>");
        CHECK(ConsoleHas(t.ConsoleLines(), "3 https://site.test/app/two?x=1 22 SecurityError"));
        CHECK(t.Url() == "https://site.test/app/two?x=1" && t.CanGoBack());
        CHECK(t.GoBack());
        CHECK(t.GetState() == BrowserTab::State::Ready && t.Url() == "https://site.test/app/one");
        CHECK(t.TakeFetches().empty());
        CHECK(t.GoBack());
        CHECK(t.GoForward() && t.GoForward());
        t.Scripts()->RunScript("console.log('pops ' + pops.join(' '))", "c");
        CHECK(ConsoleHas(t.ConsoleLines(), "pops {\"n\":1}@/app/one null@/app/ {\"n\":1}@/app/one {\"n\":22}@/app/two"));
        if (!ConsoleHas(t.ConsoleLines(), "pops {\"n\":1}@/app/one null@/app/")) std::cerr << Dump(t.ConsoleLines());
        // history.back() from a script: done at the next Tick
        t.Scripts()->RunScript("history.back()", "c");
        CHECK(t.Url() == "https://site.test/app/two?x=1");
        t.Tick(16);
        CHECK(t.Url() == "https://site.test/app/one");
        // a real navigation away and back loads the page again (its own entries go with it)
        t.Navigate("https://site.test/other.html");
        CHECK(t.GetState() == BrowserTab::State::Loading);
    }

    // ---- the person's keys, mouse and focus reach the page
    {
        BrowserTab t;
        t.Open("https://site.test/k.html");
        t.TakeFetches();
        t.DeliverPage("https://site.test/k.html", true, 200,
                      "<input id=a><input id=b><div id=box>box</div><script>var ev = [];"
                      "const a = document.getElementById('a'), b = document.getElementById('b'), box = document.getElementById('box');"
                      "a.addEventListener('focus', () => ev.push('focus a')); a.addEventListener('blur', () => ev.push('blur a'));"
                      "document.addEventListener('focusin', e => ev.push('in ' + e.target.id));"
                      "document.addEventListener('keydown', e => { ev.push('key ' + e.key + ' ' + e.code + ' ' + e.shiftKey + ' ' + e.target.id); if (e.key == 'x') e.preventDefault(); });"
                      "box.addEventListener('mousedown', e => ev.push('down ' + e.clientX + ',' + e.clientY + ' ' + e.button));"
                      "box.addEventListener('mouseover', e => ev.push('over ' + (e.relatedTarget ? e.relatedTarget.id : 'none')));"
                      "box.addEventListener('mouseout', e => ev.push('out ' + (e.relatedTarget ? e.relatedTarget.id : 'none')));"
                      "</script>");
        const HtmlDocument* doc = t.Document();
        t.SetFocus(ById(*doc, "a"));
        KeyInfo k;
        k.key = "A";
        k.code = "KeyA";
        k.shift = true;
        CHECK(!t.KeyEvent("keydown", k));
        k.key = "x";
        k.shift = false;
        CHECK(t.KeyEvent("keydown", k));
        t.SetFocus(ById(*doc, "b"));
        MouseInfo m;
        m.x = 12;
        m.y = 34;
        CHECK(!t.PointerEvent(*ById(*doc, "box"), "mousedown", m));
        t.SetHover(ById(*doc, "box"));
        t.SetHover(ById(*doc, "a"));
        t.Scripts()->RunScript("console.log(ev.join('|'))", "c");
        const std::string want = "focus a|in a|key A KeyA true a|key x KeyX false a|blur a|in b|down 12,34 0|over none|out a";
        CHECK(ConsoleHas(t.ConsoleLines(), "focus a|in a|key A KeyA true a|key x"));
        CHECK(ConsoleHas(t.ConsoleLines(), "blur a|in b|down 12,34 0|over none|out a"));
        if (!ConsoleHas(t.ConsoleLines(), "blur a|in b|down 12,34 0|over none|out a")) std::cerr << "  want " << want << "\n" << Dump(t.ConsoleLines());
        // el.focus() from a script moves the tab's focus; document.activeElement follows
        t.Scripts()->RunScript("a.focus(); console.log('active ' + document.activeElement.id)", "c");
        CHECK(t.Focus() == ById(*doc, "a"));
        CHECK(ConsoleHas(t.ConsoleLines(), "active a"));
        t.Scripts()->RunScript("a.blur(); console.log('active ' + document.activeElement.tagName)", "c");
        CHECK(t.Focus() == nullptr && ConsoleHas(t.ConsoleLines(), "active BODY"));
    }

    // ---- scripts while the page is read: document.write, currentScript, defer and async, scripts and styles a script adds
    {
        BrowserTab t;
        t.Open("https://site.test/w.html");
        t.TakeFetches();
        t.DeliverPage("https://site.test/w.html", true, 200,
                      "<body><p id=a>1</p><script id=s1>var order = ['inline:' + document.currentScript.id];"
                      "document.write('<p id=w>written</p><script>order.push(\"written\")<\\/script>');</script><p id=b>2</p>"
                      "<script defer src='d.js'></script><script async src='as.js'></script><script>order.push('inline2')</script>"
                      "<script>document.addEventListener('DOMContentLoaded', () => order.push('dcl')); addEventListener('load', () => order.push('load'));</script></body>");
        const HtmlDocument* doc = t.Document();
        std::string ids;
        for (const auto& c : doc->Body()->children) if (const std::string* id = c->Attr("id")) ids += *id + " ";
        CHECK(ids.find("a s1 w") == 0 && ids.find("w b") != std::string::npos);
        t.DeliverScript("https://site.test/d.js", true, "order.push('defer')");
        t.DeliverScript("https://site.test/as.js", true, "order.push('async')");
        t.Scripts()->RunScript("console.log(order.join(','))", "c");
        CHECK(ConsoleHas(t.ConsoleLines(), "inline:s1,written,inline2,defer,dcl,async,load"));
        if (!ConsoleHas(t.ConsoleLines(), "inline:s1,written,inline2,defer,dcl,async,load")) std::cerr << Dump(t.ConsoleLines());
        // after the page is read, document.write does not wipe it (a line in the console instead)
        t.Scripts()->RunScript("document.write('<p>gone?</p>')", "late");
        CHECK(ById(*doc, "a") != nullptr && ConsoleHas(t.ConsoleLines(), "document.write", ConsoleLine::Warn));
        // a script element a script adds: inline runs at once, src is fetched and runs when it arrives
        t.TakeFetches();
        t.Scripts()->RunScript("var s = document.createElement('script'); s.textContent = 'var added = 7;'; document.body.appendChild(s);"
                               "console.log('added ' + added);"
                               "var s2 = document.createElement('script'); s2.src = '/late.js'; s2.onload = () => console.log('late onload'); document.head.appendChild(s2);"
                               "var st = document.createElement('style'); st.textContent = '#a { color: rgb(255, 0, 0); display: none }'; document.head.appendChild(st);", "dyn");
        CHECK(ConsoleHas(t.ConsoleLines(), "added 7"));
        const std::vector<std::string> f = t.TakeFetches();
        CHECK(std::find(f.begin(), f.end(), "https://site.test/late.js") != f.end());
        t.DeliverScript("https://site.test/late.js", true, "console.log('late ran')");
        CHECK(ConsoleHas(t.ConsoleLines(), "late ran") && ConsoleHas(t.ConsoleLines(), "late onload"));
        // the added <style> counts for getComputedStyle and the layout
        t.Scripts()->RunScript("var cs = getComputedStyle(document.getElementById('a')); console.log('cs ' + cs.color + ' ' + cs.display + ' ' + cs.getPropertyValue('display'))", "cs");
        CHECK(ConsoleHas(t.ConsoleLines(), "cs rgb(255, 0, 0) none none"));
        if (!ConsoleHas(t.ConsoleLines(), "cs rgb(255, 0, 0) none none")) std::cerr << Dump(t.ConsoleLines());
        const LayoutResult& lay = t.Layout(400, kMeasure);
        CHECK(DumpLayout(lay).find("'1'") == std::string::npos && DumpLayout(lay).find("'2'") != std::string::npos);
    }

    // ---- live collections, dataset writes, dialogs
    {
        BrowserTab t;
        t.Open("https://site.test/l.html");
        t.TakeFetches();
        t.DeliverPage("https://site.test/l.html", true, 200,
                      "<ul id=u><li>a</li><li class=k>b</li></ul><div id=d data-x='1'></div><script>"
                      "const lis = document.getElementsByTagName('li'), ks = document.getElementsByClassName('k'), kids = document.getElementById('u').children;"
                      "const nodes = document.getElementById('u').childNodes; const qs = document.querySelectorAll('li');"
                      "const n0 = lis.length + ',' + ks.length + ',' + kids.length;"
                      "const li = document.createElement('li'); li.className = 'k'; document.getElementById('u').appendChild(li);"
                      "lis[0].className = 'k';"
                      "let seen = 0; for (const e of lis) seen++;"
                      "console.log('live ' + n0 + ' ' + lis.length + ',' + ks.length + ',' + kids.length + ',' + nodes.length + ',' + qs.length + ' ' + seen + ' ' + lis.item(1).textContent + ' ' + Array.from(ks).length + ' ' + (lis[9] === undefined));"
                      "const d = document.getElementById('d'); d.dataset.fooBar = 'yes'; d.dataset.x = '2';"
                      "console.log('data ' + d.getAttribute('data-foo-bar') + ' ' + d.getAttribute('data-x') + ' ' + d.dataset.fooBar + ' ' + Object.keys(d.dataset).join('+'));"
                      "alert('hello'); var c = confirm('sure?'); var p = prompt('name?', 'bob');"
                      "for (let i = 0; i < 10; i++) alert('spam ' + i);"
                      "console.log('dialogs ' + c + ' ' + p);</script>");
        CHECK(ConsoleHas(t.ConsoleLines(), "live 2,1,2 3,3,3,3,2 3 b 3 true"));
        CHECK(ConsoleHas(t.ConsoleLines(), "data yes 2 yes x+fooBar"));
        CHECK(ConsoleHas(t.ConsoleLines(), "dialogs false null"));
        if (!ConsoleHas(t.ConsoleLines(), "data yes 2 yes x+fooBar")) std::cerr << Dump(t.ConsoleLines());
        PageDialog dlg;
        CHECK(t.TakeDialog(dlg) && dlg.kind == PageDialog::Alert && dlg.message == "hello");
        CHECK(t.TakeDialog(dlg) && dlg.kind == PageDialog::Confirm && dlg.message == "sure?");
        CHECK(t.TakeDialog(dlg) && dlg.kind == PageDialog::Prompt && dlg.defaultText == "bob");
        CHECK(!t.TakeDialog(dlg));   // the rest of that run went to the console
        CHECK(ConsoleHas(t.ConsoleLines(), "spam 0", ConsoleLine::Warn));
    }

    // ---- hostile pages: the new parts stay inside the budget and limits
    {
        Page p("<p id=x>x</p>");
        p.js->SetBudget(2'000'000);
        CHECK(!p.Run("const c = document.getElementsByTagName('p'); for (;;) c.length;"));
        CHECK(ConsoleHas(p.js->Console(), "budget", ConsoleLine::Error) || ConsoleHas(p.js->Console(), "instructions", ConsoleLine::Error));
        // no hooks: fetch is refused, storage is null, dialogs go to the console, pushState throws
        CHECK(p.Eval("typeof fetch") == "function");
        p.Run("fetch('/x').catch(e => console.log('refused ' + e.name))");
        CHECK(ConsoleHas(p.js->Console(), "refused TypeError"));
        CHECK(p.Eval("localStorage") == "null");
        CHECK(p.Eval("(() => { try { history.pushState(null, '', '/y'); return 'pushed'; } catch (e) { return e.name; } })()") == "SecurityError");
        // a document.write that writes itself again and again stops at the page's limits
        Page w("<script id=s></script>");
        CHECK(!w.js->RunScript("for (;;) document.write('<b>x</b>'.repeat(1000));", "w", w.doc.FindAnchor("s")));
        CHECK(w.js->Console().size() == 1 && w.js->Console()[0].level == ConsoleLine::Error);
    }

    TEST_MAIN_RESULT();
}
