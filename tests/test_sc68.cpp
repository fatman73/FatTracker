// Atari ST SC68 (sc68 by Benjamin Gerard) and YMST (MYST by Nicolas Pomarede) containers: 68000 replay code and
// music data played on the emulated ST of core/sndh_player. Real files (absolute paths, skipped when absent),
// external replays, refused hardware, truncated / corrupted files and a scan of the whole modules folder.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "audio/engine.h"
#include "core/ay_player.h"
#include "core/sc68_player.h"
#include "core/sndh_player.h"
#include "test.h"

namespace {
namespace fs = std::filesystem;

// the Windows checkout's modules when they exist (worktrees have none), else <source>/modules
const fs::path kModules = fs::exists("C:/development/FatTracker/modules") ? fs::path("C:/development/FatTracker/modules")
                                                                          : fs::path(FT_SOURCE_DIR) / "modules";
const fs::path kWarhawk = kModules / "Warhawk";
const fs::path kSt = kModules / "AtariST";

std::vector<uint8_t> fileBytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

float loudest(ChipPlayer& p, double seconds) {
    std::vector<float> buf(2 * 4800);
    float pk = 0;
    for (int left = (int)(seconds * 48000); left > 0; left -= 4800) {
        p.render(buf.data(), 4800, 1.f);
        for (float v : buf) pk = std::max(pk, std::fabs(v));
    }
    return pk;
}

// external replays: the AtariST folder and its Replay subfolder (names as asked, the file system ignores case)
StFileResolver stFiles() {
    return [](const std::string& name, std::vector<uint8_t>& out) {
        for (const fs::path& dir : {kSt, kSt / "Replay"}) {
            const fs::path p = dir / name;
            std::error_code ec;
            if (fs::is_regular_file(p, ec)) {
                out = fileBytes(p);
                return true;
            }
        }
        return false;
    };
}

double secondsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

// a minimal SC68 file around `code` (one track, YM, built in replay)
std::vector<uint8_t> tinySc68(const std::vector<uint8_t>& code, uint32_t flags = 1) {
    std::vector<uint8_t> f;
    const char id[] = "SC68 Music-file / (c) (BeN)jamin Gerard / SasHipA-Dev  ";
    f.insert(f.end(), id, id + sizeof id);  // with its 0
    auto chunk = [&](const char* tag, const void* body, uint32_t n) {
        f.insert(f.end(), tag, tag + 4);
        for (int i = 0; i < 4; ++i) f.push_back((uint8_t)(n >> (8 * i)));
        f.insert(f.end(), (const uint8_t*)body, (const uint8_t*)body + n);
    };
    auto value = [&](const char* tag, uint32_t v) {
        uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
        chunk(tag, b, 4);
    };
    const size_t base = f.size();
    chunk("SC68", nullptr, 0);
    chunk("SCFN", "Tiny\0", 5);
    chunk("SCMU", nullptr, 0);
    value("SCD0", 1);
    value("SCTI", 10);
    value("SCTY", flags);
    chunk("SCDA", code.data(), (uint32_t)code.size());
    chunk("SCEF", nullptr, 0);
    const uint32_t total = (uint32_t)(f.size() - (base - 0));  // the base chunk holds the size after the id
    for (int i = 0; i < 4; ++i) f[base + 4 + i] = (uint8_t)(total >> (8 * i));
    return f;
}

// 68000 code at the load address: +0 init (D0 kept), +4 RTS, +8 play: a 440 Hz tone on channel A
std::vector<uint8_t> toneCode() {
    std::vector<uint8_t> b;
    auto w = [&](uint16_t v) { b.push_back((uint8_t)(v >> 8)), b.push_back((uint8_t)v); };
    w(0x6000), w(0x000A);  // BRA.W init (+$0C)
    w(0x4E75), w(0x4E75);  // exit: RTS
    w(0x6000), w(0x0004);  // +8: BRA.W play (+$0E)
    w(0x4E75);             // +$0C init: RTS
    // +$0E play: LEA $FFFF8800.W,A0 and the tone registers
    w(0x41F8), w(0x8800);
    const uint8_t regs[][2] = {{0, 0x1C}, {1, 0x01}, {7, 0x3E}, {8, 15}, {9, 0}, {10, 0}};
    for (const auto& r : regs) w(0x10BC), w(r[0]), w(0x117C), w(r[1]), w(0x0002);
    w(0x4E75);
    return b;
}
}  // namespace

