// Audio mixed headless, so every test reads the exact output instead of listening: levels,
// buses, looping music, fades, pitch and bad files.

#include "engine/audio/audio.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
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

TEST_CASE("Bad sounds are refused, and unloading stops a sound") {
    const auto engine = make_engine();
    const std::vector<std::uint8_t> junk{'n', 'o', 't', ' ', 'a', ' ', 's', 'o', 'u', 'n', 'd'};
    CHECK_FALSE(engine->load("junk", junk));
    CHECK(engine->last_error() == "junk is not a sound miniaudio can read (WAV, FLAC or MP3).");
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
