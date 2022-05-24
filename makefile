all: client server

asio_test: asio_test.cpp
	g++ -Wall -Wextra -Wconversion -Werror -O2 -std=gnu++20 -fanalyzer -o asio_test.o asio_test.cpp

client: client.cpp common.h
	g++ -O2 -Wextra -Wconversion -Werror -std=gnu++20 -o robots-client client.cpp -lboost_program_options -pthread

server: server.cpp common.h
	g++ -O2 -Wextra -Wconversion -Werror -std=gnu++20 -o robots-server server.cpp -lboost_program_options -pthread

clean:
	rm -f robots-client robots-server *.o