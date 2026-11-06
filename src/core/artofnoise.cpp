// Art Of Noise (Bastian Spiegel / Twice) replayer for the 4 voice ("AON4") and 8 voice ("AON8") formats,
// ported from NostalgicPlayer's ArtOfNoiseWorker.cs (Thomas Neumann, MIT licence) onto AmigaSynthPlayer.
// The 8 voice format gives every voice a channel of its own, panned as NostalgicPlayer does (L L R R R R L L).
// Index checks added where the C# code relied on exceptions: negative or oversized chunks, INAM / WAVE before
// the chunks they need, wave lengths that the file cannot hold (refused instead of allocated), positions past
// the position list, patterns that do not exist (played as empty rows, as are rows past 63 after a pattern
// loop), wave numbers, fine tunes past the 16 period tables (masked to 0-15), arpeggios of more than 7 notes
// (cut at 7) and sample / loop ranges past the end of their wave.
// Approximated: with "avoid noise" (Nxy) on, a new wave of a short sample is queued behind the playing one
// with its loop part only (AmigaChannel queues one looping range; NostalgicPlayer queues the whole sample and
// then its loop).
#include <algorithm>
#include <cstring>
#include <vector>

#include "core/amiga_formats.h"
#include "core/amiga_synth.h"

namespace {
const uint16_t kPeriods[16][60] = {
    {3434, 3232, 3048, 2880, 2712, 2560, 2416, 2280, 2152, 2032, 1920, 1812, 1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140,
     1076, 1016, 960, 906, 856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453, 428, 404, 381, 360,
     339, 320, 302, 285, 269, 254, 240, 226, 214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113},
    {3400, 3208, 3028, 2860, 2696, 2548, 2404, 2268, 2140, 2020, 1908, 1800, 1700, 1604, 1514, 1430, 1348, 1274, 1202, 1134,
     1070, 1010, 954, 900, 850, 802, 757, 715, 674, 637, 601, 567, 535, 505, 477, 450, 425, 401, 379, 357,
     337, 318, 300, 284, 268, 253, 239, 225, 213, 201, 189, 179, 169, 159, 150, 142, 134, 126, 119, 113},
    {3376, 3184, 3008, 2836, 2680, 2528, 2388, 2252, 2128, 2008, 1896, 1788, 1688, 1592, 1504, 1418, 1340, 1264, 1194, 1126,
     1064, 1004, 948, 894, 844, 796, 752, 709, 670, 632, 597, 563, 532, 502, 474, 447, 422, 398, 376, 355,
     335, 316, 298, 282, 266, 251, 237, 224, 211, 199, 188, 177, 167, 158, 149, 141, 133, 125, 118, 112},
    {3352, 3164, 2984, 2816, 2660, 2512, 2368, 2236, 2112, 1992, 1880, 1776, 1676, 1582, 1492, 1408, 1330, 1256, 1184, 1118,
     1056, 996, 940, 888, 838, 791, 746, 704, 665, 628, 592, 559, 528, 498, 470, 444, 419, 395, 373, 352,
     332, 314, 296, 280, 264, 249, 235, 222, 209, 198, 187, 176, 166, 157, 148, 140, 132, 125, 118, 111},
    {3328, 3140, 2964, 2796, 2640, 2492, 2352, 2220, 2096, 1980, 1868, 1764, 1664, 1570, 1482, 1398, 1320, 1246, 1176, 1110,
     1048, 990, 934, 882, 832, 785, 741, 699, 660, 623, 588, 555, 524, 495, 467, 441, 416, 392, 370, 350,
     330, 312, 294, 278, 262, 247, 233, 220, 208, 196, 185, 175, 165, 156, 147, 139, 131, 124, 117, 110},
    {3304, 3116, 2944, 2776, 2620, 2476, 2336, 2204, 2080, 1964, 1852, 1748, 1652, 1558, 1472, 1388, 1310, 1238, 1168, 1102,
     1040, 982, 926, 874, 826, 779, 736, 694, 655, 619, 584, 551, 520, 491, 463, 437, 413, 390, 368, 347,
     328, 309, 292, 276, 260, 245, 232, 219, 206, 195, 184, 174, 164, 155, 146, 138, 130, 123, 116, 109},
    {3280, 3096, 2920, 2756, 2604, 2456, 2320, 2188, 2064, 1948, 1840, 1736, 1640, 1548, 1460, 1378, 1302, 1228, 1160, 1094,
     1032, 974, 920, 868, 820, 774, 730, 689, 651, 614, 580, 547, 516, 487, 460, 434, 410, 387, 365, 345,
     325, 307, 290, 274, 258, 244, 230, 217, 205, 193, 183, 172, 163, 154, 145, 137, 129, 122, 115, 109},
    {3256, 3072, 2900, 2736, 2584, 2440, 2300, 2172, 2052, 1936, 1828, 1724, 1628, 1536, 1450, 1368, 1292, 1220, 1150, 1086,
     1026, 968, 914, 862, 814, 768, 725, 684, 646, 610, 575, 543, 513, 484, 457, 431, 407, 384, 363, 342,
     323, 305, 288, 272, 256, 242, 228, 216, 204, 192, 181, 171, 161, 152, 144, 136, 128, 121, 114, 108},
    {3628, 3424, 3232, 3048, 2880, 2712, 2560, 2416, 2280, 2152, 2032, 1920, 1814, 1712, 1616, 1524, 1440, 1356, 1280, 1208,
     1140, 1076, 1016, 960, 907, 856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453, 428, 404, 381,
     360, 339, 320, 302, 285, 269, 254, 240, 226, 214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120},
    {3600, 3400, 3208, 3028, 2860, 2700, 2544, 2404, 2268, 2140, 2020, 1908, 1800, 1700, 1604, 1514, 1430, 1350, 1272, 1202,
     1134, 1070, 1010, 954, 900, 850, 802, 757, 715, 675, 636, 601, 567, 535, 505, 477, 450, 425, 401, 379,
     357, 337, 318, 300, 284, 268, 253, 238, 225, 212, 200, 189, 179, 169, 159, 150, 142, 134, 126, 119},
    {3576, 3376, 3184, 3008, 2836, 2680, 2528, 2388, 2252, 2128, 2008, 1896, 1788, 1688, 1592, 1504, 1418, 1340, 1264, 1194,
     1126, 1064, 1004, 948, 894, 844, 796, 752, 709, 670, 632, 597, 563, 532, 502, 474, 447, 422, 398, 376,
     355, 335, 316, 298, 282, 266, 251, 237, 223, 211, 199, 188, 177, 167, 158, 149, 141, 133, 125, 118},
    {3548, 3352, 3164, 2984, 2816, 2660, 2512, 2368, 2236, 2112, 1992, 1880, 1774, 1676, 1582, 1492, 1408, 1330, 1256, 1184,
     1118, 1056, 996, 940, 887, 838, 791, 746, 704, 665, 628, 592, 559, 528, 498, 470, 444, 419, 395, 373,
     352, 332, 314, 296, 280, 264, 249, 235, 222, 209, 198, 187, 176, 166, 157, 148, 140, 132, 125, 118},
    {3524, 3328, 3140, 2964, 2796, 2640, 2492, 2352, 2220, 2096, 1976, 1868, 1762, 1664, 1570, 1482, 1398, 1320, 1246, 1176,
     1110, 1048, 988, 934, 881, 832, 785, 741, 699, 660, 623, 588, 555, 524, 494, 467, 441, 416, 392, 370,
     350, 330, 312, 294, 278, 262, 247, 233, 220, 208, 196, 185, 175, 165, 156, 147, 139, 131, 123, 117},
    {3500, 3304, 3116, 2944, 2776, 2620, 2476, 2336, 2204, 2080, 1964, 1852, 1750, 1652, 1558, 1472, 1388, 1310, 1238, 1168,
     1102, 1040, 982, 926, 875, 826, 779, 736, 694, 655, 619, 584, 551, 520, 491, 463, 437, 413, 390, 368,
     347, 328, 309, 292, 276, 260, 245, 232, 219, 206, 195, 184, 174, 164, 155, 146, 138, 130, 123, 116},
    {3472, 3280, 3096, 2920, 2756, 2604, 2456, 2320, 2188, 2064, 1948, 1840, 1736, 1640, 1548, 1460, 1378, 1302, 1228, 1160,
     1094, 1032, 974, 920, 868, 820, 774, 730, 689, 651, 614, 580, 547, 516, 487, 460, 434, 410, 387, 365,
     345, 325, 307, 290, 274, 258, 244, 230, 217, 205, 193, 183, 172, 163, 154, 145, 137, 129, 122, 115},
    {3448, 3256, 3072, 2900, 2736, 2584, 2440, 2300, 2172, 2052, 1936, 1828, 1724, 1628, 1536, 1450, 1368, 1292, 1220, 1150,
     1086, 1026, 968, 914, 862, 814, 768, 725, 684, 646, 610, 575, 543, 513, 484, 457, 431, 407, 384, 363,
     342, 323, 305, 288, 272, 256, 242, 228, 216, 203, 192, 181, 171, 161, 152, 144, 136, 128, 121, 114}};
const uint8_t kVibratoSine[32] = {0,   24,  49,  74,  97,  120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
                                  255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120, 97,  74,  49,  24};
const uint8_t kVibratoRampDown[32] = {255, 248, 240, 232, 224, 216, 208, 200, 192, 184, 176, 168, 160, 152, 144, 136,
                                      128, 120, 112, 104, 96,  88,  80,  72,  64,  56,  48,  40,  32,  24,  16,  8};
const int8_t kNibble[16] = {0, 1, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -1};

uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

enum Effect : uint8_t {
    kArpeggio = 0, kSlideUp, kSlideDown, kTonePortamento, kVibrato, kTonePortamentoAndVolumeSlide,
    kVibratoAndVolumeSlide, kSetSampleOffset = 9, kVolumeSlide, kPositionJump, kSetVolume, kPatternBreak,
    kExtraEffects, kSetSpeed, kNewVolume, kSynthControl, kWaveTableSpeedControl, kSetArpSpeed,
    kSetVolumeAndVibrato, kFineSlideAndPortamentoUp, kFineSlideAndPortamentoDown, kAvoidNoise, kOversize,
    kFineVolumeSlideAndVibrato, kVolumeAndPortDownSlide, kSetVolumeAndTonePortamento,
    kFineVolumeSlideAndTonePortamento, kSetTrackVolume, kSetWaveTableMode, kExternalEvent = 33
};
enum ExtraEffect : uint8_t {
    kSetFilter = 0x00, kFineSlideUp = 0x10, kFineSlideDown = 0x20, kSetVibratoWaveform = 0x40, kSetLoop = 0x50,
    kPatternLoop = 0x60, kRetrigNote = 0x90, kFineVolumeSlideUp = 0xA0, kFineVolumeSlideDown = 0xB0,
    kNoteCut = 0xC0, kNoteDelay = 0xD0, kPatternDelay = 0xE0
};
enum Envelope : uint8_t { kDone, kAdd, kSub };

bool isTonePortamento(uint8_t fx) {
    return fx == kTonePortamento || fx == kTonePortamentoAndVolumeSlide || fx == kSetVolumeAndTonePortamento ||
           fx == kFineVolumeSlideAndTonePortamento;
}

struct TrackLine {
    uint8_t note = 0, instrument = 0, arpeggio = 0, effect = 0, effectArg = 0;
};
struct Instrument {
    bool synth = false;
    uint8_t volume = 0, fineTune = 0, waveForm = 0;
    uint8_t envelopeStart = 0, envelopeAdd = 0, envelopeEnd = 0, envelopeSub = 0;
    uint32_t startOffset = 0, length = 0, loopStart = 0, loopLength = 0;  // sample
    uint8_t synthLength = 0, vibParam = 0, vibDelay = 0, vibWave = 0, waveSpeed = 0, waveLength = 0,
            waveLoopStart = 0, waveLoopLength = 0, waveLoopControl = 0;  // synth
};

struct Module {
    bool eight = false;
    int channels = 4;
    uint8_t numberOfPositions = 0, restartPosition = 0;
    std::vector<uint8_t> positionList;
    std::vector<std::vector<TrackLine>> patterns;  // [pattern][row * channels + voice]
    std::vector<Instrument> instruments;
    std::vector<std::vector<int8_t>> waves;  // empty = none (NostalgicPlayer's null)
    uint8_t arpeggios[16][4] = {};
};

// The chunks, as ArtOfNoiseWorker.Load reads them (each parser leaves the position where the C# code does).
bool parse(const uint8_t* d, size_t n, Module& m) {
    if (!d || n < 54 || std::memcmp(d, "AON", 3) != 0 || (d[3] != '4' && d[3] != '8')) return false;
    m.eight = d[3] == '8';
    m.channels = m.eight ? 8 : 4;
    bool haveArpg = false, havePlst = false, havePatt = false, haveInst = false, haveWlen = false;
    size_t pos = 46;
    while (n - pos >= 8) {
        const uint8_t* name = d + pos;
        const int32_t size = (int32_t)rd32(d + pos + 4);
        pos += 8;
        if (size < 0 || (size_t)size > n - pos) return false;
        const size_t start = pos, end = pos + (size_t)size;
        if (!std::memcmp(name, "INFO", 4)) {
            if (n - start < 3) return false;  // skips a version byte
            m.numberOfPositions = d[start + 1];
            m.restartPosition = d[start + 2];
            if (m.restartPosition >= m.numberOfPositions) m.restartPosition = 0;
            pos = end;
        } else if (!std::memcmp(name, "ARPG", 4)) {
            if (n - start < 64) return false;
            for (int i = 0; i < 16; ++i) std::memcpy(m.arpeggios[i], d + start + 4 * i, 4);
            haveArpg = true;
            pos = start + 64;
        } else if (!std::memcmp(name, "PLST", 4)) {
            m.positionList.assign(d + start, d + end);
            havePlst = true;
            pos = end;
        } else if (!std::memcmp(name, "PATT", 4)) {
            const size_t patSize = (size_t)4 * m.channels * 64, count = (size_t)size / patSize;
            m.patterns.assign(count, std::vector<TrackLine>((size_t)64 * m.channels));
            for (size_t i = 0; i < count; ++i)
                for (size_t k = 0; k < (size_t)64 * m.channels; ++k) {
                    const uint8_t* b = d + start + i * patSize + 4 * k;
                    TrackLine& t = m.patterns[i][k];
                    t.instrument = (uint8_t)(b[1] & 0x3F);
                    t.note = (uint8_t)(b[0] & 0x3F);
                    t.arpeggio = (uint8_t)(((b[2] & 0xC0) >> 4) | ((b[1] & 0xC0) >> 6));
                    t.effect = (uint8_t)(b[2] & 0x3F);
                    t.effectArg = b[3];
                }
            havePatt = true;
            pos = start + count * patSize;
        } else if (!std::memcmp(name, "INST", 4)) {
            const size_t count = (size_t)size / 32;
            m.instruments.assign(count, Instrument{});
            for (size_t i = 0; i < count; ++i) {
                const uint8_t* b = d + start + 32 * i;
                Instrument& in = m.instruments[i];
                if (b[0] > 1) return false;
                in.synth = b[0] == 1;
                in.volume = b[1];
                in.fineTune = b[2];
                in.waveForm = b[3];
                if (!in.synth) {
                    in.startOffset = rd32(b + 4);
                    in.length = rd32(b + 8);
                    in.loopStart = rd32(b + 12);
                    in.loopLength = rd32(b + 16);
                } else {
                    in.synthLength = b[4];
                    in.vibParam = b[10];
                    in.vibDelay = b[11];
                    in.vibWave = b[12];
                    in.waveSpeed = b[13];
                    in.waveLength = b[14];
                    in.waveLoopStart = b[15];
                    in.waveLoopLength = b[16];
                    in.waveLoopControl = b[17];
                }
                in.envelopeStart = b[28];
                in.envelopeAdd = b[29];
                in.envelopeEnd = b[30];
                in.envelopeSub = b[31];
            }
            haveInst = true;
            pos = start + 32 * count;
        } else if (!std::memcmp(name, "INAM", 4)) {
            if (!haveInst || 32 * m.instruments.size() > n - start) return false;  // names are not used
            pos = end;
        } else if (!std::memcmp(name, "WLEN", 4)) {
            const size_t count = (size_t)size / 4;
            uint64_t total = 0;
            for (size_t i = 0; i < count; ++i) total += rd32(d + start + 4 * i);
            if (total > n) return false;  // more than the file could hold: refused instead of allocated
            m.waves.assign(count, {});
            for (size_t i = 0; i < count; ++i) m.waves[i].assign(rd32(d + start + 4 * i), 0);
            haveWlen = true;
            pos = start + 4 * count;
        } else if (!std::memcmp(name, "WAVE", 4)) {
            if (!haveWlen) return false;
            for (auto& w : m.waves) {
                if (w.size() > n - pos) return false;
                std::memcpy(w.data(), d + pos, w.size());
                pos += w.size();
            }
        } else {
            pos = end;  // NAME, AUTH, RMRK, DATE, ...
        }
    }
    if (m.positionList.size() < m.numberOfPositions) m.numberOfPositions = (uint8_t)m.positionList.size();
    if (m.restartPosition >= m.numberOfPositions) m.restartPosition = 0;
    return m.numberOfPositions != 0 && haveArpg && havePlst && havePatt && haveInst && haveWlen;
}

struct Voice {
    uint8_t chFlag = 0, lastNote = 0;
    int waveForm = -1;  // wave numbers (-1 = none)
    uint32_t waveFormOffset = 0;
    uint16_t waveLen = 0, oldWaveLen = 0;
    int repeatStart = -1;
    uint32_t repeatOffset = 0;
    uint16_t repeatLength = 0;
    int instrument = -1;
    int16_t instrumentNumber = 0;
    uint8_t volume = 0, stepFxCnt = 0, chMode = 0;
    uint16_t period = 0;
    int16_t perSlide = 0;
    uint16_t arpeggioOff = 0;
    uint8_t arpeggioFineTune = 0;
    int16_t arpeggioTab[8] = {};
    uint8_t arpeggioSpd = 0, arpeggioCnt = 0;
    int synthWaveAct = -1;
    uint32_t synthWaveActOffset = 0, synthWaveEndOffset = 0;
    int synthWaveRep = -1;
    uint32_t synthWaveRepOffset = 0, synthWaveRepEndOffset = 0;
    int32_t synthWaveAddBytes = 0;
    uint8_t synthWaveCnt = 0, synthWaveSpd = 0, synthWaveRepCtrl = 0, synthWaveCont = 0, synthWaveStop = 0;
    uint8_t synthAdd = 0, synthSub = 0, synthEnd = 0;
    Envelope synthEnv = kDone;
    uint8_t synthVol = 0, vibOn = 0;
    bool vibDone = false;
    uint8_t vibCont = 0, vibratoSpd = 0, vibratoAmpl = 0, vibratoPos = 0;
    int16_t vibratoTrigDelay = 0;
    uint8_t fxCom = 0, fxDat = 0;
    bool slideFlag = false;
    uint32_t oldSampleOffset = 0;
    uint8_t glissSpd = 0, trackVolume = 64;
};

class ArtOfNoise : public AmigaSynthPlayer {
public:
    bool load(const uint8_t* d, size_t n) {
        if (!parse(d, n, m_)) return false;
        voices_ = m_.channels;
        ownChannels_ = true;
        format_ = m_.eight ? "ART OF NOISE 8V (AMIGA)" : "ART OF NOISE 4V (AMIGA)";
        visited_.assign(256, false);
        measure();
        return true;
    }

protected:
    bool leftSide(int v) const override {
        static const bool kPan8[8] = {true, true, false, false, false, false, true, true};
        return m_.eight ? kPan8[v & 7] : AmigaSynthPlayer::leftSide(v);
    }

