// Delta Music 1.0 ("ALL ") replayer, ported from NostalgicPlayer's DeltaMusic10Worker.cs
// (Thomas Neumann, MIT licence) onto AmigaSynthPlayer. Index checks added where the C# code relied on
// exceptions (instrument, block and period indexes, track jumps, sound table walks, sample ranges).
// Sample loops: the file keeps repeat start / length in words (they end exactly at the sample's end in
// the real modules), so they are doubled here; NostalgicPlayer passes them on as bytes.
#include <cstring>
#include <vector>

#include "core/amiga_formats.h"
#include "core/amiga_synth.h"

namespace {
const uint16_t kPeriods[] = {
    0,    6848, 6464, 6096, 5760, 5424, 5120, 4832, 4560, 4304, 4064, 3840, 3616, 3424, 3232, 3048, 2880,
    2712, 2560, 2416, 2280, 2152, 2032, 1920, 1808, 1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076,
    960,  904,  856,  808,  762,  720,  678,  640,  604,  570,  538,  508,  480,  452,  428,  404,  381,
    360,  339,  320,  302,  285,  269,  254,  240,  226,  214,  202,  190,  180,  170,  160,  151,  143,
    135,  127,  120,  113,  113,  113,  113,  113,  113,  113,  113,  113,  113,  113,  113,  113};
constexpr int kPeriodCount = (int)(sizeof kPeriods / sizeof kPeriods[0]);
uint16_t period(int i) { return kPeriods[i < 0 ? 0 : (i >= kPeriodCount ? kPeriodCount - 1 : i)]; }

enum Effect : uint8_t {
    kNone, kSetSpeed, kSlideUp, kSlideDown, kSetFilter, kSetVibratoWait, kSetVibratoStep, kSetVibratoLength,
    kSetBendRate, kSetPortamento, kSetVolume, kSetArp1, /* .. kSetArp8 = 0x12 */ kSetArp1_5 = 0x13, kSetArp2_6,
    kSetArp3_7, kSetArp4_8, kSetAttackStep, kSetAttackDelay, kSetDecayStep, kSetDecayDelay, kSetSustain1,
    kSetSustain2, kSetReleaseStep, kSetReleaseDelay
};

struct Line {
    uint8_t instrument = 0, note = 0, effect = 0, arg = 0;
};
using Block = Line[16];
const Block kEmptyBlock = {};

struct TrackEntry {
    uint8_t block;
    int8_t transpose;
};

// The parameters effects may change while the song plays (copied fresh at every restart).
struct Inst {
    bool present = false, isSample = false;
    uint8_t attackStep = 0, attackDelay = 0, decayStep = 0, decayDelay = 0;
    uint16_t sustain = 0;
    uint8_t releaseStep = 0, releaseDelay = 0, volume = 0, vibratoWait = 0, vibratoStep = 0, vibratoLength = 0;
    int8_t bendRate = 0;
    uint8_t portamento = 0, tableDelay = 0;
    uint8_t arpeggio[8] = {};
    uint16_t sampleLength = 0, repeatStart = 0, repeatLength = 0;
    uint8_t table[48] = {};
    const std::vector<int8_t>* data = nullptr;
};

struct Chan {
    Inst* soundData = nullptr;
    uint16_t period = 0;
    uint8_t soundTableCounter = 0, soundTableDelay = 0;
    uint16_t trackCounter = 0;
    const Line* block = kEmptyBlock;
    uint32_t blockCounter = 0;
    uint8_t vibratoWait = 0, vibratoLength = 0, vibratoPosition = 0, vibratoCompare = 0;
    uint16_t vibratoFrequency = 0;
    uint8_t frequencyData = 0, actualVolume = 0, attackDelay = 0, decayDelay = 0;
    uint16_t sustain = 0;
    uint8_t releaseDelay = 0, playSpeed = 1;
    int16_t bendRateFrequency = 0;
    int8_t transpose = 0;
    uint8_t status = 0, arpeggioCounter = 0, effectNumber = 0, effectData = 0;
    bool retriggerSound = false;
};

struct Reader {
    const uint8_t* d;
    size_t n, p = 0;
    bool eof = false;
    uint8_t u8() {
        if (p >= n) {
            eof = true;
            return 0;
        }
        return d[p++];
    }
    uint16_t u16() {
        const uint16_t hi = u8();
        return (uint16_t)(hi << 8 | u8());
    }
    uint32_t u32() {
        const uint32_t hi = u16();
        return hi << 16 | u16();
    }
};

uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

class DeltaMusic1 : public AmigaSynthPlayer {
public:
    bool load(const uint8_t* d, size_t n) {
        Reader r{d, n, 4};
        uint32_t trackLengths[4], instLengths[20];
        for (uint32_t& l : trackLengths) l = r.u32();
        const uint32_t blockLength = r.u32();
        for (uint32_t& l : instLengths) l = r.u32();
        for (int c = 0; c < 4; ++c) {
            tracks_[c].resize(trackLengths[c] / 2);
            for (TrackEntry& t : tracks_[c]) {
                t.block = r.u8();
                t.transpose = (int8_t)r.u8();
            }
        }
        if (r.eof) return false;
        blocks_.resize(blockLength / 64);
        for (auto& b : blocks_)
            for (Line& l : b.lines) {
                l.instrument = r.u8();
                l.note = r.u8();
                l.effect = r.u8();
                l.arg = r.u8();
            }
        if (r.eof || blocks_.empty()) return false;
        for (int i = 0; i < 20; ++i) {
            if (!instLengths[i]) continue;
            Inst& in = backup_[i];
            in.present = true;
            in.attackStep = r.u8();
            in.attackDelay = r.u8();
            in.decayStep = r.u8();
            in.decayDelay = r.u8();
            in.sustain = r.u16();
            in.releaseStep = r.u8();
            in.releaseDelay = r.u8();
            in.volume = r.u8();
            in.vibratoWait = r.u8();
            in.vibratoStep = r.u8();
            in.vibratoLength = r.u8();
            in.bendRate = (int8_t)r.u8();
            in.portamento = r.u8();
            in.isSample = r.u8() != 0;
            in.tableDelay = r.u8();
            for (uint8_t& a : in.arpeggio) a = r.u8();
            in.sampleLength = r.u16();
            in.repeatStart = r.u16();
            in.repeatLength = r.u16();
            if (!in.isSample)
                for (uint8_t& t : in.table) t = r.u8();
            const uint32_t head = in.isSample ? 30 : 78;
            if (r.eof || instLengths[i] < head || instLengths[i] - head > n - r.p) return false;
            sampleData_[i].assign((const int8_t*)d + r.p, (const int8_t*)d + r.p + (instLengths[i] - head));
            r.p += instLengths[i] - head;
            in.data = &sampleData_[i];
        }
        format_ = "DELTA MUSIC 1.0 (AMIGA)";
        measure();
        return true;
    }

protected:
    void initSound() override {
        speed_ = 6;
        for (int i = 0; i < 20; ++i) inst_[i] = backup_[i];
        for (Chan& c : chan_) c = Chan{};
        std::memset(ended_, 0, sizeof ended_);
    }

