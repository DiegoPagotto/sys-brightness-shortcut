// Software renderer for the brightness OSD.
// Pure C (no libnx) so it can be exercised on the host. Every shape is a signed
// distance field, which gives anti-aliased edges without supersampling.

#include "osd_render.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#define PI_F 3.14159265358979f

// ---- Layout (framebuffer pixels) --------------------------------------------
#define PANEL_X0      10.0f
#define PANEL_Y0      10.0f
#define PANEL_X1      310.0f
#define PANEL_Y1      62.0f
#define PANEL_RADIUS  14.0f
#define PANEL_CY      ((PANEL_Y0 + PANEL_Y1) * 0.5f)

#define SHADOW_SPREAD   9.0f
#define SHADOW_OFFSET_Y 2.0f
#define SHADOW_ALPHA    0.35f

#define ICON_CX       40.0f
#define ICON_CORE_R   5.0f
#define ICON_RAY_IN   8.5f
#define ICON_RAY_HALF 1.15f

#define BAR_X0        66.0f
#define BAR_X1        230.0f
#define BAR_RADIUS    3.0f

#define TEXT_RIGHT    296.0f
#define TEXT_HEIGHT   15.6f        // digit cap height
#define TEXT_UNIT     (TEXT_HEIGHT / 6.0f)
#define TEXT_ADVANCE  (5.2f * TEXT_UNIT)
#define TEXT_HALF_W   1.15f        // stroke half width

// ---- Colors (straight alpha, premultiplied while compositing) ---------------
typedef struct { float r, g, b, a; } Rgba;

static const Rgba COLOR_PANEL  = { 22.0f / 255.0f, 24.0f / 255.0f, 28.0f / 255.0f, 0.90f };
static const Rgba COLOR_SHADOW = { 0.0f, 0.0f, 0.0f, 0.35f };
static const Rgba COLOR_TRACK  = { 1.0f, 1.0f, 1.0f, 0.18f };
static const Rgba COLOR_FILL   = { 26.0f / 255.0f, 159.0f / 255.0f, 1.0f, 1.0f };
static const Rgba COLOR_ICON   = { 0.94f, 0.94f, 0.96f, 1.0f };
static const Rgba COLOR_TEXT   = { 0.94f, 0.94f, 0.96f, 0.95f };

// ---- Math helpers -----------------------------------------------------------
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Pixel coverage of a shape whose signed distance (negative inside) is d.
static inline float coverage(float d) { return clampf(0.5f - d, 0.0f, 1.0f); }

static float sdRoundRect(float px, float py, float x0, float y0, float x1, float y1, float r)
{
    float qx = fabsf(px - (x0 + x1) * 0.5f) - (x1 - x0) * 0.5f + r;
    float qy = fabsf(py - (y0 + y1) * 0.5f) - (y1 - y0) * 0.5f + r;
    float ox = qx > 0.0f ? qx : 0.0f;
    float oy = qy > 0.0f ? qy : 0.0f;
    float inside = qx > qy ? qx : qy;
    return sqrtf(ox * ox + oy * oy) + (inside < 0.0f ? inside : 0.0f) - r;
}

static inline float segDist2(float px, float py, float ax, float ay, float bx, float by)
{
    float pax = px - ax, pay = py - ay;
    float bax = bx - ax, bay = by - ay;
    float len2 = bax * bax + bay * bay;
    float h = len2 > 0.0f ? clampf((pax * bax + pay * bay) / len2, 0.0f, 1.0f) : 0.0f;
    float dx = pax - bax * h, dy = pay - bay * h;
    return dx * dx + dy * dy;
}

// Premultiplied "over": dst = src * a + dst * (1 - a)
static inline void blend(Rgba *dst, const Rgba *src, float cov)
{
    float a = src->a * cov;
    if (a <= 0.0f) return;
    float k = 1.0f - a;
    dst->r = src->r * a + dst->r * k;
    dst->g = src->g * a + dst->g * k;
    dst->b = src->b * a + dst->b * k;
    dst->a = a + dst->a * k;
}

