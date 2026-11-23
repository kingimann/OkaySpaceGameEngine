// okay::CopyFileOver (engine/include/okay/Core/FileCopy.hpp): copy over an existing file, onto a new file, from a
// missing source, onto a folder (the fallback path that copies beside the target and renames, which must clean up and
// leave the target as it was), and the throwing form. Header only: no engine link.
// The MinGW "file exists" refusal itself only shows on Windows; this checks the promise on every platform:
// the target ends up a full copy of the source, or it is left exactly as it was and the error says why.
#include "test_framework.hpp"

#include "okay/Core/FileCopy.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

static void WriteText(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}
static std::string ReadText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

int main() {
    RUN_SUITE("CopyFileOver");
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / "okay_test_file_copy";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path src = dir / "source.txt";
    WriteText(src, "new content");

    {   // the target exists (longer, so a copy that did not truncate would leave a tail): fully replaced
        const fs::path to = dir / "existing.txt";
        WriteText(to, "old content that is longer than the new one");
        CHECK(okay::CopyFileOver(src, to, ec));
        CHECK(!ec);
        CHECK(ReadText(to) == "new content");
        CHECK(!fs::exists(dir / "existing.txt.copytmp"));
        // and again over the copy it just made
        CHECK(okay::CopyFileOver(src, to, ec));
        CHECK(ReadText(to) == "new content");
    }
    {   // the target is missing: created
        const fs::path to = dir / "fresh.txt";
        CHECK(okay::CopyFileOver(src, to, ec));
        CHECK(!ec);
        CHECK(ReadText(to) == "new content");
    }
    {   // the source is missing: false, an error, the existing target left as it was, nothing new created
        const fs::path to = dir / "keep.txt";
        WriteText(to, "keep me");
        CHECK(!okay::CopyFileOver(dir / "no_such_file.txt", to, ec));
        CHECK(static_cast<bool>(ec));
        CHECK(ReadText(to) == "keep me");
        CHECK(!fs::exists(dir / "keep.txt.copytmp"));
        CHECK(!okay::CopyFileOver(dir / "no_such_file.txt", dir / "never.txt", ec));
        CHECK(static_cast<bool>(ec));
        CHECK(!fs::exists(dir / "never.txt"));
    }
    {   // the target is a folder: the plain copy fails, the fallback copies beside it, the rename fails too;
        // the folder and what is in it stay, and the temp copy is removed
        const fs::path to = dir / "a_folder";
        fs::create_directories(to, ec);
        WriteText(to / "inside.txt", "inside");
        CHECK(!okay::CopyFileOver(src, to, ec));
        CHECK(static_cast<bool>(ec));
        CHECK(fs::is_directory(to));
        CHECK(ReadText(to / "inside.txt") == "inside");
        CHECK(!fs::exists(dir / "a_folder.copytmp"));
    }
    {   // the throwing form: copies over a file, throws filesystem_error naming both paths on failure
        const fs::path to = dir / "thrown.txt";
        WriteText(to, "old");
        bool threw = false;
        try { okay::CopyFileOver(src, to); } catch (const fs::filesystem_error&) { threw = true; }
        CHECK(!threw);
        CHECK(ReadText(to) == "new content");
        threw = false;
        try {
            okay::CopyFileOver(dir / "no_such_file.txt", to);
        } catch (const fs::filesystem_error& e) {
            threw = true;
            CHECK(e.path2() == to);
            CHECK(static_cast<bool>(e.code()));
        }
        CHECK(threw);
        CHECK(ReadText(to) == "new content");
    }

    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