    void initSound() override { initializeSound(0); }

    void playTick() override {
        ++frameCnt_;
        if (speed_ != 0 && frameCnt_ >= speed_) {
            frameCnt_ = 0;
            playNewStep();
        }
        currentPattern_ = m_.positionList[position_];  // PlayFx
        for (int i = 0; i < m_.channels; ++i) doFx(voice_[i]);
        for (int i = 0; i < m_.channels; ++i) setupChannel(voice_[i], ch_[i]);
        if (restartSong_) {  // F00
            restartSong_ = false;
            initializeSound(0);
        }
    }

private:
    Module m_;
    std::vector<bool> visited_;
    Voice voice_[8];
    uint8_t tempo_ = 125, speed_ = 6, frameCnt_ = 0;
    bool patternBreak_ = false;
    uint8_t patCnt_ = 0;
    int8_t patDelayCnt_ = 0;
    bool loopFlag_ = false;
    uint8_t loopPoint_ = 0, loopCnt_ = 0, position_ = 0, newPosition_ = 0, currentPattern_ = 0;
    bool noiseAvoid_ = false, oversize_ = false, restartSong_ = false;

    void initializeSound(int startPosition) {
        tickHz_ = 50.0;
        tempo_ = 125;
        speed_ = 6;
        frameCnt_ = 0;
        patternBreak_ = false;
        patCnt_ = 0;
        patDelayCnt_ = 0;
        loopFlag_ = false;
        loopPoint_ = loopCnt_ = 0;
        position_ = (uint8_t)startPosition;
        currentPattern_ = m_.positionList[(size_t)startPosition];
        noiseAvoid_ = oversize_ = false;
        for (Voice& v : voice_) v = Voice{};
        std::fill(visited_.begin(), visited_.end(), false);
        visited_[position_] = true;
    }

