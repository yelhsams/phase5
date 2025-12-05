FROM --platform=linux/amd64 ubuntu:22.04
LABEL Description="Build environment"

ENV HOME=/root

SHELL ["/bin/bash", "-c"]

RUN apt update && apt -y --no-install-recommends install \
    software-properties-common gpg-agent

RUN apt -y --no-install-recommends install \
    build-essential time git cmake gdb flex bison valgrind \
    libantlr4-runtime-dev wget ca-certificates default-jre-headless \
    python3 python3-pip hyperfine

RUN add-apt-repository ppa:ubuntu-toolchain-r/test -y

RUN apt -y --no-install-recommends install gcc-13 g++-13

RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100

RUN wget https://github.com/antlr/website-antlr4/raw/gh-pages/download/antlr-4.10.1-complete.jar -O /usr/local/lib/antlr-4.10.1-complete.jar && \
    echo "#!/bin/sh\njava -jar /usr/local/lib/antlr-4.10.1-complete.jar \"\$@\"" > /usr/bin/antlr4 && \
    chmod +x /usr/bin/antlr4

RUN pip3 install --no-cache-dir typer rich