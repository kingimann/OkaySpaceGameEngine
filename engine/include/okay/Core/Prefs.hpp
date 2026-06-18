#pragma once
#include <string>
#include <unordered_map>

namespace okay {

/// Persistent key/value storage, like Unity's PlayerPrefs — the easy way to save
/// high scores, settings, and progress. Values live in memory until Save() writes
/// them to a small text file that Load() reads back. Numbers are stored as
/// strings, so a key can be read as int, float, or string interchangeably.
class Prefs {
public:
    static void  SetInt(const std::string& key, int value);
    static void  SetFloat(const std::string& key, float value);
    static void  SetString(const std::string& key, const std::string& value);

    static int          GetInt(const std::string& key, int def = 0);
    static float        GetFloat(const std::string& key, float def = 0.0f);
    static std::string  GetString(const std::string& key, const std::string& def = "");

    static bool Has(const std::string& key);
    static void Delete(const std::string& key);
    static void Clear();

    /// Write all keys to `path`. Returns false if the file can't be opened.
    static bool Save(const std::string& path);
    /// Replace in-memory keys with the contents of `path`. Missing file = empty.
    static bool Load(const std::string& path);

private:
    static std::unordered_map<std::string, std::string>& Store();
};

} // namespace okay
