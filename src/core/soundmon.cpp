// BP SoundMon (Brian Postma) replayer for the "V.2" (SoundMon 1.1 / 2.0) and "V.3" (SoundMon 2.2 / 3.0)
// formats, ported from NostalgicPlayer's SoundMonWorker.cs (Thomas Neumann, MIT licence) onto
// AmigaSynthPlayer. The older "BPSM" files are not supported (NostalgicPlayer does not read them either).
// Index checks added where the C# code relied on exceptions: wave table reads and writes (tables, ADSR /
// LFO / EG / MOD pointers and synth buffers may point past the wave tables), sample and loop ranges,
// instrument numbers and jump targets. Track numbers must be 1..tracks (NostalgicPlayer indexes
// tracks[track - 1]); the loader refuses files where they are not.
// One change in behaviour: synth effect 1 (averaging) runs every FX speed ticks; NostalgicPlayer tests
// FxControl instead of FxCount there, so it never averages.
#include <algorithm>
#include <cstring>
#include <vector>

#include "core/amiga_formats.h"
#include "core/amiga_synth.h"

namespace {
const uint16_t kPeriods[] = {
    6848, 6464, 6080, 5760, 5440, 5120, 4832, 4576, 4320, 4064, 3840, 3616, 3424, 3232, 3040, 2880, 2720,
    2560, 2416, 2288, 2160, 2032, 1920, 1808, 1712, 1616, 1520, 1440, 1360, 1280, 1208, 1144, 1080, 1016,
    960,  904,  856,  808,  760,  720,  680,  640,  604,  572,  540,  508,  480,  452,  428,  404,  380,
    360,  340,  320,  302,  286,  270,  254,  240,  226,  214,  202,  190,  180,  170,  160,  151,  143,
    135,  127,  120,  113,  107,  101,  95,   90,   85,   80,   76,   72,   68,   64,   60,   57};
constexpr int kPeriodCount = (int)(sizeof kPeriods / sizeof kPeriods[0]);
uint16_t notePeriod(int note) {  // Periods[note + 36 - 1], clamped like NostalgicPlayer does
    const int i = note + 36 - 1;
    return kPeriods[i < 0 ? 0 : (i >= kPeriodCount ? kPeriodCount - 1 : i)];
}
const int16_t kVibratoTable[8] = {0, 64, 128, 64, 0, -64, -128, -64};

enum Optional : uint8_t {
    kArpeggioOnce, kSetVolume, kSetSpeed, kFilter, kPortUp, kPortDown,
    kVibrato,  // V.2: set repeat count
    kJump,     // V.2: dbra repeat count
    kSetAutoSlide, kSetArpeggio, kTranspose, kChangeFx, kOpt0C, kChangeInversion, kResetAdsr, kChangeNote
};

struct Instrument {
    bool synth = false;
    // sample
    uint16_t length = 0, loopStart = 0, loopLength = 0;
    const int8_t* adr = nullptr;
    // synth
    uint8_t waveTable = 0;
    uint16_t waveLength = 0;
    uint8_t adsrControl = 0, adsrTable = 0;
    uint16_t adsrLength = 0;
    uint8_t adsrSpeed = 0, lfoControl = 0, lfoTable = 0, lfoDepth = 0;
    uint16_t lfoLength = 0;
    uint8_t lfoDelay = 0, lfoSpeed = 0, egControl = 0, egTable = 0;
    uint16_t egLength = 0;
    uint8_t egDelay = 0, egSpeed = 0, fxControl = 0, fxSpeed = 0, fxDelay = 0, modControl = 0, modTable = 0,
            modSpeed = 0, modDelay = 0;
    uint16_t modLength = 0;
    uint16_t volume = 0;
};

struct Step {
    uint16_t track;
    int8_t soundTranspose, transpose;
};

struct Row {
    int8_t note;
    uint8_t instrument, optional, optionalData;
};

struct Voice {  // BpCurrent
    bool restart = false, useDefaultVolume = false, synthMode = false;
    int synthOffset = -1;
    uint16_t period = 0;
    uint8_t volume = 0, instrument = 0, note = 0, arpValue = 0;
    int8_t autoSlide = 0;
    uint8_t autoArp = 0;
    uint16_t egPtr = 0, lfoPtr = 0, adsrPtr = 0, modPtr = 0;
    uint8_t egCount = 0, lfoCount = 0, adsrCount = 0, modCount = 0, fxCount = 0, oldEgValue = 0;
    uint8_t egControl = 0, lfoControl = 0, adsrControl = 0, modControl = 0, fxControl = 0;
    int8_t vibrato = 0;
};

uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

// The file's layout, checked against its size (shared by isSoundMon and the loader).
struct Layout {
    bool v3 = false;
    uint8_t waveNum = 0;
    uint16_t stepNum = 0, trackNum = 0;
    size_t steps = 0, tracks = 0, waves = 0, samples = 0, end = 0;
};

bool parseLayout(const uint8_t* d, size_t n, Layout& l) {
    if (!d || n < 512 || std::memcmp(d + 26, "V.", 2) != 0 || (d[28] != '2' && d[28] != '3')) return false;
    l.v3 = d[28] == '3';
    l.waveNum = d[29];
    l.stepNum = rd16(d + 30);
    if (l.stepNum == 0) return false;
    l.steps = 32 + 15 * 32;
    l.tracks = l.steps + (size_t)l.stepNum * 16;
    if (l.tracks > n) return false;
    uint16_t maxTrack = 0;
    for (size_t i = 0; i < (size_t)l.stepNum * 4; ++i) {
        const uint16_t t = rd16(d + l.steps + 4 * i);
        if (t == 0) return false;  // NostalgicPlayer would index tracks[-1]
        if (t > maxTrack) maxTrack = t;
    }
    l.trackNum = maxTrack;
    l.waves = l.tracks + (size_t)l.trackNum * 48;
    l.samples = l.waves + (size_t)l.waveNum * 64;
    size_t end = l.samples;
    for (int i = 0; i < 15; ++i) {
        const uint8_t* in = d + 32 + 32 * i;
        if (in[0] != 0xFF) end += (size_t)rd16(in + 24) * 2;
    }
    l.end = end;
    return end <= n;
}

class SoundMon : public AmigaSynthPlayer {
public:
    bool load(const uint8_t* d, size_t n) {
        Layout l;
        if (!parseLayout(d, n, l)) return false;
        v3_ = l.v3;
        for (int i = 0; i < 15; ++i) {
            const uint8_t* p = d + 32 + 32 * i;
            Instrument& in = inst_[i];
            if (p[0] == 0xFF) {
                in.synth = true;
                in.waveTable = p[1];
                in.waveLength = (uint16_t)(rd16(p + 2) * 2);
                in.adsrControl = p[4];
                in.adsrTable = p[5];
                in.adsrLength = rd16(p + 6);
                in.adsrSpeed = p[8];
                in.lfoControl = p[9];
                in.lfoTable = p[10];
                in.lfoDepth = p[11];
                in.lfoLength = rd16(p + 12);
                if (!v3_) {
                    in.lfoDelay = p[15];
                    in.lfoSpeed = p[16];
                    in.egControl = p[17];
                    in.egTable = p[18];
                    in.egLength = rd16(p + 20);
                    in.egDelay = p[23];
                    in.egSpeed = p[24];
                    in.fxControl = 0;
                    in.fxSpeed = 1;
                    in.fxDelay = 0;
                    in.modControl = 0;
                    in.modTable = 0;
                    in.modSpeed = 1;
                    in.modDelay = 0;
                    in.volume = p[25];
                    in.modLength = 0;
                } else {
                    in.lfoDelay = p[14];
                    in.lfoSpeed = p[15];
                    in.egControl = p[16];
                    in.egTable = p[17];
                    in.egLength = rd16(p + 18);
                    in.egDelay = p[20];
                    in.egSpeed = p[21];
                    in.fxControl = p[22];
                    in.fxSpeed = p[23];
                    in.fxDelay = p[24];
                    in.modControl = p[25];
                    in.modTable = p[26];
                    in.modSpeed = p[27];
                    in.modDelay = p[28];
                    in.volume = p[29];
                    in.modLength = rd16(p + 30);
                }
                if (in.volume > 64) in.volume = 64;
            } else {
                in.length = (uint16_t)(rd16(p + 24) * 2);
                in.loopStart = rd16(p + 26);
                in.loopLength = (uint16_t)(rd16(p + 28) * 2);
                in.volume = rd16(p + 30);
                if (in.volume > 64) in.volume = 64;
                if (in.loopStart + in.loopLength > in.length)  // fix for Karate.bp
                    in.loopLength = (uint16_t)(in.length - in.loopStart);
            }
        }
        stepNum_ = l.stepNum;
        steps_.resize((size_t)stepNum_ * 4);
        for (size_t i = 0; i < steps_.size(); ++i) {
            const uint8_t* p = d + l.steps + 4 * i;
            steps_[i] = Step{rd16(p), (int8_t)p[2], (int8_t)p[3]};
        }
        tracks_.resize((size_t)l.trackNum * 16);
        for (size_t i = 0; i < tracks_.size(); ++i) {
            const uint8_t* p = d + l.tracks + 3 * i;
            tracks_[i] = Row{(int8_t)p[0], (uint8_t)(p[1] >> 4), (uint8_t)(p[1] & 0x0F), p[2]};
        }
        waveTables_.assign((const int8_t*)d + l.waves, (const int8_t*)d + l.samples);
        sampleData_.assign((const int8_t*)d + l.samples, (const int8_t*)d + l.end);
        size_t at = 0;
        for (Instrument& in : inst_) {
            if (in.synth || in.length == 0) continue;
            in.adr = sampleData_.data() + at;
            at += in.length;
        }
        waves_ = waveTables_;
        visited_.assign(stepNum_, false);
        format_ = v3_ ? "SOUNDMON V.3 (AMIGA)" : "SOUNDMON V.2 (AMIGA)";
        measure();
        return true;
    }

protected:
    void initSound() override {
        arpCount_ = 1;
        bpCount_ = 1;
        bpDelay_ = 6;
        bpRepCount_ = 0;
        vibIndex_ = 0;
        bpStep_ = 0;
        bpPatCount_ = 0;
        st_ = tr_ = 0;
        newPos_ = 0;
        posFlag_ = firstRepeat_ = false;
        for (Voice& v : voice_) v = Voice{};
        std::memset(synthBuffer_, 0, sizeof synthBuffer_);
        std::copy(waveTables_.begin(), waveTables_.end(), waves_.begin());  // no reallocation: channels point here
        std::fill(visited_.begin(), visited_.end(), false);
        visited_[0] = true;
    }

