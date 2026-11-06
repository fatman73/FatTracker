// SidMon 2.0 ("SIDMON II - THE MIDI VERSION") replayer, ported from NostalgicPlayer's SidMon20Worker.cs
// (Thomas Neumann, MIT licence) onto AmigaSynthPlayer.
// Added for hostile files: every block, offset and index is checked (the C# code relied on exceptions),
// out of range tracks / samples / table entries are treated as empty, the wave / arpeggio / vibrato lists
// are one flat table each (as in Amiga memory), the song position is an int (an sbyte in the C# code,
// which breaks past 128 positions), and the instruments, samples and negation state the song modifies
// while it plays are restored on restart.
#include <cstring>
#include <vector>

#include "core/amiga_synth.h"
#include "core/amiga_formats.h"

namespace {
// Period table (index = note)
const uint16_t kPeriods[] = {
    0, 5760, 5424, 5120, 4832, 4560, 4304, 4064, 3840, 3616, 3424, 3232, 3048, 2880, 2712, 2560,
    2416, 2280, 2152, 2032, 1920, 1808, 1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016,
    960, 904, 856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453, 428, 404,
    381, 360, 339, 320, 302, 285, 269, 254, 240, 226, 214, 202, 190, 180, 170, 160,
    151, 143, 135, 127, 120, 113, 107, 101, 95,
};
constexpr int kPeriodCount = (int)(sizeof kPeriods / sizeof kPeriods[0]);
uint16_t period(int i) { return i >= 0 && i < kPeriodCount ? kPeriods[i] : 0; }

const char kId[] = "SIDMON II - THE MIDI VERSION";

enum Env : uint8_t { kDone, kAttack, kDecay, kSustain, kRelease };

struct Instrument {
    uint8_t waveList = 0, waveListLen = 0, waveListSpeed = 0, waveListDelay = 0, arp = 0, arpLen = 0, arpSpeed = 0,
            arpDelay = 0, vib = 0, vibLen = 0, vibSpeed = 0, vibDelay = 0;
    int8_t bendSpeed = 0;
    uint8_t bendDelay = 0, attackMax = 0, attackSpeed = 0, decayMin = 0, decaySpeed = 0, sustainTime = 0,
            releaseMin = 0, releaseSpeed = 0;
};
struct Sample {
    std::vector<int8_t> data;
    uint32_t length = 0, loopStart = 0, loopLength = 0;
};
struct Negate {
    uint32_t startOffset = 0, endOffset = 0;
    uint16_t loopIndex = 0, status = 0;
    int16_t speed = 0;
    int32_t position = 0;
    uint16_t index = 0;
    int16_t doNegation = 0;
};
struct Seq {
    uint8_t track = 0;
    int8_t noteTranspose = 0, instrTranspose = 0;
};
struct Voice {
    const Seq* seqList = nullptr;
    const int8_t* sampleData = nullptr;
    uint32_t sampleLength = 0;
    uint16_t samplePeriod = 0;
    int16_t sampleVolume = 0;
    Env env = kAttack;
    uint16_t sustainCounter = 0;
    int instr = 0;
    const int8_t* loopSample = nullptr;
    uint32_t loopOffset = 0, loopLength = 0;
    uint16_t originalNote = 0, waveListDelay = 0;
    int16_t waveListOffset = 0;
    uint16_t arpDelay = 0;
    int16_t arpOffset = 0;
    uint16_t vibDelay = 0;
    int16_t vibOffset = 0;
    uint16_t currentNote = 0, currentInstrument = 0, currentEffect = 0, currentEffectArg = 0, bendCounter = 0;
    int16_t instrTranspose = 0, bendValue = 0;
    uint16_t slideNote = 0;
    int16_t slideSpeed = 0;
    int trackPos = 0, track = -1;
    uint16_t emptyNotes = 0;
    int16_t noteTranspose = 0;
    uint16_t currentSample = 0;
};

struct Block {
    uint64_t offset = 0, length = 0;
};

class SidMon2 : public AmigaSynthPlayer {
public:
    bool load(const uint8_t* d, size_t n) {
        d_ = d;
        n_ = n;
        // song data (LoadSongData)
        positions_ = d[2] + 1;
        if (positions_ > 255) return false;  // a byte in the replayer
        startSpeed_ = d[3];
        const uint32_t samples = u16(4) / 64;
        uint64_t offset = 58 + (uint64_t)u32(6) + u32(10);
        Block blk[10];  // positions, note transposes, instrument transposes, instruments, wave lists, arpeggios,
                        // vibratos, sample infos, track table, tracks
        for (int i = 0; i < 10; ++i) {
            blk[i].offset = offset;
            blk[i].length = u32(14 + 4 * i);
            offset += blk[i].length;
            if (offset > n) return false;
        }
        // lists
        if (!list(blk[4], waveList_) || !list(blk[5], arps_) || !list(blk[6], vibs_)) return false;
        // sequences
        if (blk[0].length != blk[1].length || blk[1].length != blk[2].length || blk[0].length != positions_ * 4u)
            return false;
        seq_.resize((size_t)positions_ * 4);
        for (int ch = 0; ch < 4; ++ch)
            for (int j = 0; j < positions_; ++j) {
                const size_t at = (size_t)ch * positions_ + j;
                Seq& s = seq_[at];
                s.track = d[blk[0].offset + at];
                s.noteTranspose = (int8_t)d[blk[1].offset + at];
                s.instrTranspose = (int8_t)d[blk[2].offset + at];
            }
        // tracks
        const uint32_t tracks = (uint32_t)(blk[8].length / 2);
        trackStart_.resize(tracks);
        trackLen_.resize(tracks);
        uint64_t sampleOffset = blk[8].offset;
        for (uint32_t i = 0; i < tracks; ++i) {
            const uint32_t at = u16(blk[8].offset + 2ull * i);
            const uint32_t end = i + 1 == tracks ? (uint32_t)blk[9].length : u16(blk[8].offset + 2ull * (i + 1));
            if (end < at || end > blk[9].length) return false;
            trackStart_[i] = (uint32_t)(blk[9].offset + at);
            trackLen_[i] = end - at;
            sampleOffset = blk[9].offset + end;
        }
        // instruments
        if (blk[3].length % 32 || blk[3].length == 0) return false;
        inst_.resize((size_t)(blk[3].length / 32));
        for (size_t i = 0; i < inst_.size(); ++i) {
            const uint8_t* p = d + blk[3].offset + i * 32;
            Instrument& in = inst_[i];
            in.waveList = p[0], in.waveListLen = p[1], in.waveListSpeed = p[2], in.waveListDelay = p[3];
            in.arp = p[4], in.arpLen = p[5], in.arpSpeed = p[6], in.arpDelay = p[7];
            in.vib = p[8], in.vibLen = p[9], in.vibSpeed = p[10], in.vibDelay = p[11];
            in.bendSpeed = (int8_t)p[12], in.bendDelay = p[13];
            in.attackMax = p[16], in.attackSpeed = p[17], in.decayMin = p[18], in.decaySpeed = p[19];
            in.sustainTime = p[20], in.releaseMin = p[21], in.releaseSpeed = p[22];
        }
        // samples: the infos, then the data after the tracks
        if (blk[7].offset + (uint64_t)samples * 64 > n) return false;
        samples_.resize(samples);
        negate_.resize(samples);
        uint64_t total = 0;
        for (uint32_t i = 0; i < samples; ++i) {
            const uint64_t p = blk[7].offset + 64ull * i;
            Sample& s = samples_[i];
            s.length = u16(p + 4) * 2u;
            s.loopStart = u16(p + 6) * 2u;
            s.loopLength = u16(p + 8) * 2u;
            if (s.loopStart > s.length) s.loopStart = s.loopLength = 0;
            else if (s.loopStart + s.loopLength > s.length) s.loopLength = s.length - s.loopStart;
            Negate& g = negate_[i];
            g.startOffset = u16(p + 10) * 2u;
            g.endOffset = u16(p + 12) * 2u;
            g.loopIndex = (uint16_t)u16(p + 14);
            g.status = (uint16_t)u16(p + 16);
            g.speed = (int16_t)u16(p + 18);
            g.position = (int32_t)u32(p + 20);
            g.index = (uint16_t)u16(p + 24);
            g.doNegation = (int16_t)u16(p + 26);
            total += s.length;
        }
        // The replayer meant to start the samples on an even offset (the C# code tests "% 1"): do so when
        // the file has room for that pad byte.
        if ((sampleOffset & 1) && sampleOffset + 1 + total <= n) ++sampleOffset;
        if (sampleOffset + total > n) return false;
        for (Sample& s : samples_) {
            s.data.assign((const int8_t*)d + sampleOffset, (const int8_t*)d + sampleOffset + s.length);
            sampleOffset += s.length;
        }
        positions_ -= 1;  // the replayer counts from 0
        samplesStart_ = samples_;
        negateStart_ = negate_;
        instStart_ = inst_;
        format_ = "SIDMON 2.0 (AMIGA)";
        measure();
        return true;
    }

protected:
    void initSound() override {
        inst_ = instStart_;
        negate_ = negateStart_;
        for (size_t i = 0; i < samples_.size(); ++i)  // in place: the channels may point at the data
            std::memcpy(samples_[i].data.data(), samplesStart_[i].data.data(), samples_[i].data.size());
        position_ = 0;
        row_ = 0;
        patternLength_ = 64;
        rast_ = rast2_ = 0;
        speed_ = startSpeed_;
        for (int i = 0; i < 4; ++i) {
            Voice& v = v_[i];
            v = Voice{};
            v.seqList = &seq_[(size_t)i * (positions_ + 1)];
            v.instr = 0;
            v.currentSample = waveListAt(inst_[0].waveList, 0) < 0 ? 0 : (uint16_t)waveListAt(inst_[0].waveList, 0);
            const Sample* s = sample(v.currentSample);
            v.sampleData = v.loopSample = s ? s->data.data() : nullptr;
            v.sampleLength = s ? s->length : 0;
            v.loopOffset = s ? s->loopStart : 0;
            v.loopLength = s ? s->loopLength : 0;
            findNote(v);
        }
    }