    int waveRef(int number) const {  // NostalgicPlayer's waveForms[number]: null when empty (or missing)
        return number >= 0 && (size_t)number < m_.waves.size() && !m_.waves[(size_t)number].empty() ? number : -1;
    }
    static const uint16_t* periods(uint8_t fineTune) { return kPeriods[fineTune & 15]; }

    TrackLine row(uint8_t pattern, uint8_t r, int voice) const {
        if (pattern >= m_.patterns.size() || r >= 64) return TrackLine{};
        return m_.patterns[pattern][(size_t)r * m_.channels + voice];
    }

    void playNewStep() {
        bool oneMoreTime;
        do {
            oneMoreTime = false;
            if (!patternBreak_) {
                if (patDelayCnt_ > 0) {
                    --patDelayCnt_;
                    return;
                }
                patDelayCnt_ = -1;
                for (int i = 0; i < m_.channels; ++i) getDaChannel(row(currentPattern_, patCnt_, i), voice_[i]);
                if (loopFlag_) {
                    loopFlag_ = false;
                    patCnt_ = loopPoint_;
                    return;
                }
                ++patCnt_;
                if (patCnt_ < 64) return;
            } else {
                position_ = newPosition_;
            }
            patCnt_ = 0;
            patDelayCnt_ = 0;
            loopPoint_ = 0;
            loopCnt_ = 0;
            ++position_;
            if (position_ >= m_.numberOfPositions) {
                position_ = m_.restartPosition;
                endReached_ = true;
            } else if (visited_[position_]) {
                endReached_ = true;
            }
            visited_[position_] = true;
            if (patternBreak_) {
                patternBreak_ = false;
                currentPattern_ = m_.positionList[position_];
                oneMoreTime = true;
            }
        } while (oneMoreTime);
    }

