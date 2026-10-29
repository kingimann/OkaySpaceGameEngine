#include "okay/Components/QuestLog.hpp"
#include "StoryKv.hpp"
#include "okay/Components/Character.hpp"          // AssetBaseDir
#include "okay/Components/DialogueGraph.hpp"      // DialogueValueFromText (reward variables)
#include "okay/Components/GridInventory.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/InventoryPlayer.hpp"
#include "okay/Components/ItemPickup.hpp"         // InventoryOf
#include "okay/Components/StoryState.hpp"
#include "okay/Components/SurvivalComponents.hpp" // HealthDeathHook
#include "okay/Components/UIAnchor.hpp"           // UICanvas
#include "okay/Components/UIToast.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Inventory/InventoryEvents.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/Scripting/ScriptUI.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace okay {

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------
namespace {
const char* kObjNames[] = {"talk", "collect", "kill", "reach", "custom"};
const char* kObjLabels[] = {"Talk to", "Collect", "Kill", "Reach", "Custom"};
const char* kStatusNames[] = {"inactive", "active", "completed", "failed"};

std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) { if (!s.empty()) s += ','; s += x; }
    return s;
}
std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s + ",") {
        if (c == ',') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else if (c != ' ') cur += c;
    }
    return out;
}
bool TagMatches(GameObject* who, const std::string& tag) {
    if (!who) return false;
    if (tag.empty()) return true;
    return who->tag == tag || who->name == tag || who->name.find(tag) != std::string::npos;
}
} // namespace

const char* QuestObjective::KindName(Kind k) { return kObjNames[(int)k]; }
const char* QuestObjective::KindLabel(Kind k) { return kObjLabels[(int)k]; }
bool QuestObjective::KindFromName(const std::string& s, Kind& out) {
    for (int i = 0; i < 5; ++i) if (s == kObjNames[i]) { out = (Kind)i; return true; }
    return false;
}
const char* QuestStatusName(QuestStatus s) { return kStatusNames[(int)s]; }
bool QuestStatusFromName(const std::string& s, QuestStatus& out) {
    for (int i = 0; i < 4; ++i) if (s == kStatusNames[i]) { out = (QuestStatus)i; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// The .okayquest format
// ---------------------------------------------------------------------------
std::string QuestFile::Write(const std::vector<QuestDef>& quests) {
    std::ostringstream o;
    o << "okayquest 1\n";
    for (const auto& q : quests) {
        storykv::Out k;
        k.str("id", q.id).str("title", q.title).str("desc", q.description).b("repeatable", q.repeatable);
        if (!q.prerequisites.empty()) k.str("pre", Join(q.prerequisites));
        o << "quest" << k.text() << "\n";
        for (const auto& ob : q.objectives) {
            storykv::Out ko;
            ko.str("kind", QuestObjective::KindName(ob.kind)).str("desc", ob.description).str("target", ob.target)
              .num("count", ob.count).b("optional", ob.optional);
            o << "objective" << ko.text() << "\n";
        }
        for (const auto& it : q.reward.items) { storykv::Out kr; kr.str("item", it.first).num("count", it.second); o << "reward" << kr.text() << "\n"; }
        for (const auto& v : q.reward.vars) { storykv::Out kr; kr.str("var", v.first).str("value", v.second); o << "reward" << kr.text() << "\n"; }
        if (!q.reward.event.empty()) { storykv::Out kr; kr.str("event", q.reward.event); o << "reward" << kr.text() << "\n"; }
    }
    return o.str();
}

bool QuestFile::Parse(const std::string& text, std::vector<QuestDef>& out, std::string* err) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line)) { if (err) *err = "empty file"; return false; }
    {
        std::istringstream h(line);
        std::string magic; int version = 0;
        h >> magic >> version;
        if (magic != "okayquest" || version < 1) { if (err) *err = "not a quest file (expected 'okayquest 1')"; return false; }
    }
    out.clear();
    int lineNo = 1;
    while (std::getline(in, line)) {
        ++lineNo;
        std::string head, rest;
        if (!storykv::SplitHead(line, head, rest) || head[0] == '#') continue;
        if (head == "quest") {
            storykv::In k(rest);
            QuestDef q;
            std::string pre;
            k.str("id", q.id); k.str("title", q.title); k.str("desc", q.description); k.b("repeatable", q.repeatable); k.str("pre", pre);
            q.prerequisites = Split(pre);
            out.push_back(q);
        } else if (head == "objective" || head == "reward") {
            if (out.empty()) { if (err) *err = "line " + std::to_string(lineNo) + ": " + head + " before any quest"; return false; }
            storykv::In k(rest);
            QuestDef& q = out.back();
            if (head == "objective") {
                QuestObjective ob;
                std::string kind;
                k.str("kind", kind); k.str("desc", ob.description); k.str("target", ob.target); k.i("count", ob.count); k.b("optional", ob.optional);
                if (!QuestObjective::KindFromName(kind, ob.kind)) { if (err) *err = "line " + std::to_string(lineNo) + ": unknown objective kind '" + kind + "'"; return false; }
                q.objectives.push_back(ob);
            } else {
                std::string item, var, value, event;
                int count = 1;
                k.str("item", item); k.i("count", count); k.str("var", var); k.str("value", value); k.str("event", event);
                if (!item.empty()) q.reward.items.push_back({item, count});
                if (!var.empty()) q.reward.vars.push_back({var, value});
                if (!event.empty()) q.reward.event = event;
            }
        } else {
            if (err) *err = "line " + std::to_string(lineNo) + ": unknown record '" + head + "'";
            return false;
        }
    }
    return true;
}

