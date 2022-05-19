all:

asio_test: asio_test.cpp
	g++ -Wall -Wextra -Wconversion -Werror -O2 -std=gnu++20 -fanalyzer -o asio_test.o asio_test.cpp

client: client.cpp common.h
	g++ -O2 -Wextra -Wconversion -Werror -std=gnu++20 -o client.o client.cpp -lboost_program_options -pthread

clean:
	rm *.o