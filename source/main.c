// Include the most common headers from the C standard library
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include the main libnx system header, for Switch development
#include <switch.h>

#include "log.h"
#include "osd.h"
#include "sound.h"

// Size of the inner heap. Holds the OSD framebuffers (~0x68000 incl. linear
// shadow), the nvdrv transfer memory (0x40000) and the sound buffers (0x12000).
#define INNER_HEAP_SIZE 0x140000

#define BRIGHTNESS_STEP 0.1f

#ifdef __cplusplus
extern "C" {
#endif

// Sysmodules should not use applet*.
u32 __nx_applet_type = AppletType_None;

// Sysmodules will normally only want to use one FS session.
u32 __nx_fs_num_sessions = 1;

// libnx default (8 MiB) is meant for games; the OSD only needs small buffers.
u32 __nx_nv_transfermem_size = 0x40000;

// Newlib heap configuration function (makes malloc/free work).
void __libnx_initheap(void)
{
    static u8 inner_heap[INNER_HEAP_SIZE];
    extern void* fake_heap_start;
    extern void* fake_heap_end;

    // Configure the newlib heap.
    fake_heap_start = inner_heap;
    fake_heap_end   = inner_heap + sizeof(inner_heap);
}

// Service initialization. Only what logging needs: the remaining services are
// opened from main() after the system has finished booting (see initServices).
void __appInit(void)
{
    Result rc;

    rc = smInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

    // Retrieve the current version of Horizon OS.
    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw;
        rc = setsysGetFirmwareVersion(&fw);
        if (R_SUCCEEDED(rc))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }

    rc = fsInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

    fsdevMountSdmc();

    smExit();
}

// Service deinitialization. libnx exit functions are no-ops for services
// that were never initialized.
void __appExit(void)
{
    soundExit();
    hidExit();
    fsdevUnmountAll();
    fsExit();
}

#ifdef __cplusplus
}
#endif

// lbl allows only a handful of sessions system-wide, and Atmosphère's fatal
// handler aborts if it cannot open one to turn the backlight on. Never keep a
// session open: connect around each brightness operation only.
static Result lblOpen(void)
{
    Result rc = smInitialize();
    if (R_FAILED(rc)) return rc;
    rc = lblInitialize();
    smExit();
    return rc;
}

static Result readBrightness(float *brightness)
{
    Result rc = lblOpen();
    if (R_FAILED(rc)) return rc;
    rc = lblGetCurrentBrightnessSetting(brightness);
    lblExit();
    return rc;
}

static float g_brightness = 0.5f;

// Moves brightness one step in `direction` (+1/-1) with sound and OSD feedback.
static void stepBrightness(int direction)
{
    Result rc = lblOpen();
    if (R_FAILED(rc)) {
        logLine("Error opening lbl: 0x%x", rc);
        return;
    }

    // Re-read: brightness may have been changed from the system settings.
    float before = g_brightness;
    if (R_SUCCEEDED(lblGetCurrentBrightnessSetting(&before))) g_brightness = before;

    float target = before + BRIGHTNESS_STEP * (float)direction;
    if (target < 0.0f) target = 0.0f;
    if (target > 1.0f) target = 1.0f;

    rc = lblSetCurrentBrightnessSetting(target);
    if (R_SUCCEEDED(rc)) rc = lblApplyCurrentBrightnessSettingToBacklight();
    if (R_SUCCEEDED(rc)) rc = lblSaveCurrentSetting();

    float after = target;
    if (R_SUCCEEDED(rc)) lblGetCurrentBrightnessSetting(&after);
    lblExit();

    if (R_FAILED(rc)) {
        logLine("Error setting brightness: 0x%x", rc);
        return;
    }
    g_brightness = after;

    soundPlayTick(after);
    osdShow(after, before);
    logLine("Brightness %s to: %.1f%%", direction > 0 ? "increased" : "decreased", after * 100.0f);
}

// Seconds to wait after process start before touching HID/AudioOut, so the
// rest of the system finishes booting first.
#define BOOT_SETTLE_SECONDS 10

