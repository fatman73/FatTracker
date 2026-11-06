// SidMon 1.0 replayer, ported from NostalgicPlayer's SidMon10Worker.cs (Thomas Neumann, MIT licence)
// onto AmigaSynthPlayer. SidMon 1 songs are saved together with their replay routine, so the loader
// finds the data tables by looking for the routine's instructions (as NostalgicPlayer does).
// Added for hostile files: every offset, count and index is checked (the C# code relied on exceptions),
// out of range rows / tracks / instruments / waveforms / samples are treated as empty, wave lists are one
// flat table (as in Amiga memory), and the instruments and waveforms the song modifies while it plays
// are restored on restart.
#include <algorithm>
#include <cstring>
#include <vector>

#include "core/amiga_synth.h"
#include "core/amiga_formats.h"

namespace {
// Period tables of the three player versions (fine tune steps of 67 / 68 entries)
const uint16_t kPeriods1[] = {
    0, 5760, 5424, 5120, 4832, 4560, 4304, 4064, 3840, 3616, 3424, 3232, 3048, 2880, 2712, 2560,
    2416, 2280, 2152, 2032, 1920, 1808, 1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016,
    960, 904, 856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 452, 428, 404,
    381, 360, 339, 320, 302, 285, 269, 254, 240, 226, 214, 202, 190, 180, 170, 160,
    151, 143, 135, 127, 0, 0, 0, 0, 0, 0, 0, 4028, 3806, 3584, 3394, 3204,
    3013, 2855, 2696, 2538, 2395, 2268, 2141, 2014, 1903, 1792, 1697, 1602, 1507, 1428, 1348, 1269,
    1198, 1134, 1071, 1007, 952, 896, 849, 801, 754, 714, 674, 635, 599, 567, 536, 504,
    476, 448, 425, 401, 377, 357, 337, 310, 300, 284, 268, 252, 238, 224, 213, 201,
    189, 179, 169, 159, 150, 142, 134, 0, 0, 0, 0, 0, 0, 0, 3993, 3773,
    3552, 3364, 3175, 2987, 2830, 2672, 2515, 2374, 2248, 2122, 1997, 1887, 1776, 1682, 1588, 1494,
    1415, 1336, 1258, 1187, 1124, 1061, 999, 944, 888, 841, 794, 747, 708, 668, 629, 594,
    562, 531, 500, 472, 444, 421, 397, 374, 354, 334, 315, 297, 281, 266, 250, 236,
    222, 211, 199, 187, 177, 167, 158, 149, 141, 133, 0, 0, 0, 0, 0, 0,
    0, 3957, 3739, 3521, 3334, 3147, 2960, 2804, 2648, 2493, 2353, 2228, 2103, 1979, 1870, 1761,
    1667, 1574, 1480, 1402, 1324, 1247, 1177, 1114, 1052, 990, 935, 881, 834, 787, 740, 701,
    662, 624, 589, 557, 526, 495, 468, 441, 417, 394, 370, 351, 331, 312, 295, 279,
    263, 248, 234, 221, 209, 197, 185, 176, 166, 156, 148, 140, 132, 0, 0, 0,
    0, 0, 0, 0, 3921, 3705, 3489, 3304, 3119, 2933, 2779, 2625, 2470, 2331, 2208, 2084,
    1961, 1853, 1745, 1652, 1560, 1467, 1390, 1313, 1235, 1166, 1104, 1042, 981, 927, 873, 826,
    780, 734, 695, 657, 618, 583, 552, 521, 491, 464, 437, 413, 390, 367, 348, 329,
    309, 292, 276, 261, 246, 232, 219, 207, 195, 184, 174, 165, 155, 146, 138, 131,
    0, 0, 0, 0, 0, 0, 0, 3886, 3671, 3457, 3274, 3090, 2907, 2754, 2601, 2448,
    2310, 2188, 2065, 1943, 1836, 1729, 1637, 1545, 1454, 1377, 1301, 1224, 1155, 1094, 1033, 972,
    918, 865, 819, 773, 727, 689, 651, 612, 578, 547, 517, 486, 459, 433, 410, 387,
    364, 345, 326, 306, 289, 274, 259, 243, 230, 217, 205, 194, 182, 173, 163, 153,
    145, 137, 130, 0, 0, 0, 0, 0, 0, 0, 3851, 3638, 3426, 3244, 3062, 2880,
    2729, 2577, 2426, 2289, 2168, 2047, 1926, 1819, 1713, 1622, 1531, 1440, 1365, 1289, 1213, 1145,
    1084, 1024, 963, 910, 857, 811, 766, 720, 683, 645, 607, 573, 542, 512, 482, 455,
    429, 406, 383, 360, 342, 323, 304, 287, 271, 256, 241, 228, 215, 203, 192, 180,
    171, 162, 152, 144, 136, 128,
};
const uint16_t kPeriods2[] = {
    0, 5760, 5424, 5120, 4832, 4560, 4304, 4064, 3840, 3616, 3424, 3232, 3048, 2880, 2712, 2560,
    2416, 2280, 2152, 2032, 1920, 1808, 1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016,
    960, 904, 856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 452, 428, 404,
    381, 360, 339, 320, 302, 285, 269, 254, 240, 226, 214, 202, 190, 180, 170, 160,
    151, 143, 135, 127, 0, 0, 0, 0, 0, 0, 0, 0, 4028, 3806, 3584, 3394,
    3204, 3013, 2855, 2696, 2538, 2395, 2268, 2141, 2014, 1903, 1792, 1697, 1602, 1507, 1428, 1348,
    1269, 1198, 1134, 1071, 1007, 952, 896, 849, 801, 754, 714, 674, 635, 599, 567, 536,
    504, 476, 448, 425, 401, 377, 357, 337, 310, 300, 284, 268, 252, 238, 224, 213,
    201, 189, 179, 169, 159, 150, 142, 134, 0, 0, 0, 0, 0, 0, 0, 0,
    3993, 3773, 3552, 3364, 3175, 2987, 2830, 2672, 2515, 2374, 2248, 2122, 1997, 1887, 1776, 1682,
    1588, 1494, 1415, 1336, 1258, 1187, 1124, 1061, 999, 944, 888, 841, 794, 747, 708, 668,
    629, 594, 562, 531, 500, 472, 444, 421, 397, 374, 354, 334, 315, 297, 281, 266,
    250, 236, 222, 211, 199, 187, 177, 167, 158, 149, 141, 133, 0, 0, 0, 0,
    0, 0, 0, 0, 3957, 3739, 3521, 3334, 3147, 2960, 2804, 2648, 2493, 2353, 2228, 2103,
    1979, 1870, 1761, 1667, 1574, 1480, 1402, 1324, 1247, 1177, 1114, 1052, 990, 935, 881, 834,
    787, 740, 701, 662, 624, 589, 557, 526, 495, 468, 441, 417, 394, 370, 351, 331,
    312, 295, 279, 263, 248, 234, 221, 209, 197, 185, 176, 166, 156, 148, 140, 132,
    0, 0, 0, 0, 0, 0, 0, 0, 3921, 3705, 3489, 3304, 3119, 2933, 2779, 2625,
    2470, 2331, 2208, 2084, 1961, 1853, 1745, 1652, 1560, 1467, 1390, 1313, 1235, 1166, 1104, 1042,
    981, 927, 873, 826, 780, 734, 695, 657, 618, 583, 552, 521, 491, 464, 437, 413,
    390, 367, 348, 329, 309, 292, 276, 261, 246, 232, 219, 207, 195, 184, 174, 165,
    155, 146, 138, 131, 0, 0, 0, 0, 0, 0, 0, 0, 3886, 3671, 3457, 3274,
    3090, 2907, 2754, 2601, 2448, 2310, 2188, 2065, 1943, 1836, 1729, 1637, 1545, 1454, 1377, 1301,
    1224, 1155, 1094, 1033, 972, 918, 865, 819, 773, 727, 689, 651, 612, 578, 547, 517,
    486, 459, 433, 410, 387, 364, 345, 326, 306, 289, 274, 259, 243, 230, 217, 205,
    194, 182, 173, 163, 153, 145, 137, 130, 0, 0, 0, 0, 0, 0, 0, 0,
    3851, 3638, 3426, 3244, 3062, 2880, 2729, 2577, 2426, 2289, 2168, 2047, 1926, 1819, 1713, 1622,
    1531, 1440, 1365, 1289, 1213, 1145, 1084, 1024, 963, 910, 857, 811, 766, 720, 683, 645,
    607, 573, 542, 512, 482, 455, 429, 406, 383, 360, 342, 323, 304, 287, 271, 256,
    241, 228, 215, 203, 192, 180, 171, 162, 152, 144, 136, 128, 0, 0, 0, 0,
    0,
};
const uint16_t kPeriods3[] = {
    0, 6848, 6464, 6096, 5760, 5424, 5120, 4832, 4560, 4304, 4064, 3840, 3616, 3424, 3232, 3048,
    2880, 2712, 2560, 2416, 2280, 2152, 2032, 1920, 1808, 1712, 1616, 1524, 1440, 1356, 1280, 1208,
    1140, 1076, 1016, 960, 904, 856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480,
    452, 428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226, 214, 202, 190,
    180, 170, 160, 151, 143, 135, 127, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0,
};

constexpr int kSearch = 4096;  // the part of the file the replay routine is looked for in

struct Special {  // sample played by an instrument number >= 60 (a later player version)
    uint32_t instrument;
    int64_t offset;
    int64_t length;
};

// What the replay routine's instructions tell (ExtractInfoFromInitFunction / ExtractInfoFromPlayFunction).
struct Probe {
    int64_t base = 0, seq[4] = {}, trackData = 0, trackTable = 0, instruments = 0, waveList = 0, waves = 0,
            samples = 0, songData = 0;
    uint32_t noLoop = 0;
    const uint16_t* periods = nullptr;
    int periodCount = 0, fineTuneMul = 0;
    bool filter = false, mixing = false, special = false;
    std::vector<Special> specials;
};

struct Search {
    uint8_t d[kSearch] = {};
    int at(int64_t i) const { return i >= 0 && i < kSearch ? d[i] : 0; }
    bool is(int64_t i, int a, int b) const { return at(i) == a && at(i + 1) == b; }
    int64_t s16(int64_t i) const { return (int16_t)(at(i) << 8 | at(i + 1)); }
    int64_t u16(int64_t i) const { return at(i) << 8 | at(i + 1); }
    uint32_t u32(int64_t i) const { return (uint32_t)(u16(i) << 16 | u16(i + 2)); }
};

bool probeInit(const Search& b, Probe& p) {
    constexpr int L = kSearch;
    int64_t i;
    for (i = 0; i < L - 2; i += 2)
        if (b.is(i, 0x41, 0xfa)) break;
    if (i >= L - 4) return false;
    p.base = b.u16(i + 2) + i + 2;
    i += 4;
    for (int ch = 0; i < L - 2 && ch < 4; i += 2)
        if (b.is(i, 0xd1, 0xe8)) p.seq[ch++] = b.s16(i + 2) + p.base;
    if (i >= L - 4) return false;
    for (; i < L - 2; i += 2)
        if (b.is(i, 0x70, 0x03) || (b.is(i, 0x20, 0x3c) && b.at(i + 5) == 0x03)) break;
    if (i >= L - 4) return false;
    if (b.at(i) == 0x20) i += 4;
    if (!b.is(i + 2, 0x41, 0xfa) || !b.is(i + 6, 0xd1, 0xe8)) return false;
    p.trackData = b.s16(i + 8) + p.base;
    i += 10;
    for (; i < L - 2; i += 2)
        if (b.is(i, 0xd9, 0xec)) break;
    if (i >= L - 4) return false;
    p.trackTable = b.s16(i + 2) + p.base;
    i += 4;
    for (; i < L - 2; i += 2)
        if (b.is(i, 0xd9, 0xec)) break;
    if (i >= L - 4) return false;
    p.instruments = b.s16(i + 2) + p.base;
    return true;
}

bool probePlay(const Search& b, Probe& p) {
    constexpr int L = kSearch;
    int64_t i;
    for (i = 0; i < L - 4; i += 2)
        if (b.is(i, 0x48, 0xe7) && b.is(i + 2, 0xff, 0xfe)) break;
    if (i >= L - 4) return false;
    for (; i < L - 4; i += 2)  // bsr PlayNote
        if (b.is(i, 0x61, 0x00)) break;
    if (i >= L - 4) return false;
    const int64_t playNote = b.s16(i + 2) + i + 2;
    for (; i < L - 4; i += 2)
        if (b.is(i, 0xd3, 0xe9)) break;
    if (i >= L - 4) return false;
    p.songData = b.s16(i + 2) + p.base;
    for (; i < L - 4; i += 2)  // the routines called before the registers come back: filter / mixing
        if (b.is(i, 0x4c, 0xdf)) break;
    if (i >= L - 4) return false;
    i -= 4;
    if (b.is(i, 0x00, 0xdf) || b.is(i, 0x4e, 0x71)) i -= 6;
    for (int k = 0; k < 3; ++k) {
        if (!b.is(i, 0x61, 0x00)) break;
        const int64_t c = b.s16(i + 2) + i + 2;
        if (b.is(c + 14, 0x44, 0x10)) p.filter = true;
        else if (b.is(c + 12, 0x20, 0x10) || b.is(c + 14, 0x20, 0x10)) p.mixing = true;
        i -= 4;
    }
    for (i = playNote; i < L; i += 2)
        if (b.is(i, 0xd9, 0xec)) break;
    if (i < L - 4) p.waveList = b.s16(i + 2) + p.base;
    else i = playNote;
    for (; i < L; i += 2)
        if (b.at(i) == 0xeb && (b.at(i + 1) == 0x81 || b.at(i + 1) == 0x49)) break;
    if (i >= L - 4) return false;
    for (; i < L; i += 2)
        if (b.is(i, 0xdd, 0xee)) break;
    if (i >= L - 4) return false;
    p.waves = b.s16(i + 2) + p.base;
    int64_t j;
    for (j = i; j < L; j += 2)
        if (b.is(j, 0x16, 0x2c)) break;
    if (j >= L - 4) {
        p.periods = kPeriods3, p.periodCount = (int)(sizeof kPeriods3 / 2), p.fineTuneMul = 0;
    } else if (b.is(j + 4, 0xc6, 0xfc)) {
        p.periods = kPeriods1, p.periodCount = (int)(sizeof kPeriods1 / 2), p.fineTuneMul = 67;
    } else {
        p.periods = kPeriods2, p.periodCount = (int)(sizeof kPeriods2 / 2), p.fineTuneMul = 68;
    }
    for (; i >= 0; i -= 2)  // bcc PlaySample
        if (b.is(i, 0x64, 0x00)) break;
    if (i < 4) return false;
    if (b.is(i - 2, 0x00, 0x3c)) {  // samples hard coded in the player (cmp.b #instrument)
        i = b.s16(i + 2) + i + 2;
        p.special = true;
        for (; i < L; i += 2) {
            if (b.is(i, 0x4e, 0x75)) break;
            if (b.is(i, 0x0c, 0x00)) {
                const uint32_t instr = (uint32_t)b.u16(i + 2);
                i += 8;
                if (!b.is(i, 0x4d, 0xfa)) return false;
                int64_t start = b.s16(i + 2) + i + 2;
                if (b.is(i + 4, 0xdd, 0xfc)) {
                    start += (int32_t)b.u32(i + 6);
                    i += 6;
                }
                i += 10;
                if (!b.is(i, 0x33, 0xfc)) return false;
                p.specials.push_back({instr, start, b.u16(i + 2) * 2});
            }
        }
        if (i >= L - 4) return false;
    } else {
        i = b.s16(i + 2) + i + 2;
        for (; i < L; i += 2)
            if (b.is(i, 0xd9, 0xec)) break;
        if (i >= L - 4) return false;
        p.samples = b.s16(i + 2) + p.base;
        for (; i < L; i += 2)
            if (b.is(i, 0x0c, 0xab)) break;
        if (i >= L - 4) return false;
        p.noLoop = b.u32(i + 2);
    }
    return true;
}

bool probe(const uint8_t* d, size_t n, Probe& p) {
    if (!d || n < 1024) return false;
    if (!std::memcmp(d, "SC68", 4) || !std::memcmp(d, "PSID", 4) || !std::memcmp(d, "RSID", 4)) return false;
    Search b;
    std::memcpy(b.d, d, n < (size_t)kSearch ? n : (size_t)kSearch);
    return probeInit(b, p) && probePlay(b, p);
}

// Big endian reads that note when they run past the end of the file.
struct Reader {
    const uint8_t* d;
    size_t n;
    int64_t pos = 0;
    bool over = false;
    void seek(int64_t at) { pos = at; }
    int u8() {
        if (pos < 0 || pos >= (int64_t)n) {
            over = true;
            ++pos;
            return 0;
        }
        return d[pos++];
    }
    uint32_t u32() {
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) v = v << 8 | (uint32_t)u8();
        return v;
    }
};

