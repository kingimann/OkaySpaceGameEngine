// okay::CopyFileOver (engine/include/okay/Core/FileCopy.hpp): copy over an existing file, onto a new file, from a
// missing source, onto a folder (the fallback path that copies beside the target and renames, which must clean up and
// leave the target as it was), and the throwing form. Header only: no engine link.
// The MinGW "file exists" refusal itself only shows on Windows; this checks the promise on every platform:
// the target ends up a full copy of the source, or it is left exactly as it was and the error says why.
#include "test_framework.hpp"

#include "okay/Core/FileCopy.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
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

    {   // more cases: folder source, missing folder, self copy, stale temp, UTF-8 names, empty, 5 MB, read-only
    const fs::path d = dir / "more";
    fs::create_directories(d, ec);
    auto NoLeftovers = [](const fs::path& p) { for (auto& e : fs::directory_iterator(p)) if (e.path().extension() == ".copytmp") return false; return true; };
    WriteText(d / "keep.txt", "keep me");
    // a source that is a folder fails and leaves the target intact
    fs::create_directories(d / "folder");
    CHECK(!okay::CopyFileOver(d / "folder", d / "keep.txt", ec));
    CHECK(ReadText(d / "keep.txt") == "keep me");
    CHECK(NoLeftovers(d));

    // target folder missing is reported
    CHECK(!okay::CopyFileOver(src, d / "no_such_dir" / "x.txt", ec));
    CHECK(ec);
    CHECK(!fs::exists(d / "no_such_dir"));

    // onto itself: the file survives whole
    WriteText(d / "self.txt", "self content");
    okay::CopyFileOver(d / "self.txt", d / "self.txt", ec);
    CHECK(ReadText(d / "self.txt") == "self content");
    CHECK(NoLeftovers(d));

    // a stale temp file from a crashed copy does not block the copy
    WriteText(d / "c.txt", "c-old");
    WriteText(d / "c.txt.copytmp", "stale");
    CHECK(okay::CopyFileOver(src, d / "c.txt", ec) && ReadText(d / "c.txt") == "new content");
    CHECK(NoLeftovers(d));

    // spaces and non-ASCII (UTF-8) names
    const fs::path u1 = d / fs::u8path("my file \xC3\xA9\xE6\x97\xA5\xE6\x9C\xAC.txt");
    const fs::path u2 = d / fs::u8path("dest \xC3\xBC ber.txt");
    WriteText(u1, "unicode");
    WriteText(u2, "old");
    CHECK(okay::CopyFileOver(u1, u2, ec) && ReadText(u2) == "unicode");

    // empty file over a full one
    WriteText(d / "empty.bin", "");
    WriteText(d / "full.bin", "something");
    CHECK(okay::CopyFileOver(d / "empty.bin", d / "full.bin", ec) && fs::file_size(d / "full.bin") == 0);

    // 5 MB file over a small one, byte for byte
    std::string big(5 * 1024 * 1024, 'x');
    for (size_t i = 0; i < big.size(); i += 4099) big[i] = char(i / 4099);
    WriteText(d / "big.bin", big);
    WriteText(d / "bigdest.bin", "tiny");
    CHECK(okay::CopyFileOver(d / "big.bin", d / "bigdest.bin", ec) && ReadText(d / "bigdest.bin") == big);

    // throwing form
    bool threw = false;
    try { okay::CopyFileOver(d / "missing.txt", d / "keep.txt"); } catch (const fs::filesystem_error&) { threw = true; }
    CHECK(threw);
    okay::CopyFileOver(src, d / "keep.txt");
    CHECK(ReadText(d / "keep.txt") == "new content");

#if !defined(_WIN32)
    // read-only target file: replaced (a rename needs the folder, not the file)
    WriteText(d / "ro.txt", "readonly old");
    fs::permissions(d / "ro.txt", fs::perms::owner_read);
    CHECK(okay::CopyFileOver(src, d / "ro.txt", ec) && ReadText(d / "ro.txt") == "new content");

    // read-only destination folder (skipped when the OS ignores it, e.g. running as root): old target intact
    fs::create_directories(d / "rodir");
    WriteText(d / "rodir" / "t.txt", "old in rodir");
    fs::permissions(d / "rodir", fs::perms::owner_read | fs::perms::owner_exec);
    bool blocked = false;
    { std::ofstream probe(d / "rodir" / "probe"); blocked = !probe.is_open(); }
    if (blocked) {
        CHECK(!okay::CopyFileOver(src, d / "rodir" / "t.txt", ec));
        CHECK(ReadText(d / "rodir" / "t.txt") == "old in rodir");
        CHECK(NoLeftovers(d / "rodir"));
    }
    fs::permissions(d / "rodir", fs::perms::owner_all);
#endif

    }

    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
