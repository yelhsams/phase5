#!/usr/bin/env bash
#
# Run all tests in a directory (argument), skipping any whose filename contains
# "bad". For each .mit test, compile to bytecode then run the VM; for bare
# .mitbc, run the VM directly. Compare stdout against the matching .out file.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

if [ $# -ne 1 ]; then
  echo "Usage: $0 <test_dir>" >&2
  exit 1
fi

TEST_DIR="$1"
if [ ! -d "$TEST_DIR" ]; then
  echo "Error: '$TEST_DIR' is not a directory" >&2
  exit 1
fi

pass=0
fail=0

shopt -s nullglob
tests=()
for f in "$TEST_DIR"/*.mit; do
  [ -e "$f" ] && tests+=("$f")
done
for f in "$TEST_DIR"/*.mitbc; do
  base="${f%.mitbc}.mit"
  # Skip .mitbc if a source .mit exists.
  if [ ! -f "$base" ]; then
    tests+=("$f")
  fi
done
shopt -u nullglob

for test_file in "${tests[@]}"; do
  [ -e "$test_file" ] || continue

  base="${test_file%.*}"
  base_name="${base##*/}"
  rel_name="${TEST_DIR#*/}/${base_name}"

  is_bad=false
  [[ "$base_name" == *bad* ]] && is_bad=true

  out_file="${base}.out"
  in_file="${base}.in"

  # For non-"bad" tests, an expected output file is required.
  if [ "$is_bad" = false ] && [ ! -f "$out_file" ]; then
    printf "SKIP %-24s (missing %s)\n" "$rel_name" "$out_file"
    continue
  fi

  tmp_out="$(mktemp)"
  tmp_bc="$(mktemp)"
  run_status=0

  if [[ "$test_file" == *.mit ]]; then
    # Use the derby subcommand to compile and execute in one step.
    if [ -f "$in_file" ]; then
      ./run.sh derby "$test_file" -O all <"$in_file" >"$tmp_out" 2>&1 || run_status=$?
    else
      ./run.sh derby "$test_file" -O all >"$tmp_out" 2>&1 || run_status=$?
    fi
  else
    if [ -f "$in_file" ]; then
      ./run.sh vm "$test_file" -O all <"$in_file" >"$tmp_out" 2>&1 || run_status=$?
    else
      ./run.sh vm "$test_file" -O all >"$tmp_out" 2>&1 || run_status=$?
    fi
  fi

  if [ "$is_bad" = true ]; then
    if [ $run_status -ne 0 ]; then
      printf "PASS %-24s (bad test expected non-zero)\n" "$rel_name"
      pass=$((pass + 1))
    else
      printf "FAIL %-24s (bad test exited 0)\n" "$rel_name"
      fail=$((fail + 1))
    fi
  else
    if [ $run_status -ne 0 ]; then
      printf "FAIL %-24s (runtime error)\n" "$rel_name"
      fail=$((fail + 1))
    elif diff -u "$out_file" "$tmp_out" >/dev/null; then
      printf "PASS %-24s\n" "$rel_name"
      pass=$((pass + 1))
    else
      printf "FAIL %-24s (output mismatch)\n" "$rel_name"
      fail=$((fail + 1))
    fi
  fi

  rm -f "$tmp_out" "$tmp_out.compile.err" "$tmp_bc"
done

printf "\nSummary: %d passed, %d failed\n" "$pass" "$fail"
exit $fail
