/*
 * Copyright 2026, Kris Beazley (ablyss) hTV@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
 
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <new>
#include <math.h>

// Native Haiku (Be API) interface kit -- used for the right-click "Config"
// popup menu and the Audio Configuration window (15-band EQ / reverb / FX).
// SDL already brings up a BApplication under the hood on Haiku, so it's
// safe to create additional BWindows/BPopUpMenus from application code.
#include <Application.h>
#include <Handler.h>
#include <Window.h>
#include <View.h>
#include <GroupView.h>
#include <GroupLayout.h>
#include <StringView.h>
#include <CheckBox.h>
#include <Slider.h>
#include <MenuField.h>
#include <PopUpMenu.h>
#include <MenuItem.h>
#include <Message.h>
#include <String.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <InterfaceDefs.h>
#include <Font.h>
#include <Rect.h>
#include <Point.h>
#include <Size.h>
#include <Button.h>
#include <ListView.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <FilePanel.h>
#include <Entry.h>
#include <Messenger.h>

// Playlist folder scanning/shuffling -- same std::filesystem-based approach
// HaikuSuperMusicThingy uses for its own MilkDrop preset list.
#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>
#include <random>
#include <ctime>
#include <cctype>

// Unified state tracker containing both graphics backend slots
struct PlayerCtx {
    SDL_Window* window;
    SDL_GLContext glContext;   // Used if driver is present
    SDL_Renderer* renderer;    // Used if fallback software pipe runs
    SDL_Texture* texture;      // Used if fallback software pipe runs
    mpv_handle* mpv;
    mpv_render_context* mpvRender;
    bool isRunning;
    bool isFullscreen;
    char currentTitle[512]; 
    int texWidth, texHeight;
};

// =============================================================================
// AUDIO CONFIGURATION -- 15-Band EQ, Reverb & FX
// =============================================================================
// This mirrors the 15-band graphic EQ found in ablyss's HaikuSuperMusicThingy
// (https://github.com/ablyssx74/HaikuSuperMusicThingy) -- same 15 ISO-ish
// band centers, same +/-15dB range, same "equalizer=f=..:width_type=o:w=1:g=.."
// mpv/ffmpeg filter chain construction -- but settings are persisted using a
// flat BMessage (Flatten()/Unflatten() to a single settings file), the same
// technique hDesktop uses (https://github.com/ablyssx74/hDesktop), instead of
// HaikuSuperMusicThingy's JSON config file.
//
// The EQ is paired with a mastering limiter (In/Threshold/Release), same as
// HaikuSuperMusicThingy's own `alimiter` stage chained after its EQ bands.
//
// mpv/ffmpeg has no dedicated "reverb" audio filter, so the Reverb section
// below builds a tuned ffmpeg `aecho` chain -- the technique mpv's own manual
// recommends for adding echo/reverb-style ambience -- with Room/Hall/Plate/
// Canyon presets, plus a tunable `chorus` filter as an extra bonus effect.

// mpv client handle shared with the audio-config UI thread. libmpv's client
// API (mpv_set_property_string etc.) is documented as thread-safe, so the
// ConfigWindow (which runs on its own BWindow looper thread) can push filter
// changes directly without any extra locking.
static mpv_handle* g_mpv = nullptr;

struct AudioConfig {
    bool  eqEnabled = false;
    float eqBands[15] = {0.0f};

    // Mastering limiter -- same In/Threshold/Release controls
    // HaikuSuperMusicThingy pairs with its EQ (an ffmpeg `alimiter` chained
    // right after the 15 equalizer bands), active whenever the EQ is.
    float limitInput = 0.0f;      // -20..20 dB
    float limitThreshold = 0.0f;  // -20..0 dB
    float limitRelease = 100.0f;  // 10..1000 ms

    bool  reverbEnabled = false;
    int32 reverbType = 0;          // 0 = Room, 1 = Hall, 2 = Plate, 3 = Canyon
    float reverbRoomSize = 50.0f;  // 0..100
    float reverbDamping = 50.0f;   // 0..100
    float reverbWet = 30.0f;       // 0..100

    bool  chorusEnabled = false;
    float chorusRate = 30.0f;   // 0..100
    float chorusDepth = 40.0f;  // 0..100
    float chorusMix = 50.0f;    // 0..100

    // Playlist folder + random-play toggle. Grouped into this same struct
    // and settings file as the audio FX config above for simplicity --
    // hTV persists everything through one flat BMessage, so there's no
    // separate settings file to keep in sync.
    std::string playlistFolder;   // empty = none chosen yet
    bool        randomPlay = false;
};

static AudioConfig gAudioCfg;

// 15-band frequency centers, identical to HaikuSuperMusicThingy's mbeq_1197
// compatible layout.
static const float kEqFrequencies[15] = {
    50, 100, 156, 220, 311, 440, 622, 880,
    1250, 1750, 2500, 3500, 5000, 10000, 20000
};

static const char* kEqFreqLabels[15] = {
    "50", "100", "156", "220", "311", "440", "622", "880",
    "1k2", "1k7", "2k5", "3k5", "5k", "10k", "20k"
};

// EQ curve presets, matching the ones shipped with HaikuSuperMusicThingy.
static const float kEqPresetFlat[15] = {
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0
};
static const float kEqPresetRock[15] = {
    4.0, 3.5, 3.0, 2.5, 2.0, 1.0, -1.0, -1.0,
    0.0, 1.0, 1.5, 2.0, 2.5, 3.5, 4.0
};
static const float kEqPresetJazz[15] = {
    3.0, 2.5, 2.0, 1.5, 1.0, 2.0, -1.0, -1.0,
    -0.5, 0.0, 0.5, 1.0, 1.5, 2.5, 3.0
};
static const float kEqPresetBass[15] = {
    11.0, 9.0, 4.0, 2.0, 1.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 3.0, 4.0, 7.0, 9.0
};

static const char* kSettingsFileName = "hTV_settings";

// Builds the full mpv `af` filter-chain string from the current AudioConfig.
static void BuildAudioFilterChain(BString& chain) {
    chain = "";

    if (gAudioCfg.eqEnabled) {
        for (int i = 0; i < 15; i++) {
            BString band;
            band.SetToFormat("equalizer=f=%.0f:width_type=o:w=1:g=%.2f,",
                              kEqFrequencies[i], gAudioCfg.eqBands[i]);
            chain << band;
        }

        // Mastering limiter, chained right after the EQ bands -- same
        // In/Threshold/Release controls and dB-to-linear-gain math
        // HaikuSuperMusicThingy uses for its own `alimiter` stage.
        float inputGain = pow(10.0f, gAudioCfg.limitInput / 20.0f);
        float limitVal  = pow(10.0f, gAudioCfg.limitThreshold / 20.0f);
        if (limitVal <= 0.001f) limitVal = 0.001f;
        if (inputGain <= 0.001f) inputGain = 0.001f;

        BString limiter;
        limiter.SetToFormat("alimiter=level_in=%.2f:limit=%.2f:release=%.2f,",
                             inputGain, limitVal, gAudioCfg.limitRelease);
        chain << limiter;
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

        BString delays, decays;
        for (int i = 0; i < 3; i++) {
            BString d, g;
            d.SetToFormat("%.0f", profile.delayMs[i] * roomScale);
            g.SetToFormat("%.3f", profile.decay[i] * (1.0f - damp * 0.6f));
            if (i > 0) { delays << "|"; decays << "|"; }
            delays << d;
            decays << g;
        }

        float inGain  = 0.6f + wet * 0.3f;
        float outGain = 0.5f + wet * 0.5f;

        BString reverb;
        reverb.SetToFormat("aecho=%.2f:%.2f:%s:%s,", inGain, outGain,
                            delays.String(), decays.String());
        chain << reverb;
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

        BString chorus;
        chorus.SetToFormat("chorus=%.2f:%.2f:55|60|40:0.4|0.32|0.3:%.2f|%.2f|%.2f:%.2f|%.2f|%.2f,",
                            inGain, outGain,
                            rate, rate * 1.15f, rate * 0.7f,
                            depth, depth * 0.9f, depth * 1.1f);
        chain << chorus;
    }

    if (chain.Length() > 0 && chain[chain.Length() - 1] == ',') {
        chain.Truncate(chain.Length() - 1);
    }
}

// Pushes the current AudioConfig to mpv as the "af" audio filter chain.
static void ApplyAudioFilters(mpv_handle* mpv) {
    if (!mpv) return;
    BString chain;
    BuildAudioFilterChain(chain);
    mpv_set_property_string(mpv, "af", chain.String());
}

// Persists gAudioCfg to a single flat BMessage on disk, the same way
// hDesktop flattens its settings BMessage straight to a BFile (repeated
// fields, e.g. "eq_band" x15, follow the same pattern hDesktop uses for its
// repeated "favorite_path" entries).
static void SaveAudioConfig() {
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK) return;
    path.Append(kSettingsFileName);

    BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
    if (file.InitCheck() != B_OK) return;

    BMessage settings;
    settings.AddBool("eq_enabled", gAudioCfg.eqEnabled);
    for (int i = 0; i < 15; i++) {
        settings.AddFloat("eq_band", gAudioCfg.eqBands[i]);
    }
    settings.AddFloat("limit_input", gAudioCfg.limitInput);
    settings.AddFloat("limit_threshold", gAudioCfg.limitThreshold);
    settings.AddFloat("limit_release", gAudioCfg.limitRelease);
    settings.AddBool("reverb_enabled", gAudioCfg.reverbEnabled);
    settings.AddInt32("reverb_type", gAudioCfg.reverbType);
    settings.AddFloat("reverb_room_size", gAudioCfg.reverbRoomSize);
    settings.AddFloat("reverb_damping", gAudioCfg.reverbDamping);
    settings.AddFloat("reverb_wet", gAudioCfg.reverbWet);
    settings.AddBool("chorus_enabled", gAudioCfg.chorusEnabled);
    settings.AddFloat("chorus_rate", gAudioCfg.chorusRate);
    settings.AddFloat("chorus_depth", gAudioCfg.chorusDepth);
    settings.AddFloat("chorus_mix", gAudioCfg.chorusMix);
    settings.AddString("playlist_folder", gAudioCfg.playlistFolder.c_str());
    settings.AddBool("random_play", gAudioCfg.randomPlay);

    ssize_t size = settings.FlattenedSize();
    char* buffer = new (std::nothrow) char[size];
    if (buffer != nullptr) {
        if (settings.Flatten(buffer, size) == B_OK) {
            file.Write(buffer, size);
        }
        delete[] buffer;
    }
}

// Loads gAudioCfg from disk, falling back to safe defaults on any failure.
static void LoadAudioConfig() {
    gAudioCfg.eqEnabled = false;
    for (int i = 0; i < 15; i++) gAudioCfg.eqBands[i] = 0.0f;
    gAudioCfg.limitInput = 0.0f;
    gAudioCfg.limitThreshold = 0.0f;
    gAudioCfg.limitRelease = 100.0f;
    gAudioCfg.reverbEnabled = false;
    gAudioCfg.reverbType = 0;
    gAudioCfg.reverbRoomSize = 50.0f;
    gAudioCfg.reverbDamping = 50.0f;
    gAudioCfg.reverbWet = 30.0f;
    gAudioCfg.chorusEnabled = false;
    gAudioCfg.chorusRate = 30.0f;
    gAudioCfg.chorusDepth = 40.0f;
    gAudioCfg.chorusMix = 50.0f;
    gAudioCfg.playlistFolder = "";
    gAudioCfg.randomPlay = false;

    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK) return;
    path.Append(kSettingsFileName);

    BFile file(path.Path(), B_READ_ONLY);
    if (file.InitCheck() != B_OK) return;

    BMessage settings;
    if (settings.Unflatten(&file) != B_OK) return;

    bool valBool;
    float valFloat;
    int32 valInt32;

    if (settings.FindBool("eq_enabled", &valBool) == B_OK) gAudioCfg.eqEnabled = valBool;
    for (int i = 0; i < 15; i++) {
        if (settings.FindFloat("eq_band", i, &valFloat) == B_OK) gAudioCfg.eqBands[i] = valFloat;
    }
    if (settings.FindFloat("limit_input", &valFloat) == B_OK) gAudioCfg.limitInput = valFloat;
    if (settings.FindFloat("limit_threshold", &valFloat) == B_OK) gAudioCfg.limitThreshold = valFloat;
    if (settings.FindFloat("limit_release", &valFloat) == B_OK) gAudioCfg.limitRelease = valFloat;
    if (settings.FindBool("reverb_enabled", &valBool) == B_OK) gAudioCfg.reverbEnabled = valBool;
    if (settings.FindInt32("reverb_type", &valInt32) == B_OK) gAudioCfg.reverbType = valInt32;
    if (settings.FindFloat("reverb_room_size", &valFloat) == B_OK) gAudioCfg.reverbRoomSize = valFloat;
    if (settings.FindFloat("reverb_damping", &valFloat) == B_OK) gAudioCfg.reverbDamping = valFloat;
    if (settings.FindFloat("reverb_wet", &valFloat) == B_OK) gAudioCfg.reverbWet = valFloat;
    if (settings.FindBool("chorus_enabled", &valBool) == B_OK) gAudioCfg.chorusEnabled = valBool;
    if (settings.FindFloat("chorus_rate", &valFloat) == B_OK) gAudioCfg.chorusRate = valFloat;
    if (settings.FindFloat("chorus_depth", &valFloat) == B_OK) gAudioCfg.chorusDepth = valFloat;
    if (settings.FindFloat("chorus_mix", &valFloat) == B_OK) gAudioCfg.chorusMix = valFloat;

    const char* valString;
    if (settings.FindString("playlist_folder", &valString) == B_OK) gAudioCfg.playlistFolder = valString;
    if (settings.FindBool("random_play", &valBool) == B_OK) gAudioCfg.randomPlay = valBool;
}

// =============================================================================
// PLAYLIST -- folder of local media files, shown as an expandable list
// (same show/hide-a-BScrollView pattern HaikuSuperMusicThingy uses for its
// MilkDrop preset list), plus a manual Shuffle/Random Play option mirroring
// HaikuSuperMusicThingy's own MSG_SHUFFLE: a one-shot random pick, not a
// continuous auto-advancing playlist (hTV doesn't listen for mpv's
// end-of-file event, and this deliberately doesn't add that).
// =============================================================================

// Broad allowlist of extensions mpv commonly plays. libmpv has no API to
// query its exact supported-format list (it ultimately depends on
// whichever ffmpeg demuxers/decoders it was built against), so this is a
// practical list covering the formats that actually show up in a media
// folder, not an exhaustive one.
static const char* kPlayableExtensions[] = {
    // Audio
    "mp3", "flac", "wav", "wv", "ape", "ogg", "oga", "opus", "m4a", "aac",
    "wma", "mka", "aiff", "aif", "ac3", "dts", "mid", "midi", "amr", "au",
    // Video
    "mp4", "m4v", "mkv", "webm", "avi", "mov", "flv", "wmv", "mpg", "mpeg",
    "m2ts", "mts", "ts", "3gp", "3g2", "ogv", "vob", "asf", "rm", "rmvb",
    "divx", "m2v", "mxf", "y4m"
};

static bool IsPlayableExtension(const std::string& extLower) {
    for (const char* ext : kPlayableExtensions) {
        if (extLower == ext) return true;
    }
    return false;
}

// Files currently shown in the playlist BListView, in the same order --
// index N here is index N there. Kept as full paths (rather than joining
// the folder path back on at play time) so a later folder change can't
// accidentally make an old selection resolve to the wrong file.
static std::vector<std::string> gPlaylistFullPaths;

// (Re)scans folderPath (non-recursive -- a playlist folder, not a nested
// preset library) for playable files and repopulates list + gPlaylistFullPaths
// to match, sorted by filename.
static void PopulatePlaylistList(BListView* list, const std::string& folderPath) {
    list->MakeEmpty();
    gPlaylistFullPaths.clear();
    if (folderPath.empty()) return;

    std::error_code ec;
    if (!std::filesystem::exists(folderPath, ec) || ec) return;

    // Same try/catch guard load_random_preset uses around its own
    // recursive_directory_iterator -- a permission error or a symlink
    // dangling mid-scan throws filesystem_error even with the
    // error_code-taking constructor, since range-based for always calls
    // the throwing operator++().
    std::vector<std::filesystem::directory_entry> entries;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(folderPath, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
            std::transform(ext.begin(), ext.end(), ext.begin(),
                            [](unsigned char c) { return std::tolower(c); });
            if (IsPlayableExtension(ext)) entries.push_back(entry);
        }
    } catch (const std::filesystem::filesystem_error&) {
        // Whatever was found before the error is still shown below.
    }

    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.path().filename().string() < b.path().filename().string();
    });

    for (const auto& entry : entries) {
        list->AddItem(new BStringItem(entry.path().filename().string().c_str()));
        gPlaylistFullPaths.push_back(entry.path().string());
    }
}

// Loads a specific playlist entry by its BListView index.
static void PlayPlaylistFileAt(int32 index) {
    if (index < 0 || index >= (int32)gPlaylistFullPaths.size() || g_mpv == nullptr) return;
    const char* loadCmd[] = {"loadfile", gPlaylistFullPaths[index].c_str(), nullptr};
    mpv_command(g_mpv, loadCmd);
}

// Picks and plays one random file from the current playlist -- the same
// one-shot random-pick HaikuSuperMusicThingy's own Shuffle does, not a
// continuous auto-advance.
static void PlayRandomPlaylistFile() {
    if (gPlaylistFullPaths.empty() || g_mpv == nullptr) return;
    static std::mt19937 rng(static_cast<unsigned int>(std::time(nullptr)));
    std::uniform_int_distribution<size_t> dist(0, gPlaylistFullPaths.size() - 1);
    size_t index = dist(rng);
    const char* loadCmd[] = {"loadfile", gPlaylistFullPaths[index].c_str(), nullptr};
    mpv_command(g_mpv, loadCmd);
}

// A BSlider that also responds to the mouse wheel -- same small helper
// HaikuSuperMusicThingy uses for its EQ sliders.
class WheelSlider : public BSlider {
public:
    WheelSlider(const char* name, const char* label, BMessage* msg,
                int32 min, int32 max, orientation orient, int32 multiplier = 1)
        : BSlider(name, label, msg, min, max, orient),
          fMultiplier(multiplier) {}

    virtual void MessageReceived(BMessage* msg) {
        if (msg->what == B_MOUSE_WHEEL_CHANGED) {
            float dy;
            if (msg->FindFloat("be:wheel_delta_y", &dy) == B_OK) {
                int32 min, max;
                GetLimits(&min, &max);

                int32 newValue = Value() - (int32)(dy * fMultiplier);
                if (newValue < min) newValue = min;
                if (newValue > max) newValue = max;

                SetValue(newValue);
                Invoke();
            }
        } else {
            BSlider::MessageReceived(msg);
        }
    }

private:
    int32 fMultiplier;
};

enum {
    MSG_CFG_EQ_TOGGLE      = 'cfeq',
    MSG_CFG_EQ_SLIDER      = 'cfes',
    MSG_CFG_PRESET_SELECT  = 'cfps',
    MSG_CFG_LIMITER_SLIDER = 'cfls',
    MSG_CFG_REVERB_TOGGLE  = 'cfrt',
    MSG_CFG_REVERB_TYPE    = 'cfry',
    MSG_CFG_REVERB_SLIDER  = 'cfrs',
    MSG_CFG_CHORUS_TOGGLE  = 'cfch',
    MSG_CFG_CHORUS_SLIDER  = 'cfcs',

    MSG_CFG_PLAYLIST_TOGGLE        = 'cfpt',
    MSG_CFG_PLAYLIST_FOLDER        = 'cfpb',
    MSG_CFG_PLAYLIST_DIR_CHOSEN    = 'cfpc',
    MSG_CFG_PLAYLIST_SELECTED      = 'cfpl',
    MSG_CFG_PLAYLIST_RANDOM_TOGGLE = 'cfpr',
    MSG_CFG_PLAYLIST_SHUFFLE       = 'cfpz'
};

class ConfigWindow : public BWindow {
public:
    ConfigWindow();
    virtual ~ConfigWindow();
    virtual void MessageReceived(BMessage* message);
    virtual bool QuitRequested();

private:
    void _ApplyPreset(const float* values);

    BCheckBox*  fEQToggle;
    BSlider*    fEQSliders[15];
    BMenuField* fPresetField;

    BSlider*    fLimitInputSlider;
    BSlider*    fLimitThresholdSlider;
    BSlider*    fLimitReleaseSlider;

    BCheckBox*  fReverbToggle;
    BMenuField* fReverbTypeField;
    BSlider*    fRoomSizeSlider;
    BSlider*    fDampingSlider;
    BSlider*    fWetSlider;

    BCheckBox*  fChorusToggle;
    BSlider*    fChorusRateSlider;
    BSlider*    fChorusDepthSlider;
    BSlider*    fChorusMixSlider;

    BButton*     fPlaylistFolderButton;
    BFilePanel*  fPlaylistFolderPanel;
    BCheckBox*   fPlaylistToggle;
    BListView*   fPlaylistList;
    BScrollView* fPlaylistScroll;
    BCheckBox*   fRandomPlayToggle;
    BButton*     fShuffleButton;
};

// Global handle to the (single) open Config window, so a second right-click
// activates the existing window instead of spawning duplicates.
static ConfigWindow* gConfigWindow = nullptr;

ConfigWindow::ConfigWindow()
    : BWindow(BRect(80, 60, 80 + 700, 60 + 400), "hTV - Audio Configuration",
              B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS)
{
    // A window's implicit top view defaults to plain white rather than the
    // system panel color, while every native control below (BCheckBox,
    // BSlider, BMenuField, ...) already paints itself using that theme
    // color -- which is exactly why the empty space around them showed up
    // stark white against the already-dark, theme-colored controls. Wrap
    // everything in one BView explicitly tinted with the same system panel
    // color, and give the window itself a trivial single-child layout that
    // stretches that view to fill the whole window (the same technique
    // HaikuSuperMusicThingy uses via BLayoutBuilder::Group<>(this, ...)
    // with a single top-level child).
    BView* background = new BView("config_background", B_WILL_DRAW);
    background->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    background->SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));

    BGroupLayout* windowLayout = new BGroupLayout(B_VERTICAL, 0);
    windowLayout->SetInsets(0);
    SetLayout(windowLayout);
    AddChild(background);

    BGroupLayout* rootLayout = new BGroupLayout(B_VERTICAL, 10);
    rootLayout->SetInsets(14, 14, 14, 14);
    background->SetLayout(rootLayout);

    // ---- 15-Band EQ ----
    BStringView* eqTitle = new BStringView(NULL, "15-Band Equalizer");
    eqTitle->SetFont(be_bold_font);
    background->AddChild(eqTitle);

    fEQToggle = new BCheckBox("eq_toggle", "Enable Equalizer", new BMessage(MSG_CFG_EQ_TOGGLE));
    fEQToggle->SetValue(gAudioCfg.eqEnabled ? B_CONTROL_ON : B_CONTROL_OFF);
    background->AddChild(fEQToggle);

    BPopUpMenu* presetMenu = new BPopUpMenu("Preset");
    const char* presetNames[] = { "Flat", "Rock", "Jazz", "Bass Boost" };
    for (int i = 0; i < 4; i++) {
        BMessage* msg = new BMessage(MSG_CFG_PRESET_SELECT);
        msg->AddInt32("preset", i);
        presetMenu->AddItem(new BMenuItem(presetNames[i], msg));
    }
    fPresetField = new BMenuField("preset_field", "Preset:", presetMenu);
    background->AddChild(fPresetField);

    BGroupView* sliderRow = new BGroupView(B_HORIZONTAL, 4);
    for (int i = 0; i < 15; i++) {
        BGroupView* bandGroup = new BGroupView(B_VERTICAL, 2);

        BMessage* sliderMsg = new BMessage(MSG_CFG_EQ_SLIDER);
        sliderMsg->AddInt32("band", i);

        BString sliderName;
        sliderName.SetToFormat("eq_band_%d", i);
        fEQSliders[i] = new WheelSlider(sliderName.String(), "", sliderMsg, -15, 15, B_VERTICAL, 1);
        fEQSliders[i]->SetValue((int32)gAudioCfg.eqBands[i]);
        fEQSliders[i]->SetHashMarks(B_HASH_MARKS_LEFT);
        fEQSliders[i]->SetHashMarkCount(7);
        fEQSliders[i]->SetExplicitMinSize(BSize(28, 140));

        BStringView* lbl = new BStringView(NULL, kEqFreqLabels[i]);
        lbl->SetFontSize(9);
        lbl->SetExplicitAlignment(BAlignment(B_ALIGN_CENTER, B_ALIGN_VERTICAL_UNSET));

        bandGroup->AddChild(fEQSliders[i]);
        bandGroup->AddChild(lbl);
        sliderRow->AddChild(bandGroup);
    }
    background->AddChild(sliderRow);

    // ---- Limiter (paired with the EQ, same as HaikuSuperMusicThingy) ----
    BStringView* limiterTitle = new BStringView(NULL, "Limiter");
    limiterTitle->SetFont(be_bold_font);
    background->AddChild(limiterTitle);

    BMessage* limitInMsg = new BMessage(MSG_CFG_LIMITER_SLIDER);
    limitInMsg->AddInt32("param", 0);
    fLimitInputSlider = new WheelSlider("limit_in", "In", limitInMsg, -20, 20, B_HORIZONTAL, 1);
    fLimitInputSlider->SetValue((int32)gAudioCfg.limitInput);
    background->AddChild(fLimitInputSlider);

    BMessage* limitThreshMsg = new BMessage(MSG_CFG_LIMITER_SLIDER);
    limitThreshMsg->AddInt32("param", 1);
    fLimitThresholdSlider = new WheelSlider("limit_thr", "Threshold", limitThreshMsg, -20, 0, B_HORIZONTAL, 1);
    fLimitThresholdSlider->SetValue((int32)gAudioCfg.limitThreshold);
    background->AddChild(fLimitThresholdSlider);

    BMessage* limitRelMsg = new BMessage(MSG_CFG_LIMITER_SLIDER);
    limitRelMsg->AddInt32("param", 2);
    fLimitReleaseSlider = new WheelSlider("limit_rel", "Release", limitRelMsg, 10, 1000, B_HORIZONTAL, 5);
    fLimitReleaseSlider->SetValue((int32)gAudioCfg.limitRelease);
    background->AddChild(fLimitReleaseSlider);

    // ---- Reverb & FX ----
    BStringView* fxTitle = new BStringView(NULL, "Reverb & Effects");
    fxTitle->SetFont(be_bold_font);
    background->AddChild(fxTitle);

    fReverbToggle = new BCheckBox("reverb_toggle", "Enable Reverb", new BMessage(MSG_CFG_REVERB_TOGGLE));
    fReverbToggle->SetValue(gAudioCfg.reverbEnabled ? B_CONTROL_ON : B_CONTROL_OFF);
    background->AddChild(fReverbToggle);

    BPopUpMenu* reverbTypeMenu = new BPopUpMenu("Type");
    const char* reverbTypeNames[] = { "Room", "Hall", "Plate", "Canyon" };
    for (int i = 0; i < 4; i++) {
        BMessage* msg = new BMessage(MSG_CFG_REVERB_TYPE);
        msg->AddInt32("type", i);
        reverbTypeMenu->AddItem(new BMenuItem(reverbTypeNames[i], msg));
    }
    reverbTypeMenu->ItemAt(gAudioCfg.reverbType % 4)->SetMarked(true);
    fReverbTypeField = new BMenuField("reverb_type_field", "Type:", reverbTypeMenu);
    background->AddChild(fReverbTypeField);

    BMessage* roomMsg = new BMessage(MSG_CFG_REVERB_SLIDER);
    roomMsg->AddInt32("param", 0);
    fRoomSizeSlider = new WheelSlider("reverb_room", "Room Size", roomMsg, 0, 100, B_HORIZONTAL, 1);
    fRoomSizeSlider->SetValue((int32)gAudioCfg.reverbRoomSize);
    background->AddChild(fRoomSizeSlider);

    BMessage* dampMsg = new BMessage(MSG_CFG_REVERB_SLIDER);
    dampMsg->AddInt32("param", 1);
    fDampingSlider = new WheelSlider("reverb_damp", "Damping", dampMsg, 0, 100, B_HORIZONTAL, 1);
    fDampingSlider->SetValue((int32)gAudioCfg.reverbDamping);
    background->AddChild(fDampingSlider);

    BMessage* wetMsg = new BMessage(MSG_CFG_REVERB_SLIDER);
    wetMsg->AddInt32("param", 2);
    fWetSlider = new WheelSlider("reverb_wet", "Wet Level", wetMsg, 0, 100, B_HORIZONTAL, 1);
    fWetSlider->SetValue((int32)gAudioCfg.reverbWet);
    background->AddChild(fWetSlider);

    fChorusToggle = new BCheckBox("chorus_toggle", "Enable Chorus", new BMessage(MSG_CFG_CHORUS_TOGGLE));
    fChorusToggle->SetValue(gAudioCfg.chorusEnabled ? B_CONTROL_ON : B_CONTROL_OFF);
    background->AddChild(fChorusToggle);

    // Chorus gets the same dynamic Rate/Depth/Mix sliders Reverb has,
    // instead of being a fixed-parameter on/off toggle.
    BMessage* chorusRateMsg = new BMessage(MSG_CFG_CHORUS_SLIDER);
    chorusRateMsg->AddInt32("param", 0);
    fChorusRateSlider = new WheelSlider("chorus_rate", "Rate", chorusRateMsg, 0, 100, B_HORIZONTAL, 1);
    fChorusRateSlider->SetValue((int32)gAudioCfg.chorusRate);
    background->AddChild(fChorusRateSlider);

    BMessage* chorusDepthMsg = new BMessage(MSG_CFG_CHORUS_SLIDER);
    chorusDepthMsg->AddInt32("param", 1);
    fChorusDepthSlider = new WheelSlider("chorus_depth", "Depth", chorusDepthMsg, 0, 100, B_HORIZONTAL, 1);
    fChorusDepthSlider->SetValue((int32)gAudioCfg.chorusDepth);
    background->AddChild(fChorusDepthSlider);

    BMessage* chorusMixMsg = new BMessage(MSG_CFG_CHORUS_SLIDER);
    chorusMixMsg->AddInt32("param", 2);
    fChorusMixSlider = new WheelSlider("chorus_mix", "Mix", chorusMixMsg, 0, 100, B_HORIZONTAL, 1);
    fChorusMixSlider->SetValue((int32)gAudioCfg.chorusMix);
    background->AddChild(fChorusMixSlider);

    // ---- Playlist ----
    // Folder button + label mirrors HaikuDVR's "Save Recordings To Folder"
    // button (labels itself to the currently-selected folder); the
    // expandable file list + play-on-select mirrors HaikuSuperMusicThingy's
    // MilkDrop preset list (a checkbox shows/hides a BScrollView-wrapped
    // BListView, and selecting an entry plays it).
    BStringView* playlistTitle = new BStringView(NULL, "Playlist");
    playlistTitle->SetFont(be_bold_font);
    background->AddChild(playlistTitle);

    BString folderButtonLabel("Playlist Folder: ");
    folderButtonLabel << (gAudioCfg.playlistFolder.empty()
        ? "(none selected)" : gAudioCfg.playlistFolder.c_str());
    fPlaylistFolderButton = new BButton("playlist_folder_btn", folderButtonLabel.String(),
        new BMessage(MSG_CFG_PLAYLIST_FOLDER));
    background->AddChild(fPlaylistFolderButton);

    fPlaylistFolderPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), NULL,
        B_DIRECTORY_NODE, false, new BMessage(MSG_CFG_PLAYLIST_DIR_CHOSEN));

    BGroupView* playlistToggleRow = new BGroupView(B_HORIZONTAL, 6);
    fPlaylistToggle = new BCheckBox("playlist_toggle", "Show Playable Files",
        new BMessage(MSG_CFG_PLAYLIST_TOGGLE));
    fRandomPlayToggle = new BCheckBox("random_play_toggle", "Random Play",
        new BMessage(MSG_CFG_PLAYLIST_RANDOM_TOGGLE));
    fRandomPlayToggle->SetValue(gAudioCfg.randomPlay ? B_CONTROL_ON : B_CONTROL_OFF);
    fShuffleButton = new BButton("shuffle_btn", "Shuffle", new BMessage(MSG_CFG_PLAYLIST_SHUFFLE));
    playlistToggleRow->AddChild(fPlaylistToggle);
    playlistToggleRow->AddChild(fRandomPlayToggle);
    playlistToggleRow->AddChild(fShuffleButton);
    background->AddChild(playlistToggleRow);

    fPlaylistList = new BListView("playlist_list");
    fPlaylistList->SetSelectionMessage(new BMessage(MSG_CFG_PLAYLIST_SELECTED));
    fPlaylistScroll = new BScrollView("playlist_scroll", fPlaylistList,
        0 /* resizingMode */, 0 /* flags */, false /* horizontal */, true /* vertical */,
        B_FANCY_BORDER);
    // Collapsed by default (same as HaikuSuperMusicThingy's preset list) --
    // keeps the window compact until the user actually wants to browse
    // files, rather than always reserving vertical space for it.
    fPlaylistScroll->Hide();
    fPlaylistScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 120));
    fPlaylistScroll->SetExplicitMaxSize(BSize(B_SIZE_UNSET, 220));
    background->AddChild(fPlaylistScroll);

    PopulatePlaylistList(fPlaylistList, gAudioCfg.playlistFolder);

    // Route every control's message to this window.
    fEQToggle->SetTarget(this);
    presetMenu->SetTargetForItems(this);
    for (int i = 0; i < 15; i++) fEQSliders[i]->SetTarget(this);
    fLimitInputSlider->SetTarget(this);
    fLimitThresholdSlider->SetTarget(this);
    fLimitReleaseSlider->SetTarget(this);
    fReverbToggle->SetTarget(this);
    reverbTypeMenu->SetTargetForItems(this);
    fRoomSizeSlider->SetTarget(this);
    fDampingSlider->SetTarget(this);
    fWetSlider->SetTarget(this);
    fChorusToggle->SetTarget(this);
    fChorusRateSlider->SetTarget(this);
    fChorusDepthSlider->SetTarget(this);
    fChorusMixSlider->SetTarget(this);
    fPlaylistFolderButton->SetTarget(this);
    fPlaylistToggle->SetTarget(this);
    fRandomPlayToggle->SetTarget(this);
    fShuffleButton->SetTarget(this);

    // Grow the window to fit everything (15 vertical EQ sliders plus the
    // reverb/chorus rows need more room than a fixed guess reliably gives),
    // then center it on screen.
    BSize preferred = background->PreferredSize();
    ResizeTo(preferred.Width(), preferred.Height());
    CenterOnScreen();
}

