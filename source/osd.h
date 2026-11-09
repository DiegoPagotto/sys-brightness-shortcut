#pragma once

#include <switch.h>

// Creates the VI layer. Call early during boot: a layer created after the HOME
// menu is up never becomes visible. The layer is kept for the process lifetime
// and holds a transparent frame while the bar is hidden.
void osdInitialize(void);

// Shows (or refreshes) the brightness bar. `previous` is where the bar animates
// from when the OSD was hidden. Retries osdInitialize() if it failed before.
void osdShow(float level, float previous);

// Advances fade/fill animations and presents a frame when something changed.
void osdUpdate(void);

// True while the OSD needs frequent osdUpdate() calls (visible or fading).
bool osdIsActive(void);

// Releases the VI layer and display sessions (no-op when already released).
void osdExit(void);