    void playTick() override {
        if (++rast2_ == 3) rast2_ = 0;
        if (++rast_ >= speed_) {
            rast_ = rast2_ = 0;
            for (Voice& v : v_) getNote(v);
            for (int i = 0; i < 4; ++i) playVoice(v_[i], ch_[i]);
            negation();
            if (++row_ == patternLength_) {
                row_ = 0;
                if (position_ == positions_) {
                    position_ = -1;
                    endReached_ = true;
                }
                ++position_;
                for (Voice& v : v_) findNote(v);
            }
        }
        for (int i = 0; i < 4; ++i) effects(v_[i], ch_[i]);
        if (rast_ != 0) negation();
    }

private:
    const uint8_t* d_ = nullptr;
    size_t n_ = 0;
    int positions_ = 0;
    uint8_t startSpeed_ = 0;
    std::vector<uint8_t> waveList_;
    std::vector<int8_t> arps_, vibs_;  // 16 entries per list
    std::vector<Seq> seq_;             // 4 channels of positions_ + 1
    std::vector<uint32_t> trackStart_, trackLen_;
    std::vector<Instrument> inst_, instStart_;
    std::vector<Sample> samples_, samplesStart_;
    std::vector<Negate> negate_, negateStart_;
    int position_ = 0;
    uint8_t row_ = 0, patternLength_ = 64, rast_ = 0, rast2_ = 0, speed_ = 0;
    Voice v_[4];

