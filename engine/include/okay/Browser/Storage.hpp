#pragma once
// Browser (page engine), round 3: Web Storage (localStorage and sessionStorage), written from the HTML standard section 12
// ("Web storage": the Storage interface, 12.2.1; the per-origin storage areas, 12.2.2-12.2.3). One WebStorage holds the
// areas of many origins: the browser profile keeps one for localStorage (saved with ToJson in the profile folder) and each
// tab keeps its own for sessionStorage (gone with the tab).
//
// Limits (our choice, close to other browsers): an origin may keep at most kMaxBytesPerOrigin bytes of keys and values
// (UTF-8); a setItem past that throws QuotaExceededError in the page and changes nothing. At most kMaxOrigins origins are
// kept; the one used longest ago goes first.
// Standard library and okay/Core/Json.hpp only; no files (the host saves ToJson()).
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace okay::browser {

class StorageArea {
public:
    std::size_t Length() const { return m_keys.size(); }
    /// The n-th key (in the order the keys were added), or false.
    bool Key(std::size_t n, std::string& out) const;
    bool Get(const std::string& key, std::string& out) const;
    /// False (nothing changed) when the origin's quota would be passed.
    bool Set(const std::string& key, const std::string& value, std::size_t quota);
    bool Remove(const std::string& key);
    void Clear();
    std::size_t Bytes() const { return m_bytes; }
    const std::vector<std::string>& Keys() const { return m_keys; }
    std::int64_t lastUse = 0;   ///< a counter: the area used longest ago is dropped first when there are too many

private:
    std::map<std::string, std::string> m_values;
    std::vector<std::string> m_keys;   // in the order they were added
    std::size_t m_bytes = 0;
};

class WebStorage {
public:
    static constexpr std::size_t kMaxBytesPerOrigin = 5u << 20;
    static constexpr std::size_t kMaxOrigins = 1000;

    /// The area of `origin` ("https://site.test"; OriginOf in Url.hpp), made when it is missing.
    StorageArea& Area(const std::string& origin);
    const StorageArea* Find(const std::string& origin) const;
    void ClearOrigin(const std::string& origin);
    std::size_t Origins() const { return m_areas.size(); }
    std::size_t quota = kMaxBytesPerOrigin;

    /// Set by a change through a page; the host saves ToJson() and clears it.
    bool TakeChanged() { const bool c = m_changed; m_changed = false; return c; }
    void MarkChanged() { m_changed = true; }

    std::string ToJson() const;
    /// Read what ToJson wrote; false when the text is not ours (nothing is changed then).
    bool FromJson(const std::string& text);

private:
    std::map<std::string, StorageArea> m_areas;
    std::int64_t m_use = 0;
    bool m_changed = false;
};

} // namespace okay::browser