// BFilePanel isn't a BView the window's own child hierarchy owns/deletes
// automatically (same reason HaikuDVR's own folder-panel window explicitly
// deletes its BFilePanel in its destructor), so it needs cleaning up here.
ConfigWindow::~ConfigWindow() {
    delete fPlaylistFolderPanel;
}

void ConfigWindow::_ApplyPreset(const float* values) {
    for (int i = 0; i < 15; i++) {
        gAudioCfg.eqBands[i] = values[i];
        fEQSliders[i]->SetValue((int32)values[i]);
    }
    if (!gAudioCfg.eqEnabled) {
        gAudioCfg.eqEnabled = true;
        fEQToggle->SetValue(B_CONTROL_ON);
    }
    ApplyAudioFilters(g_mpv);
    SaveAudioConfig();
}

void ConfigWindow::MessageReceived(BMessage* message) {
    switch (message->what) {
        case MSG_CFG_EQ_TOGGLE: {
            gAudioCfg.eqEnabled = (fEQToggle->Value() == B_CONTROL_ON);
            ApplyAudioFilters(g_mpv);
            SaveAudioConfig();
            break;
        }
        case MSG_CFG_EQ_SLIDER: {
            int32 band = 0;
            if (message->FindInt32("band", &band) == B_OK && band >= 0 && band < 15) {
                gAudioCfg.eqBands[band] = (float)fEQSliders[band]->Value();
            }
            ApplyAudioFilters(g_mpv);
            SaveAudioConfig();
            break;
        }
        case MSG_CFG_LIMITER_SLIDER: {
            int32 param = 0;
            message->FindInt32("param", &param);
            switch (param) {
                case 0: gAudioCfg.limitInput     = (float)fLimitInputSlider->Value(); break;
                case 1: gAudioCfg.limitThreshold = (float)fLimitThresholdSlider->Value(); break;
                case 2: gAudioCfg.limitRelease    = (float)fLimitReleaseSlider->Value(); break;
                default: break;
            }
            ApplyAudioFilters(g_mpv);
            SaveAudioConfig();
            break;
        }
        case MSG_CFG_PRESET_SELECT: {
            int32 preset = 0;
            if (message->FindInt32("preset", &preset) == B_OK) {
                switch (preset) {
                    case 1:  _ApplyPreset(kEqPresetRock); break;
                    case 2:  _ApplyPreset(kEqPresetJazz); break;
                    case 3:  _ApplyPreset(kEqPresetBass); break;
                    default: _ApplyPreset(kEqPresetFlat); break;
                }
            }
            break;
        }
        case MSG_CFG_REVERB_TOGGLE: {
            gAudioCfg.reverbEnabled = (fReverbToggle->Value() == B_CONTROL_ON);
            ApplyAudioFilters(g_mpv);
            SaveAudioConfig();
            break;
        }
        case MSG_CFG_REVERB_TYPE: {
            int32 type = 0;
            if (message->FindInt32("type", &type) == B_OK) {
                gAudioCfg.reverbType = type;
                ApplyAudioFilters(g_mpv);
                SaveAudioConfig();
            }
            break;
        }
        case MSG_CFG_REVERB_SLIDER: {
            int32 param = 0;
            message->FindInt32("param", &param);
            switch (param) {
                case 0: gAudioCfg.reverbRoomSize = (float)fRoomSizeSlider->Value(); break;
                case 1: gAudioCfg.reverbDamping  = (float)fDampingSlider->Value(); break;
                case 2: gAudioCfg.reverbWet      = (float)fWetSlider->Value(); break;
                default: break;
            }
            ApplyAudioFilters(g_mpv);
            SaveAudioConfig();
            break;
        }
        case MSG_CFG_CHORUS_TOGGLE: {
            gAudioCfg.chorusEnabled = (fChorusToggle->Value() == B_CONTROL_ON);
            ApplyAudioFilters(g_mpv);
            SaveAudioConfig();
            break;
        }
        case MSG_CFG_CHORUS_SLIDER: {
            int32 param = 0;
            message->FindInt32("param", &param);
            switch (param) {
                case 0: gAudioCfg.chorusRate  = (float)fChorusRateSlider->Value(); break;
                case 1: gAudioCfg.chorusDepth = (float)fChorusDepthSlider->Value(); break;
                case 2: gAudioCfg.chorusMix   = (float)fChorusMixSlider->Value(); break;
                default: break;
            }
            ApplyAudioFilters(g_mpv);
            SaveAudioConfig();
            break;
        }
        case MSG_CFG_PLAYLIST_FOLDER: {
            if (fPlaylistFolderPanel) fPlaylistFolderPanel->Show();
            break;
        }
        case MSG_CFG_PLAYLIST_DIR_CHOSEN: {
            entry_ref ref;
            if (message->FindRef("refs", &ref) == B_OK) {
                BEntry entry(&ref, true);
                BPath path;
                if (entry.GetPath(&path) == B_OK) {
                    gAudioCfg.playlistFolder = path.Path();
                    BString newLabel("Playlist Folder: ");
                    newLabel << gAudioCfg.playlistFolder.c_str();
                    fPlaylistFolderButton->SetLabel(newLabel.String());
                    PopulatePlaylistList(fPlaylistList, gAudioCfg.playlistFolder);
                    SaveAudioConfig();
                }
            }
            break;
        }
        case MSG_CFG_PLAYLIST_TOGGLE: {
            bool show = (fPlaylistToggle->Value() == B_CONTROL_ON);
            if (show) {
                fPlaylistScroll->Show();
            } else {
                fPlaylistScroll->Hide();
            }
            InvalidateLayout(true);
            ResizeToPreferred();
            break;
        }
        case MSG_CFG_PLAYLIST_SELECTED: {
            int32 index = fPlaylistList->CurrentSelection();
            if (index >= 0) {
                // Random Play changes what a click does: instead of
                // playing the file actually clicked, it plays a random
                // one from the list. Manual, one-shot -- no auto-advance
                // when a track ends (see the top of this section).
                if (gAudioCfg.randomPlay) {
                    PlayRandomPlaylistFile();
                } else {
                    PlayPlaylistFileAt(index);
                }
            }
            break;
        }
        case MSG_CFG_PLAYLIST_RANDOM_TOGGLE: {
            gAudioCfg.randomPlay = (fRandomPlayToggle->Value() == B_CONTROL_ON);
            SaveAudioConfig();
            break;
        }
        case MSG_CFG_PLAYLIST_SHUFFLE: {
            PlayRandomPlaylistFile();
            break;
        }
        default:
            BWindow::MessageReceived(message);
            break;
    }
}

