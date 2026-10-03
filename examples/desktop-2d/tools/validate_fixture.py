"""Static bounds/provenance/reference validation; not a substitute for engine tests."""
from pathlib import Path
import json
import struct
import wave
import zlib
ROOT=Path(__file__).resolve().parents[1]

def rgba(path):
    data=path.read_bytes(); assert data[:8] == b"\x89PNG\r\n\x1a\n"
    offset=8; compressed=b""; size=None
    while offset < len(data):
        length=struct.unpack_from(">I",data,offset)[0]; kind=data[offset+4:offset+8]
        payload=data[offset+8:offset+8+length]; crc=struct.unpack_from(">I",data,offset+8+length)[0]
        assert zlib.crc32(kind+payload)&0xffffffff == crc, "bad PNG CRC"
        if kind==b"IHDR":
            width,height,depth,color,_,_,interlace=struct.unpack(">IIBBBBB",payload)
            assert depth==8 and color==6 and interlace==0
            size=(width,height)
        elif kind==b"IDAT": compressed+=payload
        offset+=length+12
    rows=zlib.decompress(compressed); width,height=size
    assert len(rows)==height*(width*4+1)
    pixels=bytearray()
    for y in range(height):
        row=rows[y*(width*4+1):(y+1)*(width*4+1)]; assert row[0]==0; pixels.extend(row[1:])
    return width,height,pixels

def main():
    dimensions={}
    for filename in ("sprites.png","font.png"):
        width,height,pixels=rgba(ROOT/"assets"/filename); dimensions[filename]=(width,height)
        assert min(pixels[3::4])==0 and max(pixels[3::4])==255
    assert any(0<a<255 for a in rgba(ROOT/"assets/sprites.png")[2][3::4]), "missing translucent test pixels"
    font=json.loads((ROOT/"assets/font.json").read_text()); assert font["texture"]=="asset:font-texture"
    glyphs={g["codepoint"]:g for g in font["glyphs"]}; assert len(glyphs)==len(font["glyphs"])
    assert {63,ord("Ω"),ord("十"),*range(32,127)} <= glyphs.keys()
    for glyph in glyphs.values():
        x,y,w,h=glyph["source"]; assert x>=0 and y>=0 and x+w<=128 and y+h<=56
    for filename in ("cue.wav","loop.wav"):
        with wave.open(str(ROOT/"assets"/filename),"rb") as audio:
            assert audio.getnchannels()==1 and audio.getsampwidth()==2 and audio.getframerate()==22050 and audio.getnframes()>0
    for path in ROOT.glob("project*.json"):
        project=json.loads(path.read_text()); assets={a["id"]:a for a in project["assets"]}
        assert len(assets)==len(project["assets"])
        for asset in assets.values():
            resolved=(ROOT/asset["path"]).resolve(); assert resolved.is_relative_to(ROOT.resolve()) and resolved.is_file()
        files={ROOT/project["startup_scene"]}
        files.update(ROOT/a["path"] for a in assets.values() if a["kind"]=="scene")
        for scene_path in files:
            scene=json.loads(scene_path.read_text()); ids={e["id"] for e in scene["entities"]}
            assert len(ids)==len(scene["entities"])
            for entity in scene["entities"]:
                components=entity["components"]
                if "Script" in components: assert assets[components["Script"]["asset"]]["kind"]=="script"
                if "Camera2D" in components: assert components["Camera2D"]["follow"] in ids
                if "SpriteRenderer" in components:
                    sprite=components["SpriteRenderer"]; texture=assets[sprite["texture"]]; assert texture["kind"]=="texture"
                    tw,th=dimensions[Path(texture["path"]).name]
                    for x,y,w,h in [sprite["source"],*sprite.get("frames",[])]: assert x>=0 and y>=0 and w>0 and h>0 and x+w<=tw and y+h<=th
            if "inputBindings" in project:
                assert {"actor:player","actor:dove1","actor:dove2","actor:scene-dove","entry"} <= ids
                assert project["inputBindings"]["story_demo"]==["Key:T"]
    provenance=json.loads((ROOT/"assets/provenance.json").read_text()); assert all((ROOT/"assets"/f).is_file() for f in provenance["assets"])
    print("DESKTOP2D_STATIC_PASS: 7 manifests, bounded RGBA/font/PCM16 assets, stable references")

if __name__=="__main__": main()
