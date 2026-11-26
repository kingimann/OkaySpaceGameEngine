// Browser: forms (see Forms.hpp). Written from the WHATWG HTML standard, section 4.10; no code from any browser.
#include "okay/Browser/Forms.hpp"

#include "okay/Browser/Url.hpp"
#include "okay/Core/StringUtil.hpp"

#include <algorithm>
#include <cstdlib>

namespace okay::browser {

namespace {

using okay::str::Trim;
using okay::str::LowerAscii;
std::string AttrOr(const Node& el, const char* name, const std::string& fallback = std::string()) {
    const std::string* v = el.Attr(name);
    return v ? *v : fallback;
}
// Every element below `root`, in tree order.
void Walk(const Node& root, std::vector<const Node*>& out) {
    std::vector<const Node*> todo{&root};
    while (!todo.empty()) {
        const Node* n = todo.back();
        todo.pop_back();
        if (n->kind == NodeKind::Element) out.push_back(n);
        for (std::size_t k = n->children.size(); k-- > 0;) todo.push_back(n->children[k].get());
    }
}
// Newlines to CRLF (4.10.21.4 "converting an entry list to a list of name-value pairs").
std::string CrLf(const std::string& s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') { o += "\r\n"; if (i + 1 < s.size() && s[i + 1] == '\n') ++i; }
        else if (s[i] == '\n') o += "\r\n";
        else o += s[i];
    }
    return o;
}
// The value of an <option>: its value="" or its text with spaces collapsed.
std::string OptionValue(const Node& o) {
    if (const std::string* v = o.Attr("value")) return *v;
    std::string t = o.TextContent(), out;
    bool sp = false;
    for (char c : Trim(t)) {
        const bool w = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
        if (w) { if (!sp) out += ' '; sp = true; } else { out += c; sp = false; }
    }
    return out;
}
std::vector<const Node*> OptionsOf(const Node& select) {
    std::vector<const Node*> all, out;
    Walk(select, all);
    for (const Node* n : all) if (n->name == "option") out.push_back(n);
    return out;
}

} // namespace

std::string InputType(const Node& el) {
    const std::string t = LowerAscii(Trim(AttrOr(el, "type", "text")));
    static const char* const known[] = {"text", "password", "checkbox", "radio", "submit", "reset", "button", "hidden", "email",
                                        "search", "url", "tel", "number", "file", "image", "range", "date", "color", "time",
                                        "datetime-local", "month", "week", nullptr};
    for (const char* const* k = known; *k; ++k) if (t == *k) return t;
    return "text";
}

ControlKind KindOf(const Node& el) {
    if (el.kind != NodeKind::Element) return ControlKind::None;
    if (el.name == "textarea") return ControlKind::TextArea;
    if (el.name == "select") return ControlKind::Select;
    if (el.name == "button") {
        const std::string t = LowerAscii(Trim(AttrOr(el, "type", "submit")));
        return t == "reset" ? ControlKind::Reset : t == "button" ? ControlKind::Button : ControlKind::Submit;
    }
    if (el.name != "input") return ControlKind::None;
    const std::string t = InputType(el);
    if (t == "password") return ControlKind::Password;
    if (t == "checkbox") return ControlKind::Checkbox;
    if (t == "radio") return ControlKind::Radio;
    if (t == "submit") return ControlKind::Submit;
    if (t == "reset") return ControlKind::Reset;
    if (t == "button") return ControlKind::Button;
    if (t == "hidden") return ControlKind::Hidden;
    if (t == "file") return ControlKind::File;
    if (t == "image") return ControlKind::Image;
    return ControlKind::Text;   // text, email, search, url, tel, number and the rest: typed as text
}

bool FormState::IsTypable(const Node& c) {
    const ControlKind k = KindOf(c);
    return k == ControlKind::Text || k == ControlKind::Password || k == ControlKind::TextArea;
}

bool FormState::IsDisabled(const Node& c) {
    if (c.Attr("disabled")) return true;
    // a disabled fieldset disables what is in it, except what is in its first legend (4.10.19.2)
    for (const Node* p = c.parent; p; p = p->parent) {
        if (p->IsElement("fieldset") && p->Attr("disabled")) {
            const Node* legend = nullptr;
            for (const auto& k : p->children) if (k->IsElement("legend")) { legend = k.get(); break; }
            bool inLegend = false;
            for (const Node* q = &c; q && q != p; q = q->parent) if (q == legend) inLegend = true;
            if (!inLegend) return true;
        }
    }
    return false;
}

