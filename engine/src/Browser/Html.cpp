// Browser (page engine): the HTML reader. Written from the WHATWG HTML standard, section 13.2 (tokenization and a reduced
// tree construction); what is reduced is listed in Html.hpp.
#include "okay/Browser/Html.hpp"

#include "okay/Core/StringUtil.hpp"
#include "okay/Browser/Encoding.hpp"
#include "okay/Core/Utf8.hpp"

#include <cstdint>
#include <cstring>

namespace okay::browser {

// ---- small helpers ---------------------------------------------------------------------------------------------------

namespace {

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r'; }
bool IsAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool IsDigit(char c) { return c >= '0' && c <= '9'; }
bool IsAlnum(char c) { return IsAlpha(c) || IsDigit(c); }
using okay::str::LowerAsciiChar;

bool In(const std::string& s, const char* const* list) {
    for (; *list; ++list) if (s == *list) return true;
    return false;
}

// Named character references this reader knows (a subset of the standard's table: the ones a hand-written page uses).
// `legacy` ones are also recognised without their ';' (13.2.5.73).
struct NamedRef { const char* name; std::uint32_t cp; bool legacy; };
const NamedRef kNamed[] = {
    {"amp", '&', true}, {"lt", '<', true}, {"gt", '>', true}, {"quot", '"', true}, {"apos", '\'', false},
    {"nbsp", 0xA0, true}, {"copy", 0xA9, true}, {"reg", 0xAE, true}, {"trade", 0x2122, false}, {"deg", 0xB0, true},
    {"plusmn", 0xB1, true}, {"times", 0xD7, true}, {"divide", 0xF7, true}, {"middot", 0xB7, true}, {"para", 0xB6, true},
    {"sect", 0xA7, true}, {"laquo", 0xAB, true}, {"raquo", 0xBB, true}, {"cent", 0xA2, true}, {"pound", 0xA3, true},
    {"yen", 0xA5, true}, {"euro", 0x20AC, false}, {"hellip", 0x2026, false}, {"mdash", 0x2014, false}, {"ndash", 0x2013, false},
    {"lsquo", 0x2018, false}, {"rsquo", 0x2019, false}, {"ldquo", 0x201C, false}, {"rdquo", 0x201D, false}, {"bull", 0x2022, false},
    {"larr", 0x2190, false}, {"rarr", 0x2192, false}, {"uarr", 0x2191, false}, {"darr", 0x2193, false}, {"harr", 0x2194, false},
    {"hearts", 0x2665, false}, {"check", 0x2713, false}, {"shy", 0xAD, true}, {"iexcl", 0xA1, true}, {"iquest", 0xBF, true},
    {"frac12", 0xBD, true}, {"frac14", 0xBC, true}, {"frac34", 0xBE, true}, {"sup2", 0xB2, true}, {"sup3", 0xB3, true},
    {"micro", 0xB5, true}, {"eacute", 0xE9, true}, {"egrave", 0xE8, true}, {"aacute", 0xE1, true}, {"agrave", 0xE0, true},
    {"ouml", 0xF6, true}, {"uuml", 0xFC, true}, {"auml", 0xE4, true}, {"szlig", 0xDF, true}, {"ccedil", 0xE7, true},
    {"ntilde", 0xF1, true}, {"thinsp", 0x2009, false}, {"ensp", 0x2002, false}, {"emsp", 0x2003, false}, {"zwj", 0x200D, false},
    {"zwnj", 0x200C, false}, {"dagger", 0x2020, false}, {"permil", 0x2030, false}, {"prime", 0x2032, false}, {"infin", 0x221E, false},
    {"ne", 0x2260, false}, {"le", 0x2264, false}, {"ge", 0x2265, false}, {"minus", 0x2212, false}, {"star", 0x2606, false},
};

// HTML 13.2.6.4.1 (the initial insertion mode): does this DOCTYPE put the page in quirks mode? `rest` is what follows the name,
// lower-cased. A reduced form of the standard's list: a name other than "html", the legacy public identifiers that start with
// the HTML 2.0 / 3.x / 4.0 / IETF / Netscape / Microsoft prefixes, and HTML 4.01 Transitional or Frameset with no system id.
bool QuirksDoctype(const std::string& name, const std::string& rest) {
    if (name != "html") return true;
    const std::size_t pub = rest.find("public");
    if (pub == std::string::npos) return rest.find("http://www.ibm.com/data/dtd/v11/ibmxhtml1-transitional.dtd") != std::string::npos;
    std::size_t q = rest.find_first_of("\"'", pub);
    if (q == std::string::npos) return false;
    const char quote = rest[q];
    const std::size_t qe = rest.find(quote, q + 1);
    const std::string id = rest.substr(q + 1, qe == std::string::npos ? std::string::npos : qe - q - 1);
    const bool systemId = qe != std::string::npos && rest.find_first_of("\"'", qe + 1) != std::string::npos;
    static const char* const kPrefixes[] = {"-//ietf//dtd html", "-//w3c//dtd html 3", "-//w3c//dtd html 4.0 transitional//",
                                            "-//w3c//dtd html 4.0 frameset//", "-//w3o//dtd w3 html", "-//netscape comm. corp.//",
                                            "-//microsoft//dtd internet explorer", "-//webtechs//dtd mozilla html", "-//w3c//dtd w3 html//",
                                            "-//w3c//dtd html experimental", "-//sq//dtd html 2.0", "-//softquad"};
    for (const char* p : kPrefixes) if (id.compare(0, std::strlen(p), p) == 0) return true;
    if (id == "-/w3c/dtd html 4.0 transitional/en" || id == "html") return true;
    if (!systemId && (id.compare(0, 36, "-//w3c//dtd html 4.01 transitional//") == 0 || id.compare(0, 32, "-//w3c//dtd html 4.01 frameset//") == 0))
        return true;
    return false;
}

// The numeric character reference end step (13.2.5.80): code points 0x80-0x9F are read as windows-1252.
std::uint32_t FixNumeric(std::uint64_t v) {
    if (v == 0 || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return 0xFFFD;
    if (v >= 0x80 && v <= 0x9F) return SingleByteCodePoint(TextEncoding::Windows1252, (unsigned char)v);   // Encoding.hpp
    return (std::uint32_t)v;
}

// Try to read one character reference at text[i] == '&'. On success appends the decoded text, moves i past it, returns true.
bool ReadCharRef(const std::string& text, std::size_t& i, bool inAttribute, std::string& out) {
    const std::size_t n = text.size();
    std::size_t j = i + 1;
    if (j < n && text[j] == '#') {
        ++j;
        bool hex = false;
        if (j < n && (text[j] == 'x' || text[j] == 'X')) { hex = true; ++j; }
        const std::size_t digitsAt = j;
        std::uint64_t v = 0;
        while (j < n && (hex ? (IsDigit(text[j]) || (LowerAsciiChar(text[j]) >= 'a' && LowerAsciiChar(text[j]) <= 'f')) : IsDigit(text[j]))) {
            const char c = LowerAsciiChar(text[j]);
            const int d = IsDigit(c) ? c - '0' : c - 'a' + 10;
            if (v <= 0x10FFFFull) v = v * (hex ? 16 : 10) + (std::uint64_t)d;   // stays above the range once past it
            ++j;
        }
        if (j == digitsAt) return false;   // "&#" with no digits: not a reference
        if (j < n && text[j] == ';') ++j;
        okay::utf8::Append(out, FixNumeric(v));
        i = j;
        return true;
    }
    // Named: the longest known name that matches.
    const NamedRef* best = nullptr;
    std::size_t bestLen = 0;
    for (const NamedRef& r : kNamed) {
        const std::size_t len = std::strlen(r.name);
        if (len <= bestLen || text.compare(j, len, r.name) != 0) continue;
        const bool semi = j + len < n && text[j + len] == ';';
        if (!semi && !r.legacy) continue;
        best = &r; bestLen = len;
    }
    if (!best) return false;
    const std::size_t after = j + bestLen;
    const bool semi = after < n && text[after] == ';';
    if (!semi && inAttribute && after < n && (text[after] == '=' || IsAlnum(text[after]))) return false;
    okay::utf8::Append(out, best->cp);
    i = after + (semi ? 1 : 0);
    return true;
}

const char* const kVoid[] = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr",
                             "param", "keygen", "basefont", "bgsound", "frame", nullptr};
const char* const kSpecial[] = {
    "address", "applet", "area", "article", "aside", "base", "basefont", "bgsound", "blockquote", "body", "br", "button", "caption",
    "center", "col", "colgroup", "dd", "details", "dir", "div", "dl", "dt", "embed", "fieldset", "figcaption", "figure", "footer",
    "form", "frame", "frameset", "h1", "h2", "h3", "h4", "h5", "h6", "head", "header", "hgroup", "hr", "html", "iframe", "img",
    "input", "keygen", "li", "link", "listing", "main", "marquee", "menu", "meta", "nav", "noembed", "noframes", "noscript",
    "object", "ol", "p", "param", "plaintext", "pre", "script", "search", "section", "select", "source", "style", "summary",
    "table", "tbody", "td", "template", "textarea", "tfoot", "th", "thead", "title", "tr", "track", "ul", "wbr", "xmp", nullptr};
// Start tags that close an open <p> (13.2.6.4.7).
const char* const kClosesP[] = {
    "address", "article", "aside", "blockquote", "center", "details", "dialog", "dir", "div", "dl", "fieldset", "figcaption",
    "figure", "footer", "header", "hgroup", "main", "menu", "nav", "ol", "p", "search", "section", "summary", "ul", "h1", "h2",
    "h3", "h4", "h5", "h6", "pre", "listing", "form", "table", "hr", "xmp", "plaintext", nullptr};
// End tags handled with "has an element in scope" then "pop until" (13.2.6.4.7).
const char* const kBlockEnd[] = {
    "address", "article", "aside", "blockquote", "button", "center", "details", "dialog", "dir", "div", "dl", "fieldset",
    "figcaption", "figure", "footer", "header", "hgroup", "listing", "main", "menu", "nav", "ol", "pre", "search", "section",
    "summary", "ul", "form", "table", "tbody", "thead", "tfoot", "tr", "td", "th", "caption", "select", "dd", "dt", nullptr};
const char* const kImpliedEnd[] = {"dd", "dt", "li", "optgroup", "option", "p", "rb", "rp", "rt", "rtc", nullptr};
const char* const kHeadContent[] = {"title", "meta", "link", "style", "script", "base", "basefont", "bgsound", "noframes", "template", nullptr};
const char* const kScopeBase[] = {"applet", "caption", "html", "table", "td", "th", "marquee", "object", "template", nullptr};

bool IsHeading(const std::string& t) { return t.size() == 2 && t[0] == 'h' && t[1] >= '1' && t[1] <= '6'; }

// ---- the tree builder ------------------------------------------------------------------------------------------------

struct Builder {
    HtmlDocument& doc;
    const HtmlLimits& lim;
    std::vector<Node*> stack;   // open elements, html first
    std::vector<unsigned char> flags;   // kFlag* of each open element (worked out once, so the scope walks are cheap)
    enum : unsigned char { kFlagSpecial = 1, kFlagScope = 2, kFlagImplied = 4, kFlagHeading = 8 };
    void Push(Node* n) {
        unsigned char f = 0;
        if (In(n->name, kSpecial)) f |= kFlagSpecial;
        if (In(n->name, kScopeBase)) f |= kFlagScope;
        if (In(n->name, kImpliedEnd)) f |= kFlagImplied;
        if (IsHeading(n->name)) f |= kFlagHeading;
        stack.push_back(n);
        flags.push_back(f);
    }
    void Pop() { stack.pop_back(); flags.pop_back(); }
    Node* html = nullptr;
    Node* head = nullptr;
    Node* body = nullptr;
    enum Mode { Initial, BeforeHead, InHead, AfterHead, InBody } mode = Initial;
    std::size_t nodes = 0, textBytes = 0;

