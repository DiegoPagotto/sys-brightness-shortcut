// On-screen brightness indicator drawn on a managed VI layer above everything
// else (same technique Tesla overlays use, but without taking input focus).

#include "osd.h"

#include <math.h>

#include "log.h"
#include "osd_render.h"

// Layer geometry in VI's 1920x1080 coordinate space (scaled 1.5x from the framebuffer).
#define LAYER_WIDTH  (OSD_FB_WIDTH * 3 / 2)
#define LAYER_HEIGHT (OSD_FB_HEIGHT * 3 / 2)
#define LAYER_X      24   // top-left corner
#define LAYER_Y      24

// Timings in seconds.
#define HOLD_TIME        1.5f   // fully visible after the last change
#define FADE_IN_TIME     0.12f
#define FADE_OUT_TIME    0.30f
#define FILL_TIME_CONST  0.06f  // exponential smoothing of the bar fill
#define SLIDE_DISTANCE   6.0f   // pixels the panel slides while fading
#define INIT_RETRY_DELAY 10.0f

// Not declared in libnx headers; makes viCreateLayer open our managed layer.
extern u64 __nx_vi_layer_id;

typedef enum { OSD_UNINIT, OSD_READY, OSD_FAILED } OsdState;

static OsdState    g_state = OSD_UNINIT;
static u64         g_retryTick;
static ViDisplay   g_display;
static ViLayer     g_layer;
static NWindow     g_window;
static Framebuffer g_fb;

static float g_target;          // level shown by the number and the fill target
static float g_fill;            // animated fill level
static float g_alpha;           // linear fade progress 0..1
static bool  g_cleared = true;  // last presented frame was fully transparent
static u64   g_lastChangeTick;
static u64   g_lastFrameTick;

// Last presented frame, to skip redundant presents during the hold period.
static float g_drawnFill = -1.0f, g_drawnOpacity = -1.0f, g_drawnOffset;
static int   g_drawnPercent = -1;

static float ticksToSeconds(u64 ticks) { return (float)armTicksToNs(ticks) / 1e9f; }

// IManagerDisplayService::AddToLayerStack, not wrapped by libnx.
static Result addToLayerStack(ViLayerStack stack)
{
    const struct {
        u32 stack;
        u32 pad;
        u64 layer_id;
    } in = { stack, 0, g_layer.layer_id };
    return serviceDispatchIn(viGetSession_IManagerDisplayService(), 6000, in);
}

// IManagerDisplayService::DestroyManagedLayer, for a layer viCreateLayer never opened.
static void destroyManagedLayerId(u64 layer_id)
{
    serviceDispatchIn(viGetSession_IManagerDisplayService(), 2011, layer_id);
}

static bool osdInit(void)
{
    Result rc;
    const char *step;
    bool viUp = false, displayOpen = false, managedCreated = false, layerOpen = false, windowUp = false, fbUp = false;

    // __appInit closed the SM session; services below are looked up through it.
    step = "smInitialize";
    if (R_FAILED(rc = smInitialize())) goto fail_no_sm;

    step = "viInitialize";
    if (R_FAILED(rc = viInitialize(ViServiceType_Manager))) goto fail;
    viUp = true;

    step = "viOpenDefaultDisplay";
    if (R_FAILED(rc = viOpenDefaultDisplay(&g_display))) goto fail;
    displayOpen = true;

    step = "viCreateManagedLayer";
    if (R_FAILED(rc = viCreateManagedLayer(&g_display, (ViLayerFlags)0, 0, &__nx_vi_layer_id))) goto fail;
    managedCreated = true;

    step = "viCreateLayer";
    if (R_FAILED(rc = viCreateLayer(&g_display, &g_layer))) goto fail;
    layerOpen = true;

    step = "viSetLayerScalingMode";
    if (R_FAILED(rc = viSetLayerScalingMode(&g_layer, ViScalingMode_FitToLayer))) goto fail;

    s32 z = 0;
    Result zrc = viGetZOrderCountMax(&g_display, &z);
    Result setzrc = R_SUCCEEDED(zrc) && z > 0 ? viSetLayerZ(&g_layer, z) : viSetLayerZ(&g_layer, 255);

    // Every layer stack, exactly as libtesla does, so the bar also shows up
    // in screenshots and recordings.
    step = "AddToLayerStack(Default)";
    if (R_FAILED(rc = addToLayerStack(ViLayerStack_Default))) goto fail;
    Result lcdrc = addToLayerStack(ViLayerStack_Lcd);
    Result ssrc = addToLayerStack(ViLayerStack_Screenshot);
    Result recrc = addToLayerStack(ViLayerStack_Recording);
    Result lfrc = addToLayerStack(ViLayerStack_LastFrame);
    Result arbrc = addToLayerStack(ViLayerStack_Arbitrary);
    Result nullrc = addToLayerStack(ViLayerStack_Null);
    Result dbgrc = addToLayerStack(ViLayerStack_ApplicationForDebug);

    s32 dw = 0, dh = 0, lw = 0, lh = 0;
    viGetDisplayResolution(&g_display, &dw, &dh);
    viGetDisplayLogicalResolution(&g_display, &lw, &lh);
    logLine("OSD: zmax 0x%x (%d) setz 0x%x | stacks lcd 0x%x ss 0x%x rec 0x%x lf 0x%x arb 0x%x null 0x%x dbg 0x%x | display %dx%d logical %dx%d",
            zrc, z, setzrc, lcdrc, ssrc, recrc, lfrc, arbrc, nullrc, dbgrc, dw, dh, lw, lh);

    step = "viSetLayerSize";
    if (R_FAILED(rc = viSetLayerSize(&g_layer, LAYER_WIDTH, LAYER_HEIGHT))) goto fail;

    step = "viSetLayerPosition";
    if (R_FAILED(rc = viSetLayerPosition(&g_layer, (float)LAYER_X, (float)LAYER_Y))) goto fail;

    step = "nwindowCreateFromLayer";
    if (R_FAILED(rc = nwindowCreateFromLayer(&g_window, &g_layer))) goto fail;
    windowUp = true;

    step = "framebufferCreate";
    if (R_FAILED(rc = framebufferCreate(&g_fb, &g_window, OSD_FB_WIDTH, OSD_FB_HEIGHT, PIXEL_FORMAT_RGBA_8888, 2))) goto fail;
    fbUp = true;

    step = "framebufferMakeLinear";
    if (R_FAILED(rc = framebufferMakeLinear(&g_fb))) goto fail;

    smExit();
    logLine("OSD layer ready (layer id %lu, z %d)", __nx_vi_layer_id, z);
    return true;

fail:
    if (fbUp) framebufferClose(&g_fb);
    if (windowUp) nwindowClose(&g_window);
    if (layerOpen) viDestroyManagedLayer(&g_layer);
    else if (managedCreated) destroyManagedLayerId(__nx_vi_layer_id);
    __nx_vi_layer_id = 0;
    if (displayOpen) viCloseDisplay(&g_display);
    if (viUp) viExit();
    smExit();
fail_no_sm:
    logLine("OSD init failed at %s: 0x%x", step, rc);
    return false;
}

