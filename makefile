all: client #server

client: client.cpp common.h
	g++ -O2 -Wextra -Wconversion -Werror -std=gnu++20 -o robots-client client.cpp -lboost_program_options -pthread

#server: server.cpp common.h
#	g++ -O2 -Wextra -Wconversion -Werror -std=gnu++20 -o robots-server server.cpp -lboost_program_options -pthread

clean:
	rm -f robots-client robots-server *.o