    void getDaChannel(const TrackLine& t, Voice& v) {
        v.fxCom = t.effect;
        v.fxDat = t.effectArg;
        if (v.fxCom == kPatternBreak && patCnt_ == 63) patCnt_ = 62;
        const uint8_t argHi = (uint8_t)(v.fxDat & 0xF0), argLow = (uint8_t)(v.fxDat & 0x0F);
        if (v.fxCom == kNewVolume) {
            v.stepFxCnt = argLow;
        } else if (v.fxCom == kExtraEffects) {
            if (argHi == kNoteCut) {
                v.stepFxCnt = argLow;
            } else if (argHi == kPatternDelay) {
                if (patDelayCnt_ < 0) patDelayCnt_ = (int8_t)argLow;
            } else if (argHi == kPatternLoop) {
                if (loopCnt_ != 0xF0 && argLow != 0) {
                    if (loopCnt_ == 0) loopCnt_ = argLow;
                    --loopCnt_;
                    if (loopCnt_ == 0) loopCnt_ = 0xF0;
                    loopFlag_ = true;
                }
            }
        }
        int instr;
        const int instrNum = t.instrument - 1;
        if (instrNum < 0) {  // the old instrument
            instr = v.instrument;
            if (instr < 0) return;
            if (t.note != 0 && !isTonePortamento(v.fxCom)) useOldInstrument(t, v, instr);
        } else {
            if ((size_t)instrNum >= m_.instruments.size()) return;
            instr = instrNum;
            if (t.note == 0) {  // no note: only the repeat
                if (!m_.instruments[(size_t)instr].synth && v.instrument != instr) {
                    v.instrument = instr;
                    v.instrumentNumber = (int16_t)instrNum;
                    v.chFlag = 1;
                    startRepeat(t, v, m_.instruments[(size_t)instr]);
                }
                v.volume = m_.instruments[(size_t)instr].volume;
            } else {
                v.oldSampleOffset = 0;
                if (v.instrument != instr || !isTonePortamento(v.fxCom)) {
                    v.instrument = instr;
                    v.instrumentNumber = (int16_t)instrNum;
                    useOldInstrument(t, v, instr);
                }
                v.volume = m_.instruments[(size_t)instr].volume;
            }
        }
        const Instrument& in = m_.instruments[(size_t)instr];
        uint8_t note = t.note;
        if (note == 0) {
            note = v.lastNote;
            if (note == 0 || note > 60) return;
        } else {
            v.slideFlag = false;
            v.lastNote = note;
            if (note > 60) return;
        }
        v.arpeggioFineTune = in.fineTune;
        --note;
        if (isTonePortamento(v.fxCom) && t.note != 0) {
            v.slideFlag = true;
            const uint16_t period = periods(v.arpeggioFineTune)[note];
            v.perSlide = (int16_t)(v.period + v.perSlide - period);
        }
        if (v.arpeggioTab[1] == -1) {
            v.arpeggioOff = 0;
            v.arpeggioCnt = 0;
        }
        if (v.fxCom == kArpeggio && v.fxDat != 0) {  // ProTracker arpeggio
            v.arpeggioTab[0] = note;
            v.arpeggioTab[1] = (int16_t)(note + ((v.fxDat & 0xF0) >> 4));
            v.arpeggioTab[2] = (int16_t)(note + (v.fxDat & 0x0F));
            v.arpeggioTab[3] = -1;
        } else {  // "professional" arpeggio (NostalgicPlayer reads the second byte for every later pair)
            const uint8_t* arp = m_.arpeggios[t.arpeggio & 15];
            int write = 0;
            uint8_t arpByte = arp[0];
            uint8_t count = (uint8_t)(arpByte >> 4);
            if (count != 0) {
                v.arpeggioTab[write++] = (int16_t)((arpByte & 0x0F) + note);
                --count;
                while (count > 0 && write < 7) {  // the table holds 7 notes and the end mark
                    arpByte = arp[1];
                    v.arpeggioTab[write++] = (int16_t)(((arpByte & 0xF0) >> 4) + note);
                    --count;
                    if (count == 0 || write >= 7) break;
                    v.arpeggioTab[write++] = (int16_t)((arpByte & 0x0F) + note);
                    --count;
                }
            } else {
                v.arpeggioOff = 0;
                v.arpeggioCnt = (uint8_t)(v.arpeggioSpd - 1);
                v.arpeggioTab[write++] = note;
            }
            v.arpeggioTab[write] = -1;
        }
    }

