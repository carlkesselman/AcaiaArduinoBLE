#!/bin/sh
# Host test of the paddle-interlock firmware: the real shotStopper.ino and
# AcaiaArduinoBLE library, built for this computer against stub Arduino / BLE
# headers and run through scripted scenarios in virtual time.
#
#   ./run.sh              run every scenario, print one summary line each
#   ./run.sh NAME -v      run one scenario with the firmware's serial output
#
# Needs a C++17 compiler (clang++ or g++). Nothing here runs on the ESP32.
set -e
cd "$(dirname "$0")"
ROOT=../..
CXX=${CXX:-c++}
mkdir -p build/fw
# 32-bit millis() arithmetic, as on the ESP32
sed 's/\r$//; s/unsigned long/uint32_t/g' $ROOT/AcaiaArduinoBLE.cpp > build/fw/AcaiaArduinoBLE.cpp
sed 's/\r$//; s/unsigned long/uint32_t/g' $ROOT/AcaiaArduinoBLE.h > build/fw/AcaiaArduinoBLE.h
sed 's/unsigned long/uint32_t/g; s/1000ULL/1000u/g' $ROOT/examples/shotStopper/shotStopper.ino > build/fw/shotStopper.ino.h
$CXX -std=c++17 -g -Istubs -Ibuild -Ibuild/fw -DPADDLE_INTERLOCK=true -DMOMENTARY=false -DARDUINO_ESP32C3_DEV \
  -w harness.cpp build/fw/AcaiaArduinoBLE.cpp -o build/harness
if [ -n "$1" ]; then
  exec build/harness "$@"
fi
for s in $(build/harness); do
  build/harness "$s" > "build/$s.log" 2>&1 || true
  printf '%-18s %s | %s\n' "$s" \
    "$(grep 'END:' "build/$s.log" | sed 's/.*END: //; s/, K1.*//')" \
    "$(grep '^   summary:' "build/$s.log" | sed 's/.*summary: *//')"
done
echo "Logs: tests/interlock/build/*.log   (a 'req-3 violation' during a reset is the board's"
echo "pull-down closing K1 while the ESP32 restarts: hardware behaviour, not a firmware fault)"
