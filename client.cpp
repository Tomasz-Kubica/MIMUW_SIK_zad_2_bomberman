#include <iostream>
#include <string>
#include <boost/program_options.hpp>
#include <boost/array.hpp>
#include <boost/bind/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>

#include "common.h"

#define BUFFER_SIZE 80000

using boost::asio::ip::udp;
using boost::asio::ip::tcp;

namespace p_opt = boost::program_options;

std::pair<std::string, std::string> split_address(const std::string &address) {
    size_t poss_to_split = address.find_last_of(':');
    if (poss_to_split == std::string::npos) {
        std::cout << "Incorrect address (" << address
                  << ") format, use format: <(host name):(port) lub (IPv4):(port) lub (IPv6):(port)>" << std::endl;
        exit(1);
    }
    auto ip = address.substr(0, poss_to_split);
    auto port = address.substr(poss_to_split + 1);
    return {ip, port};
}

struct ClientGameInfo {
    // flags
    bool hello_received = false;
    bool game_started = false;
    bool join_sent = false;

    // const for server
    std::string server_name;
    uint8_t player_count;
    uint16_t size_x;
    uint16_t size_y;
    uint16_t game_length;
    uint16_t explosion_radius;
    uint16_t bomb_timer;

    // only for game
    uint16_t turn;
    std::unordered_map<PlayerId, Player> players;
    std::unordered_map<PlayerId, Position> player_positions;
    std::unordered_map<PlayerId, Score> scores;
    std::vector<Position> blocks;
    std::unordered_map<BombId, Bomb> bombs;

};

ClientMessage client_message_from_input_message(const InputMessage &input_message) {
    switch (input_message.type) {
        case InputMessageType::PlaceBomb: {
            return ClientMessage({ClientMessageType::PlaceBomb, std::monostate()});
        }

        case InputMessageType::PlaceBlock: {
            return ClientMessage({ClientMessageType::PlaceBlock, std::monostate()});
        }

        case InputMessageType::Move: {
            return ClientMessage({ClientMessageType::Move, input_message.direction});
        }
    }
    std::cerr << "Error: client_message_from_input_message impossible input_message type" << std::endl;
    exit(1);
}

template<typename T>
void remove_from_vector(std::vector<T> &vec, const T &to_remove) {
    auto found = std::find(vec.begin(), vec.end(), to_remove);
    if (found != vec.end()) {
        vec.erase(found);
    }
}

class client_server {
public:
    client_server(boost::asio::io_context &io_context, uint16_t receive_gui_port, const std::string &server_address,
                  const std::string &server_port,
                  const std::string &gui_address, const std::string &gui_port, const std::string &player_name)
            : gui_socket_(io_context, udp::endpoint(udp::v6(), receive_gui_port)), server_socket_(io_context),
              udp_resolver_(io_context), gui_address_(gui_address), gui_port_(gui_port), player_name(player_name) {

        tcp::resolver resolver(io_context);
        tcp::resolver::results_type server_endpoints;
        try {
            // try finding ip v4 endpoint
            server_endpoints = resolver.resolve(tcp::v4(), server_address, server_port);
        } catch (std::exception &e) {
            try {
                // try finding ip v6 endpoint
                server_endpoints = resolver.resolve(tcp::v6(), server_address, server_port);
            } catch (std::exception &e) {
                std::cerr << "Error: resolving server address failed" << std::endl;
                exit(1);
            }
        }
        try {
            boost::asio::connect(server_socket_, server_endpoints);
        } catch (std::exception &e) {
            std::cerr << "Error: connecting to server failed" << std::endl;
            exit(1);
        }

        try {
            // try resolving as ip v4
            gui_endpoint_ = *udp_resolver_.resolve(udp::v4(), gui_address_, gui_port_).begin();
        } catch (std::exception &e) {
            try {
                // try resolving as ip v6
                gui_endpoint_ = *udp_resolver_.resolve(udp::v6(), gui_address_, gui_port_).begin();
            } catch (std::exception &e) {
                std::cerr << "Error: resolving gui address failed" << std::endl;
                exit(1);
            }
        }

        boost::asio::ip::tcp::no_delay no_delay_option(true);
        server_socket_.set_option(no_delay_option);

        start_receive_from_gui();
        start_receive_from_server();
    }

private:
    void start_receive_from_gui() {
        gui_socket_.async_receive_from(
                boost::asio::buffer(gui_recv_buffer_), gui_remote_endpoint_,
                boost::bind(&client_server::handle_receive_from_gui, this,
                            boost::asio::placeholders::error,
                            boost::asio::placeholders::bytes_transferred));
    }