    void useOldInstrument(const TrackLine& t, Voice& v, int instr) {
        const Instrument& in = m_.instruments[(size_t)instr];
        v.vibCont = 0;
        initAdsr(v, in);
        if (!in.synth) startSample(t, v, in);
        else initSynth(t, v, in);
    }

    void startSample(const TrackLine& t, Voice& v, const Instrument& in) {
        v.vibOn = 0x21;
        v.chMode = 0;
        if (v.fxCom == kExtraEffects) {
            const uint8_t extra = (uint8_t)(v.fxDat & 0xF0), argLow = (uint8_t)(v.fxDat & 0x0F);
            if (extra == kNoteDelay) {
                v.stepFxCnt = argLow;
            } else {
                if (extra == kRetrigNote) v.stepFxCnt = argLow;
                v.chFlag = 3;  // new sample wave
            }
        } else {
            v.chFlag = 3;
        }
        startRepeat(t, v, in);
    }

    void startRepeat(const TrackLine& t, Voice& v, const Instrument& in) {
        if (t.note != 0) v.perSlide = 0;
        const int wave = waveRef(in.waveForm);
        v.oldWaveLen = v.waveLen;
        v.waveLen = (uint16_t)in.length;
        if (in.loopLength != 0) {
            if (oversize_ || in.loopStart != 0) {
                v.repeatStart = wave;
                v.repeatOffset = in.loopStart * 2;
                v.repeatLength = (uint16_t)in.loopLength;
                if (!oversize_) v.waveLen = (uint16_t)(in.loopLength + in.loopStart);
            } else {
                v.repeatStart = wave;
                v.repeatOffset = 0;
                v.repeatLength = (uint16_t)in.loopLength;
            }
        } else {
            v.repeatStart = -1;
            v.repeatOffset = 0;
            v.repeatLength = 0;
        }
        const uint32_t offset = in.startOffset * 2;
        v.waveLen = (uint16_t)(v.waveLen - (uint16_t)(v.oldSampleOffset / 2));
        if (v.fxCom == kSetSampleOffset) {
            const uint32_t newOffset = v.fxDat * 256u;
            const int16_t newWaveLen = (int16_t)(v.waveLen - newOffset / 2);
            if (newWaveLen < 0) {
                v.waveForm = v.repeatStart;
                v.waveFormOffset = v.repeatOffset;
                v.waveLen = (uint16_t)(v.repeatOffset + v.repeatLength);
            } else {
                v.waveLen = (uint16_t)newWaveLen;
                v.oldSampleOffset += newOffset;
                v.waveForm = wave;
                v.waveFormOffset = offset + v.oldSampleOffset;
            }
        } else {
            v.waveForm = wave;
            v.waveFormOffset = offset + v.oldSampleOffset;
        }
    }

    static void initAdsr(Voice& v, const Instrument& in) {
        if (v.fxCom == kSynthControl && (v.fxDat & 0x01) != 0) return;
        v.synthVol = in.envelopeStart;
        if (in.envelopeAdd != 0) {
            v.synthAdd = in.envelopeAdd;
            v.synthSub = in.envelopeSub;
            v.synthEnd = in.envelopeEnd;
            v.synthEnv = kAdd;
        } else {
            v.synthVol = 127;
            v.synthEnv = kDone;
        }
    }