enum Env : uint8_t { kAttack, kDecay, kSustain, kRelease, kDone };

struct Instrument {
    uint32_t waveform = 0;  // >= 16: sample number + 16
    uint8_t arp[16] = {};
    uint8_t attackSpeed = 0, attackMax = 0, decaySpeed = 0, decayMin = 0, sustainTime = 0, releaseSpeed = 0,
            releaseMin = 0, phaseShift = 0, phaseSpeed = 0, fineTune = 0;
    int8_t pitchFall = 0;
    uint16_t volume = 0;
};
struct Row {
    int8_t note = 0;
    uint8_t instrument = 0, effect = 0, param = 0, duration = 0;
};
struct Seq {
    uint32_t track = 0;
    int8_t transpose = 0;
};
struct Sample {
    std::vector<int8_t> data;
    int loopStart = -1;
};
struct Mix {
    uint32_t source1 = 0, source2 = 0, dest = 0, speed = 0;
};
struct MixState {
    uint32_t counter = 0, position = 0;
};
struct Voice {
    int seqIndex = 0, rowIndex = 0, track = -1, instr = -1;
    Seq seq;
    uint16_t notePeriod = 0, pitchCtl = 0, pitchFallCtl = 0;
    uint8_t bendTo = 0, arpIndex = 0, rowCount = 0, volume = 0, sustainCtl = 0, phaseIndex = 0, phaseSpeed = 0,
            waveIndex = 0, waveformNumber = 0, waveSpeed = 0;
    int8_t bendSpeed = 0, noteOffset = 0;
    Env env = kDone;
    bool loopControl = false;
};

