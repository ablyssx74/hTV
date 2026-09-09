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
3. Settings are saved as a flat BMessage to `hTV_settings` in your Haiku settings directory and reloaded automatically on the next launch.

Make it your Default on Haiku
1. Open FileTypes and set Video and or Audio to use hTV as preferred application.  ( manually select it in /boot/system/apps/ )

Supports 64/32 bit builds

### To build
```
make release
```


