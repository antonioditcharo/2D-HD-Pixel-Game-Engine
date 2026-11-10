# Audio

> **Status: v0.1.** Sound effects and looping music from WAV, FLAC and MP3 assets, mixed
> into Music and Sounds buses under a master volume, with fades and crossfades, and
> no-code actions to play them. Tested by mixing headless and checking the samples.
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
audio->load("chime", wav_bytes);                        // decodes WAV, FLAC or MP3
audio->play_sound("chime", /*volume*/ 0.8, /*pitch*/ 1.0);
audio->load("town", music_bytes);
audio->play_music("town", /*fade_seconds*/ 1.0);       // crossfades from the music before
audio->set_volume(audio::Bus::Music, 0.5);
```

| Call | What it does |
|---|---|
| `load(key, bytes)` / `unload(key)` | Decodes a sound into memory under a key (the runtime uses the asset GUID). Loading a key again replaces it, for hot reload; unloading stops it wherever it plays |
| `play_sound(key, volume, pitch)` | Plays once on the Sounds bus. Pitch 2 is an octave up and twice as fast |
| `play_music(key, fade)` | Loops on the Music bus. The music before fades out over the same time as this fades in. Playing the music already playing does nothing, so a Rule can ask for the town music every time the player enters |
| `stop_music(fade)` | Fades out, or stops at once with 0 |
| `set_volume(bus, v)`, `set_master_volume(v)` | 0 to 1, clamped. Sound volume, bus volume and master volume multiply |
| `render(out)` | Headless only: mixes the next frames into a buffer |

## 3. How it works

- **Decoding up front.** `load()` decodes the whole sound with miniaudio's decoder into
  floats at the engine's rate and channel count, so playing never touches the disk or
  waits. Each playing voice reads that shared data through its own buffer reference.
  miniaudio's resource manager, and its loading threads, are compiled out.
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
| `play_music(music, fade)` | *Play music {music}, fading over {fade} seconds* |
| `stop_music(fade)` | *Stop the music, fading over {fade} seconds* |
| `set_music_volume(percent)`, `set_sound_volume(percent)` | *Set the music volume to {percent}%*, 0 to 100 |

- Sounds are named by **asset GUID** (what the editor writes) or by **path** in `assets/`
  (for logic written by hand). Each is read and decoded the first time it plays.
- A sound that does not exist or cannot be decoded is reported in `Runtime::problems()`,
  in plain language with its path; the game goes on.
- `AudioObject::apply(changes)` forgets sounds whose files changed, so they are read again
  next time (hot reload).

## 5. Tests

All mix headless and read the samples, so they run anywhere, with no sound card:

| Test | Checks |
|---|---|
| Sounds | A 0.1 s sound plays once at exactly its level; volume, bus and master volume multiply; two sounds add; pitch 2 ends in half the time |
| Music | Loops without a gap; playing it again does not restart it; the bus volume; stopping; a one-second fade in (silent, half, full); a crossfade (old full, both half, new alone); a fade out |
| Bad sounds | Junk bytes, nothing, and a sound with no samples refused with messages; playing unknown keys refused; unloading stops a playing sound and the music; loading a key again replaces it |
| In a game | A Rule on game start plays the theme by path; Interact's Rule plays a chime over it (0.5 + 0.25); missing and unreadable sounds reported; volumes in percent clamped; no engine, no problems |

## 6. Next increments

1. **Ogg Vorbis**, through an added, permissively licensed decoder (ADR-009).
2. **Streaming** long music from disk instead of decoding it all, once memory use matters.
3. **Snapshots and ducking:** lowering the music while a character speaks.
4. **Positional sound** for the Diorama style: sounds placed in the world, quieter with
   distance.
5. **Footstep surfaces**, **zones** and **adaptive music** (ADR-009).