class SidMon1 : public AmigaSynthPlayer {
public:
    bool load(const uint8_t* d, size_t n, Probe&& pr) {
        p_ = static_cast<Probe&&>(pr);
        Reader r{d, n};
        const int64_t size = (int64_t)n;
        // FindCounts: the pointers in the player point at the real offsets
        auto deref = [&](int64_t& off) {
            if (off < 0 || off + 4 > size) return false;
            r.seek(off);
            off = (int64_t)(int32_t)r.u32() + p_.base;
            return off >= 0 && off <= size;
        };
        for (int64_t& s : p_.seq)
            if (!deref(s)) return false;
        if (!deref(p_.trackData) || !deref(p_.trackTable) || !deref(p_.instruments)) return false;
        if (p_.waveList != 0 && !deref(p_.waveList)) return false;
        if (!deref(p_.waves)) return false;
        if (p_.samples != 0 && !deref(p_.samples)) return false;
        if (p_.samples == 0 && !p_.special) return false;
        if (!deref(p_.songData)) return false;
        int64_t sorted[] = {p_.seq[0], p_.seq[1], p_.seq[2], p_.seq[3], p_.trackData, p_.trackTable, p_.instruments,
                            p_.waveList, p_.waves, p_.samples, p_.songData};
        std::sort(std::begin(sorted), std::end(sorted));
        auto next = [&](int64_t off) {  // the block after this one (the end of the file for the last)
            const int64_t* at = std::find(std::begin(sorted), std::end(sorted), off);
            return at + 1 < std::end(sorted) ? at[1] : size;
        };
        int64_t tracks;
        if (std::find(std::begin(sorted), std::end(sorted), p_.trackTable) + 1 == std::end(sorted)) {
            tracks = 1;  // last block: count the offsets (the first one is always 0)
            r.seek(p_.trackTable + 4);
            while (r.pos < size) {
                const uint32_t o = r.u32();
                if (o == 0 || o > n) break;
                ++tracks;
            }
        } else {
            tracks = (next(p_.trackTable) - p_.trackTable) / 4;
        }
        int64_t instruments = (next(p_.instruments) - p_.instruments) / 32;
        if (instruments > 63) instruments = 63;
        const int64_t waveLists = (next(p_.waveList) - p_.waveList) / 16;
        const int64_t waves = (next(p_.waves) - p_.waves) / 32;
        if (tracks < 1 || tracks > 65536 || instruments < 0) return false;

        // song data
        r.seek(p_.songData);
        mix_[0].source1 = r.u32(), mix_[1].source1 = r.u32();
        mix_[0].source2 = r.u32(), mix_[1].source2 = r.u32();
        mix_[0].dest = r.u32(), mix_[1].dest = r.u32();
        startRows_ = r.u32();
        const uint32_t positions = r.u32() - 1;
        startSpeed_ = r.u32();
        mix_[0].speed = r.u32(), mix_[1].speed = r.u32();
        if (r.over || positions == 0 || positions > n / 6) return false;

        // instruments
        r.seek(p_.instruments);
        inst_.assign((size_t)instruments + p_.specials.size(), Instrument{});
        for (int64_t i = 0; i < instruments; ++i) {
            Instrument& in = inst_[(size_t)i];
            in.waveform = r.u32();
            for (uint8_t& a : in.arp) a = (uint8_t)r.u8();
            in.attackSpeed = (uint8_t)r.u8(), in.attackMax = (uint8_t)r.u8();
            in.decaySpeed = (uint8_t)r.u8(), in.decayMin = (uint8_t)r.u8();
            in.sustainTime = (uint8_t)r.u8();
            r.u8();
            in.releaseSpeed = (uint8_t)r.u8(), in.releaseMin = (uint8_t)r.u8();
            in.phaseShift = (uint8_t)r.u8(), in.phaseSpeed = (uint8_t)r.u8();
            in.fineTune = (uint8_t)r.u8();
            in.pitchFall = (int8_t)r.u8();
            if (r.over) return false;
            if (!p_.specials.empty()) {  // that player version keeps the pitch fall where the fine tune is
                in.pitchFall = (int8_t)in.fineTune;
                in.fineTune = 0;
            } else if (in.fineTune > 6) {
                in.fineTune = 0;
            }
        }
        for (size_t i = 0; i < p_.specials.size(); ++i) {
            Instrument& in = inst_[(size_t)instruments + i];
            in.waveform = p_.specials[i].instrument - 60 + 16;
            in.volume = 64;
        }

        // tracks
        std::vector<int32_t> trackOffs((size_t)tracks);
        r.seek(p_.trackTable);
        for (int32_t& o : trackOffs) o = (int32_t)r.u32();
        if (r.over) return false;
        trackStart_.assign((size_t)tracks, 0);
        trackLen_.assign((size_t)tracks, 0);
        rows_.clear();
        for (int64_t t = 0; t < tracks; ++t) {
            r.seek(p_.trackData + trackOffs[(size_t)t]);
            trackStart_[(size_t)t] = (uint32_t)rows_.size();
            for (int line = 0; line < 64;) {
                Row row;
                row.note = (int8_t)r.u8();
                row.instrument = (uint8_t)r.u8();
                row.effect = (uint8_t)r.u8();
                row.param = (uint8_t)r.u8();
                row.duration = (uint8_t)r.u8();
                if (!p_.specials.empty() && row.instrument >= 60)
                    row.instrument = (uint8_t)(inst_.size() - p_.specials.size() + (row.instrument - 60));
                rows_.push_back(row);
                line += row.duration + 1;
            }
            trackLen_[(size_t)t] = (uint32_t)rows_.size() - trackStart_[(size_t)t];
            if (r.over) return false;
        }

        // sequences
        for (int ch = 0; ch < 4; ++ch) {
            r.seek(p_.seq[ch]);
            seq_[ch].assign(positions, Seq{});
            for (Seq& s : seq_[ch]) {
                s.track = r.u32();
                r.u8();
                s.transpose = (int8_t)r.u8();
            }
            if (r.over) return false;
        }

        // wave lists and waveforms
        if (p_.waveList == 0) {  // none in the file: one per instrument, playing its waveform
            waveList_.assign(inst_.size() * 16, 0);
            for (size_t i = 0; i < inst_.size(); ++i) {
                waveList_[i * 16] = (uint8_t)(i + 1);
                waveList_[i * 16 + 1] = 0x01;
                waveList_[i * 16 + 2] = 0xff;
                waveList_[i * 16 + 3] = 0x10;
            }
        } else {
            if (waveLists < 0 || p_.waveList + waveLists * 16 > size) return false;
            waveList_.assign(d + p_.waveList, d + p_.waveList + waveLists * 16);
        }
        if (waves < 0 || p_.waves + waves * 32 > size) return false;
        waveCount_ = (uint32_t)waves;
        waves_.assign((const int8_t*)d + p_.waves, (const int8_t*)d + p_.waves + waves * 32);
        wavesStart_ = waves_;

        if (!loadSamples(d, n, r)) return false;
        instStart_ = inst_;
        format_ = "SIDMON 1.0 (AMIGA)";
        measure();
        return true;
    }

protected:
    void initSound() override {
        inst_ = instStart_;  // effects change the instruments and the mixing changes the waveforms
        std::copy(wavesStart_.begin(), wavesStart_.end(), waves_.begin());
        numRows_ = startRows_;
        speed_ = startSpeed_;
        speedCounter_ = startSpeed_;
        newTrack_ = loopSong_ = false;
        currentRow_ = -1;
        currentPosition_ = 1;
        mixState_[0] = mixState_[1] = MixState{};
        for (int i = 0; i < 4; ++i) {
            Voice& v = v_[i];
            v = Voice{};
            v.notePeriod = 0x9999;
            v.seq = seq_[i][0];
            v.track = trackIndex(v.seq.track);
            const int instr = rowAt(v.track, 0).instrument - 1;
            if (instr >= 0 && instr < (int)inst_.size()) {
                v.instr = instr;
            }
        }
    }

