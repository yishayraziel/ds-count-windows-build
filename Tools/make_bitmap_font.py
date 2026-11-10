# Builds a bitmap-style TTF: each glyph is the aliased raster of an OFL source font at a small pixel size,
# stored as square pixels so it scales as crisp blocks.
import sys
from PIL import Image, ImageDraw, ImageFont
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

src, px, family, style, out = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4], sys.argv[5]
em = int(sys.argv[6]); track = int(sys.argv[7]); dil = int(sys.argv[8]) if len(sys.argv) > 8 else 0
U = 100
f = ImageFont.truetype(src, px)
rasc, rdesc = f.getmetrics()
asc, desc = 11, 3
chars = [chr(c) for c in range(32, 127)] + list("°±×µ·–—‘’“”…")
glyphs, advances, cmap, order = {}, {}, {}, [".notdef"]
def emptyglyph():
    p = TTGlyphPen(None); return p.glyph()
glyphs[".notdef"] = emptyglyph(); advances[".notdef"] = (px * U // 2, 0)
for ch in chars:
    name = "uni%04X" % ord(ch)
    w = int(round(f.getlength(ch)))
    # Render after a reference "H" so every glyph shares the string baseline, then remove the "H".
    hw = int(round(f.getlength("H")))
    def ras(t):
        im = Image.new("1", (hw + w + px * 3, asc + desc + 4), 0)
        dd = ImageDraw.Draw(im); dd.fontmode = "1"
        dd.text((px, 2), t, font=f, fill=1, anchor="la")
        return im
    a, b = ras("H" + ch), ras("H")
    from PIL import ImageChops
    full = ImageChops.subtract(a.convert("L"), b.convert("L")).point(lambda v: 255 if v > 0 else 0).convert("1")
    if dil:
        full = ImageChops.logical_or(full, ImageChops.offset(full, 1, 0))
    img = Image.new("1", (w + px * 2, asc + desc + 4), 0)
    img.paste(full.crop((hw, 0, hw + w + px * 2, asc + desc + 4)), (0, 0))
    pen = TTGlyphPen(None)
    W, H = img.size
    pix = img.load()
    for y in range(H):
        x = 0
        while x < W:
            if pix[x, y]:
                x0 = x
                while x < W and pix[x, y]: x += 1
                # baseline is at row asc+2; y grows downward
                top = (rasc + 2 - y) * U; bot = top - U
                l = (x0 - px) * U; r = (x - px) * U
                pen.moveTo((l, bot)); pen.lineTo((l, top)); pen.lineTo((r, top)); pen.lineTo((r, bot)); pen.closePath()
            else:
                x += 1
    glyphs[name] = pen.glyph(); advances[name] = ((w + track + dil) * U, 0); cmap[ord(ch)] = name; order.append(name)
fb = FontBuilder(em * U, isTTF=True)
fb.setupGlyphOrder(order); fb.setupCharacterMap(cmap); fb.setupGlyf(glyphs)
fb.setupHorizontalMetrics({g: (advances[g][0], 0) for g in order})
fb.setupHorizontalHeader(ascent=asc * U, descent=-desc * U)
fb.setupNameTable({"familyName": family, "styleName": style, "copyright": "Bitmap rasterisation of Arimo (c) 2020 The Arimo Project Authors, SIL Open Font License 1.1", "licenseDescription": "SIL Open Font License 1.1", "version": "Version 1.000"})
fb.setupOS2(sTypoAscender=asc * U, sTypoDescender=-desc * U, usWinAscent=asc * U, usWinDescent=desc * U, usWeightClass=700 if style == "Bold" else 400, fsSelection=0x20 if style == "Bold" else 0x40)
fb.setupPost(); fb.setupHead(unitsPerEm=em * U, macStyle=1 if style == "Bold" else 0)
fb.save(out)
print(out, len(order), "glyphs, asc", asc, "desc", desc)
