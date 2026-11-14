#include "engine/audio/audio.h"

// The only file that sees miniaudio. Sounds are decoded up front into float PCM at the
// engine's rate and channel count; each voice plays from its own buffer reference over that
// shared PCM, so no resource manager (and none of its loading threads) is involved.
#ifdef _WIN32
// miniaudio includes <windows.h>, whose min and max macros would break std::min and std::max.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_GENERATION
// Ogg Vorbis: miniaudio decodes it when stb_vorbis's declarations come first; the
// implementation follows at the end of this file, as miniaudio's documentation asks.
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c> // NOLINT(bugprone-suspicious-include): stb ships it as a .c file
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace engine::audio {

namespace {

/// Decoded sound longer than this is refused, so a hostile file cannot fill memory.
constexpr double max_sound_seconds = 30.0 * 60.0;

[[nodiscard]] float clamp_volume(double volume) {
    return std::isfinite(volume) ? static_cast<float>(std::clamp(volume, 0.0, 1.0)) : 0.0F;
}

[[nodiscard]] std::string describe(const char* what, ma_result result) {
    return std::string(what) + ": " + ma_result_description(result) + ".";
}

/// A sound decoded to interleaved floats.
struct Pcm {
    std::vector<float> samples;
    ma_uint64 frames = 0;
};

/// One playing instance of a sound.
struct Voice {
    Voice() = default;
    Voice(const Voice&) = delete;
    Voice& operator=(const Voice&) = delete;
    Voice(Voice&&) = delete;
    Voice& operator=(Voice&&) = delete;
    ~Voice() {
        if (sound_ready) {
            ma_sound_uninit(&sound);
        }
        if (ref_ready) {
            ma_audio_buffer_ref_uninit(&ref);
        }
        if (decoder_ready) {
            ma_decoder_uninit(&decoder);
        }
    }

    std::string key;
    std::shared_ptr<const Pcm> pcm; ///< Kept alive while the voice plays.
    ma_audio_buffer_ref ref{};
    /// A streamed sound: its own decoder through the file's bytes, kept alive with it.
    std::shared_ptr<const std::vector<std::uint8_t>> file;
    ma_decoder decoder{};
    ma_sound sound{};
    bool ref_ready = false;
    bool decoder_ready = false;
    bool sound_ready = false;
};

} // namespace

struct AudioEngine::Impl {
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() {
        // Voices first: they belong to the groups and the engine.
        sounds.clear();
        loops.clear();
        music.reset();
        fading.clear();
        if (groups_ready) {
            ma_sound_group_uninit(&sound_group);
            ma_sound_group_uninit(&music_group);
        }
        if (engine_ready) {
            ma_engine_uninit(&engine);
        }
    }

    /// A voice for `key` on `group`, ready to start; null with `error` set.
    std::unique_ptr<Voice> voice(std::string_view key, ma_sound_group* group) {
        const auto found = decoded.find(key);
        const auto streamed = streams.find(key);
        if (found == decoded.end() && streamed == streams.end()) {
            error = "There is no loaded sound " + std::string(key) + ".";
            return nullptr;
        }
        auto made = std::make_unique<Voice>();
        made->key = std::string(key);
        ma_data_source* source = nullptr;
        if (found != decoded.end()) {
            made->pcm = found->second;
            const ma_result result = ma_audio_buffer_ref_init(
                ma_format_f32, channels, made->pcm->samples.data(), made->pcm->frames, &made->ref);
            if (result != MA_SUCCESS) {
                error = describe("Preparing a sound failed", result);
                return nullptr;
            }
            made->ref_ready = true;
            made->ref.sampleRate = sample_rate;
            source = &made->ref;
        } else {
            made->file = streamed->second;
            const ma_decoder_config config =
                ma_decoder_config_init(ma_format_f32, channels, sample_rate);
            const ma_result result = ma_decoder_init_memory(made->file->data(), made->file->size(),
                                                            &config, &made->decoder);
            if (result != MA_SUCCESS) {
                error = describe("Preparing a sound failed", result);
                return nullptr;
            }
            made->decoder_ready = true;
            source = &made->decoder;
        }
        const ma_result result = ma_sound_init_from_data_source(
            &engine, source, MA_SOUND_FLAG_NO_SPATIALIZATION, group, &made->sound);
        if (result != MA_SUCCESS) {
            error = describe("Preparing a sound failed", result);
            return nullptr;
        }
        made->sound_ready = true;
        return made;
    }

    /// Drops one-shot sounds that have finished and music that has faded out.
    void collect() {
        std::erase_if(sounds, [](const std::unique_ptr<Voice>& v) {
            return ma_sound_at_end(&v->sound) == MA_TRUE || ma_sound_is_playing(&v->sound) == 0;
        });
        std::erase_if(fading, [](const std::unique_ptr<Voice>& v) {
            return ma_sound_is_playing(&v->sound) == 0;
        });
    }

    ma_engine engine{};
    ma_sound_group music_group{};
    ma_sound_group sound_group{};
    bool engine_ready = false;
    bool groups_ready = false;
    bool headless = false;
    std::uint32_t sample_rate = 48000;
    std::uint32_t channels = 2;
    float master = 1.0F;
    float music_volume = 1.0F;
    float sound_volume = 1.0F;
    float player_music = 1.0F;
    float player_sounds = 1.0F;
    float duck = 1.0F; ///< The music's ducking.
    std::map<std::string, std::shared_ptr<const Pcm>, std::less<>> decoded;
    std::map<std::string, std::shared_ptr<const std::vector<std::uint8_t>>, std::less<>> streams;
    std::vector<std::unique_ptr<Voice>> sounds;
    std::map<std::uint64_t, std::unique_ptr<Voice>> loops; ///< Sounds on repeat, by id.
    std::uint64_t next_loop = 1;
    std::unique_ptr<Voice> music;
    std::string music_key; ///< music's key, or empty: kept beside it for music().
    std::vector<std::unique_ptr<Voice>> fading; ///< Earlier music, fading out.
    std::string error;
};

AudioEngine::AudioEngine(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

AudioEngine::~AudioEngine() = default;

std::unique_ptr<AudioEngine> AudioEngine::create(const AudioOptions& options, std::string& error) {
    auto impl = std::make_unique<Impl>();
    impl->headless = options.headless;
    impl->sample_rate = std::clamp<std::uint32_t>(options.sample_rate, 8000, 192000);
    impl->channels = std::clamp<std::uint32_t>(options.channels, 1, 8);

    ma_engine_config config = ma_engine_config_init();
    config.noDevice = options.headless ? MA_TRUE : MA_FALSE;
    config.channels = impl->channels;
    config.sampleRate = impl->sample_rate;
    ma_result result = ma_engine_init(&config, &impl->engine);
    if (result != MA_SUCCESS) {
        error = describe("Starting audio failed", result);
        return nullptr;
    }
    impl->engine_ready = true;
    // With a device, it may have chosen its own format; mix in what it uses.
    impl->sample_rate = ma_engine_get_sample_rate(&impl->engine);
    impl->channels = ma_engine_get_channels(&impl->engine);

    // Buses only sum and scale: without a pitch resampler they have no latency and keep
    // no history, which would otherwise replay the last samples of a stopped voice into
    // the next one started on the bus.
    constexpr ma_uint32 group_flags = MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION;
    result = ma_sound_group_init(&impl->engine, group_flags, nullptr, &impl->music_group);
    if (result == MA_SUCCESS) {
        result = ma_sound_group_init(&impl->engine, group_flags, nullptr, &impl->sound_group);
        if (result != MA_SUCCESS) {
            ma_sound_group_uninit(&impl->music_group);
        }
    }
    if (result != MA_SUCCESS) {
        error = describe("Creating the audio buses failed", result);
        return nullptr;
    }
    impl->groups_ready = true;
    return std::unique_ptr<AudioEngine>(new AudioEngine(std::move(impl)));
}

bool AudioEngine::load(std::string_view key, std::vector<std::uint8_t> bytes) {
    Impl& a = *impl_;
    const ma_decoder_config config =
        ma_decoder_config_init(ma_format_f32, a.channels, a.sample_rate);
    ma_decoder decoder;
    const ma_result result = ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder);
    if (result != MA_SUCCESS) {
        a.error =
            std::string(key) + " is not a sound miniaudio can read (WAV, Ogg Vorbis, FLAC or MP3).";
        return false;
    }
    auto pcm = std::make_shared<Pcm>();
    const auto limit = static_cast<ma_uint64>(max_sound_seconds * a.sample_rate);
    std::vector<float> chunk(std::size_t{4096} * a.channels);
    bool too_long = false;
    for (;;) {
        ma_uint64 read = 0;
        const ma_result step = ma_decoder_read_pcm_frames(&decoder, chunk.data(), 4096, &read);
        if (read > 0) {
            pcm->samples.insert(pcm->samples.end(), chunk.begin(),
                                chunk.begin() + static_cast<std::ptrdiff_t>(read * a.channels));
            pcm->frames += read;
        }
        if (pcm->frames > limit) {
            too_long = true;
            break;
        }
        if (step != MA_SUCCESS || read == 0) {
            break;
        }
    }
    ma_decoder_uninit(&decoder);
    if (too_long) {
        a.error = std::string(key) + " is longer than 30 minutes.";
        return false;
    }
    if (pcm->frames == 0) {
        a.error = std::string(key) + " has no sound in it.";
        return false;
    }
    unload(key);
    a.decoded.emplace(std::string(key), std::move(pcm));
    return true;
}

bool AudioEngine::load_stream(std::string_view key, std::vector<std::uint8_t> bytes) {
    Impl& a = *impl_;
    // Read it through once: a stream that fails half-way would fail while playing.
    const ma_decoder_config config =
        ma_decoder_config_init(ma_format_f32, a.channels, a.sample_rate);
    ma_decoder decoder;
    if (ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder) != MA_SUCCESS) {
        a.error =
            std::string(key) + " is not a sound miniaudio can read (WAV, Ogg Vorbis, FLAC or MP3).";
        return false;
    }
    ma_uint64 frames = 0;
    const bool counted = ma_decoder_get_length_in_pcm_frames(&decoder, &frames) == MA_SUCCESS;
    ma_decoder_uninit(&decoder);
    if (!counted || frames == 0) {
        a.error = std::string(key) + " has no sound in it.";
        return false;
    }
    unload(key);
    a.streams.emplace(std::string(key),
                      std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes)));
    return true;
}