    void playTick() override {
        doEffects();
        doSynths();
        if (--bpCount_ == 0) {
            bpCount_ = bpDelay_;
            bpNext();
            for (int i = 0; i < 4; ++i) {
                Voice& v = voice_[i];
                if (!v.restart) continue;
                if (v.synthOffset >= 0) {  // copy the temporary synth data back
                    std::memcpy(waves_.data() + v.synthOffset, synthBuffer_[i], 32);
                    v.synthOffset = -1;
                }
                playIt(i);
            }
        }
    }

private:
    bool v3_ = false;
    Instrument inst_[15];
    uint16_t stepNum_ = 0;
    std::vector<Step> steps_;  // [step * 4 + voice]
    std::vector<Row> tracks_;  // [(track - 1) * 16 + row]
    std::vector<int8_t> waveTables_, waves_, sampleData_;
    std::vector<bool> visited_;

    Voice voice_[4];
    int8_t synthBuffer_[4][32] = {};
    uint16_t bpStep_ = 0;
    uint8_t vibIndex_ = 0, arpCount_ = 1, bpCount_ = 1, bpDelay_ = 6;
    int8_t st_ = 0, tr_ = 0;
    uint8_t bpPatCount_ = 0, bpRepCount_ = 0, newPos_ = 0;
    bool posFlag_ = false, firstRepeat_ = false;

