#ifndef SIK_2022_COMMON_H
#define SIK_2022_COMMON_H

#include <arpa/inet.h>
#include <optional>
#include <vector>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <concepts>
#include <cassert>
#include <variant>
#include <cstring>

/* = = = *
 * TYPES *
 * = = = */

enum class Direction {
    Up = 0,
    Right = 1,
    Down = 2,
    Left = 3
};

enum class ClientMessageType {
    Join = 0,
    PlaceBomb = 1,
    PlaceBlock = 2,
    Move = 3
};

struct ClientMessage {
    ClientMessageType type;
    std::variant<std::monostate, std::string, Direction> variant;
};

typedef uint32_t BombId;
typedef uint8_t PlayerId;
typedef uint32_t Score;
typedef std::pair<uint16_t, uint16_t> Position; // <x, y>
typedef std::pair<Position, uint16_t> Bomb; // <position, timer>
typedef std::pair<std::string, std::string> Player; // <name, address>

enum class EventType {
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

enum class ServerMessageType {
    Hello = 0,
    AcceptedPlayer = 1,
    GameStarted = 2,
    Turn = 3,
    GameEnded = 4,
};

struct server_message_hello_t {
    std::string server_name;
    uint8_t players_count;
    uint16_t size_x;
    uint16_t size_y;
    uint16_t game_length;
    uint16_t explosion_radius;
    uint16_t bomb_timer;
};

struct server_message_accepted_player_t {
    PlayerId id;
    Player player;
};

struct server_message_game_started_t {
    std::unordered_map<PlayerId, Player> players;
};

struct server_message_turn_t {
    uint16_t turn;
    std::vector<Event> events;
};

struct server_message_game_ended_t {
    std::unordered_map<PlayerId, Score> scores;
};

struct ServerMessage {
    ServerMessageType type;
    std::variant<server_message_hello_t, server_message_accepted_player_t, server_message_game_started_t,
        server_message_turn_t, server_message_game_ended_t> variant;
};

enum class InputMessageType {
    PlaceBomb = 0,
    PlaceBlock = 1,
    Move = 2,
};

struct InputMessage {
    InputMessageType type;
    Direction direction; // używane tylko dla typu InputMove
};

enum class DrawMessageType {
    Lobby = 0,
    Game = 1,
};

struct draw_message_lobby_t {
    std::string server_name;
    uint8_t players_count;
    uint16_t size_x;
    uint16_t size_y;
    uint16_t game_length;
    uint16_t explosion_radius;
    uint16_t bomb_timer;
    std::unordered_map<PlayerId, Player> players;
};

struct draw_message_game_t {
    std::string server_name;
    uint16_t size_x;
    uint16_t size_y;
    uint16_t game_length;
    uint16_t turn;
    std::unordered_map<PlayerId, Player> players;
    std::unordered_map<PlayerId, Position> player_positions;
    std::vector<Position> blocks;
    std::vector<Bomb> bombs;
    std::vector<Position> explosions;
    std::unordered_map<PlayerId, Score> scores;
};

struct DrawMessage {
    DrawMessageType type;
    std::variant<draw_message_lobby_t, draw_message_game_t> variant;
};

/* = = = = = *
 * concepts  *
 * = = = = = */

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

template<typename  T>
concept MyEnum = requires(T t) {
    requires std::same_as<T, Direction> || std::same_as<T, ClientMessageType> || std::same_as<T, EventType>
        || std::same_as<T, ServerMessageType> || std::same_as<T, InputMessageType> || std::same_as<T, DrawMessageType>;
};

/* = = = *
 * PARSE *
 * = = = */

/* * * * * * * * *
 * declarations  *
 * * * * * * * * */

/* Parsuje obiekt typu T.
 * Jeżeli parsowanie zakończyło się niepowodzeniem i bytes_to_read = 0,
 * to jedynym powodem niepowodzenia była zbyt mała ilość danych */
template<typename T>
std::optional<T> parse(char* *buffer, size_t *bytes_to_read) = delete;

/* standard library types */

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

template<>
std::optional<InputMessageType> parse<InputMessageType>(char* *buffer, size_t *bytes_to_read);

/* my types */

template<>
std::optional<Event> parse<Event>(char* *buffer, size_t *bytes_to_read);

template<>
std::optional<ServerMessage> parse<ServerMessage>(char* *buffer, size_t *bytes_to_read);

template<>
std::optional<InputMessage> parse<InputMessage>(char* *buffer, size_t *bytes_to_read);

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
    if (*bytes_to_read < 2) {
        *bytes_to_read = 0;
        return {};
    }
    auto result = *(uint16_t*)*buffer;
    *buffer += 2;
    *bytes_to_read -= 2;

