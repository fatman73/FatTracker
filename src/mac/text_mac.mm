// Text and Font on Core Text (macOS). Bahnschrift and Cascadia Mono do not exist on the Mac:
//   Ui   - DIN Alternate Bold (a DIN face like Bahnschrift), else the system font semibold
//   Mono - the system monospaced font (SF Mono) semibold, else Menlo Bold
// Text is drawn with CTLine (font fallback for the arrow codes), the pattern glyphs with CTFontDrawGlyphs
// at whole device pixels, grouped by color.
#include "ui/font.h"
#include "ui/text.h"

#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>

#include <cmath>
#include <vector>

namespace {
CGColorSpaceRef srgb() {
    static CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    return cs;
}

void setFill(CGContextRef ctx, Color c) {
    const CGFloat comp[4] = {c.r, c.g, c.b, c.a};
    CGContextSetFillColorSpace(ctx, srgb());
    CGContextSetFillColor(ctx, comp);
}

CTFontRef createBase(FontRole role) {
    NSFont* f = nil;
    if (role == FontRole::Ui) {
        f = [NSFont fontWithName:@"DINAlternate-Bold" size:100];
        if (!f) f = [NSFont systemFontOfSize:100 weight:NSFontWeightSemibold];
    } else {
        f = [NSFont monospacedSystemFontOfSize:100 weight:NSFontWeightSemibold];
        if (!f) f = [NSFont fontWithName:@"Menlo-Bold" size:100];
    }
    return f ? (CTFontRef)CFBridgingRetain(f) : CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 100, nullptr);
}

// UTF-16 of a UI string: codes 1..6 become the arrow characters
CFStringRef makeString(const char* s) {
    wchar_t w[256];
    const int n = Text::widen(s, w, 256);
    UniChar u[256];
    for (int i = 0; i < n; ++i) u[i] = (UniChar)w[i];
    return CFStringCreateWithCharacters(nullptr, u, n);
}

// A line in `font` whose glyphs take the context's fill color
CTLineRef makeLine(CTFontRef font, const char* s) {
    CFStringRef str = makeString(s);
    if (!str) return nullptr;
    const void* keys[2] = {kCTFontAttributeName, kCTForegroundColorFromContextAttributeName};
    const void* vals[2] = {font, kCFBooleanTrue};
    CFDictionaryRef attrs = CFDictionaryCreate(nullptr, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef as = CFAttributedStringCreate(nullptr, str, attrs);
    CTLineRef line = as ? CTLineCreateWithAttributedString(as) : nullptr;
    if (as) CFRelease(as);
    CFRelease(attrs);
    CFRelease(str);
    return line;
}
}  // namespace

// ---------------------------------------------------------------- Text

Text::~Text() {
    for (Entry& e : cache_)
        if (e.font) CFRelease((CTFontRef)e.font);
    for (const void* b : base_)
        if (b) CFRelease((CTFontRef)b);
}

bool Text::init() {
    base_[0] = createBase(FontRole::Ui);
    base_[1] = createBase(FontRole::Mono);
    // advance of a monospace digit relative to the em size (like the Windows measure of "0000000000")
    CTFontRef mono = (CTFontRef)base_[1];
    if (mono) {
        UniChar zero = '0';
        CGGlyph g = 0;
        if (CTFontGetGlyphsForCharacters(mono, &zero, &g, 1)) {
            CGSize adv;
            CTFontGetAdvancesForGlyphs(mono, kCTFontOrientationHorizontal, &g, &adv, 1);
            if (adv.width > 0) monoAdvance_ = (float)adv.width / 100.f;
        }
    }
    return base_[0] && base_[1];
}

const void* Text::font(FontRole role, float size) {
    const int s10 = (int)std::lround(size * 10.f);
    for (Entry& e : cache_)
        if (e.font && e.role == role && e.size10 == s10) return e.font;
    CTFontRef base = (CTFontRef)base_[role == FontRole::Mono ? 1 : 0];
    if (!base) return nullptr;
    CTFontRef f = CTFontCreateCopyWithAttributes(base, s10 / 10.0, nullptr, nullptr);
    Entry& slot = cache_[cacheNext_];
    cacheNext_ = (cacheNext_ + 1) % 48;
    if (slot.font) CFRelease((CTFontRef)slot.font);
    slot = {role, s10, f};
    return f;
}

int Text::widen(const char* s, wchar_t* out, int cap) {
    static const wchar_t arrows[7] = {0, 0x25B2, 0x25BC, 0x25C0, 0x25B6, 0x25B2, 0x25BC};
    int n = 0;
    for (; s[n] && n < cap - 1; ++n) {
        unsigned char c = (unsigned char)s[n];
        out[n] = (c >= 1 && c <= 6) ? arrows[c] : (wchar_t)c;
    }
    out[n] = 0;
    return n;
}

