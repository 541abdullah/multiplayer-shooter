#pragma once
#include <string>
#include <vector>

struct Player {
    int fd;        
    int id;        
    std::string name;
    int x;          
    int y;          
    int lives;     
};

struct Bullet {
    int x, y;
    int owner;      
    int direction;  
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
