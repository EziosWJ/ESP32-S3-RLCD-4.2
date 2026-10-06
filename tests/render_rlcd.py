"""Render production drawing commands; reject out-of-bounds/overlapping content."""
from pathlib import Path
import re
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]


def render_commands(commands):
    glyphs = {char: [int(x.strip()) for x in rows.split(',')]
              for char, rows in re.findall(r"\{'(.)', \{([0-9, ]+)\}\}",
                                          (ROOT / 'main/rlcd.c').read_text(encoding='utf-8'))}
    pixels = [bytearray([255] * 400) for _ in range(300)]
    text_boxes, graphics = [], set()

    def black(x, y, primitive=False):
        assert 0 <= x < 400 and 0 <= y < 300, (x, y)
        pixels[y][x] = 0
        if primitive:
            graphics.add((x, y))

    for line in commands.splitlines():
        parts = line.split(' ', 4)
        kind, x, y, arg = parts[:4]
        x, y, arg = int(x), int(y), int(arg)
        if kind == 'T':
            text = parts[4]
            box = (x, y, x + (len(text) * 6 - 1) * arg, y + 7 * arg)
            assert box[2] <= 400 and box[3] <= 300, text
            for old_box, old_text in text_boxes:
                assert not (box[0] < old_box[2] and old_box[0] < box[2] and box[1] < old_box[3] and old_box[1] < box[3]), (text, old_text)
            text_boxes.append((box, text))
            for i, char in enumerate(text):
                if char == ' ':
                    continue
                assert char in glyphs, char
                for row, bits in enumerate(glyphs[char]):
                    for col in range(5):
                        if bits & (1 << (4 - col)):
                            for dy in range(arg):
                                for dx in range(arg):
                                    black(x + i * 6 * arg + col * arg + dx, y + row * arg + dy)
        elif kind == 'H':
            for dx in range(arg):
                black(x + dx, y, True)
        elif kind == 'R':
            height = int(parts[4])
            for dx in range(arg):
                black(x + dx, y, True); black(x + dx, y + height - 1, True)
            for dy in range(height):
                black(x, y + dy, True); black(x + arg - 1, y + dy, True)
        else:
            raise AssertionError(line)
    for (left, top, right, bottom), text in text_boxes:
        assert not any(left <= x < right and top <= y < bottom for x, y in graphics), ('Graphic overlaps text', text)

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 400, 300, 8, 0, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(b''.join(b'\x00' + p for p in pixels))) + chunk(b'IEND', b''))
