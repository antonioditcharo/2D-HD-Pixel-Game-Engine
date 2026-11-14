# Audio

> **Status: v0.3.** Sound effects and looping music from WAV, Ogg Vorbis, FLAC and MP3
> assets, mixed into Music and Sounds buses under a master volume, with fades and
> crossfades; music streamed as it plays; sounds placed in the world, panned and quieter
> with distance; and no-code actions to play them. Tested by mixing headless and checking the samples.
>
> Code: [`engine/audio/include/engine/audio/audio.h`](../../engine/audio/include/engine/audio/audio.h),
> [`src/audio.cpp`](../../engine/audio/src/audio.cpp), and the logic object in
> [`engine/game/include/engine/game/sound.h`](../../engine/game/include/engine/game/sound.h).
> Tests: [`tests/audio/audio_test.cpp`](../../tests/audio/audio_test.cpp) and the sound
> test in [`tests/game/runtime_test.cpp`](../../tests/game/runtime_test.cpp).

## 1. What it is for

The sound of a game: footsteps, chimes, menu clicks, and music that changes as the player
walks into a town or a battle starts. ADR-009 chose **miniaudio** (public domain or MIT-0)
for mixing and output; Sparxel builds its buses, music handling and no-code actions on
top. miniaudio's types never appear in a header, and only `src/audio.cpp` includes it.

## 2. Using it

```cpp
std::string error;
auto audio = audio::AudioEngine::create({}, error);     // the sound card; null if none
audio->load("chime", wav_bytes);                        // WAV, Ogg Vorbis, FLAC or MP3
audio->play_sound("chime", /*volume*/ 0.8, /*pitch*/ 1.0);
audio->load("town", music_bytes);
audio->play_music("town", /*fade_seconds*/ 1.0);       // crossfades from the music before
audio->set_volume(audio::Bus::Music, 0.5);
```

| Call | What it does |
|---|---|
| `load(key, bytes)` / `unload(key)` | Decodes a sound into memory under a key (the runtime uses the asset GUID). Loading a key again replaces it, for hot reload; unloading stops it wherever it plays |
| `load_stream(key, bytes)` | Keeps only the file's bytes, to be decoded as it plays: for long music, whose decoded form would fill memory. Each voice has its own decoder, so a stream can play twice at once and loop |
| `play_sound(key, volume, pitch, pan)` | Plays once on the Sounds bus. Pitch 2 is an octave up and twice as fast; pan -1 is the left speaker only, 1 the right only (balance: the other side gets quieter) |
| `play_loop(key, volume, pan)`, `set_loop(id, volume, pan)`, `stop_loop(id)` | A sound on repeat on the Sounds bus, named by the id `play_loop` returns (0 if it could not start), whose volume and pan can change as its listener moves. `playing_loops()` counts them; unloading a sound stops its loops |
| `play_music(key, fade)` | Loops on the Music bus. The music before fades out over the same time as this fades in. Playing the music already playing does nothing, so a Rule can ask for the town music every time the player enters |
| `stop_music(fade)` | Fades out, or stops at once with 0 |
| `set_volume(bus, v)`, `set_master_volume(v)` | 0 to 1, clamped. Sound volume, bus volume and master volume multiply |
| `render(out)` | Headless only: mixes the next frames into a buffer |

## 3. How it works

- **Decoding up front.** `load()` decodes the whole sound with miniaudio's decoder into
  floats at the engine's rate and channel count, so playing never touches the disk or
  waits. Each playing voice reads that shared data through its own buffer reference.
- **Ogg Vorbis** is decoded by stb_vorbis (public domain or MIT), compiled into
  `src/audio.cpp` beside miniaudio, which then reads `.ogg` files like any other. Its
  implementation is included at the very end of the file, because it defines short
  macros that must not reach Sparxel's code.
  miniaudio's resource manager, and its loading threads, are compiled out.
- **Streaming:** a streamed key is read through once when loaded (a file that fails
  half-way would fail while playing), then each voice decodes it with its own miniaudio
  decoder over the shared bytes, looping by seeking back. Decoded and streamed voices
  mix alike, so music can crossfade from one kind to the other.
