// JamCracker Pro ("BeEp") replayer, ported from NostalgicPlayer's JamCrackerWorker.cs
// (Thomas Neumann, MIT licence) onto AmigaSynthPlayer. Index checks added where the C# code relied
// on exceptions (instrument number, period index, AM wave offsets).
#include <cstring>
#include <vector>

#include "core/amiga_synth.h"
#include "core/amiga_formats.h"

namespace {
const uint16_t kPeriods[] = {1019, 962, 908, 857, 809, 763, 720, 680, 642, 606, 572, 540, 509, 481, 454, 428, 404,
                             381,  360, 340, 321, 303, 286, 270, 254, 240, 227, 214, 202, 190, 180, 170, 160, 151,
                             143,  135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135};
constexpr int kPeriodCount = (int)(sizeof kPeriods / sizeof kPeriods[0]);
uint16_t period(int i) { return kPeriods[i < 0 ? 0 : (i >= kPeriodCount ? kPeriodCount - 1 : i)]; }

struct Inst {
    uint8_t flags = 0;
    uint32_t size = 0;
    std::vector<int8_t> data;
};
struct Note {
    uint8_t period, speed, arpeggio, vibrato, phase, volume, porta;
    int8_t instr;
};
struct Patt {
    uint16_t size = 0;
    std::vector<Note> notes;
};
struct Voice {
    uint16_t waveOffset = 0, dmacon = 0;
    int insNum = -1;
    uint16_t insLen = 0;
    const int8_t* insAddress = nullptr;   // what plays (the sample, or waveBuffer for AM)
    const Inst* realIns = nullptr;        // the instrument behind it
    int8_t waveBuffer[0x40] = {};
    int perIndex = 0;
    uint16_t pers[3] = {};
    int16_t por = 0, deltaPor = 0, porLevel = 0, vib = 0, deltaVib = 0, vol = 0, deltaVol = 0;
    uint16_t volLevel = 0, phase = 0;
    int16_t deltaPhase = 0;
    uint8_t vibCnt = 0, vibMax = 0, flags = 0;
};

uint16_t rd16(const uint8_t*& p, const uint8_t* end) {
    if (end - p < 2) {
        p = end;
        return 0;
    }
    const uint16_t v = (uint16_t)(p[0] << 8 | p[1]);
    p += 2;
    return v;
}
uint32_t rd32(const uint8_t*& p, const uint8_t* end) {
    const uint32_t hi = rd16(p, end);
    return hi << 16 | rd16(p, end);
}

class JamCracker : public AmigaSynthPlayer {
public:
    bool load(const uint8_t* d, size_t n) {
        const uint8_t *p = d + 4, *end = d + n;
        const int samples = rd16(p, end);
        if (samples > 255 || (size_t)(end - p) < (size_t)samples * 40) return false;
        inst_.resize(samples);
        for (Inst& in : inst_) {
            p += 31;
            in.flags = *p++;
            in.size = rd32(p, end);
            rd32(p, end);
        }
        const int patterns = rd16(p, end);
        if ((size_t)(end - p) < (size_t)patterns * 6) return false;
        patt_.resize(patterns);
        for (Patt& pt : patt_) {
            pt.size = rd16(p, end);
            rd32(p, end);
        }
        const int len = rd16(p, end);
        if (len == 0 || (size_t)(end - p) < (size_t)len * 2) return false;
        song_.resize(len);
        for (auto& s : song_) {
            s = rd16(p, end);
            if (s >= patterns) return false;
        }
        for (Patt& pt : patt_) {
            if (!pt.size || (size_t)(end - p) < (size_t)pt.size * 4 * 8) return false;
            pt.notes.resize((size_t)pt.size * 4);
            for (Note& nt : pt.notes) {
                nt.period = p[0];
                nt.instr = (int8_t)p[1];
                nt.speed = p[2];
                nt.arpeggio = p[3];
                nt.vibrato = p[4];
                nt.phase = p[5];
                nt.volume = p[6];
                nt.porta = p[7];
                p += 8;
            }
        }
        for (size_t i = 0; i < inst_.size(); ++i) {  // the last sample may be cut short
            Inst& in = inst_[i];
            if (!in.size) continue;
            const size_t take = (size_t)(end - p) < in.size ? (size_t)(end - p) : in.size;
            if (take < in.size && i + 1 != inst_.size()) return false;
            in.data.assign(p, p + take);
            in.data.resize(in.size, 0);
            p += take;
        }
        format_ = "JAMCRACKER (AMIGA)";
        measure();
        return true;
    }

protected:
    void initSound() override {
        songPos_ = 0;
        noteCnt_ = patt_[song_[0]].size;
        patt_index_ = song_[0];
        addrIndex_ = 0;
        wait_ = 6;
        waitCnt_ = 1;
        visited_.assign(song_.size(), false);
        visited_[0] = true;
        uint16_t waveOff = 0x80;
        for (int i = 0; i < 4; ++i) {
            v_[i] = Voice{};
            v_[i].waveOffset = waveOff;
            v_[i].dmacon = (uint16_t)(1 << i);
            v_[i].pers[0] = 1019;
            v_[i].volLevel = 0x40;
            waveOff += 0x40;
        }
    }

