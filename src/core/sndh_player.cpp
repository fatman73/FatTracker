#include "core/sndh_player.h"

#include <cmath>
#include <cstring>
#include <mutex>
#include <string>

#include "core/song.h"

extern "C" {
#include "core/ayumi/ayumi.h"
#include "core/m68k/m68k.h"
}

namespace {
uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

// ---------------------------------------------------------------- the emulated Atari ST
constexpr uint32_t kRam = 4u << 20;              // 4 MB
constexpr double kCpuHz = 8010613.0;             // PAL ST 68000 clock
constexpr double kMfpHz = 2457600.0;             // MFP 68901 timer clock
constexpr double kYmHz = 2000000.0;              // YM2149 clock
constexpr double kVblCycles = 160256.0;          // 313 lines x 512 cycles (50.05 Hz)
constexpr double kLineCycles = 512.0;            // one scan line (timer B event count)
constexpr double kHz200Cycles = kCpuHz / 200.0;  // the system timer tick (_hz_200)
constexpr int64_t kRunaway = (int64_t)kCpuHz;    // a play call (or a blocked one) longer than 1 s: runaway
constexpr int64_t kInitBudget = (int64_t)(kCpuHz * 5);  // init may take 5 ST seconds
constexpr uint32_t kHeap = 0x200000;             // Malloc() hands out memory from here to the top
constexpr uint32_t kMaxImage = kHeap - SndhPlayer::kLoadAddress;
constexpr uint32_t kStackTop = SndhPlayer::kLoadAddress - 0x100;  // supervisor stack below the tune
constexpr uint32_t kUserStack = 0x8000;
constexpr uint32_t kScreen = kRam - 0x8000;      // the (unseen) screen at the top of memory
// code stubs in low memory (the "ROM" of this machine)
constexpr uint32_t kIdle = 0x800;     // BRA.S *        - the main program waits here between calls
constexpr uint32_t kRte = 0x804;      // RTE            - default for interrupts and traps
constexpr uint32_t kHalt = 0x808;     // STOP #$2700    - crash vectors (bus / address error, illegal, line A/F)
constexpr uint32_t kMfpStubs = 0x820; // 16 x (clear the in-service bit; RTE) - default MFP vectors
constexpr uint32_t kOsHeader = 0xA00; // a fake TOS header (_sysbase)
constexpr uint32_t kKbdVecs = 0xB00;  // Kbdvbase() table
constexpr uint32_t kIorec = 0xB80;
constexpr uint32_t kDta = 0xC00;

constexpr int kPrescale[8] = {0, 4, 10, 16, 50, 64, 100, 200};
constexpr int kTimerChannel[4] = {13, 8, 5, 4};  // MFP interrupt channels of timers A, B, C, D

std::mutex g_cpuMutex;  // Musashi keeps one CPU in globals: one machine runs at a time
std::once_flag g_cpuOnce;
std::vector<uint8_t> g_cpuTemplate;  // the CPU context right after m68k_init (callbacks set, 68000)
}  // namespace

// ---------------------------------------------------------------- ICE! 2.x depacker
// The packed stream is read from its end backwards (info bytes and literal bytes interleaved); the output is
// written from its end backwards. Literal runs, back references and an optional bit plane ("picture") pass.
bool iceDepack(const uint8_t* d, size_t n, std::vector<uint8_t>& out) {
    if (!d || n < 13 || std::memcmp(d, "ICE!", 4) != 0) return false;
    const uint32_t packed = be32(d + 4), size = be32(d + 8);
    if (packed < 13 || packed > n || size == 0 || size > kRam) return false;
    out.assign(size, 0);
    uint8_t* dst = out.data();
    size_t src = packed, a6 = size;
    bool bad = false;
    auto byte = [&]() -> uint8_t {
        if (src <= 12) {
            bad = true;
            return 0;
        }
        return d[--src];
    };
    uint8_t d7 = byte();
    auto bit = [&]() -> int {  // ADD.B D7,D7 / refill with ADDX: the lowest set bit marks the end
        int c = d7 >> 7;
        d7 = (uint8_t)(d7 << 1);
        if (!d7) {
            const uint8_t b = byte();
            c = b >> 7;
            d7 = (uint8_t)(b << 1 | 1);
        }
        return c;
    };
    auto bits = [&](int count) {
        int v = 0;
        for (int i = 0; i < count; ++i) v = v << 1 | bit();
        return v;
    };
    static const int kLitBits[5] = {15, 8, 3, 2, 2}, kLitMask[5] = {0x7FFF, 0xFF, 7, 3, 3},
                     kLitBase[5] = {269, 14, 7, 4, 1};
    static const int kLenBits[5] = {10, 2, 1, 0, 0}, kLenBase[5] = {8, 4, 2, 1, 0};
    static const int kOffBits[3] = {12, 5, 8}, kOffBase[3] = {0x11F, -1, 0x1F};
    for (;;) {
        if (bit()) {  // literal bytes
            int count = 0;
            if (bit()) {
                int i = 4;
                for (;;) {
                    const int v = bits(kLitBits[i]);
                    if (v != kLitMask[i] || i == 0) {
                        count = v + kLitBase[i];
                        break;
                    }
                    --i;
                }
            }
            if ((size_t)count + 1 > a6) return false;
            for (int k = 0; k <= count; ++k) dst[--a6] = byte();
        }
        if (bad) return false;
        if (a6 == 0) break;
        int d2 = 3;  // string length: up to four 1 bits select the table entry
        while (d2 >= 0 && bit()) --d2;
        const int li = d2 + 1;
        const int l2 = kLenBase[li] + (kLenBits[li] ? bits(kLenBits[li]) : 0);  // length - 2
        int off;
        if (l2 == 0) {
            const bool far = bit();
            off = bits(far ? 9 : 6) + (far ? 63 : -1);
        } else {
            int e = 1;
            while (e >= 0 && bit()) --e;
            off = bits(kOffBits[e + 1]) + kOffBase[e + 1];
            if (off < 0) off -= l2;
        }
        if (bad) return false;
        const int len = l2 + 2;
        const long long from = (long long)a6 + 2 + l2 + off;  // copied downwards from just below `from`
        if ((size_t)len > a6 || from > (long long)size || from - len < 0) return false;
        size_t a1 = (size_t)from;
        for (int k = 0; k < len; ++k) dst[--a6] = dst[--a1];
    }
    if (bit() && !bad && size >= 32000) {  // picture: 32000 bytes of interleaved bit planes
        size_t a3 = size;
        for (int it = 0; it < 4000; ++it) {
            uint16_t pl[4] = {};
            for (int w = 0; w < 4; ++w) {
                a3 -= 2;
                uint16_t v = be16(dst + a3);
                for (int b = 0; b < 4; ++b)
                    for (int r = 0; r < 4; ++r) {
                        pl[r] = (uint16_t)(pl[r] << 1 | v >> 15);
                        v = (uint16_t)(v << 1);
                    }
            }
            for (int r = 0; r < 4; ++r) dst[a3 + r * 2] = (uint8_t)(pl[r] >> 8), dst[a3 + r * 2 + 1] = (uint8_t)pl[r];
        }
    }
    return true;
}

