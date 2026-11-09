#pragma once

#include <stdint.h>

// Length of one feedback tick, in seconds.
#define TONE_DURATION 0.12f

// Synthesizes the brightness feedback "tick" as interleaved int16 PCM.
// Pitch follows `level` (0..1) along a major pentatonic scale, so stepping
// brightness up or down plays an ascending or descending melody.
void toneRender(int16_t *out, uint32_t frames, uint32_t channels, uint32_t sample_rate, float level);
