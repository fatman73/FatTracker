#include "ui/demo_fx.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ui/pixel_canvas.h"

namespace {
constexpr double kPi = 3.14159265358979323846;

uint32_t rgbOf(int r, int g, int b) {
    auto c = [](int v) { return (uint32_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); };
    return 0xFF000000u | (c(r) << 16) | (c(g) << 8) | c(b);
}

// Rainbow like an Amiga copper list: hue 0..1 -> colour.
uint32_t copper(double h, double light = 1.0) {
    h -= std::floor(h);
    const double r = 0.5 + 0.5 * std::cos(2 * kPi * (h + 0.0));
    const double g = 0.5 + 0.5 * std::cos(2 * kPi * (h + 0.33));
    const double b = 0.5 + 0.5 * std::cos(2 * kPi * (h + 0.67));
    return rgbOf((int)(r * 255 * light), (int)(g * 255 * light), (int)(b * 255 * light));
}

const char kScroll[] =
    "      *** FATTRACKER ***   A PROTRACKER STYLE MUSIC EDITOR FOR WINDOWS - UP TO 32 CHANNELS AND 16 BIT "
    "SAMPLES, WITH THE LOOK AND THE WORKFLOW OF THE GOOD OLD AMIGA ...   IT LOADS MOD, XM, OCTAMED, DIGIBOOSTER, "
    "SYMPHONIE AND TFMX ...   SAVES XM, EXPORTS MOD AND WAV ...   PURE MODE TURNS YOUR PC INTO AN AMIGA 500: "
    "PAULA SOUND WITH THE LED FILTER, A FLOPPY DRIVE THAT CLICKS AND THE CLASSIC PROTRACKER SCREEN ...   "
    "COPPER BARS, STARS, GLENZ VECTORS AND PLASMA - CLICK HERE FOR THE NEXT EFFECT ...   CODED IN C++ WITH "
    "DIRECT2D AND WASAPI IN LESS THAN HALF A MEGABYTE ...   GREETINGS TO EVERY TRACKER MUSICIAN WHO EVER "
    "FILLED A PATTERN AT FOUR IN THE MORNING ...   KEEP THE AMIGA SPIRIT ALIVE!         ";
}  // namespace

DemoFx::DemoFx() : px_((size_t)W * H, 0xFF000000u) {}

const char* DemoFx::scrollText() { return kScroll; }

float DemoFx::rnd() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return (float)((seed_ >> 8) & 0xFFFF) / 65535.f;
}

void DemoFx::blend(int x, int y, uint32_t c, int a) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    uint32_t& d = px_[(size_t)y * W + x];
    auto mix = [&](int sh) {
        const int s = (c >> sh) & 255, t = (d >> sh) & 255;
        const int v = t + ((s - t) * a >> 8);
        return (uint32_t)(v < 0 ? 0 : (v > 255 ? 255 : v)) << sh;
    };
    d = 0xFF000000u | mix(16) | mix(8) | mix(0);
}

// 8x8 Amiga style glyphs enlarged; rowColors (per screen row) colour the text like a copper list.
void DemoFx::bigText(int x, int y, const char* s, int sx, int sy, const uint32_t* rowColors, int clipBottom) {
    for (; *s; ++s, x += 8 * sx) {
        if (x > W || x + 8 * sx < 0) continue;
        const uint8_t* g = amigaGlyph(*s);
        for (int r = 0; r < 7; ++r)
            for (int b = 0; b < 7; ++b)
                if (g[r] & (0x40 >> b))
                    for (int yy = 0; yy < sy; ++yy) {
                        const int py = y + r * sy + yy;
                        if (py >= clipBottom) continue;
                        for (int xx = 0; xx < sx; ++xx) put(x + b * sx + xx, py, rowColors[py < 0 ? 0 : (py >= H ? H - 1 : py)]);
                    }
    }
}

void DemoFx::render(double t) {
    switch (effect_) {
        case CopperBars: copperBars(t); break;
        case SineScroller: sineScroller(t); break;
        case Starfield: starfield(t); break;
        case GlenzVectors: glenz(t); break;
        default: plasma(t); break;
    }
}

