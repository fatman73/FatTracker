// Sonic Arranger (Carsten Schlote, Branko Mikic, Carsten Herbst) replayer, ported from NostalgicPlayer's
// SonicArrangerWorker.cs (Thomas Neumann, MIT licence) onto AmigaSynthPlayer, with sub-songs. Reads the editor
// format ("SOARV1.0"), its lh.library packed form ("@OARV1.0", decompressed like LibAncient's
// LhLibraryDecompressor) and ripped files (a player with the data behind it, or the data alone), which are
// turned into the editor format first, as NostalgicPlayer's SonicArrangerFinalFormat converter does.
// Index checks added where the C# code relied on exceptions: section counts against the file size, sample /
// wave / ADSR / AMF table numbers, instrument numbers, arpeggio positions, positions and the 128 byte synth
// buffer every synth effect works on (writes outside it are dropped, reads give 0). A vibrato level of 0 (a
// division by zero in NostalgicPlayer) leaves the period alone; a tempo of 0 plays at 50 Hz. The noise
// generators use their own pseudo random numbers (the same on every run).
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "core/amiga_formats.h"
#include "core/amiga_synth.h"

namespace {
const uint16_t kPeriods[109] = {
    0,     13696, 12928, 12192, 11520, 10848, 10240, 9664, 9120, 8608, 8128, 7680, 7248, 6848, 6464, 6096,
    5760,  5424,  5120,  4832,  4560,  4304,  4064,  3840, 3624, 3424, 3232, 3048, 2880, 2712, 2560, 2416,
    2280,  2152,  2032,  1920,  1812,  1712,  1616,  1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016, 960,
    906,   856,   808,   762,   720,   678,   640,   604,  570,  538,  508,  480,  453,  428,  404,  381,
    360,   339,   320,   302,   285,   269,   254,   240,  226,  214,  202,  190,  180,  170,  160,  151,
    143,   135,   127,   120,   113,   107,   101,   95,   90,   85,   80,   75,   71,   67,   63,   60,
    56,    53,    50,    47,    45,    42,    40,    37,   35,   33,   31,   30,   28};
const int8_t kVibrato[256] = {
    0,    3,    6,    9,    12,   16,   19,   22,   25,   28,   31,   34,   37,   40,   43,   46,   49,   52,   54,
    57,   60,   63,   66,   68,   71,   73,   76,   78,   81,   83,   86,   88,   90,   92,   94,   96,   98,   100,
    102,  104,  106,  108,  109,  111,  112,  114,  115,  116,  118,  119,  120,  121,  122,  123,  123,  124,  125,
    125,  126,  126,  126,  127,  127,  127,  127,  127,  127,  127,  126,  126,  125,  125,  124,  124,  123,  122,
    121,  120,  119,  118,  117,  116,  114,  113,  112,  110,  108,  107,  105,  103,  101,  99,   97,   95,   93,
    91,   89,   87,   84,   82,   80,   77,   75,   72,   69,   67,   64,   61,   59,   56,   53,   50,   47,   44,
    41,   39,   36,   32,   29,   26,   23,   20,   17,   14,   11,   8,    5,    2,    -1,   -4,   -7,   -10,  -14,
    -17,  -20,  -23,  -26,  -29,  -32,  -35,  -38,  -41,  -44,  -47,  -50,  -53,  -55,  -58,  -61,  -64,  -66,  -69,
    -72,  -74,  -77,  -79,  -82,  -84,  -86,  -88,  -91,  -93,  -95,  -97,  -99,  -101, -103, -104, -106, -108, -109,
    -111, -112, -114, -115, -116, -118, -119, -120, -121, -122, -122, -123, -124, -124, -125, -125, -126, -126, -126,
    -126, -126, -126, -126, -126, -126, -126, -125, -125, -124, -124, -123, -122, -121, -120, -119, -118, -117, -116,
    -115, -113, -112, -110, -109, -107, -105, -104, -102, -100, -98,  -96,  -94,  -92,  -90,  -87,  -85,  -83,  -80,
    -78,  -75,  -73,  -70,  -68,  -65,  -62,  -60,  -57,  -54,  -51,  -48,  -45,  -42,  -39,  -37,  -34,  -30,  -27,
    -24,  -21,  -18,  -15,  -12,  -9,   -6,   -3,   0};

constexpr size_t kMaxUnpacked = 16u << 20;  // packed files: at most this much in all after decompression

uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
void put16(std::vector<uint8_t>& f, uint32_t v) { f.push_back((uint8_t)(v >> 8)), f.push_back((uint8_t)v); }
void put32(std::vector<uint8_t>& f, uint32_t v) { put16(f, v >> 16), put16(f, v & 0xFFFF); }
void putMark(std::vector<uint8_t>& f, const char* m) { f.insert(f.end(), m, m + std::strlen(m)); }

// ---------------------------------------------------------------- lh.library decompressor (LibAncient)
// Adaptive Huffman over 317 codes (literals 0-255, copies of 1-60 bytes, 316 ends), no halving at 32K.
class DynamicHuffman {
public:
    static constexpr uint32_t kMax = 317, kRoot = kMax * 2 - 2;
    DynamicHuffman() {
        for (uint32_t i = 0; i < kMax; ++i) {
            n_[i] = Node{1, i, kMax + (i >> 1), 0, 0};
            map_[i] = i;
        }
        for (uint32_t i = kMax, j = 0; i < kMax * 2 - 1; ++i, j += 2) {
            n_[i] = Node{n_[j].freq + n_[j + 1].freq, i, kMax + (i >> 1), j, j + 1};
            map_[i] = i;
        }
    }
    template <class Bit>
    uint32_t decode(Bit bit) const {
        uint32_t code = kRoot;
        for (int guard = 0; code >= kMax && guard < (int)(kMax * 2); ++guard) code = bit() ? n_[code].right : n_[code].left;
        return code < kMax ? code : 316;  // a broken tree cannot happen; end the stream if it did
    }
    void update(uint32_t code) {
        while (code != kRoot) {
            ++n_[code].freq;
            const uint32_t index = n_[code].index;
            uint32_t dest = index;
            while (dest != kRoot && n_[code].freq > n_[map_[dest + 1]].freq) ++dest;
            if (index != dest) {
                const uint32_t other = map_[dest];
                std::swap(n_[code].index, n_[other].index);
                std::swap(map_[index], map_[dest]);
                uint32_t& a = parentSlot(code);
                uint32_t& b = parentSlot(other);
                std::swap(a, b);
                std::swap(n_[code].parent, n_[other].parent);
            }
            code = n_[code].parent;
        }
        ++n_[code].freq;
    }
    uint32_t maxFrequency() const { return n_[kRoot].freq; }

private:
    struct Node {
        uint32_t freq, index, parent, left, right;
    };
    uint32_t& parentSlot(uint32_t code) {
        Node& p = n_[n_[code].parent];
        return p.left == code ? p.left : p.right;
    }
    Node n_[kMax * 2 - 1];
    uint32_t map_[kMax * 2 - 1];
};

// Fills dst[0, rawSize) from src (MSB first bit stream); false on a broken stream or one that ends early.
bool lhLibraryDecompress(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t rawSize) {
    size_t in = 0, out = 0;
    uint32_t buf = 0, bufLen = 0;
    bool bad = false;
    auto bits = [&](uint32_t count) {
        uint32_t ret = 0;
        while (count) {
            if (bufLen == 0) {
                if (in >= srcLen) {
                    bad = true;
                    return 0u;
                }
                buf = src[in++], bufLen = 8;
            }
            const uint32_t take = count < bufLen ? count : bufLen;
            bufLen -= take;
            ret = ret << take | (buf >> bufLen & ((1u << take) - 1));
            count -= take;
        }
        return ret;
    };
    static const uint8_t kLen[16] = {5, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10};
    uint32_t offsets[16], acc = 0;
    for (int i = 0; i < 16; ++i) offsets[i] = acc, acc += 1u << kLen[i];
    std::unique_ptr<DynamicHuffman> tree(new DynamicHuffman);
    while (out < rawSize) {
        const uint32_t code = tree->decode([&] { return bits(1); });
        if (bad) return false;
        if (code == 316) break;  // the rest stays 0, as in LibAncient
        if (tree->maxFrequency() < 0x8000) tree->update(code);
        if (code < 256) {
            dst[out++] = (uint8_t)code;
            continue;
        }
        const uint32_t base = bits(4);
        const uint32_t distance = offsets[base] + bits(kLen[base]);
        const uint32_t count = code - 255;
        if (bad || distance == 0 || distance > out || count > rawSize - out) return false;
        for (uint32_t i = 0; i < count; ++i, ++out) dst[out] = dst[out - distance];
    }
    return true;
}

// ---------------------------------------------------------------- reading
struct Reader {
    const uint8_t* d;
    size_t n, pos = 0;
    bool bad = false;
    bool has(uint64_t k) const { return pos <= n && k <= n - pos; }
    const uint8_t* take(uint64_t k) {
        if (!has(k)) {
            bad = true;
            pos = n;
            return nullptr;
        }
        const uint8_t* p = d + pos;
        pos += (size_t)k;
        return p;
    }
    uint8_t u8() {
        const uint8_t* p = take(1);
        return p ? p[0] : 0;
    }
    uint16_t u16() {
        const uint8_t* p = take(2);
        return p ? rd16(p) : 0;
    }
    uint32_t u32() {
        const uint8_t* p = take(4);
        return p ? rd32(p) : 0;
    }
    bool mark(const char* m) {
        const uint8_t* p = take(4);
        return p && std::memcmp(p, m, 4) == 0;
    }
};

struct SongInfo {
    uint16_t startSpeed, rowsPerTrack, firstPosition, lastPosition, restartPosition, tempo;
};
struct PositionInfo {
    uint16_t startTrackRow;
    int8_t soundTranspose, noteTranspose;
};
struct TrackLine {
    uint8_t note = 0, instrument = 0;
    bool disableSoundTranspose = false, disableNoteTranspose = false;
    uint8_t arpeggio = 0, effect = 0, effectArg = 0;
};
struct Arpeggio {
    uint8_t length = 0, repeat = 0;
    int8_t values[14] = {};
};
struct Instrument {
    uint16_t type = 0;  // 0 = sample, 1 = synth (anything else plays like a synth without its effects)
    uint16_t waveformNumber = 0, waveformLength = 0, repeatLength = 0, volume = 0;
    int16_t fineTuning = 0;
    uint16_t portamentoSpeed = 0, vibratoDelay = 0, vibratoSpeed = 0, vibratoLevel = 0;
    uint16_t amfNumber = 0, amfDelay = 0, amfLength = 0, amfRepeat = 0;
    uint16_t adsrNumber = 0, adsrDelay = 0, adsrLength = 0, adsrRepeat = 0, sustainPoint = 0, sustainDelay = 0;
    uint16_t effectArg1 = 0, effect = 0, effectArg2 = 0, effectArg3 = 0, effectDelay = 0;
    Arpeggio arpeggios[3];
};
using Table = std::array<int8_t, 128>;

struct Module {
    std::vector<SongInfo> songs;
    std::vector<std::array<PositionInfo, 4>> positions;
    std::vector<TrackLine> tracks;
    std::vector<Instrument> instruments;
    std::vector<std::vector<int8_t>> samples;
    std::vector<Table> waves, adsr, amf;
};

constexpr size_t kInstrumentSize = 152;

// The editor format after its 8 byte mark (SonicArrangerWorker.LoadNormalModule).
bool parseEditor(const uint8_t* d, size_t n, Module& m) {
    Reader r{d, n, 8};
    if (!r.mark("STBL")) return false;
    const uint32_t numSongs = r.u32();
    for (uint32_t i = 0; i < numSongs && !r.bad; ++i) {
        const uint8_t* p = r.take(12);
        if (!p) return false;
        const SongInfo s{rd16(p), rd16(p + 2), rd16(p + 4), rd16(p + 6), rd16(p + 8), rd16(p + 10)};
        if (s.lastPosition == 0xFFFF || s.restartPosition == 0xFFFF) continue;
        bool seen = false;
        for (const SongInfo& o : m.songs) seen = seen || (o.firstPosition == s.firstPosition && o.lastPosition == s.lastPosition);
        if (!seen) m.songs.push_back(s);
    }
    if (r.bad || !r.mark("OVTB")) return false;
    const uint32_t numPos = r.u32();
    const uint8_t* p = r.take((uint64_t)numPos * 16);
    if (!p) return false;
    m.positions.resize(numPos);
    for (uint32_t i = 0; i < numPos; ++i)
        for (int c = 0; c < 4; ++c, p += 4) m.positions[i][c] = PositionInfo{rd16(p), (int8_t)p[2], (int8_t)p[3]};
    if (!r.mark("NTBL")) return false;
    const uint32_t numRows = r.u32();
    p = r.take((uint64_t)numRows * 4);
    if (!p) return false;
    m.tracks.resize(numRows);
    for (uint32_t i = 0; i < numRows; ++i, p += 4) {
        TrackLine& t = m.tracks[i];
        t.note = p[0];
        t.instrument = p[1];
        t.disableSoundTranspose = (p[2] & 0x80) != 0;
        t.disableNoteTranspose = (p[2] & 0x40) != 0;
        t.arpeggio = (uint8_t)((p[2] & 0x30) >> 4);
        t.effect = (uint8_t)(p[2] & 0x0F);
        t.effectArg = p[3];
    }
    if (!r.mark("INST")) return false;
    const uint32_t numInst = r.u32();
    p = r.take((uint64_t)numInst * kInstrumentSize);
    if (!p) return false;
    m.instruments.resize(numInst);
    for (uint32_t i = 0; i < numInst; ++i, p += kInstrumentSize) {
        Instrument& in = m.instruments[i];
        in.type = rd16(p);
        in.waveformNumber = rd16(p + 2);
        in.waveformLength = rd16(p + 4);
        in.repeatLength = rd16(p + 6);
        in.volume = (uint16_t)(rd16(p + 16) & 0xFF);
        in.fineTuning = (int8_t)p[19];
        in.portamentoSpeed = rd16(p + 20);
        in.vibratoDelay = rd16(p + 22);
        in.vibratoSpeed = rd16(p + 24);
        in.vibratoLevel = rd16(p + 26);
        in.amfNumber = rd16(p + 28);
        in.amfDelay = rd16(p + 30);
        in.amfLength = rd16(p + 32);
        in.amfRepeat = rd16(p + 34);
        in.adsrNumber = rd16(p + 36);
        in.adsrDelay = rd16(p + 38);
        in.adsrLength = rd16(p + 40);
        in.adsrRepeat = rd16(p + 42);
        in.sustainPoint = rd16(p + 44);
        in.sustainDelay = rd16(p + 46);
        in.effectArg1 = rd16(p + 64);
        in.effect = rd16(p + 66);
        in.effectArg2 = rd16(p + 68);
        in.effectArg3 = rd16(p + 70);
        in.effectDelay = rd16(p + 72);
        for (int a = 0; a < 3; ++a) {
            const uint8_t* q = p + 74 + 16 * a;
            in.arpeggios[a].length = q[0];
            in.arpeggios[a].repeat = q[1];
            std::memcpy(in.arpeggios[a].values, q + 2, 14);
        }
    }
    if (!r.mark("SD8B")) return false;
    const uint32_t numSamples = r.u32();
    if (r.bad || numSamples > 0x7FFFFFFF || !r.take((uint64_t)numSamples * 38)) return false;
    p = r.take((uint64_t)numSamples * 4);
    if (!p) return false;
    m.samples.resize(numSamples);
    for (uint32_t i = 0; i < numSamples; ++i) {
        const uint32_t len = rd32(p + 4 * (size_t)i);
        if (len == 0) continue;
        const uint8_t* s = r.take(len);
        if (!s) return false;
        m.samples[i].assign((const int8_t*)s, (const int8_t*)s + len);
    }
    std::vector<Table>* tables[3] = {&m.waves, &m.adsr, &m.amf};
    const char* marks[3] = {"SYWT", "SYAR", "SYAF"};
    for (int t = 0; t < 3; ++t) {
        if (!r.mark(marks[t])) return false;
        const uint32_t count = r.u32();
        p = r.take((uint64_t)count * 128);
        if (!p) return false;
        tables[t]->resize(count);
        for (uint32_t i = 0; i < count; ++i) std::memcpy((*tables[t])[i].data(), p + 128 * (size_t)i, 128);
    }
    // NostalgicPlayer would index sub-song 0 and position 0 of empty lists
    return !m.songs.empty() && !m.positions.empty();
}

// The lh.library packed editor format, unpacked to the plain one (LoadCompressedModule).
bool unpackPacked(const uint8_t* d, size_t n, std::vector<uint8_t>& out) {
    Reader r{d, n, 8};
    size_t total = 0;
    putMark(out, "SOARV1.0");
    auto unpackTo = [&](uint32_t count, uint32_t elementSize) {  // ReadCompressedData
        if (count == 0) return true;
        const uint32_t packedLen = r.u32();
        const uint64_t rawLen = (uint64_t)count * elementSize;
        const uint8_t* src = r.take(packedLen);
        if (!src || rawLen > kMaxUnpacked - total) return false;
        total += (size_t)rawLen;
        const size_t at = out.size();
        out.resize(at + (size_t)rawLen);
        return lhLibraryDecompress(src, packedLen, out.data() + at, (size_t)rawLen);
    };
    if (!r.mark("@TBL")) return false;
    uint32_t count = r.u32();
    const uint8_t* p = r.take((uint64_t)count * 12);
    if (!p) return false;
    putMark(out, "STBL"), put32(out, count), out.insert(out.end(), p, p + (size_t)count * 12);
    const char* marks[3] = {"@VTB", "@TBL", "@NST"};
    const char* plain[3] = {"OVTB", "NTBL", "INST"};
    const uint32_t sizes[3] = {16, 4, (uint32_t)kInstrumentSize};
    for (int s = 0; s < 3; ++s) {
        if (!r.mark(marks[s])) return false;
        count = r.u32();
        if (r.bad) return false;
        putMark(out, plain[s]), put32(out, count);
        if (!unpackTo(count, sizes[s])) return false;
    }
    if (!r.mark("@D8B")) return false;
    count = r.u32();
    if (r.bad || count > 0x7FFFFFFF || !(p = r.take((uint64_t)count * 38))) return false;
    putMark(out, "SD8B"), put32(out, count), out.insert(out.end(), p, p + (size_t)count * 38);
    p = r.take((uint64_t)count * 4);
    if (!p) return false;
    out.insert(out.end(), p, p + (size_t)count * 4);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t len = rd32(p + 4 * (size_t)i);
        if (len == 0) continue;
        if (!unpackTo(1, len)) return false;
    }
    const char* tmarks[3] = {"@YWT", "@YAR", "@YAF"};
    const char* tplain[3] = {"SYWT", "SYAR", "SYAF"};
    for (int s = 0; s < 3; ++s) {
        if (!r.mark(tmarks[s])) return false;
        count = r.u32();
        if (r.bad) return false;
        putMark(out, tplain[s]), put32(out, count);
        if (!unpackTo(count, 128)) return false;
    }
    return true;
}

