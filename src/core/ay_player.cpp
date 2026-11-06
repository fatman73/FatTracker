#include "core/ay_player.h"

#include <cmath>
#include <cstring>
#include <string>

#include "core/song.h"

#include "core/ay_meters.h"

extern "C" {
#include "core/ayumi/ayumi.h"
}

// ---------------------------------------------------------------- LH5 (static Huffman, 8 KB window)
namespace {
class Lh5 {
public:
    Lh5(const uint8_t* src, size_t n) : src_(src), n_(n) {}

    bool decode(uint8_t* dst, size_t size) {
        fill(16);
        size_t out = 0;
        while (out < size) {
            int c = decodeC();
            if (c < 0) return false;
            if (c < 256) {
                dst[out++] = (uint8_t)c;
            } else {
                const size_t len = (size_t)(c - 256 + 3);
                const int p = decodeP();
                if (p < 0 || (size_t)p + 1 > out) return false;  // reaches before the start
                const size_t from = out - (size_t)p - 1;
                for (size_t k = 0; k < len && out < size; ++k) dst[out++] = dst[from + k];
            }
            if (bad_) return false;
        }
        return true;
    }

private:
    static constexpr int NC = 510, NT = 19, NP = 14, CBIT = 9, TBIT = 5, PBIT = 4;
    const uint8_t* src_;
    size_t n_, pos_ = 0;
    unsigned bitbuf_ = 0, subbuf_ = 0;
    int bitcount_ = 0;
    unsigned blocksize_ = 0;
    bool bad_ = false;
    int virt_ = 0;  // bytes read past the end of the data
    uint8_t cLen_[NC] = {}, ptLen_[NT > NP ? NT : NP] = {};
    uint16_t cTable_[4096] = {}, ptTable_[256] = {};
    uint16_t left_[2 * NC - 1] = {}, right_[2 * NC - 1] = {};

    void fill(int n) {
        while (n > bitcount_) {
            n -= bitcount_;
            bitbuf_ = (bitbuf_ << bitcount_) + (subbuf_ >> (8 - bitcount_));
            if (pos_ < n_) subbuf_ = src_[pos_++];
            else if (++virt_ > 2) bad_ = true, subbuf_ = 0;  // a cut stream: fail instead of decoding zeros
            else subbuf_ = 0;
            bitcount_ = 8;
        }
        bitcount_ -= n;
        bitbuf_ = ((bitbuf_ << n) + (subbuf_ >> (8 - n))) & 0xFFFF;
        subbuf_ = (subbuf_ << n) & 0xFF;
    }
    unsigned get(int n) {
        const unsigned x = bitbuf_ >> (16 - n);
        fill(n);
        return x;
    }

    bool makeTable(int nchar, const uint8_t* bitlen, int tablebits, uint16_t* table) {
        unsigned count[17] = {}, weight[17] = {}, start[18] = {};
        for (int i = 0; i < nchar; ++i) {
            if (bitlen[i] > 16) return false;
            count[bitlen[i]]++;
        }
        for (int i = 1; i <= 16; ++i) start[i + 1] = start[i] + (count[i] << (16 - i));
        if ((start[17] & 0xFFFF) != 0) return false;  // not a complete code
        const int jut = 16 - tablebits;
        for (int i = 1; i <= tablebits; ++i) {
            start[i] >>= jut;
            weight[i] = 1u << (tablebits - i);
        }
        for (int i = tablebits + 1; i <= 16; ++i) weight[i] = 1u << (16 - i);
        unsigned i = start[tablebits + 1] >> jut;
        const unsigned limit = 1u << tablebits;
        if (i != (1u << 16))
            while (i < limit) table[i++] = 0;
        unsigned avail = (unsigned)nchar;
        const unsigned mask = 1u << (15 - tablebits);
        for (int ch = 0; ch < nchar; ++ch) {
            const int len = bitlen[ch];
            if (!len) continue;
            unsigned k = start[len];
            const unsigned next = k + weight[len];
            if (len <= tablebits) {
                if (next > limit) return false;
                for (unsigned j = k; j < next; ++j) table[j] = (uint16_t)ch;
            } else {
                if ((k >> jut) >= limit) return false;
                uint16_t* p = &table[k >> jut];
                for (int j = len - tablebits; j; --j) {
                    if (*p == 0) {
                        if (avail >= 2 * NC - 1) return false;
                        right_[avail] = left_[avail] = 0;
                        *p = (uint16_t)avail++;
                    }
                    p = (k & mask) ? &right_[*p] : &left_[*p];
                    k <<= 1;
                }
                *p = (uint16_t)ch;
            }
            start[len] = next;
        }
        return true;
    }

