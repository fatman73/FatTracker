// ProTracker MOD export. Whatever MOD cannot hold is converted when possible and reported.
#include <cmath>
#include <cstring>

#include "core/tables.h"
#include "formats/writers.h"

namespace {
void warn(ModExport& out, const char* msg) {
    for (const char* w : out.warnings)
        if (w == msg) return;
    out.warnings.push_back(msg);
}

void put16be(std::vector<uint8_t>& b, int v) {
    b.push_back((uint8_t)(v >> 8));
    b.push_back((uint8_t)v);
}
}  // namespace

ModExport exportMod(const Song& s) {
    ModExport out;
    std::vector<uint8_t>& b = out.data;
    const int ch = s.numChannels;
    // global sample index -> MOD sample number (1..31)
    const bool inst = !s.instruments.empty();
    if (s.samples.size() > 31) warn(out, "ONLY THE FIRST 31 SAMPLES ARE EXPORTED");
    const int nSamples = s.samples.size() > 31 ? 31 : (int)s.samples.size();

    char title[20] = {};
    std::memcpy(title, s.name, 20);
    b.insert(b.end(), title, title + 20);
    std::vector<std::vector<int8_t>> pcm(nSamples);
    for (int i = 0; i < 31; ++i) {
        Sample sm = i < nSamples ? s.samples[i] : Sample{};
        char name[22] = {};
        std::memcpy(name, sm.name, 22);
        b.insert(b.end(), name, name + 22);
        uint32_t len = sm.length;
        if (len > 131070) {
            warn(out, "SAMPLES LONGER THAN 128 KB ARE CUT");
            len = 131070;
        }
        len &= ~1u;
        if (i < nSamples) {
            std::vector<int8_t>& d = pcm[i];
            d.resize(len);
            if (sm.is16) {
                warn(out, "16-BIT SAMPLES CONVERTED TO 8 BIT");
                for (uint32_t k = 0; k < len; ++k) d[k] = (int8_t)(sm.data16[k] >> 8);
            } else {
                for (uint32_t k = 0; k < len; ++k) d[k] = sm.data8[k];
            }
            if (sm.relativeNote) warn(out, "SAMPLE RELATIVE NOTES ARE LOST");
            if (sm.loop == LoopType::PingPong) warn(out, "PING-PONG LOOPS BECOME FORWARD LOOPS");
        }
        put16be(b, (int)(len / 2));
        int ft = sm.finetune / 16;
        ft = ft < -8 ? -8 : (ft > 7 ? 7 : ft);
        b.push_back((uint8_t)(ft & 15));
        b.push_back(sm.volume > 64 ? 64 : sm.volume);
        uint32_t ls = sm.loop == LoopType::None ? 0 : (sm.loopStart & ~1u);
        uint32_t ll = sm.loop == LoopType::None ? 2 : (sm.loopLength & ~1u);
        if (ls >= len) ls = 0, ll = 2;
        if (ls + ll > len) ll = len - ls;
        if (ll < 2) ll = 2;
        put16be(b, (int)(ls / 2));
        put16be(b, (int)(ll / 2));
    }
    const int songLen = s.order.size() > 128 ? 128 : (int)s.order.size();
    if (s.order.size() > 128) warn(out, "ONLY 128 SONG POSITIONS FIT IN A MOD");
    // used patterns only, renumbered in order of first use (MOD pattern numbers must be dense)
    int remap[256];
    for (int& v : remap) v = -1;
    std::vector<int> used;  // new index -> old pattern
    for (int i = 0; i < songLen; ++i) {
        const int o = s.order[i] < s.patterns.size() ? s.order[i] : 0;
        if (remap[o] < 0) {
            if (used.size() >= 100) {
                warn(out, "ONLY 100 PATTERNS FIT IN A MOD");
                continue;
            }
            remap[o] = (int)used.size();
            used.push_back(o);
        }
    }
    if (used.empty()) used.push_back(0), remap[0] = 0;
    if (s.patterns.size() > used.size()) warn(out, "UNUSED PATTERNS ARE NOT EXPORTED");
    if (s.restartPos) warn(out, "THE RESTART POSITION IS LOST");
    for (const Instrument& in : s.instruments)
        if ((in.volEnv.flags | in.panEnv.flags) & ENV_ON) warn(out, "INSTRUMENT ENVELOPES ARE LOST");
    b.push_back((uint8_t)songLen);
    b.push_back(0x7F);
    for (int i = 0; i < 128; ++i) {
        const int o = i < songLen && s.order[i] < s.patterns.size() ? s.order[i] : 0;
        b.push_back((uint8_t)(i < songLen && remap[o] >= 0 ? remap[o] : 0));
    }
    char sig[5] = "M.K.";
    if (ch == 4 && used.size() > 64) std::memcpy(sig, "M!K!", 4);  // ProTracker's >64 patterns tag
    if (ch != 4) {
        if (ch < 10)
            sig[0] = (char)('0' + ch), sig[1] = 'C', sig[2] = 'H', sig[3] = 'N';
        else
            sig[0] = (char)('0' + ch / 10), sig[1] = (char)('0' + ch % 10), sig[2] = 'C', sig[3] = 'H';
    }
    b.insert(b.end(), sig, sig + 4);

    // initial tempo: MOD has no header field, so put Fxx commands on the first row
    int tempoFx[2] = {-1, -1};  // params still to place: speed, bpm
    if (s.initialSpeed != 6) tempoFx[0] = s.initialSpeed;
    if (s.initialBpm != 125) tempoFx[1] = s.initialBpm;

    for (size_t np = 0; np < used.size(); ++np) {
        const Pattern* pat = &s.patterns[used[np]];
        if (pat->rows != 64) warn(out, "PATTERNS ARE CUT OR PADDED TO 64 ROWS");
        for (int r = 0; r < 64; ++r)
            for (int c = 0; c < ch; ++c) {
                Cell cell = (r < pat->rows && c < pat->channels) ? pat->at(r, c) : Cell{};
                int sample = 0, period = 0;
                if (cell.note >= 1 && cell.note <= 96) {
                    int n = cell.note;
                    int idx = xmNoteToPt(n);
                    if (idx < 0) {
                        warn(out, "NOTES OUTSIDE C-1..B-3 ARE MOVED BY OCTAVES");
                        while (idx < 0 && n < 37) n += 12, idx = xmNoteToPt(n);
                        while (idx < 0 && n > 72) n -= 12, idx = xmNoteToPt(n);
                    }
                    period = idx >= 0 ? kPtPeriods[0][idx] : 0;
                } else if (cell.note == NOTE_OFF) {
                    warn(out, "KEY-OFF NOTES ARE DROPPED");
                }
                if (cell.instr) {
                    int g = cell.instr - 1;
                    if (inst) {
                        g = -1;
                        if (cell.instr <= s.instruments.size() && cell.note >= 1 && cell.note <= 96)
                            g = s.instruments[cell.instr - 1].sampleMap[cell.note - 1];
                    }
                    sample = (g >= 0 && g < nSamples) ? g + 1 : 0;
                    if (g >= nSamples) warn(out, "ONLY THE FIRST 31 SAMPLES ARE EXPORTED");
                }
                int fx = cell.fx, param = cell.param;
                if (fx > 15) {
                    warn(out, "XM-ONLY EFFECTS (G-Z) ARE DROPPED");
                    fx = param = 0;
                }
                if (cell.vol) {
                    if (cell.vol >= 0x10 && cell.vol <= 0x50 && fx == 0 && param == 0) {
                        fx = 0xC;
                        param = cell.vol - 0x10;
                    } else {
                        warn(out, "VOLUME COLUMN LOST WHERE AN EFFECT IS USED");
                    }
                }
                if (np == 0 && r == 0 && fx == 0 && param == 0) {
                    for (int& t : tempoFx)
                        if (t >= 0) {
                            fx = 0xF;
                            param = t;
                            t = -1;
                            break;
                        }
                }
                b.push_back((uint8_t)((sample & 0xF0) | ((period >> 8) & 0x0F)));
                b.push_back((uint8_t)(period & 0xFF));
                b.push_back((uint8_t)(((sample & 0x0F) << 4) | (fx & 0x0F)));
                b.push_back((uint8_t)param);
            }
    }
    if (tempoFx[0] >= 0 || tempoFx[1] >= 0) warn(out, "THE INITIAL TEMPO COULD NOT BE STORED");    for (int i = 0; i < nSamples; ++i) b.insert(b.end(), (const uint8_t*)pcm[i].data(), (const uint8_t*)pcm[i].data() + pcm[i].size());
    if (s.numChannels > 32) warn(out, "TOO MANY CHANNELS");
    return out;
}
