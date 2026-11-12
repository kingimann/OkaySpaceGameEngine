#include "okay/Core/GameSaves.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneManager.hpp"
#include <algorithm>
#include <memory>

namespace okay {

namespace {

SaveManager* g_manager = nullptr;
GameSaveConfig g_settings;
int g_restoring = 0;   // loads that were asked for and haven't finished

const char* kBadSlot = " isn't a valid slot name (letters, digits, - and _, up to 64)";

bool Fail(std::string* error, const std::string& why) {
    Log::Error("[save] ", why);
    if (error) *error = why;
    return false;
}

} // namespace

SaveManager& GameSaves::Manager() { return g_manager ? *g_manager : SaveManager::Default(); }
void GameSaves::UseManager(SaveManager* manager) { g_manager = manager; g_restoring = 0; }
GameSaveConfig& GameSaves::Settings() { return g_settings; }
bool GameSaves::Restoring() { return g_restoring > 0; }

bool GameSaves::Save(Scene& scene, const std::string& slot, const std::string& label, std::string* error) {
    if (!SaveStore::ValidSlot(slot)) return Fail(error, "Couldn't save: \"" + slot + "\"" + kBadSlot);
    SaveRequest req;
    req.label = label.empty() ? std::string("Saved game") : label;
    std::string err;
    if (!Manager().Save(scene, slot, req, &err)) return Fail(error, "Couldn't save slot \"" + slot + "\": " + err);
    return true;
}

bool GameSaves::Load(Scene& scene, const std::string& slot, std::string* error) {
    if (!SaveStore::ValidSlot(slot)) return Fail(error, "Couldn't load: \"" + slot + "\"" + kBadSlot);
    SaveStore& store = Manager().Store();
    if (!store.Exists(slot)) return Fail(error, "Couldn't load: there is no save in slot \"" + slot + "\".");
    // Read it now, so a damaged or newer-version slot is reported where the game asked, not a frame later.
    SaveDocument doc;
    SaveSlotInfo info;
    std::string rerr;
    if (!store.Read(slot, doc, &info, &rerr)) return Fail(error, "Couldn't load slot \"" + slot + "\": " + rerr);
    if (!rerr.empty()) Log::Warning("[save] slot \"", slot, "\": ", rerr);   // read from the backup copy
    SaveLoadOptions o;
    o.deferred = true;
    // Only a level the game ships is loaded by name: the slot header is not trusted to name a file.
    if (!info.scene.empty() && SceneManager::IndexOf(info.scene) >= 0)
        o.reloadScene = SceneManager::Scenes()[(std::size_t)SceneManager::IndexOf(info.scene)];
    ++g_restoring;
    o.onDone = [slot](bool ok, const std::string& why) {
        if (g_restoring > 0) --g_restoring;
        if (!ok) Log::Error("[save] Couldn't load slot \"", slot, "\": ", why);
    };
    std::string err;
    if (!Manager().Load(scene, slot, o, &err)) {
        if (g_restoring > 0) --g_restoring;
        return Fail(error, "Couldn't load slot \"" + slot + "\": " + err);
    }
    return true;
}

bool GameSaves::Continue(Scene& scene, std::string* error) {
    const std::string slot = ContinueSlot();
    if (!slot.empty()) return Load(scene, slot, error);
    // Nothing valid: say why, so a slot from a newer version isn't reported as "no save".
    for (const SaveSlotInfo& i : Manager().ListSlots(SlotSort::Newest, true))
        return Fail(error, "Couldn't continue: slot \"" + i.slot + "\" " + (i.error.empty() ? std::string("can't be loaded") : i.error));
    return Fail(error, "Couldn't continue: there is no save yet.");
}

bool GameSaves::AutoSave(Scene& scene, std::string* error) {
    Manager().SetAutosaveCount(std::min(std::max(1, g_settings.autosaveSlots), 20));
    SaveRequest req;
    req.label = "Autosave";
    std::string err;
    if (!Manager().AutoSave(scene, req, &err)) return Fail(error, "Autosave failed: " + err);
    return true;
}

bool GameSaves::Delete(const std::string& slot) {
    if (!SaveStore::ValidSlot(slot) || !Manager().Store().Exists(slot)) return false;
    return Manager().Delete(slot);
}

bool GameSaves::Exists(const std::string& slot) {
    return SaveStore::ValidSlot(slot) && Manager().Store().Exists(slot);
}

std::string GameSaves::ContinueSlot() { return Manager().ContinueSlot(); }
bool GameSaves::CanContinue() { return !ContinueSlot().empty(); }

long long GameSaves::SavedAt(const std::string& slot) {
    if (!Exists(slot)) return 0;
    return Manager().Store().Info(slot).savedAt;
}

double GameSaves::SecondsSinceSave(const std::string& slot) {
    const long long at = SavedAt(slot);
    if (at <= 0) return -1.0;
    return std::max(0.0, (double)Time::UnixSeconds() - (double)at);
}

std::string GameSaves::SavedText(const std::string& slot) {
    const long long at = SavedAt(slot);
    return at > 0 ? SaveMenuModel::FormatDateTime(at, 0) : std::string();
}

std::string GameSaves::Label(const std::string& slot) {
    if (!Exists(slot)) return std::string();
    return Manager().Store().Info(slot).label;
}

void GameSaves::RequestSceneStartAutosave(Scene& scene) {
    Scene* sc = &scene;
    // Next frame, once every object has started; and not when a load is putting a save back
    // (that would overwrite the ring with the half-restored scene).
    SaveSystem::RunNextFrame(scene, [sc]() {
        if (Restoring()) return;
        std::string err;
        GameSaves::AutoSave(*sc, &err);
    });
}

// ---- SaveConfig ----

void SaveConfig::Start() {
    m_timer = 0.0f;
    g_settings.autosaveInterval = autosaveInterval;
    g_settings.autosaveOnSceneStart = autosaveOnSceneStart;
    g_settings.autosaveSlots = std::min(std::max(1, autosaveSlots), 20);
    if (autosaveOnSceneStart && !GameSaves::Restoring())
        if (Scene* s = GetScene()) GameSaves::RequestSceneStartAutosave(*s);
}

void SaveConfig::Update(float dt) {
    if (autosaveInterval <= 0.0f || GameSaves::Restoring()) return;
    m_timer += dt;
    if (m_timer < autosaveInterval) return;
    m_timer = 0.0f;
    if (Scene* s = GetScene()) {
        g_settings.autosaveSlots = std::min(std::max(1, autosaveSlots), 20);
        std::string err;
        GameSaves::AutoSave(*s, &err);
    }
}

} // namespace okay
