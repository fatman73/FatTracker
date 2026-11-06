// Digital Mugician 1 / 2 (Reinier van Vliet) replayer, ported from NostalgicPlayer's
// DigitalMugicianWorker.cs (Thomas Neumann, MIT licence) onto AmigaSynthPlayer. Plays the first playable
// sub-song. Mugician 2 has 7 voices: 0-2 come from the sub-song's sequences, 3-6 from the next sub-song's
// and are mixed into Paula channel 3 (AmigaSynthPlayer's voices_ = 7).
// Index checks added where the C# code relied on exceptions: waveform, sample, arpeggio, track and
// instrument numbers, wave effect lengths (loop lengths reach 510 bytes on 128 byte waveforms), effect
// indexes, the period table, positions past the second sequence list in Mugician 2, and chTab (4 entries in
// NostalgicPlayer, which 7 voices can overflow; 8 here). Sample data is kept once (samples point into it),
// so overlapping sample ranges cannot multiply memory. Waveforms and instruments, which wave effects change
// while playing, are restored on every restart.
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "core/amiga_formats.h"
#include "core/amiga_synth.h"

namespace {
const uint16_t kPeriods[] = {
    4825, 4554, 4299, 4057, 3820, 3615, 3412, 3220, 3040, 2869, 2708, 2556, 2412, 2277, 2149, 2029,
    1915, 1807, 1706, 1610, 1520, 1434, 1354, 1278, 1206, 1139, 1075, 1014, 957, 904, 854, 805,
    760, 717, 677, 639, 603, 569, 537, 507, 479, 452, 426, 403, 380, 359, 338, 319,
    302, 285, 269, 254, 239, 226, 213, 201, 190, 179, 169, 160, 151, 142, 134, 127,
    4842, 4571, 4314, 4072, 3843, 3628, 3424, 3232, 3051, 2879, 2718, 2565, 2421, 2285, 2157, 2036,
    1922, 1814, 1712, 1616, 1525, 1440, 1359, 1283, 1211, 1143, 1079, 1018, 961, 907, 856, 808,
    763, 720, 679, 641, 605, 571, 539, 509, 480, 453, 428, 404, 381, 360, 340, 321,
    303, 286, 270, 254, 240, 227, 214, 202, 191, 180, 170, 160, 151, 143, 135, 127,
    4860, 4587, 4330, 4087, 3857, 3641, 3437, 3244, 3062, 2890, 2728, 2574, 2430, 2294, 2165, 2043,
    1929, 1820, 1718, 1622, 1531, 1445, 1364, 1287, 1215, 1147, 1082, 1022, 964, 910, 859, 811,
    765, 722, 682, 644, 607, 573, 541, 511, 482, 455, 430, 405, 383, 361, 341, 322,
    304, 287, 271, 255, 241, 228, 215, 203, 191, 181, 170, 161, 152, 143, 135, 128,
    4878, 4604, 4345, 4102, 3871, 3654, 3449, 3255, 3073, 2900, 2737, 2584, 2439, 2302, 2173, 2051,
    1936, 1827, 1724, 1628, 1536, 1450, 1369, 1292, 1219, 1151, 1086, 1025, 968, 914, 862, 814,
    768, 725, 684, 646, 610, 575, 543, 513, 484, 457, 431, 407, 384, 363, 342, 323,
    305, 288, 272, 256, 242, 228, 216, 203, 192, 181, 171, 161, 152, 144, 136, 128,
    4895, 4620, 4361, 4116, 3885, 3667, 3461, 3267, 3084, 2911, 2747, 2593, 2448, 2310, 2181, 2058,
    1943, 1834, 1731, 1634, 1542, 1455, 1374, 1297, 1224, 1155, 1090, 1029, 971, 917, 865, 817,
    771, 728, 687, 648, 612, 578, 545, 515, 486, 458, 433, 408, 385, 364, 343, 324,
    306, 289, 273, 257, 243, 229, 216, 204, 193, 182, 172, 162, 153, 144, 136, 129,
    4913, 4637, 4377, 4131, 3899, 3681, 3474, 3279, 3095, 2921, 2757, 2603, 2456, 2319, 2188, 2066,
    1950, 1840, 1737, 1639, 1547, 1461, 1379, 1301, 1228, 1159, 1094, 1033, 975, 920, 868, 820,
    774, 730, 689, 651, 614, 580, 547, 516, 487, 460, 434, 410, 387, 365, 345, 325,
    307, 290, 274, 258, 244, 230, 217, 205, 193, 183, 172, 163, 154, 145, 137, 129,
    4931, 4654, 4393, 4146, 3913, 3694, 3486, 3291, 3106, 2932, 2767, 2612, 2465, 2327, 2196, 2073,
    1957, 1847, 1743, 1645, 1553, 1466, 1384, 1306, 1233, 1163, 1098, 1037, 978, 923, 872, 823,
    777, 733, 692, 653, 616, 582, 549, 518, 489, 462, 436, 411, 388, 366, 346, 326,
    308, 291, 275, 259, 245, 231, 218, 206, 194, 183, 173, 163, 154, 145, 137, 130,
    4948, 4671, 4409, 4161, 3928, 3707, 3499, 3303, 3117, 2942, 2777, 2621, 2474, 2335, 2204, 2081,
    1964, 1854, 1750, 1651, 1559, 1471, 1389, 1311, 1237, 1168, 1102, 1040, 982, 927, 875, 826,
    779, 736, 694, 655, 619, 584, 551, 520, 491, 463, 437, 413, 390, 368, 347, 328,
    309, 292, 276, 260, 245, 232, 219, 206, 195, 184, 174, 164, 155, 146, 138, 130,
    4966, 4688, 4425, 4176, 3942, 3721, 3512, 3315, 3129, 2953, 2787, 2631, 2483, 2344, 2212, 2088,
    1971, 1860, 1756, 1657, 1564, 1477, 1394, 1315, 1242, 1172, 1106, 1044, 985, 930, 878, 829,
    782, 738, 697, 658, 621, 586, 553, 522, 493, 465, 439, 414, 391, 369, 348, 329,
    310, 293, 277, 261, 246, 233, 219, 207, 196, 185, 174, 164, 155, 146, 138, 131,
    4984, 4705, 4441, 4191, 3956, 3734, 3524, 3327, 3140, 2964, 2797, 2640, 2492, 2352, 2220, 2096,
    1978, 1867, 1762, 1663, 1570, 1482, 1399, 1320, 1246, 1176, 1110, 1048, 989, 934, 881, 832,
    785, 741, 699, 660, 623, 588, 555, 524, 495, 467, 441, 416, 392, 370, 350, 330,
    312, 294, 278, 262, 247, 233, 220, 208, 196, 185, 175, 165, 156, 147, 139, 131,
    5002, 4722, 4457, 4206, 3970, 3748, 3537, 3339, 3151, 2974, 2807, 2650, 2501, 2361, 2228, 2103,
    1985, 1874, 1769, 1669, 1576, 1487, 1404, 1325, 1251, 1180, 1114, 1052, 993, 937, 884, 835,
    788, 744, 702, 662, 625, 590, 557, 526, 496, 468, 442, 417, 394, 372, 351, 331,
    313, 295, 279, 263, 248, 234, 221, 209, 197, 186, 175, 166, 156, 148, 139, 131,
    5020, 4739, 4473, 4222, 3985, 3761, 3550, 3351, 3163, 2985, 2818, 2659, 2510, 2369, 2236, 2111,
    1992, 1881, 1775, 1675, 1581, 1493, 1409, 1330, 1255, 1185, 1118, 1055, 996, 940, 887, 838,
    791, 746, 704, 665, 628, 592, 559, 528, 498, 470, 444, 419, 395, 373, 352, 332,
    314, 296, 280, 264, 249, 235, 222, 209, 198, 187, 176, 166, 157, 148, 140, 132,
    5039, 4756, 4489, 4237, 3999, 3775, 3563, 3363, 3174, 2996, 2828, 2669, 2519, 2378, 2244, 2118,
    2000, 1887, 1781, 1681, 1587, 1498, 1414, 1335, 1260, 1189, 1122, 1059, 1000, 944, 891, 841,
    794, 749, 707, 667, 630, 594, 561, 530, 500, 472, 445, 420, 397, 374, 353, 334,
    315, 297, 281, 265, 250, 236, 223, 210, 198, 187, 177, 167, 157, 149, 140, 132,
    5057, 4773, 4505, 4252, 4014, 3788, 3576, 3375, 3186, 3007, 2838, 2679, 2528, 2387, 2253, 2126,
    2007, 1894, 1788, 1688, 1593, 1503, 1419, 1339, 1264, 1193, 1126, 1063, 1003, 947, 894, 844,
    796, 752, 710, 670, 632, 597, 563, 532, 502, 474, 447, 422, 398, 376, 355, 335,
    316, 298, 282, 266, 251, 237, 223, 211, 199, 188, 177, 167, 158, 149, 141, 133,
    5075, 4790, 4521, 4268, 4028, 3802, 3589, 3387, 3197, 3018, 2848, 2688, 2538, 2395, 2261, 2134,
    2014, 1901, 1794, 1694, 1599, 1509, 1424, 1344, 1269, 1198, 1130, 1067, 1007, 951, 897, 847,
    799, 754, 712, 672, 634, 599, 565, 533, 504, 475, 449, 423, 400, 377, 356, 336,
    317, 299, 283, 267, 252, 238, 224, 212, 200, 189, 178, 168, 159, 150, 141, 133,
    5093, 4808, 4538, 4283, 4043, 3816, 3602, 3399, 3209, 3029, 2859, 2698, 2547, 2404, 2269, 2142,
    2021, 1908, 1801, 1700, 1604, 1514, 1429, 1349, 1273, 1202, 1134, 1071, 1011, 954, 900, 850,
    802, 757, 715, 675, 637, 601, 567, 535, 505, 477, 450, 425, 401, 379, 357, 337,
    318, 300, 284, 268, 253, 238, 225, 212, 201, 189, 179, 169, 159, 150, 142, 134
};
constexpr int kPeriodCount = (int)(sizeof kPeriods / sizeof kPeriods[0]);
constexpr int kPeriodStartOffset = 7;

enum Effect : int {
    kNone, kPitchBend, kNoInstrumentEffect, kNoInstrumentVolume, kNoInstrumentEffectAndVolume, kPatternLength,
    kSongSpeed, kFilterOn, kFilterOff, kSwitchFilter, kNoDma, kArpeggio, kNoWander, kShuffle
};

enum InstEffect : uint8_t {
    kFxNone, kFilter, kMixing, kScrLeft, kScrRight, kUpsample, kDownsample, kNegate, kMadMix1, kAddition,
    kFilter2, kMorphing, kMorphF, kFilter3, kPolygate, kColgate
};

struct Instrument {
    uint8_t waveformNumber = 0;  // >= 32: sample number + 32
    uint16_t loopLength = 0;
    uint8_t volume = 0, volumeSpeed = 0, arpeggioNumber = 0, pitch = 0, effectIndex = 0, delay = 0, finetune = 0,
            pitchLoop = 0, pitchSpeed = 0, effect = 0, sourceWave1 = 0, sourceWave2 = 0, effectSpeed = 0;
    bool volumeLoop = false;
};

struct Sample {
    uint32_t offset = 0, length = 0;  // in the sample area
    int64_t loopStart = -1;           // from the sample's start, -1: no loop
};

struct Voice {  // VoiceInfo
    uint16_t track = 0;
    int16_t transpose = 0;
    uint16_t lastNote = 0, lastInstrument = 0;
    int lastEffect = kNone;
    uint16_t lastEffectParam = 0, fineTune = 0, notePeriod = 0, pitchBendEndNote = 0, pitchBendEndPeriod = 0;
    int16_t currentPitchBendPeriod = 0;
    uint16_t pitchIndex = 0, arpeggioIndex = 0, volumeIndex = 0, volumeSpeedCounter = 0;
    uint8_t instrumentDelay = 0, instrumentEffectSpeed = 0;
};

using Wave = std::array<int8_t, 128>;
const uint8_t kEmptyRow[4] = {};

uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

// The file's layout, checked against its size (shared by isMugician and the loader).
struct Layout {
    bool v2 = false, arps = false;
    uint16_t numTracks = 0;
    uint32_t seqLen[8] = {}, numInst = 0, numWaves = 0, numSamples = 0, samplesSize = 0;
    size_t seq[8] = {}, inst = 0, waves = 0, sampleInfo = 0, tracks = 0, sampleData = 0;
    int song = -1;  // the first playable sub-song
};

bool parseLayout(const uint8_t* d, size_t n, Layout& l) {
    if (!d || n < 204) return false;
    if (std::memcmp(d, " MUGICIAN/SOFTEYES 1990 ", 24) == 0) l.v2 = false;
    else if (std::memcmp(d, " MUGICIAN2/SOFTEYES 1990", 24) == 0) l.v2 = true;
    else return false;
    l.arps = rd16(d + 24) != 0;
    l.numTracks = rd16(d + 26);
    for (int i = 0; i < 8; ++i) l.seqLen[i] = rd32(d + 28 + 4 * i);
    l.numInst = rd32(d + 60);
    l.numWaves = rd32(d + 64);
    l.numSamples = rd32(d + 68);
    l.samplesSize = rd32(d + 72);
    if (l.numTracks == 0) return false;  // NostalgicPlayer indexes tracks[track] without a check
    uint64_t at = 204;
    for (int i = 0; i < 8; ++i) {
        l.seq[i] = (size_t)at;
        at += (uint64_t)l.seqLen[i] * 8;
        if (at > n) return false;
    }
    l.inst = (size_t)at;
    at += (uint64_t)l.numInst * 16;
    if (at > n) return false;
    l.waves = (size_t)at;
    at += (uint64_t)l.numWaves * 128;
    if (at > n) return false;
    l.sampleInfo = (size_t)at;
    at += (uint64_t)l.numSamples * 32;
    if (at > n) return false;
    l.tracks = (size_t)at;
    at += (uint64_t)l.numTracks * 256;
    if (at > n) return false;
    l.sampleData = (size_t)at;
    for (uint32_t i = 0; i < l.numSamples; ++i) {
        const uint8_t* p = d + l.sampleInfo + 32 * (size_t)i;
        const uint32_t start = rd32(p), end = rd32(p + 4);
        if (end < start || l.sampleData + (uint64_t)end > n) return false;
    }
    if (l.arps && l.sampleData + (uint64_t)l.samplesSize + 256 > n) return false;
    // sub-songs that can be played: complete sequence lists with something in them
    const int step = l.v2 ? 2 : 1;
    for (int i = 0; i < 8 && l.song < 0; i += step) {
        if (l.seqLen[i] != d[76 + 16 * i + 3]) continue;
        for (uint32_t j = 0; j < l.seqLen[i] * 4 && l.song < 0; ++j)
            if (d[l.seq[i] + 2 * j] != 0 || d[l.seq[i] + 2 * j + 1] != 0) l.song = i;
    }
    return l.song >= 0;
}

class Mugician : public AmigaSynthPlayer {
public:
    bool load(const uint8_t* d, size_t n) {
        Layout l;
        if (!parseLayout(d, n, l)) return false;
        v2_ = l.v2;
        voices_ = v2_ ? 7 : 4;
        const uint8_t* sub = d + 76 + 16 * l.song;
        loopSong_ = sub[0] != 0;
        loopPosition_ = sub[1];
        songSpeed_ = sub[2];
        songLength_ = sub[3];
        seq1_.assign(d + l.seq[l.song], d + l.seq[l.song] + (size_t)l.seqLen[l.song] * 8);
        if (v2_) seq2_.assign(d + l.seq[l.song + 1], d + l.seq[l.song + 1] + (size_t)l.seqLen[l.song + 1] * 8);
        instBackup_.resize(l.numInst);
        for (uint32_t i = 0; i < l.numInst; ++i) {
            const uint8_t* p = d + l.inst + 16 * (size_t)i;
            Instrument& in = instBackup_[i];
            in.waveformNumber = p[0];
            in.loopLength = (uint16_t)(p[1] * 2);
            in.volume = p[2];
            in.volumeSpeed = p[3];
            in.arpeggioNumber = p[4];
            in.pitch = p[5];
            in.effectIndex = p[6];
            in.delay = p[7];
            in.finetune = p[8];
            in.pitchLoop = p[9];
            in.pitchSpeed = p[10];
            in.effect = p[11];
            in.sourceWave1 = p[12];
            in.sourceWave2 = p[13];
            in.effectSpeed = p[14];
            in.volumeLoop = p[15] != 0;
        }
        wavesBackup_.resize(l.numWaves);
        for (uint32_t i = 0; i < l.numWaves; ++i) std::memcpy(wavesBackup_[i].data(), d + l.waves + 128 * (size_t)i, 128);
        sampleArea_.assign((const int8_t*)d + l.sampleData, (const int8_t*)d + n);
        samples_.resize(l.numSamples);
        for (uint32_t i = 0; i < l.numSamples; ++i) {
            const uint8_t* p = d + l.sampleInfo + 32 * (size_t)i;
            Sample& s = samples_[i];
            const uint32_t start = rd32(p), end = rd32(p + 4);
            const int32_t loop = (int32_t)rd32(p + 8);
            s.offset = start;
            s.length = end - start;
            s.loopStart = loop != 0 ? (int64_t)loop - start : -1;
        }
        tracks_.assign(d + l.tracks, d + l.tracks + (size_t)l.numTracks * 256);
        numTracks_ = l.numTracks;
        std::memset(arpeggios_, 0, sizeof arpeggios_);
        if (l.arps) std::memcpy(arpeggios_, d + l.sampleData + l.samplesSize, sizeof arpeggios_);
        inst_ = instBackup_;
        waves_ = wavesBackup_;
        format_ = v2_ ? "DIGITAL MUGICIAN 2 (AMIGA)" : "DIGITAL MUGICIAN (AMIGA)";
        measure();
        return true;
    }

protected:
    void initSound() override {
        inst_ = instBackup_;                                         // same size: no reallocation
        std::copy(wavesBackup_.begin(), wavesBackup_.end(), waves_.begin());  // channels point into waves_
        initializeSound();
    }

