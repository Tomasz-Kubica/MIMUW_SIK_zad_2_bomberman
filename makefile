all:

asio_test: asio_test.cpp
	g++ -Wall -Wextra -Wconversion -Werror -O2 -std=gnu++20 -fanalyzer -o asio_test.o asio_test.cpp

client: client.cpp
	g++ -Wall -Wextra -Wconversion -Werror -O2 -std=gnu++20 -fanalyzer -o client.o client.cpp -lboost_program_options

clean:
	rm *.o