#pragma once
// A minimal stand-in for editor/EditorState.hpp, used ONLY by the okayui port tests that run editor panels headless (put first on the include path).
// The real class owns the Scene, the Play loop, the undo stack and more of the engine; a panel test that only needs the project folder, the open file
// path and a few file operations uses this one. It implements just what the panel under test calls, with in-memory behaviour a test can inspect.
// See docs/ui/own_ui_toolkit.md 6.17.
#include <Okay.hpp>   // what the real header pulls in (components, scene, game), so the panel code that includes it compiles
#include "okay/Core/Game.hpp"
#include <string>
#include <vector>

namespace okay { namespace editor {
class EditorState {
public:
    bool dirty = false;
    const std::string& path() const { return m_path; }
    const std::string& projectDir() const { return m_projectDir; }
    bool isPlaying() const { return m_playing; }
    void NewScene() { m_path.clear(); dirty = false; ++newScenes; }
    bool Save(const std::string& p, std::string* error = nullptr) { (void)error; m_path = p; dirty = false; ++saves; return true; }
    bool Load(const std::string& p, std::string* error = nullptr) { if (p.empty()) { if (error) *error = "empty path"; return false; } m_path = p; dirty = false; ++loads; return true; }
    void FollowMovedFile(const std::string& newPath) { m_path = newPath; }
    // test hooks
    void SetProjectDir(const std::string& d) { m_projectDir = d; }
    void SetPlaying(bool p) { m_playing = p; }
    int newScenes = 0, saves = 0, loads = 0;
private:
    std::string m_path, m_projectDir;
    bool m_playing = false;
};
}}
