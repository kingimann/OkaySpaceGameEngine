#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Render/Color.hpp"
#include <functional>
#include <string>
#include <unordered_map>

namespace okay {

/// Persistent key/value storage, like PlayerPrefs — the easy way to save
/// high scores, settings, and progress. Values live in memory until Save() writes
/// them to a small text file that Load() reads back. Numbers are stored as
/// strings, so a key can be read as int, float, or string interchangeably.
/// Every call may be made from any thread (one lock guards the keys; listeners run
/// outside it, on the thread that made the change).
class Prefs {
public:
    static void  SetInt(const std::string& key, int value);
    static void  SetFloat(const std::string& key, float value);
    static void  SetString(const std::string& key, const std::string& value);

    static int          GetInt(const std::string& key, int def = 0);
    static float        GetFloat(const std::string& key, float def = 0.0f);
    static std::string  GetString(const std::string& key, const std::string& def = "");

    // ---- Typed values ----
    // Stored as text ("1" / "0" for bool, "x,y,z" for Vec3, "r,g,b,a" for Color), so
    // files stay readable by older builds and a key reads as any type it parses as.
    // A missing or unparsable key gives `def`.
    static void  SetBool(const std::string& key, bool value);
    static bool  GetBool(const std::string& key, bool def = false);   ///< "1", "true", "yes", "on" / "0", "false", "no", "off" (any case); other numbers are true when non-zero
    static void  SetVec3(const std::string& key, const Vec3& value);
    static Vec3  GetVec3(const std::string& key, const Vec3& def = Vec3());
    static void  SetColor(const std::string& key, const Color& value);
    static Color GetColor(const std::string& key, const Color& def = Color());

    static bool Has(const std::string& key);
    static void Delete(const std::string& key);
    static void Clear();
    /// Same as Has / Delete / Clear, under the names settings code looks for.
    static bool HasKey(const std::string& key) { return Has(key); }
    static void DeleteKey(const std::string& key) { Delete(key); }
    static void DeleteAll() { Clear(); }

    // ---- Change notification ----
    /// Called with the key after a Set* changes its value (a Set to the value it
    /// already has is silent), after Delete / DeleteKey removes it, and with each key
    /// Clear / DeleteAll removes. Load calls it with "" (everything may have changed).
    using Listener = std::function<void(const std::string& key)>;
    static int  AddListener(Listener fn);   ///< Returns an id for RemoveListener.
    static void RemoveListener(int id);

    /// Write all keys to `path` (in key order). The file is written aside, flushed to the
    /// disk and swapped in, so a crash or full disk leaves the old file alone; the old file,
    /// when it was a good one, is kept as `<path>.bak`. False if it couldn't be written
    /// (`LastError()` says why: disk full, read-only folder...).
    static bool Save(const std::string& path);
    /// Replace in-memory keys with the contents of `path`. A missing file gives an
    /// empty store and returns false (no file was read). A file that is cut off, damaged or
    /// unreadable falls back to `<path>.bak` (true; `LastLoadUsedBackup()` and `LastError()`
    /// say so); with no good backup it returns false and leaves the current keys untouched.
    /// Files saved on Windows with CR LF line ends load too.
    static bool Load(const std::string& path);
    /// Why the last Save or Load failed or fell back to a backup ("" when it went fine).
    static std::string LastError();
    /// True when the last Load read `<path>.bak` because the main file was damaged.
    static bool LastLoadUsedBackup();

private:
    static std::unordered_map<std::string, std::string>& Store();
};

} // namespace okay
