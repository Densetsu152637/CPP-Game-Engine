"""Regenerate original synthetic PNG/font/WAV fixture assets using Python stdlib.
These simple test bitmaps, glyph shapes and tones are authored for this example;
no external artwork, font, music or scripture is embedded. Python is not needed
at runtime. Run from any directory; outputs stay within this example.
"""
from pathlib import Path
import json
import math
import struct
import wave
import zlib

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"

def png(path, width, height, pixels):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    rows = b"".join(b"\0" + bytes(pixels[y * width * 4:(y + 1) * width * 4]) for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))

# Original 5x7 developer letterforms, drawn as binary grids. Lowercase uses
# the same readable uppercase forms; each scalar still has its own atlas entry.
PATTERNS = {
 "A":"01110/10001/10001/11111/10001/10001/10001", "B":"11110/10001/10001/11110/10001/10001/11110",
 "C":"01111/10000/10000/10000/10000/10000/01111", "D":"11110/10001/10001/10001/10001/10001/11110",
 "E":"11111/10000/10000/11110/10000/10000/11111", "F":"11111/10000/10000/11110/10000/10000/10000",
 "G":"01111/10000/10000/10111/10001/10001/01110", "H":"10001/10001/10001/11111/10001/10001/10001",
 "I":"11111/00100/00100/00100/00100/00100/11111", "J":"00111/00010/00010/00010/10010/10010/01100",
 "K":"10001/10010/10100/11000/10100/10010/10001", "L":"10000/10000/10000/10000/10000/10000/11111",
 "M":"10001/11011/10101/10101/10001/10001/10001", "N":"10001/11001/11001/10101/10011/10011/10001",
 "O":"01110/10001/10001/10001/10001/10001/01110", "P":"11110/10001/10001/11110/10000/10000/10000",
 "Q":"01110/10001/10001/10001/10101/10010/01101", "R":"11110/10001/10001/11110/10100/10010/10001",
 "S":"01111/10000/10000/01110/00001/00001/11110", "T":"11111/00100/00100/00100/00100/00100/00100",
 "U":"10001/10001/10001/10001/10001/10001/01110", "V":"10001/10001/10001/10001/10001/01010/00100",
 "W":"10001/10001/10001/10101/10101/10101/01010", "X":"10001/10001/01010/00100/01010/10001/10001",
 "Y":"10001/10001/01010/00100/00100/00100/00100", "Z":"11111/00001/00010/00100/01000/10000/11111",
 "0":"01110/10001/10011/10101/11001/10001/01110", "1":"00100/01100/00100/00100/00100/00100/01110",
 "2":"01110/10001/00001/00010/00100/01000/11111", "3":"11110/00001/00001/01110/00001/00001/11110",
 "4":"00010/00110/01010/10010/11111/00010/00010", "5":"11111/10000/10000/11110/00001/00001/11110",
 "6":"01110/10000/10000/11110/10001/10001/01110", "7":"11111/00001/00010/00100/01000/01000/01000",
 "8":"01110/10001/10001/01110/10001/10001/01110", "9":"01110/10001/10001/01111/00001/00001/01110",
 "?":"01110/10001/00001/00010/00100/00000/00100", ".":"00000/00000/00000/00000/00000/00100/00100",
 ":":"00000/00100/00100/00000/00100/00100/00000", ";":"00000/00100/00100/00000/00100/00100/01000",
 ",":"00000/00000/00000/00000/00100/00100/01000", "!":"00100/00100/00100/00100/00100/00000/00100",
 "-":"00000/00000/00000/11111/00000/00000/00000", "_":"00000/00000/00000/00000/00000/00000/11111",
 "[":"01110/01000/01000/01000/01000/01000/01110", "]":"01110/00010/00010/00010/00010/00010/01110",
 "(":"00010/00100/01000/01000/01000/00100/00010", ")":"01000/00100/00010/00010/00010/00100/01000",
 "/":"00001/00001/00010/00100/01000/10000/10000", "|":"00100/00100/00100/00100/00100/00100/00100",
 "=":"00000/00000/11111/00000/11111/00000/00000", "+":"00000/00100/00100/11111/00100/00100/00000",
 ">":"10000/01000/00100/00010/00100/01000/10000", "<":"00001/00010/00100/01000/00100/00010/00001",
 "'":"00100/00100/01000/00000/00000/00000/00000", '"':"01010/01010/00000/00000/00000/00000/00000",
 "Ω":"01110/10001/10001/10001/10001/01010/11011", "十":"00100/00100/00100/11111/00100/00100/00100",
}