    void playTick() override {
        std::memset(chTab_, 0x80, sizeof chTab_);
        chTabIndex_ = 0;
        for (int i = 0; i < voices_; ++i) playNote(i);
        for (int i = 0; i < voices_; ++i) doEffects(i);
        newPattern_ = newRow_ = false;
        if (--speed_ == 0) {
            speed_ = (uint16_t)(currentSpeed_ & 0x0F);
            changeSpeed((uint16_t)(((currentSpeed_ & 0x0F) << 4) | ((currentSpeed_ & 0xF0) >> 4)));
            newRow_ = true;
            if (++currentRow_ == 64 || currentRow_ == patternLength_) {
                currentRow_ = 0;
                newPattern_ = true;
                if (++currentPosition_ >= songLength_) {
                    if (loopSong_) currentPosition_ = loopPosition_ < songLength_ ? loopPosition_ : 0;
                    else initializeSound();
                    endReached_ = true;
                }
            }
        }
    }

private:
    bool v2_ = false, loopSong_ = false;
    uint8_t loopPosition_ = 0, songSpeed_ = 0, songLength_ = 0;
    std::vector<uint8_t> seq1_, seq2_;  // [position * 4 + voice]: track, transpose
    std::vector<Instrument> instBackup_, inst_;
    std::vector<Wave> wavesBackup_, waves_;
    std::vector<int8_t> sampleArea_;
    std::vector<Sample> samples_;
    std::vector<uint8_t> tracks_;  // [track][row]: note, instrument, effect, parameter
    uint16_t numTracks_ = 0;
    uint8_t arpeggios_[8][32] = {};

