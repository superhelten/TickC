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
    // Phase 52: the two-row time band (PAD_B 34) moves the panes up 16 px:
    // the price pane ends at 552 and the volume pane begins at 558, so the
    // gap's pointer is 555; and the panel with both panes and a 126 px price
    // pane is 306 px high now (290 would drop the volume pane behind the
    // candles), hence the case's new name.
    { "vol_1h_gap_hover",       1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0, 700, 555, FALSE,  96, FALSE, FALSE },
    { "vol_rsi_15m_306_hover",   560, 306, FALSE, 15 * MIN_MS,  360, FALSE, 300,  0, 1.0, 1.0, 280, 107, FALSE,  96, FALSE, TRUE },
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
    { "ohlc_desktop_3840x1600", 3840,1600, TRUE,  MIN_MS,      2400, FALSE, 300,  0, 0.0, 0.0,  -1,  -1, FALSE,  96, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_OHLC },
    { "line_1h_1280x720",       1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_LINE },
    { "line_1h_hover",          1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0, 700, 300, FALSE,  96, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_LINE },
    { "line_light_15m_alerts",  1280, 720, FALSE, 15 * MIN_MS,  360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, TRUE,   96, TRUE,  FALSE, 0.0, 0,  0, FALSE, CHART_LINE },
    { "line_dpi192_1m",         2560,1440, FALSE, MIN_MS,      2400, FALSE, 300,  0, 1.0, 1.0,1400, 600, FALSE, 192, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_LINE },
    { "line_desktop_1920x1080", 1920,1080, TRUE,  MIN_MS,      2400, FALSE, 300,  0, 0.0, 0.0,  -1,  -1, FALSE,  96, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_LINE },
    { "line_desktop_3840x1600", 3840,1600, TRUE,  MIN_MS,      2400, FALSE, 300,  0, 0.0, 0.0,  -1,  -1, FALSE,  96, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_LINE },
    { "mountain_1h_1280x720",   1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_MOUNTAIN },
    { "mountain_light_1h_hover",1280, 720, FALSE, HOUR_MS,      360, FALSE, 300,  0, 1.0, 1.0, 700, 300, FALSE,  96, TRUE,  FALSE, 0.0, 0,  0, FALSE, CHART_MOUNTAIN },
    { "mountain_15m_alerts",    1280, 720, FALSE, 15 * MIN_MS,  360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, TRUE,   96, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_MOUNTAIN },
    { "mountain_dpi144_rsi",    1920,1080, FALSE, MIN_MS,      2400, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE, 144, FALSE, TRUE,  0.0, 0,  0, FALSE, CHART_MOUNTAIN },
    { "mountain_rsi_400x250",    400, 250, FALSE, 15 * MIN_MS,  360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, TRUE,  0.0, 0,  0, FALSE, CHART_MOUNTAIN },
    // A 185 px price pane: the fill's top (the highest close, 7 % under the
    // pane's top, on 15 Sep) lies under the averages' legend.
    { "mountain_light_560x300",  560, 300, FALSE, HOUR_MS,  360, FALSE, 300,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, TRUE,  FALSE, 0.0, 0,  0, FALSE, CHART_MOUNTAIN },
    // More candles than pixels: many points per column, and the fill's
    // top edge runs up and down the same column.
    { "mountain_1m_dense",      1280, 720, FALSE, MIN_MS,      2400, FALSE,   0,  0, 1.0, 1.0,  -1,  -1, FALSE,  96, FALSE, FALSE, 0.0, 0,  0, FALSE, CHART_MOUNTAIN },
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
// pictures, and a table edit that breaks it fails the run. The dark theme
// was not held to it until phase 51: its muted grays on the box (dim 3.7:1,
// yesterday's levels 4.0) are older, deliberate choices, recorded in the
// work log, and are named in DARK_ALLOW below.
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
    // Phase 51: the stamp of the line and the mountain in the series'
    // colors (Bloomberg's white box with a black number), and the view's
    // high and low labels in the chart - on the background, and on the
    // mountain's fill, where they stand without a patch.
    CP("price stamp, line/mountain", onStamp, stamp),
    CP("high/low labels",         text,      bg),
    CP("high/low labels on fill", text,      mountain),
    // Where the fill covers a level name or the averages' legend, they stand
    // on the theme's fillCell (the navy in the dark theme, bg in the light).
    CP("today's level names on fill", session, fillCell),
    CP("yesterday's names on fill",   prev,    fillCell),
    CP("SMA legend on fill",      sma,       fillCell),
    CP("EMA legend on fill",      ema,       fillCell),
    CP("VWAP legend on fill",     vwap,      fillCell),
    // Phase 52: the statistics box and the volume legend are framed boxes on
    // the box surface (Bloomberg's), their text in the text color and the
    // high/average/low glyphs in the axis color; the volume tag of the line
    // and the mountain carries its number on the series' steel blue.
    CP("statistics box text",     text,      box),
    CP("statistics box glyphs",   axis,      box),
    CP("volume legend box",       text,      box),
    CP("volume tag, line/mountain", onVolSeries, volSeries),
};

