// Walkers: characters who walk by themselves, and the paths they find (behaviours.h).

#include "engine/game/behaviours.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <queue>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::game {

namespace {

/// How near a point counts as reaching it, in tiles.
constexpr double arrive_within = 0.02;
/// How far around the start and the goal the way is searched, in tiles.
constexpr std::int64_t search_margin = 64;
/// The most tiles searched for one way, so a far or walled-off goal never stalls a step.
constexpr std::size_t most_searched = 40000;
/// Following: how often the way to who is followed is found again, in seconds.
constexpr double follow_repath = 0.5;
/// How long a walker may stand stuck (pushed against something) before it gives up the
/// way and finds another, in seconds.
constexpr double give_up_after = 0.75;

/// A point of a route ("2,3; 8,3"), or nothing if the text is not one.
std::optional<std::array<double, 2>> parse_point(std::string_view text) {
    const auto trim = [](std::string_view s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
            s.remove_prefix(1);
        }
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
            s.remove_suffix(1);
        }
        return s;
    };
    const std::size_t comma = text.find(',');
    if (comma == std::string_view::npos) {
        return std::nullopt;
    }
    std::array<double, 2> point{};
    const std::array<std::string_view, 2> parts{trim(text.substr(0, comma)),
                                                trim(text.substr(comma + 1))};
    for (std::size_t i = 0; i < 2; ++i) {
        const std::string_view part = parts.at(i);
        const auto [end, error] =
            std::from_chars(part.data(), part.data() + part.size(), point.at(i));
        if (error != std::errc{} || end != part.data() + part.size() ||
            !std::isfinite(point.at(i))) {
            return std::nullopt;
        }
    }
    return point;
}

std::vector<std::array<double, 2>> parse_route(std::string_view text) {
    std::vector<std::array<double, 2>> points;
    while (!text.empty()) {
        const std::size_t semicolon = text.find(';');
        if (const auto point = parse_point(text.substr(0, semicolon))) {
            points.push_back(*point);
        }
        if (semicolon == std::string_view::npos) {
            break;
        }
        text.remove_prefix(semicolon + 1);
    }
    return points;
}

/// A random number from xorshift32: each walker's choices replay the same every run.
std::uint32_t next_random(std::uint32_t& state) {
    if (state == 0) {
        state = 0x9E3779B9U;
    }
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

} // namespace

void Walker::walk_to(double x, double y) {
    mode = WalkMode::GoTo;
    goal_x = x;
    goal_y = y;
    path.clear();
    waiting = 0.0;
}

void Walker::follow(const std::string& name) {
    mode = WalkMode::Follow;
    target = name;
    path.clear();
    repath = 0.0;
}

void Walker::wander() {
    mode = WalkMode::Wander;
    homed = false; // near where it is now
    path.clear();
    waiting = 0.0;
}

void Walker::patrol() {
    mode = WalkMode::Patrol;
    leg = 0;
    path.clear();
    waiting = 0.0;
}

void Walker::stand() {
    mode = WalkMode::Stand;
    path.clear();
}