bool QuestFile::Load(const std::string& path, std::vector<QuestDef>& out, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "can't open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
    return Parse(ss.str(), out, err);
}

bool QuestFile::Save(const std::string& path, const std::vector<QuestDef>& quests) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << Write(quests);
    return (bool)f;
}

std::vector<QuestFile::Issue> QuestFile::Validate(const std::vector<QuestDef>& quests, const std::vector<std::string>* knownItems) {
    std::vector<Issue> out;
    auto add = [&](const std::string& q, int sev, std::string text) { out.push_back({q, sev, std::move(text)}); };
    auto knownItem = [&](const std::string& it) {
        return !knownItems || knownItems->empty() || std::find(knownItems->begin(), knownItems->end(), it) != knownItems->end();
    };
    std::set<std::string> ids;
    for (const auto& q : quests) {
        if (q.id.empty()) { add(q.id, 2, "A quest has no id."); continue; }
        if (!ids.insert(q.id).second) add(q.id, 2, "Duplicate quest id \"" + q.id + "\".");
    }
    for (const auto& q : quests) {
        if (q.title.empty()) add(q.id, 1, "No title.");
        if (q.objectives.empty()) add(q.id, 1, "No objectives: it completes only from a script or a dialogue.");
        for (const auto& p : q.prerequisites) {
            if (!ids.count(p)) add(q.id, 2, "Prerequisite \"" + p + "\" is not a quest here.");
            if (p == q.id) add(q.id, 2, "A quest can't require itself.");
        }
        bool required = false;
        for (std::size_t i = 0; i < q.objectives.size(); ++i) {
            const auto& ob = q.objectives[i];
            const std::string where = "Objective " + std::to_string(i + 1);
            if (!ob.optional) required = true;
            if (ob.description.empty()) add(q.id, 1, where + " has no description.");
            if (ob.kind != QuestObjective::Kind::Custom && ob.target.empty()) add(q.id, 2, where + " has no target.");
            if (ob.kind == QuestObjective::Kind::Collect && !knownItem(ob.target)) add(q.id, 1, where + ": unknown item \"" + ob.target + "\".");
            if ((ob.kind == QuestObjective::Kind::Collect || ob.kind == QuestObjective::Kind::Kill) && ob.count <= 0)
                add(q.id, 2, where + ": the count must be at least 1.");
        }
        if (!q.objectives.empty() && !required) add(q.id, 1, "Every objective is optional, so the quest completes at once.");
        for (const auto& it : q.reward.items) {
            if (it.first.empty()) add(q.id, 2, "A reward item has no id.");
            else if (!knownItem(it.first)) add(q.id, 1, "Reward: unknown item \"" + it.first + "\".");
            if (it.second <= 0) add(q.id, 2, "Reward item count must be at least 1.");
        }
        for (const auto& v : q.reward.vars) if (v.first.empty()) add(q.id, 2, "A reward variable has no name.");
    }
    return out;
}

