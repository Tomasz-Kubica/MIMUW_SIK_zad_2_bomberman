#include <iostream>
#include <string>
#include <boost/program_options.hpp>
#include <boost/array.hpp>
#include <boost/bind/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <queue>

#include "common.h"

#define BUFFER_SIZE 80000

namespace p_opt = boost::program_options;

using boost::asio::ip::tcp;

// program parameters
uint16_t bomb_timer;
uint8_t players_count;
uint64_t turn_duration; // in milliseconds
uint16_t explosion_radius;
uint16_t initial_blocks;
uint16_t game_length;
std::string server_name;
uint16_t port;
uint32_t seed; // later holds last generated random numer
uint16_t size_x;
uint16_t size_y;

/* = = = = = = = = = = = = = *
 * RANDOM NUMBERS GENERATOR  *
 * = = = = = = = = = = = = = */

#define RNG_MULTIPLIER 48271
#define RNG_MODULO 2147483647

std::mutex rng_mutex;

uint32_t get_nex_random() {
    const std::lock_guard<std::mutex> lock(rng_mutex);
    auto new_random = (uint32_t)(((uint64_t)seed * RNG_MULTIPLIER) % RNG_MODULO);
    seed = new_random;
    return new_random;
}

/* = = = = = = = = = = *
 * DATA KEPT BY SERVER *
 * = = = = = = = = = = */

enum class PlayerActionType {
    NothingReceived,
    Move,
    PlaceBlock,
    PlaceBomb
};

struct PlayerAction {
    PlayerActionType type;
    Direction direction = Direction::Up; // default value should never be used
};

ServerMessage hello_message; // hello message is always the same

std::mutex data_mutex;

struct game_data_t {
    TurnNo turn_no = 0; // or 1 ??? TODO
    std::vector<server_message_turn_t> turns;
    std::unordered_map<PlayerId, Position> players_positions;
    std::unordered_map<PlayerId, Score> scores;
    std::unordered_map<BombId, Bomb> bombs;
    BombId next_bomb_id = 0;
    std::set<Position> blocks;
    std::unordered_map<PlayerId, PlayerAction> selected_actions;
};

bool is_game_played = false;
std::condition_variable conditional_game_started;
PlayerId next_player_id = 0;
std::unordered_map<PlayerId, Player> accepted_players;
game_data_t game_data;

std::vector<std::pair<std::shared_ptr<tcp::socket>, std::shared_ptr<bool>>> clients_sockets;

//std::condition_variable conditional_new_data;
//std::mutex mutex_new_data;

/* = = = = = = = = = *
 * UTILITY FUNCTIONS *
 * = = = = = = = = = */

// You need to have data_mutex to run this function
void send_to_all_clients(const ServerMessage &message) {
    char buff[BUFFER_SIZE];
    for (auto &socket_flag_pair : clients_sockets) {
        auto client_socket = socket_flag_pair.first;
        char *buff_ptr = buff;
        size_t buff_size = BUFFER_SIZE;
        assert(serialize(message, &buff_ptr, &buff_size));

        boost::system::error_code ignored_error;
        boost::asio::write(*client_socket, boost::asio::buffer(buff, BUFFER_SIZE - buff_size), ignored_error);
    }
}

/* = = = = = = = = = = = = = = = = = = = *
 * CLASS HANDLING CONNECTION WITH PLAYER *
 * = = = = = = = = = = = = = = = = = = = */

class player_connection : public boost::enable_shared_from_this<player_connection> {
public:
    typedef boost::shared_ptr<player_connection> pointer;

    ~player_connection() {
        const std::lock_guard<std::mutex> lock(data_mutex);
        remove_from_vector(clients_sockets, {socket_, is_playing_});
    }

    static pointer create(boost::asio::io_context& io_context) {
        return pointer(new player_connection(io_context));
    }

    tcp::socket& socket() {
        return *socket_;
    }