// ---------------------------------------------------------------- the player
struct SndhPlayer::Impl {
    // the file
    std::vector<uint8_t> image;  // unpacked SNDH
    std::string title, composer, format;
    std::vector<std::string> names;
    std::vector<uint32_t> frames;  // FRMS per sub-song
    std::vector<uint16_t> times;   // TIME per sub-song (seconds)
    int songs = 1, defSong = 0, hz = 50;
    int song = 0;
    bool loaded = false;

    // the machine
    std::vector<uint8_t> ram;
    std::vector<uint8_t> shadow;  // $FF8000-$FF8FFF: video, DMA sound, ... (written values read back)
    std::vector<uint8_t> ctx;     // this machine's 68000 while another one runs
    uint8_t ymReg[16] = {};
    int ymSel = 0;
    struct Timer {
        int mode = 0;        // control 0 stop, 1-7 delay (prescaler), 8 event count, 9-15 pulse width
        int data = 0;        // reload value (0 = 256)
        int counter = 0;     // main counter when stopped
        double left = 0;     // CPU cycles to the next underflow while running
    } timer[4];
    uint8_t mfp[24] = {};
    bool vblPending = false;
    int64_t now = 0;                     // CPU cycles since boot
    double tickLeft = 0, vblLeft = 0, hz200Left = 0;
    double owed = 0;                     // cycles executed beyond what was asked (an instruction overshoots)
    bool callActive = false, tickPending = false, dead = false, inInit = false, dmaUsed = false;
    int64_t callStart = 0, pendingSince = 0;
    uint32_t heapPtr = kHeap;
    uint32_t mfpServed = 0;

    // the sound
    struct ayumi ay;
    uint32_t rate = 48000;
    double cycleAcc = 0;
    float vu[3] = {}, level[3] = {};
    float scope[3][kScopeLen] = {};
    uint32_t scopePos = 0;

    // ------------------------------------------------ memory map
    uint8_t ymRead() const { return ymSel < 16 ? ymReg[ymSel] : 0xFF; }
    void ymWrite(int r, uint8_t v) {
        static const uint8_t kMask[16] = {0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0xFF,
                                          0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF};
        if (r < 0 || r > 15) return;
        v &= kMask[r];
        ymReg[r] = v;
        if (dead) return;
        switch (r) {
            case 0: case 1: case 2: case 3: case 4: case 5:
                ayumi_set_tone(&ay, r >> 1, ymReg[r & ~1] | (ymReg[r | 1] & 15) << 8);
                break;
            case 6: ayumi_set_noise(&ay, v & 31); break;
            case 7: for (int c = 0; c < 3; ++c) mixer(c); break;
            case 8: case 9: case 10:
                mixer(r - 8);
                ayumi_set_volume(&ay, r - 8, v & 15);
                break;
            case 11: case 12: ayumi_set_envelope(&ay, ymReg[11] | ymReg[12] << 8); break;
            case 13: ayumi_set_envelope_shape(&ay, v & 15); break;
            default: break;
        }
    }
    void mixer(int c) {
        ayumi_set_mixer(&ay, c, (ymReg[7] >> c) & 1, (ymReg[7] >> (c + 3)) & 1, (ymReg[8 + c] >> 4) & 1);
    }

