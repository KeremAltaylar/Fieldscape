// Design System/Tokens.md for the iOS app (rulebook C-1: no value chosen anywhere else). Colours are
// the OKLCH ladder converted with the same OKLab matrices the web uses (index.html oklchToHex), so
// they are the web's values to the digit. Sizes: the token rem scale at 16 pt per rem.
import SwiftUI

enum T {
    static func hex(_ v: UInt32) -> Color {
        Color(red: Double((v >> 16) & 0xff) / 255, green: Double((v >> 8) & 0xff) / 255, blue: Double(v & 0xff) / 255)
    }
    // surfaces, hue 162
    static let sunk = hex(0x0d1310), ground = hex(0x19211d), panel = hex(0x222b27), raised = hex(0x2e3833), hairline = hex(0x36403b)
    // text, hue 150
    static let ink = hex(0xe3e7e4), dim = hex(0xb3b9b4), faint = hex(0x9ca49d)
    // the one accent (level meters), and lamp (points that carry a recording, as on the web map)
    static let accent = hex(0xbbceb5), lamp = hex(0xbae6b1)

    // type: display / body / mono (Cormorant Garamond, Newsreader, Courier Prime; bundled, OFL)
    static let xs: CGFloat = 10.88, sm: CGFloat = 12.48, base: CGFloat = 17, md: CGFloat = 18.4, lg: CGFloat = 24
    static func display(_ size: CGFloat, _ weight: Font.Weight = .semibold) -> Font { .custom("Cormorant Garamond", size: size).weight(weight) }
    static func body(_ size: CGFloat = base, _ weight: Font.Weight = .regular) -> Font { .custom("Newsreader", size: size).weight(weight) }
    static func mono(_ size: CGFloat = sm) -> Font { .custom("Courier Prime", size: size).monospacedDigit() }
    static let trackMeta: CGFloat = 0.18, trackEyebrow: CGFloat = 0.28      // em

    // spacing
    static let s1: CGFloat = 4, s2: CGFloat = 8, s3: CGFloat = 12, s4: CGFloat = 16, s5: CGFloat = 24, s6: CGFloat = 32, s8: CGFloat = 48
    static let target: CGFloat = 44                                        // M-4: touch targets

    // data scale (Tokens.md): a chord root's colour, hsl(pc x 30, 42%, 62%) - index.html rootColour
    static func rootHex(_ pc: Int) -> String {
        let h = Double(((pc % 12) + 12) % 12 * 30), s = 0.42, l = 0.62
        let c = (1 - abs(2 * l - 1)) * s, x = c * (1 - abs((h / 60).truncatingRemainder(dividingBy: 2) - 1)), m = l - c / 2
        let (r, g, b): (Double, Double, Double) = h < 60 ? (c, x, 0) : h < 120 ? (x, c, 0) : h < 180 ? (0, c, x) : h < 240 ? (0, x, c) : h < 300 ? (x, 0, c) : (c, 0, x)
        return String(format: "#%02x%02x%02x", Int(((r + m) * 255).rounded()), Int(((g + m) * 255).rounded()), Int(((b + m) * 255).rounded()))
    }
    static func root(_ pc: Int) -> Color { hex(UInt32(rootHex(pc).dropFirst(), radix: 16) ?? 0) }
}