    void playTick() override {
        if (--waitCnt_ == 0) {
            newNote();
            waitCnt_ = wait_;
        }
        for (int i = 0; i < 4; ++i) setChannel(v_[i], ch_[i]);
    }

private:
    std::vector<Inst> inst_;
    std::vector<Patt> patt_;
    std::vector<uint16_t> song_;
    std::vector<bool> visited_;
    Voice v_[4];
    int songPos_ = 0, patt_index_ = 0, addrIndex_ = 0;
    uint16_t noteCnt_ = 0, tmpDmacon_ = 0;
    uint8_t wait_ = 6, waitCnt_ = 1;

    void newNote() {
        const int pat = patt_index_, idx = addrIndex_;
        addrIndex_ += 4;
        if (--noteCnt_ == 0) {
            if (++songPos_ >= (int)song_.size()) songPos_ = 0;
            if (visited_[songPos_]) endReached_ = true;
            visited_[songPos_] = true;
            patt_index_ = song_[songPos_];
            noteCnt_ = patt_[patt_index_].size;
            addrIndex_ = 0;
        }
        tmpDmacon_ = 0;
        const std::vector<Note>& notes = patt_[pat].notes;
        for (int i = 0; i < 4; ++i)
            if ((size_t)(idx + i) < notes.size()) nwNote(notes[idx + i], v_[i]);
        for (int i = 0; i < 4; ++i) setVoice(v_[i], ch_[i]);
    }

    void clearIns(Voice& v) {
        v.insAddress = nullptr;
        v.realIns = nullptr;
        v.insLen = 0;
        v.insNum = -1;
        v.flags = 0;
    }

    void nwNote(const Note& a, Voice& v) {
        int perIndex;
        if (a.period != 0) {
            perIndex = a.period - 1;
            if (a.speed & 64) {
                v.porLevel = (int16_t)period(perIndex);
            } else {
                tmpDmacon_ |= v.dmacon;
                v.perIndex = perIndex;
                v.pers[0] = v.pers[1] = v.pers[2] = period(perIndex);
                v.por = 0;
                if (a.instr < 0 || a.instr >= (int)inst_.size() || inst_[a.instr].data.empty()) {
                    clearIns(v);
                } else {
                    const Inst& in = inst_[a.instr];
                    if (!(in.flags & 2)) {
                        v.insAddress = in.data.data();
                        v.realIns = &in;
                        v.insLen = (uint16_t)(in.size / 2 > 0xFFFF ? 0xFFFF : in.size / 2);
                    } else {  // AM synthesis: a 64 byte wave taken from the instrument
                        v.realIns = &in;
                        for (int k = 0; k < 0x40; ++k) {
                            const size_t at = (size_t)v.waveOffset + k;
                            v.waveBuffer[k] = at < in.data.size() ? in.data[at] : 0;
                        }
                        v.insAddress = v.waveBuffer;
                        v.insLen = 0x20;
                    }
                    v.flags = in.flags;
                    v.vol = (int16_t)v.volLevel;
                    v.insNum = a.instr;
                }
            }
        }
        if (a.speed & 15) wait_ = (uint8_t)(a.speed & 15);
        perIndex = v.perIndex;
        if (a.arpeggio) {
            if (a.arpeggio == 255) {
                v.pers[0] = v.pers[1] = v.pers[2] = period(perIndex);
            } else {
                v.pers[2] = period(perIndex + (a.arpeggio & 15));
                v.pers[1] = period(perIndex + (a.arpeggio >> 4));
                v.pers[0] = period(perIndex);
            }
        }
        if (a.vibrato) {
            v.vib = 0;
            if (a.vibrato == 255) {
                v.deltaVib = 0;
                v.vibCnt = 0;
            } else {
                v.deltaVib = (int16_t)(a.vibrato & 15);
                v.vibMax = (uint8_t)(a.vibrato >> 4);
                v.vibCnt = (uint8_t)(a.vibrato >> 5);
            }
        }
        if (a.phase) {
            v.phase = 0;
            v.deltaPhase = a.phase == 255 ? -1 : (int16_t)(a.phase & 15);
        }
        int16_t t = a.volume;
        if (t == 0) {
            if (a.speed & 128) {
                v.vol = t;
                v.volLevel = (uint16_t)t;
                v.deltaVol = 0;
            }
        } else if (t == 255) {
            v.deltaVol = 0;
        } else if (a.speed & 128) {
            v.vol = t;
            v.volLevel = (uint16_t)t;
            v.deltaVol = 0;
        } else {
            t &= 0x7f;
            if (a.volume & 128) t = (int16_t)-t;
            v.deltaVol = t;
        }
        if ((t = a.porta) != 0) {
            v.por = 0;
            if (t == 255) {
                v.deltaPor = 0;
            } else {
                if (a.speed & 64) {
                    if (v.porLevel <= (int16_t)v.pers[0]) t = (int16_t)-t;
                } else {
                    t &= 0x7f;
                    if (!(a.porta & 128)) {
                        t = (int16_t)-t;
                        v.porLevel = 135;
                    } else {
                        v.porLevel = 1019;
                    }
                }
                v.deltaPor = t;
            }
        }
    }

