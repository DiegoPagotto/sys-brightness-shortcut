#pragma once

#include <switch.h>

// Opens AudioOut. Needs an active SM session. Until it succeeds,
// soundPlayTick() does nothing.
Result soundInitialize(void);

// Plays the brightness feedback tick for the given level (0..1).
void soundPlayTick(float level);

// Stops playback and closes AudioOut.
void soundExit(void);
