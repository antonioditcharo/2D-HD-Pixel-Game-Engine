# Audio test data

| File | What it is | Made with |
|---|---|---|
| `tone-440hz.ogg` | Half a second of a 440 Hz sine at 0.5, stereo, 48 kHz, Ogg Vorbis | `ffmpeg -f lavfi -i "aevalsrc=0.5*sin(2*PI*440*t)\|0.5*sin(2*PI*440*t):s=48000:d=0.5" -c:a libvorbis -q:a 4 -fflags +bitexact -flags:a +bitexact tone-440hz.ogg` |

Made for these tests and dedicated to the public domain (CC0).
