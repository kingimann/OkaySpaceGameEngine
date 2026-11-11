// MPEG-1/2/2.5 Layer III decoder, written for this engine from ISO/IEC 11172-3
// and 13818-3 (see the table notes in Mp3Tables.hpp). The pipeline per granule
// is: side info -> scalefactors -> Huffman -> requantisation -> stereo (MS and
// intensity) -> short block reordering -> alias reduction -> IMDCT with window
// switching -> frequency inversion -> polyphase synthesis.
//
// Everything that touches the input goes through BitReader, which returns zero
// bits past the end of its buffer, and every table index is clamped, so
// truncated or corrupt files only ever produce wrong audio, never a crash.
#include "okay/Audio/Mp3Decoder.hpp"
#include "Mp3Tables.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace okay {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kGranuleLines = 576;

// ---------------------------------------------------------------------------
// Static tables
// ---------------------------------------------------------------------------

// Scalefactor band boundaries. Index: 0-2 MPEG-1 (44.1, 48, 32 kHz), 3-5 MPEG-2
// (22.05, 24, 16 kHz), 6-8 MPEG-2.5 (11.025, 12, 8 kHz).
const int kBandLong[9][23] = {
    {0,4,8,12,16,20,24,30,36,44,52,62,74,90,110,134,162,196,238,288,342,418,576},
    {0,4,8,12,16,20,24,30,36,42,50,60,72,88,106,128,156,190,230,276,330,384,576},
    {0,4,8,12,16,20,24,30,36,44,54,66,82,102,126,156,194,240,296,364,448,550,576},
    {0,6,12,18,24,30,36,44,54,66,80,96,116,140,168,200,238,284,336,396,464,522,576},
    {0,6,12,18,24,30,36,44,54,66,80,96,114,136,162,194,232,278,332,394,464,540,576},
    {0,6,12,18,24,30,36,44,54,66,80,96,116,140,168,200,238,284,336,396,464,522,576},
    {0,6,12,18,24,30,36,44,54,66,80,96,116,140,168,200,238,284,336,396,464,522,576},
    {0,6,12,18,24,30,36,44,54,66,80,96,116,140,168,200,238,284,336,396,464,522,576},
    {0,12,24,36,48,60,72,88,108,132,160,192,232,280,336,400,476,566,568,570,572,574,576},
};
const int kBandShort[9][14] = {
    {0,4,8,12,16,22,30,40,52,66,84,106,136,192},
    {0,4,8,12,16,22,28,38,50,64,80,100,126,192},
    {0,4,8,12,16,22,30,42,58,78,104,138,180,192},
    {0,4,8,12,18,24,32,42,56,74,100,132,174,192},
    {0,4,8,12,18,26,36,48,62,80,104,136,180,192},
    {0,4,8,12,18,26,36,48,62,80,104,134,174,192},
    {0,4,8,12,18,26,36,48,62,80,104,134,174,192},
    {0,4,8,12,18,26,36,48,62,80,104,134,174,192},
    {0,8,16,24,36,52,72,96,124,160,162,164,166,192},
};
const int kPretab[22] = {0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,2,2,3,3,3,2,0};
const int kSlen1[16] = {0,0,0,0,3,1,1,1,2,2,2,3,3,3,4,4};
const int kSlen2[16] = {0,1,2,3,0,1,2,3,1,2,3,1,2,3,2,3};
// MPEG-2 scalefactor partition sizes: [table][long, short, mixed][partition].
const int kLsfPartitions[6][3][4] = {
    {{6,5,5,5},   {9,9,9,9},    {6,9,9,9}},
    {{6,5,7,3},   {9,9,12,6},   {6,9,12,6}},
    {{11,10,0,0}, {18,18,0,0},  {15,18,0,0}},
    {{7,7,7,0},   {12,12,12,0}, {6,15,12,0}},
    {{6,6,6,3},   {12,9,9,6},   {6,12,9,6}},
    {{8,8,5,0},   {15,12,9,0},  {6,18,9,0}},
};
const int kLinbits16[8] = {1,2,3,4,6,8,10,13};
const int kLinbits24[8] = {4,5,6,7,8,9,11,13};

// count1 quad table A (ISO Table B.7, table 32); table B is the 4-bit code
// 15 - value.
const std::uint8_t kQuadLenA[16] = {1,4,4,5,4,6,5,6,4,5,5,6,5,6,6,6};
const std::uint8_t kQuadCodeA[16] = {1,5,4,5,6,5,4,4,7,3,6,0,7,2,3,1};

// A binary decoding tree: nodes[i][bit] is a child node index (> 0), a leaf
// (-(symbol + 1)) or 0 for "no such code".
struct Trie {
    std::vector<std::array<std::int16_t, 2>> nodes;
    int dim = 0;

    void Build(const std::uint8_t* len, const std::uint32_t* code, int count, int dimension) {
        dim = dimension;
        nodes.assign(1, {0, 0});
        for (int s = 0; s < count; ++s) {
            int node = 0;
            for (int b = static_cast<int>(len[s]) - 1; b >= 0; --b) {
                const int bit = static_cast<int>((code[s] >> b) & 1u);
                if (b == 0) {
                    nodes[node][bit] = static_cast<std::int16_t>(-(s + 1));
                } else {
                    if (nodes[node][bit] <= 0) {
                        nodes.push_back({0, 0});
                        nodes[node][bit] = static_cast<std::int16_t>(nodes.size() - 1);
                    }
                    node = nodes[node][bit];
                }
            }
        }
    }
};

struct Tables {
    Trie pair[25];      // indexed by table number (16 and 24 are the escape tables)
    Trie quadA;
    int linbits[32] = {};
    int pairTable[32] = {};            // table_select -> index into pair[], 0 = none
    float pow43[8208] = {};
    float window[512] = {};            // synthesis window D[]
    float cosSynth[33][32] = {};
    float cos36[36][18] = {};
    float cos12[12][6] = {};
    float winLong[4][36] = {};         // block types 0, 1, 3 (index 2 unused)
    float winShort[12] = {};
    float aliasCs[8] = {};
    float aliasCa[8] = {};
    float isRatio[7][2] = {};          // MPEG-1 intensity: left / right factor per is_pos 0..6