    void start_receive_from_server() {
        server_socket_.async_receive(
                boost::asio::buffer(server_recv_buffer_),
                boost::bind(&client_server::handle_receive_from_server, this,
                            boost::asio::placeholders::error,
                            boost::asio::placeholders::bytes_transferred));
    }

    void send_join() {
        char buff[BUFFER_SIZE];
        char *buff_ptr = buff;
        size_t buff_size = BUFFER_SIZE;
        assert(serialize(ClientMessage({
                                               ClientMessageType::Join,
                                               player_name
                                       }), &buff_ptr, &buff_size));
        try {
            boost::asio::write(server_socket_, boost::asio::buffer(buff, BUFFER_SIZE - buff_size));
        } catch (std::exception &e) {
            // error sending to server
            std::cerr << "Error: sending message to server failed" << std::endl;
            exit(1);
        }
    }

    void handle_receive_from_gui(const boost::system::error_code &error,
                                 std::size_t bytes_transferred) {
        if (!error) {
            auto bytes_left = bytes_transferred;
            char *buff_to_read = gui_recv_buffer_.c_array();
            auto received_message = parse<InputMessage>(&buff_to_read, &bytes_left);
            if (received_message && bytes_left == 0) {
                // after receiving anny correct message try joining if not in game
                client_game_info_mutex.lock();
                if (client_game_info.hello_received && !client_game_info.game_started) {
                    client_game_info_mutex.unlock();

                    send_join();

                    start_receive_from_gui();
                    return;
                } else {
                    client_game_info_mutex.unlock();
                }

                ClientMessage client_message = client_message_from_input_message(received_message.value());

                char buff[BUFFER_SIZE];
                size_t buff_size = BUFFER_SIZE;
                char *buff_to_write = buff;
                assert(serialize(client_message, &buff_to_write, &buff_size));
                server_socket_.send(boost::asio::buffer(buff, BUFFER_SIZE - buff_size));
            }
            start_receive_from_gui();
        } else {
            std::cerr << "Error: receiving message from gui failed" << std::endl;
            exit(1);
        }
    }

    void handle_receive_from_server(const boost::system::error_code &error,
                                    std::size_t bytes_transferred) {
        if (error == boost::asio::error::eof) {
            std::cerr << "Error: connection with server closed" << std::endl;
            exit(1);
        } else if (error) {
            std::cerr << "Error: receiving message from server failed" << std::endl;
            exit(1);
        }

        server_saved_buffer_ += std::string(server_recv_buffer_.c_array(), bytes_transferred);

        while (!server_saved_buffer_.empty()) {
            char *buff = server_saved_buffer_.data();
            auto bytes_to_read = (size_t) server_saved_buffer_.size();
            auto server_message = parse<ServerMessage>(&buff, &bytes_to_read);
            if (!server_message && bytes_to_read == 0) {
                // Part of the message hasn't been received yet, waiting for the rest of it
                break;
            } else if (!server_message) {
                // Message was incorrect, disconnect
                std::cerr << "Error: incorrect message from server" << std::endl;
                exit(1);
            }
            // Correct message
            auto parsed_size = (size_t) (buff - server_saved_buffer_.data());
            server_saved_buffer_.erase(0, parsed_size);
            process_server_message(server_message.value());
        }

        start_receive_from_server();
    }

    void handle_send(boost::shared_ptr<std::string> /*message*/,
                     const boost::system::error_code & /*error*/,
                     std::size_t /*bytes_transferred*/) {}

    // lock client_game_info before calling this method
    void send_lobby_message() {
        DrawMessage to_send = {
                DrawMessageType::Lobby,
                draw_message_lobby_t{
                        client_game_info.server_name,
                        client_game_info.player_count,
                        client_game_info.size_x,
                        client_game_info.size_y,
                        client_game_info.game_length,
                        client_game_info.explosion_radius,
                        client_game_info.bomb_timer,
                        client_game_info.players
                }
        };

        char send_buffer[BUFFER_SIZE];
        char *write_ptr = send_buffer;
        size_t bytes_to_write = BUFFER_SIZE;
        assert(serialize(to_send, &write_ptr, &bytes_to_write));

        try {
            gui_socket_.send_to(boost::asio::buffer(send_buffer, BUFFER_SIZE - bytes_to_write), gui_remote_endpoint_);
        } catch (std::exception &e) {
            std::cerr << "Error: sending message to gui failed" << std::endl;
        }
    }

    bool check_if_block_on_position(Position position) {
        for (auto &block: client_game_info.blocks) {
            if (block == position) {
                return true;
            }
        }
        return false;
    }

