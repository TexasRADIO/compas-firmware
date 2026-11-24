// Render fixed lettering from local Taylor Rushing fonts without embedding the fonts.
import CoreGraphics
import CoreText
import Foundation

let args = CommandLine.arguments
guard args.count == 4,
      let source = CGDataProvider(url: URL(fileURLWithPath: args[1]) as CFURL),
      let face = CGFont(source) else {
    fatalError("usage: swift wordmark.swift path/to/font.otf TEXT svg|lcd|subtitle-lcd")
}
let font = CTFontCreateWithGraphicsFont(face, 100, nil, nil)
let title = args[2]
let path = CGMutablePath()
var cursor: CGFloat = 0
for scalar in title.utf16 {
    var character: UInt16 = scalar == 0x00c1 ? 65 : scalar
    var glyph: CGGlyph = 0
    guard CTFontGetGlyphsForCharacters(font, &character, &glyph, 1) else {
        fatalError("Missing glyph for UTF-16 code unit \(scalar)")
    }
    var shift = CGAffineTransform(translationX: cursor, y: 0)
    if let outline = CTFontCreatePathForGlyph(font, glyph, &shift) {
        path.addPath(outline)
        if scalar == 0x00c1 {
            let box = outline.boundingBoxOfPath
            let x = box.midX, y = box.maxY + 5
            path.move(to: CGPoint(x:x-4,y:y))
            path.addLine(to: CGPoint(x:x+1,y:y))
            path.addLine(to: CGPoint(x:x+13,y:y+10))
            path.addLine(to: CGPoint(x:x+6,y:y+10))
            path.closeSubpath()
        }
    }
    var advance = CGSize.zero
    CTFontGetAdvancesForGlyphs(font, .horizontal, &glyph, &advance, 1)
    cursor += advance.width
}
let bounds = path.boundingBoxOfPath
if args[3] == "svg" {
    func point(_ p: CGPoint) -> String {
        String(format: "%.2f %.2f", p.x-bounds.minX+1, bounds.maxY-p.y+1)
    }
    var data = ""
    path.applyWithBlock { element in
        let points = element.pointee.points
        switch element.pointee.type {
        case .moveToPoint: data += "M"+point(points[0])
        case .addLineToPoint: data += "L"+point(points[0])
        case .addQuadCurveToPoint: data += "Q"+point(points[0])+" "+point(points[1])
        case .addCurveToPoint: data += "C"+point(points[0])+" "+point(points[1])+" "+point(points[2])
        case .closeSubpath: data += "Z"
        @unknown default: fatalError("Unsupported path element")
        }
    }
    print("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 \(Int(ceil(bounds.width))+2) \(Int(ceil(bounds.height))+2)\"><path fill=\"#ffcf55\" d=\"\(data)\"/></svg>")
} else if args[3] == "lcd" || args[3] == "subtitle-lcd" {
    let subtitle = args[3] == "subtitle-lcd"
    let width = subtitle ? 160 : 104
    let height = subtitle ? 18 : 23
    let prefix = subtitle ? "SUBTITLE" : "WORD"
    let name = subtitle ? "subtitle" : "wordmark"
    let scale = min(CGFloat(width)/bounds.width, CGFloat(height)/bounds.height)
    var pixels = [UInt8](repeating: 0, count: width*height)
    pixels.withUnsafeMutableBytes { raw in
        let ctx = CGContext(data: raw.baseAddress, width: width, height: height,
            bitsPerComponent: 8, bytesPerRow: width, space: CGColorSpaceCreateDeviceGray(),
            bitmapInfo: CGImageAlphaInfo.none.rawValue)!
        ctx.translateBy(x: (CGFloat(width)-bounds.width*scale)/2, y: (CGFloat(height)-bounds.height*scale)/2)
        ctx.scaleBy(x: scale, y: scale)
        ctx.translateBy(x: -bounds.minX, y: -bounds.minY)
        ctx.setFillColor(gray: 1, alpha: 1)
        ctx.addPath(path)
        ctx.fillPath()
    }
    let lettering = subtitle ? "Rodeo" : "Picnic"
    print("/* Generated from the private Taylor Rushing \(lettering) lettering. */")
    print("enum { \(prefix)_WIDTH = \(width), \(prefix)_HEIGHT = \(height) };")
    print("static const uint8_t \(name)[] = {")
    for y in 0..<height {
        let row = stride(from: 0, to: width, by: 8).map { x in
            let value = (0..<8).reduce(0) { value, bit in
                value | (pixels[y*width+x+bit] >= 112 ? 128 >> bit : 0)
            }
            return String(format: "0x%02x", value)
        }
        print("    " + row.joined(separator: ",") + ",")
    }
    print("};")
} else {
    fatalError("Expected svg, lcd or subtitle-lcd")
}