    Voice voice_[7];
    uint16_t speed_ = 0, currentSpeed_ = 0, lastShownSpeed_ = 0, currentPosition_ = 0, currentRow_ = 0,
             patternLength_ = 64;
    bool newPattern_ = true, newRow_ = true;
    uint8_t chTab_[8] = {};
    int chTabIndex_ = 0;

    void initializeSound() {
        speed_ = songSpeed_;
        currentSpeed_ = (uint16_t)(((songSpeed_ & 0x0F) << 4) | (songSpeed_ & 0x0F));
        lastShownSpeed_ = currentSpeed_;
        newPattern_ = newRow_ = true;
        currentPosition_ = 0;
        currentRow_ = 0;
        patternLength_ = 64;
        for (Voice& v : voice_) v = Voice{};
        std::memset(chTab_, 0, sizeof chTab_);
    }

    void changeSpeed(uint16_t s) {
        if (s != lastShownSpeed_) lastShownSpeed_ = currentSpeed_ = s;
    }

    uint16_t getPeriod(int note, int transpose, int fineTune) const {
        const int i = kPeriodStartOffset + note + transpose + fineTune * 64;
        if (i < 0) return 0;
        return kPeriods[i >= kPeriodCount ? kPeriodCount - 1 : i];
    }

    Wave* wave(unsigned w) { return w < waves_.size() ? &waves_[w] : nullptr; }

