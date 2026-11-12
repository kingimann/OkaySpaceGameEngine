#pragma once
// Save game framework: a layer on top of SaveData.hpp / SaveGame.hpp.
//
//   SaveBlock / SaveDocument   typed key/value blocks a game fills with OnSave
//   SaveStore                  slot files: header metadata, versioning + migrations,
//                              backups, listing, copy / rename / delete, Verify
//   Saveable, SaveSystem       per-object state captured from a Scene and put back
//   SaveManager                quick save, an autosave ring, Continue
//   CheckpointManager/Trigger  named checkpoints
//   ISaveSync + SaveSyncer     cloud / sync hooks (no network code here)
//   SaveMenuModel              rows a load/save menu can bind to (no rendering)
//
// Slots written here are files named <slot>.okayslot in the save folder
// (Save::BaseDir()/saves by default), next to SaveGame's <slot>.okaygame files; the
// two systems never touch each other's files. See docs/save_framework.md.
//
// Not thread safe: use it from the game's main thread.

#include "okay/Core/SaveData.hpp"
#include "okay/Core/SaveGame.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Scene/Component.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace okay {

class Scene;
class GameObject;

// =====================================================================
// 1. Typed field blocks
// =====================================================================

/// A flat bag of typed values (int, int64, float, bool, string, Vec3, Quat, string
/// list) keyed by name. Keys are kept sorted, so the same contents always give the
/// same bytes (and the same checksum).
class SaveBlock {
public:
    void SetInt(const std::string& key, int v);
    void SetInt64(const std::string& key, long long v);
    void SetFloat(const std::string& key, float v);
    void SetBool(const std::string& key, bool v);
    void SetString(const std::string& key, const std::string& v);
    void SetVec3(const std::string& key, const Vec3& v);
    void SetQuat(const std::string& key, const Quat& v);
    /// Stored as `key.n` (the count) plus `key.0`, `key.1`, ...
    void SetStrings(const std::string& key, const std::vector<std::string>& v);

    /// A missing key, or one holding a different kind of value, returns `def`.
    /// Numbers convert between int, int64 and float.
    int         GetInt(const std::string& key, int def = 0) const;
    long long   GetInt64(const std::string& key, long long def = 0) const;
    float       GetFloat(const std::string& key, float def = 0.0f) const;
    bool        GetBool(const std::string& key, bool def = false) const;
    std::string GetString(const std::string& key, const std::string& def = "") const;
    Vec3        GetVec3(const std::string& key, const Vec3& def = Vec3::Zero) const;
    Quat        GetQuat(const std::string& key, const Quat& def = Quat()) const;
    std::vector<std::string> GetStrings(const std::string& key) const;

    bool Has(const std::string& key) const { return m_values.find(key) != m_values.end(); }
    /// True if any key starts with `prefix`.
    bool HasPrefix(const std::string& prefix) const;
    bool Erase(const std::string& key) { return m_values.erase(key) > 0; }
    void Clear() { m_values.clear(); }
    bool Empty() const { return m_values.empty(); }
    std::size_t Count() const { return m_values.size(); }
    std::vector<std::string> Keys() const;
    /// The raw `type:payload` text per key (tools, migrations).
    const std::map<std::string, std::string>& Raw() const { return m_values; }
    void SetRaw(const std::string& key, const std::string& typedValue) { m_values[key] = typedValue; }

    bool operator==(const SaveBlock& o) const { return m_values == o.m_values; }
    bool operator!=(const SaveBlock& o) const { return !(*this == o); }

private:
    std::map<std::string, std::string> m_values;
    bool Payload(const std::string& key, char& type, std::string& payload) const;
};

/// Write side of a block, handed to Saveable::OnSave. `Scope("inv")` gives a writer
/// whose keys all start with `inv.`, so a component can nest its fields.
/// Each call writes one named field; read it back with the SaveReader method of
/// the same name.
class SaveWriter {
public:
    explicit SaveWriter(SaveBlock& block, std::string prefix = std::string())
        : m_block(&block), m_prefix(std::move(prefix)) {}
    void Int(const std::string& key, int v)                       { m_block->SetInt(m_prefix + key, v); }
    void Int64(const std::string& key, long long v)               { m_block->SetInt64(m_prefix + key, v); }
    void Float(const std::string& key, float v)                   { m_block->SetFloat(m_prefix + key, v); }
    void Bool(const std::string& key, bool v)                     { m_block->SetBool(m_prefix + key, v); }
    void String(const std::string& key, const std::string& v)     { m_block->SetString(m_prefix + key, v); }
    void Vector3(const std::string& key, const Vec3& v)           { m_block->SetVec3(m_prefix + key, v); }
    void Rotation(const std::string& key, const Quat& v)          { m_block->SetQuat(m_prefix + key, v); }
    void Strings(const std::string& key, const std::vector<std::string>& v) { m_block->SetStrings(m_prefix + key, v); }
    bool Has(const std::string& key) const { return m_block->Has(m_prefix + key); }
    SaveWriter Scope(const std::string& name) const { return SaveWriter(*m_block, m_prefix + name + "."); }

private:
    SaveBlock* m_block;
    std::string m_prefix;
};

