// Little Amiga demo effects shown in the logo box of the main screen (click = next effect):
// copper bars, a sine scroller with copper colours and a water reflection, a starfield,
// glenz vectors and a plasma. Pure C++ pixel rendering (unit tested), drawn with sharp pixels.
#pragma once
#include <cstdint>
#include <vector>

class DemoFx {
public:
    static constexpr int W = 320, H = 60;
    enum Effect : int { CopperBars, SineScroller, Starfield, GlenzVectors, Plasma, kCount };

    DemoFx();
    void next() { effect_ = (effect_ + 1) % kCount; }
    int effect() const { return effect_; }
    void setEffect(int e) { effect_ = ((e % kCount) + kCount) % kCount; }
    void render(double seconds);  // draws the current effect at this time
    const uint32_t* pixels() const { return px_.data(); }
    static const char* scrollText();

private:
    void copperBars(double t);
    void sineScroller(double t);
    void starfield(double t);
    void glenz(double t);
    void plasma(double t);
    void put(int x, int y, uint32_t c) {
        if (x >= 0 && y >= 0 && x < W && y < H) px_[(size_t)y * W + x] = c;
    }
    void blend(int x, int y, uint32_t c, int alpha256);
    void bigText(int x, int y, const char* s, int scaleX, int scaleY, const uint32_t* rowColors, int clipBottom);
    void triangle(const float* a, const float* b, const float* c, uint32_t color, int alpha256);

    std::vector<uint32_t> px_;
    int effect_ = SineScroller;
    struct Star {
        float x, y, z;
    };
    std::vector<Star> stars_;
    uint32_t seed_ = 0x2468ACE;
    double lastStarT_ = -1;
    float rnd();
};
