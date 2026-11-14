// Audio mixed headless, so every test reads the exact output instead of listening: levels,
// buses, looping music, fades, pitch, Ogg Vorbis and bad files.

#include "engine/audio/audio.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "support/wav.h"

namespace audio = engine::audio;

namespace {

constexpr std::uint32_t rate = 48000;

using test_support::wav;

std::unique_ptr<audio::AudioEngine> make_engine() {
    std::string error;
    auto engine = audio::AudioEngine::create(audio::AudioOptions{true, rate, 2}, error);
    INFO(error);
    REQUIRE(engine != nullptr);
    return engine;
}

/// Mixes `frames` frames and returns the left channel.
std::vector<float> mix(audio::AudioEngine& engine, std::uint32_t frames) {
    std::vector<float> out(std::size_t{frames} * 2, -9.0F);
    REQUIRE(engine.render(out));
    std::vector<float> left;
    left.reserve(frames);
    for (std::uint32_t i = 0; i < frames; ++i) {
        left.push_back(out[std::size_t{i} * 2]);
        CHECK(out[(std::size_t{i} * 2) + 1] == out[std::size_t{i} * 2]); // both channels
    }
    return left;
}

/// Frames ignored at each edge of a checked range. A voice starts one frame late (the
/// resampler that lets its pitch change has one frame of latency), so levels are checked
/// away from the edges, where they must be exact to 0.001.
constexpr std::size_t edge = 4;

/// True if every sample in [from + edge, to - edge) is within 0.001 of `level`.
bool flat(const std::vector<float>& samples, std::size_t from, std::size_t to, float level) {
    for (std::size_t i = from + edge; i + edge < to; ++i) {
        if (std::abs(samples[i] - level) > 0.001F) {
            MESSAGE("sample " << i << " is " << samples[i] << ", not " << level);
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("Sounds play once at their volume, on their bus, and mix") {
    const auto engine = make_engine();
    CHECK(engine->sample_rate() == rate);
    CHECK(engine->channels() == 2);
    REQUIRE(engine->load("half", wav(4800, 16384))); // 0.1 s at 0.5
    REQUIRE(engine->load("quarter", wav(2400, 8192)));
    CHECK(engine->is_loaded("half"));

    REQUIRE(engine->play_sound("half"));
    CHECK(engine->playing_sounds() == 1);
    std::vector<float> out = mix(*engine, 6000);
    CHECK(flat(out, 0, 4800, 0.5F));
    CHECK(flat(out, 4800, 6000, 0.0F)); // played once
    CHECK(engine->playing_sounds() == 0);

    // Volume, bus volume and master volume multiply; two sounds add up.
    REQUIRE(engine->play_sound("half", 0.5));
    CHECK(flat(mix(*engine, 4800), 0, 4800, 0.25F));
    engine->set_volume(audio::Bus::Sounds, 0.5);
    engine->set_master_volume(0.5);
    CHECK(engine->volume(audio::Bus::Sounds) == 0.5);
    REQUIRE(engine->play_sound("half"));
    CHECK(flat(mix(*engine, 4800), 0, 4800, 0.125F));
    engine->set_volume(audio::Bus::Sounds, 7.0); // clamped to 1
    engine->set_master_volume(1.0);
    CHECK(engine->volume(audio::Bus::Sounds) == 1.0);
    REQUIRE(engine->play_sound("half"));
    REQUIRE(engine->play_sound("quarter"));
    out = mix(*engine, 4800);
    CHECK(flat(out, 0, 2400, 0.75F));
    CHECK(flat(out, 2400, 4800, 0.5F));

    // Twice the pitch: twice as fast, so it ends after half the time.
    REQUIRE(engine->play_sound("half", 1.0, 2.0));
    out = mix(*engine, 4800);
    CHECK(flat(out, 100, 2300, 0.5F));
    CHECK(flat(out, 2500, 4800, 0.0F));
}

TEST_CASE("Music loops, fades and crossfades") {
    const auto engine = make_engine();
    REQUIRE(engine->load("theme", wav(4800, 16384)));
    REQUIRE(engine->load("battle", wav(4800, 8192)));

    REQUIRE(engine->play_music("theme"));
    CHECK(engine->music() == "theme");
    const std::vector<float> looped = mix(*engine, 3 * 4800);
    CHECK(flat(looped, 0, looped.size(), 0.5F)); // loops without a gap
    REQUIRE(engine->play_music("theme"));        // already playing: no restart
    engine->set_volume(audio::Bus::Music, 0.5);
    CHECK(flat(mix(*engine, 4800), 0, 4800, 0.25F));
    engine->set_volume(audio::Bus::Music, 1.0);
    engine->stop_music();
    CHECK(engine->music().empty());
    CHECK(flat(mix(*engine, 4800), 0, 4800, 0.0F));

    // A one-second fade in: silent at first, half way at 0.5 s, full after 1 s.
    REQUIRE(engine->play_music("theme", 1.0));
    std::vector<float> out = mix(*engine, rate * 2);
    CHECK(std::abs(out[0]) < 0.01F);
    CHECK(std::abs(out[rate / 2] - 0.25F) < 0.01F);
    CHECK(flat(out, rate + 10, out.size(), 0.5F));

    // Crossfade to the battle music: the theme fades out as the battle fades in.
    REQUIRE(engine->play_music("battle", 1.0));
    CHECK(engine->music() == "battle");
    out = mix(*engine, rate * 2);
    CHECK(std::abs(out[0] - 0.5F) < 0.01F);                    // the theme, still full
    CHECK(std::abs(out[rate / 2] - (0.25F + 0.125F)) < 0.01F); // half of each
    CHECK(flat(out, rate + 10, out.size(), 0.25F));            // the battle alone

    // Fading out to silence.
    engine->stop_music(0.5);
    out = mix(*engine, rate);
    CHECK(flat(out, rate / 2 + 10, rate, 0.0F));
}

TEST_CASE("Ogg Vorbis sounds decode and play") {
    // Half a second of a 440 Hz tone at 0.5 in both channels, made with ffmpeg and
    // libvorbis (tests/audio/data/README.md). Vorbis is lossy, so the tone is checked by
    // its loudness and pitch rather than sample by sample.
    std::ifstream file(SPARXEL_AUDIO_TEST_DATA "/tone-440hz.ogg", std::ios::binary);
    REQUIRE(file);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());
    REQUIRE(bytes.size() > 1000);

    const auto engine = make_engine();
    REQUIRE(engine->load("tone", bytes));
    REQUIRE(engine->play_sound("tone"));
    const std::vector<float> out = mix(*engine, rate); // a second: the tone, then silence

    // The middle of the tone, away from the encoder's edges: 0.4 s.
    const std::size_t from = rate / 20;
    const std::size_t to = rate * 9 / 20;
    double squares = 0.0;
    float peak = 0.0F;
    int rises = 0; // upward zero crossings: one per cycle
    for (std::size_t i = from; i < to; ++i) {
        squares += double{out[i]} * out[i];
        peak = std::max(peak, std::abs(out[i]));
        rises += (out[i - 1] < 0.0F && out[i] >= 0.0F) ? 1 : 0;
    }
    const double rms = std::sqrt(squares / static_cast<double>(to - from));
    CHECK(rms == doctest::Approx(0.5 / std::sqrt(2.0)).epsilon(0.03)); // a sine of 0.5
    CHECK(peak == doctest::Approx(0.5).epsilon(0.05));
    CHECK(rises >= 175); // 440 Hz for 0.4 s: 176 cycles
    CHECK(rises <= 177);

    // Then it ends: silence, and no voice left.
    CHECK(flat(out, rate * 11 / 20, rate, 0.0F));
    CHECK(engine->playing_sounds() == 0);
}

TEST_CASE("Streamed music plays as decoded music does, from the file's bytes") {
    const auto engine = make_engine();
    // A tenth of a second at 0.5, looping: twenty times over two seconds, with no gap.
    REQUIRE(engine->load_stream("theme", wav(4800, 16384)));
    CHECK(engine->is_loaded("theme"));
    REQUIRE(engine->play_music("theme"));
    CHECK(flat(mix(*engine, rate * 2), 0, std::size_t{rate} * 2, 0.5F));
    // Crossfading to decoded music, and back to the stream: both kinds mix alike.
    REQUIRE(engine->load("battle", wav(4800, 8192)));
    REQUIRE(engine->play_music("battle", 1.0));
    std::vector<float> out = mix(*engine, rate * 2);
    CHECK(std::abs(out[rate / 2] - (0.25F + 0.125F)) < 0.01F);
    CHECK(flat(out, rate + 10, out.size(), 0.25F));
    engine->stop_music();
    CHECK(flat(mix(*engine, 480), 0, 480, 0.0F));
    // A stream can be a sound too, and played twice at once: each voice decodes its own.
    REQUIRE(engine->play_sound("theme"));
    REQUIRE(engine->play_sound("theme"));
    CHECK(flat(mix(*engine, 2400), 0, 2400, 1.0F));
    // Unloading stops it; bad bytes and silence are refused.
    engine->unload("theme");
    CHECK_FALSE(engine->is_loaded("theme"));
    CHECK(engine->playing_sounds() == 0);
    const std::vector<std::uint8_t> junk{'n', 'o', 'p', 'e'};
    CHECK_FALSE(engine->load_stream("junk", junk));
    CHECK(engine->last_error() ==
          "junk is not a sound miniaudio can read (WAV, Ogg Vorbis, FLAC or MP3).");
    CHECK_FALSE(engine->load_stream("silent", wav(0, 0)));
    CHECK(engine->last_error() == "silent has no sound in it.");

    // An Ogg Vorbis stream: the same tone as decoded (see "Ogg Vorbis sounds...").
    std::ifstream file(SPARXEL_AUDIO_TEST_DATA "/tone-440hz.ogg", std::ios::binary);
    REQUIRE(file);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());
    REQUIRE(engine->load_stream("tone", bytes));
    REQUIRE(engine->play_sound("tone"));
    out = mix(*engine, rate / 2);
    double squares = 0.0;
    const std::size_t from = rate / 20; // the middle 0.4 s, as for the decoded tone
    const std::size_t to = std::size_t{rate} * 9 / 20;
    for (std::size_t i = from; i < to; ++i) {
        squares += double{out[i]} * out[i];
    }
    const double rms = std::sqrt(squares / static_cast<double>(to - from));
    CHECK(rms == doctest::Approx(0.5 / std::sqrt(2.0)).epsilon(0.03));
}

TEST_CASE("Panning moves a sound between the speakers") {
    const auto engine = make_engine();
    REQUIRE(engine->load("beep", wav(4800, 16384))); // 0.5 in both channels
    const auto stereo = [&](double pan) {
        REQUIRE(engine->play_sound("beep", 1.0, 1.0, pan));
        std::vector<float> out(std::size_t{480} * 2, -9.0F);
        REQUIRE(engine->render(out));
        engine->unload("beep");
        REQUIRE(engine->load("beep", wav(4800, 16384)));
        constexpr std::size_t middle = 200; // a frame mid-way
        return std::array<float, 2>{out[middle * 2], out[(middle * 2) + 1]};
    };
    const std::array<float, 2> centre = stereo(0.0);
    CHECK(centre[0] == doctest::Approx(0.5));
    CHECK(centre[1] == doctest::Approx(0.5));
    // Balance: towards one side, the other side gets quieter; at the end, silent.
    const std::array<float, 2> left = stereo(-1.0);
    CHECK(left[0] == doctest::Approx(0.5));
    CHECK(left[1] == doctest::Approx(0.0));
    const std::array<float, 2> right_half = stereo(0.5);
    CHECK(right_half[0] == doctest::Approx(0.25));
    CHECK(right_half[1] == doctest::Approx(0.5));
    // Out of range is clamped; not a number is the middle.
    const std::array<float, 2> far = stereo(9.0);
    CHECK(far[0] == doctest::Approx(0.0));
    CHECK(far[1] == doctest::Approx(0.5));
    const std::array<float, 2> nan = stereo(std::nan(""));
    CHECK(nan[0] == doctest::Approx(0.5));
    CHECK(nan[1] == doctest::Approx(0.5));
}

TEST_CASE("Looping sounds repeat until stopped, at a volume and pan that can change") {
    const auto engine = make_engine();
    REQUIRE(engine->load("hum", wav(2400, 16384))); // 0.05 s at 0.5
    CHECK(engine->play_loop("missing") == 0);
    const std::uint64_t hum = engine->play_loop("hum", 0.5);
    REQUIRE(hum != 0);
    CHECK(engine->playing_loops() == 1);
    CHECK(engine->playing_sounds() == 0); // not a one-shot
    // Three times its length, without a gap: it repeats.
    CHECK(flat(mix(*engine, 7200), 0, 7200, 0.25F));
    // Louder, as the listener comes closer; then off to the right.
    engine->set_loop(hum, 1.0, 0.0);
    CHECK(flat(mix(*engine, 2400), 0, 2400, 0.5F));
    engine->set_loop(hum, 1.0, 1.0);
    std::vector<float> out(std::size_t{480} * 2, -9.0F);
    REQUIRE(engine->render(out));
    constexpr std::size_t middle = 200; // a frame mid-way
    CHECK(out[middle * 2] == doctest::Approx(0.0));
    CHECK(out[(middle * 2) + 1] == doctest::Approx(0.5));
    // A second loop has its own name; unknown names do nothing.
    const std::uint64_t other = engine->play_loop("hum", 0.0);
    CHECK(other != 0);
    CHECK(other != hum);
    CHECK(engine->playing_loops() == 2);
    engine->set_loop(9999, 1.0, 0.0);
    engine->stop_loop(9999);
    CHECK(engine->playing_loops() == 2);
    // Stopped: silence at once. Unloading the sound stops the loops still playing it.
    engine->stop_loop(hum);
    CHECK(engine->playing_loops() == 1);
    CHECK(flat(mix(*engine, 2400), 0, 2400, 0.0F));
    engine->unload("hum");
    CHECK(engine->playing_loops() == 0);
}

TEST_CASE("Bad sounds are refused, and unloading stops a sound") {
    const auto engine = make_engine();
    const std::vector<std::uint8_t> junk{'n', 'o', 't', ' ', 'a', ' ', 's', 'o', 'u', 'n', 'd'};
    CHECK_FALSE(engine->load("junk", junk));
    CHECK(engine->last_error() ==
          "junk is not a sound miniaudio can read (WAV, Ogg Vorbis, FLAC or MP3).");
    CHECK_FALSE(engine->load("empty", {}));
    CHECK_FALSE(engine->load("silent", wav(0, 0)));
    CHECK_FALSE(engine->play_sound("nothing"));
    CHECK(engine->last_error() == "There is no loaded sound nothing.");
    CHECK_FALSE(engine->play_music("nothing"));

    REQUIRE(engine->load("long", wav(48000, 16384)));
    REQUIRE(engine->play_sound("long"));
    REQUIRE(engine->play_music("long"));
    CHECK(flat(mix(*engine, 480), 0, 480, 1.0F)); // the sound and the music
    engine->unload("long");
    CHECK_FALSE(engine->is_loaded("long"));
    CHECK(engine->music().empty());
    CHECK(engine->playing_sounds() == 0);
    CHECK(flat(mix(*engine, 480), 0, 480, 0.0F));

    // Loading a key again replaces the sound.
    REQUIRE(engine->load("beep", wav(480, 16384)));
    REQUIRE(engine->load("beep", wav(480, 8192)));
    REQUIRE(engine->play_sound("beep"));
    CHECK(flat(mix(*engine, 480), 0, 480, 0.25F));
}
