#include "core/amiga_synth.h"

#include <cmath>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>

#include "core/amiga_formats.h"
#include "core/song.h"

namespace {
constexpr double kPaulaClock = 3546895.0;  // PAL
}

// ---------------------------------------------------------------- AmigaChannel
void AmigaChannel::playSample(const int8_t* data, uint32_t start, uint32_t length) {
    data_ = data;
    pos_ = start;
    end_ = start + length;
    loop_ = pending_ = false;
    active_ = data && length > 0;
}

void AmigaChannel::setSample(const int8_t* data, uint32_t start, uint32_t length) {
    if (!active_) {  // nothing playing: the new sample starts now
        playSample(data, start, length);
        setLoop(start, length);
        return;
    }
    nextData_ = data;
    nextStart_ = start;
    nextLen_ = length;
    pending_ = data && length > 0;
}

void AmigaChannel::setLoop(uint32_t start, uint32_t length) {
    loopStart_ = start;
    loopEnd_ = start + length;
    loop_ = length > 0;
}

float AmigaChannel::next(double rate) {
    if (!active_ || !data_ || period_ < 2) return 0.f;
    const uint32_t i = (uint32_t)pos_;
    const float s = (i < end_ ? data_[i] : 0) / 128.f * volume_ / 256.f;
    pos_ += kPaulaClock / period_ / rate;
    if ((uint32_t)pos_ >= end_) {
        if (pending_) {  // a queued sample replaces the loop (Paula reloads at the end of the buffer)
            data_ = nextData_;
            pos_ = nextStart_ + (pos_ - end_);
            end_ = nextStart_ + nextLen_;
            loopStart_ = nextStart_;
            loopEnd_ = end_;
            loop_ = true;
            pending_ = false;
        } else if (loop_ && loopEnd_ > loopStart_) {
            const double over = pos_ - end_;
            end_ = loopEnd_;
            pos_ = loopStart_ + std::fmod(over, (double)(loopEnd_ - loopStart_));
        } else {
            active_ = false;
        }
    }
    return s;
}

// ---------------------------------------------------------------- AmigaSynthPlayer
std::unique_ptr<AmigaSynthPlayer> AmigaSynthPlayer::open(const uint8_t* d, size_t n) {
    if (!d || n < 8 || n > (64u << 20)) return nullptr;
    if (auto p = openJamCracker(d, n)) return p;
    if (auto p = openDeltaMusic1(d, n)) return p;
    if (auto p = openDeltaMusic2(d, n)) return p;
    if (auto p = openSidMon2(d, n)) return p;
    if (auto p = openSidMon1(d, n)) return p;
    if (auto p = openSoundMon(d, n)) return p;
    if (auto p = openMugician(d, n)) return p;
    if (auto p = openWhittaker(d, n)) return p;
    if (auto p = openSonicArranger(d, n)) return p;
    if (auto p = openArtOfNoise(d, n)) return p;
    return nullptr;
}

bool AmigaSynthPlayer::needsFiles(const uint8_t* d, size_t n) { return isIffSmus(d, n); }

std::unique_ptr<AmigaSynthPlayer> AmigaSynthPlayer::openWithFiles(const uint8_t* d, size_t n,
                                                                  const AmigaFileResolver& files, std::string* error) {
    if (!d || n < 8 || n > (64u << 20)) return nullptr;
    if (isIffSmus(d, n)) return openIffSmus(d, n, files, error);
    return open(d, n);
}

// ---------------------------------------------------------------- companion files
bool isPlainFileName(const std::string& name) {
    if (name.empty() || name.size() > 255 || name.find("..") != std::string::npos) return false;
    for (const char ch : name) {
        const unsigned char c = (unsigned char)ch;
        if (c < 0x20 || c == 0x7F || c == '/' || c == '\\' || c == ':') return false;
    }
    return true;
}

namespace {
// The regular file in `dir` whose name is `want` (exactly, else ignoring case), read when it is at most `maxBytes`.
bool readFromFolder(const std::filesystem::path& dir, const std::wstring& want, size_t maxBytes, std::vector<uint8_t>& out) {
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) return false;
    std::filesystem::path found;
    bool exact = false;
    for (int guard = 0; it != std::filesystem::directory_iterator() && guard < 100000; ++guard) {
        const std::wstring name = it->path().filename().wstring();
        const bool same = name == want;
        if ((same || _wcsicmp(name.c_str(), want.c_str()) == 0) && it->is_regular_file(ec) && !ec) {
            if (same || found.empty()) found = it->path();
            exact = exact || same;
            if (exact) break;
        }
        it.increment(ec);
        if (ec) break;
    }
    if (found.empty()) return false;
    const uintmax_t size = std::filesystem::file_size(found, ec);
    if (ec || size > maxBytes) return false;
    std::ifstream in(found, std::ios::binary);
    if (!in) return false;
    out.resize((size_t)size);
    if (size && !in.read(reinterpret_cast<char*>(out.data()), (std::streamsize)size)) {
        out.clear();
        return false;
    }
    return true;
}
}  // namespace

