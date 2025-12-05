#!/bin/bash
set -euo pipefail

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
  echo "Usage: $0 [TEST_DIR] [GOOD_PATTERN] [BAD_PATTERN]" >&2
  echo "  TEST_DIR      Directory containing good*.mit / bad*.mit (default: tests/phase2/public)" >&2
  echo "  GOOD_PATTERN  Glob (without path) for good tests (default: good*.mit)" >&2
  echo "  BAD_PATTERN   Glob (without path) for bad tests (default: bad*.mit)" >&2
  exit 0
fi

TEST_DIR=${1:-tests/phase2/public}
GOOD_GLOB=${2:-good*.mit}
BAD_GLOB=${3:-bad*.mit}

if [ ! -d "$TEST_DIR" ]; then
  echo "Error: test directory '$TEST_DIR' does not exist" >&2
  exit 2
fi

RUN="./run.sh interpret"

good_total=0
good_pass=0
bad_total=0
bad_pass=0

echo "Running interpreter tests in $TEST_DIR"\
  " (good: $GOOD_GLOB, bad: $BAD_GLOB)"
echo

# Good tests
shopt -s nullglob
for f in "$TEST_DIR"/$GOOD_GLOB; do
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
  if ./run.sh interpret "$f" >"$stdout_file" 2>"$stderr_file"; then
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
for f in "$TEST_DIR"/$BAD_GLOB; do
  [ -e "$f" ] || continue
  bad_total=$((bad_total+1))
  output=$(./run.sh interpret "$f" 2>&1 || true)
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