static float smoothstep01(float t) { return t * t * (3.0f - 2.0f * t); }

static u32 g_framesPresented;

static void present(float fill, int percent, float opacity, float offset)
{
    u32 stride = 0;
    u32 *pixels = (u32 *)framebufferBegin(&g_fb, &stride);
    if (!pixels) {
        logLine("OSD: framebufferBegin returned NULL");
        return;
    }
    osdRenderFrame(pixels, stride / sizeof(u32), fill, percent, opacity, offset);
    framebufferEnd(&g_fb);
    if (g_framesPresented++ == 0)
        logLine("OSD: first frame presented (stride %u, first px 0x%08x, opacity %.2f)", stride, pixels[40 * (stride / 4) + 40], opacity);

    g_drawnFill = fill;
    g_drawnPercent = percent;
    g_drawnOpacity = opacity;
    g_drawnOffset = offset;
    g_cleared = opacity <= 0.0f;
}

static void tryInit(u64 now)
{
    if (g_state == OSD_FAILED && ticksToSeconds(now - g_retryTick) >= INIT_RETRY_DELAY)
        g_state = OSD_UNINIT;
    if (g_state != OSD_UNINIT) return;
    if (osdInit()) {
        g_state = OSD_READY;
        // Start with a transparent frame so nothing stale is ever shown.
        present(0.0f, 0, 0.0f, 0.0f);
        g_framesPresented = 0;
    } else {
        g_state = OSD_FAILED;
        g_retryTick = now;
    }
}

void osdInitialize(void)
{
    tryInit(armGetSystemTick());
}

void osdShow(float level, float previous)
{
    u64 now = armGetSystemTick();

    tryInit(now);
    if (g_state != OSD_READY) return;

    if (g_alpha <= 0.0f) {
        g_fill = previous;
        g_lastFrameTick = now;
    }
    g_target = level;
    g_lastChangeTick = now;
}

bool osdIsActive(void)
{
    return g_state == OSD_READY && (g_alpha > 0.0f || !g_cleared ||
           ticksToSeconds(armGetSystemTick() - g_lastChangeTick) < HOLD_TIME);
}

void osdUpdate(void)
{
    if (!osdIsActive()) return;

    u64 now = armGetSystemTick();
    float dt = ticksToSeconds(now - g_lastFrameTick);
    if (dt > 0.1f) dt = 0.1f;
    g_lastFrameTick = now;

    bool visible = ticksToSeconds(now - g_lastChangeTick) < HOLD_TIME;
    g_alpha += visible ? dt / FADE_IN_TIME : -dt / FADE_OUT_TIME;
    if (g_alpha < 0.0f) g_alpha = 0.0f;
    if (g_alpha > 1.0f) g_alpha = 1.0f;

    g_fill += (g_target - g_fill) * (1.0f - expf(-dt / FILL_TIME_CONST));
    if (fabsf(g_target - g_fill) < 0.001f) g_fill = g_target;

    float opacity = smoothstep01(g_alpha);
    float offset = -(1.0f - opacity) * SLIDE_DISTANCE;
    int percent = (int)lroundf(g_target * 100.0f);

    if (opacity <= 0.0f) {
        // Keep the layer (one created after the HOME menu is up is never
        // shown), just leave a transparent frame on it.
        if (!g_cleared) {
            present(0.0f, 0, 0.0f, 0.0f);
            logLine("OSD hidden after %u frames", g_framesPresented);
            g_framesPresented = 0;
        }
        return;
    }

    // Nothing moved since the last present: the compositor keeps showing it.
    if (g_fill == g_drawnFill && percent == g_drawnPercent && opacity == g_drawnOpacity && offset == g_drawnOffset)
        return;

    present(g_fill, percent, opacity, offset);
}

void osdExit(void)
{
    if (g_state != OSD_READY) return;
    framebufferClose(&g_fb);
    nwindowClose(&g_window);
    viDestroyManagedLayer(&g_layer);
    __nx_vi_layer_id = 0;
    viCloseDisplay(&g_display);
    viExit();
    g_state = OSD_UNINIT;
    g_alpha = 0.0f;
    g_cleared = true;
    g_drawnFill = -1.0f;
    g_drawnOpacity = -1.0f;
    g_drawnPercent = -1;
}