    uint32_t u16(uint64_t at) const { return at + 2 <= n_ ? (uint32_t)(d_[at] << 8 | d_[at + 1]) : 0; }
    uint32_t u32(uint64_t at) const { return u16(at) << 16 | u16(at + 2); }
    template <typename T>
    bool list(const Block& b, std::vector<T>& out) {
        if (b.length % 16) return false;
        out.assign((const T*)(d_ + b.offset), (const T*)(d_ + b.offset + b.length));
        return true;
    }
    int waveListAt(uint8_t list, int index) const {  // -1 past the table
        const size_t at = (size_t)list * 16 + (size_t)index;
        return index >= 0 && at < waveList_.size() ? waveList_[at] : -1;
    }
    int8_t arpAt(uint8_t list, int index) const {
        const size_t at = (size_t)list * 16 + (size_t)index;
        return index >= 0 && at < arps_.size() ? arps_[at] : 0;
    }
    int8_t vibAt(uint8_t list, int index) const {
        const size_t at = (size_t)list * 16 + (size_t)index;
        return index >= 0 && at < vibs_.size() ? vibs_[at] : 0;
    }
    const Sample* sample(uint32_t i) const { return i < samples_.size() ? &samples_[i] : nullptr; }
    int trackByte(const Voice& v, int pos) const {  // -1 past the track
        return v.track >= 0 && pos >= 0 && (uint32_t)pos < trackLen_[(size_t)v.track]
                   ? d_[trackStart_[(size_t)v.track] + (uint32_t)pos]
                   : -1;
    }

