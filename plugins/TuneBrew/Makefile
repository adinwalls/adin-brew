#!/usr/bin/make -f
NAME = TuneBrew
FILES_DSP = TuneBrewPlugin.cpp glibc_compat.c
FILES_UI  = TuneBrewUI.cpp glibc_compat.c
UI_TYPE = cairo
DPF_PATH = ../../dpf
DPF_TARGET_DIR = ../../bin
DPF_BUILD_DIR = ../../build/TuneBrew
include ../../dpf/Makefile.plugins.mk
# portable binaries: don't depend on the host's libstdc++ version
LINK_FLAGS += -static-libstdc++ -static-libgcc
BUILD_CXX_FLAGS += -std=gnu++17
TARGETS += lv2_sep vst2 vst3 clap
all: $(TARGETS)
