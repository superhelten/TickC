// chart_golden.c - golden tests for the chart engine (phase 35).
//
// Draws chart.c into a memory DC with no window, no app, no network and no
// clock: the candles are synthetic and seeded, the local time offset is a
// constant, and the fonts, pens and brushes come from ChartStyleCreate, the
// same function TickC uses. Each case is hashed (FNV-1a 64 over the RGB of
// every pixel) and compared with tests/golden/chart.txt.
//
// Build from the repository root, in a Visual Studio developer prompt (x86,
// like TickC):
//   cl /nologo /W4 /O2 /I. /Fo:tests\ /Fe:tests\chart_golden.exe tests\chart_golden.c chart.c user32.lib gdi32.lib
// Run:
//   tests\chart_golden.exe            compare with the goldens, exit 1 on any difference
//   tests\chart_golden.exe --update   rewrite the goldens (look at tests\out\*.bmp first)
//   tests\chart_golden.exe --bmp      also write every case to tests\out\<name>.bmp
//
// The hashes hold for one machine: text goes through the installed fonts and
// the ClearType setting (fontSmall is CLEARTYPE_QUALITY). If only text differs
// on another machine, compare the BMPs and rerun --update there.
//
// Every case is also drawn a second time into a bitmap from
// CreateCompatibleBitmap on the screen DC - the kind TickC's back buffer is -
// and the two must be pixel-identical. That ties the DIB section here to what
// the app shows.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "chart.h"

#define MAX_CANDLES 3000
#define MIN_MS      60000LL
#define HOUR_MS     3600000LL
#define DAY_MS      86400000LL
#define WEEK_MS     (7 * DAY_MS)
// The last candle closes at 2026-09-21 14:00 UTC. The 1m buffer reaches back
// past the previous day's start, so today's and yesterday's levels are drawn.
#define T_END_MS    1789999200000LL
// CEST, fixed: the goldens must not move when the machine leaves DST.
#define UTC_OFFSET_MS (2 * HOUR_MS)

typedef struct {
    const char* name;
    int  W, H;
    BOOL desktop;
    long long ivMs;
    int  n;           // candles in the buffer
    BOOL histDone;
    int  view;        // visible candles, 0 = all
    int  back;        // candles the view is panned back from the live edge
    double volF, indF;
    int  hoverX, hoverY;   // -1 = no crosshair
    BOOL alerts;      // two alerts, a ghost tag on the axis and an afterglow
    int  dpi;         // 96 = 100 %, 144 = 150 %, 192 = 200 % (phase 36)
    BOOL light;       // ChartThemeLight instead of the default dark (phase 38)
    BOOL rsi;         // the RSI band on (phase 39)
} Case;

