// Small deflate (RFC 1951) / gzip (RFC 1952) decoder for packed tunes (.vgz), in the style of zlib's
// puff.c by Mark Adler: canonical Huffman codes decoded bit by bit. Bounded: output is capped, every
// read is checked, broken data returns false.
#include "core/inflate.h"

namespace {
struct Huff {
    short count[16];
    short symbol[288];
};

class Inflater {
public:
    Inflater(const uint8_t* in, size_t n, std::vector<uint8_t>& out, size_t cap) : in_(in), n_(n), out_(out), cap_(cap) {}

    bool run() {
        for (;;) {
            const int last = bits(1);
            const int type = bits(2);
            if (err_) return false;
            bool ok;
            if (type == 0) ok = stored();
            else if (type == 1) ok = fixed();
            else if (type == 2) ok = dynamic();
            else return false;
            if (!ok || err_) return false;
            if (last) return true;
        }
    }
    size_t consumed() const { return pos_; }

private:
    const uint8_t* in_;
    size_t n_, pos_ = 0;
    std::vector<uint8_t>& out_;
    size_t cap_;
    unsigned bitbuf_ = 0;
    int bitcnt_ = 0;
    bool err_ = false;

    int bits(int need) {
        long val = bitbuf_;
        while (bitcnt_ < need) {
            if (pos_ >= n_) {
                err_ = true;
                return 0;
            }
            val |= (long)in_[pos_++] << bitcnt_;
            bitcnt_ += 8;
        }
        bitbuf_ = (unsigned)(val >> need);
        bitcnt_ -= need;
        return (int)(val & ((1L << need) - 1));
    }

    bool put(uint8_t b) {
        if (out_.size() >= cap_) return false;
        out_.push_back(b);
        return true;
    }

    bool stored() {
        bitbuf_ = 0;
        bitcnt_ = 0;
        if (pos_ + 4 > n_) return false;
        const unsigned len = in_[pos_] | in_[pos_ + 1] << 8;
        const unsigned nlen = in_[pos_ + 2] | in_[pos_ + 3] << 8;
        pos_ += 4;
        if (len != (~nlen & 0xFFFF) || pos_ + len > n_) return false;
        for (unsigned i = 0; i < len; ++i)
            if (!put(in_[pos_++])) return false;
        return true;
    }

    int decode(const Huff& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; ++len) {
            code |= bits(1);
            if (err_) return -1;
            const int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }

    // returns 0 complete, > 0 incomplete (allowed for one-code distance trees), < 0 over-subscribed
    static int construct(Huff& h, const short* length, int n) {
        for (int len = 0; len <= 15; ++len) h.count[len] = 0;
        for (int s = 0; s < n; ++s) h.count[length[s]]++;
        if (h.count[0] == n) return 0;
        int left = 1;
        for (int len = 1; len <= 15; ++len) {
            left <<= 1;
            left -= h.count[len];
            if (left < 0) return left;
        }
        short offs[16];
        offs[1] = 0;
        for (int len = 1; len < 15; ++len) offs[len + 1] = (short)(offs[len] + h.count[len]);
        for (int s = 0; s < n; ++s)
            if (length[s] != 0) h.symbol[offs[length[s]]++] = (short)s;
        return left;
    }

    bool codes(const Huff& lencode, const Huff& distcode) {
        static const short lbase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                        31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const short dbase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                        193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int sym = decode(lencode);
            if (sym < 0) return false;
            if (sym < 256) {
                if (!put((uint8_t)sym)) return false;
            } else if (sym == 256) {
                return true;
            } else {
                sym -= 257;
                if (sym >= 29) return false;
                const int len = lbase[sym] + bits(lext[sym]);
                const int ds = decode(distcode);
                if (ds < 0 || ds >= 30) return false;
                const size_t dist = (size_t)(dbase[ds] + bits(dext[ds]));
                if (err_ || dist > out_.size()) return false;
                for (int k = 0; k < len; ++k)
                    if (!put(out_[out_.size() - dist])) return false;
            }
        }
    }

    bool fixed() {
        static Huff lencode, distcode;
        static bool built = false;
        if (!built) {
            short lengths[288];
            int s = 0;
            for (; s < 144; ++s) lengths[s] = 8;
            for (; s < 256; ++s) lengths[s] = 9;
            for (; s < 280; ++s) lengths[s] = 7;
            for (; s < 288; ++s) lengths[s] = 8;
            construct(lencode, lengths, 288);
            for (s = 0; s < 30; ++s) lengths[s] = 5;
            construct(distcode, lengths, 30);
            built = true;
        }
        return codes(lencode, distcode);
    }

    bool dynamic() {
        static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        short lengths[320];
        Huff lencode, distcode;
        const int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
        if (err_ || nlen > 286 || ndist > 30) return false;
        int index = 0;
        for (; index < ncode; ++index) lengths[order[index]] = (short)bits(3);
        for (; index < 19; ++index) lengths[order[index]] = 0;
        if (err_ || construct(lencode, lengths, 19) != 0) return false;
        index = 0;
        while (index < nlen + ndist) {
            int sym = decode(lencode);
            if (sym < 0) return false;
            if (sym < 16) {
                lengths[index++] = (short)sym;
            } else {
                short len = 0;
                if (sym == 16) {
                    if (index == 0) return false;
                    len = lengths[index - 1];
                    sym = 3 + bits(2);
                } else if (sym == 17) {
                    sym = 3 + bits(3);
                } else {
                    sym = 11 + bits(7);
                }
                if (err_ || index + sym > nlen + ndist) return false;
                while (sym--) lengths[index++] = len;
            }
        }
        if (lengths[256] == 0) return false;
        const int err = construct(lencode, lengths, nlen);
        if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) return false;
        const int derr = construct(distcode, lengths + nlen, ndist);
        if (derr < 0 || (derr > 0 && ndist - distcode.count[0] != 1)) return false;
        return codes(lencode, distcode);
    }
};
}  // namespace

bool inflateRaw(const uint8_t* in, size_t n, std::vector<uint8_t>& out, size_t maxOut) {
    out.clear();
    if (!in) return false;
    Inflater f(in, n, out, maxOut);
    return f.run();
}

bool isGzip(const uint8_t* d, size_t n) { return d && n >= 18 && d[0] == 0x1F && d[1] == 0x8B && d[2] == 8; }

bool gunzip(const uint8_t* d, size_t n, std::vector<uint8_t>& out, size_t maxOut) {
    out.clear();
    if (!isGzip(d, n)) return false;
    const int flags = d[3];
    size_t p = 10;
    if (flags & 4) {  // FEXTRA
        if (p + 2 > n) return false;
        p += 2 + (size_t)(d[p] | d[p + 1] << 8);
    }
    if (flags & 8)  // FNAME
        while (p < n && d[p++]) {}
    if (flags & 16)  // FCOMMENT
        while (p < n && d[p++]) {}
    if (flags & 2) p += 2;  // FHCRC
    if (p >= n) return false;
    return inflateRaw(d + p, n - p, out, maxOut);
}
