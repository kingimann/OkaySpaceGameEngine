#include "link.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace okaycc {

// Encoded from the Intel SDM vol. 2 (MOV 8B/89, LEA 8D, REX, ModRM and SIB bytes).
const unsigned char kStartArgs[14] = {
    0x48, 0x8B, 0x1C, 0x24,        // mov rbx, [rsp]
    0x4C, 0x8D, 0x64, 0x24, 0x08,  // lea r12, [rsp + 8]
    0x4D, 0x8D, 0x6C, 0xDC, 0x08,  // lea r13, [r12 + rbx * 8 + 8]
};
const unsigned char kMainArgs[8] = {
    0x89, 0xDF,        // mov edi, ebx
    0x4C, 0x89, 0xE6,  // mov rsi, r12
    0x4C, 0x89, 0xEA,  // mov rdx, r13
};

namespace {

constexpr std::uint64_t kBase = 0x400000;
constexpr std::uint64_t kLoad = 64 + 56;

std::uint64_t rd(const std::vector<unsigned char>& b, std::size_t at, int n) {
    std::uint64_t v = 0;
    for (int i = 0; i < n; ++i) v |= static_cast<std::uint64_t>(b[at + static_cast<std::size_t>(i)]) << (8 * i);
    return v;
}

void wr(std::vector<unsigned char>& b, std::size_t at, std::uint64_t v, int n) {
    for (int i = 0; i < n; ++i) b[at + static_cast<std::size_t>(i)] = static_cast<unsigned char>((v >> (8 * i)) & 0xffu);
}

struct Sym {
    std::string name;
    unsigned bind = 0;
    unsigned type = 0;
    std::uint16_t shndx = 0;
    std::uint64_t value = 0;
    std::uint64_t size = 0;
};

struct Rela {
    int sec = 0;
    std::uint64_t offset = 0;
    std::uint32_t sym = 0;
    std::uint32_t type = 0;
    std::int64_t addend = 0;
};

struct Obj {
    std::vector<unsigned char> text, ro, data;
    std::vector<Sym> syms;
    std::vector<Rela> relas;
    std::uint64_t textVa = 0;
    std::uint64_t roVa = 0;
    std::uint64_t dataVa = 0;
    int textId = 0;
    int roId = 0;
    int dataId = 0;
};

std::string cstr(const std::vector<unsigned char>& b, std::size_t at) {
    std::string s;
    while (at < b.size() && b[at]) s.push_back(static_cast<char>(b[at++]));
    return s;
}

bool parseObj(const std::vector<unsigned char>& b, Obj& o, std::string& error) {
    if (b.size() < 64 || b[0] != 0x7f || b[1] != 'E' || b[2] != 'L' || b[3] != 'F' || b[4] != 2) {
        error = "not an ELF64 object";
        return false;
    }
    if (rd(b, 16, 2) != 1 || rd(b, 18, 2) != 62) {
        error = "not a relocatable x86-64 object";
        return false;
    }
    std::uint64_t shoff = rd(b, 40, 8);
    int shentsize = static_cast<int>(rd(b, 58, 2));
    int shnum = static_cast<int>(rd(b, 60, 2));
    int shstr = static_cast<int>(rd(b, 62, 2));
    if (shentsize < 64 || shoff + static_cast<std::uint64_t>(shnum) * static_cast<std::uint64_t>(shentsize) > b.size()) {
        error = "bad section headers";
        return false;
    }
    auto field = [&](int si, int off, int n) { return rd(b, shoff + static_cast<std::uint64_t>(si * shentsize + off), n); };
    std::uint64_t strOff = field(shstr, 24, 8);
    std::uint64_t strSize = field(shstr, 32, 8);
    std::vector<unsigned char> shstrtab(b.begin() + static_cast<std::ptrdiff_t>(strOff),
                                        b.begin() + static_cast<std::ptrdiff_t>(strOff + strSize));
    struct Sec {
        std::string name;
        std::uint32_t type = 0;
        std::uint64_t off = 0;
        std::uint64_t size = 0;
        std::uint32_t link = 0;
        std::uint32_t info = 0;
        std::vector<unsigned char> data;
    };
    std::vector<Sec> secs(static_cast<std::size_t>(shnum));
    for (int i = 0; i < shnum; ++i) {
        secs[static_cast<std::size_t>(i)].name = cstr(shstrtab, static_cast<std::size_t>(field(i, 0, 4)));
        secs[static_cast<std::size_t>(i)].type = static_cast<std::uint32_t>(field(i, 4, 4));
        secs[static_cast<std::size_t>(i)].off = field(i, 24, 8);
        secs[static_cast<std::size_t>(i)].size = field(i, 32, 8);
        secs[static_cast<std::size_t>(i)].link = static_cast<std::uint32_t>(field(i, 40, 4));
        secs[static_cast<std::size_t>(i)].info = static_cast<std::uint32_t>(field(i, 44, 4));
        auto off = secs[static_cast<std::size_t>(i)].off;
        auto sz = secs[static_cast<std::size_t>(i)].size;
        if (off + sz > b.size()) {
            error = "section runs past the file";
            return false;
        }
        secs[static_cast<std::size_t>(i)].data.assign(b.begin() + static_cast<std::ptrdiff_t>(off),
                                                      b.begin() + static_cast<std::ptrdiff_t>(off + sz));
    }
    auto find = [&](const char* name) -> int {
        for (int i = 0; i < shnum; ++i)
            if (secs[static_cast<std::size_t>(i)].name == name) return i;
        return -1;
    };
    int itext = find(".text");
    int iro = find(".rodata");
    int idata = find(".data");
    int isym = find(".symtab");
    int istr = find(".strtab");
    if (itext < 0 || isym < 0 || istr < 0) {
        error = "object is missing .text or the symbol table";
        return false;
    }
    o.text = secs[static_cast<std::size_t>(itext)].data;
    if (iro >= 0) o.ro = secs[static_cast<std::size_t>(iro)].data;
    if (idata >= 0) o.data = secs[static_cast<std::size_t>(idata)].data;
    o.textId = itext;
    o.roId = iro;
    o.dataId = idata;
    const std::vector<unsigned char>& strs = secs[static_cast<std::size_t>(istr)].data;
    const std::vector<unsigned char>& sy = secs[static_cast<std::size_t>(isym)].data;
    for (std::size_t i = 0; i + 24 <= sy.size(); i += 24) {
        Sym s;
        s.name = cstr(strs, static_cast<std::size_t>(rd(sy, i, 4)));
        unsigned info = sy[i + 4];
        s.bind = info >> 4;
        s.type = info & 0xf;
        s.shndx = static_cast<std::uint16_t>(rd(sy, i + 6, 2));
        s.value = rd(sy, i + 8, 8);
        s.size = rd(sy, i + 16, 8);
        o.syms.push_back(s);
    }
    auto loadRela = [&](int si, int targetSec) {
        if (si < 0) return;
        const std::vector<unsigned char>& r = secs[static_cast<std::size_t>(si)].data;
        for (std::size_t i = 0; i + 24 <= r.size(); i += 24) {
            Rela rel;
            rel.sec = targetSec;
            rel.offset = rd(r, i, 8);
            std::uint64_t info = rd(r, i + 8, 8);
            rel.sym = static_cast<std::uint32_t>(info >> 32);
            rel.type = static_cast<std::uint32_t>(info & 0xffffffffu);
            rel.addend = static_cast<std::int64_t>(rd(r, i + 16, 8));
            o.relas.push_back(rel);
        }
    };
    int irt = find(".rela.text");
    int irr = find(".rela.rodata");
    int ird = find(".rela.data");
    loadRela(irt, itext);
    loadRela(irr, iro);
    loadRela(ird, idata);
    return true;
}

std::uint64_t align16(std::uint64_t n) { return (n + 15) & ~std::uint64_t{15}; }

void poke(std::vector<unsigned char>& b, std::size_t at, std::uint64_t v, int n) {
    for (int i = 0; i < n; ++i) b[at + static_cast<std::size_t>(i)] = static_cast<unsigned char>((v >> (8 * i)) & 0xffu);
}

}  // namespace