std::optional<std::vector<std::array<double, 2>>>
find_path(const world::World& world, const TopDownMover& mover, world::Entity self, double x,
          double y, double to_x, double to_y, std::uint32_t ppu, world::Entity ignore) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(to_x) || !std::isfinite(to_y)) {
        return std::nullopt;
    }
    if (std::abs(to_x - x) <= arrive_within && std::abs(to_y - y) <= arrive_within) {
        return std::vector<std::array<double, 2>>{};
    }
    // Solid Bodies where they stand now.
    std::vector<scene2d::Box> bodies;
    if (const reflect::TypeInfo* type = world.registry().find_type<Body>()) {
        world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
            const Body* body = ref.as<Body>();
            const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(entity);
            if (body != nullptr && at != nullptr && body->enabled && entity != self &&
                entity != ignore) {
                bodies.push_back(feet_box(at->x, at->y, body->width, body->height));
            }
        });
    }
    // Tiles: a character's hitbox stands in the middle of one, its feet below the middle.
    const double half = mover.hitbox_height / 2.0;
    const auto cell_of = [half](double px, double py) {
        return std::array<std::int64_t, 2>{static_cast<std::int64_t>(std::floor(px)),
                                           static_cast<std::int64_t>(std::floor(py - half))};
    };
    const auto feet_of = [half](std::int64_t cx, std::int64_t cy) {
        return std::array<double, 2>{static_cast<double>(cx) + 0.5,
                                     static_cast<double>(cy) + 0.5 + half};
    };
    const std::array<std::int64_t, 2> start = cell_of(x, y);
    const std::array<std::int64_t, 2> goal = cell_of(to_x, to_y);
    const std::int64_t left = std::min(start[0], goal[0]) - search_margin;
    const std::int64_t top = std::min(start[1], goal[1]) - search_margin;
    const std::int64_t width = std::abs(start[0] - goal[0]) + (2 * search_margin) + 1;
    const std::int64_t height = std::abs(start[1] - goal[1]) + (2 * search_margin) + 1;
    if (width > 4 * search_margin || height > 4 * search_margin) {
        return std::nullopt; // farther than is searched
    }
    const auto index = [&](std::int64_t cx, std::int64_t cy) {
        return static_cast<std::size_t>(((cy - top) * width) + (cx - left));
    };
    const auto inside = [&](std::int64_t cx, std::int64_t cy) {
        return cx >= left && cy >= top && cx < left + width && cy < top + height;
    };
    // Whether the character fits in a tile, worked out once per tile.
    std::vector<std::int8_t> fits(static_cast<std::size_t>(width * height), -1);
    const auto open = [&](std::int64_t cx, std::int64_t cy) {
        if (!inside(cx, cy)) {
            return false;
        }
        std::int8_t& known = fits[index(cx, cy)];
        if (known < 0) {
            const auto [fx, fy] = feet_of(cx, cy);
            const scene2d::Box box = hitbox(mover, fx, fy);
            bool free = !scene2d::solid_at(world, box, ppu);
            for (const scene2d::Box& body : bodies) {
                free = free && !scene2d::overlaps(box, body);
            }
            known = free ? 1 : 0;
        }
        return known == 1;
    };
    if (!open(goal[0], goal[1])) {
        return std::nullopt;
    }

    // A*, eight ways, never across a wall's corner; octile distance to the goal.
    constexpr double diagonal = 1.4142135623730951;
    const auto guess = [&](std::int64_t cx, std::int64_t cy) {
        const auto ax = static_cast<double>(std::abs(cx - goal[0]));
        const auto ay = static_cast<double>(std::abs(cy - goal[1]));
        return std::max(ax, ay) + ((diagonal - 1.0) * std::min(ax, ay));
    };
    std::vector<double> cost(fits.size(), std::numeric_limits<double>::infinity());
    std::vector<std::size_t> from(fits.size(), std::numeric_limits<std::size_t>::max());
    using Entry = std::pair<double, std::size_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> todo;
    const std::size_t first = index(start[0], start[1]);
    const std::size_t last = index(goal[0], goal[1]);
    cost[first] = 0.0;
    todo.emplace(guess(start[0], start[1]), first);
    std::size_t searched = 0;
    bool found = first == last;
    while (!todo.empty() && !found && searched < most_searched) {
        const auto [estimate, at] = todo.top();
        todo.pop();
        const auto cx = static_cast<std::int64_t>(at % static_cast<std::size_t>(width)) + left;
        const auto cy = static_cast<std::int64_t>(at / static_cast<std::size_t>(width)) + top;
        if (estimate > cost[at] + guess(cx, cy) + 1e-9) {
            continue; // a better way here was found already
        }
        ++searched;
        for (std::int64_t ny = -1; ny <= 1; ++ny) {
            for (std::int64_t nx = -1; nx <= 1; ++nx) {
                if ((nx == 0 && ny == 0) || !open(cx + nx, cy + ny)) {
                    continue;
                }
                if (nx != 0 && ny != 0 && (!open(cx + nx, cy) || !open(cx, cy + ny))) {
                    continue; // a corner
                }
                const std::size_t next = index(cx + nx, cy + ny);
                const double through = cost[at] + (nx != 0 && ny != 0 ? diagonal : 1.0);
                if (through + 1e-9 < cost[next]) {
                    cost[next] = through;
                    from[next] = at;
                    if (next == last) {
                        found = true;
                    }
                    todo.emplace(through + guess(cx + nx, cy + ny), next);
                }
            }
        }
    }
    if (!found) {
        return std::nullopt;
    }
    // Back from the goal; keep only the tiles where the way turns, then the goal itself.
    std::vector<std::array<std::int64_t, 2>> cells;
    for (std::size_t at = last; at != first; at = from[at]) {
        cells.push_back({static_cast<std::int64_t>(at % static_cast<std::size_t>(width)) + left,
                         static_cast<std::int64_t>(at / static_cast<std::size_t>(width)) + top});
    }
    std::reverse(cells.begin(), cells.end());
    std::vector<std::array<double, 2>> points;
    std::array<std::int64_t, 2> previous = start;
    for (std::size_t i = 0; i + 1 < cells.size(); ++i) {
        const std::array<std::int64_t, 2>& here = cells[i];
        const std::array<std::int64_t, 2>& next = cells[i + 1];
        const bool turns = (here[0] - previous[0] != next[0] - here[0]) ||
                           (here[1] - previous[1] != next[1] - here[1]);
        if (turns) {
            points.push_back(feet_of(here[0], here[1]));
        }
        previous = here;
    }
    // The way's first turn may be the start's own tile: walk to its middle first, so the
    // character does not clip a corner it was standing beside.
    if (!cells.empty() && (std::abs(x - feet_of(start[0], start[1])[0]) > 0.25 ||
                           std::abs(y - feet_of(start[0], start[1])[1]) > 0.25)) {
        points.insert(points.begin(), feet_of(start[0], start[1]));
    }
    points.push_back({to_x, to_y});
    return points;
}