// Runs one service init step, logging the system tick before and after so the
// log can be lined up with OccurrenceTick in atmosphere/erpt_reports.
static Result initStep(const char *name, Result (*fn)(void))
{
    logLine("[tick %lu] %s ...", armGetSystemTick(), name);
    Result rc = fn();
    logLine("[tick %lu] %s -> 0x%x", armGetSystemTick(), name, rc);
    return rc;
}

static Result initHid(void) { return hidInitialize(); }
static Result initSound(void) { return soundInitialize(); }

static Result readInitialBrightness(void) { return readBrightness(&g_brightness); }

static PadState g_pad;

static Result configurePad(void)
{
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);
    return 0;
}

// Returns false when a service the shortcut cannot work without is missing.
static bool initServices(void)
{
    Result rc = smInitialize();
    if (R_FAILED(rc)) {
        logLine("smInitialize failed: 0x%x", rc);
        return false;
    }

    bool ok = R_SUCCEEDED(initStep("hidInitialize", initHid)) &&
              R_SUCCEEDED(initStep("padConfigureInput", configurePad));
    // Sound is optional: without it the shortcut and the OSD still work.
    if (ok && R_FAILED(initStep("soundInitialize", initSound)))
        logLine("Continuing without sound feedback");

    smExit();
    return ok;
}

// Main program entrypoint
int main(int argc, char* argv[])
{
    logReset();
    logLine("Brightness Shortcut Sysmodule started successfully!");
    logLine("Title ID: 0x0004000001EB3400");
    logLine("[tick %lu] main (tick freq %lu Hz)", armGetSystemTick(), armGetSystemTickFreq());
    logLine("Controls: L+R+Up = Increase, L+R+Down = Decrease (hold for continuous)");
    logLine("Features: sound feedback, on-screen brightness bar");

    // The OSD layer has to exist before the HOME menu comes up to be visible.
    logLine("[tick %lu] osdInitialize ...", armGetSystemTick());
    osdInitialize();
    logLine("[tick %lu] osdInitialize done", armGetSystemTick());

    logLine("Waiting %d s for the system to finish booting", BOOT_SETTLE_SECONDS);
    svcSleepThread(BOOT_SETTLE_SECONDS * 1000000000ULL);

    if (!initServices()) {
        logLine("Required service unavailable, exiting");
        osdExit();
        return 0;
    }

    Result rc = initStep("lblGetCurrentBrightnessSetting", readInitialBrightness);
    if (R_FAILED(rc)) g_brightness = 0.5f;
    logLine("Initial brightness: %.1f%%", g_brightness * 100.0f);

    const u64 ticksPerSecond = armGetSystemTickFreq();
    const u64 initialDelay = ticksPerSecond / 2;  // 500ms before auto-repeat
    const u64 repeatDelay = ticksPerSecond / 10;  // then every 100ms
    const u64 startTick = armGetSystemTick();
    u64 lastLogTick = startTick;
    u64 pressTick = 0;
    u64 lastStepTick = 0;
    int lastDirection = 0;

    while (true) {
        padUpdate(&g_pad);
        u64 kHeld = padGetButtons(&g_pad);
        u64 now = armGetSystemTick();

        int direction = 0;
        if ((kHeld & HidNpadButton_L) && (kHeld & HidNpadButton_R)) {
            bool up = (kHeld & HidNpadButton_Up) != 0;
            bool down = (kHeld & HidNpadButton_Down) != 0;
            if (up != down) direction = up ? 1 : -1;
        }

        if (direction != 0) {
            if (direction != lastDirection) {
                // First press: immediate step
                stepBrightness(direction);
                pressTick = now;
                lastStepTick = now;
            } else if (now - pressTick > initialDelay && now - lastStepTick > repeatDelay) {
                stepBrightness(direction);
                lastStepTick = now;
            }
        }
        lastDirection = direction;

        osdUpdate();

        // Log status every 5 minutes to show the service is running
        if ((now - lastLogTick) / ticksPerSecond >= 300) {
            logLine("Service running... %lu seconds elapsed, brightness: %.1f%%",
                    (now - startTick) / ticksPerSecond, g_brightness * 100.0f);
            lastLogTick = now;
        }

        // ~60 Hz while the OSD animates, 20 Hz otherwise to keep CPU usage low.
        svcSleepThread(osdIsActive() ? 16000000ULL : 50000000ULL);
    }

    osdExit();
    return 0;
}