    uint8_t read8(uint32_t a) {
        a &= 0xFFFFFF;
        if (a < kRam) return ram[a];
        if (a >= 0xFF8800 && a < 0xFF8900) return (a & 3) == 0 ? ymRead() : 0xFF;
        if (a >= 0xFFFA00 && a < 0xFFFA40) return (a & 1) && a < 0xFFFA30 ? mfpRead((a & 0x3F) >> 1) : 0xFF;
        if (a >= 0xFF8000 && a < 0xFF9000) return shadow[a - 0xFF8000];
        if (a >= 0xFFFC00 && a < 0xFFFC08) return (a & 3) == 0 ? 0x02 : 0;  // ACIA: ready to send, nothing in
        return 0;
    }
    void write8(uint32_t a, uint8_t v) {
        a &= 0xFFFFFF;
        if (a < kRam) {
            ram[a] = v;
            return;
        }
        if (a >= 0xFF8800 && a < 0xFF8900) {
            if ((a & 3) == 0) ymSel = v < 16 ? v : 0xFF;
            else if ((a & 3) == 2 && ymSel < 16) ymWrite(ymSel, v);
            return;
        }
        if (a >= 0xFFFA00 && a < 0xFFFA40) {
            if ((a & 1) && a < 0xFFFA30) mfpWrite((a & 0x3F) >> 1, v);
            return;
        }
        if (a >= 0xFF8000 && a < 0xFF9000) {
            shadow[a - 0xFF8000] = v;
            if (a == 0xFF8901 && (v & 1)) dmaUsed = true;  // STe DMA sound started (not played)
        }
    }
    uint32_t read16(uint32_t a) {
        a &= 0xFFFFFF;
        if (a + 1 < kRam) return (uint32_t)ram[a] << 8 | ram[a + 1];
        return (uint32_t)read8(a) << 8 | read8(a + 1);
    }
    uint32_t read32(uint32_t a) {
        a &= 0xFFFFFF;
        if (a + 3 < kRam) return be32(&ram[a]);
        return read16(a) << 16 | read16(a + 2);
    }
    void write16(uint32_t a, uint32_t v) {
        a &= 0xFFFFFF;
        if (a + 1 < kRam) {
            ram[a] = (uint8_t)(v >> 8), ram[a + 1] = (uint8_t)v;
            return;
        }
        write8(a, (uint8_t)(v >> 8)), write8(a + 1, (uint8_t)v);
    }
    void write32(uint32_t a, uint32_t v) {
        write16(a, v >> 16);
        write16(a + 2, v & 0xFFFF);
    }

    // ------------------------------------------------ MFP 68901
    enum { GPIP, AER, DDR, IERA, IERB, IPRA, IPRB, ISRA, ISRB, IMRA, IMRB, VR, TACR, TBCR, TCDCR, TADR, TBDR, TCDR, TDDR };
    uint16_t pair(int a) const { return (uint16_t)(mfp[a] << 8 | mfp[a + 1]); }
    void setPair(int a, uint16_t v) { mfp[a] = (uint8_t)(v >> 8), mfp[a + 1] = (uint8_t)v; }
    double countCycles(int t) const {  // CPU cycles per timer count
        const int m = timer[t].mode;
        if (m == 8) return t == 1 ? kLineCycles : 0;  // event count: timer B counts scan lines, A has no input
        return kPrescale[m & 7] * kCpuHz / kMfpHz;
    }
    int reload(int t) const { return timer[t].data ? timer[t].data : 256; }
    int counterNow(int t) const {
        if (!timer[t].mode) return timer[t].counter;
        const double cc = countCycles(t);
        if (cc <= 0) return timer[t].counter;
        int c = (int)std::ceil(timer[t].left / cc);
        if (c < 1) c = 1;
        if (c > 256) c = 256;
        return c & 0xFF;
    }
    void setMode(int t, int mode) {
        Timer& tm = timer[t];
        if (mode == tm.mode) return;
        if (tm.mode) tm.counter = counterNow(t);
        tm.mode = mode;
        if (mode) {
            const double cc = countCycles(t);
            tm.left = cc > 0 ? (tm.counter ? tm.counter : 256) * cc : 1e300;
        }
    }
    uint8_t mfpRead(int r) {
        switch (r) {
            case GPIP: return (uint8_t)(mfp[GPIP] | 0xFF);  // nothing pulls an input line low (colour monitor)
            case TACR: return (uint8_t)timer[0].mode;
            case TBCR: return (uint8_t)timer[1].mode;
            case TCDCR: return (uint8_t)(timer[2].mode << 4 | timer[3].mode);
            case TADR: case TBDR: case TCDR: case TDDR: return (uint8_t)counterNow(r - TADR);
            case 22: return 0x80;  // USART transmitter empty
            default: return r < 24 ? mfp[r] : 0xFF;
        }
    }
    void mfpWrite(int r, uint8_t v) {
        switch (r) {
            case IERA: case IERB:
                mfp[r] = v;
                mfp[r + 2] &= v;  // a disabled channel loses its pending request
                break;
            case IPRA: case IPRB: case ISRA: case ISRB: mfp[r] &= v; break;  // writing 0 clears
            case VR:
                mfp[VR] = v;
                if (!(v & 8)) mfp[ISRA] = mfp[ISRB] = 0;
                break;
            case TACR: setMode(0, v & 15); break;
            case TBCR: setMode(1, v & 15); break;
            case TCDCR: setMode(2, (v >> 4) & 7), setMode(3, v & 7); break;
            case TADR: case TBDR: case TCDR: case TDDR: {
                Timer& tm = timer[r - TADR];
                tm.data = v;
                if (!tm.mode) tm.counter = v;
                break;
            }
            default:
                if (r < 24) mfp[r] = v;
                break;
        }
        updateIrq();
    }
    void underflow(int t) {
        const int ch = kTimerChannel[t];
        const uint16_t bit = (uint16_t)(1u << ch);
        if (pair(IERA) & bit) setPair(IPRA, pair(IPRA) | bit);
    }
    static int highest(uint16_t v) {
        for (int i = 15; i >= 0; --i)
            if (v & (1u << i)) return i;
        return -1;
    }
    int mfpRequest() const {  // the channel the MFP asks the CPU to serve, -1 none
        const int p = highest(pair(IPRA) & pair(IMRA));
        return p >= 0 && p > highest(pair(ISRA)) ? p : -1;
    }
    void updateIrq() const { m68k_set_irq(mfpRequest() >= 0 ? 6 : (vblPending ? 4 : 0)); }
    int intAck(int lvl) {
        int vec = M68K_INT_ACK_AUTOVECTOR;
        if (lvl == 6) {
            const int ch = mfpRequest();
            if (ch < 0) {
                vec = M68K_INT_ACK_SPURIOUS;
            } else {
                setPair(IPRA, (uint16_t)(pair(IPRA) & ~(1u << ch)));
                if (mfp[VR] & 8) setPair(ISRA, (uint16_t)(pair(ISRA) | 1u << ch));
                vec = (mfp[VR] & 0xF0) | ch;
                ++mfpServed;
            }
        } else if (lvl == 4) {
            vblPending = false;
        }
        updateIrq();
        return vec;
    }

