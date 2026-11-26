// PluginRegistry (PluginRegistry.hpp): commands and providers that editor plugins add to a host window.
#include "PluginRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace pluginreg {


bool ParseShortcut(const std::string& text, Shortcut& out) {
    out = Shortcut{};
    std::string t;
    for (char c : text) if (c != ' ') t += c;
    if (t.empty()) return false;
    std::size_t a = 0;
    while (a < t.size()) {
        std::size_t p = t.find('+', a);
        if (p == a) p = t.find('+', a + 1);              // a '+' key on its own ("Ctrl++")
        std::string part = t.substr(a, p == std::string::npos ? std::string::npos : p - a);
        for (char& c : part) c = (char)std::toupper((unsigned char)c);   // key names are kept in upper case
        const bool last = p == std::string::npos;
        if (part == "CTRL" || part == "CMD" || part == "CONTROL") out.ctrl = true;
        else if (part == "SHIFT") out.shift = true;
        else if (part == "ALT" || part == "OPTION") out.alt = true;
        else {
            if (!out.key.empty() || !last) return false;   // two keys, or a key before a modifier
            out.key = part == "RETURN" ? "ENTER" : part == "ESC" ? "ESCAPE" : part;
        }
        if (last) break;
        a = p + 1;
    }
    return !out.key.empty();
}

std::string FormatShortcut(const Shortcut& s) {
    if (s.key.empty()) return "";
    std::string out;
    if (s.ctrl) out += "Ctrl+";
    if (s.shift) out += "Shift+";
    if (s.alt) out += "Alt+";
    std::string k = s.key;
    if (k.size() > 1 && k[0] != 'F') { for (std::size_t i = 1; i < k.size(); ++i) k[i] = (char)std::tolower((unsigned char)k[i]); }
    return out + k;
}