// ---------------------------------------------------------------- detection
TEST(sc68_detect) {
    const auto f = tinySc68(toneCode());
    CHECK(Sc68Player::detect(f.data(), f.size()));
    CHECK(!Sc68Player::detectYmst(f.data(), f.size()));
    CHECK(!Sc68Player::detect(f.data(), 40));  // cut inside the id string
    std::vector<uint8_t> bad = f;
    bad[5] = 'X';
    CHECK(!Sc68Player::detect(bad.data(), bad.size()));
    CHECK(!SndhPlayer::detect(f.data(), f.size()));
    CHECK(!AyPlayer::detect(f.data(), f.size()));
    const uint8_t ymst[] = "YMST\x80\x00YM\0\0\0\0\0\0\0\0\0\0\0\0";
    CHECK(Sc68Player::detectYmst(ymst, sizeof ymst));
    CHECK(!Sc68Player::detect(ymst, sizeof ymst));
    CHECK(!AyPlayer::detect(ymst, sizeof ymst));
}

TEST(sc68_tiny_tune_plays) {
    const auto f = tinySc68(toneCode());
    Sc68Player p;
    p.setRate(48000);
    std::string err;
    CHECK(p.load(f.data(), f.size(), {}, -1, &err));
    CHECK_EQ(p.subsongs(), 1);
    CHECK(loudest(p, 0.5) > 0.05f);
    CHECK_EQ((int)p.ymRegister(8), 15);
    CHECK_NEAR(p.seconds(), 10.0, 0.01);
    CHECK(std::strstr(p.format(), "SC68") != nullptr);
    CHECK(std::strcmp(p.title(), "Tiny") == 0);
}

TEST(sc68_refuses_amiga_and_dma_only_tracks) {
    for (uint32_t flags : {4u, 2u}) {  // Paula, STE DMA without the YM
        const auto f = tinySc68(toneCode(), flags);
        Sc68Player p;
        std::string err;
        CHECK(!p.load(f.data(), f.size(), {}, -1, &err));
        CHECK(!err.empty());
        if (flags == 4) CHECK(err.find("AMIGA") != std::string::npos);
    }
}

TEST(sc68_hostile_chunks_are_refused) {
    const auto good = tinySc68(toneCode());
    // every cut of the file: refused or harmless, never a hang
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t cut = 0; cut < good.size(); ++cut) {
        Sc68Player p;
        p.setRate(48000);
        if (p.load(good.data(), cut)) loudest(p, 0.02);
    }
    CHECK(secondsSince(t0) < 20.0);
    // a chunk size beyond the end
    std::vector<uint8_t> big = good;
    const size_t da = std::string((const char*)big.data(), big.size()).find("SCDA");
    CHECK(da != std::string::npos);
    big[da + 4] = big[da + 5] = big[da + 6] = big[da + 7] = 0xFF;
    Sc68Player p;
    CHECK(!p.load(big.data(), big.size()));
    // a load address inside the machine's own memory or past the top
    for (uint32_t at : {0u + 0x400, 0x3FFFF0u, 0xFFFFFFFEu}) {
        std::vector<uint8_t> f = good;
        // insert an SCAT chunk right after SCMU
        const size_t mu = std::string((const char*)f.data(), f.size()).find("SCMU") + 8;
        const uint8_t chunk[12] = {'S', 'C', 'A', 'T', 4, 0, 0, 0, (uint8_t)at, (uint8_t)(at >> 8), (uint8_t)(at >> 16), (uint8_t)(at >> 24)};
        f.insert(f.begin() + (ptrdiff_t)mu, chunk, chunk + 12);
        const size_t base = 56;
        const uint32_t total = (uint32_t)(f.size() - base);
        for (int i = 0; i < 4; ++i) f[base + 4 + i] = (uint8_t)(total >> (8 * i));
        Sc68Player q;
        q.setRate(48000);
        CHECK(!q.load(f.data(), f.size()));
    }
}

