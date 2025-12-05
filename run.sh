#!/usr/bin/env bash

set -e -o pipefail

# Change to the directory where this script is located
cd "$(dirname "$0")"

# If no args, defer to the binary help.
if [ $# -eq 0 ]; then
    ./build/release/mitscript -h
    exit 1
fi

# Hand off all arguments directly to the binary; it now handles derby/VM/mem/opt.
./build/release/mitscript "$@"