    Builder(HtmlDocument& d, const HtmlLimits& l) : doc(d), lim(l) {}

    Node* Current() { return stack.empty() ? doc.root.get() : stack.back(); }

    bool Budget() {
        if (nodes >= lim.maxNodes) { doc.truncated = true; return false; }
        ++nodes;
        return true;
    }
    Node* Append(Node* parent, std::unique_ptr<Node> n) {
        n->parent = parent;
        parent->children.push_back(std::move(n));
        return parent->children.back().get();
    }
    Node* MakeElement(Node* parent, const std::string& name) {
        auto n = std::make_unique<Node>();
        n->kind = NodeKind::Element;
        n->name = name;
        ++nodes;   // the structural elements (html, head, body) are always made, even past the budget
        return Append(parent, std::move(n));
    }
    void AddAttrs(Node* el, const std::vector<std::pair<std::string, std::string>>& attrs) {
        for (const auto& a : attrs) {
            if (el->Attr(a.first.c_str())) continue;
            if (el->attrs.size() >= lim.maxAttrs) { doc.truncated = true; break; }
            el->attrs.push_back(a);
        }
    }

    // ---- scope ----
    bool InScope(const std::string& tag, const char* const* extra) {
        for (std::size_t k = stack.size(); k-- > 0;) {
            const std::string& t = stack[k]->name;
            if (t == tag) return true;
            if (flags[k] & kFlagScope) return false;
            if (extra && In(t, extra)) return false;
        }
        return false;
    }
    bool InButtonScope(const std::string& tag) { static const char* const b[] = {"button", nullptr}; return InScope(tag, b); }
    bool InListItemScope(const std::string& tag) { static const char* const b[] = {"ol", "ul", nullptr}; return InScope(tag, b); }
    bool InTableScope(const std::string& tag) {
        for (std::size_t k = stack.size(); k-- > 0;) {
            const std::string& t = stack[k]->name;
            if (t == tag) return true;
            if (t == "html" || t == "table" || t == "template") return false;
        }
        return false;
    }
    bool AnyHeadingInScope() {
        for (std::size_t k = stack.size(); k-- > 0;) {
            if (flags[k] & kFlagHeading) return true;
            if (flags[k] & kFlagScope) return false;
        }
        return false;
    }
    void GenerateImpliedEnd(const char* except) {
        while (stack.size() > 1 && (flags.back() & kFlagImplied) && !(except && stack.back()->name == except)) Pop();
    }
    void PopUntil(const std::string& tag) {
        while (stack.size() > 1) {
            const bool hit = stack.back()->name == tag;
            Pop();
            if (hit) return;
        }
    }
    void ClosePIfInButtonScope() {
        if (!InButtonScope("p")) return;
        GenerateImpliedEnd("p");
        PopUntil("p");
    }