    void playTick() override {
        for (int i = 0; i < 4; ++i) calculateFrequency(i, chan_[i]);
    }

private:
    std::vector<TrackEntry> tracks_[4];
    struct BlockData {
        Line lines[16];
    };
    std::vector<BlockData> blocks_;
    Inst backup_[20], inst_[20];
    std::vector<int8_t> sampleData_[20];
    Chan chan_[4];
    uint8_t speed_ = 6;
    bool ended_[4] = {};

    void onEndReached(int ch) {  // the song ends when every channel has wrapped
        ended_[ch] = true;
        if (ended_[0] && ended_[1] && ended_[2] && ended_[3]) {
            endReached_ = true;
            std::memset(ended_, 0, sizeof ended_);
        }
    }

    static void setPeriod(AmigaChannel& c, uint32_t p) { c.setAmigaPeriod(p && p < 113 ? 113 : p); }

    void nextBlock(int n, Chan& c) {
        const std::vector<TrackEntry>& track = tracks_[n];
        if (track.empty()) {  // nothing to play on this channel
            onEndReached(n);
            c.block = kEmptyBlock;
            return;
        }
        if (c.trackCounter >= track.size()) {
            c.trackCounter = 0;
            onEndReached(n);
        }
        TrackEntry t = track[c.trackCounter];
        for (size_t guard = 0; t.block == 0xFF && t.transpose == -1; ++guard) {  // jump
            if (guard > track.size()) {  // jumps that only lead to jumps: rest on this channel
                onEndReached(n);
                c.block = kEmptyBlock;
                return;
            }
            const uint16_t old = c.trackCounter;
            const TrackEntry to = c.trackCounter + 1u < track.size() ? track[c.trackCounter + 1] : TrackEntry{0, 0};
            c.trackCounter = (uint16_t)(((to.block << 8) | (uint8_t)to.transpose) & 0x7FF);
            if (c.trackCounter >= track.size()) c.trackCounter = 0;
            if (c.trackCounter < old) onEndReached(n);
            t = track[c.trackCounter];
        }
        c.transpose = t.transpose;
        c.block = t.block < blocks_.size() ? blocks_[t.block].lines : kEmptyBlock;
        c.trackCounter++;
    }

