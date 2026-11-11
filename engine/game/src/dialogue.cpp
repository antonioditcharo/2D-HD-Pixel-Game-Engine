#include "engine/game/dialogue.h"

namespace engine::game {

void register_dialogue_types(reflect::Registry& registry) {
    auto dialogue = registry.add_type<DialogueObject>("dialogue")
                        .display_name("Dialogue")
                        .category("Story")
                        .tooltip("Words on screen: what characters say.");
    dialogue.action<&DialogueObject::say>("say", {"text"})
        .text("Say {text}")
        .tooltip("Shows a message box. The player reads it and presses Interact to go on.");
    dialogue.condition<&DialogueObject::is_open>("is_open")
        .text("A message is open")
        .tooltip("Whether a message box is on screen.");
    dialogue.event<>("closed")
        .text("When the message closes")
        .tooltip("Happens when the player closes the last message.");
}

} // namespace engine::game