    Tables() {
        using namespace mp3tab;
        const struct { int id; const std::uint8_t* len; const std::uint32_t* code; int count; int dim; } defs[] = {
            {1, kLen1, kCode1, 4, 2},     {2, kLen2, kCode2, 9, 3},     {3, kLen3, kCode3, 9, 3},
            {5, kLen5, kCode5, 16, 4},    {6, kLen6, kCode6, 16, 4},    {7, kLen7, kCode7, 36, 6},
            {8, kLen8, kCode8, 36, 6},    {9, kLen9, kCode9, 36, 6},    {10, kLen10, kCode10, 64, 8},
            {11, kLen11, kCode11, 64, 8}, {12, kLen12, kCode12, 64, 8}, {13, kLen13, kCode13, 256, 16},
            {15, kLen15, kCode15, 256, 16}, {16, kLen16, kCode16, 256, 16}, {24, kLen24, kCode24, 256, 16},
        };
        for (const auto& d : defs) pair[d.id].Build(d.len, d.code, d.count, d.dim);
        std::uint32_t quadCode[16];
        for (int i = 0; i < 16; ++i) quadCode[i] = kQuadCodeA[i];
        quadA.Build(kQuadLenA, quadCode, 16, 4);
        for (int s = 0; s < 32; ++s) {
            if (s == 0 || s == 4 || s == 14) { pairTable[s] = 0; linbits[s] = 0; }
            else if (s < 16) { pairTable[s] = s; linbits[s] = 0; }
            else if (s < 24) { pairTable[s] = 16; linbits[s] = kLinbits16[s - 16]; }
            else { pairTable[s] = 24; linbits[s] = kLinbits24[s - 24]; }
        }
        for (int i = 0; i < 8208; ++i) pow43[i] = static_cast<float>(std::pow(static_cast<double>(i), 4.0 / 3.0));

        // Synthesis window: 257 stored entries, the rest by (anti)symmetry.
        std::int32_t d[512];
        for (int i = 0; i <= 256; ++i) d[i] = kSynthWindow[i];
        for (int i = 1; i < 256; ++i) d[512 - i] = (i % 64 == 0) ? d[i] : -d[i];
        for (int i = 0; i < 512; ++i) window[i] = static_cast<float>(d[i]) / 65536.0f;

        // Matrixing: V[i] = C[16 + i] with C[m] = sum_k S[k] cos(m (2k+1) pi / 64);
        // only m = 0..32 are distinct (C[64 - m] = -C[m], C[128 - m] = C[m]).
        for (int m = 0; m <= 32; ++m)
            for (int k = 0; k < 32; ++k)
                cosSynth[m][k] = static_cast<float>(std::cos(m * (2 * k + 1) * kPi / 64.0));

        for (int i = 0; i < 36; ++i)
            for (int k = 0; k < 18; ++k)
                cos36[i][k] = static_cast<float>(std::cos(kPi / 72.0 * (2 * i + 1 + 18) * (2 * k + 1)));
        for (int i = 0; i < 12; ++i)
            for (int k = 0; k < 6; ++k)
                cos12[i][k] = static_cast<float>(std::cos(kPi / 24.0 * (2 * i + 1 + 6) * (2 * k + 1)));
        for (int i = 0; i < 36; ++i) {
            winLong[0][i] = static_cast<float>(std::sin(kPi / 36.0 * (i + 0.5)));
            // start window
            if (i < 18) winLong[1][i] = winLong[0][i];
            else if (i < 24) winLong[1][i] = 1.0f;
            else if (i < 30) winLong[1][i] = static_cast<float>(std::sin(kPi / 12.0 * (i - 18 + 0.5)));
            else winLong[1][i] = 0.0f;
            // stop window
            if (i < 6) winLong[3][i] = 0.0f;
            else if (i < 12) winLong[3][i] = static_cast<float>(std::sin(kPi / 12.0 * (i - 6 + 0.5)));
            else if (i < 18) winLong[3][i] = 1.0f;
            else winLong[3][i] = winLong[0][i];
        }
        for (int i = 0; i < 12; ++i) winShort[i] = static_cast<float>(std::sin(kPi / 12.0 * (i + 0.5)));

        static const double ci[8] = {-0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037};
        for (int i = 0; i < 8; ++i) {
            const double sq = std::sqrt(1.0 + ci[i] * ci[i]);
            aliasCs[i] = static_cast<float>(1.0 / sq);
            aliasCa[i] = static_cast<float>(ci[i] / sq);
        }
        for (int p = 0; p < 7; ++p) {
            const double a = p * kPi / 12.0;
            const double s = std::sin(a), c = std::cos(a);
            isRatio[p][0] = static_cast<float>(s / (s + c));
            isRatio[p][1] = static_cast<float>(c / (s + c));
        }
    }
};

const Tables& GetTables() {
    static const Tables t;
    return t;
}

// ---------------------------------------------------------------------------
// Bit reader (zero bits past the end)
// ---------------------------------------------------------------------------

struct BitReader {
    const std::uint8_t* data = nullptr;
    std::size_t sizeBits = 0;
    std::size_t pos = 0;

    BitReader() = default;
    BitReader(const std::uint8_t* d, std::size_t bytes) : data(d), sizeBits(bytes * 8) {}

    int Bit() {
        int v = 0;
        if (pos < sizeBits) v = (data[pos >> 3] >> (7 - (pos & 7))) & 1;
        ++pos;
        return v;
    }
    std::uint32_t Get(int n) {
        std::uint32_t v = 0;
        for (int i = 0; i < n; ++i) v = (v << 1) | static_cast<std::uint32_t>(Bit());
        return v;
    }
};

// ---------------------------------------------------------------------------
// Header and side info
// ---------------------------------------------------------------------------

struct FrameHeader {
    bool lsf = false;           // MPEG-2 or 2.5 (one granule per frame)
    bool mpeg25 = false;
    int srIndex = 0;
    int sampleRate = 0;
    int bitrate = 0;            // kbit/s
    int padding = 0;
    int mode = 0;               // 0 stereo, 1 joint, 2 dual, 3 mono
    int modeExt = 0;
    bool crc = false;           // CRC word follows the header
    int channels = 2;
    int frameBytes = 0;
    int bandSet = 0;            // row in kBandLong / kBandShort
};

