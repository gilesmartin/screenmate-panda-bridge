#!/bin/sh
# Linux fixtures only: no Android transport or real CAN devices are touched.
set -eu
if [ "$(uname -s)" != Linux ]; then
  printf '%s
' 'Full socket/ring tests require Linux (SO_BINDTODEVICE and lo). Use Linux CI; do not weaken production interface checks for macOS.' >&2
  exit 2
fi
cd "$(CDPATH="" cd -- "$(dirname -- "$0")/.." && pwd)"
mkdir -p build
python3 -m unittest discover -s tests -p 'test_*.py' -v
for name in raw_mcu_ring stock_guard session_inventory proc_fd_inventory; do
  "${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror -pthread "tests/test_${name}.cpp" -o "build/test_${name}"
  "build/test_${name}"
done
