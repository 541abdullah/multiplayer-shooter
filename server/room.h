#pragma once
#include <string>
#include <vector>

struct Player {
    int fd;         // Client socket
    int id;         // 1 or 2
    std::string name;
    int x;          // horizontal start pos
    int y;          // vertical pos
    int lives;      // 3
};

struct Bullet {
    int x, y;
    int owner;      // player_id
    int direction;  // +1 = down, -1 = up
};

struct GameState {
    std::vector<Player> players; 
    std::vector<Bullet> bullets; 
};

struct Room {
    std::string name;
    std::vector<Player> players;
    GameState state;
    bool isFull() const { return players.size() >= 2; }
};
