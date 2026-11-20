// See AddComponentRegistry.hpp.
#include "AddComponentRegistry.hpp"

#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/PropertyRegistry.hpp"

#include <algorithm>
#include <map>
#include <typeindex>

namespace oked {
namespace addcomp {

std::vector<RegItem> RegistryItems(const okay::GameObject& go) {
    std::vector<RegItem> out;
    for (const auto& info : okay::PropertyRegistry::All()) {
        if (!info || !info->add || info->objectLevel || info->name == "Transform") continue;
        RegItem r;
        r.info = info;
        r.label = PrettyTypeName(info->name);
        r.nkey = Canon(KeyOf(info->name));
        for (const auto& c : go.Components()) if (c && std::type_index(typeid(*c)) == info->type) { r.present = true; break; }
        out.push_back(std::move(r));
    }
    return out;
}

bool AddRegistered(okay::GameObject& go, const okay::ComponentTypeInfo& info) { return info.add && info.add(go) != nullptr; }

std::vector<Entry> EntriesFromRegistry(const okay::GameObject& go, std::vector<std::shared_ptr<const okay::ComponentTypeInfo>>* infos) {
    std::vector<Entry> out;
    if (infos) infos->clear();
    for (const RegItem& r : RegistryItems(go)) {
        Entry e = MakeEntry(r.info->category, r.label, !r.present);
        out.push_back(std::move(e));
        if (infos) infos->push_back(r.info);
    }
    ApplyPrereqs(out);
    return out;
}

std::string PreviewText(const Entry& e) {
    std::string t = e.name + "\n" + e.description;
    std::vector<std::string> also = AlsoAdds(e.name);
    std::shared_ptr<const okay::ComponentTypeInfo> info;
    for (const auto& i : okay::PropertyRegistry::All())
        if (i && i->add && !i->objectLevel && Canon(KeyOf(i->name)) == e.nkey) { info = i; break; }
    if (info) {
        for (const auto& rq : okay::ComponentRules::RequirementsOf(info->type)) {
            const std::string nm = PrettyTypeNameFromTypeInfo(rq.type.name());
            if (std::find(also.begin(), also.end(), nm) == also.end()) also.push_back(nm);
        }
    }
    if (!also.empty()) { t += "\nAlso adds: "; for (std::size_t i = 0; i < also.size(); ++i) t += (i ? ", " : "") + also[i]; t += "."; }
    if (info) {
        std::vector<std::string> names;
        for (const auto& p : info->properties) if (p.name != "enabled") names.push_back(p.name);
        if (!names.empty()) {
            t += "\nYou can change: ";
            const std::size_t show = std::min<std::size_t>(names.size(), 6);
            for (std::size_t i = 0; i < show; ++i) t += (i ? ", " : "") + names[i];
            if (names.size() > show) t += " and " + std::to_string(names.size() - show) + " more";
            t += ".";
        }
    }
    if (!e.available) t += "\n" + e.reason;
    return t;
}

}  // namespace addcomp
}  // namespace oked