    // ---- making the structure ----
    void EnsureHtml() {
        if (html) return;
        html = MakeElement(doc.root.get(), "html");
        Push(html);
        mode = BeforeHead;
    }
    void EnsureHead() {
        EnsureHtml();
        if (head) return;
        head = MakeElement(html, "head");
        Push(head);
        mode = InHead;
    }
    void EnsureBody() {
        EnsureHead();
        if (body) return;
        while (stack.size() > 1 && stack.back() != html) Pop();   // leave head
        body = MakeElement(html, "body");
        Push(body);
        mode = InBody;
    }

    // ---- tokens ----
    void Text(const std::string& s) {
        if (s.empty()) return;
        bool allSpace = true;
        for (char c : s) if (!IsSpace(c)) { allSpace = false; break; }
        Node* parent = nullptr;
        if ((mode == InHead && Current() != head) || (mode == AfterHead && headReopened && Current() != head && Current() != html)) {
            parent = Current();   // inside <title>, <style>, <script> ... in head
        } else if (mode == Initial || mode == BeforeHead) {
            if (allSpace) return;
            EnsureBody();
        } else if (mode == InHead) {
            if (allSpace) parent = Current();
            else EnsureBody();
        } else if (mode == AfterHead) {
            if (allSpace) parent = html;
            else EnsureBody();
        }
        if (!parent) parent = Current();
        if (textBytes + s.size() > lim.maxTextBytes) { doc.truncated = true; return; }
        textBytes += s.size();
        if (!parent->children.empty() && parent->children.back()->kind == NodeKind::Text) {
            parent->children.back()->data += s;
            return;
        }
        if (!Budget()) return;
        auto n = std::make_unique<Node>();
        n->kind = NodeKind::Text;
        n->data = s;
        Append(parent, std::move(n));
    }
    void Comment(const std::string& s) {
        if (!Budget()) return;
        auto n = std::make_unique<Node>();
        n->kind = NodeKind::Comment;
        n->data = s;
        Node* parent = (mode == Initial) ? doc.root.get() : (mode == AfterHead ? html : Current());
        Append(parent, std::move(n));
    }
    void Doctype(const std::string& name, const std::string& rest) {
        if (mode != Initial || html) return;
        doc.quirks = QuirksDoctype(name, rest);
        if (!Budget()) return;
        auto n = std::make_unique<Node>();
        n->kind = NodeKind::Doctype;
        n->name = name;
        Append(doc.root.get(), std::move(n));
    }