    bool readPtLen(int nn, int nbit, int special) {
        const int n = (int)get(nbit);
        if (n == 0) {
            const unsigned c = get(nbit);
            for (int i = 0; i < nn; ++i) ptLen_[i] = 0;
            for (auto& t : ptTable_) t = (uint16_t)c;
            return c < (unsigned)nn;
        }
        if (n > nn) return false;
        int i = 0;
        while (i < n) {
            int c = (int)(bitbuf_ >> 13);
            if (c == 7) {
                unsigned m = 1u << 12;
                while (m & bitbuf_) {
                    m >>= 1;
                    if (++c > 16) return false;
                }
            }
            fill(c < 7 ? 3 : c - 3);
            ptLen_[i++] = (uint8_t)c;
            if (i == special) {
                int z = (int)get(2);
                while (--z >= 0 && i < nn) ptLen_[i++] = 0;
            }
        }
        while (i < nn) ptLen_[i++] = 0;
        return makeTable(nn, ptLen_, 8, ptTable_);
    }

    bool readCLen() {
        const int n = (int)get(CBIT);
        if (n == 0) {
            const unsigned c = get(CBIT);
            for (auto& l : cLen_) l = 0;
            for (auto& t : cTable_) t = (uint16_t)c;
            return c < (unsigned)NC;
        }
        if (n > NC) return false;
        int i = 0;
        while (i < n) {
            int c = ptTable_[bitbuf_ >> 8];
            if (c >= NT) {
                unsigned m = 1u << 7;
                int guard = 0;
                do {
                    c = (bitbuf_ & m) ? right_[c] : left_[c];
                    m >>= 1;
                } while (c >= NT && ++guard < 32);
                if (c >= NT) return false;
            }
            fill(ptLen_[c]);
            if (c <= 2) {
                int z = c == 0 ? 1 : (c == 1 ? (int)get(4) + 3 : (int)get(CBIT) + 20);
                while (--z >= 0 && i < NC) cLen_[i++] = 0;
            } else {
                cLen_[i++] = (uint8_t)(c - 2);
            }
        }
        while (i < NC) cLen_[i++] = 0;
        return makeTable(NC, cLen_, 12, cTable_);
    }

    int decodeC() {
        if (blocksize_ == 0) {
            blocksize_ = get(16);
            if (!readPtLen(NT, TBIT, 3) || !readCLen() || !readPtLen(NP, PBIT, -1)) return -1;
            if (blocksize_ == 0) return -1;
        }
        --blocksize_;
        int j = cTable_[bitbuf_ >> 4];
        if (j >= NC) {
            unsigned m = 1u << 3;
            int guard = 0;
            do {
                j = (bitbuf_ & m) ? right_[j] : left_[j];
                m >>= 1;
            } while (j >= NC && ++guard < 32);
            if (j >= NC) return -1;
        }
        fill(cLen_[j]);
        return j;
    }

    int decodeP() {
        int j = ptTable_[bitbuf_ >> 8];
        if (j >= NP) {
            unsigned m = 1u << 7;
            int guard = 0;
            do {
                j = (bitbuf_ & m) ? right_[j] : left_[j];
                m >>= 1;
            } while (j >= NP && ++guard < 32);
            if (j >= NP) return -1;
        }
        fill(ptLen_[j]);
        if (j != 0) j = (int)((1u << (j - 1)) + get(j - 1));
        return j;
    }
};

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
constexpr size_t kMaxUnpacked = 64u << 20;
}  // namespace

