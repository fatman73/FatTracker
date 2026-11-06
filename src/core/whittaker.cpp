// David Whittaker replayer, ported from NostalgicPlayer's DavidWhittakerWorker.cs (Thomas Neumann, MIT licence)
// onto AmigaSynthPlayer. David Whittaker songs are saved together with their 68000 replay routine, so the
// loader finds the data tables and the player's features by looking for the routine's instructions (as
// NostalgicPlayer does). Files with several sub-songs start on the longest one (the first sub-song of some
// files is a short jingle that stops the song).
// Added for hostile files: the instruction search treats a read outside its 16 KB window as a failure (the
// C# code throws there), every count, offset and index is checked (the C# code relied on exceptions), sample
// frequency 0 is refused, position lists / tracks / arpeggios / envelopes have size limits, a channel whose
// tracks hold no note or wait (or an empty position list, where the C# code hangs) gives up for that tick,
// one replay call reads at most 64 KB of track bytes per channel, command thresholds at or below $80 (the
// end of track byte) are refused, notes outside the period table are clamped, and the square waveform's
// sample is restored on restart.
// Not ported: NostalgicPlayer's module information lines and sample list (display only), and the sound
// effect command of Emlyn Hughes International Soccer (NostalgicPlayer skips it too).
#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

#include "core/amiga_formats.h"
#include "core/amiga_synth.h"