bool ConfigWindow::QuitRequested() {
    SaveAudioConfig();
    gConfigWindow = nullptr;
    return true;
}

// Shows the Config window, or activates it if it's already open.
static void ShowConfigWindow() {
    if (gConfigWindow != nullptr) {
        if (gConfigWindow->IsHidden()) gConfigWindow->Show();
        gConfigWindow->Activate();
        return;
    }
    gConfigWindow = new ConfigWindow();
    gConfigWindow->Show();
}

enum {
    MSG_OPEN_CONFIG = 'ocfg'
};

// Target for the context menu's "Config" item. Calling BPopUpMenu::Go()
// synchronously blocks the calling thread until the menu is dismissed --
// on the SDL main-loop thread that means SDL_WaitEvent()/mpv rendering
// stop dead, which is exactly the frozen-video-behind-the-menu behavior.
// Routing the menu item's message to this BHandler (attached to be_app's
// own looper, which already has its own thread from SDL) instead lets
// Go() run asynchronously: it returns immediately, the SDL loop keeps
// pumping events and rendering frames while the menu is open, and the
// selection arrives here later as an ordinary posted message.
class ContextMenuHandler : public BHandler {
public:
    ContextMenuHandler() : BHandler("hTVContextMenuHandler") {}

    virtual void MessageReceived(BMessage* message) {
        if (message->what == MSG_OPEN_CONFIG) {
            ShowConfigWindow();
        } else {
            BHandler::MessageReceived(message);
        }
    }
};