// The desktop stamp is sized from H (DeskPillFontH); 1920x1080 and a 3840x1600
// surface give two different stamp fonts.
static const Case CASES[] = {
    { "panel_1m_1280x720",      1280, 720, FALSE, MIN_MS,      2400, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE },
    { "panel_15m_1280x720",     1280, 720, FALSE, 15 * MIN_MS,  360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE },
    { "panel_1h_1280x720",      1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE },
    { "panel_1d_1280x720",      1280, 720, FALSE, DAY_MS,       200, TRUE,    0,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE },
    { "panel_15m_560x300",       560, 300, FALSE, 15 * MIN_MS,  360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE },
    { "panel_1h_400x250",        400, 250, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE },
    { "panel_1h_hover",         1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0, 700, 300, FALSE,  96, FALSE, FALSE },
    { "panel_1h_panned_zoomed", 1280, 720, FALSE, HOUR_MS,      360, FALSE,  60, 90, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE },
    { "panel_1h_overlays_off",  1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 0.0, 0.0,  -1,  -1, FALSE,  96, FALSE, FALSE },
    { "panel_1h_overlays_fade", 1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 0.5, 0.5,  -1,  -1, FALSE,  96, FALSE, FALSE },
    { "panel_15m_alerts",       1280, 720, FALSE, 15 * MIN_MS,  360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, TRUE,   96, FALSE, FALSE },
    { "panel_1m_few_candles",    560, 300, FALSE, MIN_MS,        12, TRUE,    0,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE },
    { "desktop_1m_1920x1080",   1920,1080, TRUE,  MIN_MS,      2400, FALSE, 300,  0, 0.0, 0.0,  -1,  -1, FALSE,  96, FALSE, FALSE },
    { "desktop_1m_3840x1600",   3840,1600, TRUE,  MIN_MS,      2400, FALSE, 300,  0, 0.0, 0.0,  -1,  -1, FALSE,  96, FALSE, FALSE },
    // Phase 36: panels at 150 % and 200 %, in device pixels - what a
    // per-monitor-aware panel gets for 1280x720 and 560x300 logical.
    { "dpi144_1h_1920x1080",    1920,1080, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE, 144, FALSE, FALSE },
    { "dpi144_1h_hover",        1920,1080, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0,1050, 450, FALSE, 144, FALSE, FALSE },
    { "dpi144_15m_alerts",      1920,1080, FALSE, 15 * MIN_MS,  360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, TRUE,  144, FALSE, FALSE },
    { "dpi144_15m_840x450",      840, 450, FALSE, 15 * MIN_MS,  360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE, 144, FALSE, FALSE },
    { "dpi192_1m_2560x1440",    2560,1440, FALSE, MIN_MS,      2400, FALSE, 300,  0, 1.0, 1.0,1400, 600, FALSE, 192, FALSE, FALSE },
    // Phase 38: the light theme, with every element that has a color of its
    // own in view - candles, bars, averages, VWAP, levels, alerts, crosshair.
    { "light_15m_alerts",       1280, 720, FALSE, 15 * MIN_MS,  360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, TRUE,   96, TRUE, FALSE },
    { "light_1h_hover",         1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0, 700, 300, FALSE,  96, TRUE, FALSE },
    { "light_desktop_1920x1080",1920,1080, TRUE,  MIN_MS,      2400, FALSE, 300,  0, 0.0, 0.0,  -1,  -1, FALSE,  96, TRUE, FALSE },
    // Phase 39: the RSI band. The crosshair in the price pane (RSI row in the
    // box) and in the band (RSI level tag), the smallest panel (the band just
    // fits: 142 px of price), 150 %, the light theme and the desktop.
    { "rsi_1h_hover",           1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0, 700, 300, FALSE,  96, FALSE, TRUE },
    { "rsi_1h_band_hover",      1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0, 700, 640, FALSE,  96, FALSE, TRUE },
    { "rsi_15m_400x250",         400, 250, FALSE, 15 * MIN_MS,  360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, TRUE },
    { "rsi_1m_dpi144",          1920,1080, FALSE, MIN_MS,      2400, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, TRUE,  144, FALSE, TRUE },
    { "rsi_light_1h",           1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, TRUE,  TRUE },
    { "rsi_desktop_1920x1080",  1920,1080, TRUE,  MIN_MS,      2400, FALSE, 300,  0, 0.0, 0.0,  -1,  -1, FALSE,  96, FALSE, TRUE },
    // Phase 41: the ranges 1Y (365 x 1d) and 5Y (261 x 1w), where the time
    // labels need the longer steps NiceTimeStep got (60 days, 26 weeks).
    { "range_1y_1d",            1280, 720, FALSE, DAY_MS,       400, TRUE,  365,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE },
    { "range_5y_1w",            1280, 720, FALSE, WEEK_MS,      300, TRUE,  261,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE },
    // Phase 43: the volume pane. The crosshair in the pane (the volume tag),
    // both panes on a 560x300 panel (the smallest size where both fit), the
    // light theme with both panes and the pointer in the volume pane, and the
    // desktop with the volume on. rsi_15m_400x250 is the fallback: no room
    // for two panes, so the bars stand behind the candles, as before.
    { "vol_1h_pane_hover",      1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0, 700, 650, FALSE,  96, FALSE, FALSE },
    { "vol_rsi_15m_560x300",     560, 300, FALSE, 15 * MIN_MS,  360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, TRUE },
    { "vol_light_rsi_hover",    1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0, 900, 530, FALSE,  96, TRUE,  TRUE },
    { "vol_desktop_1920x1080",  1920,1080, TRUE,  MIN_MS,      2400, FALSE, 300,  0, 1.0, 0.0,  -1,  -1, FALSE,  96, FALSE, FALSE },
};
#define NCASES ((int)(sizeof(CASES) / sizeof(CASES[0])))

static Candle s_candles[MAX_CANDLES];

// --- Synthetic candles: a seeded random walk, the same on every run ---
static unsigned int s_rng;
static double Rnd(void) {
    s_rng = s_rng * 1664525u + 1013904223u;
    return (double)(s_rng >> 8) / 16777216.0;
}

