// ZX Spectrum Pro Tracker 3.x / Vortex Tracker II (.pt3) player on ayumi. Playback only.
//
// Credits and licences:
//  - PT3Play, the original Pro Tracker 3 player, by Sergey Bulba (svbulba@gmail.com,
//    https://bulba.untergrund.net/vortex_e.htm). The note and volume tables below are his.
//  - The C# port in PT3PlayBlazor by Ben Baker (https://github.com/benbaker76/PT3PlayBlazor), released under
//    the MIT licence; it contains modified code from ayfly (https://github.com/l29ah/ayfly). This file follows
//    PT3PlayBlazor/AYAudio/PT3Play.cs (PT3_Init, PT3_PatternInterpreter, PT3_ChangeRegisters, PT3_Play_Chip).
//  - The AY chip itself is emulated by ayumi by Peter Sovietov (MIT, src/core/ayumi/LICENSE).
//
//  MIT licence (PT3PlayBlazor): Permission is hereby granted, free of charge, to any person obtaining a copy of
//  this software and associated documentation files (the "Software"), to deal in the Software without
//  restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute,
//  sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
//  do so, subject to the following conditions: The above copyright notice and this permission notice shall be
//  included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT
//  WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
//  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
//  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// Differences from the C# port:
//  - every read of the module goes through a bounds checked accessor (outside the file reads as 0), a row of
//    pattern data is capped, the module is validated at load and the length dry run is capped;
//  - the envelope period is Env_Base_hi * 256 + Env_Base_lo (the C# port reads a word at module offset
//    Env_Base_lo, a porting slip; ayfly uses the two bytes as one word);
//  - the "byte 0x62 is not a space" pattern renumbering is applied at every position change (as in ayfly),
//    not only for the first position, and dropped when it would point outside the file;
//  - TurboSound files (two modules for two AY chips, Vortex Tracker 2.5+) play the first module only;
//  - register writes go to ayumi once per 50 Hz frame instead of to Bulba's AY emulator.
#include "core/pt3_player.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "core/song.h"

extern "C" {
#include "core/ayumi/ayumi.h"
}