/// Read side of a block, handed to Saveable::OnLoad. Every getter takes the value to
/// use when the field is missing, so `hp = r.Float("hp", hp);` leaves a field alone
/// that an older save never wrote.
class SaveReader {
public:
    explicit SaveReader(const SaveBlock& block, std::string prefix = std::string())
        : m_block(&block), m_prefix(std::move(prefix)) {}
    int         Int(const std::string& key, int def = 0) const                  { return m_block->GetInt(m_prefix + key, def); }
    long long   Int64(const std::string& key, long long def = 0) const          { return m_block->GetInt64(m_prefix + key, def); }
    float       Float(const std::string& key, float def = 0.0f) const           { return m_block->GetFloat(m_prefix + key, def); }
    bool        Bool(const std::string& key, bool def = false) const            { return m_block->GetBool(m_prefix + key, def); }
    std::string String(const std::string& key, const std::string& def = "") const { return m_block->GetString(m_prefix + key, def); }
    Vec3        Vector3(const std::string& key, const Vec3& def = Vec3::Zero) const { return m_block->GetVec3(m_prefix + key, def); }
    Quat        Rotation(const std::string& key, const Quat& def = Quat()) const { return m_block->GetQuat(m_prefix + key, def); }
    std::vector<std::string> Strings(const std::string& key) const              { return m_block->GetStrings(m_prefix + key); }
    bool Has(const std::string& key) const { return m_block->Has(m_prefix + key); }
    /// True if anything was written under this reader's scope.
    bool HasAny() const { return m_block->HasPrefix(m_prefix); }
    SaveReader Scope(const std::string& name) const { return SaveReader(*m_block, m_prefix + name + "."); }

private:
    const SaveBlock* m_block;
    std::string m_prefix;
};

/// What a slot holds: named blocks. SaveSystem::CaptureScene fills the reserved
/// "scene" and "obj.<n>" blocks; a game adds its own ("game", "profile", ...).
class SaveDocument {
public:
    /// The block called `name`, created if it isn't there yet.
    SaveBlock& Block(const std::string& name) { return m_blocks[name]; }
    const SaveBlock* Find(const std::string& name) const {
        auto it = m_blocks.find(name);
        return it == m_blocks.end() ? nullptr : &it->second;
    }
    bool Has(const std::string& name) const { return m_blocks.find(name) != m_blocks.end(); }
    bool Remove(const std::string& name) { return m_blocks.erase(name) > 0; }
    void Clear() { m_blocks.clear(); }
    bool Empty() const { return m_blocks.empty(); }
    std::size_t Count() const { return m_blocks.size(); }
    std::vector<std::string> Names() const;
    SaveWriter Writer(const std::string& name) { return SaveWriter(Block(name)); }
    /// A reader over `name`; an empty block when it doesn't exist.
    SaveReader Reader(const std::string& name) const;
    /// Copy every block of `other` in (replacing blocks of the same name).
    void Merge(const SaveDocument& other);

    /// The text a slot stores: one `@block<TAB>name<TAB>n` line per block followed
    /// by its n `key=type:value` lines. Deterministic.
    std::string Serialize() const;
    static bool Parse(const std::string& text, SaveDocument& out, std::string* error = nullptr);
    /// Debug check: serialising, parsing the text and serialising again gives the
    /// same blocks and the same text. `why` says what differed.
    bool RoundTrips(std::string* why = nullptr) const;

    bool operator==(const SaveDocument& o) const { return m_blocks == o.m_blocks; }
    bool operator!=(const SaveDocument& o) const { return !(*this == o); }

private:
    std::map<std::string, SaveBlock> m_blocks;
};

// =====================================================================
// 2. Slot files: SaveStore
// =====================================================================

/// What a slot is for (SaveSlotInfo::kind).
struct SlotKind {
    static constexpr const char* Manual = "manual";
    static constexpr const char* Quick = "quick";
    static constexpr const char* Auto = "auto";
    static constexpr const char* Checkpoint = "checkpoint";
};

enum class SlotSort { Newest, Oldest, Name, PlayTime };

/// What happened while a slot was read.
struct SaveLoadReport {
    bool usedBackup = false;     ///< the main file was damaged; the .bak was read instead
    bool migrated = false;       ///< the slot was older and went through the migration chain
    int  fromVersion = 0;        ///< the data version the file was written in
    int  toVersion = 0;          ///< the data version it was brought to
    std::vector<int> steps;      ///< the `from` of every migration step that ran
};

/// Slot files in one folder. A slot file is:
///   - a header block (name-free metadata: version, game version, time, play time,
///     scene, label, kind, thumbnail, custom fields, payload size and checksum, and a
///     checksum of the header itself), then
///   - the payload text of a SaveDocument.
/// The header is read without the payload, so a menu can list hundreds of big saves
/// cheaply. Files are written aside and swapped in, the previous good file is kept as
/// <slot>.okayslot.bak, a slot written by a newer build is never half loaded, and one
/// that is overwritten is first copied to <slot>.okayslot.newer.bak.
class SaveStore {
public:
    static constexpr int kContainerVersion = 1;
    static constexpr std::size_t kDefaultMaxBytes = 256u * 1024u * 1024u;
    static constexpr std::size_t kMaxHeaderBytes = 64u * 1024u;
    static constexpr const char* kExtension = ".okayslot";

