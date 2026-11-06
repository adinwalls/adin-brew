#!/usr/bin/env bash
# Installs TuneBrew Autotune for the current user (no sudo needed).
# Usage: ./install.sh            install (builds first if there are no prebuilt binaries)
#        ./install.sh --rebuild  force a fresh build from source
set -e
cd "$(dirname "$0")"

if [ ! -d bin/TuneBrew.lv2 ] || [ "$1" = "--rebuild" ]; then
    echo "==> Building TuneBrew from source..."
    if [ ! -f dpf/Makefile.plugins.mk ]; then
        if [ -d .git ]; then
            echo "==> Fetching the DPF framework (git submodule)..."
            git submodule update --init --recursive
        else
            echo "ERROR: the dpf/ folder is missing. Clone with:"
            echo "  git clone --recursive https://github.com/adinwalls/tunebrew.git"
            exit 1
        fi
    fi
    if ! command -v g++ >/dev/null || ! pkg-config --exists cairo x11 2>/dev/null; then
        echo "ERROR: build tools or headers are missing. On Ubuntu / Pop!_OS / Mint run:"
        echo "  sudo apt install build-essential pkg-config libcairo2-dev libx11-dev libxext-dev"
        exit 1
    fi
    make -j"$(nproc)"
fi

mkdir -p ~/.lv2 ~/.vst3 ~/.clap ~/.vst
rm -rf ~/.lv2/TuneBrew.lv2 ~/.vst3/TuneBrew.vst3
cp -r bin/TuneBrew.lv2  ~/.lv2/
cp -r bin/TuneBrew.vst3 ~/.vst3/
cp    bin/TuneBrew.clap ~/.clap/
cp    bin/TuneBrew-vst.so ~/.vst/
echo ""
echo "TuneBrew Autotune installed:"
echo "  LV2  -> ~/.lv2/TuneBrew.lv2"
echo "  VST3 -> ~/.vst3/TuneBrew.vst3"
echo "  CLAP -> ~/.clap/TuneBrew.clap"
echo "  VST2 -> ~/.vst/TuneBrew-vst.so"
echo "Rescan plugins in your DAW and look for 'TuneBrew Autotune'."