    void send_message(const ServerMessage &message) {
        char buff[BUFFER_SIZE];
        char *buff_ptr = buff;
        size_t buff_size = BUFFER_SIZE;
        assert(serialize(message, &buff_ptr, &buff_size));

        boost::asio::async_write(*socket_, boost::asio::buffer(buff, BUFFER_SIZE - buff_size),
                                 boost::bind(&player_connection::handle_write, shared_from_this(),
                                             boost::asio::placeholders::error,
                                             boost::asio::placeholders::bytes_transferred));
    }

    void start() {
        boost::asio::ip::tcp::no_delay no_delay_option(true);
        socket_->set_option(no_delay_option);
        send_message(hello_message);
        start_receive();
    }

private:
    player_connection(boost::asio::io_context& io_context)
            : socket_(new tcp::socket(io_context)), is_playing_(new bool(false)) {
        const std::lock_guard<std::mutex> lock(data_mutex);
        clients_sockets.push_back({socket_, is_playing_});
        socket_->is_open();
    }

    void send_current_state() {
        const std::lock_guard<std::mutex> lock(data_mutex);
        if (is_game_played) { // send game started and turns of current game
            ServerMessage game_started_message({
                ServerMessageType::GameStarted,
                server_message_game_started_t({
                    accepted_players
                })
            });
            for (auto &turn : game_data.turns) {
                ServerMessage turn_message({
                    ServerMessageType::Turn,
                    turn
                });
                send_message(turn_message);
            }
        } else { // send accepted players
            for (auto &player : accepted_players) {
                ServerMessage message({
                    ServerMessageType::AcceptedPlayer,
                    server_message_accepted_player_t({
                        player.first,
                        player.second
                    })
                });
                send_message(message);
            }
        }
    }

    void start_receive() {
        socket_->async_receive(
                boost::asio::buffer(recv_buffer_),
                boost::bind(&player_connection::handle_receive, shared_from_this() /* this */,
                            boost::asio::placeholders::error,
                            boost::asio::placeholders::bytes_transferred));
    }

    void handle_receive(
            const boost::system::error_code &error,
            std::size_t bytes_transferred) {
        if (error == boost::asio::error::eof) {
            return;
        } else if (error) {
            return;
        }

        std::string received(recv_buffer_.data(), bytes_transferred);
        saved_buffer_ += received;

        while (!saved_buffer_.empty()) {
            char *buff = saved_buffer_.data();
            auto bytes_to_read = (size_t) saved_buffer_.size();
            auto message = parse<ClientMessage>(&buff, &bytes_to_read);
            if (!message && bytes_to_read == 0) {
                // Part of the message hasn't been received yet, waiting for the rest of it
                break;
            } else if (!message) {
                // Message was incorrect, disconnect
                return;
            }
            // Correct message
            auto parsed_size = (size_t) (buff - saved_buffer_.data());
            saved_buffer_.erase(0, parsed_size);
            handle_message(message.value());
        }
        start_receive(); // Wait for nex message
    }

    void handle_message(ClientMessage &message) {
        const std::lock_guard<std::mutex> lock(data_mutex);
        switch (message.type) {
            case ClientMessageType::Join: {
                if (*is_playing_ || is_game_played || accepted_players.size() == players_count)
                    return;
                auto player_name = std::get<std::string>(message.variant);
                Player player = {player_name, get_client_address()};
                player_id_ = next_player_id;
                next_player_id++;
                accepted_players.insert({
                    player_id_,
                    player
                });
                *is_playing_ = true;

                ServerMessage accepted_player_message({
                    ServerMessageType::AcceptedPlayer,
                    server_message_accepted_player_t({
                        player_id_,
                        player
                    })});
                send_to_all_clients(accepted_player_message);

                if (accepted_players.size() == players_count) {
                    conditional_game_started.notify_all();
                }
                break;
            }
            case ClientMessageType::PlaceBomb: {
                if (is_playing_) {
                    PlayerAction action;
                    action.type = PlayerActionType::PlaceBomb;
                    game_data.selected_actions[player_id_] = action;
                }
                break;
            }
            case ClientMessageType::PlaceBlock: {
                if (is_playing_) {
                    PlayerAction action;
                    action.type = PlayerActionType::PlaceBlock;
                    game_data.selected_actions[player_id_] = action;
                }
                break;
            }
            case ClientMessageType::Move: {
                if (is_playing_) {
                    PlayerAction action;
                    action.type = PlayerActionType::Move;
                    action.direction = std::get<Direction>(message.variant);
                    game_data.selected_actions[player_id_] = action;
                }
                break;
            }
        }
    }