    /// A step that rewrites a payload saved in `fromVersion` into the layout of the
    /// next version (false + message = can't).
    using Migration = std::function<bool(SaveDocument& doc, std::string* error)>;

    /// `directory` empty = Save::Resolve("saves"), looked up each time, so
    /// Save::SetBaseDir still decides where the default store writes.
    explicit SaveStore(std::string directory = std::string());
    /// The store the engine-wide helpers (SaveManager::Default) use.
    static SaveStore& Default();

    void SetDirectory(const std::string& directory) { m_dir = directory; }
    std::string Directory() const;
    std::string SlotPath(const std::string& slot) const;
    /// Letters, digits, '-' and '_' (at most 64).
    static bool ValidSlot(const std::string& slot);

    // ---- versioning ----
    /// The data version this build writes and reads up to (default 1). Raise it when
    /// the payload layout changes, and register a Migration for the old version.
    void SetDataVersion(int version) { m_dataVersion = version < 1 ? 1 : version; }
    int  DataVersion() const { return m_dataVersion; }
    /// Free text stored with every slot ("1.4.2").
    void SetGameVersion(const std::string& v) { m_gameVersion = v; }
    const std::string& GameVersion() const { return m_gameVersion; }
    /// Register the step from `fromVersion` to `fromVersion + 1`. Steps run in order,
    /// so a version 1 slot read by a version 4 build runs the steps from 1, 2 and 3.
    /// A version with no step is left as is (same layout). Empty function removes it.
    void RegisterMigration(int fromVersion, Migration fn);
    void ClearMigrations() { m_migrations.clear(); }
    /// The versions a slot written in `fromVersion` would pass through.
    std::vector<int> MigrationPath(int fromVersion) const;

    // ---- environment (tests and tools) ----
    /// Replace the clock (milliseconds since 1970) used for save times.
    void SetClock(std::function<long long()> nowMs) { m_clock = std::move(nowMs); }
    long long NowMs() const;
    void SetMaxBytes(std::size_t bytes) { m_maxBytes = bytes ? bytes : kDefaultMaxBytes; }
    /// Debug: after every Write, read the slot back and check it equals what was
    /// written (catches an OnSave that writes something that can't be read back).
    void SetVerifyAfterWrite(bool on) { m_verifyAfterWrite = on; }

    // ---- read / write ----
    /// Write `doc` to `slot` with `meta` (label, scene, playTime, thumbnail, kind and
    /// fields are used; the version and the time are set here).
    bool Write(const std::string& slot, const SaveDocument& doc, SaveSlotInfo meta, std::string* error = nullptr);
    /// Read `slot` into `out`, applying migrations. A damaged main file falls back to
    /// its .bak (the call still succeeds, `report->usedBackup` is set and `error` says
    /// so). A slot from a newer build is refused with a clear message and left alone.
    bool Read(const std::string& slot, SaveDocument& out, SaveSlotInfo* info = nullptr,
              std::string* error = nullptr, SaveLoadReport* report = nullptr) const;

    // ---- listing and info ----
    /// The header of one slot (cheap: the payload is not read). `deep` also reads the
    /// whole file and checks its checksum.
    SaveSlotInfo Info(const std::string& slot, bool deep = false) const;
    /// Every slot file in the folder with corrupt and incompatible ones flagged.
    std::vector<SaveSlotInfo> ListSlots(SlotSort sort = SlotSort::Newest, bool deep = false) const;
    bool Exists(const std::string& slot) const;
    /// The newest slot that is neither corrupt nor incompatible ("" if none). Reads
    /// each candidate whole, so a file that fails its checksum is skipped.
    std::string NewestValidSlot() const;

    // ---- manage ----
    bool Delete(const std::string& slot);
    /// Copy a slot (and its thumbnail file if it sits beside the slot as <slot>.<ext>).
    bool Copy(const std::string& from, const std::string& to, bool overwrite = false, std::string* error = nullptr);
    /// Rename the slot file (with its .bak files and thumbnail).
    bool Rename(const std::string& from, const std::string& to, bool overwrite = false, std::string* error = nullptr);
    /// Change a slot's label, thumbnail, kind or custom fields without touching its
    /// payload. `fn` edits a copy of the info; the other fields are ignored.
    bool UpdateInfo(const std::string& slot, const std::function<void(SaveSlotInfo&)>& fn, std::string* error = nullptr);
    bool SetLabel(const std::string& slot, const std::string& label, std::string* error = nullptr);

    // ---- verify ----
    /// Debug: read the main file whole, check its checksums, parse it, bring it to
    /// the current version, and check the payload round-trips (serialise, parse,
    /// serialise gives the same text). `report` says what was found.
    bool Verify(const std::string& slot, std::string* report = nullptr) const;

