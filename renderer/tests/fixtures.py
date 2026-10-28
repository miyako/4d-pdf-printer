"""Generate original synthetic OPC packages; no external documents or font assets."""

import io
import struct
import zipfile
import zlib
from xml.sax.saxutils import escape

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen


TEXT = "AB \u03a9\u4e2d"


def font():
    builder = FontBuilder(1000, isTTF=True)
    names = [".notdef", "space", "A", "B", "Omega", "CJK"]
    builder.setupGlyphOrder(names)
    builder.setupCharacterMap({32: "space", 65: "A", 66: "B", 937: "Omega", 20013: "CJK"})
    glyphs = {}
    for i, name in enumerate(names):
        pen = TTGlyphPen(None)
        if name != "space":
            # Deliberately artificial, distinct outlines, not a typographic fidelity corpus.
            pen.moveTo((80, 0))
            pen.lineTo((520, 0))
            pen.lineTo((520 - i * 30, 700))
            pen.lineTo((80 + i * 20, 700))
            pen.closePath()
        glyphs[name] = pen.glyph()
    builder.setupGlyf(glyphs)
    builder.setupHorizontalMetrics({name: (600, 0) for name in names})
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    builder.setupNameTable({
        "familyName": "Original Synthetic Worker Test",
        "styleName": "Regular",
        "uniqueFontIdentifier": "WorkerTest-1",
        "fullName": "Original Synthetic Worker Test",
        "psName": "WorkerTest-1",
    })
    builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
    builder.setupPost()
    builder.setupMaxp()
    builder.font["head"].created = 0
    builder.font["head"].modified = 0
    data = io.BytesIO()
    builder.save(data)
    return data.getvalue()


def png():
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    # 2 x 2 RGB: red, green, blue, white.
    pixels = b"\0\xff\0\0\0\xff\0\0\0\0\xff\xff\xff\xff"
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))


