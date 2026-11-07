#!/usr/bin/make -f
NAME = LevelBrew
FILES_DSP = LevelBrewPlugin.cpp ../TuneBrew/glibc_compat.c
FILES_UI  = LevelBrewUI.cpp ../TuneBrew/glibc_compat.c
UI_TYPE = cairo
DPF_PATH = ../../dpf
DPF_TARGET_DIR = ../../bin
DPF_BUILD_DIR = ../../build/LevelBrew
include ../../dpf/Makefile.plugins.mk
# portable binaries: don't depend on the host's libstdc++ version
LINK_FLAGS += -static-libstdc++ -static-libgcc -pthread
BUILD_CXX_FLAGS += -std=gnu++17 -pthread
# no CLAP: DPF passes CLAP hosts a steady sample counter, not the song position LevelBrew needs
TARGETS += lv2_sep vst2 vst3
all: $(TARGETS)