bool lh5Decode(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize) {
    if (!src || !dst) return false;
    auto* d = new Lh5(src, srcSize);  // ~20 KB of tables: off the stack
    const bool ok = d->decode(dst, dstSize);
    delete d;
    return ok;
}

bool lhaUnpack(const uint8_t* d, size_t n, std::vector<uint8_t>& out) {
    if (n < 24 || d[2] != '-' || d[3] != 'l' || d[4] != 'h' || d[6] != '-') return false;
    const int level = d[20];
    if (level > 1) return false;
    const uint32_t packed = le32(d + 7), size = le32(d + 11);
    size_t data = (size_t)d[0] + 2;  // level 0 / 1: header size byte + checksum byte
    uint32_t packedData = packed;
    if (level == 1) {  // extended headers follow; their bytes are counted in the packed size
        size_t p = data - 2;
        while (p + 2 <= n) {
            const uint16_t ext = le16(d + p);
            if (!ext) break;
            if (ext > packedData || p + ext > n) return false;
            packedData -= ext;
            p += ext;
        }
        data = p + 2;
    }
    if (size == 0 || size > kMaxUnpacked || data > n || packedData > n - data) return false;
    out.assign(size, 0);
    if (d[5] == '0') {
        if (packedData < size) return false;
        std::memcpy(out.data(), d + data, size);
        return true;
    }
    if (d[5] != '5') return false;
    return lh5Decode(d + data, packedData, out.data(), size);
}

// ---------------------------------------------------------------- the player
struct AyPlayer::Impl {
    struct ayumi ay;
    std::vector<uint8_t> regs;  // 16 registers per frame
    int frames = 0, loop = 0, hz = 50;
    double clock = 2000000;
    bool ym = true;
    int stereo = 0;  // 0 mono, 1 ABC, 2 ACB
    uint32_t rate = 48000;
    int frame = 0;
    double frameLeft = 0;
    std::string title, format;
    AyMeters meters;  // each channel's own output
    bool loaded = false;

    void configure() {
        std::memset(&ay, 0, sizeof ay);
        ayumi_configure(&ay, ym ? 1 : 0, clock, (int)rate);
        static const double kPans[3][3] = {{0.5, 0.5, 0.5}, {0.1, 0.5, 0.9}, {0.1, 0.9, 0.5}};
        for (int c = 0; c < 3; ++c) ayumi_set_pan(&ay, c, kPans[stereo < 0 || stereo > 2 ? 0 : stereo][c], 0);
        frame = 0;
        frameLeft = 0;
    }
    void apply(const uint8_t* r) {
        for (int c = 0; c < 3; ++c) {
            ayumi_set_tone(&ay, c, r[c * 2] | (r[c * 2 + 1] & 15) << 8);
            ayumi_set_mixer(&ay, c, (r[7] >> c) & 1, (r[7] >> (c + 3)) & 1, (r[8 + c] >> 4) & 1);
            ayumi_set_volume(&ay, c, r[8 + c] & 15);
        }
        ayumi_set_noise(&ay, r[6] & 31);
        ayumi_set_envelope(&ay, r[11] | r[12] << 8);
        if (r[13] != 0xFF) ayumi_set_envelope_shape(&ay, r[13] & 15);  // 0xFF: no write (keeps the envelope running)
    }
};

AyPlayer::AyPlayer() : impl_(new Impl) {}
AyPlayer::~AyPlayer() = default;

bool AyPlayer::detect(const uint8_t* d, size_t n) {
    if (!d || n < 16) return false;
    if (std::memcmp(d, "YM2!", 4) == 0 || std::memcmp(d, "YM3!", 4) == 0 || std::memcmp(d, "YM3b", 4) == 0 ||
        std::memcmp(d, "YM5!", 4) == 0 || std::memcmp(d, "YM6!", 4) == 0 || std::memcmp(d, "PSG\x1A", 4) == 0)
        return true;
    if ((d[0] == 'a' && d[1] == 'y') || (d[0] == 'y' && d[1] == 'm'))  // VTX: sane header fields
        return n > 20 && d[2] <= 6 && le32(d + 12) > 0 && le32(d + 12) < kMaxUnpacked && d[9] >= 1 && d[9] <= 200 &&
               le32(d + 5) >= 500000 && le32(d + 5) <= 4000000;
    // LHA packed YM: method -lh5- / -lh0-, a member called *.ym or anything (YM rips are single member archives)
    return n >= 24 && d[2] == '-' && d[3] == 'l' && d[4] == 'h' && (d[5] == '5' || d[5] == '0') && d[6] == '-';
}