    return ntohs(result);
}

template<>
std::optional<uint32_t> parse<uint32_t>(char* *buffer, size_t *bytes_to_read) {
    if (*bytes_to_read < 4) {
        *bytes_to_read = 0;
        return {};
    }
    auto result = *(uint32_t*)*buffer;
    *buffer += 4;
    *bytes_to_read -= 4;

    return ntohl(result);
}

/* * * * * * * * * * * * * *
 * standard library types  *
 * * * * * * * * * * * * * */

template<>
std::optional<std::string> parse(char* *buffer, size_t *bytes_to_read) {
    auto size = parse<uint8_t>(buffer, bytes_to_read);
    if (!size)
        return {};
    auto size_value = size.value();
    if (*bytes_to_read < size_value) {
        *bytes_to_read = 0;
        return {}; // not enough bytes left
    }
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
    if (!result.has_value())
        return {};
    else if (result.value() > 4) {
        *bytes_to_read = std::max(*bytes_to_read, (size_t)1);
        return {};
    }
    return Direction(result.value());
}

template<>
std::optional<ClientMessageType> parse<ClientMessageType>(char* *buffer, size_t *bytes_to_read) {
    auto result = parse<uint8_t>(buffer, bytes_to_read);
    if (!result.has_value())
        return {};
    else if (result.value() > 4) {
        *bytes_to_read = std::max(*bytes_to_read, (size_t)1);
        return {};
    }
    return ClientMessageType(result.value());
}

template<>
std::optional<EventType> parse<EventType>(char* *buffer, size_t *bytes_to_read) {
    auto result = parse<uint8_t>(buffer, bytes_to_read);
    if (!result.has_value())
        return {};
    else if (result.value() > 4) {
        *bytes_to_read = std::max(*bytes_to_read, (size_t)1);
        return {};
    }
    return EventType(result.value());
}

template<>
std::optional<ServerMessageType> parse<ServerMessageType>(char* *buffer, size_t *bytes_to_read) {
    auto result = parse<uint8_t>(buffer, bytes_to_read);
    if (!result.has_value())
        return {};
    else if (result.value() > 5) {
        *bytes_to_read = std::max(*bytes_to_read, (size_t)1);
        return {};
    }
    return ServerMessageType(result.value());
}

template<>
std::optional<InputMessageType> parse<InputMessageType>(char* *buffer, size_t *bytes_to_read) {
    auto result = parse<uint8_t>(buffer, bytes_to_read);
    if (!result.has_value())
        return {};
    else if (result.value() > 3) {
        *bytes_to_read = std::max(*bytes_to_read, (size_t)1);
        return {};
    }
    return InputMessageType(result.value());
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
        case EventType::BombPlaced: {
            auto bomb_id = parse<BombId>(buffer, bytes_to_read);
            auto position = parse<Position>(buffer, bytes_to_read);
            if (!bomb_id || !position)
                return {};
            result.variant = event_bomb_placed_t({bomb_id.value(), position.value()});
            break;
        }

        case EventType::BombExploded: {
            auto bomb_id = parse<BombId>(buffer, bytes_to_read);
            auto robots_destroyed = parse<std::vector<PlayerId>>(buffer, bytes_to_read);
            auto blocks_destroyed = parse<std::vector<Position>>(buffer, bytes_to_read);
            if (!bomb_id || !robots_destroyed || !blocks_destroyed)
                return {};
            result.variant = event_bomb_exploded_t({
                bomb_id.value(),
                robots_destroyed.value(),
                blocks_destroyed.value()
            });
            break;
        }

        case EventType::PlayerMoved: {
            auto player_id = parse<PlayerId>(buffer, bytes_to_read);
            auto position = parse<Position>(buffer, bytes_to_read);
            if (!player_id || !position)
                return {};
            result.variant = event_player_moved_t({player_id.value(), position.value()});
            break;
        }

        case EventType::BlockPlaced: {
            auto position = parse<Position>(buffer, bytes_to_read);
            if (!position)
                return {};
            result.variant = event_block_placed_t({position.value()});
            break;
        }

            assert(false);
    }
    return result;
}