// ---------------------------------------------------------------------------
// QuestLog
// ---------------------------------------------------------------------------
QuestLog* QuestLog::Get(Scene* scene, bool create) {
    if (!scene) return nullptr;
    if (auto* q = scene->FindObjectOfType<QuestLog>()) return q;
    if (!create) return nullptr;
    GameObject* go = scene->Find("Quests");
    if (!go) go = scene->CreateGameObject("Quests");
    return go->AddComponent<QuestLog>();
}

void QuestLog::InstallDeathHook() {
    auto& hook = HealthDeathHook();
    if (hook) return;
    hook = [](GameObject* victim) {
        Scene* s = victim ? victim->scene() : nullptr;
        if (!s) return;
        for (QuestLog* q : s->FindObjectsOfType<QuestLog>()) if (q) q->NotifyKill(victim);
    };
}

void QuestLog::Awake() { InstallDeathHook(); }

void QuestLog::Start() {
    InstallDeathHook();
    if (!questFile.empty() && (!m_fileLoaded || m_loadedPath != ResolvedFile())) LoadFile(nullptr);
}

std::string QuestLog::ResolvedFile() const {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p(questFile);
    if (questFile.empty() || p.is_absolute() || Character::AssetBaseDir().empty() || fs::exists(p, ec)) return questFile;
    return (fs::path(Character::AssetBaseDir()) / p).string();
}

bool QuestLog::LoadFile(std::string* err) {
    m_fileQuests.clear();
    m_fileLoaded = true;
    m_loadedPath = ResolvedFile();
    if (questFile.empty()) return true;
    return QuestFile::Load(m_loadedPath, m_fileQuests, err);
}

const QuestDef* QuestLog::Def(const std::string& id) const {
    for (const auto& q : quests) if (q.id == id) return &q;
    for (const auto& q : m_fileQuests) if (q.id == id) return &q;
    return nullptr;
}
std::vector<const QuestDef*> QuestLog::Defs() const {
    std::vector<const QuestDef*> out;
    for (const auto& q : quests) out.push_back(&q);
    for (const auto& q : m_fileQuests) if (!Def(q.id) || Def(q.id) == &q) out.push_back(&q);
    return out;
}
std::vector<std::string> QuestLog::Ids() const {
    std::vector<std::string> out;
    for (const QuestDef* q : Defs()) out.push_back(q->id);
    return out;
}

const QuestRecord* QuestLog::Record(const std::string& id) const {
    for (const auto& r : records) if (r.id == id) return &r;
    return nullptr;
}
QuestRecord& QuestLog::RecordFor(const std::string& id) {
    for (auto& r : records) if (r.id == id) return r;
    QuestRecord r; r.id = id;
    records.push_back(r);
    return records.back();
}
QuestStatus QuestLog::Status(const std::string& id) const {
    const QuestRecord* r = Record(id);
    return r ? r->status : QuestStatus::Inactive;
}
std::vector<std::string> QuestLog::ActiveIds() const {
    std::vector<std::string> out;
    for (const auto& r : records) if (r.status == QuestStatus::Active) out.push_back(r.id);
    return out;
}