// ---- Percentage text: stroked vector glyphs ---------------------------------
typedef struct { float ax, ay, bx, by; } Seg;
typedef struct { int start, end; float x0, y0, x1, y1; } GlyphRange;

#define MAX_SEGS   256
#define MAX_GLYPHS 4

static Seg        g_segs[MAX_SEGS];
static int        g_segCount;
static GlyphRange g_glyphs[MAX_GLYPHS];
static int        g_glyphCount;
static int        g_cachedPercent = -1;

// Pen maps glyph space (x 0..4, y 0..6, y down) onto framebuffer pixels.
typedef struct { float ox, oy; bool has_last; float lx, ly; } Pen;

static void penMove(Pen *p, float x, float y)
{
    p->lx = p->ox + x * TEXT_UNIT;
    p->ly = p->oy + y * TEXT_UNIT;
    p->has_last = true;
}

static void penTo(Pen *p, float x, float y)
{
    float nx = p->ox + x * TEXT_UNIT, ny = p->oy + y * TEXT_UNIT;
    if (p->has_last && g_segCount < MAX_SEGS)
        g_segs[g_segCount++] = (Seg){ p->lx, p->ly, nx, ny };
    p->lx = nx;
    p->ly = ny;
    p->has_last = true;
}

static void penLine(Pen *p, float x0, float y0, float x1, float y1)
{
    penMove(p, x0, y0);
    penTo(p, x1, y1);
}

// Elliptical arc; angles in degrees, 0 = right, 90 = down.
// `connect` continues from the current pen position instead of starting a new stroke.
static void penArc(Pen *p, float cx, float cy, float rx, float ry, float a0, float a1, int steps, bool connect)
{
    for (int i = 0; i <= steps; i++) {
        float a = (a0 + (a1 - a0) * (float)i / (float)steps) * PI_F / 180.0f;
        float x = cx + rx * cosf(a), y = cy + ry * sinf(a);
        if (i == 0 && !connect) penMove(p, x, y);
        else penTo(p, x, y);
    }
}

static void penQuad(Pen *p, float x0, float y0, float cx, float cy, float x1, float y1, int steps)
{
    penMove(p, x0, y0);
    for (int i = 1; i <= steps; i++) {
        float t = (float)i / (float)steps, u = 1.0f - t;
        penTo(p, u * u * x0 + 2.0f * u * t * cx + t * t * x1, u * u * y0 + 2.0f * u * t * cy + t * t * y1);
    }
}

static void glyph(Pen *p, char c)
{
    switch (c) {
    case '0':
        penArc(p, 2.0f, 3.0f, 1.85f, 2.9f, 0.0f, 360.0f, 28, false);
        break;
    case '1':
        penLine(p, 0.9f, 1.3f, 2.3f, 0.05f);
        penTo(p, 2.3f, 6.0f);
        break;
    case '2':
        penArc(p, 2.0f, 1.9f, 1.8f, 1.8f, 195.0f, 380.0f, 14, false);
        penTo(p, 0.2f, 5.95f);
        penTo(p, 3.9f, 5.95f);
        break;
    case '3':
        penArc(p, 2.0f, 1.5f, 1.7f, 1.45f, 210.0f, 450.0f, 14, false);
        penArc(p, 2.0f, 4.45f, 1.85f, 1.5f, 270.0f, 510.0f, 14, true);
        break;
    case '4':
        penLine(p, 2.9f, 6.0f, 2.9f, 0.05f);
        penTo(p, 0.1f, 4.3f);
        penTo(p, 3.9f, 4.3f);
        break;
    case '5':
        penLine(p, 3.6f, 0.05f, 0.7f, 0.05f);
        penTo(p, 0.45f, 2.75f);
        penArc(p, 1.95f, 4.1f, 1.9f, 1.85f, 225.0f, 495.0f, 16, true);
        break;
    case '6':
        penArc(p, 2.0f, 4.1f, 1.85f, 1.85f, 0.0f, 360.0f, 22, false);
        penQuad(p, 3.3f, 0.15f, 0.15f, 0.9f, 0.15f, 4.1f, 10);
        break;
    case '7':
        penLine(p, 0.1f, 0.05f, 3.9f, 0.05f);
        penTo(p, 1.4f, 6.0f);
        break;
    case '8':
        penArc(p, 2.0f, 1.5f, 1.55f, 1.45f, 0.0f, 360.0f, 18, false);
        penArc(p, 2.0f, 4.45f, 1.85f, 1.5f, 0.0f, 360.0f, 20, false);
        break;
    case '9':
        penArc(p, 2.0f, 1.9f, 1.85f, 1.85f, 0.0f, 360.0f, 22, false);
        penQuad(p, 3.85f, 1.9f, 3.85f, 5.1f, 0.7f, 5.85f, 10);
        break;
    case '%':
        penArc(p, 0.95f, 1.1f, 0.85f, 0.95f, 0.0f, 360.0f, 14, false);
        penArc(p, 3.05f, 4.9f, 0.85f, 0.95f, 0.0f, 360.0f, 14, false);
        penLine(p, 3.5f, 0.1f, 0.5f, 5.9f);
        break;
    default:
        break;
    }
}