// ---- 1. copper bars: shaded raster bars swinging on sines, the logo in chrome on top
void DemoFx::copperBars(double t) {
    std::fill(px_.begin(), px_.end(), rgbOf(0, 0, 24));
    for (int b = 0; b < 7; ++b) {
        const double centre = H / 2.0 + std::sin(t * 2.1 + b * 0.55) * (H / 2.0 - 6);
        for (int k = -6; k <= 6; ++k) {
            const int y = (int)centre + k;
            if (y < 0 || y >= H) continue;
            const double light = std::cos(k / 6.0 * kPi / 2);
            const uint32_t c = copper(b / 7.0 + t * 0.05, 0.25 + 0.75 * light);
            for (int x = 0; x < W; ++x) px_[(size_t)y * W + x] = c;
        }
    }
    uint32_t chrome[H];
    for (int y = 0; y < H; ++y) {
        const double v = 0.5 + 0.5 * std::cos((y - 18) / 18.0 * kPi);
        chrome[y] = rgbOf((int)(120 + 135 * v), (int)(120 + 135 * v), (int)(150 + 105 * v));
    }
    const char* logo = "FATTRACKER";
    const int w = (int)std::strlen(logo) * 8 * 3;
    const int x = (W - w) / 2 + (int)(std::sin(t * 1.3) * 20);
    bigText(x + 2, 11, logo, 3, 5, std::vector<uint32_t>(H, rgbOf(0, 0, 0)).data(), H);  // shadow
    bigText(x, 9, logo, 3, 5, chrome, H);
}

// ---- 2. sine scroller: copper coloured text on a sine, raster background, reflection in the water
void DemoFx::sineScroller(double t) {
    constexpr int kWater = 42;  // first row of the water
    for (int y = 0; y < kWater; ++y) {
        const uint32_t c = copper(y / 90.0 + t * 0.08, 0.18);
        for (int x = 0; x < W; ++x) px_[(size_t)y * W + x] = c;
    }
    uint32_t rows[H];
    for (int y = 0; y < H; ++y) rows[y] = copper(y / 40.0 - t * 0.6);
    // scroll position in pixels; each character column follows the sine on its own
    const int len = (int)std::strlen(kScroll);
    const double pos = t * 70.0;
    const int charW = 16;
    const int first = (int)(pos / charW);
    const int shift = (int)pos % charW;
    for (int i = 0; i <= W / charW + 1; ++i) {
        const char ch[2] = {kScroll[(first + i) % len], 0};
        const int x = i * charW - shift;
        for (int col = 0; col < charW; col += 2) {
            const int sx = x + col;
            const int yoff = (int)(std::sin((sx + pos * 0.5) * 0.035 + t * 2.0) * 9);
            // draw a 2 pixel wide slice of the glyph so the sine bends the letters
            const uint8_t* g = amigaGlyph(ch[0]);
            for (int r = 0; r < 7; ++r)
                if (g[r] & (0x40 >> (col / 2)))
                    for (int yy = 0; yy < 4; ++yy) {
                        const int py = 12 + yoff + r * 4 + yy;
                        if (py < kWater) {
                            put(sx, py, rows[py < 0 ? 0 : py]);
                            put(sx + 1, py, rows[py < 0 ? 0 : py]);
                        }
                    }
        }
    }
    // water: mirror of the rows above, wavy and tinted blue
    for (int y = kWater; y < H; ++y) {
        const int src = 2 * kWater - y - 1;
        const int wave = (int)(std::sin(y * 0.8 + t * 6.0) * 2.0);
        for (int x = 0; x < W; ++x) {
            const int sx = std::clamp(x + wave, 0, W - 1);
            const uint32_t s = px_[(size_t)std::clamp(src, 0, H - 1) * W + sx];
            const int r = ((s >> 16) & 255) / 3, g = ((s >> 8) & 255) / 2, b = (s & 255) / 2 + 70;
            px_[(size_t)y * W + x] = rgbOf(r, g, b);
        }
    }
}