    void playTick() override {
        for (int i = 0; i < 4; ++i) playNote(v_[i], ch_[i], seq_[i]);
        if (++speedCounter_ > speed_) speedCounter_ = 0;
        newTrack_ = loopSong_ = false;
        if (speedCounter_ == 0 && ++currentRow_ == (int64_t)numRows_) {
            currentRow_ = 0;
            newTrack_ = true;
            if (++currentPosition_ > seq_[0].size()) {
                currentPosition_ = 1;
                loopSong_ = true;
                endReached_ = true;
            }
        }
        waveMixing(mix_[0], mixState_[0]);
        waveMixing(mix_[1], mixState_[1]);
        if (p_.filter && waveCount_) {
            int8_t& w = waves_[mixState_[0].position & 31];
            w = (int8_t)-w;
        }
    }

private:
    Probe p_;
    std::vector<Instrument> inst_, instStart_;
    std::vector<Row> rows_;
    std::vector<uint32_t> trackStart_, trackLen_;
    std::vector<Seq> seq_[4];
    std::vector<uint8_t> waveList_;
    std::vector<int8_t> waves_, wavesStart_;  // 32 bytes each
    uint32_t waveCount_ = 0;
    std::vector<uint32_t> sampleNums_;  // sorted sample numbers (waveform - 16) ...
    std::vector<Sample> samples_;      // ... and their samples
    Mix mix_[2];
    MixState mixState_[2];
    uint32_t startRows_ = 0, startSpeed_ = 0, numRows_ = 0, speed_ = 0, speedCounter_ = 0, currentPosition_ = 1;
    int64_t currentRow_ = -1;
    bool newTrack_ = false, loopSong_ = false;
    Voice v_[4];

