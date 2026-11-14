#!/usr/bin/env python3
"""Writes the sample project in samples/hello: a small village to walk around.

The art is drawn here, in code, so the sample has no third-party assets and can be
regenerated exactly: python3 tools/make_sample.py. GUIDs are fixed, so the scene and
logic files keep pointing at the same assets.
"""

import json
import math
import pathlib
import struct
import wave
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent / "samples" / "hello"

# Fixed GUIDs, one per asset file.
GUIDS = {
    "art/hero.png": "5a1c0e2b7f3d4c6a9b8e1d2f3a4b5c6d",
    "art/elder.png": "6b2d1f3c8a4e4d7b0c9f2e3a4b5c6d7e",
    "art/tiles.png": "7c3e2a4d9b5f4e8c1d0a3f4b5c6d7e8f",
    "sfx/chime.wav": "8d4f3b5e0c6a4f9d2e1b4a5c6d7e8f90",
    "maps/village.dscene": "9e5a4c6f1d7b4a0e3f2c5b6d7e8f90a1",
    "logic/main.drules": "af6b5d7a2e8c4b1f4a3d6c7e8f90a1b2",
    "sfx/stream.wav": "b07c6e8b3f9d4c2a5b4e7d8f90a1b2c3",
    "maps/road.dscene": "c18d7f9c4a0e4d3b6c5f8e90a1b2c3d4",
    "logic/road.drules": "d29e80ad5b1f4e4c7d6a9f0a1b2c3d4e",
}