- **Limits for hostile files:** bytes that are not a sound, or decode to nothing, are
  refused with a message; a sound longer than 30 minutes is refused rather than filling
  memory.
- **Buses** are miniaudio sound groups created **without pitch or spatialisation**. A
  group with pitch has a resampler, and its history replayed the last sample of a stopped
  voice into the next voice started on the bus; found by a test, which now guards it.
- **Latency:** a voice starts one frame (about 0.02 ms) after it is asked to, the cost of
  the resampler that lets its pitch change. Tests check levels away from the first and
  last few frames of each sound.
- **Fades** are linear in volume, over the given time, using miniaudio's per-sound fader.
- **No sound card:** `create()` fails with a message, and the game runtime then carries on
  silent (§4).

## 4. Sound in games

The runtime opens audio as `RuntimeOptions::audio` says: `Device` (the default),
`Headless` (tests) or `Off`. Without a sound output the game runs silent, with an issue
saying so.

Logic uses the **Audio** object (`audio`):

| Action | Text |
|---|---|
| `play_sound(sound)` | *Play sound {sound}* |
| `play_sound_at(sound, where)` | *Play sound {sound} at {where}*: where someone is, heard from the player (the first Top-down Mover, which the camera follows): 1 − distance / 16 as loud (silent 16 tiles away) and panned by how far to the side, fully at 8 tiles |
| `play_music(music, fade)` | *Play music {music}, fading over {fade} seconds* |
| `stop_music(fade)` | *Stop the music, fading over {fade} seconds* |
| `set_music_volume(percent)`, `set_sound_volume(percent)` | *Set the music volume to {percent}%*, 0 to 100 |

A **Sound Source** component (`sound_source`: `sound`, `volume` 1, `range` 8 tiles,
`playing`, `fade` 0.5 seconds) repeats a sound where its entity is: a waterfall, a fire, a crowd. Each step
the runtime sets its loop to how the player hears it now: `volume` × (1 − distance /
`range`), silent `range` tiles away, and panned by how far to the side it is (fully at 8
tiles), as `heard()` works out. It starts when it first plays, stops when `playing` is
turned off or its entity goes, starts again if its sound is changed, and a sound that
cannot be played is reported once. It fades in over `fade` seconds as it starts, and out
from where it was as it stops (turned off, or its entity gone); 0 starts and stops it at
once. In the editor, a sound dragged into the scene becomes
one ([editor.md](editor.md)).

- Sounds are named by **asset GUID** (what the editor writes) or by **path** in `assets/`
  (for logic written by hand). Each is read the first time it plays: sound effects are
  decoded whole, music is streamed. A file is loaded once, the way it is first used, and
  plays either way after that.
- A sound that does not exist or cannot be decoded is reported in `Runtime::problems()`,
  in plain language with its path; the game goes on.
- `AudioObject::apply(changes)` forgets sounds whose files changed, so they are read again
  next time (hot reload).


## The sound effect generator

**File ▸ New sound effect…** in the editor opens a generator of retro sound effects, as
sfxr and its descendants make them (`plan/02` J2). A button for each kind (**Coin**,
**Jump**, **Hit**, **Explosion**, **Power-up**, **Laser**, **Blip**) makes a sound of that
kind and plays it; each click makes another, different one of the kind. **Mutate** nudges
every number a little for a variation. Every number can be changed by hand: the **wave**
(square, saw, sine, triangle or noise), the envelope (**attack**, **sustain**, **decay**),
the **pitch** and how it **slides** (octaves a second, up or down), **vibrato**, the square
wave's **duty** and its sweep, a **jump** in pitch partway through (a coin's two notes),
**brightness** (a low-pass filter) and **volume**. **Save to the project** writes a mono
16-bit WAV, `assets/sfx/<name>.wav`, which shows in the assets at once, ready to drag into
the scene or play from logic. Names that are taken or would leave the folder are refused.

