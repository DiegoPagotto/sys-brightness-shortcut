#include "sound.h"

#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "log.h"
#include "tone.h"

// A small pool so a tick never overwrites samples that are still playing.
#define BUFFER_COUNT 3
// Ticks allowed to wait in the queue; more would make held repeats lag behind.
#define MAX_QUEUED 2

static AudioOutBuffer g_buffers[BUFFER_COUNT];
static bool           g_queued[BUFFER_COUNT];
static u32            g_frames;
static u32            g_channels;
static u32            g_sampleRate;
static bool           g_started;
static bool           g_available;

Result soundInitialize(void)
{
    Result rc = audoutInitialize();
    g_available = R_SUCCEEDED(rc);
    return rc;
}

static bool soundStart(void)
{
    g_sampleRate = audoutGetSampleRate();
    g_channels = audoutGetChannelCount();
    if (g_sampleRate == 0 || g_channels == 0) return false;

    g_frames = (u32)(TONE_DURATION * (float)g_sampleRate);
    u64 data_size = (u64)g_frames * g_channels * sizeof(s16);
    u64 buffer_size = (data_size + 0xFFF) & ~0xFFFULL;

    for (int i = 0; i < BUFFER_COUNT; i++) {
        void *mem = memalign(0x1000, buffer_size);
        if (!mem) goto fail;
        memset(mem, 0, buffer_size);
        g_buffers[i] = (AudioOutBuffer){
            .next = NULL,
            .buffer = mem,
            .buffer_size = buffer_size,
            .data_size = data_size,
            .data_offset = 0,
        };
        g_queued[i] = false;
    }

    Result rc = audoutStartAudioOut();
    if (R_FAILED(rc)) {
        logLine("audoutStartAudioOut failed: 0x%x", rc);
        goto fail;
    }
    g_started = true;
    return true;

fail:
    for (int i = 0; i < BUFFER_COUNT; i++) {
        free(g_buffers[i].buffer);
        g_buffers[i].buffer = NULL;
    }
    return false;
}

static void reclaimReleased(void)
{
    for (int guard = 0; guard <= BUFFER_COUNT; guard++) {
        AudioOutBuffer *released = NULL;
        u32 count = 0;
        if (R_FAILED(audoutGetReleasedAudioOutBuffer(&released, &count)) || count == 0 || !released) return;
        for (int i = 0; i < BUFFER_COUNT; i++)
            if (released == &g_buffers[i]) g_queued[i] = false;
    }
}

void soundPlayTick(float level)
{
    if (!g_available) return;
    if (!g_started && !soundStart()) return;

    reclaimReleased();

    int queued = 0, free_index = -1;
    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (g_queued[i]) queued++;
        else if (free_index < 0) free_index = i;
    }
    if (free_index < 0 || queued >= MAX_QUEUED) return;

    AudioOutBuffer *buf = &g_buffers[free_index];
    toneRender((s16 *)buf->buffer, g_frames, g_channels, g_sampleRate, level);
    armDCacheFlush(buf->buffer, buf->data_size);

    if (R_SUCCEEDED(audoutAppendAudioOutBuffer(buf)))
        g_queued[free_index] = true;
}

void soundExit(void)
{
    if (g_started) {
        audoutStopAudioOut();
        for (int i = 0; i < BUFFER_COUNT; i++) {
            free(g_buffers[i].buffer);
            g_buffers[i].buffer = NULL;
        }
        g_started = false;
    }
    if (g_available) {
        audoutExit();
        g_available = false;
    }
}
