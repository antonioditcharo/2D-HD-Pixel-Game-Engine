#include "engine/game/hud.h"

namespace engine::game {

std::string fill_values(std::string_view text,
                        const std::function<std::string(std::string_view)>& value) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '{' && i + 1 < text.size() && text[i + 1] == '{') {
            out += '{';
            ++i;
            continue;
        }
        if (c != '{') {
            out += c;
            continue;
        }
        const std::size_t end = text.find('}', i + 1);
        if (end == std::string_view::npos) {
            out.append(text.substr(i));
            break;
        }
        out += value(text.substr(i + 1, end - i - 1));
        i = end;
    }
    return out;
}

void register_hud(reflect::Registry& registry) {
    registry.add_choice_list<ScreenAnchor>("screen_anchor")
        .display_name("Place on the screen")
        .entry(ScreenAnchor::TopLeft, "top_left")
        .entry(ScreenAnchor::Top, "top")
        .entry(ScreenAnchor::TopRight, "top_right")
        .entry(ScreenAnchor::Left, "left")
        .entry(ScreenAnchor::Middle, "middle")
        .entry(ScreenAnchor::Right, "right")
        .entry(ScreenAnchor::BottomLeft, "bottom_left")
        .entry(ScreenAnchor::Bottom, "bottom")
        .entry(ScreenAnchor::BottomRight, "bottom_right");
    auto text = registry.add_type<ScreenText>("screen_text")
                    .display_name("Screen Text")
                    .category("Interface")
                    .tooltip("Words fixed to the screen, not the map: the gold carried, a "
                             "score. Values in braces are filled as the game plays.");
    text.property<&ScreenText::text>("text").tooltip(
        "The words, with values in braces: {gold} (or any item carried), {hp:hero}, "
        "{max_hp:hero}, {mp:hero}, {level:hero}, {var:score} (a logic variable), {time} (the "
        "time of day), {timer:race} (seconds left).");
    text.property<&ScreenText::anchor>("anchor").display_name("Place").tooltip(
        "The corner, side or middle of the screen it is fixed to.");
    text.property<&ScreenText::x>("x")
        .range(0.0, 1000.0)
        .step(1.0)
        .tooltip("Pixels in from the place's side.");
    text.property<&ScreenText::y>("y")
        .range(0.0, 1000.0)
        .step(1.0)
        .tooltip("Pixels in from the place's top or bottom.");
    text.property<&ScreenText::size>("size").range(1.0, 2.0).tooltip("2: twice as large.");
    text.property<&ScreenText::visible>("visible").tooltip("Hidden, for logic to show later.");
    auto bar = registry.add_type<ScreenBar>("screen_bar")
                   .display_name("Screen Bar")
                   .category("Interface")
                   .tooltip("A bar fixed to the screen, filled as far as a value is towards "
                            "its most: the hero's HP, a boss's, a timer.");
    bar.property<&ScreenBar::value>("value").tooltip(
        "What fills it, as Screen Text's values: {hp:hero}, {var:boss_hp}, {timer:race}.");
    bar.property<&ScreenBar::most>("most").tooltip(
        "The value of a full bar: {max_hp:hero}, or a number such as 100.");
    bar.property<&ScreenBar::anchor>("anchor").display_name("Place").tooltip(
        "The corner, side or middle of the screen it is fixed to.");
    bar.property<&ScreenBar::x>("x")
        .range(0.0, 1000.0)
        .step(1.0)
        .tooltip("Pixels in from the place's side.");
    bar.property<&ScreenBar::y>("y")
        .range(0.0, 1000.0)
        .step(1.0)
        .tooltip("Pixels in from the place's top or bottom.");
    bar.property<&ScreenBar::width>("width").range(4.0, 1000.0).step(1.0);
    bar.property<&ScreenBar::height>("height").range(1.0, 100.0).step(1.0);
    bar.property<&ScreenBar::color>("color").display_name("Colour").tooltip(
        "The fill, as #rrggbb.");
    bar.property<&ScreenBar::visible>("visible").tooltip("Hidden, for logic to show later.");
}

bool register_hud_components(world::World& world) {
    const bool text = world.register_component<ScreenText>();
    const bool bar = world.register_component<ScreenBar>();
    return text && bar;
}

} // namespace engine::game
