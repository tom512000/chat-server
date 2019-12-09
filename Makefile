ASIO=/home/bib/aassif/asio-1.12.2

server: server.hpp main.cpp
	c++ -std=c++14 -DASIO_STANDALONE -I${ASIO}/include -pthread main.cpp -o server

