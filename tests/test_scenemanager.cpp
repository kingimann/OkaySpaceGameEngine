#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>
#include <fstream>

using namespace okay;

int main() {
    RUN_SUITE("scenemanager");

    SceneManager::ClearScenes();
    CHECK(SceneManager::SceneCount() == 0);
    CHECK(SceneManager::ActiveIndex() == -1);

    // Build list: add scenes, dedupe exact paths, look up by index/name.
    int i0 = SceneManager::AddScene("Assets/Menu.okayscene");
    int i1 = SceneManager::AddScene("Assets/Level1.okayscene");
    int i2 = SceneManager::AddScene("Assets/Level2.okayscene");
    CHECK(i0 == 0 && i1 == 1 && i2 == 2);
    CHECK(SceneManager::AddScene("Assets/Level1.okayscene") == 1);   // dedupe
    CHECK(SceneManager::SceneCount() == 3);
    CHECK(SceneManager::IndexOf("Level2") == 2);                     // by name
    CHECK(SceneManager::IndexOf("Assets/Menu.okayscene") == 0);      // by path
    CHECK(SceneManager::IndexOf("Nope") == -1);
    CHECK(SceneManager::SceneName(1) == "Level1");

    // Write two real scene files so loads can resolve at end-of-frame.
    {
        Scene a("Menu");  a.CreateGameObject("MenuRoot");
        Scene b("Level1"); b.CreateGameObject("Hero");
        SceneSerializer::SaveToFile(a, "Menu.okayscene");
        SceneSerializer::SaveToFile(b, "Level1.okayscene");
    }
    SceneManager::ClearScenes();
    SceneManager::AddScene("Menu.okayscene");
    SceneManager::AddScene("Level1.okayscene");

    // LoadScene defers via RequestLoad; the swap happens on the next Update.
    Scene scene("boot");
    scene.CreateGameObject("Placeholder");
    CHECK(SceneManager::LoadScene(scene, 0));
    CHECK(scene.HasPendingLoad());
    CHECK(SceneManager::ActiveIndex() == 0);
    scene.Update(0.0f);                 // applies the deferred load
    CHECK(scene.Find("MenuRoot") != nullptr);

    // Load the next scene by wrapping the build list.
    CHECK(SceneManager::LoadNextScene(scene));
    CHECK(SceneManager::ActiveIndex() == 1);
    scene.Update(0.0f);
    CHECK(scene.Find("Hero") != nullptr);

    // Wrap back to index 0.
    CHECK(SceneManager::LoadNextScene(scene));
    CHECK(SceneManager::ActiveIndex() == 0);

    // By-name load resolves through the registry.
    CHECK(SceneManager::LoadSceneByName(scene, "Level1"));
    CHECK(SceneManager::ActiveIndex() == 1);

    // Out-of-range index fails cleanly.
    CHECK(!SceneManager::LoadScene(scene, 9));

    std::remove("Menu.okayscene");
    std::remove("Level1.okayscene");
    TEST_MAIN_RESULT();
}