    // Insert an element at the current node; returns it (nullptr when the budget is gone). `push`: enter it.
    Node* Insert(const std::string& tag, const std::vector<std::pair<std::string, std::string>>& attrs, bool push) {
        if (!Budget()) return nullptr;
        auto n = std::make_unique<Node>();
        n->kind = NodeKind::Element;
        n->name = tag;
        Node* el = Append(Current(), std::move(n));
        AddAttrs(el, attrs);
        if (push) {
            if ((int)stack.size() >= lim.maxDepth) doc.truncated = true;   // too deep: added, not entered
            else Push(el);
        }
        return el;
    }

    // Returns true when the element was entered (so the tokenizer may switch to raw text for it).
    bool StartTag(const std::string& tag, const std::vector<std::pair<std::string, std::string>>& attrs, bool& entered) {
        entered = false;
        if (tag == "html") {
            EnsureHtml();
            AddAttrs(html, attrs);
            return false;
        }
        if (mode == Initial || mode == BeforeHead) {
            if (tag == "head") { EnsureHead(); AddAttrs(head, attrs); return false; }
            EnsureHead();
        }
        if (mode == InHead) {
            if (In(tag, kHeadContent)) { entered = InsertHeadThing(tag, attrs); return entered; }
            if (tag == "head") return false;
            if (tag == "noscript") { /* scripting is off: read as body content */ }
            mode = AfterHead;
            while (stack.size() > 1 && stack.back() != html) Pop();
        }
        if (mode == AfterHead) {
            if (tag == "body") {
                body = MakeElement(html, "body");
                AddAttrs(body, attrs);
                Push(body);
                mode = InBody;
                return false;
            }
            if (In(tag, kHeadContent)) {   // put back into head (13.2.6.4.6)
                Push(head);
                entered = InsertHeadThing(tag, attrs);
                if (!entered) Pop();
                else { headReopened = true; }
                return entered;
            }
            EnsureBody();
        }
        // ---- in body ----
        if (tag == "body") { if (body) AddAttrs(body, attrs); return false; }
        if (tag == "head") return false;
        if (tag == "base" || tag == "title" || tag == "meta" || tag == "link" || tag == "style" || tag == "script") {
            entered = InsertHeadThing(tag, attrs);
            return entered;
        }
        if (In(tag, kClosesP)) {
            ClosePIfInButtonScope();
            if (IsHeading(tag) && IsHeading(Current()->name)) Pop();
        }
        if (tag == "li" || tag == "dd" || tag == "dt") {
            for (std::size_t k = stack.size(); k-- > 1;) {
                const std::string& t = stack[k]->name;
                const bool match = (tag == "li") ? (t == "li") : (t == "dd" || t == "dt");
                if (match) { GenerateImpliedEnd(t.c_str()); PopUntil(t); break; }
                if ((flags[k] & kFlagSpecial) && t != "address" && t != "div" && t != "p") break;
            }
            ClosePIfInButtonScope();
        }
        if (tag == "a") {
            for (std::size_t k = stack.size(); k-- > 1;) {
                if (stack[k]->name == "a") { PopUntil("a"); break; }
                if (flags[k] & kFlagSpecial) break;
            }
        }
        if (tag == "button" && InScope("button", nullptr)) { GenerateImpliedEnd(nullptr); PopUntil("button"); }
        if (tag == "option" && Current()->name == "option") Pop();
        if (tag == "td" || tag == "th") {
            for (std::size_t k = stack.size(); k-- > 1;) {
                const std::string& t = stack[k]->name;
                if (t == "td" || t == "th") { GenerateImpliedEnd(nullptr); PopUntil(t); break; }
                if (t == "tr" || t == "table" || t == "html" || t == "template") break;
            }
        }
        if (tag == "tr" || tag == "tbody" || tag == "thead" || tag == "tfoot") {
            for (std::size_t k = stack.size(); k-- > 1;) {
                const std::string& t = stack[k]->name;
                if (t == "td" || t == "th" || t == "tr" || ((tag != "tr") && (t == "tbody" || t == "thead" || t == "tfoot"))) {
                    GenerateImpliedEnd(nullptr); PopUntil(t); continue;
                }
                if (t == "table" || t == "html" || t == "template" || t == "tbody" || t == "thead" || t == "tfoot") break;
            }
        }
        const bool isVoid = IsVoidElement(tag);
        Node* el = Insert(tag, attrs, !isVoid);
        entered = el && !isVoid && Current() == el;
        if (entered && (tag == "pre" || tag == "listing" || tag == "textarea")) skipNewline = true;
        return entered;
    }
    bool headReopened = false;
    bool skipNewline = false;

