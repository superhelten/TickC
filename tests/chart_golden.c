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
//   tests\chart_golden.exe --update   rewrite the goldens (look at tests\out\*.bmp first);
//                                     refused when a case is unstable or off the screen bitmap
//   tests\chart_golden.exe --bmp      also write every case to tests\out\<name>.bmp
//   tests\chart_golden.exe --perf     only time the chart types (phase 49), no checks
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
#include <limits.h>
#include "chart.h"

#define MAX_CANDLES 6000   // TickC's buffer; the phase 49 timing draws all of it
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
    // Phase 46, all zero in the older rows. base: the first candle's price
    // (0 = 63000); a small one makes the view's dollars per pixel small, as
    // SOL zoomed in. ghost: where the pointer stands in the price column -
    // 0 as `alerts` puts it, 1 ghostDy px from alert 0's row, 2 ghostDy px
    // from the stamp's row, 3 the first row from a third down whose rounded
    // price lies 3 px or more away, 4 no ghost and alert 0 on the legend's
    // middle row, 5 ghostDy px from the price pane's bottom row. loudLast:
    // the last candle has 50 times its volume.
    double base;
    int  ghost, ghostDy;
    BOOL loudLast;
    // Phase 49: the chart type, CHART_* (0, the candles, in the older rows).
    int  type;
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
    // Phase 44: the pointer in the 6 px gap above the volume pane (the price
    // pane ends at 565, the volume pane begins at 571): the horizontal stands
    // on the pane's top row, and its tag, which would reach up over the price
    // column's bottom label, is not drawn. And a 290 px panel with both panes
    // and the averages, where the hover box (139 px) is taller than the price
    // pane (126 px): the box keeps out of the header and hangs into the pane
    // below.
    { "vol_1h_gap_hover",       1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0, 700, 568, FALSE,  96, FALSE, FALSE },
    { "vol_rsi_15m_290_hover",   560, 290, FALSE, 15 * MIN_MS,  360, FALSE, 300,  0, 1.0, 1.0, 280, 107, FALSE,  96, FALSE, TRUE },
    // Phase 46: the axis column's rank and the rows. The pointer in the price
    // column 12 px under an alert (outside its 8 px hit zone) and 10 px over
    // the stamp; a 15 dollar symbol with 12 candles, where one cent is ~50 px
    // and the rounded price's row lies far from the pointer's; an alert on
    // the legend's row; and the view panned back from a last candle louder
    // than every visible one.
    { "ghost_near_alert",       1280, 720, FALSE, MIN_MS,      2400, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, TRUE,   96, FALSE, FALSE, 0.0, 1,  12 },
    { "ghost_near_stamp",       1280, 720, FALSE, MIN_MS,      2400, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, TRUE,   96, FALSE, FALSE, 0.0, 2, -10 },
    { "ghost_row_zoomed",        560, 300, FALSE, MIN_MS,        12, TRUE,    0,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE, 15.0, 3,  0 },
    { "legend_alert_row",       1280, 720, FALSE, 15 * MIN_MS,  360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, TRUE,   96, FALSE, FALSE, 0.0, 4,  0 },
    { "vol_loud_last_panned",   1280, 720, FALSE, HOUR_MS,      360, FALSE,  60, 90, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE, 0.0, 0,  0, TRUE },
    // The pointer on the price pane's bottom row over the volume pane, whose
    // value tag stands at its top (the last candle is the loudest): the
    // ghost tag would reach 2 rows into that tag.
    { "ghost_pane_edge",        1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE, 0.0, 5,  0, TRUE },
    // Phase 49: the chart types. Each in both themes, with the averages,
    // levels and the volume pane; a crosshair, alerts and a ghost; 150 % and
    // 200 %; the desktop edge to edge; the smallest panel, where the bars
    // stand behind the price; OHLC zoomed in (full ticks) and with more
    // candles than pixels (the ticks collapse into the high-low line).
    { "ohlc_1h_1280x720",       1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_OHLC },
    { "ohlc_1h_zoomed_hover",   1280, 720, FALSE, HOUR_MS,      360, FALSE,  40,  5, 1.0, 1.0, 640, 300, FALSE,  96, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_OHLC },
    { "ohlc_1m_dense",          1280, 720, FALSE, MIN_MS,      2400, FALSE,   0,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_OHLC },
    { "ohlc_light_15m_alerts",  1280, 720, FALSE, 15 * MIN_MS,  360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, TRUE,   96, TRUE,  FALSE, 0.0, 0,  0, FALSE, CHART_OHLC },
    { "ohlc_dpi144_1h",         1920,1080, FALSE, HOUR_MS,      360, FALSE,  60,  0, 1.0, 1.0,  -1,  -1, FALSE, 144, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_OHLC },
    { "ohlc_desktop_1920x1080", 1920,1080, TRUE,  MIN_MS,      2400, FALSE, 300,  0, 0.0, 0.0,  -1,  -1, FALSE,  96, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_OHLC },
    { "line_1h_1280x720",       1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_LINE },
    { "line_1h_hover",          1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0, 700, 300, FALSE,  96, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_LINE },
    { "line_light_15m_alerts",  1280, 720, FALSE, 15 * MIN_MS,  360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, TRUE,   96, TRUE,  FALSE, 0.0, 0,  0, FALSE, CHART_LINE },
    { "line_dpi192_1m",         2560,1440, FALSE, MIN_MS,      2400, FALSE, 300,  0, 1.0, 1.0,1400, 600, FALSE, 192, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_LINE },
    { "line_desktop_1920x1080", 1920,1080, TRUE,  MIN_MS,      2400, FALSE, 300,  0, 0.0, 0.0,  -1,  -1, FALSE,  96, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_LINE },
    { "mountain_1h_1280x720",   1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_MOUNTAIN },
    { "mountain_light_1h_hover",1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0, 700, 300, FALSE,  96, TRUE,  FALSE, 0.0, 0,  0, FALSE, CHART_MOUNTAIN },
    { "mountain_15m_alerts",    1280, 720, FALSE, 15 * MIN_MS,  360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, TRUE,   96, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_MOUNTAIN },
    { "mountain_dpi144_rsi",    1920,1080, FALSE, MIN_MS,      2400, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE, 144, FALSE, TRUE,  0.0, 0,  0, FALSE, CHART_MOUNTAIN },
    { "mountain_rsi_400x250",    400, 250, FALSE, 15 * MIN_MS,  360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, TRUE,  0.0, 0,  0, FALSE, CHART_MOUNTAIN },
    // A 185 px price pane: the fill's top (the highest close, 7 % under the
    // pane's top, on 15 Sep) lies under the averages' legend.
    { "mountain_light_560x300",  560, 300, FALSE, HOUR_MS,  360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, TRUE,  FALSE, 0.0, 0,  0, FALSE, CHART_MOUNTAIN },
    { "mountain_desktop_3840x1600",3840,1600,TRUE, MIN_MS,      2400, FALSE, 300,  0, 0.0, 0.0,  -1,  -1, FALSE,  96, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_MOUNTAIN },
    { "mountain_light_desktop", 1920,1080, TRUE,  MIN_MS,      2400, FALSE, 300,  0, 0.0, 0.0,  -1,  -1, FALSE,  96, TRUE,  FALSE, 0.0, 0,  0, FALSE, CHART_MOUNTAIN },
};
#define NCASES ((int)(sizeof(CASES) / sizeof(CASES[0])))