    int8_t wave(size_t i) const { return i < waves_.size() ? waves_[i] : 0; }
    void setWave(size_t i, int8_t v) {
        if (i < waves_.size()) waves_[i] = v;
    }

    void bpNext() {
        for (int i = 0; i < 4; ++i) {
            Voice& cur = voice_[i];
            const Step& step = steps_[(size_t)bpStep_ * 4 + i];
            st_ = step.soundTranspose;
            tr_ = step.transpose;
            const Row& row = tracks_[(size_t)(step.track - 1) * 16 + bpPatCount_];
            int8_t note = row.note;
            if (note != 0) {
                cur.autoSlide = 0;
                cur.autoArp = 0;
                cur.vibrato = 0;
                if (row.optional != kTranspose || (row.optionalData & 0xF0) == 0) note = (int8_t)(note + tr_);
                cur.note = (uint8_t)note;
                cur.period = notePeriod(note);
                cur.restart = false;
                if (row.optional < kChangeInversion) {  // retrigger
                    cur.restart = true;
                    cur.useDefaultVolume = true;
                }
                uint8_t inst = row.instrument;
                if (inst == 0) inst = cur.instrument;
                if (inst != 0 && (row.optional != kTranspose || (row.optionalData & 0x0F) == 0)) {
                    inst = (uint8_t)(inst + st_);
                    if (inst < 1 || inst > 15) inst = (uint8_t)(inst - st_);
                }
                if (row.optional < kChangeInversion && (!cur.synthMode || cur.instrument != inst)) cur.instrument = inst;
            }
            doOptionals(i, row.optional, row.optionalData);
        }
        if (posFlag_) {  // a jump
            bpPatCount_ = 0;
            bpStep_ = newPos_;
        } else if (++bpPatCount_ == 16) {
            posFlag_ = true;
            bpPatCount_ = 0;
            if (++bpStep_ >= stepNum_) bpStep_ = 0;  // done with the module: repeat it
        }
        if (bpStep_ >= stepNum_) bpStep_ = 0;  // a jump past the last position
        if (posFlag_) {
            if (bpRepCount_ == 0 || firstRepeat_) {
                if (visited_[bpStep_]) endReached_ = true;
                visited_[bpStep_] = true;
            }
            posFlag_ = false;
        }
    }