    // ---- raw bytes (sync) ----
    /// The slot's file bytes, if it is a readable slot (a damaged one is refused).
    bool ExportRaw(const std::string& slot, std::string& bytes, SaveSlotInfo* info = nullptr, std::string* error = nullptr) const;
    /// Install file bytes as `slot` after checking them; the previous good file
    /// becomes the .bak.
    bool ImportRaw(const std::string& slot, const std::string& bytes, bool overwrite, std::string* error = nullptr);
    /// Check the header of slot-file bytes (and the payload checksum if the whole
    /// file is there) without a store: for sync backends.
    static bool ParseHeader(const std::string& bytes, SaveSlotInfo& info, std::string* error = nullptr);
    /// The checksum sync compares: FNV-1a of the whole file, as 16 hex digits.
    static std::string Checksum(const std::string& bytes);

private:
    std::string m_dir;
    int m_dataVersion = 1;
    std::string m_gameVersion;
    std::map<int, Migration> m_migrations;
    std::function<long long()> m_clock;
    std::size_t m_maxBytes = kDefaultMaxBytes;
    bool m_verifyAfterWrite = false;

    bool Install(const std::string& slot, const std::string& bytes, std::string* error);
    bool Migrate(SaveDocument& doc, int from, SaveLoadReport* report, std::string* error) const;
};

// =====================================================================
// 3. Saveable objects
// =====================================================================

/// Anything that can write and read its own state. Saveable implements it, and so can
/// any other component (derive from both, or from Saveable itself).
class ISaveable {
public:
    virtual ~ISaveable() = default;
    /// The name this component's fields are stored under ("" = c0, c1, ... by its
    /// place among the object's ISaveable components). Keep it stable between builds.
    virtual std::string SaveName() const { return std::string(); }
    virtual void OnSave(SaveWriter& /*w*/) {}
    virtual void OnLoad(SaveReader& /*r*/) {}
    /// Called once after the whole scene has been restored (every object exists and
    /// has its state), for fixing up references between objects.
    virtual void OnLoadComplete() {}
};

/// Marks an object to be saved by SaveSystem::CaptureScene. Subclass it and override
/// OnSave / OnLoad, or set the `onSave` / `onLoad` lambdas for a quick one.
///
/// Identity: `saveId`, when set, is the object's key (it survives renames and moves
/// in the hierarchy). Without one, the key is the object's path in the scene
/// ("Root/Child", "Crate#1" for the second sibling called Crate), so renaming an
/// object breaks the link to its old save.
///
/// The scene file stores it (record "saveable"), so a level loaded from a file keeps its
/// Saveable components: the editor's Add Component > Save > Saveable, or code. For
/// objects without one, SaveApplyOptions::prepare can attach them after a load, or mark
/// objects with a tag and save with SaveCaptureOptions::includeTag.
class Saveable : public Behaviour, public ISaveable {
public:
    std::string saveId;           ///< stable key (empty = the scene path; dynamic objects get one)
    bool dynamic = false;         ///< spawned while playing: respawned from `prefab` on load
    std::string prefab;           ///< .okayprefab path, or "factory:Name" (empty = the object's prefabSource)
    bool saveTransform = true;    ///< local position / rotation / scale
    bool saveActive = true;
    bool saveComponents = true;   ///< the built-in adapters: Rigidbody2D/3D, Inventory, Stats
    /// Property-registry paths to store too ("Light.intensity", "Health.current"): any
    /// registered component property, so a no-code user picks fields without writing an
    /// OnSave. A path that does not resolve is reported (capture) and skipped (load).
    std::vector<std::string> properties;
    /// Also store this object's script variables (text scripts' globals) and the
    /// variables of its visual scripts.
    bool saveVariables = false;
    /// Also store the scene-wide variables once per save: Action List numbers and text,
    /// the visual scripts' scene variables and their global variables. Tick it on one
    /// object per scene (a manager object); it is written once however many ask.
    bool saveSceneVariables = false;
    std::function<void(SaveWriter&)> onSave;
    std::function<void(SaveReader&)> onLoad;

    std::string SaveName() const override { return "saveable"; }
    /// Notes this object as part of the level (so a later save can record it as destroyed).
    void Start() override;
    void OnSave(SaveWriter& w) override { if (onSave) onSave(w); }
    void OnLoad(SaveReader& r) override { if (onLoad) onLoad(r); }
    /// Give this object a fresh unique id if it has none; returns the id.
    const std::string& EnsureId();
};

/// How CaptureScene picks the objects to save.
struct SaveCaptureOptions {
    bool allObjects = false;      ///< save every non-transient object, Saveable or not
    std::string includeTag;       ///< also save objects with this tag
    bool components = true;       ///< run the built-in adapters (Rigidbody, Inventory, Stats)
};

struct SaveCaptureReport {
    int objects = 0;              ///< objects written
    int dynamic = 0;              ///< of those, spawned while playing
    int destroyed = 0;            ///< static objects recorded as destroyed
    std::vector<std::string> warnings;
};