    bool loadSamples(const uint8_t* d, size_t n, Reader& r) {
        for (const Instrument& in : inst_)
            if (in.waveform >= 16) sampleNums_.push_back(in.waveform - 16);
        std::sort(sampleNums_.begin(), sampleNums_.end());
        sampleNums_.erase(std::unique(sampleNums_.begin(), sampleNums_.end()), sampleNums_.end());
        auto readData = [&](Sample& s, int64_t at, int64_t len) {
            if (len < 0) return false;
            const int64_t avail = at < 0 || at >= (int64_t)n ? 0 : std::min<int64_t>(len, (int64_t)n - at);
            if (avail < len - 32) return false;
            s.data.assign((size_t)len, 0);
            if (avail > 0) std::memcpy(s.data.data(), d + at, (size_t)avail);
            return true;
        };
        if (p_.samples == 0) {  // samples hard coded in the player: in the order the player lists them
            sampleNums_.clear();
            samples_.assign(p_.specials.size(), Sample{});
            for (size_t i = 0; i < p_.specials.size(); ++i) {
                sampleNums_.push_back((uint32_t)i);
                if (!readData(samples_[i], p_.specials[i].offset, p_.specials[i].length)) return false;
            }
            return true;
        }
        r.seek(p_.samples);
        const uint32_t sampleBase = r.u32();
        if (r.over) return false;
        const int64_t infoAt = p_.samples + 4;
        int64_t firstSample = sampleBase != 0 ? (int64_t)sampleBase : (int64_t)UINT32_MAX;
        samples_.assign(sampleNums_.size(), Sample{});
        for (size_t k = 0; k < sampleNums_.size(); ++k) {
            const int64_t at = (int64_t)sampleNums_[k] * 32;
            if (at >= firstSample) continue;  // the info table ends where the first sample starts
            r.seek(infoAt + at);
            const uint32_t start = r.u32();
            const int32_t loop = (int32_t)r.u32();
            const uint32_t end = r.u32();
            for (int c = 0; c < 20; ++c) r.u8();  // name
            if (r.over) return false;
            Sample& s = samples_[k];
            if ((int64_t)loop == (int64_t)p_.noLoop || (int64_t)loop >= (int64_t)end || (int64_t)start > (int64_t)loop)
                s.loopStart = -1;
            else
                s.loopStart = (int)((int64_t)loop - start);
            const int64_t pos = infoAt + sampleBase + start;
            firstSample = std::min(firstSample, pos);
            if (!readData(s, pos, (int64_t)end - (int64_t)start)) return false;
        }
        return true;
    }

