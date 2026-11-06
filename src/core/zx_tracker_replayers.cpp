// ZX Spectrum AY tracker replayers for ZxTrackerPlayer: Sound Tracker compiled (STC), Pro Tracker 2 (PT2),
// ASC Sound Master (ASC), SQ-Tracker (SQT), and the Sound Tracker 1.1 source format converted to STC.
//
// Credits and licences:
//  - The players are Sergey Bulba's (AY-3-8910/12 Emulator "Ay_Emul", (c) 1999-2004 S.V. Bulba,
//    https://bulba.untergrund.net/), as ported to C++ in ayfly by Deryabin Andrew and contributors
//    (https://github.com/l29ah/ayfly, src/libayfly/players/STCPlay.h, PT2Play.h, ASCPlay.h, SQTPlay.h,
//    PT1Play.h for the PT2 note table). ayfly is free software under the GNU General Public License, version 2
//    or (at your option) any later version; this file follows those players and is distributed under the same
//    terms as part of FatTracker (GPL).
//  - The structure checks follow ayfly's *_Detect functions and the format descriptions of ZXTune by Vitamin
//    (https://github.com/vitamin-caig/zxtune, src/formats/chiptune/aym/*.cpp, GNU LGPL v3), which also
//    documents the Sound Tracker 1.1 source layout used for the conversion below. No ZXTune code is copied.
//
// Differences from ayfly:
//  - every read of the module goes through a bounds checked accessor (outside the file reads as 0), every
//    search through a table is bounded, a row of pattern data is capped;
//  - PT2's tone slide and its target distance are 16 bit as in Bulba's original (ayfly keeps them in a byte);
//  - ASC's portamento with a zero step count (a division by zero in the original) sets the slide at once;
//  - SQ-Tracker channel state starts at zero (ayfly leaves most of it uninitialised);
//  - a sample, ornament or pattern number missing from its table leaves the channel as it was.
#include "core/zx_tracker_replayers.h"

#include <cstdlib>
#include <cstring>