// Ripped files (SonicArrangerFinalFormat.Identify): where the data starts, or false.
bool findRippedData(const uint8_t* d, size_t n, size_t& moduleOffset) {
    if (n < 16 * 4) return false;
    uint32_t check = rd32(d);
    if ((check & 0xFFFF0000) == 0x4EFA0000) {  // a player in front: seven JMPs, then the init routine
        const int initOffset = (int16_t)((check & 0xFFFF) + 2);
        if (n < 0x1630 || initOffset < 0) return false;
        for (int i = 1; i < 7; ++i)
            if (rd16(d + 4 * i) != 0x4EFA) return false;
        if ((uint64_t)initOffset + 8 > n) return false;
        if (rd32(d + initOffset) != 0x48E7FFFE || rd16(d + initOffset + 4) != 0x41FA) return false;
        moduleOffset = initOffset + 6 + rd16(d + initOffset + 6);
        return moduleOffset < n;
    }
    if ((check & 0xFFFF0000) != 0 || check == 0) return false;  // the data alone: offsets that make sense
    uint32_t check1 = check & 0xF8;
    if (check1 != check) return false;
    check = rd32(d + 4);
    if (check == 0 || (check & 0xFFFF0000) != 0) return false;
    check1 = check - check1;
    if (check1 % 12 != 0 || check1 / 12 > 32) return false;
    check1 = rd32(d + 8);
    if (check1 == 0 || (check1 & 0xFFFF0000) != 0 || check > check1) return false;
    const uint32_t check2 = check1 - check;
    if ((check2 & 0xFFF0) != check2) return false;
    uint32_t prev = check1;
    for (int i = 3; i < 7; ++i) {
        const uint32_t v = rd32(d + 4 * i);
        if (v == 0 || (v & 0xFFFF0000) != 0 || prev > v) return false;
        prev = v;
    }
    moduleOffset = 0;
    return true;
}