static ContextMenuHandler* gContextMenuHandler = nullptr;

// Builds and runs the right-click "apps screen" popup menu. `screenPoint`
// must be in screen coordinates. Run asynchronously (see ContextMenuHandler
// above) so the video keeps playing/rendering behind the menu instead of
// freezing until it's dismissed. In async mode BPopUpMenu deletes itself
// once it closes, so it must not be deleted here.
static void ShowMainContextMenu(BPoint screenPoint) {
    BPopUpMenu* contextMenu = new BPopUpMenu("hTVContextMenu", false, false);

    BMenuItem* configItem = new BMenuItem("Config", new BMessage(MSG_OPEN_CONFIG));
    if (gContextMenuHandler != nullptr) {
        configItem->SetTarget(gContextMenuHandler);
    }
    contextMenu->AddItem(configItem);

    contextMenu->Go(screenPoint, true /* deliversMessage */, false /* openAnyway */, true /* async */);
}

// Wake up the main loop on a new video frame arrival
void on_mpv_render_update(void* ctx) {
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_USEREVENT;
    SDL_PushEvent(&event);
}

void UpdatePlayerWindowTitle(PlayerCtx* ctx) {
    if (!ctx->mpv || !ctx->window) return;

    char* titleStr = nullptr;
    mpv_get_property(ctx->mpv, "media-title", MPV_FORMAT_STRING, &titleStr);

    int isBuffering = 0;
    int64_t bufferPercent = 0;
    mpv_get_property(ctx->mpv, "paused-for-cache", MPV_FORMAT_FLAG, &isBuffering);
    mpv_get_property(ctx->mpv, "cache-buffering-state", MPV_FORMAT_INT64, &bufferPercent);

    const char* activeName = "Ready to Stream";
    char* pathStr = nullptr;
    mpv_get_property(ctx->mpv, "path", MPV_FORMAT_STRING, &pathStr);

    if (pathStr == nullptr || pathStr[0] == '\0') {
        activeName = "Idle";
    } else if (titleStr != nullptr && titleStr[0] != '\0') { 
        activeName = titleStr;
    } else {
        activeName = pathStr; 
    }

    char finalTitle[512];
    if (isBuffering && pathStr != nullptr && pathStr[0] != '\0') {
        snprintf(finalTitle, sizeof(finalTitle), "hTV - [Buffering %d%%] - %s", (int)bufferPercent, activeName);
    } else {
        snprintf(finalTitle, sizeof(finalTitle), "hTV - %s", activeName);
    }

    if (strcmp(ctx->currentTitle, finalTitle) != 0) {
        snprintf(ctx->currentTitle, sizeof(ctx->currentTitle), "%s", finalTitle);
        SDL_SetWindowTitle(ctx->window, ctx->currentTitle);
    }

    if (titleStr) mpv_free(titleStr);
    if (pathStr) mpv_free(pathStr);
}