namespace {
const uint16_t kPeriods1[] = {256, 242, 228, 215, 203, 192, 181, 171, 161, 152, 144, 136};
const uint16_t kPeriods2[] = {
    4096, 3864, 3648, 3444, 3252, 3068, 2896, 2732, 2580, 2436, 2300, 2168, 2048, 1932, 1824,
    1722, 1626, 1534, 1448, 1366, 1290, 1218, 1150, 1084, 1024, 966,  912,  861,  813,  767,
    724,  683,  645,  609,  575,  542,  512,  483,  456,  430,  406,  383,  362,  341,  322,
    304,  287,  271,
    256,  241,  228,  // not in the original player: arpeggio or transpose sometimes go out of range
};
const uint16_t kPeriods3[] = {
    8192, 7728, 7296, 6888, 6504, 6136, 5792, 5464, 5160, 4872, 4600, 4336, 4096, 3864, 3648,
    3444, 3252, 3068, 2896, 2732, 2580, 2436, 2300, 2168, 2048, 1932, 1824, 1722, 1626, 1534,
    1448, 1366, 1290, 1218, 1150, 1084, 1024, 966,  912,  861,  813,  767,  724,  683,  645,
    609,  575,  542,  512,  483,  456,  430,  406,  383,  362,  341,  322,  304,  287,  271,
    256,  241,  228,  215,  203,  191,  181,  170,  161,  152,  143,  135,
};

constexpr int kSearch = 16384;              // the part of the file the replay routine is looked for in
constexpr size_t kMaxSongs = 256;           // sub-songs read at most
constexpr size_t kMaxPositions = 4096;      // entries in one position list (a list that never ends is refused)
constexpr size_t kMaxList = 0x10000;        // bytes in one track / arpeggio / envelope
constexpr int kMaxBytesPerRow = 0x10000;     // track bytes one channel reads in one replay call
constexpr size_t kMaxTrackBytes = 16u << 20;  // all tracks together

enum Effect : uint8_t {
    kEndOfTrack, kSlide, kMute, kWaitUntilNextRow, kStopSong, kGlobalTranspose, kStartVibrato, kStopVibrato,
    kEffect8, kEffect9, kSetSpeed, kGlobalVolumeFade, kSetGlobalVolume, kStartOrStopSoundFx, kStopSoundFx
};

// What the replay routine's instructions tell (ExtractInfoFromInitFunction / ExtractInfoFromPlayFunction).
struct Probe {
    bool oldPlayer = false, uses32BitPointers = false;
    int64_t startOffset = 0, sampleInfoOffset = 0, sampleDataOffset = 0, subSongListOffset = 0,
            arpeggioListOffset = 0, envelopeListOffset = 0, channelVolumeOffset = 0;
    int numberOfSamples = 0, numberOfChannels = 0;
    bool enableSampleTranspose = false, enableChannelTranspose = false, enableGlobalTranspose = false;
    uint8_t newSampleCmd = 0, newEnvelopeCmd = 0, newArpeggioCmd = 0;
    bool enableArpeggio = false, enableEnvelopes = false, enableVibrato = false;
    bool enableVolumeFade = false, enableHalfVolume = false, enableGlobalVolumeFade = false,
         enableSetGlobalVolume = false;
    bool enableSquareWaveform = false;
    int64_t squareWaveformSampleNumber = 0;
    uint32_t squareWaveformSampleLength = 0;
    uint16_t squareChangeMinPosition = 0, squareChangeMaxPosition = 0;
    uint8_t squareChangeSpeed = 0;
    int8_t squareByte1 = 0, squareByte2 = 0;
    bool useExtraCounter = false, enableDelayCounter = false, enableDelayMultiply = false, enableDelaySpeed = false;
    const uint16_t* periods = nullptr;
    int periodCount = 0;
};

// The search window. A read outside it marks the search as failed (an exception in the C# code).
struct Search {
    uint8_t d[kSearch] = {};
    mutable bool bad = false;
    int operator[](int64_t i) const {
        if (i < 0 || i >= kSearch) {
            bad = true;
            return 0;
        }
        return d[i];
    }
    int64_t s16(int64_t i) const {  // ((sbyte)b[i] << 8) | b[i + 1]
        const int hi = (*this)[i];
        return (int64_t)(int8_t)hi * 256 + (*this)[i + 1];
    }
    int64_t u16(int64_t i) const {
        const int hi = (*this)[i];
        return (int64_t)hi * 256 + (*this)[i + 1];
    }
};

bool extractInit(const Search& b, Probe& p) {
    constexpr int64_t L = kSearch;
    int64_t index;
    for (index = 0; index < L; index += 2)  // lea base(pc),a3
        if (b[index] == 0x47 && b[index + 1] == 0xfa && (b[index + 2] & 0xf0) == 0xf0) break;
    if (index >= L - 6) return false;
    p.startOffset = b.s16(index + 2) + index + 2;
    for (; index < L; index += 2)  // bsr
        if (b[index] == 0x61 && b[index + 1] == 0x00) break;
    if (index >= L - 8) return false;
    const int64_t startOfInit = index;
    int64_t startOfSampleInit = index;
    if (b[index + 4] == 0x61 && b[index + 5] == 0x00) startOfSampleInit = b.s16(index + 6) + index + 6;

    // samples
    for (index = startOfSampleInit; index < L; index += 2)
        if (b[index] == 0x4a && b[index + 1] == 0x2b) break;
    if (index >= L - 36 || b.bad) return false;
    if (b[index + 4] != 0x66) {
        // the sample initializing may not be in a sub-function, like in QBall (the old player)
        for (index = startOfInit; index < L; index += 2)
            if (b[index] == 0x41 && b[index + 1] == 0xeb) break;
        if (index >= L - 36) return false;
        p.sampleDataOffset = b.s16(index + 2) + p.startOffset;
        index += 4;
        if (b[index + 4] != 0x72) return false;
        p.numberOfSamples = (b[index + 5] & 0x00ff) + 1;
        for (; index < L - 4; index += 2)
            if (b[index] == 0x41 && b[index + 1] == 0xeb && b[index + 4] == 0xe3 && b[index + 5] == 0x4f) break;
        if (index >= L - 4) return false;
        p.channelVolumeOffset = b.s16(index + 2) + p.startOffset;
        // sub-songs
        for (index = startOfInit; index < L; index += 2)
            if (b[index] == 0x41 && b[index + 1] == 0xeb && b[index + 4] == 0x17) break;
        if (index >= L - 4) return false;
        p.subSongListOffset = b.s16(index + 2) + p.startOffset;
        p.uses32BitPointers = true;
        p.oldPlayer = true;
    } else {
        p.oldPlayer = false;
        if (b[index + 5] == 0x00) index += 2;
        if (b[index + 6] != 0x41 || b[index + 7] != 0xfa) return false;
        p.sampleDataOffset = b.s16(index + 8) + index + 8;
        index += 10;
        if (b[index] == 0x27 && b[index + 1] == 0x48 && b[index + 4] == 0xd0 && b[index + 5] == 0xfc) {
            p.sampleDataOffset += b.u16(index + 6);
            index += 12;
            if (b[index] != 0xd0 || b[index + 1] != 0xfc) return false;
            p.sampleDataOffset += b.u16(index + 2);
            index += 4;
        }
        if (b[index] != 0x4b || b[index + 1] != 0xfa || b[index + 4] != 0x72) return false;
        p.sampleInfoOffset = b.s16(index + 2) + index + 2;
        p.numberOfSamples = (b[index + 5] & 0x00ff) + 1;
        index += 8;
        for (; index < L - 4; index += 2) {
            if (b[index] == 0x37 && b[index + 1] == 0x7c) {
                p.squareWaveformSampleLength = (uint32_t)(b.u16(index + 2) * 2);
                break;
            }
        }
        // sub-songs
        for (index = startOfInit; index < L; index += 2)
            if (b[index] == 0x41 && b[index + 1] == 0xfa && b[index + 4] != 0x4b) break;
        if (index >= L - 4) return false;
        if ((b[index + 4] != 0x12 || b[index + 5] != 0x30) && (b[index + 4] != 0x37 || b[index + 5] != 0x70))
            return false;
        p.subSongListOffset = b.s16(index + 2) + index + 2;
        index += 4;
        // pointers or offsets
        for (; index < L; index += 2)
            if (b[index] == 0x41 && b[index + 1] == 0xfa && b[index + 4] != 0x23) break;
        if (index >= L - 8) return false;
        if (b[index + 4] == 0x20 && b[index + 5] == 0x70) p.uses32BitPointers = true;
        else if (b[index + 4] == 0x30 && b[index + 5] == 0x70) p.uses32BitPointers = false;
        else return false;
    }
    return !b.bad;
}

bool extractPlay(const Search& b, Probe& p) {
    constexpr int64_t L = kSearch;
    int64_t index, offset;
    for (index = 0; index < L; index += 2) {  // lea base(pc),a3 / tst.b x(a3) / beq
        if (b[index] == 0x47 && b[index + 1] == 0xfa) {
            if (index >= L - 10) return false;
            if (b[index + 4] == 0x4a && b[index + 5] == 0x2b && b[index + 8] == 0x67) {
                if (b[index + 10] == 0x33 && b[index + 11] == 0xfc) continue;
                if (b[index + 10] == 0x17 && b[index + 11] == 0x7c) continue;
                if (b[index + 10] == 0x08 && b[index + 11] == 0xb9) continue;
                break;
            }
        }
    }
    const int64_t startOfPlay = index;
    if (startOfPlay >= L) return false;  // the reads below would leave the window

    // delay counter
    p.enableDelayCounter = p.enableDelayMultiply = false;
    for (index = startOfPlay; index < startOfPlay + 100; index += 2) {
        if (b[index] == 0x10 && b[index + 1] == 0x3a) {
            p.enableDelayCounter = true;
            if (b[index + 6] == 0xc0 && b[index + 7] == 0xfc) p.enableDelayMultiply = true;
            break;
        }
    }
    // extra counter
    p.useExtraCounter = false;
    for (index = startOfPlay; index < startOfPlay + 100; index += 2) {
        if (b[index] == 0x53 && b[index + 1] == 0x2b && b[index + 4] == 0x66) {
            if (b[index + 6] == 0x17) {
                offset = b.u16(index - 4);
                p.useExtraCounter = b[offset + p.startOffset] != 0;
            }
            break;
        }
    }
    // square waveform
    p.enableSquareWaveform = false;
    for (index = startOfPlay; index < startOfPlay + 100; index += 2) {
        if (b[index] == 0x20 && b[index + 1] == 0x7a && b[index + 4] == 0x30 && b[index + 5] == 0x3a) {
            p.enableSquareWaveform = true;
            offset = b.s16(index + 2) + index + 2;
            p.squareWaveformSampleNumber = (offset - p.sampleInfoOffset) / 12;
            if ((b[index + 14] != 0x31 && b[index + 14] != 0x11) || b[index + 15] != 0xbc) return false;
            p.squareByte1 = (int8_t)b[index + 17];
            if ((b[index + 20] & 0xf0) != 0x50 || b[index + 21] != 0x6b) return false;
            p.squareChangeSpeed = (uint8_t)((b[index + 20] & 0x0e) >> 1);
            if (b[index + 24] != 0x0c || b[index + 25] != 0x6b) return false;
            p.squareChangeMaxPosition = (uint16_t)b.u16(index + 26);
            if ((b[index + 38] != 0x31 && b[index + 38] != 0x11) || b[index + 39] != 0xbc) return false;
            p.squareByte2 = (int8_t)b[index + 41];
            if (b[index + 48] != 0x0c || b[index + 49] != 0x6b) return false;
            p.squareChangeMinPosition = (uint16_t)b.u16(index + 50);
            break;
        }
    }
    // number of channels (moveq #n,d7 or cmp #n,d7)
    p.numberOfChannels = 0;
    for (index = startOfPlay; index < startOfPlay + 200; index += 2) {
        if (b[index] == 0x7e) {
            p.numberOfChannels = b[index + 1];
            if (p.numberOfChannels == 0) {
                for (; index < startOfPlay + 500; index += 2) {
                    if (b[index] == 0xbe && (b[index + 1] == 0x7c || b[index + 1] == 0x3c)) {
                        p.numberOfChannels = b[index + 3];
                        break;
                    }
                }
            } else {
                ++p.numberOfChannels;
            }
            break;
        }
    }
    if (p.numberOfChannels == 0 || b.bad) return false;

    // the parts of the player
    int64_t readTrackCommandsOffset, doFrameStuffOffset;
    if (p.oldPlayer) {
        for (index = startOfPlay; index < L; index += 2)
            if (b[index] == 0x70 && b[index + 1] == 0x00) break;
        readTrackCommandsOffset = index;
        if (b[index + 2] != 0x10) return false;
        doFrameStuffOffset = -1;
    } else {
        for (index = startOfPlay; index < L; index += 2)
            if (b[index] == 0x53 && b[index + 1] == 0x68) break;
        if (index >= L - 16) return false;
        if (b[index + 4] != 0x67) return false;
        readTrackCommandsOffset = b[index + 5] + index + 6;
        if (b[index + 12] != 0x66) return false;
        if (b[index + 13] == 0x00) doFrameStuffOffset = b.u16(index + 14) + index + 14;
        else doFrameStuffOffset = b[index + 13] + index + 14;
    }
    for (index = readTrackCommandsOffset; index < L; index += 2)
        if (b[index] == 0x6b && b[index + 1] == 0x00) break;
    const int64_t doCommandsOffset = b.u16(index + 2) + index + 2;
    if (b.bad) return false;

    // the period table tells the version
    if (p.oldPlayer) {
        p.periods = kPeriods1, p.periodCount = (int)(sizeof kPeriods1 / 2);
    } else {
        for (index = readTrackCommandsOffset; index < L; index += 2)
            if (b[index] == 0x45 && b[index + 1] == 0xfa && b[index + 4] == 0x32 && b[index + 5] == 0x2d) break;
        if (index >= L - 6) return false;
        offset = b.u16(index + 2) + index + 2;
        if (offset >= L - 72 * 2) return false;
        if (b[offset] == 0x10 && b[offset + 1] == 0x00) p.periods = kPeriods2, p.periodCount = (int)(sizeof kPeriods2 / 2);
        else if (b[offset] == 0x20 && b[offset + 1] == 0x00) p.periods = kPeriods3, p.periodCount = (int)(sizeof kPeriods3 / 2);
        else return false;
    }

    // transposes
    for (index = readTrackCommandsOffset; index < L; index += 2)
        if (b[index] == 0x6b && b[index + 1] == 0x00) break;
    if (index >= L - 6) return false;
    p.enableSampleTranspose = b[index + 4] == 0xd0 && b[index + 5] == 0x2d;
    p.enableGlobalTranspose = false;
    if (doFrameStuffOffset != -1 && b[doFrameStuffOffset] == 0x10 && b[doFrameStuffOffset + 1] == 0x28) {
        if (b[doFrameStuffOffset + 4] == 0xd0 && b[doFrameStuffOffset + 5] == 0x3a) p.enableGlobalTranspose = true;
        if (b[doFrameStuffOffset + 8] == 0xd0 && b[doFrameStuffOffset + 9] == 0x28) p.enableChannelTranspose = true;
    }

    // command ranges
    p.enableArpeggio = p.enableEnvelopes = false;
    p.newSampleCmd = 0;
    for (index = doCommandsOffset; index < L - 28; index += 2) {
        if (b[index] == 0x4e && (b[index + 1] == 0xd2 || b[index + 1] == 0xf3)) break;
        if ((b[index] == 0xb0 && b[index + 1] == 0x3c) || (b[index] == 0x0c && b[index + 1] == 0x00)) {
            if (b[index + 2] == 0x00 && (b[index + 4] == 0x65 || b[index + 4] == 0x6d)) {
                if (b[index + 10] == 0xd0 && b[index + 11] == 0x40 && b[index + 12] == 0x45 && b[index + 13] == 0xfa) {
                    if (b[index + 22] == 0x21 && b[index + 23] == 0x4a && b[index + 26] == 0x21 && b[index + 27] == 0x4a) {
                        p.enableArpeggio = true;
                        p.newArpeggioCmd = (uint8_t)b[index + 3];
                        p.arpeggioListOffset = b.s16(index + 14) + index + 14;
                    } else if (b[index + 22] == 0x21 && b[index + 23] == 0x4a && b[index + 26] == 0x11 && b[index + 27] == 0x6a) {
                        p.enableEnvelopes = true;
                        p.newEnvelopeCmd = (uint8_t)b[index + 3];
                        p.envelopeListOffset = b.s16(index + 14) + index + 14;
                    }
                } else if (b[index + 10] == 0x4b && b[index + 11] == 0xfa && b[index + 14] == 0xc0 && b[index + 15] == 0xfc) {
                    p.newSampleCmd = (uint8_t)b[index + 3];
                }
            }
        }
    }
    if (!p.oldPlayer && p.newSampleCmd == 0) return false;
    // $80 is "end of track": a threshold at or below it would make it a sample / envelope / arpeggio command,
    // and then no track could end
    if ((!p.oldPlayer && p.newSampleCmd <= 0x80) || (p.enableEnvelopes && p.newEnvelopeCmd <= 0x80) ||
        (p.enableArpeggio && p.newArpeggioCmd <= 0x80))
        return false;

    // effects, from the command jump table
    int64_t jumpTableOffset;
    if (b[index - 10] == 0x45 && b[index - 9] == 0xfa) jumpTableOffset = b.s16(index - 8) + index - 8;
    else if (b[index - 8] == 0x45 && b[index - 7] == 0xfa) jumpTableOffset = b.s16(index - 6) + index - 6;
    else if (b[index - 10] == 0x45 && b[index - 9] == 0xeb) jumpTableOffset = b.s16(index - 8) + p.startOffset;
    else return false;
    auto effect = [&](int n) { return b.u16(jumpTableOffset + n * 2) + p.startOffset; };

    int64_t e = effect(6);
    p.enableVibrato = e >= 0 && e < L - 6 && b[e] == 0x50 && b[e + 1] == 0xe8 && b[e + 4] == 0x11 && b[e + 5] == 0x59;
    e = effect(8);
    p.enableVolumeFade = e >= 0 && e < L - 2 && b[e] == 0x17 && b[e + 1] == 0x59;
    p.enableHalfVolume = false;
    e = effect(8);
    if (e >= 0 && e < L - 2 && b[e] == 0x50 && b[e + 1] == 0xe8) {
        e = effect(9);
        if (e < L - 2 && b[e] == 0x51 && b[e + 1] == 0xe8) p.enableHalfVolume = true;
    }
    e = effect(11);
    p.enableGlobalVolumeFade = e >= 0 && e < L - 2 && b[e] == 0x17 && b[e + 1] == 0x59;
    e = effect(10);
    p.enableDelaySpeed = e >= 0 && e < L - 4 && b[e] == 0x10 && b[e + 1] == 0x19 && b[e + 2] == 0x17 && b[e + 3] == 0x40;
    p.enableSetGlobalVolume = true;
    e = effect(12);
    if (e >= 0 && e < L - 4 && b[e + 2] == 0x42 && b[e + 3] == 0x41) p.enableSetGlobalVolume = false;
    return !b.bad;
}

bool probe(const uint8_t* d, size_t n, Probe& p) {
    if (!d || n < 2048) return false;
    // SC68 files are handled by a converter in NostalgicPlayer; C64 tunes are never Amiga code
    if (!std::memcmp(d, "SC68", 4) || !std::memcmp(d, "PSID", 4) || !std::memcmp(d, "RSID", 4)) return false;
    const std::unique_ptr<Search> b(new Search);
    std::memcpy(b->d, d, n < (size_t)kSearch ? n : (size_t)kSearch);
    return extractInit(*b, p) && extractPlay(*b, p) && p.numberOfChannels <= 4;
}

// NostalgicPlayer's ModuleStream: big endian reads that return 0 past the end, EndOfStream tells whether
// the last read came up short (or the last seek went past the end). A seek before the start throws in C#.
struct Stream {
    const uint8_t* d;
    int64_t n;
    int64_t pos = 0;
    bool eos = false, bad = false;
    void seek(int64_t at) {
        if (at < 0) {
            bad = true;
            at = 0;
        }
        pos = at;
        eos = at > n;
    }
    int64_t read(uint8_t* out, int64_t count) {
        const int64_t avail = pos >= n ? 0 : std::min(count, n - pos);
        if (avail > 0) std::memcpy(out, d + pos, (size_t)avail);
        pos += avail;
        eos = avail < count;
        return avail;
    }
    uint8_t u8() {
        uint8_t b = 0;
        return read(&b, 1) == 1 ? b : 0;
    }
    uint16_t u16() {
        uint8_t b[2];
        return read(b, 2) == 2 ? (uint16_t)(b[0] << 8 | b[1]) : 0;
    }
    uint32_t u32() {
        uint8_t b[4];
        return read(b, 4) == 4 ? (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3] : 0;
    }
};

struct Track {
    std::vector<uint8_t> bytes;
    int arpeggios = 0, envelopes = 0;  // highest arpeggio / envelope number used + 1
    int newPositionListOffset = 0;     // position list restart (effect 9)
};
struct PositionList {
    std::vector<int> tracks;  // track numbers
    uint16_t restartPosition = 0;
};
struct SongInfo {
    uint16_t speed = 0;
    uint8_t delayCounterSpeed = 0;
    PositionList lists[4];
};
struct Sample {
    std::vector<int8_t> data;
    uint32_t length = 0;
    int32_t loopStart = -1;
    uint16_t volume = 0, fineTunePeriod = 0;
    int8_t transpose = 0;
};
struct GlobalPlayingInfo {
    int8_t transpose = 0;
    uint16_t volumeFadeSpeed = 0, globalVolume = 64;
    uint8_t globalVolumeFadeSpeed = 0, globalVolumeFadeCounter = 0;
    uint16_t squareChangePosition = 0;
    bool squareChangeDirection = false;
    uint8_t extraCounter = 1, delayCounterSpeed = 0;
    uint16_t delayCounter = 0, speed = 0;
};
struct ChannelInfo {
    const PositionList* positionList = nullptr;
    uint16_t currentPosition = 1;
    int track = -1;  // -1: the empty track
    int trackDataPosition = 0;
    int sample = -1;  // current sample info
    uint8_t note = 0;
    int8_t transpose = 0;
    bool enableHalfVolume = false;
    uint16_t speed = 0, speedCounter = 1;
    int arpeggio = -1, arpeggioPosition = 0;
    int envelope = -1, envelopePosition = 0;
    uint8_t envelopeSpeed = 0;
    int8_t envelopeCounter = 0;
    bool slideEnabled = false;
    int8_t slideSpeed = 0;
    uint8_t slideCounter = 0;
    int16_t slideValue = 0;
    int8_t vibratoDirection = 0;
    uint8_t vibratoSpeed = 0, vibratoValue = 0, vibratoMaxValue = 0;
};

class Whittaker : public AmigaSynthPlayer {
public:
    bool load(const uint8_t* d, size_t n, const Probe& pr) {
        p_ = pr;
        Stream s{d, (int64_t)n};
        int arpeggios = 0, envelopes = 0;
        if (!loadSubSongInfoAndTracks(s, arpeggios, envelopes)) return false;
        if (!loadLists(s, arpeggios, p_.arpeggioListOffset, 0, arpeggios_)) return false;
        if (arpeggios == 0) arpeggios_.assign(1, std::vector<uint8_t>{0x80});  // at least one empty arpeggio
        if (!loadLists(s, envelopes, p_.envelopeListOffset, 1, envelopes_)) return false;
        if (!loadChannelVolumes(s) || !loadSampleInfo(s) || !loadSampleData(s)) return false;
        if (p_.enableSquareWaveform) {  // the C# code throws on InitSound otherwise
            if (p_.squareWaveformSampleNumber < 0 || p_.squareWaveformSampleNumber >= (int64_t)samples_.size())
                return false;
            Sample& sq = samples_[(size_t)p_.squareWaveformSampleNumber];
            sq.length = p_.squareWaveformSampleLength;
            if ((size_t)(sq.length / 2) * 2 > sq.data.size()) return false;
            squareData_ = sq.data;
        }
        format_ = "DAVID WHITTAKER (AMIGA)";
        subsongs_ = (int)songs_.size();
        selectLongestSubsong();  // measures it
        return true;
    }

protected:
    void initSound() override {
        const SongInfo& song = songs_[(size_t)subsong_];
        g_ = GlobalPlayingInfo{};
        g_.speed = song.speed;
        g_.delayCounterSpeed = song.delayCounterSpeed;
        if (p_.enableDelayMultiply) g_.delayCounterSpeed = (uint8_t)(g_.delayCounterSpeed * 16);
        channelsDone_ = 0;
        if (p_.enableSquareWaveform) samples_[(size_t)p_.squareWaveformSampleNumber].data = squareData_;
        initializeChannelInfo();
        initializeSquareWaveform();
    }