const Node* FormState::FormOwner(const Node& c, const HtmlDocument& doc) {
    if (const std::string* f = c.Attr("form")) {   // form="id": that form, wherever it is
        std::vector<const Node*> all;
        if (doc.root) Walk(*doc.root, all);
        for (const Node* n : all) if (n->name == "form") if (const std::string* id = n->Attr("id")) if (*id == *f) return n;
        return nullptr;
    }
    for (const Node* p = c.parent; p; p = p->parent) if (p->IsElement("form")) return p;
    return nullptr;
}

const Node* FormState::LabelTarget(const Node& label, const HtmlDocument& doc) {
    if (const std::string* f = label.Attr("for")) {
        std::vector<const Node*> all;
        if (doc.root) Walk(*doc.root, all);
        for (const Node* n : all) if (const std::string* id = n->Attr("id")) if (*id == *f) return KindOf(*n) != ControlKind::None ? n : nullptr;
        return nullptr;
    }
    std::vector<const Node*> inside;
    Walk(label, inside);
    for (const Node* n : inside) if (n != &label && KindOf(*n) != ControlKind::None && KindOf(*n) != ControlKind::Hidden) return n;
    return nullptr;
}

void FormState::Reset(const HtmlDocument& doc) {
    m_values.clear(); m_checked.clear(); m_selected.clear();
    m_defaultValues.clear(); m_defaultChecked.clear(); m_defaultSelected.clear();
    m_doc = &doc;
    if (!doc.root) return;
    std::vector<const Node*> all;
    Walk(*doc.root, all);
    for (const Node* n : all) {
        const ControlKind k = KindOf(*n);
        if (k == ControlKind::None) continue;
        if (k == ControlKind::TextArea) {
            std::string t = n->TextContent();
            if (!t.empty() && t[0] == '\n') t.erase(0, 1);   // the newline right after <textarea> is not part of the value
            m_defaultValues[n] = t;
        } else if (k == ControlKind::Select) {
            const std::vector<const Node*> opts = OptionsOf(*n);
            int sel = -1;
            for (std::size_t i = 0; i < opts.size(); ++i) if (opts[i]->Attr("selected")) sel = (int)i;   // the last selected (single select)
            if (sel < 0 && !opts.empty() && !n->Attr("multiple")) sel = 0;
            m_defaultSelected[n] = sel;
        } else if (k == ControlKind::Checkbox || k == ControlKind::Radio) {
            m_defaultChecked[n] = n->Attr("checked") != nullptr;
        } else {
            std::string v = AttrOr(*n, "value");
            if (k == ControlKind::Text || k == ControlKind::Password) { v.erase(std::remove(v.begin(), v.end(), '\n'), v.end()); v.erase(std::remove(v.begin(), v.end(), '\r'), v.end()); }
            m_defaultValues[n] = v;
        }
    }
    // a radio group with several "checked": only the last stays checked
    for (const Node* n : all) if (KindOf(*n) == ControlKind::Radio && m_defaultChecked[n]) {
        for (auto& kv : m_defaultChecked) {
            const Node* o = kv.first;
            if (o == n || KindOf(*o) != ControlKind::Radio || !kv.second) continue;
            if (AttrOr(*o, "name") == AttrOr(*n, "name") && !AttrOr(*n, "name").empty() && FormOwner(*o, doc) == FormOwner(*n, doc)) {
                // the earlier one in tree order loses
                const auto io = std::find(all.begin(), all.end(), o), in = std::find(all.begin(), all.end(), n);
                if (io < in) kv.second = false;
            }
        }
    }
    m_values = m_defaultValues;
    m_checked = m_defaultChecked;
    m_selected = m_defaultSelected;
}

void FormState::ResetForm(const Node* form) {
    if (!m_doc) return;
    for (auto& kv : m_defaultValues) if (!form || FormOwner(*kv.first, *m_doc) == form) m_values[kv.first] = kv.second;
    for (auto& kv : m_defaultChecked) if (!form || FormOwner(*kv.first, *m_doc) == form) m_checked[kv.first] = kv.second;
    for (auto& kv : m_defaultSelected) if (!form || FormOwner(*kv.first, *m_doc) == form) m_selected[kv.first] = kv.second;
}

