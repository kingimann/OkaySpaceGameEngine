// A file dragged from the Project panel onto an Inspector slot is stored relative to the project
// (editor/ProjectRelativePath.hpp), and that relative path still opens: in the editor the asset folder is the
// project (EditorState::setProjectDir), in a built game it is the data folder (okay::Paths::FindAsset).
#include "test_framework.hpp"
#include "EditorState.hpp"
#include "ProjectRelativePath.hpp"
#include "okay/Core/Paths.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include <filesystem>
#include <iostream>
#include <string>

using oked::ProjectRelativePath;
using oked::StoredDropPath;
namespace fs = std::filesystem;

static void Expect(const std::string& got, const std::string& want, const char* what) {
    if (got != want) std::cout << "  " << what << ": got \"" << got << "\", want \"" << want << "\"\n";
    CHECK(got == want);
}

static void WindowsPaths() {
    RUN_SUITE("windows paths");
    Expect(ProjectRelativePath("C:\\Games\\Proj", "C:\\Games\\Proj\\Assets\\a.png"), "Assets/a.png", "backslashes");
    Expect(ProjectRelativePath("C:\\Games\\Proj", "c:/games/proj/Assets/x/b.ttf"), "Assets/x/b.ttf", "lower-case drive and folders, forward slashes");
    Expect(ProjectRelativePath("c:/Games/Proj", "C:\\Games\\Proj\\Assets\\x\\b.ttf"), "Assets/x/b.ttf", "lower-case drive on the project");
    Expect(ProjectRelativePath("C:\\Games\\Proj\\", "C:\\Games\\Proj\\Assets\\a.png"), "Assets/a.png", "trailing backslash on the project");
    Expect(ProjectRelativePath("C:/Games/Proj/", "C:/Games/Proj/Assets/a.png"), "Assets/a.png", "trailing slash on the project");
    Expect(ProjectRelativePath("C:\\Games\\Proj", "C:\\Games\\Proj\\Assets\\..\\Assets\\.\\tex\\a.png"), "Assets/tex/a.png", "'..' and '.' inside");
    Expect(ProjectRelativePath("C:\\Games\\Proj\\.", "C:\\Games\\Other\\..\\Proj\\Assets\\a.png"), "Assets/a.png", "'..' that comes back in");
    Expect(ProjectRelativePath("C:\\Games\\Proj", "C:\\Games\\Proj\\\\Assets//a.png"), "Assets/a.png", "doubled separators");
    // outside: unchanged
    Expect(ProjectRelativePath("C:\\Games\\Proj", "C:\\Games\\Other\\a.png"), "C:\\Games\\Other\\a.png", "another folder");
    Expect(ProjectRelativePath("C:\\Games\\Proj", "C:\\Games\\Project2\\a.png"), "C:\\Games\\Project2\\a.png", "a folder whose name starts the same");
    Expect(ProjectRelativePath("C:\\Games\\Proj", "D:\\Games\\Proj\\Assets\\a.png"), "D:\\Games\\Proj\\Assets\\a.png", "another drive");
    Expect(ProjectRelativePath("C:\\Games\\Proj", "C:\\Games\\Proj\\..\\Other\\a.png"), "C:\\Games\\Proj\\..\\Other\\a.png", "'..' that climbs out");
    Expect(ProjectRelativePath("C:\\Games\\Proj", "C:\\Games\\Proj"), "C:\\Games\\Proj", "the project folder itself");
    Expect(ProjectRelativePath("C:\\Games\\Proj", "C:\\Games"), "C:\\Games", "above the project");
}

