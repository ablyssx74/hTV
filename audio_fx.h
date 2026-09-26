/*
 * Copyright 2026, Kris Beazley (ablyss) hTV@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

// =============================================================================
// AUDIO CONFIGURATION -- 15-Band EQ, Limiter, Reverb & FX
// =============================================================================
// Platform-independent core shared by both the Haiku (Be API) and Linux (Qt)
// builds: the AudioConfig data, the frequency/preset tables, and the mpv/
// ffmpeg `af` filter-chain builder. Everything here is plain C++ (std::string,
// no BString/QString), so it compiles unchanged into hTV.cpp on Haiku and
// into linux_config_ui.cpp on Linux.
//
// This mirrors the 15-band graphic EQ found in ablyss's HaikuSuperMusicThingy
// (https://github.com/ablyssx74/HaikuSuperMusicThingy) -- same 15 ISO-ish band
// centers, same +/-15dB range, same "equalizer=f=..:width_type=o:w=1:g=.."
// filter construction, paired with the same In/Threshold/Release mastering
// limiter (`alimiter`, chained right after the EQ bands).
//
// mpv/ffmpeg has no dedicated "reverb" filter, so Reverb builds a tuned
// `aecho` chain -- the technique mpv's own manual recommends for adding
// echo/reverb-style ambience -- with Room/Hall/Plate/Canyon presets, plus a
// tunable `chorus` filter (Rate/Depth/Mix) as an extra bonus effect.
//
// Settings persistence differs per platform (a flat BMessage on Haiku, same
// technique hDesktop uses; QSettings on Linux) and is implemented separately
// in each platform's own file -- see LoadAudioConfig()/SaveAudioConfig().

#include <cstdint>
#include <string>

struct mpv_handle;

struct AudioConfig {
    bool  eqEnabled = false;
    float eqBands[15] = {0.0f};

    // Mastering limiter -- same In/Threshold/Release controls
    // HaikuSuperMusicThingy pairs with its EQ, active whenever the EQ is.
    float limitInput = 0.0f;      // -20..20 dB
    float limitThreshold = 0.0f;  // -20..0 dB
    float limitRelease = 100.0f;  // 10..1000 ms

    bool    reverbEnabled = false;
    int32_t reverbType = 0;         // 0 = Room, 1 = Hall, 2 = Plate, 3 = Canyon
    float   reverbRoomSize = 50.0f; // 0..100
    float   reverbDamping = 50.0f;  // 0..100
    float   reverbWet = 30.0f;      // 0..100

    bool  chorusEnabled = false;
    float chorusRate = 30.0f;   // 0..100
    float chorusDepth = 40.0f;  // 0..100
    float chorusMix = 50.0f;    // 0..100

    // Playlist folder + random-play toggle (Haiku only for now -- see
    // hTV.cpp's Playlist section). Grouped into this same struct and
    // settings file as the audio FX config above for simplicity, same as
    // Haiku's own flat BMessage keeps everything in one settings file.
    // The Linux build currently just carries these unused.
    std::string playlistFolder;   // empty = none chosen yet
    bool        randomPlay = false;
};

// The live, shared audio configuration. Both the SDL/mpv thread and whichever
// thread owns the config UI (the Be window's own looper thread on Haiku, the
// Qt thread on Linux) read/write this directly -- there's no lock around it,
// same simplification the rest of this codebase already relies on (e.g. the
// UI thread is the only writer, and reads happen either before the UI exists
// or after a change the UI just made).
extern AudioConfig gAudioCfg;

// mpv client handle shared with the audio-config UI. libmpv's client API
// (mpv_set_property_string etc.) is documented as thread-safe, so the config
// UI can push filter changes directly without any extra locking regardless
// of which thread it runs on.
extern mpv_handle* g_mpv;

// 15-band frequency centers, identical to HaikuSuperMusicThingy's mbeq_1197
// compatible layout.
inline constexpr float kEqFrequencies[15] = {
    50, 100, 156, 220, 311, 440, 622, 880,
    1250, 1750, 2500, 3500, 5000, 10000, 20000
};

inline constexpr const char* kEqFreqLabels[15] = {
    "50", "100", "156", "220", "311", "440", "622", "880",
    "1k2", "1k7", "2k5", "3k5", "5k", "10k", "20k"
};

// EQ curve presets, matching the ones shipped with HaikuSuperMusicThingy.
inline constexpr float kEqPresetFlat[15] = {
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0
};
inline constexpr float kEqPresetRock[15] = {
    4.0, 3.5, 3.0, 2.5, 2.0, 1.0, -1.0, -1.0,
    0.0, 1.0, 1.5, 2.0, 2.5, 3.5, 4.0
};
inline constexpr float kEqPresetJazz[15] = {
    3.0, 2.5, 2.0, 1.5, 1.0, 2.0, -1.0, -1.0,
    -0.5, 0.0, 0.5, 1.0, 1.5, 2.5, 3.0
};
inline constexpr float kEqPresetBass[15] = {
    11.0, 9.0, 4.0, 2.0, 1.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 3.0, 4.0, 7.0, 9.0
};

// Builds the full mpv `af` filter-chain string from the current gAudioCfg.
void BuildAudioFilterChain(std::string& chain);

// Pushes the current gAudioCfg to mpv as the "af" audio filter chain.
void ApplyAudioFilters(mpv_handle* mpv);

// Persists/restores gAudioCfg. Implemented once per platform: the Haiku
// build defines these in hTV.cpp using a flat BMessage; the Linux build
// defines them in linux_config_ui.cpp using QSettings.
void SaveAudioConfig();
void LoadAudioConfig();