    void findNote(Voice& v) {
        const Seq& s = v.seqList[position_ >= 0 && position_ <= positions_ ? position_ : 0];
        v.track = s.track < trackStart_.size() ? s.track : -1;
        v.trackPos = 0;
        v.noteTranspose = s.noteTranspose;
        v.instrTranspose = s.instrTranspose;
        v.emptyNotes = 0;
    }

    void getNote(Voice& v) {
        getNote2(v);
        if (v.currentNote != 0) v.currentNote = (uint16_t)(v.currentNote + v.noteTranspose);
    }

    void getNote2(Voice& v) {
        v.currentNote = v.currentInstrument = v.currentEffect = v.currentEffectArg = 0;
        if (v.emptyNotes != 0) {
            --v.emptyNotes;
            return;
        }
        auto next = [&]() {  // past the end of the track: 0
            const int b = trackByte(v, v.trackPos++);
            return b < 0 ? 0 : b;
        };
        const int first = trackByte(v, v.trackPos);
        if (first >= 0) ++v.trackPos;
        int8_t value = (int8_t)(first < 0 ? 0xff : first);
        if (value == 0) {
            v.currentEffect = (uint16_t)next();
            v.currentEffectArg = (uint16_t)next();
            return;
        }
        if (value > 0) {
            if (value >= 0x70) {
                v.currentEffect = (uint8_t)value;
                v.currentEffectArg = (uint16_t)next();
                return;
            }
            v.currentNote = (uint16_t)value;
            value = (int8_t)next();
            if (value >= 0) {
                if (value >= 0x70) {
                    v.currentEffect = (uint8_t)value;
                    v.currentEffectArg = (uint16_t)next();
                    return;
                }
                v.currentInstrument = (uint16_t)value;
                value = (int8_t)next();
                if (value >= 0) {
                    v.currentEffect = (uint8_t)value;
                    v.currentEffectArg = (uint16_t)next();
                    return;
                }
            }
        }
        v.emptyNotes = (uint16_t)~value;
    }

    void playVoice(Voice& v, AmigaChannel& c) {
        v.bendValue = 0;
        if (v.currentNote == 0) return;
        v.sampleVolume = 0;
        v.waveListDelay = 0;
        v.waveListOffset = 0;
        v.arpDelay = 0;
        v.arpOffset = 0;
        v.vibDelay = 0;
        v.vibOffset = 0;
        v.bendCounter = 0;
        v.slideSpeed = 0;
        v.env = kAttack;
        v.sustainCounter = 0;
        if (v.currentInstrument != 0) {
            const uint16_t instr = (uint16_t)(v.currentInstrument - 1 + v.instrTranspose);
            if (instr < inst_.size()) {
                v.instr = instr;
                const int first = waveListAt(inst_[instr].waveList, 0);
                v.currentSample = (uint16_t)(first < 0 ? 0 : first);
                const Sample* s = sample(v.currentSample);
                v.sampleData = v.loopSample = s ? s->data.data() : nullptr;
                v.sampleLength = s ? s->length : 0;
                v.loopOffset = s ? s->loopStart : 0;
                v.loopLength = s ? s->loopLength : 0;
            }
        }
        const int note = v.currentNote + arpAt(inst_[(size_t)v.instr].arp, 0);
        if (note >= 0 && note < kPeriodCount) {
            v.originalNote = (uint16_t)note;
            v.samplePeriod = kPeriods[note];
            if (v.sampleLength != 0 && v.sampleData) {
                c.playSample(v.sampleData, 0, v.sampleLength);
                // SetLoop(LoopSample, ...): the loop may be in another sample (after a wave list change),
                // which is what a queued sample does
                if (v.loopLength > 2 && v.loopSample) c.setSample(v.loopSample, v.loopOffset, v.loopLength);
            } else {
                c.mute();
            }
        } else {
            c.mute();
        }
        c.setAmigaPeriod(v.samplePeriod);
    }

    void effects(Voice& v, AmigaChannel& c) {
        const Instrument& in = inst_[(size_t)v.instr];
        adsr(v, in, c);
        waveForm(v, in, c);
        arpeggio(v, in);
        soundTracker(v, c);
        vibrato(v, in);
        pitchBend(v, in);
        noteSlide(v);
        v.samplePeriod = (uint16_t)(v.samplePeriod + v.bendValue);
        if (v.samplePeriod < 95) v.samplePeriod = 95;
        else if (v.samplePeriod > 5760) v.samplePeriod = 5760;
        c.setAmigaPeriod(v.samplePeriod);
    }