static void MakeCandles(Candle* c, int n, long long ivMs) {
    // Per-candle move grows with the interval, so every interval gets a
    // chart that fills the price axis the way real data does.
    double step = (ivMs >= DAY_MS) ? 0.03 : (ivMs >= HOUR_MS) ? 0.008
                : (ivMs >= 15 * MIN_MS) ? 0.004 : 0.0015;
    s_rng = 0x5EED0000u ^ (unsigned int)(ivMs / MIN_MS);
    double p = 63000.0;
    long long t0 = T_END_MS - (long long)n * ivMs;
    for (int i = 0; i < n; i++) {
        double o = p;
        double cl = o * (1.0 + (Rnd() - 0.5) * 2.0 * step);
        double hi = (o > cl ? o : cl) * (1.0 + Rnd() * step * 0.6);
        double lo = (o < cl ? o : cl) * (1.0 - Rnd() * step * 0.6);
        c[i].openTime = t0 + (long long)i * ivMs;
        c[i].open = o; c[i].close = cl; c[i].high = hi; c[i].low = lo;
        c[i].volume = (2.0 + Rnd() * 30.0) * (double)(ivMs / MIN_MS);
        p = cl;
    }
}

// --- One case into a DC ---
static void DrawCase(HDC hdc, const Case* k, const ChartStyle* base) {
    MakeCandles(s_candles, k->n, k->ivMs);

    ChartState st;
    ZeroMemory(&st, sizeof(st));
    st.viewCount  = k->view;
    st.viewStart  = k->n - k->back - k->view;
    st.followLive = (k->back == 0);
    // TickC clamps only a real view; 0 ("show all") would become MIN_VIEW.
    if (k->view > 0) ClampView(&st, k->n);
    SyncDisp(&st, s_candles, k->n);
    st.dispVolF = k->volF;
    st.dispIndF = k->indF;
    st.dispRsiF = k->rsi ? 1.0 : 0.0;
    st.hoverIdx = -1;
    if (k->hoverX >= 0) {
        // As TickC's probe field 104 does it: a mouse move at (x, y).
        ChartRect g = ChartGeometry(k->W, k->H, k->desktop, k->dpi, k->rsi, k->volF > 0.0);
        st.hoverIdx = HitCandle(&st, k->n, &g, k->hoverX, k->hoverY);
        st.hoverY   = k->hoverY;
    }

    double last = s_candles[k->n - 1].close;
    double alerts[2] = { floor(last * 1.003), -floor(last * 0.985) };

    ChartData in;
    ZeroMemory(&in, sizeof(in));
    in.candles = s_candles; in.count = k->n;
    in.intervalMs = k->ivMs;
    in.histDone = k->histDone;
    in.desktop = k->desktop;
    in.alertHot = -1;
    in.axisHotY = -1;
    in.utcOffsetMs = UTC_OFFSET_MS;
    in.band = k->rsi;
    // The volume's choice (phase 43): on whenever its display is, so a fade
    // case is a pane with bars half grown, as in the app.
    in.vol = (k->volF > 0.0);
    if (k->alerts) {
        ChartRect g = ChartGeometry(k->W, k->H, k->desktop, k->dpi, k->rsi, k->volF > 0.0);
        in.alerts = alerts; in.alertCount = 2;
        in.axisHotY = g.top + (g.bottom - g.top) / 3;   // ghost tag under the pointer
        in.alertFlashLevel = floor(last * 0.975);   // a fired one, gone from the list
        in.alertFlashF = 0.6;
    }

    ChartStyle sty = *base;
    sty.fontPill = k->desktop ? ChartPillFontCreate(k->H) : NULL;
    ChartDrawBackground(hdc, k->W, k->H, NULL, sty.brBg);
    ChartDrawBody(hdc, k->W, k->H, &st, &in, &sty);
    GdiFlush();
    if (sty.fontPill) DeleteObject(sty.fontPill);
}

// --- Pixels ---
// A 32 bpp top-down DIB section; the alpha byte is left to GDI and ignored.
typedef struct { HDC dc; HBITMAP bmp, old; DWORD* px; int W, H; } Surface;

static BOOL SurfaceOpen(Surface* s, int W, int H) {
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    s->W = W; s->H = H;
    s->dc = CreateCompatibleDC(NULL);
    s->bmp = CreateDIBSection(s->dc, &bi, DIB_RGB_COLORS, (void**)&s->px, NULL, 0);
    if (!s->dc || !s->bmp) return FALSE;
    s->old = (HBITMAP)SelectObject(s->dc, s->bmp);
    return TRUE;
}