    void calculateFrequency(int n, Chan& c) {
        AmigaChannel& ch = ch_[n];
        Inst* inst = c.soundData;
        if (--c.playSpeed == 0) {
            c.playSpeed = speed_;
            if (c.blockCounter == 0) nextBlock(n, c);
            const Line& line = c.block[c.blockCounter];
            if (line.effect != kNone) {
                c.effectNumber = line.effect;
                c.effectData = line.arg;
            }
            if (line.note != 0) {
                c.frequencyData = (uint8_t)(line.note + c.transpose);
                c.status = 0;
                c.bendRateFrequency = 0;
                c.arpeggioCounter = 0;
                c.effectNumber = line.effect;
                c.effectData = line.arg;
                inst = line.instrument < 20 && inst_[line.instrument].present ? &inst_[line.instrument] : nullptr;
                c.soundData = inst;
                c.soundTableCounter = 0;
                if (!inst) {
                    ch.mute();
                } else {
                    if (inst->isSample) {
                        uint32_t start = 0, len = inst->sampleLength;
                        if (clipRange(inst->data->size(), start, len)) {
                            ch.playSample(inst->data->data(), start, len);
                            uint32_t ls = inst->repeatStart * 2u, ll = inst->repeatLength * 2u;
                            if (inst->repeatLength > 1 && clipRange(inst->data->size(), ls, ll) && ll > 2)
                                ch.setLoop(ls, ll);
                        } else {
                            ch.mute();
                        }
                    } else {
                        c.retriggerSound = true;
                    }
                    c.vibratoWait = inst->vibratoWait;
                    c.vibratoLength = c.vibratoPosition = inst->vibratoLength;
                    c.vibratoCompare = (uint8_t)(inst->vibratoLength * 2);
                    c.actualVolume = 0;
                    c.soundTableDelay = 0;
                    c.soundTableCounter = 0;
                    c.attackDelay = 0;
                    c.decayDelay = 0;
                    c.sustain = inst->sustain;
                    c.releaseDelay = 0;
                }
            }
            c.blockCounter = (c.blockCounter + 1) & 15;
        }
        if (inst) {
            if (!inst->isSample) {
                if (c.soundTableDelay == 0) soundTableHandler(ch, c, *inst);
                else c.soundTableDelay--;
            }
            portamentoHandler(c, *inst);
            vibratoHandler(c, *inst);
            c.bendRateFrequency = (int16_t)(inst->bendRate >= 0 ? c.bendRateFrequency - inst->bendRate
                                                                 : c.bendRateFrequency + -inst->bendRate);
            effectHandler(c, *inst);
            arpeggioHandler(ch, c, *inst);
            volumeHandler(ch, c, *inst);
        }
    }

