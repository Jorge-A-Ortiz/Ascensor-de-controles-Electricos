#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../.."
binary=$(mktemp /tmp/elevator-tests.XXXXXX)
trap 'rm -f "$binary"' EXIT
cc -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable \
  -fsanitize=address,undefined -g \
  -I tests/host -I main/include -I main/storage -I main/elevator -I main/rules \
  tests/host/test.c main/elevator/elevator.c main/rules/rules.c -o "$binary"
"$binary"