struct SaveApplyOptions {
    bool removeUnsavedDynamic = true;   ///< dynamic objects that aren't in the save are destroyed
    bool startSpawned = true;           ///< run Awake/Start on spawned objects before restoring them
    bool sceneMustMatch = false;        ///< refuse a save made in a scene with another name
    bool restoreComponents = true;      ///< run the built-in adapters
    bool restorePlayTime = true;
    /// Runs first. Attach Saveable components to objects a level file loaded (the
    /// scene format doesn't store them).
    std::function<void(Scene&)> prepare;
};

struct SaveApplyReport {
    bool ok = true;
    std::string error;
    int restored = 0;             ///< existing objects that got their state back
    int spawned = 0;              ///< dynamic objects created
    int destroyed = 0;            ///< objects removed (saved as destroyed, or not in the save)
    int missing = 0;              ///< saved static objects the scene no longer has
    std::vector<std::string> warnings;
};

/// A component adapter: how SaveSystem saves and restores a built-in component type.
/// `save` writes under its own scope and returns false when the object doesn't have
/// the component (nothing is stored). `load` runs only if something was stored.
struct SaveAdapter {
    std::string name;
    std::function<bool(GameObject&, SaveWriter&)> save;
    std::function<void(GameObject&, SaveReader&)> load;
};

/// Capture a Scene's state into a SaveDocument and put it back, matching objects by
/// key so it works after the level is reloaded.
///
/// Saved per object: active, local transform, every ISaveable component's fields, and
/// the adapters (Rigidbody2D/3D velocity, Inventory contents, Stats). Objects spawned
/// while playing (Saveable::dynamic) are recorded with their prefab and recreated when
/// they are missing. A static object that was in the level when MarkSceneLoaded was
/// called and has since been destroyed is recorded as destroyed and destroyed again
/// on apply.
class SaveSystem {
public:
    static constexpr int kSceneFormat = 1;

    /// Remember which objects the level starts with, so a later capture can tell
    /// which were destroyed. Call it right after a level loads. (CaptureScene does it
    /// itself the first time it sees a scene, which then reports nothing destroyed.)
    static void MarkSceneLoaded(Scene& scene, const SaveCaptureOptions& options = {});
    /// What Saveable::Start calls: when `go` is part of a level that was just (re)loaded,
    /// take the starting set now, so objects destroyed before the first save are recorded.
    static void NoteLevelObject(Scene& scene, GameObject& go);
    /// Objects queued with Scene::Destroy are still in the scene until the frame ends,
    /// so they are captured as present: save at the start of a frame, or the next one.
    static SaveDocument CaptureScene(Scene& scene, const SaveCaptureOptions& options = {}, SaveCaptureReport* report = nullptr);
    /// Put a captured scene back. Call it between frames, not from inside a component's
    /// Update: it can spawn objects and run Start on them. SaveManager::Load does it at
    /// a safe time with `deferred` or `reloadScene`. Objects it removes go with the
    /// frame's destroy pass, like any Scene::Destroy.
    static SaveApplyReport ApplyScene(Scene& scene, const SaveDocument& doc, const SaveApplyOptions& options = {});
    /// Capture, serialise, parse and compare: true if the scene's state survives the
    /// text round trip (debug check for OnSave code).
    static bool VerifyCapture(Scene& scene, std::string* why = nullptr, const SaveCaptureOptions& options = {});

    /// The key `go` is saved under: "id:<saveId>" or "path:Root/Child". O(scene size).
    static std::string KeyOf(const GameObject& go);
    /// A new unique id for a dynamic object.
    static std::string NewId();

    /// Create a dynamic object from a prefab file (or "factory:Name") at `position`,
    /// already marked Saveable + dynamic with a fresh id. nullptr if it can't be made.
    static GameObject* Spawn(Scene& scene, const std::string& prefab, const Vec3& position = Vec3::Zero,
                             std::string* error = nullptr);
    /// Register a way to build a dynamic object in code, used by "factory:<name>" prefabs.
    static void RegisterFactory(const std::string& name, std::function<GameObject*(Scene&)> make);
    static void UnregisterFactory(const std::string& name);

    /// Add or replace an adapter (by name). Built-ins: "rigidbody2d", "rigidbody3d",
    /// "inventory", "stats".
    static void RegisterAdapter(SaveAdapter adapter);
    static void ResetAdapters();
    static std::vector<std::string> AdapterNames();

    /// Seconds played: carried through loads (SaveManager::Load sets it from the slot).
    static float PlayTime();
    static void SetPlayTime(float seconds);

    /// Run `fn` at the start of the scene's next frame (outside any component's
    /// Update), through the scene's own scheduler, so it is dropped if the scene is.
    static void RunNextFrame(Scene& scene, std::function<void()> fn);
};

// =====================================================================
// 4. Quick save, autosave ring, continue
// =====================================================================

struct SaveRequest {
    std::string label;
    std::string thumbnail;                     ///< image path stored in the header
    std::string kind;                          ///< SlotKind::*; empty = manual
    std::map<std::string, std::string> fields; ///< custom header fields
    float playTime = -1.0f;                    ///< < 0 = SaveSystem::PlayTime()
    SaveCaptureOptions capture;
    SaveDocument extra;                        ///< the game's own blocks, stored beside the scene
    std::function<void(SaveDocument&)> collect;   ///< or add them here, when the save is made
    /// Save at the start of the scene's next frame instead of now. Then the result
    /// comes through `onDone`.
    bool deferred = false;
    std::function<void(bool ok, const std::string& error)> onDone;
};

