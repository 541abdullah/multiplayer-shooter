#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <cstdlib>
#include <csignal>
#include <termios.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include "../common/json.hpp"

// Alternate screen: no scrollback during game (clean exit when done)
static void exit_alternate_screen() {
    std::cout << "\033[?1049l" << std::flush;
}

using json = nlohmann::json;

int player_id = 0;
std::mutex state_mutex;
std::atomic<bool> game_started{false};

struct Player {
    int id;
    int x, y;
    int lives;
};

struct Bullet {
    int x, y;
    int owner;  // for missile direction
};

std::vector<Player> players;
std::vector<Bullet> bullets;
int sock;

// For player 2, flip Y so "my ship at bottom, enemy at top" on screen
static int display_y(int y) {
    return (player_id == 2) ? (19 - y) : y;
}

// Terminal input without Enter
int getch() {
    struct termios oldt, newt;
    int ch;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}

// Send all bytes (send() can return partial)
static void send_all(const char* buf, size_t len) {
    while (len > 0) {
        ssize_t n = send(sock, buf, len, 0);
        if (n <= 0) return;
        buf += (size_t)n;
        len -= (size_t)n;
    }
}

// Send JSON to server
void send_json(const json &j) {
    std::string s = j.dump() + "\n";
    send_all(s.c_str(), s.size());
}

// Heart display: filled ♥, empty ♡
static std::string lives_hearts(int lives) {
    std::string s;
    for (int i = 0; i < 3; i++) s += (i < lives) ? "\xe2\x99\xa5" : "\xe2\x99\xa1";  // ♥ / ♡
    return s;
}

// Render 20x20 grid (your ship at bottom, enemy at top)
void render() {
    std::lock_guard<std::mutex> lock(state_mutex);
    std::vector<std::string> grid(20, std::string(20, ' '));

    // Pixel-art ships (2 rows each): nose + body
    for (auto &p : players) {
        int dy = display_y(p.y);
        bool is_me = (p.id == player_id);
        if (p.x < 0 || p.x >= 20) continue;
        if (is_me) {
            // My ship (bottom, pointing up): body above, nose at bottom
            if (dy >= 0 && dy < 20) grid[dy][p.x] = '^';       // nose
            if (dy - 1 >= 0 && dy - 1 < 20) grid[dy - 1][p.x] = '*';  // body
        } else {
            // Enemy ship (top, pointing down): nose at top, body below
            if (dy >= 0 && dy < 20) grid[dy][p.x] = 'V';       // nose
            if (dy + 1 >= 0 && dy + 1 < 20) grid[dy + 1][p.x] = '*';  // body
        }
    }

    // Missile-style bullets (head + trail)
    for (auto &b : bullets) {
        int dy = display_y(b.y);
        bool going_up = (b.owner == player_id);  // my bullet goes up toward enemy
        if (b.x < 0 || b.x >= 20) continue;
        if (dy >= 0 && dy < 20) grid[dy][b.x] = '*';  // head
        int tail_dy = going_up ? dy + 1 : dy - 1;
        if (tail_dy >= 0 && tail_dy < 20) grid[tail_dy][b.x] = '|';  // trail
    }

    // Lives: my and enemy (hearts)
    int my_lives = 3, enemy_lives = 3;
    for (auto &p : players) {
        if (p.id == player_id) my_lives = p.lives;
        else enemy_lives = p.lives;
    }

    // Clear screen and move cursor to top (works in threads / WSL)
    std::cout << "\033[2J\033[H";
    std::cout << "  Enemy (top)\n  +--------------------+\n";
    for (auto &row : grid)
        std::cout << "  |" << row << "|\n";
    std::cout << "  +--------------------+\n  You (bottom)\n";
    std::cout << "  My lives:   " << lives_hearts(my_lives) << "   |   Enemy: " << lives_hearts(enemy_lives) << "\n";
    std::cout << "  A/D move, W shoot\n";
    std::cout << std::flush;
}

// Listener thread: parse newline-delimited JSON messages
void listen_server() {
    std::string buf;
    char chunk[1024];
    while (true) {
        int bytes = recv(sock, chunk, sizeof(chunk), 0);
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

                if (type == "STATE_UPDATE") {
                    {
                        std::lock_guard<std::mutex> lock(state_mutex);
                        players.clear();
                        bullets.clear();
                        for (auto &p : msg["players"])
                            players.push_back({p["id"], p["x"], p["y"], p["lives"]});
                        for (auto &b : msg["bullets"])
                            bullets.push_back({b["x"], b["y"], b["owner"]});
                    }
                    render();  // lock released first so render() can acquire it (was deadlock!)
                } else if (type == "GAME_START") {
                    std::cout << "\033[?1049h" << std::flush;  // enter alternate screen (no scrollback)
                    game_started = true;
                    std::cout << "Game started! Use A/D to move, W to shoot.\n" << std::flush;
                } else if (type == "ROOM_CREATED") {
                    player_id = msg["player_id"];
                    std::cout << "Room created. Waiting for opponent...\n";
                } else if (type == "ROOM_JOINED") {
                    player_id = msg["player_id"];
                    std::cout << "Joined room! Game starting...\n" << std::flush;
                } else if (type == "ROOM_FULL") {
                    exit_alternate_screen();
                    std::cout << "Room is full.\n";
                    exit(1);
                } else if (type == "ROOM_NOT_FOUND") {
                    exit_alternate_screen();
                    std::cout << "Room not found.\n";
                    exit(1);
                } else if (type == "GAME_OVER") {
                    int winner = msg["winner_id"];
                    exit_alternate_screen();
                    std::cout << "\nGame Over! Winner: Player " << winner << (winner == player_id ? " (You!)\n" : "\n");
                    exit(0);
                }
            } catch (...) {}
        }
    }
}

// Input thread (only send once game has started)
void input_thread() {
    while (true) {
        int c = getch();
        if (!game_started) continue;
        json input_msg;
        if (c == 'a' || c == 'A') input_msg = {{"type","INPUT"},{"action","MOVE_LEFT"}};
        if (c == 'd' || c == 'D') input_msg = {{"type","INPUT"},{"action","MOVE_RIGHT"}};
        if (c == 'w' || c == 'W') input_msg = {{"type","INPUT"},{"action","SHOOT"}};
        if (!input_msg.empty())
            send_json(input_msg);
    }
}

int main() {
    std::string ip = "127.0.0.1";
    int port = 5000;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect"); return 1;
    }

    std::atexit(exit_alternate_screen);  // restore screen on any exit
    std::signal(SIGINT, [](int) {
        const char* exit_alt = "\033[?1049l";
        (void)write(STDOUT_FILENO, exit_alt, 8);
        _Exit(0);
    });

    int one = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    // Name and room
    std::cout << "Enter your name: ";
    std::string name;
    std::cin >> name;

    std::cout << "1) Create Room\n2) Join Room\nChoice: ";
    int choice;
    std::cin >> choice;

    std::string room;
    std::cout << "Enter room name: ";
    std::cin >> room;

    json msg;
    if (choice == 1) msg = {{"type","CREATE_ROOM"},{"room",room},{"name",name}};
    else msg = {{"type","JOIN_ROOM"},{"room",room},{"name",name}};

    // Start listener and input threads FIRST so we're ready to receive the response
    std::thread(listen_server).detach();
    std::thread(input_thread).detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // let listener reach recv()

    if (choice == 2) std::cout << "Joining room...\n" << std::flush;
    send_json(msg);

    // Keep main alive
    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));

    close(sock);
    return 0;
}
