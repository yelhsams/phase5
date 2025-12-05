#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

IMAGE_NAME="mitscript-dev"

if docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
    echo "Docker image '$IMAGE_NAME' already exists. Skipping build."
else
    echo "Building Docker image for x86-64..."
    docker build --platform=linux/amd64 -t "$IMAGE_NAME" "$SCRIPT_DIR"
fi

echo "Launching interactive shell in container..."
echo "Your workspace is mounted at /workspace"
echo "Type 'exit' to leave the container"

# Run the container with interactive shell
# Mount the current directory to /workspace in the container
# Set working directory to /workspace
docker run --platform=linux/amd64 -it --rm \
    -v "$SCRIPT_DIR:/workspace" \
    -w /workspace \
    "$IMAGE_NAME" \
    /bin/bash
