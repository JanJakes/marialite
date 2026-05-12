FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        binutils \
        bison \
        build-essential \
        ca-certificates \
        cmake \
        file \
        flex \
        git \
        libaio-dev \
        libboost-dev \
        libcurl4-openssl-dev \
        libfmt-dev \
        lld \
        libncurses-dev \
        libpcre2-dev \
        libssl-dev \
        ninja-build \
        perl \
        pkg-config \
        python3 \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