bool AudioEngine::is_loaded(std::string_view key) const {
    return impl_->decoded.contains(key) || impl_->streams.contains(key);
}

void AudioEngine::unload(std::string_view key) {
    Impl& a = *impl_;
    std::erase_if(a.loops, [key](const auto& loop) { return loop.second->key == key; });
    std::erase_if(a.sounds, [key](const std::unique_ptr<Voice>& v) { return v->key == key; });
    std::erase_if(a.fading, [key](const std::unique_ptr<Voice>& v) { return v->key == key; });
    if (a.music != nullptr && a.music->key == key) {
        a.music.reset();
        a.music_key.clear();
    }
    if (const auto found = a.decoded.find(key); found != a.decoded.end()) {
        a.decoded.erase(found);
    }
    if (const auto found = a.streams.find(key); found != a.streams.end()) {
        a.streams.erase(found);
    }
}

bool AudioEngine::play_sound(std::string_view key, double volume, double pitch, double pan) {
    Impl& a = *impl_;
    a.collect();
    std::unique_ptr<Voice> voice = a.voice(key, &a.sound_group);
    if (voice == nullptr) {
        return false;
    }
    ma_sound_set_volume(&voice->sound,
                        std::isfinite(volume) ? static_cast<float>(std::max(volume, 0.0)) : 0.0F);
    ma_sound_set_pitch(&voice->sound,
                       std::isfinite(pitch) && pitch > 0.0 ? static_cast<float>(pitch) : 1.0F);
    ma_sound_set_pan(&voice->sound,
                     std::isfinite(pan) ? static_cast<float>(std::clamp(pan, -1.0, 1.0)) : 0.0F);
    if (ma_sound_start(&voice->sound) != MA_SUCCESS) {
        a.error = "Starting a sound failed.";
        return false;
    }
    a.sounds.push_back(std::move(voice));
    return true;
}

