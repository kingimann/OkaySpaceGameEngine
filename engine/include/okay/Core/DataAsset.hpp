#pragma once
#include <string>
#include <vector>
#include <utility>

namespace okay {

/// A "Scriptable Object" — a reusable data asset, saved as a small text file
/// (`.okaydata`), that holds named fields you author in the editor and read
/// from OkayScript. Use it for item/enemy/level definitions and game config so
/// data lives outside code. Fields are ordered
/// key/value pairs stored as strings, so a key reads as number or string.
///
/// File format (one field per line):  `key = value`. Save writes `# okaydata 2`
/// first and escapes \\, newline, tab and '=' in keys; files without that line
/// (older, or written by hand) are read as plain text.
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

    /// Written aside and swapped in, so a crash or full disk keeps the old file.
    bool Save(const std::string& path) const;
    /// False (fields untouched) if the file can't be opened.
    bool Load(const std::string& path);

    /// Base directory prepended to relative paths when a bare path isn't found
    /// (the windowed runtime sets this to where the game's files live).
    static void SetBaseDir(const std::string& dir);
    static const std::string& BaseDir();
    /// Load (and cache) the asset at `path`; repeated calls reuse the instance. A
    /// file that can't be opened is not remembered as empty: you get an empty asset
    /// to Set() into, and while it stays untouched each call tries the file again.
    static DataAsset& Cached(const std::string& path);
    /// Re-read `path` into the cache (an edited or newly created file). False, with
    /// the cached copy kept, if it can't be read.
    static bool Reload(const std::string& path);
    static void Invalidate(const std::string& path);   ///< forget one cached asset
    static void InvalidateAll();

private:
    std::vector<Field> m_fields;
    bool m_loaded = false;      // set by the cache once a file was read into it
};

} // namespace okay