def font():
    scalars = list(range(32, 127)) + [ord("Ω"), ord("十")]
    width, height = 128, 56
    pixels = bytearray(width * height * 4)
    glyphs = []
    for index, scalar in enumerate(scalars):
        x, y = (index % 16) * 8, (index // 16) * 8
        char = chr(scalar)
        pattern = PATTERNS.get(char.upper(), PATTERNS["?"]) if char != " " else "00000/" * 6 + "00000"
        for row, bits in enumerate(pattern.split("/")):
            for column, bit in enumerate(bits):
                if bit == "1":
                    at = ((y + row) * width + x + column) * 4
                    pixels[at:at + 4] = bytes((235, 245, 255, 255))
        glyphs.append({"codepoint":scalar, "source":[x,y,5,7], "advance":6, "bearing":[0,0]})
    png(ASSETS / "font.png", width, height, pixels)
    (ASSETS / "font.json").write_text(json.dumps({"schema":1,"texture":"asset:font-texture","lineHeight":8,"fallback":63,"glyphs":glyphs},indent=2)+"\n",encoding="utf-8")

def atlas():
    width, height = 128, 32
    pixels = bytearray(width * height * 4)
    def put(frame, x, y, color):
        px, py = (frame % 8)*16+x, (frame//8)*16+y
        at=(py*width+px)*4; pixels[at:at+4]=bytes(color)
    for frame in range(16):
        for y in range(16):
            for x in range(16):
                color=(0,0,0,0)
                if frame < 4:
                    if 5 <= x <= 10 and 2 <= y <= 7: color=(255,210,140,255)
                    if 4 <= x <= 11 and 8 <= y <= 12: color=(55,155+frame*15,235,255)
                    if y in (13,14) and x in (5+frame%2,9-frame%2): color=(100,210,255,255)
                elif frame < 8:
                    wing = 2 + (frame-4)%3
                    if abs(x-8)+abs(y-8) <= 3: color=(245,250,255,255)
                    if 3 <= x <= 12 and abs(y-(5+wing)) <= 1: color=(205,235,255,255)
                    if x == 11 and y == 7: color=(250,180,80,255)
                elif frame in (8,9,10,11):
                    palette={8:(245,210,105),9:(245,140,75),10:(95,205,150),11:(100,170,250)}
                    if 2 <= x <= 13 and 2 <= y <= 13: color=(*palette[frame],255 if x in (2,13) or y in (2,13) else 120)
                elif frame in (12,13):
                    radius=5+(frame%2)
                    if (x-8)**2+(y-8)**2 <= radius**2: color=(255,255,230,95 if frame==12 else 140)
                elif frame == 14: color=(28+(x+y)%2*4,36+(x+y)%2*4,48+(x+y)%2*4,255)
                else: color=(68+(x%4==0)*20,76+(y%4==0)*20,90,255)
                put(frame,x,y,color)
    png(ASSETS / "sprites.png",width,height,pixels)

def tone(name, frequency, seconds, loop):
    rate=22050; count=round(rate*seconds)
    samples=[]
    for i in range(count):
        amplitude=.12 if loop else .25*math.sin(math.pi*i/count)**2
        samples.append(round(32767*amplitude*math.sin(2*math.pi*frequency*i/rate)))
    with wave.open(str(ASSETS/name),"wb") as output:
        output.setnchannels(1); output.setsampwidth(2); output.setframerate(rate)
        output.writeframes(struct.pack("<"+"h"*len(samples),*samples))

if __name__ == "__main__":
    ASSETS.mkdir(exist_ok=True)
    atlas(); font(); tone("cue.wav",660,.2,False); tone("loop.wav",220,1,True)
    print("Original RGBA/font/PCM16 fixture assets regenerated")