static Candle s_candles[MAX_CANDLES];

// --- Synthetic candles: a seeded random walk, the same on every run ---
static unsigned int s_rng;
static double Rnd(void) {
    s_rng = s_rng * 1664525u + 1013904223u;
    return (double)(s_rng >> 8) / 16777216.0;
}

static void MakeCandles(Candle* c, int n, long long ivMs, double base) {
    // Per-candle move grows with the interval, so every interval gets a
    // chart that fills the price axis the way real data does.
    double step = (ivMs >= DAY_MS) ? 0.03 : (ivMs >= HOUR_MS) ? 0.008
                : (ivMs >= 15 * MIN_MS) ? 0.004 : 0.0015;
    s_rng = 0x5EED0000u ^ (unsigned int)(ivMs / MIN_MS);
    double p = (base > 0.0) ? base : 63000.0;
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
// out (phase 46), when not NULL, gets the state and the data the frame was
// drawn from, so a unit check can compute rows with the engine's own
// functions. out->alerts points at s_alerts.
static double s_alerts[2];
static void DrawCase(HDC hdc, const Case* k, const ChartStyle* base, ChartState* outSt, ChartData* outIn) {
    MakeCandles(s_candles, k->n, k->ivMs, k->base);
    if (k->loudLast) s_candles[k->n - 1].volume *= 50.0;

    ChartState st;
    ZeroMemory(&st, sizeof(st));
    st.viewCount  = k->view;
    st.viewStart  = k->n - k->back - k->view;
    st.followLive = (k->back == 0);
    st.chartType  = k->type;   // phase 49: before SyncDisp, which scales by it
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
    double* alerts = s_alerts;
    alerts[0] = floor(last * 1.003);
    alerts[1] = -floor(last * 0.985);

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
    // Phase 46: the pointer's row in the price column, as TickC's mouse move
    // sets axisHotY (the app draws a ghost only off every alert's hit zone,
    // and alertHot stays -1 here).
    if (k->ghost != 0) {
        ChartRect g = ChartGeometry(k->W, k->H, k->desktop, k->dpi, k->rsi, k->volF > 0.0);
        if (k->ghost == 1) {
            in.axisHotY = AlertY(&st, &g, fabs(alerts[0])) + ChartPx(k->dpi, k->ghostDy);
        } else if (k->ghost == 2) {
            in.axisHotY = AlertY(&st, &g, last) + ChartPx(k->dpi, k->ghostDy);
        } else if (k->ghost == 3) {
            int y = g.top + (g.bottom - g.top) / 3;
            while (y < g.bottom && abs(AlertY(&st, &g, AlertPriceAtY(&st, &g, y)) - y) < 3) y++;
            in.axisHotY = y;
        } else if (k->ghost == 5) {
            in.axisHotY = g.bottom + ChartPx(k->dpi, k->ghostDy);
        } else if (k->ghost == 4) {
            // The legend's text starts 4 px under the top and is 15 px tall.
            alerts[0] = AlertPriceAtY(&st, &g, g.top + ChartPx(k->dpi, 4) + ChartPx(k->dpi, 7));
            in.axisHotY = -1;
            in.alertFlashF = 0.0;
        }
    }

    ChartStyle sty = *base;
    sty.fontPill = k->desktop ? ChartPillFontCreate(k->H) : NULL;
    ChartDrawBackground(hdc, k->W, k->H, NULL, sty.brBg);
    ChartDrawBody(hdc, k->W, k->H, &st, &in, &sty);
    GdiFlush();
    if (sty.fontPill) DeleteObject(sty.fontPill);
    if (outSt) *outSt = st;
    if (outIn) *outIn = in;
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
        DrawCase(dc, k, sty, NULL, NULL);
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
#define GOLD_MAX 96   // phase 49: 61 cases
static Golden s_gold[GOLD_MAX];
static int    s_goldCount;

static void LoadGoldens(const char* path) {
    FILE* f;
    s_goldCount = 0;
    if (fopen_s(&f, path, "r") != 0) return;
    char line[256];
    while (fgets(line, sizeof(line), f) && s_goldCount < GOLD_MAX) {
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
    // Phase 43: the value tag carries the close's colors on the box (the
    // hover box's close row). Phase 45: the pane's bars are strong enough
    // that text over them cannot reach 4.5:1, so the legend stands on a
    // rectangle of the background, and its pair is text on bg; the phase 43
    // pairs (text on volUp, on volDown) are gone with it.
    CP("volume legend",           text,      bg),
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

// --- The time axis on the smallest panels (phase 44) ---
// With two label widths in the chart (N = 2) TimeTickStep gave the whole view
// as the step, NiceTimeStep rounded it past the view, and a 400x250 panel on
// 1h never got a time label. The label width is measured in the axis font,
// with the function ChartDrawBody measures it with (phase 45: the widest of
// "00:00" and "30 Sep", ChartTimeLabelW), not assumed. Then the N <= 2 branch
// over the phase 11 ranges (chart width 160-4000, minDx 80-118, 1-1440
// candles): the spacing step * chartW / dCount is never below minDx. N >= 3
// is the phase 11 formula, untouched.
static int CheckTimeAxisSmall(void) {
    int bad = 0;
    ChartStyle sty;
    HDC dc = CreateCompatibleDC(NULL);
    if (!dc || !ChartStyleCreate(&sty, 96, NULL)) {
        printf("FAIL time axis: no DC or style\n");
        if (dc) DeleteDC(dc);
        return 1;
    }
    HGDIOBJ oldF = SelectObject(dc, sty.fontAxis);
    SIZE tsz = { 0, 0 };
    tsz.cx = ChartTimeLabelW(dc, HOUR_MS);
    SelectObject(dc, oldF);
    ChartStyleDestroy(&sty);
    DeleteDC(dc);

    ChartRect g = ChartGeometry(400, 250, FALSE, 96, FALSE, TRUE);
    int minDx = tsz.cx + TIME_LBL_GAP;
    if (minDx < TIME_DX_MIN) minDx = TIME_DX_MIN;
    int step = NiceTimeStep(TimeTickStep(300.0, g.cw, minDx), HOUR_MS);
    // A label's center must stay tsz.cx / 2 inside [left, right]; labels
    // step * slot apart (+ 2 px for the truncation to whole pixels) that fit
    // in that window always put at least one in it, wherever the view is.
    double dx = (double)step * (double)g.cw / 300.0;
    if (step >= 300 || dx + 2.0 > (double)(g.cw - tsz.cx)) {
        printf("FAIL time axis 400x250 1h: cw %d, minDx %d, step %d (%.1f px), window %d px\n",
               g.cw, minDx, step, dx, g.cw - tsz.cx);
        bad++;
    } else {
        printf("ok   time axis 400x250 1h: cw %d, minDx %d, step %d candles, %.1f px apart\n",
               g.cw, minDx, step, dx);
    }

    long long checked = 0;
    for (int w = 160; w <= 4000; w++) {
        for (int dxMin = 80; dxMin <= 118; dxMin++) {
            if (w / dxMin > 2) continue;   // the N <= 2 branch only
            for (int m = 1; m <= 1440; m++) {
                int s = TimeTickStep((double)m, w, dxMin);
                checked++;
                if (s < 1 || (long long)s * w < (long long)m * dxMin) {
                    if (bad < 10)
                        printf("FAIL time axis N<=2: W %d, minDx %d, M %d gives S %d, %.1f px\n",
                               w, dxMin, m, s, (double)s * w / m);
                    bad++;
                }
            }
        }
    }
    if (!bad) printf("ok   time axis N<=2: %lld cases, never closer than minDx\n", checked);
    return bad;
}

// --- The hover box's time row (phase 45) ---
// Intraday the row is "21 Sep 14:35". The small font is proportional, so
// every month is measured ("30 Mmm 00:00"; the digits are tabular) at each
// dpi the cases use, against the row's room: HOVER_BOX_W less the 7 px left
// and 6 px right inset ChartDrawBody gives it. DrawTextW would clip a row
// that does not fit without a word.
static int CheckHoverTime(void) {
    static const wchar_t* const MON[12] = { L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
                                            L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec" };
    static const int DPIS[3] = { 96, 144, 192 };
    int bad = 0;
    HDC dc = CreateCompatibleDC(NULL);
    if (!dc) { printf("FAIL hover time: no DC\n"); return 1; }
    for (int d = 0; d < 3; d++) {
        ChartStyle sty;
        if (!ChartStyleCreate(&sty, DPIS[d], NULL)) { printf("FAIL hover time: no style\n"); bad++; continue; }
        HGDIOBJ oldF = SelectObject(dc, sty.fontSmall);
        int room = ChartPx(DPIS[d], HOVER_BOX_W) - ChartPx(DPIS[d], 7) - ChartPx(DPIS[d], 6);
        int widest = 0;
        for (int m = 0; m < 12; m++) {
            wchar_t s[24];
            swprintf_s(s, 24, L"30 %s 00:00", MON[m]);
            SIZE sz = { 0, 0 };
            GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
            if (sz.cx > widest) widest = sz.cx;
        }
        SelectObject(dc, oldF);
        ChartStyleDestroy(&sty);
        if (widest > room) {
            printf("FAIL hover time at %d dpi: %d px in a %d px row\n", DPIS[d], widest, room);
            bad++;
        } else {
            printf("ok   hover time at %d dpi: widest %d px in a %d px row\n", DPIS[d], widest, room);
        }
    }
    DeleteDC(dc);
    return bad;
}

// --- FormatCandleTime (phase 45) ---
// A 1d candle opens at 00:00 UTC and is dated in UTC: at UTC - 5 the local
// conversion gave the day before (phase 44 review, F4). And the quote line's
// At (tickc.c, 1m) stays a clock at local midnight - the date belongs to the
// axis and the hover box, not to it.
static int CheckTimeForms(void) {
    const long long dayUtc = T_END_MS - 14 * HOUR_MS;   // 2026-09-21 00:00 UTC
    wchar_t s[24];
    int bad = 0;
    FormatCandleTime(dayUtc, DAY_MS, -5 * HOUR_MS, s, 24);
    if (wcscmp(s, L"2026-09-21") != 0) { printf("FAIL time forms: 1d at UTC-5 gave %ls\n", s); bad++; }
    FormatCandleTime(dayUtc - 2 * HOUR_MS, MIN_MS, UTC_OFFSET_MS, s, 24);
    if (wcscmp(s, L"00:00") != 0) { printf("FAIL time forms: 1m at local midnight gave %ls\n", s); bad++; }
    if (!bad) printf("ok   time forms: 1d dated in UTC, the 1m clock without a date\n");
    return bad;
}

// --- Phase 46: the axis column's rank, the ghost's row, the legend, the labels ---
// Pixel checks on the cases' own pictures: each draws a case by name into a
// DIB section and reads what landed where, with the rows computed by the
// engine's exported functions (AlertY, AlertPriceAtY, SessionStart ...).
typedef struct {
    const Case* k;
    Surface     s;
    ChartStyle  sty;
    ChartState  st;
    ChartData   in;
    ChartRect   g;
    int         tagHalf, axR;
} Scene;

static const Case* FindCase(const char* name) {
    for (int i = 0; i < NCASES; i++) if (strcmp(CASES[i].name, name) == 0) return &CASES[i];
    return NULL;
}

static BOOL SceneOpen(Scene* sc, const Case* k) {
    ZeroMemory(sc, sizeof(*sc));
    sc->k = k;
    if (!k) return FALSE;
    if (!ChartStyleCreate(&sc->sty, k->dpi, k->light ? &ChartThemeLight : NULL)) return FALSE;
    if (!SurfaceOpen(&sc->s, k->W, k->H)) { ChartStyleDestroy(&sc->sty); return FALSE; }
    DrawCase(sc->s.dc, k, &sc->sty, &sc->st, &sc->in);
    sc->g = ChartGeometry(k->W, k->H, k->desktop, k->dpi, k->rsi, k->volF > 0.0);
    sc->tagHalf = ChartPx(k->dpi, 8);
    sc->axR = k->W - ChartPx(k->dpi, AXIS_PAD_R);
    return TRUE;
}

static void SceneClose(Scene* sc) {
    SurfaceClose(&sc->s);
    ChartStyleDestroy(&sc->sty);
}

// The pixel as a COLORREF (the DIB is 00RRGGBB, a COLORREF 00BBGGRR).
static COLORREF PxAt(const Scene* sc, int x, int y) {
    DWORD v = sc->s.px[y * sc->s.W + x];
    return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

// Pixels in [x0, x1) x [y0, y1) that are (same = TRUE) or are not (FALSE) c.
static int CountPx(const Scene* sc, int x0, int y0, int x1, int y1, COLORREF c, BOOL same) {
    int n = 0;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > sc->s.W) x1 = sc->s.W;
    if (y1 > sc->s.H) y1 = sc->s.H;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if ((PxAt(sc, x, y) == c) == same) n++;
    return n;
}

// F2: the ghost tag - the price a click would set - has a place in the
// column's rank. 12 px under an alert, the alert keeps its surface and loses
// its number (the phase 29 rule for the crosshair tag): the strip of the
// alert tag above the ghost is plain amber. 10 px over the stamp, the ghost
// is whole (no stamp color inside it) and the strip of the stamp under it
// carries no cut number (plain up or down).
static int CheckGhostRank(void) {
    int bad = 0;
    Scene sc;
    if (!SceneOpen(&sc, FindCase("ghost_near_alert"))) { printf("FAIL ghost rank: no scene\n"); return 1; }
    {
        int ya = AlertY(&sc.st, &sc.g, fabs(sc.in.alerts[0]));
        int n = CountPx(&sc, sc.g.edge + 1, ya - sc.tagHalf, sc.axR + ChartPx(sc.k->dpi, 3), ya + 2,
                        sc.sty.clr.alert, FALSE);
        if (n) { printf("FAIL ghost rank: %d px of a number in the alert tag's strip above the ghost (alert row %d, pointer %d)\n",
                        n, ya, sc.in.axisHotY); bad++; }
        else printf("ok   ghost rank: the alert 12 px above the ghost keeps its surface, no cut number\n");
    }
    SceneClose(&sc);
    if (!SceneOpen(&sc, FindCase("ghost_near_stamp"))) { printf("FAIL ghost rank: no scene\n"); return bad + 1; }
    {
        int n = sc.in.count;
        double last = s_candles[n - 1].close;
        COLORREF stampC = (last >= s_candles[n - 2].close) ? sc.sty.clr.up : sc.sty.clr.down;
        int yp = AlertY(&sc.st, &sc.g, last);
        int yh = sc.in.axisHotY;
        int yr = AlertY(&sc.st, &sc.g, AlertPriceAtY(&sc.st, &sc.g, yh));   // where the alert would land
        int yTop = (yh > yr ? yh : yr) - sc.tagHalf, yBot = (yh < yr ? yh : yr) + sc.tagHalf;
        int x0 = sc.g.edge + 1, x1 = sc.axR + ChartPx(sc.k->dpi, 3);
        int inGhost = CountPx(&sc, x0, yTop, x1, yBot, stampC, TRUE);
        int lo = (yh > yr ? yh : yr) + sc.tagHalf;
        int inStrip = CountPx(&sc, x0, lo, x1, yp + sc.tagHalf, stampC, FALSE);
        if (inGhost || inStrip) {
            printf("FAIL ghost rank: ghost 10 px over the stamp - %d px of stamp in the ghost, %d px of text in the stamp's strip (stamp %d, pointer %d)\n",
                   inGhost, inStrip, yp, yh);
            bad++;
        } else printf("ok   ghost rank: the ghost 10 px over the stamp is whole, the stamp's strip has no cut number\n");
    }
    SceneClose(&sc);
    // Drawn after the stamp, the ghost tag also lands after the panes' value
    // tags. On the price pane's bottom row it would reach 2 rows over the
    // volume pane's top, into the value tag standing there: the phase 44
    // rule for the crosshair tag, a tag stays in its pane's rows (the line is
    // drawn). No ghost frame in the value tag's rows.
    if (!SceneOpen(&sc, FindCase("ghost_pane_edge"))) { printf("FAIL ghost rank: no scene\n"); return bad + 1; }
    {
        int x0 = sc.g.edge + 1, x1 = sc.axR + ChartPx(sc.k->dpi, 3);
        int n = CountPx(&sc, x0, sc.g.volTop, x1, sc.g.volTop + 2 * sc.tagHalf, sc.sty.clr.alertText, TRUE);
        int line = CountPx(&sc, sc.g.left, sc.g.bottom, sc.g.edge, sc.g.bottom + 1, sc.sty.clr.alertLine, TRUE);
        if (n || line == 0) {
            printf("FAIL ghost rank: on the pane's edge %d px of ghost frame in the volume tag's rows, line %d px\n", n, line);
            bad++;
        } else printf("ok   ghost rank: on the pane's edge the ghost keeps out of the volume tag, its line stays (%d px)\n", line);
    }
    SceneClose(&sc);
    return bad;
}

// F7: the ghost line previews the row the alert will be drawn on - the row of
// the ROUNDED price - not the pointer's row. On a 15 dollar symbol with 12
// candles one cent is ~50 px, and the two rows lie apart.
static int CheckGhostRow(void) {
    int bad = 0;
    Scene sc;
    if (!SceneOpen(&sc, FindCase("ghost_row_zoomed"))) { printf("FAIL ghost row: no scene\n"); return 1; }
    int yh = sc.in.axisHotY;
    int yr = AlertY(&sc.st, &sc.g, AlertPriceAtY(&sc.st, &sc.g, yh));
    int w = sc.g.edge - sc.g.left;
    int onR = CountPx(&sc, sc.g.left, yr, sc.g.edge, yr + 1, sc.sty.clr.alertLine, TRUE);
    int onH = CountPx(&sc, sc.g.left, yh, sc.g.edge, yh + 1, sc.sty.clr.alertLine, TRUE);
    if (yr == yh || onR < w / 2 || onH > 0) {
        printf("FAIL ghost row: pointer row %d has %d px of line, the alert's row %d has %d of %d\n",
               yh, onH, yr, onR, w);
        bad++;
    } else printf("ok   ghost row: the line is on the alert's row %d (%d of %d px), not the pointer's %d\n",
                  yr, onR, w, yh);
    SceneClose(&sc);
    return bad;
}

// F8: an alert line through the indicator legend. The legend is opaque over
// it, as over the level lines: on the alert's row, under the legend, no line.
static int CheckLegendAlert(void) {
    int bad = 0;
    Scene sc;
    if (!SceneOpen(&sc, FindCase("legend_alert_row"))) { printf("FAIL legend alert: no scene\n"); return 1; }
    int ya = AlertY(&sc.st, &sc.g, fabs(sc.in.alerts[0]));
    int x0 = sc.g.left + ChartPx(sc.k->dpi, 6);
    int n = CountPx(&sc, x0, ya, x0 + ChartPx(sc.k->dpi, 200), ya + 1, sc.sty.clr.alertLine, TRUE);
    int beyond = CountPx(&sc, sc.g.right - ChartPx(sc.k->dpi, 200), ya, sc.g.right, ya + 1, sc.sty.clr.alertLine, TRUE);
    if (n || beyond == 0) {
        printf("FAIL legend alert: %d px of the alert line (row %d) through the legend, %d px beyond it\n", n, ya, beyond);
        bad++;
    } else printf("ok   legend alert: the line (row %d) stops at the legend, %d px of it beyond\n", ya, beyond);
    SceneClose(&sc);
    return bad;
}

// F6: the last candle is out of view and louder than every visible one. Its
// value tag would be pinned to the pane's top, where no bar is; the stamp's
// rule says not drawn. Control: the same candle in view, where its bar
// reaches the top and the tag belongs there.
static int CheckVolTag(void) {
    int bad = 0;
    Scene sc;
    if (!SceneOpen(&sc, FindCase("vol_loud_last_panned"))) { printf("FAIL volume tag: no scene\n"); return 1; }
    int x0 = sc.g.edge + 1, x1 = sc.axR + ChartPx(sc.k->dpi, 3);
    int n = CountPx(&sc, x0, sc.g.volTop, x1, sc.g.volTop + 2 * sc.tagHalf + 1, sc.sty.clr.box, TRUE);
    if (n) { printf("FAIL volume tag: %d px of a tag at the pane's top for a candle out of view\n", n); bad++; }
    else printf("ok   volume tag: none for a louder last candle out of view\n");
    SceneClose(&sc);
    Case live = *FindCase("vol_loud_last_panned");
    live.view = 300; live.back = 0;
    if (!SceneOpen(&sc, &live)) { printf("FAIL volume tag: no scene\n"); return bad + 1; }
    n = CountPx(&sc, x0, sc.g.volTop, x1, sc.g.volTop + 2 * sc.tagHalf + 1, sc.sty.clr.box, TRUE);
    if (!n) { printf("FAIL volume tag (control): no tag for the loud last candle in view\n"); bad++; }
    else printf("ok   volume tag (control): the loud last candle in view keeps its tag (%d px)\n", n);
    SceneClose(&sc);
    return bad;
}

// U7: the level names (HOD, LOD, PDC, PDH, PDL) must not be struck by the
// candles. In every panel case with the levels at full strength: the label
// pixels are the exact line colors off the lines' rows; they are grouped
// into labels, and no candle pixel (exact up or down) may lie inside a
// label's box, nor another level's line. The level rows come from the
// engine's session functions.
static int CheckLevelLabels(void) {
    int bad = 0, labels = 0, cases = 0;
    for (int i = 0; i < NCASES; i++) {
        const Case* k = &CASES[i];
        if (k->desktop || k->indF < 1.0) continue;
        Scene sc;
        if (!SceneOpen(&sc, k)) { printf("FAIL level labels %s: no scene\n", k->name); bad++; continue; }
        cases++;
        const ChartRect* g = &sc.g;
        double range = sc.st.dispMax - sc.st.dispMin;
        if (range < 1e-9) range = 1.0;
        int yl[LVL_COUNT], nl = 0;
        BOOL full = FALSE;
        int s0 = SessionStart(s_candles, k->n, k->ivMs, k->histDone, &full);
        if (s0 >= 0 && full) {
            double p[LVL_COUNT];
            int np = 2;
            SessionHiLo(s_candles, s0, k->n, &p[0], &p[1]);
            int pe = -1;
            BOOL pf = FALSE;
            int ps = PrevSession(s_candles, k->n, k->ivMs, k->histDone, &pe, &pf);
            if (ps >= 0 && pf) {
                p[2] = s_candles[pe - 1].close;
                SessionHiLo(s_candles, ps, pe, &p[3], &p[4]);
                np = 5;
            }
            for (int q = 0; q < np; q++)
                yl[nl++] = g->top + (int)(((sc.st.dispMax - p[q]) / range) * (double)g->ch);
        }
        // Label pixels, grouped: a pixel within 12 px across and 16 px down
        // of a group's box joins it, as long as the box stays one label tall
        // (the axis font's 15 px cell) - two labels on either side of a line
        // are two groups, a line through one label is inside one.
        int maxH = ChartPx(k->dpi, 16);
        RECT box[16];
        int nb = 0;
        for (int y = g->top; y <= g->bottom; y++) {
            BOOL onLine = FALSE;
            for (int q = 0; q < nl; q++) if (abs(y - yl[q]) <= 1) onLine = TRUE;
            if (onLine) continue;
            for (int x = g->left; x < g->right; x++) {
                COLORREF c = PxAt(&sc, x, y);
                if (c != sc.sty.clr.session && c != sc.sty.clr.prev) continue;
                int b = 0;
                for (; b < nb; b++)
                    if (x >= box[b].left - 12 && x < box[b].right + 12 &&
                        y >= box[b].top - 16 && y < box[b].bottom + 16 &&
                        y + 1 - box[b].top <= maxH) break;
                if (b == nb) {
                    if (nb == 16) continue;
                    box[nb].left = x; box[nb].right = x + 1; box[nb].top = y; box[nb].bottom = y + 1;
                    nb++;
                } else {
                    if (x < box[b].left) box[b].left = x;
                    if (x + 1 > box[b].right) box[b].right = x + 1;
                    if (y + 1 > box[b].bottom) box[b].bottom = y + 1;
                }
            }
        }
        for (int b = 0; b < nb; b++) {
            labels++;
            // Phase 49: the marks of the case's chart type. The candles and
            // the OHLC bars are up and down; the line and the mountain are
            // the line - and the mountain's fill must not show inside a
            // label either: over the fill the text stands on the background
            // (its gray is not 4.5:1 on the fill in the light theme).
            int hit;
            if (k->type == CHART_LINE || k->type == CHART_MOUNTAIN)
                hit = CountPx(&sc, box[b].left, box[b].top, box[b].right, box[b].bottom, sc.sty.clr.line, TRUE);
            else
                hit = CountPx(&sc, box[b].left, box[b].top, box[b].right, box[b].bottom, sc.sty.clr.up, TRUE) +
                      CountPx(&sc, box[b].left, box[b].top, box[b].right, box[b].bottom, sc.sty.clr.down, TRUE);
            if (hit) {
                printf("FAIL level labels %s: %d px of the price's marks inside the label at %d,%d-%d,%d\n", k->name, hit,
                       box[b].left, box[b].top, box[b].right, box[b].bottom);
                bad++;
            }
            if (k->type == CHART_MOUNTAIN) {
                int fill = CountPx(&sc, box[b].left, box[b].top, box[b].right, box[b].bottom, sc.sty.clr.mountain, TRUE);
                if (fill) {
                    printf("FAIL level labels %s: %d px of the mountain's fill inside the label at %d,%d-%d,%d\n",
                           k->name, fill, box[b].left, box[b].top, box[b].right, box[b].bottom);
                    bad++;
                }
            }
            // And no other level's line through it: the label's own line
            // lies 2 rows under it or 3 over it, outside the box.
            for (int q = 0; q < nl; q++) {
                if (yl[q] >= box[b].top - 1 && yl[q] <= box[b].bottom) {
                    printf("FAIL level labels %s: a level line (row %d) through the label at %d,%d-%d,%d\n",
                           k->name, yl[q], box[b].left, box[b].top, box[b].right, box[b].bottom);
                    bad++;
                    break;
                }
            }
        }
        SceneClose(&sc);
    }
    if (!bad) printf("ok   level labels: %d labels in %d cases, none struck by the price's marks\n", labels, cases);
    return bad;
}

// --- Phase 49: the chart types ---
// The display the frame was drawn from, and the engine's candle geometry
// from it: slot, body width, the mark's stroke (ChartPx(dpi, 1), no wider
// than the body), and x and y as ChartDrawBody computes them.
typedef struct { double dStart, slot, maxP, range; int bodyW, w, vs, vc; } Marks;

static void MarksOf(const Scene* sc, Marks* m) {
    double dc = (sc->st.dispCount < 1.0) ? 1.0 : sc->st.dispCount;
    m->dStart = sc->st.dispStart;
    m->slot = (double)sc->g.cw / dc;
    m->bodyW = (int)(m->slot * 0.62);
    if (m->bodyW < 1) m->bodyW = 1;
    if (m->bodyW > ChartPx(sc->k->dpi, 18)) m->bodyW = ChartPx(sc->k->dpi, 18);
    m->w = ChartPx(sc->k->dpi, 1);
    if (m->w > m->bodyW) m->w = m->bodyW;
    m->maxP = sc->st.dispMax;
    m->range = sc->st.dispMax - sc->st.dispMin;
    if (m->range < 1e-9) m->range = 1.0;
    GetView(&sc->st, sc->in.count, &m->vs, &m->vc);
}

static int MarkX(const Scene* sc, const Marks* m, int i) {
    return sc->g.left + (int)(((double)i - m->dStart + 0.5) * m->slot);
}

static int MarkY(const Scene* sc, const Marks* m, double p) {
    return sc->g.top + (int)(((m->maxP - p) / m->range) * (double)sc->g.ch);
}

// Is x exactly Blend(bg, c, t) for some t in 1..254? (pitfall 87)
static BOOL OnBlendLine(COLORREF bg, COLORREF c, COLORREF x) {
    for (int t = 1; t < 255; t++) if (Blend(bg, c, t) == x) return TRUE;
    return FALSE;
}

static unsigned long long HashCase(const Case* k) {
    ChartStyle sty;
    Surface s;
    unsigned long long h = 0;
    if (!ChartStyleCreate(&sty, k->dpi, k->light ? &ChartThemeLight : NULL)) return 0;
    if (SurfaceOpen(&s, k->W, k->H)) {
        DrawCase(s.dc, k, &sty, NULL, NULL);
        h = Fnv(s.px, k->W * k->H);
        SurfaceClose(&s);
    }
    ChartStyleDestroy(&sty);
    return h;
}

// Each type is drawn as its own marks, and on its own scale:
//  - every typed case differs from the same case drawn as candles;
//  - the line and the mountain scale on the closes (the display the frame
//    snapped to, and PriceRangeFor), the bars on high and low;
//  - the line: pixels of clr.line along the closes, and no candle colors in
//    the price pane (the last-price line's row aside);
//  - the mountain: the fill under the line and never above it;
//  - OHLC: the open's tick on the left and the close's on the right, and no
//    body between them;
//  - the colors: the mountain is Blend(bg, line), and neither is on a text
//    role's blend line (pitfall 87).
static int CheckChartTypes(void) {
    int bad = 0, typed = 0;
    // The colors, both themes.
    const ChartTheme* th[2] = { &ChartThemeDark, &ChartThemeLight };
    for (int t = 0; t < 2; t++) {
        const ChartTheme* c = th[t];
        const COLORREF text[] = { c->text, c->dim, c->axis, c->up, c->down, c->session, c->prev,
                                  c->sma, c->ema, c->vwap, c->rsi, c->alert, c->alertText,
                                  c->onAlert, c->onHot, c->quote, c->onAccent,
                                  RGB(0xFF, 0xFF, 0xFF) };   // the dark watermark's ink
        int nt = (int)(sizeof(text) / sizeof(text[0])), on = 0;
        for (int r = 0; r < nt; r++) {
            if (c->line == text[r] || c->mountain == text[r] ||
                OnBlendLine(c->bg, text[r], c->line) || OnBlendLine(c->bg, text[r], c->mountain)) on++;
        }
        BOOL fillOk = OnBlendLine(c->bg, c->line, c->mountain);
        if (on || !fillOk) {
            printf("FAIL chart types %s: line/mountain on %d text blend line(s), mountain %s Blend(bg, line)\n",
                   t ? "light" : "dark", on, fillOk ? "is" : "is NOT");
            bad++;
        } else printf("ok   chart types %s: mountain = Blend(bg, line), off every text role's blend line\n",
                      t ? "light" : "dark");
    }
    // The axis per type, straight from PriceRangeFor.
    {
        MakeCandles(s_candles, 360, HOUR_MS, 0.0);
        double mn0, mx0, mn, mx, cmn = s_candles[60].close, cmx = cmn;
        for (int i = 61; i < 360; i++) {
            if (s_candles[i].close < cmn) cmn = s_candles[i].close;
            if (s_candles[i].close > cmx) cmx = s_candles[i].close;
        }
        double pad = (cmx - cmn) * 0.08;
        PriceRange(s_candles, 60, 300, &mn0, &mx0);
        int wrong = 0;
        for (int t = 0; t < CHART_TYPE_COUNT; t++) {
            PriceRangeFor(s_candles, 60, 300, t, &mn, &mx);
            BOOL closes = (t == CHART_LINE || t == CHART_MOUNTAIN);
            double emn = closes ? cmn - pad : mn0, emx = closes ? cmx + pad : mx0;
            if (fabs(mn - emn) > 1e-6 || fabs(mx - emx) > 1e-6) {
                printf("FAIL chart types: PriceRangeFor type %d gives %.2f..%.2f, expected %.2f..%.2f\n",
                       t, mn, mx, emn, emx);
                wrong++;
            }
        }
        if (wrong) bad++;
        else printf("ok   chart types: PriceRangeFor - line and mountain on the closes, candles and OHLC on high/low\n");
    }
    for (int i = 0; i < NCASES; i++) {
        const Case* k = &CASES[i];
        if (k->type == CHART_CANDLES) continue;
        typed++;
        Case asCandles = *k;
        asCandles.type = CHART_CANDLES;
        unsigned long long hT = HashCase(k), h0 = HashCase(&asCandles);
        if (hT == h0) {
            printf("FAIL chart types %s: the same picture as the candles\n", k->name);
            bad++;
        }
        Scene sc;
        if (!SceneOpen(&sc, k)) { printf("FAIL chart types %s: no scene\n", k->name); bad++; continue; }
        Marks m;
        MarksOf(&sc, &m);
        const ChartRect* g = &sc.g;
        int n = sc.in.count;
        // The display the frame snapped to.
        double emn, emx;
        PriceRange(s_candles, m.vs, m.vc, &emn, &emx);
        if (k->type == CHART_LINE || k->type == CHART_MOUNTAIN) {
            double cmn = s_candles[m.vs].close, cmx = cmn;
            for (int j = m.vs + 1; j < m.vs + m.vc; j++) {
                if (s_candles[j].close < cmn) cmn = s_candles[j].close;
                if (s_candles[j].close > cmx) cmx = s_candles[j].close;
            }
            emn = cmn - (cmx - cmn) * 0.08;
            emx = cmx + (cmx - cmn) * 0.08;
        }
        if (fabs(sc.st.dispMin - emn) > 1e-6 || fabs(sc.st.dispMax - emx) > 1e-6) {
            printf("FAIL chart types %s: the axis is %.2f..%.2f, expected %.2f..%.2f\n",
                   k->name, sc.st.dispMin, sc.st.dispMax, emn, emx);
            bad++;
        }
        int yLast = MarkY(&sc, &m, s_candles[n - 1].close);
        if (k->type == CHART_LINE || k->type == CHART_MOUNTAIN) {
            int lineN = CountPx(&sc, g->left, g->top, g->right, g->bottom + 1, sc.sty.clr.line, TRUE);
            int candleN = 0;
            if (k->hoverX < 0) {   // the hover box's close row is up or down
                for (int y = g->top; y <= g->bottom; y++) {
                    if (y == yLast) continue;
                    candleN += CountPx(&sc, g->left, y, g->right, y + 1, sc.sty.clr.up, TRUE) +
                               CountPx(&sc, g->left, y, g->right, y + 1, sc.sty.clr.down, TRUE);
                }
            }
            if (lineN < g->cw / 2 || candleN) {
                printf("FAIL chart types %s: %d px of the line (want >= %d), %d px of candle colors\n",
                       k->name, lineN, g->cw / 2, candleN);
                bad++;
            }
        }
        if (k->type == CHART_MOUNTAIN) {
            // Columns at the candles' centers: no fill above the line, and
            // the fill below it (the grid, the levels, the alert lines and
            // the bars behind the price stand on it, so most, not all).
            int above = 0, belowN = 0, belowFill = 0, cols = 0;
            int yEnd = g->bottom;
            if (k->volF > 0.0 && g->volBottom <= g->bottom) yEnd -= ChartVolBarsH(g) + 1;
            int step = (m.vc > 40) ? m.vc / 40 : 1;
            for (int j = m.vs + 1; j < m.vs + m.vc - 1; j += step) {
                int x = MarkX(&sc, &m, j);
                if (x < g->left || x >= g->right) continue;
                int y0 = MarkY(&sc, &m, s_candles[j - 1].close), y1 = MarkY(&sc, &m, s_candles[j].close);
                int y2 = MarkY(&sc, &m, s_candles[j + 1].close);
                int yMin = y0 < y1 ? y0 : y1, yMax = y0 > y1 ? y0 : y1;
                if (y2 < yMin) yMin = y2;
                if (y2 > yMax) yMax = y2;
                yMin -= 2 * m.w; yMax += 2 * m.w;
                cols++;
                above += CountPx(&sc, x, g->top, x + 1, yMin, sc.sty.clr.mountain, TRUE);
                if (yEnd - yMax >= 10) {
                    belowN += yEnd - yMax;
                    belowFill += CountPx(&sc, x, yMax + 1, x + 1, yEnd + 1, sc.sty.clr.mountain, TRUE);
                }
            }
            if (above || belowN == 0 || belowFill * 10 < belowN * 7) {
                printf("FAIL chart types %s: %d columns, %d px of fill above the line, %d of %d px under it\n",
                       k->name, cols, above, belowFill, belowN);
                bad++;
            }
        }
        if (k->type == CHART_OHLC && m.bodyW >= 5) {
            // Candles far enough from the crosshair's box and the view's
            // edges; ticks where the open and the close are, and nothing of
            // the candle's color where a body would be.
            int samples = 0, tickOk = 0, bodyless = 0, bodyN = 0;
            for (int j = m.vs + 1; j < m.vs + m.vc - 1; j++) {
                const Candle* c = &s_candles[j];
                COLORREF col = (c->close >= c->open) ? sc.sty.clr.up : sc.sty.clr.down;
                int cx = MarkX(&sc, &m, j), x0 = cx - m.bodyW / 2, x1 = x0 + m.bodyW - 1;
                if (x0 < g->left || x1 >= g->right) continue;
                // The hover box stands 12 px beside the crosshair, on either side.
                int boxR = ChartPx(k->dpi, 12 + HOVER_BOX_W + 2);
                if (k->hoverX >= 0 && x1 >= k->hoverX - boxR && x0 <= k->hoverX + boxR) continue;
                int yO = MarkY(&sc, &m, c->open), yC = MarkY(&sc, &m, c->close);
                samples++;
                if (PxAt(&sc, x0, yO) == col && PxAt(&sc, x1, yC) == col) tickOk++;
                int yA = (yO < yC ? yO : yC) + m.w + 1, yB = (yO < yC ? yC : yO) - m.w - 1;
                if (yB - yA >= 2) {
                    bodyN++;
                    int ym = (yA + yB) / 2;
                    if (PxAt(&sc, x0, ym) != col && PxAt(&sc, x1, ym) != col) bodyless++;
                }
            }
            if (samples == 0 || tickOk * 10 < samples * 9 || bodyN == 0 || bodyless * 10 < bodyN * 9) {
                printf("FAIL chart types %s: ticks on %d of %d bars, no body in %d of %d\n",
                       k->name, tickOk, samples, bodyless, bodyN);
                bad++;
            }
        }
        SceneClose(&sc);
    }
    // The averages' legend over the mountain stands on the background: in
    // the legend's box, no fill. Precondition (pitfall 127): the line rises
    // into the legend's rows under the legend's columns - closes drawn above
    // the legend's bottom, so without the rule the fill would be in the box.
    {
        Scene sc;
        if (!SceneOpen(&sc, FindCase("mountain_light_560x300"))) { printf("FAIL chart types: no scene\n"); return bad + 1; }
        const ChartRect* g = &sc.g;
        int dpi = sc.k->dpi, ly = g->top + ChartPx(dpi, 4), lh = ChartPx(dpi, 15);
        int lx0 = INT_MAX, lx1 = -1;
        for (int y = ly; y < ly + lh; y++)
            for (int x = g->left; x < g->right; x++) {
                COLORREF c = PxAt(&sc, x, y);
                if (c == sc.sty.clr.sma || c == sc.sty.clr.ema || c == sc.sty.clr.vwap) {
                    if (x < lx0) lx0 = x;
                    if (x > lx1) lx1 = x;
                }
            }
        int inLegend = (lx1 >= lx0) ? CountPx(&sc, lx0, ly, lx1 + 1, ly + lh, sc.sty.clr.mountain, TRUE) : -1;
        Marks m;
        MarksOf(&sc, &m);
        int reach = 0;   // closes under the legend's columns drawn above its bottom
        for (int j = m.vs; j < m.vs + m.vc; j++) {
            int x = MarkX(&sc, &m, j);
            if (x >= lx0 && x <= lx1 && MarkY(&sc, &m, s_candles[j].close) < ly + lh) reach++;
        }
        if (reach == 0 || inLegend != 0) {
            printf("FAIL chart types: %d closes rise into the legend (%d..%d), %d px of fill inside it\n",
                   reach, lx0, lx1, inLegend);
            bad++;
        } else printf("ok   chart types: %d closes rise into the legend's box, and no fill shows in it\n", reach);
        SceneClose(&sc);
    }
    if (!bad) printf("ok   chart types: %d typed cases - own marks, own scale, none drawn as candles\n", typed);
    return bad;
}

// --- Phase 49: draw time per type (--perf, not part of the run) ---
// TickC's whole buffer, 6000 1m candles, all in view at 3840x1600 - the
// densest frame the app can draw - once as a panel with the volume pane and
// the averages, once as the desktop. The types are drawn round robin, so
// the machine's state is shared; the median of 21 draws per type.
static void PerfTypes(void) {
    static const char* const NAME[CHART_TYPE_COUNT] = { "candles", "ohlc", "line", "mountain" };
    for (int desk = 0; desk < 2; desk++) {
        double ms[CHART_TYPE_COUNT][21];
        ChartStyle sty;
        Surface s;
        if (!ChartStyleCreate(&sty, 96, NULL) || !SurfaceOpen(&s, 3840, 1600)) { printf("perf: no surface\n"); return; }
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        for (int r = -2; r < 21; r++) {
            for (int t = 0; t < CHART_TYPE_COUNT; t++) {
                Case k = { "perf", 3840, 1600, desk, MIN_MS, MAX_CANDLES, FALSE, 0, 0,
                           desk ? 0.0 : 1.0, desk ? 0.0 : 1.0, -1, -1, FALSE, 96, FALSE, FALSE };
                k.type = t;
                LARGE_INTEGER a, b;
                QueryPerformanceCounter(&a);
                DrawCase(s.dc, &k, &sty, NULL, NULL);
                QueryPerformanceCounter(&b);
                if (r >= 0) ms[t][r] = (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
            }
        }
        for (int t = 0; t < CHART_TYPE_COUNT; t++) {
            for (int i = 1; i < 21; i++)
                for (int j = i; j > 0 && ms[t][j] < ms[t][j - 1]; j--) {
                    double tmp = ms[t][j]; ms[t][j] = ms[t][j - 1]; ms[t][j - 1] = tmp;
                }
            printf("perf %-7s 3840x1600, %d candles, %-8s median %.2f ms (min %.2f, max %.2f)\n",
                   desk ? "desktop" : "panel", MAX_CANDLES, NAME[t], ms[t][10], ms[t][0], ms[t][20]);
        }
        SurfaceClose(&s);
        ChartStyleDestroy(&sty);
    }
}

int main(int argc, char** argv) {
    BOOL update = FALSE, bmp = FALSE;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--update") == 0) update = TRUE;
        else if (strcmp(argv[i], "--bmp") == 0) bmp = TRUE;
        else if (strcmp(argv[i], "--perf") == 0) { PerfTypes(); return 0; }
        else { printf("usage: chart_golden [--update] [--bmp] [--perf]\n"); return 2; }
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
    int contrastFails = CheckContrast(&ChartThemeLight, "light") + CheckPriceFloor() +
                        CheckTimeAxisSmall() + CheckHoverTime() + CheckTimeForms() +
                        CheckGhostRank() + CheckGhostRow() + CheckLegendAlert() + CheckVolTag() +
                        CheckLevelLabels() + CheckChartTypes();
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

        DrawCase(s.dc, k, &sty, NULL, NULL);
        unsigned long long h = Fnv(s.px, k->W * k->H);
        hashes[i] = h;

        // The same case twice must give the same pixels: catches state that
        // leaks from one frame into the next, or reads of uninitialized memory.
        DrawCase(s.dc, k, &sty, NULL, NULL);
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

    // Phase 44: --update writes only a clean run. Under --update a case fails
    // when it is UNSTABLE, differs from the screen bitmap or could not be
    // drawn, and a hash of such a picture is no golden. Until now the file
    // was written anyway, and the next run compared against it.
    if (update && fails) {
        printf("not writing %s: %d case(s) failed, see the FAIL lines above\n",
               goldPath, fails);
    } else if (update) {
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
    if (contrastFails) printf("%d unit checks failed (contrast pairs, price floor, time axis, hover time, time forms,\n"
                              "ghost rank and row, legend alert, volume tag, level labels, chart types)\n", contrastFails);
    return (fails || contrastFails) ? 1 : 0;
}
