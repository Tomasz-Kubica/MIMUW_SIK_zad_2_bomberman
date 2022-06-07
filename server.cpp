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

/* = = = = = = = = = *
 * UTILITY FUNCTIONS *
 * = = = = = = = = = */



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
    TurnNo turn_no = 1; // or 0 ??? TODO
    std::vector<server_message_turn_t> turns;
    std::unordered_map<PlayerId, Position> players_positions;
    std::unordered_map<PlayerId, Score> scores;
    std::unordered_map<BombId, Bomb> bombs;
    std::set<Position> blocks;
};

bool is_game_played = false;
PlayerId next_player_id = 0;
std::unordered_map<PlayerId, Player> accepted_players;
game_data_t game_data;
std::unordered_map<PlayerId, PlayerAction> selected_actions;

std::condition_variable conditional_new_data;
std::mutex mutex_new_data;

/* = = = = = = = = = = = = = = = = = = = = = = = = = *
 * DATA SEND FROM SERVER TO THREADS MANAGING CLIENTS *
 * = = = = = = = = = = = = = = = = = = = = = = = = = */

struct SynchronizedMessagesQueue {
    std::mutex mutex;
    std::queue<ServerMessage> messages_queue;
};

std::set<std::shared_ptr<SynchronizedMessagesQueue>> clients_queues;

/* = = = = = = = = = = = = = = = = = = = *
 * CLASS HANDLING CONNECTION WITH PLAYER *
 * = = = = = = = = = = = = = = = = = = = */

class player_connection : public boost::enable_shared_from_this<player_connection> {
public:
    typedef boost::shared_ptr<player_connection> pointer;

    static pointer create(boost::asio::io_context& io_context) {
        return pointer(new player_connection(io_context));
    }

    tcp::socket& socket() {
        return socket_;
    }

    void send_message(const ServerMessage &message) {
        char buff[BUFFER_SIZE];
        char *buff_ptr = buff;
        size_t buff_size = BUFFER_SIZE;
        assert(serialize(message, &buff_ptr, &buff_size));

        boost::asio::async_write(socket_, boost::asio::buffer(buff, BUFFER_SIZE - buff_size),
                                 boost::bind(&player_connection::handle_write, shared_from_this(),
                                             boost::asio::placeholders::error,
                                             boost::asio::placeholders::bytes_transferred));
    }

    void start() {
        boost::asio::ip::tcp::no_delay no_delay_option(true);
        socket_.set_option(no_delay_option);
        send_message(hello_message);
        start_receive();
        send_new_data();
    }

private:
    player_connection(boost::asio::io_context& io_context)
            : socket_(io_context) {}

    void start_receive() {
        socket_.async_receive(
                boost::asio::buffer(recv_buffer_),
                boost::bind(&player_connection::handle_receive, this,
                            boost::asio::placeholders::error,
                            boost::asio::placeholders::bytes_transferred));
    }

    void handle_receive(
            const boost::system::error_code &error,
            std::size_t bytes_transferred) {
        if (error == boost::asio::error::eof) {
            std::cerr << "Connection with client closed" << std::endl;
            return;
        } else if (error) {
            std::cerr << "Error receiving message from client failed" << std::endl;
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
                std::cerr << "Error: incorrect message from client" << std::endl;
                // TODO close connection
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
                if (is_game_played)
                    return;
                auto player_name = std::get<std::string>(message.variant);
                Player player = {player_name, get_client_address()};
                player_id_ = next_player_id;
                next_player_id++;
                accepted_players.insert({
                    player_id_,
                    player
                });
                is_playing_ = true;

                ServerMessage accepted_player_message({
                    ServerMessageType::AcceptedPlayer,
                    server_message_accepted_player_t({
                        player_id_,
                        player
                    })});
                send_message(accepted_player_message);

                // TODO check if game should start
                break;
            }
            case ClientMessageType::PlaceBomb: {
                if (is_playing_ /*&& is_game_played && accepted_players.at(player_id_).second == get_client_address()*/) {
                    PlayerAction action;
                    action.type = PlayerActionType::PlaceBomb;
                    selected_actions[player_id_] = action;
                }
                break;
            }
            case ClientMessageType::PlaceBlock: {
                if (is_playing_) {
                    PlayerAction action;
                    action.type = PlayerActionType::PlaceBlock;
                    selected_actions[player_id_] = action;
                }
                break;
            }
            case ClientMessageType::Move: {
                if (is_playing_) {
                    PlayerAction action;
                    action.type = PlayerActionType::Move;
                    action.direction = std::get<Direction>(message.variant);
                    selected_actions[player_id_] = action;
                }
                break;
            }
        }
    }

    void send_new_data() {
        std::unique_lock<std::mutex> data_lock(data_mutex);
        while(true /* TODO add some condition */) {
            if (is_game_played && !is_game_played_) {
                // Game started
                is_game_played_ = true;
                ServerMessage game_started_message({
                    ServerMessageType::GameStarted,
                    server_message_game_started_t({
                        accepted_players
                    })});
                last_send_turn_ = 0;
            }

            if (!is_game_played && is_game_played_) {
                // Game ended
                is_game_played_ = false;
                ServerMessage game_ended_message({
                    ServerMessageType::GameEnded,
                    server_message_game_ended_t({
                        game_data.scores
                    })});
            }
            conditional_new_data.wait(data_lock, []{return true;});
        }
    }

    void handle_write(const boost::system::error_code& /*error*/,
                      size_t /*bytes_transferred*/) {}

    std::string get_client_address() {
        return socket_.remote_endpoint().address().to_string();
    }

    tcp::socket socket_;
    boost::array<char, BUFFER_SIZE> recv_buffer_;
    std::string saved_buffer_;
    PlayerId player_id_;
    bool is_playing_ = false;
    bool is_game_played_ = false; // represents if client thinks that game is played
    TurnNo last_send_turn_ = 0;
};


/* = = = = = = = = = = = = = = = = = = = = = = = *
 * CLASS HANDLING ACCEPTING INCOMING CONNECTIONS *
 * = = = = = = = = = = = = = = = = = = = = = = = */

class tcp_server {
public:
    tcp_server(boost::asio::io_context& io_context)
            : io_context_(io_context),
              acceptor_(io_context, tcp::endpoint(tcp::v6(), port)) {
        std::cerr << "Server waiting for incoming connections" << std::endl;
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
            std::cerr << "New connection received" << std::endl;
            new_connection->start();
        }

        start_accept();
    }

    boost::asio::io_context& io_context_;
    tcp::acceptor acceptor_;
};

int main(int argc, char *argv[]) {

    auto time_now = std::chrono::system_clock::now().time_since_epoch().count();

    try {
        p_opt::options_description description("Allowed options");
        description.add_options()
                ("help,h", "Wypisuje jak używać programu")
                ("bomb-timer,b", p_opt::value<uint16_t>(&bomb_timer)->required(), "czas trwania tury w milisekundach")
                ("players-count,c", p_opt::value<uint8_t>(&players_count)->required(), "liczba grających graczy")
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
            return 1;
        }

        p_opt::notify(var_map);
    }
    catch (std::exception &e) {
        std::cout << e.what() << '\n';
        return 1;
    }

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

    return 0;
}