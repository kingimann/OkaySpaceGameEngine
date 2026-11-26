// okay::brotli (engine/include/okay/Core/Brotli.hpp), the engine's Brotli decoder (RFC 7932).
// 1. Its tables against the RFC: the dictionary's and the transform list's length and CRC-32 (Appendix A, B), the context
//    lookups' CRC-32 (7.1), the insert / copy / block count tables (sections 5, 6, as printed), NDBITS against DICTSIZE (8).
// 2. The RFC's own examples: the bit packing example (1.5.1) for the test writer, the canonical code example (3.2) decoded
//    through a literal code, the trivial compressor's streams (11.1), the 16 / 17 repeat example (3.5).
// 3. Streams built by hand (tests/brotli_writer.hpp): uncompressed and metadata meta-blocks, simple codes of 1..4 symbols,
//    complex codes, every context mode, every distance symbol for several NPOSTFIX / NDIRECT, the window limit, dictionary
//    words with transforms, block switches and context maps.
// 4. Hostile input: every cut-off of valid streams, bad lengths and codes, the large-window marker, output over the cap,
//    fill bits, trailing bytes, random and mutated bytes (no crash, no hang).
#include "test_framework.hpp"
#include "brotli_writer.hpp"
#include "okay/Core/Brotli.hpp"
#include "okay/Core/Inflate.hpp"   // the engine's one CRC-32 (RFC 1952), for the RFC's check values

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using Bytes = std::vector<std::uint8_t>;
namespace br = okay::brotli;
namespace bd = okay::brotli::detail;
using okay::inflate::detail::Crc32;

static bool Dec(const Bytes& in, Bytes& out, std::string* whyOut = nullptr, std::size_t cap = std::size_t(1) << 30, bool trailing = false) {
    br::Options op;
    op.maxOutput = cap;
    op.allowTrailing = trailing;
    std::string why;
    const bool ok = br::Decode(in.data(), in.size(), out, op, why);
    if (whyOut) *whyOut = why;
    return ok;
}
static bool Refused(const Bytes& in, std::size_t cap = std::size_t(1) << 30) {
    Bytes out;
    std::string why;
    const bool ok = Dec(in, out, &why, cap);
    return !ok && out.empty() && !why.empty();
}
static Bytes Str(const std::string& s) { return Bytes(s.begin(), s.end()); }
static std::uint32_t Rng(std::uint32_t& s) { s = s * 1664525u + 1013904223u; return s >> 8; }