    void playIt(int n) {
        Voice& cur = voice_[n];
        AmigaChannel& ch = ch_[n];
        cur.restart = false;
        ch.setAmigaPeriod(cur.period);
        if (cur.instrument == 0 || cur.instrument > 15) return;
        const Instrument& inst = inst_[cur.instrument - 1];
        if (inst.synth) {
            cur.synthMode = true;
            cur.egPtr = cur.lfoPtr = cur.adsrPtr = cur.modPtr = 0;
            cur.egCount = (uint8_t)(inst.egDelay + 1);
            cur.lfoCount = (uint8_t)(inst.lfoDelay + 1);
            cur.adsrCount = 1;  // start immediately
            cur.modCount = (uint8_t)(inst.modDelay + 1);
            cur.fxCount = (uint8_t)(inst.fxDelay + 1);
            cur.fxControl = inst.fxControl;
            cur.egControl = inst.egControl;
            cur.lfoControl = inst.lfoControl;
            cur.adsrControl = inst.adsrControl;
            cur.modControl = inst.modControl;
            cur.oldEgValue = 0;

            const uint32_t waveOffset = inst.waveTable * 64u;
            uint32_t start = waveOffset, len = inst.waveLength;
            if (clipRange(waves_.size(), start, len)) {
                ch.playSample(waves_.data(), start, len);
                ch.setLoop(start, len);
            } else {
                ch.mute();
            }
            if (cur.adsrControl != 0) {
                int tmp = (wave(inst.adsrTable * 64u) + 128) / 4;
                if (cur.useDefaultVolume) {
                    cur.volume = (uint8_t)inst.volume;
                    cur.useDefaultVolume = false;
                }
                tmp = tmp * cur.volume / 16;
                ch.setVolume((uint32_t)(tmp > 256 ? 256 : tmp));
            } else {
                const int tmp = (cur.useDefaultVolume ? inst.volume : cur.volume) * 4;
                ch.setVolume((uint32_t)(tmp > 256 ? 256 : tmp));
            }
            // the other effects work on a copy of the wave (only when all 32 bytes are there)
            if ((cur.egControl != 0 || cur.modControl != 0 || cur.fxControl != 0) && waveOffset + 32 <= waves_.size()) {
                cur.synthOffset = (int)waveOffset;
                std::memcpy(synthBuffer_[n], waves_.data() + waveOffset, 32);
            }
        } else {
            cur.synthMode = false;
            cur.lfoControl = 0;
            uint32_t start = 0, len = inst.length;
            if (!inst.adr || !clipRange(inst.length, start, len)) {
                ch.mute();
            } else {
                ch.playSample(inst.adr, 0, inst.length);
                uint32_t ls = inst.loopStart, ll = inst.loopLength;
                if (inst.loopLength > 2 && clipRange(inst.length, ls, ll)) ch.setLoop(ls, ll);
                const int tmp = (cur.useDefaultVolume ? inst.volume : cur.volume) * 4;
                ch.setVolume((uint32_t)(tmp > 256 ? 256 : tmp));
            }
        }
    }