bool ParseHeader(const std::uint8_t* p, std::size_t avail, FrameHeader& h) {
    if (avail < 4) return false;
    if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) return false;
    const int ver = (p[1] >> 3) & 3;
    const int layer = (p[1] >> 1) & 3;
    if (ver == 1 || layer != 1) return false;           // reserved version / not Layer III
    const int brIdx = (p[2] >> 4) & 15;
    const int srIdx = (p[2] >> 2) & 3;
    if (brIdx == 0 || brIdx == 15 || srIdx == 3) return false;   // free format unsupported / invalid
    static const int br1[15] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320};
    static const int br2[15] = {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160};
    static const int sr1[3] = {44100, 48000, 32000};
    h.lsf = ver != 3;
    h.mpeg25 = ver == 0;
    h.srIndex = srIdx;
    h.sampleRate = sr1[srIdx] >> (ver == 3 ? 0 : (ver == 2 ? 1 : 2));
    h.bitrate = h.lsf ? br2[brIdx] : br1[brIdx];
    h.padding = (p[2] >> 1) & 1;
    h.crc = ((p[1] & 1) == 0);
    h.mode = (p[3] >> 6) & 3;
    h.modeExt = (p[3] >> 4) & 3;
    h.channels = h.mode == 3 ? 1 : 2;
    h.frameBytes = (h.lsf ? 72000 : 144000) * h.bitrate / h.sampleRate + h.padding;
    h.bandSet = (ver == 3 ? 0 : (ver == 2 ? 3 : 6)) + srIdx;
    return h.frameBytes >= 4;
}

bool Compatible(const FrameHeader& a, const FrameHeader& b) {
    return a.lsf == b.lsf && a.mpeg25 == b.mpeg25 && a.sampleRate == b.sampleRate && a.channels == b.channels;
}

struct GranuleInfo {
    int part23 = 0;
    int bigValues = 0;
    int globalGain = 0;
    int sfCompress = 0;
    bool ws = false;
    int blockType = 0;
    bool mixed = false;
    int tableSel[3] = {0, 0, 0};
    int sbGain[3] = {0, 0, 0};
    int region0Count = 0;
    int region1Count = 0;
    bool preflag = false;
    int sfScale = 0;
    int count1Table = 0;
    bool IsShort() const { return ws && blockType == 2; }
};

struct SideInfo {
    int mainDataBegin = 0;
    int scfsi[2][4] = {};
    GranuleInfo gr[2][2];
};

void ParseSideInfo(BitReader& br, const FrameHeader& h, SideInfo& si) {
    const int nch = h.channels;
    const int ngr = h.lsf ? 1 : 2;
    si.mainDataBegin = static_cast<int>(br.Get(h.lsf ? 8 : 9));
    br.Get(h.lsf ? (nch == 1 ? 1 : 2) : (nch == 1 ? 5 : 3));   // private bits
    if (!h.lsf)
        for (int ch = 0; ch < nch; ++ch)
            for (int b = 0; b < 4; ++b) si.scfsi[ch][b] = br.Bit();
    for (int gr = 0; gr < ngr; ++gr) {
        for (int ch = 0; ch < nch; ++ch) {
            GranuleInfo& g = si.gr[gr][ch];
            g = GranuleInfo();
            g.part23 = static_cast<int>(br.Get(12));
            g.bigValues = static_cast<int>(br.Get(9));
            g.globalGain = static_cast<int>(br.Get(8));
            g.sfCompress = static_cast<int>(br.Get(h.lsf ? 9 : 4));
            g.ws = br.Bit() != 0;
            if (g.ws) {
                g.blockType = static_cast<int>(br.Get(2));
                g.mixed = br.Bit() != 0;
                g.tableSel[0] = static_cast<int>(br.Get(5));
                g.tableSel[1] = static_cast<int>(br.Get(5));
                for (int w = 0; w < 3; ++w) g.sbGain[w] = static_cast<int>(br.Get(3));
                if (g.blockType == 0) { g.ws = false; g.mixed = false; }   // reserved: treat as a normal block
                // The region sizes are implicit when the window switches.
                g.region0Count = (g.blockType == 2 && !g.mixed) ? 8 : 7;
                g.region1Count = 20 - g.region0Count;
            } else {
                for (int r = 0; r < 3; ++r) g.tableSel[r] = static_cast<int>(br.Get(5));
                g.region0Count = static_cast<int>(br.Get(4));
                g.region1Count = static_cast<int>(br.Get(3));
            }
            g.preflag = h.lsf ? false : br.Bit() != 0;
            g.sfScale = br.Bit();
            g.count1Table = br.Bit();
        }
    }
}

// ---------------------------------------------------------------------------
// Decoder state and granule decoding
// ---------------------------------------------------------------------------

struct ChannelState {
    float overlap[32][18] = {};
    float vbuf[16][64] = {};
    int vpos = 0;
};

struct Scalefactors {
    int l[22] = {};
    int s[13][3] = {};
};

class Layer3Decoder {
public:
    Layer3Decoder() : tab_(GetTables()) {}

    // Decodes one frame. Appends ngr*576*channels floats to out.
    // Returns false if the frame had to be replaced by silence.
    bool DecodeFrame(const std::uint8_t* frame, std::size_t frameLen, const FrameHeader& h, std::vector<float>& out);

private:
    const Tables& tab_;
    ChannelState ch_[2];
    std::vector<std::uint8_t> reservoir_;