template<>
std::optional<server_message_hello_t> parse<server_message_hello_t>(char* *buffer, size_t *bytes_to_read) {
    auto server_name = parse<std::string>(buffer, bytes_to_read);
    auto player_count = parse<uint8_t>(buffer, bytes_to_read);
    auto size_x = parse<uint16_t>(buffer, bytes_to_read);
    auto size_y = parse<uint16_t>(buffer, bytes_to_read);
    auto game_length = parse<uint16_t>(buffer, bytes_to_read);
    auto explosion_radius = parse<uint16_t>(buffer, bytes_to_read);
    auto bomb_timer = parse<uint16_t>(buffer, bytes_to_read);
    if(!server_name || !player_count || !size_x || !size_y || !game_length || !explosion_radius || !bomb_timer)
        return {};
    return server_message_hello_t({ server_name.value(), player_count.value(), size_x.value(), size_y.value(),
                                    game_length.value(), explosion_radius.value(), bomb_timer.value()});
}

template<>
std::optional<server_message_accepted_player_t> parse<server_message_accepted_player_t>(char* *buffer, size_t *bytes_to_read) {
    auto id = parse<PlayerId>(buffer, bytes_to_read);
    auto player = parse<Player>(buffer, bytes_to_read);
    if (!id || !player)
        return {};
    return server_message_accepted_player_t({id.value(), player.value()});
}

template<>
std::optional<server_message_game_started_t> parse<server_message_game_started_t>(char* *buffer, size_t *bytes_to_read) {
    auto players = parse<std::unordered_map<PlayerId, Player>>(buffer, bytes_to_read);
    if (!players)
        return {};
    return server_message_game_started_t({players.value()});
}

template<>
std::optional<server_message_turn_t> parse<server_message_turn_t>(char* *buffer, size_t *bytes_to_read) {
    auto turn = parse<uint16_t>(buffer, bytes_to_read);
    auto events = parse<std::vector<Event>>(buffer, bytes_to_read);
    if (!turn || !events)
        return {};
    return server_message_turn_t({turn.value(), events.value()});
}

template<>
std::optional<server_message_game_ended_t> parse<server_message_game_ended_t>(char* *buffer, size_t *bytes_to_read) {
    auto scores = parse<std::unordered_map<PlayerId, Score>>(buffer, bytes_to_read);
    if (!scores)
        return {};
    return server_message_game_ended_t({scores.value()});
}

template<>
std::optional<ServerMessage> parse<ServerMessage>(char* *buffer, size_t *bytes_to_read) {
    auto type = parse<ServerMessageType>(buffer, bytes_to_read);
    if (!type)
        return {};
    ServerMessage result;
    result.type = type.value();

    switch (type.value()) {
        case ServerMessageType::Hello: {
            auto hello = parse<server_message_hello_t>(buffer, bytes_to_read);
            if (!hello)
                return {};
            result.variant = hello.value();
            break;
        }

        case ServerMessageType::AcceptedPlayer: {
            auto accepted_player = parse<server_message_accepted_player_t>(buffer, bytes_to_read);
            if (!accepted_player)
                return {};
            result.variant = accepted_player.value();
            break;
        }

        case ServerMessageType::GameStarted: {
            auto game_started = parse<server_message_game_started_t>(buffer, bytes_to_read);
            if (!game_started)
                return {};
            result.variant = game_started.value();
            break;
        }

        case ServerMessageType::Turn: {
            auto turn = parse<server_message_turn_t>(buffer, bytes_to_read);
            if (!turn)
                return {};
            result.variant = turn.value();
            break;
        }

        case ServerMessageType::GameEnded: {
            auto game_ended = parse<server_message_game_ended_t>(buffer, bytes_to_read);
            if (!game_ended)
                return {};
            result.variant = game_ended.value();
            break;
        }

        assert(false);
    }

    return result;
}