static void LinuxPaths() {
    RUN_SUITE("linux paths");
    Expect(ProjectRelativePath("/home/me/Proj", "/home/me/Proj/Assets/a.png"), "Assets/a.png", "inside");
    Expect(ProjectRelativePath("/home/me/Proj/", "/home/me/Proj/Assets/fonts/b.ttf"), "Assets/fonts/b.ttf", "trailing slash");
    Expect(ProjectRelativePath("/home/me/Proj", "/home/me/Proj/./Assets/../Assets/a.png"), "Assets/a.png", "'.' and '..'");
    Expect(ProjectRelativePath("/home/me/Proj", "/home/me/proj/Assets/a.png"), "/home/me/proj/Assets/a.png", "case matters on Linux");
    Expect(ProjectRelativePath("/home/me/Proj", "/home/me/Other/a.png"), "/home/me/Other/a.png", "outside");
    Expect(ProjectRelativePath("/home/me/Proj", "/home/me/Proj/../Other/a.png"), "/home/me/Proj/../Other/a.png", "'..' that climbs out");
    Expect(ProjectRelativePath("/home/me/Proj", "C:/home/me/Proj/a.png"), "C:/home/me/Proj/a.png", "a drive path against a Linux project");
}

static void Unchanged() {
    RUN_SUITE("left as it is");
    Expect(ProjectRelativePath("", "C:\\Games\\Proj\\Assets\\a.png"), "C:\\Games\\Proj\\Assets\\a.png", "no project open");
    Expect(ProjectRelativePath("C:\\Games\\Proj", "Assets/a.png"), "Assets/a.png", "already relative");
    Expect(ProjectRelativePath("C:\\Games\\Proj", ""), "", "empty");
}

static void WhatADropStores() {
    RUN_SUITE("what a drop stores");
    Expect(StoredDropPath("C:\\Games\\Proj", "C:\\Games\\Proj\\Assets\\a.PNG"), "Assets/a.PNG", "a picture");
    Expect(StoredDropPath("C:\\Games\\Proj", "C:\\Games\\Proj\\Assets\\f.otf"), "Assets/f.otf", "a font");
    // kinds whose loaders do not look under the asset folder yet keep the full path
    Expect(StoredDropPath("C:\\Games\\Proj", "C:\\Games\\Proj\\Assets\\s.wav"), "C:\\Games\\Proj\\Assets\\s.wav", "a sound");
    Expect(StoredDropPath("C:\\Games\\Proj", "C:\\Games\\Proj\\Assets.png\\noext"), "C:\\Games\\Proj\\Assets.png\\noext", "a dot in a folder name");
}

// End to end: the editor's project folder becomes the asset folder, so the relative path a drop stores opens
// even when the working directory is somewhere else (the launcher starts the editor in its own folder).
static void RelativePathOpens() {
    RUN_SUITE("a stored relative path opens");
    std::error_code ec;
    const fs::path root = fs::temp_directory_path(ec) / "okay_drop_path_relative";
    fs::remove_all(root, ec);
    fs::create_directories(root / "Proj" / "Assets" / "tex", ec);
    fs::create_directories(root / "elsewhere", ec);
    okay::Image img(4, 2);
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x) img.SetPixel(x, y, okay::Color(1.0f, 0.0f, 0.0f, 1.0f));
    const fs::path file = root / "Proj" / "Assets" / "tex" / "red.png";
    CHECK(img.SavePNG(file.string()));

    const fs::path oldCwd = fs::current_path(ec);
    fs::current_path(root / "elsewhere", ec);
    {
        okay::editor::EditorState ed;
        ed.setProjectDir((root / "Proj").string());
        const std::string stored = StoredDropPath(ed.projectDir(), file.string());
        Expect(stored, "Assets/tex/red.png", "what the drop stores");
        Expect(okay::Paths::AssetBaseDir(), (root / "Proj").string(), "the asset folder is the project");
        const std::string found = okay::Paths::FindAsset(stored);
        CHECK(fs::equivalent(fs::path(found), file, ec));
        okay::Image* t = okay::GetCachedTexture(stored);
        CHECK(t != nullptr);
        CHECK(t && t->Width() == 4 && t->Height() == 2);
        ed.setProjectDir("");
        Expect(okay::Paths::FindAsset(stored), stored, "no project: the path as written");
    }
    fs::current_path(oldCwd, ec);
    fs::remove_all(root, ec);
}

int main() {
    WindowsPaths();
    LinuxPaths();
    Unchanged();
    WhatADropStores();
    RelativePathOpens();
    TEST_MAIN_RESULT();
}
