#!/bin/bash
# Fast path: DSP unit tests with no JUCE and no CMake.
set -e
cd "$(dirname "$0")/../.."
mkdir -p build-tests
g++ -std=c++20 -O2 -Wall -Wextra -o build-tests/dscount_tests Tests/Unit/*.cpp Source/DSP/Engine.cpp Source/DSP/DeEsser.cpp Source/DSP/Maximizer.cpp Source/DSP/Chain.cpp
cd build-tests && ./dscount_tests "$@"