    void soundTableHandler(AmigaChannel& ch, Chan& c, Inst& inst) {
        c.soundTableDelay = inst.tableDelay;
        for (int guard = 0; guard < 256; ++guard) {  // a table of only delays / jumps would walk forever
            if (c.soundTableCounter >= 48) c.soundTableCounter = 0;
            const uint8_t data = inst.table[c.soundTableCounter];
            if (data == 0xFF) {
                c.soundTableCounter = c.soundTableCounter + 1 < 48 ? inst.table[c.soundTableCounter + 1] : 0;
            } else if (data >= 0x80) {
                inst.tableDelay = data & 0x7F;
                c.soundTableCounter++;
            } else {
                c.soundTableCounter++;
                uint32_t start = data * 32u, len = inst.sampleLength;
                if (clipRange(inst.data->size(), start, len)) {
                    const int8_t* wave = inst.data->data();
                    if (c.retriggerSound) {
                        ch.playSample(wave, start, len);
                        c.retriggerSound = false;
                    } else if (len > 2) {
                        ch.setSample(wave, start, len);
                    }
                    if (len > 2) ch.setLoop(start, len);
                }
                break;
            }
        }
    }

    void portamentoHandler(Chan& c, const Inst& inst) {
        if (!inst.portamento) return;
        const uint16_t wanted = (uint16_t)(period(c.frequencyData) + c.bendRateFrequency);
        if (c.period == 0) {
            c.period = wanted;
        } else {
            uint16_t p = c.period;
            if (p > wanted) {
                p = (uint16_t)(p - inst.portamento);
                c.period = p < wanted ? wanted : p;
            } else if (p < wanted) {
                p = (uint16_t)(p + inst.portamento);
                c.period = p > wanted ? wanted : p;
            }
        }
    }

    void vibratoHandler(Chan& c, const Inst& inst) {
        if (c.vibratoWait != 0) {
            c.vibratoWait--;
            return;
        }
        c.vibratoFrequency = (uint16_t)(c.vibratoPosition * inst.vibratoStep);
        if (c.status & 1) {
            if (--c.vibratoPosition == 0) c.status ^= 1;
        } else {
            if (++c.vibratoPosition == c.vibratoCompare) c.status ^= 1;
        }
    }

    void effectHandler(Chan& c, Inst& inst) {
        uint8_t data = c.effectData;
        const uint8_t capped = data > 64 ? 64 : data;
        switch (c.effectNumber) {
        case kSetSpeed:
            if (data) speed_ = data;
            break;
        case kSlideUp: c.bendRateFrequency = (int16_t)(c.bendRateFrequency - data); break;
        case kSlideDown: c.bendRateFrequency = (int16_t)(c.bendRateFrequency + data); break;
        case kSetFilter: break;  // no Amiga LED filter here
        case kSetVibratoWait: inst.vibratoWait = data; break;
        case kSetVibratoStep: inst.vibratoStep = data; break;
        case kSetVibratoLength: inst.vibratoLength = data; break;
        case kSetBendRate: inst.bendRate = (int8_t)data; break;
        case kSetPortamento: inst.portamento = data; break;
        case kSetVolume: inst.volume = capped; break;
        case kSetArp1_5:
        case kSetArp2_6:
        case kSetArp3_7:
        case kSetArp4_8:
            inst.arpeggio[c.effectNumber - kSetArp1_5] = data;
            inst.arpeggio[c.effectNumber - kSetArp1_5 + 4] = data;
            break;
        case kSetAttackStep: inst.attackStep = capped; break;
        case kSetAttackDelay: inst.attackDelay = data; break;
        case kSetDecayStep: inst.decayStep = capped; break;
        case kSetDecayDelay: inst.decayDelay = data; break;
        case kSetSustain1: inst.sustain = (uint16_t)((inst.sustain & 0x00FF) | (data << 8)); break;
        case kSetSustain2: inst.sustain = (uint16_t)((inst.sustain & 0xFF00) | data); break;
        case kSetReleaseStep: inst.releaseStep = capped; break;
        case kSetReleaseDelay: inst.releaseDelay = data; break;
        default:
            if (c.effectNumber >= kSetArp1 && c.effectNumber < kSetArp1_5) inst.arpeggio[c.effectNumber - kSetArp1] = data;
            break;
        }
    }