    void handle_write(const boost::system::error_code& /*error*/,
                      size_t /*bytes_transferred*/) {}

    std::string get_client_address() {
        std::string result;
        auto endpoint = socket_->remote_endpoint();
        auto address = endpoint.address();
        if (address.is_v4()) {
            result = address.to_v4().to_string();
        } else if (address.is_v6()) {
            auto v6_address = address.to_v6();
//            if (v6_address.is_v4_mapped()) {
//                result = v6_address.to_v4().to_string();
//            } else {
//                result = v6_address.to_string();
//            }
            result = v6_address.to_string();

        }
        return "[" + result + "]:" + std::to_string(endpoint.port());
    }

    std::shared_ptr<tcp::socket> socket_;
    boost::array<char, BUFFER_SIZE> recv_buffer_;
    std::string saved_buffer_;
    PlayerId player_id_;
    std::shared_ptr<bool> is_playing_;
};

/* = = = = = = = = = = = = = = = = = = = = = = = *
 * CLASS HANDLING ACCEPTING INCOMING CONNECTIONS *
 * = = = = = = = = = = = = = = = = = = = = = = = */

class tcp_server {
public:
    tcp_server(boost::asio::io_context& io_context)
            : io_context_(io_context),
              acceptor_(io_context, tcp::endpoint(tcp::v6(), port)) {
        start_accept();
    }

private:
    void start_accept() {
        player_connection::pointer new_connection =
                player_connection::create(io_context_);

        acceptor_.async_accept(new_connection->socket(),
                               boost::bind(&tcp_server::handle_accept, this, new_connection,
                                           boost::asio::placeholders::error));
    }

    void handle_accept(player_connection::pointer new_connection, const boost::system::error_code& error) {
        if (!error) {
            new_connection->start();
        }

        start_accept();
    }

    boost::asio::io_context& io_context_;
    tcp::acceptor acceptor_;
};

void start_accepting_connections(boost::asio::io_context& io_context) {
    tcp_server server(io_context);
    io_context.run();
}