    void doOptionals(int n, uint8_t optional, uint8_t data) {
        Voice& cur = voice_[n];
        switch (optional) {
        case kArpeggioOnce: cur.arpValue = data; break;
        case kSetVolume:
            if (data > 64) data = 64;
            cur.volume = data;
            cur.useDefaultVolume = false;
            if (!v3_ || !cur.synthMode) ch_[n].setAmigaVolume(data);
            break;
        case kSetSpeed:
            bpCount_ = data;
            bpDelay_ = data;
            break;
        case kFilter: break;  // no Amiga LED filter here
        case kPortUp:
            cur.period = (uint16_t)(cur.period - data);
            cur.arpValue = 0;
            break;
        case kPortDown:
            cur.period = (uint16_t)(cur.period + data);
            cur.arpValue = 0;
            break;
        case kVibrato:
            if (!v3_) {  // set repeat count
                if (bpRepCount_ == 0) {
                    bpRepCount_ = data;
                    if (bpRepCount_ != 0) firstRepeat_ = true;
                }
            } else {
                cur.vibrato = (int8_t)data;
            }
            break;
        case kJump:
            if (!v3_) {  // dbra repeat count
                if (bpRepCount_ != 0) {
                    firstRepeat_ = false;
                    if (--bpRepCount_ != 0) {
                        newPos_ = data;
                        posFlag_ = true;
                        // set the "end" mark when the block repeats a lot
                        if (bpRepCount_ == 254 || (bpRepCount_ >= 15 && newPos_ < bpStep_)) endReached_ = true;
                    }
                }
            } else {
                newPos_ = data;
                posFlag_ = true;
            }
            break;
        case kSetAutoSlide: cur.autoSlide = (int8_t)data; break;
        case kSetArpeggio:
            cur.autoArp = data;
            if (v3_) {
                cur.adsrPtr = 0;
                if (cur.adsrControl == 0) cur.adsrControl = 1;
            }
            break;
        case kChangeFx: cur.fxControl = data; break;
        case kChangeInversion:  // inversion <-> backward inversion, transform <-> backward transform
            cur.autoArp = data;
            cur.fxControl = (uint8_t)(cur.fxControl ^ 1);
            cur.adsrPtr = 0;
            if (cur.adsrControl == 0) cur.adsrControl = 1;
            break;
        case kResetAdsr:  // reset ADSR on a synth sound, but not EG, averaging, transform, ...
            cur.autoArp = data;
            cur.adsrPtr = 0;
            if (cur.adsrControl == 0) cur.adsrControl = 1;
            break;
        case kChangeNote: cur.autoArp = data; break;  // as above without the ADSR reset
        default: break;
        }
    }