    void adsr(Voice& v, const Instrument& in, AmigaChannel& c) {
        switch (v.env) {
            case kAttack:
                v.sampleVolume = (int16_t)(v.sampleVolume + in.attackSpeed);
                if (v.sampleVolume >= in.attackMax) {
                    v.sampleVolume = in.attackMax;
                    v.env = kDecay;
                }
                break;
            case kDecay:
                if (in.decaySpeed == 0) {
                    v.env = kSustain;
                } else {
                    v.sampleVolume = (int16_t)(v.sampleVolume - in.decaySpeed);
                    if (v.sampleVolume <= in.decayMin) {
                        v.sampleVolume = in.decayMin;
                        v.env = kSustain;
                    }
                }
                break;
            case kSustain:
                if (v.sustainCounter == in.sustainTime) v.env = kRelease;
                else ++v.sustainCounter;
                break;
            case kRelease:
                if (in.releaseSpeed == 0) {
                    v.env = kDone;
                } else {
                    v.sampleVolume = (int16_t)(v.sampleVolume - in.releaseSpeed);
                    if (v.sampleVolume <= in.releaseMin) {
                        v.sampleVolume = in.releaseMin;
                        v.env = kDone;
                    }
                }
                break;
            case kDone:
                break;
        }
        c.setVolume((uint16_t)v.sampleVolume);
    }

    void waveForm(Voice& v, const Instrument& in, AmigaChannel& c) {
        if (in.waveListLen == 0) return;
        if (v.waveListDelay != in.waveListDelay) {
            ++v.waveListDelay;
            return;
        }
        v.waveListDelay = (uint16_t)(v.waveListDelay - in.waveListSpeed);
        if (v.waveListOffset == in.waveListLen) v.waveListOffset = -1;
        ++v.waveListOffset;
        const int value = waveListAt(in.waveList, v.waveListOffset);
        const Sample* s = value >= 0 && value < 0x80 ? sample((uint32_t)value) : nullptr;
        if (!s) {  // the end of the list (or a sample that is not there)
            if (value < 0 || value >= 0x80) --v.waveListOffset;
            return;
        }
        v.currentSample = (uint16_t)value;
        v.loopSample = s->data.data();
        v.loopOffset = s->loopStart;
        v.loopLength = s->loopLength;
        if (c.active()) {
            c.setSample(v.loopSample, v.loopOffset, v.loopLength);
            c.setLoop(v.loopOffset, v.loopLength);
        }
    }

    void arpeggio(Voice& v, const Instrument& in) {
        if (in.arpLen == 0) return;
        if (v.arpDelay != in.arpDelay) {
            ++v.arpDelay;
            return;
        }
        v.arpDelay = (uint16_t)(v.arpDelay - in.arpSpeed);
        if (v.arpOffset == in.arpLen) v.arpOffset = -1;
        ++v.arpOffset;
        v.samplePeriod = period((int16_t)(v.originalNote + arpAt(in.arp, v.arpOffset)));
    }

    void vibrato(Voice& v, const Instrument& in) {
        if (in.vibLen == 0) return;
        if (v.vibDelay != in.vibDelay) {
            ++v.vibDelay;
            return;
        }
        v.vibDelay = (uint16_t)(v.vibDelay - in.vibSpeed);
        if (v.vibOffset == in.vibLen) v.vibOffset = -1;
        ++v.vibOffset;
        v.samplePeriod = (uint16_t)(v.samplePeriod + vibAt(in.vib, v.vibOffset));
    }

    void pitchBend(Voice& v, const Instrument& in) {
        if (in.bendSpeed == 0) return;
        if (v.bendCounter == in.bendDelay) v.bendValue = (int16_t)(v.bendValue + in.bendSpeed);
        else ++v.bendCounter;
    }

    void noteSlide(Voice& v) {
        if (v.currentEffect != 0 && v.currentEffect < 0x70 && v.currentEffectArg != 0) {
            v.slideNote = period(v.currentEffect);
            const int direction = v.slideNote - v.samplePeriod;
            if (direction == 0) return;
            int16_t arg = (int16_t)v.currentEffectArg;
            if (direction < 0) arg = (int16_t)-arg;
            v.slideSpeed = arg;
        }
        const int16_t speed = v.slideSpeed;
        if (speed == 0) return;
        v.samplePeriod = (uint16_t)(v.samplePeriod + speed);
        if (speed < 0 ? v.samplePeriod <= v.slideNote : v.samplePeriod >= v.slideNote) {
            v.samplePeriod = v.slideNote;
            v.slideSpeed = 0;
        }
    }

