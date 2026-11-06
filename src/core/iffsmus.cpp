// IFF SMUS replayer (Electronic Arts Deluxe Music Construction Set / Aegis Sonix scores with external
// instruments), ported from NostalgicPlayer's IffSmus player (IffSmusWorker.cs and Instruments/*.cs,
// Thomas Neumann, MIT licence) onto AmigaSynthPlayer.
//
// The score ("FORM" .... "SMUS": SHDR, NAME, AUTH, SNX1-9, INS1, TRAK chunks) names its instruments in INS1
// chunks; each is the file <name>.instr ('?' '<' '>' -> '!', '*' -> '+'), read through the caller's resolver
// (NostalgicPlayer looks in an "Instruments" folder next to the score). An instrument file is a Sonix synthesis
// sound (first byte 0 or "Synthesis"), a SampledSound (its sample data in <sample name>.ss next to it) or an
// IFF 8SVX sample. Like NostalgicPlayer, a score whose instrument cannot be opened or read is refused; MIDI
// instruments and empty names are skipped.
//
// Added for hostile files: every read is bounded by the data (the C# code relied on exceptions), chunk sizes
// must fit the file and may not be negative, at most 8 tracks (the Amiga synth layer's voices), no more TRAK
// chunks than tracks, names asked from the resolver must be plain file names (no \ / : or ".."), companion
// files are bounded in size (16 MB each, 64 MB together), sample ranges are clipped to their data, wave buffer
// and oscillator indexes are checked, and C# integer wrap-around is kept with explicit unsigned arithmetic.
#include <cstring>
#include <string>
#include <vector>

#include "core/amiga_formats.h"
#include "core/amiga_synth.h"

