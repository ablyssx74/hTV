/*
 * Copyright 2026, Kris Beazley (ablyss) hTV@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

// Linux/Qt front-end for the Audio Configuration window and its right-click
// context menu (Qt widgets, chosen to match a KDE/Plasma desktop's own
// theme). Qt runs on its own dedicated thread with its own QApplication and
// event loop -- entirely independent of SDL's main loop thread, the same
// two-independent-loops shape the Haiku build uses (there, SDL brings up its
// own BApplication thread; here, hTV brings up a Qt one itself). Because the
// two loops never share a thread, popping the context menu or the config
// window can never stall SDL's video rendering.

// Starts the Qt thread (QApplication + event loop) and blocks briefly until
// it's up and ready to receive requests. Call once, after LoadAudioConfig().
void StartLinuxAudioUI();

// Asks the Qt thread to open (or re-activate) the Audio Configuration
// window. Safe to call from any thread; the actual widget work happens on
// the Qt thread.
void ShowLinuxConfigWindow();

// Asks the Qt thread to pop the right-click context menu at the given
// screen coordinates. Safe to call from any thread (SDL's main loop calls
// this directly on right-click).
void ShowLinuxContextMenu(int screenX, int screenY);

// Stops the Qt event loop and joins its thread. Call once during shutdown.
void StopLinuxAudioUI();