    bool InsertHeadThing(const std::string& tag, const std::vector<std::pair<std::string, std::string>>& attrs) {
        const bool isVoid = IsVoidElement(tag);
        Node* el = Insert(tag, attrs, !isVoid);
        if (el && tag == "base" && doc.baseHref.empty()) if (const std::string* h = el->Attr("href")) doc.baseHref = *h;
        return el && !isVoid && Current() == el;
    }

    void EndTag(const std::string& tag) {
        if (headReopened && stack.size() > 1 && stack.back()->name == tag && stack.back()->parent == head) {
            Pop();
            if (!stack.empty() && stack.back() == head) Pop();
            headReopened = false;
            return;
        }
        if (mode == Initial || mode == BeforeHead) {
            if (tag != "head" && tag != "body" && tag != "html" && tag != "br") return;
            EnsureHead();
        }
        if (mode == InHead) {
            if (tag == "head") {
                while (stack.size() > 1 && stack.back() != html) Pop();
                mode = AfterHead;
                return;
            }
            if (tag == "title" || tag == "style" || tag == "script" || tag == "noframes" || tag == "template") {
                if (Current()->name == tag) Pop();
                return;
            }
            if (tag != "body" && tag != "html" && tag != "br") return;
            EnsureBody();
        }
        if (mode == AfterHead) {
            if (tag != "body" && tag != "html" && tag != "br") return;
            EnsureBody();
        }
        // ---- in body ----
        if (tag == "body" || tag == "html") return;   // content after </body> still goes into body
        if (tag == "br") { bool e = false; StartTag("br", {}, e); return; }
        if (tag == "p") {
            if (!InButtonScope("p")) { bool e = false; StartTag("p", {}, e); }
            GenerateImpliedEnd("p");
            PopUntil("p");
            return;
        }
        if (tag == "li") {
            if (!InListItemScope("li")) return;
            GenerateImpliedEnd("li");
            PopUntil("li");
            return;
        }
        if (IsHeading(tag)) {
            if (!AnyHeadingInScope()) return;
            GenerateImpliedEnd(nullptr);
            while (stack.size() > 1) { const bool h = (flags.back() & kFlagHeading) != 0; Pop(); if (h) break; }
            return;
        }
        if (tag == "td" || tag == "th" || tag == "tr" || tag == "tbody" || tag == "thead" || tag == "tfoot" || tag == "table") {
            if (!InTableScope(tag)) return;
            GenerateImpliedEnd(nullptr);
            PopUntil(tag);
            return;
        }
        if (In(tag, kBlockEnd)) {
            if (!InScope(tag, nullptr)) return;
            GenerateImpliedEnd(nullptr);
            PopUntil(tag);
            return;
        }
        // "Any other end tag" (13.2.6.4.7), which also stands in for the adoption agency algorithm.
        for (std::size_t k = stack.size(); k-- > 1;) {
            Node* node = stack[k];
            if (node->name == tag) {
                GenerateImpliedEnd(tag.c_str());
                while (stack.size() > k) Pop();
                return;
            }
            if (flags[k] & kFlagSpecial) return;
        }
    }
};

// ---- reading tags and text (13.2.5) ----

struct Tokenizer {
    const std::string& s;
    const HtmlLimits& lim;
    Builder& b;
    HtmlDocument& doc;
    std::size_t i = 0;
    std::string text;   // pending character tokens