    // ------------------------------------------------ TOS: GEMDOS (trap #1), BIOS (#13), XBIOS (#14)
    uint32_t reg(m68k_register_t r) const { return m68k_get_reg(nullptr, r); }
    int trap(int t) {
        if (t != 1 && t != 13 && t != 14) return 0;
        if ((read32(0x80 + (uint32_t)t * 4) & 0xFFFFFF) != kRte) return 0;  // the tune has its own handler
        const uint32_t sp = reg(M68K_REG_A7);
        const int fn = (int)read16(sp);
        uint32_t d0 = 0;
        if (t == 1) {
            switch (fn) {
                case 0x20: d0 = read32(sp + 2) == 1 ? 0xFFFFFFFF : sp; break;  // Super: already supervisor
                case 0x30: d0 = 0x1300; break;                                 // Sversion
                case 0x2F: d0 = kDta; break;                                   // Fgetdta
                case 0x3D: d0 = (uint32_t)-33; break;                          // Fopen: file not found
                case 0x44: case 0x48: {                                        // Mxalloc / Malloc
                    const uint32_t size = read32(sp + 2);
                    if (size == 0xFFFFFFFF) {
                        d0 = kRam - heapPtr;
                    } else if (size <= kRam - heapPtr) {
                        d0 = heapPtr;
                        heapPtr += (size + 1) & ~1u;
                    }
                    break;
                }
                default: break;
            }
        } else if (t == 13) {
            switch (fn) {
                case 5: {  // Setexc
                    const uint32_t a = (read16(sp + 2) & 0xFF) * 4u;
                    d0 = read32(a);
                    const uint32_t v = read32(sp + 4);
                    if (v != 0xFFFFFFFF) write32(a, v);
                    break;
                }
                case 6: d0 = 20; break;          // Tickcal: ms per tick
                case 8: d0 = 0xFFFFFFFF; break;  // Bcostat: ready
                default: break;
            }
        } else {
            switch (fn) {
                case 2: case 3: d0 = kScreen; break;  // Physbase / Logbase
                case 13: {                            // Mfpint
                    const uint32_t n = read16(sp + 2) & 15;
                    write32((uint32_t)((mfp[VR] & 0xF0) | n) * 4, read32(sp + 4));
                    break;
                }
                case 14: d0 = kIorec; break;
                case 17: d0 = (uint32_t)(now * 2654435761u) & 0xFFFFFF; break;  // Random
                case 26: case 27: {  // Jdisint / Jenabint
                    const uint16_t bit = (uint16_t)(1u << (read16(sp + 2) & 15));
                    if (fn == 26) {
                        setPair(IERA, (uint16_t)(pair(IERA) & ~bit));
                        setPair(IPRA, (uint16_t)(pair(IPRA) & ~bit));
                        setPair(ISRA, (uint16_t)(pair(ISRA) & ~bit));
                    } else {
                        setPair(IERA, (uint16_t)(pair(IERA) | bit));
                        setPair(IMRA, (uint16_t)(pair(IMRA) | bit));
                    }
                    updateIrq();
                    break;
                }
                case 28: {  // Giaccess
                    const int r = (int)(read16(sp + 4) & 0xFF);
                    if (r & 0x80) ymWrite(r & 15, (uint8_t)read16(sp + 2));
                    d0 = ymReg[r & 15];
                    break;
                }
                case 29: ymWrite(14, (uint8_t)(ymReg[14] & read16(sp + 2))); break;  // Offgibit
                case 30: ymWrite(14, (uint8_t)(ymReg[14] | read16(sp + 2))); break;  // Ongibit
                case 31: {  // Xbtimer(timer, control, data, vector)
                    const int tn = (int)(read16(sp + 2) & 3);
                    const int ctrl = (int)(read16(sp + 4) & 15), data = (int)(read16(sp + 6) & 0xFF);
                    const uint32_t vec = read32(sp + 8);
                    const int ch = kTimerChannel[tn];
                    setMode(tn, 0);
                    timer[tn].data = timer[tn].counter = data;
                    write32((uint32_t)((mfp[VR] & 0xF0) | ch) * 4, vec);
                    setMode(tn, tn >= 2 ? ctrl & 7 : ctrl);
                    setPair(IERA, (uint16_t)(pair(IERA) | 1u << ch));
                    setPair(IMRA, (uint16_t)(pair(IMRA) | 1u << ch));
                    updateIrq();
                    break;
                }
                case 34: d0 = kKbdVecs; break;  // Kbdvbase
                case 38: {                      // Supexec(func): call it (it returns behind the TRAP)
                    const uint32_t func = read32(sp + 2);
                    const uint32_t nsp = sp - 4;
                    write32(nsp, reg(M68K_REG_PC));
                    m68k_set_reg(M68K_REG_A7, nsp);
                    m68k_set_reg(M68K_REG_PC, func);
                    return 1;
                }
                default: break;
            }
        }
        m68k_set_reg(M68K_REG_D0, d0);
        return 1;
    }