    void playNote(int n) {
        Voice& vi = voice_[n];
        AmigaChannel& ch = ch_[n];
        if (newPattern_) {
            const std::vector<uint8_t>& seq = !v2_ || n < 3 ? seq1_ : seq2_;
            const size_t at = ((size_t)currentPosition_ * 4 + (size_t)(!v2_ || n < 3 ? n : n - 3)) * 2;
            vi.track = at + 1 < seq.size() ? seq[at] : 0;
            vi.transpose = at + 1 < seq.size() ? (int8_t)seq[at + 1] : 0;
        }
        if (newRow_) {
            const uint8_t* row = vi.track < numTracks_ ? &tracks_[((size_t)vi.track * 64 + currentRow_) * 4] : kEmptyRow;
            const uint8_t note = row[0], instNo = row[1], effect = row[2], param = row[3];
            if (note != 0) {
                if (effect != kNoWander + 62) {
                    vi.lastNote = note;
                    if (instNo != 0) vi.lastInstrument = (uint16_t)(instNo - 1);
                }
                vi.lastInstrument &= 0x3F;
                vi.lastEffect = effect < 64 ? kPitchBend : effect - 62;
                vi.lastEffectParam = param;
                if (vi.lastInstrument < inst_.size()) {
                    Instrument& instr = inst_[vi.lastInstrument];
                    vi.fineTune = instr.finetune;
                    if (vi.lastEffect == kNoWander) {
                        vi.pitchBendEndNote = note;
                        vi.pitchBendEndPeriod = getPeriod(vi.pitchBendEndNote, vi.transpose, vi.fineTune);
                    } else {
                        vi.pitchBendEndNote = effect;
                        if (vi.lastEffect == kPitchBend)
                            vi.pitchBendEndPeriod = getPeriod(vi.pitchBendEndNote, vi.transpose, vi.fineTune);
                    }
                    if (vi.lastEffect == kArpeggio) instr.arpeggioNumber = (uint8_t)(vi.lastEffectParam & 7);
                    const uint8_t waveform = instr.waveformNumber;
                    if (vi.lastEffect != kNoWander) {
                        if (waveform >= 32) {
                            if (waveform - 32u < samples_.size()) {
                                const Sample& s = samples_[waveform - 32];
                                uint32_t start = s.offset, len = s.length;
                                if (clipRange(sampleArea_.size(), start, len)) {
                                    ch.playSample(sampleArea_.data(), start, len);
                                    if (s.loopStart >= 0 && s.loopStart < (int64_t)len)
                                        ch.setLoop(start + (uint32_t)s.loopStart, len - (uint32_t)s.loopStart);
                                } else {
                                    ch.mute();
                                }
                            } else {
                                ch.mute();
                            }
                        } else if (Wave* w = wave(waveform)) {
                            const uint32_t len = instr.loopLength > 128 ? 128 : instr.loopLength;
                            if (vi.lastEffect != kNoDma) {
                                ch.playSample(w->data(), 0, len);
                                ch.setLoop(0, len);
                            } else {
                                ch.setSample(w->data(), 0, len);
                            }
                            if (!v2_ && instr.effect != kFxNone && vi.lastEffect != kNoInstrumentEffect &&
                                vi.lastEffect != kNoInstrumentEffectAndVolume) {
                                if (Wave* src = wave(instr.sourceWave1)) *w = *src;
                                instr.effectIndex = 0;
                                vi.instrumentEffectSpeed = instr.effectSpeed;
                            }
                        } else {
                            ch.mute();
                        }
                    }
                    if (vi.lastEffect != kNoInstrumentVolume && vi.lastEffect != kNoInstrumentEffectAndVolume &&
                        vi.lastEffect != kNoWander) {
                        vi.volumeSpeedCounter = 1;
                        vi.volumeIndex = 0;
                    }
                    vi.currentPitchBendPeriod = 0;
                    vi.instrumentDelay = instr.delay;
                    vi.pitchIndex = 0;
                    vi.arpeggioIndex = 0;
                }
            }
        }
        switch (vi.lastEffect) {
        case kPatternLength:
            if (vi.lastEffectParam != 0 && vi.lastEffectParam <= 64) patternLength_ = vi.lastEffectParam;
            break;
        case kSongSpeed: {
            const uint8_t parm = (uint8_t)(vi.lastEffectParam & 0x0F);
            if (parm != 0) changeSpeed((uint16_t)((parm << 4) | parm));
            break;
        }
        case kFilterOn:
        case kFilterOff: break;  // no Amiga LED filter here
        case kShuffle:
            vi.lastEffect = kNone;
            if ((vi.lastEffectParam & 0x0F) != 0 && (vi.lastEffectParam & 0xF0) != 0) changeSpeed(vi.lastEffectParam);
            break;
        default: break;
        }
    }

