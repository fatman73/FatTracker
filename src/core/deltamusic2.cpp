// Delta Music 2.0 (".FNL") replayer, ported from NostalgicPlayer's DeltaMusic20Worker.cs
// (Thomas Neumann, MIT licence) onto AmigaSynthPlayer. Index checks added where the C# code relied on
// exceptions (instrument, block, period and waveform indexes, sound table jumps, sample ranges).
// The waveforms are read from the end of the instrument area the header names (NostalgicPlayer reads on
// from the last instrument it loaded, the same place in well-formed files).
#include <cstring>
#include <vector>

#include "core/amiga_formats.h"
#include "core/amiga_synth.h"

namespace {
const uint16_t kPeriods[] = {
    0,    6848, 6464, 6096, 5760, 5424, 5120, 4832, 4560, 4304, 4064, 3840, 3616, 3424, 3232, 3048, 2880, 2712,
    2560, 2416, 2280, 2152, 2032, 1920, 1808, 1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016, 960,
    904,  856,  808,  762,  720,  678,  640,  604,  570,  538,  508,  480,  452,  428,  404,  381,  360,  339,
    320,  302,  285,  269,  254,  240,  226,  214,  202,  190,  180,  170,  160,  151,  143,  135,  127,  120,
    113,  113,  113,  113,  113,  113,  113,  113,  113,  113,  113,  113,  113};
constexpr int kPeriodCount = (int)(sizeof kPeriods / sizeof kPeriods[0]);
uint16_t period(int i) { return kPeriods[i < 0 ? 0 : (i >= kPeriodCount ? kPeriodCount - 1 : i)]; }

enum Effect : uint8_t {
    kNone, kSetSpeed, kSetFilter, kSetBendRateUp, kSetBendRateDown, kSetPortamento, kSetVolume, kSetGlobalVolume,
    kSetArp
};

struct Line {
    uint8_t note = 0, instrument = 0, effect = 0, arg = 0;
};
const Line kEmptyBlock[16] = {};

struct TrackEntry {
    uint8_t block;
    int8_t transpose;
};

struct Envelope {
    uint8_t speed, level, sustain;  // volume: speed / level / sustain; vibrato: speed / delay / sustain
};

struct Inst {
    bool present = false, isSample = false;
    uint16_t sampleLength = 0, repeatStart = 0, repeatLength = 0;
    Envelope volume[5] = {}, vibrato[5] = {};
    uint16_t pitchBend = 0;
    uint8_t sampleNumber = 0;
    uint8_t table[48] = {};
    std::vector<int8_t> data;
};

struct Chan {
    const Inst* instrument = nullptr;
    const std::vector<TrackEntry>* track = nullptr;
    const Line* block = kEmptyBlock;
    int16_t currentTrackPosition = -1;
    uint16_t nextTrackPosition = 0, blockPosition = 0;
    uint8_t soundTableDelay = 0, soundTablePosition = 0;
    uint16_t finalPeriod = 0, period = 0;
    uint8_t note = 0, maxVolume = 63;
    int16_t pitchBend = 0, actualVolume = 0;
    uint16_t volumePosition = 0;
    uint8_t volumeSustain = 0, portamento = 0;
    bool vibratoDirection = false;
    uint16_t vibratoPeriod = 0;
    uint8_t vibratoDelay = 0;
    int8_t transpose = 0;
    const int8_t* arpeggio = nullptr;
    uint16_t arpeggioPosition = 0;
    uint8_t vibratoPosition = 0, vibratoSustain = 0;
    uint16_t trackLoopPosition = 0, trackLength = 0;
    bool retriggerSound = false;
};

uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
uint32_t rd32(const uint8_t* p) { return (uint32_t)rd16(p) << 16 | rd16(p + 2); }

class DeltaMusic2 : public AmigaSynthPlayer {
public:
    bool load(const uint8_t* d, size_t n) {  // isDeltaMusic2 has checked the chunk lengths
        startSpeed_ = (int8_t)d[0xBBB];
        std::memcpy(arpeggios_, d + 0xBCA, sizeof arpeggios_);
        size_t p = 0xFCA;
        for (int c = 0; c < 4; ++c, p += 4) {
            loop_[c] = rd16(d + p);
            tracks_[c].resize(rd16(d + p + 2) / 2);
        }
        for (int c = 0; c < 4; ++c) {
            if (tracks_[c].size() * 2 > n - p) return false;
            for (TrackEntry& t : tracks_[c]) {
                t.block = d[p++];
                t.transpose = (int8_t)d[p++];
            }
        }
        if (n - p < 4) return false;
        const uint32_t blockLength = rd32(d + p);
        p += 4;
        if (blockLength / 64 == 0 || blockLength / 64 * 64 > n - p) return false;
        blocks_.resize(blockLength / 64);
        for (auto& b : blocks_)
            for (Line& l : b.lines) {
                l.note = d[p];
                l.instrument = d[p + 1];
                l.effect = d[p + 2];
                l.arg = d[p + 3];
                p += 4;
            }
        if (n - p < 256) return false;
        uint16_t offsets[128] = {};
        for (int i = 1; i < 128; ++i) offsets[i] = rd16(d + p + 2 * (i - 1));
        const uint16_t breakOffset = rd16(d + p + 254);
        p += 256;
        const size_t instStart = p;
        for (int i = 0; i < 128; ++i) {
            if (offsets[i] == breakOffset) break;
            const size_t at = instStart + offsets[i];
            if (at > n || n - at < 88) return false;
            const uint8_t* q = d + at;
            Inst& in = inst_[i];
            in.present = true;
            in.sampleLength = (uint16_t)(rd16(q) * 2);
            in.repeatStart = rd16(q + 2);
            in.repeatLength = (uint16_t)(rd16(q + 4) * 2);
            if (in.repeatStart + in.repeatLength >= in.sampleLength)
                in.repeatLength = in.repeatStart < in.sampleLength ? (uint16_t)(in.sampleLength - in.repeatStart) : 0;
            q += 6;
            for (Envelope& e : in.volume) e = {q[0], q[1], q[2]}, q += 3;
            for (Envelope& e : in.vibrato) e = {q[0], q[1], q[2]}, q += 3;
            in.pitchBend = rd16(q);
            in.isSample = q[2] == 0xFF;
            in.sampleNumber = q[3];
            std::memcpy(in.table, q + 4, 48);
        }
        p = instStart + breakOffset;
        if (p > n || n - p < 4) return false;
        const uint32_t waveLength = rd32(d + p);
        p += 4;
        if (waveLength / 256 == 0 || waveLength / 256 * 256 > n - p) return false;
        waveforms_.resize(waveLength / 256);
        for (auto& w : waveforms_) {
            std::memcpy(w.data, d + p, 256);
            p += 256;
        }
        if (n - p < 64 + 32) return false;
        p += 64;
        uint32_t sampleOffsets[8];
        for (uint32_t& o : sampleOffsets) o = rd32(d + p), p += 4;
        for (Inst& in : inst_) {
            if (!in.present || !in.isSample) continue;
            const uint32_t off = sampleOffsets[in.sampleNumber & 7];
            const size_t at = off < n - p ? p + off : n;
            const size_t got = n - at < in.sampleLength ? n - at : in.sampleLength;
            if ((int)got < in.sampleLength - 256) return false;  // a little may be missing at the very end
            in.data.assign((const int8_t*)d + at, (const int8_t*)d + at + got);
            in.data.resize(in.sampleLength, 0);
        }
        format_ = "DELTA MUSIC 2.0 (AMIGA)";
        measure();
        return true;
    }

protected:
    void initSound() override {
        lastNoise_ = 0;
        globalVolume_ = 63;
        speed_ = startSpeed_;
        tick_ = 1;
        for (int i = 0; i < 4; ++i) {
            chan_[i] = Chan{};
            chan_[i].track = &tracks_[i];
            chan_[i].trackLoopPosition = loop_[i];
            chan_[i].trackLength = (uint16_t)tracks_[i].size();
            chan_[i].arpeggio = arpeggios_[0];
        }
        std::memset(ended_, 0, sizeof ended_);
    }