bool QuestLog::CanStart(const std::string& id, std::string* why) const {
    const QuestDef* q = Def(id);
    if (!q) { if (why) *why = "unknown quest \"" + id + "\""; return false; }
    const QuestStatus st = Status(id);
    if (st == QuestStatus::Active) { if (why) *why = "already active"; return false; }
    if (st == QuestStatus::Completed && !q->repeatable) { if (why) *why = "already completed"; return false; }
    for (const auto& p : q->prerequisites)
        if (Status(p) != QuestStatus::Completed) { if (why) *why = "needs \"" + p + "\" completed first"; return false; }
    return true;
}

bool QuestLog::StartQuest(const std::string& id) {
    if (!CanStart(id)) return false;
    const QuestDef* q = Def(id);
    QuestRecord& r = RecordFor(id);
    r.status = QuestStatus::Active;
    r.progress.assign(q->objectives.size(), 0);
    r.done.assign(q->objectives.size(), 0);
    if (tracked.empty()) tracked = id;
    Notify("Quest started", q->title.empty() ? id : q->title, 0);
    FireEvent("on_quest_started", "OnQuestStarted", id, 0.0f);
    CheckCompletion(id);   // a quest with no required objectives completes at once
    return true;
}

bool QuestLog::CompleteQuest(const std::string& id) {
    const QuestDef* q = Def(id);
    QuestRecord* r = nullptr;
    for (auto& x : records) if (x.id == id) r = &x;
    if (!q || !r || r->status != QuestStatus::Active) return false;
    for (auto& d : r->done) d = 1;
    r->status = QuestStatus::Completed;
    ++r->completions;
    if (tracked == id) { tracked.clear(); for (const auto& x : records) if (x.status == QuestStatus::Active) { tracked = x.id; break; } }
    GrantRewards(*q);
    Notify("Quest completed", q->title.empty() ? id : q->title, 1);
    FireEvent("on_quest_completed", "OnQuestCompleted", id, (float)r->completions);
    return true;
}

bool QuestLog::FailQuest(const std::string& id) {
    QuestRecord* r = nullptr;
    for (auto& x : records) if (x.id == id) r = &x;
    if (!r || r->status != QuestStatus::Active) return false;
    r->status = QuestStatus::Failed;
    if (tracked == id) tracked.clear();
    const QuestDef* q = Def(id);
    Notify("Quest failed", q && !q->title.empty() ? q->title : id, 2);
    FireEvent("on_quest_failed", "OnQuestFailed", id, 0.0f);
    return true;
}

void QuestLog::ResetQuest(const std::string& id) {
    for (std::size_t i = 0; i < records.size(); ++i)
        if (records[i].id == id) { records.erase(records.begin() + (long)i); break; }
    if (tracked == id) tracked.clear();
}

bool QuestLog::CompleteObjective(const std::string& id, int index) {
    const QuestDef* q = Def(id);
    QuestRecord* r = nullptr;
    for (auto& x : records) if (x.id == id) r = &x;
    if (!q || !r || r->status != QuestStatus::Active) return false;
    if (index < 0 || index >= (int)q->objectives.size()) return false;
    if (r->done.size() != q->objectives.size()) r->done.assign(q->objectives.size(), 0);
    if (r->progress.size() != q->objectives.size()) r->progress.assign(q->objectives.size(), 0);
    if (r->done[index]) return true;
    r->done[index] = 1;
    r->progress[index] = std::max(r->progress[index], q->objectives[index].count);
    const std::string title = q->title.empty() ? id : q->title;
    const std::string what = q->objectives[index].description.empty() ? "Objective " + std::to_string(index + 1) : q->objectives[index].description;
    Notify(title, what + " - done", 1);
    FireEvent("on_objective_completed", "OnObjectiveCompleted", id, (float)index);
    CheckCompletion(id);
    return true;
}