    bool DecodeGranule(const FrameHeader& h, const SideInfo& si, int gr, const std::uint8_t* buf, std::size_t bufBytes,
                       std::size_t& bitOffset, Scalefactors sfPrev[2], float* outPcm);
    int ReadScalefactors(BitReader& br, const FrameHeader& h, const SideInfo& si, int gr, int ch,
                         const Scalefactors& prev, Scalefactors& sf) const;
    void ReadSpectrum(BitReader& br, std::size_t endBit, const FrameHeader& h, const GranuleInfo& g, int* is) const;
    void Requantize(const FrameHeader& h, const GranuleInfo& g, const Scalefactors& sf, const int* is, float* xr) const;
    void Stereo(const FrameHeader& h, const GranuleInfo g[2], const Scalefactors sf[2], float xr[2][kGranuleLines]) const;
    void Reorder(const FrameHeader& h, const GranuleInfo& g, float* xr) const;
    void AliasReduce(const GranuleInfo& g, float* xr) const;
    void Imdct(const GranuleInfo& g, const float* xr, ChannelState& st, float sub[18][32]) const;
    void Synthesize(ChannelState& st, const float sub[18][32], float* pcm, int stride) const;
    static int MixedLongLines(const FrameHeader& h) { return 3 * kBandShort[h.bandSet][3]; }
};

int Layer3Decoder::ReadScalefactors(BitReader& br, const FrameHeader& h, const SideInfo& si, int gr, int ch,
                                    const Scalefactors& prev, Scalefactors& sf) const {
    const GranuleInfo& g = si.gr[gr][ch];
    const std::size_t start = br.pos;
    sf = Scalefactors();
    const bool shortBlock = g.IsShort();
    if (!h.lsf) {
        const int slen1 = kSlen1[g.sfCompress & 15], slen2 = kSlen2[g.sfCompress & 15];
        if (shortBlock) {
            if (g.mixed) {
                for (int sfb = 0; sfb < 8; ++sfb) { sf.l[sfb] = static_cast<int>(br.Get(slen1)); }
                for (int sfb = 3; sfb < 6; ++sfb)
                    for (int w = 0; w < 3; ++w) { sf.s[sfb][w] = static_cast<int>(br.Get(slen1)); }
            } else {
                for (int sfb = 0; sfb < 6; ++sfb)
                    for (int w = 0; w < 3; ++w) { sf.s[sfb][w] = static_cast<int>(br.Get(slen1)); }
            }
            for (int sfb = 6; sfb < 12; ++sfb)
                for (int w = 0; w < 3; ++w) { sf.s[sfb][w] = static_cast<int>(br.Get(slen2)); }
        } else {
            static const int groupStart[5] = {0, 6, 11, 16, 21};
            for (int grp = 0; grp < 4; ++grp) {
                const int slen = grp < 2 ? slen1 : slen2;
                for (int sfb = groupStart[grp]; sfb < groupStart[grp + 1]; ++sfb) {
                    if (gr == 1 && si.scfsi[ch][grp]) sf.l[sfb] = prev.l[sfb];
                    else sf.l[sfb] = static_cast<int>(br.Get(slen));
                }
            }
        }
    } else {
        int sc = g.sfCompress;
        int slen[4] = {0, 0, 0, 0};
        int table = 0;
        const bool intensityCh = (h.modeExt & 1) != 0 && h.mode == 1 && ch == 1;
        if (intensityCh) {
            sc >>= 1;
            if (sc < 180) { slen[0] = sc / 36; slen[1] = (sc % 36) / 6; slen[2] = (sc % 36) % 6; table = 3; }
            else if (sc < 244) { sc -= 180; slen[0] = (sc & 63) >> 4; slen[1] = (sc & 15) >> 2; slen[2] = sc & 3; table = 4; }
            else { sc -= 244; slen[0] = sc / 3; slen[1] = sc % 3; table = 5; }
        } else {
            if (sc < 400) { slen[0] = (sc >> 4) / 5; slen[1] = (sc >> 4) % 5; slen[2] = (sc & 15) >> 2; slen[3] = sc & 3; table = 0; }
            else if (sc < 500) { sc -= 400; slen[0] = (sc >> 2) / 5; slen[1] = (sc >> 2) % 5; slen[2] = sc & 3; table = 1; }
            else { sc -= 500; slen[0] = sc / 3; slen[1] = sc % 3; table = 2; }
        }
        const int kind = shortBlock ? (g.mixed ? 2 : 1) : 0;
        int k = 0;
        for (int part = 0; part < 4; ++part) {
            const int count = kLsfPartitions[table][kind][part];
            for (int i = 0; i < count; ++i, ++k) {
                const int v = slen[part] > 0 ? static_cast<int>(br.Get(slen[part])) : 0;
                if (!shortBlock) {
                    if (k < 21) { sf.l[k] = v; }
                } else if (g.mixed && k < 6) {
                    sf.l[k] = v;
                } else {
                    const int kk = g.mixed ? k - 6 : k;
                    const int sfb = (g.mixed ? 3 : 0) + kk / 3;
                    if (sfb < 12) { sf.s[sfb][kk % 3] = v; }
                }
            }
        }
    }
    return static_cast<int>(br.pos - start);
}

