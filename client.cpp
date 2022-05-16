#include <iostream>
#include <boost/program_options.hpp>
#include <boost/asio.hpp>

namespace p_opt = boost::program_options;

int main(int argc, char *argv[]) {
    p_opt::options_description description("Allowed options");
    description.add_options()
            ("gui-address,d", "<(nazwa hosta):(port) lub (IPv4):(port) lub (IPv6):(port)>")
            ("help,h", "Wypisuje jak używać programu")
            ("player-name,n", "Nazwa gracza")
            ("port,p", "Port na którym klient nasłuchuje komunikatów od GUI")
            ("server-address,s", "<(nazwa hosta):(port) lub (IPv4):(port) lub (IPv6):(port)>")
    ;

    p_opt::variables_map var_map;
    p_opt::store(p_opt::parse_command_line(argc, argv, description), var_map);
    p_opt::notify(var_map);

    if (var_map.count("help")) {
        std::cout << description << "\n";
        return 1;
    }

    return 0;
}