bool QuestLog::AddProgress(const std::string& id, int index, int amount) {
    const QuestDef* q = Def(id);
    QuestRecord* r = nullptr;
    for (auto& x : records) if (x.id == id) r = &x;
    if (!q || !r || r->status != QuestStatus::Active || amount <= 0) return false;
    if (index < 0 || index >= (int)q->objectives.size()) return false;
    if (r->done.size() != q->objectives.size()) r->done.assign(q->objectives.size(), 0);
    if (r->progress.size() != q->objectives.size()) r->progress.assign(q->objectives.size(), 0);
    if (r->done[index]) return true;
    const int need = std::max(1, q->objectives[index].count);
    r->progress[index] = std::min(need, r->progress[index] + amount);
    if (r->progress[index] >= need) return CompleteObjective(id, index);
    const std::string title = q->title.empty() ? id : q->title;
    const std::string what = q->objectives[index].description.empty() ? "Objective " + std::to_string(index + 1) : q->objectives[index].description;
    Notify(title, what + "  " + std::to_string(r->progress[index]) + " / " + std::to_string(need), 0);
    return true;
}

int QuestLog::Progress(const std::string& id, int index) const {
    const QuestRecord* r = Record(id);
    return r && index >= 0 && index < (int)r->progress.size() ? r->progress[index] : 0;
}
bool QuestLog::ObjectiveDone(const std::string& id, int index) const {
    const QuestRecord* r = Record(id);
    return r && index >= 0 && index < (int)r->done.size() && r->done[index] != 0;
}

void QuestLog::CheckCompletion(const std::string& id) {
    const QuestDef* q = Def(id);
    const QuestRecord* r = Record(id);
    if (!q || !r || r->status != QuestStatus::Active) return;
    for (std::size_t i = 0; i < q->objectives.size(); ++i) {
        if (q->objectives[i].optional) continue;
        if (i >= r->done.size() || !r->done[i]) return;
    }
    CompleteQuest(id);
}

GameObject* QuestLog::Player() const {
    return FindInventoryPlayer(gameObject ? gameObject->scene() : nullptr, playerName);
}

int QuestLog::CountItem(const std::string& item) const {
    GameObject* p = Player();
    if (!p) return 0;
    int n = 0;
    if (Inventory* inv = ItemPickup::InventoryOf(p)) n += inv->Count(item);
    if (auto* grid = p->GetComponent<GridInventory>())
        for (const auto& it : grid->items) if (it.name == item) n += it.count;
    return n;
}

void QuestLog::GrantRewards(const QuestDef& q) {
    Scene* s = gameObject ? gameObject->scene() : nullptr;
    if (GameObject* p = Player()) {
        for (const auto& it : q.reward.items) {
            if (it.first.empty() || it.second <= 0) continue;
            if (Inventory* inv = ItemPickup::InventoryOf(p)) { inv->AddPartial(it.first, it.second); continue; }
            if (auto* grid = p->GetComponent<GridInventory>()) grid->AddDefined(it.first, it.second);
        }
    }
    if (!q.reward.vars.empty())
        if (StoryState* st = StoryState::Get(s, true))
            for (const auto& v : q.reward.vars) st->Set(v.first, DialogueValueFromText(v.second));
    if (!q.reward.event.empty()) FireEvent("on_quest_event", "OnQuestEvent", q.reward.event, 0.0f);
}

void QuestLog::FireEvent(const char* scriptEvent, const char* vsEntry, const std::string& id, float value) {
    const std::vector<vs::VsValue> args{vs::VsValue(id), vs::VsValue(value)};
    InventoryEvents::Fire(gameObject, scriptEvent, vsEntry, args);
    if (GameObject* p = Player(); p && p != gameObject) InventoryEvents::Fire(p, scriptEvent, vsEntry, args);
}

void QuestLog::Notify(const std::string& title, const std::string& body, int kind) {
    notifications.push_back(title + ": " + body);
    if (notifications.size() > 20) notifications.erase(notifications.begin());
    if (onNotify) onNotify(title, body, kind);
    if (!showNotifications) return;
    Scene* s = gameObject ? gameObject->scene() : nullptr;
    if (UIToast* t = s ? s->FindObjectOfType<UIToast>() : nullptr) { t->Push(title, body, kind); return; }
    if (ScriptUIBridge* ui = GetScriptUI()) ui->Toast((title + ": " + body).c_str(), kind, 4.0f);
}