namespace {
// Table #0 of Pro Tracker 3.3x - 3.4r
const uint16_t kNotesPT33[96] = {0x0C21, 0x0B73, 0x0ACE, 0x0A33, 0x09A0, 0x0916, 0x0893, 0x0818, 0x07A4, 0x0736, 0x06CE, 0x066D, 0x0610, 0x05B9, 0x0567, 0x0519, 0x04D0, 0x048B, 0x0449, 0x040C, 0x03D2, 0x039B, 0x0367, 0x0336, 0x0308, 0x02DC, 0x02B3, 0x028C, 0x0268, 0x0245, 0x0224, 0x0206, 0x01E9, 0x01CD, 0x01B3, 0x019B, 0x0184, 0x016E, 0x0159, 0x0146, 0x0134, 0x0122, 0x0112, 0x0103, 0x00F4, 0x00E6, 0x00D9, 0x00CD, 0x00C2, 0x00B7, 0x00AC, 0x00A3, 0x009A, 0x0091, 0x0089, 0x0081, 0x007A, 0x0073, 0x006C, 0x0066, 0x0061, 0x005B, 0x0056, 0x0051, 0x004D, 0x0048, 0x0044, 0x0040, 0x003D, 0x0039, 0x0036, 0x0033, 0x0030, 0x002D, 0x002B, 0x0028, 0x0026, 0x0024, 0x0022, 0x0020, 0x001E, 0x001C, 0x001B, 0x0019, 0x0018, 0x0016, 0x0015, 0x0014, 0x0013, 0x0012, 0x0011, 0x0010, 0x000F, 0x000E, 0x000D, 0x000C};
// Table #0 of Pro Tracker 3.4x - 3.5x
const uint16_t kNotesPT34[96] = {0x0C22, 0x0B73, 0x0ACF, 0x0A33, 0x09A1, 0x0917, 0x0894, 0x0819, 0x07A4, 0x0737, 0x06CF, 0x066D, 0x0611, 0x05BA, 0x0567, 0x051A, 0x04D0, 0x048B, 0x044A, 0x040C, 0x03D2, 0x039B, 0x0367, 0x0337, 0x0308, 0x02DD, 0x02B4, 0x028D, 0x0268, 0x0246, 0x0225, 0x0206, 0x01E9, 0x01CE, 0x01B4, 0x019B, 0x0184, 0x016E, 0x015A, 0x0146, 0x0134, 0x0123, 0x0112, 0x0103, 0x00F5, 0x00E7, 0x00DA, 0x00CE, 0x00C2, 0x00B7, 0x00AD, 0x00A3, 0x009A, 0x0091, 0x0089, 0x0082, 0x007A, 0x0073, 0x006D, 0x0067, 0x0061, 0x005C, 0x0056, 0x0052, 0x004D, 0x0049, 0x0045, 0x0041, 0x003D, 0x003A, 0x0036, 0x0033, 0x0031, 0x002E, 0x002B, 0x0029, 0x0027, 0x0024, 0x0022, 0x0020, 0x001F, 0x001D, 0x001B, 0x001A, 0x0018, 0x0017, 0x0016, 0x0014, 0x0013, 0x0012, 0x0011, 0x0010, 0x000F, 0x000E, 0x000D, 0x000C};
// Table #1 of Pro Tracker 3.3x - 3.5x (Sound Tracker)
const uint16_t kNotesST[96] = {0x0EF8, 0x0E10, 0x0D60, 0x0C80, 0x0BD8, 0x0B28, 0x0A88, 0x09F0, 0x0960, 0x08E0, 0x0858, 0x07E0, 0x077C, 0x0708, 0x06B0, 0x0640, 0x05EC, 0x0594, 0x0544, 0x04F8, 0x04B0, 0x0470, 0x042C, 0x03FD, 0x03BE, 0x0384, 0x0358, 0x0320, 0x02F6, 0x02CA, 0x02A2, 0x027C, 0x0258, 0x0238, 0x0216, 0x01F8, 0x01DF, 0x01C2, 0x01AC, 0x0190, 0x017B, 0x0165, 0x0151, 0x013E, 0x012C, 0x011C, 0x010A, 0x00FC, 0x00EF, 0x00E1, 0x00D6, 0x00C8, 0x00BD, 0x00B2, 0x00A8, 0x009F, 0x0096, 0x008E, 0x0085, 0x007E, 0x0077, 0x0070, 0x006B, 0x0064, 0x005E, 0x0059, 0x0054, 0x004F, 0x004B, 0x0047, 0x0042, 0x003F, 0x003B, 0x0038, 0x0035, 0x0032, 0x002F, 0x002C, 0x002A, 0x0027, 0x0025, 0x0023, 0x0021, 0x001F, 0x001D, 0x001C, 0x001A, 0x0019, 0x0017, 0x0016, 0x0015, 0x0013, 0x0012, 0x0011, 0x0010, 0x000F};
// Table #2 of Pro Tracker 3.4r
const uint16_t kNotesASM34r[96] = {0x0D3E, 0x0C80, 0x0BCC, 0x0B22, 0x0A82, 0x09EC, 0x095C, 0x08D6, 0x0858, 0x07E0, 0x076E, 0x0704, 0x069F, 0x0640, 0x05E6, 0x0591, 0x0541, 0x04F6, 0x04AE, 0x046B, 0x042C, 0x03F0, 0x03B7, 0x0382, 0x034F, 0x0320, 0x02F3, 0x02C8, 0x02A1, 0x027B, 0x0257, 0x0236, 0x0216, 0x01F8, 0x01DC, 0x01C1, 0x01A8, 0x0190, 0x0179, 0x0164, 0x0150, 0x013D, 0x012C, 0x011B, 0x010B, 0x00FC, 0x00EE, 0x00E0, 0x00D4, 0x00C8, 0x00BD, 0x00B2, 0x00A8, 0x009F, 0x0096, 0x008D, 0x0085, 0x007E, 0x0077, 0x0070, 0x006A, 0x0064, 0x005E, 0x0059, 0x0054, 0x0050, 0x004B, 0x0047, 0x0043, 0x003F, 0x003C, 0x0038, 0x0035, 0x0032, 0x002F, 0x002D, 0x002A, 0x0028, 0x0026, 0x0024, 0x0022, 0x0020, 0x001E, 0x001D, 0x001B, 0x001A, 0x0019, 0x0018, 0x0015, 0x0014, 0x0013, 0x0012, 0x0011, 0x0010, 0x000F, 0x000E};
// Table #2 of Pro Tracker 3.4x - 3.5x
const uint16_t kNotesASM34[96] = {0x0D10, 0x0C55, 0x0BA4, 0x0AFC, 0x0A5F, 0x09CA, 0x093D, 0x08B8, 0x083B, 0x07C5, 0x0755, 0x06EC, 0x0688, 0x062A, 0x05D2, 0x057E, 0x052F, 0x04E5, 0x049E, 0x045C, 0x041D, 0x03E2, 0x03AB, 0x0376, 0x0344, 0x0315, 0x02E9, 0x02BF, 0x0298, 0x0272, 0x024F, 0x022E, 0x020F, 0x01F1, 0x01D5, 0x01BB, 0x01A2, 0x018B, 0x0174, 0x0160, 0x014C, 0x0139, 0x0128, 0x0117, 0x0107, 0x00F9, 0x00EB, 0x00DD, 0x00D1, 0x00C5, 0x00BA, 0x00B0, 0x00A6, 0x009D, 0x0094, 0x008C, 0x0084, 0x007C, 0x0075, 0x006F, 0x0069, 0x0063, 0x005D, 0x0058, 0x0053, 0x004E, 0x004A, 0x0046, 0x0042, 0x003E, 0x003B, 0x0037, 0x0034, 0x0031, 0x002F, 0x002C, 0x0029, 0x0027, 0x0025, 0x0023, 0x0021, 0x001F, 0x001D, 0x001C, 0x001A, 0x0019, 0x0017, 0x0016, 0x0015, 0x0014, 0x0012, 0x0011, 0x0010, 0x000F, 0x000E, 0x000D};
// Table #3 of Pro Tracker 3.4r
const uint16_t kNotesREAL34r[96] = {0x0CDA, 0x0C22, 0x0B73, 0x0ACF, 0x0A33, 0x09A1, 0x0917, 0x0894, 0x0819, 0x07A4, 0x0737, 0x06CF, 0x066D, 0x0611, 0x05BA, 0x0567, 0x051A, 0x04D0, 0x048B, 0x044A, 0x040C, 0x03D2, 0x039B, 0x0367, 0x0337, 0x0308, 0x02DD, 0x02B4, 0x028D, 0x0268, 0x0246, 0x0225, 0x0206, 0x01E9, 0x01CE, 0x01B4, 0x019B, 0x0184, 0x016E, 0x015A, 0x0146, 0x0134, 0x0123, 0x0113, 0x0103, 0x00F5, 0x00E7, 0x00DA, 0x00CE, 0x00C2, 0x00B7, 0x00AD, 0x00A3, 0x009A, 0x0091, 0x0089, 0x0082, 0x007A, 0x0073, 0x006D, 0x0067, 0x0061, 0x005C, 0x0056, 0x0052, 0x004D, 0x0049, 0x0045, 0x0041, 0x003D, 0x003A, 0x0036, 0x0033, 0x0031, 0x002E, 0x002B, 0x0029, 0x0027, 0x0024, 0x0022, 0x0020, 0x001F, 0x001D, 0x001B, 0x001A, 0x0018, 0x0017, 0x0016, 0x0014, 0x0013, 0x0012, 0x0011, 0x0010, 0x000F, 0x000E, 0x000D};
// Table #3 of Pro Tracker 3.4x - 3.5x
const uint16_t kNotesREAL34[96] = {0x0CDA, 0x0C22, 0x0B73, 0x0ACF, 0x0A33, 0x09A1, 0x0917, 0x0894, 0x0819, 0x07A4, 0x0737, 0x06CF, 0x066D, 0x0611, 0x05BA, 0x0567, 0x051A, 0x04D0, 0x048B, 0x044A, 0x040C, 0x03D2, 0x039B, 0x0367, 0x0337, 0x0308, 0x02DD, 0x02B4, 0x028D, 0x0268, 0x0246, 0x0225, 0x0206, 0x01E9, 0x01CE, 0x01B4, 0x019B, 0x0184, 0x016E, 0x015A, 0x0146, 0x0134, 0x0123, 0x0112, 0x0103, 0x00F5, 0x00E7, 0x00DA, 0x00CE, 0x00C2, 0x00B7, 0x00AD, 0x00A3, 0x009A, 0x0091, 0x0089, 0x0082, 0x007A, 0x0073, 0x006D, 0x0067, 0x0061, 0x005C, 0x0056, 0x0052, 0x004D, 0x0049, 0x0045, 0x0041, 0x003D, 0x003A, 0x0036, 0x0033, 0x0031, 0x002E, 0x002B, 0x0029, 0x0027, 0x0024, 0x0022, 0x0020, 0x001F, 0x001D, 0x001B, 0x001A, 0x0018, 0x0017, 0x0016, 0x0014, 0x0013, 0x0012, 0x0011, 0x0010, 0x000F, 0x000E, 0x000D};

// Volume tables: [channel volume][sample amplitude]. Pro Tracker 3.3x - 3.4x, then 3.5x.
const uint8_t kVol34[16][16] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2}, {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3},
    {0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4}, {0, 0, 0, 1, 1, 1, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5},
    {0, 0, 0, 1, 1, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6}, {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7},
    {0, 0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8}, {0, 0, 1, 1, 2, 3, 3, 4, 5, 5, 6, 6, 7, 8, 8, 9},
    {0, 0, 1, 2, 2, 3, 4, 4, 5, 6, 6, 7, 8, 8, 9, 10}, {0, 0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11},
    {0, 0, 1, 2, 3, 4, 4, 5, 6, 7, 8, 8, 9, 10, 11, 12}, {0, 0, 1, 2, 3, 4, 5, 6, 7, 7, 8, 9, 10, 11, 12, 13},
    {0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}};