    void doEffects() {
        arpCount_ = (uint8_t)((arpCount_ - 1) & 3);
        vibIndex_ = (uint8_t)((vibIndex_ + 1) & 7);
        for (int i = 0; i < 4; ++i) {
            Voice& cur = voice_[i];
            AmigaChannel& ch = ch_[i];
            cur.period = (uint16_t)(cur.period + cur.autoSlide);
            if (cur.vibrato != 0) ch.setAmigaPeriod((uint16_t)(cur.period + kVibratoTable[vibIndex_] / cur.vibrato));
            else ch.setAmigaPeriod(cur.period);
            if (cur.arpValue != 0 || cur.autoArp != 0) {
                int note = (int8_t)cur.note;
                if (arpCount_ == 0) note += ((cur.arpValue & 0xF0) >> 4) + ((cur.autoArp & 0xF0) >> 4);
                else if (arpCount_ == 1) note += (cur.arpValue & 0x0F) + (cur.autoArp & 0x0F);
                cur.restart = false;
                cur.period = notePeriod(note);
                ch.setAmigaPeriod(cur.period);
            }
        }
    }

    void doSynths() {
        for (int i = 0; i < 4; ++i) {
            Voice& cur = voice_[i];
            if (!cur.synthMode || cur.instrument == 0 || cur.instrument > 15) continue;
            const Instrument& inst = inst_[cur.instrument - 1];
            if (!inst.synth) continue;
            doAdsr(i, cur, inst);
            doLfo(i, cur, inst);
            if (cur.synthOffset >= 0) {
                doEg(i, cur, inst);
                doFx(i, cur, inst);
                doMod(cur, inst);
            }
        }
    }

    void doAdsr(int n, Voice& cur, const Instrument& inst) {
        if (cur.adsrControl == 0 || --cur.adsrCount != 0) return;
        cur.adsrCount = inst.adsrSpeed;
        const int value = (((wave(inst.adsrTable * 64u + cur.adsrPtr) + 128) / 4) * cur.volume) / 16;
        ch_[n].setVolume((uint32_t)(value > 256 ? 256 : value));
        if (++cur.adsrPtr == inst.adsrLength) {
            cur.adsrPtr = 0;
            if (cur.adsrControl == 1) cur.adsrControl = 0;  // only once
        }
    }

    void doLfo(int n, Voice& cur, const Instrument& inst) {
        if (cur.lfoControl == 0 || --cur.lfoCount != 0) return;
        cur.lfoCount = inst.lfoSpeed;
        int value = wave(inst.lfoTable * 64u + cur.lfoPtr);
        if (inst.lfoDepth != 0) value /= inst.lfoDepth;
        ch_[n].setAmigaPeriod((uint16_t)(cur.period + value));
        if (++cur.lfoPtr == inst.lfoLength) {
            cur.lfoPtr = 0;
            if (cur.lfoControl == 1) cur.lfoControl = 0;
        }
    }