    void playTick() override {
        if (p_.enableDelayCounter) {
            g_.delayCounter = (uint16_t)(g_.delayCounter + g_.delayCounterSpeed);
            if (g_.delayCounter > 255) {
                g_.delayCounter = (uint16_t)(g_.delayCounter - 256);
                return;
            }
        }
        if (p_.useExtraCounter) {
            if (--g_.extraCounter == 0) {
                g_.extraCounter = 6;
                return;
            }
        }
        if (p_.enableGlobalVolumeFade && g_.globalVolumeFadeSpeed != 0 && g_.globalVolume > 0) {
            if (--g_.globalVolumeFadeCounter == 0) {
                --g_.globalVolume;
                if (g_.globalVolume > 0) g_.globalVolumeFadeCounter = g_.globalVolumeFadeSpeed;
            }
        }
        changeSquareWaveform();
        for (int i = 0; i < p_.numberOfChannels; ++i) {
            ChannelInfo& c = chan_[i];
            --c.speedCounter;
            if (c.speedCounter == 0) readTrackCommands(i);
            else if (c.speedCounter > 1) doFrameStuff(i);
        }
    }

private:
    Probe p_;
    std::vector<SongInfo> songs_;
    std::vector<Track> tracks_;
    std::map<uint32_t, int> trackByOffset_;
    size_t trackBytes_ = 0;
    std::vector<std::vector<uint8_t>> arpeggios_, envelopes_;  // envelopes: speed first
    std::vector<uint16_t> channelVolumes_;
    std::vector<Sample> samples_;
    std::vector<int8_t> squareData_;  // the square waveform's sample as loaded
    GlobalPlayingInfo g_;
    ChannelInfo chan_[4];
    unsigned channelsDone_ = 0;
    int trackSwitches_ = 0;  // track changes in this ReadTrackCommands call

