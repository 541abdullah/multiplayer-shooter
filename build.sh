#!/bin/bash
# Build from project root (multiplayer-shooter/). No make required.
set -e
cd "$(dirname "$0")"
echo "Building server..."
g++ -std=c++17 -Wall -O2 -I common -o server/server server/main.cpp
echo "Building client..."
g++ -std=c++17 -Wall -O2 -I common -o client/client client/client.cpp
echo "Done. Run ./server/server then ./client/client (in two terminals)."