    // ------------------------------------------------ running the machine
    uint32_t pc() const { return reg(M68K_REG_PC) & 0xFFFFFF; }
    bool atIdle() const { return pc() == kIdle; }
    void call(uint32_t entry, int d0) {  // the main program calls entry, which returns to the idle loop
        m68k_set_reg(M68K_REG_SR, 0x2300);
        write32(kStackTop - 4, kIdle);
        m68k_set_reg(M68K_REG_A7, kStackTop - 4);
        if (d0 >= 0) m68k_set_reg(M68K_REG_D0, (uint32_t)d0);
        m68k_set_reg(M68K_REG_PC, entry);
        callActive = true;
        callStart = now;
    }
    void crash() {  // a runaway program: silence until restart
        dead = true;
        callActive = tickPending = false;
        static const uint8_t quiet[16] = {0, 0, 0, 0, 0, 0, 0, 0x3F, 0, 0, 0, 0, 0, 0, 0, 0};
        for (int r = 0; r < 14; ++r) ymReg[r] = quiet[r];
        for (int c = 0; c < 3; ++c) {
            ayumi_set_volume(&ay, c, 0);
            ayumi_set_mixer(&ay, c, 1, 1, 0);
        }
        m68k_set_irq(0);
    }
    double tickPeriod() const { return kCpuHz / hz; }
    void onTick() {
        if (callActive || !atIdle()) {
            if (!tickPending) tickPending = true, pendingSince = now;
            return;
        }
        call(SndhPlayer::kLoadAddress + 8, -1);
    }
    // Runs the CPU for n cycles with the MFP timers, the VBL and the play calls on time.
    void run(double n) {
        n -= owed;
        owed = 0;
        while (n >= 1 && !dead) {
            double slice = n;
            if (!inInit && tickLeft < slice) slice = tickLeft;
            if (vblLeft < slice) slice = vblLeft;
            if (hz200Left < slice) slice = hz200Left;
            for (const Timer& t : timer)
                if (t.mode && t.left < slice) slice = t.left;
            int ask = (int)std::ceil(slice);
            if (ask < 1) ask = 1;
            int used = m68k_execute(ask);
            if (used < 1) used = ask;
            now += used;
            n -= used;
            if (!inInit && (tickLeft -= used) <= 0) {
                tickLeft += tickPeriod();
                if (tickLeft < 1) tickLeft = 1;
                onTick();
            }
            if ((vblLeft -= used) <= 0) {
                vblLeft += kVblCycles;
                vblPending = true;
                write32(0x466, read32(0x466) + 1);  // _frclock
                write32(0x462, read32(0x462) + 1);  // _vbclock
            }
            if ((hz200Left -= used) <= 0) {
                hz200Left += kHz200Cycles;
                write32(0x4BA, read32(0x4BA) + 1);  // _hz_200
            }
            for (int t = 0; t < 4; ++t) {
                Timer& tm = timer[t];
                if (!tm.mode) continue;
                if ((tm.left -= used) <= 0) {
                    const double period = reload(t) * countCycles(t);
                    if (period <= 0) {
                        tm.left = 1e300;
                        continue;
                    }
                    tm.left += period;
                    if (tm.left <= 0) tm.left = period;  // more than one underflow in this slice: one request
                    underflow(t);
                }
            }
            if (callActive && atIdle()) callActive = false;
            if (tickPending && !callActive && atIdle()) {
                tickPending = false;
                call(SndhPlayer::kLoadAddress + 8, -1);
            }
            if (!inInit && ((callActive && now - callStart > kRunaway) || (tickPending && now - pendingSince > kRunaway)))
                crash();
            if (!dead) updateIrq();
        }
        if (n < 0) owed = -n;
    }