const uint8_t kVol35[16][16] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2}, {0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3},
    {0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4}, {0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5},
    {0, 0, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6}, {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7},
    {0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8}, {0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 7, 7, 8, 8, 9},
    {0, 1, 1, 2, 3, 3, 4, 5, 5, 6, 7, 7, 8, 9, 9, 10}, {0, 1, 1, 2, 3, 4, 4, 5, 6, 7, 7, 8, 9, 10, 10, 11},
    {0, 1, 2, 2, 3, 4, 5, 6, 6, 7, 8, 9, 10, 10, 11, 12}, {0, 1, 2, 3, 3, 4, 5, 6, 7, 8, 9, 10, 10, 11, 12, 13},
    {0, 1, 2, 3, 4, 5, 6, 7, 7, 8, 9, 10, 11, 12, 13, 14}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}};

constexpr size_t kHeader = 0xC9;      // name, tables, then the position list
constexpr size_t kMaxModule = 0x10000;  // a module addresses itself with 16 bit offsets
constexpr int kMaxRowBytes = 256;     // pattern bytes read for one row before giving up on it
constexpr int kHz = 50;
constexpr int kMaxFrames = kHz * 60 * 30;  // the length dry run stops after 30 minutes
constexpr double kClock = 1773400;          // ZX Spectrum 128 AY