static void buildText(int percent)
{
    if (percent == g_cachedPercent) return;
    g_cachedPercent = percent;
    g_segCount = 0;
    g_glyphCount = 0;

    char text[MAX_GLYPHS];
    int n = 0;
    if (percent >= 100) text[n++] = (char)('0' + (percent / 100) % 10);
    if (percent >= 10)  text[n++] = (char)('0' + (percent / 10) % 10);
    text[n++] = (char)('0' + percent % 10);
    text[n++] = '%';

    // Right-aligned so the number does not jump around while the value changes.
    float x = TEXT_RIGHT - ((float)(n - 1) * TEXT_ADVANCE + 4.0f * TEXT_UNIT);
    float top = PANEL_CY - TEXT_HEIGHT * 0.5f;
    float pad = TEXT_HALF_W + 1.0f;

    for (int i = 0; i < n; i++) {
        Pen pen = { .ox = x, .oy = top, .has_last = false };
        GlyphRange *g = &g_glyphs[g_glyphCount++];
        g->start = g_segCount;
        glyph(&pen, text[i]);
        g->end = g_segCount;
        g->x0 = x - pad;
        g->x1 = x + 4.0f * TEXT_UNIT + pad;
        g->y0 = top - pad;
        g->y1 = top + TEXT_HEIGHT + pad;
        x += TEXT_ADVANCE;
    }
}

static float textCoverage(float px, float py)
{
    float best = INFINITY;
    for (int gi = 0; gi < g_glyphCount; gi++) {
        const GlyphRange *g = &g_glyphs[gi];
        if (px < g->x0 || px > g->x1 || py < g->y0 || py > g->y1) continue;
        for (int i = g->start; i < g->end; i++) {
            const Seg *s = &g_segs[i];
            float d2 = segDist2(px, py, s->ax, s->ay, s->bx, s->by);
            if (d2 < best) best = d2;
        }
    }
    if (best == INFINITY) return 0.0f;
    return coverage(sqrtf(best) - TEXT_HALF_W);
}

// ---- Sun icon ---------------------------------------------------------------
static inline float iconRayOuter(float level) { return ICON_RAY_IN + 2.0f + 3.0f * level; }

static float iconCoverage(float px, float py, float level)
{
    float dx = px - ICON_CX, dy = py - PANEL_CY;
    float d = sqrtf(dx * dx + dy * dy) - ICON_CORE_R;

    // Rays grow with brightness, like a sun getting stronger.
    float rayOut = iconRayOuter(level);
    for (int i = 0; i < 8; i++) {
        float a = (float)i * PI_F / 4.0f;
        float c = cosf(a), s = sinf(a);
        float rd = sqrtf(segDist2(dx, dy, c * ICON_RAY_IN, s * ICON_RAY_IN, c * rayOut, s * rayOut)) - ICON_RAY_HALF;
        if (rd < d) d = rd;
    }
    return coverage(d);
}