    void setupMemory() {
        ram.assign(kRam, 0);
        shadow.assign(0x1000, 0);
        std::memcpy(&ram[SndhPlayer::kLoadAddress], image.data(), image.size());
        auto w16 = [&](uint32_t a, uint16_t v) { ram[a] = (uint8_t)(v >> 8), ram[a + 1] = (uint8_t)v; };
        auto w32 = [&](uint32_t a, uint32_t v) { w16(a, (uint16_t)(v >> 16)), w16(a + 2, (uint16_t)v); };
        for (uint32_t v = 2; v < 256; ++v) w32(v * 4, kRte);
        w32(0, kStackTop);  // reset: SSP, PC
        w32(4, kIdle);
        for (uint32_t v : {2u, 3u, 4u, 8u, 10u, 11u}) w32(v * 4, kHalt);  // bus/address error, illegal, privilege, line A/F
        for (uint32_t ch = 0; ch < 16; ++ch) {                         // MFP vectors $40-$4F
            const uint32_t at = kMfpStubs + ch * 8;
            w32((0x40 + ch) * 4, at);
            w16(at, 0x11FC);                                              // MOVE.B #~bit,$FFFFFAxx.W (in-service off)
            w16(at + 2, (uint16_t)(~(1u << (ch & 7)) & 0xFF));
            w16(at + 4, ch >= 8 ? 0xFA0F : 0xFA11);
            w16(at + 6, 0x4E73);                                          // RTE
        }
        w16(kIdle, 0x60FE);
        w16(kRte, 0x4E73);
        w16(kHalt, 0x4E72), w16(kHalt + 2, 0x2700), w16(kHalt + 4, 0x60FA);
        w16(kOsHeader, 0x6030), w16(kOsHeader + 2, 0x0104);  // TOS 1.04
        w32(kOsHeader + 8, kOsHeader);
        w32(0x4F2, kOsHeader);   // _sysbase
        w32(0x42E, kRam);        // phystop
        w32(0x432, kHeap);       // _membot
        w32(0x436, kRam);        // _memtop
        w32(0x44E, kScreen);     // _v_bas_ad
        w16(0x452, 1);           // vblsem
        w16(0x454, 8);           // nvbls
        heapPtr = kHeap;
    }

    // A fresh machine running the tune's init for sub-song s. false: init ran away.
    bool boot(int s) {
        song = s;
        dead = callActive = tickPending = vblPending = dmaUsed = false;
        setupMemory();
        std::memset(ymReg, 0, sizeof ymReg);
        ymReg[7] = 0xFF;
        ymSel = 0;
        std::memset(mfp, 0, sizeof mfp);
        mfp[GPIP] = 0xFF;
        mfp[VR] = 0x48;                  // vectors from $100, software end of interrupt (as TOS sets it)
        mfp[IMRA] = mfp[IMRB] = 0xFF;    // masks open; nothing is enabled
        for (Timer& t : timer) t = Timer{};
        now = 0;
        owed = cycleAcc = 0;
        tickLeft = 0;
        vblLeft = kVblCycles;
        hz200Left = kHz200Cycles;
        configureSound();
        std::memset(vu, 0, sizeof vu);
        std::memset(level, 0, sizeof level);
        heapPtr = kHeap;
        mfpServed = 0;

        ctx = g_cpuTemplate;
        m68k_set_context(ctx.data());
        m68k_set_cpu_type(M68K_CPU_TYPE_68000);
        m68k_pulse_reset();  // SSP and PC from the vectors: supervisor, idle loop
        m68k_set_reg(M68K_REG_USP, kUserStack);
        inInit = true;
        call(SndhPlayer::kLoadAddress, s + 1);
        while (callActive && now < kInitBudget && !dead) run(20000);
        inInit = false;
        const bool ok = !callActive;
        if (!ok) crash();
        tickLeft = 0;  // the first play call right away
        m68k_get_context(ctx.data());
        return ok;
    }

    void configureSound() {
        std::memset(&ay, 0, sizeof ay);
        ayumi_configure(&ay, 1, kYmHz, (int)rate);
        for (int c = 0; c < 3; ++c) ayumi_set_pan(&ay, c, 0.5, 0);
        for (int r = 0; r < 14; ++r) {  // the registers as they are (shape only when it was written)
            if (r == 13) continue;
            const uint8_t v = ymReg[r];
            ymWrite(r, v);
        }
    }
    void levels() {
        for (int c = 0; c < 3; ++c) {
            const uint8_t v = ymReg[8 + c];
            const bool mixedOff = ((ymReg[7] >> c) & 1) && ((ymReg[7] >> (c + 3)) & 1);  // a digi voice: volume only
            float l = (v & 16) ? 0.8f : (v & 15) / 15.f;
            if (mixedOff && !(v & 16)) l *= 0.5f;
            if (dead) l = 0;
            if (l > level[c]) level[c] = l;
        }
    }

