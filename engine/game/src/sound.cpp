#include "engine/game/sound.h"

#include "engine/game/behaviours.h"
#include "engine/game/logic_world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

namespace engine::game {

std::string AudioObject::prepare(const std::string& sound, bool stream) {
    if (engine_ == nullptr || assets_ == nullptr) {
        return {};
    }
    const std::optional<Guid> guid = Guid::parse(sound);
    const assets::AssetRecord* record = guid ? assets_->find(*guid) : assets_->find(sound);
    if (record == nullptr) {
        problems_.push_back("There is no sound " + sound + " in assets/.");
        return {};
    }
    // One key per file, loaded the way it is first used: the engine plays either kind as
    // a sound or as music.
    std::string key = record->guid.to_string();
    if (engine_->is_loaded(key)) {
        return key;
    }
    std::optional<std::vector<std::uint8_t>> bytes = assets_->read(record->guid);
    if (!bytes) {
        problems_.push_back(record->path + " cannot be read.");
        return {};
    }
    if (!(stream ? engine_->load_stream(key, std::move(*bytes))
                 : engine_->load(key, std::move(*bytes)))) {
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

void AudioObject::play_sound_at(const std::string& sound, const std::string& where) {
    const world::Entity entity = world_ != nullptr ? resolve(*world_, where) : world::Entity{};
    const scene2d::Transform2D* at =
        entity.is_valid() ? world_->get<scene2d::Transform2D>(entity) : nullptr;
    if (engine_ == nullptr) {
        return;
    }
    if (at == nullptr) {
        problems_.push_back("There is no one called " + where + " to play a sound at.");
        return;
    }
    const std::string key = prepare(sound);
    if (key.empty()) {
        return;
    }
    // Heard from the player; without one, from where the sound is.
    const std::array<double, 2> ear =
        player_position(*world_).value_or(std::array<double, 2>{at->x, at->y});
    const double dx = at->x - ear[0];
    const double distance = std::hypot(dx, at->y - ear[1]);
    const double volume = std::clamp(1.0 - (distance / hearing_range), 0.0, 1.0);
    if (volume > 0.0) {
        engine_->play_sound(key, volume, 1.0, std::clamp(dx / full_pan, -1.0, 1.0));
    }
}

void AudioObject::play_music(const std::string& music, double fade_seconds) {
    if (const std::string key = prepare(music, true); !key.empty()) {
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
    for (const std::vector<Guid>* changed : {&changes.modified, &changes.removed}) {
        for (const Guid guid : *changed) {
            engine_->unload(guid.to_string());
        }
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
    object.action<&AudioObject::play_sound_at>("play_sound_at", {"sound", "where"})
        .text("Play sound {sound} at {where}")
        .tooltip("Plays a sound effect where someone is: quieter the further they are from "
                 "the player, and from their side.");
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
