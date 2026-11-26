// Browser (page engine), round 3: Web Storage areas (see Storage.hpp), written from the HTML standard section 12.2 (the
// Storage interface's length, key(), getItem(), setItem() with its quota check, removeItem() and clear()).
#include "okay/Browser/Storage.hpp"

#include "okay/Core/Json.hpp"

#include <algorithm>

namespace okay::browser {

bool StorageArea::Key(std::size_t n, std::string& out) const {
    if (n >= m_keys.size()) return false;
    out = m_keys[n];
    return true;
}

bool StorageArea::Get(const std::string& key, std::string& out) const {
    auto it = m_values.find(key);
    if (it == m_values.end()) return false;
    out = it->second;
    return true;
}

bool StorageArea::Set(const std::string& key, const std::string& value, std::size_t quota) {
    auto it = m_values.find(key);
    const std::size_t old = it == m_values.end() ? 0 : key.size() + it->second.size();
    const std::size_t add = key.size() + value.size();
    if (add > quota || m_bytes - old > quota - add) return false;   // checked before anything changes
    m_bytes = m_bytes - old + add;
    if (it == m_values.end()) {
        m_values.emplace(key, value);
        m_keys.push_back(key);
    } else {
        it->second = value;
    }
    return true;
}

bool StorageArea::Remove(const std::string& key) {
    auto it = m_values.find(key);
    if (it == m_values.end()) return false;
    m_bytes -= std::min(m_bytes, key.size() + it->second.size());
    m_values.erase(it);
    m_keys.erase(std::find(m_keys.begin(), m_keys.end(), key));
    return true;
}

void StorageArea::Clear() {
    m_values.clear();
    m_keys.clear();
    m_bytes = 0;
}

StorageArea& WebStorage::Area(const std::string& origin) {
    auto it = m_areas.find(origin);
    if (it == m_areas.end()) {
        while (m_areas.size() >= kMaxOrigins) {   // the area used longest ago goes
            auto oldest = m_areas.begin();
            for (auto a = m_areas.begin(); a != m_areas.end(); ++a) if (a->second.lastUse < oldest->second.lastUse) oldest = a;
            m_areas.erase(oldest);
            m_changed = true;
        }
        it = m_areas.emplace(origin, StorageArea()).first;
    }
    it->second.lastUse = ++m_use;
    return it->second;
}

const StorageArea* WebStorage::Find(const std::string& origin) const {
    auto it = m_areas.find(origin);
    return it == m_areas.end() ? nullptr : &it->second;
}

void WebStorage::ClearOrigin(const std::string& origin) {
    if (m_areas.erase(origin)) m_changed = true;
}

std::string WebStorage::ToJson() const {
    using okay::json::Escape;
    std::string o = "{\"version\": 1, \"origins\": {";
    bool firstArea = true;
    for (const auto& a : m_areas) {
        if (a.second.Length() == 0) continue;
        o += firstArea ? "\n  " : ",\n  ";
        firstArea = false;
        Escape(a.first, o);
        o += ": [";
        bool first = true;
        for (const std::string& k : a.second.Keys()) {
            std::string v;
            a.second.Get(k, v);
            o += first ? "[" : ", [";
            first = false;
            Escape(k, o);
            o += ", ";
            Escape(v, o);
            o += "]";
        }
        o += "]";
    }
    o += "}}\n";
    return o;
}

bool WebStorage::FromJson(const std::string& text) {
    okay::json::Value v;
    if (!okay::json::Parse(text, v) || !v.IsObject()) return false;
    const okay::json::Value* origins = v.Get("origins");
    if (!origins || !origins->IsObject()) return false;
    std::map<std::string, StorageArea> areas;
    for (const auto& kv : origins->object) {
        if (!kv.second.IsArray() || areas.size() >= kMaxOrigins) continue;
        StorageArea& a = areas[kv.first];
        for (const okay::json::Value& pair : kv.second.array)
            if (pair.IsArray() && pair.array.size() == 2 && pair.array[0].IsString() && pair.array[1].IsString())
                a.Set(pair.array[0].string, pair.array[1].string, quota);
    }
    m_areas.swap(areas);
    m_changed = false;
    return true;
}

} // namespace okay::browser
