#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <map>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include "room.h"
#include "../common/json.hpp"

using json = nlohmann::json;

std::vector<Room> rooms;
std::mutex room_mutex;

static void send_all(int fd, const char* buf, size_t len) {
    while (len > 0) {
        ssize_t n = send(fd, buf, len, 0);
        if (n <= 0) return;
        buf += (size_t)n;
        len -= (size_t)n;
    }
}

void send_json(int fd, const json& j) {
    std::string s = j.dump() + "\n";
    send_all(fd, s.c_str(), s.size());
}

void game_loop(Room* room) {
    const int TICK_MS = 50;
    while (true) {
        {
            std::lock_guard<std::mutex> lock(room_mutex);

          
            for (auto& b : room->state.bullets) {
                b.y += b.direction;

                for (auto& p : room->players) {
                    if (p.id != b.owner && p.x == b.x && p.y == b.y) {
                        p.lives--;
                        b.y = -1; // mark bullet for removal
                    }
                }
            }

        
            room->state.bullets.erase(
                std::remove_if(room->state.bullets.begin(),
                               room->state.bullets.end(),
                               [](Bullet& b){ return b.y < 0 || b.y > 19; }),
                room->state.bullets.end()
            );

      
            for (auto& p : room->players) {
                if (p.lives <= 0) {
                    json msg = {{"type","GAME_OVER"},{"winner_id", (p.id==1?2:1)}};
                    for (auto& pl : room->players)
                        send_json(pl.fd, msg);
                    return; // end loop
                }
            }

            json stateMsg;
            stateMsg["type"] = "STATE_UPDATE";
            stateMsg["players"] = json::array();
            for (auto& p : room->players) {
                stateMsg["players"].push_back({
                    {"id", p.id},
                    {"x", p.x},
                    {"y", p.y},
                    {"lives", p.lives}
                });
            }

            stateMsg["bullets"] = json::array();
            for (auto& b : room->state.bullets) {
                stateMsg["bullets"].push_back({
                    {"x", b.x},
                    {"y", b.y},
                    {"owner", b.owner}
                });
            }

            for (auto& p : room->players)
                send_json(p.fd, stateMsg);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(TICK_MS));
    }
}


void handle_client(int client_fd) {
    int one = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    char chunk[1024];
    std::string buf;
    Player thisPlayer;
    Room* playerRoom = nullptr;

    while (true) {
        int bytes = recv(client_fd, chunk, sizeof(chunk), 0);
        if (bytes <= 0) break;
        buf.append(chunk, bytes);

        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (line.empty()) continue;

            try {
                auto msg = json::parse(line);
                std::string type = msg["type"];

                if (type == "CREATE_ROOM") {
                Room newRoom;
                newRoom.name = msg["room"];

                thisPlayer.fd = client_fd;
                thisPlayer.id = 1;
                thisPlayer.name = msg["name"];
                // Explicit initialization
                thisPlayer.x = 10;
                thisPlayer.y = 19; // bottom row
                thisPlayer.lives = 3;

                newRoom.players.push_back(thisPlayer);

                std::lock_guard<std::mutex> lock(room_mutex);
                rooms.push_back(newRoom);
                playerRoom = &rooms.back();

                json reply = {{"type","ROOM_CREATED"},{"player_id",1}};
                send_json(client_fd, reply);
            }
            else if (type == "JOIN_ROOM") {
                std::string roomName = msg["room"];
                std::lock_guard<std::mutex> lock(room_mutex);
                bool found = false;

                for (auto &r : rooms) {
                    if (r.name == roomName) {
                        if (r.isFull()) {
                            send_json(client_fd, {{"type","ROOM_FULL"}});
                            found = true;
                            break;
                        }

                        thisPlayer.fd = client_fd;
                        thisPlayer.id = 2;
                        thisPlayer.name = msg["name"];
            
                        thisPlayer.x = 10;
                        thisPlayer.y = 0; // top row
                        thisPlayer.lives = 3;

                        r.players.push_back(thisPlayer);
                        playerRoom = &r;

                        send_json(client_fd, {{"type","ROOM_JOINED"},{"player_id",2}});
                        send_json(r.players[0].fd, {{"type","GAME_START"}});
                        send_json(r.players[1].fd, {{"type","GAME_START"}});

                        std::thread(game_loop, &r).detach();
                        found = true;
                        break;
                    }
                }

                if (!found)
                    send_json(client_fd, {{"type","ROOM_NOT_FOUND"}});
            }
            else if (type == "INPUT") {
                if (!playerRoom) continue;

                std::string action = msg["action"];
                std::lock_guard<std::mutex> lock(room_mutex);

                int idx = -1;
                for (size_t i = 0; i < playerRoom->players.size(); i++) {
                    if (playerRoom->players[i].fd == client_fd) { idx = (int)i; break; }
                }
                if (idx < 0) continue;

                Player& p = playerRoom->players[idx];

                if (action == "MOVE_LEFT")
                    p.x = std::max(0, p.x - 1);
                else if (action == "MOVE_RIGHT")
                    p.x = std::min(19, p.x + 1);
                else if (action == "SHOOT") {
                    Bullet b;
                    b.owner = p.id;
                    b.x = p.x;
                    b.y = p.y + (p.id == 1 ? -1 : 1);
                    b.direction = (p.id == 1 ? -1 : 1);
                    playerRoom->state.bullets.push_back(b);
                }
            }

            } catch (...) {
                std::cout << "Invalid JSON from client\n";
            }
        }
    }


    if (playerRoom) {
        std::lock_guard<std::mutex> lock(room_mutex);
        playerRoom->players.erase(
            std::remove_if(playerRoom->players.begin(), playerRoom->players.end(),
                           [client_fd](Player& p){ return p.fd == client_fd; }),
            playerRoom->players.end()
        );
    }

    close(client_fd);
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket failed"); return 1; }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(5000);

    if (bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed"); return 1;
    }

    if (listen(server_fd, 10) < 0) { perror("listen failed"); return 1; }

    std::cout << "Server listening on port 5000...\n";

    std::vector<std::thread> threads;

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd >= 0) {
            std::cout << "Client connected: FD " << client_fd << "\n";
            threads.emplace_back(handle_client, client_fd);
        }
    }

    close(server_fd);
    return 0;
}