struct SaveLoadOptions {
    /// Reload this .okayscene first (at the end of the frame), then put the saved
    /// state on it. Empty = apply to the scene as it is.
    std::string reloadScene;
    SaveApplyOptions apply;
    /// Load at the start of the scene's next frame instead of now. Use it when loading
    /// from inside a component's Update or a script callback: loading can create objects
    /// and start them, which must not happen while the scene is walking its components.
    bool deferred = false;
    /// Called after the scene state is back, with the whole document (the game's own
    /// blocks are in it) and the slot's info.
    std::function<void(const SaveDocument& doc, const SaveSlotInfo& info)> onLoaded;
    std::function<void(bool ok, const std::string& error)> onDone;
    SaveDocument* outDocument = nullptr;       ///< receives the document (immediate loads)
    SaveApplyReport* outReport = nullptr;      ///< receives the apply report (immediate loads)
};

/// Slots for a game: save / load by name, a quick slot, N rotating autosaves and
/// Continue (the newest valid slot).
class SaveManager {
public:
    explicit SaveManager(SaveStore& store) : m_store(&store) {}
    static SaveManager& Default();
    SaveStore& Store() { return *m_store; }
    const SaveStore& Store() const { return *m_store; }

    bool Save(Scene& scene, const std::string& slot, const SaveRequest& request = {}, std::string* error = nullptr);
    bool Load(Scene& scene, const std::string& slot, const SaveLoadOptions& options = {}, std::string* error = nullptr);

    // quick save
    void SetQuickSlot(const std::string& slot) { m_quickSlot = slot; }
    const std::string& QuickSlot() const { return m_quickSlot; }
    bool QuickSave(Scene& scene, SaveRequest request = {}, std::string* error = nullptr);
    bool QuickLoad(Scene& scene, const SaveLoadOptions& options = {}, std::string* error = nullptr);
    bool HasQuickSave() const { return m_store->Exists(m_quickSlot); }

    // autosave ring: autosave_1 ... autosave_N, each save goes to the first free one,
    // then to the oldest
    void SetAutosaveCount(int n) { m_autoCount = n < 1 ? 1 : n; }
    int  AutosaveCount() const { return m_autoCount; }
    void SetAutosavePrefix(const std::string& prefix) { m_autoPrefix = prefix; }
    std::vector<std::string> AutosaveSlots() const;
    /// The slot the next AutoSave will write.
    std::string NextAutosaveSlot() const;
    /// Save into the ring; `slotUsed` gets the slot name.
    bool AutoSave(Scene& scene, SaveRequest request = {}, std::string* error = nullptr, std::string* slotUsed = nullptr);

    // continue
    /// The newest slot (of any kind) that is not corrupt or incompatible; "" if none.
    std::string ContinueSlot() const { return m_store->NewestValidSlot(); }
    bool CanContinue() const { return !ContinueSlot().empty(); }
    /// Load the newest valid slot; if it turns out to be unreadable, the next newest.
    bool Continue(Scene& scene, const SaveLoadOptions& options = {}, std::string* error = nullptr, std::string* slotUsed = nullptr);

    // management (thin wrappers over the store)
    std::vector<SaveSlotInfo> ListSlots(SlotSort sort = SlotSort::Newest, bool deep = false) const { return m_store->ListSlots(sort, deep); }
    bool Delete(const std::string& slot) { return m_store->Delete(slot); }
    bool Copy(const std::string& from, const std::string& to, bool overwrite = false, std::string* error = nullptr) { return m_store->Copy(from, to, overwrite, error); }
    bool Rename(const std::string& from, const std::string& to, bool overwrite = false, std::string* error = nullptr) { return m_store->Rename(from, to, overwrite, error); }

private:
    SaveStore* m_store;
    std::string m_quickSlot = "quicksave";
    std::string m_autoPrefix = "autosave_";
    int m_autoCount = 3;
};

// =====================================================================
// 5. Checkpoints
// =====================================================================

/// Named checkpoints, each a slot (kind "checkpoint") holding the scene state at the
/// moment it was reached. Restoring puts the scene back to that moment.
class CheckpointManager {
public:
    explicit CheckpointManager(SaveManager& saves) : m_saves(&saves) {}
    /// Bound to SaveManager::Default().
    static CheckpointManager& Global();
    /// The manager CheckpointTrigger uses: SetActive(&mine), or Global() if none.
    static CheckpointManager& Active();
    static void SetActive(CheckpointManager* manager);

    /// The slot a checkpoint name is stored in: "checkpoint_<name>", with characters a
    /// slot name can't hold replaced by '_'.
    static std::string SlotFor(const std::string& name);

    /// Keep only the newest `n` checkpoints (0 = keep all).
    void SetMaxCheckpoints(int n) { m_max = n < 0 ? 0 : n; }
    int  MaxCheckpoints() const { return m_max; }