def png(path, width, height, pixels):
    """Writes RGBA pixels (a list of rows of (r, g, b, a)) as a PNG."""
    raw = b"".join(b"\x00" + b"".join(bytes(p) for p in row) for row in pixels)
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))
    data = (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


CLEAR = (0, 0, 0, 0)


def canvas(width, height):
    return [[CLEAR] * width for _ in range(height)]


def rect(img, x0, y0, x1, y1, colour):
    for y in range(y0, y1):
        for x in range(x0, x1):
            img[y][x] = colour


def blit(dest, src, dx, dy):
    for y, row in enumerate(src):
        for x, p in enumerate(row):
            if p[3]:
                dest[dy + y][dx + x] = p


def character(robe, skin, hair, facing, step, beard=False):
    """A 16 x 16 person. facing: 0 down, 1 left, 2 right, 3 up. step: 0, 1, 2."""
    img = canvas(16, 16)
    outline = (30, 24, 36, 255)
    # Legs: apart or together with the step.
    lift = {0: (0, 1), 1: (0, 0), 2: (1, 0)}[step]
    rect(img, 5, 12 - lift[0], 7, 15 - lift[0], outline)
    rect(img, 9, 12 - lift[1], 11, 15 - lift[1], outline)
    # Body and head.
    rect(img, 4, 7, 12, 13, robe)
    rect(img, 4, 7, 12, 8, tuple(max(c - 40, 0) for c in robe[:3]) + (255,))
    rect(img, 5, 2, 11, 7, skin)
    rect(img, 5, 1, 11, 3, hair)
    if facing == 0:  # eyes looking down the screen
        img[4][6] = outline
        img[4][9] = outline
    elif facing == 1:
        img[4][5] = outline
        rect(img, 9, 2, 11, 7, hair)
    elif facing == 2:
        img[4][10] = outline
        rect(img, 5, 2, 7, 7, hair)
    else:  # seen from behind
        rect(img, 5, 2, 11, 6, hair)
    if beard and facing != 3:
        rect(img, 6, 5, 10, 8, (235, 235, 240, 255))
    return img


def tiles():
    img = canvas(48, 16)
    grass = (76, 150, 72, 255)
    dark = (58, 122, 60, 255)
    path = (196, 170, 120, 255)
    pebble = (170, 144, 100, 255)
    stone = (128, 128, 140, 255)
    mortar = (92, 92, 104, 255)
    rect(img, 0, 0, 16, 16, grass)
    for x, y in [(2, 3), (9, 5), (13, 11), (5, 12), (11, 1), (7, 8)]:
        img[y][x] = dark
    rect(img, 16, 0, 32, 16, path)
    for x, y in [(19, 4), (27, 9), (22, 13), (29, 2)]:
        img[y][x] = pebble
    rect(img, 32, 0, 48, 16, stone)
    for y in (0, 5, 10, 15):
        rect(img, 32, y, 48, y + 1, mortar)
    for y0, offset in ((0, 0), (5, 4), (10, 0)):
        for x in range(32 + offset, 48, 8):
            rect(img, x, y0, x + 1, y0 + 5, mortar)
    return img


def chime(path):
    """A short bell-like tone: 880 Hz with an octave, fading out."""
    rate = 22050
    frames = int(rate * 0.25)
    samples = bytearray()
    for i in range(frames):
        t = i / rate
        level = math.exp(-t * 14)
        value = 0.4 * level * (math.sin(2 * math.pi * 880 * t) + 0.5 * math.sin(2 * math.pi * 1760 * t))
        samples += struct.pack("<h", int(max(-1.0, min(1.0, value)) * 32767))
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(bytes(samples))


def stream(path):
    """Running water: a second of soft noise that repeats without a click.

    Noise from a fixed sequence (so the file is always the same), smoothed; its first
    tenth of a second is blended with what would follow its end.
    """
    rate = 22050
    frames = rate
    state = 12345
    raw = []
    for _ in range(frames + rate // 10):
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        raw.append(state / 0x7FFFFFFF * 2.0 - 1.0)
    smooth = []
    low = 0.0
    for value in raw:
        low += 0.18 * (value - low)
        smooth.append(low)
    tail = rate // 10
    samples = bytearray()
    for i in range(frames):
        value = smooth[i]
        if i < tail:
            # The start fades in over what follows the end, so the end runs into it.
            t = i / tail
            value = value * t + smooth[frames + i] * (1.0 - t)
        samples += struct.pack("<h", int(max(-1.0, min(1.0, value * 0.7)) * 32767))
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(bytes(samples))


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
                    encoding="utf-8")


def main():
    assets = ROOT / "assets"
    # Art.
    hero = canvas(48, 64)
    for facing in range(4):
        for step in range(3):
            blit(hero, character((60, 96, 200, 255), (240, 200, 160, 255), (90, 60, 30, 255),
                                 facing, step), step * 16, facing * 16)
    png(assets / "art/hero.png", 48, 64, hero)
    png(assets / "art/elder.png", 16, 16,
        character((120, 70, 150, 255), (230, 190, 150, 255), (230, 230, 235, 255), 0, 1, True))
    png(assets / "art/tiles.png", 48, 16, tiles())
    chime(assets / "sfx/chime.wav")
    stream(assets / "sfx/stream.wav")

    # The village: 20 x 12 tiles of grass, a path, and a wall around it.
    width, height = 20, 12
    ground = []
    for y in range(height):
        ground.append(",".join("1" if y == 8 else "0" for _ in range(width)))
    walls = []
    for y in range(height):
        row = []
        for x in range(width):
            edge = x in (0, width - 1) or y in (0, height - 1)
            row.append("2" if edge else "")
        walls.append(",".join(row))
    tiles_guid = GUIDS["art/tiles.png"]
    scene = {
        "format": "sparxel.scene",
        "version": 1,
        "entities": [
            {"guid": "10000000000040008000000000000001", "name": "ground",
             "components": {"transform_2d": {"x": 0.0, "y": 0.0, "layer": -1},
                            "tilemap": {"tileset": tiles_guid, "tiles": "\n".join(ground)}}},
            {"guid": "10000000000040008000000000000002", "name": "walls",
             "components": {"transform_2d": {"x": 0.0, "y": 0.0, "layer": -1},
                            "tilemap": {"tileset": tiles_guid, "tiles": "\n".join(walls),
                                        "solid": True, "height": 1.0}}},
            {"guid": "10000000000040008000000000000003", "name": "hero",
             "components": {"transform_2d": {"x": 10.0, "y": 9.0},
                            "sprite": {"image": GUIDS["art/hero.png"], "frame_width": 16,
                                       "frame_height": 16, "frame": 1},
                            "top_down_mover": {"speed": 4.0},
                            "walk_animation": {"frames_per_direction": 3}}},
            {"guid": "10000000000040008000000000000004", "name": "elder",
             "components": {"transform_2d": {"x": 6.0, "y": 5.0},
                            "sprite": {"image": GUIDS["art/elder.png"]},
                            "body": {}, "interactable": {}}},
            {"guid": "10000000000040008000000000000005", "name": "gate",
             "components": {"transform_2d": {"x": 18.0, "y": 9.0},
                            "trigger": {"width": 1.0, "height": 1.0}}},
            # Warm lights for the Diorama style: a lantern by the gate and a lamp by the
            # elder's house. They glow when the weather turns dark.
            {"guid": "10000000000040008000000000000006", "name": "lantern",
             "components": {"transform_2d": {"x": 17.5, "y": 7.5},
                            "light": {"radius": 4.0, "height": 1.5, "intensity": 1.2}}},
            {"guid": "10000000000040008000000000000007", "name": "lamp",
             "components": {"transform_2d": {"x": 4.5, "y": 4.0},
                            "light": {"radius": 3.5, "height": 1.0, "color_g": 200,
                                      "color_b": 150}}},
        ],
    }
    write_json(assets / "maps/village.dscene", scene)

    # The road beyond the gate: a second scene. A stream runs across it, heard louder the
    # closer the hero comes; the way back is at its west end.
    road_ground = []
    for y in range(height):
        row = []
        for x in range(width):
            row.append("1" if y == 8 else "0")
        road_ground.append(",".join(row))
    road = {
        "format": "sparxel.scene",
        "version": 1,
        "entities": [
            {"guid": "20000000000040008000000000000001", "name": "ground",
             "components": {"transform_2d": {"x": 0.0, "y": 0.0, "layer": -1},
                            "tilemap": {"tileset": tiles_guid, "tiles": "\n".join(road_ground)}}},
            {"guid": "20000000000040008000000000000002", "name": "walls",
             "components": {"transform_2d": {"x": 0.0, "y": 0.0, "layer": -1},
                            "tilemap": {"tileset": tiles_guid, "tiles": "\n".join(walls),
                                        "solid": True, "height": 1.0}}},
            {"guid": "20000000000040008000000000000003", "name": "hero",
             "components": {"transform_2d": {"x": 3.0, "y": 9.0},
                            "sprite": {"image": GUIDS["art/hero.png"], "frame_width": 16,
                                       "frame_height": 16, "frame": 1},
                            "top_down_mover": {"speed": 4.0},
                            "walk_animation": {"frames_per_direction": 3}}},
            {"guid": "20000000000040008000000000000004", "name": "milestone",
             "components": {"transform_2d": {"x": 3.0, "y": 9.0}}},
            {"guid": "20000000000040008000000000000005", "name": "way_back",
             "components": {"transform_2d": {"x": 1.5, "y": 9.0},
                            "trigger": {"width": 1.0, "height": 1.0}}},
            {"guid": "20000000000040008000000000000006", "name": "stream",
             "components": {"transform_2d": {"x": 14.0, "y": 5.0},
                            "sound_source": {"sound": GUIDS["sfx/stream.wav"], "range": 14.0,
                                             "volume": 0.8}}},
        ],
    }
    write_json(assets / "maps/road.dscene", road)

    def text(value):
        return {"kind": "literal", "value": value}

    def log(block, value):
        return {"block": block, "kind": "log", "values": [text(value)]}

    def light(block, member, value):
        return {"block": block, "kind": "set", "object": "lighting", "member": member,
                "values": [text(float(value))]}

    def say(block, value):
        return {"block": block, "kind": "do", "object": "dialogue", "member": "say",
                "values": [text(value)]}

    logic = {
        "format": "sparxel.logic",
        "version": 1,
        "name": "main",
        "variables": [{"id": "talks", "kind": "whole_number"}],
        "rules": [
            {"block": 1, "event": "game_start",
             "do": [say(2, "Welcome to the village! Arrow keys to walk, Z to talk.")]},
            {"block": 3, "object": "elder", "event": "interacted",
             "do": [say(4, "Elder: Welcome, traveller. The road east leads home."),
                    {"block": 5, "kind": "do", "object": "audio", "member": "play_sound",
                     "values": [text("sfx/chime.wav")]},
                    {"block": 6, "kind": "set_variable", "member": "talks",
                     "values": [{"kind": "arithmetic", "op": "add",
                                 "operands": [{"kind": "variable", "member": "talks"},
                                              text(1)]}]},
                    # A question, then wait until it is answered, and reply.
                    {"block": 9, "kind": "do", "object": "dialogue", "member": "ask",
                     "values": [text("Elder: Shall I tell you about the road?"),
                                text("Yes, please|No, thanks")]},
                    {"block": 10, "kind": "wait_until",
                     "values": [{"kind": "not", "operands": [
                         {"kind": "call", "object": "dialogue", "member": "is_open"}]}]},
                    {"block": 11, "kind": "if",
                     "values": [{"kind": "compare", "op": "equal",
                                 "operands": [{"kind": "call", "object": "dialogue",
                                               "member": "answer"}, text(1)]}],
                     "body": [say(12, "Elder: Follow the path east, through the gate. "
                                      "Home is not far.")],
                     "otherwise": [say(13, "Elder: Safe travels, then.")]}]},
            {"block": 7, "object": "gate", "event": "entered",
             "do": [say(8, "The gate creaks. Beyond it, the road home.")]},
        ],
        "brains": [
            {"block": 20, "name": "weather",
             "states": [
                 {"block": 21, "name": "sunny",
                  "enter": [log(22, "The sun is out."),
                            light(27, "ambient", 0.55), light(28, "sun", 0.45)],
                  "go": [{"block": 23, "to": "rain", "after": 30}]},
                 {"block": 24, "name": "rain",
                  "enter": [log(25, "It starts to rain."),
                            light(29, "ambient", 0.25), light(30, "sun", 0.1)],
                  "go": [{"block": 26, "to": "sunny", "after": 15}]},
             ]},
        ],
    }
    write_json(assets / "logic/main.drules", logic)

    # Moving between the two scenes: through the gate to the road, and back.
    def go(block, scene_path, where):
        return {"block": block, "kind": "do", "object": "game", "member": "go_to_scene_at",
                "values": [text(scene_path), text(where)]}

    road_logic = {
        "format": "sparxel.logic",
        "version": 1,
        "name": "road",
        "rules": [
            {"block": 1, "object": "gate", "event": "entered",
             "do": [go(2, "maps/road.dscene", "milestone")]},
            {"block": 3, "object": "way_back", "event": "entered",
             "do": [go(4, "maps/village.dscene", "lantern")]},
            {"block": 5, "event": "scene_start",
             "if": [{"kind": "call", "object": "game", "member": "in_scene",
                       "operands": [text("maps/road.dscene")]}],
             "do": [say(6, "The road home. A stream runs beside it.")],
             "otherwise": [say(7, "The village again.")]},
        ],
    }
    write_json(assets / "logic/road.drules", road_logic)

    for path, guid in GUIDS.items():
        importer = "image" if path.endswith(".png") else "file"
        write_json(assets / (path + ".meta"),
                   {"format": "sparxel.meta", "version": 1, "guid": guid, "importer": importer})

    write_json(ROOT / "project.sparxel.json", {
        "format": "sparxel.project",
        "version": 1,
        "name": "Hello Sparxel",
        "start_scene": "maps/village.dscene",
        "render_style": "classic_2d",
        "ppu": 16,
        "width": 320,
        "height": 180,
        "background": [24, 28, 44],
        "camera_pitch": 35,
        "camera_distance": 12,
        "camera_fov": 30,
        # The HD-2D look: in the Diorama style, a tilt-shift band keeps the middle of the
        # screen sharp; everywhere, a little bloom, warmth and a soft vignette.
        "look": {"tilt_shift": True, "focus_center": 0.55, "focus_height": 0.4, "blur": 2.5,
                 "blur_ramp": 0.3, "bloom": 0.35, "bloom_threshold": 0.7, "bloom_radius": 5,
                 "contrast": 1.05, "saturation": 1.1, "vignette": 0.3},
    })


if __name__ == "__main__":
    main()