std::string FormState::Value(const Node& c) const {
    auto it = m_values.find(&c);
    if (it != m_values.end()) return it->second;
    return AttrOr(c, "value");
}

void FormState::SetValue(const Node& c, const std::string& v) {
    std::string s = v.size() > kMaxValueBytes ? v.substr(0, kMaxValueBytes) : v;
    if (KindOf(c) != ControlKind::TextArea) { s.erase(std::remove(s.begin(), s.end(), '\n'), s.end()); s.erase(std::remove(s.begin(), s.end(), '\r'), s.end()); }
    if (const std::string* ml = c.Attr("maxlength")) {
        const long n = std::strtol(ml->c_str(), nullptr, 10);
        if (n >= 0) {   // maxlength counts characters (code points here)
            std::size_t cps = 0, i = 0;
            for (; i < s.size(); ++i) if (((unsigned char)s[i] & 0xC0) != 0x80) { if ((long)cps == n) break; ++cps; }
            s.resize(i);
        }
    }
    m_values[&c] = s;
}

bool FormState::Checked(const Node& c) const {
    auto it = m_checked.find(&c);
    return it != m_checked.end() ? it->second : c.Attr("checked") != nullptr;
}

void FormState::SetChecked(const Node& c, bool on) {
    m_checked[&c] = on;
    if (!on || KindOf(c) != ControlKind::Radio || !m_doc) return;
    const std::string name = AttrOr(c, "name");
    if (name.empty()) return;
    const Node* owner = FormOwner(c, *m_doc);
    for (auto& kv : m_checked) {
        const Node* o = kv.first;
        if (o != &c && KindOf(*o) == ControlKind::Radio && AttrOr(*o, "name") == name && FormOwner(*o, *m_doc) == owner) kv.second = false;
    }
}

void FormState::Toggle(const Node& c) {
    if (IsDisabled(c)) return;
    const ControlKind k = KindOf(c);
    if (k == ControlKind::Checkbox) SetChecked(c, !Checked(c));
    else if (k == ControlKind::Radio) SetChecked(c, true);
}

std::vector<std::pair<std::string, std::string>> FormState::Options(const Node& select) const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const Node* o : OptionsOf(select)) out.push_back({OptionValue(*o), Trim(o->TextContent())});
    return out;
}

int FormState::SelectedIndex(const Node& select) const {
    auto it = m_selected.find(&select);
    return it != m_selected.end() ? it->second : -1;
}

void FormState::Select(const Node& select, int index) {
    const int n = (int)OptionsOf(select).size();
    m_selected[&select] = index >= 0 && index < n ? index : -1;
}

std::string FormState::Display(const Node& c) const {
    switch (KindOf(c)) {
    case ControlKind::Password: {
        std::string v = Value(c), dots;
        for (unsigned char ch : v) if ((ch & 0xC0) != 0x80) dots += "\xE2\x80\xA2";   // one bullet per character
        return dots;
    }
    case ControlKind::Submit: case ControlKind::Reset: case ControlKind::Button:
        if (c.name == "button") return Trim(c.TextContent());
        if (const std::string* v = c.Attr("value")) return *v;
        return KindOf(c) == ControlKind::Reset ? "Reset" : KindOf(c) == ControlKind::Submit ? "Submit" : "";
    case ControlKind::Select: {
        const int i = SelectedIndex(c);
        const auto opts = Options(c);
        return i >= 0 && i < (int)opts.size() ? opts[(std::size_t)i].second : std::string();
    }
    case ControlKind::File: return "Choose file (not supported)";
    case ControlKind::Checkbox: case ControlKind::Radio: return Checked(c) ? "\x01" : std::string();   // the layout passes it to the view
    case ControlKind::Hidden: case ControlKind::None: case ControlKind::Image: return std::string();
    default: return Value(c);
    }
}