    Tokenizer(const std::string& src, const HtmlLimits& l, Builder& bld, HtmlDocument& d) : s(src), lim(l), b(bld), doc(d) {}

    void Flush() {
        if (text.empty()) { b.skipNewline = false; return; }
        if (b.skipNewline) {
            b.skipNewline = false;
            if (text[0] == '\n') text.erase(0, 1);
        }
        b.Text(text);
        text.clear();
    }

    std::string ReadName(std::size_t& j) {
        std::string name;
        while (j < s.size() && !IsSpace(s[j]) && s[j] != '/' && s[j] != '>') {
            if (name.size() < lim.maxNameBytes) name += LowerAsciiChar(s[j] == '\0' ? '?' : s[j]);
            else doc.truncated = true;
            ++j;
        }
        return name;
    }

    // Read the attributes and the end of a tag starting at j (just after the tag name). Returns false at end of input
    // (the tag is dropped, as the standard says for EOF in a tag).
    bool ReadAttrs(std::size_t& j, std::vector<std::pair<std::string, std::string>>& attrs, bool& selfClosing) {
        selfClosing = false;
        const std::size_t n = s.size();
        while (true) {
            while (j < n && IsSpace(s[j])) ++j;
            if (j >= n) return false;
            if (s[j] == '>') { ++j; return true; }
            if (s[j] == '/') {
                ++j;
                if (j < n && s[j] == '>') { selfClosing = true; ++j; return true; }
                continue;
            }
            // attribute name (a leading '=' is part of the name, 13.2.5.32)
            std::string name;
            bool cut = false;
            do {
                if (name.size() < lim.maxNameBytes) name += LowerAsciiChar(s[j]); else cut = true;
                ++j;
            } while (j < n && !IsSpace(s[j]) && s[j] != '/' && s[j] != '>' && s[j] != '=');
            while (j < n && IsSpace(s[j])) ++j;
            std::string value;
            if (j < n && s[j] == '=') {
                ++j;
                while (j < n && IsSpace(s[j])) ++j;
                if (j >= n) return false;
                std::size_t vs = j, ve;
                if (s[j] == '"' || s[j] == '\'') {
                    const char q = s[j];
                    vs = ++j;
                    const std::size_t e = s.find(q, j);
                    if (e == std::string::npos) return false;
                    ve = e; j = e + 1;
                } else {
                    while (j < n && !IsSpace(s[j]) && s[j] != '>') ++j;
                    ve = j;
                }
                std::size_t len = ve - vs;
                if (len > lim.maxAttrValueBytes) { len = lim.maxAttrValueBytes; cut = true; }
                value = DecodeCharRefs(s.substr(vs, len), true);
                if (value.size() > lim.maxAttrValueBytes) { value.resize(lim.maxAttrValueBytes); cut = true; }
            }
            if (cut) doc.truncated = true;
            bool dup = false;
            for (const auto& a : attrs) if (a.first == name) { dup = true; break; }
            if (dup) continue;
            if (attrs.size() >= lim.maxAttrs) { doc.truncated = true; continue; }
            attrs.emplace_back(std::move(name), std::move(value));
        }
    }

    // Raw text (RAWTEXT, or RCDATA when `decode`) up to "</tag" followed by space, '/' or '>'.
    void RawText(const std::string& tag, bool decode) {
        const std::size_t n = s.size();
        std::size_t j = i;
        std::size_t end = n;
        while (true) {
            const std::size_t lt = s.find("</", j);
            if (lt == std::string::npos) break;
            bool match = lt + 2 + tag.size() <= n;
            for (std::size_t k = 0; match && k < tag.size(); ++k) if (LowerAsciiChar(s[lt + 2 + k]) != tag[k]) match = false;
            const std::size_t after = lt + 2 + tag.size();
            if (match && (after >= n || IsSpace(s[after]) || s[after] == '/' || s[after] == '>')) { end = lt; break; }
            j = lt + 2;
        }
        std::string body = s.substr(i, end - i);
        if (decode) body = DecodeCharRefs(body, false);
        if (tag == "style") doc.styleSheets.push_back(body);
        if (tag == "title" && doc.title.empty()) {
            std::string t;
            bool space = false;
            for (char c : body) {
                if (IsSpace(c)) { space = !t.empty(); continue; }
                if (space) t += ' ';
                space = false;
                t += c;
            }
            doc.title = t;
        }
        text += body;
        Flush();
        i = end;
        if (i < n) {   // the end tag itself
            std::size_t k = i + 2;
            ReadName(k);
            std::vector<std::pair<std::string, std::string>> ignored;
            bool sc = false;
            if (!ReadAttrs(k, ignored, sc)) k = n;
            i = k;
            b.EndTag(tag);
        }
    }