// ---- 3. starfield: stars fly out of the centre of the screen
void DemoFx::starfield(double t) {
    std::fill(px_.begin(), px_.end(), 0xFF000000u);
    if (stars_.empty()) {
        stars_.resize(260);
        for (auto& s : stars_) s = {rnd() * 2 - 1, rnd() * 2 - 1, rnd()};
    }
    const double dt = lastStarT_ < 0 ? 0 : std::clamp(t - lastStarT_, 0.0, 0.1);
    lastStarT_ = t;
    for (auto& s : stars_) {
        s.z -= (float)(dt * 0.45);
        if (s.z <= 0.02f) s = {rnd() * 2 - 1, rnd() * 2 - 1, 1.f};
        const float px = W / 2 + s.x / s.z * W * 0.25f, py = H / 2 + s.y / s.z * H * 0.6f;
        if (px < 0 || py < 0 || px >= W || py >= H) {
            s = {rnd() * 2 - 1, rnd() * 2 - 1, 1.f};
            continue;
        }
        const int v = (int)(255 * (1.f - s.z));
        put((int)px, (int)py, rgbOf(v, v, v));
        if (s.z < 0.35f) put((int)px + 1, (int)py, rgbOf(v, v, v));  // close stars are bigger
    }
    wireLogo(t);
}

// ---- 4. glenz vectors: a see-through rotating object, back faces dark, front faces bright
void DemoFx::triangle(const float* a, const float* b, const float* c, uint32_t color, int alpha) {
    const float* v[3] = {a, b, c};
    std::sort(v, v + 3, [](const float* p, const float* q) { return p[1] < q[1]; });
    auto edgeX = [](const float* p, const float* q, float y) {
        return q[1] == p[1] ? p[0] : p[0] + (q[0] - p[0]) * (y - p[1]) / (q[1] - p[1]);
    };
    for (int y = std::max(0, (int)std::ceil(v[0][1])); y <= std::min(H - 1, (int)v[2][1]); ++y) {
        const float fy = (float)y;
        float x0 = edgeX(v[0], v[2], fy);
        float x1 = fy < v[1][1] ? edgeX(v[0], v[1], fy) : edgeX(v[1], v[2], fy);
        if (x0 > x1) std::swap(x0, x1);
        for (int x = std::max(0, (int)std::ceil(x0)); x <= std::min(W - 1, (int)x1); ++x) blend(x, y, color, alpha);
    }
}

void DemoFx::glenz(double t) {
    for (int y = 0; y < H; ++y) {
        const uint32_t c = rgbOf(0, 10 + y / 3, 40 + y);
        for (int x = 0; x < W; ++x) px_[(size_t)y * W + x] = c;
    }
    // octahedron with every face split in 4 (the classic glenz checker look)
    static const float kV[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const int kF[8][3] = {{0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4}, {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5}};
    const double ax = t * 0.9, ay = t * 1.3, az = t * 0.4;
    auto rot = [&](const float* p, float* o) {
        double x = p[0], y = p[1], z = p[2];
        double y1 = y * std::cos(ax) - z * std::sin(ax), z1 = y * std::sin(ax) + z * std::cos(ax);
        double x2 = x * std::cos(ay) + z1 * std::sin(ay), z2 = -x * std::sin(ay) + z1 * std::cos(ay);
        double x3 = x2 * std::cos(az) - y1 * std::sin(az), y3 = x2 * std::sin(az) + y1 * std::cos(az);
        const double s = 26.0 + 4.0 * std::sin(t * 2.0);
        const double persp = 3.2 / (3.2 + z2);
        o[0] = (float)(W / 2 + std::sin(t * 0.7) * 90 + x3 * s * persp * 1.6);
        o[1] = (float)(H / 2 + y3 * s * persp);
        o[2] = (float)z2;
    };
    float P[6][3];
    for (int i = 0; i < 6; ++i) rot(kV[i], P[i]);
    for (int pass = 0; pass < 2; ++pass)  // back faces first, then the front ones over them
        for (int f = 0; f < 8; ++f) {
            const float *a = P[kF[f][0]], *b = P[kF[f][1]], *c = P[kF[f][2]];
            const float cross = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
            const bool front = cross < 0;
            if ((pass == 1) != front) continue;
            float m[3][3];  // edge midpoints
            for (int k = 0; k < 3; ++k) {
                const float* p = P[kF[f][k]];
                const float* q = P[kF[f][(k + 1) % 3]];
                for (int d = 0; d < 3; ++d) m[k][d] = (p[d] + q[d]) / 2;
            }
            const uint32_t c1 = front ? rgbOf(90, 160, 255) : rgbOf(40, 30, 120);
            const uint32_t c2 = front ? rgbOf(255, 255, 255) : rgbOf(80, 60, 160);
            const int al = front ? 150 : 200;
            triangle(a, m[0], m[2], (f & 1) ? c1 : c2, al);
            triangle(m[0], b, m[1], (f & 1) ? c2 : c1, al);
            triangle(m[2], m[1], c, (f & 1) ? c2 : c1, al);
            triangle(m[0], m[1], m[2], (f & 1) ? c1 : c2, al);
        }
    flyingText(t);
}

// ---- 5. plasma
void DemoFx::plasma(double t) {
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const double v = std::sin(x * 0.045 + t) + std::sin(y * 0.11 - t * 1.3) +
                             std::sin((x + y) * 0.03 + t * 0.7) + std::sin(std::sqrt((x - 160.0) * (x - 160.0) + (y - 30.0) * (y - 30.0) * 9) * 0.05 - t * 2);
            px_[(size_t)y * W + x] = copper(v / 8.0 + t * 0.05, 0.85);
        }
    plasmaText(t);
}