void Layer3Decoder::ReadSpectrum(BitReader& br, std::size_t endBit, const FrameHeader& h, const GranuleInfo& g, int* is) const {
    std::fill(is, is + kGranuleLines, 0);
    const int* bandLong = kBandLong[h.bandSet];
    const int bigLines = std::min(g.bigValues * 2, kGranuleLines);
    int r0, r1;
    if (g.ws) {
        // Window switching: region 0 covers the first three short bands (36
        // lines, 72 at 8 kHz) for short blocks, otherwise it ends at long band
        // 8 (36 lines at 32-48 kHz, 54 at the MPEG-2 rates, 108 at 8 kHz);
        // region 1 takes the rest.
        r0 = g.IsShort() ? MixedLongLines(h) : bandLong[8];
        r1 = kGranuleLines;
    } else {
        r0 = bandLong[std::min(g.region0Count + 1, 22)];
        r1 = bandLong[std::min(g.region0Count + g.region1Count + 2, 22)];
    }
    r0 = std::min(r0, bigLines);
    r1 = std::min(std::max(r1, r0), bigLines);
    const int regionEnd[3] = {r0, r1, bigLines};

    int line = 0;
    bool bad = false;
    for (int region = 0; region < 3 && !bad; ++region) {
        const int sel = g.tableSel[region] & 31;
        const int tn = tab_.pairTable[sel];
        const int lin = tab_.linbits[sel];
        if (tn == 0) {
            // Table 0 (or an unused table number): zeros, no bits.
            line = regionEnd[region];
            continue;
        }
        const Trie& trie = tab_.pair[tn];
        for (; line < regionEnd[region]; line += 2) {
            if (br.pos >= endBit) { bad = true; break; }
            int node = 0, sym = -1;
            for (int depth = 0; depth < 24; ++depth) {
                const std::int16_t nx = trie.nodes[node][br.Bit()];
                if (nx < 0) { sym = -nx - 1; break; }
                if (nx == 0) break;
                node = nx;
            }
            if (sym < 0) { bad = true; break; }
            int x = sym / trie.dim, y = sym % trie.dim;
            if (lin > 0 && x == 15) x += static_cast<int>(br.Get(lin));
            if (x != 0 && br.Bit()) x = -x;
            if (lin > 0 && y == 15) y += static_cast<int>(br.Get(lin));
            if (y != 0 && br.Bit()) y = -y;
            if (br.pos > endBit) { bad = true; break; }
            is[line] = x;
            is[line + 1] = y;
        }
    }
    if (bad) return;

    // count1 region: quadruples of -1/0/1 until the granule's bits run out.
    const bool tableA = g.count1Table == 0;
    while (line < kGranuleLines && br.pos < endBit) {
        int idx = 0;
        if (tableA) {
            int node = 0, sym = -1;
            for (int depth = 0; depth < 8; ++depth) {
                const std::int16_t nx = tab_.quadA.nodes[node][br.Bit()];
                if (nx < 0) { sym = -nx - 1; break; }
                if (nx == 0) break;
                node = nx;
            }
            if (sym < 0) return;
            idx = sym;
        } else {
            idx = 15 - static_cast<int>(br.Get(4));
        }
        int v[4];
        for (int i = 0; i < 4; ++i) {
            v[i] = (idx >> (3 - i)) & 1;
            if (v[i] && br.Bit()) v[i] = -v[i];
        }
        if (br.pos > endBit) return;     // the last quad ran past the granule: not real data
        for (int i = 0; i < 4 && line + i < kGranuleLines; ++i) is[line + i] = v[i];
        line += 4;
    }
}

void Layer3Decoder::Requantize(const FrameHeader& h, const GranuleInfo& g, const Scalefactors& sf, const int* is, float* xr) const {
    const int* bandLong = kBandLong[h.bandSet];
    const int* bandShort = kBandShort[h.bandSet];
    const int mult = 2 * (1 + g.sfScale);      // quarter-octave steps per scalefactor unit
    auto value = [&](int v, float scale) {
        const int a = v < 0 ? -v : v;
        const float m = tab_.pow43[std::min(a, 8207)] * scale;
        return v < 0 ? -m : m;
    };
    std::fill(xr, xr + kGranuleLines, 0.0f);
    int longEndLine = kGranuleLines;     // long bands cover [0, longEndLine)
    int shortFromBand = 0;
    if (g.IsShort()) {
        longEndLine = g.mixed ? MixedLongLines(h) : 0;
        shortFromBand = g.mixed ? 3 : 0;
    }
    for (int sfb = 0; sfb < 22; ++sfb) {
        const int a = bandLong[sfb], b = bandLong[sfb + 1];
        if (a >= longEndLine) break;
        const int pre = g.preflag ? kPretab[sfb] : 0;
        const int qe = (g.globalGain - 210) - mult * (sf.l[sfb] + pre);
        const float scale = std::exp2(static_cast<float>(qe) * 0.25f);
        for (int i = a; i < b && i < longEndLine; ++i) xr[i] = is[i] != 0 ? value(is[i], scale) : 0.0f;
    }
    if (g.IsShort()) {
        for (int sfb = shortFromBand; sfb < 13; ++sfb) {
            const int width = bandShort[sfb + 1] - bandShort[sfb];
            const int start3 = 3 * bandShort[sfb];
            for (int w = 0; w < 3; ++w) {
                const int sfv = sfb < 12 ? sf.s[sfb][w] : 0;
                const int qe = (g.globalGain - 210) - 8 * g.sbGain[w] - mult * sfv;
                const float scale = std::exp2(static_cast<float>(qe) * 0.25f);
                for (int i = 0; i < width; ++i) {
                    const int idx = start3 + w * width + i;
                    if (idx < kGranuleLines && is[idx] != 0) xr[idx] = value(is[idx], scale);
                }
            }
        }
    }
}