    void Run() {
        const std::size_t n = s.size();
        while (i < n) {
            const char c = s[i];
            if (c == '&') {
                if (!ReadCharRef(s, i, false, text)) { text += '&'; ++i; }
                continue;
            }
            if (c != '<') {
                std::size_t e = i;
                while (e < n && s[e] != '<' && s[e] != '&') ++e;
                for (std::size_t k = i; k < e; ++k) {
                    if (s[k] == '\0') continue;   // U+0000 in data is dropped
                    if (s[k] == '\r') { text += '\n'; if (k + 1 < e && s[k + 1] == '\n') ++k; continue; }
                    text += s[k];
                }
                i = e;
                continue;
            }
            // '<'
            if (i + 1 >= n) { text += '<'; ++i; continue; }
            const char d = s[i + 1];
            if (d == '!') { Markup(); continue; }
            if (d == '?') { Bogus(i + 1); continue; }
            if (d == '/') {
                if (i + 2 >= n) { text += "</"; i = n; continue; }
                if (s[i + 2] == '>') { i += 3; continue; }           // "</>" is dropped
                if (!IsAlpha(s[i + 2])) { Bogus(i + 2); continue; }
                std::size_t j = i + 2;
                const std::string name = ReadName(j);
                std::vector<std::pair<std::string, std::string>> ignored;
                bool sc = false;
                if (!ReadAttrs(j, ignored, sc)) { i = n; break; }
                i = j;
                Flush();
                b.EndTag(name);
                continue;
            }
            if (!IsAlpha(d)) { text += '<'; ++i; continue; }
            std::size_t j = i + 1;
            const std::string name = ReadName(j);
            std::vector<std::pair<std::string, std::string>> attrs;
            bool selfClosing = false;
            if (!ReadAttrs(j, attrs, selfClosing)) { i = n; break; }
            i = j;
            Flush();
            bool entered = false;
            b.StartTag(name, attrs, entered);
            if (entered) {
                if (name == "style" || name == "script" || name == "xmp" || name == "iframe" || name == "noembed" || name == "noframes")
                    RawText(name, false);
                else if (name == "title" || name == "textarea") RawText(name, true);
                else if (name == "plaintext") { text += s.substr(i); i = n; }
            }
        }
        Flush();
    }

    void Bogus(std::size_t from) {   // a bogus comment: up to the next '>'
        Flush();
        const std::size_t e = s.find('>', from);
        b.Comment(s.substr(from, (e == std::string::npos ? s.size() : e) - from));
        i = e == std::string::npos ? s.size() : e + 1;
    }

    void Markup() {   // after "<!"
        Flush();
        const std::size_t n = s.size();
        if (s.compare(i + 2, 2, "--") == 0) {
            std::size_t start = i + 4;
            if (start < n && s[start] == '>') { b.Comment(""); i = start + 1; return; }               // "<!-->"
            if (s.compare(start, 2, "->") == 0) { b.Comment(""); i = start + 2; return; }             // "<!--->"
            const std::size_t e = s.find("-->", start);
            if (e == std::string::npos) { b.Comment(s.substr(start)); i = n; return; }
            b.Comment(s.substr(start, e - start));
            i = e + 3;
            return;
        }
        std::string head;
        for (std::size_t k = i + 2; k < n && k < i + 9; ++k) head += LowerAsciiChar(s[k]);
        if (head == "doctype") {
            std::size_t j = i + 9;
            while (j < n && IsSpace(s[j])) ++j;
            std::string name;
            while (j < n && !IsSpace(s[j]) && s[j] != '>') { if (name.size() < lim.maxNameBytes) name += LowerAsciiChar(s[j]); ++j; }
            const std::size_t e = s.find('>', j);
            std::string rest;   // the public and system identifiers, lower-cased (only the start is read)
            for (std::size_t k = j; k < (e == std::string::npos ? n : e) && rest.size() < 256; ++k) rest += LowerAsciiChar(s[k]);
            i = e == std::string::npos ? n : e + 1;
            b.Doctype(name, rest);
            return;
        }
        if (s.compare(i + 2, 7, "[CDATA[") == 0) { Bogus(i + 2); return; }   // CDATA only in foreign content: a comment here
        Bogus(i + 2);
    }
};

} // namespace

// ---- public ------------------------------------------------------------------------------------------------------------

bool IsVoidElement(const std::string& tag) { return In(tag, kVoid); }

const std::string* Node::Attr(const char* attrName) const {
    for (const auto& a : attrs) if (a.first == attrName) return &a.second;
    return nullptr;
}

std::string Node::TextContent() const {
    std::string out;
    std::vector<const Node*> todo{this};
    while (!todo.empty()) {   // an explicit stack: no recursion on a deep tree
        const Node* n = todo.back();
        todo.pop_back();
        if (n->kind == NodeKind::Text) out += n->data;
        for (std::size_t k = n->children.size(); k-- > 0;) todo.push_back(n->children[k].get());
    }
    return out;
}

std::string DecodeCharRefs(const std::string& text, bool inAttribute) {
    if (text.find('&') == std::string::npos) return text;
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '&' && ReadCharRef(text, i, inAttribute, out)) continue;
        out += text[i++];
    }
    return out;
}

const Node* HtmlDocument::Html() const {
    if (!root) return nullptr;
    for (const auto& c : root->children) if (c->IsElement("html")) return c.get();
    return nullptr;
}