The model is [`editor/core/sfx.h`](../../editor/core/include/editor/core/sfx.h), without Qt:
`make()` (a preset and a seed, so the same seed gives the same sound), `mutate()`,
`clamped()` (every number within its range, at most 5 s long), `synthesize()` (samples from
the numbers alone: the same numbers always give the same samples) and `to_wav()`. The
window plays through the engine's own audio, so what is heard is what the game plays.
Tests check that the same seed gives the same sound and another seed another, that the
length is the envelope's and starts and ends quiet, that a jump rises, that numbers are kept
in range, and the WAV's form; the window's test makes an explosion, mutates it and saves it
into a project.

## 5. Tests

All mix headless and read the samples, so they run anywhere, with no sound card:

| Test | Checks |
|---|---|
| Sounds | A 0.1 s sound plays once at exactly its level; volume, bus and master volume multiply; two sounds add; pitch 2 ends in half the time |
| Music | Loops without a gap; playing it again does not restart it; the bus volume; stopping; a one-second fade in (silent, half, full); a crossfade (old full, both half, new alone); a fade out |
| Ogg Vorbis | Half a second of a 440 Hz tone at 0.5 ([`tests/audio/data/`](../../tests/audio/data/), made with ffmpeg and libvorbis) decodes and plays: over its middle 0.4 s the RMS level is that of a sine of 0.5 (to 3%), the peak 0.5 (to 5%), and there are 176 cycles; then silence, with no voice left |
| Streaming | A streamed tenth of a second loops twenty times over two seconds with no gap, at exactly its level; crossfades with decoded music to the same samples as two decoded ones; plays twice at once as a sound; unloading stops it; junk and silence refused; a streamed Ogg tone has the decoded one's loudness |
| Panning | Centre, all left, half right, clamped beyond 1 and the middle for not-a-number, each channel exactly as balance says |
| Loops | A looping sound repeats without a gap at its volume; its volume and pan change while it plays; a second loop has its own id; unknown ids do nothing; stopping one is silence at once, and unloading the sound stops the rest |
| Sound sources | `heard()`: full beside it, half at half its range, in the middle above or below, to the side by x / 8, silent at its range and beyond, scaled by its own volume (clamped), silent when not playing or with no range. In the runtime: a door 5 tiles west hums at exactly half level, 5/8 to the left, and keeps repeating; after the hero walks 4 tiles towards it, at 0.9 and 1/8 to the left, still one loop; its volume halves it; turned off it is silent and stopped, and on again it starts; a missing sound is one problem, not one a step; destroying the entity stops it |
| Bad sounds | Junk bytes, nothing, and a sound with no samples refused with messages; playing unknown keys refused; unloading stops a playing sound and the music; loading a key again replaces it |
| In a game | A Rule on game start plays the theme by path; Interact's Rule plays a chime over it (0.5 + 0.25); missing and unreadable sounds reported; volumes in percent clamped; no engine, no problems. A chime at the door 5 tiles west of the hero: left 0.25 × 0.6875, right that × 0.375; out of hearing, nothing; nobody by that name, a problem; music streamed |


## Footsteps

As the player walks, a footstep sounds every **Step length** tiles (the Top-down Mover's,
0.6 by default): the sound of the ground under their feet, the **Footsteps** of a tilemap
with a tile there (grass, stone, wood boards: each its own map, or the same map), or else
the mover's own **Footsteps**, or nothing. Each step's pitch differs a little, as feet do.
Being moved by more than walking (arriving through a door) makes no step, and nothing
counts during messages, menus and battles.

## Ducking

While a message shows, the music eases down to **Music while a message shows** (the
Project view's *Menus and messages*, `music_while_talking`, 0.6 of itself by default; 1
leaves it) over a quarter of a second, and back up the same way when the last message
closes, so words and speakers' voice blips come through. It works on top of the game's
music volume and the player's own (Options), without changing either
(`AudioEngine::set_music_duck()`).

## 6. Next increments

1. **Snapshots:** whole mixes to switch between (a cave, underwater).
2. **A falloff curve to choose** for sound sources (now straight).
3. **Audio zones** and **adaptive music** (ADR-009).