void Layer3Decoder::Stereo(const FrameHeader& h, const GranuleInfo g[2], const Scalefactors sf[2], float xr[2][kGranuleLines]) const {
    const bool ms = h.mode == 1 && (h.modeExt & 2) != 0;
    const bool intensity = h.mode == 1 && (h.modeExt & 1) != 0;
    if (!ms && !intensity) return;
    // Per-line intensity position (-1 = not intensity coded).
    int isPos[kGranuleLines];
    std::fill(isPos, isPos + kGranuleLines, -1);
    if (intensity) {
        const GranuleInfo& g1 = g[1];
        const int* bandLong = kBandLong[h.bandSet];
        const int* bandShort = kBandShort[h.bandSet];
        // Positions 7 (MPEG-1) / 16 and up (MPEG-2) are illegal: such bands are
        // coded as ordinary stereo.
        auto legal = [&](int pos) { return pos < (h.lsf ? 16 : 7); };
        bool shortNonZero = false;
        if (g1.IsShort()) {
            for (int w = 0; w < 3; ++w) {
                int lastBand = g1.mixed ? 2 : -1;     // bands below 3 belong to the long part when mixed
                for (int sfb = 12; sfb >= (g1.mixed ? 3 : 0); --sfb) {
                    const int width = bandShort[sfb + 1] - bandShort[sfb];
                    const int base = 3 * bandShort[sfb] + w * width;
                    bool nz = false;
                    for (int i = 0; i < width && !nz; ++i) nz = xr[1][base + i] != 0.0f;
                    if (nz) { lastBand = sfb; shortNonZero = true; break; }
                }
                for (int sfb = lastBand + 1; sfb < 13; ++sfb) {
                    const int src = sfb < 12 ? sfb : 11;
                    const int pos = sf[1].s[src][w];
                    if (!legal(pos)) continue;
                    const int width = bandShort[sfb + 1] - bandShort[sfb];
                    const int base = 3 * bandShort[sfb] + w * width;
                    for (int i = 0; i < width && base + i < kGranuleLines; ++i) isPos[base + i] = pos;
                }
            }
        }
        // Long bands (of a mixed block only when no short band above carries data):
        // everything above the last non-zero right-channel line.
        if ((!g1.IsShort() || g1.mixed) && !shortNonZero) {
            const int longEnd = g1.IsShort() ? MixedLongLines(h) : kGranuleLines;
            int last = -1;
            for (int i = longEnd - 1; i >= 0; --i) if (xr[1][i] != 0.0f) { last = i; break; }
            int firstSfb = 0;
            while (firstSfb < 22 && bandLong[firstSfb + 1] <= last) ++firstSfb;
            if (last >= 0) ++firstSfb;    // band holding the last non-zero line stays normal
            for (int sfb = firstSfb; sfb < 22 && bandLong[sfb] < longEnd; ++sfb) {
                const int src = sfb < 21 ? sfb : 20;
                const int pos = sf[1].l[src];
                if (!legal(pos)) continue;
                for (int i = bandLong[sfb]; i < bandLong[sfb + 1] && i < longEnd; ++i) isPos[i] = pos;
            }
        }
    }
    const float invSqrt2 = 0.70710678118654752440f;
    const int isScale = (g[1].sfCompress & 1);
    const float lsfStep = isScale ? 0.70710678118654752440f : 0.84089641525371454303f;   // 2^-1/2 or 2^-1/4
    for (int i = 0; i < kGranuleLines; ++i) {
        const float a = xr[0][i];
        if (isPos[i] >= 0) {
            const int p = isPos[i];
            float kl, kr;
            if (!h.lsf) {
                kl = tab_.isRatio[p][0];
                kr = tab_.isRatio[p][1];
            } else if (p == 0) {
                kl = kr = 1.0f;
            } else if (p & 1) {
                kl = std::pow(lsfStep, static_cast<float>((p + 1) / 2));
                kr = 1.0f;
            } else {
                kl = 1.0f;
                kr = std::pow(lsfStep, static_cast<float>(p / 2));
            }
            xr[0][i] = a * kl;
            xr[1][i] = a * kr;
        } else if (ms) {
            const float b = xr[1][i];
            xr[0][i] = (a + b) * invSqrt2;
            xr[1][i] = (a - b) * invSqrt2;
        }
    }
}

void Layer3Decoder::Reorder(const FrameHeader& h, const GranuleInfo& g, float* xr) const {
    if (!g.IsShort()) return;
    const int* bandShort = kBandShort[h.bandSet];
    float tmp[kGranuleLines];
    std::memcpy(tmp, xr, sizeof(tmp));
    const int fromBand = g.mixed ? 3 : 0;
    for (int sfb = fromBand; sfb < 13; ++sfb) {
        const int width = bandShort[sfb + 1] - bandShort[sfb];
        const int start3 = 3 * bandShort[sfb];
        for (int w = 0; w < 3; ++w)
            for (int i = 0; i < width; ++i) {
                const int src = start3 + w * width + i;
                const int dst = start3 + 3 * i + w;
                if (src < kGranuleLines && dst < kGranuleLines) xr[dst] = tmp[src];
            }
    }
}

void Layer3Decoder::AliasReduce(const GranuleInfo& g, float* xr) const {
    int boundaries = 31;
    if (g.IsShort()) {
        if (!g.mixed) return;
        boundaries = 1;     // only between the two long subbands of a mixed block
    }
    for (int sb = 1; sb <= boundaries; ++sb) {
        for (int i = 0; i < 8; ++i) {
            const float a = xr[18 * sb - 1 - i];
            const float b = xr[18 * sb + i];
            xr[18 * sb - 1 - i] = a * tab_.aliasCs[i] - b * tab_.aliasCa[i];
            xr[18 * sb + i] = b * tab_.aliasCs[i] + a * tab_.aliasCa[i];
        }
    }
}

void Layer3Decoder::Imdct(const GranuleInfo& g, const float* xr, ChannelState& st, float sub[18][32]) const {
    const int mixedSubbands = (g.IsShort() && g.mixed) ? 2 : 0;
    for (int sb = 0; sb < 32; ++sb) {
        const float* in = xr + 18 * sb;
        float out[36];
        bool zero = true;
        for (int k = 0; k < 18; ++k) if (in[k] != 0.0f) { zero = false; break; }
        if (zero) {
            std::fill(out, out + 36, 0.0f);
        } else if (g.IsShort() && sb >= mixedSubbands) {
            std::fill(out, out + 36, 0.0f);
            for (int w = 0; w < 3; ++w) {
                float x[6];
                for (int k = 0; k < 6; ++k) x[k] = in[3 * k + w];
                for (int i = 0; i < 12; ++i) {
                    float s = 0.0f;
                    for (int k = 0; k < 6; ++k) s += x[k] * tab_.cos12[i][k];
                    out[6 + 6 * w + i] += s * tab_.winShort[i];
                }
            }
        } else {
            const int bt = (g.ws && sb >= mixedSubbands) ? g.blockType : 0;
            const float* win = tab_.winLong[bt & 3];
            for (int i = 0; i < 36; ++i) {
                float s = 0.0f;
                for (int k = 0; k < 18; ++k) s += in[k] * tab_.cos36[i][k];
                out[i] = s * win[i];
            }
        }
        float* ov = st.overlap[sb];
        for (int i = 0; i < 18; ++i) {
            float v = out[i] + ov[i];
            ov[i] = out[18 + i];
            if ((sb & 1) && (i & 1)) v = -v;     // frequency inversion
            sub[i][sb] = v;
        }
    }
}