    void arpeggioHandler(AmigaChannel& ch, Chan& c, const Inst& inst) {
        const uint8_t arp = inst.arpeggio[c.arpeggioCounter];
        c.arpeggioCounter = (c.arpeggioCounter + 1) & 7;
        uint16_t p = period(c.frequencyData + arp);
        p = (uint16_t)((p - c.vibratoLength * inst.vibratoStep) + c.bendRateFrequency);
        if (inst.portamento) p = c.period;
        else c.period = 0;
        p = (uint16_t)(p + c.vibratoFrequency);
        setPeriod(ch, p);
    }

    void volumeHandler(AmigaChannel& ch, Chan& c, const Inst& inst) {
        int vol = c.actualVolume;
        uint8_t status = c.status & 0x0E;
        if (status == 0) {  // attack
            if (c.attackDelay == 0) {
                c.attackDelay = inst.attackDelay;
                vol += inst.attackStep;
                if (vol >= 64) {
                    vol = 64;
                    status |= 0x02;
                    c.status |= 0x02;
                }
            } else {
                c.attackDelay--;
            }
        }
        if (status == 0x02) {  // decay
            if (c.decayDelay == 0) {
                c.decayDelay = inst.decayDelay;
                vol -= inst.decayStep;
                if (vol <= inst.volume) {
                    vol = inst.volume;
                    status |= 0x06;
                    c.status |= 0x06;
                }
            } else {
                c.decayDelay--;
            }
        }
        if (status == 0x06) {  // sustain
            if (c.sustain == 0) {
                status |= 0x0E;
                c.status |= 0x0E;
            } else {
                c.sustain--;
            }
        }
        if (status == 0x0E) {  // release
            if (c.releaseDelay == 0) {
                c.releaseDelay = inst.releaseDelay;
                vol -= inst.releaseStep;
                if (vol <= 0) {
                    vol = 0;
                    c.status &= 0x09;
                }
            } else {
                c.releaseDelay--;
            }
        }
        c.actualVolume = (uint8_t)vol;
        ch.setAmigaVolume((uint32_t)(vol < 0 ? 0 : vol));
    }
};
}  // namespace

bool isDeltaMusic1(const uint8_t* d, size_t n) {
    if (!d || n < 104 || std::memcmp(d, "ALL ", 4) != 0) return false;
    uint64_t total = 104, tracks = 0;
    for (int i = 0; i < 25; ++i) {
        const uint32_t l = rd32(d + 4 + 4 * i);
        if (i < 4) tracks += l;
        if (i == 4 && l < 64) return false;           // no block
        if (i >= 5 && l != 0 && l < 30) return false;  // shorter than an instrument header
        total += l;
    }
    return tracks > 0 && total <= n;
}

std::unique_ptr<AmigaSynthPlayer> openDeltaMusic1(const uint8_t* d, size_t n) {
    if (!isDeltaMusic1(d, n)) return nullptr;
    std::unique_ptr<DeltaMusic1> p(new DeltaMusic1);
    if (!p->load(d, n)) return nullptr;
    return p;
}
