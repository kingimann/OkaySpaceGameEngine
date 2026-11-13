#pragma once
// A tiny PE (x64) image builder for the anti-cheat tests: a code section, an import
// table, a relocation table. It makes both the on-disk file and the image as the
// loader would map it, so the code-integrity logic is tested on Linux without a
// Windows toolchain (the Windows build is exercised separately, under wine).
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace actest {

struct Import { std::string dll; std::vector<std::string> funcs; };

struct TestPe {
    std::vector<std::uint8_t> file, mapped;
    std::uint32_t textRva = 0, textRaw = 0, textLen = 0;
    std::uint32_t iatRva = 0;            // first import's IAT
    std::vector<std::uint32_t> iatRvas;  // each import's IAT
    std::uint64_t imageBase = 0x140000000ull;
};

inline std::uint32_t Align(std::uint32_t v, std::uint32_t a) { return (v + a - 1) / a * a; }
template <class T> void Put(std::vector<std::uint8_t>& b, std::size_t off, T v) {
    if (b.size() < off + sizeof(T)) b.resize(off + sizeof(T));
    for (std::size_t i = 0; i < sizeof(T); ++i) b[off + i] = (std::uint8_t)((std::uint64_t)v >> (8 * i));
}

// `relocs`: offsets inside the text where a 64-bit absolute address lives.
inline TestPe MakePe(const std::vector<std::uint8_t>& text, const std::vector<std::uint32_t>& relocs, const std::vector<Import>& imports) {
    TestPe t;
    t.textLen = (std::uint32_t)text.size();
    const std::uint32_t hdr = 0x400, secAlign = 0x1000, fileAlign = 0x200;
    // ---- .idata content
    std::vector<std::uint8_t> idata;
    const std::uint32_t textVirt = Align(t.textLen, secAlign);
    const std::uint32_t idataRva = 0x1000 + textVirt;
    std::uint32_t pos = (std::uint32_t)(20 * (imports.size() + 1));
    idata.resize(pos);
    for (std::size_t i = 0; i < imports.size(); ++i) {
        const Import& im = imports[i];
        const std::uint32_t ilt = pos, iat = pos + (std::uint32_t)(8 * (im.funcs.size() + 1));
        std::uint32_t names = iat + (std::uint32_t)(8 * (im.funcs.size() + 1));
        const std::uint32_t dllName = names;
        names += (std::uint32_t)im.dll.size() + 1;
        idata.resize(names);
        std::memcpy(&idata[dllName], im.dll.c_str(), im.dll.size() + 1);
        for (std::size_t f = 0; f < im.funcs.size(); ++f) {
            const std::uint32_t hn = (std::uint32_t)idata.size();
            idata.resize(hn + 2 + im.funcs[f].size() + 1);
            std::memcpy(&idata[hn + 2], im.funcs[f].c_str(), im.funcs[f].size() + 1);
            Put<std::uint64_t>(idata, ilt + 8 * f, idataRva + hn);
            Put<std::uint64_t>(idata, iat + 8 * f, idataRva + hn);   // unbound: points at the hint/name entry
        }
        Put<std::uint32_t>(idata, 20 * i + 0, idataRva + ilt);
        Put<std::uint32_t>(idata, 20 * i + 12, idataRva + dllName);
        Put<std::uint32_t>(idata, 20 * i + 16, idataRva + iat);
        if (i == 0) t.iatRva = idataRva + iat;
        t.iatRvas.push_back(idataRva + iat);
        pos = (std::uint32_t)idata.size();
        if (pos % 8) { idata.resize(pos + (8 - pos % 8)); pos = (std::uint32_t)idata.size(); }
    }
    // ---- .reloc content (one block per 4 KiB page)
    std::vector<std::uint8_t> reloc;
    for (std::uint32_t page = 0; page < textVirt; page += 0x1000) {
        std::vector<std::uint16_t> ents;
        for (std::uint32_t r : relocs) if (r >= page && r < page + 0x1000) ents.push_back((std::uint16_t)((10u << 12) | (r - page)));
        if (ents.empty()) continue;
        if (ents.size() % 2) ents.push_back(0);   // pad to a multiple of 4 bytes
        const std::size_t at = reloc.size();
        Put<std::uint32_t>(reloc, at, 0x1000 + page);
        Put<std::uint32_t>(reloc, at + 4, (std::uint32_t)(8 + 2 * ents.size()));
        for (std::size_t i = 0; i < ents.size(); ++i) Put<std::uint16_t>(reloc, at + 8 + 2 * i, ents[i]);
    }
    const std::uint32_t relocRva = idataRva + Align((std::uint32_t)idata.size(), secAlign);
    struct S { const char* n; std::uint32_t rva, vsize; const std::vector<std::uint8_t>* d; std::uint32_t chars; };
    const S secs[3] = {{".text", 0x1000, t.textLen, &text, 0x60000020u},
                       {".idata", idataRva, (std::uint32_t)idata.size(), &idata, 0xC0000040u},
                       {".reloc", relocRva, (std::uint32_t)reloc.size(), &reloc, 0x42000040u}};
    const int ns = reloc.empty() ? 2 : 3;
    std::uint32_t raw = hdr;
    std::uint32_t rawOff[3], rawSize[3];
    for (int i = 0; i < ns; ++i) { rawOff[i] = raw; rawSize[i] = Align((std::uint32_t)secs[i].d->size(), fileAlign); raw += rawSize[i]; }
    t.textRaw = rawOff[0];
    const std::uint32_t sizeOfImage = Align(secs[ns - 1].rva + secs[ns - 1].vsize, secAlign);
    std::vector<std::uint8_t>& f = t.file;
    f.assign(raw, 0);
    Put<std::uint16_t>(f, 0, 0x5A4D);
    Put<std::uint32_t>(f, 0x3C, 0x80);
    Put<std::uint32_t>(f, 0x80, 0x00004550);
    Put<std::uint16_t>(f, 0x84, 0x8664);
    Put<std::uint16_t>(f, 0x86, (std::uint16_t)ns);
    Put<std::uint16_t>(f, 0x94, 240);            // SizeOfOptionalHeader
    Put<std::uint16_t>(f, 0x96, 0x22);
    const std::size_t opt = 0x98;
    Put<std::uint16_t>(f, opt, 0x20B);
    Put<std::uint64_t>(f, opt + 24, t.imageBase);
    Put<std::uint32_t>(f, opt + 32, secAlign);
    Put<std::uint32_t>(f, opt + 36, fileAlign);
    Put<std::uint32_t>(f, opt + 56, sizeOfImage);
    Put<std::uint32_t>(f, opt + 60, hdr);
    Put<std::uint32_t>(f, opt + 108, 16);
    Put<std::uint32_t>(f, opt + 112 + 1 * 8, imports.empty() ? 0 : idataRva);
    Put<std::uint32_t>(f, opt + 112 + 1 * 8 + 4, imports.empty() ? 0 : (std::uint32_t)idata.size());
    Put<std::uint32_t>(f, opt + 112 + 5 * 8, reloc.empty() ? 0 : relocRva);
    Put<std::uint32_t>(f, opt + 112 + 5 * 8 + 4, (std::uint32_t)reloc.size());
    for (int i = 0; i < ns; ++i) {
        const std::size_t s = opt + 240 + 40 * i;
        std::memcpy(&f[s], secs[i].n, std::strlen(secs[i].n));
        Put<std::uint32_t>(f, s + 8, secs[i].vsize);
        Put<std::uint32_t>(f, s + 12, secs[i].rva);
        Put<std::uint32_t>(f, s + 16, rawSize[i]);
        Put<std::uint32_t>(f, s + 20, rawOff[i]);
        Put<std::uint32_t>(f, s + 36, secs[i].chars);
        if (!secs[i].d->empty()) std::memcpy(&f[rawOff[i]], secs[i].d->data(), secs[i].d->size());
    }
    // ---- the mapped image: headers, then each section at its RVA
    t.mapped.assign(sizeOfImage, 0);
    std::memcpy(t.mapped.data(), f.data(), hdr);
    for (int i = 0; i < ns; ++i) std::memcpy(&t.mapped[secs[i].rva], &f[rawOff[i]], secs[i].d->size());
    return t;
}

// What the loader does when the image lands `delta` bytes off its preferred base.
inline void Rebase(TestPe& t, const std::vector<std::uint32_t>& relocs, std::int64_t delta) {
    for (std::uint32_t r : relocs) {
        std::uint64_t v;
        std::memcpy(&v, &t.mapped[0x1000 + r], 8);
        v += (std::uint64_t)delta;
        std::memcpy(&t.mapped[0x1000 + r], &v, 8);
    }
}

} // namespace actest