// ---- objective sources ----
void QuestLog::NotifyKill(GameObject* victim) {
    if (!victim) return;
    for (const std::string& id : ActiveIds()) {
        const QuestDef* q = Def(id);
        if (!q) continue;
        for (std::size_t i = 0; i < q->objectives.size(); ++i)
            if (q->objectives[i].kind == QuestObjective::Kind::Kill && TagMatches(victim, q->objectives[i].target))
                AddProgress(id, (int)i, 1);
    }
}

void QuestLog::NotifyReach(const std::string& zone, GameObject* who) {
    (void)who;
    for (const std::string& id : ActiveIds()) {
        const QuestDef* q = Def(id);
        if (!q) continue;
        for (std::size_t i = 0; i < q->objectives.size(); ++i)
            if (q->objectives[i].kind == QuestObjective::Kind::Reach && q->objectives[i].target == zone)
                CompleteObjective(id, (int)i);
    }
}

void QuestLog::NotifyDialogueEnd(const std::string& npc, const std::string& graph) {
    for (const std::string& id : ActiveIds()) {
        const QuestDef* q = Def(id);
        if (!q) continue;
        for (std::size_t i = 0; i < q->objectives.size(); ++i) {
            const auto& ob = q->objectives[i];
            if (ob.kind != QuestObjective::Kind::Talk) continue;
            if (ob.target == npc || (!graph.empty() && ob.target == graph)) CompleteObjective(id, (int)i);
        }
    }
}

// ---- per frame: collect objectives, the journal key, the overlays ----
void QuestLog::Update(float) {
    if (!questFile.empty() && !m_fileLoaded) LoadFile(nullptr);
    for (const std::string& id : ActiveIds()) {
        const QuestDef* q = Def(id);
        if (!q) continue;
        for (std::size_t i = 0; i < q->objectives.size(); ++i) {
            const auto& ob = q->objectives[i];
            if (ob.kind != QuestObjective::Kind::Collect || ObjectiveDone(id, (int)i)) continue;
            const int have = CountItem(ob.target);
            const int need = std::max(1, ob.count);
            QuestRecord& r = RecordFor(id);
            if (r.progress.size() != q->objectives.size()) r.progress.assign(q->objectives.size(), 0);
            if (have >= need) CompleteObjective(id, (int)i);
            else if (have > r.progress[i]) AddProgress(id, (int)i, have - r.progress[i]);
            else r.progress[i] = std::max(0, have);
        }
    }
    if (allowJournal && journalKey && Input::GetKeyDown(journalKey)) journalOpen = !journalOpen;
    if (!allowJournal) journalOpen = false;
    if (journalOpen) Input::SetUICaptured(true);
    if (!drawUI) return;
    if (journalOpen) DrawJournal();
    else if (showTracker) DrawTracker();
}

void QuestLog::DrawTracker() {
    ScriptUIBridge* ui = GetScriptUI();
    if (!ui) return;
    std::vector<std::string> ids;
    if (!tracked.empty() && IsActive(tracked)) ids.push_back(tracked);
    for (const std::string& id : ActiveIds()) if (id != tracked && ids.size() < 3) ids.push_back(id);
    if (ids.empty()) return;
    int rows = 0;
    for (const auto& id : ids) if (const QuestDef* q = Def(id)) rows += 1 + (int)q->objectives.size();
    const float w = 300.0f, h = 44.0f + 24.0f * (float)rows;
    const float x = ScriptUIOriginX() + ScriptUIWidth(UICanvas::Width()) - w - 16.0f, y = ScriptUIOriginY() + 16.0f;
    ui->BeginAt("Quests", x, y, w, h);
    for (const auto& id : ids) {
        const QuestDef* q = Def(id);
        if (!q) continue;
        ui->Caption((q->title.empty() ? id : q->title).c_str(), 115, 209, 143);
        for (std::size_t i = 0; i < q->objectives.size(); ++i) {
            const auto& ob = q->objectives[i];
            std::string line = ObjectiveDone(id, (int)i) ? "[x] " : "[ ] ";
            line += ob.description.empty() ? ob.target : ob.description;
            if (ob.kind == QuestObjective::Kind::Collect || ob.kind == QuestObjective::Kind::Kill)
                line += "  " + std::to_string(std::min(Progress(id, (int)i), ob.count)) + "/" + std::to_string(ob.count);
            if (ob.optional) line += " (optional)";
            if (ObjectiveDone(id, (int)i)) ui->TextDim(line.c_str()); else ui->Text(line.c_str());
        }
    }
    ui->End();
}

