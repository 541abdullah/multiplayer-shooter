# Multiplayer Shooter CLI - Build
CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2

SERVER_DIR = server
CLIENT_DIR = client
COMMON = common

.PHONY: all server client clean

all: server client

server:
	$(CXX) $(CXXFLAGS) -I $(COMMON) -o $(SERVER_DIR)/server $(SERVER_DIR)/main.cpp

client:
	$(CXX) $(CXXFLAGS) -I $(COMMON) -o $(CLIENT_DIR)/client $(CLIENT_DIR)/client.cpp

clean:
	rm -f $(SERVER_DIR)/server $(CLIENT_DIR)/client