template<>
std::optional<InputMessage> parse<InputMessage>(char* *buffer, size_t *bytes_to_read) {
    auto type = parse<InputMessageType>(buffer, bytes_to_read);
    if (!type)
        return {};
    InputMessage result;
    result.type = type.value();
    if (type.value() == InputMessageType::Move) {
        auto direction = parse<Direction>(buffer, bytes_to_read);
        if (!direction)
            return {};
        result.direction = direction.value();
    } else {
        result.direction = Direction(0); // nie będzie używane, inicjalizacja czymkolwiek
    }
    return result;
}

/* = = = = = *
 * SERIALIZE *
 * = = = = = */

/* * * * * * * * *
 * declarations  *
 * * * * * * * * */

template<typename T>
bool serialize(T to_serialize, char* *buffer, size_t *bytes_to_write) = delete;

template<Pair T>
bool serialize(T to_serialize, char* *buffer, size_t *bytes_to_write);

template<List T>
bool serialize(T to_serialize, char* *buffer, size_t *bytes_to_write);

template<Map T>
bool serialize(T to_serialize, char* *buffer, size_t *bytes_to_write);

template<>
bool serialize(Direction to_serialize, char* *buffer, size_t *bytes_to_write);

template<MyEnum T>
bool serialize(T to_serialize, char* *buffer, size_t *bytes_to_write);

template<>
bool serialize(ClientMessage to_serialize, char* *buffer, size_t *bytes_to_write);

template<>
bool serialize(DrawMessage to_serialize, char* *buffer, size_t *bytes_to_write);

/* * * * * * * * * *
 * primitive types *
 * * * * * * * * * */

template<>
bool serialize(uint8_t to_serialize, char* *buffer, size_t *bytes_to_write) {
    if (*bytes_to_write < 1)
        return false;
    *(uint8_t*)(*buffer) = to_serialize;
    *buffer += 1;
    *bytes_to_write -= 1;
    return true;
}

template<>
bool serialize(uint16_t to_serialize, char* *buffer, size_t *bytes_to_write) {
    if (*bytes_to_write < 2)
        return false;
    *(uint16_t*)(*buffer) = htons(to_serialize);
    *buffer += 2;
    *bytes_to_write -= 2;
    return true;
}

template<>
bool serialize(uint32_t to_serialize, char* *buffer, size_t *bytes_to_write) {
    if (*bytes_to_write < 4)
        return false;
    *(uint32_t*)(*buffer) = htonl(to_serialize);
    *buffer += 4;
    *bytes_to_write -= 4;
    return true;
}

/* * * * * * * * * * * * * *
 * standard library types  *
 * * * * * * * * * * * * * */

template<>
bool serialize(std::string to_serialize, char* *buffer, size_t *bytes_to_write) {
    auto size = (uint8_t)to_serialize.size();
    auto success = serialize(size, buffer, bytes_to_write);
    if (!success || *bytes_to_write < size)
        return false;
    memcpy(*buffer, to_serialize.c_str(), size);
    *buffer += size;
    *bytes_to_write -= size;
    return true;
}

template<Pair T>
bool serialize(T to_serialize, char* *buffer, size_t *bytes_to_write) {
    return serialize(to_serialize.first, buffer, bytes_to_write)
        && serialize(to_serialize.second, buffer, bytes_to_write);
}