int main(int argc, char* argv[]) {
    setenv("BE_APP_SIGNATURE", "application/x-vnd.hTV", 1);

    const char* streamUrl = "";
    if (argc > 1 && argv[1] != nullptr) {
        streamUrl = argv[1];
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "SDL Could not initialize: %s\n", SDL_GetError());
        return 1;
    }

    // SDL_Init() brings up a BApplication (be_app) under the hood on Haiku.
    // Register the context menu's async message target on it now so the
    // right-click popup menu never blocks the SDL render loop (see
    // ContextMenuHandler above).
    gContextMenuHandler = new ContextMenuHandler();
    if (be_app->Lock()) {
        be_app->AddHandler(gContextMenuHandler);
        be_app->Unlock();
    }

    // --- CHECK FOR MESA OPENGL DRIVER CAPABILITY AT RUNTIME ---
    struct stat mesaBuffer;
    bool hasHardwareDriver = (stat("/boot/system/add-ons/opengl/egl_vendor.d/libEGL_mesa.so", &mesaBuffer) == 0);

    PlayerCtx ctx;
    ctx.isRunning = true;
    ctx.isFullscreen = false;
    ctx.glContext = nullptr;
    ctx.renderer = nullptr;
    ctx.texture = nullptr;
    ctx.texWidth = 0;
    ctx.texHeight = 0;
    memset(ctx.currentTitle, 0, sizeof(ctx.currentTitle));
    snprintf(ctx.currentTitle, sizeof(ctx.currentTitle), "hTV Player");

    if (hasHardwareDriver) {
        printf("[DEBUG] Drivers detected. Requesting hardware OpenGL context...\n");
        SDL_GL_MakeCurrent(NULL, NULL);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    } else {
        printf("[DEBUG] Drivers missing. Initializing fallback 2D blit engine...\n");
    }

    ctx.window = SDL_CreateWindow(
        ctx.currentTitle,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        740, 520,
        (hasHardwareDriver ? SDL_WINDOW_OPENGL : 0) | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
    );

    if (!ctx.window) {
        fprintf(stderr, "Failed to create window wrapper: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Allocate graphics subsystems conditionally based on our runtime flag
    if (hasHardwareDriver) {
        ctx.glContext = SDL_GL_CreateContext(ctx.window);
        if (!ctx.glContext) {
            fprintf(stderr, "OpenGL Context creation failed: %s\nFallback to SW mode...\n", SDL_GetError());
            hasHardwareDriver = false; // Graceful fallback if creation error triggers
        } else {
            SDL_GL_MakeCurrent(ctx.window, ctx.glContext);
        }
    }

    if (!hasHardwareDriver) {
        ctx.renderer = SDL_CreateRenderer(ctx.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ctx.renderer) {
            ctx.renderer = SDL_CreateRenderer(ctx.window, -1, SDL_RENDERER_SOFTWARE);
        }
    }

    ctx.mpv = mpv_create();
    if (!ctx.mpv) {
        fprintf(stderr, "Failed to create mpv handle instance\n");
        return 1;
    }
    g_mpv = ctx.mpv;
    LoadAudioConfig();

    if (hasHardwareDriver) {
        mpv_set_option_string(ctx.mpv, "vo", "libmpv");
        mpv_set_option_string(ctx.mpv, "hwdec", "yes"); 
    } else {
        mpv_set_option_string(ctx.mpv, "vo", "libmpv");
        mpv_set_option_string(ctx.mpv, "profile", "sw-fast");
        mpv_set_option_string(ctx.mpv, "hwdec", "no"); 
    }

    mpv_set_option_string(ctx.mpv, "terminal", "no");
    mpv_set_option_string(ctx.mpv, "msg-level", "all=no");
    mpv_set_option_string(ctx.mpv, "osd-level", "1");

    mpv_set_option_string(ctx.mpv, "cache", "yes");               
    mpv_set_option_string(ctx.mpv, "demuxer-max-bytes", "200M");  
    mpv_set_option_string(ctx.mpv, "demuxer-max-back-bytes", "150M");
    mpv_set_option_string(ctx.mpv, "force-seekable", "yes");     
    mpv_set_option_string(ctx.mpv, "video-sync", "audio");
    mpv_set_option_string(ctx.mpv, "audio-pitch-correction", "no");
    mpv_set_option_string(ctx.mpv, "speed", "1.05");

    if (mpv_initialize(ctx.mpv) < 0) {
        fprintf(stderr, "Failed to initialize mpv client core\n");
        return 1;
    }
    
   

    // Dynamic Parameter Layout Configuration block
    mpv_opengl_init_params glParams;
    mpv_render_param* paramsPtr;

    mpv_render_param paramsHW[] = {
        {MPV_RENDER_PARAM_API_TYPE, (void*)MPV_RENDER_API_TYPE_OPENGL},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glParams},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    mpv_render_param paramsSW[] = {
        {MPV_RENDER_PARAM_API_TYPE, (void*)MPV_RENDER_API_TYPE_SW},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    if (hasHardwareDriver) {
        glParams.get_proc_address = [](void* ctx, const char* name) -> void* {
            return (void*)SDL_GL_GetProcAddress(name);
        };
        glParams.get_proc_address_ctx = nullptr;
        paramsPtr = paramsHW;
    } else {
        paramsPtr = paramsSW;
    }

    if (mpv_render_context_create(&ctx.mpvRender, ctx.mpv, paramsPtr) < 0) {
        fprintf(stderr, "Failed to bind mpv rendering frame pipeline\n");
        return 1;
    }

    mpv_render_context_set_update_callback(ctx.mpvRender, on_mpv_render_update, nullptr);

    // Push any EQ/reverb/FX settings restored from disk before playback starts.
    ApplyAudioFilters(ctx.mpv);

    printf("[DEBUG] Launching source feed stream target: %s\n", streamUrl);
    const char* loadCmd[] = {"loadfile", streamUrl, nullptr};
    mpv_command(ctx.mpv, loadCmd);

    SDL_Event event;
    uint32_t lastTitleUpdate = 0;
    bool needsRender = false; 

	{
	    const char* targetUrl = "https://raw.githubusercontent.com/ablyssx74/hTV/refs/heads/main/VERSION";
	    const char* localVersion = "v1.2.0";
	
	    char updateCmd[1024];
	    snprintf(updateCmd, sizeof(updateCmd),
	        "(REMOTE_V=$(curl -sL \"%s\" | tr -d '\\r\\n'); "
	        "if [ ! -z \"$REMOTE_V\" ] && [ \"$REMOTE_V\" != \"%s\" ]; then "
	        "notify --title \"Update Available\" --group \"hTV\" "
	        "\"A newer version of hTV is available! ($REMOTE_V)\"; fi) &",
	        targetUrl, localVersion);	
	    system(updateCmd);
	}

    while (ctx.isRunning) {
        if (SDL_WaitEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT: {
                    ctx.isRunning = false;
                    break;
                }
                case SDL_KEYDOWN: {
                    switch (event.key.keysym.sym) {
                        case SDLK_q: ctx.isRunning = false; break;                        
                        case SDLK_UP: {
                            mpv_command_string(ctx.mpv, "add volume 5");
                            mpv_command_string(ctx.mpv, "show-text \"Volume: ${volume}%\"");
                            break;
                        }
                        case SDLK_DOWN: {
                            mpv_command_string(ctx.mpv, "add volume -5");
                            mpv_command_string(ctx.mpv, "show-text \"Volume: ${volume}%\"");
                            break;
                        }
                        case SDLK_LEFTBRACKET: mpv_command_string(ctx.mpv, "add speed -0.01"); break;
                        case SDLK_RIGHTBRACKET: mpv_command_string(ctx.mpv, "add speed 0.01"); break;
                        case SDLK_f: {
                            ctx.isFullscreen = !ctx.isFullscreen;
                            SDL_SetWindowFullscreen(ctx.window, ctx.isFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                            SDL_ShowCursor(ctx.isFullscreen ? SDL_DISABLE : SDL_ENABLE);
                            break;
                        }
                        case SDLK_SPACE: {
                            int pauseState = 0;
                            mpv_get_property(ctx.mpv, "pause", MPV_FORMAT_FLAG, &pauseState);
                            pauseState = !pauseState;
                            mpv_set_property(ctx.mpv, "pause", MPV_FORMAT_FLAG, &pauseState);
                            break;
                        }
                        case SDLK_LEFT: {
                            mpv_command_string(ctx.mpv, "seek -5 relative exact");
                            mpv_command_string(ctx.mpv, "show-progress");
                            break;
                        }
                        case SDLK_RIGHT: {
                            mpv_command_string(ctx.mpv, "seek 5 relative exact");
                            mpv_command_string(ctx.mpv, "show-progress");
                            break;
                        }
                        case SDLK_COMMA: {
                            mpv_command_string(ctx.mpv, "seek -10 relative exact");
                            mpv_command_string(ctx.mpv, "show-progress");
                            break;
                        }
                        case SDLK_PERIOD: {
                            mpv_command_string(ctx.mpv, "seek 10 relative exact");
                            mpv_command_string(ctx.mpv, "show-progress");
                            break;
                        }
                    }
                    break;
                }
                case SDL_MOUSEBUTTONDOWN: {
                    if (event.button.button == SDL_BUTTON_LEFT && event.button.clicks == 2) {
                        ctx.isFullscreen = !ctx.isFullscreen;
                        SDL_SetWindowFullscreen(ctx.window, ctx.isFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        SDL_ShowCursor(ctx.isFullscreen ? SDL_DISABLE : SDL_ENABLE);
                    } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                        mpv_command_string(ctx.mpv, "cycle mute");
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        int windowX = 0, windowY = 0;
                        SDL_GetWindowPosition(ctx.window, &windowX, &windowY);
                        BPoint screenPoint(windowX + event.button.x, windowY + event.button.y);
                        ShowMainContextMenu(screenPoint);
                    }
                    break;
                }
                case SDL_MOUSEWHEEL: {
                    if (event.wheel.y > 0) {
                        mpv_command_string(ctx.mpv, "add volume 5");
                        mpv_command_string(ctx.mpv, "show-text \"Volume: ${volume}%\"");
                    } else if (event.wheel.y < 0) {
                        mpv_command_string(ctx.mpv, "add volume -5");
                        mpv_command_string(ctx.mpv, "show-text \"Volume: ${volume}%\"");
                    }
                    break;
                }
                case SDL_DROPFILE: {
                    char* droppedFilePath = event.drop.file;
                    if (droppedFilePath != nullptr && droppedFilePath[0] != '\0') {
                        printf("[DEBUG] File received via Drag & Drop: %s\n", droppedFilePath);
                        const char* loadCmd[] = {"loadfile", droppedFilePath, nullptr};
                        mpv_command(ctx.mpv, loadCmd);
                        SDL_RaiseWindow(ctx.window);
                    }
                    SDL_free(droppedFilePath); 
                    break;
                }
                case SDL_USEREVENT: needsRender = true; break;
                case SDL_WINDOWEVENT: {
                    if (event.window.event == SDL_WINDOWEVENT_EXPOSED || 
                        event.window.event == SDL_WINDOWEVENT_RESIZED) {
                        needsRender = true;
                    }
                    break;
                }
            }
        }

        uint32_t currentTime = SDL_GetTicks();
        if (currentTime - lastTitleUpdate > 250) {
            UpdatePlayerWindowTitle(&ctx);
            lastTitleUpdate = currentTime;
        }
        
        // --- DYNAMIC RENDERING BLOCK ---
        if (needsRender && ctx.mpvRender) {
            int w, h;
            SDL_GetWindowSize(ctx.window, &w, &h);

            if (hasHardwareDriver) {
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);

                mpv_opengl_fbo fbo{ 0, w, h, 0 };
                int flip_y = 1;

                mpv_render_param renderParams[] = {
                    {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
                    {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
                    {MPV_RENDER_PARAM_INVALID, nullptr}
                };

                mpv_render_context_render(ctx.mpvRender, renderParams);
                SDL_GL_SwapWindow(ctx.window);
            } else {
                if (!ctx.texture || ctx.texWidth != w || ctx.texHeight != h) {
                    if (ctx.texture) SDL_DestroyTexture(ctx.texture);
                    ctx.texture = SDL_CreateTexture(
                        ctx.renderer, 
                        SDL_PIXELFORMAT_ARGB8888, 
                        SDL_TEXTUREACCESS_STREAMING, 
                        w, h
                    );
                    ctx.texWidth = w;
                    ctx.texHeight = h;
                }

                void* pixels = nullptr;
                int pitch = 0;
                
                if (SDL_LockTexture(ctx.texture, nullptr, &pixels, &pitch) == 0) {
                    int sizeParams[] = { w, h };
                    char fmtParams[] = "bgr0"; 

                    mpv_render_param renderParams[] = {
                        {MPV_RENDER_PARAM_SW_SIZE, sizeParams},
                        {MPV_RENDER_PARAM_SW_STRIDE, &pitch},
                        {MPV_RENDER_PARAM_SW_POINTER, pixels},
                        {MPV_RENDER_PARAM_SW_FORMAT, fmtParams}, 
                        {MPV_RENDER_PARAM_INVALID, nullptr}
                    };

                    mpv_render_context_render(ctx.mpvRender, renderParams);
                    SDL_UnlockTexture(ctx.texture);
                }

                SDL_RenderClear(ctx.renderer);
                SDL_RenderCopy(ctx.renderer, ctx.texture, nullptr, nullptr);
                SDL_RenderPresent(ctx.renderer);
            }
            needsRender = false; 
        }
    }

    if (gConfigWindow != nullptr) {
        if (gConfigWindow->Lock()) gConfigWindow->Quit();
        gConfigWindow = nullptr;
    }

    if (gContextMenuHandler != nullptr) {
        if (be_app->Lock()) {
            be_app->RemoveHandler(gContextMenuHandler);
            be_app->Unlock();
        }
        delete gContextMenuHandler;
        gContextMenuHandler = nullptr;
    }

    if (ctx.texture) SDL_DestroyTexture(ctx.texture);
    if (ctx.renderer) SDL_DestroyRenderer(ctx.renderer);
    if (ctx.glContext) SDL_GL_DeleteContext(ctx.glContext);

    mpv_render_context_free(ctx.mpvRender);
    mpv_destroy(ctx.mpv);
    SDL_DestroyWindow(ctx.window);
    SDL_Quit();

    return 0;
}

