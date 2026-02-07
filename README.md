# Multiplayer Shooter (CLI)

A 2-player networked battleship-style shooter. Each player has a ship at the bottom of their screen and the enemy at the top. Move with **A**/**D**, shoot with **W**. First to reduce the other to 0 lives wins.

## Build

**Always run these commands from the project root** (`multiplayer-shooter/`), not from inside `server/` or `client/`.

If you have **make**:
```bash
make
```

If you don't have make, use the build script:
```bash
chmod +x build.sh
./build.sh
```

Or build manually (from project root):
```bash
g++ -std=c++17 -Wall -O2 -I common -o server/server server/main.cpp
g++ -std=c++17 -Wall -O2 -I common -o client/client client/client.cpp
```

## Run

1. **Start the server** (one machine or same machine):

   ```bash
   ./server/server
   ```

2. **Run two clients** (two terminals, or two machines if you change the client IP from `127.0.0.1`):

   **Terminal 1 – create a room**
   - Enter your name
   - Choose `1) Create Room`
   - Enter a room name (e.g. `battle1`)
   - Wait for "Room created. Waiting for opponent..."

   **Terminal 2 – join the room**
   - Enter your name
   - Choose `2) Join Room`
   - Enter the **same** room name (e.g. `battle1`)

3. The game starts when both players are in. Controls:
   - **A** – move left  
   - **D** – move right  
   - **W** – shoot  

Each player has 3 lives. Dodge enemy bullets and hit the enemy ship to win.

## Requirements

- Linux (uses `termios`, POSIX sockets)
- C++17 compiler (g++ or clang++)

## Troubleshooting

- **"Address already in use"** – Port 5000 is in use (e.g. a previous server still running). Either:
  - Stop the old server (Ctrl+C in its terminal), or
  - Find and kill it: `lsof -i :5000` then `kill <PID>`, or
  - Restart the machine.
- **"make: command not found"** – Use `./build.sh` instead of `make`, or install make: `sudo apt install make`.
- **Build fails with "No such file or directory"** – Make sure you're in the project root: `cd ~/multiplayer-shooter` (or wherever the project is), then run the build again.
