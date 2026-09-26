/*
 * Copyright 2026, Kris Beazley (ablyss) hTV@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "linux_config_ui.h"
#include "audio_fx.h"

#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QMenu>
#include <QSettings>
#include <QMetaObject>
#include <QCloseEvent>

#include <thread>
#include <atomic>
#include <chrono>
#include <clocale>

namespace {

// ---- small layout helpers --------------------------------------------

// One vertical EQ-band slider with its frequency label underneath, mirroring
// the Haiku build's WheelSlider + BStringView band columns.
QSlider* AddVSlider(QBoxLayout* parent, const QString& freqLabel, int min, int max, int value) {
    QSlider* slider = new QSlider(Qt::Vertical);
    slider->setRange(min, max);
    slider->setValue(value);
    slider->setMinimumHeight(140);

    QLabel* label = new QLabel(freqLabel);
    label->setAlignment(Qt::AlignHCenter);

    QVBoxLayout* column = new QVBoxLayout();
    column->addWidget(slider, 0, Qt::AlignHCenter);
    column->addWidget(label, 0, Qt::AlignHCenter);
    parent->addLayout(column);
    return slider;
}

// One horizontal parameter slider (name label, slider, live value readout),
// mirroring the Haiku build's Limiter/Reverb/Chorus rows.
QSlider* AddHSlider(QBoxLayout* parent, const QString& name, int min, int max, int value) {
    QLabel* nameLabel = new QLabel(name);
    nameLabel->setMinimumWidth(80);

    QSlider* slider = new QSlider(Qt::Horizontal);
    slider->setRange(min, max);
    slider->setValue(value);

    QLabel* valueLabel = new QLabel(QString::number(value));
    valueLabel->setMinimumWidth(45);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QObject::connect(slider, &QSlider::valueChanged, valueLabel, [valueLabel](int v) {
        valueLabel->setText(QString::number(v));
    });

    QHBoxLayout* row = new QHBoxLayout();
    row->addWidget(nameLabel);
    row->addWidget(slider, 1);
    row->addWidget(valueLabel);
    parent->addLayout(row);
    return slider;
}

// ---- ConfigWindow -------------------------------------------------------
//
// A plain QWidget (no custom signals/slots of its own, so no Q_OBJECT/moc is
// needed here -- every control below is a stock Qt widget whose signal is
// wired straight to a lambda). Closing it via the window's close button just
// hides it (Qt's default QWidget behavior without Qt::WA_DeleteOnClose), so
// a second right-click reuses the same window instead of rebuilding it.

class ConfigWindow : public QWidget {
public:
    ConfigWindow() {
        setWindowTitle("hTV - Audio Configuration");
        setAttribute(Qt::WA_DeleteOnClose, false);

        QVBoxLayout* mainLayout = new QVBoxLayout(this);

        // ---- 15-Band EQ ----
        QGroupBox* eqGroup = new QGroupBox("15-Band Equalizer");
        QVBoxLayout* eqLayout = new QVBoxLayout(eqGroup);

        QHBoxLayout* eqTopRow = new QHBoxLayout();
        fEqToggle = new QCheckBox("Enable Equalizer");
        fEqToggle->setChecked(gAudioCfg.eqEnabled);
        eqTopRow->addWidget(fEqToggle);
        eqTopRow->addStretch(1);
        eqTopRow->addWidget(new QLabel("Preset:"));
        fPresetCombo = new QComboBox();
        fPresetCombo->addItems({"Flat", "Rock", "Jazz", "Bass Boost"});
        eqTopRow->addWidget(fPresetCombo);
        eqLayout->addLayout(eqTopRow);

        QHBoxLayout* sliderRow = new QHBoxLayout();
        for (int i = 0; i < 15; i++) {
            fEqSliders[i] = AddVSlider(sliderRow, kEqFreqLabels[i], -15, 15, (int)gAudioCfg.eqBands[i]);
        }
        eqLayout->addLayout(sliderRow);
        mainLayout->addWidget(eqGroup);

        // ---- Limiter (paired with the EQ, same as HaikuSuperMusicThingy) ----
        QGroupBox* limiterGroup = new QGroupBox("Limiter");
        QVBoxLayout* limiterLayout = new QVBoxLayout(limiterGroup);
        fLimitInputSlider = AddHSlider(limiterLayout, "In", -20, 20, (int)gAudioCfg.limitInput);
        fLimitThresholdSlider = AddHSlider(limiterLayout, "Threshold", -20, 0, (int)gAudioCfg.limitThreshold);
        fLimitReleaseSlider = AddHSlider(limiterLayout, "Release", 10, 1000, (int)gAudioCfg.limitRelease);
        mainLayout->addWidget(limiterGroup);

        // ---- Reverb & FX ----
        QGroupBox* fxGroup = new QGroupBox("Reverb && Effects");
        QVBoxLayout* fxLayout = new QVBoxLayout(fxGroup);

        QHBoxLayout* reverbTopRow = new QHBoxLayout();
        fReverbToggle = new QCheckBox("Enable Reverb");
        fReverbToggle->setChecked(gAudioCfg.reverbEnabled);
        reverbTopRow->addWidget(fReverbToggle);
        reverbTopRow->addStretch(1);
        reverbTopRow->addWidget(new QLabel("Type:"));
        fReverbTypeCombo = new QComboBox();
        fReverbTypeCombo->addItems({"Room", "Hall", "Plate", "Canyon"});
        fReverbTypeCombo->setCurrentIndex(gAudioCfg.reverbType % 4);
        reverbTopRow->addWidget(fReverbTypeCombo);
        fxLayout->addLayout(reverbTopRow);

        fRoomSizeSlider = AddHSlider(fxLayout, "Room Size", 0, 100, (int)gAudioCfg.reverbRoomSize);
        fDampingSlider = AddHSlider(fxLayout, "Damping", 0, 100, (int)gAudioCfg.reverbDamping);
        fWetSlider = AddHSlider(fxLayout, "Wet Level", 0, 100, (int)gAudioCfg.reverbWet);

        fChorusToggle = new QCheckBox("Enable Chorus");
        fChorusToggle->setChecked(gAudioCfg.chorusEnabled);
        fxLayout->addWidget(fChorusToggle);

        // Chorus gets the same dynamic Rate/Depth/Mix sliders Reverb has.
        fChorusRateSlider = AddHSlider(fxLayout, "Rate", 0, 100, (int)gAudioCfg.chorusRate);
        fChorusDepthSlider = AddHSlider(fxLayout, "Depth", 0, 100, (int)gAudioCfg.chorusDepth);
        fChorusMixSlider = AddHSlider(fxLayout, "Mix", 0, 100, (int)gAudioCfg.chorusMix);

        mainLayout->addWidget(fxGroup);

        wireSignals();
        resize(760, 640);
    }

protected:
    // Closing the window (X button) just hides it -- WA_DeleteOnClose is
    // false above, so Qt's default closeEvent handling already does this;
    // this override only exists to make that intentional and documented.
    void closeEvent(QCloseEvent* event) override {
        hide();
        event->ignore();
    }

private:
    void applyAndSave() {
        ApplyAudioFilters(g_mpv);
        SaveAudioConfig();
    }

    void applyPreset(const float* values) {
        for (int i = 0; i < 15; i++) {
            gAudioCfg.eqBands[i] = values[i];
            fEqSliders[i]->blockSignals(true);
            fEqSliders[i]->setValue((int)values[i]);
            fEqSliders[i]->blockSignals(false);
        }
        if (!gAudioCfg.eqEnabled) {
            gAudioCfg.eqEnabled = true;
            fEqToggle->setChecked(true); // triggers onEqToggled -> applyAndSave()
        } else {
            applyAndSave();
        }
    }

    void wireSignals() {
        connect(fEqToggle, &QCheckBox::toggled, this, [this](bool on) {
            gAudioCfg.eqEnabled = on;
            applyAndSave();
        });

        for (int i = 0; i < 15; i++) {
            connect(fEqSliders[i], &QSlider::valueChanged, this, [this, i](int v) {
                gAudioCfg.eqBands[i] = (float)v;
                applyAndSave();
            });
        }

        connect(fPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
            switch (index) {
                case 1:  applyPreset(kEqPresetRock); break;
                case 2:  applyPreset(kEqPresetJazz); break;
                case 3:  applyPreset(kEqPresetBass); break;
                default: applyPreset(kEqPresetFlat); break;
            }
        });

        connect(fLimitInputSlider, &QSlider::valueChanged, this, [this](int v) {
            gAudioCfg.limitInput = (float)v;
            applyAndSave();
        });
        connect(fLimitThresholdSlider, &QSlider::valueChanged, this, [this](int v) {
            gAudioCfg.limitThreshold = (float)v;
            applyAndSave();
        });
        connect(fLimitReleaseSlider, &QSlider::valueChanged, this, [this](int v) {
            gAudioCfg.limitRelease = (float)v;
            applyAndSave();
        });

        connect(fReverbToggle, &QCheckBox::toggled, this, [this](bool on) {
            gAudioCfg.reverbEnabled = on;
            applyAndSave();
        });
        connect(fReverbTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
            gAudioCfg.reverbType = index;
            applyAndSave();
        });
        connect(fRoomSizeSlider, &QSlider::valueChanged, this, [this](int v) {
            gAudioCfg.reverbRoomSize = (float)v;
            applyAndSave();
        });
        connect(fDampingSlider, &QSlider::valueChanged, this, [this](int v) {
            gAudioCfg.reverbDamping = (float)v;
            applyAndSave();
        });
        connect(fWetSlider, &QSlider::valueChanged, this, [this](int v) {
            gAudioCfg.reverbWet = (float)v;
            applyAndSave();
        });

        connect(fChorusToggle, &QCheckBox::toggled, this, [this](bool on) {
            gAudioCfg.chorusEnabled = on;
            applyAndSave();
        });
        connect(fChorusRateSlider, &QSlider::valueChanged, this, [this](int v) {
            gAudioCfg.chorusRate = (float)v;
            applyAndSave();
        });
        connect(fChorusDepthSlider, &QSlider::valueChanged, this, [this](int v) {
            gAudioCfg.chorusDepth = (float)v;
            applyAndSave();
        });
        connect(fChorusMixSlider, &QSlider::valueChanged, this, [this](int v) {
            gAudioCfg.chorusMix = (float)v;
            applyAndSave();
        });
    }

    QCheckBox* fEqToggle = nullptr;
    QSlider*   fEqSliders[15] = {nullptr};
    QComboBox* fPresetCombo = nullptr;

    QSlider* fLimitInputSlider = nullptr;
    QSlider* fLimitThresholdSlider = nullptr;
    QSlider* fLimitReleaseSlider = nullptr;

    QCheckBox* fReverbToggle = nullptr;
    QComboBox* fReverbTypeCombo = nullptr;
    QSlider*   fRoomSizeSlider = nullptr;
    QSlider*   fDampingSlider = nullptr;
    QSlider*   fWetSlider = nullptr;

    QCheckBox* fChorusToggle = nullptr;
    QSlider*   fChorusRateSlider = nullptr;
    QSlider*   fChorusDepthSlider = nullptr;
    QSlider*   fChorusMixSlider = nullptr;
};

// ---- QSettings-backed persistence ---------------------------------------
//
// Linux's natural, idiomatic analog of "one flat settings file" (the flat
// BMessage the Haiku build uses): a single ini-format QSettings file at
// ~/.config/hTV/hTV.conf.

QSettings MakeSettings() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope, "hTV", "hTV");
}

// ---- Qt thread lifecycle -------------------------------------------------

std::thread gQtThread;
std::atomic<bool> gQtReady{false};
QObject* gQtContext = nullptr; // the QApplication instance; lives on gQtThread
ConfigWindow* gConfigWindow = nullptr; // owned by, and only touched on, gQtThread

// Safety net for early-exit paths in main() (e.g. mpv or window-creation
// failure) that skip the normal StopLinuxAudioUI() shutdown call: a
// std::thread that is still joinable when destroyed calls std::terminate(),
// so if the Qt thread was ever started, detach it before gQtThread's own
// destructor runs. Destruction order for namespace-scope statics within one
// translation unit is the reverse of construction order, so this guard --
// declared right after gQtThread -- is destroyed first.
struct QtThreadGuard {
    ~QtThreadGuard() {
        if (gQtThread.joinable()) gQtThread.detach();
    }
} gQtThreadGuard;

void EnsureConfigWindow() {
    if (!gConfigWindow) {
        gConfigWindow = new ConfigWindow();
    }
}

} // namespace

void SaveAudioConfig() {
    QSettings settings = MakeSettings();
    settings.setValue("eq_enabled", gAudioCfg.eqEnabled);
    QVariantList bands;
    for (int i = 0; i < 15; i++) bands << gAudioCfg.eqBands[i];
    settings.setValue("eq_bands", bands);
    settings.setValue("limit_input", gAudioCfg.limitInput);
    settings.setValue("limit_threshold", gAudioCfg.limitThreshold);
    settings.setValue("limit_release", gAudioCfg.limitRelease);
    settings.setValue("reverb_enabled", gAudioCfg.reverbEnabled);
    settings.setValue("reverb_type", gAudioCfg.reverbType);
    settings.setValue("reverb_room_size", gAudioCfg.reverbRoomSize);
    settings.setValue("reverb_damping", gAudioCfg.reverbDamping);
    settings.setValue("reverb_wet", gAudioCfg.reverbWet);
    settings.setValue("chorus_enabled", gAudioCfg.chorusEnabled);
    settings.setValue("chorus_rate", gAudioCfg.chorusRate);
    settings.setValue("chorus_depth", gAudioCfg.chorusDepth);
    settings.setValue("chorus_mix", gAudioCfg.chorusMix);
}

void LoadAudioConfig() {
    QSettings settings = MakeSettings();
    gAudioCfg.eqEnabled = settings.value("eq_enabled", false).toBool();

    QVariantList bands = settings.value("eq_bands").toList();
    for (int i = 0; i < 15; i++) {
        gAudioCfg.eqBands[i] = (i < bands.size()) ? bands[i].toFloat() : 0.0f;
    }

    gAudioCfg.limitInput = settings.value("limit_input", 0.0f).toFloat();
    gAudioCfg.limitThreshold = settings.value("limit_threshold", 0.0f).toFloat();
    gAudioCfg.limitRelease = settings.value("limit_release", 100.0f).toFloat();
    gAudioCfg.reverbEnabled = settings.value("reverb_enabled", false).toBool();
    gAudioCfg.reverbType = settings.value("reverb_type", 0).toInt();
    gAudioCfg.reverbRoomSize = settings.value("reverb_room_size", 50.0f).toFloat();
    gAudioCfg.reverbDamping = settings.value("reverb_damping", 50.0f).toFloat();
    gAudioCfg.reverbWet = settings.value("reverb_wet", 30.0f).toFloat();
    gAudioCfg.chorusEnabled = settings.value("chorus_enabled", false).toBool();
    gAudioCfg.chorusRate = settings.value("chorus_rate", 30.0f).toFloat();
    gAudioCfg.chorusDepth = settings.value("chorus_depth", 40.0f).toFloat();
    gAudioCfg.chorusMix = settings.value("chorus_mix", 50.0f).toFloat();
}

void StartLinuxAudioUI() {
    gQtThread = std::thread([]() {
        int argc = 0;
        QApplication app(argc, nullptr);

        // QApplication's constructor calls setlocale(LC_ALL, "") to pick up
        // the user's system locale for text rendering -- which, on any
        // locale where the decimal separator isn't ".", also changes
        // LC_NUMERIC. setlocale() is process-global, not per-thread, so
        // this clobbers it for the whole process even though Qt runs on
        // its own thread here -- and libmpv refuses to initialize unless
        // LC_NUMERIC is "C" (it parses numeric strings assuming it). Set
        // it back immediately so mpv_create() on the main thread succeeds
        // regardless of the user's locale.
        setlocale(LC_NUMERIC, "C");

        gQtContext = &app;
        gQtReady.store(true);
        app.exec();
        gQtContext = nullptr;
    });

    // Block briefly until the Qt thread's event loop is actually up, so a
    // right-click arriving immediately after startup has somewhere to post
    // to. Ordinary thread bring-up, not a busy spin -- this loop runs at
    // most a couple of iterations in practice.
    while (!gQtReady.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void ShowLinuxConfigWindow() {
    if (!gQtContext) return;
    QMetaObject::invokeMethod(gQtContext, []() {
        EnsureConfigWindow();
        gConfigWindow->show();
        gConfigWindow->raise();
        gConfigWindow->activateWindow();
    }, Qt::QueuedConnection);
}

void ShowLinuxContextMenu(int screenX, int screenY) {
    if (!gQtContext) return;
    QMetaObject::invokeMethod(gQtContext, [screenX, screenY]() {
        // QMenu::exec() blocks, but only this Qt-thread event loop -- SDL's
        // main loop and mpv rendering run on a completely separate thread
        // and are unaffected, so the video never freezes behind this menu.
        QMenu menu;
        QAction* configAction = menu.addAction("Config");
        QAction* chosen = menu.exec(QPoint(screenX, screenY));
        if (chosen == configAction) {
            EnsureConfigWindow();
            gConfigWindow->show();
            gConfigWindow->raise();
            gConfigWindow->activateWindow();
        }
    }, Qt::QueuedConnection);
}

void StopLinuxAudioUI() {
    if (gQtContext) {
        QMetaObject::invokeMethod(gQtContext, []() {
            if (gConfigWindow) {
                gConfigWindow->setAttribute(Qt::WA_DeleteOnClose, true);
                gConfigWindow->close();
                gConfigWindow = nullptr;
            }
            QApplication::quit();
        }, Qt::QueuedConnection);
    }
    if (gQtThread.joinable()) gQtThread.join();
}
