#ifndef SIK_2022_COMMON_H
#define SIK_2022_COMMON_H

#include <arpa/inet.h>
#include <optional>
#include <vector>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <concepts>
#include <cassert>
#include <variant>

/* = = = *
 * TYPES *
 * = = = */

enum Direction {
    Up = 0,
    Right = 1,
    Down = 2,
    Left = 3
};

enum ClientMessageType {
    Join = 0,
    PlaceBomb = 1,
    PlaceBlock = 2,
    Move = 3
};

struct ClientMessage {
    ClientMessageType type;
    union {
        std::string name;
        Direction direction;
    };
};

typedef uint32_t BombId;
typedef uint8_t PlayerId;
typedef uint32_t Score;
typedef std::pair<uint16_t, uint16_t> Position; // <x, y>
typedef std::pair<Position, uint16_t> Bomb; // <position, timer>
typedef std::pair<std::string, std::string> Player; // <name, address>

enum EventType {
    BombPlaced = 0,
    BombExploded = 1,
    PlayerMoved = 2,
    BlockPlaced = 3
};

struct event_bomb_placed_t {
    BombId id;
    Position position;
};

struct event_bomb_exploded_t {
    BombId id;
    std::vector<PlayerId> robots_destroyed;
    std::vector<Position> blocks_destroyed;
};

struct event_player_moved_t {
    PlayerId id;
    Position position;
};

struct event_block_placed_t {
    Position position;
};

struct Event {
    EventType type;
    std::variant<event_bomb_placed_t, event_bomb_exploded_t, event_player_moved_t, event_block_placed_t> variant;
};

enum ServerMessageType {
    Hello = 0,
    AcceptedPlayer = 1,
    GameStarted = 2,
    Turn = 3,
    GameEnded = 4,
};

struct ServerMessage {
    ServerMessageType type;
    union {
        struct {
            std::string server_name;
            uint8_t players_count;
            uint16_t size_x;
            uint16_t size_y;
            uint16_t game_length;
            uint16_t explosion_radius;
            uint16_t bomb_timer;
        } hello;

        struct {
            PlayerId id;
            Player player;
        } accepted_player;

        struct {
            std::unordered_map<PlayerId, Player> players;
        } game_started;

        struct {
            uint16_t turn;
            std::vector<Event> events;
        } turn;

        struct {
            std::unordered_map<PlayerId, Score> scores;
        } game_ended;
    };
};

/* = = = *
 * PARSE *
 * = = = */

/* * * * * * *
 * concepts  *
 * * * * * * */

template<typename T>
concept Pair = requires(T t) {
    typename T::first_type;
    typename T::second_type;
    t.first;
    t.second;
    requires std::same_as<decltype(t.first), typename T::first_type>;
    requires std::same_as<decltype(t.second), typename T::second_type>;
};

template<typename T>
concept List = requires(T t, typename T::value_type e, size_t i) {
    typename T::value_type;
    t.push_back(e);
    { t.at(i) } -> std::convertible_to<typename T::value_type>;
    { t.size() } -> std::same_as<size_t>;
    requires !std::same_as<T, std::string>; // string hase its own definition of parse
};

template<typename T>
concept Map = requires(
    T t,
    typename T::value_type e,
    typename T::mapped_type v,
    typename T::key_type k
) {
    typename T::value_type;
    typename T::mapped_type;
    typename T::key_type;
    typename T::iterator;
    t.insert(e);
    { t.size() } -> std::convertible_to<size_t>;
    { t.begin() } -> std::same_as<typename T::iterator>;
    { t.end() } -> std::same_as<typename T::iterator>;
    requires std::same_as<typename T::value_type, std::pair<const typename T::key_type, typename T::mapped_type>>;
};

/* * * * * * * * *
 * declarations  *
 * * * * * * * * */

/* standard library types */

template<typename T>
std::optional<T> parse(char* *buffer, size_t *bytes_to_read) = delete;

template<Pair T>
std::optional<T> parse(char* *buffer, size_t *bytes_to_read);

template<List T>
std::optional<T> parse(char* *buffer, size_t *bytes_to_read);

template<Map T>
std::optional<T> parse(char* *buffer, size_t *bytes_to_read);

/* enums */

template<>
std::optional<Direction> parse<Direction>(char* *buffer, size_t *bytes_to_read);

template<>
std::optional<ClientMessageType> parse<ClientMessageType>(char* *buffer, size_t *bytes_to_read);

template<>
std::optional<EventType> parse<EventType>(char* *buffer, size_t *bytes_to_read);

template<>
std::optional<ServerMessageType> parse<ServerMessageType>(char* *buffer, size_t *bytes_to_read);

/* my types */

template<>
std::optional<Event> parse<Event>(char* *buffer, size_t *bytes_to_read);

/* * * * * * * * * *
 * primitive types *
 * * * * * * * * * */

template<>
std::optional<uint8_t> parse<uint8_t>(char* *buffer, size_t *bytes_to_read) {
    if (*bytes_to_read < 1)
        return {};
    auto result = *(uint8_t*)*buffer;
    *buffer += 1;
    *bytes_to_read -= 1;
    return result;
}

template<>
std::optional<uint16_t> parse<uint16_t>(char* *buffer, size_t *bytes_to_read) {
    if (*bytes_to_read < 2)
        return {};
    auto result = *(uint16_t*)*buffer;
    *buffer += 2;
    *bytes_to_read -= 2;
    return ntohs(result);
}

template<>
std::optional<uint32_t> parse<uint32_t>(char* *buffer, size_t *bytes_to_read) {
    if (*bytes_to_read < 4)
        return {};
    auto result = *(uint32_t*)*buffer;
    *buffer += 4;
    *bytes_to_read -= 4;
    return ntohl(result);
}