namespace {
// ---------------------------------------------------------------- tables (Tables.cs)
const uint16_t kTempo[128] = {
    0xfa83, 0xf525, 0xefe4, 0xeac0, 0xe5b9, 0xe0cc, 0xdbfb, 0xd744, 0xd2a8, 0xce24, 0xc9b9, 0xc567, 0xc12c,
    0xbd08, 0xb8fb, 0xb504, 0xb123, 0xad58, 0xa9a1, 0xa5fe, 0xa270, 0x9ef5, 0x9b8d, 0x9837, 0x94f4, 0x91c3,
    0x8ea4, 0x8b95, 0x8898, 0x85aa, 0x82cd, 0x8000, 0x7d41, 0x7a92, 0x77f2, 0x7560, 0x72dc, 0x7066, 0x6dfd,
    0x6ba2, 0x6954, 0x6712, 0x64dc, 0x62b3, 0x6096, 0x5e84, 0x5c7d, 0x5a82, 0x5891, 0x56ac, 0x54d0, 0x52ff,
    0x5138, 0x4f7a, 0x4dc6, 0x4c1b, 0x4a7a, 0x48e1, 0x4752, 0x45ca, 0x444c, 0x42d5, 0x4166, 0x4000, 0x3ea0,
    0x3d49, 0x3bf9, 0x3ab0, 0x396e, 0x3833, 0x36fe, 0x35d1, 0x34aa, 0x3389, 0x326e, 0x3159, 0x304b, 0x2f42,
    0x2e3e, 0x2d41, 0x2c48, 0x2b56, 0x2a68, 0x297f, 0x289c, 0x27bd, 0x26e3, 0x260d, 0x253d, 0x2470, 0x23a9,
    0x22e5, 0x2226, 0x216a, 0x20b3, 0x2000, 0x1f50, 0x1ea4, 0x1dfc, 0x1d58, 0x1cb7, 0x1c19, 0x1b7f, 0x1ae8,
    0x1a55, 0x19c4, 0x1937, 0x18ac, 0x1825, 0x17a1, 0x171f, 0x16a0, 0x1624, 0x15ab, 0x1534, 0x14bf, 0x144e,
    0x13de, 0x1371, 0x1306, 0x129e, 0x1238, 0x11d4, 0x1172, 0x1113, 0x10b5, 0x1059, 0x1000,
};
// note duration (division / dot) -> steps; -1 = not supported (the note is skipped)
const int8_t kDuration[16] = {32, 16, 8, 4, 2, -1, -1, -1, 48, 24, 12, 6, 3, -1, -1, -1};
const uint16_t kNote[13] = {0x8000, 0x78d1, 0x7209, 0x6ba2, 0x6598, 0x5fe4, 0x5a82,
                            0x556e, 0x50a3, 0x4c1c, 0x47d6, 0x43ce, 0x4000};
const uint16_t kX[64] = {
    0x8000, 0x7683, 0x6dba, 0x6597, 0x5e10, 0x5717, 0x50a2, 0x4aa8, 0x451f, 0x4000, 0x3b41, 0x36dd, 0x32cb,
    0x2f08, 0x2b8b, 0x2851, 0x2554, 0x228f, 0x2000, 0x1da0, 0x1b6e, 0x1965, 0x1784, 0x15c5, 0x1428, 0x12aa,
    0x1147, 0x1000, 0xed0,  0xdb7,  0xcb2,  0xbc2,  0xae2,  0xa14,  0x955,  0x8a3,  0x800,  0x768,  0x6db,
    0x659,  0x5e1,  0x571,  0x50a,  0x4aa,  0x451,  0x400,  0x3b4,  0x36d,  0x32c,  0x2f0,  0x2b8,  0x285,
    0x255,  0x228,  0x200,  0x1da,  0x1b6,  0x196,  0x178,  0x15c,  0x142,  0x12a,  0x114,  0x100,
};

constexpr int kMaxTracks = ChipPlayer::kMaxVoices;
constexpr size_t kMaxCompanionBytes = 64u << 20;  // all instrument and sample files of one score together

// C# int arithmetic wraps; these keep that without signed overflow
int32_t wmul(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
int32_t wadd(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
int32_t wsub(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }

// Big-endian reads over a buffer like NostalgicPlayer's ModuleStream: reading past the end gives zeros and sets
// eof; seeking past the end clamps and sets eof, seeking inside clears it.
struct Reader {
    const uint8_t* d;
    size_t n, pos = 0;
    bool eof = false;
    uint8_t u8() {
        if (pos >= n) {
            eof = true;
            return 0;
        }
        return d[pos++];
    }
    uint16_t u16() {
        const uint16_t a = u8();
        return (uint16_t)(a << 8 | u8());
    }
    uint32_t u32() {
        const uint32_t a = u16();
        return a << 16 | u16();
    }
    void seek(uint64_t to) {
        eof = to > n;
        pos = eof ? n : (size_t)to;
    }
    void skip(uint64_t k) { seek(pos + k); }
    // `len` bytes as text, up to the first 0
    std::string str(size_t len) {
        std::string s;
        bool end = false;
        for (size_t i = 0; i < len; ++i) {
            if (pos >= n) {
                eof = true;
                break;
            }
            const char c = (char)d[pos++];
            if (c == 0) end = true;
            if (!end) s += c;
        }
        return s;
    }
};

// ---------------------------------------------------------------- song and instruments
enum : uint8_t { kLastNote = 127, kRest = 128, kInstrument = 129, kTimeSig = 130, kVolume = 132, kMark = 255 };
struct Event {
    uint8_t type, data;
};

enum class Kind : uint8_t { Synth, Sampled, Form };

struct SynthData {  // SynthesisData.cs
    int8_t osc[128] = {}, lfo[256] = {};
    uint16_t waveAmt = 0, ampVolume = 0, ampEnabled = 0, ampLfo = 0, freqPort = 0, freqLfo = 0, filterFreq = 0,
             filterEg = 0, filterLfo = 0, lfoSpeed = 0;
    int16_t lfoEnabled = 0;  // LfoStatus: -1 on, 0 off, 1 once
    uint16_t lfoDelay = 0, phaseSpeed = 0, phaseDepth = 0, envLevels[4] = {}, envRates[4] = {};
    std::vector<int8_t> samples;  // 64 filtered versions of the oscillator, 128 bytes each
};
struct SsSample {  // SampledSoundSampleData.cs
    std::string name;
    uint16_t lenOct1 = 0, loopLenOct1 = 0;
    uint8_t startOct = 0, endOct = 0;
    std::vector<int8_t> data;
};
struct SampledData {  // SampledSoundData.cs
    uint16_t volume = 0, envLevels[4] = {}, envRates[4] = {};
    int16_t vibDepth = 0;
    uint16_t vibSpeed = 0, vibDelay = 0;
    int sample = 0;  // index into samples_
};
struct FormData {  // IffSample (8SVX VHDR + BODY)
    uint32_t oneShot = 0, repeat = 0, perCycle = 0, volume = 0;
    uint8_t octaves = 0;
    uint16_t skipOctaves = 0;  // numberOfHiOctavesToSkip
    std::vector<int8_t> data;
};
struct Instrument {
    std::string name;
    Kind kind = Kind::Synth;
    SynthData synth;
    SampledData sampled;
    FormData form;
};

// ---------------------------------------------------------------- playing state
enum class Setup : uint8_t { Nothing, Initialize, ReleaseNote, Mute };
enum class Status : uint8_t { Silence, Playing, Stopping };
enum class SetSample : uint8_t { Nothing, SetLoop, StartSample };

struct SynthPlay {
    uint16_t mappedNote = 0;
    int16_t freqCounter = 0, freqSpeed = 0;
    uint16_t sampleStartIndex = 0, playingOctave = 0, envIndex = 0;
    int32_t envVolume = 0;
    uint16_t lfoIndex = 0;
    int16_t lfoCounter = 0, lfoValue = 0, phaseIndex = 0, phaseDirection = 0;
};
struct SampledPlay {  // also FormPlayInfo (with volumeMultiply)
    bool loop = false;
    uint32_t loopStart = 0;
    uint16_t loopLenWords = 0, mappedNote = 0, envIndex = 0;
    int32_t envVolume = 0;
    uint16_t vibIndex = 0, vibDelayCounter = 0;
    int16_t vibValue = 0;
    uint16_t volumeMultiply = 0;
};
struct Voice {  // VoiceInfo.cs
    Status status = Status::Silence;
    Setup setup = Setup::Nothing;
    int instr = -1;  // the instrument whose format plays (InstrumentFormat), -1 = none
    uint8_t note = 0;
    uint16_t volume = 0;
    SetSample setSample = SetSample::Nothing;
    const int8_t* data = nullptr;
    size_t dataSize = 0;
    uint32_t start = 0;
    uint16_t lenWords = 0, period = 0, finalVolume = 0;
    int8_t synthBuf[256] = {};  // CalculatedSynthesisSample: two halves, one plays while the other is written
};

class IffSmus : public AmigaSynthPlayer {
public:
    bool load(const uint8_t* d, size_t n, const AmigaFileResolver& files, std::string& err) {
        files_ = &files;
        Reader r{d, n};
        r.pos = 4;
        int64_t total = (int32_t)r.u32();
        r.skip(4);
        total -= 4;
        int trackNumber = 0;
        while (total > 0) {
            const uint32_t id = r.u32();
            const uint32_t size = r.u32();
            total -= (int64_t)size + 8;
            if (r.eof) break;  // no chunks left
            if (size > 0x7FFFFFFF || size > n - r.pos) return fail(err, "broken score (a chunk is longer than the file)");
            const size_t start = r.pos;
            bool ok = true;
            switch (id) {
                case 0x4E414D45:  // NAME
                    title_ = r.str(size);
                    ok = !r.eof;
                    break;
                case 0x41555448:  // AUTH
                    author_ = r.str(size);
                    ok = !r.eof;
                    break;
                case 0x53484452:  // SHDR
                    ok = parseShdr(r, err);
                    break;
                case 0x494E5331:  // INS1
                    ok = parseIns1(r, size, err);
                    break;
                case 0x5452414B:  // TRAK
                    ok = parseTrak(r, size, trackNumber, err);
                    break;
                default:
                    if ((id & 0xFFFFFF00u) == 0x534E5800u && (id & 0xFF) >= '1' && (id & 0xFF) <= '9')  // SNX1-9
                        ok = parseSnx(r);
                    break;
            }
            if (!ok) return err.empty() ? fail(err, "broken score") : false;
            uint64_t padded = size;
            if (size & 1) {
                ++padded;
                --total;
            }
            r.seek(start + padded);
        }
        files_ = nullptr;
        if (channels_ == 0 || !mapperUsed_ || !tracksUsed_) return fail(err, "broken score (a chunk is missing)");
        voices_ = channels_;
        ownChannels_ = true;
        format_ = "IFF SMUS (AMIGA)";
        tickHz_ = 709379.0 / 11932;
        measure();
        return true;
    }

protected:
    void initSound() override {
        // a freshly loaded song (AllocateStructures), then InitializeSound
        pi_ = Playing{};
        pi_.calculatedTempo = 0x8000;
        pi_.currentVolume = 0xFFFF;
        for (Voice& v : voice_) v = Voice{};
        for (SynthPlay& p : synthPlay_) p = SynthPlay{};
        for (SampledPlay& p : samplePlay_) p = SampledPlay{};
        for (SampledPlay& p : formPlay_) p = SampledPlay{};

        stopSound();
        for (int i = 0; i < channels_; ++i) enabled_[i] = trackEnabled_[i];
        // InitializeTracks(0): every track starts at its first event
        for (int i = 0; i < channels_; ++i) {
            trackStart_[i] = tracks_[i].empty() ? -1 : 0;
            infoInstr_[i] = 0;
            infoTimeLeft_[i] = 0;
        }
        startModule();
        pi_.speedCounter = 0;
        pi_.currentVolume = 0;
        pi_.currentTempo = 0;
        newTempo_ = tempoIndex_;
        tune_ = moduleTune_;
        setVolume(1, globalVolume_);
        pi_.repeatCount = -1;
        tickHz_ = 709379.0 / 11932;  // SetCiaTimerTempo(11932), about 60 Hz
    }

    void playTick() override {
        findVolume();
        getNextNote();
        setupAndPlayInstruments();
    }

private:
    struct Playing {  // GlobalPlayingInfo.cs
        int16_t repeatCount = 0;
        uint32_t currentTime = 0;
        uint8_t flag = 0;
        uint16_t speedCounter = 0, currentTempo = 0, calculatedTempo = 0, calculatedSpeed = 0;
        uint16_t maxVolume = 0, newVolume = 0, currentVolume = 0;
        int currentInstr[kMaxTracks] = {-1, -1, -1, -1, -1, -1, -1, -1};
        uint32_t hold[kMaxTracks] = {}, release[kMaxTracks] = {};
        int trackPos[kMaxTracks] = {};
    };

    // song (ModuleInfo / GlobalInfo)
    std::string title_, author_;
    int channels_ = 0;
    uint8_t globalVolume_ = 0;
    uint16_t tempoIndex_ = 0, transpose_ = 0, moduleTune_ = 0;
    uint16_t trackVolumes_[kMaxTracks] = {};
    uint64_t trackEnabled_[kMaxTracks] = {};
    std::vector<Event> tracks_[kMaxTracks];  // empty = no TRAK chunk (null in the C# code)
    bool tracksUsed_ = false, mapperUsed_ = false;
    int mapper_[256] = {};  // instrument register -> instrument index + 1
    std::vector<Instrument> instruments_;
    std::vector<SsSample> samples_;
    const AmigaFileResolver* files_ = nullptr;  // only while loading
    size_t companionBytes_ = 0;

    // playing
    uint16_t newTempo_ = 0, tune_ = 0, volume_ = 0;
    uint64_t enabled_[kMaxTracks] = {};
    int trackStart_[kMaxTracks] = {};
    uint8_t infoInstr_[kMaxTracks] = {}, infoTimeLeft_[kMaxTracks] = {};
    Playing pi_;
    Voice voice_[kMaxTracks];
    SynthPlay synthPlay_[kMaxTracks];
    SampledPlay samplePlay_[kMaxTracks], formPlay_[kMaxTracks];

    static bool fail(std::string& err, const char* what) {
        err = what;
        return false;
    }

    // ------------------------------------------------------------ loading
    bool parseShdr(Reader& r, std::string& err) {
        const uint16_t tempo = r.u16();
        if (tempo >= 0xe11) {
            const int find = 0xe100000 / tempo;
            int i = 0;
            while (i < 128 && find < kTempo[i]) ++i;
            tempoIndex_ = (uint16_t)(i == 128 ? 127 : i);
        } else {
            tempoIndex_ = 0;
        }
        globalVolume_ = r.u8();
        if (globalVolume_ < 128) globalVolume_ = (uint8_t)(globalVolume_ * 2);
        const int channels = r.u8();
        if (r.eof) return false;
        if (channels > kMaxTracks) return fail(err, "too many tracks (at most 8 are played)");
        if (tracksUsed_ && channels != channels_) return false;  // the tracks were made for another count
        channels_ = channels;
        for (int i = 0; i < kMaxTracks; ++i) {
            trackVolumes_[i] = 0xff;
            trackEnabled_[i] = 1;
        }
        return true;
    }

    bool parseIns1(Reader& r, uint32_t size, std::string& err) {
        mapperUsed_ = true;
        const uint8_t reg = r.u8();
        if (mapper_[reg] != 0) return true;
        if (r.u8() != 0) return true;  // MIDI instruments cannot be used
        r.skip(2);                     // data1, data2
        if (size < 4) return false;  // the C# code throws on a negative name length
        const std::string name = r.str(size - 4);
        if (name.empty()) return true;
        if (r.eof) return false;
        const int number = loadInstrument(name, err);
        if (number < 0) return false;
        mapper_[reg] = number + 1;
        return true;
    }

    bool parseTrak(Reader& r, uint32_t size, int& trackNumber, std::string& err) {
        if (!tracksUsed_) {
            if (channels_ == 0) return false;
            tracksUsed_ = true;
        }
        if (trackNumber >= channels_) return fail(err, "broken score (more TRAK chunks than tracks)");
        std::vector<Event> events;
        events.reserve(size / 2 + 1);
        for (uint32_t i = 0; i < size / 2; ++i) {
            const uint8_t type = r.u8();
            uint8_t data = r.u8();
            if (type == kMark) break;
            if (type <= kLastNote || type == kRest) {
                const int8_t dur = kDuration[data & 0x0f];  // chord, tie and n-tuplet bits masked out
                if (dur < 0) continue;
                data = (uint8_t)dur;
            } else if (type == kInstrument) {
            } else if (type == kVolume) {
                trackVolumes_[trackNumber] = (uint16_t)((data & 0x7f) * 2);
                continue;
            } else {
                continue;  // time / key signature, clef, tempo, MIDI events are ignored
            }
            events.push_back({type, data});
        }
        if (r.eof) return false;
        events.push_back({kMark, 0xff});
        tracks_[trackNumber++] = static_cast<std::vector<Event>&&>(events);
        return true;
    }

    bool parseSnx(Reader& r) {
        if (channels_ == 0) return false;
        transpose_ = r.u16();
        moduleTune_ = r.u16();
        r.skip(4);
        for (int i = 0; i < channels_; ++i) trackEnabled_[i] = r.u32();
        return !r.eof;
    }

    static std::string fixFileName(std::string s) {
        for (char& c : s)
            if (c == '?' || c == '<' || c == '>') c = '!';
            else if (c == '*') c = '+';
        return s;
    }
    static bool sameName(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            const unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
            if ((x >= 'A' && x <= 'Z' ? x + 32 : x) != (y >= 'A' && y <= 'Z' ? y + 32 : y)) return false;
        }
        return true;
    }

    // A companion file through the resolver: plain names only, bounded in size.
    bool readCompanion(const std::string& name, std::vector<uint8_t>& out, std::string& err) {
        out.clear();
        if (!isPlainFileName(name) || !files_ || !*files_ || !(*files_)(name, out)) {
            out.clear();
            err = "cannot open " + name;
            return false;
        }
        companionBytes_ += out.size();
        if (out.size() > (16u << 20) || companionBytes_ > kMaxCompanionBytes) {
            err = "instrument files too big: " + name;
            return false;
        }
        return true;
    }

    int loadInstrument(const std::string& instrName, std::string& err) {
        for (size_t i = 0; i < instruments_.size(); ++i)
            if (sameName(instruments_[i].name, instrName)) return (int)i;
        const std::string fileName = fixFileName(instrName + ".instr");
        std::vector<uint8_t> f;
        if (!readCompanion(fileName, f, err)) return -1;
        if (f.size() < 32) {
            err = "cannot read " + fileName;
            return -1;
        }
        Instrument ins;
        ins.name = instrName;
        bool ok;
        auto starts = [&](const char* id) { return sameName(std::string(f.begin(), f.begin() + std::strlen(id)), id); };
        if (f[0] == 0 || starts("Synthesis")) {
            ins.kind = Kind::Synth;
            ok = loadSynth(f, ins.synth);
        } else if (starts("SampledSound")) {
            ins.kind = Kind::Sampled;
            ok = loadSampled(f, ins.sampled, err);
        } else if (starts("FORM")) {
            ins.kind = Kind::Form;
            ok = loadForm(f, ins.form);
        } else {
            ok = false;
        }
        if (!ok) {
            if (err.empty()) err = "cannot read " + fileName;
            return -1;
        }
        instruments_.push_back(static_cast<Instrument&&>(ins));
        return (int)instruments_.size() - 1;
    }

    static bool loadSynth(const std::vector<uint8_t>& f, SynthData& s) {  // SynthesisFormat.Load
        Reader r{f.data(), f.size()};
        r.seek(0x44);
        for (int8_t& b : s.osc) b = (int8_t)r.u8();
        for (int8_t& b : s.lfo) b = (int8_t)r.u8();
        if (r.eof) return false;
        r.u16();  // waveform
        r.skip(4);
        s.waveAmt = r.u16();
        s.ampVolume = r.u16();
        s.ampEnabled = r.u16();
        s.ampLfo = r.u16();
        s.freqPort = r.u16();
        s.freqLfo = r.u16();
        s.filterFreq = r.u16();
        s.filterEg = r.u16();
        s.filterLfo = r.u16();
        s.lfoSpeed = r.u16();
        s.lfoEnabled = (int16_t)r.u16();
        s.lfoDelay = r.u16();
        s.phaseSpeed = r.u16();
        s.phaseDepth = r.u16();
        for (uint16_t& v : s.envLevels) v = r.u16();
        for (uint16_t& v : s.envRates) v = r.u16();
        if (r.eof) return false;
        // CalculateSamples: 64 low-pass filtered copies of the oscillator (identical oscillators give identical
        // copies; NostalgicPlayer shares them)
        s.samples.assign(64 * 128, 0);
        int16_t d4 = (int16_t)(s.osc[127] << 7);
        int16_t d3 = 0;
        for (int i = 0; i < 64; ++i) {
            uint16_t d1 = kX[i];
            const uint16_t d2 = (uint16_t)(((0x8000 - d1) * 58982) >> 16);
            d1 /= 2;
            for (int j = 0; j < 128; ++j) {
                d3 = (int16_t)(d3 + (int16_t)((((s.osc[j] << 7) - d4) * d1) >> 14));
                d4 = (int16_t)(d4 + d3);
                s.samples[(size_t)i * 128 + j] = (int8_t)(uint8_t)(d4 >> 7);
                d3 = (int16_t)((d3 * d2) >> 15);
            }
        }
        return true;
    }

    bool loadSampled(const std::vector<uint8_t>& f, SampledData& s, std::string& err) {  // SampledSoundFormat.Load
        Reader r{f.data(), f.size()};
        r.seek(0x44);
        const std::string sampleName = r.str(32);
        r.skip(4);  // pointer to the data
        s.volume = r.u16();
        for (uint16_t& v : s.envLevels) v = r.u16();
        for (uint16_t& v : s.envRates) v = r.u16();
        s.vibDepth = (int16_t)r.u16();
        s.vibSpeed = r.u16();
        s.vibDelay = r.u16();
        if (r.eof) return false;
        for (size_t i = 0; i < samples_.size(); ++i)
            if (sameName(samples_[i].name, sampleName)) {
                s.sample = (int)i;  // shared with an earlier instrument
                return true;
            }
        const std::string fileName = fixFileName(sampleName + ".ss");
        std::vector<uint8_t> ss;
        if (!readCompanion(fileName, ss, err)) return false;
        SsSample smp;
        smp.name = sampleName;
        Reader q{ss.data(), ss.size()};
        smp.lenOct1 = q.u16();
        smp.loopLenOct1 = q.u16();
        smp.startOct = q.u8();
        smp.endOct = q.u8();
        if (q.eof || ss.size() < 0x3e) {
            err = "cannot read " + fileName;
            return false;
        }
        smp.data.assign(ss.begin() + 0x3e, ss.end());
        samples_.push_back(static_cast<SsSample&&>(smp));
        s.sample = (int)samples_.size() - 1;
        return true;
    }

    static bool loadForm(const std::vector<uint8_t>& f, FormData& s) {  // IffSampleLoader.Load + FormFormat.Load
        const size_t n = f.size();
        if (n < 12 || std::memcmp(f.data(), "FORM", 4) != 0 || std::memcmp(f.data() + 8, "8SVX", 4) != 0) return false;
        const uint32_t formLength = (uint32_t)f[4] << 24 | (uint32_t)f[5] << 16 | (uint32_t)f[6] << 8 | f[7];
        uint64_t left = (uint32_t)(formLength - 4u);  // wraps like the C# uint
        Reader r{f.data(), n};
        r.pos = 12;
        bool body = false;
        while (left > 0 && r.pos + 8 <= n) {
            const uint32_t id = r.u32();
            uint64_t len = r.u32();
            left = left >= 8 ? left - 8 : 0;
            if (len & 1) ++len;
            const size_t start = r.pos;
            if (id == 0x56484452) {  // VHDR
                if (len < 20 || start + 20 > n) return false;
                s.oneShot = r.u32();
                s.repeat = r.u32();
                s.perCycle = r.u32();
                r.u16();  // samples per second
                s.octaves = r.u8();
                if (r.u8() != 0) return false;  // compressed
                s.volume = r.u32();
            } else if (id == 0x424F4459) {  // BODY
                if (body || s.octaves == 0) return false;
                // some samples store the body length in words
                if (len * 2 + start - 8 == formLength) len *= 2;
                if (len > n - start) return false;
                s.data.assign(f.begin() + start, f.begin() + start + (size_t)len);
                body = true;
            }
            left = left >= len ? left - len : 0;
            r.seek(start + len);
            if (r.eof) break;
        }
        if (!body) return false;
        if (s.oneShot == 0 && s.repeat == 0) s.oneShot = (uint32_t)s.data.size();
        uint32_t oneShot = s.oneShot, repeat = s.repeat, perCycle = s.perCycle;
        uint16_t count = 0;
        for (;;) {
            oneShot /= 2;
            repeat /= 2;
            perCycle /= 2;
            ++count;
            if (((oneShot | repeat) & 1) != 0) break;
            if (perCycle == 1) break;
            if (count + s.octaves >= 8) break;
        }
        s.skipOctaves = count;
        return true;
    }

    // ------------------------------------------------------------ playing (IffSmusWorker.cs)
    void stopSound() {
        pi_.flag &= 0xfe;
        releaseAllVoices();
    }

    void releaseAllVoices() {
        if (pi_.repeatCount != 0) {
            pi_.repeatCount = 0;
            for (int i = 0; i < channels_; ++i)
                if (enabled_[i] != 0) beginToReleaseVoice(i);
        }
    }

    void beginToReleaseVoice(int ch) {
        Voice& v = voice_[ch];
        v.setup = Setup::Nothing;
        if (v.status == Status::Playing) v.setup = Setup::ReleaseNote;
    }

    void startModule() {  // StartModuleAndInitializeInstruments
        pi_.currentTime = 0;
        for (int i = 0; i < channels_; ++i) {
            pi_.trackPos[i] = trackStart_[i];
            if (enabled_[i] != 0) beginToReleaseVoice(i);
            pi_.hold[i] = 0;
            pi_.release[i] = infoTimeLeft_[i] + 1u;
            const uint8_t number = infoInstr_[i];
            if (number != 0 && mapper_[number - 1] != 0) pi_.currentInstr[i] = mapper_[number - 1] - 1;
        }
    }

    void setVolume(uint16_t scale, uint16_t newVolume) {
        pi_.newVolume = 0;
        if (scale != 0) {
            pi_.maxVolume = (uint16_t)(pi_.currentVolume * 256);
            volume_ = (uint16_t)(newVolume * 256);
            int volume = pi_.maxVolume - volume_;
            if (volume < 0) volume = -volume;
            volume /= scale;
            if (volume == 0) ++volume;
            pi_.newVolume = (uint16_t)volume;
        } else {
            pi_.currentVolume = newVolume;
        }
    }

    void findVolume() {
        if (pi_.newVolume == 0) return;
        int32_t temp1 = wmul(pi_.newVolume, pi_.calculatedTempo);
        if (temp1 >= 0) {
            temp1 >>= 15;
            temp1 &= 0xffff;
            int32_t temp2 = temp1;
            int32_t newVolume = volume_ - pi_.maxVolume;
            if (newVolume < 0) {
                temp2 = -temp2;
                newVolume = -newVolume;
            }
            if (temp1 < newVolume) {
                temp2 += pi_.maxVolume;
                pi_.maxVolume = (uint16_t)temp2;
                pi_.currentVolume = (uint16_t)(temp2 / 256);
                return;
            }
        }
        if (volume_ == 0) {
            const bool isSet = (pi_.flag & 0x01) != 0;
            pi_.flag &= 0xfe;
            if (isSet) releaseAllVoices();
        }
        pi_.newVolume = 0;
        pi_.currentVolume = (uint16_t)(volume_ / 256);
    }

    void getNextNote() {
        if (pi_.speedCounter != 0) pi_.speedCounter--;
        if (pi_.speedCounter != 0) return;
        if (pi_.currentTempo != newTempo_) {
            pi_.currentTempo = newTempo_;
            const uint16_t tempo = kTempo[pi_.currentTempo & 127];
            pi_.calculatedSpeed = (uint16_t)(tempo >> 12);
            pi_.calculatedTempo = (uint16_t)((tempo << 15) / (pi_.calculatedSpeed << 12));
            const uint16_t cia = (uint16_t)((pi_.calculatedTempo * 11932) >> 15);
            if (cia != 0) tickHz_ = 709379.0 / cia;
        }
        if (pi_.repeatCount == 0) return;
        pi_.speedCounter = pi_.calculatedSpeed;
        int tracksDone = 0;
        bool beginOver;
        do {
            beginOver = false;
            for (int i = 0; i < channels_; ++i) {
                if (pi_.hold[i] != 0) {
                    pi_.hold[i]--;
                    if (pi_.hold[i] == 0 && enabled_[i] != 0) beginToReleaseVoice(i);
                    continue;
                }
                if (pi_.release[i] != 0) {
                    pi_.release[i]--;
                    if (pi_.release[i] != 0) continue;
                }
                if (pi_.trackPos[i] == -1) {
                    ++tracksDone;
                    continue;
                }
                const std::vector<Event>& track = tracks_[i];
                bool oneMore;
                do {
                    oneMore = false;
                    const Event e = track[(size_t)pi_.trackPos[i]];  // every track ends with a mark
                    if (e.type == kMark) {
                        ++tracksDone;
                        continue;
                    }
                    pi_.trackPos[i]++;
                    if (e.type <= kLastNote) {
                        uint16_t duration = e.data;
                        if (enabled_[i] != 0 && pi_.currentInstr[i] >= 0) {
                            const uint16_t note = (uint16_t)(e.type + transpose_ / 16 - 8);
                            uint16_t vol = trackVolumes_[i];
                            if (enabled_[i] != 1) vol /= 2;
                            setupInstrumentIfFormatChanged(pi_.currentInstr[i], i, (uint8_t)note, vol);
                            const uint16_t temp = (uint16_t)((duration * 0xc000) >> 16);
                            pi_.hold[i] = temp;
                            duration = (uint16_t)(duration - temp);
                        }
                        pi_.release[i] = duration;
                    } else if (e.type == kRest) {
                        pi_.release[i] = e.data;
                    } else if (e.type == kInstrument) {
                        const int number = mapper_[e.data];
                        if (number != 0) pi_.currentInstr[i] = number - 1;
                        oneMore = true;
                    } else {
                        oneMore = true;
                    }
                } while (oneMore);
            }
            pi_.currentTime++;  // the end time is never set (no start / end positions)
            if (tracksDone == channels_) {
                // endless repeat (RepeatCount < 0): start over in the same tick
                startModule();
                ++tracksDone;
                beginOver = true;
                endReached_ = true;  // OnEndReachedOnAllChannels
            }
        } while (beginOver);
    }

    void setupInstrumentIfFormatChanged(int instr, int ch, uint8_t note, uint16_t volume) {
        Voice& v = voice_[ch];
        if (v.status != Status::Silence && v.instr >= 0 && instruments_[(size_t)instr].kind != instruments_[(size_t)v.instr].kind)
            muteInstrument(ch);
        v.instr = instr;
        v.note = note;
        v.volume = (uint16_t)(volume & 0xff);
        v.setup = Setup::Initialize;
    }

    void muteInstrument(int ch) {
        Voice& v = voice_[ch];
        v.setup = Setup::Nothing;
        if (v.status != Status::Silence) {
            v.setup = Setup::Mute;
            if (v.instr >= 0) setup(ch);
            v.status = Status::Silence;
            v.setup = Setup::Nothing;
        }
    }

    void setupAndPlayInstruments() {
        for (int i = 0; i < channels_; ++i) {
            Voice& v = voice_[i];
            if ((v.setup != Setup::Nothing || v.status != Status::Silence) && v.instr >= 0) setup(i);
            if (v.instr >= 0) {
                if (v.setup == Setup::Initialize) v.status = Status::Playing;
                else if (v.setup == Setup::ReleaseNote) v.status = Status::Stopping;
            } else {
                v.status = Status::Silence;
            }
            v.setup = Setup::Nothing;
        }
        for (int i = 0; i < channels_; ++i)
            if (voice_[i].status != Status::Silence) play(i);
    }

    void setup(int ch) {
        const Instrument& ins = instruments_[(size_t)voice_[ch].instr];
        switch (ins.kind) {
            case Kind::Synth: setupSynth(ch, ins.synth); break;
            case Kind::Sampled: setupSampled(ch, ins.sampled); break;
            case Kind::Form: setupForm(ch, ins.form); break;
        }
    }

    void play(int ch) {
        Voice& v = voice_[ch];
        AmigaChannel& c = ch_[ch];
        if (instruments_[(size_t)v.instr].kind == Kind::Synth) {
            const uint32_t len = v.lenWords * 2u;  // at most 128, inside the 256 byte buffer
            if (v.setSample == SetSample::StartSample) c.playSample(v.synthBuf, v.start, len);
            else c.setSample(v.synthBuf, v.start, len);
            c.setLoop(v.start, len);
        } else {
            const SampledPlay& p = instruments_[(size_t)v.instr].kind == Kind::Sampled ? samplePlay_[ch] : formPlay_[ch];
            if (v.setSample == SetSample::StartSample) {
                uint32_t start = v.start, len = v.lenWords * 2u;
                if (clipRange(v.dataSize, start, len)) c.playSample(v.data, start, len);
                else c.mute();
                v.setSample = SetSample::SetLoop;
            }
            if (v.setSample == SetSample::SetLoop && p.loop) {
                uint32_t start = p.loopStart, len = p.loopLenWords * 2u;
                if (clipRange(v.dataSize, start, len)) c.setLoop(start, len);
            }
        }
        v.setSample = SetSample::Nothing;
        c.setAmigaPeriod(v.period);
        c.setAmigaVolume(v.finalVolume);
    }

    // the ADSR shared by synthesis and sampled sounds
    void envelope(uint16_t& index, int32_t& volume, const uint16_t levels[4], const uint16_t rates[4]) const {
        const int32_t t1 = (int32_t)((uint32_t)levels[index] << 16);
        int32_t t2 = volume;
        int32_t t3 = rates[index];
        int32_t t0 = (t3 >> 5) ^ 7;
        t3 = ((((t3 & 0x1f) + 0x21) * pi_.calculatedTempo) << 3) >> (t0 & 31);
        t0 = wsub(t1, t2);
        if (t0 < 0) t0 = wsub(0, t0);
        if (t0 > t3) {
            t2 = t2 < t1 ? wadd(t2, t3) : wsub(t2, t3);
        } else {
            t2 = t1;
            if (index < 2) ++index;
        }
        volume = t2;
    }

    // ------------------------------------------------------------ SynthesisFormat.cs
    void setupSynth(int ch, const SynthData& f) {
        Voice& v = voice_[ch];
        SynthPlay& p = synthPlay_[ch];
        const int32_t ct = pi_.calculatedTempo;
        switch (v.setup) {
            case Setup::Initialize: {
                if (v.note < 36 || v.note >= 108) {
                    v.setup = Setup::Nothing;
                    if (v.status == Status::Silence) return;
                    break;
                }
                int note = v.note - 36;
                if (v.status == Status::Silence) p.envVolume = 0;
                if (v.status != Status::Playing) p.envIndex = 0;
                const int octave = note / 12;
                note %= 12;
                uint16_t mapped = (uint16_t)((kNote[note] * 54728) >> (octave + 17));
                if (p.mappedNote == 0) {
                    p.freqCounter = 0;
                } else {
                    const int diff = mapped - p.mappedNote;
                    p.freqCounter = (int16_t)((((f.freqPort << 15) / ct) >> 3) + 1);
                    p.freqSpeed = (int16_t)(diff / p.freqCounter);
                    mapped = (uint16_t)(mapped - (uint16_t)(p.freqSpeed * p.freqCounter));
                }
                p.mappedNote = mapped;
                p.phaseDirection = 1;
                if (f.phaseSpeed == 0) p.phaseIndex = 0;
                p.lfoCounter = 0;
                if (f.lfoEnabled != 0) {
                    p.lfoIndex = 0;
                    p.lfoCounter = (int16_t)(((f.lfoDelay << 15) / ct) >> 2);
                    p.lfoValue = f.lfo[0];
                }
                v.setSample = SetSample::StartSample;
                break;
            }
            case Setup::ReleaseNote:
                p.envIndex = 3;
                break;
            case Setup::Mute:
                ch_[ch].mute();
                return;
            default:
                break;
        }
        if (p.lfoCounter > 0) {
            p.lfoCounter--;
        } else if (p.lfoCounter == 0) {
            const int16_t index = (int16_t)(p.lfoIndex + (wmul(f.lfoSpeed, ct) >> 10));
            if (index < 0 && f.lfoEnabled == 1) {
                p.lfoCounter = -1;
            } else {
                p.lfoIndex = (uint16_t)index;
                p.lfoValue = f.lfo[p.lfoIndex >> 8];
            }
        }
        envelope(p.envIndex, p.envVolume, f.envLevels, f.envRates);
        uint16_t oct = 5;
        if (p.freqCounter != 0) {
            p.freqCounter--;
            p.mappedNote = (uint16_t)(p.mappedNote + p.freqSpeed);
        }
        uint16_t tempNote = p.mappedNote;
        while (tempNote > 428) {
            tempNote /= 2;
            oct--;  // wraps like the C# ushort (shift counts are masked below)
        }
        p.playingOctave = oct;
        const int po = p.playingOctave & 31;
        v.lenWords = (uint16_t)(64 >> po);
        v.period = (uint16_t)(wmul(wadd(wsub(wmul(f.freqLfo, p.lfoValue) >> 7, tune_ - 0x80), 0x1000), tempNote) >> 12);
        uint16_t vol = (uint16_t)((wmul(f.ampLfo, -p.lfoValue) >> 8) + f.ampVolume);
        if (f.ampEnabled != 0) vol = (uint16_t)(wmul(p.envVolume >> 16, vol) >> 8);
        else if (p.envIndex == 3) vol = 0;
        v.finalVolume = (uint16_t)((((((((vol & 0xff) + 1) * pi_.currentVolume) / 256) + 1) * v.volume) / 256 + 1) / 4);
        const int sampleIndex =
            ((((f.filterFreq ^ 0xff) - (wmul(p.envVolume >> 16, f.filterEg) >> 8)) + (wmul(p.lfoValue, f.filterLfo) >> 8)) & 0xff) >> 2;
        const int8_t* src = f.samples.data() + (size_t)sampleIndex * 128;
        p.sampleStartIndex ^= 0x80;
        uint32_t dest = p.sampleStartIndex;
        v.data = v.synthBuf;
        v.dataSize = sizeof v.synthBuf;
        v.start = dest;
        auto put = [&](int s) {
            if (dest < sizeof v.synthBuf) v.synthBuf[dest] = (int8_t)s;
            ++dest;
        };
        auto at = [&](int64_t i) -> int { return i >= 0 && i < 128 ? src[i] : 0; };
        const int64_t step = (int64_t)1 << po;
        if (f.phaseSpeed == 0) {
            const int length = 128 >> po;
            int64_t si = 0;
            for (int i = 0; i < length; ++i, si += step) put(at(si));
        } else if (f.phaseDepth == 0) {
            p.phaseIndex = (int16_t)(p.phaseIndex + (int16_t)(wmul(f.phaseSpeed, ct) >> 13));
            int64_t s1 = 0, s2 = (uint16_t)p.phaseIndex >> 9;
            const int length2 = (int)(s2 >> po);
            const int length1 = v.lenWords * 2 - length2;
            for (int i = 0; i < length1; ++i, s1 += step, s2 += step) put((at(s1) + at(s2)) / 2);
            if (length2 > 0) {
                s2 -= 128;
                for (int i = 0; i < length2; ++i, s1 += step, s2 += step) put((at(s1) + at(s2)) / 2);
            }
        } else {
            int16_t index = (int16_t)wadd(wmul(wmul(f.phaseSpeed, ct) >> 11, p.phaseDirection), p.phaseIndex);
            if (index < 0) {
                if (index == -32768) index = (int16_t)(index + p.phaseDirection);
                p.phaseDirection = (int16_t)-p.phaseDirection;
                index = (int16_t)-index;
            }
            p.phaseIndex = index;
            const int32_t d = wmul(f.phaseDepth, p.phaseIndex) >> ((17 + p.playingOctave) & 31);
            const int32_t length1 = v.lenWords + d, length2 = v.lenWords - d;
            if (length1 != 0) {
                const int32_t inc = 64 / length1, rem = 64 % length1;
                int64_t si = 0;
                int64_t t = 0;
                for (int32_t i = 0; i < length1; ++i) {
                    put(at(si));
                    t -= rem;
                    if (t < 0) {
                        t += length1;
                        ++si;
                    }
                    si += inc;
                }
            }
            if (length2 != 0) {
                const int32_t inc = 64 / length2, rem = 64 % length2;
                int64_t si = 64;
                int64_t t = 0;
                for (int32_t i = 0; i < length2; ++i) {
                    put(at(si));
                    t -= rem;
                    if (t < 0) {
                        t += length1;  // sic (NostalgicPlayer)
                        ++si;
                    }
                    si += inc;
                }
            }
        }
    }

    // ------------------------------------------------------------ SampledSoundFormat.cs
    void setupSampled(int ch, const SampledData& f) {
        Voice& v = voice_[ch];
        SampledPlay& p = samplePlay_[ch];
        const SsSample& smp = samples_[(size_t)f.sample];
        const int32_t ct = pi_.calculatedTempo;
        switch (v.setup) {
            case Setup::Initialize: {
                const int octave = -((v.note / 12) - 10);
                const int note = v.note % 12;
                if (octave > smp.endOct || octave < smp.startOct) {
                    v.setup = Setup::Nothing;
                    if (v.status == Status::Silence) return;
                    break;
                }
                if (v.status == Status::Silence) p.envVolume = 0;
                if (v.status != Status::Playing) p.envIndex = 0;
                p.mappedNote = (uint16_t)((kNote[note] * 54728) >> 15);
                const int temp1 = 1 << octave, temp2 = 1 << smp.startOct;  // startOct <= octave <= 10
                const int64_t size = (int64_t)smp.data.size();
                v.data = smp.data.data();
                v.dataSize = smp.data.size();
                v.start = (uint32_t)((temp1 - temp2) * smp.lenOct1);
                v.lenWords = (uint16_t)((smp.lenOct1 * temp1) / 2);
                if ((int64_t)v.start + v.lenWords * 2 > size) v.lenWords = (uint16_t)((size - v.start) / 2);
                if (smp.lenOct1 != smp.loopLenOct1) {
                    p.loop = true;
                    p.loopStart = (uint32_t)(v.start + (uint32_t)(smp.loopLenOct1 * temp1));
                    p.loopLenWords = (uint16_t)(((smp.lenOct1 - smp.loopLenOct1) * temp1) / 2);
                    if ((int64_t)p.loopStart + p.loopLenWords * 2 > size) p.loopLenWords = (uint16_t)((size - p.loopStart) / 2);
                } else {
                    p.loop = false;
                    p.loopStart = 0;
                    p.loopLenWords = 0;
                }
                p.vibIndex = 0;
                p.vibDelayCounter = (uint16_t)(((f.vibDelay << 15) / ct) / 2);
                v.setSample = SetSample::StartSample;
                ch_[ch].mute();
                break;
            }
            case Setup::ReleaseNote:
                p.envIndex = 3;
                break;
            case Setup::Mute:
                ch_[ch].mute();
                return;
            default:
                break;
        }
        if (p.vibDelayCounter == 0) p.vibIndex = (uint16_t)(p.vibIndex + (uint16_t)((wmul(f.vibSpeed, ct) >> 9) + 64));
        else p.vibDelayCounter--;
        uint16_t vib = (uint16_t)((p.vibIndex >> 7) + 128);
        if (vib & 0x100) vib ^= 0xff;
        vib ^= 0x80;
        p.vibValue = (int16_t)-(int8_t)(uint8_t)vib;
        envelope(p.envIndex, p.envVolume, f.envLevels, f.envRates);
        v.period = (uint16_t)(wmul(wadd(wsub(wmul(f.vibDepth, p.vibValue) >> 7, tune_ - 0x80), 0x1000), p.mappedNote) >> 0x13);
        const int32_t a = ((pi_.currentVolume + 1) * v.volume) / 256 + 1;
        v.finalVolume = (uint16_t)(wmul(wmul(a, f.volume) / 256, p.envVolume >> 16) >> 10);
    }

    // ------------------------------------------------------------ FormFormat.cs (IFF 8SVX)
    void setupForm(int ch, const FormData& f) {
        Voice& v = voice_[ch];
        SampledPlay& p = formPlay_[ch];
        switch (v.setup) {
            case Setup::Initialize: {
                const int octave = v.note / 12, note = v.note % 12;
                p.mappedNote = (uint16_t)((kNote[note] * 54728) >> 15);
                const int oif = (-(octave - 10)) - f.skipOctaves;  // at most 9
                if (oif < 0 || oif >= f.octaves) {
                    v.setup = Setup::Nothing;
                    if (v.status == Status::Silence) return;
                    break;
                }
                const uint32_t sampleLength = f.oneShot + f.repeat;
                const uint32_t startOffset = (sampleLength << oif) - sampleLength;
                const uint32_t oneShotLength = f.oneShot << oif, loopLength = f.repeat << oif;
                v.data = f.data.data();
                v.dataSize = f.data.size();
                v.start = startOffset;
                v.lenWords = (uint16_t)((oneShotLength != 0 ? oneShotLength : loopLength) / 2);
                if (loopLength != 0) {
                    p.loop = true;
                    p.loopStart = v.start + oneShotLength;
                    p.loopLenWords = (uint16_t)(loopLength / 2);
                } else {
                    p.loop = false;
                    p.loopStart = 0;
                    p.loopLenWords = 0;
                }
                v.setSample = SetSample::StartSample;
                ch_[ch].mute();
                p.volumeMultiply = 1;
                break;
            }
            case Setup::ReleaseNote:
                p.volumeMultiply = 0;
                break;
            case Setup::Mute:
                ch_[ch].mute();
                return;
            default:
                break;
        }
        v.period = (uint16_t)(wmul(0x1080 - tune_, p.mappedNote) >> 0x13);
        const int64_t a = ((pi_.currentVolume + 1) * v.volume) / 256 + 1;
        v.finalVolume = (uint16_t)(((a * (f.volume >> 1)) >> 0x11) * p.volumeMultiply);
    }
};
}  // namespace

bool isIffSmus(const uint8_t* d, size_t n) {
    return d && n >= 12 && std::memcmp(d, "FORM", 4) == 0 && std::memcmp(d + 8, "SMUS", 4) == 0;
}

std::unique_ptr<AmigaSynthPlayer> openIffSmus(const uint8_t* d, size_t n, const AmigaFileResolver& files,
                                              std::string* error) {
    std::string err;
    if (!isIffSmus(d, n) || n > (64u << 20)) {
        if (error) *error = "not an IFF SMUS score";
        return nullptr;
    }
    std::unique_ptr<IffSmus> s(new IffSmus);
    if (!s->load(d, n, files, err)) {
        if (error) *error = err;
        return nullptr;
    }
    if (error) error->clear();
    return s;
}