    void playTick() override {
        generateNoiseWaveform();
        tick_ = (int8_t)(tick_ - 1);
        if (tick_ < 0) tick_ = speed_;
        for (int i = 0; i < 4; ++i) processChannel(i, chan_[i]);
    }

private:
    struct BlockData {
        Line lines[16];
    };
    struct Wave {
        int8_t data[256];
    };
    int8_t arpeggios_[64][16] = {};
    std::vector<TrackEntry> tracks_[4];
    uint16_t loop_[4] = {};
    std::vector<BlockData> blocks_;
    Inst inst_[128];
    std::vector<Wave> waveforms_;
    int8_t startSpeed_ = 6, speed_ = 6, tick_ = 1;
    uint32_t lastNoise_ = 0;
    uint8_t globalVolume_ = 63;
    Chan chan_[4];
    bool ended_[4] = {};

    void onEndReached(int ch) {  // the song ends when every channel has wrapped
        ended_[ch] = true;
        if (ended_[0] && ended_[1] && ended_[2] && ended_[3]) {
            endReached_ = true;
            std::memset(ended_, 0, sizeof ended_);
        }
    }

    static void setPeriod(AmigaChannel& c, uint32_t p) { c.setAmigaPeriod(p && p < 113 ? 113 : p); }

    void generateNoiseWaveform() {  // waveform 0 is noise: 16 longwords (stored little endian, as in the C#)
        uint32_t v = lastNoise_;
        int8_t* w = waveforms_[0].data;
        for (int i = 0; i < 16; ++i) {
            v = v << 7 | v >> 25;
            v += 0x6ECA756Du;
            v ^= 0x9E59A92Bu;
            for (int b = 0; b < 4; ++b) w[4 * i + b] = (int8_t)(v >> (8 * b));
        }
        lastNoise_ = v;
    }

