all: client

# server: server.hpp
# 	g++ server.hpp -o server `pkg-config fuse --cflags --libs`

client: client.cpp server.hpp
	g++ client.cpp -o client `pkg-config fuse --cflags --libs`