void Layer3Decoder::Synthesize(ChannelState& st, const float sub[18][32], float* pcm, int stride) const {
    for (int t = 0; t < 18; ++t) {
        const float* s = sub[t];
        st.vpos = (st.vpos + 15) & 15;      // newest slot goes to the front of the FIFO
        float* v = st.vbuf[st.vpos];
        float c[33];
        for (int m = 0; m <= 32; ++m) {
            float acc = 0.0f;
            const float* row = tab_.cosSynth[m];
            for (int k = 0; k < 32; ++k) acc += s[k] * row[k];
            c[m] = acc;
        }
        for (int i = 0; i < 64; ++i) {
            const int m = 16 + i;
            float val;
            if (m <= 32) val = c[m];
            else if (m <= 64) val = -c[64 - m];
            else val = -c[m - 64];
            v[i] = val;
        }
        for (int j = 0; j < 32; ++j) {
            float acc = 0.0f;
            for (int a = 0; a < 8; ++a) {
                const float* v0 = st.vbuf[(st.vpos + 2 * a) & 15];
                const float* v1 = st.vbuf[(st.vpos + 2 * a + 1) & 15];
                acc += tab_.window[64 * a + j] * v0[j];
                acc += tab_.window[64 * a + 32 + j] * v1[32 + j];
            }
            pcm[(t * 32 + j) * stride] = acc;
        }
    }
}

bool Layer3Decoder::DecodeGranule(const FrameHeader& h, const SideInfo& si, int gr, const std::uint8_t* buf,
                                  std::size_t bufBytes, std::size_t& bitOffset, Scalefactors sfPrev[2], float* outPcm) {
    const int nch = h.channels;
    int is[2][kGranuleLines];
    float xr[2][kGranuleLines];
    Scalefactors sf[2];
    GranuleInfo g[2];
    const std::size_t totalBits = bufBytes * 8;
    for (int ch = 0; ch < nch; ++ch) {
        g[ch] = si.gr[gr][ch];
        if (h.lsf) {
            // MPEG-2 has no preflag bit: the last scalefactor range implies it.
            const bool intensityCh = (h.modeExt & 1) != 0 && h.mode == 1 && ch == 1;
            g[ch].preflag = !intensityCh && g[ch].sfCompress >= 500;
        }
        BitReader br(buf, bufBytes);
        br.pos = std::min(bitOffset, totalBits);
        const std::size_t start = bitOffset;
        const std::size_t end = std::min(start + static_cast<std::size_t>(g[ch].part23), totalBits);
        const int part2 = ReadScalefactors(br, h, si, gr, ch, sfPrev[ch], sf[ch]);
        if (!h.lsf && gr == 0) sfPrev[ch] = sf[ch];
        std::fill(is[ch], is[ch] + kGranuleLines, 0);
        if (static_cast<std::size_t>(part2) <= static_cast<std::size_t>(g[ch].part23) && br.pos <= end)
            ReadSpectrum(br, end, h, g[ch], is[ch]);
        Requantize(h, g[ch], sf[ch], is[ch], xr[ch]);
        bitOffset = start + static_cast<std::size_t>(g[ch].part23);
    }
    if (nch == 2) Stereo(h, g, sf, xr);
    float sub[18][32];
    float pcm[576];
    for (int ch = 0; ch < nch; ++ch) {
        Reorder(h, g[ch], xr[ch]);
        AliasReduce(g[ch], xr[ch]);
        Imdct(g[ch], xr[ch], ch_[ch], sub);
        Synthesize(ch_[ch], sub, pcm, 1);
        for (int i = 0; i < 576; ++i) outPcm[i * nch + ch] = pcm[i];
    }
    return true;
}

bool Layer3Decoder::DecodeFrame(const std::uint8_t* frame, std::size_t frameLen, const FrameHeader& h, std::vector<float>& out) {
    const int nch = h.channels;
    const int ngr = h.lsf ? 1 : 2;
    const std::size_t outStart = out.size();
    out.resize(outStart + static_cast<std::size_t>(ngr) * 576 * nch, 0.0f);

    const std::size_t sideStart = 4 + (h.crc ? 2 : 0);
    const std::size_t sideLen = h.lsf ? (nch == 1 ? 9 : 17) : (nch == 1 ? 17 : 32);
    if (frameLen < sideStart + sideLen) return false;
    BitReader sbr(frame + sideStart, sideLen);
    SideInfo si;
    ParseSideInfo(sbr, h, si);

    const std::uint8_t* mainData = frame + sideStart + sideLen;
    const std::size_t mainLen = frameLen - sideStart - sideLen;

    const std::size_t begin = static_cast<std::size_t>(si.mainDataBegin);
    bool ok = begin <= reservoir_.size();
    bool decoded = false;
    if (ok) {
        std::vector<std::uint8_t> buf;
        buf.reserve(begin + mainLen);
        buf.insert(buf.end(), reservoir_.end() - static_cast<std::ptrdiff_t>(begin), reservoir_.end());
        buf.insert(buf.end(), mainData, mainData + mainLen);
        std::size_t bitOffset = 0;
        Scalefactors sfPrev[2];
        decoded = true;
        for (int gr = 0; gr < ngr; ++gr)
            DecodeGranule(h, si, gr, buf.data(), buf.size(), bitOffset, sfPrev,
                          out.data() + outStart + static_cast<std::size_t>(gr) * 576 * nch);
    }
    // The bit reservoir keeps the last 511 bytes of main data for later frames.
    reservoir_.insert(reservoir_.end(), mainData, mainData + mainLen);
    if (reservoir_.size() > 511) reservoir_.erase(reservoir_.begin(), reservoir_.end() - 511);
    return decoded;
}

// ---------------------------------------------------------------------------
// Stream level: ID3v2, frame search, Xing/Info/VBRI
// ---------------------------------------------------------------------------

std::size_t SkipId3v2(const unsigned char* d, std::size_t n) {
    std::size_t pos = 0;
    while (pos + 10 <= n && d[pos] == 'I' && d[pos + 1] == 'D' && d[pos + 2] == '3' && d[pos + 3] != 0xFF &&
           d[pos + 4] != 0xFF && ((d[pos + 6] | d[pos + 7] | d[pos + 8] | d[pos + 9]) & 0x80) == 0) {
        std::size_t size = (static_cast<std::size_t>(d[pos + 6]) << 21) | (static_cast<std::size_t>(d[pos + 7]) << 14) |
                           (static_cast<std::size_t>(d[pos + 8]) << 7) | static_cast<std::size_t>(d[pos + 9]);
        std::size_t total = 10 + size + ((d[pos + 5] & 0x10) ? 10 : 0);
        if (pos + total > n) return n;
        pos += total;
    }
    return pos;
}