def package(path, *, oxps=False, pages=2, documents=1, broken=None, encrypted=False):
    ns = "http://schemas.openxps.org/oxps/v1.0" if oxps else "http://schemas.microsoft.com/xps/2005/06"
    opc = "http://schemas.openxmlformats.org/package/2006/relationships"
    types = ("<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
             "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
             "<Default Extension=\"ttf\" ContentType=\"application/vnd.ms-opentype\"/>"
             "<Default Extension=\"png\" ContentType=\"image/png\"/>")
    suffix = "oxps" if oxps else "ms-package.xps"
    types += f'<Override PartName="/FixedDocumentSequence.fdseq" ContentType="application/vnd.{suffix}-fixeddocumentsequence+xml"/>'
    sequence = f'<FixedDocumentSequence xmlns="{ns}">'
    parts = {
        "_rels/.rels": f'<Relationships xmlns="{opc}"><Relationship Id="r1" Type="{ns}/fixedrepresentation" Target="/FixedDocumentSequence.fdseq"/></Relationships>',
        "Resources/test.ttf": font(),
        "Resources/pixels.png": png(),
    }
    for d in range(1, documents + 1):
        sequence += f'<DocumentReference Source="/Documents/{d}/FixedDocument.fdoc"/>'
        document = f'<FixedDocument xmlns="{ns}">'
        types += f'<Override PartName="/Documents/{d}/FixedDocument.fdoc" ContentType="application/vnd.{suffix}-fixeddocument+xml"/>'
        for p in range(1, pages + 1):
            document += f'<PageContent Source="Pages/{p}.fpage"/>'
            types += f'<Override PartName="/Documents/{d}/Pages/{p}.fpage" ContentType="application/vnd.{suffix}-fixedpage+xml"/>'
            width, height = (384, 288) if p % 2 else (192, 384)
            content = (f'<FixedPage xmlns="{ns}" Width="{width}" Height="{height}" xml:lang="en-US">'
                       '<Path Fill="#FFFF0000" Data="M 20,20 L 120,20 120,80 20,80 Z"/>'
                       '<Canvas Clip="M 20,20 L 80,20 80,80 20,80 Z" Opacity="0.5">'
                       '<Path Fill="#FF0000FF" Data="M 0,0 L 100,0 100,100 0,100 Z"/></Canvas>'
                       '<Path Data="M 130,20 L 170,20 170,60 130,60 Z">'
                       '<Path.Fill><ImageBrush ImageSource="/Resources/pixels.png" '
                       'Viewbox="0,0,2,2" ViewboxUnits="Absolute" Viewport="130,20,40,40" '
                       'ViewportUnits="Absolute" TileMode="None"/></Path.Fill></Path>'
                       f'<Glyphs FontUri="/Resources/test.ttf" FontRenderingEmSize="24" '
                       f'OriginX="20" OriginY="150" Fill="#FF000000" UnicodeString="{escape(TEXT)}"/>'
                       '</FixedPage>')
            if broken == "xml":
                content = f'<FixedPage xmlns="{ns}" Width="384" Height="288"><Path'
            elif broken == "size":
                content = content.replace('Width="384"', 'Width="-1"')
            elif broken == "render":
                content = content.replace("M 20,20 L 120,20 120,80 20,80 Z", "NOT_A_PATH")
            elif broken == "font":
                content = content.replace("/Resources/test.ttf", "/Resources/absent.ttf")
            elif broken == "arc":
                content = content.replace("M 20,20 L 120,20 120,80 20,80 Z",
                                          "M 20,20 A 10,10 0 0 0 120,80 Z")
            elif broken == "arc-segment":
                content = content.replace('<Path Fill="#FFFF0000" Data="M 20,20 L 120,20 120,80 20,80 Z"/>',
                                          '<Path Fill="#FFFF0000"><Path.Data><PathGeometry>'
                                          '<PathFigure StartPoint="20,20"><ArcSegment Point="120,80" '
                                          'Size="10,10" RotationAngle="0" IsLargeArc="false" '
                                          'SweepDirection="Clockwise"/></PathFigure>'
                                          '</PathGeometry></Path.Data></Path>')
            elif broken == "tile":
                content = content.replace('TileMode="None"', 'TileMode="FlipX"')
            elif broken == "triangle":
                content = content.replace('<Path Fill="#FFFF0000"', '<Path StrokeDashCap="Triangle" Fill="#FFFF0000"')
            elif broken == "color":
                content = content.replace("#FFFF0000", "NotAColor")
            elif broken == "path-child":
                content = content.replace('<Path Fill="#FFFF0000" Data="M 20,20 L 120,20 120,80 20,80 Z"/>',
                                          '<Path Fill="#FFFF0000"><UnsupportedPathChild/></Path>')
            elif broken == "gradient":
                content = content.replace('<Path Fill="#FFFF0000" Data="M 20,20 L 120,20 120,80 20,80 Z"/>',
                                          '<Path Data="M 20,20 L 120,20 120,80 20,80 Z"><Path.Fill>'
                                          '<LinearGradientBrush StartPoint="20,20" EndPoint="120,80" '
                                          'ColorInterpolationMode="ScRgbLinearInterpolation">'
                                          '<LinearGradientBrush.GradientStops>'
                                          '<GradientStop Color="#FFFF0000" Offset="0"/>'
                                          '<GradientStop Color="#FF0000FF" Offset="1"/>'
                                          '</LinearGradientBrush.GradientStops>'
                                          '</LinearGradientBrush></Path.Fill></Path>')
            parts[f"Documents/{d}/Pages/{p}.fpage"] = content
            parts[f"Documents/{d}/Pages/_rels/{p}.fpage.rels"] = (
                f'<Relationships xmlns="{opc}">'
                f'<Relationship Id="font" Type="{ns}/required-resource" Target="/Resources/test.ttf"/>'
                f'<Relationship Id="image" Type="{ns}/required-resource" Target="/Resources/pixels.png"/>'
                '</Relationships>')
        parts[f"Documents/{d}/FixedDocument.fdoc"] = document + '</FixedDocument>'
    parts["FixedDocumentSequence.fdseq"] = sequence + '</FixedDocumentSequence>'
    parts["[Content_Types].xml"] = types + '</Types>'
    if broken == "relationship":
        parts["_rels/.rels"] = f'<Relationships xmlns="{opc}"/>'
    if broken == "image":
        parts["Resources/pixels.png"] = b"invalid image"
    if broken == "font-data":
        parts["Resources/test.ttf"] = b"invalid font"
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, value in parts.items():
            info = zipfile.ZipInfo(name, (2000, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, value.encode("utf-8") if isinstance(value, str) else value)
    if encrypted:
        # Flags only: must be rejected before attempting to interpret/decrypt payloads.
        data = bytearray(path.read_bytes())
        for signature, offset in ((b"PK\x03\x04", 6), (b"PK\x01\x02", 8)):
            position = 0
            while (position := data.find(signature, position)) >= 0:
                data[position + offset] |= 1
                position += 4
        path.write_bytes(data)
