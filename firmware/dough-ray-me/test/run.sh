#!/usr/bin/env sh
# Build and run the dough-ray-me host tests (pure logic, no Arduino toolchain).
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -Wall -Wextra -o /tmp/drm_test_control test_control.cpp
/tmp/drm_test_control