void QuestLog::DrawJournal() {
    ScriptUIBridge* ui = GetScriptUI();
    if (!ui) return;
    const float W = ScriptUIWidth(UICanvas::Width()), H = ScriptUIHeight(UICanvas::Height());
    const float w = std::min(620.0f, W - 32.0f), h = std::min(460.0f, H - 32.0f);
    ui->BeginAt("Journal", ScriptUIOriginX() + (W - w) * 0.5f, ScriptUIOriginY() + (H - h) * 0.5f, w, h);
    m_journalTab = ui->Tabs("Active|Completed|All", m_journalTab);
    int shown = 0;
    for (const QuestDef* q : Defs()) {
        const QuestStatus st = Status(q->id);
        if (m_journalTab == 0 && st != QuestStatus::Active) continue;
        if (m_journalTab == 1 && st != QuestStatus::Completed) continue;
        if (m_journalTab == 2 && st == QuestStatus::Inactive) continue;
        ++shown;
        ui->Text((q->title.empty() ? q->id : q->title).c_str());
        ui->SameLine();
        if (st == QuestStatus::Completed) ui->Badge("completed", 72, 160, 110);
        else if (st == QuestStatus::Failed) ui->Badge("failed", 190, 70, 70);
        else ui->Badge(tracked == q->id ? "tracked" : "active", 0, 0, 0);
        if (!q->description.empty()) ui->TextDim(q->description.c_str());
        for (std::size_t i = 0; i < q->objectives.size(); ++i) {
            const auto& ob = q->objectives[i];
            std::string line = ObjectiveDone(q->id, (int)i) ? "  [x] " : "  [ ] ";
            line += ob.description.empty() ? ob.target : ob.description;
            if (ob.kind == QuestObjective::Kind::Collect || ob.kind == QuestObjective::Kind::Kill)
                line += "  " + std::to_string(std::min(Progress(q->id, (int)i), ob.count)) + "/" + std::to_string(ob.count);
            if (ObjectiveDone(q->id, (int)i)) ui->TextDim(line.c_str()); else ui->Text(line.c_str());
        }
        if (st == QuestStatus::Active && tracked != q->id) {
            if (ui->Button(("Track " + q->id).c_str())) tracked = q->id;
        }
        ui->Separator();
    }
    if (shown == 0) ui->TextDim(m_journalTab == 0 ? "No active quests." : "Nothing here yet.");
    if (ui->PrimaryButton("Close")) journalOpen = false;
    ui->End();
}

// ---------------------------------------------------------------------------
// QuestTrigger
// ---------------------------------------------------------------------------
void QuestTrigger::Fire(GameObject* who) {
    if (!who || who == gameObject) return;
    if (once && m_fired) return;
    if (!TagMatches(who, triggerTag)) return;
    Scene* s = gameObject ? gameObject->scene() : nullptr;
    QuestLog* log = QuestLog::Get(s, false);
    if (!log) return;
    m_fired = true;
    log->NotifyReach(ZoneName(), who);
    if (!questId.empty() && objective >= 0) log->CompleteObjective(questId, objective);
}

} // namespace okay