// The ripped data converted to the editor format (SonicArrangerFinalFormat.Convert).
bool convertRipped(const uint8_t* d, size_t n, size_t base, std::vector<uint8_t>& out) {
    if (base > n || n - base < 32) return false;
    uint32_t o[8];
    for (int i = 0; i < 8; ++i) o[i] = rd32(d + base + 4 * i);
    auto copy = [&](const char* m, uint32_t from, uint32_t count, uint32_t size) {
        const uint64_t at = (uint64_t)base + from, len = (uint64_t)count * size;
        if (at > n || len > n - at) return false;
        putMark(out, m), put32(out, count);
        out.insert(out.end(), d + at, d + at + len);
        return true;
    };
    putMark(out, "SOARV1.0");
    const uint32_t songs = (o[1] - o[0]) / 12, positions = (o[2] - o[1]) / 16, rows = (o[3] - o[2]) / 4;
    const uint32_t instruments = (o[4] - o[3]) / (uint32_t)kInstrumentSize;
    if (songs == 0 || positions == 0 || rows == 0 || instruments == 0) return false;
    if (!copy("STBL", o[0], songs, 12) || !copy("OVTB", o[1], positions, 16) || !copy("NTBL", o[2], rows, 4) ||
        !copy("INST", o[3], instruments, (uint32_t)kInstrumentSize))
        return false;
    // sample block: count, byte lengths, data (the word lengths and names the editor format keeps are not
    // used for playing and are left 0)
    const uint64_t sb = (uint64_t)base + o[7];
    if (sb > n || n - sb < 4) return false;
    const uint32_t count = rd32(d + sb);
    if ((uint64_t)count * 4 > n - sb - 4) return false;
    putMark(out, "SD8B"), put32(out, count);
    out.resize(out.size() + (size_t)count * 38, 0);
    uint64_t at = sb + 4 + (uint64_t)count * 4, sum = 0;
    for (uint32_t i = 0; i < count; ++i) sum += rd32(d + sb + 4 + 4 * (size_t)i);
    if (sum > n - at) return false;
    out.insert(out.end(), d + sb + 4, d + at + sum);
    return copy("SYWT", o[4], (o[5] - o[4]) / 128, 128) && copy("SYAR", o[5], (o[6] - o[5]) / 128, 128) &&
           copy("SYAF", o[6], (o[7] - o[6]) / 128, 128);
}

