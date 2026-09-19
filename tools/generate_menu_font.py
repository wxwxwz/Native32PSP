"""Generate the Native32 Menu Bitmap subset from Unifont 16.0.04 (OFL-1.1).

Usage: python tools/generate_menu_font.py unifont-16.0.04.hex.gz
Only font data is derived; decoding/rendering code is separate.
"""
import gzip
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
points = set(range(32, 127)) | {0x25a1, 0xfffd}
mapping = {}
for encoding in ('cp932', 'gbk'):
    for lead in range(128, 256):
        for raw in [bytes([lead])] + [bytes([lead, tail]) for tail in range(256)]:
            try:
                text = raw.decode(encoding)
            except UnicodeDecodeError:
                continue
            if len(text) != 1:
                continue
            points.add(ord(text))
            if encoding == 'cp932':
                mapping[int.from_bytes(raw, 'big')] = ord(text)

glyphs = []
with gzip.open(sys.argv[1], 'rt', encoding='ascii') as font:
    for line in font:
        code, bitmap = line.strip().split(':')
        point = int(code, 16)
        if point not in points:
            continue
        bits = bytes.fromhex(bitmap)
        width = len(bits) // 2
        if width == 8:
            bits = bytes(v for byte in bits for v in (byte, 0))
        assert width in (8, 16) and len(bits) == 32
        glyphs.append((point, width, bits))
glyphs.sort()
with (root / 'src/platform/menu_font_data.inc').open('w', encoding='ascii', newline='\n') as out:
    out.write('// Generated Native32 Menu Bitmap, derived from Unifont 16.0.04.\n')
    out.write('// Font data licensed OFL-1.1; see licenses/UNIFONT-* files.\n')
    out.write('static const MenuGlyph glyphs[] = {\n')
    for point, width, bits in glyphs:
        out.write('{%d,%d,{%s}},\n' % (point, width, ','.join(str(b) for b in bits)))
    out.write('};\nstatic const CodePair cp932[] = {\n')
    for code, point in sorted(mapping.items()):
        out.write('{%d,%d},\n' % (code, point))
    out.write('};\n')
print('Generated', len(glyphs), 'glyphs and', len(mapping), 'CP932 entries')