    void process_server_message(const ServerMessage &message) {
        const std::lock_guard<std::mutex> client_game_info_lock(client_game_info_mutex);
        if (message.type == ServerMessageType::Hello && !client_game_info.hello_received) {
            auto hello = get<server_message_hello_t>(message.variant);
            client_game_info.hello_received = true;
            client_game_info.server_name = hello.server_name;
            client_game_info.player_count = hello.players_count;
            client_game_info.size_x = hello.size_x;
            client_game_info.size_y = hello.size_y;
            client_game_info.game_length = hello.game_length;
            client_game_info.explosion_radius = hello.explosion_radius;
            client_game_info.bomb_timer = hello.bomb_timer;

            send_lobby_message();
            return;

        } else if (message.type == ServerMessageType::Hello || !client_game_info.hello_received)
            return; // Ignore more than one hello message or any other message before receiving hello

        switch (message.type) {
            case ServerMessageType::AcceptedPlayer: {
                auto accepted_player = get<server_message_accepted_player_t>(message.variant);
                client_game_info.players.insert({accepted_player.id, accepted_player.player});

                // Send lobby message to gui
                send_lobby_message();

                break;
            }

            case ServerMessageType::GameStarted: {
                auto game_started = get<server_message_game_started_t>(message.variant);
                client_game_info.game_started = true;
                client_game_info.players = move(game_started.players);
                for (auto &player: client_game_info.players) {
                    client_game_info.player_positions.insert({player.first, {0, 0}});
                    client_game_info.scores.insert({player.first, 0});
                }
                break;
            }

            case ServerMessageType::Turn: {
                std::set<Position> explosions;
                std::set<PlayerId> destroyed_players;
                std::set<Position> destroyed_blocks;

                auto turn = get<server_message_turn_t>(message.variant);

                // before processing events
                client_game_info.turn = turn.turn;
                for (auto &bomb: client_game_info.bombs) {
                    bomb.second.second -= 1; // tick bomb timer
                }

                // processing events
                for (auto &event: turn.events) {
                    switch (event.type) {
                        case EventType::BlockPlaced: {
                            auto event_desc = get<event_block_placed_t>(event.variant);
                            if (std::find(client_game_info.blocks.begin(), client_game_info.blocks.end(),
                                          event_desc.position) == client_game_info.blocks.end())
                                client_game_info.blocks.push_back(event_desc.position);
                            break;
                        }

                        case EventType::BombPlaced: {
                            auto event_desc = get<event_bomb_placed_t>(event.variant);
                            Bomb bomb = {event_desc.position, client_game_info.bomb_timer};
                            if (client_game_info.bombs.contains(event_desc.id)) {
                                // Server is always right, bomb with this id was just placed (replace it with new one)
                                client_game_info.bombs[event_desc.id] = bomb;
                            } else {
                                client_game_info.bombs.insert({event_desc.id, bomb});
                            }
                            break;
                        }

                        case EventType::PlayerMoved: {
                            auto event_desc = get<event_player_moved_t>(event.variant);
                            if (client_game_info.player_positions.contains(event_desc.id))
                                client_game_info.player_positions[event_desc.id] = event_desc.position;
                            // else: move of unknown player, do nothing
                            break;
                        }

                        case EventType::BombExploded: {
                            auto event_desc = get<event_bomb_exploded_t>(event.variant);
                            if (client_game_info.bombs.contains(event_desc.id)) {
                                auto center = client_game_info.bombs[event_desc.id].first;
                                explosions.insert(center);


                                if (!check_if_block_on_position(center)) {
                                    std::vector<std::pair<int, int>> directions = {{1,  0},
                                                                                   {-1, 0},
                                                                                   {0,  1},
                                                                                   {0,  -1}};
                                    for (auto &direction: directions) {
                                        for (int i = 1; i <= client_game_info.explosion_radius; i++) {
                                            Position new_explosion = {center.first + direction.first * i,
                                                                      center.second + direction.second * i};
                                            if (new_explosion.first >= client_game_info.size_x ||
                                                new_explosion.second >= client_game_info.size_y) {
                                                break;
                                            }
                                            explosions.insert(new_explosion);
                                            if (check_if_block_on_position(new_explosion))
                                                break;
                                        }
                                    }
                                }
                                client_game_info.bombs.erase(event_desc.id);
                            } // else unknown bomb, position unknown
                            for (auto &destroyed_block: event_desc.blocks_destroyed) {
//                                remove_from_vector(client_game_info.blocks, destroyed_block);
                                destroyed_blocks.insert(destroyed_block);
                            }
                            for (auto &destroyed_player: event_desc.robots_destroyed) {
                                destroyed_players.insert(destroyed_player);
                            }
                            break;
                        }
                    }


                }
                // after processing all events
                for (auto &increase_score: destroyed_players) {
                    client_game_info.scores[increase_score] += 1;
                }
                //remove destroyed blocks
                for (auto &block: destroyed_blocks) {
                    remove_from_vector(client_game_info.blocks, block);
                }

                std::vector<Bomb> bomb_vector;
                for (auto &bomb: client_game_info.bombs) {
                    bomb_vector.push_back(bomb.second);
                }

                DrawMessage to_send = {
                        DrawMessageType::Game,
                        draw_message_game_t({
                                                    client_game_info.server_name,
                                                    client_game_info.size_x,
                                                    client_game_info.size_y,
                                                    client_game_info.game_length,
                                                    client_game_info.turn,
                                                    client_game_info.players,
                                                    client_game_info.player_positions,
                                                    client_game_info.blocks,
                                                    bomb_vector,
                                                    std::vector<Position>(explosions.begin(), explosions.end()),
                                                    client_game_info.scores,
                                            })
                };

                char send_buffer[BUFFER_SIZE];
                char *write_ptr = send_buffer;
                size_t bytes_to_write = BUFFER_SIZE;
                assert(serialize(to_send, &write_ptr, &bytes_to_write));


                try {
                    auto endpoint = *udp_resolver_.resolve(/*udp::v6(),*/ gui_address_,
                                                                          gui_port_/*,  boost::asio::ip::resolver_base::all_matching*/).begin();
                    gui_socket_.send_to(boost::asio::buffer(send_buffer, BUFFER_SIZE - bytes_to_write), endpoint);
                } catch (std::exception &e) {
                    std::cerr << "Error: sending message to gui failed" << std::endl;
                }
                break;
            }

            case ServerMessageType::GameEnded: {
                auto game_ended = get<server_message_game_ended_t>(message.variant);
                client_game_info.game_started = false; // game ended waiting for the next one
//                client_game_info.join_sent = false;
                client_game_info.players = {};
                client_game_info.player_positions = {};
                client_game_info.scores = {};
                client_game_info.bombs = {};
                client_game_info.blocks = {};

                send_lobby_message();
                break;
            }

            case ServerMessageType::Hello: {
                assert(false);
                break;
            }
        }
    }