bool startsWith(const uint8_t* d, size_t n, const char* tag) {
    const size_t k = std::strlen(tag);
    return n >= k && std::memcmp(d, tag, k) == 0;
}

// The playing state of one module: PT3Play's PT3_Parameters + three PT3_Channel_Parameters.
struct Chan {
    uint16_t addr = 0, ornPtr = 0, smpPtr = 0, ton = 0;
    uint8_t ornLoop = 0, ornLen = 0, ornPos = 0, smpLoop = 0, smpLen = 0, smpPos = 0;
    uint8_t volume = 15, skipNotes = 0, note = 0, slideToNote = 0, amplitude = 0;
    bool envOn = false, enabled = false, simpleGliss = false;
    int ampSlide = 0, noiseSlide = 0, envSlide = 0;
    int tonSlideCount = 0, onOff = 0, onOffDelay = 0, offOnDelay = 0, tonSlideDelay = 0;
    int16_t tonSliding = 0, tonAcc = 0, tonSlideStep = 0, tonDelta = 0;
    int8_t skipCounter = 1;
};

struct State {
    uint8_t envBaseLo = 0, envBaseHi = 0;
    int16_t curEnvSlide = 0, envSlideAdd = 0;
    int8_t curEnvDelay = 0, envDelay = 0;
    uint8_t noiseBase = 0, delay = 3, addToNoise = 0, delayCounter = 1, position = 0;
    Chan ch[3];
    uint8_t regs[14] = {};
    int envShape = -1;  // >= 0: register 13 written this frame (restarts the envelope)
    bool wrapped = false;  // the last frame passed the end of the position list
};
}  // namespace

struct Pt3Player::Impl {
    std::vector<uint8_t> mod;
    int version = 6;
    bool mirror = false;  // byte 0x62 renumbers patterns (TurboSound second modules)
    State start, st;
    int length = 0;
    std::string title, author, format;
    bool loaded = false;

    struct ayumi ay;
    uint32_t rate = 48000;
    double frameLeft = 0;
    float vu[3] = {}, level[3] = {};
    float scope[3][kScopeLen] = {};
    uint32_t scopePos = 0;

    uint8_t at(uint32_t a) const { return a < mod.size() ? mod[a] : 0; }
    uint16_t word(uint32_t a) const { return (uint16_t)(at(a) | at(a + 1) << 8); }
    uint16_t patternsPtr() const { return word(0x67); }
    uint16_t samplePtr(int x) const { return word(0x69 + (uint32_t)x * 2); }
    uint16_t ornamentPtr(int x) const { return word(0xA9 + (uint32_t)x * 2); }
    int positions() const { return at(0x65); }
    int loopPos() const { return at(0x66) < positions() ? at(0x66) : 0; }

    // Pattern table offset (already * 3) for a position; -1 when it does not exist.
    int patternIndex(int pos, bool mir) const {
        if (kHeader + (size_t)pos >= mod.size()) return -1;
        int i = at((uint32_t)(kHeader + pos));
        if (mir) i = at(0x62) * 3 - 3 - i;
        return i;
    }
    bool positionOk(int pos, bool mir) const {
        const int i = patternIndex(pos, mir);
        if (i < 0) return false;
        const size_t table = (size_t)patternsPtr() + (size_t)i * 2;
        if (table + 6 > mod.size()) return false;
        for (int c = 0; c < 3; ++c)
            if (word((uint32_t)(table + c * 2)) >= mod.size()) return false;
        return true;
    }
    void enterPosition(State& s, int pos) const {
        const uint32_t table = (uint32_t)patternsPtr() + (uint32_t)patternIndex(pos, mirror) * 2;
        for (int c = 0; c < 3; ++c) s.ch[c].addr = word(table + (uint32_t)c * 2);
    }