// ---------------------------------------------------------------- real SC68 files
TEST(sc68_real_files_play) {
    if (!fs::exists(kSt) && !fs::exists(kWarhawk)) {
        std::printf("SKIP sc68_real_files_play (no modules folders)\n");
        return;
    }
    struct Known {
        fs::path file;
        int songs;
        bool external;  // needs a replay from the Replay folder
    };
    const Known known[] = {
        {kSt / "Rob Hubbard - War Hawk.sc68", 1, false},
        {kSt / "Count Zero - Awesome.sc68", 13, false},
        {kSt / "David Whittaker - Bad Company (ST).sc68", 10, false},
        {kSt / "Big Alec - Bankok Knights Loader.sc68", 1, false},
        {kSt / "Tao - Androids.sc68", 1, true},
        {kWarhawk / "Atari ST SC68 - Tao - warhawk (hpn version).sc68", 1, true},
    };
    int n = 0;
    for (const Known& k : known) {
        if (!fs::exists(k.file)) continue;
        const auto d = fileBytes(k.file);
        CHECK(Sc68Player::detect(d.data(), d.size()));
        CHECK(!Sc68Player::detectYmst(d.data(), d.size()));
        if (k.external) {  // without the replay: refused, the message names it
            Sc68Player q;
            std::string err;
            CHECK(!q.load(d.data(), d.size(), {}, -1, &err));
            CHECK(err.find("REPLAY") != std::string::npos);
            std::printf("  %s without its replay: %s\n", k.file.filename().string().c_str(), err.c_str());
        }
        Sc68Player p;
        p.setRate(48000);
        std::string err;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = p.load(d.data(), d.size(), stFiles(), -1, &err);
        CHECK(ok);
        if (!ok) {
            std::printf("  FAIL %s: %s\n", k.file.filename().string().c_str(), err.c_str());
            continue;
        }
        CHECK_EQ(p.subsongs(), k.songs);
        const float pk = loudest(p, 3.0);
        CHECK(pk > 0.02f);
        CHECK(!p.crashed());
        std::printf("  %s: \"%s\" by %s, %s, %d song(s), %d Hz, %.0f s, peak %.2f (%.2f s)\n",
                    k.file.filename().string().c_str(), p.title(), p.composer(), p.format(), p.subsongs(),
                    p.playRate(), p.seconds(), pk, secondsSince(t0));
        if (p.subsongs() > 1) {  // the last sub-song too
            CHECK(p.selectSubsong(p.subsongs() - 1));
            CHECK_EQ(p.subsong(), p.subsongs() - 1);
            loudest(p, 1.0);
            CHECK(!p.crashed());
        }
        ++n;
    }
    // the Amiga tune of the Warhawk folder: refused with the reason
    const fs::path amiga = kWarhawk / "Atari ST SC68 - _unknown - warhawk.sc68";
    if (fs::exists(amiga)) {
        const auto d = fileBytes(amiga);
        CHECK(Sc68Player::detect(d.data(), d.size()));
        Sc68Player p;
        std::string err;
        CHECK(!p.load(d.data(), d.size(), stFiles(), -1, &err));
        CHECK(err.find("AMIGA") != std::string::npos);
        std::printf("  %s: %s\n", amiga.filename().string().c_str(), err.c_str());
    }
    if (!n) std::printf("SKIP sc68_real_files_play (no SC68 files)\n");
}

