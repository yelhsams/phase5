#!/usr/bin/env bash
# Run all Phase 2 private tests: compile -> vm -> diff expected
# Usage: ./run_phase2_private_tests.sh

set -euo pipefail
IFS=$'\n\t'

# Colors
if [[ -t 1 ]]; then
  RED="\033[31m"; GREEN="\033[32m"; YELLOW="\033[33m"; CYAN="\033[36m"; BOLD="\033[1m"; RESET="\033[0m"
else
  RED=""; GREEN=""; YELLOW=""; CYAN=""; BOLD=""; RESET=""
fi

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_DIR="$ROOT_DIR/tests/phase2/private"
RUN_SH="$ROOT_DIR/run.sh"

if [[ ! -x "$RUN_SH" ]]; then
  echo -e "${RED}Error:${RESET} run.sh not found or not executable at $RUN_SH" >&2
  exit 1
fi

# Gather tests (portable for macOS bash 3.x)
shopt -s nullglob
TESTS=("$TEST_DIR"/*.mit)
shopt -u nullglob

# Filter to only those with matching .out files
FILTERED_TESTS=()
for mit in "${TESTS[@]}"; do
  base="${mit%*.mit}"
  if [[ -f "${base}.out" ]]; then
    FILTERED_TESTS+=("$mit")
  fi
done

if [[ ${#FILTERED_TESTS[@]} -eq 0 ]]; then
  echo -e "${YELLOW}No tests found with matching .out files in${RESET} $TEST_DIR"
  exit 0
fi

passes=0
failures=0
skipped=0
start_ts=$(date +%s)

echo -e "${BOLD}${CYAN}Running Phase 2 private tests (${#FILTERED_TESTS[@]} total)${RESET}\n"

for mit in "${FILTERED_TESTS[@]}"; do
  base_name="$(basename "$mit" .mit)"
  expected="$TEST_DIR/${base_name}.out"
  input_file="$TEST_DIR/${base_name}.in"

  tmp_bc="$(mktemp "${ROOT_DIR}/.phase2.${base_name}.XXXXXX")"
  tmp_out="$(mktemp "${ROOT_DIR}/.phase2.${base_name}.XXXXXX")"

  # Compile to bytecode file so VM stdin remains available for program input.
  set +e
  compile_output="$($RUN_SH compile "$mit" -o "$tmp_bc" 2>&1)"
  compile_status=$?
  set -e

  if [[ $compile_status -ne 0 ]]; then
    echo -e "${RED}FAIL${RESET} ${base_name} (compile error)"
    echo "----- begin compiler output -----"
    printf "%s\n" "$compile_output" | sed -n '1,200p'
    echo "----- end compiler output -----"
    rm -f "$tmp_bc"
    rm -f "$tmp_out"
    ((failures++))
    continue
  fi

  # Run VM, feeding .in file if present.
  set +e
  if [[ -f "$input_file" ]]; then
    "$RUN_SH" vm "$tmp_bc" < "$input_file" >"$tmp_out" 2>&1
  else
    "$RUN_SH" vm "$tmp_bc" >"$tmp_out" 2>&1
  fi
  cmd_status=$?
  set -e

  if [[ $cmd_status -ne 0 ]]; then
    echo -e "${RED}FAIL${RESET} ${base_name} (non-zero exit)"
    echo "----- begin output -----"
    sed -n '1,200p' "$tmp_out"
    echo "----- end output -----"
    rm -f "$tmp_bc"
    rm -f "$tmp_out"
    ((failures++))
    continue
  fi

  # Diff actual vs expected
  set +e
  diff_out="$(diff -u --label "${base_name}.out" --label "<actual>" "$expected" "$tmp_out" 2>&1)"
  diff_status=$?
  set -e

  if [[ $diff_status -eq 0 ]]; then
    echo -e "${GREEN}PASS${RESET} ${base_name}"
    ((passes++))
  else
    echo -e "${RED}FAIL${RESET} ${base_name}"
    echo "----- begin diff (${base_name}) -----"
    printf "%s\n" "$diff_out" | sed -n '1,200p'
    echo "----- end diff (${base_name}) -----"
    ((failures++))
  fi

  rm -f "$tmp_bc"
  rm -f "$tmp_out"

done

end_ts=$(date +%s)
elapsed=$((end_ts - start_ts))

echo
echo -e "${BOLD}Summary:${RESET} ${GREEN}${passes} passed${RESET}, ${RED}${failures} failed${RESET}, ${YELLOW}${skipped} skipped${RESET}; total ${#FILTERED_TESTS[@]} (in ${elapsed}s)"

if [[ $failures -gt 0 ]]; then
  exit 1
fi
exit 0