void Text::draw(Gfx& g, RectF r, const char* s, float size, Color c, Align a, FontRole role) {
    CGContextRef ctx = g.cg();
    if (!ctx || !*s) return;
    CTFontRef f = (CTFontRef)font(role, size);
    if (!f) return;
    CTLineRef line = makeLine(f, s);
    if (!line) return;
    CGFloat ascent = 0, descent = 0, leading = 0;
    const double width = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
    const float pad = a == Align::Center ? 0.f : size * 0.45f;
    const RectF box = {r.x + pad, r.y, r.w - 2 * pad, r.h};
    double x = box.x;
    if (a == Align::Center) x = box.x + (box.w - width) * 0.5;
    else if (a == Align::Right) x = box.x + box.w - width;
    const double baseline = r.y + (r.h - (ascent + descent)) * 0.5 + ascent;  // the line centered vertically
    CGContextSaveGState(ctx);
    CGContextClipToRect(ctx, CGRectMake(box.x, box.y, box.w, box.h));
    setFill(ctx, c);
    CGContextSetTextMatrix(ctx, CGAffineTransformMakeScale(1, -1));  // the context is y-down
    CGContextSetTextPosition(ctx, x, baseline);
    CTLineDraw(line, ctx);
    CGContextRestoreGState(ctx);
    CFRelease(line);
}

float Text::measure(const char* s, float size, FontRole role) {
    CTFontRef f = (CTFontRef)font(role, size);
    if (!f || !*s) return 0;
    CTLineRef line = makeLine(f, s);
    if (!line) return 0;
    const double w = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
    CFRelease(line);
    return (float)w;
}

// ---------------------------------------------------------------- Font (pattern glyphs)

Font::~Font() {}

void Font::invalidate() {}

void Font::drawGlyphs(Gfx& g, const Glyph* glyphs, int count, float em) {
    CGContextRef ctx = g.cg();
    const Viewport& vp = g.viewport();
    if (!ctx || !text_ || vp.scale <= 0 || count <= 0) return;
    int emPx = (int)std::lround(em * vp.scale);
    if (emPx < 6) emPx = 6;
    CTFontRef f = (CTFontRef)text_->font(FontRole::Mono, (float)emPx);
    if (!f) return;
    // glyph ids for codes 1..127 at this size (arrows 1..6 may be missing in the font: skipped)
    UniChar chars[128];
    CGGlyph ids[128] = {};
    for (int i = 0; i < 128; ++i) {
        char s[2] = {(char)(i ? i : ' '), 0};
        wchar_t w[2];
        Text::widen(s, w, 2);
        chars[i] = (UniChar)w[0];
    }
    CTFontGetGlyphsForCharacters(f, chars, ids, 128);
    const CGFloat ascent = CTFontGetAscent(f);
    CGContextSaveGState(ctx);
    // device pixels, top-left origin (the viewport scale undone, the flip kept)
    CGContextScaleCTM(ctx, 1.0 / vp.scale, 1.0 / vp.scale);
    CGContextTranslateCTM(ctx, -vp.offX, -vp.offY);
    CGContextSetTextMatrix(ctx, CGAffineTransformMakeScale(1, -1));
    std::vector<CGGlyph> gl;
    std::vector<CGPoint> pos;
    gl.reserve(count);
    pos.reserve(count);
    std::vector<char> done((size_t)count, 0);
    for (int i = 0; i < count; ++i) {
        if (done[i]) continue;
        const Color c = glyphs[i].c;
        gl.clear();
        pos.clear();
        for (int j = i; j < count; ++j) {  // every glyph of this color in one call
            if (done[j]) continue;
            const Glyph& q = glyphs[j];
            if (q.c.r != c.r || q.c.g != c.g || q.c.b != c.b || q.c.a != c.a) continue;
            done[j] = 1;
            const unsigned char ch = (unsigned char)q.ch;
            if (ch == ' ' || ch == 0 || ch >= 128 || !ids[ch]) continue;
            const float dx = std::floor(vp.offX + q.x * vp.scale + 0.5f);
            const float dy = std::floor(vp.offY + q.y * vp.scale + 0.5f);
            gl.push_back(ids[ch]);
            // positions are in text space: the flipping text matrix negates y
            pos.push_back(CGPointMake(dx, -std::round(dy + ascent)));
        }
        if (gl.empty()) continue;
        setFill(ctx, c);
        CTFontDrawGlyphs(f, gl.data(), pos.data(), gl.size(), ctx);
    }
    CGContextRestoreGState(ctx);
}