std::vector<const Command*> RankCommands(const std::string& query, const std::vector<const Command*>& commands) {
    if (query.empty()) return commands;
    std::vector<std::pair<int, const Command*>> scored;
    for (const Command* c : commands) {
        int s = scriptide::FuzzyScore(query, c->title);
        if (s >= 0) s += 20;                                  // a title match beats a category match
        else s = scriptide::FuzzyScore(query, c->category + ": " + c->title);
        if (s >= 0) scored.push_back({s, c});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<const Command*> out;
    for (const auto& s : scored) out.push_back(s.second);
    return out;
}

bool PluginRegistry::AddCommand(Command c) {
    if (c.id.empty() || !c.run) return false;
    if (c.title.empty()) c.title = c.id;
    if (!c.shortcut.empty()) { Shortcut s; if (ParseShortcut(c.shortcut, s)) c.shortcut = FormatShortcut(s); else c.shortcut.clear(); }
    for (auto& e : commands_) if (e.id == c.id) { e = std::move(c); return true; }
    commands_.push_back(std::move(c));
    return true;
}
bool PluginRegistry::RemoveCommand(const std::string& id) {
    const auto n = commands_.size();
    commands_.erase(std::remove_if(commands_.begin(), commands_.end(), [&](const Command& c) { return c.id == id; }), commands_.end());
    return commands_.size() != n;
}
const Command* PluginRegistry::FindCommand(const std::string& id) const {
    for (const auto& c : commands_) if (c.id == id) return &c;
    return nullptr;
}
std::vector<const Command*> PluginRegistry::Commands() const {
    std::vector<const Command*> out;
    for (const auto& c : commands_) out.push_back(&c);
    return out;
}
const Command* PluginRegistry::CommandForShortcut(const Shortcut& s) const {
    if (s.Empty()) return nullptr;
    for (const auto& c : commands_) {
        Shortcut cs;
        if (!c.shortcut.empty() && ParseShortcut(c.shortcut, cs) && cs == s) return &c;
    }
    return nullptr;
}
bool PluginRegistry::Run(const std::string& id) const {
    const Command* c = FindCommand(id);
    if (!c) return false;
    try { c->run(); } catch (...) { return false; }
    return true;
}

void PluginRegistry::AddCompletion(const std::string& owner, const std::string& language, CompletionFn fn) { if (fn) completions_.push_back({owner, language, std::move(fn)}); }
void PluginRegistry::AddLint(const std::string& owner, const std::string& language, LintFn fn) { if (fn) lints_.push_back({owner, language, std::move(fn)}); }
void PluginRegistry::AddFormatter(const std::string& owner, const std::string& language, FormatFn fn) { if (fn) formatters_.push_back({owner, language, std::move(fn)}); }
void PluginRegistry::AddHover(const std::string& owner, const std::string& language, HoverFn fn) { if (fn) hovers_.push_back({owner, language, std::move(fn)}); }
void PluginRegistry::AddSnippet(Snippet s) {
    if (s.prefix.empty() || s.body.empty()) return;
    if (s.title.empty()) s.title = s.prefix;
    snippets_.push_back(std::move(s));
}
void PluginRegistry::AddPanel(SidePanel p) {
    if (p.title.empty() || !p.draw) return;
    if (p.id.empty()) p.id = p.owner + "#" + p.title;
    for (auto& e : panels_) if (e.id == p.id) { e = std::move(p); return; }
    panels_.push_back(std::move(p));
}

std::vector<CompletionItem> PluginRegistry::Complete(const DocContext& ctx) const {
    std::vector<CompletionItem> out;
    for (const auto& p : completions_) {
        if (!ForLanguage(p.language, ctx.language)) continue;
        try {
            for (auto& it : p.fn(ctx)) {
                if (it.label.empty()) continue;
                if (it.insert.empty()) it.insert = it.label;
                it.owner = p.owner;
                out.push_back(std::move(it));
            }
        } catch (...) {}
    }
    return out;
}
std::vector<scriptide::Diagnostic> PluginRegistry::Lint(const DocContext& ctx) const {
    std::vector<scriptide::Diagnostic> out;
    for (const auto& p : lints_) {
        if (!ForLanguage(p.language, ctx.language)) continue;
        try {
            for (auto& d : p.fn(ctx)) {
                if (d.source.empty()) d.source = OwnerName(p.owner);
                out.push_back(std::move(d));
            }
        } catch (...) {}
    }
    std::stable_sort(out.begin(), out.end(), [](const scriptide::Diagnostic& a, const scriptide::Diagnostic& b) { return a.line < b.line; });
    return out;
}
bool PluginRegistry::Format(const DocContext& ctx, std::string& out) const {
    for (const auto& p : formatters_) {
        if (!ForLanguage(p.language, ctx.language)) continue;
        try { if (p.fn(ctx, out)) return true; } catch (...) {}
    }
    return false;
}
bool PluginRegistry::HasFormatter(const std::string& language) const {
    for (const auto& p : formatters_) if (ForLanguage(p.language, language)) return true;
    return false;
}
std::string PluginRegistry::Hover(const DocContext& ctx) const {
    std::string out;
    for (const auto& p : hovers_) {
        if (!ForLanguage(p.language, ctx.language)) continue;
        try {
            const std::string h = p.fn(ctx);
            if (!h.empty()) out += (out.empty() ? "" : "\n\n") + h;
        } catch (...) {}
    }
    return out;
}
std::vector<Snippet> PluginRegistry::Snippets(const std::string& language) const {
    std::vector<Snippet> out;
    for (const auto& s : snippets_) if (ForLanguage(s.language, language)) out.push_back(s);
    return out;
}

int PluginRegistry::RemoveOwner(const std::string& owner) {
    int n = 0;
    auto drop = [&](auto& v) {
        const auto before = v.size();
        v.erase(std::remove_if(v.begin(), v.end(), [&](const auto& e) { return e.owner == owner; }), v.end());
        n += (int)(before - v.size());
    };
    drop(commands_); drop(completions_); drop(lints_); drop(formatters_); drop(hovers_); drop(snippets_); drop(panels_);
    names_.erase(std::remove_if(names_.begin(), names_.end(), [&](const auto& kv) { return kv.first == owner; }), names_.end());
    return n;
}
void PluginRegistry::SetOwnerName(const std::string& owner, const std::string& name) {
    for (auto& kv : names_) if (kv.first == owner) { kv.second = name; return; }
    names_.push_back({owner, name});
}
std::string PluginRegistry::OwnerName(const std::string& owner) const {
    for (const auto& kv : names_) if (kv.first == owner) return kv.second;
    return owner.empty() ? std::string("editor") : std::filesystem::path(owner).stem().string();
}
void PluginRegistry::Clear() {
    commands_.clear(); completions_.clear(); lints_.clear(); formatters_.clear(); hovers_.clear(); snippets_.clear();
    panels_.clear(); names_.clear();
}

std::vector<scriptide::Diagnostic> ScriptProblems(const PluginRegistry& reg, const DocContext& ctx, std::vector<scriptide::Diagnostic> own) {
    for (auto& d : reg.Lint(ctx)) own.push_back(std::move(d));
    std::stable_sort(own.begin(), own.end(), [](const scriptide::Diagnostic& a, const scriptide::Diagnostic& b) { return a.line < b.line; });
    return own;
}

PluginRegistry& ScriptEditorPlugins() {
    static PluginRegistry r;
    return r;
}

} // namespace pluginreg