    void setVoice(Voice& v, AmigaChannel& c) {
        if (!(tmpDmacon_ & v.dmacon)) return;
        if (!v.insAddress) c.mute();
        else {
            c.playSample(v.insAddress, 0, (uint32_t)v.insLen * 2);
            c.setAmigaPeriod(v.pers[0]);
        }
        if (!(v.flags & 1)) {  // one shot: nothing to repeat
            v.insAddress = nullptr;
            v.realIns = nullptr;
            v.insLen = 0;
            v.insNum = -1;
        }
        if (v.insAddress) c.setLoop(0, (uint32_t)v.insLen * 2);
    }

    void rotate(Voice& v) {
        const uint16_t t = v.pers[0];
        v.pers[0] = v.pers[1];
        v.pers[1] = v.pers[2];
        v.pers[2] = t;
    }

    void setChannel(Voice& v, AmigaChannel& c) {
        for (int k = 0; k < 3 && v.pers[0] == 0; ++k) rotate(v);
        int per = (int16_t)(v.pers[0] + v.por);
        if (v.por < 0) {
            if (per < v.porLevel) per = v.porLevel;
        } else if (v.por != 0 && per > v.porLevel) {
            per = v.porLevel;
        }
        per += v.vib;
        per = per < 135 ? 135 : (per > 1019 ? 1019 : per);
        c.setAmigaPeriod((uint32_t)per);
        rotate(v);
        v.por = (int16_t)(v.por + v.deltaPor);
        v.por = v.por < -1019 ? -1019 : (v.por > 1019 ? 1019 : v.por);
        if (v.vibCnt) {
            v.vib = (int16_t)(v.vib + v.deltaVib);
            if (--v.vibCnt == 0) {
                v.deltaVib = (int16_t)-v.deltaVib;
                v.vibCnt = v.vibMax;
            }
        }
        c.setAmigaVolume((uint32_t)(v.vol < 0 ? 0 : v.vol));
        v.vol = (int16_t)(v.vol + v.deltaVol);
        v.vol = v.vol < 0 ? 0 : (v.vol > 64 ? 64 : v.vol);
        if ((v.flags & 1) && v.deltaPhase != 0 && v.realIns && v.insAddress == v.waveBuffer) {  // AM phase
            if (v.deltaPhase < 0) v.deltaPhase = 0;
            const std::vector<int8_t>& wave = v.realIns->data;
            int wavePhase = v.phase / 4;
            for (int i = 0; i < 64; ++i, ++wavePhase) {
                const int a = (size_t)i < wave.size() ? wave[i] : 0;
                const int b = (size_t)wavePhase < wave.size() ? wave[wavePhase] : 0;
                v.waveBuffer[i] = (int8_t)((a + b) >> 1);
            }
            v.phase = (uint16_t)(v.phase + v.deltaPhase);
            if (v.phase >= 256) v.phase -= 256;
        }
    }
};
}  // namespace

std::unique_ptr<AmigaSynthPlayer> openJamCracker(const uint8_t* d, size_t n) {
    if (n < 6 || std::memcmp(d, "BeEp", 4) != 0) return nullptr;
    std::unique_ptr<JamCracker> p(new JamCracker);
    if (!p->load(d, n)) return nullptr;
    return p;
}
