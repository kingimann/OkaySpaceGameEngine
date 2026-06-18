#include "EditorState.hpp"

namespace okay::editor {

EditorState::EditorState() : m_scene("Untitled") {}

GameObject* EditorState::CreateEmpty(const std::string& name) {
    GameObject* go = m_scene.CreateGameObject(name);
    m_selected = go;
    dirty = true;
    return go;
}

GameObject* EditorState::CreateSprite(const std::string& name) {
    GameObject* go = m_scene.CreateGameObject(name);
    auto* sr = go->AddComponent<SpriteRenderer>();
    sr->glyph = '#';
    sr->color = Color::White;
    m_selected = go;
    dirty = true;
    return go;
}

GameObject* EditorState::CreateCamera(const std::string& name) {
    GameObject* go = m_scene.CreateGameObject(name);
    go->AddComponent<Camera>();
    m_selected = go;
    dirty = true;
    return go;
}

void EditorState::DeleteSelected() {
    if (!m_selected) return;
    m_scene.Destroy(m_selected);
    m_scene.Update(0.0f); // flush the destroy queue immediately
    m_selected = nullptr;
    dirty = true;
}

void EditorState::NewScene() {
    if (m_playing) Stop();
    m_scene.Clear();
    m_scene.SetName("Untitled");
    m_selected = nullptr;
    m_path.clear();
    dirty = false;
}

bool EditorState::Save(const std::string& path) {
    if (!SceneSerializer::SaveToFile(m_scene, path)) return false;
    m_path = path;
    dirty = false;
    return true;
}

bool EditorState::Load(const std::string& path, std::string* error) {
    if (m_playing) Stop();
    if (!SceneSerializer::LoadFromFile(m_scene, path, error)) return false;
    m_path = path;
    m_selected = nullptr;
    dirty = false;
    return true;
}

void EditorState::Play() {
    if (m_playing) return;
    m_snapshot = SceneSerializer::Serialize(m_scene); // remember edit state
    m_selected = nullptr;
    m_scene.Start();
    m_playing = true;
}

void EditorState::Stop() {
    if (!m_playing) return;
    m_playing = false;
    SceneSerializer::Deserialize(m_scene, m_snapshot); // restore edit state
    m_selected = nullptr;
}

void EditorState::Tick(float dt) {
    if (m_playing) m_scene.Update(dt);
}

} // namespace okay::editor