std::vector<world::Entity> steer_walkers(world::World& world, double seconds, std::uint32_t ppu,
                                         bool paused) {
    std::vector<world::Entity> arrived;
    const reflect::TypeInfo* type = world.registry().find_type<Walker>();
    if (type == nullptr || !std::isfinite(seconds) || seconds < 0.0) {
        return arrived;
    }
    world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
        Walker* walker = ref.as<Walker>();
        const TopDownMover* mover = world.get<TopDownMover>(entity);
        const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(entity);
        if (walker == nullptr) {
            return;
        }
        walker->step_dx = 0.0;
        walker->step_dy = 0.0;
        walker->step_scale = 1.0;
        if (!walker->enabled || mover == nullptr || at == nullptr || !mover->enabled || paused) {
            return;
        }
        const double x = at->x;
        const double y = at->y;
        if (!walker->homed) {
            walker->home_x = x;
            walker->home_y = y;
            walker->homed = true;
        }
        if (walker->seed == 0) {
            const std::string guid = world.guid(entity).to_string();
            std::uint32_t hash = 2166136261U;
            for (const char c : guid) {
                hash = (hash ^ static_cast<std::uint8_t>(c)) * 16777619U;
            }
            walker->seed = hash == 0 ? 1U : hash;
        }
        const auto rest = [&] {
            walker->path.clear();
            walker->waiting = std::max(walker->rest, 0.0);
        };
        // Choose where to go when there is no way to walk.
        switch (walker->mode) {
        case WalkMode::Stand:
            walker->path.clear();
            return;
        case WalkMode::Wander:
        case WalkMode::Patrol:
            if (walker->path.empty()) {
                if (walker->waiting > 0.0) {
                    walker->waiting -= seconds;
                    return;
                }
                std::optional<std::array<double, 2>> goal;
                if (walker->mode == WalkMode::Wander) {
                    // A tile near home, tried a few times in case some are walls.
                    const double radius = std::clamp(walker->wander_radius, 0.0, 32.0);
                    for (int attempt = 0; attempt < 4 && !goal; ++attempt) {
                        const double a = (next_random(walker->seed) % 10000U) / 10000.0;
                        const double b = (next_random(walker->seed) % 10000U) / 10000.0;
                        // Whole tiles from home, so it stands as it stood there.
                        const std::array<double, 2> spot{
                            walker->home_x + std::round(((a * 2.0) - 1.0) * radius),
                            walker->home_y + std::round(((b * 2.0) - 1.0) * radius)};
                        if (auto way =
                                find_path(world, *mover, entity, x, y, spot[0], spot[1], ppu)) {
                            walker->path = std::move(*way);
                            goal = spot;
                        }
                    }
                } else {
                    const std::vector<std::array<double, 2>> route = parse_route(walker->route);
                    if (route.empty()) {
                        return;
                    }
                    const std::array<double, 2> spot = route[walker->leg % route.size()];
                    walker->leg = static_cast<std::uint32_t>((walker->leg + 1) % route.size());
                    if (auto way = find_path(world, *mover, entity, x, y, spot[0], spot[1], ppu)) {
                        walker->path = std::move(*way);
                        goal = spot;
                    }
                }
                if (!goal || walker->path.empty()) {
                    rest();
                    return;
                }
            }
            break;
        case WalkMode::Follow: {
            const std::vector<world::Entity> named = world.find_named(walker->target);
            const scene2d::Transform2D* leader =
                named.empty() ? nullptr : world.get<scene2d::Transform2D>(named.front());
            if (leader == nullptr || named.front() == entity) {
                walker->path.clear();
                return;
            }
            const double far = std::hypot(leader->x - x, leader->y - y);
            if (far <= std::max(walker->distance, arrive_within)) {
                walker->path.clear();
                return;
            }
            walker->repath -= seconds;
            if (walker->path.empty() || walker->repath <= 0.0) {
                walker->repath = follow_repath;
                walker->path =
                    find_path(world, *mover, entity, x, y, leader->x, leader->y, ppu, named.front())
                        .value_or(std::vector<std::array<double, 2>>{});
            }
            break;
        }
        case WalkMode::GoTo:
            if (walker->path.empty()) {
                if (std::abs(walker->goal_x - x) <= arrive_within &&
                    std::abs(walker->goal_y - y) <= arrive_within) {
                    walker->mode = WalkMode::Stand;
                    arrived.push_back(entity);
                    return;
                }
                const auto way =
                    find_path(world, *mover, entity, x, y, walker->goal_x, walker->goal_y, ppu);
                if (!way || way->empty()) {
                    walker->mode = WalkMode::Stand; // nowhere to go: it stays
                    return;
                }
                walker->path = *way;
            }
            break;
        }
        if (walker->path.empty()) {
            return;
        }
        // Points reached are passed; then towards the next, slowing so as not to overshoot.
        while (!walker->path.empty() && std::hypot(walker->path.front()[0] - x,
                                                   walker->path.front()[1] - y) <= arrive_within) {
            walker->path.erase(walker->path.begin());
        }
        if (walker->path.empty()) {
            if (walker->mode == WalkMode::GoTo) {
                walker->mode = WalkMode::Stand;
                arrived.push_back(entity);
            } else if (walker->mode != WalkMode::Follow) {
                rest();
            }
            return;
        }
        const double dx = walker->path.front()[0] - x;
        const double dy = walker->path.front()[1] - y;
        const double far = std::hypot(dx, dy);
        const double full = std::max(mover->speed, 0.0) * seconds;
        walker->step_dx = dx;
        walker->step_dy = dy;
        walker->step_scale = full > 0.0 ? std::min(1.0, far / full) : 0.0;
        // Pushed against something that moved into the way: give up the way after a while.
        const bool still =
            std::abs(x - walker->last_x) < 1e-6 && std::abs(y - walker->last_y) < 1e-6;
        walker->stuck = still ? walker->stuck + seconds : 0.0;
        walker->last_x = x;
        walker->last_y = y;
        if (walker->stuck > give_up_after) {
            walker->stuck = 0.0;
            if (walker->mode == WalkMode::GoTo || walker->mode == WalkMode::Follow) {
                walker->path.clear(); // found again next step
            } else {
                rest();
            }
            walker->step_dx = 0.0;
            walker->step_dy = 0.0;
        }
    });
    return arrived;
}

} // namespace engine::game