bool AyPlayer::load(const uint8_t* data, size_t size) {
    Impl& m = *impl_;
    m.loaded = false;
    if (!detect(data, size)) return false;
    std::vector<uint8_t> buf;
    if (data[2] == '-' && data[3] == 'l') {
        if (!lhaUnpack(data, size, buf)) return false;
        if (!detect(buf.data(), buf.size()) || (buf.size() > 2 && buf[2] == '-')) return false;
        data = buf.data();
        size = buf.size();
    }
    m.regs.clear();
    m.loop = 0;
    m.hz = 50;
    m.clock = 2000000;
    m.ym = true;
    m.stereo = 0;
    m.title.clear();
    const uint8_t* d = data;
    if (std::memcmp(d, "YM2!", 4) == 0 || std::memcmp(d, "YM3!", 4) == 0 || std::memcmp(d, "YM3b", 4) == 0) {
        const bool b = d[3] == 'b';
        const size_t body = size - 4 - (b ? 4 : 0);
        m.frames = (int)(body / 14);
        if (b && size >= 8) m.loop = (int)le32(d + size - 4);  // YM3b: loop frame (little endian, as written by ST-Sound)
        m.regs.assign((size_t)m.frames * 16, 0);
        for (int f = 0; f < m.frames; ++f)
            for (int r = 0; r < 14; ++r) m.regs[(size_t)f * 16 + r] = d[4 + (size_t)r * m.frames + f];
        m.format = "YM (ATARI ST)";
    } else if (std::memcmp(d, "YM5!", 4) == 0 || std::memcmp(d, "YM6!", 4) == 0) {
        if (size < 34 || std::memcmp(d + 4, "LeOnArD!", 8) != 0) return false;
        m.frames = (int)be32(d + 12);
        const uint32_t attr = be32(d + 16);
        const int drums = be16(d + 20);
        m.clock = be32(d + 22);
        m.hz = be16(d + 26);
        m.loop = (int)be32(d + 28);
        size_t p = 34 + be16(d + 32);
        for (int k = 0; k < drums && p + 4 <= size; ++k) p += 4 + be32(d + p);  // digidrums are not played
        for (int s = 0; s < 3 && p < size; ++s) {  // name, author, comment
            const size_t start = p;
            while (p < size && d[p]) ++p;
            if (s == 0) m.title.assign((const char*)d + start, p - start);
            ++p;
        }
        if (p > size || m.frames <= 0 || (size_t)m.frames * 16 > size - p) return false;
        m.regs.assign((size_t)m.frames * 16, 0);
        for (int f = 0; f < m.frames; ++f)
            for (int r = 0; r < 16; ++r)
                m.regs[(size_t)f * 16 + r] = (attr & 1) ? d[p + (size_t)r * m.frames + f] : d[p + (size_t)f * 16 + r];
        for (int f = 0; f < m.frames; ++f) {  // the special effect bits of YM5/6 are not AY registers
            uint8_t* r = &m.regs[(size_t)f * 16];
            r[1] &= 15, r[3] &= 15, r[5] &= 15, r[6] &= 31, r[8] &= 31, r[9] &= 31, r[10] &= 31;
        }
        m.format = d[2] == '6' ? "YM6 (ATARI ST)" : "YM5 (ATARI ST)";
    } else if (std::memcmp(d, "PSG\x1A", 4) == 0) {
        uint8_t r[16] = {};
        r[13] = 0xFF;
        for (size_t p = 16; p < size;) {
            const uint8_t c = d[p++];
            if (c == 0xFF || c == 0xFE) {
                int n = 1;
                if (c == 0xFE) n = p < size ? d[p++] * 4 : 0;
                for (int k = 0; k < n && m.regs.size() < kMaxUnpacked; ++k) {
                    m.regs.insert(m.regs.end(), r, r + 16);
                    r[13] = 0xFF;  // the envelope shape is written once
                }
            } else if (c == 0xFD) {
                break;
            } else if (c < 16) {
                if (p >= size) break;
                r[c] = d[p++];
            } else {
                ++p;  // unknown command with a value
            }
        }
        m.frames = (int)(m.regs.size() / 16);
        m.ym = false;
        m.clock = 1773400;  // ZX Spectrum AY
        m.format = "PSG (AY)";
    } else {  // VTX
        m.ym = d[0] == 'y';
        m.stereo = d[2] == 1 ? 1 : (d[2] == 2 ? 2 : (d[2] == 0 ? 0 : 1));
        m.loop = le16(d + 3);
        m.clock = le32(d + 5);
        m.hz = d[9];
        const uint32_t unpacked = le32(d + 12);
        size_t p = 16;
        for (int s = 0; s < 5 && p < size; ++s) {  // title, author, program, tracker, comment
            const size_t start = p;
            while (p < size && d[p]) ++p;
            if (s == 0) m.title.assign((const char*)d + start, p - start);
            ++p;
        }
        if (p >= size || unpacked < 14) return false;
        std::vector<uint8_t> raw(unpacked);
        if (!lh5Decode(d + p, size - p, raw.data(), unpacked)) return false;
        m.frames = (int)(unpacked / 14);
        m.regs.assign((size_t)m.frames * 16, 0);
        for (int f = 0; f < m.frames; ++f)
            for (int r = 0; r < 14; ++r) m.regs[(size_t)f * 16 + r] = raw[(size_t)r * m.frames + f];
        m.format = m.ym ? "VTX (YM)" : "VTX (AY)";
    }
    if (m.frames <= 0) return false;
    if (m.hz < 25 || m.hz > 200) m.hz = 50;
    if (m.clock < 500000 || m.clock > 4000000) m.clock = m.ym ? 2000000 : 1773400;
    if (m.loop < 0 || m.loop >= m.frames) m.loop = 0;
    m.configure();
    m.loaded = true;
    return true;
}