// ---- extra demo parts: lines, the 3D wireframe logo, flying and cut-out texts

void DemoFx::line(int x0, int y0, int x1, int y1, uint32_t c) {
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 2000; ++guard) {
        put(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void DemoFx::wireLogo(double t) {
    static const char* const kLogo = "FATTRACKER";
    const int n = (int)std::strlen(kLogo);
    if (logoEdges_.empty()) {  // outline of every glyph pixel block, plus the corners for depth lines
        auto on = [&](int i, int c, int r) {
            if (c < 0 || c > 6 || r < 0 || r > 6) return false;
            return (amigaGlyph(kLogo[i])[r] & (0x40 >> c)) != 0;
        };
        for (int i = 0; i < n; ++i)
            for (int r = 0; r < 7; ++r)
                for (int c = 0; c < 7; ++c) {
                    if (!on(i, c, r)) continue;
                    const float x = (float)(i * 8 + c), y = (float)r;
                    const bool u = on(i, c, r - 1), d = on(i, c, r + 1), l = on(i, c - 1, r), rt = on(i, c + 1, r);
                    if (!u) logoEdges_.push_back({x, y, x + 1, y});
                    if (!d) logoEdges_.push_back({x, y + 1, x + 1, y + 1});
                    if (!l) logoEdges_.push_back({x, y, x, y + 1});
                    if (!rt) logoEdges_.push_back({x + 1, y, x + 1, y + 1});
                    if (!u && !l) logoCorners_.insert(logoCorners_.end(), {x, y});
                    if (!u && !rt) logoCorners_.insert(logoCorners_.end(), {x + 1, y});
                    if (!d && !l) logoCorners_.insert(logoCorners_.end(), {x, y + 1});
                    if (!d && !rt) logoCorners_.insert(logoCorners_.end(), {x + 1, y + 1});
                }
    }
    const float cx = n * 8 / 2.f, cy = 3.5f, depth = 1.6f;
    const double ay = std::sin(t * 0.55) * 1.1, ax = std::sin(t * 0.8) * 0.35, az = std::sin(t * 0.37) * 0.15;
    const double zoom = 1.0 + 0.12 * std::sin(t * 1.7);
    auto project = [&](float mx, float my, float mz, int& sx, int& sy) {
        const double x = mx - cx, y = (my - cy) * 1.25, z = mz;
        const double x1 = x * std::cos(az) - y * std::sin(az), y1 = x * std::sin(az) + y * std::cos(az);
        const double y2 = y1 * std::cos(ax) - z * std::sin(ax), z2 = y1 * std::sin(ax) + z * std::cos(ax);
        const double x3 = x1 * std::cos(ay) + z2 * std::sin(ay), z3 = -x1 * std::sin(ay) + z2 * std::cos(ay);
        const double persp = 70.0 / (70.0 + z3);
        sx = (int)std::lround(W / 2 + x3 * 3.4 * persp * zoom);
        sy = (int)std::lround(H / 2 + y2 * 5.2 * persp * zoom);
    };
    const uint32_t back = rgbOf(30, 70, 160), mid = rgbOf(60, 140, 220);
    const uint32_t front = copper(0.55 + 0.1 * std::sin(t * 0.9));
    for (const Edge& e : logoEdges_) {  // back outline
        int a0, b0, a1, b1;
        project(e.x0, e.y0, depth, a0, b0);
        project(e.x1, e.y1, depth, a1, b1);
        line(a0, b0, a1, b1, back);
    }
    for (size_t k = 0; k + 1 < logoCorners_.size(); k += 2) {  // depth lines
        int a0, b0, a1, b1;
        project(logoCorners_[k], logoCorners_[k + 1], -depth, a0, b0);
        project(logoCorners_[k], logoCorners_[k + 1], depth, a1, b1);
        line(a0, b0, a1, b1, mid);
    }
    for (const Edge& e : logoEdges_) {  // front outline
        int a0, b0, a1, b1;
        project(e.x0, e.y0, -depth, a0, b0);
        project(e.x1, e.y1, -depth, a1, b1);
        line(a0, b0, a1, b1, front);
    }
}

void DemoFx::flyingText(double t) {
    // every 12 seconds the logo flies from right to left for 6 seconds
    const double phase = std::fmod(t, 12.0);
    if (phase < 5.0 || phase > 11.0) return;
    const double p = (phase - 5.0) / 6.0;
    static const char* const kText = "FATTRACKER";
    const int n = (int)std::strlen(kText), sx = 3, sy = 3, cw = 8 * sx;
    const int x0 = (int)(W - p * (W + n * cw));
    for (int i = 0; i < n; ++i) {
        const int x = x0 + i * cw;
        if (x > W || x + cw < 0) continue;
        const int y = 14 + (int)(std::sin(t * 4.0 + i * 0.7) * 12);
        const uint8_t* g = amigaGlyph(kText[i]);
        for (int r = 0; r < 7; ++r)
            for (int b = 0; b < 7; ++b)
                if (g[r] & (0x40 >> b))
                    for (int yy = 0; yy < sy; ++yy)
                        for (int xx = 0; xx < sx; ++xx) {
                            const int px = x + b * sx + xx, py = y + r * sy + yy;
                            put(px + 2, py + 2, rgbOf(0, 0, 30));                   // shadow
                            put(px, py, copper(py / 30.0 + t * 0.7 + i * 0.05));  // copper coloured letters
                        }
    }
}

void DemoFx::plasmaText(double t) {
    // the logo pulses in for 6 of every 10 seconds, cut out of the plasma (inverted colours, dark rim)
    const double phase = std::fmod(t + 2.0, 10.0);
    if (phase > 6.0) return;
    static const char* const kText = "FATTRACKER";
    const int n = (int)std::strlen(kText);
    const double fade = std::min(1.0, std::min(phase, 6.0 - phase) / 0.8);
    const double scale = (0.85 + 0.12 * std::sin(t * 2.4)) * (0.4 + 0.6 * fade);
    const double sxs = scale * 3.6, sys = scale * 6.0;
    const double w = n * 8 * sxs, h = 7 * sys;
    const double ox = (W - w) / 2, oy = (H - h) / 2 + std::sin(t * 1.3) * 3;
    static std::vector<uint8_t> mask;
    mask.assign((size_t)W * H, 0);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const double gx = (x - ox) / sxs, gy = (y - oy) / sys;
            if (gx < 0 || gy < 0 || gx >= n * 8 || gy >= 7) continue;
            const int i = (int)gx / 8, c = (int)gx % 8, r = (int)gy;
            if (c < 7 && (amigaGlyph(kText[i])[r] & (0x40 >> c))) mask[(size_t)y * W + x] = 1;
        }
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            uint32_t& d = px_[(size_t)y * W + x];
            if (mask[(size_t)y * W + x]) {
                d = 0xFF000000u | (~d & 0xFFFFFF);  // inverted plasma inside the letters
            } else if ((x > 0 && mask[(size_t)y * W + x - 1]) || (x + 1 < W && mask[(size_t)y * W + x + 1]) ||
                       (y > 0 && mask[(size_t)(y - 1) * W + x]) || (y + 1 < H && mask[(size_t)(y + 1) * W + x])) {
                d = rgbOf(0, 0, 0);  // dark rim around the letters
            }
        }
}