    uint16_t noteFreq(int j) const {
        if (j < 0) j = 0;
        if (j > 95) j = 95;
        switch (at(0x63)) {
            case 0: return version <= 3 ? kNotesPT33[j] : kNotesPT34[j];
            case 1: return kNotesST[j];
            case 2: return version <= 3 ? kNotesASM34r[j] : kNotesASM34[j];
            default: return version <= 3 ? kNotesREAL34r[j] : kNotesREAL34[j];
        }
    }

    void setOrnament(Chan& c, int n) const {
        c.ornPtr = ornamentPtr(n);
        c.ornLoop = at(c.ornPtr);
        c.ornPtr++;
        c.ornLen = at(c.ornPtr);
        c.ornPtr++;
    }
    void setSample(Chan& c, int n) const {
        c.smpPtr = samplePtr(n);
        c.smpLoop = at(c.smpPtr);
        c.smpPtr++;
        c.smpLen = at(c.smpPtr);
        c.smpPtr++;
    }
    void writeEnvShape(State& s, int shape) const {
        s.envShape = shape & 15;
        s.regs[13] = (uint8_t)(shape & 15);
    }

    // PT3_PatternInterpreter: one row of one channel
    void interpret(State& s, Chan& c) const {
        const uint8_t prnote = c.note;
        const int16_t prsliding = c.tonSliding;
        int counter = 0, f1 = 0, f2 = 0, f3 = 0, f4 = 0, f5 = 0, f8 = 0, f9 = 0;
        bool quit = false;
        for (int guard = 0; !quit; ++guard) {
            if (guard >= kMaxRowBytes) {  // no note / rest / stop in sight: broken data, end the row here
                c.enabled = false;
                break;
            }
            const uint8_t v = at(c.addr);
            if (v >= 0xF0) {
                setOrnament(c, v - 0xF0);
                c.addr++;
                setSample(c, at(c.addr) / 2);
                c.envOn = false;
                c.ornPos = 0;
            } else if (v >= 0xD1) {
                setSample(c, v - 0xD0);
            } else if (v == 0xD0) {
                quit = true;
            } else if (v >= 0xC1) {
                c.volume = (uint8_t)(v - 0xC0);
            } else if (v == 0xC0) {
                c.smpPos = 0;
                c.ampSlide = c.noiseSlide = c.envSlide = 0;
                c.ornPos = 0;
                c.tonSlideCount = 0;
                c.tonSliding = 0;
                c.tonAcc = 0;
                c.onOff = 0;
                c.enabled = false;
                quit = true;
            } else if (v >= 0xB2) {
                c.envOn = true;
                writeEnvShape(s, v - 0xB1);
                c.addr++;
                s.envBaseHi = at(c.addr);
                c.addr++;
                s.envBaseLo = at(c.addr);
                c.ornPos = 0;
                s.curEnvSlide = 0;
                s.curEnvDelay = 0;
            } else if (v == 0xB1) {
                c.addr++;
                c.skipNotes = at(c.addr);
            } else if (v == 0xB0) {
                c.envOn = false;
                c.ornPos = 0;
            } else if (v >= 0x50) {
                c.note = (uint8_t)(v - 0x50);
                c.smpPos = 0;
                c.ampSlide = c.noiseSlide = c.envSlide = 0;
                c.ornPos = 0;
                c.tonSlideCount = 0;
                c.tonSliding = 0;
                c.tonAcc = 0;
                c.onOff = 0;
                c.enabled = true;
                quit = true;
            } else if (v >= 0x40) {
                setOrnament(c, v - 0x40);
                c.ornPos = 0;
            } else if (v >= 0x20) {
                s.noiseBase = (uint8_t)(v - 0x20);
            } else if (v >= 0x10) {
                if (v == 0x10) {
                    c.envOn = false;
                } else {
                    writeEnvShape(s, v - 0x10);
                    c.addr++;
                    s.envBaseHi = at(c.addr);
                    c.addr++;
                    s.envBaseLo = at(c.addr);
                    c.envOn = true;
                    s.curEnvSlide = 0;
                    s.curEnvDelay = 0;
                }
                c.addr++;
                setSample(c, at(c.addr) / 2);
                c.ornPos = 0;
            } else if (v == 9) {
                f9 = ++counter;
            } else if (v == 8) {
                f8 = ++counter;
            } else if (v == 5) {
                f5 = ++counter;
            } else if (v == 4) {
                f4 = ++counter;
            } else if (v == 3) {
                f3 = ++counter;
            } else if (v == 2) {
                f2 = ++counter;
            } else if (v == 1) {
                f1 = ++counter;
            }
            c.addr++;
        }
        for (; counter > 0; --counter) {  // effect parameters follow the row, last effect first
            if (counter == f1) {  // glissando
                c.tonSlideDelay = at(c.addr);
                c.tonSlideCount = c.tonSlideDelay;
                c.addr++;
                c.tonSlideStep = (int16_t)word(c.addr);
                c.addr += 2;
                c.simpleGliss = true;
                c.onOff = 0;
                if (c.tonSlideCount == 0 && version >= 7) c.tonSlideCount++;
            } else if (counter == f2) {  // portamento to the note
                c.simpleGliss = false;
                c.onOff = 0;
                c.tonSlideDelay = at(c.addr);
                c.tonSlideCount = c.tonSlideDelay;
                c.addr += 3;
                const int16_t step = (int16_t)word(c.addr);
                c.tonSlideStep = (int16_t)(step < 0 ? -step : step);
                c.addr += 2;
                c.tonDelta = (int16_t)(noteFreq(c.note) - noteFreq(prnote));
                c.slideToNote = c.note;
                c.note = prnote;
                if (version >= 6) c.tonSliding = prsliding;
                if (c.tonDelta - c.tonSliding < 0) c.tonSlideStep = (int16_t)-c.tonSlideStep;
            } else if (counter == f3) {  // sample offset
                c.smpPos = at(c.addr);
                c.addr++;
            } else if (counter == f4) {  // ornament offset
                c.ornPos = at(c.addr);
                c.addr++;
            } else if (counter == f5) {  // vibrato (on / off)
                c.onOffDelay = at(c.addr);
                c.addr++;
                c.offOnDelay = at(c.addr);
                c.onOff = c.onOffDelay;
                c.addr++;
                c.tonSlideCount = 0;
                c.tonSliding = 0;
            } else if (counter == f8) {  // envelope slide
                s.envDelay = (int8_t)at(c.addr);
                s.curEnvDelay = s.envDelay;
                c.addr++;
                s.envSlideAdd = (int16_t)word(c.addr);
                c.addr += 2;
            } else if (counter == f9) {  // speed
                s.delay = at(c.addr);
                c.addr++;
            }
        }
        c.skipCounter = (int8_t)c.skipNotes;
    }