    void doEg(int n, Voice& cur, const Instrument& inst) {
        if (cur.egControl == 0 || --cur.egCount != 0) return;
        cur.egCount = inst.egSpeed;
        int value = cur.oldEgValue;
        cur.oldEgValue = (uint8_t)((wave(inst.egTable * 64u + cur.egPtr) + 128) / 8);  // 0..31
        if (cur.oldEgValue != value) {
            const int8_t* source = synthBuffer_[n];
            int src = value, dst = cur.synthOffset + value;
            if (cur.oldEgValue < value) {
                for (int j = value - cur.oldEgValue; j > 0; --j) setWave((size_t)--dst, source[--src]);
            } else {
                for (int j = cur.oldEgValue - value; j > 0; --j) setWave((size_t)dst++, (int8_t)-source[src++]);
            }
        }
        if (++cur.egPtr == inst.egLength) {
            cur.egPtr = 0;
            if (cur.egControl == 1) cur.egControl = 0;
        }
    }

    void doFx(int n, Voice& cur, const Instrument& inst) {
        switch (cur.fxControl) {
        case 1:
            if (--cur.fxCount == 0) {
                cur.fxCount = inst.fxSpeed;
                averaging(n);
            }
            break;
        case 2: transform(n, (int8_t)inst.fxSpeed, synthBuffer_[n] + 31, -1); break;
        case 3:
        case 5: transform(n, (int8_t)inst.fxSpeed, synthBuffer_[n], 1); break;
        case 4: transform4(n, (int8_t)inst.fxSpeed); break;
        case 6:
            if (--cur.fxCount == 0) {
                cur.fxControl = 0;
                cur.fxCount = 1;
                for (int j = 0; j < 32; ++j) setWave((size_t)cur.synthOffset + j, wave((size_t)cur.synthOffset + 64 + j));
            }
            break;
        default: break;
        }
    }

    void doMod(Voice& cur, const Instrument& inst) {
        if (cur.modControl == 0 || --cur.modCount != 0) return;
        cur.modCount = inst.modSpeed;
        setWave((size_t)cur.synthOffset + 32, wave(inst.modTable * 64u + cur.modPtr));
        if (++cur.modPtr == inst.modLength) {
            cur.modPtr = 0;
            if (cur.modControl == 1) cur.modControl = 0;
        }
    }

    void averaging(int n) {
        size_t at = (size_t)voice_[n].synthOffset;
        int8_t last = wave(at);
        for (int i = 0; i < 32 - 1; ++i) {
            last = (int8_t)((last + wave(at + 1)) / 2);
            setWave(at++, last);
        }
    }

    // Transform 2 (source read backwards) and 3 / 5 (forwards): moves the wave towards the saved copy.
    void transform(int n, int8_t delta, const int8_t* src, int dir) {
        size_t dst = (size_t)voice_[n].synthOffset;
        for (int i = 0; i < 32; ++i, src += dir, ++dst) {
            const int8_t s = *src, v = wave(dst);
            if (s < v) setWave(dst, (int8_t)(v - delta));
            else if (s > v) setWave(dst, (int8_t)(v + delta));
        }
    }

    void transform4(int n, int8_t delta) {  // towards the next wave table
        size_t src = (size_t)voice_[n].synthOffset + 64, dst = (size_t)voice_[n].synthOffset;
        for (int i = 0; i < 32; ++i, ++src, ++dst) {
            const int8_t s = wave(src), v = wave(dst);
            if (s < v) setWave(dst, (int8_t)(v - delta));
            else if (s > v) setWave(dst, (int8_t)(v + delta));
        }
    }
};
}  // namespace

bool isSoundMon(const uint8_t* d, size_t n) {
    Layout l;
    return parseLayout(d, n, l);
}

std::unique_ptr<AmigaSynthPlayer> openSoundMon(const uint8_t* d, size_t n) {
    if (!isSoundMon(d, n)) return nullptr;
    std::unique_ptr<SoundMon> p(new SoundMon);
    if (!p->load(d, n)) return nullptr;
    return p;
}