/* * * * * * * * * * * * * *
 * standard library types  *
 * * * * * * * * * * * * * */


/* Definitions */

template<>
std::optional<std::string> parse(char* *buffer, size_t *bytes_to_read) {
    auto size = parse<uint8_t>(buffer, bytes_to_read);
    if (!size)
        return {};
    auto size_value = size.value();
    if (*bytes_to_read < size_value)
        return {}; // not enough bytes left
    std::string result(*buffer, size_value);
    *buffer += size_value;
    *bytes_to_read -= size_value;
    return result;
}


template<List T>
std::optional<T> parse(char* *buffer, size_t *bytes_to_read) {
    auto size = parse<uint32_t>(buffer, bytes_to_read);
    if (!size)
        return {};
    auto size_value = size.value();
    T result;
    for (uint32_t i = 0; i < size_value; i++) {
        auto to_push_option = parse<typename T::value_type>(buffer, bytes_to_read);
        if (!to_push_option)
            return {};
        result.push_back(to_push_option.value());
    }
    return result;
}

template<Map T>
std::optional<T> parse(char* *buffer, size_t *bytes_to_read) {
    auto size = parse<uint32_t>(buffer, bytes_to_read);
    if (!size)
        return {};
    auto size_value = size.value();
    T result;
    for (uint32_t i = 0; i < size_value; i++) {
        auto key_option = parse<typename T::key_type>(buffer, bytes_to_read);
        if (!key_option)
            return {};
        auto value_option = parse<typename T::mapped_type>(buffer, bytes_to_read);
        if (!value_option)
            return {};
        result.insert({key_option.value(), value_option.value()});
    }
    return result;
}


template<Pair T>
std::optional<T> parse(char* *buffer, size_t *bytes_to_read) {
    auto first = parse<typename T::first_type>(buffer, bytes_to_read);
    auto second = parse<typename T::second_type>(buffer, bytes_to_read);
    if (first && second) {
        T result;
        result.first = first.value();
        result.second = second.value();
        return result;
    }
    return {};
}

/* * * * * * *
 * My types  *
 * * * * * * */

/* Enums */

template<>
std::optional<Direction> parse<Direction>(char* *buffer, size_t *bytes_to_read) {
    auto result = parse<uint8_t>(buffer, bytes_to_read);
    if (!result.has_value() || result.value() < 4)
        return {};
    return Direction(result.value());
}

template<>
std::optional<ClientMessageType> parse<ClientMessageType>(char* *buffer, size_t *bytes_to_read) {
    auto result = parse<uint8_t>(buffer, bytes_to_read);
    if (!result.has_value() || result.value() < 4)
        return {};
    return ClientMessageType(result.value());
}

template<>
std::optional<EventType> parse<EventType>(char* *buffer, size_t *bytes_to_read) {
    auto result = parse<uint8_t>(buffer, bytes_to_read);
    if (!result.has_value() || result.value() < 4)
        return {};
    return EventType(result.value());
}

template<>
std::optional<ServerMessageType> parse<ServerMessageType>(char* *buffer, size_t *bytes_to_read) {
    auto result = parse<uint8_t>(buffer, bytes_to_read);
    if (!result.has_value() || result.value() < 5)
        return {};
    return ServerMessageType(result.value());
}

/* Structs */

template<>
std::optional<Event> parse<Event>(char* *buffer, size_t *bytes_to_read) {
    auto type_option = parse<EventType>(buffer, bytes_to_read);
    if (!type_option)
        return {};
    auto type = type_option.value();
    Event result;
    result.type = type;
    switch (type) {
        case BombPlaced: {
            auto bomb_id = parse<BombId>(buffer, bytes_to_read);
            auto position = parse<Position>(buffer, bytes_to_read);
            if (!bomb_id || !position)
                return {};
//            return Event({type, event_bomb_placed_t({bomb_id.value(), position.value()})});
            result.variant = event_bomb_placed_t({bomb_id.value(), position.value()});
            break;
        }

        case BombExploded: {
            auto bomb_id = parse<BombId>(buffer, bytes_to_read);
            auto robots_destroyed = parse<std::vector<PlayerId>>(buffer, bytes_to_read);
            auto blocks_destroyed = parse<std::vector<Position>>(buffer, bytes_to_read);
            if (!bomb_id || !robots_destroyed || !blocks_destroyed)
                return {};
//            return Event({
//                type,
//                event_bomb_exploded_t({
//                    bomb_id.value(),
//                    robots_destroyed.value(),
//                    blocks_destroyed.value()
//                })
//            });
            result.variant = event_bomb_exploded_t({
                bomb_id.value(),
                robots_destroyed.value(),
                blocks_destroyed.value()
            });
            break;
        }

        case PlayerMoved: {
            auto player_id = parse<PlayerId>(buffer, bytes_to_read);
            auto position = parse<Position>(buffer, bytes_to_read);
            if (!player_id || !position)
                return {};
//            return Event({
//                type,
//                event_player_moved_t({player_id.value(), position.value()})
//            });
            result.variant = event_player_moved_t({player_id.value(), position.value()});
            break;
        }

        case BlockPlaced: {
            auto position = parse<Position>(buffer, bytes_to_read);
            if (!position)
                return {};
//            return Event({type, event_block_placed_t({position.value()})});
            result.variant = event_block_placed_t({position.value()});
            break;
        }

            assert(false);
    }
    return result;
}

/* = = = = = *
 * SERIALIZE *
 * = = = = = */

#endif //SIK_2022_COMMON_H
