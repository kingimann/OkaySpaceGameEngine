// Our own <filesystem>: path decomposition and normal form, and the file operations in a scratch
// directory under /tmp (made and removed here).
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

static void show(const fs::path &p) {
    std::printf("[%s] name=[%s] stem=[%s] ext=[%s] parent=[%s] abs=%d norm=[%s]\n", p.string().c_str(),
                p.filename().string().c_str(), p.stem().string().c_str(), p.extension().string().c_str(),
                p.parent_path().string().c_str(), p.is_absolute(), p.lexically_normal().string().c_str());
}

int main() {
    const char *paths[] = {"/usr/lib/libx.so.1", "assets/tex/brick.png", "a/b/", ".hidden", "dir/.cfg",
                           "./a/../b/./c.txt", "/", "file", "a//b///c", "../up/x", "a/b/..", ""};
    for (const char *s : paths) show(fs::path(s));
    fs::path j = fs::path("root") / "sub" / "leaf.bin";
    std::printf("%s %s\n", j.string().c_str(), (fs::path("/x") / "/abs").string().c_str());
    fs::path r = j;
    r.replace_extension(".txt");
    std::printf("%s %s\n", r.string().c_str(), fs::path("a/b/c").lexically_relative("a/x").string().c_str());
    std::printf("%s\n", fs::u8path("p/q.json").stem().u8string().c_str());

    fs::path dir = "/tmp/okaycc_fs22_dir";
    std::error_code ec;
    fs::remove_all(dir, ec);
    bool made = fs::create_directories(dir / "deep" / "er", ec);
    std::printf("made %d %d\n", made, (bool)ec);
    std::printf("again %d\n", fs::create_directories(dir / "deep" / "er", ec));
    {
        std::ofstream out((dir / "a.txt").string());
        out << "hello";
    }
    {
        std::ofstream out((dir / "deep" / "b.dat").string(), std::ios::binary);
        out << "0123456789";
    }
    bool e1 = fs::exists(dir / "a.txt"), e2 = fs::exists(dir / "nope"), e3 = fs::exists(dir / "nope", ec);
    std::printf("exists %d %d %d\n", e1, e2, e3);
    std::printf("kinds %d %d %d\n", fs::is_regular_file(dir / "a.txt"), fs::is_directory(dir / "deep"),
                fs::is_directory(dir / "a.txt"));
    std::printf("size %lu %lu\n", (unsigned long)fs::file_size(dir / "a.txt"), (unsigned long)fs::file_size(dir / "deep" / "b.dat"));
    fs::file_size(dir / "missing", ec);
    std::printf("missing %d %d\n", (bool)ec, ec.value());
    fs::rename(dir / "a.txt", dir / "c.txt", ec);
    std::printf("renamed %d %d\n", fs::exists(dir / "c.txt"), fs::exists(dir / "a.txt"));
    fs::copy_file(dir / "c.txt", dir / "d.txt", ec);
    std::printf("copied %d %lu\n", (bool)ec, (unsigned long)fs::file_size(dir / "d.txt"));
    fs::copy_file(dir / "c.txt", dir / "d.txt", ec);
    std::printf("copy again %d\n", ec.value());
    std::vector<std::string> names;
    for (const auto &e : fs::directory_iterator(dir)) names.push_back(e.path().filename().string());
    std::sort(names.begin(), names.end());
    for (const auto &n : names) std::printf("entry %s\n", n.c_str());
    std::vector<std::string> all;
    for (fs::recursive_directory_iterator it(dir), end; it != end; ++it)
        all.push_back(it->path().lexically_relative(dir).string() + (it->is_directory() ? "/" : ""));
    std::sort(all.begin(), all.end());
    for (const auto &n : all) std::printf("tree %s\n", n.c_str());
    std::printf("time %d\n", fs::last_write_time(dir / "c.txt") <= fs::file_time_type::clock::now());
    bool first = fs::remove(dir / "d.txt");
    bool second = fs::remove(dir / "d.txt");
    std::printf("remove %d %d\n", first, second);
    try {
        fs::file_size(dir / "gone");
    } catch (const fs::filesystem_error &e) {
        std::printf("threw %d %s\n", e.code().value(), e.path1().filename().string().c_str());
    }
    unsigned long gone = (unsigned long)fs::remove_all(dir);
    std::printf("removed %lu %d\n", gone, fs::exists(dir));
    std::printf("weak %s\n", fs::weakly_canonical("/tmp/./x/../y").string().c_str());
    return 0;
}