std::vector<std::pair<std::string, std::string>> FormState::Entries(const Node& form, const Node* submitter, const HtmlDocument& doc) const {
    std::vector<std::pair<std::string, std::string>> out;
    std::vector<const Node*> all;
    if (doc.root) Walk(*doc.root, all);
    for (const Node* n : all) {
        const ControlKind k = KindOf(*n);
        if (k == ControlKind::None || FormOwner(*n, doc) != &form || IsDisabled(*n)) continue;
        if (n->name == "option" || n->name == "fieldset") continue;
        const bool button = k == ControlKind::Submit || k == ControlKind::Reset || k == ControlKind::Button;
        if (button && n != submitter) continue;
        if ((k == ControlKind::Checkbox || k == ControlKind::Radio) && !Checked(*n)) continue;
        if (k == ControlKind::Image || k == ControlKind::File) continue;   // no files; an image button sends coordinates (not done)
        const std::string name = AttrOr(*n, "name");
        if (name.empty()) continue;
        if (k == ControlKind::Select) {
            const int i = SelectedIndex(*n);
            const auto opts = Options(*n);
            if (i >= 0 && i < (int)opts.size()) out.push_back({name, opts[(std::size_t)i].first});
            continue;
        }
        if (k == ControlKind::Checkbox || k == ControlKind::Radio) { out.push_back({name, AttrOr(*n, "value", "on")}); continue; }
        if (k == ControlKind::TextArea) { out.push_back({CrLf(name), CrLf(Value(*n))}); continue; }
        if (n->name == "button") { out.push_back({name, AttrOr(*n, "value")}); continue; }
        out.push_back({name, Value(*n)});
        // dirname="" is not done (no text direction)
    }
    return out;
}

Submission FormState::Submit(const Node& form, const Node* submitter, const std::string& pageUrl, const HtmlDocument& doc) const {
    Submission s;
    // the submitter's formaction / formmethod / formenctype win over the form's (4.10.18.6)
    auto pick = [&](const char* own, const char* formAttr) {
        if (submitter) if (const std::string* v = submitter->Attr(own)) return *v;
        return AttrOr(form, formAttr);
    };
    std::string base = pageUrl;
    if (!doc.baseHref.empty()) base = ResolveUrl(base, doc.baseHref);
    const std::string action = Trim(pick("formaction", "action"));
    std::string url = action.empty() ? pageUrl : ResolveUrl(base, action);
    const std::string scheme = ParseUrl(url).scheme;
    if (scheme != "http" && scheme != "https") { s.note = "The form sends to an address that cannot be opened (only http and https)."; return s; }
    const std::string method = LowerAscii(Trim(pick("formmethod", "method")));
    std::string enctype = LowerAscii(Trim(pick("formenctype", "enctype")));
    const auto entries = Entries(form, submitter, doc);
    if (enctype == "multipart/form-data") {
        enctype = "application/x-www-form-urlencoded";
        s.note = "This form sends files; files are not sent yet, only its fields.";
    }
    if (enctype != "text/plain") enctype = "application/x-www-form-urlencoded";
    std::string body;
    if (enctype == "text/plain") for (const auto& e : entries) body += e.first + "=" + e.second + "\r\n";
    else body = FormUrlEncodePairs(entries);
    s.ok = true;
    if (method == "post") {
        s.method = "POST";
        s.url = url;
        s.body = body;
        s.enctype = enctype;
    } else {   // GET (and dialog, which is not done): the entries become the query; the fragment stays (4.10.21.3)
        s.method = "GET";
        Url u = ParseUrl(url);
        u.query = enctype == "text/plain" ? FormUrlEncodePairs(entries) : body;
        u.hasQuery = true;
        s.url = ToString(u);
    }
    return s;
}

bool FormState::ImplicitSubmitter(const Node& control, const HtmlDocument& doc, const Node*& form, const Node*& submitter) const {
    form = FormOwner(control, doc);
    submitter = nullptr;
    if (!form || KindOf(control) == ControlKind::TextArea) return false;
    std::vector<const Node*> all;
    if (doc.root) Walk(*doc.root, all);
    int fields = 0;
    for (const Node* n : all) {
        if (FormOwner(*n, doc) != form) continue;
        const ControlKind k = KindOf(*n);
        if (k == ControlKind::Submit && !submitter) submitter = n;   // the default button: the first submit button
        if (k == ControlKind::Text || k == ControlKind::Password) ++fields;
    }
    if (submitter) return !IsDisabled(*submitter);
    return fields == 1;   // no button and only one field: Enter submits
}

} // namespace okay::browser