    // ---- loading
    bool loadSubSongInfoAndTracks(Stream& s, int& arpeggios, int& envelopes) {
        s.seek(p_.subSongListOffset);
        if (s.bad) return false;
        int64_t minPositionOffset = INT64_MAX;
        for (;;) {
            if (s.pos + 8 >= minPositionOffset || songs_.size() >= kMaxSongs) break;
            SongInfo song;
            if (p_.enableDelayCounter) {
                song.speed = s.u8();
                song.delayCounterSpeed = s.u8();
            } else {
                song.speed = s.u16();
            }
            if (song.speed > 255) break;
            uint32_t positionOffsets[4] = {};
            for (int i = 0; i < p_.numberOfChannels; ++i) {
                positionOffsets[i] = p_.uses32BitPointers ? s.u32() : s.u16();
                minPositionOffset = std::min(minPositionOffset, (int64_t)positionOffsets[i] + p_.startOffset);
            }
            if (s.eos) return false;
            const int64_t currentPosition = s.pos;
            for (int i = 0; i < p_.numberOfChannels; ++i)
                if (!loadPositionList(s, positionOffsets[i], song.lists[i], arpeggios, envelopes)) return false;
            songs_.push_back(static_cast<SongInfo&&>(song));
            s.seek(currentPosition);
        }
        return !songs_.empty();
    }