    void initSynth(const TrackLine& t, Voice& v, const Instrument& in) {
        v.chMode = 1;
        const uint8_t effect = v.fxCom, extra = (uint8_t)(v.fxDat & 0xF0);
        if (effect == kExtraEffects && extra == kRetrigNote) v.stepFxCnt = (uint8_t)(v.fxDat & 0x0F);
        if (t.note == 0) return;
        v.perSlide = 0;
        const uint8_t arg = effect == kSynthControl ? v.fxDat : 0;
        if ((arg & 0x10) == 0) {
            if (effect == kExtraEffects && extra == kNoteDelay) v.stepFxCnt = (uint8_t)(v.fxDat & 0x0F);
            else v.chFlag = 3;  // new wave
            const int wave = waveRef(in.waveForm);
            bool reset = true;
            if (v.waveForm == wave && v.waveFormOffset == 0) {
                v.chFlag = 0;
                reset = v.synthWaveCont == 0;
            }
            if (reset) {
                v.waveForm = wave;
                v.waveFormOffset = 0;
                uint16_t waveLength = in.synthLength;
                uint32_t offset = 0;
                if (effect == kSetSampleOffset) {
                    offset = (uint32_t)v.fxDat * waveLength;
                    if (v.synthWaveStop != 0) {
                        v.synthWaveAct = wave;
                        v.synthWaveActOffset = offset;
                        v.repeatStart = wave;
                        v.repeatOffset = offset;
                    }
                }
                if (v.synthWaveStop == 0) {
                    v.synthWaveAct = wave;
                    v.synthWaveActOffset = offset;
                    v.oldWaveLen = v.waveLen;
                    v.waveLen = waveLength;
                    v.repeatLength = waveLength;
                    waveLength = (uint16_t)(waveLength * 2);
                    v.synthWaveAddBytes = waveLength;
                    v.repeatStart = wave;
                    v.repeatOffset = (uint32_t)in.waveLoopStart * waveLength + offset;
                    v.synthWaveEndOffset = (uint32_t)in.waveLength * waveLength + offset;
                    v.synthWaveRep = wave;
                    v.synthWaveRepOffset = (uint32_t)in.waveLoopStart * waveLength + offset;
                    v.synthWaveRepEndOffset = (uint32_t)(in.waveLoopLength + in.waveLoopStart) * waveLength + offset;
                    v.synthWaveCnt = in.waveSpeed;
                    v.synthWaveSpd = in.waveSpeed;
                    v.synthWaveRepCtrl = in.waveLoopControl;
                }
            }
        }
        v.vibOn = 0;  // the vibrato
        if (in.vibWave != 3) {
            v.vibratoTrigDelay = in.vibDelay;
            if (in.vibParam != 0) {
                doFxVibrato(v, in.vibParam);
                v.vibratoAmpl &= 0x9F;
                v.vibratoAmpl |= (uint8_t)((uint8_t)(in.vibWave >> 3) | (uint8_t)(in.vibWave << 5));
                v.vibCont = 1;
            } else {
                v.vibratoTrigDelay = -2;
            }
        } else {
            v.vibOn = 0x21;
        }
    }

    // ---- effects (PlayFx / DoFx)
    void doFx(Voice& v) {
        if (v.vibCont == 0) v.vibOn = 0x21;
        ++v.arpeggioCnt;
        if (v.arpeggioCnt >= v.arpeggioSpd) v.arpeggioCnt = 0;
        int16_t periodOffset = v.arpeggioTab[v.arpeggioOff];
        if (periodOffset >= 0) {
            if (periodOffset >= 60) periodOffset = 59;
            v.period = periods(v.arpeggioFineTune)[periodOffset];
            v.arpeggioOff = (uint16_t)((v.arpeggioOff + 1) & 0x07);
        } else {
            v.arpeggioOff = 0;
        }
        if (v.fxCom != kArpeggio) {
            const uint8_t arg = v.fxDat;
            if (frameCnt_ != 0) {
                switch (v.fxCom) {
                case kSlideUp: v.perSlide = (int16_t)(v.perSlide - arg); break;
                case kSlideDown: v.perSlide = (int16_t)(v.perSlide + arg); break;
                case kTonePortamento:
                    if (arg != 0) v.glissSpd = arg;
                    doFxToneSlide(v);
                    break;
                case kVibrato:
                    v.vibOn = 1;
                    doFxVibrato(v, arg);
                    break;
                case kTonePortamentoAndVolumeSlide:
                    doFxToneSlide(v);
                    doFxVolumeSlide(v, arg);
                    break;
                case kVibratoAndVolumeSlide:
                    doFxVibOldAmpl(v);
                    doFxVolumeSlide(v, arg);
                    break;
                case kVolumeSlide: doFxVolumeSlide(v, arg); break;
                default: break;
                }
            }
            switch (v.fxCom) {
            case kPositionJump:
                newPosition_ = (uint8_t)(arg - 1);
                patCnt_ = 0;
                patternBreak_ = true;
                break;
            case kSetVolume: v.volume = arg > 64 ? 64 : arg; break;
            case kPatternBreak: {
                const uint8_t temp = (uint8_t)((arg & 0xF0) >> 1);
                uint8_t p = (uint8_t)(temp + (temp >> 2) + (arg & 0x0F));  // decimal
                if (p >= 64) p = 0;
                patCnt_ = p;
                newPosition_ = position_;
                patternBreak_ = true;
                break;
            }
            case kExtraEffects: doFxECommands(v, arg); break;
            case kSetSpeed:
                if (arg != 0) {
                    if (arg <= 32) speed_ = arg;  // 0x20 counts as a speed, as in the original player
                    else if (arg <= 200) tempo_ = arg, tickHz_ = arg / 2.5;
                } else {
                    endReached_ = true;
                    restartSong_ = true;
                }
                break;
            case kNewVolume:
                if (v.stepFxCnt == 0) v.volume = (uint8_t)(((arg & 0xF0) >> 4) * 4 + 4);
                else --v.stepFxCnt;
                break;
            case kWaveTableSpeedControl: v.synthWaveSpd = (uint8_t)((arg & 0xF0) >> 4); break;
            case kSetArpSpeed:
                if ((arg & 0x0F) != 0) v.arpeggioSpd = (uint8_t)(arg & 0x0F);
                break;
            case kSetVolumeAndVibrato:
                doFxVibOldAmpl(v);
                v.volume = arg > 64 ? 64 : arg;
                break;
            case kFineSlideAndPortamentoUp:
            case kFineSlideAndPortamentoDown: {
                const int8_t nib = kNibble[(arg & 0xF0) >> 4];
                if (nib < 0) doFxFineVolDown(v, (uint8_t)-nib);
                else doFxFineVolUp(v, (uint8_t)nib);
                if (frameCnt_ != 0) {
                    if (v.fxCom == kFineSlideAndPortamentoUp) v.perSlide = (int16_t)(v.perSlide - (arg & 0x0F));
                    else v.perSlide = (int16_t)(v.perSlide + (arg & 0x0F));
                }
                break;
            }
            case kAvoidNoise: noiseAvoid_ = arg != 0; break;
            case kOversize: oversize_ = arg != 0; break;
            case kFineVolumeSlideAndVibrato:
                doFxVibOldAmpl(v);
                doFxFineVolUpDown(v, arg);
                break;
            case kVolumeAndPortDownSlide:  // synth drums
                v.perSlide = (int16_t)(v.perSlide + (uint8_t)((arg >> 4) * 8));
                doFxVolumeSlide(v, (uint8_t)(arg & 0x0F));
                break;
            case kSetVolumeAndTonePortamento:
                doFxToneSlide(v);
                v.volume = arg > 64 ? 64 : arg;
                break;
            case kFineVolumeSlideAndTonePortamento:
                doFxToneSlide(v);
                doFxFineVolUpDown(v, arg);
                break;
            case kSetTrackVolume: v.trackVolume = arg > 64 ? 64 : arg; break;
            case kSetWaveTableMode:
                v.synthWaveCont = (uint8_t)(arg & 0x0F);
                v.synthWaveStop = (uint8_t)((arg & 0xF0) >> 4);
                break;
            case kExternalEvent: break;  // a sync byte for the program around the player
            default: break;
            }
        }
        doSynth(v);
    }