// ---------------------------------------------------------------- YMST (MYST)
TEST(ymst_real_files_play) {
    if (!fs::exists(kSt) && !fs::exists(kWarhawk)) {
        std::printf("SKIP ymst_real_files_play (no modules folders)\n");
        return;
    }
    struct Known {
        fs::path file;
        int songs;
        bool external;  // music data for a replay file (YM.<name>_replay) next to it
    };
    const Known known[] = {
        {kWarhawk / "Atari ST YMST - Rob Hubbard - warhawk.ymst", 1, false},
        {kWarhawk / "Atari ST YMST - Jochen Hippel - warhawkremix.ymst", 1, false},
        {kWarhawk / "Atari ST YMST - Jochen Hippel - warhawk.ymst", 1, true},
        {kSt / "Count Zero - Awesome 1.ymst", 9, false},
        {kSt / "Unknown - I Ball.ymst", 1, false},
    };
    int n = 0;
    for (const Known& k : known) {
        if (!fs::exists(k.file)) continue;
        const auto d = fileBytes(k.file);
        CHECK(Sc68Player::detectYmst(d.data(), d.size()));
        CHECK(!Sc68Player::detect(d.data(), d.size()));
        CHECK(!AyPlayer::detect(d.data(), d.size()));
        if (k.external) {
            Sc68Player q;
            std::string err;
            CHECK(!q.load(d.data(), d.size(), {}, -1, &err));
            CHECK(err.find("REPLAY") != std::string::npos);
            std::printf("  %s without its replay: %s\n", k.file.filename().string().c_str(), err.c_str());
        }
        Sc68Player p;
        p.setRate(48000);
        std::string err;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = p.load(d.data(), d.size(), stFiles(), -1, &err);
        CHECK(ok);
        if (!ok) {
            std::printf("  FAIL %s: %s\n", k.file.filename().string().c_str(), err.c_str());
            continue;
        }
        CHECK_EQ(p.subsongs(), k.songs);
        CHECK(std::strstr(p.format(), "YMST") != nullptr);
        const float pk = loudest(p, 3.0);
        CHECK(pk > 0.02f);
        CHECK(!p.crashed());
        std::printf("  %s: \"%s\" by %s, %s, %d song(s), %.0f s, peak %.2f (%.2f s)\n", k.file.filename().string().c_str(),
                    p.title(), p.composer(), p.format(), p.subsongs(), p.seconds(), pk, secondsSince(t0));
        if (p.subsongs() > 1) {
            CHECK(p.selectSubsong(p.subsongs() - 1));
            loudest(p, 1.0);
            CHECK(!p.crashed());
        }
        ++n;
    }
    // a replay file alone is not a tune
    const fs::path replay = kSt / "ym.big_replay";
    if (fs::exists(replay)) {
        const auto d = fileBytes(replay);
        CHECK(Sc68Player::detectYmst(d.data(), d.size()));
        Sc68Player p;
        std::string err;
        CHECK(!p.load(d.data(), d.size(), stFiles(), -1, &err));
        CHECK(!err.empty());
    }
    if (!n) std::printf("SKIP ymst_real_files_play (no YMST files)\n");
}

