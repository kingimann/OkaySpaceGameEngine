#include "okay/Components/DialogueRunner.hpp"
#include "okay/Audio/AudioLoad.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/Character.hpp"       // AssetBaseDir
#include "okay/Components/GridInventory.hpp"
#include "okay/Components/HoldPrompt.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/InventoryPlayer.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/StoryState.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIAnchor.hpp"        // UICanvas
#include "okay/Core/Log.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Inventory/InventoryEvents.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scripting/ScriptUI.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace okay {

// ---------------------------------------------------------------------------
// The host: story variables, items and quests from the scene
// ---------------------------------------------------------------------------
vs::VsValue DialogueRunner::SceneHost::GetVar(const std::string& name) {
    StoryState* st = StoryState::Get(r->gameObject ? r->gameObject->scene() : nullptr, false);
    return st ? st->Get(name) : vs::VsValue{};
}
void DialogueRunner::SceneHost::SetVar(const std::string& name, const vs::VsValue& v) {
    if (StoryState* st = StoryState::Get(r->gameObject ? r->gameObject->scene() : nullptr, true)) st->Set(name, v);
}
bool DialogueRunner::SceneHost::HasFlag(const std::string& name) {
    StoryState* st = StoryState::Get(r->gameObject ? r->gameObject->scene() : nullptr, false);
    return st && st->HasFlag(name);
}
void DialogueRunner::SceneHost::SetFlag(const std::string& name, bool on) {
    if (StoryState* st = StoryState::Get(r->gameObject ? r->gameObject->scene() : nullptr, true)) st->SetFlag(name, on);
}
std::string DialogueRunner::SceneHost::QuestState(const std::string& id) {
    QuestLog* q = QuestLog::Get(r->gameObject ? r->gameObject->scene() : nullptr, false);
    return q ? q->StatusName(id) : "inactive";
}
int DialogueRunner::SceneHost::ItemCount(const std::string& item) {
    GameObject* p = r->Player();
    if (!p) return 0;
    int n = 0;
    if (Inventory* inv = ItemPickup::InventoryOf(p)) n += inv->Count(item);
    if (auto* grid = p->GetComponent<GridInventory>())
        for (const auto& it : grid->items) if (it.name == item) n += it.count;
    return n;
}
bool DialogueRunner::SceneHost::GiveItem(const std::string& item, int count) {
    GameObject* p = r->Player();
    if (!p || item.empty() || count <= 0) return false;
    if (Inventory* inv = ItemPickup::InventoryOf(p)) return inv->AddPartial(item, count) == 0;
    if (auto* grid = p->GetComponent<GridInventory>()) return grid->AddDefined(item, count) >= 0;
    return false;
}
bool DialogueRunner::SceneHost::TakeItem(const std::string& item, int count) {
    GameObject* p = r->Player();
    if (!p || item.empty() || count <= 0) return false;
    if (Inventory* inv = ItemPickup::InventoryOf(p)) return inv->Remove(item, count);
    if (auto* grid = p->GetComponent<GridInventory>()) {
        int left = count;
        for (int i = (int)grid->items.size() - 1; i >= 0 && left > 0; --i) {
            if (grid->items[i].name != item) continue;
            const int take = std::min(left, grid->items[i].count);
            grid->items[i].count -= take; left -= take;
            if (grid->items[i].count <= 0) grid->RemoveAt(i);
        }
        return left <= 0;
    }
    return false;
}
void DialogueRunner::SceneHost::StartQuest(const std::string& id) {
    if (QuestLog* q = QuestLog::Get(r->gameObject ? r->gameObject->scene() : nullptr, true)) q->StartQuest(id);
}
void DialogueRunner::SceneHost::CompleteObjective(const std::string& id, int index) {
    if (QuestLog* q = QuestLog::Get(r->gameObject ? r->gameObject->scene() : nullptr, true)) q->CompleteObjective(id, index);
}
void DialogueRunner::SceneHost::Event(const std::string& name) {
    r->events.push_back(name);
    if (r->onEvent) r->onEvent(name);
    const std::vector<vs::VsValue> args{vs::VsValue(name)};
    InventoryEvents::Fire(r->gameObject, "on_dialogue_event", "OnDialogueEvent", args);
    if (GameObject* p = r->Player(); p && p != r->gameObject) InventoryEvents::Fire(p, "on_dialogue_event", "OnDialogueEvent", args);
}
void DialogueRunner::SceneHost::Sound(const std::string& path) {
    auto* au = r->gameObject ? r->gameObject->GetComponent<AudioSource>() : nullptr;
    if (!au) return;
    const std::string file = r->ResolveAsset(path);
    if (au->clipPath != file) {
        auto clip = LoadAudioFile(file);
        if (!clip) return;
        au->clip = *clip;
        au->clipPath = file;
    }
    au->Play();
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------
void DialogueRunner::Start() {
    m_host.r = this;
    if (!graphFile.empty() && (!m_loaded || m_loadedPath != ResolvedGraphFile())) {
        std::string err;
        if (!LoadGraph(&err)) Log::Warning("[dialogue] ", gameObject ? gameObject->name : std::string(), ": ", err);
    }
}

std::string DialogueRunner::ResolveAsset(const std::string& path) const {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p(path);
    if (path.empty() || p.is_absolute() || Character::AssetBaseDir().empty() || fs::exists(p, ec)) return path;
    return (fs::path(Character::AssetBaseDir()) / p).string();
}
std::string DialogueRunner::ResolvedGraphFile() const { return ResolveAsset(graphFile); }

bool DialogueRunner::LoadGraph(std::string* err) {
    const std::string path = ResolvedGraphFile();
    DialogueGraph g;
    if (!g.Load(path, err)) return false;
    m_graph = g;
    m_loaded = true;
    m_loadedPath = path;
    return true;
}

std::string DialogueRunner::GraphName() const {
    if (!m_graph.name.empty()) return m_graph.name;
    return std::filesystem::path(graphFile).stem().string();
}

GameObject* DialogueRunner::Player() const {
    return FindInventoryPlayer(gameObject ? gameObject->scene() : nullptr, playerName);
}

bool DialogueRunner::InRange() const {
    GameObject* p = Player();
    if (!p || !p->transform || !gameObject || !gameObject->transform) return false;
    return (p->transform->Position() - gameObject->transform->Position()).Magnitude() <= interactRange;
}

DialogueRunner* DialogueRunner::ActiveIn(Scene* scene) {
    if (!scene) return nullptr;
    for (DialogueRunner* r : scene->FindObjectsOfType<DialogueRunner>()) if (r && r->IsActive()) return r;
    return nullptr;
}

DialogueRunner* DialogueRunner::FindByName(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    if (GameObject* go = scene->Find(name)) if (auto* r = go->GetComponent<DialogueRunner>()) return r;
    for (DialogueRunner* r : scene->FindObjectsOfType<DialogueRunner>()) {
        if (!r) continue;
        if (r->GraphName() == name || r->graphFile == name) return r;
        if (std::filesystem::path(r->graphFile).stem().string() == std::filesystem::path(name).stem().string() && !name.empty()) return r;
    }
    return nullptr;
}

bool DialogueRunner::StartDialogue(int nodeId) {
    m_host.r = this;
    if (!m_loaded && !graphFile.empty()) {
        std::string err;
        if (!LoadGraph(&err)) { Log::Warning("[dialogue] ", err); return false; }
    }
    if (m_graph.nodes.empty()) return false;
    if (IsActive()) Stop();
    events.clear();
    m_session.Begin(&m_graph, &m_host, nodeId >= 0 ? nodeId : startNode);
    if (!m_session.Active()) { Ended(); return false; }
    m_wasActive = true;
    SetPrompt("");
    Present();
    return true;
}

void DialogueRunner::Present() {
    const DialogueNode* n = m_session.Node();
    m_lineId = n ? n->id : -1;
    m_shown = typewriterSpeed <= 0.0f ? 1e9f : 0.0f;
    m_lineTime = 0.0f;
}

void DialogueRunner::Advance() {
    if (!IsActive()) return;
    const DialogueNode* n = m_session.Node();
    if (n && n->kind == DialogueNode::Kind::Line && m_shown < (float)n->text.size()) { m_shown = 1e9f; return; }
    m_session.Advance();
    if (m_session.Active()) Present(); else Ended();
}

bool DialogueRunner::Choose(int index) {
    if (!IsActive() || !m_session.AtChoice()) return false;
    if (!m_session.Choose(index)) return false;
    if (m_session.Active()) Present(); else Ended();
    return true;
}

void DialogueRunner::Stop() {
    if (!m_session.Active()) return;
    m_session.Stop();
    Ended();
}

void DialogueRunner::Ended() {
    if (!m_wasActive) return;
    m_wasActive = false;
    m_lineId = -1;
    Scene* s = gameObject ? gameObject->scene() : nullptr;
    if (QuestLog* q = QuestLog::Get(s, false)) q->NotifyDialogueEnd(gameObject ? gameObject->name : std::string(), GraphName());
    const std::vector<vs::VsValue> args{vs::VsValue(GraphName())};
    InventoryEvents::Fire(gameObject, "on_dialogue_end", "OnDialogueEnd", args);
    if (GameObject* p = Player(); p && p != gameObject) InventoryEvents::Fire(p, "on_dialogue_end", "OnDialogueEnd", args);
    if (onEnd) onEnd();
}

DialogueRunner::Line DialogueRunner::CurrentLine() const {
    Line l;
    const DialogueNode* n = m_session.Node();
    if (!m_session.Active() || !n) return l;
    l.nodeId = n->id;
    if (n->kind == DialogueNode::Kind::Choice) {
        l.choice = true;
        l.complete = true;
        for (const auto& c : m_session.Choices()) l.choices.push_back(c.text);
        // A choice shows the line before it (the question) when one led here.
        const auto& trail = m_session.Trail();
        for (std::size_t i = trail.size(); i-- > 0;) {
            const DialogueNode* prev = m_graph.Find(trail[i]);
            if (prev && prev->kind == DialogueNode::Kind::Line) { l.speaker = prev->speaker; l.text = prev->text; l.portrait = prev->portrait; break; }
        }
        l.shownText = l.text;
        return l;
    }
    l.speaker = n->speaker; l.text = n->text; l.portrait = n->portrait;
    const std::size_t shown = std::min(n->text.size(), (std::size_t)std::max(0.0f, m_shown));
    l.shownText = n->text.substr(0, shown);
    l.complete = shown >= n->text.size();
    return l;
}

void DialogueRunner::SetPrompt(const std::string& text) {
    if (text == m_promptShown) return;
    Scene* s = gameObject ? gameObject->scene() : nullptr;
    if (!s) return;
    if (s->FindObjectOfType<HoldPrompt>()) {
        std::string& extra = HoldPrompt::ExtraPrompt();
        if (text.empty() && extra != m_promptShown) { m_promptShown.clear(); return; }   // another runner owns it now
        extra = text;
    } else if (GameObject* label = s->Find(promptObject)) {
        if (auto* tr = label->GetComponent<TextRenderer>()) {
            if (text.empty() && tr->text != m_promptShown) { m_promptShown.clear(); return; }
            tr->text = text;
            tr->background = !text.empty();
        }
    }
    m_promptShown = text;
}

void DialogueRunner::Update(float dt) {
    m_host.r = this;
    if (!m_loaded && !graphFile.empty()) {
        std::string err;
        if (!LoadGraph(&err)) { m_loaded = true; Log::Warning("[dialogue] ", err); }
    }
    if (IsActive()) {
        if (pausePlayer) Input::SetUICaptured(true);
        ItemPickup::BlockInteract();   // E advances the conversation; no crate takes the press
        const DialogueNode* n = m_session.Node();
        if (n && n->kind == DialogueNode::Kind::Line) {
            const float len = (float)n->text.size();
            if (m_shown < len) m_shown = std::min(len, m_shown + typewriterSpeed * dt);
            else {
                m_lineTime += dt;
                const float wait = n->autoAdvance > 0.0f ? n->autoAdvance : autoAdvance;
                if (wait > 0.0f && m_lineTime >= wait) { Advance(); if (!IsActive()) return; }
            }
        }
        HandleInput();
        if (!IsActive()) return;
        if (drawUI) DrawUI();
        return;
    }
    if (m_wasActive) Ended();
    // Not talking: the prompt and the interact key.
    const bool near = startMode == 0 && interactKey && enabled && InRange();
    if (near) {
        SetPrompt(promptText.empty() ? Input::KeyName(interactKey) + " to talk" : promptText);
        if (Input::GetKeyDown(interactKey) && !Input::UICaptured()) {
            ItemPickup::BlockInteract();
            StartDialogue();
            if (IsActive() && drawUI) DrawUI();
        }
    } else SetPrompt("");
}

void DialogueRunner::HandleInput() {
    const DialogueNode* n = m_session.Node();
    if (!n) return;
    if (canCancel && Input::GetKeyDown((char)27)) { Stop(); return; }
    if (n->kind == DialogueNode::Kind::Choice) {
        for (int i = 0; i < (int)m_session.Choices().size() && i < 6; ++i)
            if (Input::GetKeyDown((char)('1' + i))) { Choose(i); return; }
        return;
    }
    const bool confirm = (interactKey && Input::GetKeyDown(interactKey)) || Input::GetKeyDown('\r') ||
                         Input::GetKeyDown(' ') || Input::GetMouseButtonDown(0);
    if (confirm) Advance();
}

void DialogueRunner::DrawUI() {
    ScriptUIBridge* ui = GetScriptUI();
    if (!ui) return;
    const Line l = CurrentLine();
    if (l.nodeId < 0) return;
    const float W = ScriptUIWidth(UICanvas::Width()), H = ScriptUIHeight(UICanvas::Height());
    const int nChoices = l.choice ? (int)l.choices.size() : 0;
    const float w = std::min(760.0f, std::max(320.0f, W - 32.0f));
    const float h = 150.0f + 40.0f * (float)nChoices;
    const float x = ScriptUIOriginX() + (W - w) * 0.5f, y = ScriptUIOriginY() + H - h - 20.0f;
    const std::string title = l.speaker.empty() ? std::string("Dialogue") : l.speaker;
    ui->BeginAt(title.c_str(), x, y, w, h);
    bool drewPortrait = false;
    if (!l.portrait.empty()) drewPortrait = ui->Image(ResolveAsset(l.portrait).c_str(), 64.0f, 64.0f);
    if (!drewPortrait) ui->Avatar(l.speaker.empty() ? "?" : l.speaker.c_str(), 48.0f);
    ui->SameLine();
    ui->TextWrapped(l.shownText.c_str());
    if (l.choice) {
        for (std::size_t i = 0; i < l.choices.size(); ++i) {
            const std::string label = std::to_string(i + 1) + ". " + l.choices[i];
            if (ui->PrimaryButton(label.c_str())) { Choose((int)i); break; }
        }
    } else {
        const std::string hint = l.complete ? Input::KeyName(interactKey ? interactKey : '\r') + " / click to continue" : "...";
        ui->TextDim(hint.c_str());
    }
    ui->End();
}

bool StoryUIModal(const GameObject* go) {
    if (!go) return false;
    if (auto* r = go->GetComponent<DialogueRunner>()) if (r->IsActive() && r->pausePlayer) return true;
    if (auto* q = go->GetComponent<QuestLog>()) if (q->journalOpen) return true;
    return false;
}

} // namespace okay
