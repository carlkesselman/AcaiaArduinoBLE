#!/bin/sh
# Host test of the event log (examples/shotStopper/eventlog.h). Needs a C++17 compiler.
set -e
cd "$(dirname "$0")"
mkdir -p build
${CXX:-c++} -std=c++17 -g -Wall -I../../examples/shotStopper test.cpp -o build/test
build/test
