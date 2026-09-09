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

Playlist
1. The Config window also has a Playlist section. The "Playlist Folder" button opens a folder picker and labels itself to whatever folder you pick, exactly like it does in [HaikuDVR](https://github.com/ablyssx74/HaikuDVR)'s "Save Recordings To Folder" button.
2. Check "Show Playable Files" to expand a list of everything playable in that folder -- clicking any entry plays it immediately. Same expandable-list-on-selection-plays-it idea as [HaikuSuperMusicThingy](https://github.com/ablyssx74/HaikuSuperMusicThingy)'s MilkDrop preset list.
3. "Random Play" and the "Shuffle" button pick and play one random file from the folder -- a manual, one-shot pick (same as HaikuSuperMusicThingy's own Shuffle), not a continuously auto-advancing playlist: hTV doesn't jump to a new file on its own when the current one ends.
4. The folder path and Random Play setting are saved to the same `hTV_settings` file as the audio config above.
5. Recognizes a broad range of formats mpv commonly plays (mp3, flac, wav, ogg/opus, m4a/aac, wma and more for audio; mp4, mkv, webm, avi, mov, flv and more for video) -- not an exhaustive list of everything mpv/ffmpeg can technically decode, since libmpv doesn't expose one to query.

Make it your Default on Haiku
1. Open FileTypes and set Video and or Audio to use hTV as preferred application.  ( manually select it in /boot/system/apps/ )

Supports 64/32 bit builds

### To build
```
make release
```