    bool parse(const uint8_t* d, size_t n) {
        title.clear(), composer.clear(), names.clear(), frames.clear(), times.clear();
        songs = 1, defSong = 0, hz = 50;
        const size_t end = n < 0x10000 ? n : 0x10000;
        size_t p = 16;
        auto str = [&](std::string* dst) {
            const size_t s = p;
            while (p < end && d[p]) ++p;
            if (dst) dst->assign((const char*)d + s, p - s);
            if (p < end) ++p;
        };
        auto digit = [&](size_t i) { return i < end && d[i] >= '0' && d[i] <= '9'; };
        auto number = [&](size_t i) {  // decimal digits from i; p moves behind them and a terminating 0
            int v = 0;
            while (digit(i) && v < 100000) v = v * 10 + (d[i++] - '0');
            p = i;
            if (p < end && d[p] == 0) ++p;
            return v;
        };
        while (p + 4 <= end) {
            const char* t = (const char*)d + p;
            if (!std::memcmp(t, "HDNS", 4)) break;
            if (!std::memcmp(t, "TITL", 4)) p += 4, str(&title);
            else if (!std::memcmp(t, "COMM", 4)) p += 4, str(&composer);
            else if (!std::memcmp(t, "RIPP", 4) || !std::memcmp(t, "CONV", 4) || !std::memcmp(t, "YEAR", 4)) p += 4, str(nullptr);
            else if (t[0] == '#' && t[1] == '#' && digit(p + 2)) songs = number(p + 2);
            else if (!std::memcmp(t, "#!SN", 4)) {
                p += 4;
                const size_t base = p;
                size_t last = p + (size_t)songs * 2;
                for (int i = 0; i < songs && base + (size_t)i * 2 + 2 <= end; ++i) {
                    size_t s = base + be16(d + base + (size_t)i * 2), e = s;
                    while (e < end && d[e]) ++e;
                    names.emplace_back(s < end ? std::string((const char*)d + s, e - s) : std::string());
                    if (e + 1 > last) last = e + 1;
                }
                p = last;
            } else if (t[0] == '#' && t[1] == '!' && digit(p + 2)) defSong = number(p + 2) - 1;
            else if (t[0] == 'T' && t[1] >= 'A' && t[1] <= 'D' && digit(p + 2)) hz = number(p + 2);
            else if (t[0] == '!' && t[1] == 'V' && digit(p + 2)) hz = number(p + 2);
            else if (!std::memcmp(t, "TIME", 4)) {
                p += 4;
                for (int i = 0; i < songs && p + 2 <= end; ++i, p += 2) times.push_back(be16(d + p));
            } else if (!std::memcmp(t, "FRMS", 4)) {
                p += 4;
                for (int i = 0; i < songs && p + 4 <= end; ++i, p += 4) frames.push_back(be32(d + p));
            } else {
                ++p;
            }
            if (songs < 1) songs = 1;
            if (songs > 99) songs = 99;
        }
        if (defSong < 0 || defSong >= songs) defSong = 0;
        if (hz < 1 || hz > 1000) hz = 50;
        return true;
    }
};

// ---------------------------------------------------------------- Musashi callbacks (one machine at a time)
namespace {
SndhPlayer::Impl* g_cur = nullptr;

// Locks the CPU for one machine: its context is loaded, memory callbacks reach it.
class CpuLock {
public:
    explicit CpuLock(SndhPlayer::Impl& m) : lock_(g_cpuMutex), m_(m) {
        std::call_once(g_cpuOnce, [] {
            m68k_init();
            m68k_set_cpu_type(M68K_CPU_TYPE_68000);
            g_cpuTemplate.resize(m68k_context_size());
            m68k_get_context(g_cpuTemplate.data());
        });
        g_cur = &m;
        if (!m.ctx.empty()) m68k_set_context(m.ctx.data());
    }
    ~CpuLock() {
        if (!m_.ctx.empty()) m68k_get_context(m_.ctx.data());
        g_cur = nullptr;
    }
    CpuLock(const CpuLock&) = delete;
    CpuLock& operator=(const CpuLock&) = delete;

private:
    std::lock_guard<std::mutex> lock_;
    SndhPlayer::Impl& m_;
};
}  // namespace

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) { return g_cur ? g_cur->read8(a) : 0; }
unsigned int m68k_read_memory_16(unsigned int a) { return g_cur ? g_cur->read16(a) : 0; }
unsigned int m68k_read_memory_32(unsigned int a) { return g_cur ? g_cur->read32(a) : 0; }
void m68k_write_memory_8(unsigned int a, unsigned int v) {
    if (g_cur) g_cur->write8(a, (uint8_t)v);
}
void m68k_write_memory_16(unsigned int a, unsigned int v) {
    if (g_cur) g_cur->write16(a, v & 0xFFFF);
}
void m68k_write_memory_32(unsigned int a, unsigned int v) {
    if (g_cur) g_cur->write32(a, v);
}
int ft_sndh_int_ack(int level) { return g_cur ? g_cur->intAck(level) : M68K_INT_ACK_SPURIOUS; }
int ft_sndh_trap(int t) { return g_cur ? g_cur->trap(t) : 0; }
}

// ---------------------------------------------------------------- SndhPlayer
SndhPlayer::SndhPlayer() : impl_(new Impl) {}
SndhPlayer::~SndhPlayer() = default;

bool SndhPlayer::detect(const uint8_t* d, size_t n) {
    if (!d || n < 16) return false;
    if (!std::memcmp(d + 12, "SNDH", 4)) return true;
    if (std::memcmp(d, "ICE!", 4) != 0) return false;
    std::vector<uint8_t> out;
    return iceDepack(d, n, out) && out.size() >= 16 && !std::memcmp(out.data() + 12, "SNDH", 4);
}

