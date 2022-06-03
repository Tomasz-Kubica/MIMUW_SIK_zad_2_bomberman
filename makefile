all: client #server

client: client.cpp common.h
	export LD_LIBRARY_PATH=/opt/gcc-11.2/lib64
	/opt/gcc-11.2/bin/g++-11.2 -O2 -Wextra -Wconversion -Werror -std=gnu++20 -o robots-client client.cpp -lboost_program_options -pthread

#server: server.cpp common.h
#	g++ -O2 -Wextra -Wconversion -Werror -std=gnu++20 -o robots-server server.cpp -lboost_program_options -pthread

clean:
	rm -f robots-client robots-server *.o