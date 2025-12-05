# Skeleton

This repository hosts the skeleton code for 6.1120, a class on building a virtual machine for a Python-like language called MITScript.

## Setup

### Docker

We recommend you use Docker for development. You'll first need to [install Docker Desktop](https://docs.docker.com/get-docker/).

To start a Docker container, run the following command in this directory:

```sh
$ ./docker.sh
Docker image 'mitscript-dev' already exists. Skipping build.
Launching interactive shell in container...
Your workspace is mounted at /workspace
Type 'exit' to leave the container
root@bc0efce609d6:/workspace#
```

The very first time you run `docker.sh` will cause it to build the Docker image described in `Dockerfile`. It may take up to a 15 or so minutes to build the container for the first time.

Once the shell starts, just write commands like you were using a regular
Linux terminal. The `/workspace` directory in the container is mapped to this directory on your machine.

```sh
root@bc0efce609d6:/workspace# ls
CMakeLists.txt  Dockerfile  Makefile  README.md  build  build.sh  docker.sh  run.sh  src  test.sh
```

The Docker container stops automatically when your shell exits.
You can restart it from where you left off, by running `docker.sh` again.

### Dependencies

If you're not using Docker, install the following dependencies on your local system:

#### Ubuntu

```sh
sudo apt install git build-essential

# optionally, if you want to use ANTLR for P1
sudo apt install default-jre libantlr4-runtime-dev

# optionally, if you want to use flex/bison for P1
sudo apt install flex bison

# faster CMake backend for faster builds
sudo apt install ninja-build

# to help with debugging
sudo apt install valgrind

# to help with benchmarking
sudo apt install hyperfine
```

#### macOS

If you're on macOS, you can install the dependencies using Homebrew:

```sh
# Install Homebrew if you don't have it
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install basic build tools
brew install git cmake

# optionally, if you want to use ANTLR for P1
brew install openjdk antlr4-cpp-runtime

# optionally, if you want to use flex/bison for P1
brew install flex bison

# faster CMake backend for faster builds
brew install ninja

# to help with debugging
brew install valgrind

# to help with benchmarking
brew install hyperfine
```

### Updating

To pull updates from this repository, go to your own repository, and run:

```sh
# only do this once
git remote add upstream git@github.com:6112-fa25/skeleton.git

# do this every time the skeleton repository is updated
git pull upstream main
```

## Build and Run

### Build

The `build.sh` script will be used for building when grading,
so you should make sure it can build your code.

A `build.sh` is provided for you if you're planning to use CMake -
otherwise, you should modify it to fit your needs.

You can also use the script to build the code locally, or you can
run the commands directly as described below.

#### CMake

If you'd like to learn more about CMake, you can read the first
few chapters of the [tutorial](https://cmake.org/cmake/help/latest/guide/tutorial/index.html).
If you have more questions, ask on Piazza!

By default, `build.sh` will build two binaries in the following paths:

1. `build/cmake/debug/mitscript` -- Debug mode binary. Has AddressSanitizer and UBSanitizer turned on and debug symbols enabled.
2. `build/cmake/release/mitscript` -- Release mode binary. Has stripped debug symbols and has `-O3` turned on.

If you want to add compile options or flags, use:

- [`target_compile_options`](https://cmake.org/cmake/help/latest/command/target_compile_options.html)
- [`target_compile_definitions`](https://cmake.org/cmake/help/latest/command/target_compile_definitions.html)
- etc.

Make sure you're looking at documentation for your version of CMake (`cmake --version`).

#### Make

Just run `make` directly to build all targets. For information, run `make help` to show all the possible targets you can build.

#### IDEs (CLion, Visual Studio Code, etc.)

These will usually work with CMake right out of the box.
Ask on Piazza if you have trouble connecting your IDE to the build system.

### Run

The `run.sh` script will be used for running your project when grading,
so you should make sure it works.

`run.sh` will run the release mode binary after you have built your project. Release mode is compiled with optimization flags turned on and sanitizers turned off.

#### Tests

To run the provided tests, please see the `./test.sh` script and the `grade.py` script under the `tests/` git submodule.
