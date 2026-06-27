# hTV Build Script (Native Haiku OS Conversion)
SHELL := /bin/bash
GUI_TARGET = hTV
VERSION = 1.0.3
PACKAGE_DIR := build/package
DUMMY_PC_PATH := $(shell pwd)/build/pkgconfig

# Source mapping parameters
GUI_SRCS = hTV.cpp
GUI_OBJS = $(GUI_SRCS:.cpp=.o)

# Check if an rdef file exists, otherwise skip resource compilation
HAS_RDEF := $(shell [ -f hTV.rdef ] && echo yes || echo no)
ifeq ($(HAS_RDEF), yes)
    GUI_RSRCS = hTV.rsrc
else
    GUI_RSRCS = 
endif

# --- Architecture & Path Setup ---
UNAME_M := $(shell uname -p)
ifeq ($(UNAME_M), x86)
    CXX = g++-x86
    ARCH = x86_gcc2
    LIB_ARCH_DIR = /x86
    DEFINES += -DIS_HAIKU_32BIT
    PKG_CONFIG_CMD = x86-pkg-config
else
    CXX = g++
    ARCH = x86_64
    LIB_ARCH_DIR = 
    PKG_CONFIG_CMD = pkg-config
endif

# Set up Pkg-Config Environment
export PKG_CONFIG_PATH := $(DUMMY_PC_PATH):/boot/home/config/non-packaged/lib/pkgconfig:/boot/home/config/non-packaged/lib$(LIB_ARCH_DIR)/pkgconfig:/boot/system/develop/lib$(LIB_ARCH_DIR)/pkgconfig:

# --- Compiler & Linker Flags ---
CXXFLAGS = -std=c++17 -O3 -Wall -rdynamic -I/boot/system/develop/headers/private/shared
INCLUDES = -I/boot/home/config/non-packaged/include -I/boot/system/develop/headers
LIB_PATH = -L/boot/system/lib$(LIB_ARCH_DIR) -L/boot/system/develop/lib$(LIB_ARCH_DIR) -L/boot/home/config/non-packaged/lib$(LIB_ARCH_DIR) 

# Core hTV Media Pipeline & Interface Libraries (Added -lmpv)
EXTRA_LIBS = $(shell $(PKG_CONFIG_CMD) --libs sdl2) \
             -lmpv -lGL -lavformat -lavcodec -lavutil -lswscale -lswresample -lcurl -lnetwork


# Added -lmedia to support BSoundPlayer architecture natively
HAIKU_LIBS = -lbe -lmedia -ltranslation -ltracker -lshared -lroot -lpthread

# Inject projectM-4 flags directly into CXXFLAGS
CXXFLAGS += $(shell $(PKG_CONFIG_CMD) --cflags projectM-4)

# Combine paths and libs
LIBS = $(LIB_PATH) $(EXTRA_LIBS) $(HAIKU_LIBS)

# --- Master Build Targets ---
.PHONY: all clean setup_dummy

all: setup_dummy $(GUI_TARGET)

# Dummy setup rule expanded to feed projectM both alias definitions
setup_dummy:
	@mkdir -p $(DUMMY_PC_PATH)
	@if [ ! -f $(DUMMY_PC_PATH)/gl.pc ]; then \
		printf "Name: gl\nDescription: Dummy GL for Haiku\nVersion: 1.0\nLibs: -lGL\n" > $(DUMMY_PC_PATH)/gl.pc; \
	fi
	@if [ ! -f $(DUMMY_PC_PATH)/opengl.pc ]; then \
		printf "Name: opengl\nDescription: Dummy OpenGL for projectM verification\nVersion: 1.0\nLibs: -lGL\n" > $(DUMMY_PC_PATH)/opengl.pc; \
	fi

# Link the graphical desktop client binary
$(GUI_TARGET): $(GUI_OBJS) $(GUI_RSRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(GUI_TARGET) $(GUI_OBJS) $(LIBS) 
ifeq ($(HAS_RDEF), yes)
	xres -o $(GUI_TARGET) $(GUI_RSRCS)
endif
	mimeset -f $(GUI_TARGET)

# Compile visual layout script components (Only executes if hTV.rdef is found)
%.rsrc: %.rdef
	rc -o $@ $<

# General object file compilation hooks
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Deep system cleaning target
clean:
	rm -f *.o *.rsrc *.hpkg $(GUI_TARGET) 
	rm -rf build


release: all
	@[ -n "$(PACKAGE_DIR)" ] || { echo "PACKAGE_DIR is undefined"; exit 1; }
	rm -rf "./$(PACKAGE_DIR)"
	mkdir -p $(PACKAGE_DIR)
	sed -e 's/$$(GUI_TARGET)/$(GUI_TARGET)/g' -e 's/$$(VERSION)/$(VERSION)/g' -e 's/$$(ARCH)/$(ARCH)/' -e 's/$$(YEAR)/$(shell date +%Y)/' $(GUI_TARGET).tpl > $(PACKAGE_DIR)/.PackageInfo
	mkdir -p $(PACKAGE_DIR)/apps
	mkdir -p $(PACKAGE_DIR)/bin
	mkdir -p $(PACKAGE_DIR)/data/deskbar/menu/Applications
	cp $(GUI_TARGET) $(PACKAGE_DIR)/apps/$(GUI_TARGET)
	ln -s ../apps/$(GUI_TARGET) $(PACKAGE_DIR)/bin/$(GUI_TARGET)
	ln -s ../../../../apps/$(GUI_TARGET) $(PACKAGE_DIR)/data/deskbar/menu/Applications/$(GUI_TARGET)
	package create -C $(PACKAGE_DIR) $(GUI_TARGET)-$(VERSION)-1-$(ARCH).hpkg	



