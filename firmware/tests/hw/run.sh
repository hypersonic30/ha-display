#!/bin/sh
# Host test of the hardware-configuration checks (no ESP32 needed): ./run.sh
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -I shim -I ../../include hw_validate_test.cpp ../../src/hw.cpp -o /tmp/hw_validate_test
/tmp/hw_validate_test
g++ -std=c++17 -I ../../include beep_test.cpp -o /tmp/beep_test
/tmp/beep_test