    static void doFxToneSlide(Voice& v) {
        if (!v.slideFlag || v.perSlide == 0) return;
        if (v.perSlide < 0) {
            v.perSlide = (int16_t)(v.perSlide + v.glissSpd);
            if (v.perSlide >= 0) v.perSlide = 0;
        } else {
            v.perSlide = (int16_t)(v.perSlide - v.glissSpd);
            if (v.perSlide < 0) v.perSlide = 0;
        }
    }

    static void doFxVibrato(Voice& v, uint8_t arg) {
        if (arg == 0) return;
        const uint8_t spd = (uint8_t)((arg & 0xF0) >> 4);
        if (spd != 0) v.vibratoSpd = spd;
        const uint8_t ampl = (uint8_t)(arg & 0x0F);
        if (ampl != 0) v.vibratoAmpl = (uint8_t)((v.vibratoAmpl & 0xF0) | ampl);
    }

    static void doFxVibOldAmpl(Voice& v) {
        if (v.vibDone) return;
        v.vibDone = true;
        const uint8_t kind = (uint8_t)(v.vibratoAmpl & 0x60);
        const uint8_t pos = (uint8_t)(v.vibratoPos & 0x1F);
        const uint8_t vibVal = kind == 0 ? kVibratoSine[pos] : kind == 32 ? kVibratoRampDown[pos] : 255;
        const uint16_t add = (uint16_t)(((v.vibratoAmpl & 0x0F) * vibVal) >> 7);
        if ((v.vibratoAmpl & 0x80) != 0) v.period = (uint16_t)(v.period - add);
        else v.period = (uint16_t)(v.period + add);
        v.vibratoPos = (uint8_t)(v.vibratoPos + v.vibratoSpd);
        if (v.vibratoPos >= 32) {
            v.vibratoPos &= 0x1F;
            v.vibratoAmpl ^= 0x80;
        }
    }

    static void doFxVolumeSlide(Voice& v, uint8_t arg) {
        const uint8_t argLow = (uint8_t)(arg & 0x0F), argHi = (uint8_t)((arg & 0xF0) >> 4);
        int8_t vol = (int8_t)v.volume;
        if (argHi == 0) {
            vol = (int8_t)(vol - argLow);
            if (vol < 0) vol = 0;
        } else {
            vol = (int8_t)(vol + argHi);
            if (vol > 64) vol = 64;
        }
        v.volume = (uint8_t)vol;
    }

    void doFxFineVolUpDown(Voice& v, uint8_t arg) const {
        const uint8_t argHi = (uint8_t)((arg & 0xF0) >> 4);
        if (argHi == 0) doFxFineVolDown(v, (uint8_t)(arg & 0x0F));
        else doFxFineVolUp(v, argHi);
    }
    void doFxFineVolUp(Voice& v, uint8_t arg) const {
        if (frameCnt_ != 0) return;
        v.volume = (uint8_t)(v.volume + arg);
        if (v.volume > 64) v.volume = 64;
    }
    void doFxFineVolDown(Voice& v, uint8_t arg) const {
        if (frameCnt_ != 0) return;
        const int8_t vol = (int8_t)(v.volume - arg);
        v.volume = (uint8_t)(vol < 0 ? 0 : vol);
    }

    void doFxECommands(Voice& v, uint8_t arg) {
        const uint8_t argLow = (uint8_t)(arg & 0x0F);
        switch (arg & 0xF0) {
        case kSetFilter: break;  // no Amiga LED filter here
        case kFineSlideUp:
            if (frameCnt_ == 0) v.perSlide = (int16_t)(v.perSlide - argLow);
            break;
        case kFineSlideDown:
            if (frameCnt_ == 0) v.perSlide = (int16_t)(v.perSlide + argLow);
            break;
        case kSetVibratoWaveform: {
            const uint8_t w = (uint8_t)(argLow & 0x03);
            v.vibratoAmpl = (uint8_t)((v.vibratoAmpl & 0x9F) | (uint8_t)((uint8_t)(w >> 3) | (uint8_t)(w << 5)));
            break;
        }
        case kSetLoop: setLoopPoint(); break;
        case kPatternLoop:
            if (argLow == 0) setLoopPoint();
            break;
        case kRetrigNote:
        case kNoteDelay:
            if (v.stepFxCnt == 0) {
                v.chFlag = 3;
                v.fxCom = 0xEF;  // (as NostalgicPlayer does: no effect any more)
            } else {
                --v.stepFxCnt;
            }
            break;
        case kFineVolumeSlideUp: doFxFineVolUp(v, argLow); break;
        case kFineVolumeSlideDown: doFxFineVolDown(v, argLow); break;
        case kNoteCut:
            if (v.stepFxCnt == 0) v.volume = 0;
            else --v.stepFxCnt;
            break;
        default: break;
        }
    }

