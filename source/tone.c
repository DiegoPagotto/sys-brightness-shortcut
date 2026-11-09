// Feedback sound: a short, soft mallet-like "tock" instead of a raw sine beep.
//  - smooth 2 ms attack and a click-free fade at the end (no pops)
//  - a few harmonics with faster decay on the upper ones (warm, rounded body)
//  - tiny downward pitch glide at the onset (pluck feel)
//  - two quiet, different-per-ear early reflections (space, less "dry")

#include "tone.h"

#include <math.h>

#define PI_F 3.14159265358979f

#define BASE_FREQ    392.0f   // G4
#define ATTACK_TIME  0.002f
#define RELEASE_TIME 0.015f
#define GLIDE_AMOUNT 0.04f    // starts 4% sharp...
#define GLIDE_TIME   0.006f   // ...and settles within a few ms
#define PEAK_GAIN    0.20f    // of full scale

typedef struct { float ratio, amp, decay; } Partial;

static const Partial PARTIALS[] = {
    { 1.0f,  1.00f, 0.040f },
    { 2.0f,  0.30f, 0.022f },
    { 3.0f,  0.12f, 0.012f },
    { 4.16f, 0.08f, 0.005f },  // slightly inharmonic: the "wooden" strike
};
#define PARTIAL_COUNT (sizeof(PARTIALS) / sizeof(PARTIALS[0]))

typedef struct { float delay, gain; } Echo;

static const Echo ECHO_L[] = { { 0.019f, 0.22f }, { 0.043f, 0.10f } };
static const Echo ECHO_R[] = { { 0.025f, 0.22f }, { 0.049f, 0.10f } };
#define ECHO_COUNT 2

// Major pentatonic from G4 to G6: 11 notes for brightness 0%, 10%, ..., 100%.
static const float SCALE[] = { 0, 2, 4, 7, 9, 12, 14, 16, 19, 21, 24 };
#define SCALE_LAST (sizeof(SCALE) / sizeof(SCALE[0]) - 1)

static float levelToFrequency(float level)
{
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    float pos = level * (float)SCALE_LAST;
    unsigned i = (unsigned)pos;
    if (i >= SCALE_LAST) i = SCALE_LAST - 1;
    float frac = pos - (float)i;
    float semis = SCALE[i] + (SCALE[i + 1] - SCALE[i]) * frac;
    return BASE_FREQ * exp2f(semis / 12.0f);
}

static float dry(float t, float freq)
{
    if (t < 0.0f) return 0.0f;
    // Integral of freq * (1 + g * e^(-t/tau)) gives the glide's phase in closed form.
    float phase = 2.0f * PI_F * freq * (t + GLIDE_AMOUNT * GLIDE_TIME * (1.0f - expf(-t / GLIDE_TIME)));
    float v = 0.0f;
    for (unsigned k = 0; k < PARTIAL_COUNT; k++)
        v += PARTIALS[k].amp * expf(-t / PARTIALS[k].decay) * sinf(PARTIALS[k].ratio * phase);
    if (t < ATTACK_TIME) v *= 0.5f - 0.5f * cosf(PI_F * t / ATTACK_TIME);
    return v;
}

static float withEchoes(float t, float freq, const Echo *echoes)
{
    float v = dry(t, freq);
    for (int i = 0; i < ECHO_COUNT; i++)
        v += echoes[i].gain * dry(t - echoes[i].delay, freq);
    return v;
}

void toneRender(int16_t *out, uint32_t frames, uint32_t channels, uint32_t sample_rate, float level)
{
    const float freq = levelToFrequency(level);
    const float duration = (float)frames / (float)sample_rate;
    // Peak of the summed partials right after the attack is ~1.5.
    const float scale = PEAK_GAIN * 32767.0f / 1.5f;

    for (uint32_t n = 0; n < frames; n++) {
        float t = (float)n / (float)sample_rate;
        float release = 1.0f;
        float remaining = duration - t;
        if (remaining < RELEASE_TIME) release = 0.5f - 0.5f * cosf(PI_F * remaining / RELEASE_TIME);

        float l = withEchoes(t, freq, ECHO_L) * release * scale;
        float r = withEchoes(t, freq, ECHO_R) * release * scale;
        if (l > 32767.0f) l = 32767.0f; else if (l < -32768.0f) l = -32768.0f;
        if (r > 32767.0f) r = 32767.0f; else if (r < -32768.0f) r = -32768.0f;

        for (uint32_t c = 0; c < channels; c++)
            out[n * channels + c] = (int16_t)((c & 1) ? r : l);
    }
}
