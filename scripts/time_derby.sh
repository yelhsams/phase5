#!/usr/bin/env bash
# Time derby workloads to spot optimization wins.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

if [ $# -lt 1 ]; then
  echo "Usage: $0 <test_dir> [derby opts...]" >&2
  exit 1
fi

TEST_DIR="$1"
shift || true
EXTRA_OPTS=("$@")

if [ ! -d "$TEST_DIR" ]; then
  echo "Error: '$TEST_DIR' is not a directory" >&2
  exit 1
fi

format_row() {
  printf "%-12s %8s %s\n" "$1" "$2" "$3"
}

format_row "Test" "Seconds" "Status"
format_row "------------" "--------" "------"

shopt -s nullglob
for test_file in "$TEST_DIR"/*.mit; do
  base="${test_file%.*}"
  base_name="${base##*/}"
  in_file="${base}.in"
  mem_file="${base}.memlimit"
  mem_flag=()
  if [ -f "$mem_file" ]; then
    mem_flag=("-m" "$(cat "$mem_file")")
  fi

  tmp_time="$(mktemp)"
  tmp_err="$(mktemp)"
  status=0
  cmd=("./run.sh" "derby" "$test_file" "${EXTRA_OPTS[@]}" "${mem_flag[@]}")
  TIMEFORMAT='%R'
  if [ -f "$in_file" ]; then
    { time "${cmd[@]}" <"$in_file" >/dev/null; } 2>"$tmp_err" || status=$?
  else
    { time "${cmd[@]}" >/dev/null; } 2>"$tmp_err" || status=$?
  fi
  elapsed="$(tail -n 1 "$tmp_err")"
  if [ $status -eq 0 ]; then
    format_row "$base_name" "$elapsed" "ok"
  else
    format_row "$base_name" "$elapsed" "exit $status"
  fi
  rm -f "$tmp_time" "$tmp_err"

done
shopt -u nullglob