    void setLoopPoint() {
        const uint8_t point = (uint8_t)(patCnt_ - 1);
        if (point != loopPoint_) {
            loopPoint_ = point;
            loopCnt_ = 0;
        }
    }

    void resetLoop(Voice& v) {
        switch (v.synthWaveRepCtrl) {
        case 0:  // normal
            v.synthWaveAct = v.synthWaveRep;
            v.synthWaveActOffset = v.synthWaveRepOffset;
            v.synthWaveEndOffset = v.synthWaveRepEndOffset;
            break;
        case 1: {  // backwards
            v.synthWaveAct = v.synthWaveRep;
            v.synthWaveActOffset = v.synthWaveEndOffset;
            int32_t offset = v.synthWaveAddBytes;
            if (offset >= 0) offset = -offset;
            if (v.synthWaveStop != 0) return;
            v.synthWaveActOffset = (uint32_t)(v.synthWaveActOffset + offset);
            v.synthWaveAddBytes = offset;
            break;
        }
        default:  // ping-pong
            v.synthWaveAct = v.synthWaveRep;
            v.synthWaveEndOffset = v.synthWaveRepEndOffset;
            v.synthWaveActOffset = (uint32_t)(v.synthWaveActOffset - v.synthWaveAddBytes);
            v.synthWaveAddBytes = -v.synthWaveAddBytes;
            break;
        }
    }

    void doSynth(Voice& v) {
        v.vibDone = false;
        if (v.chFlag == 0) {
            if (v.chMode != 0 && v.waveForm >= 0 && v.synthWaveStop == 0) {
                ++v.synthWaveCnt;
                if (v.synthWaveCnt >= v.synthWaveSpd) {
                    v.synthWaveCnt = 0;
                    const int32_t add = v.synthWaveAddBytes;
                    v.synthWaveActOffset = (uint32_t)(v.synthWaveActOffset + add);
                    if (add < 0) {
                        if ((int64_t)(int32_t)v.synthWaveActOffset < (int64_t)v.synthWaveRepOffset) resetLoop(v);
                    } else if ((int64_t)(int32_t)v.synthWaveActOffset >= (int64_t)v.synthWaveEndOffset) {
                        resetLoop(v);
                    }
                    v.chFlag = 1;  // new repeat offset
                    v.repeatStart = v.synthWaveAct;
                    v.repeatOffset = v.synthWaveActOffset;
                }
            }
            if (v.waveForm >= 0) {
                int vol = v.synthVol;
                if (v.synthEnv == kAdd) {
                    vol += v.synthAdd;
                    if (vol > 127) {
                        vol = 127;
                        v.synthEnv = kSub;
                    }
                } else if (v.synthEnv == kSub) {
                    vol -= v.synthSub;
                    if (vol <= v.synthEnd) {
                        vol = v.synthEnd;
                        v.synthEnv = kDone;
                    }
                }
                v.synthVol = (uint8_t)vol;
                if (v.vibOn != 0x21) {  // the instrument's vibrato starts after its delay
                    if (v.vibratoTrigDelay == -1) v.vibOn = 1;
                    else v.vibratoTrigDelay = (int16_t)(v.vibratoTrigDelay - 1);
                }
            }
        }
        if (v.vibOn == 1) doFxVibOldAmpl(v);
    }

    // The loop of a new wave (NostalgicPlayer's SetLoop with an array): the same wave loops in place, another
    // one is queued (AmigaChannel plays it when the current buffer ends).
    void loopWave(AmigaChannel& ch, int playing, int wave, uint32_t offset, uint32_t length) {
        const std::vector<int8_t>& w = m_.waves[(size_t)wave];
        if (!clipRange(w.size(), offset, length)) return;
        if (wave == playing) ch.setLoop(offset, length);
        else ch.setSample(w.data(), offset, length);
    }

    void setupChannel(Voice& v, AmigaChannel& ch) {
        if (v.fxCom == kExtraEffects && (v.fxDat & 0xF0) == kNoteDelay) return;
        const bool hasRepeat = v.repeatStart >= 0 && v.repeatLength > 1;
        const uint32_t repeatLen = v.repeatLength * 2u;
        if ((v.chFlag & 0x02) != 0 && v.waveForm >= 0) {
            const std::vector<int8_t>& w = m_.waves[(size_t)v.waveForm];
            uint32_t start = v.waveFormOffset, length = v.waveLen * 2u;
            if (!clipRange(w.size(), start, length)) {
                ch.mute();
            } else if (noiseAvoid_ && !(v.oldWaveLen > 255 || v.oldWaveLen == 0 || v.waveLen > 255)) {
                // queued behind the playing wave; only one looping range can wait (see the top of the file)
                if (hasRepeat) loopWave(ch, -2, v.repeatStart, v.repeatOffset, repeatLen);
                else ch.setSample(w.data(), start, length);
            } else {
                ch.playSample(w.data(), start, length);
                if (hasRepeat) loopWave(ch, v.waveForm, v.repeatStart, v.repeatOffset, repeatLen);
            }
        } else if (hasRepeat) {
            uint32_t start = v.repeatOffset, length = repeatLen;
            const std::vector<int8_t>& w = m_.waves[(size_t)v.repeatStart];
            if (clipRange(w.size(), start, length)) ch.setSample(w.data(), start, length);
        }
        uint32_t period = (uint32_t)(v.period + v.perSlide);
        if (period < 103) period = 103;
        ch.setAmigaPeriod(period);
        ch.setAmigaVolume((uint32_t)(v.volume * v.synthVol / 128 * v.trackVolume / 64));
        v.chFlag = 0;
    }
};
}  // namespace

bool isArtOfNoise(const uint8_t* d, size_t n) {
    Module m;
    return parse(d, n, m);
}

std::unique_ptr<AmigaSynthPlayer> openArtOfNoise(const uint8_t* d, size_t n) {
    std::unique_ptr<ArtOfNoise> p(new ArtOfNoise);
    if (!p->load(d, n)) return nullptr;
    return p;
}