    // PT3_ChangeRegisters: one channel's sample / ornament step for this frame
    void changeRegisters(State& s, Chan& c, int& addToEnv, int& mixer) const {
        if (c.enabled) {
            const uint32_t sp = (uint32_t)c.smpPtr + (uint32_t)c.smpPos * 4;
            c.ton = (uint16_t)(word(sp + 2) + (uint16_t)c.tonAcc);
            const uint8_t b0 = at(sp), b1 = at(sp + 1);
            if (b1 & 0x40) c.tonAcc = (int16_t)c.ton;
            int j = (uint8_t)(c.note + at((uint32_t)c.ornPtr + c.ornPos));
            if ((int8_t)j < 0) j = 0;
            else if (j > 95) j = 95;
            c.ton = (uint16_t)((c.ton + c.tonSliding + noteFreq(j)) & 0xFFF);
            if (c.tonSlideCount > 0) {
                if (--c.tonSlideCount == 0) {
                    c.tonSliding = (int16_t)(c.tonSliding + c.tonSlideStep);
                    c.tonSlideCount = c.tonSlideDelay;
                    if (!c.simpleGliss && ((c.tonSlideStep < 0 && c.tonSliding <= c.tonDelta) ||
                                           (c.tonSlideStep >= 0 && c.tonSliding >= c.tonDelta))) {
                        c.note = c.slideToNote;
                        c.tonSlideCount = 0;
                        c.tonSliding = 0;
                    }
                }
            }
            if (b0 & 0x80) {
                if (b0 & 0x40) {
                    if (c.ampSlide < 15) c.ampSlide++;
                } else if (c.ampSlide > -15) {
                    c.ampSlide--;
                }
            }
            int amp = (b1 & 15) + c.ampSlide;
            amp = amp < 0 ? 0 : (amp > 15 ? 15 : amp);
            c.amplitude = version <= 4 ? kVol34[c.volume & 15][amp] : kVol35[c.volume & 15][amp];
            if (!(b0 & 1) && c.envOn) c.amplitude |= 16;
            if (b1 & 0x80) {  // envelope period offset
                const uint8_t e = (uint8_t)(((b0 & 0x20) ? ((b0 >> 1) | 0xF0) : ((b0 >> 1) & 15)) + c.envSlide);
                if (b1 & 0x20) c.envSlide = e;
                addToEnv = (int8_t)(addToEnv + (int8_t)e);
            } else {  // noise period offset
                s.addToNoise = (uint8_t)((b0 >> 1) + c.noiseSlide);
                if (b1 & 0x20) c.noiseSlide = s.addToNoise;
            }
            mixer |= (b1 >> 1) & 0x48;
            if (++c.smpPos >= c.smpLen) c.smpPos = c.smpLoop;
            if (++c.ornPos >= c.ornLen) c.ornPos = c.ornLoop;
        } else {
            c.amplitude = 0;
        }
        mixer >>= 1;
        if (c.onOff > 0 && --c.onOff == 0) {
            c.enabled = !c.enabled;
            c.onOff = c.enabled ? c.onOffDelay : c.offOnDelay;
        }
    }

