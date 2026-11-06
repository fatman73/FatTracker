// Generates res/FatTracker.icns (16 ... 1024 px) for the macOS app: the design of tools/make_icon.ps1
// (res/icon.ico) - grey bevelled panel, black screen, blue "FT", three green VU bars.
// usage: swift tools/make_icon_mac.swift   (from the repository root; needs iconutil)
import AppKit
import Foundation

func rgb(_ r: Int, _ g: Int, _ b: Int) -> CGColor {
    CGColor(srgbRed: CGFloat(r) / 255, green: CGFloat(g) / 255, blue: CGFloat(b) / 255, alpha: 1)
}

func iconPng(_ s: Int) -> Data {
    let cs = CGColorSpace(name: CGColorSpace.sRGB)!
    let ctx = CGContext(data: nil, width: s, height: s, bitsPerComponent: 8, bytesPerRow: 0, space: cs,
                        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    // top-left origin like GDI+
    ctx.translateBy(x: 0, y: CGFloat(s))
    ctx.scaleBy(x: 1, y: -1)
    let S = CGFloat(s)
    func fill(_ c: CGColor, _ x: CGFloat, _ y: CGFloat, _ w: CGFloat, _ h: CGFloat) {
        ctx.setFillColor(c)
        ctx.fill(CGRect(x: x, y: y, width: w, height: h))
    }
    let e = CGFloat(max(1, s / 16))
    fill(rgb(0xB8, 0xBC, 0xC4), 0, 0, S, S)
    fill(rgb(0xEE, 0xF1, 0xF5), 0, 0, S, e)
    fill(rgb(0xEE, 0xF1, 0xF5), 0, 0, e, S)
    fill(rgb(0x5A, 0x60, 0x6B), 0, S - e, S, e)
    fill(rgb(0x5A, 0x60, 0x6B), S - e, 0, e, S)
    let m = CGFloat(Int(S * 0.12))
    fill(rgb(4, 6, 10), m, m, S - 2 * m, S - 2 * m)
    let bw = CGFloat(max(1, Int(S * 0.09)))
    let base = S - m - CGFloat(max(1, Int(S * 0.06)))
    let heights: [CGFloat] = [0.30, 0.45, 0.22]
    for i in 0..<3 {
        let h = CGFloat(Int(S * heights[i]))
        let x = CGFloat(Int(S * 0.58)) + CGFloat(i) * (bw + CGFloat(max(1, Int(S * 0.03))))
        fill(rgb(0x2F, 0xE0, 0x6A), x, base - h, bw, h)
    }
    // "FT" in a bold monospaced face (Consolas Bold on Windows)
    let fontSize = max(5, S * 0.36) * 0.86  // SF Mono is wider than Consolas: the same width as on Windows
    let font = NSFont.monospacedSystemFont(ofSize: fontSize, weight: .bold)
    let attr = NSAttributedString(string: "FT", attributes: [
        .font: font, .foregroundColor: NSColor(cgColor: rgb(0x3F, 0x6F, 0xE8))!,
    ])
    let line = CTLineCreateWithAttributedString(attr)
    ctx.textMatrix = CGAffineTransform(scaleX: 1, y: -1)
    // GDI+ DrawString puts the top of the line box at the point; the baseline is one ascent lower
    ctx.textPosition = CGPoint(x: m * 0.9 + fontSize * 0.14, y: m * 0.8 + font.ascender + fontSize * 0.2)
    CTLineDraw(line, ctx)
    let img = ctx.makeImage()!
    let rep = NSBitmapImageRep(cgImage: img)
    return rep.representation(using: .png, properties: [:])!
}

let fm = FileManager.default
let set = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("FatTracker.iconset")
try? fm.removeItem(at: set)
try! fm.createDirectory(at: set, withIntermediateDirectories: true)
for base in [16, 32, 128, 256, 512] {
    try! iconPng(base).write(to: set.appendingPathComponent("icon_\(base)x\(base).png"))
    try! iconPng(base * 2).write(to: set.appendingPathComponent("icon_\(base)x\(base)@2x.png"))
}
let out = URL(fileURLWithPath: "res/FatTracker.icns")
let p = Process()
p.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
p.arguments = ["-c", "icns", set.path, "-o", out.path]
try! p.run()
p.waitUntilExit()
try? fm.removeItem(at: set)
print(p.terminationStatus == 0 ? "wrote \(out.path)" : "iconutil failed")
exit(p.terminationStatus)
