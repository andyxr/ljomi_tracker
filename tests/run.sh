#!/bin/sh
# Build and run the offline Flux engine tests (no Rack needed). Run from the repo root in an MSYS2 MinGW64 shell.
set -e
mkdir -p tests/out
g++ -std=c++17 -O2 -Wall -Isrc tests/flux_test.cpp src/WavLoad.cpp -o tests/out/flux_test
./tests/out/flux_test