// Phase 51: the dark theme is checked too. Its pairs on the box surface are
// older, deliberate choices (phase 40 left the dark table alone) and stay
// under 4.5:1 on any background: dim on the box. They are named here with
// the ratio they have, and must not drop below it; every other dark pair
// must reach 4.5:1. The black background of phase 51 lifted the pairs on bg
// (dim 4.12 -> 4.57), and yesterday's gray, a step lighter for the navy,
// left the list (on the box 3.99 -> 4.58).
typedef struct { const char* where; double floor; } ContrastAllow;
static const ContrastAllow DARK_ALLOW[] = {
    { "ghost tag, slots full",  3.69 },
    { "hover box labels",       3.69 },
    { "overlay headings",       3.69 },
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

static int CheckContrast(const ChartTheme* t, const char* name, const ContrastAllow* allow, int nAllow) {
    int n = (int)(sizeof(CONTRAST_PAIRS) / sizeof(CONTRAST_PAIRS[0])), bad = 0, known = 0;
    double worst = 99.0;
    for (int i = 0; i < n; i++) {
        const ContrastPair* p = &CONTRAST_PAIRS[i];
        COLORREF fg = *(const COLORREF*)((const char*)t + p->text);
        COLORREF bg = *(const COLORREF*)((const char*)t + p->surface);
        double r = ContrastRatio(fg, bg), need = 4.5;
        for (int a = 0; a < nAllow; a++)
            if (strcmp(allow[a].where, p->where) == 0) { need = allow[a].floor - 0.005; known++; }
        if (need >= 4.5 && r < worst) worst = r;
        if (r < need) {
            printf("FAIL contrast %s: %s %02X%02X%02X on %02X%02X%02X is %.2f:1 (needs %.2f)\n", name, p->where,
                   GetRValue(fg), GetGValue(fg), GetBValue(fg),
                   GetRValue(bg), GetGValue(bg), GetBValue(bg), r, (need < 4.5) ? need + 0.005 : 4.5);
            bad++;
        }
    }
    if (!bad) printf("ok   contrast %s: %d text pairs, lowest %.2f:1%s\n", name, n, worst,
                     known ? ", and the named older pairs at their floors" : "");
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

// Phase 52: the 1 px frames of color edge whose top left corner lies in
// [x0, x1) x [y0, y1) - the statistics box and the volume legend are framed
// in boxEdge, as the hover box is. A corner is an edge pixel with edge to its
// right and below and none to its left and above; the frame's width and
// height are its top row's and left column's runs, and its bottom row and
// right column must be mostly edge too (a hover box drawn over part of it
// leaves most). Crosshair tags are solid boxEdge, but they stand in the price
// column, outside every range the checks scan.
static COLORREF PxOr(const Scene* sc, int x, int y) {
    if (x < 0 || y < 0 || x >= sc->s.W || y >= sc->s.H) return (COLORREF)-1;
    return PxAt(sc, x, y);
}

static int FindFrames(const Scene* sc, int x0, int y0, int x1, int y1, COLORREF edge, RECT* out, int max) {
    int n = 0, minW = ChartPx(sc->k->dpi, 24), minH = ChartPx(sc->k->dpi, 12);
    for (int y = y0; y < y1 && n < max; y++)
        for (int x = x0; x < x1 && n < max; x++) {
            if (PxOr(sc, x, y) != edge || PxOr(sc, x + 1, y) != edge || PxOr(sc, x, y + 1) != edge) continue;
            if (PxOr(sc, x - 1, y) == edge || PxOr(sc, x, y - 1) == edge) continue;
            int w = 0, h = 0;
            while (PxOr(sc, x + w, y) == edge) w++;
            while (PxOr(sc, x, y + h) == edge) h++;
            if (w < minW || h < minH) continue;
            int bot = CountPx(sc, x, y + h - 1, x + w, y + h, edge, TRUE);
            int rgt = CountPx(sc, x + w - 1, y, x + w, y + h, edge, TRUE);
            if (bot * 10 < w * 6 || rgt * 10 < h * 6) continue;
            SetRect(&out[n++], x, y, x + w, y + h);
        }
    return n;
}

// The statistics box: the frame in the price pane whose left edge stands
// PX(6) inside the plot's left edge (the hover box, the other frame there,
// is clamped to the edge itself). FALSE when there is none.
static BOOL LegendBoxOf(const Scene* sc, RECT* rc) {
    RECT fr[8];
    const ChartRect* g = &sc->g;
    int x = g->left + ChartPx(sc->k->dpi, 6);
    int n = FindFrames(sc, x, g->top, x + 1, g->bottom + 1, sc->sty.clr.boxEdge, fr, 8);
    for (int i = 0; i < n; i++)
        if (fr[i].bottom <= g->bottom + 1) { *rc = fr[i]; return TRUE; }
    return FALSE;
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
    int bad = 0, labels = 0, cases = 0, navyCells = 0;
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
        RECT lbN = { 0, 0, 0, 0 };
        BOOL hasLbN = LegendBoxOf(&sc, &lbN);
        for (int b = 0; b < nb; b++) {
            labels++;
            // Phase 52: a name gives way to the statistics box.
            RECT tmpN;
            if (hasLbN && IntersectRect(&tmpN, &box[b], &lbN)) {
                printf("FAIL level labels %s: the label at %d,%d-%d,%d is on the statistics box\n", k->name,
                       box[b].left, box[b].top, box[b].right, box[b].bottom);
                bad++;
            }
            // Phase 49: the marks of the case's chart type. The candles and
            // the OHLC bars are up and down; the line and the mountain are
            // the line - and the mountain's fill must not show inside a
            // label either: over the fill the text stands on the background
            // (its gray is not 4.5:1 on the fill in the light theme) -
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
            // Phase 51: unless the theme's fillCell is the fill (the dark
            // navy carries the grays at 4.5:1) and the label stands wholly
            // on it - fill, and no background, inside its box.
            if (k->type == CHART_MOUNTAIN) {
                int fill = CountPx(&sc, box[b].left, box[b].top, box[b].right, box[b].bottom, sc.sty.clr.mountain, TRUE);
                int bgIn = CountPx(&sc, box[b].left, box[b].top, box[b].right, box[b].bottom, sc.sty.clr.bg, TRUE);
                if (fill && (sc.sty.clr.fillCell != sc.sty.clr.mountain || bgIn)) {
                    printf("FAIL level labels %s: %d px of the mountain's fill (and %d of bg) inside the label at %d,%d-%d,%d\n",
                           k->name, fill, bgIn, box[b].left, box[b].top, box[b].right, box[b].bottom);
                    bad++;
                }
                // And the other way: a label wholly on the navy (a ring 4 px
                // outside its ink, past its cell, is mostly fill) stands on
                // the navy, not on a black patch - the phase 49 box that
                // was plain to see on the default chart.
                if (sc.sty.clr.fillCell == sc.sty.clr.mountain) {
                    int rx0 = box[b].left - 4, rx1 = box[b].right + 4, ry0 = box[b].top - 4, ry1 = box[b].bottom + 4;
                    int ring = 2 * (rx1 - rx0) + 2 * (ry1 - ry0);
                    int ringFill = CountPx(&sc, rx0, ry0, rx1, ry0 + 1, sc.sty.clr.mountain, TRUE) +
                                   CountPx(&sc, rx0, ry1 - 1, rx1, ry1, sc.sty.clr.mountain, TRUE) +
                                   CountPx(&sc, rx0, ry0, rx0 + 1, ry1, sc.sty.clr.mountain, TRUE) +
                                   CountPx(&sc, rx1 - 1, ry0, rx1, ry1, sc.sty.clr.mountain, TRUE);
                    if (ringFill * 10 >= ring * 8 && bgIn) {
                        printf("FAIL level labels %s: the label at %d,%d-%d,%d is on the navy but stands on %d px of bg\n",
                               k->name, box[b].left, box[b].top, box[b].right, box[b].bottom, bgIn);
                        bad++;
                    } else if (ringFill * 10 >= ring * 8) {
                        navyCells++;
                    }
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
    if (!bad) printf("ok   level labels: %d labels in %d cases, none struck by the price's marks, %d on a navy cell\n",
                     labels, cases, navyCells);
    // The navy-cell rule must have been exercised: the dark mountain cases
    // put LOD and PDL on the fill.
    if (navyCells == 0) { printf("FAIL level labels: no label stood wholly on the navy\n"); bad++; }
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
    m->w = sc->k->desktop ? DeskLineW(sc->k->H) : ChartPx(sc->k->dpi, 1);   // phase 49: DeskLineW
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
        // Phase 51: the fill is Bloomberg's navy under a white line, no
        // longer a blend of the line toward the background (its own check is
        // CheckBloomberg); here it must only be a color of its own.
        BOOL fillOk = (c->mountain != c->line && c->mountain != c->bg);
        if (on || !fillOk) {
            printf("FAIL chart types %s: line/mountain on %d text blend line(s), mountain %s a color of its own\n",
                   t ? "light" : "dark", on, fillOk ? "is" : "is NOT");
            bad++;
        } else printf("ok   chart types %s: line and mountain off every text role's blend line\n",
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

// --- Phase 49 (the app's half): the desktop's line follows the height ---
// DeskLineW keeps the panel's 1 px line to 16 px stamp on the desktop's own
// stamp: 1 px at 1080, 2 at 1600, 3 at 2160. And the picture: a line pixel
// of a line two pixels wide has line pixels beside it AND above or below it
// wherever the line runs, at any slope; a 1 px line has one or the other,
// both only at a bend. The 1080 case is the control - the measure has to
// tell a 1 px line from a 2 px one, or the 1600 case proves nothing.
static double LineThick(const char* name) {
    Scene sc;
    if (!SceneOpen(&sc, FindCase(name))) return -1.0;
    const ChartRect* g = &sc.g;
    COLORREF c = sc.sty.clr.line;
    int all = 0, both = 0;
    for (int y = g->top + 1; y < g->bottom; y++) {
        for (int x = g->left + 1; x < g->right - 1; x++) {
            if (PxAt(&sc, x, y) != c) continue;
            all++;
            BOOL h = PxAt(&sc, x - 1, y) == c || PxAt(&sc, x + 1, y) == c;
            BOOL v = PxAt(&sc, x, y - 1) == c || PxAt(&sc, x, y + 1) == c;
            if (h && v) both++;
        }
    }
    SceneClose(&sc);
    return all ? (double)both / (double)all : -1.0;
}

// The OHLC bars' strokes follow the same width (DeskLineW). Most of a bar is
// its vertical high-low stroke: in candle colors, a pixel of a 2 px stroke
// has a pixel of its color beside it, one of a 1 px stroke only on a tick.
static double OhlcThick(const char* name) {
    Scene sc;
    if (!SceneOpen(&sc, FindCase(name))) return -1.0;
    const ChartRect* g = &sc.g;
    int all = 0, beside = 0;
    for (int y = g->top + 1; y < g->bottom; y++) {
        for (int x = g->left + 1; x < g->right - 1; x++) {
            COLORREF c = PxAt(&sc, x, y);
            if (c != sc.sty.clr.up && c != sc.sty.clr.down) continue;
            all++;
            if (PxAt(&sc, x - 1, y) == c || PxAt(&sc, x + 1, y) == c) beside++;
        }
    }
    SceneClose(&sc);
    return all ? (double)beside / (double)all : -1.0;
}

static int CheckDeskLine(void) {
    int bad = 0;
    if (DeskLineW(1080) != 1 || DeskLineW(1600) != 2 || DeskLineW(2160) != 3 || DeskLineW(600) != 1) {
        printf("FAIL desktop line: DeskLineW gives %d %d %d %d at 600, 1080, 1600, 2160 px (want 1 1 2 3)\n",
               DeskLineW(600), DeskLineW(1080), DeskLineW(1600), DeskLineW(2160));
        bad++;
    }
    double t1 = LineThick("line_desktop_1920x1080"), t2 = LineThick("line_desktop_3840x1600");
    if (t1 < 0.0 || t2 < 0.0 || t1 > 0.4 || t2 < 0.8) {
        printf("FAIL desktop line: %.2f of the line's pixels are thick at 1080 (want <= 0.40, 1 px), %.2f at 1600 (want >= 0.80, 2 px)\n",
               t1, t2);
        bad++;
    } else printf("ok   desktop line: 1 px at 1080 (%.2f of its pixels thick), 2 px at 1600 (%.2f)\n", t1, t2);
    double o1 = OhlcThick("ohlc_desktop_1920x1080"), o2 = OhlcThick("ohlc_desktop_3840x1600");
    if (o1 < 0.0 || o2 < 0.0 || o1 > 0.6 || o2 < 0.9) {
        printf("FAIL desktop OHLC: %.2f of the bars' pixels have a neighbor beside them at 1080 (want <= 0.60, 1 px), %.2f at 1600 (want >= 0.90, 2 px)\n",
               o1, o2);
        bad++;
    } else printf("ok   desktop OHLC: 1 px strokes at 1080 (%.2f beside), 2 px at 1600 (%.2f)\n", o1, o2);
    return bad;
}

// --- Phase 51: the Bloomberg GIP chart area ---
// The reference is the user's screenshot of Bloomberg's GIP Standard Chart
// (SPX Index, 1Y, mountain): a black background, a navy fill under a white
// line, a dotted grid in both directions, a price axis with a line and ticks,
// and the last price in a white box with a black number.

// The theme's own properties, both tables. HEXRGB prints a COLORREF as
// RRGGBB, the way the tables and chart.h write colors.
#define HEXRGB(c) ((unsigned)((GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c)))
static int CheckBloombergTheme(void) {
    int bad = 0;
    const ChartTheme* d = &ChartThemeDark;
    const ChartTheme* l = &ChartThemeLight;
#define MINC(c) min(min(GetRValue(c), GetGValue(c)), GetBValue(c))
#define MAXC(c) max(max(GetRValue(c), GetGValue(c)), GetBValue(c))
    COLORREF m = d->mountain;
    BOOL navy = GetRValue(m) <= 0x10 && GetGValue(m) >= 0x10 && GetGValue(m) <= 0x30 &&
                GetBValue(m) >= 0x28 && GetBValue(m) <= 0x48 && GetBValue(m) > GetGValue(m) &&
                GetGValue(m) > GetRValue(m);
    COLORREF lm = l->mountain;
    BOOL lightBlue = GetRValue(lm) >= 0xC8 && GetBValue(lm) >= GetGValue(lm) &&
                     GetGValue(lm) >= GetRValue(lm) && GetBValue(lm) - GetRValue(lm) >= 0x10;
    BOOL ok = d->bg == RGB(0, 0, 0) && MINC(d->line) >= 0xF0 && navy &&
              MINC(d->stamp) >= 0xF0 && MAXC(d->onStamp) <= 0x10 &&
              lightBlue && MAXC(l->stamp) <= 0x70 && GetBValue(l->stamp) > GetRValue(l->stamp) &&
              MINC(l->onStamp) >= 0xF0;
    if (!ok) {
        printf("FAIL bloomberg theme: dark bg %06X (want 000000), line %06X (white), mountain %06X (navy), stamp %06X on %06X "
               "(white, black); light mountain %06X (light blue), stamp %06X on %06X (navy, white)\n",
               HEXRGB(d->bg), HEXRGB(d->line), HEXRGB(m), HEXRGB(d->stamp), HEXRGB(d->onStamp),
               HEXRGB(lm), HEXRGB(l->stamp), HEXRGB(l->onStamp));
        bad++;
    } else printf("ok   bloomberg theme: black bg, white line on a navy fill, white stamp; light blue fill, navy stamp\n");
#undef MINC
#undef MAXC
    // Pitfall 87 for every line and surface a probe counts, both themes: none
    // on the blend line from bg to a text color. On black every darker copy
    // of a text color is on it - the old blends toward the background
    // (the bars behind the price, the alert line) had to leave it.
    const ChartTheme* th[2] = { d, l };
    for (int t = 0; t < 2; t++) {
        const ChartTheme* c = th[t];
        const COLORREF text[] = { c->text, c->dim, c->axis, c->up, c->down, c->session, c->prev,
                                  c->sma, c->ema, c->vwap, c->rsi, c->alert, c->alertText,
                                  c->onAlert, c->onHot, c->quote, c->onAccent,
                                  t ? RGB(0x1F, 0x23, 0x28) : RGB(0xFF, 0xFF, 0xFF) };   // the watermark's ink
        const COLORREF role[] = { c->grid, c->gridDot, c->axisLine, c->cross, c->volUp, c->volDown,
                                  c->volPaneUp, c->volPaneDown, c->alertLine, c->line, c->mountain,
                                  c->volSeries };
        static const char* const NAME[] = { "grid", "gridDot", "axisLine", "cross", "volUp", "volDown",
                                            "volPaneUp", "volPaneDown", "alertLine", "line", "mountain",
                                            "volSeries" };
        int nt = (int)(sizeof(text) / sizeof(text[0])), on = 0;
        for (int r = 0; r < (int)(sizeof(role) / sizeof(role[0])); r++)
            for (int q = 0; q < nt; q++)
                if (role[r] == text[q] || OnBlendLine(c->bg, text[q], role[r])) {
                    printf("FAIL bloomberg theme %s: %s %06X lies on the blend line of text color %06X\n",
                           t ? "light" : "dark", NAME[r], HEXRGB(role[r]), HEXRGB(text[q]));
                    on++;
                }
        // The grid's dots stay weaker than the crosshair's: the dotted line
        // that follows the hand is the one to read.
        BOOL weaker = ContrastRatio(c->gridDot, c->bg) < ContrastRatio(c->cross, c->bg);
        if (!weaker) printf("FAIL bloomberg theme %s: the grid's dots %06X are not weaker than the crosshair %06X\n",
                            t ? "light" : "dark", HEXRGB(c->gridDot), HEXRGB(c->cross));
        if (on || !weaker) bad++;
        else printf("ok   bloomberg theme %s: lines and surfaces off every text blend line, grid dots under the crosshair\n",
                    t ? "light" : "dark");
    }
    return bad;
}

// The engine's hover box, as ChartDrawBody places it (FALSE: none drawn).
static BOOL HoverBoxOf(const Scene* sc, RECT* rc) {
    const ChartRect* g = &sc->g;
    int n = sc->in.count, dpi = sc->k->dpi;
    if (sc->st.hoverIdx < 0 || sc->st.hoverIdx >= n) return FALSE;
    double dc = (sc->st.dispCount < 1.0) ? 1.0 : sc->st.dispCount;
    double hrel = (double)sc->st.hoverIdx - sc->st.dispStart;
    if (hrel < 0.0 || hrel >= dc) return FALSE;
    double slot = (double)g->cw / dc;
    int hx = g->left + (int)((hrel + 0.5) * slot), hy = sc->st.hoverY;
    BOOL volPane = g->volBottom > g->bottom, bandOn = g->bandBottom > g->bandTop;
    BOOL inVol = volPane && hy > g->bottom && hy <= g->volBottom;
    BOOL inBand = bandOn && hy > (volPane ? g->volBottom : g->bottom);
    int lo = inBand ? g->bandTop : inVol ? g->volTop : g->top;
    int hi = inBand ? g->bandBottom : inVol ? g->volBottom : g->bottom;
    if (hy < lo) hy = lo;
    if (hy > hi) hy = hi;
    int rows = 6 + ((sc->st.dispIndF > 0.0) ? 3 : 0) + ((bandOn && sc->st.dispRsiF > 0.0) ? 1 : 0);
    int boxW = ChartPx(dpi, HOVER_BOX_W), boxH = ChartPx(dpi, 4) + rows * ChartPx(dpi, 13) + ChartPx(dpi, 5);
    int bx = hx + ChartPx(dpi, 12);
    if (bx + boxW > g->right) bx = hx - ChartPx(dpi, 12) - boxW;
    if (bx < g->left) bx = g->left;
    int by = hy - boxH / 2;
    if (by + boxH > g->bottom) by = g->bottom - boxH;
    if (by < g->top) by = g->top;
    SetRect(rc, bx, by, bx + boxW, by + boxH);
    return TRUE;
}

// The dotted grid: a horizontal of single pixels (no two side by side), and
// verticals of single pixels at the time labels - each over a label, and
// none on the desktop, which has no time axis to anchor them to.
static int GridRowDots(const Scene* sc, int y, int* adj) {
    int n = 0;
    *adj = 0;
    for (int x = sc->g.left; x < sc->g.edge; x++) {
        if (PxAt(sc, x, y) != sc->sty.clr.gridDot) continue;
        n++;
        if (x + 1 < sc->g.edge && PxAt(sc, x + 1, y) == sc->sty.clr.gridDot) (*adj)++;
    }
    return n;
}

static int GridColumns(const Scene* sc, int* labeled, int* adjacent, int* inVol) {
    const ChartRect* g = &sc->g;
    int cols = 0;
    *labeled = 0; *adjacent = 0; *inVol = 0;
    int axisB = ChartPanesBottom(g);
    for (int x = g->left; x < g->right; x++) {
        int c = 0, adj = 0;
        for (int y = g->top + 1; y < g->bottom; y++) {
            BOOL gridRow = FALSE;
            for (int i = 0; i <= 4; i++) if (y == g->top + (g->ch * i) / 4) gridRow = TRUE;
            if (gridRow || PxAt(sc, x, y) != sc->sty.clr.gridDot) continue;
            c++;
            BOOL nextRow = FALSE;   // a dot of the row below is no neighbor
            for (int i = 0; i <= 4; i++) if (y + 1 == g->top + (g->ch * i) / 4) nextRow = TRUE;
            if (!nextRow && PxAt(sc, x, y + 1) == sc->sty.clr.gridDot) adj++;
        }
        if (c < g->ch / 12) continue;
        cols++;
        if (adj) (*adjacent)++;
        if (!sc->k->desktop &&
            CountPx(sc, x - 40, axisB + 1, x + 41, sc->s.H, sc->sty.clr.axis, TRUE) > 0) (*labeled)++;
        if (g->volBottom > g->bottom &&
            CountPx(sc, x, g->volTop + 1, x + 1, g->volBottom, sc->sty.clr.gridDot, TRUE) > 0) (*inVol)++;
    }
    return cols;
}

static int CheckBloombergGrid(void) {
    int bad = 0;
    static const char* const ROWS[] = { "panel_1h_1280x720", "mountain_1h_1280x720", "light_1h_hover",
                                        "dpi144_1h_1920x1080", "vol_rsi_15m_560x300" };
    for (int i = 0; i < (int)(sizeof(ROWS) / sizeof(ROWS[0])); i++) {
        Scene sc;
        if (!SceneOpen(&sc, FindCase(ROWS[i]))) { printf("FAIL bloomberg grid %s: no scene\n", ROWS[i]); bad++; continue; }
        int y = sc.g.top + sc.g.ch / 4, adj = 0;
        int n = GridRowDots(&sc, y, &adj);
        int want = (sc.g.edge - sc.g.left) / 10;
        if (n < want || adj) {
            printf("FAIL bloomberg grid %s: row %d has %d dot pixels (want >= %d), %d side by side (want 0)\n",
                   ROWS[i], y, n, want, adj);
            bad++;
        } else printf("ok   bloomberg grid %s: row %d dotted, %d single pixels\n", ROWS[i], y, n);
        int labeled, adjacent, inVol;
        int cols = GridColumns(&sc, &labeled, &adjacent, &inVol);
        BOOL volWant = sc.g.volBottom > sc.g.bottom;
        // Through the volume pane: most columns show dots there - the pane's
        // legend box and a bar as tall as the pane can hide a column's few.
        if (cols < 3 || labeled != cols || adjacent || (volWant && inVol * 10 < cols * 8)) {
            printf("FAIL bloomberg grid %s: %d dotted verticals (want >= 3), %d over a time label, %d not dotted, %d through the volume pane\n",
                   ROWS[i], cols, labeled, adjacent, inVol);
            bad++;
        } else printf("ok   bloomberg grid %s: %d dotted verticals, each over its time label%s\n", ROWS[i], cols,
                      volWant ? " and through the volume pane" : "");
        SceneClose(&sc);
    }
    // The desktop: no time labels, so no verticals.
    Scene sd;
    if (!SceneOpen(&sd, FindCase("line_desktop_1920x1080"))) { printf("FAIL bloomberg grid: no scene\n"); return bad + 1; }
    int labeled, adjacent, inVol;
    int cols = GridColumns(&sd, &labeled, &adjacent, &inVol);
    if (cols) { printf("FAIL bloomberg grid desktop: %d verticals without a time axis\n", cols); bad++; }
    else printf("ok   bloomberg grid desktop: no verticals\n");
    SceneClose(&sd);
    return bad;
}

// The price column as an axis: a line on column edge down each pane, and a
// tick PX(3) long at each price label drawn. None on the desktop.
static int CheckBloombergAxis(void) {
    int bad = 0;
    static const char* const AX[] = { "panel_1h_1280x720", "ohlc_dpi144_1h", "light_15m_alerts",
                                      "vol_rsi_15m_560x300", "mountain_1h_1280x720" };
    for (int i = 0; i < (int)(sizeof(AX) / sizeof(AX[0])); i++) {
        Scene sc;
        if (!SceneOpen(&sc, FindCase(AX[i]))) { printf("FAIL bloomberg axis %s: no scene\n", AX[i]); bad++; continue; }
        const ChartRect* g = &sc.g;
        COLORREF a = sc.sty.clr.axisLine;
        int e = g->edge, dpi = sc.k->dpi;
        int onPrice = CountPx(&sc, e, g->top, e + 1, g->bottom + 1, a, TRUE), wantP = g->bottom - g->top + 1 - 2;
        int onVol = 0, wantV = 0, onBand = 0, wantB = 0;
        if (g->volBottom > g->bottom) {
            onVol = CountPx(&sc, e, g->volTop, e + 1, g->volBottom + 1, a, TRUE);
            wantV = g->volBottom - g->volTop + 1 - 2;
        }
        if (g->bandBottom > g->bandTop) {
            onBand = CountPx(&sc, e, g->bandTop, e + 1, g->bandBottom + 1, a, TRUE);
            wantB = g->bandBottom - g->bandTop + 1 - 2;
        }
        int ticks = 0, tickLen = ChartPx(dpi, 3);
        for (int q = 0; q <= 4; q++) {
            int y = g->top + (g->ch * q) / 4;
            if (CountPx(&sc, e + 1, y, e + 1 + tickLen, y + 1, a, TRUE) == tickLen) ticks++;
        }
        if (onPrice < wantP || onVol < wantV || onBand < wantB || ticks < 2) {
            printf("FAIL bloomberg axis %s: line %d of %d px in the price pane, %d of %d in the volume pane, %d of %d in the band, %d ticks (want >= 2)\n",
                   AX[i], onPrice, wantP, onVol, wantV, onBand, wantB, ticks);
            bad++;
        } else printf("ok   bloomberg axis %s: the line down every pane, %d ticks\n", AX[i], ticks);
        SceneClose(&sc);
    }
    Scene sd;
    if (!SceneOpen(&sd, FindCase("line_desktop_1920x1080"))) { printf("FAIL bloomberg axis: no scene\n"); return bad + 1; }
    int onD = CountPx(&sd, sd.g.edge, 0, sd.g.edge + 1, sd.s.H, sd.sty.clr.axisLine, TRUE);
    if (onD) { printf("FAIL bloomberg axis desktop: %d px of an axis line\n", onD); bad++; }
    else printf("ok   bloomberg axis desktop: no axis line\n");
    SceneClose(&sd);
    return bad;
}

// The stamp: white with a black number for the line and the mountain on the
// panel (navy with a white one in the light theme); up/down with bg for the
// candles, the bars and every type on the desktop.
static int CheckBloombergStamp(void) {
    int bad = 0;
    static const struct { const char* name; BOOL series; } ST[] = {
        { "line_1h_1280x720", TRUE }, { "mountain_1h_1280x720", TRUE }, { "line_light_15m_alerts", TRUE },
        { "mountain_light_1h_hover", TRUE }, { "mountain_dpi144_rsi", TRUE },
        { "panel_1h_1280x720", FALSE }, { "ohlc_1h_1280x720", FALSE },
        { "line_desktop_1920x1080", FALSE }, { "mountain_desktop_3840x1600", FALSE },
    };
    for (int i = 0; i < (int)(sizeof(ST) / sizeof(ST[0])); i++) {
        Scene sc;
        if (!SceneOpen(&sc, FindCase(ST[i].name))) { printf("FAIL bloomberg stamp %s: no scene\n", ST[i].name); bad++; continue; }
        Marks m;
        MarksOf(&sc, &m);
        int n = sc.in.count, dpi = sc.k->dpi;
        int yl = MarkY(&sc, &m, s_candles[n - 1].close);
        int half = sc.k->desktop ? DeskPillH(sc.k->H) / 2 : ChartPx(dpi, 8);
        int x0 = sc.g.edge + 1, x1 = sc.k->W - ChartPx(dpi, AXIS_PAD_R) + ChartPx(dpi, 3);
        int area = (x1 - x0) * 2 * half;
        int nS = CountPx(&sc, x0, yl - half, x1, yl + half, sc.sty.clr.stamp, TRUE);
        int nO = CountPx(&sc, x0, yl - half, x1, yl + half, sc.sty.clr.onStamp, TRUE);
        int nUD = CountPx(&sc, x0, yl - half, x1, yl + half, sc.sty.clr.up, TRUE) +
                  CountPx(&sc, x0, yl - half, x1, yl + half, sc.sty.clr.down, TRUE);
        BOOL ok = ST[i].series ? (nS * 2 >= area && nO > 10 && nUD == 0) : (nUD * 2 >= area);
        if (!ok) {
            printf("FAIL bloomberg stamp %s: %d px of stamp color, %d of its text, %d of up/down in %d (want %s)\n",
                   ST[i].name, nS, nO, nUD, area, ST[i].series ? "the series' white/navy box" : "up/down");
            bad++;
        } else printf("ok   bloomberg stamp %s: %s\n", ST[i].name,
                      ST[i].series ? "the series' box with its number" : "up/down, as before");
        SceneClose(&sc);
    }
    return bad;
}

// The view's high and low as labels in the chart (panel only). Present beside
// their points in the plain cases; in every panel case each label - a group
// of text-colored pixels in the price pane outside the hover box - is clear
// of the price's marks, the legend, the level names, the alert lines, the
// hover box, and stays left of the plot's right edge (so off the stamp and
// the tags in the column).
static int HiLoWindow(const Scene* sc, BOOL high) {
    Marks m;
    MarksOf(sc, &m);
    const ChartRect* g = &sc->g;
    int dpi = sc->k->dpi, best = -1;
    BOOL closes = (sc->k->type == CHART_LINE || sc->k->type == CHART_MOUNTAIN);
    double bv = 0.0;
    for (int j = (int)floor(m.dStart); j < m.dStart + m.vc + 1 && j < sc->in.count; j++) {
        if (j < 0) continue;
        int x = MarkX(sc, &m, j);
        if (x < g->left || x >= g->right) continue;
        double v = closes ? s_candles[j].close : (high ? s_candles[j].high : s_candles[j].low);
        if (best < 0 || (high ? v > bv : v < bv)) { best = j; bv = v; }
    }
    if (best < 0) return 0;
    int cx = MarkX(sc, &m, best), cy = MarkY(sc, &m, bv);
    int y0 = high ? cy - ChartPx(dpi, 24) : cy - ChartPx(dpi, 12);
    int y1 = high ? cy + ChartPx(dpi, 12) : cy + ChartPx(dpi, 24);
    if (y0 < g->top) y0 = g->top;
    if (y1 > g->bottom + 1) y1 = g->bottom + 1;
    return CountPx(sc, cx - ChartPx(dpi, 100), y0, cx + ChartPx(dpi, 100), y1, sc->sty.clr.text, TRUE);
}

// A label's cell is opaque: a line or a mark it would stand on is cut, not
// in its pixels (a mutation that let the labels onto them changed 15 and 5
// goldens and no pixel check). So the collisions are computed from the
// frame's own state, with the engine's functions. The rows drawn across the
// plot, each from the x it starts at: the levels from today's first candle,
// the alerts, the ghost's and the afterglow's from the left, and the last
// price's from its candle.
static int PlotRows(const Scene* sc, int* rows, int* x0s, int max) {
    const ChartRect* g = &sc->g;
    const Case* k = sc->k;
    Marks m;
    MarksOf(sc, &m);
    int n = 0;
    if (sc->st.dispIndF > 0.0) {
        BOOL full = FALSE;
        int s0 = SessionStart(s_candles, k->n, k->ivMs, k->histDone, &full);
        int xs = (s0 >= 0) ? g->left + (int)floor(((double)s0 - m.dStart) * m.slot) : g->edge;
        if (s0 >= 0 && full && xs < g->edge) {
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
            for (int q = 0; q < np && n < max; q++) {
                double yy = ((m.maxP - p[q]) / m.range) * (double)g->ch;
                if (yy < 0.0 || yy > (double)g->ch) continue;
                rows[n] = g->top + (int)yy; x0s[n++] = (xs > g->left) ? xs : g->left;
            }
        }
    }
    for (int a = 0; a < sc->in.alertCount && n < max; a++) {
        int y = AlertY(&sc->st, g, fabs(sc->in.alerts[a]));
        if (y >= g->top && y <= g->bottom) { rows[n] = y; x0s[n++] = g->left; }
    }
    if (sc->in.axisHotY >= g->top && sc->in.axisHotY <= g->bottom && n < max &&
        (sc->in.alertHot < 0 || sc->in.alertHot >= sc->in.alertCount)) {
        int y = AlertY(&sc->st, g, AlertPriceAtY(&sc->st, g, sc->in.axisHotY));
        if (y >= g->top && y <= g->bottom) { rows[n] = y; x0s[n++] = g->left; }
    }
    if (sc->in.alertFlashF > 0.0 && n < max) {
        int y = AlertY(&sc->st, g, sc->in.alertFlashLevel);
        if (y >= g->top && y <= g->bottom) { rows[n] = y; x0s[n++] = g->left; }
    }
    if (n < max) {
        int xl = MarkX(sc, &m, sc->in.count - 1);
        if (xl < g->left) xl = g->left;
        if (xl > g->right) xl = g->right;
        rows[n] = MarkY(sc, &m, s_candles[sc->in.count - 1].close); x0s[n++] = xl;
    }
    return n;
}

// Does the chart type's mark of any candle in view reach into r? The candles'
// wick column and body, the bars' stroke and ticks, the close line's
// segments (sampled every half pixel).
static BOOL MarksInRect(const Scene* sc, const RECT* r) {
    Marks m;
    MarksOf(sc, &m);
    const ChartRect* g = &sc->g;
    int n = sc->in.count, t = sc->k->type;
    int j0 = (int)floor(m.dStart) - 1, j1 = (int)ceil(m.dStart + sc->st.dispCount) + 1;
    if (j0 < 0) j0 = 0;
    if (j1 > n - 1) j1 = n - 1;
    RECT tmp;
    for (int j = j0; j <= j1; j++) {
        const Candle* c = &s_candles[j];
        if (t == CHART_LINE || t == CHART_MOUNTAIN) {
            if (j == j1) break;
            double x0 = g->left + floor(((double)j - m.dStart + 0.5) * m.slot);
            double x1 = g->left + floor(((double)j + 1 - m.dStart + 0.5) * m.slot);
            double y0 = MarkY(sc, &m, c->close), y1 = MarkY(sc, &m, s_candles[j + 1].close);
            double len = fabs(x1 - x0) + fabs(y1 - y0);
            int steps = (int)(len * 2.0) + 1;
            for (int s = 0; s <= steps; s++) {
                double f = (double)s / steps;
                int x = (int)floor(x0 + (x1 - x0) * f), y = (int)floor(y0 + (y1 - y0) * f);
                if (x >= r->left && x < r->right && y >= r->top && y < r->bottom) return TRUE;
            }
            continue;
        }
        int cx = MarkX(sc, &m, j), xb = cx - m.bodyW / 2;
        int yH = MarkY(sc, &m, c->high), yL = MarkY(sc, &m, c->low);
        int yO = MarkY(sc, &m, c->open), yC = MarkY(sc, &m, c->close);
        RECT stroke, body;
        SetRect(&stroke, cx - m.w / 2, yH, cx - m.w / 2 + ((t == CHART_OHLC) ? m.w : 1), yL + 1);
        if (IntersectRect(&tmp, &stroke, r)) return TRUE;
        if (t == CHART_OHLC) {
            SetRect(&body, xb, yO - m.w / 2, xb + m.bodyW, yO - m.w / 2 + m.w);
            if (IntersectRect(&tmp, &body, r)) return TRUE;
            SetRect(&body, xb, yC - m.w / 2, xb + m.bodyW, yC - m.w / 2 + m.w);
        } else {
            SetRect(&body, xb, min(yO, yC), xb + m.bodyW, max(max(yO, yC), min(yO, yC) + 1));
        }
        if (IntersectRect(&tmp, &body, r)) return TRUE;
    }
    return FALSE;
}

static int CheckBloombergHiLo(void) {
    int bad = 0;
    static const char* const PRESENT[] = { "panel_1h_1280x720", "ohlc_1h_1280x720", "line_1h_1280x720",
                                           "mountain_1h_1280x720", "dpi144_1h_1920x1080", "rsi_light_1h" };
    for (int i = 0; i < (int)(sizeof(PRESENT) / sizeof(PRESENT[0])); i++) {
        Scene sc;
        if (!SceneOpen(&sc, FindCase(PRESENT[i]))) { printf("FAIL bloomberg high/low %s: no scene\n", PRESENT[i]); bad++; continue; }
        int h = HiLoWindow(&sc, TRUE), l = HiLoWindow(&sc, FALSE);
        if (h < 10 || l < 10) {
            printf("FAIL bloomberg high/low %s: %d text px by the high, %d by the low (want >= 10 each)\n", PRESENT[i], h, l);
            bad++;
        } else printf("ok   bloomberg high/low %s: labels by the high (%d px) and the low (%d px)\n", PRESENT[i], h, l);
        SceneClose(&sc);
    }
    int labels = 0, cases = 0;
    for (int i = 0; i < NCASES; i++) {
        const Case* k = &CASES[i];
        if (k->desktop) continue;
        Scene sc;
        if (!SceneOpen(&sc, k)) { printf("FAIL bloomberg high/low %s: no scene\n", k->name); bad++; continue; }
        cases++;
        const ChartRect* g = &sc.g;
        RECT hb = { 0, 0, 0, 0 };
        BOOL hasBox = HoverBoxOf(&sc, &hb);
        // Phase 52: the statistics box's text is the text color too; its
        // frame is found in the picture, and a label must keep out of it.
        RECT lb = { 0, 0, 0, 0 };
        BOOL hasLb = LegendBoxOf(&sc, &lb);
        RECT box[8];
        int nb = 0, maxH = ChartPx(k->dpi, 16), joinX = ChartPx(k->dpi, 24), joinY = ChartPx(k->dpi, 16);
        for (int y = g->top; y <= g->bottom; y++) {
            for (int x = g->left; x < g->edge; x++) {
                if (hasBox && x >= hb.left && x < hb.right && y >= hb.top && y < hb.bottom) continue;
                if (hasLb && x >= lb.left && x < lb.right && y >= lb.top && y < lb.bottom) continue;
                if (PxAt(&sc, x, y) != sc.sty.clr.text) continue;
                // One label is one group: "H 63030.12" has a space, so the
                // pixels join across 24 px at 96 dpi (the level names' check uses 12).
                int b = 0;
                for (; b < nb; b++)
                    if (x >= box[b].left - joinX && x < box[b].right + joinX &&
                        y >= box[b].top - joinY && y < box[b].bottom + joinY && y + 1 - box[b].top <= maxH) break;
                if (b == nb) {
                    if (nb == 8) continue;
                    SetRect(&box[nb], x, y, x + 1, y + 1);
                    nb++;
                } else {
                    if (x < box[b].left) box[b].left = x;
                    if (x + 1 > box[b].right) box[b].right = x + 1;
                    if (y + 1 > box[b].bottom) box[b].bottom = y + 1;
                }
            }
        }
        // The scan is row by row, so the top row of a label's far digits can
        // open a group of its own before the rows below join the near ones:
        // merge groups that the same rule would have joined.
        for (BOOL merged = TRUE; merged; ) {
            merged = FALSE;
            for (int a = 0; a < nb && !merged; a++)
                for (int b = a + 1; b < nb && !merged; b++) {
                    int t0 = min(box[a].top, box[b].top), b1 = max(box[a].bottom, box[b].bottom);
                    if (box[b].left < box[a].right + joinX && box[a].left < box[b].right + joinX &&
                        box[b].top < box[a].bottom + joinY && box[a].top < box[b].bottom + joinY && b1 - t0 <= maxH) {
                        UnionRect(&box[a], &box[a], &box[b]);
                        box[b] = box[--nb];
                        merged = TRUE;
                    }
                }
        }
        if (nb > 2) { printf("FAIL bloomberg high/low %s: %d text groups in the price pane (want <= 2)\n", k->name, nb); bad++; }
        for (int b = 0; b < nb; b++) {
            labels++;
            const RECT* r = &box[b];
            int marks;
            if (k->type == CHART_LINE || k->type == CHART_MOUNTAIN)
                marks = CountPx(&sc, r->left, r->top, r->right, r->bottom, sc.sty.clr.line, TRUE);
            else
                marks = CountPx(&sc, r->left, r->top, r->right, r->bottom, sc.sty.clr.up, TRUE) +
                        CountPx(&sc, r->left, r->top, r->right, r->bottom, sc.sty.clr.down, TRUE);
            int others = 0;
            const COLORREF oc[] = { sc.sty.clr.sma, sc.sty.clr.ema, sc.sty.clr.vwap, sc.sty.clr.session,
                                    sc.sty.clr.prev, sc.sty.clr.alertLine, sc.sty.clr.stamp };
            for (int q = 0; q < (int)(sizeof(oc) / sizeof(oc[0])); q++)
                others += CountPx(&sc, r->left, r->top, r->right, r->bottom, oc[q], TRUE);
            RECT grown = hb, tmp;
            InflateRect(&grown, 1, 1);
            BOOL onBox = hasBox && IntersectRect(&tmp, r, &grown);
            RECT lg = lb;
            InflateRect(&lg, 1, 1);
            if (hasLb && IntersectRect(&tmp, r, &lg)) onBox = TRUE;
            // The geometry: the ink grown by two rows each way lies inside
            // the label's cell and the row above it, which the engine keeps
            // free of lines (HiLoFree); marks in the ink itself.
            int rows[LVL_COUNT + 8], x0s[LVL_COUNT + 8];
            int nr = PlotRows(&sc, rows, x0s, LVL_COUNT + 8), lined = -1;
            int d2 = ChartPx(k->dpi, 2);
            for (int q = 0; q < nr; q++)
                if (rows[q] >= r->top - d2 && rows[q] < r->bottom + d2 && r->right > x0s[q]) lined = rows[q];
            BOOL onMarks = MarksInRect(&sc, r);
            if (marks || others || onBox || r->right > g->right || lined >= 0 || onMarks) {
                printf("FAIL bloomberg high/low %s: label at %d,%d-%d,%d has %d px of the price's marks, %d of the legend,"
                       " levels, alert lines or stamp, %s the hover box or the statistics box, right edge %d (plot ends at %d),"
                       " line row %d through its cell, marks under it %d\n",
                       k->name, r->left, r->top, r->right, r->bottom, marks, others, onBox ? "touches" : "clear of",
                       r->right, g->right, lined, onMarks);
                bad++;
            }
        }
        SceneClose(&sc);
    }
    if (!bad) printf("ok   bloomberg high/low: %d labels in %d panel cases, none struck or covering\n", labels, cases);
    return bad;
}

// The black background everywhere the dark theme draws: not one pixel of the
// old 0D1117 left, and the navy under the mountain's line.
static int CheckBloombergBg(void) {
    int bad = 0;
    static const char* const BG[] = { "panel_1h_1280x720", "mountain_1h_1280x720", "rsi_1h_band_hover",
                                      "desktop_1m_1920x1080", "line_desktop_3840x1600" };
    for (int i = 0; i < (int)(sizeof(BG) / sizeof(BG[0])); i++) {
        Scene sc;
        if (!SceneOpen(&sc, FindCase(BG[i]))) { printf("FAIL bloomberg bg %s: no scene\n", BG[i]); bad++; continue; }
        int old = CountPx(&sc, 0, 0, sc.s.W, sc.s.H, RGB(0x0D, 0x11, 0x17), TRUE);
        int blk = CountPx(&sc, 0, 0, sc.s.W, sc.s.H, RGB(0, 0, 0), TRUE);
        if (old || blk * 4 < sc.s.W * sc.s.H) {
            printf("FAIL bloomberg bg %s: %d px of the old 0D1117, %d of black in %d\n", BG[i], old, blk, sc.s.W * sc.s.H);
            bad++;
        } else printf("ok   bloomberg bg %s: black, none of the old background\n", BG[i]);
        SceneClose(&sc);
    }
    return bad;
}

// --- Phase 52: the Bloomberg look, part 2 - legends and axes ---
// The same screenshot is the answer key: the statistics box in the price
// pane (Last Price, High on, Average, Low on, with the overlays as rows), the
// volume pane in one steel blue with a white average line, a framed legend
// and a blue tag, the time axis in two rows with a separator where the
// coarse unit changes, and a narrow proportional font for the axis numbers.

// The view's statistics against an independent count: the candles whose
// middle is in the plot, the high and low in the prices the axis scales on,
// the mean close.
static int CheckViewStats(void) {
    int bad = 0;
    static const char* const VS[] = { "panel_1h_1280x720", "mountain_1h_1280x720", "panel_1h_panned_zoomed",
                                       "range_1y_1d", "ohlc_1m_dense" };
    for (int i = 0; i < (int)(sizeof(VS) / sizeof(VS[0])); i++) {
        Scene sc;
        if (!SceneOpen(&sc, FindCase(VS[i]))) { printf("FAIL view stats %s: no scene\n", VS[i]); bad++; continue; }
        Marks m;
        MarksOf(&sc, &m);
        BOOL closes = (sc.k->type == CHART_LINE || sc.k->type == CHART_MOUNTAIN);
        int cnt = 0, ih = -1, il = -1;
        double hi = 0, lo = 0, sum = 0;
        for (int j = 0; j < sc.in.count; j++) {
            double fx = ((double)j - m.dStart + 0.5) * m.slot;
            if (fx < 0.0 || fx >= (double)sc.g.cw) continue;
            double hv = closes ? s_candles[j].close : s_candles[j].high, lv = closes ? s_candles[j].close : s_candles[j].low;
            if (ih < 0 || hv > hi) { ih = j; hi = hv; }
            if (il < 0 || lv < lo) { il = j; lo = lv; }
            sum += s_candles[j].close;
            cnt++;
        }
        ChartViewStat vs;
        BOOL ok = ChartViewStats(s_candles, sc.in.count, sc.st.dispStart, sc.st.dispCount, sc.g.cw, sc.k->type, &vs);
        if (!ok || vs.count != cnt || vs.iHigh != ih || vs.iLow != il || fabs(vs.avg - sum / cnt) > 1e-6 ||
            vs.high != hi || vs.low != lo) {
            printf("FAIL view stats %s: %d candles, high #%d %.2f, low #%d %.2f, avg %.4f (want %d, #%d %.2f, #%d %.2f, %.4f)\n",
                   VS[i], vs.count, vs.iHigh, vs.high, vs.iLow, vs.low, vs.avg, cnt, ih, hi, il, lo, sum / cnt);
            bad++;
        } else printf("ok   view stats %s: %d candles, high, low and the mean close\n", VS[i], cnt);
        SceneClose(&sc);
    }
    return bad;
}

// The statistics box. Where it is expected, its rows (bit 0 Last Price, 1
// High, 2 Average, 3 Low, 4 SMA, 5 EMA, 6 VWAP, in that order down the box):
// the box's height is exactly its rows, and each row has its text and its
// mark - the series' color square for Last Price (the stamp's white or navy
// for the line and the mountain, up or down for the candles and the bars),
// a T for the high (the bar on top), a -o- for the average, an inverted T
// for the low (the bar at the bottom), and the overlays' color squares.
// In every panel case with a box: a left corner of the price pane, no more
// than LGD_MAX_PCT of the plot's width and of the pane's height, lower-left
// unless the price's marks enter there and not the upper-left.
static int RowBits(int mask, int* order) {
    int n = 0;
    for (int b = 0; b < 7; b++) if (mask & (1 << b)) order[n++] = b;
    return n;
}

static int CheckStatsBox(void) {
    int bad = 0;
    static const struct { const char* name; int rows; } SB[] = {
        { "mountain_1h_1280x720", 0x7F }, { "panel_1h_1280x720", 0x7F }, { "line_1h_1280x720", 0x7F },
        { "ohlc_1h_1280x720", 0x7F }, { "panel_1h_overlays_off", 0x0F }, { "light_1h_hover", 0x7F },
        { "dpi144_1h_1920x1080", 0x7F }, { "panel_15m_560x300", 0x75 }, { "mountain_light_560x300", 0x75 },
        { "panel_1h_400x250", 0 }, { "desktop_1m_1920x1080", 0 }, { "line_desktop_1920x1080", 0 },
    };
    for (int i = 0; i < (int)(sizeof(SB) / sizeof(SB[0])); i++) {
        Scene sc;
        if (!SceneOpen(&sc, FindCase(SB[i].name))) { printf("FAIL statistics box %s: no scene\n", SB[i].name); bad++; continue; }
        int dpi = sc.k->dpi;
        RECT b = { 0, 0, 0, 0 };
        BOOL has = !sc.k->desktop && LegendBoxOf(&sc, &b);
        if (!SB[i].rows) {
            if (has) { printf("FAIL statistics box %s: a box at %d,%d-%d,%d where none fits\n", SB[i].name, b.left, b.top, b.right, b.bottom); bad++; }
            else printf("ok   statistics box %s: none, as the size rule says\n", SB[i].name);
            SceneClose(&sc);
            continue;
        }
        if (!has) { printf("FAIL statistics box %s: no box\n", SB[i].name); bad++; SceneClose(&sc); continue; }
        int order[7], nr = RowBits(SB[i].rows, order), fails = 0;
        int wantH = ChartPx(dpi, LGD_PAD_T) + nr * ChartPx(dpi, LGD_ROW_H) + ChartPx(dpi, LGD_PAD_B);
        if (b.bottom - b.top != wantH) {
            printf("FAIL statistics box %s: %d px tall, %d rows want %d\n", SB[i].name, b.bottom - b.top, nr, wantH);
            fails++;
        }
        int sw = ChartPx(dpi, LGD_SWATCH);
        int sx0 = b.left + ChartPx(dpi, LGD_PAD_X), sx1 = sx0 + sw;
        BOOL series = (sc.k->type == CHART_LINE || sc.k->type == CHART_MOUNTAIN);
        for (int r = 0; r < nr; r++) {
            int y0 = b.top + ChartPx(dpi, LGD_PAD_T) + r * ChartPx(dpi, LGD_ROW_H), y1 = y0 + ChartPx(dpi, LGD_ROW_H);
            int text = CountPx(&sc, sx1, y0, b.right - 1, y1, sc.sty.clr.text, TRUE);
            int mark = 0, want = sw * sw * 7 / 10;
            const char* what = "";
            switch (order[r]) {
            case 0:
                mark = series ? CountPx(&sc, sx0, y0, sx1, y1, sc.sty.clr.stamp, TRUE)
                              : CountPx(&sc, sx0, y0, sx1, y1, sc.sty.clr.up, TRUE) + CountPx(&sc, sx0, y0, sx1, y1, sc.sty.clr.down, TRUE);
                what = "the series' square";
                break;
            case 1: case 3: {
                // The glyph's bar: the top (High) or the bottom (Low) row of
                // its axis-colored pixels is the widest.
                int top = -1, bot = -1;
                for (int y = y0; y < y1; y++)
                    if (CountPx(&sc, sx0, y, sx1, y + 1, sc.sty.clr.axis, TRUE)) { if (top < 0) top = y; bot = y; }
                int wTop = (top >= 0) ? CountPx(&sc, sx0, top, sx1, top + 1, sc.sty.clr.axis, TRUE) : 0;
                int wBot = (bot >= 0) ? CountPx(&sc, sx0, bot, sx1, bot + 1, sc.sty.clr.axis, TRUE) : 0;
                mark = (order[r] == 1) ? (wTop > wBot ? wTop : 0) : (wBot > wTop ? wBot : 0);
                want = sw / 2;
                what = (order[r] == 1) ? "a T" : "an inverted T";
                break;
            }
            case 2:
                mark = CountPx(&sc, sx0, y0, sx1, y1, sc.sty.clr.axis, TRUE);
                want = sw / 2;
                what = "the average's glyph";
                break;
            case 4: mark = CountPx(&sc, sx0, y0, sx1, y1, sc.sty.clr.sma, TRUE); what = "SMA's square"; break;
            case 5: mark = CountPx(&sc, sx0, y0, sx1, y1, sc.sty.clr.ema, TRUE); what = "EMA's square"; break;
            case 6: mark = CountPx(&sc, sx0, y0, sx1, y1, sc.sty.clr.vwap, TRUE); what = "VWAP's square"; break;
            }
            if (text < 8 || mark < want) {
                printf("FAIL statistics box %s: row %d (bit %d) has %d text px, %d px of %s (want >= %d)\n",
                       SB[i].name, r, order[r], text, mark, what, want);
                fails++;
            }
        }
        if (fails) bad++;
        else printf("ok   statistics box %s: %d rows, each with its text and its mark\n", SB[i].name, nr);
        SceneClose(&sc);
    }
    // The rules, in every panel case that has a box.
    int boxes = 0, upper = 0;
    for (int i = 0; i < NCASES; i++) {
        const Case* k = &CASES[i];
        if (k->desktop) continue;
        Scene sc;
        if (!SceneOpen(&sc, k)) { bad++; continue; }
        RECT b;
        if (LegendBoxOf(&sc, &b)) {
            boxes++;
            const ChartRect* g = &sc.g;
            int dpi = k->dpi, w = b.right - b.left, h = b.bottom - b.top;
            RECT ll = { g->left + ChartPx(dpi, LGD_INSET_X), g->bottom - ChartPx(dpi, LGD_INSET_Y) - h, 0, g->bottom - ChartPx(dpi, LGD_INSET_Y) };
            RECT ul = { ll.left, g->top + ChartPx(dpi, LGD_INSET_Y), 0, g->top + ChartPx(dpi, LGD_INSET_Y) + h };
            ll.right = ul.right = ll.left + w;
            BOOL atLL = EqualRect(&b, &ll), atUL = EqualRect(&b, &ul);
            BOOL inLL = MarksInRect(&sc, &ll), inUL = MarksInRect(&sc, &ul);
            BOOL sizeOk = w * 100 <= g->cw * LGD_MAX_PCT && h * 100 <= g->ch * LGD_MAX_PCT;
            BOOL cornerOk = atLL ? (!inLL || inUL) : (atUL && inLL && !inUL);
            if (atUL) upper++;
            if (!sizeOk || !(atLL || atUL) || !cornerOk) {
                printf("FAIL statistics box %s: %d,%d-%d,%d (%dx%d in a %dx%d plot), %s; the price's marks %s the lower-left, %s the upper-left\n",
                       k->name, b.left, b.top, b.right, b.bottom, w, h, g->cw, g->ch,
                       atLL ? "lower-left" : atUL ? "upper-left" : "in no corner",
                       inLL ? "enter" : "keep out of", inUL ? "enter" : "keep out of");
                bad++;
            }
        }
        SceneClose(&sc);
    }
    printf("%s statistics box: %d panel cases with a box, %d of them in the upper-left\n", boxes ? "ok  " : "FAIL", boxes, upper);
    if (!boxes) bad++;
    return bad;
}

// The alert line through the statistics box: the box is opaque over it, as
// the one-line legend was (phase 46, F8). Precondition: the alert's row is
// inside the box.
static int CheckLegendAlert(void) {
    int bad = 0;
    Scene sc;
    if (!SceneOpen(&sc, FindCase("legend_alert_row"))) { printf("FAIL legend alert: no scene\n"); return 1; }
    int ya = AlertY(&sc.st, &sc.g, fabs(sc.in.alerts[0]));
    RECT b;
    if (!LegendBoxOf(&sc, &b) || ya <= b.top || ya >= b.bottom - 1) {
        printf("FAIL legend alert: the alert's row %d is not inside a statistics box\n", ya);
        SceneClose(&sc);
        return 1;
    }
    int n = CountPx(&sc, b.left, ya, b.right, ya + 1, sc.sty.clr.alertLine, TRUE);
    int beyond = CountPx(&sc, b.right, ya, sc.g.edge, ya + 1, sc.sty.clr.alertLine, TRUE);
    if (n || beyond == 0) {
        printf("FAIL legend alert: %d px of the alert line (row %d) inside the box, %d px beyond it\n", n, ya, beyond);
        bad++;
    } else printf("ok   legend alert: the line (row %d) stops at the statistics box, %d px of it beyond\n", ya, beyond);
    SceneClose(&sc);
    return bad;
}

// The volume pane. The line and the mountain: every bar in the steel blue,
// none in up/down; the candles and the bars keep up/down. In every panel
// case a white (light: navy) average line across the pane, a framed legend
// in its top left corner with a swatch of the bars' color and its text, and
// the tag in the column filled with the series' blue (the candles: the
// stamp's up or down). The desktop keeps its muted bars, no line.
static int CheckVolumePane(void) {
    int bad = 0;
    static const char* const VP[] = { "mountain_1h_1280x720", "line_1h_1280x720", "mountain_light_1h_hover",
                                      "panel_1h_1280x720", "ohlc_1h_1280x720", "vol_light_rsi_hover", "dpi144_1h_1920x1080" };
    for (int i = 0; i < (int)(sizeof(VP) / sizeof(VP[0])); i++) {
        Scene sc;
        if (!SceneOpen(&sc, FindCase(VP[i]))) { printf("FAIL volume pane %s: no scene\n", VP[i]); bad++; continue; }
        const ChartRect* g = &sc.g;
        int dpi = sc.k->dpi, vt = g->volTop, vb = g->volBottom;
        BOOL series = (sc.k->type == CHART_LINE || sc.k->type == CHART_MOUNTAIN);
        RECT hb = { 0, 0, 0, 0 };
        HoverBoxOf(&sc, &hb);
        int blue = CountPx(&sc, g->left, vt + 1, g->right, vb + 1, sc.sty.clr.volSeries, TRUE);
        int ud = CountPx(&sc, g->left, vt + 1, g->right, vb + 1, sc.sty.clr.volPaneUp, TRUE) +
                 CountPx(&sc, g->left, vt + 1, g->right, vb + 1, sc.sty.clr.volPaneDown, TRUE);
        BOOL colorsOk = series ? (blue > g->cw && ud == 0) : (ud > g->cw && blue == 0);
        // The average line: columns of the pane with a pixel of the line's
        // color (the hover box can hang into the pane: its columns aside).
        int cols = 0, colsAll = 0;
        for (int x = g->left + ChartPx(dpi, 60); x < g->right - ChartPx(dpi, 10); x++) {
            if (x >= hb.left && x < hb.right) continue;
            colsAll++;
            if (CountPx(&sc, x, vt + 1, x + 1, vb + 1, sc.sty.clr.line, TRUE)) cols++;
        }
        RECT fr[4];
        int nf = FindFrames(&sc, g->left, vt, g->left + ChartPx(dpi, 12), vt + ChartPx(dpi, 12), sc.sty.clr.boxEdge, fr, 4);
        int swatch = 0, ltext = 0;
        if (nf) {
            int sx0 = fr[0].left + ChartPx(dpi, LGD_PAD_X), sw = ChartPx(dpi, LGD_SWATCH);
            swatch = series ? CountPx(&sc, sx0, fr[0].top, sx0 + sw, fr[0].bottom, sc.sty.clr.volSeries, TRUE)
                            : CountPx(&sc, sx0, fr[0].top, sx0 + sw, fr[0].bottom, sc.sty.clr.volPaneUp, TRUE) +
                              CountPx(&sc, sx0, fr[0].top, sx0 + sw, fr[0].bottom, sc.sty.clr.volPaneDown, TRUE);
            ltext = CountPx(&sc, sx0 + sw, fr[0].top, fr[0].right, fr[0].bottom, sc.sty.clr.text, TRUE);
        }
        int sw2 = ChartPx(dpi, LGD_SWATCH) * ChartPx(dpi, LGD_SWATCH) * 7 / 10;
        int x0 = g->edge + 1, x1 = sc.axR + ChartPx(dpi, 3);
        int tag = series ? CountPx(&sc, x0, vt, x1, vb + 1, sc.sty.clr.volSeries, TRUE)
                         : CountPx(&sc, x0, vt, x1, vb + 1, sc.sty.clr.up, TRUE) + CountPx(&sc, x0, vt, x1, vb + 1, sc.sty.clr.down, TRUE);
        int tagWant = (x1 - x0) * ChartPx(dpi, 16) / 2;
        if (!colorsOk || cols * 10 < colsAll * 9 || nf != 1 || swatch < sw2 || ltext < 10 || tag < tagWant) {
            printf("FAIL volume pane %s: %d px blue and %d up/down in the bars (want %s), average line in %d of %d columns,"
                   " %d framed legends (swatch %d px, text %d px), tag %d px (want >= %d)\n",
                   VP[i], blue, ud, series ? "all blue" : "up/down", cols, colsAll, nf, swatch, ltext, tag, tagWant);
            bad++;
        } else printf("ok   volume pane %s: %s bars, the average line across, a framed legend, the %s tag\n",
                      VP[i], series ? "steel blue" : "up/down", series ? "blue" : "up/down");
        SceneClose(&sc);
    }
    Scene sd;
    if (!SceneOpen(&sd, FindCase("vol_desktop_1920x1080"))) { printf("FAIL volume pane desktop: no scene\n"); return bad + 1; }
    {
        int H = sd.s.H;
        int muted = CountPx(&sd, 0, 0, sd.s.W, H, sd.sty.clr.volUp, TRUE) + CountPx(&sd, 0, 0, sd.s.W, H, sd.sty.clr.volDown, TRUE);
        int blue = CountPx(&sd, 0, 0, sd.s.W, H, sd.sty.clr.volSeries, TRUE);
        int line = CountPx(&sd, 0, sd.g.volTop, sd.s.W, sd.g.volBottom + 1, sd.sty.clr.line, TRUE);
        if (muted < 1000 || blue || line) {
            printf("FAIL volume pane desktop: %d px of the muted bars, %d of blue, %d of an average line\n", muted, blue, line);
            bad++;
        } else printf("ok   volume pane desktop: the muted bars (%d px), no blue, no line\n", muted);
    }
    SceneClose(&sd);
    return bad;
}

// The time axis in two rows. Per case the coarse unit expected; the test
// finds the unit's boundaries itself (FileTimeToSystemTime on the candles'
// open times, local under 1d, UTC from 1d) and wants a separator in the
// coarse row at each boundary in the plot and nowhere else, every coarse
// label inside one span and centered in it, fine labels at least the
// spacing apart, and on 1Y each fine label on the first candle of a month.
static BOOL UnitKey(long long t, long long ivMs, int unit, long long* key) {
    long long off = (ivMs >= DAY_MS) ? 0 : UTC_OFFSET_MS;
    ULONGLONG ft = (ULONGLONG)((t + off) / 1000) * 10000000ULL + 116444736000000000ULL;
    FILETIME f;
    f.dwLowDateTime = (DWORD)(ft & 0xFFFFFFFF);
    f.dwHighDateTime = (DWORD)(ft >> 32);
    SYSTEMTIME s;
    if (!FileTimeToSystemTime(&f, &s)) return FALSE;
    *key = (unit == CHART_TUNIT_DAY) ? (long long)s.wYear * 10000 + s.wMonth * 100 + s.wDay
         : (unit == CHART_TUNIT_MONTH) ? (long long)s.wYear * 100 + s.wMonth : (long long)s.wYear;
    return TRUE;
}

// Columns in [x0, x1) with a pixel that is neither bg nor the axis line in
// rows [y0, y1), grouped where they lie at most join px apart.
static int InkGroups(const Scene* sc, int x0, int x1, int y0, int y1, int join, int* gl, int* gr, int max) {
    int n = 0, last = -1000;
    for (int x = x0; x < x1; x++) {
        int ink = 0;
        for (int y = y0; y < y1 && !ink; y++) {
            COLORREF c = PxAt(sc, x, y);
            if (c != sc->sty.clr.bg && c != sc->sty.clr.axisLine) ink = 1;
        }
        if (!ink) continue;
        if (n > 0 && x - last <= join) gr[n - 1] = x + 1;
        else if (n < max) { gl[n] = x; gr[n] = x + 1; n++; }
        last = x;
    }
    return n;
}

static int CheckTimeRows(void) {
    int bad = 0;
    static const struct { const char* name; int unit; } TR[] = {
        { "panel_15m_1280x720", CHART_TUNIT_DAY }, { "panel_1m_1280x720", CHART_TUNIT_DAY },
        { "panel_1h_1280x720", CHART_TUNIT_MONTH }, { "panel_1h_400x250", CHART_TUNIT_MONTH },
        { "range_1y_1d", CHART_TUNIT_YEAR }, { "range_5y_1w", CHART_TUNIT_YEAR },
        { "panel_1d_1280x720", CHART_TUNIT_YEAR }, { "dpi144_1h_1920x1080", CHART_TUNIT_MONTH },
        { "dpi192_1m_2560x1440", CHART_TUNIT_DAY }, { "vol_rsi_15m_560x300", CHART_TUNIT_MONTH },
    };
    for (int i = 0; i < (int)(sizeof(TR) / sizeof(TR[0])); i++) {
        Scene sc;
        if (!SceneOpen(&sc, FindCase(TR[i].name))) { printf("FAIL time rows %s: no scene\n", TR[i].name); bad++; continue; }
        const ChartRect* g = &sc.g;
        int dpi = sc.k->dpi, axisB = ChartPanesBottom(g), H = sc.s.H;
        int r1 = axisB + ChartPx(dpi, TIME_ROW_TOP), r2 = r1 + ChartPx(dpi, TIME_ROW_PITCH);
        // The ink: the fine row's descenders reach into the coarse row's
        // cell, whose capitals begin a few rows down it - the rows split
        // there.
        int rInk = r2 + ChartPx(dpi, 2) + 1;
        Marks m;
        MarksOf(&sc, &m);
        // The boundaries of the unit in the plot.
        int sepX[64], nSep = 0;
        int j0 = (int)floor(m.dStart), j1 = (int)ceil(m.dStart + sc.st.dispCount);
        if (j0 < 0) j0 = 0;
        if (j1 > sc.in.count) j1 = sc.in.count;
        long long prev = 0, key = 0;
        for (int j = j0; j < j1; j++) {
            UnitKey(s_candles[j].openTime, sc.k->ivMs, TR[i].unit, &key);
            if (j > j0 && key != prev && j > 0) {
                int x = g->left + (int)floor(((double)j - m.dStart) * m.slot);
                if (x > g->left && x < g->right && nSep < 64) sepX[nSep++] = x;
            }
            prev = key;
        }
        // The separators drawn: columns of the coarse row with a run of the
        // axis line's color.
        int gotX[64], nGot = 0, fails = 0;
        for (int x = g->left; x <= g->right; x++)
            if (CountPx(&sc, x, r2, x + 1, H, sc.sty.clr.axisLine, TRUE) >= ChartPx(dpi, 8) && nGot < 64) gotX[nGot++] = x;
        BOOL sepOk = (nGot == nSep);
        for (int s = 0; s < nSep && sepOk; s++) if (gotX[s] != sepX[s]) sepOk = FALSE;
        if (!sepOk) {
            printf("FAIL time rows %s: %d separators drawn, %d unit boundaries in the plot (first at %d, drawn %d)\n",
                   TR[i].name, nGot, nSep, nSep ? sepX[0] : -1, nGot ? gotX[0] : -1);
            fails++;
        }
        // The coarse row: every label inside one span, centered in it.
        int gl[64], gr[64];
        int ng = InkGroups(&sc, g->left, g->right + 1, rInk, H, ChartPx(dpi, 12), gl, gr, 64);
        if (ng == 0) { printf("FAIL time rows %s: nothing in the coarse row (rows %d-%d)\n", TR[i].name, r2, H); fails++; }
        for (int q = 0; q < ng; q++) {
            int a = g->left, b = g->right;
            for (int s = 0; s < nSep; s++) { if (sepX[s] <= gl[q]) a = sepX[s]; else if (sepX[s] >= gr[q]) { b = sepX[s]; break; } }
            int cGot = (gl[q] + gr[q]) / 2, cWant = (a + b) / 2;
            if (abs(cGot - cWant) > ChartPx(dpi, 4) || gl[q] < a || gr[q] > b + 1) {
                printf("FAIL time rows %s: coarse label %d-%d not centered in its span %d-%d\n", TR[i].name, gl[q], gr[q], a, b);
                fails++;
            }
        }
        // The fine row: labels at least the spacing apart.
        int nf = InkGroups(&sc, g->left, g->right + 1, r1, rInk, ChartPx(dpi, 6), gl, gr, 64);
        int minDx = ChartPx(dpi, TIME_DX_MIN) - 2;
        if (nf == 0) { printf("FAIL time rows %s: no fine labels\n", TR[i].name); fails++; }
        for (int q = 1; q < nf; q++)
            if ((gl[q] + gr[q]) / 2 - (gl[q - 1] + gr[q - 1]) / 2 < minDx) {
                printf("FAIL time rows %s: fine labels at %d and %d closer than %d px\n", TR[i].name,
                       (gl[q - 1] + gr[q - 1]) / 2, (gl[q] + gr[q]) / 2, minDx);
                fails++;
                break;
            }
        // 1Y: each fine label on a month's first candle.
        if (strcmp(TR[i].name, "range_1y_1d") == 0) {
            int on = 0;
            for (int q = 0; q < nf; q++) {
                int c = (gl[q] + gr[q]) / 2;
                for (int j = j0 + 1; j < j1; j++) {
                    long long k0, k1;
                    UnitKey(s_candles[j - 1].openTime, sc.k->ivMs, CHART_TUNIT_MONTH, &k0);
                    UnitKey(s_candles[j].openTime, sc.k->ivMs, CHART_TUNIT_MONTH, &k1);
                    if (k0 != k1 && abs(MarkX(&sc, &m, j) - c) <= ChartPx(dpi, 3)) { on++; break; }
                }
            }
            if (nf < 6 || on != nf) { printf("FAIL time rows %s: %d of %d fine labels on a month's first candle\n", TR[i].name, on, nf); fails++; }
        }
        if (fails) bad++;
        else printf("ok   time rows %s: %d fine labels, %d coarse, %d separators where the unit changes\n", TR[i].name, nf, ng, nSep);
        SceneClose(&sc);
    }
    return bad;
}

// The axis font: a narrow proportional face with tabular digits at every
// dpi, so the numbers still line up; the price column's text room holds a
// six-digit price with its cents ("888888.88"), which Lucida Console's 9 px
// cells did not; and the time band holds two rows of its cells.
static int CheckAxisFont(void) {
    int bad = 0;
    static const int DPIS[3] = { 96, 144, 192 };
    HDC dc = CreateCompatibleDC(NULL);
    if (!dc) { printf("FAIL axis font: no DC\n"); return 1; }
    for (int d = 0; d < 3; d++) {
        ChartStyle sty;
        if (!ChartStyleCreate(&sty, DPIS[d], NULL)) { printf("FAIL axis font: no style\n"); bad++; continue; }
        HGDIOBJ oldF = SelectObject(dc, sty.fontAxis);
        wchar_t face[64];
        GetTextFaceW(dc, 64, face);
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        int dmin = 999, dmax = 0;
        for (wchar_t c = L'0'; c <= L'9'; c++) {
            SIZE s = { 0, 0 };
            GetTextExtentPoint32W(dc, &c, 1, &s);
            if (s.cx < dmin) dmin = s.cx;
            if (s.cx > dmax) dmax = s.cx;
        }
        SIZE w9 = { 0, 0 };
        GetTextExtentPoint32W(dc, L"888888.88", 9, &w9);
        SelectObject(dc, oldF);
        ChartStyleDestroy(&sty);
        int room = ChartAxisW(DPIS[d]) - ChartPx(DPIS[d], AXIS_LBL_GAP) - ChartPx(DPIS[d], AXIS_PAD_R);
        int band = ChartPx(DPIS[d], TIME_ROW_TOP) + ChartPx(DPIS[d], TIME_ROW_PITCH) + tm.tmHeight;
        BOOL rowsApart = tm.tmHeight - tm.tmInternalLeading <= ChartPx(DPIS[d], TIME_ROW_PITCH);
        if (wcscmp(face, L"Arial") != 0 || dmin != dmax || w9.cx > room || band > ChartPx(DPIS[d], PAD_B) || !rowsApart) {
            printf("FAIL axis font at %d dpi: %ls, digits %d-%d px, \"888888.88\" %d px in %d, two rows need %d of PAD_B %d, ink %d in a %d px row\n",
                   DPIS[d], face, dmin, dmax, w9.cx, room, band, ChartPx(DPIS[d], PAD_B),
                   (int)(tm.tmHeight - tm.tmInternalLeading), ChartPx(DPIS[d], TIME_ROW_PITCH));
            bad++;
        } else printf("ok   axis font at %d dpi: %ls, tabular %d px digits, \"888888.88\" %d px in %d, two rows in %d of %d\n",
                      DPIS[d], face, dmin, w9.cx, room, band, ChartPx(DPIS[d], PAD_B));
    }
    DeleteDC(dc);
    return bad;
}

static int CheckPhase52(void) {
    return CheckViewStats() + CheckStatsBox() + CheckVolumePane() + CheckTimeRows() + CheckAxisFont();
}

static int CheckBloomberg(void) {
    return CheckBloombergTheme() + CheckBloombergBg() + CheckBloombergGrid() + CheckBloombergAxis() +
           CheckBloombergStamp() + CheckBloombergHiLo();
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
    int contrastFails = CheckContrast(&ChartThemeLight, "light", NULL, 0) +
                        CheckContrast(&ChartThemeDark, "dark", DARK_ALLOW,
                                      (int)(sizeof(DARK_ALLOW) / sizeof(DARK_ALLOW[0]))) +
                        CheckPriceFloor() +
                        CheckTimeAxisSmall() + CheckHoverTime() + CheckTimeForms() +
                        CheckGhostRank() + CheckGhostRow() + CheckLegendAlert() + CheckVolTag() +
                        CheckLevelLabels() + CheckChartTypes() + CheckDeskLine() + CheckBloomberg() +
                        CheckPhase52();
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
                              "ghost rank and row, legend alert, volume tag, level labels, chart types, bloomberg,\n"
                              "view stats, statistics box, volume pane, time rows, axis font)\n", contrastFails);
    return (fails || contrastFails) ? 1 : 0;
}