void AyPlayer::restart() {
    if (impl_->loaded) impl_->configure();
}

void AyPlayer::setRate(uint32_t hz) {
    impl_->rate = hz ? hz : 48000;
    if (impl_->loaded) impl_->configure();
}

void AyPlayer::clearMeters() { impl_->meters.clear(); }

void AyPlayer::render(float* out, uint32_t frames, float gain) {
    std::memset(out, 0, sizeof(float) * 2 * frames);
    Impl& m = *impl_;
    if (!m.loaded) return;
    m.meters.begin();
    for (uint32_t f = 0; f < frames; ++f) {
        if (m.frameLeft <= 0) {
            if (m.frame >= m.frames) m.frame = m.loop;
            m.apply(&m.regs[(size_t)m.frame * 16]);
            ++m.frame;
            m.frameLeft += (double)m.rate / m.hz;
        }
        m.frameLeft -= 1;
        ayumi_process(&m.ay);
        ayumi_remove_dc(&m.ay);
        out[2 * f] = (float)m.ay.left * gain * 1.4f;
        out[2 * f + 1] = (float)m.ay.right * gain * 1.4f;
        m.meters.sample(m.ay);
    }
    m.meters.end(m.rate);
}

float AyPlayer::vu(int v) const { return impl_->meters.vuOf(v); }

void AyPlayer::scope(int v, float* dst, int n) const { impl_->meters.scopeOf(v, dst, n); }

double AyPlayer::seconds() const { return impl_->loaded ? (double)impl_->frames / impl_->hz : 0.0; }
const char* AyPlayer::format() const { return impl_->loaded ? impl_->format.c_str() : ""; }
const char* AyPlayer::title() const { return impl_->title.c_str(); }
int AyPlayer::frames() const { return impl_->frames; }

bool AyPlayer::buildDisplay(Song& out) {
    if (!impl_->loaded) return false;
    out = makeEmptySong(4);
    return true;
}
