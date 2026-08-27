#!/usr/bin/env bash
set -euo pipefail

# Optional Ubuntu/Debian bootstrap for compilers and Linux platform headers.
# SDL3, SDL3_image, SDL3_ttf, SDL3_mixer and FFmpeg are built privately by
# build-dependencies.sh and are intentionally not installed from apt.
sudo apt-get install \
    build-essential \
    cmake \
    curl \
    git \
    patchelf \
    libasound2-dev \
    libcurl4-openssl-dev \
    libegl1-mesa-dev \
    libfribidi-dev \
    libgl1-mesa-dev \
    libpulse-dev \
    libthai-dev \
    libudev-dev \
    libwayland-dev \
    libx11-dev \
    libxcursor-dev \
    libxext-dev \
    libxfixes-dev \
    libxi-dev \
    libxkbcommon-dev \
    libxrandr-dev \
    libxrender-dev \
    libxss-dev \
    libxtst-dev \
    wayland-protocols \
    xauth \
    xvfb
