// The script library: what each typed script's Behaviour class offers other
// scripts (GetComponent<Health>().TakeDamage(10)). Every script registers itself
// when it loads, so a script can use any script of the scene, whatever order they
// load in: a script that names one that hasn't loaded yet is checked again when
// the scene starts (NextScriptVM::Bind).
#include "ScriptInternal.hpp"

namespace okay::script {

namespace {
std::unordered_map<std::string, std::shared_ptr<const ScriptView>>& Views() {
    static std::unordered_map<std::string, std::shared_ptr<const ScriptView>> views;
    return views;
}
std::vector<std::string>& ClassNames() {
    static std::vector<std::string> names;
    return names;
}
} // namespace

void RegisterScriptSource(const std::string& source) {
    Program p;
    std::vector<ScriptDiagnostic> ignored;
    Parse(Lex(source, ignored), p);
    for (const ClassDecl& c : p.classes) {
        if (c.bases.empty() || c.bases[0] != "Behaviour") continue;
        auto v = std::make_shared<ScriptView>();
        v->name = c.name;
        for (const FieldDecl& f : c.fields)
            if (f.isPublic && !f.isStatic && !f.isConst && !f.isEvent) v->fields.push_back({f.name, f.type, f.loc});
        for (const MethodDecl& m : c.methods) {
            if (!m.isPublic || m.isStatic || m.isCtor) continue;
            ScriptView::Method sm;
            sm.name = m.name;
            sm.ret = m.ret;
            for (const Param& prm : m.params) sm.params.push_back(prm.type);
            v->methods.push_back(std::move(sm));
        }
        ScriptClassId(c.name);
        Views()[c.name] = std::move(v);
        return;   // one Behaviour class per script
    }
}

std::shared_ptr<const ScriptView> FindScriptView(const std::string& name) {
    auto it = Views().find(name);
    return it == Views().end() ? nullptr : it->second;
}

int ScriptClassId(const std::string& name) {
    auto& names = ClassNames();
    for (std::size_t i = 0; i < names.size(); ++i) if (names[i] == name) return (int)i;
    names.push_back(name);
    return (int)names.size() - 1;
}

const std::string& ScriptClassName(int id) {
    static const std::string none;
    const auto& names = ClassNames();
    return id >= 0 && id < (int)names.size() ? names[(std::size_t)id] : none;
}

} // namespace okay::script