    bool loadPositionList(Stream& s, uint32_t startPosition, PositionList& list, int& arpeggios, int& envelopes) {
        if (startPosition == 0) return false;
        s.seek((int64_t)startPosition + p_.startOffset);
        if (s.bad) return false;
        int64_t minPosition = startPosition, maxPosition = startPosition;
        for (;;) {
            const uint32_t trackOffset = p_.uses32BitPointers ? s.u32() : s.u16();
            if (trackOffset == 0 || trackOffset >= (uint64_t)s.n || (trackOffset & 0x8000) != 0) break;
            if (s.eos) return false;
            if (list.tracks.size() >= kMaxPositions) return false;
            int64_t currentPosition = s.pos;
            const int t = loadTrack(s, trackOffset);
            if (t < 0) return false;
            list.tracks.push_back(t);
            const Track& tr = tracks_[(size_t)t];
            arpeggios = std::max(arpeggios, tr.arpeggios);
            envelopes = std::max(envelopes, tr.envelopes);
            const int64_t np = tr.newPositionListOffset;
            if (np != 0 && (np < minPosition || np > maxPosition)) {
                list.restartPosition = (uint16_t)list.tracks.size();
                currentPosition = np + p_.startOffset;
            }
            minPosition = std::min(minPosition, currentPosition);
            maxPosition = std::max(maxPosition, currentPosition);
            s.seek(currentPosition);
            if (s.bad) return false;
        }
        return true;
    }

    // Bytes after an effect command; -1 ends the track, -2 is an invalid command (the C# code throws).
    int effectByteCount(int effect) const {
        if (p_.oldPlayer) {  // QBall: the effects are in another order
            switch (effect) {
                case 0: case 1: return -1;  // end of track, stop song
                case 2: return 1;
                default: return -2;
            }
        }
        switch (effect) {
            case kEndOfTrack: case kStopSong: return -1;
            case kMute: case kWaitUntilNextRow: case kStopVibrato: case kStopSoundFx: return 0;
            case kGlobalTranspose: case kEffect8: case kSetSpeed: case kGlobalVolumeFade: case kSetGlobalVolume: return 1;
            case kSlide: case kStartVibrato: return 2;
            case kEffect9: return p_.enableHalfVolume ? 0 : 2;
            case kStartOrStopSoundFx: return p_.enableSetGlobalVolume ? 1 : 0;
            default: return -2;
        }
    }

    // What a track byte >= 0x80 is: a wait / sample command, an envelope, an arpeggio (none of them has
    // bytes after it) or an effect.
    enum Kind { kCmd, kEnvelope, kArpeggio, kFx };
    Kind kind(uint8_t byt) const {
        if (byt >= 0xe0) return kCmd;
        if (!p_.oldPlayer && byt >= p_.newSampleCmd) return kCmd;
        if (p_.enableEnvelopes && byt >= p_.newEnvelopeCmd) return kEnvelope;
        if (p_.enableArpeggio && byt >= p_.newArpeggioCmd) return kArpeggio;
        return kFx;
    }