bool parseAny(const uint8_t* d, size_t n, Module& m) {
    if (!d || n < 16) return false;
    if (std::memcmp(d, "SOARV1.0", 8) == 0) return parseEditor(d, n, m);
    std::vector<uint8_t> plain;
    if (std::memcmp(d, "@OARV1.0", 8) == 0) return unpackPacked(d, n, plain) && parseEditor(plain.data(), plain.size(), m);
    size_t base = 0;
    return findRippedData(d, n, base) && convertRipped(d, n, base, plain) && parseEditor(plain.data(), plain.size(), m);
}

// ---------------------------------------------------------------- the replayer
struct Voice {
    uint16_t startTrackRow = 0;
    int8_t soundTranspose = 0, noteTranspose = 0;
    uint8_t note = 0, instrument = 0;
    bool disableSoundTranspose = false, disableNoteTranspose = false;
    uint8_t arpeggio = 0, effect = 0, effectArg = 0;
    uint16_t transposedNote = 0, previousTransposedNote = 0;
    int instr = -1;  // index into instruments (InstrumentInfo), -1 = none
    uint16_t transposedInstrument = 0;
    uint16_t currentVolume = 0;
    int16_t volumeSlideSpeed = 0;
    uint16_t vibratoPosition = 0, vibratoDelay = 0, vibratoSpeed = 0, vibratoLevel = 0;
    uint16_t portamentoSpeed = 0, portamentoPeriod = 0, arpeggioPosition = 0;
    int16_t slideSpeed = 0, slideValue = 0;
    uint16_t adsrPosition = 0, adsrDelayCounter = 0, sustainDelayCounter = 0, amfPosition = 0, amfDelayCounter = 0;
    uint16_t synthEffectPosition = 0, synthEffectWavePosition = 0, effectDelayCounter = 0;
    uint8_t flag = 0x01;
    int8_t buffer[128] = {};
};

