#!/usr/bin/env bash

set -euo pipefail

# Change to repo root
cd "$(dirname "$0")"

ROOT_DIR=$(pwd)
ADD_DIR="$ROOT_DIR/additional-tests"

pass=0
fail=0

echo "==> Compiling and running Phase 3 GC tests (public + additional)"

OUT_DIR="$ROOT_DIR/build/phase3-tests"
mkdir -p "$OUT_DIR"

# Simple helper to compile a single C++ test with includes matching public tests
compile_and_run() {
  local src="$1"
  local name
  name=$(basename "$src" .cpp)
  local exe="$OUT_DIR/${name}"
  # For singleton tests (files starting with 's'), include AST implementation to satisfy RTTI used by interpreter.cpp
  local -a extra_src
  if [[ "$name" == s* ]]; then
    extra_src=("$ROOT_DIR/src/mitscript-interpreter/ast.cpp")
  else
    extra_src=()
  fi

  if g++ -std=c++20 -I "$ROOT_DIR/tests/gc" -I "$ROOT_DIR/tests/phase3" "$src" ${extra_src[@]:-} -o "$exe" >/dev/null 2>&1; then
    if "$exe" >/dev/null 2>&1; then
      echo "[PASS] phase3: $(basename "$src")"
      pass=$((pass+1))
    else
      echo "[FAIL] phase3 (runtime): $(basename "$src")"
      fail=$((fail+1))
    fi
  else
    echo "[FAIL] phase3 (compile): $(basename "$src")"
    fail=$((fail+1))
  fi
}

echo ""
echo "-- Public Phase 3 tests --"
for src in "$ROOT_DIR"/tests/phase3/public/t*.cpp; do
  [ -e "$src" ] || continue
  compile_and_run "$src"
done

echo ""
echo "-- Additional Phase 3 tests --"
for src in "$ADD_DIR"/phase3/*.cpp; do
  [ -e "$src" ] || continue
  compile_and_run "$src"
done

echo ""
echo "==> Summary (Phase 3)"
echo "Passed: $pass"
echo "Failed: $fail"

if [ "$fail" -ne 0 ]; then
  exit 1
fi
exit 0