    // LoadTrack and the parse of LoadAndParseTrack; the track number, or -1.
    int loadTrack(Stream& s, uint32_t trackOffset) {
        const auto found = trackByOffset_.find(trackOffset);
        if (found != trackByOffset_.end()) return found->second;
        s.seek((int64_t)trackOffset + p_.startOffset);
        if (s.bad) return -1;
        Track t;
        for (;;) {
            const uint8_t byt = s.u8();
            if (s.eos || t.bytes.size() >= kMaxList) return -1;
            t.bytes.push_back(byt);
            if ((byt & 0x80) == 0 || kind(byt) != kFx) continue;
            int count = effectByteCount(byt & 0x7f);
            if (count == -1) break;
            if (count == -2) return -1;
            for (; count > 0; --count) t.bytes.push_back(s.u8());
        }
        trackBytes_ += t.bytes.size();
        if (trackBytes_ > kMaxTrackBytes) return -1;
        for (size_t i = 0; i < t.bytes.size();) {
            const uint8_t byt = t.bytes[i++];
            if ((byt & 0x80) == 0) continue;
            const Kind k = kind(byt);
            if (k == kEnvelope) t.envelopes = std::max(t.envelopes, byt - p_.newEnvelopeCmd + 1);
            if (k == kArpeggio) t.arpeggios = std::max(t.arpeggios, byt - p_.newArpeggioCmd + 1);
            if (k != kFx) continue;
            const int effect = byt & 0x7f;
            const int count = effectByteCount(effect);
            if (count < 0) break;
            i += (size_t)count;
            if (effect == kEffect9 && !p_.enableHalfVolume && !p_.oldPlayer && i <= t.bytes.size())
                t.newPositionListOffset = t.bytes[i - 2] << 8 | t.bytes[i - 1];
        }
        trackByOffset_[trackOffset] = (int)tracks_.size();
        tracks_.push_back(static_cast<Track&&>(t));
        return (int)tracks_.size() - 1;
    }

    // LoadArpeggios / LoadEnvelopes: a table of 16 bit offsets, each list ends with a byte with bit 7 set.
    // An envelope starts with its speed, the byte before the offset.
    bool loadLists(Stream& s, int count, int64_t tableOffset, int before, std::vector<std::vector<uint8_t>>& out) {
        out.clear();
        if (count == 0) return true;
        std::vector<uint16_t> offsets((size_t)count);
        s.seek(tableOffset);
        if (s.bad) return false;
        for (uint16_t& o : offsets) o = s.u16();
        for (uint16_t o : offsets) {
            s.seek(o + p_.startOffset - before);
            if (s.bad) return false;
            std::vector<uint8_t> list;
            if (before) list.push_back(s.u8());
            for (;;) {
                const uint8_t b = s.u8();
                if (s.eos || list.size() >= kMaxList) return false;
                list.push_back(b);
                if (b & 0x80) break;
            }
            out.push_back(static_cast<std::vector<uint8_t>&&>(list));
        }
        return true;
    }

    bool loadChannelVolumes(Stream& s) {
        if (!p_.oldPlayer) return true;
        channelVolumes_.assign((size_t)p_.numberOfChannels, 0);
        s.seek(p_.channelVolumeOffset);
        if (s.bad) return false;
        for (uint16_t& v : channelVolumes_) v = s.u16();
        return !s.eos;
    }

    bool loadSampleInfo(Stream& s) {
        samples_.assign((size_t)p_.numberOfSamples, Sample{});
        if (p_.oldPlayer) {
            for (Sample& sm : samples_) {
                sm.loopStart = -1;
                sm.volume = 64;
            }
            return true;
        }
        s.seek(p_.sampleInfoOffset);
        if (s.bad) return false;
        for (Sample& sm : samples_) {
            s.seek(s.pos + 4);  // pointer to the sample data
            sm.loopStart = (int32_t)s.u32();
            sm.length = s.u16() * 2u;
            if (s.eos) return false;
            if (sm.loopStart != -1 && sm.loopStart > 64 * 1024) sm.loopStart = -1;  // fix for Jaws
            if (p_.uses32BitPointers) {
                s.seek(s.pos + 2);
                sm.fineTunePeriod = s.u16();
                sm.volume = s.u16();
                sm.transpose = 0;
                if (s.eos) return false;
            } else {
                sm.fineTunePeriod = s.u16();
                if (!p_.enableEnvelopes) {
                    sm.volume = s.u16();
                    sm.transpose = (int8_t)s.u8();
                    if (s.eos) return false;
                    s.seek(s.pos + 1);
                } else {
                    sm.volume = 64;
                    sm.transpose = 0;
                }
            }
        }
        return true;
    }

    bool loadSampleData(Stream& s) {
        s.seek(p_.sampleDataOffset);
        if (s.bad) return false;
        for (Sample& sm : samples_) {
            sm.length = s.u32();
            const uint16_t frequency = s.u16();
            if (frequency == 0) return false;
            sm.fineTunePeriod = (uint16_t)(3579545 / frequency);
            if (s.pos > s.n || sm.length > (uint64_t)(s.n - s.pos)) return false;
            sm.data.assign((const int8_t*)s.d + s.pos, (const int8_t*)s.d + s.pos + sm.length);
            s.pos += sm.length;
        }
        return true;
    }

    // ---- playing
    void initializeChannelInfo() {
        const SongInfo& song = songs_[(size_t)subsong_];
        for (int i = 0; i < p_.numberOfChannels; ++i) {
            ChannelInfo& c = chan_[i];
            c = ChannelInfo{};
            c.positionList = &song.lists[i];
            if (p_.enableArpeggio) c.arpeggio = 0;
            c.track = c.positionList->tracks.empty() ? -1 : c.positionList->tracks[0];
        }
    }

    void initializeSquareWaveform() {
        if (!p_.enableSquareWaveform) return;
        Sample& sm = samples_[(size_t)p_.squareWaveformSampleNumber];
        const uint32_t half = sm.length / 2;
        for (uint32_t i = 0; i < half; ++i) {
            sm.data[i] = p_.squareByte1;
            sm.data[i + half] = p_.squareByte2;
        }
        g_.squareChangePosition = p_.squareChangeMinPosition;
        g_.squareChangeDirection = false;
    }

    void changeSquareWaveform() {
        if (!p_.enableSquareWaveform) return;
        std::vector<int8_t>& wave = samples_[(size_t)p_.squareWaveformSampleNumber].data;
        auto put = [&](uint32_t at, int8_t v) {
            if (at < wave.size()) wave[at] = v;
        };
        if (g_.squareChangeDirection) {
            for (int i = 0; i < p_.squareChangeSpeed; ++i) put(g_.squareChangePosition + (uint32_t)i, p_.squareByte2);
            g_.squareChangePosition = (uint16_t)(g_.squareChangePosition - p_.squareChangeSpeed);
            if (g_.squareChangePosition == p_.squareChangeMinPosition) g_.squareChangeDirection = false;
        } else {
            for (int i = 0; i < p_.squareChangeSpeed; ++i) put(g_.squareChangePosition + (uint32_t)i, p_.squareByte1);
            g_.squareChangePosition = (uint16_t)(g_.squareChangePosition + p_.squareChangeSpeed);
            if (g_.squareChangePosition == p_.squareChangeMaxPosition) g_.squareChangeDirection = true;
        }
    }

