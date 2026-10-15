# hTV Build Script (Native Haiku OS Conversion)
SHELL := /bin/bash
GUI_TARGET = hTV
# The one place a release changes is the VERSION file: the program, the package and the resource all take it from there.
VERSION := $(shell tr -d 'v \r\n' < VERSION)
V_MAJOR := $(word 1,$(subst ., ,$(VERSION)))
V_MINOR := $(word 2,$(subst ., ,$(VERSION)))
V_MIDDLE := $(word 3,$(subst ., ,$(VERSION)))
REVISION = 1
PACKAGE_DIR := build/package
DUMMY_PC_PATH := $(shell pwd)/build/pkgconfig

# Source mapping parameters
GUI_SRCS = hTV.cpp audio_fx.cpp
GUI_OBJS = $(GUI_SRCS:.cpp=.o)

# Check if an rdef file exists, otherwise skip resource compilation
HAS_RDEF := $(shell [ -f hTV.rdef ] && echo yes || echo no)
ifeq ($(HAS_RDEF), yes)
    GUI_RSRCS = hTV.rsrc
else
    GUI_RSRCS = 
endif

# --- Architecture & Path Setup ---
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M), BePC)
    CXX = g++-x86
    ARCH = x86_gcc2
    LIB_ARCH_DIR = /x86
    DEFINES += -DIS_HAIKU_32BIT
    is32bit = _x86
    PKG_CONFIG_CMD = pkg-config-x86
else
    CXX = g++
    ARCH = x86_64
    LIB_ARCH_DIR = 
    PKG_CONFIG_CMD = pkg-config
endif


# --- Compiler & Linker Flags ---
CXXFLAGS = -std=c++17 -O3 -Wall
DEFINES += -DAPP_LOCAL_VERSION=\"v$(VERSION)\"
INCLUDES = -I/boot/home/config/non-packaged/include -I/boot/system/develop/headers
LIB_PATH = -L/boot/system/lib$(LIB_ARCH_DIR) -L/boot/system/develop/lib$(LIB_ARCH_DIR) -L/boot/home/config/non-packaged/lib$(LIB_ARCH_DIR) 

# Core hTV Media Pipeline & Interface Libraries (Added -lmpv)
EXTRA_LIBS = -lSDL2 -lmpv -lGL -lavformat -lavcodec -lavutil -lswscale -lswresample -lcurl -lnetwork


# Added -lmedia to support BSoundPlayer architecture natively
HAIKU_LIBS = -lbe -lmedia -ltranslation -ltracker -lshared -lroot -lpthread


# Combine paths and libs
LIBS = $(LIB_PATH) $(EXTRA_LIBS) $(HAIKU_LIBS)

# --- Master Build Targets ---
.PHONY: all clean setup_dummy

all: setup_dummy $(GUI_TARGET)


# Link the graphical desktop client binary
$(GUI_TARGET): $(GUI_OBJS) $(GUI_RSRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(GUI_TARGET) $(GUI_OBJS) $(LIBS) 
ifeq ($(HAS_RDEF), yes)
	xres -o $(GUI_TARGET) $(GUI_RSRCS)
endif
	mimeset -f $(GUI_TARGET)

# Compile visual layout script components (Only executes if hTV.rdef is found)
# (the version fields of the resource are filled in from the VERSION file)
%.rsrc: %.rdef VERSION
	sed -e 's/@V_MAJOR@/$(V_MAJOR)/' -e 's/@V_MINOR@/$(V_MINOR)/' -e 's/@V_MIDDLE@/$(V_MIDDLE)/' -e 's/@VERSION@/$(VERSION)/' $< > $*.version.rdef
	rc -o $@ $*.version.rdef

# General object file compilation hooks
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) -c $< -o $@

# The version is compiled in: a new VERSION means a new build of the file that shows it
hTV.o: VERSION

# Deep system cleaning target
clean:
	rm -f *.o *.rsrc *.hpkg *.version.rdef $(GUI_TARGET) 
	rm -rf build


release: all
	@[ -n "$(PACKAGE_DIR)" ] || { echo "PACKAGE_DIR is undefined"; exit 1; }
	rm -rf "./$(PACKAGE_DIR)"
	mkdir -p $(PACKAGE_DIR)
	sed -e 's/$$(GUI_TARGET)/$(GUI_TARGET)/g'  -e 's/$$(REVISION)/$(REVISION)/g'  -e 's/$$(VERSION)/$(VERSION)/g'  -e 's/$$(is32bit)/$(is32bit)/g'  -e 's/$$(ARCH)/$(ARCH)/' -e 's/$$(YEAR)/$(shell date +%Y)/' $(GUI_TARGET).tpl > $(PACKAGE_DIR)/.PackageInfo
	mkdir -p $(PACKAGE_DIR)/apps
	mkdir -p $(PACKAGE_DIR)/bin
	mkdir -p $(PACKAGE_DIR)/data/deskbar/menu/Applications
	cp $(GUI_TARGET) $(PACKAGE_DIR)/apps/$(GUI_TARGET)
	ln -s ../apps/$(GUI_TARGET) $(PACKAGE_DIR)/bin/$(GUI_TARGET)
	ln -s ../../../../apps/$(GUI_TARGET) $(PACKAGE_DIR)/data/deskbar/menu/Applications/$(GUI_TARGET)
	package create -C $(PACKAGE_DIR) $(GUI_TARGET)-$(VERSION)-$(REVISION)-$(ARCH).hpkg	