    // PT3_Play_Chip: one 50 Hz frame; fills s.regs
    void frame(State& s) const {
        s.envShape = -1;
        s.wrapped = false;
        if (--s.delayCounter == 0) {
            Chan& a = s.ch[0];
            if (--a.skipCounter == 0) {
                if (at(a.addr) == 0) {  // end of the pattern: next position
                    s.position++;
                    if (s.position >= positions()) {
                        s.position = (uint8_t)loopPos();
                        s.wrapped = true;
                    }
                    enterPosition(s, s.position);
                    s.noiseBase = 0;
                }
                interpret(s, a);
            }
            for (int c = 1; c < 3; ++c)
                if (--s.ch[c].skipCounter == 0) interpret(s, s.ch[c]);
            s.delayCounter = s.delay;
        }
        int addToEnv = 0, mixer = 0;
        for (Chan& c : s.ch) changeRegisters(s, c, addToEnv, mixer);
        s.regs[7] = (uint8_t)mixer;
        for (int c = 0; c < 3; ++c) {
            s.regs[c * 2] = (uint8_t)(s.ch[c].ton & 0xFF);
            s.regs[c * 2 + 1] = (uint8_t)((s.ch[c].ton >> 8) & 15);
            s.regs[8 + c] = s.ch[c].amplitude;
        }
        s.regs[6] = (uint8_t)((s.noiseBase + s.addToNoise) & 31);
        const uint16_t env = (uint16_t)((s.envBaseHi << 8 | s.envBaseLo) + addToEnv + s.curEnvSlide);
        s.regs[11] = (uint8_t)(env & 0xFF);
        s.regs[12] = (uint8_t)(env >> 8);
        if (s.curEnvDelay > 0 && --s.curEnvDelay == 0) {
            s.curEnvDelay = s.envDelay;
            s.curEnvSlide = (int16_t)(s.curEnvSlide + s.envSlideAdd);
        }
    }

    // PT3_Init: every channel starts on ornament 0 and sample 1 at the first position
    void init(State& s) const {
        s = State{};
        s.delay = at(0x64);
        s.delayCounter = 1;
        enterPosition(s, 0);
        Chan proto;
        setOrnament(proto, 0);
        setSample(proto, 1);
        proto.volume = 15;
        proto.skipCounter = 1;
        for (int c = 0; c < 3; ++c) {
            const uint16_t addr = s.ch[c].addr;
            s.ch[c] = proto;
            s.ch[c].addr = addr;
        }
    }

    void configure() {
        std::memset(&ay, 0, sizeof ay);
        ayumi_configure(&ay, 0, kClock, (int)rate);
        static const double kPan[3] = {0.1, 0.5, 0.9};  // ABC stereo, as on the Spectrum 128 clones
        for (int c = 0; c < 3; ++c) ayumi_set_pan(&ay, c, kPan[c], 0);
        st = start;
        frameLeft = 0;
    }
    void apply(const State& s) {
        const uint8_t* r = s.regs;
        for (int c = 0; c < 3; ++c) {
            ayumi_set_tone(&ay, c, r[c * 2] | (r[c * 2 + 1] & 15) << 8);
            ayumi_set_mixer(&ay, c, (r[7] >> c) & 1, (r[7] >> (c + 3)) & 1, (r[8 + c] >> 4) & 1);
            ayumi_set_volume(&ay, c, r[8 + c] & 15);
            level[c] = (r[8 + c] & 16) ? 0.8f : (r[8 + c] & 15) / 15.f;
            if (((r[7] >> c) & 1) && ((r[7] >> (c + 3)) & 1)) level[c] = 0;  // tone and noise off: silent
        }
        ayumi_set_noise(&ay, r[6] & 31);
        ayumi_set_envelope(&ay, r[11] | r[12] << 8);
        if (s.envShape >= 0) ayumi_set_envelope_shape(&ay, s.envShape);
    }
};

Pt3Player::Pt3Player() : impl_(new Impl) {}
Pt3Player::~Pt3Player() = default;

bool Pt3Player::detect(const uint8_t* d, size_t n) {
    if (!d || n < kHeader + 1) return false;
    return startsWith(d, n, "ProTracker 3.") || startsWith(d, n, "Vortex Tracker II");
}

