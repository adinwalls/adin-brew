#!/usr/bin/env bash
# Installs the Adin Brew plugins (TuneBrew Autotune, LevelBrew Vocal Rider) for the
# current user (no sudo needed).
# Usage: ./install.sh            install (builds first if there are no prebuilt binaries)
#        ./install.sh --rebuild  force a fresh build from source
set -e
cd "$(dirname "$0")"

if { [ ! -d bin/TuneBrew.lv2 ] && [ ! -d bin/LevelBrew.lv2 ]; } || [ "$1" = "--rebuild" ]; then
    echo "==> Building from source..."
    if [ ! -f dpf/Makefile.plugins.mk ]; then
        if [ -d .git ]; then
            echo "==> Fetching the DPF framework (git submodule)..."
            git submodule update --init --recursive
        else
            echo "ERROR: the dpf/ folder is missing. Clone with:"
            echo "  git clone --recursive https://github.com/adinwalls/adin-brew.git"
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
installed=""
for p in TuneBrew LevelBrew; do
    [ -d "bin/$p.lv2" ] || continue
    rm -rf ~/.lv2/"$p".lv2 ~/.vst3/"$p".vst3
    cp -r "bin/$p.lv2" ~/.lv2/
    [ -d "bin/$p.vst3" ]    && cp -r "bin/$p.vst3" ~/.vst3/
    [ -f "bin/$p.clap" ]    && cp "bin/$p.clap" ~/.clap/
    [ -f "bin/$p-vst.so" ]  && cp "bin/$p-vst.so" ~/.vst/
    installed="$installed $p"
done
echo ""
echo "Installed:$installed"
echo "  LV2 -> ~/.lv2   VST3 -> ~/.vst3   CLAP -> ~/.clap   VST2 -> ~/.vst"
echo "Rescan plugins in your DAW and look for 'TuneBrew Autotune' and 'LevelBrew Vocal Rider'."