bool SndhPlayer::load(const uint8_t* data, size_t size, int sub) {
    Impl& m = *impl_;
    m.loaded = false;
    if (!detect(data, size)) return false;
    std::vector<uint8_t> img;
    if (!std::memcmp(data, "ICE!", 4)) {
        if (!iceDepack(data, size, img)) return false;
    } else {
        img.assign(data, data + size);
    }
    if (img.size() < 16 || img.size() > kMaxImage || std::memcmp(img.data() + 12, "SNDH", 4) != 0) return false;
    m.image = static_cast<std::vector<uint8_t>&&>(img);
    m.parse(m.image.data(), m.image.size());
    const int s = sub >= 0 && sub < m.songs ? sub : m.defSong;
    CpuLock cpu(m);
    if (!m.boot(s)) return false;
    m.loaded = true;
    return true;
}

void SndhPlayer::restart() {
    if (!impl_->loaded) return;
    CpuLock cpu(*impl_);
    impl_->boot(impl_->song);
}

void SndhPlayer::setRate(uint32_t hz) {
    impl_->rate = hz ? hz : 48000;
    impl_->configureSound();
}

void SndhPlayer::clearMeters() {
    std::memset(impl_->vu, 0, sizeof impl_->vu);
    std::memset(impl_->scope, 0, sizeof impl_->scope);
}

void SndhPlayer::render(float* out, uint32_t frames, float gain) {
    std::memset(out, 0, sizeof(float) * 2 * frames);
    Impl& m = *impl_;
    if (!m.loaded) return;
    CpuLock cpu(m);
    const double perSample = kCpuHz / m.rate;
    for (int c = 0; c < 3; ++c) m.level[c] = 0;
    for (uint32_t f = 0; f < frames; ++f) {
        m.cycleAcc += perSample;
        const double n = std::floor(m.cycleAcc);
        m.cycleAcc -= n;
        if (!m.dead) m.run(n);
        m.levels();
        ayumi_process(&m.ay);
        ayumi_remove_dc(&m.ay);
        const float l = m.dead ? 0.f : (float)m.ay.left * gain * 1.4f, r = m.dead ? 0.f : (float)m.ay.right * gain * 1.4f;
        out[2 * f] = l;
        out[2 * f + 1] = r;
        const float s = l + r;
        for (int c = 0; c < 3; ++c) m.scope[c][(m.scopePos + f) & (kScopeLen - 1)] = m.level[c] > 0 ? s : 0.f;
    }
    m.scopePos = (m.scopePos + frames) & (kScopeLen - 1);
    const float decay = (float)std::pow(0.5, (double)frames / (m.rate * 0.08));
    for (int c = 0; c < 3; ++c) {
        const float d = m.vu[c] * decay;
        m.vu[c] = m.level[c] > d ? m.level[c] : d;
    }
}

float SndhPlayer::vu(int v) const { return v >= 0 && v < 3 ? impl_->vu[v] * 0.6f : 0.f; }

void SndhPlayer::scope(int v, float* dst, int n) const {
    if (n <= 0) return;
    if (n > kScopeLen) n = kScopeLen;
    if (v < 0 || v >= 3) {
        for (int k = 0; k < n; ++k) dst[k] = 0;
        return;
    }
    const uint32_t start = (impl_->scopePos - (uint32_t)n) & (kScopeLen - 1);
    for (int k = 0; k < n; ++k) dst[k] = impl_->scope[v][(start + k) & (kScopeLen - 1)];
}

double SndhPlayer::seconds() const {
    const Impl& m = *impl_;
    if (!m.loaded) return 0;
    if (m.song < (int)m.frames.size() && m.frames[m.song]) return (double)m.frames[m.song] / m.hz;
    if (m.song < (int)m.times.size()) return m.times[m.song];
    return 0;
}

const char* SndhPlayer::format() const {
    Impl& m = *impl_;
    if (!m.loaded) return "";
    m.format = m.dmaUsed ? "SNDH (ATARI STE, DMA SOUND NOT PLAYED)" : "SNDH (ATARI ST)";
    return m.format.c_str();
}

bool SndhPlayer::buildDisplay(Song& out) {
    if (!impl_->loaded) return false;
    out = makeEmptySong(4);
    return true;
}

int SndhPlayer::subsongs() const { return impl_->loaded ? impl_->songs : 1; }
int SndhPlayer::subsong() const { return impl_->loaded ? impl_->song : 0; }

bool SndhPlayer::selectSubsong(int i) {
    Impl& m = *impl_;
    if (!m.loaded || i < 0 || i >= m.songs) return false;
    CpuLock cpu(m);
    m.boot(i);  // an init that runs away leaves the sub-song silent
    return true;
}

const char* SndhPlayer::subsongTitle() const {
    const Impl& m = *impl_;
    return m.loaded && m.song < (int)m.names.size() ? m.names[m.song].c_str() : "";
}

const char* SndhPlayer::title() const { return impl_->title.c_str(); }
const char* SndhPlayer::composer() const { return impl_->composer.c_str(); }
int SndhPlayer::playRate() const { return impl_->hz; }
bool SndhPlayer::crashed() const { return impl_->dead; }
uint8_t SndhPlayer::ymRegister(int r) const { return r >= 0 && r < 16 ? impl_->ymReg[r] : 0; }

uint32_t SndhPlayer::mfpInterrupts() const { return impl_->mfpServed; }

uint32_t SndhPlayer::peek32(uint32_t a) const {
    const Impl& m = *impl_;
    return a + 3 < m.ram.size() ? be32(&m.ram[a]) : 0;
}