enum Effect : uint8_t {
    kArpeggio, kSetSlideSpeed, kRestartAdsr, kSetVibrato = 4, kSync, kSetMasterVolume, kSetPortamento,
    kSkipPortamento, kSetTrackLen, kVolumeSlide, kPositionJump, kSetVolume, kTrackBreak, kSetFilter, kSetSpeed
};

class SonicArranger : public AmigaSynthPlayer {
public:
    bool load(const uint8_t* d, size_t n) {
        if (!parseAny(d, n, m_)) return false;
        visited_.assign(m_.positions.size(), false);
        format_ = "SONIC ARRANGER (AMIGA)";
        subsongs_ = (int)m_.songs.size();
        subsong_ = 0;
        measure();
        return true;
    }

protected:
    void initSound() override {
        const SongInfo& s = m_.songs[(size_t)subsong_];
        song_ = s;
        masterVolume_ = 64;
        speedCounter_ = currentSpeed_ = s.startSpeed;
        songPosition_ = (int16_t)(s.firstPosition - 1);
        rowPosition_ = rowsPerTrack_ = s.rowsPerTrack;
        for (Voice& v : voice_) v = Voice{};
        tickHz_ = s.tempo == 0 ? 50.0 : s.tempo > 200 ? 200.0 : (double)s.tempo;
        random_ = 0x12345678u;
        std::fill(visited_.begin(), visited_.end(), false);
    }

    void playTick() override {
        ++speedCounter_;
        if (speedCounter_ >= currentSpeed_) {
            speedCounter_ = 0;
            getNextRow();
        }
        for (int i = 0; i < 4; ++i) updateVoiceEffect(voice_[i], ch_[i]);
    }

private:
    Module m_;
    std::vector<bool> visited_;
    Voice voice_[4];
    SongInfo song_{};
    uint16_t masterVolume_ = 64, speedCounter_ = 0, currentSpeed_ = 0, rowPosition_ = 0, rowsPerTrack_ = 0;
    int16_t songPosition_ = 0;
    uint32_t random_ = 1;

    int8_t randomByte() {  // RandomGenerator.GetRandomNumber(-128, 127)
        random_ = random_ * 1664525u + 1013904223u;
        return (int8_t)(random_ >> 24);
    }
    const Table* wave(uint32_t number) const { return number < m_.waves.size() ? &m_.waves[number] : nullptr; }
    static int8_t waveAt(const Table* t, int64_t i) { return t && i >= 0 && i < 128 ? (*t)[(size_t)i] : 0; }
    static int8_t get(const Voice& v, int64_t i) { return i >= 0 && i < 128 ? v.buffer[i] : 0; }
    static void put(Voice& v, int64_t i, int value) {
        if (i >= 0 && i < 128) v.buffer[i] = (int8_t)value;
    }

    void getNextRow() {
        ++rowPosition_;
        if (rowPosition_ >= rowsPerTrack_) {
            rowPosition_ = 0;
            getNextPosition();
        }
        for (int i = 0; i < 4; ++i) {
            Voice& v = voice_[i];
            const size_t at = (size_t)v.startTrackRow + rowPosition_;
            const TrackLine t = at < m_.tracks.size() ? m_.tracks[at] : TrackLine{};
            v.note = t.note;
            v.instrument = t.instrument;
            v.disableSoundTranspose = t.disableSoundTranspose;
            v.disableNoteTranspose = t.disableNoteTranspose;
            v.arpeggio = t.arpeggio;
            v.effect = t.effect;
            v.effectArg = t.effectArg;
            playVoice(v, ch_[i]);
        }
    }

    void getNextPosition() {
        songPosition_ = (int16_t)(songPosition_ + 1);
        if (songPosition_ < 0 || songPosition_ > song_.lastPosition || (size_t)songPosition_ >= m_.positions.size())
            songPosition_ = (int16_t)song_.restartPosition;
        if (songPosition_ < 0 || (size_t)songPosition_ >= m_.positions.size()) songPosition_ = 0;  // restart past the list
        if (visited_[(size_t)songPosition_]) endReached_ = true;
        visited_[(size_t)songPosition_] = true;
        const auto& row = m_.positions[(size_t)songPosition_];
        for (int i = 0; i < 4; ++i) {
            voice_[i].startTrackRow = row[i].startTrackRow;
            voice_[i].soundTranspose = row[i].soundTranspose;
            voice_[i].noteTranspose = row[i].noteTranspose;
        }
    }

    void playVoice(Voice& v, AmigaChannel& ch) {
        uint16_t note = v.note;
        int16_t instrNum = v.instrument;
        if (note == 0) {
            if (instrNum != 0) setInstrument(v, (uint16_t)instrNum);
        } else if (note != 0x80) {
            if (note == 0x7F) {
                forceQuiet(v, ch);
            } else {
                if (!v.disableNoteTranspose) note = (uint16_t)(note + v.noteTranspose);
                if (instrNum != 0 && !v.disableSoundTranspose) instrNum = (int16_t)(instrNum + v.soundTranspose);
                v.previousTransposedNote = v.transposedNote;
                v.transposedNote = note;
                if (v.previousTransposedNote == 0) v.previousTransposedNote = note;
                if (instrNum < 0) {
                    v.flag |= 0x01;
                    forceQuiet(v, ch);
                    return;
                }
                int instr;
                if (instrNum == 0) {
                    instr = v.instr;
                    if (instr < 0) {
                        forceQuiet(v, ch);
                        return;
                    }
                    setSynthInstrument(v, m_.instruments[(size_t)instr]);
                } else {
                    instr = setInstrument(v, (uint16_t)instrNum);
                    if (instr < 0) {  // an instrument the module does not have
                        v.flag |= 0x01;
                        forceQuiet(v, ch);
                        return;
                    }
                }
                const Instrument& in = m_.instruments[(size_t)instr];
                if (in.type == 0) playSampleInstrument(v, ch, in);
                else playSynthInstrument(v, ch, in);
            }
        }
        setEffects(v);
    }

