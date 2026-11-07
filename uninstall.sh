#!/usr/bin/env bash
for p in TuneBrew LevelBrew; do
    rm -rf ~/.lv2/"$p".lv2 ~/.vst3/"$p".vst3 ~/.clap/"$p".clap ~/.vst/"$p"-vst.so
done
echo "TuneBrew Autotune and LevelBrew Vocal Rider removed."
