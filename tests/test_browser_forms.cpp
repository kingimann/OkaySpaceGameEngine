// Browser (page engine): forms (okay/Browser/Forms.hpp) and what the tab does with them, from the WHATWG HTML standard 4.10:
// values and checkedness, radio groups, labels, disabled controls, the entry list, the urlencoded and text/plain bodies, GET
// and POST submission (also through BrowserTab, with the body the host sends), implicit submission and reset; and the tab's
// linked style sheets (<link rel=stylesheet>, @import, url() resolved against the sheet) and web-font requests.
#include "test_framework.hpp"
#include "okay/Browser/Browser.hpp"

#include <string>
#include <vector>

using namespace okay::browser;

static const Node* ById(const Node& n, const std::string& id) {
    if (const std::string* v = n.Attr("id")) if (*v == id) return &n;
    for (const auto& c : n.children) if (const Node* f = ById(*c, id)) return f;
    return nullptr;
}

static const char* kForm =
    "<form id=f action='/send?old=1#top' method=get>"
    "<input id=name name=user value='Ann'>"
    "<input id=pw type=password name=pw>"
    "<input id=keep type=checkbox name=keep checked>"
    "<input id=news type=checkbox name=news value=yes>"
    "<input id=r1 type=radio name=size value=s checked><input id=r2 type=radio name=size value=m>"
    "<select id=sel name=color><option>red</option><option value=g selected>Green</option><option>blue</option></select>"
    "<textarea id=ta name=note>\nline1\nline2</textarea>"
    "<input type=hidden name=token value='a&b=c'>"
    "<input id=off name=off value=x disabled>"
    "<fieldset disabled><input name=infs value=1><legend><input id=leg name=leg value=2></legend></fieldset>"
    "<input name='' value=noname>"
    "<label id=lab for=news>News</label><label id=lab2>Size M <input id=inl type=checkbox name=inl></label>"
    "<input id=go type=submit name=act value=Go><button id=b2 name=act value=other>Other</button><button id=rs type=reset>R</button>"
    "</form>"
    "<input id=outside form=f name=outside value=o>"
    "<form id=g><input id=only name=q></form>";