// ---------------------------------------------------------------- broken real files: refused or safe, in time
TEST(sc68_ymst_truncated_and_corrupted_files) {
    std::vector<fs::path> files;
    for (const fs::path& dir : {kSt, kWarhawk}) {
        std::error_code ec;
        if (!fs::exists(dir, ec)) continue;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            const std::string ext = e.path().extension().string();
            if (ext == ".sc68" || ext == ".ymst") files.push_back(e.path());
        }
    }
    if (files.empty()) {
        std::printf("SKIP sc68_ymst_truncated_and_corrupted_files (no files)\n");
        return;
    }
    uint32_t seed = 12345;
    auto rnd = [&]() { return seed = seed * 1103515245u + 12345u, seed >> 8; };
    const auto t0 = std::chrono::steady_clock::now();
    int refused = 0, played = 0;
    for (const fs::path& f : files) {
        const auto d = fileBytes(f);
        std::vector<std::vector<uint8_t>> variants;
        for (size_t cut : {d.size() - 1, d.size() * 3 / 4, d.size() / 2, (size_t)200, (size_t)64, (size_t)8})
            if (cut < d.size()) variants.emplace_back(d.begin(), d.begin() + (ptrdiff_t)cut);
        for (int k = 0; k < 6; ++k) {  // random bytes of the header area and of the whole file changed
            std::vector<uint8_t> c = d;
            for (int i = 0; i < 8; ++i) {
                const size_t at = (k < 3 ? rnd() % std::min<size_t>(c.size(), 0x200) : rnd() % c.size());
                c[at] = (uint8_t)rnd();
            }
            variants.push_back(std::move(c));
        }
        for (const auto& v : variants) {
            const auto t1 = std::chrono::steady_clock::now();
            Sc68Player p;
            p.setRate(48000);
            if (p.load(v.data(), v.size(), stFiles())) {
                loudest(p, 0.5);
                ++played;
            } else {
                ++refused;
            }
            CHECK(secondsSince(t1) < 8.0);  // init budget plus half a second of music
        }
    }
    std::printf("  %zu files: %d broken variants refused, %d played safely (%.1f s)\n", files.size(), refused, played,
                secondsSince(t0));
}

// ---------------------------------------------------------------- nothing else is taken for SC68 / YMST
TEST(sc68_ymst_scan_detects_only_their_files) {
    std::error_code ec;
    if (!fs::exists(kModules, ec)) {
        std::printf("SKIP sc68_ymst_scan_detects_only_their_files (no modules folder)\n");
        return;
    }
    int sc68 = 0, ymst = 0, files = 0, wrong = 0;
    for (auto it = fs::recursive_directory_iterator(kModules, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const uintmax_t size = it->file_size(ec);
        if (ec || size > (8u << 20)) continue;
        const auto d = fileBytes(it->path());
        ++files;
        std::string name = it->path().filename().string();
        for (char& c : name) c = (char)std::tolower((unsigned char)c);
        const bool isSc68 = name.size() > 5 && name.compare(name.size() - 5, 5, ".sc68") == 0;
        const bool isYmst = (name.size() > 5 && name.compare(name.size() - 5, 5, ".ymst") == 0) ||
                            (name.rfind("ym.", 0) == 0 && name.find("replay") != std::string::npos);
        const bool a = Sc68Player::detect(d.data(), d.size()), b = Sc68Player::detectYmst(d.data(), d.size());
        sc68 += a, ymst += b;
        if (a != isSc68 || b != isYmst) {
            std::printf("  wrong: %s (sc68 %d, ymst %d)\n", it->path().string().c_str(), a, b);
            ++wrong;
        }
        if (isSc68 || isYmst) {  // and the other players leave them alone
            CHECK(!SndhPlayer::detect(d.data(), d.size()));
            CHECK(!AyPlayer::detect(d.data(), d.size()));
        }
    }
    CHECK_EQ(wrong, 0);
    std::printf("  %d files scanned: %d SC68, %d YMST\n", files, sc68, ymst);
}

// ---------------------------------------------------------------- the engine
TEST(sc68_engine_load) {
    Engine e;
    const auto f = tinySc68(toneCode());
    CHECK(e.loadSc68(f.data(), f.size(), {}, "TINY", false));
    CHECK(e.tfmx());
    CHECK(std::strstr(e.tfmxFormat(), "SC68") != nullptr);
    CHECK_EQ(e.chipSubsongs(), 1);
    std::string err;
    const auto amiga = tinySc68(toneCode(), 4);
    CHECK(!e.loadSc68(amiga.data(), amiga.size(), {}, "AMIGA", false, &err));
    CHECK(err.find("AMIGA") != std::string::npos);
}