static void SurfaceClose(Surface* s) {
    if (s->dc && s->old) SelectObject(s->dc, s->old);
    if (s->bmp) DeleteObject(s->bmp);
    if (s->dc) DeleteDC(s->dc);
    ZeroMemory(s, sizeof(*s));
}

static unsigned long long Fnv(const DWORD* px, int count) {
    unsigned long long h = 1469598103934665603ULL;
    for (int i = 0; i < count; i++) {
        DWORD v = px[i];
        for (int b = 0; b < 3; b++) {
            h ^= (v >> (8 * b)) & 0xFF;
            h *= 1099511628211ULL;
        }
    }
    return h;
}

// Draws the case into a CreateCompatibleBitmap bitmap on the screen DC and
// counts the pixels that differ from ref. -1 = no screen DC to test against.
static int CrossCheck(const Case* k, const ChartStyle* sty, const DWORD* ref) {
    HDC scr = GetDC(NULL);
    if (!scr) return -1;
    HDC dc = CreateCompatibleDC(scr);
    HBITMAP bmp = CreateCompatibleBitmap(scr, k->W, k->H);
    int diff = -1;
    if (dc && bmp) {
        HBITMAP old = (HBITMAP)SelectObject(dc, bmp);
        DrawCase(dc, k, sty);
        SelectObject(dc, old);
        BITMAPINFO bi;
        ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = k->W;
        bi.bmiHeader.biHeight = -k->H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        DWORD* got = (DWORD*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)k->W * k->H * 4);
        if (got && GetDIBits(scr, bmp, 0, (UINT)k->H, got, &bi, DIB_RGB_COLORS) == k->H) {
            diff = 0;
            for (int i = 0; i < k->W * k->H; i++)
                if ((got[i] ^ ref[i]) & 0x00FFFFFF) diff++;
        }
        if (got) HeapFree(GetProcessHeap(), 0, got);
    }
    if (bmp) DeleteObject(bmp);
    if (dc) DeleteDC(dc);
    ReleaseDC(NULL, scr);
    return diff;
}

static BOOL WriteBmp(const char* path, const Surface* s) {
    FILE* f;
    if (fopen_s(&f, path, "wb") != 0) return FALSE;
    DWORD sz = (DWORD)s->W * s->H * 4;
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    ZeroMemory(&fh, sizeof(fh));
    ZeroMemory(&ih, sizeof(ih));
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + sz;
    ih.biSize = sizeof(ih);
    ih.biWidth = s->W;
    ih.biHeight = -s->H;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    // Opaque alpha, so viewers that honor it do not show a blank image.
    for (int i = 0; i < s->W * s->H; i++) {
        DWORD v = s->px[i] | 0xFF000000u;
        fwrite(&v, 4, 1, f);
    }
    fclose(f);
    return TRUE;
}

// --- Golden file ---
typedef struct { char name[64]; int W, H; unsigned long long hash; } Golden;
static Golden s_gold[64];
static int    s_goldCount;

static void LoadGoldens(const char* path) {
    FILE* f;
    s_goldCount = 0;
    if (fopen_s(&f, path, "r") != 0) return;
    char line[256];
    while (fgets(line, sizeof(line), f) && s_goldCount < 64) {
        if (line[0] == '#' || line[0] == '\n') continue;
        Golden* g = &s_gold[s_goldCount];
        if (sscanf_s(line, "%63s %dx%d %llx", g->name, (unsigned)sizeof(g->name),
                     &g->W, &g->H, &g->hash) == 4) s_goldCount++;
    }
    fclose(f);
}

static const Golden* FindGolden(const char* name) {
    for (int i = 0; i < s_goldCount; i++)
        if (strcmp(s_gold[i].name, name) == 0) return &s_gold[i];
    return NULL;
}