    int trackIndex(uint32_t t) const { return t < trackStart_.size() ? (int)t : -1; }
    Row rowAt(int track, int index) const {
        if (track < 0 || index < 0 || (uint32_t)index >= trackLen_[(size_t)track]) return Row{};
        return rows_[trackStart_[(size_t)track] + (size_t)index];
    }
    const Sample* sample(uint32_t number) const {  // waveform number >= 16
        if (number < 16) return nullptr;
        const auto at = std::lower_bound(sampleNums_.begin(), sampleNums_.end(), number - 16);
        if (at == sampleNums_.end() || *at != number - 16) return nullptr;
        const Sample& s = samples_[(size_t)(at - sampleNums_.begin())];
        return s.data.empty() ? nullptr : &s;
    }
    const int8_t* wave(int number) const {  // 1 based
        return number >= 1 && (uint32_t)number <= waveCount_ ? &waves_[(size_t)(number - 1) * 32] : nullptr;
    }
    uint16_t period(int64_t index) const { return index >= 0 && index < p_.periodCount ? p_.periods[index] : 0; }

    void playNote(Voice& v, AmigaChannel& c, const std::vector<Seq>& seq) {
        const int8_t* addr = nullptr;
        uint32_t addrLen = 0;
        int loopStart = -1;
        bool addrSet = false;
        uint32_t per = 0;
        if (speedCounter_ == 0) {
            if (newTrack_) {
                v.rowCount = 0;
                ++v.seqIndex;
                if (loopSong_ || v.seqIndex >= (int)seq.size()) v.seqIndex = 0;
                v.seq = seq[(size_t)v.seqIndex];
                v.track = trackIndex(v.seq.track);
                v.rowIndex = 0;
            }
            if (v.rowCount == 0) {
                auto playSample = [&](uint32_t number) {
                    if (v.loopControl) c.mute();
                    const Sample* s = sample(number);
                    addr = s ? s->data.data() : nullptr;
                    addrLen = s ? (uint32_t)s->data.size() : 0;
                    loopStart = s ? s->loopStart : -1;
                    addrSet = true;
                    v.loopControl = true;
                };
                const Row row = rowAt(v.track, v.rowIndex);
                if (row.instrument != 0 && row.instrument <= inst_.size()) {
                    const Instrument& in = inst_[row.instrument - 1u];
                    if (v.loopControl) {
                        c.mute();
                        v.loopControl = false;
                    }
                    const uint32_t wf = in.waveform;
                    if (wf >= 16) playSample(wf);
                    v.waveIndex = 0;
                    v.waveformNumber = (uint8_t)wf;
                    if (!addrSet && wf != 0) {
                        const size_t at = (size_t)(v.waveformNumber - 1) * 16;
                        if (at + 1 < waveList_.size()) {
                            v.waveSpeed = waveList_[at + 1];
                            if (const int8_t* w = wave(waveList_[at])) {
                                addr = w;
                                addrLen = 32;
                                loopStart = 0;
                            }
                        }
                    }
                    v.instr = row.instrument - 1;
                    v.env = kAttack;
                    v.pitchFallCtl = 0;
                    v.pitchCtl = 0;
                    v.rowCount = row.duration;
                } else if (row.note != 0) {
                    v.rowCount = row.duration;
                    if (v.loopControl && row.note != -1) playSample(v.waveformNumber);
                }
                int8_t note = row.note;
                if (note != 0) {
                    v.rowCount = row.duration;
                    if (note != -1 && v.instr >= 0) {
                        Instrument& in = inst_[(size_t)v.instr];
                        const int8_t transpose = v.seq.transpose;
                        note = (int8_t)(note + transpose);
                        v.notePeriod = period(note + in.fineTune * p_.fineTuneMul + 1);
                        v.noteOffset = note;
                        v.env = kAttack;
                        v.volume = 0;
                        v.sustainCtl = 0;
                        v.pitchCtl = 0;
                        v.pitchFallCtl = 0;
                        v.bendSpeed = 0;
                        v.phaseSpeed = in.phaseSpeed;
                        switch (row.effect) {
                            case 0:
                                if (row.param != 0) {
                                    in.attackMax = row.param;
                                    in.attackSpeed = row.param;
                                    v.waveSpeed = 0;
                                }
                                break;
                            case 2:
                                speed_ = row.param;
                                v.waveSpeed = 0;
                                break;
                            case 3:
                                numRows_ = row.param;
                                v.waveSpeed = 0;
                                break;
                            default:
                                v.bendTo = (uint8_t)(row.effect + transpose);
                                v.bendSpeed = (int8_t)row.param;
                                break;
                        }
                    }
                }
                ++v.rowIndex;
            } else {
                --v.rowCount;
            }
        }

        if (v.instr < 0 || v.waveformNumber == 0) return;
        const Instrument& in = inst_[(size_t)v.instr];
        const int volume = envelope(v, in);
        per = arpeggio(v, in);
        bending(v, in);
        phaseShift(v, in);
        v.pitchFallCtl = (uint16_t)(v.pitchFallCtl - in.pitchFall);
        v.notePeriod = (uint16_t)(v.notePeriod + v.pitchFallCtl);
        if (!v.loopControl) {
            if (v.waveSpeed == 0) {
                if (v.waveIndex != 16) {
                    const size_t at = (size_t)(v.waveformNumber - 1) * 16 + v.waveIndex;
                    if (at + 1 >= waveList_.size()) {
                        v.waveIndex = 16;  // past the wave lists: stop
                    } else if (waveList_[at] == 0xff) {
                        v.waveIndex = (uint8_t)(waveList_[at + 1] & 0xfe);
                    } else {
                        if (const int8_t* w = wave(waveList_[at])) {
                            addr = w;
                            addrLen = 32;
                            loopStart = 0;
                        }
                        v.waveSpeed = waveList_[at + 1];
                        v.waveIndex = (uint8_t)(v.waveIndex + 2);
                    }
                }
            } else {
                --v.waveSpeed;
            }
        }
        if (per != 0) c.setAmigaPeriod(v.notePeriod);
        if (volume != 0) c.setVolume((uint32_t)volume);
        if (addr) {
            c.playSample(addr, 0, addrLen);
            if (loopStart != -1) c.setLoop((uint32_t)loopStart, addrLen - (uint32_t)loopStart);
        }
    }