int main() {
    RUN_SUITE("browser forms");
    const HtmlDocument doc = ParseHtml(kForm);
    const Node& root = *doc.root;
    auto el = [&](const char* id) -> const Node& { const Node* n = ById(root, id); if (!n) { std::cerr << "no #" << id << "\n"; std::abort(); } return *n; };
    FormState fs;
    fs.Reset(doc);

    // kinds, owners, labels, disabled
    CHECK(KindOf(el("name")) == ControlKind::Text && KindOf(el("pw")) == ControlKind::Password && KindOf(el("sel")) == ControlKind::Select);
    CHECK(KindOf(el("b2")) == ControlKind::Submit && KindOf(el("rs")) == ControlKind::Reset && KindOf(el("ta")) == ControlKind::TextArea);
    CHECK(FormState::FormOwner(el("name"), doc) == &el("f"));
    CHECK(FormState::FormOwner(el("outside"), doc) == &el("f"));   // form="f" from outside the form
    CHECK(FormState::LabelTarget(el("lab"), doc) == &el("news"));
    CHECK(FormState::LabelTarget(el("lab2"), doc) == &el("inl"));
    CHECK(FormState::IsDisabled(el("off")) && !FormState::IsDisabled(el("leg")));   // a legend's controls stay enabled

    // the document's values
    CHECK(fs.Value(el("name")) == "Ann" && fs.Checked(el("keep")) && !fs.Checked(el("news")) && fs.Checked(el("r1")));
    CHECK(fs.SelectedIndex(el("sel")) == 1 && fs.Display(el("sel")) == "Green");
    CHECK(fs.Value(el("ta")) == "line1\nline2");   // the first newline after <textarea> is not part of it

    // typing and clicking
    fs.SetValue(el("name"), "Bo b\n");        // a one-line field drops newlines
    fs.SetValue(el("pw"), "s\xC3\xA9" "cret");
    CHECK(fs.Value(el("name")) == "Bo b" && fs.Display(el("pw")) == "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2");   // 6 characters
    fs.Toggle(el("news"));
    fs.Toggle(el("keep"));
    fs.Toggle(el("r2"));                       // the radio group: r1 goes off
    CHECK(fs.Checked(el("news")) && !fs.Checked(el("keep")) && fs.Checked(el("r2")) && !fs.Checked(el("r1")));
    fs.Toggle(el("r2"));                       // a ticked radio stays ticked
    CHECK(fs.Checked(el("r2")));
    fs.Select(el("sel"), 2);
    fs.SetValue(el("ta"), "a\nb");
    // maxlength counts characters
    {
        const HtmlDocument d2 = ParseHtml("<input id=m maxlength=3>");
        FormState f2;
        f2.Reset(d2);
        f2.SetValue(*ById(*d2.root, "m"), "\xC3\xA9" "abcd");
        CHECK(f2.Value(*ById(*d2.root, "m")) == "\xC3\xA9" "ab");
    }

    // the entry list: in tree order, only the pressed button, no disabled or nameless controls, CRLF in a textarea
    const auto entries = fs.Entries(el("f"), &el("go"), doc);
    const std::vector<std::pair<std::string, std::string>> want = {
        {"user", "Bo b"}, {"pw", "s\xC3\xA9" "cret"}, {"news", "yes"}, {"size", "m"}, {"color", "blue"}, {"note", "a\r\nb"},
        {"token", "a&b=c"}, {"leg", "2"}, {"act", "Go"}, {"outside", "o"}};
    CHECK(entries == want);
    if (entries != want) for (const auto& e : entries) std::cerr << "  entry " << e.first << "=" << e.second << "\n";

    // GET: the entries replace the query; the fragment stays
    const Submission g = fs.Submit(el("f"), &el("b2"), "https://site.test/dir/page.html", doc);
    CHECK(g.ok && g.method == "GET");
    CHECK(g.url == "https://site.test/send?user=Bo+b&pw=s%C3%A9cret&news=yes&size=m&color=blue&note=a%0D%0Ab&token=a%26b%3Dc&leg=2&act=other&outside=o#top");
    // POST: the body, urlencoded
    {
        const HtmlDocument d = ParseHtml("<base href='https://other.test/base/'><form id=p method=POST action=login>"
                                         "<input name='a b' value='1+1=2'><input type=submit></form>"
                                         "<form id=t method=post enctype=text/plain><input name=x value='y z'></form>"
                                         "<form id=m method=post enctype=multipart/form-data><input name=k value=v><input type=file name=f></form>"
                                         "<form id=j action='javascript:alert(1)'><input name=z></form>");
        FormState f;
        f.Reset(d);
        const Submission p = f.Submit(*ById(*d.root, "p"), nullptr, "https://site.test/x", d);
        CHECK(p.ok && p.method == "POST" && p.url == "https://other.test/base/login" && p.body == "a+b=1%2B1%3D2" &&
              p.enctype == "application/x-www-form-urlencoded");
        const Submission t = f.Submit(*ById(*d.root, "t"), nullptr, "https://site.test/x", d);
        CHECK(t.ok && t.body == "x=y z\r\n" && t.enctype == "text/plain");
        const Submission m = f.Submit(*ById(*d.root, "m"), nullptr, "https://site.test/x", d);
        CHECK(m.ok && m.body == "k=v" && !m.note.empty());   // files are not sent, and it says so
        const Submission j = f.Submit(*ById(*d.root, "j"), nullptr, "https://site.test/x", d);
        CHECK(!j.ok);   // never a javascript: address
    }
    // implicit submission: Enter in a field presses the form's first submit button; a form with one field and no button
    // submits too; a textarea never
    {
        const Node* form = nullptr; const Node* sub = nullptr;
        CHECK(fs.ImplicitSubmitter(el("name"), doc, form, sub) && form == &el("f") && sub == &el("go"));
        CHECK(fs.ImplicitSubmitter(el("only"), doc, form, sub) && form == &el("g") && sub == nullptr);
        CHECK(!fs.ImplicitSubmitter(el("ta"), doc, form, sub));
    }
    // reset: back to the document's values
    fs.ResetForm(&el("f"));
    CHECK(fs.Value(el("name")) == "Ann" && fs.Checked(el("keep")) && fs.Checked(el("r1")) && !fs.Checked(el("r2")) && fs.SelectedIndex(el("sel")) == 1);

    // ---- through the tab: typing, a POST with its body, the answer ----
    {
        BrowserTab t;
        t.SetAnyOrigin(true);
        t.Open("https://site.test/login.html");
        t.TakeFetches();
        t.DeliverPage("https://site.test/login.html", true, 200,
                      "<form method=post action=/session><input id=u name=user><input id=p type=password name=pass><button id=s>Sign in</button></form>");
        const Node& u = *ById(*t.Document()->root, "u");
        const Node& p = *ById(*t.Document()->root, "p");
        t.Forms().SetValue(u, "me@example.test");
        t.Forms().SetValue(p, "p&ss w");
        // the layout shows the typed value (and bullets for the password)
        const FixedMeasure fm;
        const LayoutResult& lay = t.Layout(600, fm);
        int found = 0;
        for (const DrawCmd& c : lay.cmds) if (c.kind == DrawCmd::Control) { if (c.text == "me@example.test" || c.text == "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2") ++found; }
        CHECK(found == 2 && lay.controls.size() == 3);   // the two fields and the <button>
        std::string note;
        CHECK(t.SubmitForm(*ById(*t.Document()->root, "u")->parent, ById(*t.Document()->root, "s"), note));
        const std::vector<std::string> f = t.TakeFetches();
        std::string body;
        CHECK(f.size() == 1 && f[0] == "https://site.test/session" && t.PostBody(f[0], body) && body == "user=me%40example.test&pass=p%26ss+w");
        t.DeliverPage("https://site.test/session", true, 200, "<title>Hi</title>");
        CHECK(t.Title() == "Hi");
    }

    // ---- linked style sheets, @import, url() against the sheet, web fonts ----
    {
        BrowserTab t;
        t.SetAnyOrigin(true);
        t.Open("https://site.test/a/page.html");
        t.TakeFetches();
        t.DeliverPage("https://site.test/a/page.html", true, 200,
                      "<link rel=stylesheet href=../css/main.css><style>#x{height:5px}</style><link rel=stylesheet href=print.css media=print>"
                      "<div id=x></div>");
        std::vector<std::string> f = t.TakeFetches();
        CHECK(f.size() == 1 && f[0] == "https://site.test/css/main.css" && t.KindOfFetch(f[0]) == BrowserTab::FetchKind::Style);
        CHECK(t.Sheets().size() == 1);   // the inline one so far
        t.DeliverStyle("https://site.test/css/main.css", true,
                       "@import 'base.css'; #x{width:40px;background:url(img/bg.png)} @font-face{font-family:'My Font';src:url(../fonts/my.ttf) format('truetype')}");
        f = t.TakeFetches();
        CHECK(f.size() == 2);
        if (f.size() == 2) {
            CHECK(f[0] == "https://site.test/fonts/my.ttf" && t.KindOfFetch(f[0]) == BrowserTab::FetchKind::Font && t.FontFor(f[0]) &&
                  t.FontFor(f[0])->family == "my font");
            CHECK(f[1] == "https://site.test/css/base.css" && t.KindOfFetch(f[1]) == BrowserTab::FetchKind::Style);
        }
        t.DeliverStyle("https://site.test/css/base.css", true, "#x{width:10px;height:1px}");
        CHECK(t.Sheets().size() == 3);   // base (imported, first), main, then the inline <style>
        const FixedMeasure fm;
        const LayoutResult& lay = t.Layout(300, fm);
        const BoxInfo* x = nullptr;
        for (const BoxInfo& b : lay.boxes) if (b.node && b.node->Attr("id") && *b.node->Attr("id") == "x") x = &b;
        CHECK(x && x->w == 40 && x->h == 5);   // main.css wins over base.css; the inline sheet comes last
        f = t.TakeFetches();   // the background image, resolved against the sheet's own address
        CHECK(f.size() == 1 && f[0] == "https://site.test/css/img/bg.png" && t.KindOfFetch(f[0]) == BrowserTab::FetchKind::Image);
    }
    TEST_MAIN_RESULT();
}