    /// Save a checkpoint now (or, with `deferred`, at the start of the next frame).
    bool Reach(Scene& scene, const std::string& name, bool deferred = false, std::string* error = nullptr);
    bool Restore(Scene& scene, const std::string& name, const SaveLoadOptions& options = {}, std::string* error = nullptr);
    bool RestoreLast(Scene& scene, const SaveLoadOptions& options = {}, std::string* error = nullptr);

    /// The checkpoint reached most recently ("" if none).
    std::string Last() const;
    /// Every checkpoint, newest first.
    std::vector<std::string> Names() const;
    bool Has(const std::string& name) const;
    bool Remove(const std::string& name);
    /// Delete every checkpoint; returns how many.
    int Clear();

    /// Called after a checkpoint has been saved.
    std::function<void(const std::string& name)> onReached;

    SaveManager& Saves() { return *m_saves; }

private:
    SaveManager* m_saves;
    int m_max = 0;
    void Prune();
};

class Collider2D;
class Collider3D;

/// A spot that saves a checkpoint when the player reaches it: either when an object
/// with `targetTag` comes within `radius`, or when such an object enters a trigger
/// collider on this object. Uses CheckpointManager::Active().
class CheckpointTrigger : public Behaviour {
public:
    std::string checkpoint = "checkpoint";
    std::string targetTag = "Player";
    float radius = 2.0f;          ///< 0 = react to trigger colliders only
    bool once = true;             ///< fire a single time per scene load
    bool deferred = true;         ///< save at the start of the next frame
    float checkInterval = 0.1f;   ///< seconds between distance checks
    bool triggered = false;       ///< runtime: it has fired

    /// Reach the checkpoint now. False if `once` and it already fired.
    bool Fire();
    void Update(float dt) override;
    void OnTriggerEnter3D(Collider3D* other) override;
    void OnTriggerEnter2D(Collider2D* other) override;

private:
    float m_timer = 1.0e9f;       // the first Update checks at once
};

// =====================================================================
// 6. Cloud / sync hooks
// =====================================================================

/// One slot as a sync backend sees it.
struct SaveSyncEntry {
    std::string slot;
    long long modifiedMs = 0;     ///< the slot's save time
    std::string checksum;         ///< SaveStore::Checksum of its bytes
    long long size = 0;
};

/// A place slots can be mirrored to: a cloud service, a shared folder. Implement it
/// with whatever transport the game has; nothing here talks to a network. Methods
/// return false (with `error` set) when the backend can't be reached.
class ISaveSync {
public:
    virtual ~ISaveSync() = default;
    virtual bool List(std::vector<SaveSyncEntry>& out, std::string* error = nullptr) = 0;
    virtual bool Upload(const std::string& slot, const std::string& bytes, long long modifiedMs, std::string* error = nullptr) = 0;
    virtual bool Download(const std::string& slot, std::string& bytes, SaveSyncEntry* entry = nullptr, std::string* error = nullptr) = 0;
    virtual bool Remove(const std::string& slot, std::string* error = nullptr) = 0;
};

/// Slots kept in memory (tests, or a base for a real backend).
class MemorySaveSync : public ISaveSync {
public:
    bool List(std::vector<SaveSyncEntry>& out, std::string* error = nullptr) override;
    bool Upload(const std::string& slot, const std::string& bytes, long long modifiedMs, std::string* error = nullptr) override;
    bool Download(const std::string& slot, std::string& bytes, SaveSyncEntry* entry = nullptr, std::string* error = nullptr) override;
    bool Remove(const std::string& slot, std::string* error = nullptr) override;
    /// Make every call fail as if the network were down.
    void SetOffline(bool offline) { m_offline = offline; }
    bool Has(const std::string& slot) const { return m_slots.find(slot) != m_slots.end(); }
    std::size_t Count() const { return m_slots.size(); }
    int uploads = 0, downloads = 0;

private:
    struct Item { std::string bytes; long long modifiedMs = 0; };
    std::map<std::string, Item> m_slots;
    bool m_offline = false;
};

/// Slots kept in a folder as <slot>.okayslot (a synced drive, a mounted share).
class FolderSaveSync : public ISaveSync {
public:
    explicit FolderSaveSync(std::string directory) : m_dir(std::move(directory)) {}
    bool List(std::vector<SaveSyncEntry>& out, std::string* error = nullptr) override;
    bool Upload(const std::string& slot, const std::string& bytes, long long modifiedMs, std::string* error = nullptr) override;
    bool Download(const std::string& slot, std::string& bytes, SaveSyncEntry* entry = nullptr, std::string* error = nullptr) override;
    bool Remove(const std::string& slot, std::string* error = nullptr) override;
    const std::string& Directory() const { return m_dir; }

private:
    std::string m_dir;
    std::string PathFor(const std::string& slot) const;
};

enum class SyncState { InSync, LocalOnly, RemoteOnly, LocalChanged, RemoteChanged, Conflict };
enum class SyncChoice { UseLocal, UseRemote, KeepBoth, Skip };
enum class ConflictPolicy {
    Newest,        ///< the later save time wins; equal times stay unresolved
    PreferLocal,
    PreferRemote,
    Ask,           ///< ask SaveSyncer::Options::resolve; without one, leave it
    Skip
};

