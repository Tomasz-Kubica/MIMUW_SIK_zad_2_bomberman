#include <iostream>
#include <string>
#include <boost/program_options.hpp>
#include <boost/asio.hpp>

namespace p_opt = boost::program_options;

int main(int argc, char *argv[]) {
    std::string player_name;
    std::string gui_address;
    std::string server_address;
    uint16_t port;

    p_opt::options_description description("Allowed options");
    description.add_options()
            ("gui-address,d", p_opt::value<std::string>(&gui_address), "<(nazwa hosta):(port) lub (IPv4):(port) lub (IPv6):(port)>")
            ("help,h", "Wypisuje jak używać programu")
            ("player-name,n", p_opt::value<std::string>(&player_name), "Nazwa gracza")
            ("port,p", p_opt::value<uint16_t>(&port), "Port na którym klient nasłuchuje komunikatów od GUI")
            ("server-address,s", p_opt::value<std::string>(&server_address), "<(nazwa hosta):(port) lub (IPv4):(port) lub (IPv6):(port)>")
    ;

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

    return 0;
}