std::vector<unsigned char> linkObjects(const std::vector<std::vector<unsigned char>>& files, std::string& error) {
    std::vector<Obj> objs;
    for (const auto& f : files) {
        Obj o;
        if (!parseObj(f, o, error)) return {};
        objs.push_back(std::move(o));
    }
    std::uint64_t va = kBase + kLoad;
    for (Obj& o : objs) {
        if (!o.text.empty()) {
            o.textVa = va;
            va = align16(va + o.text.size());
        }
    }
    bool needStart = true;
    for (const Obj& o : objs)
        for (const Sym& s : o.syms)
            if (s.name == "_start" && s.shndx != 0) needStart = false;
    std::uint64_t startVa = 0;
    // Global objects with constructors or destructors: each object file may define
    // __okay_init_<file> (run before main, in link order) and __okay_fini_<file> (after main, in
    // reverse order).
    std::vector<std::string> inits;
    std::vector<std::string> finis;
    for (const Obj& o : objs)
        for (const Sym& s : o.syms) {
            if (s.bind != 1 || s.shndx == 0) continue;
            if (s.name.compare(0, 12, "__okay_init_") == 0) inits.push_back(s.name);
            if (s.name.compare(0, 12, "__okay_fini_") == 0) finis.insert(finis.begin(), s.name);
        }
    const int kStart = 21 + 22 + 5 * static_cast<int>(inits.size() + finis.size());
    if (needStart) {
        startVa = va;
        va = align16(va + kStart);
    }
    for (Obj& o : objs) {
        if (!o.ro.empty()) {
            o.roVa = va;
            va = align16(va + o.ro.size());
        }
    }
    for (Obj& o : objs) {
        if (!o.data.empty()) {
            o.dataVa = va;
            va = align16(va + o.data.size());
        }
    }
    // A global (STB_GLOBAL) symbol may be defined once. A weak one (STB_WEAK, an inline function
    // or a vtable) may be defined in many objects: the first is kept, and a global definition
    // wins over a weak one (ELF gABI, "Symbol Table").
    std::map<std::string, std::uint64_t> global;
    std::set<std::string> weakOnly;
    for (const Obj& o : objs) {
        for (const Sym& s : o.syms) {
            if ((s.bind != 1 && s.bind != 2) || s.shndx == 0 || s.name.empty()) continue;
            std::uint64_t base = 0;
            if (s.shndx == o.textId) base = o.textVa;
            else if (s.shndx == o.roId) base = o.roVa;
            else if (s.shndx == o.dataId) base = o.dataVa;
            else continue;
            if (s.bind == 2) {
                if (global.count(s.name)) continue;
                weakOnly.insert(s.name);
                global[s.name] = base + s.value;
                continue;
            }
            if (global.count(s.name) && !weakOnly.count(s.name)) {
                error = "duplicate symbol " + s.name;
                return {};
            }
            weakOnly.erase(s.name);
            global[s.name] = base + s.value;
        }
    }
    if (needStart) {
        if (!global.count("main")) {
            error = "missing main";
            return {};
        }
        global["_start"] = startVa;
    }
    auto secVa = [&](const Obj& o, int shndx) -> std::uint64_t {
        if (shndx == o.textId) return o.textVa;
        if (shndx == o.roId) return o.roVa;
        if (shndx == o.dataId) return o.dataVa;
        return 0;
    };
    auto resolve = [&](const Obj& o, std::uint32_t si, std::uint64_t& out) -> bool {
        if (si >= o.syms.size()) return false;
        const Sym& s = o.syms[si];
        if (s.shndx != 0 && (s.bind == 2 || s.bind == 1) && !s.name.empty() && global.count(s.name)) {
            out = global[s.name];  // the one kept definition
            return true;
        }
        if (s.shndx != 0) {
            out = secVa(o, s.shndx) + s.value;
            return true;
        }
        auto it = global.find(s.name);
        if (it == global.end()) {
            error = "undefined symbol " + s.name;
            return false;
        }
        out = it->second;
        return true;
    };
    std::vector<unsigned char> image(static_cast<std::size_t>(va - (kBase + kLoad)), 0);
    auto place = [&](std::uint64_t at, const std::vector<unsigned char>& bytes) {
        if (bytes.empty()) return;
        std::size_t off = static_cast<std::size_t>(at - (kBase + kLoad));
        for (std::size_t i = 0; i < bytes.size(); ++i) image[off + i] = bytes[i];
    };
    for (Obj& o : objs) {
        place(o.textVa, o.text);
        place(o.roVa, o.ro);
        place(o.dataVa, o.data);
        for (const Rela& r : o.relas) {
            std::uint64_t S = 0;
            if (!resolve(o, r.sym, S)) return {};
            std::uint64_t P = secVa(o, r.sec) + r.offset;
            std::size_t at = static_cast<std::size_t>(P - (kBase + kLoad));
            if (r.type == 1) {
                wr(image, at, S + static_cast<std::uint64_t>(r.addend), 8);
            } else if (r.type == 2 || r.type == 4) {
                auto rel = static_cast<std::int64_t>(S) + r.addend - static_cast<std::int64_t>(P);
                wr(image, at, static_cast<std::uint64_t>(static_cast<std::int32_t>(rel)), 4);
            } else {
                error = "unsupported relocation";
                return {};
            }
        }
    }
    if (needStart) {
        std::size_t at = static_cast<std::size_t>(startVa - (kBase + kLoad));
        std::uint64_t pc = startVa;
        auto emit = [&](unsigned char byte) { image[at++] = byte; ++pc; };
        auto callTo = [&](const std::string& name) {
            emit(0xE8);
            auto rel = static_cast<std::int64_t>(global[name]) - static_cast<std::int64_t>(pc + 4);
            wr(image, at, static_cast<std::uint64_t>(static_cast<std::int32_t>(rel)), 4);
            at += 4;
            pc += 4;
        };
        // main(argc, argv, envp): at the entry rsp points to argc, then argv[0..argc-1], a null,
        // then the environment (System V AMD64 ABI 3.4.1, figure 3.9). They are kept in rbx, r12
        // and r13, which the constructors called first preserve (3.2.1), and passed in edi, rsi
        // and rdx.
        for (unsigned char b : kStartArgs) emit(b);
        for (const std::string& n : inits) callTo(n);
        for (unsigned char b : kMainArgs) emit(b);
        callTo("main");
        emit(0x50);  // push rax twice: keeps the stack aligned for the calls below
        emit(0x50);
        for (const std::string& n : finis) callTo(n);
        emit(0x58);  // pop rax
        emit(0x58);
        emit(0x48);  // mov rdi, rax
        emit(0x89);
        emit(0xC7);
        emit(0x48);  // mov rax, 60
        emit(0xC7);
        emit(0xC0);
        emit(0x3C);
        emit(0x00);
        emit(0x00);
        emit(0x00);
        emit(0x0F);  // syscall
        emit(0x05);
    }
    std::uint64_t filesz = kLoad + image.size();
    std::uint64_t entry = needStart ? startVa : global["_start"];
    std::vector<unsigned char> out(static_cast<std::size_t>(filesz), 0);
    out[0] = 0x7f;
    out[1] = 'E';
    out[2] = 'L';
    out[3] = 'F';
    out[4] = 2;
    out[5] = 1;
    out[6] = 1;
    poke(out, 16, 2, 2);
    poke(out, 18, 62, 2);
    poke(out, 20, 1, 4);
    poke(out, 24, entry, 8);
    poke(out, 32, 64, 8);
    poke(out, 52, 64, 2);
    poke(out, 54, 56, 2);
    poke(out, 56, 1, 2);
    poke(out, 64, 1, 4);
    poke(out, 68, 7, 4);
    poke(out, 80, kBase, 8);
    poke(out, 88, kBase, 8);
    poke(out, 96, filesz, 8);
    poke(out, 104, filesz, 8);
    poke(out, 112, 0x1000, 8);
    for (std::size_t i = 0; i < image.size(); ++i) out[kLoad + i] = image[i];
    return out;
}

}  // namespace okaycc