    int envelope(Voice& v, const Instrument& in) {
        if (in.volume != 0) return in.volume * 4;
        int volume = 0;
        switch (v.env) {
            case kAttack:
                volume = v.volume + in.attackSpeed;
                if (volume > in.attackMax) {
                    volume = in.attackMax;
                    v.env = kDecay;
                }
                v.volume = (uint8_t)volume;
                break;
            case kDecay:
                volume = v.volume - in.decaySpeed;
                if (volume <= in.decayMin) {
                    volume = in.decayMin;
                    v.sustainCtl = in.sustainTime;
                    v.env = kSustain;
                }
                v.volume = (uint8_t)volume;
                break;
            case kSustain:
                volume = v.volume;
                if (--v.sustainCtl == 0) v.env = kRelease;
                break;
            case kRelease:
                volume = v.volume - in.releaseSpeed;
                if (volume <= in.releaseMin) {
                    volume = in.releaseMin;
                    v.env = kDone;
                }
                v.volume = (uint8_t)volume;
                break;
            case kDone:
                break;
        }
        return volume;
    }

    uint32_t arpeggio(Voice& v, const Instrument& in) {
        v.arpIndex = (uint8_t)((v.arpIndex + 1) & 0x0f);
        v.notePeriod = period(v.noteOffset + in.arp[v.arpIndex] + in.fineTune * p_.fineTuneMul);
        return v.notePeriod;
    }