// --- Contrast (phase 40) ---
// Every text/surface pair the chart and the app draw, as theme roles. The
// light theme must give WCAG AA (4.5:1) on each; the check runs before the
// pictures, and a table edit that breaks it fails the run. The dark theme is
// not held to it: its muted grays (dim on box 3.7:1, yesterday's levels 4.0)
// are older, deliberate choices, recorded in the work log.
typedef struct { const char* where; size_t text, surface; } ContrastPair;
#define CP(w, t, s) { w, offsetof(ChartTheme, t), offsetof(ChartTheme, s) }
static const ContrastPair CONTRAST_PAIRS[] = {
    CP("alert tag",               onAlert,   alert),
    CP("alert tag under pointer", onHot,     hot),
    CP("ghost tag",               alertText, box),
    CP("ghost tag, slots full",   dim,       box),
    CP("price stamp, up",         bg,        up),
    CP("price stamp, down",       bg,        down),
    CP("today's level tags",      session,   box),
    CP("yesterday's level tags",  prev,      box),
    CP("crosshair tags",          text,      boxEdge),
    CP("axis labels",             axis,      bg),
    CP("RSI value tag",           rsi,       box),
    CP("RSI legend",              rsi,       bg),
    CP("SMA legend",              sma,       bg),
    CP("EMA legend",              ema,       bg),
    CP("VWAP legend",             vwap,      bg),
    CP("today's level labels",    session,   bg),
    CP("yesterday's level labels",prev,      bg),
    CP("hover box text",          text,      box),
    CP("hover box labels",        dim,       box),
    CP("hover box close, up",     up,        box),
    CP("hover box close, down",   down,      box),
    CP("hover box SMA",           sma,       box),
    CP("hover box EMA",           ema,       box),
    CP("hover box VWAP",          vwap,      box),
    CP("hover box RSI",           rsi,       box),
    CP("header price",            text,      bg),
    CP("header stale/offline",    dim,       bg),
    CP("header change, up",       up,        bg),
    CP("header change, down",     down,      bg),
    CP("toolbar pill at rest",    dim,       bg),
    CP("toolbar pill hot/on",     text,      box),
    CP("overlay headings",        dim,       box),
    CP("overlay active row",      up,        box),
    // Phase 42: the quote line and the range field.
    CP("quote line values",       quote,     bg),
    CP("range cells",             text,      boxEdge),
    CP("range cell selected",     onAccent,  accent),
    // Phase 43: the volume legend stands over the bars, and the value tag
    // carries the close's colors on the box (the hover box's close row).
    CP("volume legend, up bar",   text,      volUp),
    CP("volume legend, down bar", text,      volDown),
};

static double RelLum(COLORREF c) {
    double ch[3] = { GetRValue(c) / 255.0, GetGValue(c) / 255.0, GetBValue(c) / 255.0 };
    for (int i = 0; i < 3; i++)
        ch[i] = (ch[i] <= 0.03928) ? ch[i] / 12.92 : pow((ch[i] + 0.055) / 1.055, 2.4);
    return 0.2126 * ch[0] + 0.7152 * ch[1] + 0.0722 * ch[2];
}

static double ContrastRatio(COLORREF a, COLORREF b) {
    double la = RelLum(a), lb = RelLum(b);
    return (la > lb) ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}

static int CheckContrast(const ChartTheme* t, const char* name) {
    int n = (int)(sizeof(CONTRAST_PAIRS) / sizeof(CONTRAST_PAIRS[0])), bad = 0;
    double worst = 99.0;
    for (int i = 0; i < n; i++) {
        const ContrastPair* p = &CONTRAST_PAIRS[i];
        COLORREF fg = *(const COLORREF*)((const char*)t + p->text);
        COLORREF bg = *(const COLORREF*)((const char*)t + p->surface);
        double r = ContrastRatio(fg, bg);
        if (r < worst) worst = r;
        if (r < 4.5) {
            printf("FAIL contrast %s: %s %02X%02X%02X on %02X%02X%02X is %.2f:1\n", name, p->where,
                   GetRValue(fg), GetGValue(fg), GetBValue(fg),
                   GetRValue(bg), GetGValue(bg), GetBValue(bg), r);
            bad++;
        }
    }
    if (!bad) printf("ok   contrast %s: %d text pairs, lowest %.2f:1\n", name, n, worst);
    return bad;
}

// --- The price axis floor (phase 41) ---
// A view whose low is small against its range must not get an axis below
// zero: Max on 1w (3 100 to 126 000) drew a grid label of -7054.
static int CheckPriceFloor(void) {
    Candle c[2];
    ZeroMemory(c, sizeof(c));
    c[0].open = c[0].low = 3100.0;   c[0].high = c[0].close = 20000.0;
    c[1].open = c[1].low = 20000.0;  c[1].high = c[1].close = 126000.0;
    double mn = 0, mx = 0;
    PriceRange(c, 0, 2, &mn, &mx);
    if (mn < 0.0) { printf("FAIL price floor: min %.2f below zero\n", mn); return 1; }
    printf("ok   price floor: min %.2f, max %.2f\n", mn, mx);
    return 0;
}

