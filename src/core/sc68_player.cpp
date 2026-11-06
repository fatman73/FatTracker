#include "core/sc68_player.h"

#include <cstring>
#include <map>
#include <memory>

#include "core/inflate.h"

namespace {
using Bytes = std::shared_ptr<const std::vector<uint8_t>>;

uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

constexpr size_t kMaxFile = 4u << 20;      // unpacked SC68 / YMST file
constexpr size_t kMaxReplay = 512u << 10;  // an external replay
constexpr uint32_t kStub = SndhPlayer::kProgramLow;  // the init stub of a sub-song (generated 68000 code)
constexpr uint32_t kScratch = kStub + 0x100;         // YMST: the Amiga emulator's register buffer (unused here)
constexpr uint32_t kYmstAt = 0x10000;                // YMST: where the file goes

// 68000 code for the init stubs
struct Code {
    std::vector<uint8_t> b;
    void w(uint16_t v) { b.push_back((uint8_t)(v >> 8)), b.push_back((uint8_t)v); }
    void l(uint32_t v) { w((uint16_t)(v >> 16)), w((uint16_t)v); }
    void op(uint16_t o, uint32_t v) { w(o), l(v); }  // an opcode with an absolute long / immediate long
};

// A zero-terminated text inside [from, end), at most `max` characters, control characters dropped.
std::string text(const uint8_t* d, size_t from, size_t end, size_t max = 120) {
    std::string s;
    for (size_t i = from; i < end && d[i] && s.size() < max; ++i)
        if (d[i] >= 0x20) s += (char)d[i];
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

std::string upper(std::string s) {
    for (char& c : s) c = (char)((unsigned char)c < 127 ? toupper((unsigned char)c) : '_');
    return s;
}

// ---------------------------------------------------------------- SC68
const char kSc68Id[] = "SC68 Music-file / (c) (BeN)jamin Gerard / SasHipA-Dev  ";  // 56 bytes with its 0
constexpr size_t kSc68IdLen = sizeof kSc68Id;

bool plainSc68(const uint8_t* d, size_t n) {
    return d && n >= kSc68IdLen + 8 && !std::memcmp(d, kSc68Id, kSc68IdLen) && !std::memcmp(d + kSc68IdLen, "SC68", 4);
}

// The plain SC68 bytes of a file (gzip and ICE! unpacked into `buf`), or null.
const uint8_t* unpackSc68(const uint8_t* d, size_t n, std::vector<uint8_t>& buf, size_t& size) {
    if (plainSc68(d, n)) {
        size = n;
        return d;
    }
    if (!d || n < 16) return nullptr;
    bool ok = false;
    if (isGzip(d, n)) ok = gunzip(d, n, buf, kMaxFile);
    else if (!std::memcmp(d, "ICE!", 4)) ok = iceDepack(d, n, buf);
    if (!ok || !plainSc68(buf.data(), buf.size())) return nullptr;
    size = buf.size();
    return buf.data();
}

bool plainName(const std::string& s) {  // a replay name that can only name a file in the replay folders
    if (s.empty() || s.size() > 64 || s[0] == '.') return false;
    for (char c : s)
        if (!(isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' || c == ' ' || c == '+')) return false;
    return true;
}

// ---------------------------------------------------------------- YMST
struct Ymst {
    const uint8_t* d = nullptr;
    size_t n = 0;
    uint32_t tag[64] = {};  // by letter - '@'
    bool has[64] = {};
    size_t tagsEnd = 0;

    bool parse(const uint8_t* data, size_t size) {
        d = data, n = size;
        if (!d || n < 16 || std::memcmp(d, "YMST", 4) != 0) return false;
        size_t p = 4;
        int count = 0;
        while (p + 8 <= n && d[p] == 0x80 && d[p + 1] == 0 && d[p + 2] == 'Y' && count < 64) {
            const int k = d[p + 3] - '@';
            if (k < 0 || k >= 64) return false;
            tag[k] = be32(d + p + 4), has[k] = true;
            p += 8, ++count;
        }
        tagsEnd = p;
        return count > 0 && p + 4 <= n && be32(d + p) == 0;  // the tags end with a zero long
    }
    bool is(char k) const { return has[k - '@']; }
    uint32_t value(char k, uint32_t def = 0) const { return is(k) ? tag[k - '@'] : def; }
    // an offset into the file with room for `room` bytes behind it
    bool at(char k, size_t room = 4) const { return is(k) && tag[k - '@'] >= tagsEnd && tag[k - '@'] + room <= n; }
    bool code(char k) const { return at(k, 2) && !(tag[k - '@'] & 1); }
    std::string str(char k) const { return at(k, 1) ? text(d, tag[k - '@'], n) : std::string(); }
    int kind() const { return (int)value('M'); }
};
}  // namespace

// ---------------------------------------------------------------- Sc68Player
bool Sc68Player::detect(const uint8_t* d, size_t n) {
    std::vector<uint8_t> buf;
    size_t size = 0;
    return unpackSc68(d, n, buf, size) != nullptr;
}

bool Sc68Player::detectYmst(const uint8_t* d, size_t n) {
    return d && n >= 16 && !std::memcmp(d, "YMST", 4) && d[4] == 0x80 && d[5] == 0 && d[6] == 'Y';
}

bool Sc68Player::load(const uint8_t* data, size_t size, const StFileResolver& files, int sub, std::string* error) {
    std::string err;
    replay_.clear();
    bool ok = false;
    if (detectYmst(data, size)) {
        ymst_ = true;
        ok = loadYmst(data, size, files, sub, err);
        if (!ok && err.empty()) err = "YMST: BROKEN FILE OR THE REPLAY DOES NOT RUN";
    } else {
        ymst_ = false;
        ok = loadSc68(data, size, files, sub, err);
        if (!ok && err.empty()) err = "SC68: BROKEN FILE OR THE REPLAY DOES NOT RUN";
    }
    if (error) *error = ok ? std::string() : err;
    return ok;
}

bool Sc68Player::loadSc68(const uint8_t* data, size_t size, const StFileResolver& files, int sub, std::string& err) {
    std::vector<uint8_t> buf;
    size_t n = 0;
    const uint8_t* d = unpackSc68(data, size, buf, n);
    if (!d) return false;
    // chunks: "SC" + 2 letters + little-endian size; the base chunk ("SC68") holds the size of what follows the id
    const uint32_t total = le32(d + kSc68IdLen + 4);
    if (total <= 8 || total > n - kSc68IdLen) {
        err = "SC68: CUT OR BROKEN FILE";
        return false;
    }
    const size_t end = kSc68IdLen + total;
    struct Track {
        uint32_t d0 = 0, at = 0, frq = 0, time = 0, frames = 0, flags = 0;
        std::string name, author, composer, replay;
        const uint8_t* data = nullptr;
        uint32_t size = 0;
    };
    std::vector<Track> tracks;
    std::string disk;
    int defTrack = 0;
    bool eof = false;
    size_t p = kSc68IdLen + 8;
    while (p + 8 <= end) {
        if (d[p] != 'S' || d[p + 1] != 'C') break;
        const char a = (char)d[p + 2], b = (char)d[p + 3];
        const uint32_t len = le32(d + p + 4);
        const size_t body = p + 8;
        if (len > end - body) {
            err = "SC68: CUT OR BROKEN FILE";
            return false;
        }
        auto is = [&](const char* t) { return a == t[0] && b == t[1]; };
        Track* cur = tracks.empty() ? nullptr : &tracks.back();
        const bool trackChunk = is("MN") || is("AN") || is("CN") || is("RE") || is("D0") || is("AT") || is("TI") ||
                                is("FR") || is("FQ") || is("TY") || is("DA");
        if (trackChunk && !cur) return false;  // track information before the first track
        const bool valueChunk = is("DF") || is("D0") || is("AT") || is("TI") || is("FR") || is("FQ") || is("TY");
        if (valueChunk && len < 4) return false;
        const uint32_t v = valueChunk ? le32(d + body) : 0;
        if (is("EF")) {
            eof = true;
            break;
        } else if (is("FN")) {
            disk = text(d, body, body + len);
        } else if (is("DF")) {
            defTrack = (int)v;
        } else if (is("MU")) {
            if (tracks.size() >= 99) break;  // sc68 shows at most 99 tracks
            tracks.emplace_back();
        } else if (is("MN")) {
            cur->name = text(d, body, body + len);
        } else if (is("AN")) {
            cur->author = text(d, body, body + len);
        } else if (is("CN")) {
            cur->composer = text(d, body, body + len);
        } else if (is("RE")) {
            cur->replay = text(d, body, body + len, 64);
        } else if (is("D0")) {
            cur->d0 = v;
        } else if (is("AT")) {
            cur->at = v;
        } else if (is("TI")) {
            cur->time = v;
        } else if (is("FR")) {
            cur->frames = v;
        } else if (is("FQ")) {
            cur->frq = v;
        } else if (is("TY")) {
            cur->flags = v;
        } else if (is("DA")) {
            cur->data = d + body, cur->size = len;
        }
        p = body + len;
    }
    if (!eof && p != end) {
        err = "SC68: CUT OR BROKEN FILE";
        return false;
    }
    if (tracks.empty()) {
        err = "SC68: NO MUSIC IN THE FILE";
        return false;
    }
    // what a track does not say comes from the one before it (sc68's valid())
    const uint8_t* prevData = nullptr;
    uint32_t prevSize = 0;
    std::string author, composer;
    for (Track& t : tracks) {
        if (!t.data) t.data = prevData, t.size = prevSize;
        if (!t.data || !t.size) {
            err = "SC68: A TRACK WITHOUT MUSIC DATA";
            return false;
        }
        prevData = t.data, prevSize = t.size;
        if (t.author.empty()) t.author = author;
        if (t.composer.empty()) t.composer = composer.empty() ? t.author : composer;
        author = t.author, composer = t.composer;
    }
    // the sub-songs: the tracks this machine plays
    std::map<std::string, Bytes> replays;
    std::vector<StSong> songs;
    std::map<const uint8_t*, Bytes> datas;  // tracks share their data
    std::string skipped;
    int def = 0;
    const char* const kHw = "SC68: AMIGA (PAULA) TUNE, NOT PLAYED";
    for (size_t i = 0; i < tracks.size(); ++i) {
        const Track& t = tracks[i];
        const uint32_t flags = t.flags ? t.flags : 1;  // none: YM2149
        if (flags & 4) {
            if (skipped.empty()) skipped = kHw;
            continue;
        }
        if (!(flags & 1)) {
            if (skipped.empty()) skipped = "SC68: STE DMA SOUND ONLY, NOT PLAYED";
            continue;
        }
        Bytes rep;
        if (!t.replay.empty()) {
            std::string name = t.replay;
            for (char& c : name) c = (char)tolower((unsigned char)c);
            auto it = replays.find(name);
            if (it == replays.end()) {
                std::vector<uint8_t> out;
                bool found = false;
                if (plainName(name) && files)
                    for (const std::string& f : {name + ".bin", name})
                        if (files(f, out) && out.size() >= 12 && out.size() <= kMaxReplay) {
                            found = true;
                            break;
                        }
                it = replays.emplace(name, found ? std::make_shared<const std::vector<uint8_t>>(std::move(out)) : Bytes()).first;
            }
            rep = it->second;
            if (!rep) {
                if (skipped.empty() || skipped == kHw) skipped = "SC68: REPLAY '" + upper(t.replay) + "' NOT FOUND";
                continue;
            }
            if (replay_.empty()) replay_ = t.replay;
        }
        const uint32_t at = t.at ? t.at : 0x8000;
        const uint32_t dataAt = at + (rep ? (uint32_t)((rep->size() + 1) & ~(size_t)1) : 0);
        if ((at & 1) || at < kStub + 0x1000 || at >= kProgramHigh || t.size > kProgramHigh - dataAt || dataAt >= kProgramHigh) {
            if (skipped.empty()) skipped = "SC68: LOAD ADDRESS OUT OF RANGE";
            continue;
        }
        Bytes& blob = datas[t.data];
        if (!blob) blob = std::make_shared<const std::vector<uint8_t>>(t.data, t.data + t.size);
        // init stub: D0, D1 (1 = STF), D2 = data size, A0 = data, then the replay's init (it returns to the caller)
        Code c;
        c.op(0x203C, t.d0), c.op(0x223C, (flags & 2) ? 0 : 1), c.op(0x243C, t.size), c.op(0x41F9, dataAt);
        c.op(0x4EF9, at);
        StSong s;
        s.blocks.push_back({kStub, std::make_shared<const std::vector<uint8_t>>(std::move(c.b))});
        if (rep) s.blocks.push_back({at, rep});
        s.blocks.push_back({dataAt, blob});
        s.init = kStub;
        s.play = at + 8;
        s.d0 = t.d0;
        s.hz = t.frq >= 1 && t.frq <= 1000 ? (int)t.frq : 50;
        s.seconds = t.frames ? (double)t.frames / s.hz : (double)t.time;
        s.name = t.name;
        if ((int)i == defTrack) def = (int)songs.size();
        songs.push_back(std::move(s));
    }
    if (songs.empty()) {
        err = skipped;
        return false;
    }
    const Track& first = tracks[0];
    const std::string title = !disk.empty() ? disk : first.name;
    const std::string by = !first.composer.empty() ? first.composer : first.author;
    if (!loadSongs(std::move(songs), title, by, "SC68 (ATARI ST)", sub >= 0 ? sub : def)) {
        err = "SC68: THE REPLAY DOES NOT RUN";
        return false;
    }
    return true;
}

bool Sc68Player::loadYmst(const uint8_t* d, size_t n, const StFileResolver& files, int sub, std::string& err) {
    Ymst m;
    if (n > kMaxReplay || !m.parse(d, n)) {
        err = "YMST: CUT OR BROKEN FILE";
        return false;
    }
    if (m.kind() == 1) {
        err = "YMST: THIS IS A REPLAY FOR OTHER YMST FILES, NOT A TUNE";
        return false;
    }
    if (m.kind() != 0 && m.kind() != 2) return false;
    const bool external = m.kind() == 2;
    Ymst r;  // the replay file of music data
    std::vector<uint8_t> repBytes;
    if (external) {
        const std::string name = m.str('O');
        if (!plainName(name)) return false;
        std::vector<std::string> tries = {name};
        std::string spaced = name;  // Modland names some replays with spaces for the underscores
        for (char& c : spaced) c = c == '_' ? ' ' : c;
        tries.push_back(spaced);
        const size_t last = name.rfind('_');
        if (last != std::string::npos) tries.push_back(spaced.substr(0, last) + name.substr(last));
        bool found = false;
        if (files)
            for (const std::string& f : tries)
                if (files(f, repBytes) && repBytes.size() <= kMaxReplay && r.parse(repBytes.data(), repBytes.size()) &&
                    r.kind() == 1) {
                    found = true;
                    break;
                }
        if (!found) {
            err = "YMST: REPLAY '" + upper(name) + "' NOT FOUND";
            return false;
        }
        replay_ = name;
    }
    // entry points: the music's Z always; X (relocation) when there; the play call is [ (else \) of the replay
    const Ymst& player = external ? r : m;
    if (!m.code('Z') || !player.code('Z') || (!player.code('[') && !player.code('\\'))) return false;
    if ((m.is('X') && !m.code('X')) || (external && r.is('X') && !r.code('X'))) return false;
    const uint32_t musicAt = kYmstAt;
    const uint32_t replayAt = (uint32_t)((musicAt + n + 0x10000 + 0xFF) & ~(size_t)0xFF);
    std::vector<uint8_t> music(d, d + n);
    auto slot = [](std::vector<uint8_t>& f, const Ymst& y, char k, uint32_t v) {  // fill a pointer slot in the file
        if (!y.at(k)) return;
        const uint32_t o = y.value(k);
        f[o] = (uint8_t)(v >> 24), f[o + 1] = (uint8_t)(v >> 16), f[o + 2] = (uint8_t)(v >> 8), f[o + 3] = (uint8_t)v;
    };
    slot(music, m, 'P', kScratch);
    if (external) slot(repBytes, r, 'P', kScratch);
    const Bytes musicBytes = std::make_shared<const std::vector<uint8_t>>(std::move(music));
    const Bytes replayBytes = external ? std::make_shared<const std::vector<uint8_t>>(std::move(repBytes)) : Bytes();
    // sub-songs ] .. ^ (D0 = the number, or its entry of the ` table), lengths in R
    const uint32_t lo = m.value(']', 1), hi = m.value('^', lo);
    const int count = hi >= lo && hi - lo < 99 ? (int)(hi - lo + 1) : 1;
    const uint32_t defNum = m.value('_', lo);
    const int def = defNum >= lo && defNum - lo < (uint32_t)count ? (int)(defNum - lo) : 0;
    std::vector<StSong> songs;
    for (int i = 0; i < count; ++i) {
        const uint32_t num = lo + (uint32_t)i;
        uint32_t d0 = num;
        if (num >= 1 && m.at('`', (size_t)num * 4)) d0 = be32(d + m.value('`') + (size_t)(num - 1) * 4);
        Code c;
        auto store = [&]() {  // the replay gets the music's A0 (address) and A1 - A0 (size)
            if (r.at('c')) c.op(0x23C8, replayAt + r.value('c'));  // MOVE.L A0,abs
            c.w(0x93C8);                                              // SUBA.L A0,A1
            if (r.at('d')) c.op(0x23C9, replayAt + r.value('d'));  // MOVE.L A1,abs
        };
        c.w(0x2F00);  // MOVE.L D0,-(SP)
        if (m.is('X')) c.op(0x4EB9, musicAt + m.value('X'));  // JSR
        if (external) {
            store();
            if (r.is('X')) c.op(0x4EB9, replayAt + r.value('X'));
        }
        c.w(0x201F);  // MOVE.L (SP)+,D0
        c.op(0x4EB9, musicAt + m.value('Z'));
        if (external) {
            store();
            c.op(0x4EB9, replayAt + r.value('Z'));
        }
        c.w(0x4E75);  // RTS
        StSong s;
        s.blocks.push_back({kStub, std::make_shared<const std::vector<uint8_t>>(std::move(c.b))});
        s.blocks.push_back({musicAt, musicBytes});
        if (external) s.blocks.push_back({replayAt, replayBytes});
        s.init = kStub;
        s.d0 = d0;
        const uint32_t base = external ? replayAt : musicAt;
        s.play = base + player.value(player.code('[') ? '[' : '\\');
        s.hz = 50;  // MYST calls the play routine every VBL
        if (m.at('R', (size_t)(i + 1) * 4)) {
            const uint32_t t = be32(d + m.value('R') + (size_t)i * 4);
            const double secs = (t >> 16) * 60.0 + (t & 0xFFFF);
            s.seconds = secs < 86400 ? secs : 0;
        }
        songs.push_back(std::move(s));
    }
    std::string title = m.str('S');
    if (const size_t cut = title.find(" / "); cut != std::string::npos && cut > 0) title.resize(cut);  // "Name / Game"
    std::string by = m.str('T');
    if (const size_t cut = by.find(" / "); cut != std::string::npos && cut > 0) by.resize(cut);
    if (!loadSongs(std::move(songs), title, by, "YMST (ATARI ST)", sub >= 0 ? sub : def)) {
        err = "YMST: THE REPLAY DOES NOT RUN";
        return false;
    }
    return true;
}
