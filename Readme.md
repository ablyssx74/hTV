# Haiku SDL2 Audio/Video Streamer with nebula opengl support.
- Requires patched mpv to play and patched mpv_devel to build
-- [mpv-0.41.0-3-x86_64.hpkg](https://github.com/ablyssx74/Haiku_Recipes/blob/main/mpv_opengl/mpv-0.41.0-3-x86_64.hpkg)
-- [mpv_devel-0.41.0-3-x86_64.hpkg](https://github.com/ablyssx74/Haiku_Recipes/blob/main/mpv_opengl/mpv_devel-0.41.0-3-x86_64.hpkg)

- In addition to the patched MPV, hTV also requires:
-  [Haiku Nightly](https://download.haiku-os.org/nightly-images/x86_64/), a Turing+ GPU supported Nvidia card, [libglvnd-1.7.0-4-x86_64.hpkg](https://github.com/X547/nvidia-haiku/releases/download/v0.0.1/libglvnd-1.7.0-4-x86_64.hpkg) and [nebula-0.0.2-1.x86_64.hpkg](https://github.com/X547/nvidia-haiku/releases/download/v0.0.2/nebula-0.0.2-1.x86_64.hpkg).

### To build

```
make release
```