bool AudioEngine::play_music(std::string_view key, double fade_seconds) {
    Impl& a = *impl_;
    a.collect();
    if (a.music != nullptr && a.music->key == key) {
        return true;
    }
    std::unique_ptr<Voice> voice = a.voice(key, &a.music_group);
    if (voice == nullptr) {
        return false;
    }
    const auto fade = static_cast<ma_uint64>(
        std::isfinite(fade_seconds) ? std::max(fade_seconds, 0.0) * 1000.0 : 0.0);
    ma_sound_set_looping(&voice->sound, MA_TRUE);
    if (fade > 0) {
        ma_sound_set_fade_in_milliseconds(&voice->sound, 0.0F, 1.0F, fade);
    }
    stop_music(fade_seconds);
    if (ma_sound_start(&voice->sound) != MA_SUCCESS) {
        a.error = "Starting the music failed.";
        return false;
    }
    a.music = std::move(voice);
    a.music_key = std::string(key);
    return true;
}

void AudioEngine::stop_music(double fade_seconds) {
    Impl& a = *impl_;
    if (a.music == nullptr) {
        return;
    }
    const auto fade = static_cast<ma_uint64>(
        std::isfinite(fade_seconds) ? std::max(fade_seconds, 0.0) * 1000.0 : 0.0);
    a.music_key.clear();
    if (fade == 0) {
        a.music.reset();
        return;
    }
    ma_sound_stop_with_fade_in_milliseconds(&a.music->sound, fade);
    a.fading.push_back(std::move(a.music));
}

