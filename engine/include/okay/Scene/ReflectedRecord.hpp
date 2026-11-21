#pragma once
// Scene-file records written from a component's reflected Inspector rows, so a new row is saved and loaded with no
// extra code:   <record> name="text" name="text" ...   (every value is its exact text form, quoted; unknown names are skipped).
// Written from this engine's PropertyRegistry / ComponentSerializerRegistry only.
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/PropertyBuilder.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include <functional>
#include <sstream>

namespace okay {
namespace reflected {

inline std::string QuoteValue(const std::string& v) {
    std::string o = "\"";
    for (char c : v) { if (c == '\\' || c == '"') o += '\\'; o += (c == '\n' ? ' ' : c); }
    return o + "\"";
}

/// Parse `key="value" key="value"` (values may hold escaped quotes) into pairs.
inline std::vector<std::pair<std::string, std::string>> ParseRecord(std::istream& in) {
    std::string line; std::getline(in, line);
    std::vector<std::pair<std::string, std::string>> kv;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace((unsigned char)line[i])) ++i;
        std::size_t k0 = i;
        while (i < line.size() && line[i] != '=' && !std::isspace((unsigned char)line[i])) ++i;
        if (i >= line.size() || line[i] != '=') break;
        std::string key = line.substr(k0, i - k0); ++i;
        std::string val;
        if (i < line.size() && line[i] == '"') {
            ++i;
            while (i < line.size() && line[i] != '"') { if (line[i] == '\\' && i + 1 < line.size()) ++i; val += line[i++]; }
            if (i < line.size()) ++i;
        } else { while (i < line.size() && !std::isspace((unsigned char)line[i])) val += line[i++]; }
        kv.emplace_back(std::move(key), std::move(val));
    }
    return kv;
}

template <class T>
inline void RegisterReflectedRecord(const char* typeName, const char* record) {
    ComponentSerializerRegistry::Entry e;
    e.name = record;
    const std::string tn = typeName;
    e.write = [tn, record](const GameObject& go, std::ostream& out) {
        auto info = PropertyRegistry::FindByName(tn);
        if (!info) return;
        for (const T* c : go.GetComponents<T>()) {
            out << "  " << record;
            for (const PropertyDesc& d : info->properties) {
                if (d.IsRuntimeOnly() || d.IsDerived()) continue;
                PropertyValue v = d.Get(*c);
                if (!v.IsValid()) continue;
                out << ' ' << d.name << '=' << QuoteValue(v.ToString());
            }
            out << "\n";
        }
    };
    e.read = [tn](GameObject& go, std::istream& in) {
        T* c = go.AddComponent<T>();
        auto info = PropertyRegistry::FindByName(tn);
        if (!c || !info) return;
        for (auto& kv : ParseRecord(in)) {
            const PropertyDesc* d = info->Find(kv.first);
            if (!d || d->IsReadOnly()) continue;
            PropertyValue v;
            if (d->ParseText(kv.second, v)) d->Set(*c, v);   // a bad value leaves the default
        }
    };
    ComponentSerializerRegistry::Register(std::move(e));
}


} // namespace reflected
} // namespace okay