struct SaveSyncConflict {
    std::string slot;
    SaveSyncEntry local, remote;
    std::string baseChecksum;     ///< what both sides held at the last sync ("" = never synced)
};

/// Mirrors a SaveStore with an ISaveSync backend. It remembers the checksum both sides
/// held at the last successful sync (in <store dir>/sync_state.txt), so it can tell
/// which side changed. Both changed, or never synced and they differ, is a conflict.
class SaveSyncer {
public:
    SaveSyncer(SaveStore& local, ISaveSync& remote) : m_local(&local), m_remote(&remote) {}

    struct Options {
        ConflictPolicy policy = ConflictPolicy::Newest;
        std::function<SyncChoice(const SaveSyncConflict&)> resolve;
    };
    struct Item {
        std::string slot;
        SyncState state = SyncState::InSync;
        SaveSyncEntry local, remote;
        std::string base;
    };
    struct Report {
        bool ok = true;
        int uploaded = 0, downloaded = 0, conflicts = 0, unresolved = 0, keptBoth = 0, failed = 0;
        std::vector<std::string> messages;
        std::vector<std::string> conflictSlots;
    };

    /// What a sync would do, without doing it.
    bool Plan(std::vector<Item>& out, std::string* error = nullptr);
    /// Sync every slot: upload what only changed here, download what only changed
    /// there, and resolve conflicts by `options`.
    Report Sync();
    Report Sync(const Options& options);

private:
    SaveStore* m_local;
    ISaveSync* m_remote;
    std::string StatePath() const;
    std::map<std::string, std::string> LoadBase() const;
    void SaveBase(const std::map<std::string, std::string>& base) const;
};

// =====================================================================
// 7. Menu model
// =====================================================================

/// One row of a load / save menu.
struct SaveMenuRow {
    std::string slot;
    std::string title;           ///< the label, or the slot name, or "Empty slot"
    std::string label, scene, thumbnail, gameVersion, kind;
    std::string savedText;       ///< "2026-11-08 14:03"
    std::string agoText;         ///< "5 minutes ago"
    std::string playTimeText;    ///< "1h 05m"
    std::string statusText;      ///< "", "Damaged", "Saved by a newer version", "Restored from backup"
    long long savedAt = 0;       ///< seconds since 1970 (UTC)
    long long savedAtMs = 0;
    float playTime = 0.0f;
    std::map<std::string, std::string> fields;
    bool empty = false;          ///< a fixed slot with nothing in it yet
    bool corrupt = false;
    bool incompatible = false;
    bool fromBackup = false;
    bool isAuto = false, isQuick = false, isCheckpoint = false;
    bool isNewest = false;       ///< what Continue would load
    bool canLoad = false;
    bool canSave = false;        ///< a manual slot (not auto / quick / checkpoint)
    bool canDelete = false;
};

/// Rows a UI binds to. No rendering: it reads the store, formats the text, and tracks
/// which row is selected.
class SaveMenuModel {
public:
    struct Options {
        bool includeAuto = true;
        bool includeQuick = true;
        bool includeCheckpoints = false;
        bool showCorrupt = true;
        /// > 0: always list slot_1 ... slot_N (empty ones included) first, in order.
        int manualSlots = 0;
        std::string manualPrefix = "slot_";
        SlotSort sort = SlotSort::Newest;
        int utcOffsetMinutes = 0;        ///< shift shown times to local time
    };

    explicit SaveMenuModel(SaveStore& store) : m_store(&store) {}
    SaveMenuModel(SaveStore& store, const Options& options) : m_store(&store), m_options(options) {}

    Options& GetOptions() { return m_options; }
    /// Re-read the store. `nowMs` < 0 uses the store's clock (for the "ago" text).
    void Refresh(long long nowMs = -1);
    const std::vector<SaveMenuRow>& Rows() const { return m_rows; }
    const SaveMenuRow* Find(const std::string& slot) const;
    /// The slot Continue would load: the newest valid one of any kind, even one the
    /// rows hide (e.g. a checkpoint). "" if there is none.
    const std::string& ContinueSlot() const { return m_continue; }
    bool CanContinue() const { return !m_continue.empty(); }
    /// Its row, if it is listed.
    const SaveMenuRow* ContinueRow() const;

    // selection (-1 = none); Refresh keeps the selected slot selected if it is still listed
    int Selected() const { return m_selected; }
    const SaveMenuRow* SelectedRow() const;
    bool Select(int index);
    bool SelectSlot(const std::string& slot);
    void SelectNext();
    void SelectPrevious();
    /// Delete the selected slot (not an empty row) and refresh.
    bool DeleteSelected(std::string* error = nullptr);

    // text helpers, also useful on their own
    static std::string FormatPlayTime(float seconds);
    static std::string FormatDateTime(long long unixSeconds, int utcOffsetMinutes = 0);
    static std::string FormatAgo(long long secondsAgo);

private:
    SaveStore* m_store;
    Options m_options;
    std::vector<SaveMenuRow> m_rows;
    std::string m_continue;
    int m_selected = -1;
};

} // namespace okay