int main() {
    RUN_SUITE("brotli: RFC 7932 decoder");

    // ---- 1. tables against the RFC ---------------------------------------------------------------------------------------
    {
        CHECK(bd::DictionarySize() == 122784);
        CHECK(Crc32(bd::Dictionary(), bd::DictionarySize()) == 0x5136cb04u);
        // Appendix B: prefix, 0, transform value, suffix, 0 for each of the 121 transforms: 648 bytes, CRC-32 0x3d965f81
        Bytes ser;
        for (int id = 0; id < 121; ++id) {
            const bd::TransformInfo t = bd::Transform(id);
            for (const char* p = t.prefix; *p; ++p) ser.push_back(std::uint8_t(*p));
            ser.push_back(0);
            ser.push_back(std::uint8_t(t.kind));
            for (const char* p = t.suffix; *p; ++p) ser.push_back(std::uint8_t(*p));
            ser.push_back(0);
        }
        CHECK(ser.size() == 648);
        CHECK(Crc32(ser.data(), ser.size()) == 0x3d965f81u);
        // section 7.1: Lut0, Lut1, Lut2 as 256-byte sequences
        const std::uint32_t lutCrc[3] = {0x8e91efb7u, 0xd01a32f4u, 0x0dd7a0d6u};
        for (int t = 0; t < 3; ++t) {
            std::uint8_t b[256];
            for (int i = 0; i < 256; ++i) b[i] = bd::ContextLut(t, i);
            CHECK(Crc32(b, 256) == lutCrc[t]);
        }
        // a few printed values, read off the tables
        CHECK(bd::ContextLut(0, ' ') == 8 && bd::ContextLut(0, 'e') == 56 && bd::ContextLut(0, 'Z') == 52 && bd::ContextLut(0, 0xC1) == 3);
        CHECK(bd::ContextLut(1, '7') == 2 && bd::ContextLut(1, 'q') == 3 && bd::ContextLut(1, 0xE5) == 2 && bd::ContextLut(2, 255) == 7);
        // sections 5 and 6, against the printed tables (typed into brotli_writer.hpp from the RFC)
        for (int c = 0; c < 24; ++c) {
            CHECK(bd::InsertBase(c) == brw::kInsFirst[c] && bd::InsertExtra(c) == brw::kInsBits[c]);
            CHECK(bd::CopyBase(c) == brw::kCopyFirst[c] && bd::CopyExtra(c) == brw::kCopyBits[c]);
        }
        CHECK(bd::InsertBase(23) + (1u << 24) - 1 == 16799809u);   // the last printed insert length
        CHECK(bd::CopyBase(23) + (1u << 24) - 1 == 16779333u);
        const std::uint32_t blkFirst[26] = {1, 5, 9, 13, 17, 25, 33, 41, 49, 65, 81, 97, 113, 145, 177, 209, 241, 305, 369, 497, 753, 1265, 2289, 4337, 8433, 16625};
        const int blkBits[26] = {2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 7, 8, 9, 10, 11, 12, 13, 24};
        for (int c = 0; c < 26; ++c) CHECK(bd::BlockCountBase(c) == blkFirst[c] && bd::BlockCountExtra(c) == blkBits[c]);
        CHECK(bd::BlockCountBase(25) + (1u << 24) - 1 == 16793840u);
        // section 8: DOFFSET from NDBITS gives DICTSIZE
        CHECK(bd::WordOffset(4) == 0 && bd::WordOffset(25) == 122784u);
        CHECK(bd::WordOffset(24) + 24u * (1u << bd::WordBits(24)) == 122784u);
        // the first word of length 4 is "time" (the appendix's first hex bytes 74696d65)
        CHECK(std::memcmp(bd::Dictionary(), "time", 4) == 0);
    }

    // ---- 2. the RFC's own examples -----------------------------------------------------------------------------------------
    {
        // 1.5.1: 3-bit 6, 4-bit 2, prefix 110, prefix 10, 12-bit 3628 -> bytes 10010110 11000101 11100010
        brw::BitWriter w;
        w.Put(6, 3); w.Put(2, 4); w.PutCode(6, 3); w.PutCode(2, 2); w.Put(3628, 12);
        CHECK(w.bytes == (Bytes{0x96, 0xC5, 0xE2}));
        // 3.2: lengths (3, 3, 3, 3, 3, 2, 4, 4) for ABCDEFGH give the codes 010 011 100 101 110 00 1110 1111
        const std::vector<std::uint32_t> codes = brw::CanonicalCodes({3, 3, 3, 3, 3, 2, 4, 4});
        CHECK(codes == (std::vector<std::uint32_t>{2, 3, 4, 5, 6, 0, 14, 15}));
        // ... and the decoder reads them: a literal code with those lengths on 'A'..'H', the literals written with the
        // RFC's printed codes (not the writer's), in one last meta-block
        brw::BitWriter s;
        brw::WriteWindow(s, 16);
        brw::WritePlainHeader(s, true, 8, 0, 0, 0);
        std::vector<int> lens(256, 0);
        const int L[8] = {3, 3, 3, 3, 3, 2, 4, 4};
        for (int i = 0; i < 8; ++i) lens[std::size_t('A' + i)] = L[i];
        brw::WriteComplex(s, lens);
        brw::WriteSimple(s, 704, {brw::CommandSymbol(brw::LengthCode(brw::kInsFirst, 8), 0, false)});   // one command: 8 literals
        brw::WriteSimple(s, 64, {0});
        s.Put(0, 0);   // (insert code 7 = 8 with 1 extra bit of 0; the copy code has no extra bits)
        s.Put(0, 1);
        const char* printed[8] = {"010", "011", "100", "101", "110", "00", "1110", "1111"};
        for (const char* p : {"110", "1111", "010", "00"}) for (const char* q = p; *q; ++q) s.Bit(unsigned(*q - '0'));   // E H A F
        for (const char* p : {printed[1], printed[2], printed[3], printed[6]}) for (const char* q = p; *q; ++q) s.Bit(unsigned(*q - '0'));   // B C D G
        s.ToByte();
        Bytes out;
        std::string why;
        CHECK(Dec(s.bytes, out, &why) && out == Str("EHAFBCDG"));
        if (out != Str("EHAFBCDG")) std::cerr << "  3.2 example: " << why << "\n";

        // 11.1: the trivial compressor's streams: empty -> {6}; otherwise 12, 64 KiB blocks (248 255 15 + data), the rest
        // ((r & 31) << 3, r >> 5, 8 + (r >> 13) + data, r = size - 1), then 3
        CHECK(Dec(Bytes{6}, out) && out.empty());
        auto trivial = [](const Bytes& u) {
            Bytes c{12};
            std::size_t i = 0;
            for (; i + 65535 < u.size(); i += 65536) { c.push_back(248); c.push_back(255); c.push_back(15); c.insert(c.end(), u.begin() + std::ptrdiff_t(i), u.begin() + std::ptrdiff_t(i + 65536)); }
            if (i < u.size()) {
                const std::size_t r = u.size() - i - 1;
                c.push_back(std::uint8_t((r & 31) << 3)); c.push_back(std::uint8_t(r >> 5)); c.push_back(std::uint8_t(8 + (r >> 13)));
                c.insert(c.end(), u.begin() + std::ptrdiff_t(i), u.end());
            }
            c.push_back(3);
            return c;
        };
        for (std::size_t n : {std::size_t(1), std::size_t(5), std::size_t(65535), std::size_t(65536), std::size_t(65537), std::size_t(200000)}) {
            Bytes u(n);
            std::uint32_t seed = std::uint32_t(n);
            for (auto& b : u) b = std::uint8_t(Rng(seed));
            CHECK(Dec(trivial(u), out) && out == u);
        }
        // 3.5: "7, 16 (+2 bits 11), 16 (+2 bits 10) will expand to 22 code lengths of 7"; and 17 after 17. A literal code with
        // 22 symbols of length 7, 74 zeros (17 +7, 17 +7: 8 * (10 - 2) + 10), then lengths 1, 2, 4, 6 (complete: 22/128 + 1/2
        // + 1/4 + 1/16 + 1/64 = 1)
        brw::BitWriter r;
        brw::WriteWindow(r, 16);
        brw::WritePlainHeader(r, true, 6, 0, 0, 0);
        std::vector<int> rl(256, 0);
        for (int k = 0; k < 22; ++k) rl[std::size_t(k)] = 7;
        rl[96] = 1; rl[97] = 2; rl[98] = 4; rl[99] = 6;
        const brw::Code lc = brw::WriteComplexTokens(r, {{7, 0}, {16, 3}, {16, 2}, {17, 7}, {17, 7}, {1, 0}, {2, 0}, {4, 0}, {6, 0}}, rl);
        brw::WriteSimple(r, 704, {brw::CommandSymbol(6, 0, false)});   // insert code 6: 6 or 7 literals
        brw::WriteSimple(r, 64, {0});
        r.Put(0, 1);
        for (int b : {96, 97, 98, 99, 0, 21}) lc.Emit(r, b);
        r.ToByte();
        CHECK(Dec(r.bytes, out, &why) && out == (Bytes{96, 97, 98, 99, 0, 21}));
        if (out != (Bytes{96, 97, 98, 99, 0, 21})) std::cerr << "  3.5 example: " << why << "\n";
    }

    // ---- 3. streams built by hand ------------------------------------------------------------------------------------------
    Bytes out;
    std::string why;
    {
        // metadata meta-blocks (skipped), an uncompressed one, a last empty one; every window size
        for (int wb = 10; wb <= 24; ++wb) {
            brw::BitWriter w;
            brw::WriteWindow(w, wb);
            w.Put(0, 1); w.Put(3, 2); w.Put(0, 1); w.Put(1, 2); w.Put(2, 8); w.ToByte();   // metadata, 3 bytes
            w.bytes.push_back('x'); w.bytes.push_back('y'); w.bytes.push_back('z');
            const Bytes hello = Str("hello");
            brw::WriteStoredBlock(w, hello.data(), hello.size());
            brw::WriteLastEmpty(w);
            CHECK(Dec(w.bytes, out) && out == hello);
        }
        // simple codes of 1..4 symbols (and both 4-symbol shapes), complex codes, through literals and commands
        const std::string text = "abracadabra, abracadabra; the cat sat on the mat";
        for (int simple = 0; simple < 2; ++simple)
            for (int alphabetSize : {1, 2, 3, 4, 40}) {
                std::string t;
                for (char ch : text) if (std::uint8_t(ch) % 41 < alphabetSize || alphabetSize == 40) t.push_back(ch);
                if (alphabetSize < 40) { t.clear(); for (int k = 0; k < 30; ++k) t.push_back(char('a' + (k * 7 + k / 3) % alphabetSize)); }
                brw::BitWriter w;
                brw::WriteWindow(w, 16);
                brw::Command c;
                c.literals = Str(t);
                c.last = true;
                brw::WriteCommands(w, {c}, true, t.size(), 0, 0, 0, simple != 0);
                w.ToByte();
                CHECK(Dec(w.bytes, out, &why) && out == Str(t));
            }
        // the four context modes (one tree each: the mode is read but decides nothing; the stream must still decode)
        for (int mode = 0; mode < 4; ++mode) {
            brw::BitWriter w;
            brw::WriteWindow(w, 16);
            brw::Command c;
            c.literals = Str("Mode test 123 \xC3\xA9");
            c.last = true;
            brw::WriteCommands(w, {c}, true, c.literals.size(), 0, 0, mode);
            w.ToByte();
            CHECK(Dec(w.bytes, out) && out == c.literals);
        }
    }
    {
        // backward copies, overlapping copies, implicit distance, and every distance symbol for several NPOSTFIX / NDIRECT:
        // 2000 bytes stored first (window bits 10: window 1008), then one meta-block of copies of 4 bytes; each distance up
        // to 1008 copies from the output, each above it is a dictionary word (word id = distance - 1009), checked against
        // Appendix A; distances whose word id names a transform past 120 must be refused
        Bytes base(2000);
        std::uint32_t seed = 7;
        for (auto& b : base) b = std::uint8_t(Rng(seed));
        const int combos[4][2] = {{0, 0}, {1, 6}, {2, 12}, {3, 120}};
        for (const auto& pd : combos) {
            const int npostfix = pd[0], ndirect = pd[1];
            std::vector<brw::Command> cmds;
            Bytes want = base;
            std::uint32_t ring[4] = {16, 15, 11, 4};
            int last = 3;
            int checked = 0;
            auto addCopy = [&](int sym, std::uint32_t extra, int bits, std::uint64_t dist) {
                brw::Command c;
                c.copy = 4;
                c.dist = {sym, extra, bits};
                const std::size_t reach = want.size() < 1008 ? want.size() : 1008;
                if (dist <= reach) {
                    for (int k = 0; k < 4; ++k) want.push_back(want[want.size() - dist]);
                    if (sym != 0) { last = (last + 1) & 3; ring[last] = std::uint32_t(dist); }
                } else {
                    const std::uint64_t id = dist - reach - 1;   // transform 0 (identity) while id < 1024
                    for (int k = 0; k < 4; ++k) want.push_back(bd::Dictionary()[4 * id + std::uint64_t(k)]);
                }
                cmds.push_back(c);
            };
            // special symbols 0..15 after four known distances
            for (std::uint64_t d : {100u, 200u, 300u, 400u}) { const brw::Dist x = brw::DistanceFor(d, npostfix, ndirect); addCopy(x.sym, x.extra, x.bits, d); }
            static const int kDelta[6] = {-1, 1, -2, 2, -3, 3};
            for (int sym = 0; sym < 16; ++sym) {
                const std::uint64_t d = sym < 4 ? ring[(last - sym) & 3] : std::uint64_t(std::int64_t(ring[sym < 10 ? last : (last - 1) & 3]) + kDelta[(sym - 4) % 6]);
                addCopy(sym, 0, 0, d);
            }
            // direct and coded symbols, with the smallest and largest extra bits
            for (int sym = 16; sym < 16 + ndirect + (48 << npostfix); ++sym) {
                for (int top = 0; top < 2; ++top) {
                    int bits = 0;
                    std::uint64_t dist;
                    std::uint32_t extra = 0;
                    if (sym < 16 + ndirect) { if (top) continue; dist = std::uint64_t(sym - 15); }
                    else {
                        const int k = sym - ndirect - 16;
                        bits = 1 + (k >> (npostfix + 1));
                        extra = top ? (1u << bits) - 1 : 0;
                        const std::uint64_t offset = ((2 + ((std::uint64_t(k) >> npostfix) & 1)) << bits) - 4;
                        dist = ((offset + extra) << npostfix) + (std::uint64_t(k) & ((1u << npostfix) - 1)) + std::uint64_t(ndirect) + 1;
                    }
                    const brw::Dist inv = brw::DistanceFor(dist, npostfix, ndirect);
                    CHECK(inv.sym == sym && inv.extra == extra);   // the writer's inverse agrees with the RFC formula
                    if (dist > 1008 + 1024) continue;   // a transform other than identity (the transforms are checked below)
                    addCopy(sym, extra, bits, dist);
                    ++checked;
                }
            }
            // an overlapping copy (distance 2, 9 bytes) and an implicit-distance command (reuses the last distance)
            {
                brw::Command c; c.copy = 9; c.dist = brw::DistanceFor(2, npostfix, ndirect);
                if (c.dist.sym >= 0) {
                    for (int k = 0; k < 9; ++k) want.push_back(want[want.size() - 2]);
                    last = (last + 1) & 3; ring[last] = 2;
                    cmds.push_back(c);
                    brw::Command im; im.copy = 5; im.implicit = true; im.literals = Str("ab");
                    want.push_back('a'); want.push_back('b');
                    for (int k = 0; k < 5; ++k) want.push_back(want[want.size() - 2]);
                    cmds.push_back(im);
                }
            }
            brw::BitWriter w;
            brw::WriteWindow(w, 10);
            brw::WriteStoredBlock(w, base.data(), base.size());
            brw::WriteCommands(w, cmds, true, want.size() - base.size(), npostfix, ndirect);
            w.ToByte();
            const bool ok = Dec(w.bytes, out, &why);
            CHECK(ok && out == want);
            if (!ok || out != want) std::cerr << "  distances npostfix " << npostfix << " ndirect " << ndirect << ": " << why << "\n";
            CHECK(checked > 20);
        }
        // the largest distance code with all its extra bits is far past transform 120: refused
        brw::BitWriter w;
        brw::WriteWindow(w, 10);
        brw::WriteStoredBlock(w, base.data(), base.size());
        brw::Command c; c.copy = 4; c.dist = {16 + 47, (1u << 24) - 1, 24};
        brw::WriteCommands(w, {c}, true, 4);
        w.ToByte();
        CHECK(Refused(w.bytes));
    }
    {
        // the window limit: window bits 10 = 1008 bytes. After 2000 bytes, distance 1008 copies, 1009 is dictionary word 0
        // ("time"); with window bits 16 and 3 bytes out, distance 4 is the first word.
        Bytes base(2000);
        for (std::size_t k = 0; k < base.size(); ++k) base[k] = std::uint8_t(k * 31 + 7);
        auto oneCopy = [&](int wbits, const Bytes& stored, std::uint64_t dist, std::uint32_t len) {
            brw::BitWriter w;
            brw::WriteWindow(w, wbits);
            if (!stored.empty()) brw::WriteStoredBlock(w, stored.data(), stored.size());
            brw::Command c; c.copy = len; c.dist = brw::DistanceFor(dist, 0, 0);
            brw::WriteCommands(w, {c}, true, len);
            w.ToByte();
            return w.bytes;
        };
        Bytes want = base;
        for (int k = 0; k < 4; ++k) want.push_back(base[base.size() - 1008 + std::size_t(k)]);
        CHECK(Dec(oneCopy(10, base, 1008, 4), out) && out == want);
        want = base; for (char ch : std::string("time")) want.push_back(std::uint8_t(ch));
        CHECK(Dec(oneCopy(10, base, 1009, 4), out) && out == want);
        // window bits 16 (65520): the same 1009 is now an ordinary copy
        want = base; for (int k = 0; k < 4; ++k) want.push_back(base[base.size() - 1009 + std::size_t(k)]);
        CHECK(Dec(oneCopy(16, base, 1009, 4), out) && out == want);
        const Bytes three = Str("abc");
        CHECK(Dec(oneCopy(16, three, 4, 4), out) && out == Str("abctime"));
        CHECK(Dec(oneCopy(16, three, 3, 4), out) && out == Str("abcabca"));
        // a dictionary reference must have a copy length of 4..24
        CHECK(Refused(oneCopy(16, three, 4, 3)) && Refused(oneCopy(16, three, 4, 25)));
        // window bits 24, the largest
        CHECK(Dec(oneCopy(24, three, 3, 4), out) && out == Str("abcabca"));
    }
    {
        // dictionary words with transforms: word 0 of length 4 is "time"; word id = (transform << 10) | index
        auto run = [&](std::uint32_t len, std::uint64_t id, const std::string& expect) {
            brw::BitWriter w;
            brw::WriteWindow(w, 16);
            brw::Command c; c.copy = len; c.dist = brw::DistanceFor(id + 1, 0, 0);
            if (expect.empty()) {   // a word cut to nothing: follow it with one literal so the meta-block has a length
                brw::Command tail; tail.literals = Str("!"); tail.last = true;
                brw::WriteCommands(w, {c, tail}, true, 1);
                w.ToByte();
                return Dec(w.bytes, out, &why) && out == Str("!");
            }
            brw::WriteCommands(w, {c}, true, expect.size());
            w.ToByte();
            const bool ok = Dec(w.bytes, out, &why) && out == Str(expect);
            if (!ok) std::cerr << "  transform of word " << id << ": got '" << std::string(out.begin(), out.end()) << "' " << why << "\n";
            return ok;
        };
        CHECK(run(4, 0, "time"));                          // 0 Identity
        CHECK(run(4, (1u << 10), "time "));                // 1 Identity + " "
        CHECK(run(4, (3u << 10), "ime"));                  // 3 OmitFirst1
        CHECK(run(4, (4u << 10), "Time "));                // 4 FermentFirst + " "
        CHECK(run(4, (12u << 10), "tim"));                 // 12 OmitLast1
        CHECK(run(4, (44u << 10), "TIME"));                // 44 FermentAll
        CHECK(run(4, (49u << 10), "timing "));             // 49 OmitLast1 + "ing "
        CHECK(run(4, (54u << 10), ""));                    // 54 OmitFirst9: nothing left
        CHECK(run(4, (73u << 10), " the time of the "));   // 73
        CHECK(run(4, (102u << 10), "\xC2\xA0time"));       // 102 prefix C2 A0
        CHECK(run(4, (120u << 10), " Time='"));            // 120 FermentFirst, " " and "='"
        CHECK(run(4, (1u << 10) | 1, "down "));            // index 1: "down"
        // transform 121 does not exist
        brw::BitWriter w;
        brw::WriteWindow(w, 16);
        brw::Command c; c.copy = 4; c.dist = brw::DistanceFor((121u << 10) + 1, 0, 0);
        brw::WriteCommands(w, {c}, true, 4);
        w.ToByte();
        CHECK(Refused(w.bytes));
        // every length 4..24, first and last word, identity: the bytes at DOFFSET[length] + index * length
        for (std::uint32_t len = 4; len <= 24; ++len)
            for (std::uint64_t idx : {std::uint64_t(0), (std::uint64_t(1) << bd::WordBits(int(len))) - 1}) {
                const char* p = reinterpret_cast<const char*>(bd::Dictionary() + bd::WordOffset(int(len)) + idx * len);
                CHECK(run(len, idx, std::string(p, len)));
            }
        // FermentAll on UTF-8: a character that starts with a byte below 0xC0 is one byte (ASCII letters change case), one
        // that starts below 0xE0 is two bytes (bit 5 of the second flips), any other three (bits 0 and 2 of the third flip);
        // a flip past the end of the word is not done. Words of length 4 that start with a two- and a three-byte character.
        auto fermentAll = [](std::string w) {
            for (std::size_t i = 0; i < w.size();) {
                const std::uint8_t c = std::uint8_t(w[i]);
                if (c < 0xC0) { if (c >= 'a' && c <= 'z') w[i] = char(c - 32); i += 1; }
                else if (c < 0xE0) { if (i + 1 < w.size()) w[i + 1] = char(std::uint8_t(w[i + 1]) ^ 0x20); i += 2; }
                else { if (i + 2 < w.size()) w[i + 2] = char(std::uint8_t(w[i + 2]) ^ 0x05); i += 3; }
            }
            return w;
        };
        CHECK(fermentAll("time") == "TIME");
        int found2 = 0, found3 = 0;
        for (std::uint32_t len : {4u, 6u})   // (only one word of length 4 starts with a three-byte character)
            for (std::uint32_t idx = 0; idx < (1u << bd::WordBits(int(len))); ++idx) {
                const std::uint8_t* p = bd::Dictionary() + bd::WordOffset(int(len)) + idx * len;
                const std::string w(reinterpret_cast<const char*>(p), len);
                const bool two = p[0] >= 0xC0 && p[0] < 0xE0, three = p[0] >= 0xE0;
                if ((two && found2 < 2) || (three && found3 < 2)) {
                    CHECK(fermentAll(w) != w);
                    CHECK(run(len, (std::uint64_t(44) << bd::WordBits(int(len))) | idx, fermentAll(w)));
                    (two ? found2 : found3) += 1;
                }
            }
        CHECK(found2 == 2 && found3 == 2);
    }
    {
        // block switches and context maps: 2 literal block types (LSB6 and MSB6), 2 literal trees, a context map with a
        // run of zeros and the inverse move-to-front; 2 insert-and-copy block types; 2 distance trees
        brw::BitWriter w;
        brw::WriteWindow(w, 16);
        // header
        w.Put(1, 1); w.Put(0, 1);
        brw::WriteMlen(w, 8 + 4);
        // NBLTYPESL = 2: block type code (alphabet 4) and block count code (alphabet 26), first count
        w.Put(1, 1); w.Put(0, 3);
        const brw::Code ltype = brw::WriteSimple(w, 4, {1});           // always "type + 1" (wraps)
        const brw::Code lcount = brw::WriteSimple(w, 26, {0});         // counts 1..4 (2 extra bits)
        w.Put(3, 2);                                                   // first literal block: 4 literals
        // NBLTYPESI = 2
        w.Put(1, 1); w.Put(0, 3);
        const brw::Code itype = brw::WriteSimple(w, 4, {1});
        const brw::Code icount = brw::WriteSimple(w, 26, {0});
        w.Put(0, 2);                                                   // first command block: 1 command
        // NBLTYPESD = 1
        w.Put(0, 1);
        w.Put(0, 2); w.Put(0, 4);                                      // NPOSTFIX 0, NDIRECT 0
        w.Put(0, 2); w.Put(1, 2);                                      // modes: type 0 LSB6, type 1 MSB6
        // NTREESL = 2, context map of 128 entries: type 0 all tree 1, type 1 all tree 0, written as move-to-front indexes:
        // 1, 63 zeros (still 1), 1 (the list is now 1, 0, ...: index 1 is 0), 63 zeros (still 0); the zeros as runs
        w.Put(1, 1); w.Put(0, 3);
        w.Put(1, 1); w.Put(5, 4);                                      // RLEMAX = 6
        const brw::Code cm = brw::WriteSimple(w, 2 + 6, {5, 7});       // symbol 5 (32..63 zeros) and 7 (value 1)
        for (int k = 0; k < 2; ++k) { cm.Emit(w, 7); cm.Emit(w, 5); w.Put(31, 5); }
        w.Put(1, 1);                                                   // IMTF
        // NTREESD = 1
        w.Put(0, 1);
        // literal trees: tree 0 has only 'z', tree 1 only 'A'
        brw::WriteSimple(w, 256, {'z'});
        brw::WriteSimple(w, 256, {'A'});
        // two insert-and-copy trees: type 0 "4 literals, copy 4 at a distance code", type 1 "4 literals, last distance"
        const int cmd0 = brw::CommandSymbol(4, 2, false), cmd1 = brw::CommandSymbol(4, 2, true);
        brw::WriteSimple(w, 704, {cmd0});
        brw::WriteSimple(w, 704, {cmd1});
        brw::WriteSimple(w, 64, {16});                                  // distance symbol 16: distance 1 or 2 (1 extra bit)
        // commands: [type 0] 4 literals (literal block 0: tree 1 'A'), distance 1 -> "AAAA"
        w.Put(0, 1);                                                   // distance extra bit 0: distance 1
        // [switch to command type 1, count 1] 4 literals: literal block 0 is used up -> switch to type 1 (tree 0: 'z')
        itype.Emit(w, 1); icount.Emit(w, 0); w.Put(0, 2);
        ltype.Emit(w, 1); lcount.Emit(w, 0); w.Put(3, 2);
        w.ToByte();
        CHECK(Dec(w.bytes, out, &why) && out == Str("AAAAAAAAzzzz"));
        if (out != Str("AAAAAAAAzzzz")) std::cerr << "  block switch: got '" << std::string(out.begin(), out.end()) << "' " << why << "\n";
    }
    {
        // the context modes deciding something: four literal trees of one symbol each ('x', 'y', 0xC3, '.'), a context map
        // of 64 pseudo-random tree numbers; each literal is then fixed by the context of the two bytes before it, which this
        // test works out by itself (LSB6, MSB6 and, through the lookups checked above, UTF8 and Signed)
        const std::uint8_t treeByte[4] = {'x', 'y', 0xC3, '.'};
        std::uint8_t map[64];
        std::uint32_t ms = 2024;
        for (int k = 0; k < 64; ++k) map[k] = std::uint8_t(k < 4 ? k : Rng(ms) % 4);
        for (int mode = 0; mode < 4; ++mode) {
            Bytes want;
            std::uint8_t p1 = 0, p2 = 0;
            for (int k = 0; k < 200; ++k) {
                int ctx;
                if (mode == 0) ctx = p1 % 64;
                else if (mode == 1) ctx = p1 / 4;
                else if (mode == 2) ctx = bd::ContextLut(0, p1) | bd::ContextLut(1, p2);
                else ctx = bd::ContextLut(2, p1) * 8 + bd::ContextLut(2, p2);
                want.push_back(treeByte[map[ctx]]);
                p2 = p1; p1 = want.back();
            }
            brw::BitWriter w;
            brw::WriteWindow(w, 16);
            w.Put(1, 1); w.Put(0, 1);
            brw::WriteMlen(w, want.size());
            w.Put(0, 1); w.Put(0, 1); w.Put(0, 1);   // one block type each
            w.Put(0, 2); w.Put(0, 4);
            w.Put(std::uint64_t(mode), 2);
            w.Put(1, 1); w.Put(1, 3); w.Put(1, 1);   // NTREESL = 4 (x0011 with x = 1)
            w.Put(0, 1);                              // RLEMAX 0
            const brw::Code cm = brw::WriteSimple(w, 4, {0, 1, 2, 3}, 0);
            for (std::uint8_t v : map) cm.Emit(w, v);
            w.Put(0, 1);                              // no move-to-front
            w.Put(0, 1);                              // NTREESD = 1
            for (std::uint8_t b : treeByte) brw::WriteSimple(w, 256, {b});
            const int ic = brw::LengthCode(brw::kInsFirst, 200);
            brw::WriteSimple(w, 704, {brw::CommandSymbol(ic, 0, false)});
            brw::WriteSimple(w, 64, {0});
            w.Put(200 - brw::kInsFirst[ic], brw::kInsBits[ic]);
            w.ToByte();
            CHECK(Dec(w.bytes, out, &why) && out == want);
            if (out != want) std::cerr << "  context mode " << mode << ": " << why << "\n";
        }
    }
    {
        // a large literal-only meta-block with 8-bit codes for every byte (the RFC's single-symbol code-length example:
        // every length 8 through the repeat code 16)
        Bytes big(300000);
        std::uint32_t seed = 99;
        for (auto& b : big) b = std::uint8_t(Rng(seed));
        brw::BitWriter w;
        brw::WriteWindow(w, 22);
        brw::Command c; c.literals = big; c.last = true;
        brw::WriteCommands(w, {c}, true, big.size());
        w.ToByte();
        CHECK(Dec(w.bytes, out) && out == big);
        CHECK(w.bytes.size() < big.size() + 64);
    }

    // ---- 4. hostile input --------------------------------------------------------------------------------------------------
    {
        // a valid stream with a bit of everything, to cut and mutate
        Bytes base(3000);
        std::uint32_t seed = 5;
        for (auto& b : base) b = std::uint8_t('a' + Rng(seed) % 7);
        brw::BitWriter w;
        brw::WriteWindow(w, 12);
        brw::WriteStoredBlock(w, base.data(), 1500);
        std::vector<brw::Command> cmds;
        for (int k = 0; k < 40; ++k) {
            brw::Command c; c.literals = Bytes(base.begin() + 1500 + 30 * k, base.begin() + 1500 + 30 * k + 3); c.copy = 10 + std::uint32_t(k % 5);
            c.dist = brw::DistanceFor(std::uint64_t(1 + (k * 37) % 1400), 0, 0);
            cmds.push_back(c);
        }
        std::size_t mlen = 0;
        for (const auto& c : cmds) mlen += c.literals.size() + c.copy;
        brw::WriteCommands(w, cmds, true, mlen);
        w.ToByte();
        const Bytes good = w.bytes;
        CHECK(Dec(good, out) && out.size() == 1500 + mlen);
        // every cut-off
        int cutsRefused = 0;
        for (std::size_t cut = 0; cut < good.size(); ++cut) cutsRefused += Refused(Bytes(good.begin(), good.begin() + std::ptrdiff_t(cut)));
        CHECK(cutsRefused == int(good.size()));
        // output over the cap, and the cap exactly
        CHECK(Refused(good, 1500 + mlen - 1));
        CHECK(Dec(good, out, nullptr, 1500 + mlen) && out.size() == 1500 + mlen);
        // bytes after the stream: refused, or ignored when allowed
        Bytes extra = good; extra.push_back(0);
        CHECK(Refused(extra));
        CHECK(Dec(extra, out, nullptr, std::size_t(1) << 30, true) && out.size() == 1500 + mlen);
        // the large-window marker (window bit pattern 0010001)
        CHECK(Refused(Bytes{0x11, 0x03}));
        // fill bits after the last empty meta-block are not zero
        CHECK(Refused(Bytes{0x06 | 0x80}));
        // ISLASTEMPTY of a 1-byte stream and nothing else
        CHECK(Dec(Bytes{6}, out) && out.empty());
        // an uncompressed meta-block longer than the input
        {
            brw::BitWriter s; brw::WriteWindow(s, 16); s.Put(0, 1); brw::WriteMlen(s, 100); s.Put(1, 1); s.ToByte();
            s.bytes.insert(s.bytes.end(), 10, 'q'); brw::WriteLastEmpty(s);
            CHECK(Refused(s.bytes));
        }
        // MLEN with 5 nibbles and a zero top nibble
        {
            brw::BitWriter s; brw::WriteWindow(s, 16); s.Put(0, 1); s.Put(1, 2); s.Put(9, 20); s.Put(1, 1); s.ToByte();
            s.bytes.insert(s.bytes.end(), 10, 'q'); brw::WriteLastEmpty(s);
            CHECK(Refused(s.bytes));
        }
        // a meta-block of 16 MiB claimed by a few bytes: refused by a small cap before any memory is taken
        {
            brw::BitWriter s; brw::WriteWindow(s, 16); s.Put(0, 1); brw::WriteMlen(s, 1u << 24); s.Put(1, 1); s.ToByte();
            CHECK(Refused(s.bytes, 1u << 20));
        }
        // literals past MLEN, a copy past MLEN, a distance before the start (as a dictionary word too long)
        {
            brw::BitWriter s; brw::WriteWindow(s, 16);
            brw::Command c; c.literals = Str("abcdef"); c.last = true;
            brw::WriteCommands(s, {c}, true, 5); s.ToByte();
            CHECK(Refused(s.bytes));
            brw::BitWriter s2; brw::WriteWindow(s2, 16);
            brw::Command c2; c2.literals = Str("ab"); c2.copy = 10; c2.dist = brw::DistanceFor(1, 0, 0);
            brw::WriteCommands(s2, {c2}, true, 8); s2.ToByte();
            CHECK(Refused(s2.bytes));
        }
        // a special distance symbol that works out to 0: distance 3, then "last distance - 3"
        {
            brw::BitWriter s; brw::WriteWindow(s, 16);
            brw::Command a; a.literals = Str("abcd"); a.copy = 4; a.dist = brw::DistanceFor(3, 0, 0);
            brw::Command b; b.copy = 4; b.dist = {8, 0, 0};
            brw::WriteCommands(s, {a, b}, true, 12); s.ToByte();
            CHECK(Refused(s.bytes));
            brw::BitWriter s2; brw::WriteWindow(s2, 16);   // the same with "last distance - 1": distance 2, fine
            b.dist = {4, 0, 0};
            brw::WriteCommands(s2, {a, b}, true, 12); s2.ToByte();
            CHECK(Dec(s2.bytes, out) && out == Str("abcdbcdbdbdb"));
        }
        // a simple code that repeats a symbol, and one with a symbol past the alphabet
        {
            brw::BitWriter s; brw::WriteWindow(s, 16); brw::WritePlainHeader(s, true, 1, 0, 0, 0);
            s.Put(1, 2); s.Put(1, 2); s.Put('a', 8); s.Put('a', 8); s.ToByte();
            CHECK(Refused(s.bytes));
            brw::BitWriter s2; brw::WriteWindow(s2, 16); brw::WritePlainHeader(s2, true, 1, 0, 0, 0);
            brw::WriteSimple(s2, 256, {'a'});
            s2.Put(1, 2); s2.Put(0, 2); s2.Put(1000, 10); s2.ToByte();   // insert-and-copy symbol 1000 of 704
            CHECK(Refused(s2.bytes));
        }
        // a complex code whose lengths do not add up (incomplete: two symbols of length 2)
        {
            brw::BitWriter s; brw::WriteWindow(s, 16); brw::WritePlainHeader(s, true, 1, 0, 0, 0);
            std::vector<int> lens(256, 0); lens['a'] = 2; lens['b'] = 2;
            brw::WriteComplex(s, lens);
            s.ToByte();
            CHECK(Refused(s.bytes));
        }
        // reserved bit of a metadata block, non-zero fill bits before an uncompressed block
        CHECK(Refused(Bytes{0x0C | 0x10, 0x03}));
        {
            brw::BitWriter s; brw::WriteWindow(s, 16); s.Put(0, 1); brw::WriteMlen(s, 1); s.Put(1, 1);
            while (s.used) s.Bit(1);
            s.bytes.push_back('q'); brw::WriteLastEmpty(s);
            CHECK(Refused(s.bytes));
        }
        // random bytes and mutated valid streams: refused or decoded, never a crash or a hang
        std::uint32_t rs = 12345;
        int decodedRandom = 0;
        for (int it = 0; it < 20000; ++it) {
            Bytes junk(1 + Rng(rs) % 64);
            for (auto& b : junk) b = std::uint8_t(Rng(rs));
            decodedRandom += Dec(junk, out, nullptr, 1u << 20);
        }
        int decodedMutants = 0;
        for (int it = 0; it < 20000; ++it) {
            Bytes m = good;
            const int flips = 1 + int(Rng(rs) % 4);
            for (int f = 0; f < flips; ++f) m[Rng(rs) % m.size()] ^= std::uint8_t(1u << (Rng(rs) % 8));
            decodedMutants += Dec(m, out, nullptr, 1u << 20);
        }
        std::cout << "  random inputs decoded: " << decodedRandom << " of 20000; mutated streams decoded: " << decodedMutants << " of 20000\n";
        CHECK(decodedRandom < 20000 && decodedMutants < 20000);   // (and reaching here: no crash, no hang)
    }
    TEST_MAIN_RESULT();
}