    void forceQuiet(Voice& v, AmigaChannel& ch) {
        ch.mute();
        v.currentVolume = 0;
        v.transposedInstrument = 0;
        v.instr = -1;
    }

    int setInstrument(Voice& v, uint16_t instrNum) {
        const uint16_t number = (uint16_t)(instrNum & 0xFF);
        if (number == 0 || number > m_.instruments.size()) return -1;
        v.transposedInstrument = number;
        const int instr = number - 1;
        const Instrument& in = m_.instruments[(size_t)instr];
        v.instr = instr;
        v.currentVolume = in.volume;
        v.portamentoSpeed = in.portamentoSpeed;
        v.portamentoPeriod = 0;
        v.vibratoPosition = 0;
        v.vibratoDelay = in.vibratoDelay;
        v.vibratoSpeed = in.vibratoSpeed;
        v.vibratoLevel = in.vibratoLevel;
        setSynthInstrument(v, in);
        return instr;
    }

    static void setSynthInstrument(Voice& v, const Instrument& in) {
        v.slideValue = in.fineTuning;
        v.adsrDelayCounter = in.adsrDelay;
        v.adsrPosition = 0;
        v.amfDelayCounter = in.amfDelay;
        v.amfPosition = 0;
        v.synthEffectPosition = in.effectArg2;
        v.synthEffectWavePosition = 0;
        v.effectDelayCounter = in.effectDelay;
        v.arpeggioPosition = 0;
        v.flag = 0x00;
    }

    void playSampleInstrument(Voice& v, AmigaChannel& ch, const Instrument& in) {
        const std::vector<int8_t>* data = in.waveformNumber < m_.samples.size() ? &m_.samples[in.waveformNumber] : nullptr;
        if (!data || data->empty()) {
            v.flag |= 0x01;
            forceQuiet(v, ch);
            return;
        }
        uint32_t playLength = in.waveformLength, loopStart = 0, loopLength = 0;
        if (in.repeatLength == 0) {
            loopLength = in.waveformLength;
        } else if (in.repeatLength != 1) {
            playLength += in.repeatLength;
            loopStart = in.waveformLength * 2u;
            loopLength = in.repeatLength * 2u;
        }
        playLength = std::min<uint32_t>(playLength * 2, (uint32_t)data->size());
        if (playLength == 0) {
            v.flag |= 0x01;
            forceQuiet(v, ch);
            return;
        }
        ch.playSample(data->data(), 0, playLength);
        if (loopLength != 0 && clipRange(playLength, loopStart, loopLength)) ch.setLoop(loopStart, loopLength);
    }

    void playSynthInstrument(Voice& v, AmigaChannel& ch, const Instrument& in) {
        const Table* data = wave(in.waveformNumber);
        if (!data) {
            v.flag |= 0x01;
            forceQuiet(v, ch);
            return;
        }
        const uint32_t length = std::min<uint32_t>(in.waveformLength * 2u, 128);
        std::memcpy(v.buffer, data->data(), length);
        ch.playSample(v.buffer, 0, length);
        ch.setLoop(0, length);
    }

    void updateVoiceEffect(Voice& v, AmigaChannel& ch) {
        if ((v.flag & 0x01) != 0 || v.instr < 0) {
            ch.mute();
            return;
        }
        const Instrument& in = m_.instruments[(size_t)v.instr];
        uint16_t period, previousPeriod;
        doArpeggio(v, in, period, previousPeriod);
        doPortamento(period, previousPeriod, v);
        doVibrato(period, v, in);
        doAmf(period, v, in);
        doSlide(period, v);
        ch.setAmigaPeriod(period);
        if (in.type == 1) doSynthEffects(v, in);
        doAdsr(v, ch, in);
        int vol = v.currentVolume + v.volumeSlideSpeed;  // DoVolumeSlide
        v.currentVolume = (uint16_t)(vol < 0 ? 0 : vol > 64 ? 64 : vol);
    }

    void doArpeggio(Voice& v, const Instrument& in, uint16_t& period, uint16_t& previousPeriod) {
        uint16_t note = v.transposedNote, prevNote = v.previousTransposedNote;
        if (v.arpeggio != 0) {
            const Arpeggio& arp = in.arpeggios[v.arpeggio - 1];
            const int8_t arpVal = v.arpeggioPosition < 14 ? arp.values[v.arpeggioPosition] : 0;
            note = (uint16_t)(note + arpVal);
            prevNote = (uint16_t)(prevNote + arpVal);
            ++v.arpeggioPosition;
            const int maxLength = std::min(arp.length + arp.repeat, 13);
            if (v.arpeggioPosition > maxLength) v.arpeggioPosition = arp.length;
        } else if (v.effect == kArpeggio && v.effectArg != 0) {
            uint16_t arpVal = 0;
            switch (speedCounter_ % 3) {
            case 1: arpVal = (uint16_t)(v.effectArg >> 4); break;
            case 2: arpVal = (uint16_t)(v.effectArg & 0x0F); break;
            default: break;
            }
            note = (uint16_t)(note + arpVal);
            prevNote = (uint16_t)(prevNote + arpVal);
        }
        if (note >= 109) note = 0;
        if (prevNote >= 109) prevNote = 0;
        period = kPeriods[note];
        previousPeriod = kPeriods[prevNote];
    }

    static void doPortamento(uint16_t& period, uint16_t previousPeriod, Voice& v) {
        if (v.portamentoSpeed == 0) return;
        if (v.portamentoPeriod == 0) v.portamentoPeriod = previousPeriod;
        int diff = period - v.portamentoPeriod;
        if (diff < 0) diff = -diff;
        diff -= v.portamentoSpeed;
        if (diff < 0) {
            v.portamentoSpeed = 0;
        } else {
            uint16_t newPeriod = v.portamentoPeriod;
            if (newPeriod >= period) newPeriod = (uint16_t)(newPeriod - v.portamentoSpeed);
            else newPeriod = (uint16_t)(newPeriod + v.portamentoSpeed);
            v.portamentoPeriod = newPeriod;
            period = newPeriod;
        }
    }

