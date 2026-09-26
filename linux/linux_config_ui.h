/*
 * Copyright 2026, Kris Beazley (ablyss) hTV@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

// Linux/Qt front-end for the Audio Configuration window, opened via a
// right-click (Qt widgets, chosen to match a KDE/Plasma desktop's own
// theme). Qt runs on its own dedicated thread with its own QApplication and
// event loop -- entirely independent of SDL's main loop thread, the same
// two-independent-loops shape the Haiku build uses (there, SDL brings up its
// own BApplication thread; here, hTV brings up a Qt one itself). Because the
// two loops never share a thread, opening the config window can never stall
// SDL's video rendering.
//
// There's no right-click context *menu* here (unlike the Haiku build's
// BPopUpMenu): a QMenu is a Wayland "grabbing popup", which requires a
// parent window that just received the real input event that triggered it
// -- SDL's window catches the actual click, not any window Qt knows about,
// so Wayland refuses to show it (confirmed on real KDE/Wayland hardware;
// see the comment on ShowLinuxContextMenu() in the .cpp for the exact
// error). Right-click opens the Config window directly instead.

// Starts the Qt thread (QApplication + event loop) and blocks briefly until
// it's up and ready to receive requests. Call once, after LoadAudioConfig().
void StartLinuxAudioUI();

// Asks the Qt thread to open (or re-activate) the Audio Configuration
// window. Safe to call from any thread; the actual widget work happens on
// the Qt thread.
void ShowLinuxConfigWindow();

// Handles a right-click: opens the Config window (see the file comment
// above for why this isn't a popup menu). Safe to call from any thread
// (SDL's main loop calls this directly on right-click).
void ShowLinuxContextMenu();

// Stops the Qt event loop and joins its thread. Call once during shutdown.
void StopLinuxAudioUI();
