#!/bin/bash
set -euo pipefail

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
  echo "Usage: $0 [TEST_DIR]" >&2
  echo "  TEST_DIR  Directory containing *.mit tests (default: tests/phase2/public)" >&2
  echo "            Classification rule: tests with 'exn' anywhere in the file path are BAD; others are GOOD." >&2
  exit 0
fi

TEST_DIR=${1:-tests/phase2/public}

if [ ! -d "$TEST_DIR" ]; then
  echo "Error: test directory '$TEST_DIR' does not exist" >&2
  exit 2
fi

RUN="./run.sh interpret"

good_total=0
good_pass=0
bad_total=0
bad_pass=0

echo "Running interpreter tests in $TEST_DIR (classifying by 'exn' in file path)"
echo

shopt -s nullglob

# Collect all .mit files and classify by presence of 'exn' in the path
good_files=()
bad_files=()
for f in "$TEST_DIR"/*.mit; do
  [ -e "$f" ] || continue
  if [[ "$f" == *exn* ]]; then
    bad_files+=("$f")
  else
    good_files+=("$f")
  fi
done

# Good tests
for f in "${good_files[@]:-}"; do
  [ -e "$f" ] || continue
  good_total=$((good_total+1))
  base="${f%.mit}"
  expected="${base}.mit.out"
  [ -f "$expected" ] || expected="${base}.out"
  if [ ! -f "$expected" ]; then
    echo "GOOD MISSING OUT: $f (expected $expected)"
    continue
  fi
  stdout_file=$(mktemp)
  stderr_file=$(mktemp)
  if $RUN "$f" >"$stdout_file" 2>"$stderr_file"; then
    if [ -s "$stderr_file" ]; then
      echo "GOOD FAIL (stderr not empty): $f"
    elif diff -u "$expected" "$stdout_file" > /dev/null; then
      echo "GOOD PASS: $f"
      good_pass=$((good_pass+1))
    else
      echo "GOOD FAIL (diff): $f"
      diff -u "$expected" "$stdout_file" || true
    fi
  else
    echo "GOOD FAIL (non-zero exit): $f"
    cat "$stderr_file"
  fi
  rm -f "$stdout_file" "$stderr_file"
done

# Bad tests
for f in "${bad_files[@]:-}"; do
  [ -e "$f" ] || continue
  bad_total=$((bad_total+1))
  output=$($RUN "$f" 2>&1 || true)
  if [[ "$output" == *Exception* ]]; then
    echo "BAD PASS: $f"
    bad_pass=$((bad_pass+1))
  else
    echo "BAD FAIL: $f"
    echo "$output"
  fi
done

echo
echo "Summary:"
echo "  Good: $good_pass / $good_total"
echo "  Bad : $bad_pass / $bad_total"

if [ $good_pass -eq $good_total ] && [ $bad_pass -eq $bad_total ]; then
  exit 0
else
  exit 1
fi