    static void doVibrato(uint16_t& period, Voice& v, const Instrument& in) {
        if (v.vibratoDelay == 255) return;
        if (v.vibratoDelay != 0) {
            --v.vibratoDelay;
            return;
        }
        const int8_t vibVal = kVibrato[v.vibratoPosition & 0xFF];
        const uint16_t vibLevel = in.vibratoLevel;  // (NostalgicPlayer uses the instrument's level here too)
        if (vibVal != 0 && vibLevel != 0) period = (uint16_t)(period + (uint16_t)((vibVal * 4) / vibLevel));
        v.vibratoPosition = (uint16_t)((v.vibratoPosition + in.vibratoSpeed) & 0xFF);
    }

    void doAmf(uint16_t& period, Voice& v, const Instrument& in) {
        const int total = in.amfLength + in.amfRepeat;
        if (total == 0) return;
        const int8_t amfVal = in.amfNumber < m_.amf.size() && v.amfPosition < 128 ? m_.amf[in.amfNumber][v.amfPosition] : 0;
        period = (uint16_t)(period - amfVal);
        --v.amfDelayCounter;
        if (v.amfDelayCounter == 0) {
            v.amfDelayCounter = in.amfDelay;
            ++v.amfPosition;
            if (v.amfPosition >= total) {
                v.amfPosition = in.amfLength;
                if (in.amfRepeat == 0) --v.amfPosition;
            }
        }
    }

    void doSlide(uint16_t& period, Voice& v) const {
        period = (uint16_t)(period - v.slideValue);
        if (period < 113) period = 113;
        if (speedCounter_ != 0) v.slideValue = (int16_t)(v.slideValue + v.slideSpeed);
    }

    void doAdsr(Voice& v, AmigaChannel& ch, const Instrument& in) {
        const int total = in.adsrLength + in.adsrRepeat;
        if (total == 0) {
            ch.setAmigaVolume((uint16_t)(v.currentVolume * masterVolume_ / 64));
            return;
        }
        const uint8_t adsrVal =
            in.adsrNumber < m_.adsr.size() && v.adsrPosition < 128 ? (uint8_t)m_.adsr[in.adsrNumber][v.adsrPosition] : 0;
        uint16_t vol = (uint16_t)(masterVolume_ * v.currentVolume * adsrVal / 4096);
        if (vol > 64) vol = 64;
        ch.setAmigaVolume(vol);
        if (v.note == 0x80 && v.adsrPosition >= in.sustainPoint) {
            if (in.sustainDelay == 0) return;
            if (v.sustainDelayCounter != 0) {
                --v.sustainDelayCounter;
                return;
            }
            v.sustainDelayCounter = in.sustainDelay;
        }
        --v.adsrDelayCounter;
        if (v.adsrDelayCounter == 0) {
            v.adsrDelayCounter = in.adsrDelay;
            ++v.adsrPosition;
            if (v.adsrPosition >= total) {
                v.adsrPosition = in.adsrLength;
                if (in.adsrRepeat == 0) --v.adsrPosition;
                if (in.adsrRepeat == 0 && vol == 0) v.flag |= 0x01;
            }
        }
    }

    void setEffects(Voice& v) {
        v.slideSpeed = 0;
        v.volumeSlideSpeed = 0;
        switch (v.effect) {
        case kSetSlideSpeed: v.slideSpeed = (int8_t)v.effectArg; break;
        case kRestartAdsr: v.adsrPosition = v.effectArg; break;
        case kSetVibrato:
            v.vibratoDelay = 0;
            v.vibratoSpeed = (uint16_t)((v.effectArg & 0xF0) >> 3);
            v.vibratoLevel = (uint16_t)(-((v.effectArg & 0x0F) << 4) + 160);
            break;
        case kSetMasterVolume: masterVolume_ = (uint16_t)(v.effectArg == 64 ? 64 : (v.effectArg & 0x3F)); break;
        case kSetPortamento: v.portamentoSpeed = v.effectArg; break;
        case kSkipPortamento: v.portamentoSpeed = 0; break;
        case kSetTrackLen:
            if (v.effectArg <= 64) rowsPerTrack_ = v.effectArg;
            break;
        case kVolumeSlide: v.volumeSlideSpeed = (int8_t)v.effectArg; break;
        case kPositionJump:
            songPosition_ = (int16_t)(v.effectArg - 1);
            rowPosition_ = rowsPerTrack_;
            break;
        case kSetVolume: v.currentVolume = v.effectArg > 64 ? 64 : v.effectArg; break;
        case kTrackBreak: rowPosition_ = rowsPerTrack_; break;
        case kSetFilter: break;  // no Amiga LED filter here
        case kSetSpeed:
            if (v.effectArg > 0 && v.effectArg <= 16) currentSpeed_ = v.effectArg;
            break;
        default: break;
        }
    }

    // ---- synth effects on the voice's 128 byte buffer
    static void incrementPosition(Voice& v, const Instrument& in) {
        ++v.synthEffectPosition;
        if (v.synthEffectPosition >= in.effectArg3) v.synthEffectPosition = in.effectArg2;
    }