template<List T>
bool serialize(T to_serialize, char* *buffer, size_t *bytes_to_write) {
    auto size = (uint32_t)to_serialize.size();
    auto success = serialize(size, buffer, bytes_to_write);
    if (!success)
        return false;
    for (uint32_t i = 0; i < size; i++) {
        if (!serialize((typename T::value_type)to_serialize.at(i), buffer, bytes_to_write))
            return false;
    }
    return true;
}

template<Map T>
bool serialize(T to_serialize, char* *buffer, size_t *bytes_to_write) {
    auto size = (uint32_t)to_serialize.size();
    auto success = serialize(size, buffer, bytes_to_write);
    if (!success)
        return false;
    for (auto it = to_serialize.begin(); it != to_serialize.end(); it++) {
        if (!serialize((typename T::key_type)(it->first), buffer, bytes_to_write))
            return false;
        if (!serialize((typename T::mapped_type)(it->second), buffer, bytes_to_write))
            return false;
    }
    return true;
}

/* * * * * * *
 * My types  *
 * * * * * * */

/* Enums */

template<MyEnum T>
bool serialize(T to_serialize, char* *buffer, size_t *bytes_to_write) {
    auto as_number = static_cast<uint8_t>(to_serialize);
    serialize(as_number, buffer, bytes_to_write);
    return true;
}

/* Structs */

template<>
bool serialize(ClientMessage to_serialize, char* *buffer, size_t *bytes_to_write) {
    if (!serialize(to_serialize.type, buffer, bytes_to_write))
        return false;
    if (to_serialize.type == ClientMessageType::Join)
        return serialize(std::get<std::string>(to_serialize.variant), buffer, bytes_to_write);
    else if (to_serialize.type == ClientMessageType::Move)
        return serialize(std::get<Direction>(to_serialize.variant), buffer, bytes_to_write);
    return true;
}

template<>
bool serialize(draw_message_lobby_t to_serialize, char* *buffer, size_t *bytes_to_write) {
    return serialize(to_serialize.server_name, buffer, bytes_to_write) &&
            serialize(to_serialize.players_count, buffer, bytes_to_write) &&
            serialize(to_serialize.size_x, buffer, bytes_to_write) &&
            serialize(to_serialize.size_y, buffer, bytes_to_write) &&
            serialize(to_serialize.game_length, buffer, bytes_to_write) &&
            serialize(to_serialize.explosion_radius, buffer, bytes_to_write) &&
            serialize(to_serialize.bomb_timer, buffer, bytes_to_write) &&
            serialize(to_serialize.players, buffer, bytes_to_write);

}

template<>
bool serialize(draw_message_game_t to_serialize, char* *buffer, size_t *bytes_to_write) {
    return serialize(to_serialize.server_name, buffer, bytes_to_write) &&
            serialize(to_serialize.size_x, buffer, bytes_to_write) &&
            serialize(to_serialize.size_y, buffer, bytes_to_write) &&
            serialize(to_serialize.game_length, buffer, bytes_to_write) &&
            serialize(to_serialize.turn, buffer, bytes_to_write) &&
            serialize(to_serialize.players, buffer, bytes_to_write) &&
            serialize(to_serialize.player_positions, buffer, bytes_to_write) &&
            serialize(to_serialize.blocks, buffer, bytes_to_write) &&
            serialize(to_serialize.bombs, buffer, bytes_to_write) &&
            serialize(to_serialize.explosions, buffer, bytes_to_write) &&
            serialize(to_serialize.scores, buffer, bytes_to_write);
}

template<>
bool serialize(DrawMessage to_serialize, char* *buffer, size_t *bytes_to_write) {
    if (!serialize(to_serialize.type, buffer, bytes_to_write))
        return false;
    if (to_serialize.type == DrawMessageType::Lobby)
        return serialize(std::get<draw_message_lobby_t>(to_serialize.variant), buffer, bytes_to_write);
    else if (to_serialize.type == DrawMessageType::Game)
        return serialize(std::get<draw_message_game_t>(to_serialize.variant), buffer, bytes_to_write);
    assert(false);
    return false;
}

#endif //SIK_2022_COMMON_H
