#pragma once
#include <string>
#include <vector>
#include <utility>

namespace okay {

/// A "Scriptable Object" — a reusable data asset, saved as a small text file
/// (`.okaydata`), that holds named fields you author in the editor and read
/// from OkayScript. Use it for item/enemy/level definitions and game config so
/// data lives outside code (Unity's ScriptableObject idea). Fields are ordered
/// key/value pairs stored as strings, so a key reads as number or string.
///
/// File format (one field per line):  `key = value`
class DataAsset {
public:
    using Field = std::pair<std::string, std::string>;

    const std::vector<Field>& Fields() const { return m_fields; }
    std::vector<Field>&       Fields()       { return m_fields; }

    bool        Has(const std::string& key) const;
    std::string GetString(const std::string& key, const std::string& def = "") const;
    double      GetNumber(const std::string& key, double def = 0.0) const;
    void        Set(const std::string& key, const std::string& value);  // add or replace
    void        Remove(const std::string& key);

    bool Save(const std::string& path) const;
    bool Load(const std::string& path);

    /// Base directory prepended to relative paths when a bare path isn't found
    /// (the windowed runtime sets this to where the game's files live).
    static void SetBaseDir(const std::string& dir);
    /// Load (and cache) the asset at `path`; repeated calls reuse the instance.
    static DataAsset& Cached(const std::string& path);

private:
    std::vector<Field> m_fields;
};

} // namespace okay