// ---- Frame ------------------------------------------------------------------
static inline uint32_t packPremultiplied(const Rgba *c, float opacity)
{
    uint32_t r = (uint32_t)(clampf(c->r * opacity, 0.0f, 1.0f) * 255.0f + 0.5f);
    uint32_t g = (uint32_t)(clampf(c->g * opacity, 0.0f, 1.0f) * 255.0f + 0.5f);
    uint32_t b = (uint32_t)(clampf(c->b * opacity, 0.0f, 1.0f) * 255.0f + 0.5f);
    uint32_t a = (uint32_t)(clampf(c->a * opacity, 0.0f, 1.0f) * 255.0f + 0.5f);
    return r | (g << 8) | (b << 16) | (a << 24);
}

void osdRenderFrame(uint32_t *pixels, uint32_t stride_px, float level, int percent, float opacity, float offset_y)
{
    level = clampf(level, 0.0f, 1.0f);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    if (opacity <= 0.0f) {
        for (uint32_t y = 0; y < OSD_FB_HEIGHT; y++)
            memset(pixels + y * stride_px, 0, OSD_FB_WIDTH * sizeof(uint32_t));
        return;
    }

    buildText(percent);

    const float fillX = BAR_X0 + (BAR_X1 - BAR_X0) * level;
    const float iconReach = iconRayOuter(level) + ICON_RAY_HALF + 1.0f;
    const float textLeft = g_glyphCount > 0 ? g_glyphs[0].x0 : TEXT_RIGHT;

    for (uint32_t y = 0; y < OSD_FB_HEIGHT; y++) {
        uint32_t *row = pixels + y * stride_px;
        // Sample in panel space: shifting the sample point slides the whole panel.
        float py = (float)y + 0.5f - offset_y;

        if (py < PANEL_Y0 - SHADOW_SPREAD || py > PANEL_Y1 + SHADOW_OFFSET_Y + SHADOW_SPREAD) {
            memset(row, 0, OSD_FB_WIDTH * sizeof(uint32_t));
            continue;
        }

        const bool barRow  = fabsf(py - PANEL_CY) < BAR_RADIUS + 1.0f;
        const bool iconRow = fabsf(py - PANEL_CY) < iconReach;

        for (uint32_t x = 0; x < OSD_FB_WIDTH; x++) {
            float px = (float)x + 0.5f;
            Rgba c = { 0.0f, 0.0f, 0.0f, 0.0f };

            // Soft drop shadow keeps the panel readable over bright scenes.
            float ds = sdRoundRect(px, py - SHADOW_OFFSET_Y, PANEL_X0, PANEL_Y0, PANEL_X1, PANEL_Y1, PANEL_RADIUS);
            if (ds < SHADOW_SPREAD) {
                float t = clampf(1.0f - ds / SHADOW_SPREAD, 0.0f, 1.0f);
                blend(&c, &COLOR_SHADOW, t * t);
            }

            float panel = coverage(sdRoundRect(px, py, PANEL_X0, PANEL_Y0, PANEL_X1, PANEL_Y1, PANEL_RADIUS));
            if (panel > 0.0f) {
                blend(&c, &COLOR_PANEL, panel);

                if (barRow && px > BAR_X0 - BAR_RADIUS - 1.0f && px < BAR_X1 + BAR_RADIUS + 1.0f) {
                    float dt = sqrtf(segDist2(px, py, BAR_X0, PANEL_CY, BAR_X1, PANEL_CY)) - BAR_RADIUS;
                    blend(&c, &COLOR_TRACK, coverage(dt));
                    if (level > 0.001f) {
                        float df = sqrtf(segDist2(px, py, BAR_X0, PANEL_CY, fillX, PANEL_CY)) - BAR_RADIUS;
                        blend(&c, &COLOR_FILL, coverage(df));
                    }
                }

                if (iconRow && fabsf(px - ICON_CX) < iconReach)
                    blend(&c, &COLOR_ICON, iconCoverage(px, py, level));

                if (px >= textLeft)
                    blend(&c, &COLOR_TEXT, textCoverage(px, py));
            }

            row[x] = packPremultiplied(&c, opacity);
        }
    }
}
