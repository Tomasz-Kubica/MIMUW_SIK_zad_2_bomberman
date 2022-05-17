#include <iostream>
#include <string>
#include <boost/program_options.hpp>
#include <boost/array.hpp>
#include <boost/bind/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>

#include "common.h"

#define BUFFER_SIZE 20

using boost::asio::ip::udp;
using boost::asio::ip::tcp;

namespace p_opt = boost::program_options;

class client_server {
public:
    client_server(boost::asio::io_context &io_context, uint16_t gui_port, std::string server_address)
            : gui_socket_(io_context, udp::endpoint(udp::v6(), gui_port)), server_socket_(io_context) {

        try {
            std::cerr << "start connecting to server\n";
            tcp::resolver resolver(io_context);
            tcp::resolver::results_type server_endpoints = resolver.resolve(server_address, "daytime");
            std::cerr << "endpoints found\n";
            boost::asio::connect(server_socket_, server_endpoints);
            std::cerr << "connected to server\n";
        }
        catch (std::exception& e)
        {
            std::cerr << e.what() << std::endl;
            return;
        }

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

    void handle_receive_from_gui(const boost::system::error_code &error,
                                 std::size_t bytes_transferred) {
        if (!error) {
//            boost::shared_ptr<std::string> message(
//                    new std::string(make_daytime_string()));

//            socket_.async_send_to(boost::asio::buffer(*message), remote_endpoint_,
//                                  boost::bind(&udp_server::handle_send, this, message,
//                                              boost::asio::placeholders::error,
//                                              boost::asio::placeholders::bytes_transferred));

            std::cout << "received bytes: " << bytes_transferred << "\n";
            std::cout << "first byte as int: " << (int) gui_recv_buffer_[0] << "\n";

            start_receive_from_gui();
        }
    }

    void handle_receive_from_server(const boost::system::error_code &error,
                                    std::size_t bytes_transferred) {

        std::cout << "bytes received from server: " << bytes_transferred << "\n";
        start_receive_from_server();
    }

    void handle_send(boost::shared_ptr<std::string> /*message*/,
                     const boost::system::error_code & /*error*/,
                     std::size_t /*bytes_transferred*/) {}

    udp::socket gui_socket_;
    udp::endpoint gui_remote_endpoint_;
    boost::array<char, BUFFER_SIZE> gui_recv_buffer_;

    tcp::socket server_socket_;
    boost::array<char, BUFFER_SIZE> server_recv_buffer_;
};

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
    std::cout << "gui address: " << gui_address << "\n";

    if (var_map.count("port") == 0) {
        std::cout << "No gui port\n";
        return 1;
    }
    std::cout << "port: " << port << "\n";

    if (var_map.count("server-address") == 0) {
        std::cout << "No server address\n";
        return 1;
    }
    std::cout << "server address: " << server_address << "\n";


    if (var_map.count("player-name") == 0) {
        std::cout << "No player name\n";
        return 1;
    }
    std::cout << "player-name: " << player_name << "\n";

    boost::asio::io_context io_context;
    client_server client_server(io_context, port, server_address);

    return 0;
}