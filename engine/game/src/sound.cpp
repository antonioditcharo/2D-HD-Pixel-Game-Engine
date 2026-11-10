#include "engine/game/sound.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace engine::game {

std::string AudioObject::prepare(const std::string& sound) {
    if (engine_ == nullptr || assets_ == nullptr) {
        return {};
    }
    const std::optional<Guid> guid = Guid::parse(sound);
    const assets::AssetRecord* record = guid ? assets_->find(*guid) : assets_->find(sound);
    if (record == nullptr) {
        problems_.push_back("There is no sound " + sound + " in assets/.");
        return {};
    }
    std::string key = record->guid.to_string();
    if (engine_->is_loaded(key)) {
        return key;
    }
    std::optional<std::vector<std::uint8_t>> bytes = assets_->read(record->guid);
    if (!bytes) {
        problems_.push_back(record->path + " cannot be read.");
        return {};
    }
    if (!engine_->load(key, std::move(*bytes))) {
        problems_.push_back(record->path + ": " + engine_->last_error());
        return {};
    }
    return key;
}

void AudioObject::play_sound(const std::string& sound) {
    if (const std::string key = prepare(sound); !key.empty()) {
        engine_->play_sound(key);
    }
}

void AudioObject::play_music(const std::string& music, double fade_seconds) {
    if (const std::string key = prepare(music); !key.empty()) {
        engine_->play_music(key, fade_seconds);
    }
}

void AudioObject::stop_music(double fade_seconds) {
    if (engine_ != nullptr) {
        engine_->stop_music(fade_seconds);
    }
}

void AudioObject::set_music_volume(std::int64_t percent) {
    if (engine_ != nullptr) {
        engine_->set_volume(audio::Bus::Music,
                            static_cast<double>(std::clamp<std::int64_t>(percent, 0, 100)) / 100.0);
    }
}

void AudioObject::set_sound_volume(std::int64_t percent) {
    if (engine_ != nullptr) {
        engine_->set_volume(audio::Bus::Sounds,
                            static_cast<double>(std::clamp<std::int64_t>(percent, 0, 100)) / 100.0);
    }
}

void AudioObject::apply(const assets::AssetChanges& changes) {
    if (engine_ == nullptr) {
        return;
    }
    for (const Guid guid : changes.modified) {
        engine_->unload(guid.to_string());
    }
    for (const Guid guid : changes.removed) {
        engine_->unload(guid.to_string());
    }
}

std::vector<std::string> AudioObject::take_problems() {
    return std::exchange(problems_, {});
}

void register_audio_types(reflect::Registry& registry) {
    auto object =
        registry.add_type<AudioObject>("audio").display_name("Audio").category("Audio").tooltip(
            "Sound effects and music.");
    object.action<&AudioObject::play_sound>("play_sound", {"sound"})
        .text("Play sound {sound}")
        .tooltip("Plays a sound effect once.");
    object.action<&AudioObject::play_music>("play_music", {"music", "fade"})
        .text("Play music {music}, fading over {fade} seconds")
        .tooltip("Plays music on repeat. Music already playing fades out as this fades in.");
    object.action<&AudioObject::stop_music>("stop_music", {"fade"})
        .text("Stop the music, fading over {fade} seconds")
        .tooltip("Fades the music out. 0 stops it at once.");
    object.action<&AudioObject::set_music_volume>("set_music_volume", {"percent"})
        .text("Set the music volume to {percent}%")
        .tooltip("From 0 (silent) to 100 (full).");
    object.action<&AudioObject::set_sound_volume>("set_sound_volume", {"percent"})
        .text("Set the sound effects volume to {percent}%")
        .tooltip("From 0 (silent) to 100 (full).");
}

} // namespace engine::game