void manage_game_state(boost::asio::io_context& io_context) {
    while (true) {
        {
            std::vector<Event> events;
            std::unique_lock<std::mutex> lock(data_mutex);
            if (!is_game_played) { // wait until game starts
                conditional_game_started.wait(lock, []{return accepted_players.size() == players_count;});


                is_game_played = true;
                game_data = game_data_t(); // wyczyszczenie danych o grze
                for (auto &player : accepted_players) {
                    auto id = player.first;
                    auto x = uint16_t (get_nex_random() % size_x);
                    auto y = uint16_t (get_nex_random() % size_y);
                    Position position{x, y};
                    events.push_back({
                        EventType::PlayerMoved,
                        event_player_moved_t({
                            id,
                            position
                        })
                    });
                    game_data.players_positions.insert({id, position});
                    game_data.scores.insert({id, 0});
                    PlayerAction action;
                    action.type = PlayerActionType::NothingReceived;
                    game_data.selected_actions.insert({id, action});
                }
                for (uint16_t i = 0; i < initial_blocks; i++) {
                    auto x = uint16_t (get_nex_random() % size_x);
                    auto y = uint16_t (get_nex_random() % size_y);
                    Position position{x, y};
                    if (game_data.blocks.contains(position))
                        continue;
                    game_data.blocks.insert(position);
                    events.push_back({
                        EventType::BlockPlaced,
                        event_block_placed_t({
                            position
                        })
                    });
                }

                ServerMessage game_started_message({
                    ServerMessageType::GameStarted,
                    server_message_game_started_t({
                        accepted_players
                    })
                });
                send_to_all_clients(game_started_message);

            } else { // next turn
                if (game_data.turn_no > game_length) { // game ended
                    ServerMessage game_ended_message{
                        ServerMessageType::GameEnded,
                        server_message_game_ended_t{
                            game_data.scores
                        }
                    };
                    send_to_all_clients(game_ended_message);
                    is_game_played = false;
                    for (auto &socket_flag : clients_sockets) {
                        *socket_flag.second = false;
                    }
                    accepted_players = {};
                    next_player_id = 0;
                    continue;
                }

                std::set<PlayerId> destroyed_players;
                std::set<BombId> bombs_to_remove;
                std::set<Position> blocks_to_remove;
                for (auto &bomb : game_data.bombs) {
                    bomb.second.second--; // decrease bomb timer;
                    if (bomb.second.second == 0) { // bomb explodes
                        bombs_to_remove.insert(bomb.first);
                        std::set<PlayerId> players_destroyed_by_bomb;
                        std::set<Position> blocks_destroyed_by_bomb;
                        std::vector<Position> directions{{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
                        auto destroy_on_position = [&](const Position &pos){
                            for (const auto &player : game_data.players_positions) {
                                if (player.second == pos) {
                                    destroyed_players.insert(player.first);
                                    players_destroyed_by_bomb.insert(player.first);
                                }
                            }
                            if (game_data.blocks.contains(pos)) {
                                blocks_destroyed_by_bomb.insert(pos);
                                blocks_to_remove.insert(pos);
                                return true;
                            }
                            return false;
                        };
                        for (const auto &direction : directions) {
                            for (uint16_t i = 0; i <= explosion_radius; i++) {
                                auto explosion_position = bomb.second.first;
                                explosion_position.first += (uint16_t)(direction.first * i);
                                explosion_position.second += (uint16_t)(direction.second * i);
                                if (destroy_on_position(explosion_position))
                                    break;
                            }
                        }
                        for (const auto &block : blocks_destroyed_by_bomb) {
                            game_data.blocks.erase(block);
                        }
                        events.push_back({
                            EventType::BombExploded,
                            event_bomb_exploded_t({
                                bomb.first,
                                std::vector(players_destroyed_by_bomb.begin(), players_destroyed_by_bomb.end()),
                                std::vector(blocks_destroyed_by_bomb.begin(), blocks_destroyed_by_bomb.end())
                            })
                        });
                    }
                }
                for (auto &bomb : bombs_to_remove) { // remove bombs that exploded
                    game_data.bombs.erase(bomb);
                }
                for (auto &player : game_data.players_positions) {
                    auto id = player.first;
                    auto current_position = player.second;
                    if (destroyed_players.contains(id)) {
                        auto x = uint16_t (get_nex_random() % size_x);
                        auto y = uint16_t (get_nex_random() % size_y);
                        Position position{x, y};
                        events.push_back({
                            EventType::PlayerMoved,
                            event_player_moved_t({
                                id,
                                position
                            })
                        });
                        player.second = position; // zapisanie zmiany pozycji w game_data
                    } else { // player wasn't destroyed
                        auto action = game_data.selected_actions[id];
                        switch (action.type) {
                            case PlayerActionType::NothingReceived: {
                                // nothing to do
                                break;
                            }
                            case PlayerActionType::PlaceBlock: {
                                game_data.blocks.insert(current_position);
                                events.push_back({
                                    EventType::BlockPlaced,
                                    event_block_placed_t({
                                        current_position
                                    })
                                });
                                break;
                            }
                            case PlayerActionType::PlaceBomb: {
                                auto new_bomb_id = game_data.next_bomb_id;
                                game_data.next_bomb_id++;
                                game_data.bombs.insert({new_bomb_id, {current_position, bomb_timer}});
                                events.push_back({
                                   EventType::BombPlaced,
                                   event_bomb_placed_t({
                                       new_bomb_id,
                                       current_position
                                   })
                                });
                                break;
                            }
                            case PlayerActionType::Move: {
                                std::pair<int, int> new_position = {
                                        (int)current_position.first,
                                        (int)current_position.second
                                };
                                if (action.direction == Direction::Up) {
                                    new_position.second++;
                                } else if (action.direction == Direction::Right) {
                                    new_position.first++;
                                } else if (action.direction == Direction::Down) {
                                    new_position.second--;
                                } else /*if (action.direction == Direction::Left)*/ {
                                    new_position.first--;
                                }
                                if (!game_data.blocks.contains(new_position)
                                    && new_position.first >= 0 && new_position.first < size_x
                                    && new_position.second >= 0 && new_position.second < size_y) { // check if position is allowed
                                    Position new_position_verified = {
                                            (uint16_t)new_position.first,
                                            (uint16_t)new_position.second
                                    };
                                    events.push_back({
                                        EventType::PlayerMoved,
                                        event_player_moved_t({
                                            id,
                                            new_position_verified
                                        })
                                    });
                                    player.second = new_position_verified; // update position in game data
                                }
                                break;
                            }
                        }
                        // clear action that was already handled
                        game_data.selected_actions[id].type = PlayerActionType::NothingReceived;
                    }
                }
            }
            server_message_turn_t turn{
                    game_data.turn_no,
                    std::move(events)
            };
            game_data.turns.push_back(turn);
            send_to_all_clients(ServerMessage{
                    ServerMessageType::Turn,
                    std::move(turn)
            });
            game_data.turn_no++;
        }
        boost::asio::deadline_timer t(io_context, boost::posix_time::milliseconds(turn_duration));
        t.wait();
    }
}

int main(int argc, char *argv[]) {

    auto time_now = std::chrono::system_clock::now().time_since_epoch().count();
    uint16_t players_count_to_load;
    try {
        p_opt::options_description description("Allowed options");
        description.add_options()
                ("help,h", "Wypisuje jak używać programu")
                ("bomb-timer,b", p_opt::value<uint16_t>(&bomb_timer)->required(), "czas trwania tury w milisekundach")
                ("players-count,c", p_opt::value<uint16_t>(&players_count_to_load)->required(), "liczba grających graczy")
                ("turn-duration,d", p_opt::value<uint64_t>(&turn_duration)->required(),
                 "czas trwania tury w milisekundach")
                ("explosion-radius,e", p_opt::value<uint16_t>(&explosion_radius)->required(), "promień wybuchu bomby")
                ("initial-blocks,k", p_opt::value<uint16_t>(&initial_blocks)->required(),
                 "początkowa liczba bloków na mapie")
                ("game-length,l", p_opt::value<uint16_t>(&game_length)->required(), "liczba tur w rozgrywce")
                ("server-name,n", p_opt::value<std::string>(&server_name)->required(), "nazwa serwera")
                ("port,p", p_opt::value<uint16_t>(&port)->required(),
                 "port na którym serwer nasłuchuje na połączenia od graczy")
                ("seed,s", p_opt::value<uint32_t>(&seed)->default_value((uint32_t) time_now),
                 "(opcjonalny) seed wykorzystywane przez generator liczb losowych")
                ("size-x,x", p_opt::value<uint16_t>(&size_x)->required(), "rozmiar planszy wzdłuż osi x")
                ("size-y,y", p_opt::value<uint16_t>(&size_y)->required(), "rozmiar planszy wzdłuż osi y");

        p_opt::variables_map var_map;
        p_opt::store(p_opt::parse_command_line(argc, argv, description), var_map);

        if (var_map.count("help")) {
            std::cout << description << "\n";
            return 0;
        }

        p_opt::notify(var_map);
    }
    catch (std::exception &e) {
        std::cout << e.what() << '\n';
        return 1;
    }
    players_count = (uint8_t)players_count_to_load;

    // Create hello message
    hello_message = {
            ServerMessageType::Hello,
            server_message_hello_t({
                server_name,
                players_count,
                size_x,
                size_y,
                game_length,
                explosion_radius,
                bomb_timer
            })
    };

    boost::asio::io_context io_context;
    std::thread accepting_connections_thread(start_accepting_connections, std::ref(io_context));
    std::thread manage_game_state_thread(manage_game_state, std::ref(io_context));
    accepting_connections_thread.join();
    manage_game_state_thread.join();
    return 0;
}