    void processChannel(int n, Chan& c) {
        AmigaChannel& ch = ch_[n];
        if (c.trackLength == 0) {
            onEndReached(n);
            return;
        }
        const Inst* inst = c.instrument;
        if (tick_ == 0) {
            if (c.blockPosition == 0) {
                const TrackEntry& t = (*c.track)[c.nextTrackPosition];
                c.transpose = t.transpose;
                c.block = t.block < blocks_.size() ? blocks_[t.block].lines : kEmptyBlock;
                if (c.nextTrackPosition <= c.currentTrackPosition) onEndReached(n);
                c.currentTrackPosition = (int16_t)c.nextTrackPosition;
                if (++c.nextTrackPosition >= c.trackLength)
                    c.nextTrackPosition = c.trackLoopPosition >= c.trackLength ? 0 : c.trackLoopPosition;
            }
            const Line& line = c.block[c.blockPosition];
            if (line.note != 0) {
                c.note = line.note;
                c.period = period(c.note + c.transpose);
                inst = c.instrument = line.instrument < 128 && inst_[line.instrument].present ? &inst_[line.instrument] : nullptr;
                if (inst) {
                    if (inst->isSample) {
                        uint32_t start = 0, len = inst->sampleLength;
                        if (clipRange(inst->data.size(), start, len)) {
                            ch.playSample(inst->data.data(), start, len);
                            uint32_t ls = inst->repeatStart, ll = inst->repeatLength;
                            if (inst->repeatLength > 1 && clipRange(inst->data.size(), ls, ll) && ll > 2)
                                ch.setLoop(ls, ll);
                        } else {
                            ch.mute();
                        }
                    } else {
                        c.retriggerSound = true;
                    }
                    c.soundTableDelay = 0;
                    c.soundTablePosition = 0;
                    c.actualVolume = 0;
                    c.volumeSustain = 0;
                    c.volumePosition = 0;
                    c.arpeggioPosition = 0;
                    c.vibratoDirection = false;
                    c.vibratoPeriod = 0;
                    c.vibratoDelay = inst->vibrato[0].level;
                    c.vibratoPosition = 0;
                    c.vibratoSustain = inst->vibrato[0].sustain;
                } else {
                    ch.mute();
                }
            }
            parseEffect(c, line.effect, line.arg);
            c.blockPosition = (c.blockPosition + 1) & 0x0F;
        }
        if (!inst) return;
        soundTableHandler(ch, c, *inst);
        vibratoHandler(c, *inst);
        volumeHandler(c, *inst);
        portamentoHandler(c);
        arpeggioHandler(c);
        c.vibratoPeriod = (uint16_t)(c.vibratoPeriod - (uint16_t)(inst->pitchBend - c.pitchBend));
        setPeriod(ch, (uint16_t)(c.finalPeriod + c.vibratoPeriod));
        uint8_t vol = (uint8_t)((c.actualVolume >> 2) & 0x3F);
        if (vol > c.maxVolume) vol = c.maxVolume;
        if (vol > globalVolume_) vol = globalVolume_;
        ch.setAmigaVolume(vol);
    }

    void parseEffect(Chan& c, uint8_t effect, uint8_t arg) {
        switch (effect) {
        case kSetSpeed: speed_ = (int8_t)(arg & 0x0F); break;
        case kSetFilter: break;  // no Amiga LED filter here
        case kSetBendRateUp: c.pitchBend = (int16_t)-(arg & 0xFF); break;
        case kSetBendRateDown: c.pitchBend = (int16_t)(arg & 0xFF); break;
        case kSetPortamento: c.portamento = arg; break;
        case kSetVolume: c.maxVolume = arg & 0x3F; break;
        case kSetGlobalVolume: globalVolume_ = arg & 0x3F; break;
        case kSetArp: c.arpeggio = arpeggios_[arg & 0x3F]; break;
        default: break;
        }
    }