    // The next track byte, or -1 for no track (Tables.EmptyTrack) and past the end of the track (the C# code
    // throws): both end the track.
    int nextTrackByte(ChannelInfo& c) {
        const int at = c.trackDataPosition++;
        if (c.track < 0) return -1;
        const std::vector<uint8_t>& t = tracks_[(size_t)c.track].bytes;
        return at >= 0 && (size_t)at < t.size() ? t[(size_t)at] : -1;
    }
    // An effect's parameter byte ($80 past the end of the track, which then ends at the next read).
    uint8_t nextByte(ChannelInfo& c) {
        const int b = nextTrackByte(c);
        return b < 0 ? 0x80 : (uint8_t)b;
    }

    void playSample(AmigaChannel& ch, const Sample& sm) {
        uint32_t start = 0, length = sm.length;
        if (!clipRange(sm.data.size(), start, length)) {
            ch.mute();
            return;
        }
        ch.playSample(sm.data.data(), 0, length);
    }

    void readTrackCommands(int i) {
        ChannelInfo& c = chan_[i];
        AmigaChannel& ch = ch_[i];
        c.slideEnabled = false;
        trackSwitches_ = 0;
        int bytesRead = 0;
        for (;;) {
            // tracks without a note (or no tracks at all): give up for this tick, go on in the next row
            if (trackSwitches_ > (int)c.positionList->tracks.size() + 1 || ++bytesRead > kMaxBytesPerRow) break;
            const int b = nextTrackByte(c);
            if (b < 0) {  // no track, or past its end
                handleEndOfTrackEffect(i);
                continue;
            }
            uint8_t trackByte = (uint8_t)b;
            if (trackByte & 0x80) {
                const int r = doTrackCommand(i, trackByte);
                if (r == 2) return;  // the module stopped and restarted: the channel is new
                if (r == 1) break;
                continue;
            }
            // play the note
            c.note = trackByte;
            if (p_.oldPlayer) {
                const int sampleNumber = trackByte / 12, note = trackByte % 12;
                if (c.note != 0 && sampleNumber < (int)samples_.size()) {
                    ch.setAmigaPeriod(p_.periods[note]);
                    playSample(ch, samples_[(size_t)sampleNumber]);
                    ch.setAmigaVolume(channelVolumes_[(size_t)i]);
                } else {
                    ch.mute();
                }
                break;
            }
            if (c.sample < 0) break;  // no sample chosen yet (the C# code throws)
            const Sample& sm = samples_[(size_t)c.sample];
            if (p_.enableSampleTranspose) trackByte = (uint8_t)(trackByte + sm.transpose);
            if (p_.enableChannelTranspose) trackByte = (uint8_t)(trackByte + c.transpose);
            else c.note = trackByte;  // old players keep the transposed note
            playSample(ch, sm);
            if (sm.loopStart >= 0) {
                uint32_t start = (uint32_t)sm.loopStart, length = sm.length - start;
                if (start < sm.length && clipRange(sm.data.size(), start, length)) ch.setLoop(start, length);
            }
            int newVolume = sm.volume;
            if (p_.enableEnvelopes && c.envelope >= 0) {
                const std::vector<uint8_t>& env = envelopes_[(size_t)c.envelope];
                newVolume = env[1] & 0x7f;
                c.envelopePosition = env.size() > 2 ? 2 : 1;
                c.envelopeCounter = (int8_t)c.envelopeSpeed;
            }
            if (p_.enableHalfVolume && c.enableHalfVolume) newVolume /= 2;
            if (p_.enableVolumeFade) {
                newVolume -= g_.volumeFadeSpeed;
                if (newVolume < 0) newVolume = 0;
            }
            newVolume = newVolume * g_.globalVolume / 64;
            ch.setAmigaVolume((uint16_t)newVolume);
            if (trackByte >= 128) trackByte = 0;
            else if (trackByte >= p_.periodCount) trackByte = (uint8_t)(p_.periodCount - 1);
            ch.setAmigaPeriod((uint32_t)((p_.periods[trackByte] * sm.fineTunePeriod) >> 10));
            break;
        }
        c.speedCounter = c.speed;
    }

    // 0: go on reading, 1: done for this row, 2: the module was stopped
    int doTrackCommand(int i, uint8_t cmd) {
        ChannelInfo& c = chan_[i];
        switch (kind(cmd)) {
            case kCmd:
                if (cmd >= 0xe0) {  // rows to wait, including this one
                    c.speed = (uint16_t)((cmd - 0xdf) * g_.speed);
                } else if (cmd - p_.newSampleCmd < (int)samples_.size()) {
                    c.sample = cmd - p_.newSampleCmd;
                }
                return 0;
            case kEnvelope:
                if (cmd - p_.newEnvelopeCmd < (int)envelopes_.size()) {
                    c.envelope = cmd - p_.newEnvelopeCmd;
                    c.envelopePosition = 1;
                    c.envelopeSpeed = envelopes_[(size_t)c.envelope][0];
                }
                return 0;
            case kArpeggio:
                if (cmd - p_.newArpeggioCmd < (int)arpeggios_.size()) {
                    c.arpeggio = cmd - p_.newArpeggioCmd;
                    c.arpeggioPosition = 0;
                }
                return 0;
            case kFx:
                break;
        }
        return doEffects(i, cmd & 0x7f);
    }