bool Pt3Player::load(const uint8_t* data, size_t size) {
    Impl& m = *impl_;
    m.loaded = false;
    if (!detect(data, size)) return false;
    // A TurboSound file is two modules back to back (the second for a second AY chip): offsets are relative to
    // each module's start, so the first module's data is the start of the file. Only the first one is played.
    m.mod.assign(data, data + (size < kMaxModule ? size : kMaxModule));
    const int npos = m.positions();
    if (npos == 0 || kHeader + (size_t)npos > m.mod.size()) return false;
    m.version = 6;
    if (m.mod[13] >= '0' && m.mod[13] <= '9') m.version = m.mod[13] - '0';
    // Byte 0x62 renumbers the patterns when it is not a space (TurboSound second modules); drop that when it
    // does not give valid patterns but the plain numbers do.
    auto allOk = [&](bool mir) {
        for (int p = 0; p < npos; ++p)
            if (!m.positionOk(p, mir)) return false;
        return true;
    };
    m.mirror = m.mod[0x62] != 0x20;
    if (!allOk(m.mirror)) {
        if (!m.mirror || !allOk(false)) return false;
        m.mirror = false;
    }

    auto field = [&](size_t from, size_t len) {
        std::string s;
        for (size_t k = from; k < from + len; ++k) s += (m.mod[k] >= 32 && m.mod[k] < 127) ? (char)m.mod[k] : ' ';
        while (!s.empty() && s.back() == ' ') s.pop_back();
        size_t b = 0;
        while (b < s.size() && s[b] == ' ') ++b;
        return s.substr(b);
    };
    m.title = field(0x1E, 32);
    m.author = std::memcmp(m.mod.data() + 0x3E, " by ", 4) == 0 ? field(0x42, 32) : std::string();
    const bool vortex = startsWith(data, size, "Vortex Tracker II");
    m.format = vortex ? "PT3 (VORTEX TRACKER II)" : "PT3 (PRO TRACKER 3." + std::string(1, (char)m.mod[13]) + ")";
    if (!vortex && (m.mod[13] < '0' || m.mod[13] > '9')) m.format = "PT3 (PRO TRACKER 3)";

    m.init(m.start);
    // Song length: play it silently until it wraps to the loop position (capped).
    State s = m.start;
    m.length = kMaxFrames;
    for (int f = 0; f < kMaxFrames; ++f) {
        m.frame(s);
        if (s.wrapped) {
            m.length = f;
            break;
        }
    }
    if (m.length <= 0) m.length = 1;
    m.configure();
    m.loaded = true;
    return true;
}

void Pt3Player::restart() {
    if (impl_->loaded) impl_->configure();
}

void Pt3Player::setRate(uint32_t hz) {
    impl_->rate = hz ? hz : 48000;
    if (impl_->loaded) impl_->configure();
}

void Pt3Player::clearMeters() {
    std::memset(impl_->vu, 0, sizeof impl_->vu);
    std::memset(impl_->scope, 0, sizeof impl_->scope);
}

void Pt3Player::render(float* out, uint32_t frames, float gain) {
    std::memset(out, 0, sizeof(float) * 2 * frames);
    Impl& m = *impl_;
    if (!m.loaded) return;
    for (uint32_t f = 0; f < frames; ++f) {
        if (m.frameLeft <= 0) {
            m.frame(m.st);
            m.apply(m.st);
            m.frameLeft += (double)m.rate / kHz;
        }
        m.frameLeft -= 1;
        ayumi_process(&m.ay);
        ayumi_remove_dc(&m.ay);
        out[2 * f] = (float)m.ay.left * gain * 1.4f;
        out[2 * f + 1] = (float)m.ay.right * gain * 1.4f;
        const float s = (float)(m.ay.left + m.ay.right);
        for (int c = 0; c < 3; ++c) m.scope[c][(m.scopePos + f) & (kScopeLen - 1)] = m.level[c] > 0 ? s : 0.f;
    }
    m.scopePos = (m.scopePos + frames) & (kScopeLen - 1);
    const float decay = (float)std::pow(0.5, (double)frames / (m.rate * 0.08));
    for (int c = 0; c < 3; ++c) {
        const float d = m.vu[c] * decay;
        m.vu[c] = m.level[c] > d ? m.level[c] : d;
    }
}

float Pt3Player::vu(int v) const { return v >= 0 && v < 3 ? impl_->vu[v] * 0.6f : 0.f; }

void Pt3Player::scope(int v, float* dst, int n) const {
    if (n <= 0) return;
    if (n > kScopeLen) n = kScopeLen;
    if (v < 0 || v >= 3) {
        for (int k = 0; k < n; ++k) dst[k] = 0;
        return;
    }
    const uint32_t from = (impl_->scopePos - (uint32_t)n) & (kScopeLen - 1);
    for (int k = 0; k < n; ++k) dst[k] = impl_->scope[v][(from + k) & (kScopeLen - 1)];
}

double Pt3Player::seconds() const { return impl_->loaded ? (double)impl_->length / kHz : 0.0; }
const char* Pt3Player::format() const { return impl_->loaded ? impl_->format.c_str() : ""; }
const char* Pt3Player::title() const { return impl_->title.c_str(); }
const char* Pt3Player::author() const { return impl_->author.c_str(); }
int Pt3Player::frames() const { return impl_->length; }

bool Pt3Player::buildDisplay(Song& out) {
    if (!impl_->loaded) return false;
    out = makeEmptySong(4);
    return true;
}
