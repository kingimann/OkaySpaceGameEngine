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
/// (older, or written by hand) are read as plain text. A UTF-8 byte order mark at the start
/// of a file (some editors add one) is skipped.
///
/// Main thread only: the cache behind Cached() is not locked.
class DataAsset {
public:
    using Field = std::pair<std::string, std::string>;

    const std::vector<Field>& Fields() const { return m_fields; }
    std::vector<Field>&       Fields()       { return m_fields; }

    bool        Has(const std::string& key) const;
    std::string GetString(const std::string& key, const std::string& def = "") const;
    /// The value read as a number (the leading number of the text; text without one reads 0, as
    /// before). A missing key, and a value that is infinite or too big for a double ("inf", "1e999"),
    /// give `def`; NaN never comes out.
    double      GetNumber(const std::string& key, double def = 0.0) const;
    /// `GetNumber` rounded toward zero and clamped to the int range.
    int         GetInt(const std::string& key, int def = 0) const;
    /// true/false, yes/no, on/off, 1/0 in any case; a missing key or any other text gives `def`.
    bool        GetBool(const std::string& key, bool def = false) const;
    /// Strict: true (and `out` set) only when the key exists and its whole text is one finite number,
    /// so "12abc", "" and "nan" are refused. Use it where a typo in a data file should be noticed.
    bool        TryGetNumber(const std::string& key, double& out) const;
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
    /// Forget one cached asset: the next Cached() reads the file again. A reference an earlier
    /// Cached() call returned stays valid (it is emptied, not freed) and fills again on that next call.
    static void Invalidate(const std::string& path);
    static void InvalidateAll();   ///< Invalidate every cached path

private:
    std::vector<Field> m_fields;
    bool m_loaded = false;      // set by the cache once a file was read into it
};

} // namespace okay
