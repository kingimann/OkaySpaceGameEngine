// PluginScriptApi (PluginScriptApi.hpp): the script_* builtins of editor plugins.
#include "PluginScriptApi.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"

#include <cctype>

namespace oked {

using V = okay::vs::VsValue;

ScriptEditorHost& ScriptEditorHostApi() {
    static ScriptEditorHost h;
    return h;
}

namespace {
// While a plugin's lint or completion function runs, its script_problem / script_suggest calls land here (main thread only).
std::vector<scriptide::Diagnostic>* g_problems = nullptr;
std::vector<pluginreg::CompletionItem>* g_suggestions = nullptr;

std::string Str(std::vector<V>& a, std::size_t i) { return i < a.size() ? a[i].AsString() : std::string(); }
int Int(std::vector<V>& a, std::size_t i, int d = 0) { return i < a.size() ? (int)a[i].AsFloat() : d; }
int Severity(const std::string& s) {
    std::string l;
    for (char c : s) l += (char)std::tolower((unsigned char)c);
    if (l == "error" || l == "0") return 0;
    if (l == "info" || l == "hint" || l == "2") return 2;
    return 1;
}
} // namespace

void RegisterScriptEditorApi(okay::OkayScriptVM& vm, pluginreg::PluginRegistry& reg, const std::string& owner, const std::string& name) {
    okay::OkayScriptVM* vmp = &vm;
    pluginreg::PluginRegistry* r = &reg;
    reg.SetOwnerName(owner, name);
    auto call = [vmp](const std::string& fn, const std::vector<V>& args) {
        std::string err;
        return vmp->CallEventArgs(fn, args, &err);
    };

    // ---- registration ----
    vm.AddBuiltin("script_command", [r, owner, call](std::vector<V>& a) {
        pluginreg::Command c;
        c.id = Str(a, 0); c.title = Str(a, 1); c.shortcut = Str(a, 2); c.owner = owner;
        c.category = r->OwnerName(owner);
        const std::string fn = Str(a, 3).empty() ? c.id : Str(a, 3);
        c.run = [call, fn] { call(fn, {}); };
        return V{r->AddCommand(std::move(c)) ? 1.0f : 0.0f};
    });
    vm.AddBuiltin("script_lint", [r, owner, call](std::vector<V>& a) {
        const std::string fn = Str(a, 0), lang = Str(a, 1);
        r->AddLint(owner, lang, [call, fn](const pluginreg::DocContext& ctx) {
            std::vector<scriptide::Diagnostic> out;
            std::vector<scriptide::Diagnostic>* prev = g_problems;
            g_problems = &out;
            call(fn, {V{ctx.path}, V{ctx.text}, V{ctx.language}});
            g_problems = prev;
            return out;
        });
        return V{};
    });
    vm.AddBuiltin("script_completion", [r, owner, call](std::vector<V>& a) {
        const std::string fn = Str(a, 0), lang = Str(a, 1);
        r->AddCompletion(owner, lang, [call, fn](const pluginreg::DocContext& ctx) {
            std::vector<pluginreg::CompletionItem> out;
            std::vector<pluginreg::CompletionItem>* prev = g_suggestions;
            g_suggestions = &out;
            call(fn, {V{ctx.word}, V{ctx.language}});
            g_suggestions = prev;
            return out;
        });
        return V{};
    });
    vm.AddBuiltin("script_formatter", [r, owner, call](std::vector<V>& a) {
        const std::string fn = Str(a, 0), lang = Str(a, 1);
        r->AddFormatter(owner, lang, [call, fn](const pluginreg::DocContext& ctx, std::string& out) {
            const std::string t = call(fn, {V{ctx.text}, V{ctx.language}}).AsString();
            if (t.empty()) return false;
            out = t;
            return true;
        });
        return V{};
    });
    vm.AddBuiltin("script_hover", [r, owner, call](std::vector<V>& a) {
        const std::string fn = Str(a, 0), lang = Str(a, 1);
        r->AddHover(owner, lang, [call, fn](const pluginreg::DocContext& ctx) { return call(fn, {V{ctx.word}, V{ctx.language}}).AsString(); });
        return V{};
    });
    vm.AddBuiltin("script_snippet", [r, owner](std::vector<V>& a) {
        r->AddSnippet({Str(a, 0), Str(a, 3), Str(a, 1), Str(a, 2), owner});
        return V{};
    });
    vm.AddBuiltin("script_panel", [r, owner, call](std::vector<V>& a) {
        const std::string title = Str(a, 0), fn = Str(a, 1);
        r->AddPanel({owner + "#" + title, title, owner, [call, fn] { call(fn, {}); }});
        return V{};
    });

    // ---- results of a lint or completion function ----
    vm.AddBuiltin("script_problem", [](std::vector<V>& a) {
        if (!g_problems) return V{0.0f};
        scriptide::Diagnostic d;
        d.line = Int(a, 0); d.col = Int(a, 1); d.severity = Severity(Str(a, 2)); d.message = Str(a, 3);
        g_problems->push_back(std::move(d));
        return V{1.0f};
    });
    vm.AddBuiltin("script_fix", [](std::vector<V>& a) {   // (title, line, col, length, text): attaches to the last problem
        if (!g_problems || g_problems->empty()) return V{0.0f};
        scriptide::QuickFix f;
        f.title = Str(a, 0);
        f.edits.push_back({Int(a, 1, 1), Int(a, 2), Int(a, 3), Str(a, 4)});
        g_problems->back().fixes.push_back(std::move(f));
        return V{1.0f};
    });
    vm.AddBuiltin("script_suggest", [](std::vector<V>& a) {
        if (!g_suggestions) return V{0.0f};
        g_suggestions->push_back({Str(a, 0), Str(a, 2), Str(a, 1), ""});
        return V{1.0f};
    });

    // ---- the open script ----
    vm.AddBuiltin("script_text", [](std::vector<V>&) { auto& h = ScriptEditorHostApi(); return V{h.text ? h.text() : std::string()}; });
    vm.AddBuiltin("script_path", [](std::vector<V>&) { auto& h = ScriptEditorHostApi(); return V{h.path ? h.path() : std::string()}; });
    vm.AddBuiltin("script_language", [](std::vector<V>&) { auto& h = ScriptEditorHostApi(); return V{h.language ? h.language() : std::string()}; });
    vm.AddBuiltin("script_caret_line", [](std::vector<V>&) { auto& h = ScriptEditorHostApi(); return V{h.caretLine ? (float)h.caretLine() : 0.0f}; });
    vm.AddBuiltin("script_insert", [](std::vector<V>& a) { auto& h = ScriptEditorHostApi(); if (h.insert) h.insert(Str(a, 0)); return V{}; });
    vm.AddBuiltin("script_set_text", [](std::vector<V>& a) { auto& h = ScriptEditorHostApi(); if (h.setText) h.setText(Str(a, 0)); return V{}; });
    vm.AddBuiltin("script_open", [](std::vector<V>& a) { auto& h = ScriptEditorHostApi(); if (h.open) h.open(Str(a, 0), Int(a, 1, 1)); return V{}; });
}

} // namespace oked