    void soundTracker(Voice& v, AmigaChannel& c) {
        uint16_t effect = v.currentEffect;
        if (effect < 0x70) return;
        effect &= 0x0f;
        if (rast_ == 0 && effect < 5) return;
        switch (effect) {
            case 0x0: {  // arpeggio
                const uint8_t arp[4] = {(uint8_t)(v.currentEffectArg >> 4), 0, (uint8_t)(v.currentEffectArg & 0x0f), 0};
                v.samplePeriod = period(v.originalNote + arp[rast2_ & 3]);
                break;
            }
            case 0x1:
                v.bendValue = (int16_t)-v.currentEffectArg;
                break;
            case 0x2:
                v.bendValue = (int16_t)v.currentEffectArg;
                break;
            case 0x3:
            case 0x4:  // volume up / down
                if (v.env == kDone) {
                    if (rast_ == 0 && v.currentInstrument != 0) v.sampleVolume = inst_[(size_t)v.instr].attackSpeed;
                    int volume = v.sampleVolume + (effect == 3 ? 1 : -1) * v.currentEffectArg * 4;
                    if (volume >= 256) volume = 255;
                    if (volume < 0) volume = 0;
                    v.sampleVolume = (int16_t)volume;
                }
                break;
            case 0x5: {  // ADSR attack
                Instrument& in = inst_[(size_t)v.instr];
                in.attackMax = (uint8_t)v.currentEffectArg;
                in.attackSpeed = (uint8_t)v.currentEffectArg;
                break;
            }
            case 0x6:  // pattern length
                if (v.currentEffectArg != 0) patternLength_ = (uint8_t)v.currentEffectArg;
                break;
            case 0xc: {  // volume
                uint32_t volume = v.currentEffectArg;
                c.setAmigaVolume(volume > 64 ? 64 : volume);
                volume *= 4;
                if (volume >= 255) volume = 255;
                v.sampleVolume = (int16_t)volume;
                break;
            }
            case 0xf: {  // speed
                const uint8_t speed = (uint8_t)(v.currentEffectArg & 0x0f);
                if (speed != 0) speed_ = speed;
                break;
            }
            default:
                break;
        }
    }

    void negation() {
        Negate* working[4] = {};
        for (int i = 0; i < 4; ++i) {
            const uint16_t number = v_[i].currentSample;
            if (number >= negate_.size()) continue;
            Negate& g = negate_[number];
            working[i] = &g;
            if (g.doNegation != 0) continue;
            g.doNegation = -1;
            if (g.index != 0) {
                g.index = (uint16_t)((g.index + 1) & 0x1f);
                continue;
            }
            g.index = g.loopIndex;
            if (g.status == 0) continue;
            std::vector<int8_t>& data = samples_[number].data;
            const int64_t endOffset = (uint32_t)(g.endOffset - 1);
            const int64_t pos = (int32_t)(uint32_t)((int64_t)g.startOffset + g.position);
            if (pos >= 0 && pos < (int64_t)data.size()) data[(size_t)pos] = (int8_t)~data[(size_t)pos];
            g.position = (int32_t)(uint32_t)((int64_t)g.position + g.speed);
            if (g.position < 0) {
                if (g.status == 2) {
                    g.position = (int32_t)endOffset;
                } else {
                    g.position = (int32_t)(uint32_t)((int64_t)g.position - g.speed);
                    g.speed = (int16_t)-g.speed;
                }
            } else if (g.position > endOffset) {
                if (g.status == 1) {
                    g.position = 0;
                } else {
                    g.position = (int32_t)(uint32_t)((int64_t)g.position - g.speed);
                    g.speed = (int16_t)-g.speed;
                }
            }
        }
        for (Negate* g : working)
            if (g) g->doNegation = 0;
    }
};
}  // namespace

bool detectSidMon2(const uint8_t* d, size_t n) { return d && n >= 86 && std::memcmp(d + 58, kId, 28) == 0; }

std::unique_ptr<AmigaSynthPlayer> openSidMon2(const uint8_t* d, size_t n) {
    if (!detectSidMon2(d, n)) return nullptr;
    std::unique_ptr<SidMon2> s(new SidMon2);
    if (!s->load(d, n)) return nullptr;
    return s;
}