int main(int argc, char** argv) {
    BOOL update = FALSE, bmp = FALSE;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--update") == 0) update = TRUE;
        else if (strcmp(argv[i], "--bmp") == 0) bmp = TRUE;
        else { printf("usage: chart_golden [--update] [--bmp]\n"); return 2; }
    }

    // Paths next to the exe, which lives in tests\.
    char dir[MAX_PATH], goldPath[MAX_PATH], outDir[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    char* slash = strrchr(dir, '\\');
    if (slash) *slash = 0;
    sprintf_s(goldPath, MAX_PATH, "%s\\golden\\chart.txt", dir);
    sprintf_s(outDir, MAX_PATH, "%s\\out", dir);

    LoadGoldens(goldPath);
    if (!update && s_goldCount == 0) printf("no goldens in %s - run with --update\n", goldPath);

    unsigned long long hashes[NCASES] = { 0 };
    int contrastFails = CheckContrast(&ChartThemeLight, "light") + CheckPriceFloor();
    int fails = 0;
    for (int i = 0; i < NCASES; i++) {
        const Case* k = &CASES[i];
        // One style per case: the fonts are built for the case's dpi.
        ChartStyle sty;
        if (!ChartStyleCreate(&sty, k->dpi, k->light ? &ChartThemeLight : NULL)) { printf("FAIL %s: ChartStyleCreate\n", k->name); fails++; continue; }
        Surface s;
        ZeroMemory(&s, sizeof(s));
        if (!SurfaceOpen(&s, k->W, k->H)) {
            printf("FAIL %s: no DIB section\n", k->name);
            fails++;
            ChartStyleDestroy(&sty);
            continue;
        }

        DrawCase(s.dc, k, &sty);
        unsigned long long h = Fnv(s.px, k->W * k->H);
        hashes[i] = h;

        // The same case twice must give the same pixels: catches state that
        // leaks from one frame into the next, or reads of uninitialized memory.
        DrawCase(s.dc, k, &sty);
        BOOL stable = (Fnv(s.px, k->W * k->H) == h);
        int cross = CrossCheck(k, &sty, s.px);

        const Golden* g = FindGolden(k->name);
        BOOL match = g && g->W == k->W && g->H == k->H && g->hash == h;
        BOOL ok = stable && cross <= 0 && (update || match);
        if (!ok) fails++;

        printf("%-4s %-24s %4dx%-4d %016llx%s%s%s\n", ok ? "ok" : "FAIL", k->name, k->W, k->H, h,
               stable ? "" : "  UNSTABLE",
               cross > 0 ? "  DIFFERS FROM SCREEN BITMAP" : (cross < 0 ? "  (no screen cross-check)" : ""),
               update ? "" : (!g ? "  NO GOLDEN" : (match ? "" : "  DIFFERS FROM GOLDEN")));
        if (cross > 0) printf("     %d px differ from the CreateCompatibleBitmap drawing\n", cross);

        if (bmp || update || !ok) {
            CreateDirectoryA(outDir, NULL);
            sprintf_s(path, MAX_PATH, "%s\\%s.bmp", outDir, k->name);
            WriteBmp(path, &s);
        }
        SurfaceClose(&s);
        ChartStyleDestroy(&sty);
    }

    if (update) {
        FILE* f;
        sprintf_s(path, MAX_PATH, "%s\\golden", dir);
        CreateDirectoryA(path, NULL);
        if (fopen_s(&f, goldPath, "w") != 0) { printf("cannot write %s\n", goldPath); return 2; }
        fprintf(f, "# chart_golden.c: FNV-1a 64 over the RGB of every pixel.\n");
        fprintf(f, "# Written with --update. Valid for the machine that wrote it (fonts, ClearType).\n");
        for (int i = 0; i < NCASES; i++)
            fprintf(f, "%s %dx%d %016llx\n", CASES[i].name, CASES[i].W, CASES[i].H, hashes[i]);
        fclose(f);
        printf("wrote %s\n", goldPath);
    }
    if (fails) printf("%d of %d cases failed; the pictures are in %s\n", fails, NCASES, outDir);
    else       printf("all %d cases passed\n", NCASES);
    if (contrastFails) printf("%d unit checks failed (contrast pairs, price floor)\n", contrastFails);
    return (fails || contrastFails) ? 1 : 0;
}