namespace zxt {
namespace {
constexpr int kRowGuard = 64;  // commands read for one row of one channel before giving up on it

// Sound Tracker (ayfly STCPlay.h)
const uint16_t kStTable[96] = {
    0xef8, 0xe10, 0xd60, 0xc80, 0xbd8, 0xb28, 0xa88, 0x9f0, 0x960, 0x8e0, 0x858, 0x7e0, 0x77c, 0x708, 0x6b0, 0x640,
    0x5ec, 0x594, 0x544, 0x4f8, 0x4b0, 0x470, 0x42c, 0x3f0, 0x3be, 0x384, 0x358, 0x320, 0x2f6, 0x2ca, 0x2a2, 0x27c,
    0x258, 0x238, 0x216, 0x1f8, 0x1df, 0x1c2, 0x1ac, 0x190, 0x17b, 0x165, 0x151, 0x13e, 0x12c, 0x11c, 0x10b, 0xfc,
    0xef,  0xe1,  0xd6,  0xc8,  0xbd,  0xb2,  0xa8,  0x9f,  0x96,  0x8e,  0x85,  0x7e,  0x77,  0x70,  0x6b,  0x64,
    0x5e,  0x59,  0x54,  0x4f,  0x4b,  0x47,  0x42,  0x3f,  0x3b,  0x38,  0x35,  0x32,  0x2f,  0x2c,  0x2a,  0x27,
    0x25,  0x23,  0x21,  0x1f,  0x1d,  0x1c,  0x1a,  0x19,  0x17,  0x16,  0x15,  0x13,  0x12,  0x11,  0x10,  0xf};
// Pro Tracker 2 (ayfly PT1Play.h PT2_Table)
const uint16_t kPt2Table[96] = {
    0xef8, 0xe10, 0xd60, 0xc80, 0xbd8, 0xb28, 0xa88, 0x9f0, 0x960, 0x8e0, 0x858, 0x7e0, 0x77c, 0x708, 0x6b0, 0x640,
    0x5ec, 0x594, 0x544, 0x4f8, 0x4b0, 0x470, 0x42c, 0x3fd, 0x3be, 0x384, 0x358, 0x320, 0x2f6, 0x2ca, 0x2a2, 0x27c,
    0x258, 0x238, 0x216, 0x1f8, 0x1df, 0x1c2, 0x1ac, 0x190, 0x17b, 0x165, 0x151, 0x13e, 0x12c, 0x11c, 0x10a, 0xfc,
    0xef,  0xe1,  0xd6,  0xc8,  0xbd,  0xb2,  0xa8,  0x9f,  0x96,  0x8e,  0x85,  0x7e,  0x77,  0x70,  0x6b,  0x64,
    0x5e,  0x59,  0x54,  0x4f,  0x4b,  0x47,  0x42,  0x3f,  0x3b,  0x38,  0x35,  0x32,  0x2f,  0x2c,  0x2a,  0x27,
    0x25,  0x23,  0x21,  0x1f,  0x1d,  0x1c,  0x1a,  0x19,  0x17,  0x16,  0x15,  0x13,  0x12,  0x11,  0x10,  0xf};
// ASC Sound Master (ayfly ASCPlay.h): 86 notes
const uint16_t kAscTable[86] = {
    0xedc, 0xe07, 0xd3e, 0xc80, 0xbcc, 0xb22, 0xa82, 0x9ec, 0x95c, 0x8d6, 0x858, 0x7e0, 0x76e, 0x704, 0x69f, 0x640,
    0x5e6, 0x591, 0x541, 0x4f6, 0x4ae, 0x46b, 0x42c, 0x3f0, 0x3b7, 0x382, 0x34f, 0x320, 0x2f3, 0x2c8, 0x2a1, 0x27b,
    0x257, 0x236, 0x216, 0x1f8, 0x1dc, 0x1c1, 0x1a8, 0x190, 0x179, 0x164, 0x150, 0x13d, 0x12c, 0x11b, 0x10b, 0xfc,
    0xee,  0xe0,  0xd4,  0xc8,  0xbd,  0xb2,  0xa8,  0x9f,  0x96,  0x8d,  0x85,  0x7e,  0x77,  0x70,  0x6a,  0x64,
    0x5e,  0x59,  0x54,  0x50,  0x4b,  0x47,  0x43,  0x3f,  0x3c,  0x38,  0x35,  0x32,  0x2f,  0x2d,  0x2a,  0x28,
    0x26,  0x24,  0x22,  0x20,  0x1e,  0x1c};
// SQ-Tracker (ayfly SQTPlay.h)
const uint16_t kSqtTable[96] = {
    0xd5d, 0xc9c, 0xbe7, 0xb3c, 0xa9b, 0xa02, 0x973, 0x8eb, 0x86b, 0x7f2, 0x780, 0x714, 0x6ae, 0x64e, 0x5f4, 0x59e,
    0x54f, 0x501, 0x4b9, 0x475, 0x435, 0x3f9, 0x3c0, 0x38a, 0x357, 0x327, 0x2fa, 0x2cf, 0x2a7, 0x281, 0x25d, 0x23b,
    0x21b, 0x1fc, 0x1e0, 0x1c5, 0x1ac, 0x194, 0x17d, 0x168, 0x153, 0x140, 0x12e, 0x11d, 0x10d, 0xfe,  0xf0,  0xe2,
    0xd6,  0xca,  0xbe,  0xb4,  0xaa,  0xa0,  0x97,  0x8f,  0x87,  0x7f,  0x78,  0x71,  0x6b,  0x65,  0x5f,  0x5a,
    0x55,  0x50,  0x4c,  0x47,  0x43,  0x40,  0x3c,  0x39,  0x35,  0x32,  0x30,  0x2d,  0x2a,  0x28,  0x26,  0x24,
    0x22,  0x20,  0x1e,  0x1c,  0x1b,  0x19,  0x18,  0x16,  0x15,  0x14,  0x13,  0x12,  0x11,  0x10,  0xf,   0xe};

// Bounded little endian word of the raw file (-1 outside it)
long w16(const uint8_t* d, size_t n, size_t a) { return a + 1 < n ? (long)(d[a] | d[a + 1] << 8) : -1; }

// Printable text of a fixed length field, trimmed
std::string text(const Mem& m, uint32_t from, size_t len) {
    std::string s;
    for (size_t k = 0; k < len; ++k) {
        const uint8_t c = m.at(from + (uint32_t)k);
        s += (c >= 32 && c < 127) ? (char)c : ' ';
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    size_t b = 0;
    while (b < s.size() && s[b] == ' ') ++b;
    return s.substr(b);
}

void writeTones(Ay& ay, const uint16_t* ton, const uint8_t* amp) {  // channels A, B, C
    for (int c = 0; c < 3; ++c) {
        ay.set(c * 2, (uint8_t)(ton[c] & 0xFF));
        ay.set(c * 2 + 1, (uint8_t)((ton[c] >> 8) & 15));
        ay.set(8 + c, amp[c]);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Sound Tracker compiled (STC)
// Header: delay, positions, ornaments, patterns pointers, 18 byte name, size; samples from 27 (99 bytes each:
// number, 32 lines of 3 bytes, loop, loop length); ornaments 33 bytes (number + 32 offsets); pattern table of
// 7 byte entries (number, three channel pointers) ending with 0xFF.

class Stc : public Replayer {
public:
    explicit Stc(const uint8_t* d, size_t n) { m.d.assign(d, d + n); }
    void reset(Ay& ay) override {
        ay = Ay{};
        for (Ch& c : ch) {
            c = Ch{};
            c.orn = (uint16_t)(ornBase() + 1);
        }
        position = 0;
        delayCounter = 1;
        enter(0);
    }
    bool frame(Ay& ay) override {
        ay.envShape = -1;
        bool wrapped = false;
        if (--delayCounter == 0) {
            delayCounter = m.at(0);
            Ch& a = ch[0];
            if (--a.skipCounter < 0) {
                if (m.at(a.addr) == 0xFF) {
                    if (position == m.at(posBase())) {
                        position = 0;
                        wrapped = true;
                    } else {
                        position++;
                    }
                    enter(position);
                }
                interpret(ay, a);
            }
            for (int c = 1; c < 3; ++c)
                if (--ch[c].skipCounter < 0) interpret(ay, ch[c]);
        }
        uint8_t mixer = 0;
        for (Ch& c : ch) registers(ay, c, mixer);
        ay.set(7, mixer);
        const uint16_t ton[3] = {ch[0].ton, ch[1].ton, ch[2].ton};
        const uint8_t amp[3] = {ch[0].amp, ch[1].amp, ch[2].amp};
        writeTones(ay, ton, amp);
        return wrapped;
    }

    Mem m;

private:
    struct Ch {
        uint16_t addr = 0, smp = 0, orn = 0, ton = 0;
        uint8_t amp = 0, note = 0, posInSmp = 0, skip = 0;
        int8_t smpTick = -1, skipCounter = 0;
        bool env = false;
    };
    Ch ch[3];
    uint8_t delayCounter = 1, transposition = 0, position = 0;

    uint16_t posBase() const { return m.word(1); }
    uint16_t ornBase() const { return m.word(3); }
    uint16_t patBase() const { return m.word(5); }

    long patternEntry(uint8_t num) const {
        for (uint32_t i = 0; i < 32; ++i) {
            const uint32_t a = patBase() + 7 * i;
            if (a >= m.size() || m.at(a) == 0xFF) break;
            if (m.at(a) == num) return (long)a;
        }
        return -1;
    }
    long sampleAt(uint8_t num) const {  // the sample's first line
        for (uint32_t k = 0; k < 16 && 0x1B + 0x63 * (k + 1) <= posBase(); ++k)
            if (m.at(0x1B + 0x63 * k) == num) return (long)(0x1C + 0x63 * k);
        return -1;
    }
    long ornamentAt(uint8_t num) const {  // the ornament's first offset
        for (uint32_t k = 0; k < 16; ++k) {
            const uint32_t a = ornBase() + 0x21 * k;
            if (a + 0x21 > patBase() || a >= m.size()) break;
            if (m.at(a) == num) return (long)(a + 1);
        }
        return -1;
    }
    void enter(int pos) {
        transposition = m.at(posBase() + 2u + (uint32_t)pos * 2);
        const long e = patternEntry(m.at(posBase() + 1u + (uint32_t)pos * 2));
        if (e < 0) return;
        for (int c = 0; c < 3; ++c) ch[c].addr = m.word((uint32_t)e + 1 + 2 * c);
    }
    void interpret(Ay& ay, Ch& c) {
        for (int guard = 0; guard < kRowGuard; ++guard) {
            const uint8_t v = m.at(c.addr);
            if (v <= 0x5F) {  // note
                c.note = v;
                c.smpTick = 32;
                c.posInSmp = 0;
                c.addr++;
                break;
            } else if (v <= 0x6F) {  // sample
                const long s = sampleAt(v - 0x60);
                if (s >= 0) c.smp = (uint16_t)s;
            } else if (v <= 0x7F) {  // ornament, envelope off
                const long o = ornamentAt(v - 0x70);
                if (o >= 0) c.orn = (uint16_t)o;
                c.env = false;
            } else if (v == 0x80) {  // rest
                c.smpTick = -1;
                c.addr++;
                break;
            } else if (v == 0x81) {  // empty
                c.addr++;
                break;
            } else if (v == 0x82) {  // ornament 0, envelope off
                const long o = ornamentAt(0);
                if (o >= 0) c.orn = (uint16_t)o;
                c.env = false;
            } else if (v <= 0x8E) {  // envelope shape and period, ornament 0
                ay.set(13, (uint8_t)(v - 0x80));
                c.addr++;
                ay.set(11, m.at(c.addr));
                c.env = true;
                const long o = ornamentAt(0);
                if (o >= 0) c.orn = (uint16_t)o;
            } else {  // rows to skip after each note
                c.skip = (uint8_t)(v - 0xA1);
            }
            c.addr++;
        }
        c.skipCounter = (int8_t)c.skip;
    }
    void registers(Ay& ay, Ch& c, uint8_t& mixer) {
        if (c.smpTick >= 0) {
            c.smpTick--;
            c.posInSmp = (uint8_t)((c.posInSmp + 1) & 0x1F);
            if (c.smpTick == 0) {
                if (m.at(c.smp + 0x60u) != 0) {
                    c.posInSmp = m.at(c.smp + 0x60u) & 0x1F;
                    c.smpTick = (int8_t)(m.at(c.smp + 0x61u) + 1);
                } else {
                    c.smpTick = -1;
                }
            }
        }
        if (c.smpTick >= 0) {
            const uint32_t i = ((c.posInSmp - 1u) & 0x1F) * 3 + c.smp;
            const uint8_t b0 = m.at(i), b1 = m.at(i + 1), b2 = m.at(i + 2);
            if (b1 & 0x80) mixer |= 64;
            else ay.set(6, b1 & 0x1F);
            if (b1 & 0x40) mixer |= 8;
            c.amp = b0 & 15;
            uint8_t j = (uint8_t)(c.note + m.at(c.orn + ((c.posInSmp - 1u) & 0x1F)) + transposition);
            if (j > 95) j = 95;
            const uint16_t shift = (uint16_t)(b2 + ((b0 & 0xF0) << 4));
            c.ton = (uint16_t)(((b1 & 0x20) ? kStTable[j] + shift : kStTable[j] - shift) & 0xFFF);
            if (c.env) c.amp |= 16;
        } else {
            c.amp = 0;
        }
        mixer >>= 1;
    }
};

bool stcProgramName(const Mem& m) {  // the name field holds the compiler's name, not the song's
    static const char* const kPrefixes[] = {"SONG BY ST COMPIL", "SONG BY MB COMPIL",  "SONG BY ST-COMPIL",
                                            "SONG BY S.T.COMP",  "SONG ST BY COMPILE", "SOUND TRACKER",
                                            "S.T.FULL EDITION",  "S.W.COMPILE V2.0",   "STU SONG COMPILER"};
    for (const char* p : kPrefixes) {
        const size_t k = std::strlen(p);
        bool same = true;
        for (size_t i = 0; i < k && same; ++i) same = m.at(7 + (uint32_t)i) == (uint8_t)p[i];
        if (same) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------
// Pro Tracker 2 (PT2)
// Header: delay, positions, loop position, 32 sample pointers, 16 ornament pointers, pattern table pointer,
// 30 byte name; the position list from 131 ends with 0xFF. Samples: length, loop, lines of 3 bytes;
// ornaments: length, loop, offsets.

int pt2Positions(const Mem& m) {
    int n = 0;
    while (n < 255 && 131u + n < m.size() && m.at(131u + n) < 128) ++n;
    return n;
}

class Pt2 : public Replayer {
public:
    explicit Pt2(const uint8_t* d, size_t n) {
        m.d.assign(d, d + n);
        npos = pt2Positions(m);
        loop = m.at(2) < npos ? m.at(2) : 0;
    }
    void reset(Ay& ay) override {
        ay = Ay{};
        delayCounter = 1;
        delay = m.at(0);
        position = 0;
        for (Ch& c : ch) {
            c = Ch{};
            setOrnament(c, 0);
        }
        enter(0);
    }
    bool frame(Ay& ay) override {
        ay.envShape = -1;
        bool wrapped = false;
        if (--delayCounter == 0) {
            Ch& a = ch[0];
            if (--a.skipCounter < 0) {
                if (m.at(a.addr) == 0) {
                    if (++position >= npos) {
                        position = loop;
                        wrapped = true;
                    }
                    enter(position);
                }
                interpret(ay, a);
            }
            for (int c = 1; c < 3; ++c)
                if (--ch[c].skipCounter < 0) interpret(ay, ch[c]);
            delayCounter = delay;
        }
        uint8_t mixer = 0;
        for (Ch& c : ch) registers(ay, c, mixer);
        ay.set(7, mixer);
        const uint16_t ton[3] = {ch[0].ton, ch[1].ton, ch[2].ton};
        const uint8_t amp[3] = {ch[0].amp, ch[1].amp, ch[2].amp};
        writeTones(ay, ton, amp);
        return wrapped;
    }

    Mem m;

private:
    struct Ch {
        uint16_t addr = 0, orn = 0, smp = 0, ton = 0;
        uint8_t ornLoop = 0, ornLen = 0, ornPos = 0, smpLoop = 0, smpLen = 0, smpPos = 0, volume = 15, skip = 0,
                note = 0, slideTo = 0, amp = 0;
        int16_t sliding = 0, glissade = 0;
        int tonDelta = 0, glissType = 0;
        bool env = false, enabled = false;
        int8_t addNoise = 0, skipCounter = 0;
    };
    Ch ch[3];
    uint8_t delayCounter = 1, delay = 3;
    int position = 0, npos = 1, loop = 0;

    uint16_t patBase() const { return m.word(99); }
    void setOrnament(Ch& c, int x) const {
        c.orn = m.word(67u + 2u * (uint32_t)x);
        c.ornLen = m.at(c.orn);
        c.orn++;
        c.ornLoop = m.at(c.orn);
        c.orn++;
    }
    void setSample(Ch& c, int x) const {
        c.smp = m.word(3u + 2u * (uint32_t)x);
        c.smpLen = m.at(c.smp);
        c.smp++;
        c.smpLoop = m.at(c.smp);
        c.smp++;
    }
    void enter(int pos) {
        const uint32_t p = patBase() + m.at(131u + (uint32_t)pos) * 6u;
        for (int c = 0; c < 3; ++c) ch[c].addr = m.word(p + 2u * c);
    }
    void interpret(Ay& ay, Ch& c) {
        bool quit = false, gliss = false;
        for (int guard = 0; !quit && guard < kRowGuard; ++guard) {
            const uint8_t v = m.at(c.addr);
            if (v >= 0xE1) {
                setSample(c, v - 0xE0);
            } else if (v == 0xE0) {  // sound off
                c.smpPos = c.ornPos = 0;
                c.sliding = 0;
                c.glissType = 0;
                c.enabled = false;
                quit = true;
            } else if (v >= 0x80) {  // note
                c.smpPos = c.ornPos = 0;
                c.sliding = 0;
                if (gliss) {
                    c.slideTo = (uint8_t)(v - 0x80);
                    if (c.glissType == 1) c.note = c.slideTo;
                } else {
                    c.note = (uint8_t)(v - 0x80);
                    c.glissType = 0;
                }
                c.enabled = true;
                quit = true;
            } else if (v == 0x7F) {
                c.env = false;
            } else if (v >= 0x71) {  // envelope shape and period
                c.env = true;
                ay.set(13, (uint8_t)(v - 0x70));
                c.addr++;
                ay.set(11, m.at(c.addr));
                c.addr++;
                ay.set(12, m.at(c.addr));
            } else if (v == 0x70) {  // empty row
                quit = true;
            } else if (v >= 0x60) {
                setOrnament(c, v - 0x60);
                c.ornPos = 0;
            } else if (v >= 0x20) {
                c.skip = (uint8_t)(v - 0x20);
            } else if (v >= 0x10) {
                c.volume = (uint8_t)(v - 0x10);
            } else if (v == 0x0F) {  // speed
                c.addr++;
                delay = m.at(c.addr);
            } else if (v == 0x0E) {  // glissando
                c.addr++;
                c.glissade = (int8_t)m.at(c.addr);
                c.glissType = 1;
                gliss = true;
            } else if (v == 0x0D) {  // portamento to the note
                c.addr++;
                c.glissade = (int16_t)std::abs((int)(int8_t)m.at(c.addr));
                c.addr += 2;  // the precalculated distance is not used (wrong for the first note of a pattern)
                c.glissType = 2;
                gliss = true;
            } else if (v == 0x0C) {
                c.glissType = 0;
            } else {  // noise offset
                c.addr++;
                c.addNoise = (int8_t)m.at(c.addr);
            }
            c.addr++;
        }
        if (gliss && c.glissType == 2) {
            c.tonDelta = std::abs((int)kPt2Table[c.slideTo % 96] - (int)kPt2Table[c.note % 96]);
            if (c.slideTo > c.note) c.glissade = (int16_t)-c.glissade;
        }
        c.skipCounter = (int8_t)c.skip;
    }
    void registers(Ay& ay, Ch& c, uint8_t& mixer) {
        if (c.enabled) {
            const uint32_t s = c.smp + c.smpPos * 3u;
            const uint8_t b0 = m.at(s), b1 = m.at(s + 1);
            uint16_t ton = (uint16_t)(m.at(s + 2) + ((b1 & 15) << 8));
            if (!(b0 & 4)) ton = (uint16_t)-ton;
            uint8_t j = (uint8_t)(c.note + m.at(c.orn + c.ornPos));
            if (j > 95) j = 95;
            c.ton = (uint16_t)((ton + c.sliding + kPt2Table[j]) & 0xFFF);
            if (c.glissType == 2) {
                c.tonDelta -= std::abs((int)c.glissade);
                if (c.tonDelta < 0) {
                    c.note = c.slideTo;
                    c.glissType = 0;
                    c.sliding = 0;
                }
            }
            if (c.glissType != 0) c.sliding = (int16_t)(c.sliding + c.glissade);
            c.amp = (uint8_t)((c.volume * 17 + (c.volume > 7 ? 1 : 0)) * (b1 >> 4) / 256);
            if (c.env) c.amp |= 16;
            if (b0 & 1) mixer |= 64;
            else ay.set(6, (uint8_t)(((b0 >> 3) + c.addNoise) & 31));
            if (b0 & 2) mixer |= 8;
            if (++c.smpPos == c.smpLen) c.smpPos = c.smpLoop;
            if (++c.ornPos == c.ornLen) c.ornPos = c.ornLoop;
        } else {
            c.amp = 0;
        }
        mixer >>= 1;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// ASC Sound Master 1.x (0.x files get a loop position byte inserted at load)
// Header: delay, loop position, patterns, samples and ornaments table pointers, number of positions, the
// positions. Pattern table entries (6 bytes) and the 32 sample / ornament pointers are relative to their table.

class Asc : public Replayer {
public:
    explicit Asc(std::vector<uint8_t> d) {
        m.d = std::move(d);
        num = m.at(8);
        loop = m.at(1) < num ? m.at(1) : 0;
    }
    void reset(Ay& ay) override {
        ay = Ay{};
        for (Ch& c : ch) c = Ch{};
        position = 0;
        delayCounter = 1;
        delay = m.at(0);
        enter(0);
    }
    bool frame(Ay& ay) override {
        ay.envShape = -1;
        bool wrapped = false;
        if (--delayCounter == 0) {
            Ch& a = ch[0];
            if (--a.skipCounter < 0) {
                if (m.at(a.addr) == 0xFF) {
                    if (++position >= num) {
                        position = loop;
                        wrapped = true;
                    }
                    enter(position);
                    for (Ch& c : ch) c.initNoise = 0;
                }
                interpret(ay, a);
            }
            for (int c = 1; c < 3; ++c)
                if (--ch[c].skipCounter < 0) interpret(ay, ch[c]);
            delayCounter = delay;
        }
        uint8_t mixer = 0;
        for (Ch& c : ch) registers(ay, c, mixer);
        ay.set(7, mixer);
        const uint16_t ton[3] = {ch[0].ton, ch[1].ton, ch[2].ton};
        const uint8_t amp[3] = {ch[0].amp, ch[1].amp, ch[2].amp};
        writeTones(ay, ton, amp);
        return wrapped;
    }

    Mem m;

private:
    struct Ch {
        uint16_t initSmp = 0, smpPt = 0, loopSmp = 0, initOrn = 0, ornPt = 0, loopOrn = 0, addr = 0, ton = 0,
                 tonDev = 0;
        uint8_t note = 0, addNote = 0, skip = 0, initNoise = 0, curNoise = 0, volume = 0, slideCounter = 0, amp = 0,
                ampDelay = 0, ampDelayCounter = 0;
        int16_t curSlide = 0, slideStep = 0;
        int8_t skipCounter = 0, addAmp = 0;
        bool env = false, sound = false, smpFinished = false, breakSmpLoop = false;
    };
    Ch ch[3];
    uint8_t delayCounter = 1, delay = 3;
    int position = 0, num = 1, loop = 0;

    uint16_t patBase() const { return m.word(2); }
    uint16_t smpBase() const { return m.word(4); }
    uint16_t ornBase() const { return m.word(6); }
    void enter(int pos) {
        const uint32_t p = patBase() + 6u * m.at(9u + (uint32_t)pos);
        for (int c = 0; c < 3; ++c) ch[c].addr = (uint16_t)(m.word(p + 2u * c) + patBase());
    }
    // F7 / F9: portamento to the next note over `steps` rows' worth of frames
    void glide(Ch& c, bool fromSlide) {
        const int8_t steps = (int8_t)m.at(c.addr);
        const uint8_t target = m.at(c.addr + 1u);
        int16_t delta;
        if (target < 0x56)
            delta = (int16_t)(kAscTable[c.note % 86] + (fromSlide ? c.curSlide / 16 : 0) - kAscTable[target]);
        else
            delta = (int16_t)(c.curSlide / 16);
        delta = (int16_t)(delta << 4);
        if (steps == 0) {
            c.slideStep = 0;
            c.curSlide = delta;
            c.slideCounter = 0;
            return;
        }
        c.slideStep = (int16_t)(-delta / steps);
        c.curSlide = (int16_t)(delta - delta % steps);
        c.slideCounter = (uint8_t)steps;
    }
    void interpret(Ay& ay, Ch& c) {
        bool noOrnInit = false, noSmpInit = false;
        c.slideCounter = 0;
        c.ampDelayCounter = 0;
        for (int guard = 0; guard < kRowGuard; ++guard) {
            const uint8_t v = m.at(c.addr);
            if (v <= 0x55) {  // note
                c.note = v;
                c.addr++;
                c.curNoise = c.initNoise;
                if ((int8_t)c.slideCounter <= 0) c.curSlide = 0;
                if (!noSmpInit) {
                    c.addAmp = 0;
                    c.tonDev = 0;
                    c.smpPt = c.initSmp;
                    c.sound = true;
                    c.smpFinished = false;
                    c.breakSmpLoop = false;
                }
                if (!noOrnInit) {
                    c.ornPt = c.initOrn;
                    c.addNote = 0;
                }
                if (c.env) {
                    ay.set(11, m.at(c.addr));
                    c.addr++;
                }
                break;
            } else if (v <= 0x5D) {  // empty row
                c.addr++;
                break;
            } else if (v == 0x5E) {  // leave the sample's loop
                c.breakSmpLoop = true;
                c.addr++;
                break;
            } else if (v == 0x5F) {  // sound off
                c.sound = false;
                c.addr++;
                break;
            } else if (v <= 0x9F) {
                c.skip = (uint8_t)(v - 0x60);
            } else if (v <= 0xBF) {
                c.initSmp = (uint16_t)(m.word((v - 0xA0u) * 2 + smpBase()) + smpBase());
            } else if (v <= 0xDF) {
                c.initOrn = (uint16_t)(m.word((v - 0xC0u) * 2 + ornBase()) + ornBase());
            } else if (v == 0xE0) {
                c.volume = 15;
                c.env = true;
            } else if (v <= 0xEF) {
                c.volume = (uint8_t)(v - 0xE0);
                c.env = false;
            } else if (v == 0xF0) {
                c.addr++;
                c.initNoise = m.at(c.addr);
            } else if (v == 0xF1) {
                noSmpInit = true;
            } else if (v == 0xF2) {
                noOrnInit = true;
            } else if (v == 0xF3) {
                noSmpInit = noOrnInit = true;
            } else if (v == 0xF4) {
                c.addr++;
                delay = m.at(c.addr);
            } else if (v == 0xF5 || v == 0xF6) {
                c.addr++;
                const int step = (int8_t)m.at(c.addr) * 16;
                c.slideStep = (int16_t)(v == 0xF5 ? -step : step);
                c.slideCounter = 255;
            } else if (v == 0xF7) {
                c.addr++;
                noSmpInit = true;
                glide(c, true);
            } else if (v == 0xF8) {
                ay.set(13, 8);
            } else if (v == 0xF9) {
                c.addr++;
                glide(c, false);
            } else if (v == 0xFA) {
                ay.set(13, 10);
            } else if (v == 0xFB) {
                c.addr++;
                const uint8_t b = m.at(c.addr);
                c.ampDelay = (b & 32) == 0 ? (uint8_t)(b << 3) : (uint8_t)((((b << 3) ^ 0xF8) + 9) & 0xFF);
                c.ampDelayCounter = c.ampDelay;
            } else if (v == 0xFC) {
                ay.set(13, 12);
            } else if (v == 0xFE) {
                ay.set(13, 14);
            }
            c.addr++;
        }
        c.skipCounter = (int8_t)c.skip;
    }
    void registers(Ay& ay, Ch& c, uint8_t& mixer) {
        if (c.smpFinished || !c.sound) {
            c.amp = 0;
        } else {
            if (c.ampDelayCounter != 0) {
                if (c.ampDelayCounter >= 16) {
                    c.ampDelayCounter = (uint8_t)(c.ampDelayCounter - 8);
                    if (c.addAmp < -15) c.addAmp++;
                    else if (c.addAmp > 15) c.addAmp--;
                } else {
                    if (c.ampDelayCounter & 1) {
                        if (c.addAmp > -15) c.addAmp--;
                    } else if (c.addAmp < 15) {
                        c.addAmp++;
                    }
                    c.ampDelayCounter = c.ampDelay;
                }
            }
            const uint8_t s0 = m.at(c.smpPt), s1 = m.at(c.smpPt + 1u), s2 = m.at(c.smpPt + 2u);
            if (s0 & 128) c.loopSmp = c.smpPt;
            if ((s0 & 96) == 32) c.smpFinished = true;
            c.tonDev = (uint16_t)(c.tonDev + (int8_t)s1);
            mixer |= (uint8_t)((s2 & 9) << 3);
            const bool envOk = (s2 & 6) == 2;
            if ((s2 & 6) == 4 && c.addAmp > -15) c.addAmp--;
            if ((s2 & 6) == 6 && c.addAmp < 15) c.addAmp++;
            int a = c.addAmp + (s2 >> 4);
            a = a < 0 ? 0 : (a > 15 ? 15 : a);
            c.amp = (uint8_t)((a * (c.volume + 1)) >> 4);
            const int add = (int8_t)(uint8_t)(s0 << 3) / 8;  // low 5 bits, signed
            if (envOk && (mixer & 64)) ay.set(11, (uint8_t)(ay.r[11] + add));
            else c.curNoise = (uint8_t)(c.curNoise + add);
            c.smpPt = (uint16_t)(c.smpPt + 3);
            if (s0 & 64) {
                if (!c.breakSmpLoop) c.smpPt = c.loopSmp;
                else if (s0 & 32) c.smpFinished = true;
            }
            const uint8_t o0 = m.at(c.ornPt), o1 = m.at(c.ornPt + 1u);
            if (o0 & 128) c.loopOrn = c.ornPt;
            c.addNote = (uint8_t)(c.addNote + o1);
            c.curNoise = (uint8_t)(c.curNoise + ((-(int8_t)(o0 & 0x10)) | o0));
            c.ornPt = (uint16_t)(c.ornPt + 2);
            if (o0 & 64) c.ornPt = c.loopOrn;
            if (!(mixer & 64)) ay.set(6, (uint8_t)(((uint8_t)(c.curSlide >> 8) + c.curNoise) & 0x1F));
            int j = (int8_t)(uint8_t)(c.note + c.addNote);
            j = j < 0 ? 0 : (j > 0x55 ? 0x55 : j);
            c.ton = (uint16_t)((kAscTable[j] + c.tonDev + c.curSlide / 16) & 0xFFF);
            if (c.slideCounter != 0) {
                if ((int8_t)c.slideCounter > 0) c.slideCounter--;
                c.curSlide = (int16_t)(c.curSlide + c.slideStep);
            }
            if (c.env && envOk) c.amp |= 0x10;
        }
        mixer >>= 1;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// SQ-Tracker compiled (SQT)
// Header: size, then samples, ornaments, patterns, positions and loop pointers as Z80 addresses (the module's
// load address is samples pointer - 10); the sample, ornament and pattern tables follow. Relocated to file
// offsets at load (ayfly's SQT_PreInit). Positions: 7 bytes (channel C, B, A pattern with its volume /
// transposition byte, then the speed), ending with 0.

class Sqt : public Replayer {
public:
    explicit Sqt(std::vector<uint8_t> d) { m.d = std::move(d); }
    void reset(Ay& ay) override {
        ay = Ay{};
        for (Ch& c : ch) c = Ch{};
        delayCounter = delay = lines = 1;
        posPt = m.word(8);
    }
    bool frame(Ay& ay) override {
        ay.envShape = -1;
        bool wrapped = false;
        if (--delayCounter == 0) {
            delayCounter = delay;
            if (--lines == 0) wrapped = nextPosition();
            interpret(ay, ch[2]);
            interpret(ay, ch[1]);
            interpret(ay, ch[0]);
        }
        uint8_t mixer = 0;
        registers(ay, ch[2], mixer);
        registers(ay, ch[1], mixer);
        registers(ay, ch[0], mixer);
        mixer = (uint8_t)((-(mixer + 1)) & 0x3F);
        for (int c = 0; c < 3; ++c) {
            if (!ch[c].mixNoise) mixer |= (uint8_t)(8 << c);
            if (!ch[c].mixTon) mixer |= (uint8_t)(1 << c);
        }
        ay.set(7, mixer);
        const uint16_t ton[3] = {ch[0].ton, ch[1].ton, ch[2].ton};
        const uint8_t amp[3] = {ch[0].amp, ch[1].amp, ch[2].amp};
        writeTones(ay, ton, amp);
        return wrapped;
    }

    Mem m;

private:
    struct Ch {
        uint16_t addr = 0, smp = 0, smpPt = 0, orn = 0, ornPt = 0, ton = 0, ix27 = 0;
        uint8_t volume = 0, amp = 0, note = 0, ix21 = 0;
        int16_t slideStep = 0, curSlide = 0;
        int8_t smpTick = 0, ornTick = 0, transpose = 0;
        bool enabled = false, env = false, ornOn = false, gliss = false, mixNoise = false, mixTon = false,
             b4 = false, b6 = false, b7 = false;
    };
    Ch ch[3];  // A, B, C
    uint8_t delay = 1, delayCounter = 1, lines = 1;
    uint16_t posPt = 0;

    uint16_t smpBase() const { return m.word(2); }
    uint16_t ornBase() const { return m.word(4); }
    uint16_t patBase() const { return m.word(6); }

    bool nextPosition() {
        bool wrapped = false;
        for (int k = 2; k >= 0; --k) {  // C, B, A
            Ch& c = ch[k];
            if (m.at(posPt) == 0) {
                posPt = m.word(10);
                wrapped = true;
            }
            c.b4 = (int8_t)m.at(posPt) < 0;
            c.addr = m.word((uint8_t)(m.at(posPt) * 2) + (uint32_t)patBase());
            if (k == 2) lines = m.at(c.addr);
            c.addr++;
            posPt++;
            c.volume = m.at(posPt) & 15;
            const int t = m.at(posPt) >> 4;
            c.transpose = (int8_t)(t < 9 ? t : -(t - 9) - 1);
            posPt++;
            c.ix21 = 0;
        }
        delay = m.at(posPt);
        delayCounter = delay;
        posPt++;
        return wrapped;
    }
    void lc1d1(Ay& ay, Ch& c, uint16_t& p, uint8_t a) {
        p++;
        if (c.b6) {
            c.addr = (uint16_t)(p + 1);
            c.b6 = false;
        }
        const uint8_t v = m.at(p);
        switch ((int)a - 1) {
            case 0:
                if (c.b4) c.volume = v & 15;
                break;
            case 1:
                if (c.b4) c.volume = (uint8_t)((c.volume + v) & 15);
                break;
            case 2:
                if (c.b4)
                    for (Ch& x : ch) x.volume = v;
                break;
            case 3:
                if (c.b4)
                    for (Ch& x : ch) x.volume = (uint8_t)((x.volume + v) & 15);
                break;
            case 4:
                if (c.b4) {
                    delayCounter = v & 31;
                    if (delayCounter == 0) delayCounter = 32;
                    delay = delayCounter;
                }
                break;
            case 5:
                if (c.b4) {
                    delayCounter = (uint8_t)((delayCounter + v) & 31);
                    if (delayCounter == 0) delayCounter = 32;
                    delay = delayCounter;
                }
                break;
            case 6:
                c.curSlide = 0;
                c.gliss = true;
                c.slideStep = (int16_t)-v;
                break;
            case 7:
                c.curSlide = 0;
                c.gliss = true;
                c.slideStep = v;
                break;
            default:
                c.env = true;
                ay.set(13, (uint8_t)((a - 1) & 15));
                ay.set(11, v);
                break;
        }
    }
    void setSample(Ch& c, int a) {  // LC2A8
        c.env = c.ornOn = c.gliss = false;
        c.enabled = true;
        c.smp = m.word((uint32_t)a * 2 + smpBase());
        c.smpPt = (uint16_t)(c.smp + 2);
        c.smpTick = 32;
        c.mixNoise = c.mixTon = true;
    }
    void setOrnament(Ch& c, int a) {  // LC2D9
        c.orn = m.word((uint32_t)a * 2 + ornBase());
        c.ornPt = (uint16_t)(c.orn + 2);
        c.ornTick = 32;
        c.ornOn = true;
    }
    void lc283(Ay& ay, Ch& c, uint16_t& p) {
        const uint8_t v = m.at(p);
        if (v <= 0x7F) {
            lc1d1(ay, c, p, v);
        } else {
            if ((v >> 1) & 31) setSample(c, (v >> 1) & 31);
            if (v & 64) {
                int t = m.at(p + 1u) >> 4;
                if (v & 1) t |= 16;
                if (t) setOrnament(c, t);
                p++;
                if (m.at(p) & 15) lc1d1(ay, c, p, m.at(p) & 15);
            }
        }
        p++;
    }
    void lc191(Ay& ay, Ch& c, uint16_t& p) {  // repeat the last note's sample / commands
        p = c.ix27;
        c.b6 = false;
        if (m.at(p) <= 0x7F) {
            p++;
            lc283(ay, c, p);
        } else {
            setSample(c, m.at(p) & 31);
        }
    }
    void interpret(Ay& ay, Ch& c) {
        uint16_t p = 0;
        if (c.ix21 != 0) {
            c.ix21--;
            if (c.b7) lc191(ay, c, p);
            return;
        }
        p = c.addr;
        c.b6 = true;
        c.b7 = false;
        const uint8_t v = m.at(p);
        if (v <= 0x5F) {
            c.note = v;
            c.ix27 = p;
            p++;
            lc283(ay, c, p);
            if (c.b6) c.addr = p;
        } else if (v <= 0x6E) {
            lc1d1(ay, c, p, (uint8_t)(v - 0x60));
        } else if (v <= 0x7F) {
            c.mixNoise = c.mixTon = c.enabled = false;
            if (v != 0x6F) lc1d1(ay, c, p, (uint8_t)(v - 0x6F));
            else c.addr = (uint16_t)(p + 1);
        } else if (v <= 0xBF) {
            c.addr = (uint16_t)(p + 1);
            if (v <= 0x9F) {
                if (!(v & 16)) c.note = (uint8_t)(c.note + (v & 15));
                else c.note = (uint8_t)(c.note - (v & 15));
            } else {
                c.ix21 = v & 15;
                if (!(v & 16)) return;
                if (c.ix21 != 0) c.b7 = true;
            }
            lc191(ay, c, p);
        } else {
            c.addr = (uint16_t)(p + 1);
            c.ix27 = p;
            setSample(c, v & 31);
        }
    }
    void registers(Ay& ay, Ch& c, uint8_t& mixer) {
        mixer = (uint8_t)(mixer << 1);
        if (!c.enabled) {
            c.amp = 0;
            return;
        }
        const uint8_t b0 = m.at(c.smpPt), b1 = m.at(c.smpPt + 1u);
        c.amp = b0 & 15;
        if (c.amp != 0) {
            c.amp = (uint8_t)(c.amp - c.volume);
            if ((int8_t)c.amp < 0) c.amp = 0;
        } else if (c.env) {
            c.amp = 16;
        }
        if (b1 & 32) {
            mixer |= 8;
            uint8_t noise = (uint8_t)((b0 & 0xF0) >> 3);
            if ((int8_t)b1 < 0) noise++;
            ay.set(6, noise);
        }
        if (b1 & 64) mixer |= 1;
        uint8_t j = c.note;
        if (c.ornOn) {
            j = (uint8_t)(j + m.at(c.ornPt));
            if (--c.ornTick == 0) {
                if (m.at(c.orn) != 32) {
                    c.ornTick = (int8_t)m.at(c.orn + 1u);
                    c.ornPt = (uint16_t)(c.orn + 2 + m.at(c.orn));
                } else {
                    c.ornTick = (int8_t)m.at(c.smp + 1u);
                    c.ornPt = (uint16_t)(c.orn + 2 + m.at(c.smp));
                }
            } else {
                c.ornPt++;
            }
        }
        j = (uint8_t)(j + c.transpose);
        if (j > 0x5F) j = 0x5F;
        const uint16_t shift = (uint16_t)(((b1 & 15) << 8) + m.at(c.smpPt + 2u));
        c.ton = (uint16_t)((b1 & 16) == 0 ? kSqtTable[j] - shift : kSqtTable[j] + shift);
        if (--c.smpTick == 0) {
            c.smpTick = (int8_t)m.at(c.smp + 1u);
            if (m.at(c.smp) == 32) {
                c.enabled = false;
                c.ornOn = false;
            }
            c.smpPt = (uint16_t)(c.smp + 2 + m.at(c.smp) * 3);
        } else {
            c.smpPt = (uint16_t)(c.smpPt + 3);
        }
        if (c.gliss) {
            c.ton = (uint16_t)(c.ton + c.curSlide);
            c.curSlide = (int16_t)(c.curSlide + c.slideStep);
        }
        c.ton &= 0xFFF;
    }
};

// SQT_PreInit: the header and table pointers become file offsets. false when they do not fit.
bool sqtRelocate(std::vector<uint8_t>& d) {
    const size_t n = d.size();
    const long smp = w16(d.data(), n, 2), pat = w16(d.data(), n, 6), pos = w16(d.data(), n, 8);
    if (smp < 10 || pat < 0 || pos < 0) return false;
    const long base = smp - 10;
    long maxPat = 0;
    long p = pos - base;
    if (p < 0) return false;
    for (int guard = 0;; ++guard) {
        if (guard > 256 || p >= (long)n) return false;
        if (d[(size_t)p] == 0) break;
        if (p + 7 >= (long)n) return false;
        for (int k = 0; k < 3; ++k) {
            const long v = d[(size_t)p + 2 * k] & 0x7F;
            if (v > maxPat) maxPat = v;
        }
        p += 7;
    }
    const long words = (pat - base + maxPat * 2) / 2;
    if (words < 1) return false;
    for (long k = 0; k < words; ++k) {
        const size_t a = 2 + 2 * (size_t)k;
        if (a + 1 >= n) return false;
        const long v = d[a] | d[a + 1] << 8;
        if (v < base) return false;
        d[a] = (uint8_t)((v - base) & 0xFF);
        d[a + 1] = (uint8_t)((v - base) >> 8);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Sound Tracker 1.1 source modules (ZXTune's SoundTrackerUncompiled layout): 15 samples of 130 bytes (32 levels,
// 32 noise / mask bytes, 32 effect words, loop, loop length), 256 positions (pattern 1..32, transposition), the
// position count - 1, 17 ornaments of 32 offsets, tempo, rows per pattern, then patterns of 64 rows x 3 channels
// x 3 bytes (note RNNN#OOO, sample << 4 | effect, effect parameter).

constexpr size_t kSt1Header = 3009, kSt1Pattern = 576;

bool st1Raw(const uint8_t* s, size_t n) {
    if (n < kSt1Header + kSt1Pattern) return false;
    for (int k = 0; k < 15; ++k) {
        const uint8_t* x = s + 130 * k;
        for (int i = 0; i < 32; ++i)
            if (x[i] > 15 || (x[32 + i] & 0x20) || x[65 + 2 * i] > 0x1F) return false;
        if (x[128] > 0x1F || x[129] > 0x1F) return false;
    }
    for (int i = 0; i < 256; ++i)
        if (s[1950 + 2 * i] < 1 || s[1950 + 2 * i] > 0x20) return false;
    if (s[2462] > 0x7F) return false;
    if (s[3007] < 2 || s[3007] > 0x0F) return false;
    if (s[3008] < 0x20 || s[3008] > 0x40) return false;
    return true;
}

// The source module as an STC module the Stc replayer plays.
std::vector<uint8_t> st1ToStc(const uint8_t* s, size_t n) {
    static const int kHalftone[16] = {-1, -1, 9, 10, 11, -1, 0, 1, 2, 3, 4, -1, 5, 6, 7, 8};
    const size_t havePatterns = (n - kSt1Header) / kSt1Pattern;
    std::vector<uint8_t> o(27, 0);
    o[0] = s[3007];
    std::memcpy(o.data() + 7, "SOUND TRACKER v1.1", 18);
    for (int k = 0; k < 16; ++k) {  // sample 0 is silent
        o.push_back((uint8_t)k);
        const uint8_t* x = k ? s + 130 * (k - 1) : nullptr;
        for (int i = 0; i < 32; ++i) {
            const int eff = x ? x[64 + 2 * i] | x[65 + 2 * i] << 8 : 0;
            o.push_back(x ? (uint8_t)((x[i] & 15) | ((eff >> 8) & 15) << 4) : 0);
            o.push_back(x ? (uint8_t)((x[32 + i] & 0xDF) | ((eff & 0x1000) ? 0x20 : 0)) : 0);
            o.push_back((uint8_t)(eff & 0xFF));
        }
        o.push_back(x ? x[128] : 0);
        o.push_back(x ? x[129] : 0);
    }
    const size_t pos = o.size();
    const int npos = s[2462] + 1;
    o.push_back((uint8_t)(npos - 1));
    bool used[33] = {};
    for (int i = 0; i < npos; ++i) {
        o.push_back(s[1950 + 2 * i]);
        o.push_back(s[1951 + 2 * i]);
        used[s[1950 + 2 * i]] = true;
    }
    const size_t orn = o.size();
    for (int k = 0; k < 16; ++k) {
        o.push_back((uint8_t)k);
        o.insert(o.end(), s + 2463 + 32 * k, s + 2463 + 32 * (k + 1));
    }
    const size_t pat = o.size();
    std::vector<size_t> entries;
    for (int p = 1; p <= 32; ++p)
        if (used[p]) {
            entries.push_back(o.size());
            o.push_back((uint8_t)p);
            o.resize(o.size() + 6, 0);
        }
    o.push_back(0xFF);
    const int rows = s[3008];
    for (size_t e : entries) {
        const size_t p = o[e] - 1u;
        const uint8_t* src = p < havePatterns ? s + kSt1Header + kSt1Pattern * p : nullptr;
        for (int c = 0; c < 3; ++c) {
            const size_t at = o.size();
            o[e + 1 + 2 * c] = (uint8_t)(at & 0xFF);
            o[e + 2 + 2 * c] = (uint8_t)(at >> 8);
            o.push_back(0xA1);  // no rows skipped: one event per row
            int sample = 0, envType = 0, envTone = 0;
            for (int r = 0; r < rows; ++r) {
                const uint8_t* l = src ? src + r * 9 + c * 3 : nullptr;
                const uint8_t note = l ? l[0] : 0;
                const int half = kHalftone[(note >> 3) & 15];
                if (note & 0x80) {
                    o.push_back(0x80);
                    continue;
                }
                if (!(note & 0x78) || half < 0) {
                    o.push_back(0x81);
                    continue;
                }
                const int smp = l[1] >> 4, eff = l[1] & 15, par = l[2];
                if (smp && smp != sample) o.push_back((uint8_t)(0x60 + smp));
                sample = smp;
                if (eff == 15) {
                    o.push_back((uint8_t)(0x70 + (par & 15)));
                    envType = 0;
                } else if (eff == 8 || eff == 10 || eff == 12 || eff == 13 || eff == 14) {
                    if (eff != envType || par != envTone) {
                        envType = eff;
                        envTone = par;
                        o.push_back((uint8_t)(0x80 + eff));
                        o.push_back((uint8_t)par);
                    }
                } else if (eff == 1) {
                    o.push_back(0x82);
                    envType = 0;
                }
                const int value = half + 12 * (note & 7);
                o.push_back((uint8_t)(value > 0x5F ? 0x5F : value));
            }
            o.push_back(0xFF);
        }
    }
    o[1] = (uint8_t)(pos & 0xFF), o[2] = (uint8_t)(pos >> 8);
    o[3] = (uint8_t)(orn & 0xFF), o[4] = (uint8_t)(orn >> 8);
    o[5] = (uint8_t)(pat & 0xFF), o[6] = (uint8_t)(pat >> 8);
    o[25] = (uint8_t)(o.size() & 0xFF), o[26] = (uint8_t)(o.size() >> 8);
    return o;
}

// Big endian offset relative to its own position in Ay_Emul's ZXAY headers (-1 for none / outside)
long zxayPtr(const uint8_t* d, size_t n, size_t at) {
    if (at + 1 >= n) return -1;
    const int16_t rel = (int16_t)(d[at] << 8 | d[at + 1]);
    if (rel == 0) return -1;
    const long p = (long)at + rel;
    return p >= 0 && p < (long)n ? p : -1;
}

std::string zxayText(const uint8_t* d, size_t n, long at) {
    std::string s;
    for (long k = at; at >= 0 && k < (long)n && d[k] && s.size() < 64; ++k)
        s += (d[k] >= 32 && d[k] < 127) ? (char)d[k] : ' ';
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Structure checks

bool checkStc(const uint8_t* d, size_t n) {
    if (!d || n < 128 || n > 0x10000) return false;
    const long pos = w16(d, n, 1), orn = w16(d, n, 3), pat = w16(d, n, 5);
    if (d[0] < 1 || d[0] > 0x20) return false;
    if (!(27 + 99 <= pos && pos < orn && orn < pat && pat < (long)n)) return false;
    if ((pos - 27) % 99 != 0 || (pos - 27) / 99 > 16) return false;
    if (pat - orn < 33) return false;  // some compilers put a text between the ornaments and the pattern table
    const long npos = d[pos] + 1L, posEnd = pos + 1 + 2 * npos;  // positions may run into ornament 0
    if (posEnd != orn && !(posEnd > orn && posEnd < orn + 33)) return false;
    for (long a = 27; a < pos; a += 99)
        if (d[a] > 15) return false;
    if (pat - orn >= 66 && d[orn + 33] > 15) return false;  // ornament 1 (ornament 0 may be overlapped)
    // pattern table: up to 32 entries, numbers 1..32, ending with 0xFF; the streams lie in the file
    long entries = 0, lastStream = 0;
    long a = pat;
    for (;; a += 7, ++entries) {
        if (a >= (long)n) return false;
        if (d[a] == 0xFF) break;
        if (entries == 32 || a + 7 > (long)n || d[a] < 1 || d[a] > 32) return false;
        for (int c = 0; c < 3; ++c) {
            const long s = w16(d, n, (size_t)a + 1 + 2 * c);
            if (s <= a || s >= (long)n) return false;
            if (s > lastStream) lastStream = s;
        }
    }
    if (entries == 0 || d[lastStream - 1] != 0xFF) return false;
    // every position's pattern is in the table and its channel A data reaches the end mark in the file
    for (long i = 0; i < npos; ++i) {
        const uint8_t num = d[pos + 1 + 2 * i];
        long e = -1;
        for (long k = 0; k < entries && e < 0; ++k)
            if (d[pat + 7 * k] == num) e = pat + 7 * k;
        if (e < 0) return false;
        long s = w16(d, n, (size_t)e + 1);
        for (int guard = 0;; ++guard) {
            if (s >= (long)n || guard > 4096) return false;
            if (d[s] == 0xFF) break;
            s += (d[s] >= 0x83 && d[s] <= 0x8E) ? 2 : 1;
        }
    }
    return true;
}

bool checkPt2(const uint8_t* d, size_t n) {
    if (!d || n < 132 + 6 || n > 0x10000) return false;
    if (d[0] < 2) return false;  // speed
    const long pat = w16(d, n, 99);
    if (pat <= 132 || pat >= (long)n || d[pat - 1] != 0xFF) return false;
    if (w16(d, n, 3) != 0) return false;  // sample 0 is never used
    const long orn0 = w16(d, n, 67);
    if (orn0 < 131 || orn0 + 3 > (long)n || d[orn0] != 1 || d[orn0 + 1] != 0 || d[orn0 + 2] != 0) return false;
    // positions up to the 0xFF that precedes the pattern table
    long npos = 0, maxPat = 0;
    while (131 + npos < pat - 1 && d[131 + npos] != 0xFF) {
        if (d[131 + npos] >= 64 || npos == 255) return false;
        if (d[131 + npos] > maxPat) maxPat = d[131 + npos];
        ++npos;
    }
    if (npos == 0 || 131 + npos != pat - 1) return false;
    const long first = w16(d, n, (size_t)pat);
    if (first <= pat || (first - pat) % 6 != 2 || (first - pat) / 6 != maxPat + 1) return false;
    for (long k = 0; k < (maxPat + 1) * 3; ++k) {
        const long s = w16(d, n, (size_t)(pat + 2 * k));
        if (s < first || s >= (long)n) return false;
    }
    long lastOrn = 0;
    for (int k = 0; k < 16; ++k) {
        const long o = w16(d, n, 67 + 2 * (size_t)k);
        if (o != 0 && (o < 131 || o >= (long)n)) return false;
        if (o > lastOrn) lastOrn = o;
    }
    if (lastOrn + d[lastOrn] + 2 > (long)n + 1) return false;
    for (int k = 1; k < 32; ++k) {
        const long s = w16(d, n, 3 + 2 * (size_t)k);
        if (s != 0 && (s < 131 || s >= (long)n)) return false;
    }
    // every used pattern's channel A data reaches its end mark (0 at the start of a row) in the file
    for (long p = 0; p <= maxPat; ++p) {
        long s = w16(d, n, (size_t)(pat + 6 * p));
        for (int guard = 0;; ++guard) {
            if (s >= (long)n || guard > 4096) return false;
            if (d[s] == 0) break;
            for (int k = 0; k < kRowGuard; ++k) {  // one row
                if (s >= (long)n) return false;
                const uint8_t v = d[s++];
                if ((v >= 0x80 && v <= 0xE0) || v == 0x70) break;  // note, sound off, empty row
                if (v >= 0x71 && v <= 0x7E) s += 2;
                else if (v == 0x0D) s += 3;
                else if (v <= 0x0B || v == 0x0E || v == 0x0F) s += 1;
            }
        }
    }
    return true;
}

namespace {
// ASC header at offset o (0: version 0 without a loop byte, 1: with one)
bool ascCheck(const uint8_t* d, size_t n, int o) {
    if (!d || n < 9u + 64 + 64 || n > 0x10000) return false;
    const long pat = w16(d, n, 1 + (size_t)o), smp = w16(d, n, 3 + (size_t)o), orn = w16(d, n, 5 + (size_t)o);
    const long num = d[7 + o], posAt = 8 + o;
    if (num == 0) return false;
    if (o == 1) {
        const long delta = pat - num;
        if (delta < 9 || delta > 72) return false;
    } else if (pat != 8 + num) {
        return false;
    }
    if (pat + 6 > (long)n || smp + 64 > (long)n || orn + 64 > (long)n) return false;
    if (w16(d, n, (size_t)smp) != 0x40 || w16(d, n, (size_t)orn) != 0x40) return false;
    long maxPat = 0;
    for (long i = 0; i < num; ++i)
        if (d[posAt + i] > maxPat) maxPat = d[posAt + i];
    if (w16(d, n, (size_t)pat) != (maxPat + 1) * 6) return false;
    for (int k = 0; k < 32; ++k) {  // every sample and ornament lies in the file
        if (smp + w16(d, n, (size_t)smp + 2 * k) >= (long)n) return false;
        if (orn + w16(d, n, (size_t)orn + 2 * k) >= (long)n) return false;
    }
    long j = orn + w16(d, n, (size_t)orn + 0x3E);  // the last ornament ends inside the file
    while (j < (long)n && !(d[j] & 0x40)) j += 2;
    if (j >= (long)n) return false;
    // every used pattern's channels lie in the file and channel A reaches its end mark
    for (long p = 0; p <= maxPat; ++p) {
        for (int c = 0; c < 3; ++c) {
            const long s = pat + w16(d, n, (size_t)(pat + 6 * p + 2 * c));
            if (s < pat || s >= (long)n) return false;
        }
        long s = pat + w16(d, n, (size_t)(pat + 6 * p));
        bool env = false;
        for (int guard = 0;; ++guard) {
            if (s >= (long)n || guard > 8192) return false;
            const uint8_t v = d[s++];
            if (v == 0xFF) break;
            if (v <= 0x55 && env) ++s;
            else if (v == 0xE0) env = true;
            else if (v >= 0xE1 && v <= 0xEF) env = false;
            else if (v == 0xF0 || (v >= 0xF4 && v <= 0xF7) || v == 0xF9 || v == 0xFB) ++s;
        }
    }
    return true;
}
}  // namespace

bool checkAsc(const uint8_t* d, size_t n) { return ascCheck(d, n, 1) || ascCheck(d, n, 0); }

bool checkSqt(const uint8_t* d, size_t n) {
    if (!d || n < 17 || n > 0x10000) return false;
    const long smp = w16(d, n, 2), orn = w16(d, n, 4), pat = w16(d, n, 6), pos = w16(d, n, 8), loop = w16(d, n, 10);
    if (smp < 10 || orn <= smp || pat < orn || pos <= pat || loop < pos) return false;
    const long base = smp - 10;
    if (loop - base >= (long)n) return false;
    long j = pos - base, maxPat = 0;
    if (j < 0 || j >= (long)n || d[j] == 0) return false;
    for (int guard = 0; d[j] != 0; ++guard) {
        if (j + 7 >= (long)n || guard > 256) return false;
        for (int k = 0; k < 3; ++k)
            if ((d[j + 2 * k] & 0x7F) > maxPat) maxPat = d[j + 2 * k] & 0x7F;
        j += 7;
    }
    if (w16(d, n, 12) - pat - 2 != maxPat * 2) return false;
    long prev = w16(d, n, 12), at = 12;
    for (long k = 1; k <= (orn - smp) / 2; ++k) {  // samples are 0x62 bytes apart, ornament 1 follows them
        at += 2;
        const long v = w16(d, n, (size_t)at);
        if (v < 0 || v - prev != 0x62) return false;
        prev = v;
    }
    for (long k = 1; k <= (pat - orn) / 2; ++k) {  // ornaments are 0x22 bytes apart, pattern 1 follows them
        at += 2;
        const long v = w16(d, n, (size_t)at);
        if (v < 0 || v - prev != 0x22) return false;
        prev = v;
    }
    std::vector<uint8_t> r(d, d + n);
    if (!sqtRelocate(r)) return false;
    // every pattern of every position lies in the file
    const long rpat = w16(r.data(), n, 6);
    for (long p = w16(r.data(), n, 8); p >= 0 && p + 6 < (long)n && r[p] != 0; p += 7)
        for (int k = 0; k < 3; ++k) {
            const long s = w16(r.data(), n, (size_t)(rpat + (uint8_t)(r[p + 2 * k] * 2)));
            if (s < 0 || s >= (long)n) return false;
        }
    return true;
}

long findSt1(const uint8_t* d, size_t n) {
    if (!d || n > 0x10000) return -1;
    if (n >= 0x38 && std::memcmp(d, "ZXAYST11", 8) == 0) {
        const long songs = zxayPtr(d, n, 0x12);
        const long data = songs >= 0 ? zxayPtr(d, n, (size_t)songs + 2) : -1;  // channel map, length, fade
        if (data >= 0 && (size_t)data + 8 <= n && st1Raw(d + data + 8, n - (size_t)data - 8)) return data + 8;
        return st1Raw(d + 0x38, n - 0x38) ? 0x38 : -1;
    }
    return st1Raw(d, n) ? 0 : -1;
}

// ---------------------------------------------------------------------------------------------------------------
// Replayer factories

std::unique_ptr<Replayer> makeStc(const uint8_t* d, size_t n) {
    if (!checkStc(d, n)) return nullptr;
    auto r = std::make_unique<Stc>(d, n);
    if (!stcProgramName(r->m)) r->title = text(r->m, 7, 18);
    return r;
}

std::unique_ptr<Replayer> makePt2(const uint8_t* d, size_t n) {
    if (!checkPt2(d, n)) return nullptr;
    auto r = std::make_unique<Pt2>(d, n);
    r->title = text(r->m, 101, 30);
    return r;
}

std::unique_ptr<Replayer> makeAsc(const uint8_t* d, size_t n) {
    std::vector<uint8_t> v;
    if (ascCheck(d, n, 1)) {
        v.assign(d, d + n);
    } else if (ascCheck(d, n, 0) && n < 0x10000) {  // version 0: insert the loop position (0)
        v.reserve(n + 1);
        v.push_back(d[0]);
        v.push_back(0);
        v.insert(v.end(), d + 1, d + n);
        for (size_t a = 2; a <= 6; a += 2) {
            const int p = (v[a] | v[a + 1] << 8) + 1;
            v[a] = (uint8_t)(p & 0xFF);
            v[a + 1] = (uint8_t)(p >> 8);
        }
    } else {
        return nullptr;
    }
    auto r = std::make_unique<Asc>(std::move(v));
    const long pp = r->m.word(2), np = r->m.at(8);
    if (pp - np == 72) {  // "ASM COMPILATION OF " name " BY " author
        r->title = text(r->m, (uint32_t)(pp - 44), 20);
        r->author = text(r->m, (uint32_t)(pp - 20), 20);
    }
    return r;
}

std::unique_ptr<Replayer> makeSqt(const uint8_t* d, size_t n) {
    if (!checkSqt(d, n)) return nullptr;
    std::vector<uint8_t> v(d, d + n);
    if (!sqtRelocate(v)) return nullptr;
    return std::make_unique<Sqt>(std::move(v));
}

std::unique_ptr<Replayer> makeSt1(const uint8_t* d, size_t n) {
    const long at = findSt1(d, n);
    if (at < 0) return nullptr;
    const std::vector<uint8_t> stc = st1ToStc(d + at, n - (size_t)at);
    auto r = std::make_unique<Stc>(stc.data(), stc.size());
    if (at > 0) {  // the wrapper's song name and author
        const long songs = zxayPtr(d, n, 0x12);
        if (songs >= 0) r->title = zxayText(d, n, zxayPtr(d, n, (size_t)songs));
        r->author = zxayText(d, n, zxayPtr(d, n, 0x0C));
    }
    return r;
}

}  // namespace zxt
