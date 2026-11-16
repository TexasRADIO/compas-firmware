// Encode the approved PNG as a 4-bit indexed, RGB565 LCD asset.
import CoreGraphics
import Foundation
import ImageIO

let args = CommandLine.arguments
guard args.count == 2,
      let source = CGImageSourceCreateWithURL(URL(fileURLWithPath: args[1]) as CFURL, nil),
      let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else {
    fatalError("usage: swift portrait-pack.swift portrait.png")
}
let size = 108
let palette: [(Int, Int, Int)] = [
    (255, 237, 190), (12, 33, 23), (19, 51, 34), (28, 75, 40),
    (34, 106, 50), (69, 125, 67), (126, 19, 25), (174, 23, 28),
    (218, 32, 36), (244, 55, 48), (180, 77, 42), (214, 138, 77),
    (240, 175, 107), (254, 203, 135), (255, 222, 159), (255, 245, 213)
]
var rgba = [UInt8](repeating: 0, count: size*size*4)
rgba.withUnsafeMutableBytes { bytes in
    let ctx = CGContext(data: bytes.baseAddress, width: size, height: size,
        bitsPerComponent: 8, bytesPerRow: size*4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    ctx.setFillColor(red: 255/255, green: 237/255, blue: 190/255, alpha: 1)
    ctx.fill(CGRect(x: 0, y: 0, width: size, height: size))
    ctx.interpolationQuality = .high
    ctx.draw(image, in: CGRect(x: 0, y: 0, width: size, height: size))
}
func rgb565(_ color: (Int,Int,Int)) -> Int {
    (color.0 >> 3) << 11 | (color.1 >> 2) << 5 | (color.2 >> 3)
}
func nearest(_ position: Int) -> Int {
    let i = position*4
    let r = Int(rgba[i]), g = Int(rgba[i+1]), b = Int(rgba[i+2])
    var best = 0, error = Int.max
    for (index,color) in palette.enumerated() {
        let dr = r-color.0, dg = g-color.1, db = b-color.2
        let distance = 2*dr*dr+3*dg*dg+db*db
        if distance < error { error = distance; best = index }
    }
    return best
}
let packed = stride(from: 0, to: size*size, by: 2).map { nearest($0)<<4 | nearest($0+1) }
let rows = stride(from: 0, to: packed.count, by: 18).map { start in
    "    " + packed[start..<min(start+18,packed.count)].map { String(format:"0x%02x",$0) }.joined(separator:",") + ","
}
let output = "/* Generated from the Compás Johnny Canales illustration. */\n" +
    "enum { TRIBUTE_SIZE = \(size) };\n" +
    "static const uint16_t tribute_colors[16] = {" +
    palette.map { String(format:"0x%04x",rgb565($0)) }.joined(separator:",") + "};\n" +
    "static const uint8_t tribute_pixels[] = {\n" + rows.joined(separator:"\n") + "\n};\n"
print(output, terminator: "")
