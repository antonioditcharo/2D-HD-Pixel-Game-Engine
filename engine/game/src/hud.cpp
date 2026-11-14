#include "engine/game/hud.h"

#include <algorithm>

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

std::vector<SlicePiece> nine_slice(float width, float height, render::Rect destination,
                                   float border) {
    std::vector<SlicePiece> pieces;
    if (width <= 0.0F || height <= 0.0F || destination.w <= 0.0F || destination.h <= 0.0F) {
        return pieces;
    }
    // At most half of the picture, and of where it goes, on each side.
    const float b = std::max(0.0F, std::min({border, width / 2.0F, height / 2.0F,
                                             destination.w / 2.0F, destination.h / 2.0F}));
    if (b <= 0.0F) {
        pieces.push_back(SlicePiece{render::Rect{0.0F, 0.0F, width, height}, destination});
        return pieces;
    }
    // Columns and rows: the edge, the stretched middle, the other edge.
    const std::array<float, 3> source_x{0.0F, b, width - b};
    const std::array<float, 3> source_w{b, width - (2.0F * b), b};
    const std::array<float, 3> source_y{0.0F, b, height - b};
    const std::array<float, 3> source_h{b, height - (2.0F * b), b};
    const std::array<float, 3> to_x{destination.x, destination.x + b,
                                    destination.x + destination.w - b};
    const std::array<float, 3> to_w{b, destination.w - (2.0F * b), b};
    const std::array<float, 3> to_y{destination.y, destination.y + b,
                                    destination.y + destination.h - b};
    const std::array<float, 3> to_h{b, destination.h - (2.0F * b), b};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            if (source_w.at(column) <= 0.0F || source_h.at(row) <= 0.0F ||
                to_w.at(column) <= 0.0F || to_h.at(row) <= 0.0F) {
                continue;
            }
            pieces.push_back(SlicePiece{
                render::Rect{source_x.at(column), source_y.at(row), source_w.at(column),
                             source_h.at(row)},
                render::Rect{to_x.at(column), to_y.at(row), to_w.at(column), to_h.at(row)}});
        }
    }
    return pieces;
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
    auto picture = registry.add_type<ScreenPicture>("screen_picture")
                       .display_name("Screen Picture")
                       .category("Interface")
                       .tooltip("A picture fixed to the screen: an icon, a portrait, or a "
                                "frame behind Screen Text, stretched with crisp corners.");
    picture.property<&ScreenPicture::image>("image").tooltip("The picture to show.");
    picture.property<&ScreenPicture::anchor>("anchor").display_name("Place").tooltip(
        "The corner, side or middle of the screen it is fixed to.");
    picture.property<&ScreenPicture::x>("x")
        .range(0.0, 1000.0)
        .step(1.0)
        .tooltip("Pixels in from the place's side.");
    picture.property<&ScreenPicture::y>("y")
        .range(0.0, 1000.0)
        .step(1.0)
        .tooltip("Pixels in from the place's top or bottom.");
    picture.property<&ScreenPicture::width>("width")
        .range(0.0, 4000.0)
        .step(1.0)
        .tooltip("Pixels across; 0 keeps the picture's own width.");
    picture.property<&ScreenPicture::height>("height")
        .range(0.0, 4000.0)
        .step(1.0)
        .tooltip("Pixels down; 0 keeps the picture's own height.");
    picture.property<&ScreenPicture::border>("border")
        .display_name("Frame edge")
        .range(0.0, 256.0)
        .step(1.0)
        .tooltip("9-slice: the pixels at each edge drawn as they are while the middle "
                 "stretches, so a frame's corners stay crisp at any size. 0 stretches it "
                 "all.");
    picture.property<&ScreenPicture::opacity>("opacity").range(0.0, 1.0).step(0.1).tooltip(
        "1 is solid; 0.5 lets half of what is behind show through.");
    picture.property<&ScreenPicture::visible>("visible").tooltip(
        "Hidden, for logic to show later.");
}

bool register_hud_components(world::World& world) {
    const bool text = world.register_component<ScreenText>();
    const bool bar = world.register_component<ScreenBar>();
    const bool picture = world.register_component<ScreenPicture>();
    return text && bar && picture;
}

} // namespace engine::game
