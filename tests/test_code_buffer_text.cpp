// The script editor's text buffers (editor/CodeBufferText.hpp): opening a script and saving it again must never write
// back fewer bytes than were loaded. Before the fix the buffer was a fixed 64 KiB filled with strncpy, so a bigger
// script lost its end and a NUL byte cut the rest, and Save wrote the shorter text with no message.
#include "test_framework.hpp"
#include "CodeBufferText.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using okay::editor::CodeBufferStore;

namespace {

// A plausible script of about `bytes` bytes, with a recognisable last line.
std::string BigScript(std::size_t bytes) {
    std::string s;
    int n = 0;
    while (s.size() < bytes) {
        s += "var v" + std::to_string(n) + " = " + std::to_string(n * 7) + "; // padding line\n";
        ++n;
    }
    s += "function the_end() { print(\"last line\"); }\n";
    return s;
}

std::string ReadBytes(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

void WriteBytes(const std::filesystem::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary);
    f.write(s.data(), (std::streamsize)s.size());
}

void WriteBytesIfAllowed(const CodeBufferStore& store, const void* key, const std::filesystem::path& p) {
    if (store.SaveRefusal(key, p.string()).empty()) WriteBytes(p, store.Peek(key));
}

// What the editor does: open the file into a buffer, then Save writes the buffer back unless the store refuses.
// Returns true when it wrote.
bool OpenThenSave(CodeBufferStore& store, const void* key, const std::filesystem::path& p, const std::string& edit = {}) {
    const std::string loaded = ReadBytes(p);
    store.Get(key, loaded);
    if (!edit.empty()) store.Set(key, std::string(store.Peek(key)) + edit);   // the widget writes the edited text back
    if (!store.SaveRefusal(key, p.string()).empty()) return false;
    WriteBytes(p, store.Peek(key));
    return true;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "okay_code_buffer_text";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    RUN_SUITE("code buffer: a script over 64 KiB keeps every byte");
    {
        const std::string text = BigScript(100 * 1024);
        CHECK(text.size() > 100 * 1024);
        const fs::path p = dir / "big.okay";
        WriteBytes(p, text);

        CodeBufferStore store;
        int key = 0;
        CHECK(OpenThenSave(store, &key, p));
        const std::string after = ReadBytes(p);
        CHECK(after.size() == text.size());
        CHECK(after == text);
        CHECK(!store.LosesBytes(&key));

        // Add one character and save: the file grows by exactly one byte.
        CodeBufferStore store2;
        int key2 = 0;
        CHECK(OpenThenSave(store2, &key2, p, "x"));
        const std::string grown = ReadBytes(p);
        CHECK(grown.size() == text.size() + 1);
        CHECK(grown == text + "x");
    }

    RUN_SUITE("code buffer: a 1 MiB script, and replacing a big text with a small one");
    {
        CodeBufferStore store;
        int key = 0;
        const std::string huge = BigScript(1024 * 1024);
        store.Set(&key, huge);
        CHECK(std::string(store.Peek(&key)) == huge);
        store.Set(&key, "short");
        CHECK(std::string(store.Peek(&key)) == "short");     // no tail of the old text left behind
        store.Set(&key, huge + "more");
        CHECK(std::string(store.Peek(&key)) == huge + "more");
    }

    RUN_SUITE("code buffer: a NUL byte in the file is never written back cut");
    {
        std::string text = "function start() {\n  print(\"before\");\n}\n";
        text.push_back('\0');
        text += "function after_the_nul() { print(\"after\"); }\n";
        const fs::path p = dir / "nul.okay";
        WriteBytes(p, text);

        CodeBufferStore store;
        int key = 0;
        const bool wrote = OpenThenSave(store, &key, p);
        const std::string after = ReadBytes(p);
        CHECK(after == text);                                 // the bytes on disk are unchanged
        CHECK(!wrote);                                        // because the save was refused
        CHECK(store.LosesBytes(&key));                        // the editor shows it read-only
        const std::string why = store.SaveRefusal(&key, p.string());
        CHECK(why.find("NUL") != std::string::npos && why.find(p.string()) != std::string::npos);

        // The code widget writes its text back, already cut at the NUL: the refusal must stay.
        store.Set(&key, std::string(store.Peek(&key)) + "typed");
        CHECK(store.LosesBytes(&key));
        CHECK(!store.SaveRefusal(&key, p.string()).empty());
        WriteBytesIfAllowed(store, &key, p);
        CHECK(ReadBytes(p) == text);

        // A different script in the same store is not affected.
        int other = 0;
        store.Get(&other, "print(1)\n");
        CHECK(!store.LosesBytes(&other));
        // Closing the tab forgets the buffer and its mark; a fresh open of a clean file saves normally.
        store.Erase(&key);
        CHECK(store.Peek(&key) == nullptr && !store.LosesBytes(&key));
        const fs::path clean = dir / "clean.okay";
        WriteBytes(clean, "print(2)\n");
        CHECK(OpenThenSave(store, &key, clean, "x"));
        CHECK(ReadBytes(clean) == "print(2)\nx");
    }

    RUN_SUITE("code buffer: small scripts behave as before");
    {
        CodeBufferStore store;
        int key = 0;
        std::vector<char>& b = store.Get(&key, "print(1)\n");
        CHECK(b.size() >= CodeBufferStore::kMinBytes);
        CHECK(std::string(store.Peek(&key)) == "print(1)\n");
        store.Get(&key, "ignored, the buffer already exists");
        CHECK(std::string(store.Peek(&key)) == "print(1)\n");
        store.Set(&key, "");
        CHECK(std::string(store.Peek(&key)).empty());
        // Exactly 64 KiB of text (the old buffer kept only 65,535 of these bytes).
        const std::string edge(CodeBufferStore::kMinBytes, 'a');
        store.Set(&key, edge);
        CHECK(std::string(store.Peek(&key)).size() == edge.size());
    }

    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