    void bending(Voice& v, const Instrument& in) {
        if (v.bendSpeed == 0) return;
        const int16_t speed = (int16_t)-v.bendSpeed;
        const int64_t index = v.bendTo + in.fineTune * p_.fineTuneMul;
        if (index >= p_.periodCount) {
            v.bendSpeed = 0;
            return;
        }
        const uint16_t to = period(index);
        v.pitchCtl = (uint16_t)(v.pitchCtl + speed);
        v.notePeriod = (uint16_t)(v.notePeriod + v.pitchCtl);
        if (speed < 0 ? v.notePeriod < to : v.notePeriod > to) {
            v.noteOffset = (int8_t)v.bendTo;
            v.notePeriod = to;
            v.pitchCtl = 0;
            v.bendSpeed = 0;
        }
    }

    void phaseShift(Voice& v, const Instrument& in) {
        if (in.phaseShift == 0) return;
        if (v.phaseSpeed == 0) {
            if (const int8_t* w = wave(in.phaseShift)) {
                v.phaseIndex = (uint8_t)((v.phaseIndex + 1) & 0x1f);
                v.notePeriod = (uint16_t)(v.notePeriod + w[v.phaseIndex] / 4);
            }
        } else {
            --v.phaseSpeed;
        }
    }

    void waveMixing(const Mix& m, MixState& st) {
        if (!p_.mixing || m.speed == 0) return;
        if (st.counter == 0) {
            st.counter = m.speed;
            st.position = (st.position + 1) & 0x1f;
            const int8_t* s1 = m.source1 <= waveCount_ ? wave((int)m.source1) : nullptr;
            const int8_t* s2 = m.source2 <= waveCount_ ? wave((int)m.source2) : nullptr;
            int8_t* dst = m.dest >= 1 && m.dest <= waveCount_ ? &waves_[(size_t)(m.dest - 1) * 32] : nullptr;
            if (s1 && s2 && dst) {
                uint32_t pos = st.position;
                for (int i = 31; i >= 0; --i) {
                    dst[i] = (int8_t)((s1[i] + s2[pos]) / 2);
                    pos = (pos - 1) & 0x1f;
                }
            }
        }
        --st.counter;
    }
};
}  // namespace

bool detectSidMon1(const uint8_t* d, size_t n) {
    Probe p;
    return probe(d, n, p);
}

std::unique_ptr<AmigaSynthPlayer> openSidMon1(const uint8_t* d, size_t n) {
    Probe p;
    if (!probe(d, n, p)) return nullptr;
    std::unique_ptr<SidMon1> s(new SidMon1);
    if (!s->load(d, n, static_cast<Probe&&>(p))) return nullptr;
    return s;
}
