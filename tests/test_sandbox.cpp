// The test sandbox (tests/test_sandbox.hpp) that keeps tests from writing into the source tree: relative
// and save-folder writes land in its temp folder, the folder is removed afterwards, and Verify() catches a
// write into the guarded tree (new, rewritten or removed file).
#include "test_framework.hpp"
#include "test_sandbox.hpp"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static void Write(const fs::path& p, const std::string& text) { std::ofstream(p) << text; }

int main() {
    RUN_SUITE("sandbox");
    const fs::path cwd = fs::current_path();
    const std::string oldBase = okay::Save::BaseDir();
    fs::path tree = fs::temp_directory_path() / "okay_sandbox_selftest_tree";
    fs::remove_all(tree);
    fs::create_directories(tree / "saves");
    Write(tree / "keep.txt", "one");
    Write(tree / "saves" / "slot.okaygame", "slot");
    fs::path inside;
    {
        okaytest::Sandbox sb("selftest", tree.string());
        inside = sb.Dir();
        CHECK(fs::exists(inside) && fs::equivalent(fs::current_path(), inside));
        CHECK(okay::Save::Resolve("x") == inside.generic_string() + "/x");
        CHECK(sb.Verify());                                             // nothing touched yet
        Write("stray", "relative write");                              // lands in the sandbox
        CHECK(fs::exists(inside / "stray") && !fs::exists(tree / "stray") && sb.Verify());
        Write(tree / "new.txt", "x");                                  // a new top-level file in the tree
        CHECK(!sb.Verify());
        fs::remove(tree / "new.txt");
        CHECK(sb.Verify());
        Write(tree / "keep.txt", "changed and longer");                 // a rewritten file
        CHECK(!sb.Verify());
        Write(tree / "keep.txt", "one");
        Write(tree / "saves" / "autosave.okaygame", "x");              // a new save slot
        CHECK(!sb.Verify());
        fs::remove(tree / "saves" / "autosave.okaygame");
        fs::remove(tree / "saves" / "slot.okaygame");                  // a removed file
        CHECK(!sb.Verify());
    }
    CHECK(!fs::exists(inside));                                         // the temp folder is gone
    CHECK(fs::equivalent(fs::current_path(), cwd));                    // and the working folder and save folder are back
    CHECK(okay::Save::BaseDir() == oldBase);
    fs::remove_all(tree);
    TEST_MAIN_RESULT();
}