    udp::resolver udp_resolver_;
    std::string gui_address_;
    boost::asio::ip::basic_resolver_entry<udp> gui_endpoint_;
    std::string gui_port_;
    udp::socket gui_socket_;
    udp::endpoint gui_remote_endpoint_;
    boost::array<char, BUFFER_SIZE> gui_recv_buffer_;

    tcp::socket server_socket_;
    boost::array<char, BUFFER_SIZE> server_recv_buffer_;
    std::string server_saved_buffer_;

    std::mutex client_game_info_mutex;
    ClientGameInfo client_game_info;

    std::string player_name;
};

char buff[100];

int main(int argc, char *argv[]) {
    std::string player_name;
    std::string gui_address;
    std::string server_address;
    uint16_t port;

    p_opt::options_description description("Allowed options");
    description.add_options()
            ("gui-address,d", p_opt::value<std::string>(&gui_address),
             "<(nazwa hosta):(port) lub (IPv4):(port) lub (IPv6):(port)>")
            ("help,h", "Wypisuje jak używać programu")
            ("player-name,n", p_opt::value<std::string>(&player_name), "Nazwa gracza")
            ("port,p", p_opt::value<uint16_t>(&port), "Port na którym klient nasłuchuje komunikatów od GUI")
            ("server-address,s", p_opt::value<std::string>(&server_address),
             "<(nazwa hosta):(port) lub (IPv4):(port) lub (IPv6):(port)>");

    p_opt::variables_map var_map;
    p_opt::store(p_opt::parse_command_line(argc, argv, description), var_map);
    p_opt::notify(var_map);

    if (var_map.count("help")) {
        std::cout << description << "\n";
        return 1;
    }

    if (var_map.count("gui-address") == 0) {
        std::cout << "No gui address\n";
        return 1;
    }
    std::cout << gui_address << "\n";

    if (var_map.count("port") == 0) {
        std::cout << "No gui port\n";
        return 1;
    }

    if (var_map.count("server-address") == 0) {
        std::cout << "No server address\n";
        return 1;
    }
    std::cout << server_address << "\n";

    if (var_map.count("player-name") == 0) {
        std::cout << "No player name\n";
        return 1;
    }

    boost::asio::io_context io_context;
    auto split_server_address = split_address(server_address);
    auto split_gui_address = split_address(gui_address);
    client_server client_server(io_context, port,
                                split_server_address.first,
                                split_server_address.second,
                                split_gui_address.first,
                                split_gui_address.second,
                                player_name);
    io_context.run();

    return 0;
}