AmigaFileResolver folderFileResolver(const std::wstring& dir) {
    return [dir](const std::string& name, std::vector<uint8_t>& out) {
        out.clear();
        if (!isPlainFileName(name)) return false;
        std::wstring want;  // the names are Amiga (Latin-1) text
        for (const char ch : name) want += (wchar_t)(unsigned char)ch;
        constexpr size_t kMax = 16u << 20;
        const std::filesystem::path base(dir);
        return readFromFolder(base, want, kMax, out) || readFromFolder(base / L"Instruments", want, kMax, out);
    };
}

bool AmigaSynthPlayer::detect(const uint8_t* d, size_t n) {
    if (!d || n < 8) return false;
    return std::memcmp(d, "BeEp", 4) == 0  // JamCracker
           || isDeltaMusic1(d, n) || isDeltaMusic2(d, n)
           || detectSidMon2(d, n) || detectSidMon1(d, n) || isSoundMon(d, n) || isMugician(d, n)
           || detectWhittaker(d, n) || isSonicArranger(d, n) || isArtOfNoise(d, n);
}

void AmigaSynthPlayer::restart() {
    for (auto& c : ch_) c = AmigaChannel{};
    endReached_ = false;
    tickLeft_ = 0;
    initSound();
}

void AmigaSynthPlayer::clearMeters() {
    std::memset(vu_, 0, sizeof vu_);
    std::memset(scope_, 0, sizeof scope_);
}

long AmigaSynthPlayer::measure(double maxSeconds) {
    restart();
    long ticks = 0;
    const long limit = (long)(tickHz_ * maxSeconds);  // 30 minutes at most by default
    // formats that change their tick rate while playing (CIA tempo) are timed tick by tick
    const double startHz = tickHz_;
    bool steady = true;
    double secs = 0;
    while (!endReached_ && (steady ? ticks < limit : secs < maxSeconds)) {
        playTick();
        ++ticks;
        secs += 1.0 / tickHz_;
        steady = steady && tickHz_ == startHz;
    }
    seconds_ = steady ? ticks / tickHz_ : secs;
    restart();
    return ticks;
}

bool AmigaSynthPlayer::selectSubsong(int i) {
    if (i < 0 || i >= subsongs_) return false;
    subsong_ = i;
    measure();  // restarts on the sub-song
    return true;
}

void AmigaSynthPlayer::selectLongestSubsong() {
    // Each sub-song is measured up to 10 minutes (a longer one cannot lose), and all of them together up to
    // 4 hours of ticks, so files with many endless sub-songs still load quickly.
    constexpr double kCap = 10 * 60;
    long budget = (long)(tickHz_ * 4 * 60 * 60);
    int best = 0;
    double bestSeconds = -1;
    for (int i = 0; i < subsongs_ && budget > 0; ++i) {
        subsong_ = i;
        budget -= measure(kCap);
        if (seconds_ > bestSeconds) {
            best = i;
            bestSeconds = seconds_;
        }
        if (seconds_ >= kCap) break;
    }
    selectSubsong(best);
}

void AmigaSynthPlayer::render(float* out, uint32_t frames, float gain) {
    std::memset(out, 0, sizeof(float) * 2 * frames);
    float peak[kMaxVoices] = {};
    const int nv = voices_;
    bool leftOf[kMaxVoices] = {};
    for (int v = 0; v < nv; ++v) leftOf[v] = leftSide(v);
    for (uint32_t f = 0; f < frames; ++f) {
        if (tickLeft_ <= 0) {
            playTick();
            endReached_ = false;  // keeps looping
            tickLeft_ += rate_ / tickHz_;
        }
        tickLeft_ -= 1;
        float l = 0, r = 0;
        for (int v = 0; v < nv; ++v) {
            const float s = ch_[v].next(rate_) * (nv > 4 && v >= 3 && !ownChannels_ ? 0.25f : 1.f);
            const bool left = leftOf[v];  // Paula: 0 and 3 left, 1 and 2 right
            l += s * (left ? 1.f : 0.3f);
            r += s * (left ? 0.3f : 1.f);
            const float a = std::fabs(s);
            if (a > peak[v]) peak[v] = a;
            scope_[v][(scopePos_ + f) & (kScopeLen - 1)] = s;
        }
        out[2 * f] = l * gain;
        out[2 * f + 1] = r * gain;
    }
    scopePos_ = (scopePos_ + frames) & (kScopeLen - 1);
    const float decay = (float)std::pow(0.5, (double)frames / (rate_ * 0.08));
    for (int v = 0; v < kMaxVoices; ++v) {
        const float d = vu_[v] * decay;
        vu_[v] = peak[v] > d ? peak[v] : d;
    }
}

void AmigaSynthPlayer::scope(int v, float* dst, int n) const {
    if (n <= 0) return;
    if (n > kScopeLen) n = kScopeLen;
    if (v < 0 || v >= voices_) {
        for (int k = 0; k < n; ++k) dst[k] = 0;
        return;
    }
    const uint32_t start = (scopePos_ - (uint32_t)n) & (kScopeLen - 1);
    for (int k = 0; k < n; ++k) dst[k] = scope_[v][(start + k) & (kScopeLen - 1)];
}

bool AmigaSynthPlayer::buildDisplay(Song& out) {
    out = makeEmptySong(voices_);
    return true;
}