    int doEffects(int i, int effect) {
        ChannelInfo& c = chan_[i];
        if (p_.oldPlayer) {
            switch (effect) {
                case 0: handleEndOfTrackEffect(i); break;
                case 1: stopModule(); return 2;
                default: break;
            }
            return 0;
        }
        switch (effect) {
            case kEndOfTrack:
                handleEndOfTrackEffect(i);
                break;
            case kSlide:
                c.slideValue = 0;
                c.slideSpeed = (int8_t)nextByte(c);
                c.slideCounter = nextByte(c);
                c.slideEnabled = true;
                break;
            case kMute:
                ch_[i].mute();
                return 1;
            case kWaitUntilNextRow:
                return 1;
            case kStopSong:
                stopModule();
                return 2;
            case kGlobalTranspose:
                if (p_.enableGlobalTranspose) g_.transpose = (int8_t)nextByte(c);
                break;
            case kStartVibrato:
                if (p_.enableVibrato) {
                    c.vibratoDirection = -1;
                    c.vibratoSpeed = nextByte(c);
                    c.vibratoMaxValue = nextByte(c);
                    c.vibratoValue = 0;
                }
                break;
            case kStopVibrato:
                if (p_.enableVibrato) c.vibratoDirection = 0;
                break;
            case kEffect8:
                if (p_.enableVolumeFade) g_.volumeFadeSpeed = nextByte(c);
                else if (p_.enableChannelTranspose) c.transpose = (int8_t)nextByte(c);
                else if (p_.enableHalfVolume) c.enableHalfVolume = true;
                break;
            case kEffect9:
                if (p_.enableHalfVolume) c.enableHalfVolume = false;
                else c.trackDataPosition += 2;  // the position restart is handled by the loader
                break;
            case kSetSpeed:
                if (p_.enableDelaySpeed) g_.delayCounterSpeed = nextByte(c);
                else g_.speed = nextByte(c);
                break;
            case kGlobalVolumeFade:
                g_.globalVolumeFadeSpeed = nextByte(c);
                g_.globalVolumeFadeCounter = g_.globalVolumeFadeSpeed;
                break;
            case kSetGlobalVolume:
                if (p_.enableSetGlobalVolume) g_.globalVolume = nextByte(c);
                else ++c.trackDataPosition;  // start sound effect (not supported)
                break;
            case kStartOrStopSoundFx:
                if (p_.enableSetGlobalVolume) ++c.trackDataPosition;
                break;
            default:
                break;
        }
        return 0;
    }

    void onEndReached(int channel) {
        channelsDone_ |= 1u << channel;
        if (channelsDone_ == (1u << p_.numberOfChannels) - 1) {
            endReached_ = true;
            channelsDone_ = 0;
        }
    }

    void handleEndOfTrackEffect(int i) {
        ChannelInfo& c = chan_[i];
        const std::vector<int>& list = c.positionList->tracks;
        ++trackSwitches_;
        if (c.currentPosition >= list.size()) {
            c.currentPosition = (uint16_t)(c.positionList->restartPosition + 1);
            onEndReached(i);
            if (endReached_) {
                g_.transpose = 0;
                g_.volumeFadeSpeed = 0;
                g_.globalVolumeFadeSpeed = 0;
                if (g_.globalVolume == 0) g_.globalVolume = 64;
            }
        } else {
            ++c.currentPosition;
        }
        if (c.currentPosition - 1u >= list.size()) c.currentPosition = 1;  // restart past the list
        c.track = list.empty() ? -1 : list[c.currentPosition - 1u];
        c.trackDataPosition = 0;
    }

    void doFrameStuff(int i) {
        ChannelInfo& c = chan_[i];
        AmigaChannel& ch = ch_[i];
        if (c.sample < 0) return;
        const Sample& sm = samples_[(size_t)c.sample];
        int8_t note = (int8_t)c.note;
        if (p_.enableGlobalTranspose) note = (int8_t)(note + g_.transpose);
        if (p_.enableChannelTranspose) note = (int8_t)(note + c.transpose);
        if (p_.enableArpeggio && c.arpeggio >= 0) {
            const std::vector<uint8_t>& arps = arpeggios_[(size_t)c.arpeggio];
            if ((size_t)c.arpeggioPosition >= arps.size()) c.arpeggioPosition = 0;
            uint8_t arp = arps[(size_t)c.arpeggioPosition++];
            if (arp & 0x80) {
                c.arpeggioPosition = 0;
                arp &= 0x7f;
            }
            note = (int8_t)(note + arp);
        }
        if (note < 0) note = 0;  // clamped with the arpeggio in the C# code, else it throws
        else if (note >= p_.periodCount) note = (int8_t)(p_.periodCount - 1);
        uint32_t period = (uint32_t)((p_.periods[note] * sm.fineTunePeriod) >> 10);
        if (c.slideEnabled) {
            if (c.slideCounter == 0) {
                c.slideValue = (int16_t)(c.slideValue + c.slideSpeed);
                period = (uint32_t)(period - c.slideValue);
            } else {
                --c.slideCounter;
            }
        }
        if (p_.enableVibrato && c.vibratoDirection != 0) {
            if (c.vibratoDirection < 0) {
                c.vibratoValue = (uint8_t)(c.vibratoValue + c.vibratoSpeed);
                if (c.vibratoValue == c.vibratoMaxValue) c.vibratoDirection = (int8_t)-c.vibratoDirection;
            } else {
                c.vibratoValue = (uint8_t)(c.vibratoValue - c.vibratoSpeed);
                if (c.vibratoValue == 0) c.vibratoDirection = (int8_t)-c.vibratoDirection;
            }
            if (c.vibratoValue == 0) c.vibratoDirection ^= 0x01;
            if (c.vibratoDirection & 0x01) period += c.vibratoValue;
            else period -= c.vibratoValue;
        }
        ch.setAmigaPeriod(period);
        if (p_.enableEnvelopes && c.envelope >= 0) {
            if (--c.envelopeCounter < 0) {
                c.envelopeCounter = (int8_t)c.envelopeSpeed;
                const std::vector<uint8_t>& env = envelopes_[(size_t)c.envelope];
                if ((size_t)c.envelopePosition >= env.size()) c.envelopePosition = (int)env.size() - 1;
                int newVolume = env[(size_t)c.envelopePosition];
                if ((newVolume & 0x80) == 0) ++c.envelopePosition;
                newVolume &= 0x7f;
                if (p_.enableHalfVolume && c.enableHalfVolume) newVolume /= 2;
                if (p_.enableVolumeFade) {
                    newVolume -= g_.volumeFadeSpeed;
                    if (newVolume < 0) newVolume = 0;
                }
                newVolume = newVolume * g_.globalVolume / 64;
                ch.setAmigaVolume((uint16_t)newVolume);
            }
        }
    }

    void stopModule() {
        g_.transpose = 0;
        g_.volumeFadeSpeed = 0;
        g_.globalVolume = 64;
        g_.globalVolumeFadeSpeed = 0;
        for (int i = 0; i < p_.numberOfChannels; ++i) ch_[i].mute();
        initializeChannelInfo();
        initializeSquareWaveform();
        endReached_ = true;
    }
};
}  // namespace

bool detectWhittaker(const uint8_t* d, size_t n) {
    Probe p;
    return probe(d, n, p);
}

std::unique_ptr<AmigaSynthPlayer> openWhittaker(const uint8_t* d, size_t n) {
    Probe p;
    if (!probe(d, n, p)) return nullptr;
    std::unique_ptr<Whittaker> w(new Whittaker);
    if (!w->load(d, n, p)) return nullptr;
    return w;
}
