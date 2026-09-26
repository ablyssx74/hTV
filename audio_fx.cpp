/*
 * Copyright 2026, Kris Beazley (ablyss) hTV@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "audio_fx.h"

#include <mpv/client.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>

AudioConfig gAudioCfg;
mpv_handle* g_mpv = nullptr;

namespace {

// snprintf into a stack buffer and append -- the std::string equivalent of
// the BString::SetToFormat() calls this logic started life as.
void AppendFormat(std::string& out, const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    out += buf;
}

} // namespace

void BuildAudioFilterChain(std::string& chain) {
    chain.clear();

    if (gAudioCfg.eqEnabled) {
        for (int i = 0; i < 15; i++) {
            AppendFormat(chain, "equalizer=f=%.0f:width_type=o:w=1:g=%.2f,",
                         kEqFrequencies[i], gAudioCfg.eqBands[i]);
        }

        // Mastering limiter, chained right after the EQ bands -- same
        // In/Threshold/Release controls and dB-to-linear-gain math
        // HaikuSuperMusicThingy uses for its own `alimiter` stage.
        float inputGain = std::pow(10.0f, gAudioCfg.limitInput / 20.0f);
        float limitVal  = std::pow(10.0f, gAudioCfg.limitThreshold / 20.0f);
        if (limitVal <= 0.001f) limitVal = 0.001f;
        if (inputGain <= 0.001f) inputGain = 0.001f;

        AppendFormat(chain, "alimiter=level_in=%.2f:limit=%.2f:release=%.2f,",
                     inputGain, limitVal, gAudioCfg.limitRelease);
    }

    if (gAudioCfg.reverbEnabled) {
        // aecho=in_gain:out_gain:delays:decays
        // "Room size" scales the tap delays, "damping" trims the decay of
        // each successive tap (simulating high-frequency absorption), and
        // "wet level" blends the reverb signal in/out.
        float wet   = gAudioCfg.reverbWet / 100.0f;      // 0..1
        float damp  = gAudioCfg.reverbDamping / 100.0f;  // 0..1
        float roomScale = 0.5f + (gAudioCfg.reverbRoomSize / 100.0f) * 1.5f; // 0.5x..2.0x

        struct ReverbProfile { float delayMs[3]; float decay[3]; };
        static const ReverbProfile kProfiles[4] = {
            { {60.0f, 100.0f, 150.0f}, {0.35f, 0.25f, 0.15f} },   // Room
            { {150.0f, 220.0f, 340.0f}, {0.55f, 0.45f, 0.30f} },  // Hall
            { {30.0f, 55.0f, 90.0f},   {0.45f, 0.35f, 0.25f} },   // Plate
            { {280.0f, 480.0f, 700.0f}, {0.55f, 0.45f, 0.35f} }   // Canyon
        };
        const ReverbProfile& profile = kProfiles[gAudioCfg.reverbType % 4];

        std::string delays, decays;
        for (int i = 0; i < 3; i++) {
            if (i > 0) { delays += "|"; decays += "|"; }
            AppendFormat(delays, "%.0f", profile.delayMs[i] * roomScale);
            AppendFormat(decays, "%.3f", profile.decay[i] * (1.0f - damp * 0.6f));
        }

        float inGain  = 0.6f + wet * 0.3f;
        float outGain = 0.5f + wet * 0.5f;

        AppendFormat(chain, "aecho=%.2f:%.2f:%s:%s,", inGain, outGain,
                     delays.c_str(), decays.c_str());
    }

    if (gAudioCfg.chorusEnabled) {
        // chorus=in_gain:out_gain:delays:decays:speeds:depths -- three
        // slightly-detuned voices (the same base delay/decay spread as
        // ffmpeg's own documented chorus example), with Rate/Depth/Mix
        // scaling the modulation speed, modulation depth, and wet/dry
        // balance respectively.
        float rate  = 0.1f + (gAudioCfg.chorusRate / 100.0f) * 2.9f;   // 0.1..3.0 Hz
        float depth = 1.0f + (gAudioCfg.chorusDepth / 100.0f) * 9.0f;  // 1..10 ms
        float mix   = gAudioCfg.chorusMix / 100.0f;                    // 0..1
        float inGain  = 0.5f + mix * 0.2f;
        float outGain = 0.5f + mix * 0.4f;

        AppendFormat(chain, "chorus=%.2f:%.2f:55|60|40:0.4|0.32|0.3:%.2f|%.2f|%.2f:%.2f|%.2f|%.2f,",
                     inGain, outGain,
                     rate, rate * 1.15f, rate * 0.7f,
                     depth, depth * 0.9f, depth * 1.1f);
    }

    if (!chain.empty() && chain.back() == ',') {
        chain.pop_back();
    }
}

void ApplyAudioFilters(mpv_handle* mpv) {
    if (!mpv) return;
    std::string chain;
    BuildAudioFilterChain(chain);
    mpv_set_property_string(mpv, "af", chain.c_str());
}