    void soundTableHandler(AmigaChannel& ch, Chan& c, const Inst& inst) {
        if (inst.isSample) return;
        if (c.soundTableDelay != 0) {
            c.soundTableDelay--;
            return;
        }
        c.soundTableDelay = inst.sampleNumber;
        uint8_t data = inst.table[c.soundTablePosition];
        if (data == 0xFF) {
            const uint8_t to = c.soundTablePosition + 1 < 48 ? inst.table[c.soundTablePosition + 1] : 0;
            c.soundTablePosition = to < 48 ? to : 0;
            data = inst.table[c.soundTablePosition];
            if (data == 0xFF) return;
        }
        uint32_t start = 0, len = inst.sampleLength;
        const bool wave = data < waveforms_.size() && clipRange(sizeof(Wave), start, len);
        if (c.retriggerSound) {
            if (wave) {
                ch.playSample(waveforms_[data].data, 0, len);
                if (len > 2) ch.setLoop(0, len);
            } else {
                ch.mute();
            }
            c.retriggerSound = false;
        } else if (wave && len > 2) {
            ch.setSample(waveforms_[data].data, 0, len);
            ch.setLoop(0, len);
        }
        if (++c.soundTablePosition >= 48) c.soundTablePosition = 0;
    }

    void vibratoHandler(Chan& c, const Inst& inst) {
        const Envelope& info = inst.vibrato[c.vibratoPosition];  // speed, delay, sustain
        c.vibratoPeriod = (uint16_t)(c.vibratoDirection ? c.vibratoPeriod - info.speed : c.vibratoPeriod + info.speed);
        if (--c.vibratoDelay == 0) {
            c.vibratoDelay = info.level;
            c.vibratoDirection = !c.vibratoDirection;
        }
        if (c.vibratoSustain != 0) {
            c.vibratoSustain--;
        } else {
            if (++c.vibratoPosition == 5) c.vibratoPosition = 4;
            c.vibratoSustain = inst.vibrato[c.vibratoPosition].sustain;
        }
    }

    void volumeHandler(Chan& c, const Inst& inst) {
        if (c.volumeSustain != 0) {
            c.volumeSustain--;
            return;
        }
        const Envelope& info = inst.volume[c.volumePosition];
        bool next;
        if (c.actualVolume >= info.level) {
            c.actualVolume = (int16_t)(c.actualVolume - info.speed);
            next = c.actualVolume < info.level;
        } else {
            c.actualVolume = (int16_t)(c.actualVolume + info.speed);
            next = c.actualVolume > info.level;
        }
        if (next) {
            c.actualVolume = info.level;
            if (++c.volumePosition == 5) c.volumePosition = 4;
            c.volumeSustain = info.sustain;
        }
    }

    void portamentoHandler(Chan& c) {
        if (c.portamento == 0) return;
        if (c.finalPeriod >= c.period) {
            c.finalPeriod = (uint16_t)(c.finalPeriod - c.portamento);
            if (c.finalPeriod < c.period) c.finalPeriod = c.period;
        } else {
            c.finalPeriod = (uint16_t)(c.finalPeriod + c.portamento);
            if (c.finalPeriod > c.period) c.finalPeriod = c.period;
        }
    }

    void arpeggioHandler(Chan& c) {
        int8_t arp = c.arpeggio[c.arpeggioPosition];
        if (c.arpeggioPosition != 0 && arp == -128) {
            c.arpeggioPosition = 0;
            arp = c.arpeggio[0];
        }
        c.arpeggioPosition = (c.arpeggioPosition + 1) & 0x0F;
        if (c.portamento == 0) {
            uint8_t index = (uint8_t)(arp + c.note + c.transpose);
            if (index >= kPeriodCount) index = kPeriodCount - 1;
            c.finalPeriod = kPeriods[index];
        }
    }
};
}  // namespace

bool isDeltaMusic2(const uint8_t* d, size_t n) {
    if (!d || n < 0xFDA || std::memcmp(d + 0xBC6, ".FNL", 4) != 0) return false;
    uint64_t p = 0xFDA;
    for (int c = 0; c < 4; ++c) p += rd16(d + 0xFCA + 4 * c + 2);
    if (p + 4 > n) return false;
    const uint32_t blockLength = rd32(d + p);
    if (blockLength < 64) return false;  // no block
    p += 4 + (uint64_t)blockLength;
    if (p + 256 > n) return false;
    p += 256 + rd16(d + p + 254);
    if (p + 4 > n) return false;
    const uint32_t waveLength = rd32(d + p);
    if (waveLength < 256) return false;  // no noise waveform
    p += 4 + (uint64_t)waveLength + 64;
    return p <= n;
}

std::unique_ptr<AmigaSynthPlayer> openDeltaMusic2(const uint8_t* d, size_t n) {
    if (!isDeltaMusic2(d, n)) return nullptr;
    std::unique_ptr<DeltaMusic2> p(new DeltaMusic2);
    if (!p->load(d, n)) return nullptr;
    return p;
}