    void doSynthEffects(Voice& v, const Instrument& in) {
        --v.effectDelayCounter;
        if (v.effectDelayCounter != 0) return;
        v.effectDelayCounter = in.effectDelay;
        const int start = in.effectArg2, stop = in.effectArg3;
        switch (in.effect) {
        case 1:  // wave negator
            put(v, v.synthEffectPosition, -get(v, v.synthEffectPosition));
            incrementPosition(v, in);
            break;
        case 2: freeNegator(v, in); break;
        case 3: {  // rotate vertical
            const int8_t delta = (int8_t)in.effectArg1;
            int64_t at = start;
            for (int16_t count = (int16_t)(stop - start); count >= 0; --count, ++at) put(v, at, get(v, at) + delta);
            incrementPosition(v, in);
            break;
        }
        case 4: {  // rotate horizontal
            int16_t count = (int16_t)(stop - start);
            int64_t at = start;
            const int8_t first = get(v, at);
            do {
                put(v, at, get(v, at + 1));
                ++at;
                --count;
            } while (count >= 0);
            put(v, at, first);
            incrementPosition(v, in);
            break;
        }
        case 5: {  // alien voice
            const Table* src = wave(in.effectArg1);
            int64_t at = start;
            for (int16_t count = (int16_t)(stop - start); count >= 0; --count, ++at) put(v, at, get(v, at) + waveAt(src, at));
            break;
        }
        case 6: {  // poly negator
            const Table* src = wave(in.waveformNumber);
            int64_t position = v.synthEffectPosition;
            put(v, position, waveAt(src, position));
            if (position >= stop) position = (uint16_t)(start - 1);
            put(v, position + 1, -get(v, position + 1));
            incrementPosition(v, in);
            break;
        }
        case 7:  // shack wave 1
            shackWave(v, in);
            incrementPosition(v, in);
            break;
        case 8: {  // shack wave 2
            shackWave(v, in);
            const int64_t at = (int64_t)start + v.synthEffectWavePosition;
            put(v, at, -get(v, at));
            ++v.synthEffectWavePosition;
            if (v.synthEffectWavePosition > stop - start) v.synthEffectWavePosition = 0;
            incrementPosition(v, in);
            break;
        }
        case 9:  // metamorph
            if ((v.flag & 0x01) == 0) morph(v, in, wave(in.effectArg1));
            break;
        case 10: {  // laser
            if (v.synthEffectWavePosition < in.effectArg3) {
                v.slideValue = (int16_t)(v.slideValue + (int8_t)in.effectArg2);
                ++v.synthEffectWavePosition;
            }
            incrementPosition(v, in);
            break;
        }
        case 11: {  // wave alias
            const int8_t delta = (int8_t)in.effectArg1;
            int64_t at = start;
            for (int16_t count = (int16_t)(stop - start); count >= 0; --count, ++at) {
                int8_t val = get(v, at);
                if (at + 1 >= 128 || val > get(v, at + 1)) val = (int8_t)(val - delta);
                else val = (int8_t)(val + delta);
                put(v, at, val);
            }
            incrementPosition(v, in);
            break;
        }
        case 12:  // noise generator 1
            put(v, v.synthEffectPosition, get(v, v.synthEffectPosition) ^ randomByte());
            incrementPosition(v, in);
            break;
        case 13: lowPass(v, in, nullptr); break;
        case 14: lowPass(v, in, wave(in.effectArg1)); break;
        case 15: {  // oszilator
            if ((v.flag & 0x02) != 0) {
                v.flag ^= 0x08;
                v.flag &= 0xFD;
            }
            morph(v, in, wave((v.flag & 0x08) != 0 ? in.waveformNumber : in.effectArg1));
            break;
        }
        case 16: {  // noise generator 2
            int16_t count = (int16_t)(stop - start);
            do {
                const int64_t at = (int64_t)start + count;
                int val = (int8_t)(get(v, at) ^ 0x05);
                val = (int8_t)((val << 2) | (val >> 6));
                put(v, at, (int8_t)(val + randomByte()));
                --count;
            } while (count >= 0);
            break;
        }
        case 17: {  // FM drum
            if (v.synthEffectWavePosition >= in.effectArg3) {
                v.slideValue = in.fineTuning;
                v.synthEffectWavePosition = 0;
            }
            const uint16_t decrement = (uint16_t)((in.effectArg2 << 8) | (uint8_t)in.effectArg1);
            v.slideValue = (int16_t)(v.slideValue - decrement);
            ++v.synthEffectWavePosition;
            incrementPosition(v, in);
            break;
        }
        default: break;
        }
    }

    void freeNegator(Voice& v, const Instrument& in) {
        if ((v.flag & 0x04) != 0) return;
        const uint16_t waveLength = in.effectArg2, waveRepeat = in.effectArg3;
        const int16_t waveVal = (int16_t)(waveAt(wave(in.effectArg1), v.synthEffectWavePosition) & 0x7F);
        const Table* src = wave(in.waveformNumber);
        int16_t count = (int16_t)(in.waveformLength * 2);
        int64_t at = count;
        while (count > 0 && count >= waveVal) {
            --at;
            put(v, at, waveAt(src, at));
            --count;
        }
        const int16_t left = (int16_t)(waveVal - count);
        count = (int16_t)(count + left);
        at += left;
        while (count > 0) {
            --at;
            put(v, at, -waveAt(src, at));
            --count;
        }
        ++v.synthEffectWavePosition;
        if (v.synthEffectWavePosition > waveLength + waveRepeat) {
            v.synthEffectWavePosition = waveLength;
            if (waveRepeat == 0 && waveVal == 0) v.flag |= 0x04;
        }
    }

    void shackWave(Voice& v, const Instrument& in) {
        const int start = in.effectArg2, stop = in.effectArg3;
        const int8_t delta = waveAt(wave(in.effectArg1), (int64_t)start + v.synthEffectPosition);
        int64_t at = start;
        for (int16_t count = (int16_t)(stop - start); count >= 0; --count, ++at) put(v, at, get(v, at) + delta);
    }

    static void morph(Voice& v, const Instrument& in, const Table* src) {  // metamorph / oszilator
        const int start = in.effectArg2, stop = in.effectArg3;
        int64_t at = start;
        bool changed = false;
        for (int16_t count = (int16_t)(stop - start); count >= 0; --count, ++at) {
            int8_t val = get(v, at);
            const int8_t target = waveAt(src, at);
            if (val != target) {
                changed = true;
                val = (int8_t)(val < target ? val + 1 : val - 1);
                put(v, at, val);
            }
        }
        if (!changed) v.flag |= 0x02;
    }

    // Low pass filter 1 (delta = effect argument 1) and 2 (deltas from a wave table, read from the stop position on)
    static void lowPass(Voice& v, const Instrument& in, const Table* deltas) {
        const int start = in.effectArg2, stop = in.effectArg3;
        int64_t deltaAt = stop;
        for (int i = start; i <= stop; ++i) {
            int8_t val1 = get(v, i);
            const int8_t val2 = i == stop ? get(v, start) : get(v, i + 1);
            const bool up = val1 <= val2;
            val1 = (int8_t)(val1 - val2);
            if (val1 < 0) val1 = (int8_t)-val1;
            int delta = in.effectArg1;
            if (deltas) {
                delta = waveAt(deltas, deltaAt++) & 0x7F;
                deltaAt &= 0x7F;
            }
            if (val1 > delta) put(v, i, get(v, i) + (up ? 2 : -2));
        }
        incrementPosition(v, in);
    }
};
}  // namespace

bool isSonicArranger(const uint8_t* d, size_t n) {
    Module m;
    return parseAny(d, n, m);
}

std::unique_ptr<AmigaSynthPlayer> openSonicArranger(const uint8_t* d, size_t n) {
    std::unique_ptr<SonicArranger> p(new SonicArranger);
    if (!p->load(d, n)) return nullptr;
    return p;
}