    void doEffects(int n) {
        Voice& vi = voice_[n];
        AmigaChannel& ch = ch_[n];
        if (vi.lastInstrument >= inst_.size()) return;
        Instrument& instr = inst_[vi.lastInstrument];
        if (instr.effect != kFxNone && instr.waveformNumber < 32) {
            const uint8_t instNum = (uint8_t)(vi.lastInstrument + 1);
            bool seen = false;
            for (uint8_t t : chTab_) seen = seen || t == instNum;
            if (!seen && chTabIndex_ < (int)sizeof chTab_) {  // each instrument's wave changes once per tick
                chTab_[chTabIndex_++] = instNum;
                if (vi.instrumentEffectSpeed == 0) {
                    vi.instrumentEffectSpeed = instr.effectSpeed;
                    if (Wave* w = wave(instr.waveformNumber)) instEffect(*w, instr);
                } else {
                    vi.instrumentEffectSpeed--;
                }
            }
        }
        if (vi.volumeSpeedCounter != 0 && --vi.volumeSpeedCounter == 0) {
            vi.volumeSpeedCounter = instr.volumeSpeed;
            vi.volumeIndex = (uint16_t)((vi.volumeIndex + 1) & 0x7F);
            if (vi.volumeIndex == 0 && !instr.volumeLoop) {
                vi.volumeSpeedCounter = 0;
            } else if (const Wave* w = wave(instr.volume)) {
                const int volume = -(int8_t)((*w)[vi.volumeIndex] + 0x81);
                ch.setAmigaVolume((uint32_t)((volume & 0xFF) / 4));
            }
        }
        int note = vi.lastNote;
        if (instr.arpeggioNumber != 0 && instr.arpeggioNumber < 8) {
            note += arpeggios_[instr.arpeggioNumber][vi.arpeggioIndex];
            vi.arpeggioIndex = (uint16_t)((vi.arpeggioIndex + 1) & 0x1F);
        }
        const uint16_t period = getPeriod((uint16_t)note, vi.transpose, vi.fineTune);
        vi.notePeriod = period;
        if (vi.lastEffect == kNoWander || vi.lastEffect == kPitchBend) {
            const int increment = -(int8_t)vi.lastEffectParam;
            vi.currentPitchBendPeriod = (int16_t)(vi.currentPitchBendPeriod + increment);
            vi.notePeriod = (uint16_t)(vi.notePeriod + vi.currentPitchBendPeriod);
            if (vi.lastEffectParam != 0) {
                const bool done = increment < 0 ? vi.notePeriod <= vi.pitchBendEndPeriod : vi.notePeriod >= vi.pitchBendEndPeriod;
                if (done) {
                    vi.currentPitchBendPeriod = (int16_t)(vi.pitchBendEndPeriod - period);
                    vi.lastEffectParam = 0;
                }
            }
        }
        if (instr.pitch != 0) {
            if (vi.instrumentDelay == 0) {
                const Wave* w = wave(instr.pitch);
                const int8_t pitch = w ? (*w)[vi.pitchIndex & 0x7F] : 0;
                vi.pitchIndex = (uint16_t)((vi.pitchIndex + 1) & 0x7F);
                if (vi.pitchIndex == 0) vi.pitchIndex = instr.pitchLoop;
                vi.notePeriod = (uint16_t)(vi.notePeriod - pitch);
            } else {
                vi.instrumentDelay--;
            }
        }
        ch.setAmigaPeriod(vi.notePeriod);
    }

