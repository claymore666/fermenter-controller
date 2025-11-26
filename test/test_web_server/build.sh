#!/bin/bash
# Build the web server test

set -e

echo "Building web server test..."

g++ -std=c++17 \
    -I../../include \
    -pthread \
    -O2 \
    test_web_server.cpp \
    -lssl -lcrypto \
    -o web_server

echo "Build complete!"
echo ""
echo "Run with: ./web_server"
echo "Then open: http://localhost:8080/admin.html"
echo "Password: admin"