const Node* HtmlDocument::Body() const {
    const Node* h = Html();
    if (!h) return nullptr;
    for (const auto& c : h->children) if (c->IsElement("body")) return c.get();
    return nullptr;
}

const Node* HtmlDocument::FindAnchor(const std::string& id) const {
    if (!root || id.empty()) return nullptr;
    std::vector<const Node*> todo{root.get()};
    while (!todo.empty()) {
        const Node* n = todo.back();
        todo.pop_back();
        if (n->kind == NodeKind::Element) {
            const std::string* v = n->Attr("id");
            if (v && *v == id) return n;
            if (n->name == "a") { const std::string* nm = n->Attr("name"); if (nm && *nm == id) return n; }
        }
        for (std::size_t k = n->children.size(); k-- > 0;) todo.push_back(n->children[k].get());
    }
    return nullptr;
}

HtmlDocument ParseHtml(const std::string& textIn, const HtmlLimits& limits) {
    HtmlDocument doc;
    doc.root = std::make_unique<Node>();
    doc.root->kind = NodeKind::Document;
    doc.quirks = true;   // until a DOCTYPE before the first tag says otherwise (Builder::Doctype)
    std::string cut;
    const std::string* text = &textIn;
    if (textIn.size() > limits.maxInputBytes) {
        cut = textIn.substr(0, limits.maxInputBytes);
        text = &cut;
        doc.truncated = true;
    }
    std::size_t start = 0;
    if (text->compare(0, 3, "\xEF\xBB\xBF") == 0) start = 3;   // a UTF-8 byte order mark
    const std::string body = start ? text->substr(start) : std::string();
    Builder b(doc, limits);
    Tokenizer t(start ? body : *text, limits, b, doc);
    t.Run();
    b.EnsureBody();
    return doc;
}

namespace {
void DumpNode(const Node& n, int depth, std::string& out) {
    const std::string pad((std::size_t)depth * 2, ' ');
    switch (n.kind) {
    case NodeKind::Document: break;
    case NodeKind::Doctype: out += pad + "<!DOCTYPE " + n.name + ">\n"; break;
    case NodeKind::Text: out += pad + "\"" + n.data + "\"\n"; break;
    case NodeKind::Comment: out += pad + "<!-- " + n.data + " -->\n"; break;
    case NodeKind::Element:
        out += pad + "<" + n.name + ">\n";
        for (const auto& a : n.attrs) out += pad + "  " + a.first + "=\"" + a.second + "\"\n";
        break;
    }
    const int childDepth = n.kind == NodeKind::Document ? depth : depth + 1;
    for (const auto& c : n.children) DumpNode(*c, childDepth, out);
}
} // namespace

namespace {
void Escape(const std::string& in, bool attr, std::string& out) {
    for (std::size_t i = 0; i < in.size(); ++i) {
        const char ch = in[i];
        if (ch == '&') out += "&amp;";
        else if (ch == '\xC2' && i + 1 < in.size() && in[i + 1] == '\xA0') { out += "&nbsp;"; ++i; }
        else if (attr && ch == '"') out += "&quot;";
        else if (!attr && ch == '<') out += "&lt;";
        else if (!attr && ch == '>') out += "&gt;";
        else out += ch;
    }
}
bool RawTextParent(const Node& n) {
    static const char* const kRaw[] = {"style", "script", "xmp", "iframe", "noembed", "noframes", "plaintext", nullptr};
    return n.kind == NodeKind::Element && In(n.name, kRaw);
}
void SerializeInto(const Node& parent, std::string& out);
void SerializeOne(const Node* parent, const Node& c, std::string& out) {
    switch (c.kind) {
    case NodeKind::Element:
        out += '<';
        out += c.name;
        for (const auto& a : c.attrs) { out += ' '; out += a.first; out += "=\""; Escape(a.second, true, out); out += '"'; }
        out += '>';
        if (IsVoidElement(c.name)) break;
        SerializeInto(c, out);   // depth: the tree is at most HtmlLimits::maxDepth deep (scripts are held to it too)
        out += "</";
        out += c.name;
        out += '>';
        break;
    case NodeKind::Text:
        if (parent && RawTextParent(*parent)) out += c.data;
        else Escape(c.data, false, out);
        break;
    case NodeKind::Comment: out += "<!--" + c.data + "-->"; break;
    case NodeKind::Doctype: out += "<!DOCTYPE " + c.name + ">"; break;
    case NodeKind::Document: SerializeInto(c, out); break;
    }
}
void SerializeInto(const Node& parent, std::string& out) {
    for (const auto& cp : parent.children) SerializeOne(&parent, *cp, out);
}
} // namespace

std::string SerializeNode(const Node& node) {
    std::string out;
    SerializeOne(node.parent, node, out);
    return out;
}

std::string SerializeChildren(const Node& node) {
    std::string out;
    SerializeInto(node, out);
    return out;
}

std::string DumpTree(const Node& node) {
    std::string out;
    DumpNode(node, 0, out);
    return out;
}

} // namespace okay::browser