struct XingInfo {
    bool present = false;
    bool hasFrames = false;
    std::uint32_t frames = 0;
    bool hasLame = false;
    int delay = 0;
    int padding = 0;
};

std::uint32_t Be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

// Looks for a Xing/Info (or VBRI) header in the first frame of a stream.
XingInfo ParseXing(const std::uint8_t* frame, std::size_t len, const FrameHeader& h) {
    XingInfo x;
    const std::size_t sideLen = h.lsf ? (h.channels == 1 ? 9 : 17) : (h.channels == 1 ? 17 : 32);
    const std::size_t at = 4 + (h.crc ? 2 : 0) + sideLen;
    if (at + 8 <= len && (std::memcmp(frame + at, "Xing", 4) == 0 || std::memcmp(frame + at, "Info", 4) == 0)) {
        x.present = true;
        const std::uint32_t flags = Be32(frame + at + 4);
        std::size_t p = at + 8;
        if ((flags & 1) && p + 4 <= len) { x.hasFrames = true; x.frames = Be32(frame + p); p += 4; }
        else if (flags & 1) p += 4;
        if (flags & 2) p += 4;
        if (flags & 4) p += 100;
        if (flags & 8) p += 4;
        // LAME extension: 9 byte version string, then 12 bytes before the delay/padding word.
        if (p + 21 + 3 <= len && std::memcmp(frame + p, "LAME", 4) == 0) {
            x.hasLame = true;
            const std::uint8_t* q = frame + p + 21;
            x.delay = (q[0] << 4) | (q[1] >> 4);
            x.padding = ((q[1] & 15) << 8) | q[2];
        } else if (p + 21 + 3 <= len && (std::memcmp(frame + p, "Lavf", 4) == 0 || std::memcmp(frame + p, "Lavc", 4) == 0 ||
                                          std::memcmp(frame + p, "GOGO", 4) == 0)) {
            x.hasLame = true;
            const std::uint8_t* q = frame + p + 21;
            x.delay = (q[0] << 4) | (q[1] >> 4);
            x.padding = ((q[1] & 15) << 8) | q[2];
        }
    } else if (36 + 18 <= len && std::memcmp(frame + 36, "VBRI", 4) == 0) {
        x.present = true;
    }
    return x;
}

} // namespace

bool DecodeMp3(const unsigned char* data, std::size_t size, Mp3Audio& out, std::string* error, const Mp3Options* options) {
    out = Mp3Audio();
    if (!data || size < 4) {
        if (error) *error = "not a readable MP3 file";
        return false;
    }
    const bool trim = options ? options->trimGapless : Mp3Options().trimGapless;
    Layer3Decoder decoder;
    FrameHeader first;
    bool haveFirst = false;
    XingInfo xing;
    std::size_t pos = SkipId3v2(data, size);
    bool exact = false;      // next frame is expected at `pos` exactly
    std::uint32_t xingFrames = 0;

    while (pos + 4 <= size) {
        FrameHeader h;
        bool found = false;
        if (exact && ParseHeader(data + pos, size - pos, h) && pos + h.frameBytes <= size &&
            (!haveFirst || Compatible(first, h))) {
            found = true;
        } else {
            // Resynchronise: scan for a header confirmed by the one after it.
            for (std::size_t p = exact ? pos + 1 : pos; p + 4 <= size; ++p) {
                if (data[p] != 0xFF) continue;
                if (!ParseHeader(data + p, size - p, h)) continue;
                if (p + static_cast<std::size_t>(h.frameBytes) > size) continue;
                if (haveFirst && !Compatible(first, h)) continue;
                const std::size_t next = p + static_cast<std::size_t>(h.frameBytes);
                FrameHeader h2;
                const bool atEnd = next == size;
                const bool nextOk = next + 4 <= size && ParseHeader(data + next, size - next, h2) && Compatible(h, h2);
                if (atEnd || nextOk || (haveFirst && next + 4 > size)) { pos = p; found = true; break; }
            }
        }
        if (!found) break;
        const std::uint8_t* frame = data + pos;
        const std::size_t len = static_cast<std::size_t>(h.frameBytes);
        if (!haveFirst) {
            first = h;
            haveFirst = true;
            out.sampleRate = h.sampleRate;
            out.channels = h.channels;
            xing = ParseXing(frame, len, h);
            if (xing.present) {
                if (xing.hasLame) {
                    out.hasLameTag = true;
                    out.encoderDelay = xing.delay;
                    out.encoderPadding = xing.padding;
                }
                if (xing.hasFrames) xingFrames = xing.frames;
                pos += len;
                exact = true;
                continue;       // the header frame carries no audio
            }
        }
        if (decoder.DecodeFrame(frame, len, h, out.samples)) ++out.framesDecoded;
        else ++out.framesSkipped;
        pos += len;
        exact = true;
    }

    if (!haveFirst || out.samples.empty()) {
        out = Mp3Audio();
        if (error) *error = "not a readable MP3 file";
        return false;
    }

    if (trim && out.hasLameTag) {
        const std::size_t spf = first.lsf ? 576 : 1152;
        const std::size_t ch = static_cast<std::size_t>(out.channels);
        const std::size_t total = out.samples.size() / ch;
        const std::size_t startSkip = static_cast<std::size_t>(out.encoderDelay) + 529;
        std::size_t endKeep = total;
        if (xingFrames > 0) {
            const std::size_t all = static_cast<std::size_t>(xingFrames) * spf;
            const std::size_t pad = static_cast<std::size_t>(out.encoderPadding);
            const std::size_t e = all > pad ? all - pad + 529 : 0;
            endKeep = std::min(total, e);
        }
        if (startSkip < endKeep) {
            out.samples.erase(out.samples.begin() + static_cast<std::ptrdiff_t>(endKeep * ch), out.samples.end());
            out.samples.erase(out.samples.begin(), out.samples.begin() + static_cast<std::ptrdiff_t>(startSkip * ch));
        }
    }
    return true;
}

} // namespace okay
