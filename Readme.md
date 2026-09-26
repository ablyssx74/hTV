# OpenGL Haiku SDL2 Audio/Video Player with nebula/llvm support.

The only Haiku Video/Audio player ...
1. That supports multi-threaded LLVM processes.  
2. That auto detects if the user has Nebula Nvidia driver installed.
3. That uses hardware accelerated playback for the Nebula driver.

What does this do exactly?
1. Plays your favorite videos or audio either from a local file, or URL.
2. Built with  libmpv backend and libsdl2: Supports fullscreen, OCD menu, rewind fast-forward, and mute.

Audio Configuration
1. Right click anywhere on the player to bring up the context menu, then choose "Config".
2. The Config window has a 15-band graphic EQ (with Flat/Rock/Jazz/Bass Boost presets and a paired In/Threshold/Release limiter), plus Reverb (Room/Hall/Plate/Canyon) and a tunable Chorus (Rate/Depth/Mix) effect.
3. Settings are saved as a flat BMessage to `hTV_settings` in your Haiku settings directory and reloaded automatically on the next launch. On the Linux build, the same settings are saved via QSettings to `~/.config/hTV/hTV.conf` instead.

Make it your Default on Haiku
1. Open FileTypes and set Video and or Audio to use hTV as preferred application.  ( manually select it in /boot/system/apps/ )

Supports 64/32 bit builds

### To build (Haiku)
```
make release
```

## Linux build

hTV also builds natively on Linux, playback (SDL2 + libmpv) side by side with
a Qt6-based right-click Config menu/window so it looks and feels native on a
KDE/Plasma desktop (or any other Qt- or GTK-themed desktop). Qt runs on its
own thread with its own event loop, completely independent from SDL's -- so
opening the Config menu or window never stalls video playback.

Both X11 and Wayland are supported with no extra flags: SDL2 and Qt6 each
auto-detect Wayland when running in a Wayland session (e.g. Plasma/KDE on
Wayland) and fall back to X11 otherwise, as long as Qt's Wayland platform
plugin package is installed (see below).

### Dependencies (CachyOS / Arch)
```
sudo pacman -S --needed base-devel cmake sdl2 mpv ffmpeg curl qt6-base qt6-wayland
```

### Dependencies (Debian/Ubuntu-based)
```
sudo apt install build-essential cmake libsdl2-dev libmpv-dev \
    libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev \
    libcurl4-openssl-dev qt6-base-dev qt6-wayland
```

### To build (Linux)
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/hTV <url-or-file>
```