const std::string& AudioEngine::music() const noexcept {
    return impl_->music_key;
}

void AudioEngine::set_volume(Bus bus, double volume) {
    Impl& a = *impl_;
    const float v = clamp_volume(volume);
    if (bus == Bus::Music) {
        a.music_volume = v;
        ma_sound_group_set_volume(&a.music_group, v * a.player_music * a.duck);
    } else {
        a.sound_volume = v;
        ma_sound_group_set_volume(&a.sound_group, v * a.player_sounds);
    }
}

double AudioEngine::volume(Bus bus) const noexcept {
    return bus == Bus::Music ? impl_->music_volume : impl_->sound_volume;
}

void AudioEngine::set_player_volume(Bus bus, double volume) {
    Impl& a = *impl_;
    (bus == Bus::Music ? a.player_music : a.player_sounds) = clamp_volume(volume);
    set_volume(bus, this->volume(bus));
}

void AudioEngine::set_music_duck(double level) {
    impl_->duck = clamp_volume(level);
    set_volume(Bus::Music, volume(Bus::Music));
}

double AudioEngine::music_duck() const noexcept {
    return impl_->duck;
}

double AudioEngine::player_volume(Bus bus) const noexcept {
    return bus == Bus::Music ? impl_->player_music : impl_->player_sounds;
}

void AudioEngine::set_master_volume(double volume) {
    impl_->master = clamp_volume(volume);
    ma_engine_set_volume(&impl_->engine, impl_->master);
}

double AudioEngine::master_volume() const noexcept {
    return impl_->master;
}

std::uint64_t AudioEngine::play_loop(std::string_view key, double volume, double pan) {
    Impl& a = *impl_;
    std::unique_ptr<Voice> voice = a.voice(key, &a.sound_group);
    if (voice == nullptr) {
        return 0;
    }
    ma_sound_set_looping(&voice->sound, MA_TRUE);
    const std::uint64_t id = a.next_loop++;
    Voice& started = *a.loops.emplace(id, std::move(voice)).first->second;
    set_loop(id, volume, pan);
    if (ma_sound_start(&started.sound) != MA_SUCCESS) {
        a.loops.erase(id);
        a.error = "Starting a sound failed.";
        return 0;
    }
    return id;
}

void AudioEngine::set_loop(std::uint64_t loop, double volume, double pan) {
    Impl& a = *impl_;
    const auto found = a.loops.find(loop);
    if (found == a.loops.end()) {
        return;
    }
    ma_sound_set_volume(&found->second->sound,
                        std::isfinite(volume) ? static_cast<float>(std::max(volume, 0.0)) : 0.0F);
    ma_sound_set_pan(&found->second->sound,
                     std::isfinite(pan) ? static_cast<float>(std::clamp(pan, -1.0, 1.0)) : 0.0F);
}

void AudioEngine::stop_loop(std::uint64_t loop) {
    impl_->loops.erase(loop);
}

std::size_t AudioEngine::playing_loops() const {
    return impl_->loops.size();
}

std::size_t AudioEngine::playing_sounds() const {
    impl_->collect();
    return impl_->sounds.size();
}

bool AudioEngine::render(std::span<float> out) {
    Impl& a = *impl_;
    if (!a.headless) {
        return false;
    }
    const ma_uint64 frames = out.size() / a.channels;
    ma_uint64 read = 0;
    const ma_result result = ma_engine_read_pcm_frames(&a.engine, out.data(), frames, &read);
    std::fill(out.begin() + static_cast<std::ptrdiff_t>(read * a.channels), out.end(), 0.0F);
    a.collect();
    return result == MA_SUCCESS || result == MA_AT_END;
}

std::uint32_t AudioEngine::sample_rate() const noexcept {
    return impl_->sample_rate;
}

std::uint32_t AudioEngine::channels() const noexcept {
    return impl_->channels;
}

const std::string& AudioEngine::last_error() const noexcept {
    return impl_->error;
}

} // namespace engine::audio

// stb_vorbis's implementation, last: it defines short macros that must not reach our code.
// MSVC's optimizer reports "potentially uninitialized" locals inside it (C4701, C4703);
// those warnings come from code generation, which /external:W0 does not cover, so they are
// turned off for stb_vorbis's functions alone.
#undef STB_VORBIS_HEADER_ONLY
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(push)
#pragma warning(disable : 4701 4703)
#endif
#include <stb_vorbis.c> // NOLINT(bugprone-suspicious-include)
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(pop)
#endif