    // ---------------------------------------------------------------- waveform effects
    void instEffect(Wave& w, Instrument& instr) {
        const int len = instr.loopLength > 128 ? 128 : instr.loopLength;  // the C# loops would leave the wave
        const Wave* src1 = wave(instr.sourceWave1);
        const Wave* src2 = wave(instr.sourceWave2);
        switch (instr.effect) {
        case kFilter: filter(w); break;
        case kMixing: {
            if (!src1 || !src2) break;
            int index = instr.effectIndex & 0x7F;
            instr.effectIndex = (uint8_t)((instr.effectIndex + 1) & 0x7F);
            for (int i = 0; i < len; ++i) {
                w[i] = (int8_t)(((*src1)[i] + (*src2)[index]) / 2);
                index = (index + 1) & 0x7F;
            }
            break;
        }
        case kScrLeft: {
            const int8_t first = w[0];
            for (int i = 0; i < 127; ++i) w[i] = w[i + 1];
            w[127] = first;
            break;
        }
        case kScrRight: {
            const int8_t last = w[127];
            for (int i = 126; i >= 0; --i) w[i + 1] = w[i];
            w[0] = last;
            break;
        }
        case kUpsample: upSample(w); break;
        case kDownsample: {
            int s = 64, d = 128;
            for (int i = 0; i < 64; ++i) {
                w[--d] = w[--s];
                w[--d] = w[s];
            }
            break;
        }
        case kNegate:
            if (instr.effectIndex < 128) w[instr.effectIndex] = (int8_t)-w[instr.effectIndex];
            if (++instr.effectIndex >= instr.loopLength) instr.effectIndex = 0;
            break;
        case kMadMix1: {
            if (!src2) break;
            instr.effectIndex = (uint8_t)((instr.effectIndex + 1) & 0x7F);
            const int8_t increment = (*src2)[instr.effectIndex];
            int8_t add = 3;
            for (int i = 0; i < len; ++i) {
                w[i] = (int8_t)(w[i] + add);
                add = (int8_t)(add + increment);
            }
            break;
        }
        case kAddition:
            if (!src2) break;
            for (int i = 0; i < len; ++i) w[i] = (int8_t)((*src2)[i] + w[i]);
            break;
        case kFilter2:
            for (int i = 0; i < 126; ++i) w[i + 1] = (int8_t)((w[i] * 3 + w[i + 2]) / 4);
            break;
        case kMorphing:
        case kMorphF: {
            if (!src1 || !src2) break;
            const bool full = instr.effect == kMorphing;  // MorphF: 32 steps, 16 levels
            const int mask = full ? 0x7F : 0x1F, half = full ? 64 : 16, levels = full ? 0x3F : 0x0F;
            instr.effectIndex = (uint8_t)((instr.effectIndex + 1) & mask);
            int mul1, mul2;
            if (instr.effectIndex < half) {
                mul1 = instr.effectIndex;
                mul2 = (instr.effectIndex ^ 0xFF) & levels;
            } else {
                mul1 = mask - instr.effectIndex;
                mul2 = (mul1 ^ 0xFF) & levels;
            }
            for (int i = 0; i < len; ++i) w[i] = (int8_t)(((*src1)[i] * mul1 + (*src2)[i] * mul2) / half);
            break;
        }
        case kFilter3:
            for (int i = 0; i < 126; ++i) w[i + 1] = (int8_t)((w[i] + w[i + 2]) / 2);
            break;
        case kPolygate: {
            int index = instr.effectIndex;
            if (index < 128) w[index] = (int8_t)-w[index];
            index = (index + instr.sourceWave2) & (instr.loopLength - 1);
            if (index >= 0 && index < 128) w[index] = (int8_t)-w[index];
            if (++instr.effectIndex >= instr.loopLength) instr.effectIndex = 0;
            break;
        }
        case kColgate:
            filter(w);
            if (++instr.effectIndex == instr.sourceWave2) {
                instr.effectIndex = 0;
                upSample(w);
            }
            break;
        default: break;
        }
    }

    static void filter(Wave& w) {
        for (int i = 0; i < 127; ++i) w[i] = (int8_t)((w[i] + w[i + 1]) / 2);
    }

    static void upSample(Wave& w) {
        int s = 0, d = 0;
        for (int i = 0; i < 64; ++i, s += 2) w[d++] = w[s];
        s = 0;
        for (int i = 0; i < 64; ++i) w[d++] = w[s++];
    }
};
}  // namespace

bool isMugician(const uint8_t* d, size_t n) {
    Layout l;
    return parseLayout(d, n, l);
}

std::unique_ptr<AmigaSynthPlayer> openMugician(const uint8_t* d, size_t n) {
    if (!isMugician(d, n)) return nullptr;
    std::unique_ptr<Mugician> p(new Mugician);
    if (!p->load(d, n)) return nullptr;
    return p;
}
