// chart.c - TickC's chart engine (phase 34). See chart.h. Moved out of
// tickc.c verbatim; the comments carry the phase numbers from the work log.
#include "chart.h"
#include <stdio.h>
#include <math.h>
#include <limits.h>

// Volume bars (phase 21) are drawn with PolyPolygon in batches: one call per
// 256 bars instead of one FillRect per candle. Measured at 1280x720 with 300
// visible candles: FillRect per candle added 0.40 ms to a 1.44 ms repaint.
// Static, not stack - like the rest of the buffers in the file.
//
// Moving averages (phase 25) borrow s_volPts for Polyline, in batches of
// IND_BATCH points. The bars are finished drawing when the lines begin, and
// everything happens on the UI thread under the same lock, so the two cannot
// meet.
#define VOL_BATCH 256
#define IND_BATCH (VOL_BATCH * 4)
static POINT s_volPts[VOL_BATCH * 4];
static INT   s_volCnt[VOL_BATCH];

// Keeps the view within the data.
void ClampView(ChartState* ctx, int n) {
    if (n <= 0) { ctx->viewStart = 0; ctx->viewCount = 0; return; }
    if (ctx->viewCount < MIN_VIEW) ctx->viewCount = MIN_VIEW;
    if (ctx->viewCount > n)        ctx->viewCount = n;
    if (ctx->viewStart > n - ctx->viewCount) ctx->viewStart = n - ctx->viewCount;
    if (ctx->viewStart < 0)        ctx->viewStart = 0;
}

// Reads out the current view, with "show all" as the default.
void GetView(const ChartState* ctx, int n, int* vs, int* vc) {
    if (ctx->viewCount <= 0) { *vs = 0; *vc = n; }
    else                     { *vs = ctx->viewStart; *vc = ctx->viewCount; }
}

// Step length S (in candles) between the time labels.
//   N = floor(chartW / minDx),  M = ceil(dispCount),
//   S = max(1, ceil((M - 1) / (N - 1)))
// CEIL, not floor: with floor, M = 9, chartW = 320, minDx = 80 gives S = 2 and
// 71 px between the labels - collision. With ceil the spacing S * chartW /
// dispCount >= minDx for all M and N >= 2 (M >= N: (M-1)N >= (N-1)M; M < N:
// S = 1 and one candle is already wider than minDx).
//
// Phase 44: N <= 2 takes the spacing rule itself, S = ceil(M * minDx /
// chartW), so S * chartW / dispCount >= M * minDx / M = minDx. With N = 2 the
// formula above gave S = M - 1, the whole view, and NiceTimeStep rounded it
// up past the view: 300 1h candles in a 400 px panel (chartW 296, minDx 111)
// got S = 299 -> 336 (14 days in a 12.5-day view), and the time axis stood
// empty on the smallest panels. Now S = 113 -> 168 (7 days), 166 px apart.
// N < 2 gives S >= M: one label at most, as before. N >= 3 is unchanged -
// that formula is the one the phase 11 test proved exhaustively.
int TimeTickStep(double dispCount, int chartW, int minDx) {
    if (dispCount < 1.0) dispCount = 1.0;
    if (chartW <= 0 || minDx <= 0) return 1;
    int m = (int)ceil(dispCount);
    int nx = chartW / minDx;
    if (nx <= 2) {
        int s2 = (int)(((long long)m * minDx + chartW - 1) / chartW);
        return (s2 < 1) ? 1 : s2;
    }
    int s = (m - 1 + (nx - 1) - 1) / (nx - 1);
    return (s < 1) ? 1 : s;
}

// Rounds S up to a step that is a whole number of candles AND a round time
// span (5 min, 15 min, 1 h, 6 h, 1 d ...). TimeTickStep alone gives S = 23 on
// 300 1m candles at 1280 px, i.e. labels at 02:48, 03:11, 03:34 (seen in
// PrintWindow). Rounding up can only make the spacing LARGER, so the
// collision guarantee in TimeTickStep holds. If S is larger than the table,
// S is used as is.
int NiceTimeStep(int step, long long intervalMs) {
    static const long long NICE_MIN[] = {
        1, 2, 3, 5, 10, 15, 20, 30, 60, 120, 180, 240, 360, 480, 720,
        1440, 2 * 1440, 3 * 1440, 7 * 1440, 14 * 1440, 28 * 1440,
        // Phase 41: the ranges show a year of 1d candles and five years of
        // 1w. 30 and 60 days suit 1d; 91, 182 and 364 days are 13, 26 and
        // 52 weeks, so they suit 1w as well.
        30 * 1440, 60 * 1440, 91 * 1440, 182 * 1440, 364 * 1440,
    };
    if (step < 1) step = 1;
    if (intervalMs <= 0) return step;
    for (int i = 0; i < (int)(sizeof(NICE_MIN) / sizeof(NICE_MIN[0])); ++i) {
        long long ms = NICE_MIN[i] * 60000LL;
        if (ms % intervalMs != 0) continue;
        if (ms / intervalMs >= step) return (int)(ms / intervalMs);
    }
    return step;
}

// Rounds a level pointed out with the mouse to the largest power of ten that
// is not larger than one pixel in price (pxStep), so the alert moves less
// than a pixel from where it was set, but reads 75120 and not 75123.4567. The
// floor is 0.01: nothing finer is shown anywhere (pitfall 16 - the threshold
// is computed from pixels, not from a fixed dollar amount).
//
// A staircase, not pow(10, floor(log10(x))): those two calls alone added
// 28 KB to the exe (187 -> 216 KB, measured - the CRT's pow with tables), for
// a rounding that has nine possible answers. Below 1 it divides by 10 or 100
// instead of multiplying by 0.1 or 0.01, which do not exist exactly:
// 751235 / 10 is correctly rounded, 751235 * 0.1 is 75123.500000000015.
double AlertRound(double price, double pxStep) {
    if (price <= 0.0 || pxStep <= 0.0) return price;
    double r;
    if (pxStep >= 1.0) {
        double q = 1.0;
        while (q * 10.0 <= pxStep && q < 1.0e6) q *= 10.0;
        r = floor(price / q + 0.5) * q;
    } else {
        double inv = (pxStep >= 0.1) ? 10.0 : 100.0;
        r = floor(price * inv + 0.5) / inv;
    }
    return (r > 0.0) ? r : price;
}

// Moving averages (phase 25). A step machine, not a table: the averages are
// not stored anywhere. The painting feeds the candles through IndStep and
// draws the value as it comes out, so the overlay costs 40 bytes of stack and
// no double[MAX_CANDLES] next to candles[].
//
// SMA: rolling sum over the last period closes. c is the WHOLE buffer, not
// just the candle, because the step must subtract the candle that drops out
// of the window. The machine can be started on any candle; the value is
// defined from the period-th candle it has been fed onward.
// EMA: seeded with the SMA of the first period candles, then
// v += k * (close - v) with k = 2 / (period + 1) - the usual definition
// (TradingView, Binance). EMA has infinite memory, so it is ALWAYS fed from
// candle 0: started in the middle of the buffer, the line would depend on
// where the view begins, and move during panning.
//
// Returns TRUE when s->val is defined. No pow, no log: only
// + - * /, so the CRT does not grow (pitfall 75).
typedef struct { int period; BOOL ema; int fed; double sum; double val; } IndState;

static void IndInit(IndState* s, int period, BOOL ema) {
    s->period = (period > 0) ? period : 1;
    s->ema = ema;
    s->fed = 0;
    s->sum = 0.0;
    s->val = 0.0;
}

static BOOL IndStep(IndState* s, const Candle* c, int i) {
    double x = c[i].close;
    s->fed++;
    if (s->fed <= s->period) {
        s->sum += x;
        if (s->fed < s->period) return FALSE;
        s->val = s->sum / (double)s->period;
        return TRUE;
    }
    if (s->ema) {
        s->val += (2.0 / (double)(s->period + 1)) * (x - s->val);
    } else {
        s->sum += x - c[i - s->period].close;
        s->val = s->sum / (double)s->period;
    }
    return TRUE;
}

// First candle the machine must be fed from for the value on candle idx (and
// all after it) to be the correct one: 0 for EMA, idx - period + 1 for SMA.
static int IndFeedStart(int idx, int period, BOOL ema) {
    if (period < 1) period = 1;   // same guard as IndInit, otherwise nothing is fed
    int s = ema ? 0 : idx - period + 1;
    return (s > 0) ? s : 0;
}

#ifdef TICKER_PROBE
// Test build only: the average on one candle, FALSE when it is not defined
// there (too few candles before it). Probe fields 36/37 and the unit tests
// read this; the painting uses the machine directly and gets the whole view,
// and the legend's value, in one pass.
BOOL IndValueAt(const Candle* c, int n, int period, BOOL ema, int idx, double* out) {
    if (idx < 0 || idx >= n) return FALSE;
    IndState s;
    IndInit(&s, period, ema);
    BOOL ok = FALSE;
    for (int i = IndFeedStart(idx, period, ema); i <= idx; ++i) ok = IndStep(&s, c, i);
    if (ok) *out = s.val;
    return ok;
}
#endif

// Today's session, VWAP and today's high/low (phase 27). Pure functions of
// candles[], like the averages above: nothing is stored, everything is
// computed during painting.
//
// The session is the UTC DAY - Binance's daily candles and 24-hour figures
// break at 00:00 UTC, and so does VWAP at Bloomberg and TradingView. Not the
// visible view: PriceRange adds 8 % padding around the view's high and low,
// so two lines at the view's extremes would have stood in exactly the same
// place in every single frame, and a VWAP anchored in the first visible
// candle would have jumped with every candle during panning (same decision
// as the time axis: anchored in time, not in the index).
//
// SessionStartAt gives the index of the first candle in the day that candle
// idx belongs to, -1 when the session does not exist: empty buffer, or
// candles of a day or more (then the candle IS the session, and high/low are
// already in the hover box).
// Binary search - openTime is sorted, and a linear search backward would be
// 1440 64-bit comparisons per frame late in the day at 1m.
// *complete: the buffer reaches back to the start of the day. It does when
// there is an older candle before it, when the first candle opens on the day
// rollover, or when the history has ended. If the session is incomplete,
// nothing is drawn: a "today's high" computed from the last six hours is a
// wrong number.
#define DAY_MS 86400000LL

static int SessionStartAt(const Candle* c, int n, int idx, long long intervalMs,
                          BOOL histDone, BOOL* complete) {
    *complete = FALSE;
    if (n <= 0 || idx < 0 || idx >= n) return -1;
    if (intervalMs <= 0 || intervalMs >= DAY_MS) return -1;
    long long dayStart = (c[idx].openTime / DAY_MS) * DAY_MS;
    int lo = 0, hi = idx;                 // first i with openTime >= dayStart
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (c[mid].openTime >= dayStart) hi = mid; else lo = mid + 1;
    }
    *complete = (lo > 0) || histDone || (c[lo].openTime == dayStart);
    return lo;
}

// Today's session: the day of the last candle.
int SessionStart(const Candle* c, int n, long long intervalMs,
                        BOOL histDone, BOOL* complete) {
    return SessionStartAt(c, n, n - 1, intervalMs, histDone, complete);
}

// Highest high and lowest low over [s, n).
void SessionHiLo(const Candle* c, int s, int n, double* outHi, double* outLo) {
    double hi = c[s].high, lo = c[s].low;
    for (int i = s + 1; i < n; ++i) {
        if (c[i].high > hi) hi = c[i].high;
        if (c[i].low  < lo) lo = c[i].low;
    }
    *outHi = hi;
    *outLo = lo;
}

// VWAP: sum(typical price x volume) / sum(volume) from the start of the day,
// with typical price (H + L + C) / 3 - the usual definition on candles. Step
// machine like IndState: fed from the session's first candle, and the value
// comes out per candle. FALSE until there is volume to divide by. The feeder
// resets at every day rollover (see DrawVwap): each day has its own VWAP, so
// the line and the hover value are defined also when the view is in
// yesterday.
typedef struct { double pv; double v; double val; } VwapState;

static void VwapInit(VwapState* s) { s->pv = 0.0; s->v = 0.0; s->val = 0.0; }

static BOOL VwapStep(VwapState* s, const Candle* c) {
    s->pv += (c->high + c->low + c->close) / 3.0 * c->volume;
    s->v  += c->volume;
    if (s->v <= 0.0) return FALSE;
    s->val = s->pv / s->v;
    return TRUE;
}

#ifdef TICKER_PROBE
// Test build only: VWAP on one candle within the candle's own day, FALSE when
// the day is not wholly in the buffer or has no volume. Probe field 41 and
// the unit tests read this.
BOOL VwapValueAt(const Candle* c, int n, long long intervalMs,
                        BOOL histDone, int idx, double* out) {
    BOOL full = FALSE;
    int s = SessionStartAt(c, n, idx, intervalMs, histDone, &full);
    if (s < 0 || !full) return FALSE;
    VwapState v;
    VwapInit(&v);
    BOOL ok = FALSE;
    for (int i = s; i <= idx; ++i) ok = VwapStep(&v, &c[i]);
    if (ok) *out = v.val;
    return ok;
}
#endif

// RSI (phase 39), Wilder's definition as TradingView and Binance draw it: the
// first average gain and loss are the plain means of the first RSI_PERIOD
// changes, then avg = (avg * (period - 1) + x) / period. RSI = 100 - 100 /
// (1 + gain / loss); no loss gives 100, and no change at all 50. Like the EMA
// it has infinite memory, so it is ALWAYS fed from candle 0 - started where
// the view begins, the line would move during panning. Only + - * /
// (pitfall 75).
typedef struct { int fed; double prev, gain, loss, val; } RsiState;

static void RsiInit(RsiState* s) { s->fed = 0; s->prev = s->gain = s->loss = s->val = 0.0; }

static BOOL RsiStep(RsiState* s, const Candle* c) {
    double x = c->close;
    if (s->fed++ == 0) { s->prev = x; return FALSE; }
    double d = x - s->prev;
    s->prev = x;
    double up = (d > 0.0) ? d : 0.0, dn = (d < 0.0) ? -d : 0.0;
    // fed counts candles; the change on candle k is change number k - 1.
    if (s->fed <= RSI_PERIOD) {                // changes 1 .. period - 1: sum
        s->gain += up; s->loss += dn;
        return FALSE;
    }
    if (s->fed == RSI_PERIOD + 1) {            // change number period: the seed
        s->gain = (s->gain + up) / (double)RSI_PERIOD;
        s->loss = (s->loss + dn) / (double)RSI_PERIOD;
    } else if (s->fed > RSI_PERIOD + 1) {
        s->gain = (s->gain * (double)(RSI_PERIOD - 1) + up) / (double)RSI_PERIOD;
        s->loss = (s->loss * (double)(RSI_PERIOD - 1) + dn) / (double)RSI_PERIOD;
    } else {
        return FALSE;
    }
    if (s->loss <= 0.0) s->val = (s->gain <= 0.0) ? 50.0 : 100.0;
    else                s->val = 100.0 - 100.0 / (1.0 + s->gain / s->loss);
    return TRUE;
}

#ifdef TICKER_PROBE
// Test build only: the RSI on one candle, FALSE when it is not defined there
// (fewer than RSI_PERIOD changes before it). Probe field 63 reads it; the
// painting gets the view, the legend and the tag from DrawRsi in one pass.
BOOL RsiValueAt(const Candle* c, int n, int idx, double* out) {
    if (idx < 0 || idx >= n) return FALSE;
    RsiState s;
    RsiInit(&s);
    BOOL ok = FALSE;
    for (int i = 0; i <= idx; ++i) ok = RsiStep(&s, &c[i]);
    if (ok) *out = s.val;
    return ok;
}
#endif

// Yesterday (phase 28): the previous UTC day's high, low and close as
// reference levels. Pure functions of candles[] like the rest of the session
// code.
//
// PrevSession gives yesterday's first candle, and in *outEnd today's first
// (the candle AFTER yesterday's last) - the session is [return, *outEnd). -1
// when it does not exist: no session today (empty buffer, 1d candles), today's
// first candle is at the front of the buffer, or the candle before it is not
// in the previous day (a gap of a day or more - then there is no yesterday to
// show).
// *complete as in SessionStartAt: a "yesterday's high" computed from the last
// ten hours of yesterday is a wrong number, and is not drawn.
int PrevSession(const Candle* c, int n, long long intervalMs, BOOL histDone,
                       int* outEnd, BOOL* complete) {
    *complete = FALSE;
    *outEnd = -1;
    BOOL full = FALSE;
    int s1 = SessionStart(c, n, intervalMs, histDone, &full);
    if (s1 <= 0) return -1;
    if (c[s1 - 1].openTime / DAY_MS != c[s1].openTime / DAY_MS - 1) return -1;
    *outEnd = s1;
    return SessionStartAt(c, n, s1 - 1, intervalMs, histDone, complete);
}

// Must the buffer be filled backward for today's AND yesterday's session to
// be whole? Phase 27 stopped at today's day rollover; yesterday needs one more
// day (at 1m at most 2880 candles, eight fetches). FALSE when the history has
// ended, when there are no sessions (1d), and when yesterday does not exist
// (gap) - then there is nothing to fetch toward.
BOOL SessionsNeedHistory(const Candle* c, int n, long long intervalMs, BOOL histDone) {
    if (histDone) return FALSE;
    BOOL full = FALSE;
    int s1 = SessionStart(c, n, intervalMs, histDone, &full);
    if (s1 < 0) return FALSE;
    if (!full || s1 == 0) return TRUE;   // s1 == 0: today is covered, yesterday lies before the buffer
    int end = -1;
    BOOL prevFull = FALSE;
    int s0 = PrevSession(c, n, intervalMs, histDone, &end, &prevFull);
    return (s0 >= 0 && !prevFull);
}

// The span follows the zoom - "(60m)" would be wrong as soon as you zoom. With
// a variable interval it is no longer enough to count candles as minutes: 300
// candles of 1d are ten months, not five hours.
void FormatSpan(int vc, long long intervalMs, wchar_t* out, size_t cch) {
    long long mins = (long long)vc * intervalMs / 60000LL;
    if (mins < 60) {
        swprintf_s(out, cch, L"%lldm", mins);
        return;
    }
    long long hours = mins / 60, rm = mins % 60;
    if (hours < 24) {
        if (rm) swprintf_s(out, cch, L"%lldh %lldm", hours, rm);
        else    swprintf_s(out, cch, L"%lldh", hours);
        return;
    }
    long long days = hours / 24, rh = hours % 24;
    if (rh) swprintf_s(out, cch, L"%lldd %lldh", days, rh);
    else    swprintf_s(out, cch, L"%lldd", days);
}

// Linear color blend. t=0 gives a, t=255 gives b. Everything is drawn opaque
// over CLR_BG, so blending toward the background is identical to real
// transparency.
COLORREF Blend(COLORREF a, COLORREF b, int t) {
    if (t <= 0) return a;
    if (t >= 255) return b;
    int r = (GetRValue(a) * (255 - t) + GetRValue(b) * t) / 255;
    int g = (GetGValue(a) * (255 - t) + GetGValue(b) * t) / 255;
    int l = (GetBValue(a) * (255 - t) + GetBValue(b) * t) / 255;
    return RGB(r, g, l);
}

// Shared geometry for painting and mouse hits.
// Desktop mode: the header and the time band existed only for text that is no
// longer drawn (phase 14), so top, bottom and left run edge to edge. The right
// side, however, has got a margin back (phase 16) - not for axis labels, but
// for the one stamp with the last price. The margin is narrower than the
// panel's because it only has to hold the stamp.
//
// Everything else follows from here: the watermark's centering, the grid, the
// candles, the clip region and the last-price line.
// The stamp's height, font height and margin width in desktop mode. Pure
// functions of the surface's height: ChartGeometry is also called from hit
// detection and panning, where there is no DC to measure in.
int DeskPillH(int H) {
    int h = H / DESK_PILL_DIV;
    if (h < DESK_PILL_MIN) h = DESK_PILL_MIN;
    if (h > DESK_PILL_MAX) h = DESK_PILL_MAX;
    return h;
}

// Same ratio as in the panel: a 16 px stamp around a 15 px font.
int DeskPillFontH(int H) { return MulDiv(DeskPillH(H), 15, 16); }

// AXIS_CHAR_W is measured at em 15. The width is rounded UP: a character width
// that is really 22.2 px would have given eight characters 1.6 px too little,
// and the price would silently have fallen back to the axis's resolution
// instead of two decimals.
int DeskAxisW(int H, int dpi) {
    int cw = (DeskPillFontH(H) * AXIS_CHAR_W + 14) / 15;
    return ChartPx(dpi, AXIS_LBL_GAP) + AXIS_Y_CHARS * cw + ChartPx(dpi, AXIS_PAD_R);
}

// The panel's price column at dpi (phase 36): PAD_R at 96. The character
// width follows the axis font the same way as on the desktop - em 15 gives 9
// px, and the width is rounded up - so eight characters always fit.
int ChartAxisW(int dpi) {
    int cw = (ChartPx(dpi, 15) * AXIS_CHAR_W + 14) / 15;
    return ChartPx(dpi, AXIS_LBL_GAP) + AXIS_Y_CHARS * cw + ChartPx(dpi, AXIS_PAD_R);
}

ChartRect ChartGeometry(int W, int H, BOOL desktop, int dpi, BOOL band, BOOL vol) {
    ChartRect g;
    if (dpi <= 0) dpi = CHART_DPI_BASE;
    g.dpi    = dpi;
    g.left   = desktop ? 0 : ChartPx(dpi, PAD_L);
    g.top    = desktop ? 0 : ChartPx(dpi, HEADER_H);
    g.edge   = desktop ? W - DeskAxisW(H, dpi) : W - ChartAxisW(dpi);
    g.right  = g.edge - ChartPx(dpi, PLOT_PAD_R);
    g.bottom = desktop ? H : H - ChartPx(dpi, PAD_B);
    g.cw     = g.right - g.left;
    g.ch     = g.bottom - g.top;
    // The RSI band (phase 39) takes the bottom of the surface; the price pane
    // ends PANE_GAP above it. Left out when the price pane would get too low.
    // Both panes are sized from the whole chart height (ch0), so the volume
    // pane is as tall with the band as without it.
    int ch0 = g.ch, gap = ChartPx(dpi, PANE_GAP);
    g.bandTop = g.bandBottom = g.bottom;
    if (band) {
        int bh = (int)((double)ch0 * RSI_BAND_FRAC);
        if (bh < ChartPx(dpi, RSI_BAND_MIN)) bh = ChartPx(dpi, RSI_BAND_MIN);
        if (g.ch - bh - gap >= ChartPx(dpi, PRICE_PANE_MIN)) {
            g.bandTop = g.bottom - bh;
            g.bottom  = g.bandTop - gap;
            g.ch      = g.bottom - g.top;
        }
    }
    // The volume pane (phase 43) goes over the band, under the price - the
    // Bloomberg order. Without room the bars stand behind the candles, and
    // volTop = volBottom = bottom says so.
    g.volTop = g.volBottom = g.bottom;
    if (vol) {
        int vh = (int)((double)ch0 * VOL_PANE_FRAC);
        if (vh < ChartPx(dpi, VOL_PANE_MIN)) vh = ChartPx(dpi, VOL_PANE_MIN);
        if (g.ch - vh - gap >= ChartPx(dpi, PRICE_PANE_MIN)) {
            g.volBottom = g.bottom;
            g.volTop    = g.bottom - vh;
            g.bottom    = g.volTop - gap;
            g.ch        = g.bottom - g.top;
        }
    }
    return g;
}

// The tallest volume bar in pixels (phase 43): the pane under its top line
// and headroom, or VOL_FRAC of the price pane when the bars stand behind the
// candles. The drawing and the app's easing of the volume scale both read it.
int ChartVolBarsH(const ChartRect* g) {
    if (g->volBottom > g->bottom)
        return g->volBottom - g->volTop - ChartPx(g->dpi, VOL_PANE_PAD);
    return (int)((double)g->ch * VOL_FRAC);
}

// The lowest pane's bottom row (phase 43): where the hit test, the vertical
// crosshair and the time axis end. One function, so the three agree.
int ChartPanesBottom(const ChartRect* g) {
    int y = g->bottom;
    if (g->volBottom > y)  y = g->volBottom;
    if (g->bandBottom > y) y = g->bandBottom;
    return y;
}

// Which candle is the mouse over? Returns an absolute index, -1 outside.
// MUST read the same disp values as DrawChart. If one reads the target and the
// other the display, the crosshair points at the wrong candle mid-animation -
// that is bug #7 from the log in new clothes.
int HitCandle(const ChartState* ctx, int n, const ChartRect* g, int mx, int my) {
    if (ctx->dispCount <= 0.0 || g->cw <= 0) return -1;
    if (n <= 0) return -1;
    // The panes under the price (phase 39, 43) belong to the same candles:
    // the crosshair works there too.
    int yMax = ChartPanesBottom(g);
    if (mx < g->left || mx >= g->right || my < g->top || my > yMax) return -1;

    double slot = (double)g->cw / ctx->dispCount;
    if (slot <= 0.0) return -1;
    int idx = (int)(ctx->dispStart + (double)(mx - g->left) / slot);

    // Bounded by candleCount, not by vs + vc: during the animation the
    // display can hang outside the target view.
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    return idx;
}

// Price alerts (phase 23): price <-> y. Drawing and hit-testing MUST read the
// same source (pitfall 14), so both directions go through these two, and both
// read the DISPLAY (dispMin/dispMax) with the same range guard and the same
// truncation as the candles in DrawChart. The clamp before (int) is for a
// level far outside the view: 75 000 on a SOL axis with a span of 1 gives 5e7
// pixels, and an alert from the registry can be anything below ALERT_PRICE_MAX.
int AlertY(const ChartState* ctx, const ChartRect* g, double level) {
    double range = ctx->dispMax - ctx->dispMin;
    if (range < 1e-9) range = 1.0;
    double yd = ((ctx->dispMax - level) / range) * (double)g->ch;
    if (yd < -100000.0) yd = -100000.0;
    if (yd >  100000.0) yd =  100000.0;
    return g->top + (int)yd;
}

// The price at height y, rounded to below one pixel (AlertRound).
double AlertPriceAtY(const ChartState* ctx, const ChartRect* g, int y) {
    double range = ctx->dispMax - ctx->dispMin;
    if (range < 1e-9) range = 1.0;
    if (g->ch <= 0) return 0.0;
    double p = ctx->dispMax - ((double)(y - g->top) / (double)g->ch) * range;
    return AlertRound(p, range / (double)g->ch);
}

// Which alert is the pointer on in the price column? The nearest tag within
// ALERT_HIT_PX - that is, exactly the area the tag is drawn on - and -1
// otherwise. Tags outside [top, bottom] are not drawn and cannot be hit.
int AlertAxisHit(const ChartState* ctx, const ChartRect* g, const double* alerts, int alertCount, int my) {
    int hitPx = ChartPx(g->dpi, ALERT_HIT_PX);
    int best = -1, bestD = hitPx + 1;
    for (int i = 0; i < alertCount; ++i) {
        int y = AlertY(ctx, g, fabs(alerts[i]));
        if (y < g->top || y > g->bottom) continue;
        // The tag is [y - 8, y + 8): FillRect is exclusive at the bottom.
        if (my < y - hitPx || my >= y + hitPx) continue;
        int d = abs(my - y);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

// The number of decimals on the price axis is chosen from the SPACING between
// the labels, not from the size of the price. SOL around 97 dollars has a span
// of under one dollar: with "%.0f" all five labels read "97". BTC around
// 75 000 needs no decimals. Same class of bug as #5 in the log - the format
// must follow the number actually shown, not an assumed order of magnitude.
int PriceDecimals(double step) {
    if (step <= 0.0) return 2;
    int d = 0;
    while (step < 2.0 && d < 6) { step *= 10.0; ++d; }
    return d;
}

// Min/max over the visible candles, with 8% headroom above and below.
void PriceRange(const Candle* candles, int vs, int vc, double* outMin, double* outMax) {
    double mn = candles[vs].low, mx = candles[vs].high;
    for (int i = 1; i < vc; ++i) {
        const Candle* c = &candles[vs + i];
        if (c->low  < mn) mn = c->low;
        if (c->high > mx) mx = c->high;
    }
    double range = mx - mn;
    if (range < 1e-9) range = 1.0;
    double pad = range * 0.08;
    *outMin = mn - pad;
    *outMax = mx + pad;
    // Phase 41: the padding must not take the axis below zero. A view whose
    // low is small against its range - Max on 1w, from 3 100 to 126 000 -
    // got a grid label of -7054. Prices are never negative, so the floor is 0.
    if (mn >= 0.0 && *outMin < 0.0) *outMin = 0.0;
}

// Phase 49: the price axis per chart type. Commit 1 of the phase: the API
// only - every type scales as the candles do.
void PriceRangeFor(const Candle* candles, int vs, int vc, int chartType, double* outMin, double* outMax) {
    (void)chartType;
    PriceRange(candles, vs, vc, outMin, outMax);
}

// Largest volume in the view (phase 21): the scale of the bars. 0 when no
// candle has volume - then no bars are drawn, instead of the height becoming
// NaN. Called under the lock, like PriceRange.
double VolumeMax(const Candle* candles, int vs, int vc) {
    double mx = 0.0;
    for (int i = 0; i < vc; ++i) {
        double v = candles[vs + i].volume;
        if (v > mx) mx = v;
    }
    return mx;
}

// When candles drop out at the front, EVERYTHING that is an absolute index is
// shifted by the same amount. Without this the chart jumps one candle to the
// left every minute once the buffer has reached its cap, and hoverIdx points
// at the neighboring candle.
// Idempotent: delta is 0 the second time. Called under the lock.
void ApplyFrontShift(ChartState* ctx, long long frontShift) {
    long long delta = frontShift - ctx->dispShiftSeen;
    if (delta == 0) return;
    ctx->dispShiftSeen = frontShift;

    // No easing: an eviction is not a movement the user should see, and a
    // backfill (delta < 0, phase 18) should not move the frame at all.
    ctx->dispStart -= (double)delta;
    if (ctx->dispStart < 0.0) ctx->dispStart = 0.0;
    if (ctx->hoverIdx >= 0) {
        ctx->hoverIdx -= (int)delta;
        if (ctx->hoverIdx < 0) ctx->hoverIdx = -1;
    }
    ctx->panAnchorView -= (int)delta;
    if (ctx->panAnchorView < 0) ctx->panAnchorView = 0;
}

// Sets the display equal to the target without animation. Used when an
// animation makes no sense: first frame, new buffer after a config change,
// the panel opens. Called under the lock.
void SyncDisp(ChartState* ctx, const Candle* candles, int n) {
    int vs, vc;
    GetView(ctx, n, &vs, &vc);
    ctx->dispStart = (double)vs;
    ctx->dispCount = (vc > 0) ? (double)vc : 1.0;

    // With an empty buffer there is no price axis to sync against. If we mark
    // ourselves valid here, dispMin/dispMax ease from [0, 1] up to the real
    // span when the data arrives - that is, a Y axis that slides up from zero
    // for half a second after every symbol change. We stay invalid instead,
    // so the first frame WITH data snaps.
    if (n <= 0 || vc <= 0) {
        ctx->dispMin   = 0.0;
        ctx->dispMax   = 1.0;
        ctx->dispVolMax = 0.0;
        ctx->dispValid = FALSE;
        return;
    }

    PriceRange(candles, vs, vc, &ctx->dispMin, &ctx->dispMax);
    ctx->dispVolMax = VolumeMax(candles, vs, vc);   // phase 21
    ctx->dispValid = TRUE;
}

// The local offset in force NOW, in ms east of UTC - exactly what
// FileTimeToLocalFileTime adds, which the labels used until phase 35. It
// applies today's offset to every date, also one from before a DST change.
long long ChartUtcOffsetMs(void) {
    FILETIME fu, fl;
    GetSystemTimeAsFileTime(&fu);
    if (!FileTimeToLocalFileTime(&fu, &fl)) return 0;
    ULONGLONG u = ((ULONGLONG)fu.dwHighDateTime << 32) | fu.dwLowDateTime;
    ULONGLONG l = ((ULONGLONG)fl.dwHighDateTime << 32) | fl.dwLowDateTime;
    return ((long long)l - (long long)u) / 10000LL;
}

// Unix ms -> a candle's time, in one of three forms (phase 45; until then 1h
// and 4h wrote "MM-DD HH:MM" on every label, so a 1h axis read "09-12 00:00"
// nine times, and under 1h a view across midnight had no date at all):
//   TIME_CLOCK  "14:35"         the quote line's At (tickc.c, through
//                               FormatCandleTime)
//   TIME_AXIS   "14:35", and "21 Sep" on the candle a local day begins with -
//               Bloomberg marks the day on the axis
//   TIME_BOX    "21 Sep 14:35"  the hover box
// The day begins with the candle whose open lies less than one interval
// after local midnight: that is the midnight candle itself wherever the
// offset is whole hours and the interval divides them, and in CEST the 4h
// candle at 02:00 (the candles open at 00:00 UTC), which "on midnight" alone
// never finds. Month names are English and fixed, not the locale's: the repo
// and the app are English, and the axis width is measured on them.
//
// From 1 day up every form is "YYYY-MM-DD", the UTC date. Daily and weekly
// candles open at 00:00 UTC; west of UTC the local conversion put them on
// the previous day (phase 44 review, F4).
#define TIME_CLOCK 0
#define TIME_AXIS  1
#define TIME_BOX   2
static void FormatTimeAs(long long unixMs, long long intervalMs, long long utcOffsetMs,
                         int form, wchar_t* out, size_t cch) {
    static const wchar_t* const MON[12] = { L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
                                            L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec" };
    BOOL daily = (intervalMs >= 86400000LL);
    long long off = daily ? 0 : utcOffsetMs;
    ULONGLONG t = (ULONGLONG)(unixMs / 1000 + off / 1000) * 10000000ULL + 116444736000000000ULL;
    FILETIME local;
    local.dwLowDateTime  = (DWORD)(t & 0xFFFFFFFFULL);
    local.dwHighDateTime = (DWORD)(t >> 32);
    SYSTEMTIME st;
    if (!FileTimeToSystemTime(&local, &st) || st.wMonth < 1 || st.wMonth > 12) {
        wcscpy_s(out, cch, L"--:--");
        return;
    }
    if (daily) {
        swprintf_s(out, cch, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
        return;
    }
    long long tod = (long long)st.wHour * 3600000LL + st.wMinute * 60000LL + st.wSecond * 1000LL;
    BOOL dayStart = (tod < ((intervalMs > 0) ? intervalMs : 60000LL));
    if (form == TIME_BOX)
        swprintf_s(out, cch, L"%d %s %02d:%02d", st.wDay, MON[st.wMonth - 1], st.wHour, st.wMinute);
    else if (form == TIME_AXIS && dayStart)
        swprintf_s(out, cch, L"%d %s", st.wDay, MON[st.wMonth - 1]);
    else
        swprintf_s(out, cch, L"%02d:%02d", st.wHour, st.wMinute);
}

void FormatCandleTime(long long unixMs, long long intervalMs, long long utcOffsetMs,
                             wchar_t* out, size_t cch) {
    FormatTimeAs(unixMs, intervalMs, utcOffsetMs, TIME_CLOCK, out, cch);
}

// The widest label TIME_AXIS can give for the interval, measured in the font
// selected into hdc (phase 45). The spacing of the labels must hold for every
// label, and until now it was measured on the view's first one - with two
// forms on one axis, "30 Sep" would have been placed at the distance of
// "00:00". The axis font is monospace (see ChartStyleCreate), so one sample
// per form is the widest of its kind.
int ChartTimeLabelW(HDC hdc, long long intervalMs) {
    static const wchar_t* const INTRA[2] = { L"00:00", L"30 Sep" };
    SIZE sz = { 0, 0 };
    if (intervalMs >= 86400000LL) {
        GetTextExtentPoint32W(hdc, L"2026-09-21", 10, &sz);
        return sz.cx;
    }
    int w = 0;
    for (int i = 0; i < 2; ++i) {
        sz.cx = 0;
        GetTextExtentPoint32W(hdc, INTRA[i], (int)wcslen(INTRA[i]), &sz);
        if (sz.cx > w) w = sz.cx;
    }
    return w;
}

// Layout and hit detection share one function. Two independent calculations of
// the same area end up pointing at different places - see bug #7 in the log.
// Volume for the hover box (phase 21): compact, so DOGE volume in millions
// fits in 104 px. Below a thousand two decimals, otherwise K/M with one decimal.
void FormatVolume(double v, wchar_t* out, size_t cch) {
    if (v >= 1e6)      swprintf_s(out, cch, L"%.1fM", v / 1e6);
    else if (v >= 1e3) swprintf_s(out, cch, L"%.1fK", v / 1e3);
    else               swprintf_s(out, cch, L"%.2f", v);
}

// The text in a tag on the price axis (phase 23): two decimals where they
// fit, otherwise the axis resolution - same rule as the stamp for the last
// price, measured on the fully formatted string (bug #5). The font must be
// selected into hdc before the call.
void FormatTagPrice(HDC hdc, double p, double range, int avail,
                           wchar_t* out, size_t cch) {
    SIZE sz = { 0, 0 };
    swprintf_s(out, cch, L"%.2f", p);
    if (GetTextExtentPoint32W(hdc, out, (int)wcslen(out), &sz) && sz.cx > avail) {
        swprintf_s(out, cch, L"%.*f", PriceDecimals(range / 4.0), p);
    }
}

// Panning and zoom of the TARGET VIEW (phase 20). Pulled out of
// WM_MOUSEWHEEL, so wheel and keyboard share one calculation, the way the
// buttons and the shortcuts share OnButtonClick. Both are called under the
// lock, both clamp with ClampView and set followLive, and both report whether
// the view is against the wall afterwards (viewStart == 0) - then the caller
// will ask for history (phase 18). The display (disp*) is not touched here:
// it eases toward the target in WM_TIMER as before.
//
// PanView: delta candles, positive = forward in time.
BOOL PanView(ChartState* ctx, int n, int delta) {
    int vs, vc;
    GetView(ctx, n, &vs, &vc);
    ctx->viewStart = vs + delta;
    ctx->viewCount = vc;
    ClampView(ctx, n);
    ctx->followLive = (ctx->viewStart + ctx->viewCount >= n);
    return (ctx->viewStart == 0);
}

// ZoomView: notches > 0 zooms in, < 0 out, ZOOM_STEP per notch, around an
// anchor given as a fraction [0, 1] of the view - the pointer's position for
// the wheel, the middle for the keys.
BOOL ZoomView(ChartState* ctx, int n, double frac, int notches) {
    int vs, vc;
    GetView(ctx, n, &vs, &vc);
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;

    double anchor = (double)vs + frac * (double)vc;

    double f = 1.0;
    for (int k = 0; k < notches; ++k)  f *= ZOOM_STEP;
    for (int k = 0; k > notches; --k)  f /= ZOOM_STEP;

    int newCount = (int)((double)vc / f + 0.5);
    if (newCount < MIN_VIEW) newCount = MIN_VIEW;
    if (newCount > n)        newCount = n;

    ctx->viewStart = (int)(anchor - frac * (double)newCount + 0.5);
    ctx->viewCount = newCount;
    ClampView(ctx, n);
    ctx->followLive = (ctx->viewStart + ctx->viewCount >= n);
    return (ctx->viewStart == 0);
}

// A moving-average line (phase 25) over the view [i0, i1). Called from
// DrawChart under the lock and inside the chart's clip, with DC_PEN selected.
//
// The line goes ONE candle out on each side of the view, so it leaves the
// surface through the clip instead of ending in the middle of the outermost
// candle. Same x and y as the candles: the middle of the column, and
// top + (int)(...) on the price - floor on x because the candle beyond the
// left edge has a negative offset, where (int) rounds toward zero and not
// downward.
//
// y is clamped to 16 surface heights: the average looks period candles back
// and can lie far outside a zoomed-in price range, and GDI computes in 27
// bits. The clamp is so far out that it does not change the slope of
// anything visible.
//
// The points go to Polyline in batches; the last point of a batch is the
// first of the next, so the line is continuous. With more candles than
// pixels many points fall in the same column - Polyline draws them as the
// vertical stroke they are.
//
// legendIdx: the candle the legend wants the value for. TRUE when *legendVal
// is set - the value falls out of the same pass, without an extra one.
static BOOL DrawIndicator(HDC hdc, const Candle* candles, int n, const ChartRect* g,
                          int period, BOOL ema, COLORREF clr,
                          double dStart, double slot, int i0, int i1,
                          double maxP, double range,
                          int legendIdx, double* legendVal) {
    int first = (i0 > 0) ? i0 - 1 : 0;
    int last  = (i1 < n) ? i1 : n - 1;
    double yLo = -16.0 * (double)g->ch, yHi = 17.0 * (double)g->ch;

    IndState s;
    IndInit(&s, period, ema);
    BOOL haveLegend = FALSE;
    int k = 0;
    SetDCPenColor(hdc, clr);
    for (int i = IndFeedStart(first, period, ema); i <= last; ++i) {
        if (!IndStep(&s, candles, i)) continue;
        if (i == legendIdx) { *legendVal = s.val; haveLegend = TRUE; }
        if (i < first) continue;
        double yy = ((maxP - s.val) / range) * (double)g->ch;
        if (yy < yLo) yy = yLo;
        if (yy > yHi) yy = yHi;
        s_volPts[k].x = g->left + (int)floor(((double)i - dStart + 0.5) * slot);
        s_volPts[k].y = g->top + (int)yy;
        if (++k == IND_BATCH) {
            Polyline(hdc, s_volPts, k);
            s_volPts[0] = s_volPts[k - 1];
            k = 1;
        }
    }
    if (k >= 2) Polyline(hdc, s_volPts, k);
    return haveLegend;
}

// The VWAP line (phase 27) over the view [i0, i1). Same contract, same x and y
// and same batches as DrawIndicator. The difference is the anchoring: the
// machine is fed from the start of the day the first drawn candle belongs to,
// and reset at every day rollover. The line is also broken there - today's
// VWAP has nothing to do with yesterday's last value, and a stroke between
// the two would be a number that does not exist. If the oldest day is not
// fully in the buffer, nothing is drawn before the next day rollover.
static BOOL DrawVwap(HDC hdc, const ChartData* in, const ChartRect* g, COLORREF clr,
                     double dStart, double slot, int i0, int i1,
                     double maxP, double range,
                     int legendIdx, double* legendVal) {
    int n = in->count;
    const Candle* c = in->candles;
    int first = (i0 > 0) ? i0 - 1 : 0;
    int last  = (i1 < n) ? i1 : n - 1;
    double yLo = -16.0 * (double)g->ch, yHi = 17.0 * (double)g->ch;

    BOOL live = FALSE;
    int s = SessionStartAt(c, n, first, in->intervalMs, in->histDone, &live);
    if (s < 0) return FALSE;
    long long nextDay = (c[s].openTime / DAY_MS + 1) * DAY_MS;

    VwapState v;
    VwapInit(&v);
    BOOL haveLegend = FALSE;
    int k = 0;
    SetDCPenColor(hdc, clr);
    for (int i = s; i <= last; ++i) {
        if (c[i].openTime >= nextDay) {
            if (k >= 2) Polyline(hdc, s_volPts, k);
            k = 0;
            VwapInit(&v);
            live = TRUE;
            nextDay = (c[i].openTime / DAY_MS + 1) * DAY_MS;
        }
        if (!VwapStep(&v, &c[i]) || !live) continue;
        if (i == legendIdx) { *legendVal = v.val; haveLegend = TRUE; }
        if (i < first) continue;
        double yy = ((maxP - v.val) / range) * (double)g->ch;
        if (yy < yLo) yy = yLo;
        if (yy > yHi) yy = yHi;
        s_volPts[k].x = g->left + (int)floor(((double)i - dStart + 0.5) * slot);
        s_volPts[k].y = g->top + (int)yy;
        if (++k == IND_BATCH) {
            Polyline(hdc, s_volPts, k);
            s_volPts[0] = s_volPts[k - 1];
            k = 1;
        }
    }
    if (k >= 2) Polyline(hdc, s_volPts, k);
    return haveLegend;
}

// The volume bars (phase 21), standing on row base and at most hMax tall:
// the pane's bottom (phase 43) or, without room for a pane, the price pane's
// bottom behind the candles. The scale is dispVolMax - the DISPLAY, which is
// eased in WM_TIMER - not the target, otherwise the bars jump while the
// candles glide. The direction is the candle's own (close vs open), the same
// rule as the candle color and a different one from the last-price line's
// (see there). The bottom row is y = base, inclusive, as for wicks and grid
// line 4 (pitfall 33); FillRect is exclusive at the bottom, hence base + 1.
//
// One PolyPolygon per color and batch of VOL_BATCH bars, with NULL_PEN: the
// polygon fill leaves out the right and bottom edges like Rectangle, so the
// corners [x0, x1) x [base + 1 - h, base + 1) fill exactly the same pixels
// FillRect would have.
//
// WINDING, not ALTERNATE: with more candles than pixels (vc > cw) slot is
// below 1, bodyW is clamped to 1, and neighboring candles land on the same
// cx. Two identical rectangles in the same batch CANCEL each other under
// ALTERNATE (even/odd), so the bar disappears. FillRect overdrew; polygon
// fill counts edges. With WINDING and the same winding direction on all the
// rectangles they add up, and the union - the tallest - remains. Measured in
// the probe: two identical rectangles give 0 pixels under ALTERNATE and
// w x h under WINDING.
//
// dispVolF (phase 22) is the toggle's display, 0..1, and the bars grow with
// it. Phase 43: they only rise - turned off, the region goes at once (it is
// geometry, like the RSI band's), and nothing is left to sink. At 1.0 the
// factor is exact.
//
// pane (phase 45): the bars stand in the volume pane, where nothing is in
// front of them, and take the stronger volPaneUp/volPaneDown; behind the
// candles they keep the muted volUp/volDown.
static void DrawVolumeBars(HDC hdc, const ChartState* st, const ChartData* in,
                           const ChartStyle* sty, int left, double dStart, double slot,
                           int bodyW, int i0, int i1, int base, int hMax, BOOL pane) {
    if (st->dispVolMax <= 0.0 || st->dispVolF <= 0.0 || hMax <= 0) return;
    int oldFill = SetPolyFillMode(hdc, WINDING);
    HGDIOBJ oldPenV = SelectObject(hdc, GetStockObject(NULL_PEN));
    HBRUSH brUp   = pane ? sty->brVolPaneUp   : sty->brVolUp;
    HBRUSH brDown = pane ? sty->brVolPaneDown : sty->brVolDown;
    for (int pass = 0; pass < 2; ++pass) {          // 0 = up, 1 = down
        SelectObject(hdc, pass == 0 ? brUp : brDown);
        int k = 0;
        for (int i = i0; i < i1; ++i) {
            const Candle* c = &in->candles[i];
            if ((c->close >= c->open) != (pass == 0)) continue;
            int h = (int)(c->volume / st->dispVolMax * (double)hMax * st->dispVolF + 0.5);
            if (h <= 0) continue;
            if (h > hMax) h = hMax;   // mid-easing a candle can lie above the scale
            int cx = left + (int)(((double)i - dStart + 0.5) * slot);
            int x0 = cx - bodyW / 2, x1 = x0 + bodyW;
            int y0 = base + 1 - h, y1 = base + 1;
            POINT* p = &s_volPts[k * 4];
            p[0].x = x0; p[0].y = y0;
            p[1].x = x1; p[1].y = y0;
            p[2].x = x1; p[2].y = y1;
            p[3].x = x0; p[3].y = y1;
            s_volCnt[k++] = 4;
            if (k == VOL_BATCH) { PolyPolygon(hdc, s_volPts, s_volCnt, k); k = 0; }
        }
        if (k > 0) PolyPolygon(hdc, s_volPts, s_volCnt, k);
    }
    SelectObject(hdc, oldPenV);
    SetPolyFillMode(hdc, oldFill);
}

// The RSI line in the band (phase 39). Fed from candle 0 over the whole
// buffer (infinite memory, see RsiStep); points only for [i0 - 1, i1], the
// same x as the candles and the same batches as DrawIndicator. *legendVal is
// the value at legendIdx, *lastVal the one at the last candle (the axis tag).
static void DrawRsi(HDC hdc, const Candle* candles, int n, const ChartRect* g,
                    double dStart, double slot, int i0, int i1,
                    int legendIdx, double* legendVal, BOOL* legendOk,
                    double* lastVal, BOOL* lastOk) {
    int first = (i0 > 0) ? i0 - 1 : 0;
    int last  = (i1 < n) ? i1 : n - 1;
    int bt = g->bandTop, bh = g->bandBottom - g->bandTop;
    RsiState s;
    RsiInit(&s);
    int k = 0;
    for (int i = 0; i < n; ++i) {
        if (!RsiStep(&s, &candles[i])) continue;
        if (i == legendIdx) { *legendVal = s.val; *legendOk = TRUE; }
        if (i == n - 1)     { *lastVal = s.val;   *lastOk = TRUE; }
        if (i < first || i > last) continue;
        s_volPts[k].x = g->left + (int)floor(((double)i - dStart + 0.5) * slot);
        s_volPts[k].y = bt + (int)(((100.0 - s.val) / 100.0) * (double)bh);
        if (++k == IND_BATCH) {
            Polyline(hdc, s_volPts, k);
            s_volPts[0] = s_volPts[k - 1];
            k = 1;
        }
    }
    if (k >= 2) Polyline(hdc, s_volPts, k);
}

// A dashed horizontal line over [x0, x1) on row y (phase 27: today's high and
// low). Not a PS_DASH pen: the line fades with dispIndF, and a pen cannot
// change color per frame without being recreated. The dashes go to
// PolyPolyline in batches, with DC_PEN - no new GDI objects. The pattern is
// anchored at anchor (the surface's left edge), not at x0, so the dashes stand
// still when the session start slides during panning. GDI does not draw the
// end point, so [a, b) is exactly dashOn pixels. The period is shared
// (SESS_DASH_PERIOD); dashOn separates today's lines from yesterday's (phase 28).
// Both arrive scaled to the chart's dpi (phase 36).
static void DrawDashLine(HDC hdc, int x0, int x1, int y, int anchor, int dashOn, int period) {
    if (x0 < anchor) x0 = anchor;
    int x = anchor + ((x0 - anchor) / period) * period;
    int k = 0;
    for (; x < x1; x += period) {
        int a = (x < x0) ? x0 : x;
        int b = x + dashOn;
        if (b > x1) b = x1;
        if (a >= b) continue;
        s_volPts[k * 2].x     = a; s_volPts[k * 2].y     = y;
        s_volPts[k * 2 + 1].x = b; s_volPts[k * 2 + 1].y = y;
        s_volCnt[k++] = 2;
        if (k == VOL_BATCH) { PolyPolyline(hdc, s_volPts, (const DWORD*)s_volCnt, (DWORD)k); k = 0; }
    }
    if (k > 0) PolyPolyline(hdc, s_volPts, (const DWORD*)s_volCnt, (DWORD)k);
}

// The themes (phase 38). Dark is the CLR_ macros, field for field, so TickC
// draws exactly what it drew before the colors became data.
const ChartTheme ChartThemeDark = {
    CLR_BG,
    CLR_GRID,
    CLR_UP,
    CLR_DOWN,
    CLR_TEXT,
    CLR_DIM,
    CLR_CROSS,
    CLR_BOX,
    CLR_BOXEDGE,
    CLR_CLOSEHOT,
    CLR_BTNHOT,
    CLR_AXIS,
    CLR_VOL_UP,
    CLR_VOL_DOWN,
    CLR_VOL_PANE_UP,     // phase 45
    CLR_VOL_PANE_DOWN,
    CLR_SMA,
    CLR_EMA,
    CLR_VWAP,
    CLR_SESSION,
    CLR_PREV,
    CLR_ALERT,
    CLR_ALERT_LINE,
    CLR_RSI,
    CLR_BG,        // onAlert: dark text on the amber tag
    CLR_ALERT,     // alertText
    CLR_QUOTE,     // quote (phase 42)
    CLR_ACCENT,    // accent
    CLR_BTNHOT,    // onAccent: white
    CLR_LINE,      // line (phase 49)
    CLR_MOUNTAIN,  // mountain
};

// Light: the same roles on a near-white background. The candles are the
// deeper green/red of light trading charts - 00FF66 is unreadable on white.
// The overlays keep their hues but darker; VWAP turns from yellow to dark
// gold, which yellow cannot be on white. The volume bars are the candle
// colors blended about 25 % toward the background, as in the dark theme.
// Phase 45: in the pane about 65 % (5DAB95, E27381) - a notch more than the
// dark theme's 60 %, since a pastel on near-white carries less than a dark
// color on near-black; still lighter than the candles. Blue is moved off the
// exact 65 % blend (5DAB92, E2737E): up and down are text colors here too
// (pitfall 87).
//
// Phase 40: every role drawn as text reaches WCAG AA, 4.5:1, on the
// background and on the box (tests/chart_golden.c checks the pairs). The
// first table missed on 20 pairs; the alert tag's number was 2.65:1. Each
// color was darkened with its hue kept, so it also carries the light text
// of the stamp at 4.5:1 (up, down). A color cannot do both that and carry
// dark text, so the amber alert tag keeps its surface and gets dark text
// (onAlert, 5.7:1), and the ghost tag draws in a darker amber (alertText).
// Up moved from teal to green and RSI to teal-cyan: at 089981 and 00897B
// the two were almost the same color.
const ChartTheme ChartThemeLight = {
    RGB(0xFA, 0xFA, 0xFB),   // bg
    RGB(0xE8, 0xEA, 0xEE),   // grid
    RGB(0x08, 0x80, 0x5A),   // up         4.74:1 on bg
    RGB(0xD5, 0x2A, 0x3A),   // down       4.77
    RGB(0x1F, 0x23, 0x28),   // text
    RGB(0x6A, 0x73, 0x7D),   // dim        4.62
    RGB(0x9A, 0xA0, 0xA6),   // cross
    RGB(0xFF, 0xFF, 0xFF),   // box
    RGB(0xD0, 0xD7, 0xDE),   // boxEdge
    RGB(0xA3, 0x1D, 0x33),   // hot        darker than down, white text 7.6
    RGB(0xFF, 0xFF, 0xFF),   // onHot
    RGB(0x4A, 0x53, 0x60),   // axis
    RGB(0xC3, 0xDE, 0xD6),   // volUp
    RGB(0xF2, 0xCB, 0xCF),   // volDown
    RGB(0x5D, 0xAB, 0x95),   // volPaneUp    phase 45: about 65 % of up
    RGB(0xE2, 0x73, 0x81),   // volPaneDown  and of down
    RGB(0x2C, 0x78, 0xAA),   // sma        4.60
    RGB(0x8C, 0x59, 0xC6),   // ema        4.62
    RGB(0x8A, 0x6C, 0x00),   // vwap       4.76
    RGB(0x6C, 0x72, 0x7F),   // session    4.63
    RGB(0x5B, 0x6A, 0x8E),   // prev       5.17, cooler than session
    RGB(0xD9, 0x8A, 0x00),   // alert      the tag's surface
    RGB(0xE8, 0xC4, 0x80),   // alertLine
    RGB(0x00, 0x7E, 0x83),   // rsi        4.67
    RGB(0x1F, 0x23, 0x28),   // onAlert    5.71 on the amber
    RGB(0xA8, 0x54, 0x00),   // alertText  5.12
    RGB(0xA8, 0x54, 0x00),   // quote      5.12 (phase 42: the alertText amber)
    RGB(0x2F, 0x5D, 0xA8),   // accent
    RGB(0xFF, 0xFF, 0xFF),   // onAccent   6.5 on the accent
    RGB(0x1B, 0x36, 0x5D),   // line       phase 49: deep navy, 11.6 on bg
    RGB(0xDA, 0xDE, 0xE4),   // mountain   Blend(bg, line, 36)
};

// The chart's fixed GDI objects (phase 35; created in wWinMain until phase
// 34). Created once, not per repaint.
BOOL ChartStyleCreate(ChartStyle* sty, int dpi, const ChartTheme* theme) {
    const ChartTheme* t = theme ? theme : &ChartThemeDark;
    ZeroMemory(sty, sizeof(*sty));
    if (dpi <= 0) dpi = CHART_DPI_BASE;
    sty->dpi = dpi;
    sty->clr = *t;
    sty->fontSmall = CreateFontW(-ChartPx(dpi, 11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    // The price and time axes. Monospace, so the labels stand still when the
    // digits change, and AXIS_Y_W can be computed in characters. Grayscale
    // antialiasing (ANTIALIASED_QUALITY), not ClearType: no color fringing on
    // numbers. Measured with GetGlyphOutlineW(GGO_METRICS) on '0': Lucida
    // Console em 15 gives 11 px digit height, 9 px character width and
    // tmHeight 15. Consolas jumps from 10 to 12 px (em 16 -> 17), Cascadia Mono
    // em 16 gives 11 px but tmHeight 21, which does not fit in the time axis's
    // 18 px. At other dpi the em scales with it, and ChartAxisW follows.
    sty->fontAxis = CreateFontW(-ChartPx(dpi, 15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN,
                                L"Lucida Console");
    sty->penGrid  = CreatePen(PS_SOLID, 1, t->grid);
    sty->penCross = CreatePen(PS_DOT,   1, t->cross);
    // Dashed, not dotted: keeps the last-price line visually distinct from
    // both the grid (solid, muted) and the crosshair (dotted).
    sty->penLastUp   = CreatePen(PS_DASH, 1, t->up);
    sty->penLastDown = CreatePen(PS_DASH, 1, t->down);
    sty->brBg      = CreateSolidBrush(t->bg);
    sty->brBox     = CreateSolidBrush(t->box);
    sty->brBoxEdge = CreateSolidBrush(t->boxEdge);
    sty->brVolUp   = CreateSolidBrush(t->volUp);     // phase 21
    sty->brVolDown = CreateSolidBrush(t->volDown);
    sty->brVolPaneUp   = CreateSolidBrush(t->volPaneUp);     // phase 45
    sty->brVolPaneDown = CreateSolidBrush(t->volPaneDown);
    // Phase 49: the price line, scaled with the dpi (see chart.h). Solid and
    // wider than 1 at 144 dpi and up, so no pattern is lost.
    sty->penLine = CreatePen(PS_SOLID, ChartPx(dpi, 1), t->line);
    return sty->fontSmall && sty->fontAxis && sty->penGrid && sty->penCross &&
           sty->penLastUp && sty->penLastDown && sty->brBg && sty->brBox &&
           sty->brBoxEdge && sty->brVolUp && sty->brVolDown &&
           sty->brVolPaneUp && sty->brVolPaneDown && sty->penLine;
}

HFONT ChartPillFontCreate(int H) {
    return CreateFontW(-DeskPillFontH(H), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN,
                       L"Lucida Console");
}

void ChartStyleDestroy(ChartStyle* sty) {
    HGDIOBJ own[] = { sty->fontSmall, sty->fontAxis, sty->penGrid, sty->penCross,
                      sty->penLastUp, sty->penLastDown, sty->brBg, sty->brBox,
                      sty->brBoxEdge, sty->brVolUp, sty->brVolDown,
                      sty->brVolPaneUp, sty->brVolPaneDown, sty->penLine };
    for (int i = 0; i < (int)(sizeof(own) / sizeof(own[0])); i++)
        if (own[i]) DeleteObject(own[i]);
    HFONT pill = sty->fontPill;   // the app's, see chart.h
    ZeroMemory(sty, sizeof(*sty));
    sty->fontPill = pill;
}

// The background under everything: the app's cached watermark bitmap when it
// has one (built for this size), otherwise the flat background color. BitBlt
// REPLACES FillRect, it does not come in addition. Also sets the text mode
// every layer relies on.
void ChartDrawBackground(HDC hdc, int W, int H, HDC wmDC, HBRUSH brBg) {
    RECT rcAll = { 0, 0, W, H };
    if (wmDC) BitBlt(hdc, 0, 0, W, H, wmDC, 0, 0, SRCCOPY);
    else      FillRect(hdc, &rcAll, brBg);   // fallback, the watermark is decoration
    SetBkMode(hdc, TRANSPARENT);
}

// Phase 46: does a candle drawn in [i0, i1) put a pixel inside rc? The same
// x and y as the candle loop in ChartDrawBody (pitfall 14): the wick is the
// column cx, rows [yHigh, yLow) - LineTo leaves out its end point - and the
// body [cx - bodyW / 2, + bodyW) x [yTop, yBot). Only the candles whose body
// can reach rc's columns are read, a handful for a three-letter label.
static BOOL CandlesHitRect(const Candle* candles, int i0, int i1, int left, int top, int ch,
                           double dStart, double slot, int bodyW, double maxP, double range,
                           const RECT* rc) {
    if (slot <= 0.0) return FALSE;
    int a = (int)floor(dStart + (double)(rc->left - left - bodyW) / slot) - 1;
    int b = (int)ceil(dStart + (double)(rc->right - left + bodyW) / slot) + 1;
    if (a < i0) a = i0;
    if (b > i1) b = i1;
    for (int i = a; i < b; ++i) {
        const Candle* c = &candles[i];
        int cx = left + (int)(((double)i - dStart + 0.5) * slot);
        int x0 = cx - bodyW / 2, x1 = x0 + bodyW;
        if (x1 <= rc->left || x0 >= rc->right) continue;
        int yHigh  = top + (int)(((maxP - c->high)  / range) * ch);
        int yLow   = top + (int)(((maxP - c->low)   / range) * ch);
        int yOpen  = top + (int)(((maxP - c->open)  / range) * ch);
        int yClose = top + (int)(((maxP - c->close) / range) * ch);
        int yTop = (yOpen < yClose) ? yOpen : yClose;
        int yBot = (yOpen < yClose) ? yClose : yOpen;
        if (yBot - yTop < 1) yBot = yTop + 1;
        if (cx >= rc->left && cx < rc->right && yHigh < rc->bottom && yLow > rc->top) return TRUE;
        if (yTop < rc->bottom && yBot > rc->top) return TRUE;
    }
    return FALSE;
}

// The chart body: everything from the chart geometry down. The header and the
// status text are the app's (DrawHeader / DrawEmptyState in tickc.c), the
// background is ChartDrawBackground. Reads the DISPLAY in st, the data in in,
// the GDI objects in sty; writes only st (front shift, display sync, probe
// fields). W and H are the client size; the geometry follows from them and
// in->desktop.
// Every fixed length below is a 96-dpi value through PX (phase 36).
#define PX(v) ChartPx(dpi, (v))
void ChartDrawBody(HDC hdc, int W, int H, ChartState* st, const ChartData* in,
                   const ChartStyle* sty) {
    int n = in->count;
    wchar_t buf[64];
    int dpi = (sty->dpi > 0) ? sty->dpi : CHART_DPI_BASE;
    ChartRect g = ChartGeometry(W, H, in->desktop, dpi, in->band, in->vol);
    // Phase 45: the band's own height, not its bottom against the price
    // pane's. Without a band bandTop = bandBottom = the bottom before the
    // volume pane was cut out, so with the volume pane alone the old test
    // (bandBottom > bottom) found a band of height 0 on the pane's bottom row
    // and drew its top line there - a stray rule under the bars - and, while
    // the RSI faded out, its value tag in the volume pane's column.
    BOOL bandOn = (g.bandBottom > g.bandTop);   // phase 39
    BOOL volPane = (g.volBottom > g.bottom);   // phase 43: the volume has a pane
    int  axisB  = ChartPanesBottom(&g);   // the lowest pane: the time axis sits under it
    // The tags on the price axis: [y - tagHalf, y + tagHalf), and two tags
    // closer than tagH collide (16 px at 96 dpi).
    int tagHalf = PX(8), tagH = 2 * tagHalf;

    // --- Chart geometry ---
    int left = g.left, top = g.top, right = g.right, bottom = g.bottom;
    int cw = g.cw, ch = g.ch;
    int edge = g.edge;   // the axis edge; right is where the candles end
    if (cw <= 0 || ch <= 0) return;

    // Called under the lock, so frontShift can be read directly.
    ApplyFrontShift(st, in->frontShift);
    if (!st->dispValid) SyncDisp(st, in->candles, n);

    // The drawing reads the DISPLAY. The target (vs, vc) is used only for the
    // span text in the header and to work out what the display should ease
    // TOWARDS - and the latter happens in WM_TIMER, not here.
    double dStart = st->dispStart;
    double dCount = st->dispCount;
    if (dCount < 1.0) dCount = 1.0;

    double minP = st->dispMin, maxP = st->dispMax;
    double range = maxP - minP;
    if (range < 1e-9) range = 1.0;

    // --- Clipping to the chart surface ---
    // With dStart = 142.7 there are half candles at both edges, and in the
    // middle of the Y easing wicks lie above maxP and below minP. Grid and
    // candles are therefore drawn inside a clip region bounded to rcChart,
    // and nothing else: the axis texts, the stamp and the header lie outside
    // the surface and are drawn after SelectClipRgn(NULL).
    //
    // The right edge is EXCLUSIVE: column x = edge belongs to the axis margin,
    // and the grid ends at edge - 1. With edge + 1 here we measured candle
    // pixels in that column in 21 of 240 frames during panning, maximized
    // (phase 6).
    //
    // The clip goes to edge, not to right: the candles stay inside right by
    // themselves (slot is computed from cw), while the grid and the last-price
    // line must cross the air gap and reach all the way to the axis (phase 15).
    //
    // The bottom is INCLUSIVE (bottom + 1): grid line i = 4 lies at
    // y = bottom, and so does the wick of the candle with the lowest price. A
    // [top, bottom) clip would erase the bottom line (and the wick, which
    // still stands there when a pane below takes line 4's place, phase 45).
    RECT rcChart = { left, top, edge, bottom + 1 };
    IntersectClipRect(hdc, rcChart.left, rcChart.top, rcChart.right, rcChart.bottom);

    // --- Grid ---
    // Edge to edge puts line i = 0 at y = 0 and i = 4 at y = H - 1. That is a
    // 1 px frame around the whole screen - the very interference desktop mode
    // is supposed to be free of. The three inner lines carry the spatial
    // frame of reference alone.
    //
    // Phase 45: with a pane under the price (volume or RSI), line 4 is left
    // out. The pane's top line stands PANE_GAP (6 px) under it, and the two
    // read as a double rule; the pane's line is the one separator, as in
    // Bloomberg. The price label on row 4 stays - it is the pane's low.
    HPEN hOldPen = (HPEN)SelectObject(hdc, sty->penGrid);
    int gi0 = in->desktop ? 1 : 0;
    int gi1 = (in->desktop || volPane || bandOn) ? 3 : 4;
    for (int i = gi0; i <= gi1; ++i) {
        int y = top + (ch * i) / 4;
        MoveToEx(hdc, left, y, NULL);
        LineTo(hdc, edge, y);
    }
    SelectObject(hdc, hOldPen);

    // --- Candlesticks ---
    double slot = (double)cw / dCount;
    int bodyW = (int)(slot * 0.62);
    if (bodyW < 1)  bodyW = 1;
    if (bodyW > PX(18)) bodyW = PX(18);   // prevents chunky candles at full zoom-in

    // The loop still runs over VISIBLE candles, not over the whole history:
    // i1 - i0 is dCount + 1 rounded. The performance characteristics from
    // phase 1 stand.
    int i0 = (int)floor(dStart);
    int i1 = (int)ceil(dStart + dCount);
    if (i0 < 0) i0 = 0;
    if (i1 > n) i1 = n;

    // --- Volume bars behind the candles (phase 21) ---
    // Only when the volume is on and has no pane of its own (phase 43: the
    // smallest panels with the RSI band): in the bottom VOL_FRAC of the price
    // pane, inside its clip. With a pane the bars are drawn there, below.
    if (in->vol && !volPane)
        DrawVolumeBars(hdc, st, in, sty, left, dStart, slot, bodyW, i0, i1,
                       bottom, ChartVolBarsH(&g), FALSE);

    // --- Alert lines (phase 23) ---
    // Behind the candles and above the bars, inside the same clip, from left
    // to edge like the grid: a level is a reference, and the candles are what
    // is read. Muted amber (CLR_ALERT_LINE), solid - dashed is the last price,
    // and dotted is the crosshair. DC_PEN, so no new GDI objects. Drawn in
    // both modes: on the desktop the line is a spatial reference like the
    // grid, while the tag with the number exists only in the panel (phase 14).
    // Outside [top, bottom] nothing is drawn, same rule as the last price.
    {
        int na = in->alertCount;
        if (na > 0) {
            SelectObject(hdc, GetStockObject(DC_PEN));
            SetDCPenColor(hdc, sty->clr.alertLine);
            for (int a = 0; a < na; ++a) {
                int y = AlertY(st, &g, fabs(in->alerts[a]));
                if (y < top || y > bottom) continue;
                MoveToEx(hdc, left, y, NULL);
                LineTo(hdc, edge, y);
            }
        }
    }

    // --- Today's high and low (phase 27) ---
    // Behind the candles, like the alert lines: a reference, not what is read.
    // Two dashed, neutral lines at the highest high and lowest low of today's
    // UTC day, from the day's first candle in to the axis - so the line also
    // shows WHERE the day begins. If the view is in yesterday (the x lies to
    // the right of the surface), nothing is drawn, and the same outside
    // [top, bottom] - same rule as the last price and the alerts.
    //
    // Belongs to the indicator toggle (M, the MA pill, "Indicators" in the
    // tray menu) and fades with it. So they are OFF on the desktop by
    // default (phase 26), without a new key in the registry, and the toolbar
    // - already full at the minimum width - needs no new pill.
    //
    // Yesterday's close, high and low (phase 28) are the same kind of
    // reference and are drawn in the same block, from the same x: levels FOR
    // TODAY, so the candles that made them get no line across them. Cooler
    // color and its own pattern (see CLR_PREV). If yesterday is not complete
    // in the buffer, they are not drawn. All five levels sit in a table in
    // the axis column's rank order, so the collision rule, the tags and the
    // legend's "struck" are one loop each.
    // yLine is the rows that were actually drawn; the axis tags below read
    // yLvl.
    static const int LVL_DASH[LVL_COUNT] = { SESS_DASH_ON, SESS_DASH_ON,
                                             PREV_DASH_CLOSE, PREV_DASH_HL, PREV_DASH_HL };
    int    indT = (int)(st->dispIndF * 255.0 + 0.5);
    double lvlP[LVL_COUNT] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
    BOOL   lvlOn[LVL_COUNT] = { FALSE, FALSE, FALSE, FALSE, FALSE };
    int    yLvl[LVL_COUNT];    // the axis tags; the collision rule can strike them
    int    yLine[LVL_COUNT];   // the lines that were drawn
    for (int q = 0; q < LVL_COUNT; ++q) { yLvl[q] = INT_MIN; yLine[q] = INT_MIN; }
    int    lvlXs = left;       // where the level lines begin (the labels, phase 29)
#ifdef TICKER_PROBE
    LARGE_INTEGER sessQ0, sessQ1, sessQf, prevQ0, prevQ1;
    QueryPerformanceCounter(&sessQ0);
    prevQ0 = sessQ0; prevQ1 = sessQ0;
#endif
    if (indT > 0) {
        BOOL sessFull = FALSE;
        int  sessS = SessionStart(in->candles, n, in->intervalMs, in->histDone, &sessFull);
        int  xs = (sessS >= 0) ? left + (int)floor(((double)sessS - dStart) * slot) : edge;
        if (sessS >= 0 && sessFull && xs < edge) {
            SessionHiLo(in->candles, sessS, n, &lvlP[0], &lvlP[1]);
            lvlOn[0] = lvlOn[1] = TRUE;
#ifdef TICKER_PROBE
            QueryPerformanceCounter(&prevQ0);
#endif
            int  prevE = -1;
            BOOL prevFull = FALSE;
            int  prevS = PrevSession(in->candles, n, in->intervalMs, in->histDone, &prevE, &prevFull);
            if (prevS >= 0 && prevFull) {
                lvlP[2] = in->candles[prevE - 1].close;
                SessionHiLo(in->candles, prevS, prevE, &lvlP[3], &lvlP[4]);
                lvlOn[2] = lvlOn[3] = lvlOn[4] = TRUE;
            }
#ifdef TICKER_PROBE
            QueryPerformanceCounter(&prevQ1);
#endif
            lvlXs = (xs > left) ? xs : left;
            SelectObject(hdc, GetStockObject(DC_PEN));
            for (int q = 0; q < LVL_COUNT; ++q) {
                if (!lvlOn[q]) continue;
                double yy = ((maxP - lvlP[q]) / range) * (double)ch;
                if (yy < 0.0 || yy > (double)ch) continue;
                int y = top + (int)yy;
                if (y < top || y > bottom) continue;
                SetDCPenColor(hdc, Blend(sty->clr.bg, (q < 2) ? sty->clr.session : sty->clr.prev, indT));
                DrawDashLine(hdc, xs, edge, y, left, PX(LVL_DASH[q]), PX(SESS_DASH_PERIOD));
                yLvl[q] = y;
                yLine[q] = y;
            }
        }
    }
#ifdef TICKER_PROBE
    QueryPerformanceCounter(&sessQ1);
    QueryPerformanceFrequency(&sessQf);
    st->probeSessUs = (sessQ1.QuadPart - sessQ0.QuadPart) * 1000000LL / sessQf.QuadPart;
    // Field 56: the computation of yesterday (binary search + one pass over
    // the day). The three lines are in field 45 together with today's.
    st->probePrevUs = (prevQ1.QuadPart - prevQ0.QuadPart) * 1000000LL / sessQf.QuadPart;
#endif

    // The candles are drawn with the system DC_PEN and DC_BRUSH, colored per
    // candle, instead of four dedicated pens and brushes. That is four GDI
    // objects fewer; the persistent buffer takes two, so the count at rest
    // goes down by two. Solid 1 px in both cases, so the pixels are the same.
    // The color is set only when it changes.
    SelectObject(hdc, GetStockObject(DC_PEN));
    SelectObject(hdc, GetStockObject(DC_BRUSH));
    int curUp = -1;
    for (int i = i0; i < i1; ++i) {
        const Candle* c = &in->candles[i];
        int up = (c->close >= c->open);

        int cx     = left + (int)(((double)i - dStart + 0.5) * slot);
        int yHigh  = top + (int)(((maxP - c->high)  / range) * ch);
        int yLow   = top + (int)(((maxP - c->low)   / range) * ch);
        int yOpen  = top + (int)(((maxP - c->open)  / range) * ch);
        int yClose = top + (int)(((maxP - c->close) / range) * ch);

        if (up != curUp) {
            SetDCPenColor(hdc,   up ? sty->clr.up : sty->clr.down);
            SetDCBrushColor(hdc, up ? sty->clr.up : sty->clr.down);
            curUp = up;
        }

        // Wick
        MoveToEx(hdc, cx, yHigh, NULL);
        LineTo(hdc, cx, yLow);

        // Body
        int yTop = (yOpen < yClose) ? yOpen : yClose;
        int yBot = (yOpen < yClose) ? yClose : yOpen;
        if (yBot - yTop < 1) yBot = yTop + 1; // doji -> at least 1px
        Rectangle(hdc, cx - bodyW / 2, yTop, cx - bodyW / 2 + bodyW, yBot);
    }

    // --- Moving averages (phase 25) ---
    // ABOVE the candles, inside the same clip: a 1 px muted line behind
    // saturated candle bodies would vanish exactly where it crosses the price,
    // which is where it is read. Below the last-price line, the crosshair and
    // the overlay. Both modes - a curve is not text (phase 14). The price axis
    // does NOT see the averages: PriceRange is untouched, and a line outside
    // the view's price range is clipped, as in TradingView. Otherwise a
    // zoomed-in view would be squeezed by an average lying far away.
    //
    // dispIndF (0..1) fades the color towards CLR_BG - the MA toggle. DC_PEN,
    // so no new GDI objects. EMA last: the long line lies on top where the
    // two cross.
    //
    // The legend (below) shows the value at the candle under the crosshair,
    // otherwise at the last visible candle. Same condition the crosshair uses.
    //
    // VWAP (phase 27) is the third line in the same block, and on top: gold
    // over blue and purple. indVal[2] is the value at the same candle as the
    // other two.
    double indVal[3] = { 0.0, 0.0, 0.0 };
    BOOL   indOk[3]  = { FALSE, FALSE, FALSE };
#ifdef TICKER_PROBE
    LARGE_INTEGER indQ0;
    QueryPerformanceCounter(&indQ0);
    if (indT <= 0) st->probeIndUs = 0;
#endif
    if (indT > 0) {
        int legendIdx = i1 - 1;
        if (st->hoverIdx >= 0 && st->hoverIdx < n) {
            double hr = (double)st->hoverIdx - dStart;
            if (hr >= 0.0 && hr < dCount) legendIdx = st->hoverIdx;
        }
        SelectObject(hdc, GetStockObject(DC_PEN));
        indOk[0] = DrawIndicator(hdc, in->candles, n, &g, IND_SMA_PERIOD, FALSE,
                                 Blend(sty->clr.bg, sty->clr.sma, indT), dStart, slot, i0, i1,
                                 maxP, range, legendIdx, &indVal[0]);
        indOk[1] = DrawIndicator(hdc, in->candles, n, &g, IND_EMA_PERIOD, TRUE,
                                 Blend(sty->clr.bg, sty->clr.ema, indT), dStart, slot, i0, i1,
                                 maxP, range, legendIdx, &indVal[1]);
#ifdef TICKER_PROBE
        {
            LARGE_INTEGER indQ1, indQf;
            QueryPerformanceCounter(&indQ1);
            QueryPerformanceFrequency(&indQf);
            st->probeIndUs = (indQ1.QuadPart - indQ0.QuadPart) * 1000000LL / indQf.QuadPart;
            QueryPerformanceCounter(&sessQ0);
        }
#endif
        indOk[2] = DrawVwap(hdc, in, &g, Blend(sty->clr.bg, sty->clr.vwap, indT),
                            dStart, slot, i0, i1, maxP, range, legendIdx, &indVal[2]);
#ifdef TICKER_PROBE
        QueryPerformanceCounter(&sessQ1);
        st->probeSessUs += (sessQ1.QuadPart - sessQ0.QuadPart) * 1000000LL / sessQf.QuadPart;
#endif
    }

    // The clipping MUST be removed for the axis texts - they lie in the
    // margin on the right.
    SelectClipRgn(hdc, NULL);

    // --- Volume pane (phase 43) ---
    // Under the price pane and over the RSI band, the Bloomberg order, with
    // its own clip and a grid line on its top edge - the band's shape. The
    // region follows the choice at once; the bars rise with dispVolF when
    // it is turned on, and the tag and legend fade in with them.
    int vt = g.volTop, vb = g.volBottom, vbH = ChartVolBarsH(&g);
    int volT = (int)(st->dispVolF * 255.0 + 0.5);
    if (volPane) {
        HPEN hOldV = (HPEN)SelectObject(hdc, sty->penGrid);
        MoveToEx(hdc, left, vt, NULL);
        LineTo(hdc, edge, vt);
        SelectObject(hdc, hOldV);
        IntersectClipRect(hdc, left, vt, edge, vb + 1);
        // Phase 45: the stronger pane colors in the panel only - the desktop
        // is meant to be quiet, and keeps the muted bars even in a pane.
        DrawVolumeBars(hdc, st, in, sty, left, dStart, slot, bodyW, i0, i1, vb, vbH, !in->desktop);
        SelectClipRgn(hdc, NULL);
    }
    // Which pane the pointer is in. The gap above a pane belongs to it, so
    // the horizontal always stands in a pane (phase 43: three of them).
    BOOL hoverInVol  = volPane && st->hoverY > bottom && st->hoverY <= vb;
    BOOL hoverInBand = bandOn && st->hoverY > (volPane ? vb : bottom);

    // --- RSI band (phase 39) ---
    // Under the price pane, with its own clip. A grid line on its top edge
    // separates it from the pane above; the 70 and 30 levels are
    // dashed in the crosshair's gray, like the session levels; the line is
    // the theme's rsi. The content fades with dispRsiF, the region does not:
    // it is geometry, which the hit tests read without state.
    int    rsiT = (int)(st->dispRsiF * 255.0 + 0.5);
    double rsiLegend = 0.0, rsiLast = 0.0;
    BOOL   rsiLegendOk = FALSE, rsiLastOk = FALSE;
    int    rsiLegendIdx = i1 - 1;
    if (st->hoverIdx >= 0 && st->hoverIdx < n) {
        double hr = (double)st->hoverIdx - dStart;
        if (hr >= 0.0 && hr < dCount) rsiLegendIdx = st->hoverIdx;
    }
    int bt = g.bandTop, bb = g.bandBottom, bh = bb - bt;
    int y70 = bt + (int)(((100.0 - RSI_HI) / 100.0) * (double)bh);
    int y30 = bt + (int)(((100.0 - RSI_LO) / 100.0) * (double)bh);
    if (bandOn) {
        HPEN hOldB = (HPEN)SelectObject(hdc, sty->penGrid);
        MoveToEx(hdc, left, bt, NULL);
        LineTo(hdc, edge, bt);
        SelectObject(hdc, hOldB);
        if (rsiT > 0) {
            IntersectClipRect(hdc, left, bt, edge, bb + 1);
            SelectObject(hdc, GetStockObject(DC_PEN));
            SetDCPenColor(hdc, Blend(sty->clr.bg, sty->clr.cross, rsiT));
            DrawDashLine(hdc, left, edge, y70, left, PX(SESS_DASH_ON), PX(SESS_DASH_PERIOD));
            DrawDashLine(hdc, left, edge, y30, left, PX(SESS_DASH_ON), PX(SESS_DASH_PERIOD));
            SetDCPenColor(hdc, Blend(sty->clr.bg, sty->clr.rsi, rsiT));
            DrawRsi(hdc, in->candles, n, &g, dStart, slot, i0, i1,
                    rsiLegendIdx, &rsiLegend, &rsiLegendOk, &rsiLast, &rsiLastOk);
            SelectClipRgn(hdc, NULL);
        }
    }
    // The pointer is in the band (or its gap): the crosshair's horizontal and
    // its tag belong there, not to the price pane.
    // The band's column has the price column's rank: the tag with the last
    // RSI first (like the stamp), then the crosshair's tag - not drawn within
    // tagH of the value tag, where only a strip of its number would show -
    // then the 70 and 30 labels, which give way to both.
    // Phase 44: a crosshair tag is also not drawn where its box would leave
    // its pane's rows in the column. The rows are split at each lower pane's
    // top: the price column's labels at bottom reach tagHalf under it, into
    // the gap, and a pane's own tags begin at its top (the value tags are
    // clamped to top + tagHalf). The pointer in the gap puts the horizontal
    // on the pane's top row, and a tag there reached 8 px up over the price
    // column's bottom label; the other way, the price tag at bottom covered
    // the top glyph row of a value tag clamped under the pane's top. The line
    // is drawn in both cases, as it is within tagH of the stamp.
    int yRsiV = INT_MIN, yBandCross = INT_MIN;
    if (bandOn && rsiT > 0 && rsiLastOk) {
        yRsiV = bt + (int)(((100.0 - rsiLast) / 100.0) * (double)bh);
        if (yRsiV < bt + tagHalf) yRsiV = bt + tagHalf;
        if (yRsiV > bb - tagHalf) yRsiV = bb - tagHalf;
    }
    if (hoverInBand && st->hoverIdx >= 0 && st->hoverIdx < n) {
        double hrB = (double)st->hoverIdx - dStart;
        if (hrB >= 0.0 && hrB < dCount) {
            int hyB = st->hoverY;
            if (hyB < bt) hyB = bt;
            if (hyB > bb) hyB = bb;
            // Nothing is under the band in the column, so only its top counts.
            if (hyB >= bt + tagHalf && (yRsiV == INT_MIN || abs(hyB - yRsiV) >= tagH))
                yBandCross = hyB;
        }
    }
    // The volume pane's column (phase 43) has the band's rank: the tag with
    // the last candle's volume first, at the top of its bar, then the
    // crosshair's tag, not within tagH of it. No scale labels: the pane's
    // top is the view's largest volume, and the legend reads the rest.
    int yVolV = INT_MIN, yVolCross = INT_MIN;
    const Candle* lastV = &in->candles[n - 1];
    // Phase 46: a volume above the pane's scale gets no tag - the stamp's
    // rule, "a stamp clamped to the edge places the price where it is not".
    // That is the last candle panned out of view and louder than every
    // visible one (the scale is the view's): the tag was pinned to the
    // pane's top, where no bar is. As the stamp during the Y easing, it can
    // also blink out for the moment a loud new candle's scale eases up. The
    // clamps that stay are the tag's own half height at the top and bottom.
    if (volPane && volT > 0 && st->dispVolMax > 0.0 && vbH > 0) {
        int hv = (int)(lastV->volume / st->dispVolMax * (double)vbH * st->dispVolF + 0.5);
        if (hv <= vbH) {
            yVolV = vb + 1 - hv;
            if (yVolV < vt + tagHalf) yVolV = vt + tagHalf;
            if (yVolV > vb - tagHalf) yVolV = vb - tagHalf;
        }
    }
    if (hoverInVol && st->hoverIdx >= 0 && st->hoverIdx < n) {
        double hrV = (double)st->hoverIdx - dStart;
        if (hrV >= 0.0 && hrV < dCount) {
            int hyV = st->hoverY;
            if (hyV < vt) hyV = vt;
            if (hyV > vb) hyV = vb;
            // Within its rows: under the pane's top, and above the band's
            // top when the band follows (its value tag can sit right there).
            BOOL fitsV = (hyV >= vt + tagHalf) && (!bandOn || hyV + tagHalf <= bt);
            if (fitsV && (yVolV == INT_MIN || abs(hyV - yVolV) >= tagH)) yVolCross = hyV;
        }
    }

    SelectObject(hdc, GetStockObject(BLACK_PEN));
    SelectObject(hdc, GetStockObject(NULL_BRUSH));

    // --- Price labels ---
    // A separate loop after the clipping, not in the grid loop: there they
    // would have been clipped away along with everything else outside rcChart.
    // Omega_y-axis: x in [edge + AXIS_LBL_GAP, W - AXIS_PAD_R).
    int axL = edge + PX(AXIS_LBL_GAP), axR = W - PX(AXIS_PAD_R);
    SelectObject(hdc, sty->fontAxis);
    SetTextColor(hdc, sty->clr.axis);
    // The stamp for the last price lies on top of the label at the same
    // height (see below). Both are 16 px tall, and with 11 px digits a label
    // less than 16 px away was half covered, with a truncated number visible
    // underneath. A label that would collide is therefore not drawn. Same
    // rule and same yLast as the stamp.
    // Phase 14: no measured values on the desktop. The column does not exist
    // there either - axL lies outside the surface when the geometry runs
    // edge to edge.
    int yPill = INT_MIN;
    int yCross = INT_MIN;   // the crosshair tag's row when it is to be drawn (phase 29)
    int yGhost = INT_MIN;       // the ghost tag's row when it is drawn (phase 46)
    int yGhostLine = INT_MIN;   // the ghost line's row when there is a ghost
    if (!in->desktop) {
        {
            double lp = in->candles[n - 1].close;
            int yl = top + (int)(((maxP - lp) / range) * ch);
            if (yl >= top && yl <= bottom) yPill = yl;
        }
        // The alert tags (phase 23) and the ghost tag under the pointer lie in
        // the same column and are equally tall, so they get the same collision
        // rule as the stamp: a label less than 16 px away is not drawn.
        int yTag[ALERT_MAX + 2], nTag = 0;
        int nA = in->alertCount;
        // The crosshair's axis tag (phase 29) is the only tag that moves with
        // the hand, and it had no collision rule: when the pointer was 1-15 px
        // from a label, a strip of the number stuck out beneath it. The rank
        // is the stamp, then the crosshair tag, then the rest. Same visibility
        // test and same clamping as the crosshair block at the bottom; within
        // 16 px of the stamp the crosshair tag is not drawn (the last price is
        // the number that must never be cut), and then nothing yields to it
        // either.
        if (st->hoverIdx >= 0 && st->hoverIdx < n && !hoverInBand && !hoverInVol) {
            double hrelT = (double)st->hoverIdx - dStart;
            if (hrelT >= 0.0 && hrelT < dCount) {
                int hyT = st->hoverY;
                if (hyT < top) hyT = top;
                if (hyT > bottom) hyT = bottom;
                // Phase 44: with a pane below, the tag stays above that pane's
                // top row (see yBandCross): drawn after the pane's value tag,
                // it would cover the top glyph row of that tag's number.
                int nextTop = volPane ? vt : bt;   // bt == bottom without a band
                BOOL fitsT = !(volPane || bandOn) || hyT + tagHalf <= nextTop;
                if (fitsT && (yPill == INT_MIN || abs(hyT - yPill) >= tagH)) yCross = hyT;
            }
        }
        if (yCross != INT_MIN) yTag[nTag++] = yCross;
        for (int a = 0; a < nA; ++a) {
            int y = AlertY(st, &g, fabs(in->alerts[a]));
            if (y >= top && y <= bottom) yTag[nTag++] = y;
        }
        // Phase 46: the ghost stands on the row the alert will be drawn on -
        // the row of the ROUNDED price (AlertPriceAtY), not the pointer's.
        // At BTC they are the same row or the next; on a small symbol zoomed
        // in, one cent can be 50 px, and the line previewed a level that was
        // then drawn somewhere else. A rounded row outside the pane is where
        // the alert would not be drawn either: no ghost.
        // Its rank: the ghost and the crosshair tag never exist at the same
        // time (the pointer in the column gives hoverIdx = -1), and the ghost
        // takes the crosshair tag's place - an alert under 16 px from it
        // keeps its surface and loses its number, the labels and level tags
        // give way. But it goes AHEAD of the stamp: it is the price a click
        // would set, shown nowhere else, while the last price stands in the
        // header's quote line. Phase 29 let the stamp win over the crosshair
        // tag because the hover box carries that one's numbers; here the
        // same reasoning points the other way. So within 16 px the ghost is
        // drawn after the stamp, and the stamp keeps its surface without its
        // number, as a covered alert tag does. Only while the pointer is in
        // the column.
        // Drawn after the stamp, the tag is also drawn after the panes'
        // value tags; like the crosshair tag (phase 44) it stays in the
        // price pane's rows when a pane follows, and on the last two rows
        // only the line is drawn (yGhostLine).
        if (in->axisHotY >= top && in->axisHotY <= bottom &&
            (in->alertHot < 0 || in->alertHot >= nA)) {
            int yr = AlertY(st, &g, AlertPriceAtY(st, &g, in->axisHotY));
            if (yr >= top && yr <= bottom) {
                int nextTopG = volPane ? vt : bt;   // bt == bottom without a band
                yGhostLine = yr;
                if (!(volPane || bandOn) || yr + tagHalf <= nextTopG) yGhost = yr;
            }
        }
        BOOL ghost = (yGhostLine != INT_MIN);
        if (yGhost != INT_MIN) yTag[nTag++] = yGhost;

        // The tags for today's high and low (phase 27) sit in the same column
        // and rank lowest: a tag under 16 px from the stamp, an alert, the
        // ghost tag or the other session tag is not drawn (high wins over
        // low). The grid labels give way to them as to the others. Decided
        // HERE, before the labels, and drawn after the alerts.
        // Yesterday's tags (phase 28) sit last in the same table: a tag gives
        // way to the stamp, the alerts, the ghost tag and to every level
        // ahead of it in rank that was itself kept.
        for (int q = 0; q < LVL_COUNT; ++q) {
            if (yLvl[q] == INT_MIN) continue;
            BOOL hide = (yPill != INT_MIN && abs(yLvl[q] - yPill) < tagH);
            for (int t = 0; t < nTag && !hide; ++t) if (abs(yLvl[q] - yTag[t]) < tagH) hide = TRUE;
            for (int p = 0; p < q && !hide; ++p)
                if (yLvl[p] != INT_MIN && abs(yLvl[q] - yLvl[p]) < tagH) hide = TRUE;
            if (hide) yLvl[q] = INT_MIN;
        }

        for (int i = 0; i <= 4; ++i) {
            int y = top + (ch * i) / 4;
            if (yPill != INT_MIN && abs(y - yPill) < tagH) continue;
            BOOL hidden = FALSE;
            for (int t = 0; t < nTag; ++t) if (abs(y - yTag[t]) < tagH) hidden = TRUE;
            for (int q = 0; q < LVL_COUNT; ++q) if (yLvl[q] != INT_MIN && abs(y - yLvl[q]) < tagH) hidden = TRUE;
            if (hidden) continue;
            double p = maxP - (range * i) / 4.0;
            swprintf_s(buf, 64, L"%.*f", PriceDecimals(range / 4.0), p);
            RECT rcLbl = { axL, y - tagHalf, axR, y + tagHalf };
            DrawTextW(hdc, buf, -1, &rcLbl, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }

        // --- Alert tags on the price axis (phase 23) ---
        // Same surface as the stamp and the crosshair's label: [edge + 1,
        // axR + 3) x [y - 8, y + 8). Amber surface with dark text. The tag
        // the pointer is on (alertHot) turns red like the close button: a
        // click there REMOVES the alert, and the color says so before the
        // click. The stamp for the last price is drawn afterwards and sits on
        // top - if the two are at the same height, the alert is about to fire.
        for (int a = 0; a < nA; ++a) {
            double lvl = fabs(in->alerts[a]);
            int y = AlertY(st, &g, lvl);
            if (y < top || y > bottom) continue;
            BOOL hot = (a == in->alertHot && in->alerts[a] != in->alertFresh);
            RECT rcA = { edge + 1, y - tagHalf, axR + PX(3), y + tagHalf };
            SetDCBrushColor(hdc, hot ? sty->clr.hot : sty->clr.alert);
            FillRect(hdc, &rcA, (HBRUSH)GetStockObject(DC_BRUSH));
            // If the stamp or a tag drawn later lies on top of this one, only
            // a strip of the surface sticks out - and with it a number cut
            // lengthwise. Same rule as the labels: a clipped number is worse
            // than no number (seen in PrintWindow: "81034.00" halfway under
            // the stamp). The surface is drawn, the text is not.
            // Phase 46: and the ghost tag, drawn later, in the crosshair
            // tag's rank.
            BOOL covered = (yPill != INT_MIN && abs(y - yPill) < tagH) ||
                           (yCross != INT_MIN && abs(y - yCross) < tagH) ||
                           (yGhost != INT_MIN && abs(y - yGhost) < tagH);
            for (int b = a + 1; b < nA && !covered; ++b) {
                int yb = AlertY(st, &g, fabs(in->alerts[b]));
                if (yb >= top && yb <= bottom && abs(y - yb) < tagH) covered = TRUE;
            }
            if (covered) continue;
            FormatTagPrice(hdc, lvl, range, axR - axL, buf, 64);
            SetTextColor(hdc, hot ? sty->clr.onHot : sty->clr.onAlert);
            RECT rcAT = { axL, y - tagHalf, axR, y + tagHalf };
            DrawTextW(hdc, buf, -1, &rcAT, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }

        // --- Tags for today's high and low (phase 27) ---
        // Same surface as the other tags, but muted: the box color against
        // the background and gray text, no frame and no saturated surface - a
        // tag you can click is amber, and this is not one. The surface sets
        // the number apart from the grid labels, which have the same font and
        // almost the same color. Faded with the lines. Yesterday's tags
        // (phase 28) are the same, with the number in CLR_PREV like the line.
        for (int q = 0; q < LVL_COUNT; ++q) {
            if (yLvl[q] == INT_MIN) continue;
            int y = yLvl[q];
            RECT rcS = { edge + 1, y - tagHalf, axR + PX(3), y + tagHalf };
            SetDCBrushColor(hdc, Blend(sty->clr.bg, sty->clr.box, indT));
            FillRect(hdc, &rcS, (HBRUSH)GetStockObject(DC_BRUSH));
            FormatTagPrice(hdc, lvlP[q], range, axR - axL, buf, 64);
            SetTextColor(hdc, Blend(sty->clr.bg, (q < 2) ? sty->clr.session : sty->clr.prev, indT));
            RECT rcST = { axL, y - tagHalf, axR, y + tagHalf };
            DrawTextW(hdc, buf, -1, &rcST, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }

        // The ghost: the pointer is in the price column on an empty spot,
        // and a click SETS an alert here. Its line across the chart, so the
        // user sees which candles the level cuts before the click - on the
        // row the alert will stand on (phase 46, yGhost). The framed tag is
        // drawn after the stamp, which it ranks ahead of (see there).
        if (ghost) {
            SelectObject(hdc, GetStockObject(DC_PEN));
            SetDCPenColor(hdc, (nA >= ALERT_MAX) ? sty->clr.cross : sty->clr.alertLine);
            MoveToEx(hdc, left, yGhostLine, NULL);
            LineTo(hdc, edge, yGhostLine);
        }
        SetTextColor(hdc, sty->clr.axis);   // the time axis below inherits the color
    }

    // --- RSI band: axis labels, value tag and legend (phase 39) ---
    // Panel only (phase 14: no readings on the desktop). The tag with the
    // last RSI sits in the band's column like the stamp in the price
    // column, on the box surface in the line's color; a 70 or 30 label
    // closer than tagH gives way to it, as the grid labels do to the stamp.
    if (bandOn && rsiT > 0 && !in->desktop) {
        int yv = yRsiV;
        SelectObject(hdc, sty->fontAxis);
        SetTextColor(hdc, Blend(sty->clr.bg, sty->clr.axis, rsiT));
        const int lvY[2] = { y70, y30 };
        const wchar_t* lvT[2] = { L"70", L"30" };
        for (int q = 0; q < 2; ++q) {
            if (yv != INT_MIN && abs(lvY[q] - yv) < tagH) continue;
            if (yBandCross != INT_MIN && abs(lvY[q] - yBandCross) < tagH) continue;
            RECT rcL = { axL, lvY[q] - tagHalf, axR, lvY[q] + tagHalf };
            DrawTextW(hdc, lvT[q], -1, &rcL, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }
        if (yv != INT_MIN) {
            RECT rcV = { edge + 1, yv - tagHalf, axR + PX(3), yv + tagHalf };
            SetDCBrushColor(hdc, Blend(sty->clr.bg, sty->clr.box, rsiT));
            FillRect(hdc, &rcV, (HBRUSH)GetStockObject(DC_BRUSH));
            swprintf_s(buf, 64, L"%.2f", rsiLast);
            SetTextColor(hdc, Blend(sty->clr.bg, sty->clr.rsi, rsiT));
            RECT rcVT = { axL, yv - tagHalf, axR, yv + tagHalf };
            DrawTextW(hdc, buf, -1, &rcVT, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }
        // The legend in the band's top left corner, whole or not at all.
        if (rsiLegendOk) swprintf_s(buf, 64, L"RSI %d  %.2f", RSI_PERIOD, rsiLegend);
        else             swprintf_s(buf, 64, L"RSI %d  -", RSI_PERIOD);
        SIZE lsz = { 0, 0 };
        GetTextExtentPoint32W(hdc, buf, (int)wcslen(buf), &lsz);
        int lx = left + PX(6), ly = bt + PX(3);
        if (lx + lsz.cx <= right - PX(6) && ly + lsz.cy <= bb) {
            // In a low band the 70 line runs through the text: then, and
            // only then, an opaque background - the rule the price legend
            // has for the level lines (phase 27).
            BOOL struckB = (y70 >= ly - 1 && y70 <= ly + lsz.cy);
            if (struckB) { SetBkColor(hdc, sty->clr.bg); SetBkMode(hdc, OPAQUE); }
            SetTextColor(hdc, Blend(sty->clr.bg, sty->clr.rsi, rsiT));
            ExtTextOutW(hdc, lx, ly, 0, NULL, buf, (int)wcslen(buf), NULL);
            if (struckB) SetBkMode(hdc, TRANSPARENT);
        }
        SetTextColor(hdc, sty->clr.axis);
    }

    // --- Volume pane: value tag and legend (phase 43) ---
    // Panel only, like the band's. The tag carries the last candle's volume
    // in its direction's color on the box surface - the hover box's close
    // row - and the legend in the top left corner the volume at the
    // crosshair, else at the last visible candle, whole or not at all. The
    // legend is in the text color: the bars' colors are not text. Phase 45:
    // it no longer reads over them - see the rectangle below.
    if (volPane && volT > 0 && !in->desktop) {
        SelectObject(hdc, sty->fontAxis);
        if (yVolV != INT_MIN) {
            BOOL upV = (lastV->close >= lastV->open);
            RECT rcV = { edge + 1, yVolV - tagHalf, axR + PX(3), yVolV + tagHalf };
            SetDCBrushColor(hdc, Blend(sty->clr.bg, sty->clr.box, volT));
            FillRect(hdc, &rcV, (HBRUSH)GetStockObject(DC_BRUSH));
            FormatVolume(lastV->volume, buf, 64);
            SetTextColor(hdc, Blend(sty->clr.bg, upV ? sty->clr.up : sty->clr.down, volT));
            RECT rcVT = { axL, yVolV - tagHalf, axR, yVolV + tagHalf };
            DrawTextW(hdc, buf, -1, &rcVT, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }
        int volLegendIdx = i1 - 1;
        if (st->hoverIdx >= 0 && st->hoverIdx < n) {
            double hrL = (double)st->hoverIdx - dStart;
            if (hrL >= 0.0 && hrL < dCount) volLegendIdx = st->hoverIdx;
        }
        wchar_t vtxt[24];
        if (volLegendIdx >= 0) FormatVolume(in->candles[volLegendIdx].volume, vtxt, 24);
        else                   wcscpy_s(vtxt, 24, L"-");
        swprintf_s(buf, 64, L"Vol  %s", vtxt);
        SIZE lszV = { 0, 0 };
        GetTextExtentPoint32W(hdc, buf, (int)wcslen(buf), &lszV);
        int lxV = left + PX(6), lyV = vt + PX(3);
        if (lxV + lszV.cx <= right - PX(6) && lyV + lszV.cy <= vb) {
            // Phase 45: on a rectangle of the background, 3 px wider and 2 px
            // taller on each side than the text. The pane's bars are strong
            // now, and text over a bar cannot reach 4.5:1 in the light theme;
            // on bg it is the header's pair. The top stays under the pane's
            // top line, and the bottom inside the pane.
            RECT rcLV = { lxV - PX(3), lyV - PX(2), lxV + lszV.cx + PX(3), lyV + lszV.cy + PX(2) };
            if (rcLV.top < vt + 1) rcLV.top = vt + 1;
            if (rcLV.bottom > vb + 1) rcLV.bottom = vb + 1;
            FillRect(hdc, &rcLV, sty->brBg);
            SetTextColor(hdc, Blend(sty->clr.bg, sty->clr.text, volT));
            ExtTextOutW(hdc, lxV, lyV, 0, NULL, buf, (int)wcslen(buf), NULL);
        }
        SetTextColor(hdc, sty->clr.axis);
    }

    // --- Afterglow (phase 23) ---
    // An alert that has fired is REMOVED; left behind is a line in full amber
    // that fades out over a couple of seconds (alertFlashF, 1..0, eased by the
    // clock), so whoever looks at the panel sees WHERE it went off. Blended
    // against CLR_BG like everything else here - opaque over the background
    // is identical to alpha. Over the candles, not behind: this is a signal,
    // not a reference. Both modes.
    if (in->alertFlashF > 0.0) {
        int y = AlertY(st, &g, in->alertFlashLevel);
        if (y >= top && y <= bottom) {
            int t = (int)(in->alertFlashF * 255.0 + 0.5);
            SelectObject(hdc, GetStockObject(DC_PEN));
            SetDCPenColor(hdc, Blend(sty->clr.bg, sty->clr.alert, t));
            MoveToEx(hdc, left, y, NULL);
            LineTo(hdc, edge, y);
        }
    }

    // --- Time axis (Omega_x-axis) ---
    // Text only, no axis line. The labels sit under the candle's midpoint in
    // the band [bottom + 2, H - 1], and never outside [left, right]: under
    // the right column lie the price axis's bottom label and the stamp.
    //
    // Which candles get a label is anchored in TIME, not in the index i0.
    // Relative to i0 the labels would jump to new candles in every frame of
    // a pan; relative to the absolute index they would jump one candle every
    // time a candle is dropped at the front when the buffer is full.
    // openTime / intervalMs is stable through both.
    //
    // O(number of labels): one modulo to find the first label, then steps
    // of S straight in candles[]. No allocation.
    //
    // Phase 45: two forms on one axis ("14:35", and "21 Sep" where a day
    // begins), so the spacing is measured on the widest (ChartTimeLabelW),
    // not on the first label, and the edge test on each label's own width.
    if (i0 < i1 && !in->desktop) {
        wchar_t tl[24];
        int minDx = ChartTimeLabelW(hdc, in->intervalMs)   // the axis font is selected
                  + PX(TIME_LBL_GAP);
        if (minDx < PX(TIME_DX_MIN)) minDx = PX(TIME_DX_MIN);
        long long iv = (in->intervalMs > 0) ? in->intervalMs : 60000LL;
        int step = NiceTimeStep(TimeTickStep(dCount, cw, minDx), iv);

        // Anchored in LOCAL time, so 6 h steps land on 00, 06, 12 and 18
        // here and not on 02, 08 ... (UTC + 2 in summer). One offset for the
        // whole view, the one the labels are written with.
        long long t0 = in->candles[i0].openTime, tzMs = in->utcOffsetMs;
        long long slotNo = (t0 + tzMs) / iv;
        int rem = (int)(slotNo % step);
        int k = i0 + ((rem == 0) ? 0 : (step - rem));

        UINT oldAlign = SetTextAlign(hdc, TA_CENTER | TA_TOP);
        for (; k < i1; k += step) {
            int x = left + (int)(((double)k - dStart + 0.5) * slot);
            FormatTimeAs(in->candles[k].openTime, in->intervalMs, in->utcOffsetMs,
                         TIME_AXIS, tl, 24);
            int tlLen = (int)wcslen(tl);
            SIZE tsz = { 0, 0 };
            GetTextExtentPoint32W(hdc, tl, tlLen, &tsz);
            if (x - tsz.cx / 2 < left || x + (tsz.cx + 1) / 2 > right) continue;
            ExtTextOutW(hdc, x, axisB + PX(2), 0, NULL, tl, tlLen, NULL);
        }
        SetTextAlign(hdc, oldAlign);
    }

    // --- Legend for the moving averages (phase 25) ---
    // Top left of the chart surface, in the lines' own colors: the color
    // is what says which line is which, and the number is the average at
    // the candle under the crosshair (no crosshair: the last visible
    // candle). An average that is not defined there - fewer than period
    // candles before it - gets a dash. The axis font, which is selected
    // here; transparent over the candles, like the watermark under them.
    // Not on the desktop (phase 14: no readings there), and only when the
    // WHOLE text fits in the surface - same rule as the percentage in the
    // header: a clipped number is a wrong number.
#ifdef TICKER_PROBE
    if (!(indT > 0 && !in->desktop)) st->probeLblMask = 0;
#endif
    if (indT > 0 && !in->desktop) {
        RECT rcLegend = { 0, 0, 0, 0 };   // empty when the legend did not fit
        wchar_t lg[96];
        int len1, lenAll;
        if (indOk[0]) swprintf_s(lg, 96, L"SMA %d  %.2f    ", IND_SMA_PERIOD, indVal[0]);
        else          swprintf_s(lg, 96, L"SMA %d  -    ", IND_SMA_PERIOD);
        len1 = (int)wcslen(lg);
        if (indOk[1]) swprintf_s(lg + len1, 96 - len1, L"EMA %d  %.2f", IND_EMA_PERIOD, indVal[1]);
        else          swprintf_s(lg + len1, 96 - len1, L"EMA %d  -", IND_EMA_PERIOD);
        lenAll = (int)wcslen(lg);
        // VWAP (phase 27) is the third item. If all three do not fit, the
        // VWAP item drops out and the two averages stand as before; if those
        // do not fit either, nothing is shown. Each item whole or not at all.
        int len2 = lenAll;
        if (indOk[2]) swprintf_s(lg + len2, 96 - len2, L"    VWAP  %.2f", indVal[2]);
        else          swprintf_s(lg + len2, 96 - len2, L"    VWAP  -");
        int len3 = (int)wcslen(lg);
        SIZE szAll = { 0, 0 }, sz1 = { 0, 0 }, sz3 = { 0, 0 };
        GetTextExtentPoint32W(hdc, lg, lenAll, &szAll);
        GetTextExtentPoint32W(hdc, lg, len1, &sz1);
        GetTextExtentPoint32W(hdc, lg, len3, &sz3);
        int lx = left + PX(6), ly = top + PX(4);
        if (lx + szAll.cx <= right - PX(6) && ly + szAll.cy <= bottom) {
            // Today's high (phase 27) lies 8 % below the surface's top when
            // today's top is the view's, and on a short panel that is in the
            // middle of this row: the dashes ran right through the digits
            // (seen in a capture at 560x300). Then - and only then - the text
            // gets an opaque background, so the numbers stay whole and the
            // line continues behind them.
            // Phase 46: the same for every horizontal drawn before this - the
            // alert lines, the ghost's and the afterglow. They are drawn
            // from left to edge, and an alert set near the top ran through
            // the numbers.
            BOOL struck = FALSE;
            for (int q = 0; q < LVL_COUNT; ++q)
                if (yLine[q] != INT_MIN && yLine[q] >= ly - 1 && yLine[q] <= ly + szAll.cy) struck = TRUE;
            for (int a = 0; a < in->alertCount && !struck; ++a) {
                int ya = AlertY(st, &g, fabs(in->alerts[a]));
                if (ya >= top && ya <= bottom && ya >= ly - 1 && ya <= ly + szAll.cy) struck = TRUE;
            }
            if (yGhostLine != INT_MIN && yGhostLine >= ly - 1 && yGhostLine <= ly + szAll.cy) struck = TRUE;
            if (in->alertFlashF > 0.0) {
                int yf = AlertY(st, &g, in->alertFlashLevel);
                if (yf >= top && yf <= bottom && yf >= ly - 1 && yf <= ly + szAll.cy) struck = TRUE;
            }
            if (struck) { SetBkColor(hdc, sty->clr.bg); SetBkMode(hdc, OPAQUE); }
            SetTextColor(hdc, Blend(sty->clr.bg, sty->clr.sma, indT));
            ExtTextOutW(hdc, lx, ly, 0, NULL, lg, len1, NULL);
            SetTextColor(hdc, Blend(sty->clr.bg, sty->clr.ema, indT));
            ExtTextOutW(hdc, lx + sz1.cx, ly, 0, NULL, lg + len1, lenAll - len1, NULL);
            if (lx + sz3.cx <= right - PX(6)) {
                SetTextColor(hdc, Blend(sty->clr.bg, sty->clr.vwap, indT));
                ExtTextOutW(hdc, lx + szAll.cx, ly, 0, NULL, lg + len2, len3 - len2, NULL);
            }
            if (struck) SetBkMode(hdc, TRANSPARENT);
            rcLegend.left = lx; rcLegend.top = ly;
            rcLegend.right = lx + sz3.cx; rcLegend.bottom = ly + szAll.cy;
        }

        // --- Labels on the level lines (phase 29) ---
        // Five horizontal lines without names had to be read from the dash
        // pattern. The label sits at the line's LEFT end - by the axis are
        // the newest candles, and the axis tag carries the number there -
        // just above the line, below it when there is no room above. Trading
        // jargon, like VWAP and O H L C elsewhere in the panel: high/low of
        // day, previous day's close/high/low.
        // HERE and not in the level block: that is drawn behind the candles,
        // and text there would be painted over. The axis font (selected
        // above), the line's color, faded.
        // The text never touches the line's own row or the rows next to it,
        // so the dash pattern stays clean. In rank: a label that would hit
        // the legend or a label ahead of it in rank is not drawn - two
        // levels four pixels apart get one label, not two on top of each
        // other. Below 200 px of surface they all drop out.
        static const wchar_t* const LVL_NAME[LVL_COUNT] = { L"HOD", L"LOD", L"PDC", L"PDH", L"PDL" };
#ifdef TICKER_PROBE
        LARGE_INTEGER lblQ0, lblQ1, lblQf;
        QueryPerformanceCounter(&lblQ0);
        st->probeLblMask = 0;
#endif
        if (right - left >= PX(200)) {
            RECT placed[LVL_COUNT + 1];
            int  nPlaced = 0;
            if (rcLegend.right > rcLegend.left) placed[nPlaced++] = rcLegend;
            for (int q = 0; q < LVL_COUNT; ++q) {
                if (yLine[q] == INT_MIN) continue;
                SIZE szN = { 0, 0 };
                GetTextExtentPoint32W(hdc, LVL_NAME[q], 3, &szN);
                // Phase 46: the label gives way to the candles. The line's
                // left end is today's first candle, and on a young day that
                // is among the newest, by the axis: LOD sat above its line
                // on the candles that made the low, and a wick ran through
                // the letters (the review's U7, seen at 15m and 1h). A
                // background box would wipe out the candles, which are what
                // is read; so the label goes below its line when the candles
                // are above it, and when they are on both sides it is not
                // drawn - the axis tag carries the level. HOD above and LOD
                // below can never meet today's candles.
                // Another level's line through the letters is the same
                // strike (seen when LOD went below its line onto PDL's), so
                // a side with one on its rows is not taken either.
                // The rank test against the legend and the labels ahead
                // decides after the side, as before: a label is not pushed
                // to the other side by another label.
                RECT rcN = { 0, 0, 0, 0 };
                BOOL sideOk = FALSE;
                for (int side = 0; side < 2 && !sideOk; ++side) {
                    int yT = (side == 0) ? yLine[q] - PX(2) - szN.cy : yLine[q] + PX(3);
                    RECT rc = { lvlXs + PX(4), yT, lvlXs + PX(4) + szN.cx, yT + szN.cy };
                    if (rc.top < top || rc.bottom > bottom || rc.right > right) continue;
                    BOOL lined = FALSE;
                    for (int p = 0; p < LVL_COUNT; ++p)
                        if (p != q && yLine[p] != INT_MIN && yLine[p] >= rc.top - 1 && yLine[p] <= rc.bottom)
                            lined = TRUE;
                    if (lined) continue;
                    if (CandlesHitRect(in->candles, i0, i1, left, top, ch, dStart, slot, bodyW,
                                       maxP, range, &rc)) continue;
                    rcN = rc;
                    sideOk = TRUE;
                }
                if (!sideOk) continue;
                BOOL hit = FALSE;
                for (int t = 0; t < nPlaced && !hit; ++t) {
                    RECT tmp;
                    if (IntersectRect(&tmp, &rcN, &placed[t])) hit = TRUE;
                }
                if (hit) continue;
                placed[nPlaced++] = rcN;
                SetTextColor(hdc, Blend(sty->clr.bg, (q < 2) ? sty->clr.session : sty->clr.prev, indT));
                ExtTextOutW(hdc, rcN.left, rcN.top, 0, NULL, LVL_NAME[q], 3, NULL);
#ifdef TICKER_PROBE
                st->probeLblMask |= (1 << q);
#endif
            }
        }
#ifdef TICKER_PROBE
        QueryPerformanceCounter(&lblQ1);
        QueryPerformanceFrequency(&lblQf);
        st->probeLblUs = (lblQ1.QuadPart - lblQ0.QuadPart) * 1000000LL / lblQf.QuadPart;
#endif
    }

    // --- Last price: dashed line + axis stamp ---
    // Sits HERE on purpose: over the candles and the grid, under the
    // crosshair and the overlay. And before the early return in the
    // crosshair block below - without hover the line must still be drawn.
    //
    // The color follows the sign of the momentary change, dP = P_t - P_t-1,
    // that is the last close against the previous one. That is a different
    // rule from the candles' own (close against open in the SAME candle), so
    // they can point different ways: a green candle that still lies below
    // the previous close gives a red line. That is intended - the line
    // answers "where are we against the previous close", not "how is this
    // candle going".
    {
        const Candle* lastC = &in->candles[n - 1];
        double lastP = lastC->close;
        double prevP = (n >= 2) ? in->candles[n - 2].close : lastC->open;
        BOOL   lastUp = (lastP >= prevP);

        int yLast = top + (int)(((maxP - lastP) / range) * ch);

        // Outside the visible price range nothing is drawn. A stamp clamped
        // to the edge would place the price somewhere it is not.
        if (yLast >= top && yLast <= bottom) {
            int xLast = left + (int)(((double)(n - 1) - dStart + 0.5) * slot);
            if (xLast < left)  xLast = left;
            if (xLast > right) xLast = right;

            // The line runs from the last candle all the way to the stamp
            // (phase 15). Dashed over the data surface, SOLID over the gap:
            // PS_DASH ends wherever the pattern happens to be, and at 1004 px
            // width the end landed in an "off" interval - measured as a black
            // hole against the stamp. The bridge over the gap is the one part
            // that MUST hit, so it is drawn without a pattern.
            //
            // edge + 1 because LineTo does not draw the end point: without
            // that one pixel the column x = edge stays empty, and the stamp
            // only starts at edge + 1.
            HPEN penLast = lastUp ? sty->penLastUp : sty->penLastDown;
            HPEN hOld2 = (HPEN)SelectObject(hdc, penLast);
            MoveToEx(hdc, xLast, yLast, NULL);
            LineTo(hdc, right, yLast);

            SelectObject(hdc, GetStockObject(DC_PEN));
            SetDCPenColor(hdc, lastUp ? sty->clr.up : sty->clr.down);
            MoveToEx(hdc, right, yLast, NULL);
            LineTo(hdc, edge + 1, yLast);
            SelectObject(hdc, hOld2);

            // The axis stamp overwrites the grid label at this height, so
            // that there are not two numbers on top of each other.
            // The surface gets 3 px of air on each side of the text; the text
            // itself stays inside axR.
            //
            // Phase 16: the stamp is drawn in BOTH modes. On the desktop it is
            // the only text left - axis labels, time axis and header are
            // still gone - and the height follows the surface instead of the
            // panel's fixed 16 px.
            {
                int half = in->desktop ? DeskPillH(H) / 2 : tagHalf;

                RECT rcPill = { edge + 1, yLast - half, axR + PX(3), yLast + half };
                SetDCBrushColor(hdc, lastUp ? sty->clr.up : sty->clr.down);
                FillRect(hdc, &rcPill, (HBRUSH)GetStockObject(DC_BRUSH));

                // "Precise value text": two decimals where they fit, otherwise
                // the same resolution as the axis. The width is measured on
                // the fully formatted string - bug #5 again.
                HFONT fPill = (in->desktop && sty->fontPill) ? sty->fontPill
                                                           : sty->fontAxis;
                SelectObject(hdc, fPill);
                int pillDec = 2;
                swprintf_s(buf, 64, L"%.*f", pillDec, lastP);
                SIZE psz = { 0, 0 };
                int pillAvail = axR - axL;
                if (GetTextExtentPoint32W(hdc, buf, (int)wcslen(buf), &psz) &&
                    psz.cx > pillAvail) {
                    pillDec = PriceDecimals(range / 4.0);
                    swprintf_s(buf, 64, L"%.*f", pillDec, lastP);
                }

                // Dark text on the saturated surface - CLR_TEXT would drown.
                // The surface is layered with LWA_ALPHA 255, not a color key,
                // so CLR_BG is a color here and not a hole to the wallpaper.
                // Phase 46: not under 16 px from the ghost, which is drawn on
                // top of the stamp below - a strip of the number would stick
                // out of it. The surface stays, and the price is in the
                // header's quote line.
                if (yGhost == INT_MIN || abs(yLast - yGhost) >= tagH) {
                    SetTextColor(hdc, sty->clr.bg);
                    RECT rcPillTxt = { axL, yLast - half, axR, yLast + half };
                    DrawTextW(hdc, buf, -1, &rcPillTxt, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                }
            }
        }
    }

    // --- The ghost tag (phase 23; drawn here from phase 46) ---
    // Framed, not filled. If all slots are used, it is gray, and the click
    // does nothing. The price is the ROUNDED one - the one actually set, on
    // the row it will be drawn on. After the stamp, which gives way to it
    // (see yGhost): a click is about to set this price, and it is shown
    // nowhere else.
    if (yGhost != INT_MIN) {
        int y = yGhost;
        BOOL full = (in->alertCount >= ALERT_MAX);
        COLORREF gc = full ? sty->clr.dim : sty->clr.alertText;
        RECT rcG = { edge + 1, y - tagHalf, axR + PX(3), y + tagHalf };
        FillRect(hdc, &rcG, sty->brBox);
        SetDCBrushColor(hdc, gc);
        FrameRect(hdc, &rcG, (HBRUSH)GetStockObject(DC_BRUSH));
        SelectObject(hdc, sty->fontAxis);
        FormatTagPrice(hdc, AlertPriceAtY(st, &g, in->axisHotY), range, axR - axL, buf, 64);
        SetTextColor(hdc, gc);
        RECT rcGT = { axL, y - tagHalf, axR, y + tagHalf };
        DrawTextW(hdc, buf, -1, &rcGT, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }

    // --- Crosshair + hover box ---
    // Bound to the VISIBLE surface, not to the target view - the two come
    // apart in the middle of an animation.
#ifdef TICKER_PROBE
    st->probeCrossTag = 0;
#endif
    if (st->hoverIdx < 0 || st->hoverIdx >= n) return;
    double hrel = (double)st->hoverIdx - dStart;
    if (hrel < 0.0 || hrel >= dCount) return;

    const Candle* hc = &in->candles[st->hoverIdx];
    int hx = left + (int)((hrel + 0.5) * slot);
    int hy = st->hoverY;
    if (hoverInBand) {                 // phase 39: the horizontal lives in the band
        if (hy < bt) hy = bt;
        if (hy > bb) hy = bb;
    } else if (hoverInVol) {           // phase 43: or in the volume pane
        if (hy < vt) hy = vt;
        if (hy > vb) hy = vb;
    } else {
        if (hy < top) hy = top;
        if (hy > bottom) hy = bottom;
    }

    HPEN hPrev = (HPEN)SelectObject(hdc, sty->penCross);
    MoveToEx(hdc, hx, top, NULL);      LineTo(hdc, hx, axisB);   // through every pane
    // Same bridge as the last-price line: the horizontal reaches the axis,
    // otherwise there would be a gap between the cross and its label.
    MoveToEx(hdc, left, hy, NULL);     LineTo(hdc, edge, hy);
    SelectObject(hdc, hPrev);

    // In the band the tag carries the RSI level at the pointer, 0..100.
    if (yBandCross != INT_MIN && !in->desktop && bh > 0) {
        double lv = 100.0 - ((double)(hy - bt) / (double)bh) * 100.0;
        swprintf_s(buf, 64, L"%.1f", lv);
        RECT rcTagB = { edge + 1, hy - tagHalf, axR + PX(3), hy + tagHalf };
        FillRect(hdc, &rcTagB, sty->brBoxEdge);
        SelectObject(hdc, sty->fontAxis);
        SetTextColor(hdc, sty->clr.text);
        RECT rcTagBT = { axL, hy - tagHalf, axR, hy + tagHalf };
        DrawTextW(hdc, buf, -1, &rcTagBT, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }

    // In the volume pane the tag carries the volume at the pointer: the
    // pane's top is dispVolMax, its bottom row zero, as the bars stand.
    if (yVolCross != INT_MIN && !in->desktop && vbH > 0 && st->dispVolMax > 0.0) {
        double lv = st->dispVolMax * (double)(vb + 1 - hy) / (double)vbH;
        FormatVolume(lv, buf, 64);
        RECT rcTagV = { edge + 1, hy - tagHalf, axR + PX(3), hy + tagHalf };
        FillRect(hdc, &rcTagV, sty->brBoxEdge);
        SelectObject(hdc, sty->fontAxis);
        SetTextColor(hdc, sty->clr.text);
        RECT rcTagVT = { axL, hy - tagHalf, axR, hy + tagHalf };
        DrawTextW(hdc, buf, -1, &rcTagVT, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }

    // Price label on the right axis where the pointer is. Not when it would
    // partly cover the stamp (phase 29, see yCross above) or reach into the
    // pane below (phase 44): the line is drawn, the tag is not.
    if (yCross != INT_MIN) {
        double hp = maxP - ((double)(hy - top) / (double)ch) * range;
        swprintf_s(buf, 64, L"%.*f", PriceDecimals(range / 4.0), hp);
        RECT rcTag = { edge + 1, hy - tagHalf, axR + PX(3), hy + tagHalf };
        FillRect(hdc, &rcTag, sty->brBoxEdge);
        SelectObject(hdc, sty->fontAxis);
        SetTextColor(hdc, sty->clr.text);
        RECT rcTagTxt = { axL, hy - tagHalf, axR, hy + tagHalf };
        DrawTextW(hdc, buf, -1, &rcTagTxt, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
#ifdef TICKER_PROBE
    st->probeCrossTag = (yCross != INT_MIN);
#endif

    // Hover box with time + OHLC. Back to the normal small font:
    // LINE_H = 13 is measured on it, and the axis font is 15 px tall.
    SelectObject(hdc, sty->fontSmall);
    // Phase 45: date and time on intraday intervals ("21 Sep 14:35"), so the
    // box names the day the axis only marks where it begins.
    wchar_t tbuf[24];
    FormatTimeAs(hc->openTime, in->intervalMs, in->utcOffsetMs, TIME_BOX, tbuf, 24);

    // BOX_H: 4 px top + time row + O/H/L/C/V (phase 21) = 4 + 6 * 13 + 5.
    // With the indicators on (phase 27) three more rows are added: SMA, EMA
    // and VWAP at the candle under the crosshair - the same numbers the
    // legend shows, since legendIdx IS hoverIdx when this block runs (same
    // condition). The rows stay as long as the lines are visible (indT > 0)
    // and fade with them, in the lines' own colors; an average that is not
    // defined at the candle gets a dash, as in the legend.
    const int indRows = (indT > 0) ? 3 : 0;
    const int rsiRows = (bandOn && rsiT > 0) ? 1 : 0;   // phase 39
    const int LINE_H = PX(13), BOX_W = PX(HOVER_BOX_W);
    const int BOX_H = PX(4) + (6 + indRows + rsiRows) * LINE_H + PX(5);
    int bx = hx + PX(12);
    if (bx + BOX_W > right) bx = hx - PX(12) - BOX_W;   // flip to the left at the edge
    if (bx < left) bx = left;
    // Phase 44: the top clamp comes last, so it wins. On a short panel with
    // both panes and the averages (290 px: 126 px of price, a 139 px box) the
    // bottom clamp put the box over the header's quote line and under the
    // toolbar the app draws after the chart; now it hangs into the pane
    // below, which is the chart's own.
    int by = hy - BOX_H / 2;
    if (by + BOX_H > bottom) by = bottom - BOX_H;
    if (by < top) by = top;

    RECT rcBox = { bx, by, bx + BOX_W, by + BOX_H };
    FillRect(hdc, &rcBox, sty->brBox);
    FrameRect(hdc, &rcBox, sty->brBoxEdge);

    int ty = by + PX(4);
    RECT rcL = { bx + PX(7), ty, bx + BOX_W - PX(6), ty + LINE_H };
    SetTextColor(hdc, sty->clr.text);
    DrawTextW(hdc, tbuf, -1, &rcL, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    const wchar_t* lbl[5] = { L"O", L"H", L"L", L"C", L"V" };
    double val[5] = { hc->open, hc->high, hc->low, hc->close, hc->volume };
    COLORREF cclr = (hc->close >= hc->open) ? sty->clr.up : sty->clr.down;

    for (int i = 0; i < 5; ++i) {
        ty += LINE_H;
        RECT rcRow = { bx + PX(7), ty, bx + BOX_W - PX(6), ty + LINE_H };
        SetTextColor(hdc, sty->clr.dim);
        DrawTextW(hdc, lbl[i], -1, &rcRow, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        if (i == 4) FormatVolume(val[i], buf, 64);   // phase 21
        else        swprintf_s(buf, 64, L"%.2f", val[i]);
        SetTextColor(hdc, (i == 3) ? cclr : sty->clr.text);
        DrawTextW(hdc, buf, -1, &rcRow, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }

    // The indicator rows (phase 27). The label in the line's color, the
    // value in the text color - both faded against the box, not CLR_BG.
    if (indRows > 0) {
        const wchar_t* ilbl[3] = { L"SMA", L"EMA", L"VWAP" };
        const COLORREF iclr[3] = { sty->clr.sma, sty->clr.ema, sty->clr.vwap };
        for (int i = 0; i < 3; ++i) {
            ty += LINE_H;
            RECT rcRow = { bx + PX(7), ty, bx + BOX_W - PX(6), ty + LINE_H };
            SetTextColor(hdc, Blend(sty->clr.box, iclr[i], indT));
            DrawTextW(hdc, ilbl[i], -1, &rcRow, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            if (indOk[i]) swprintf_s(buf, 64, L"%.2f", indVal[i]);
            else          wcscpy_s(buf, 64, L"-");
            SetTextColor(hdc, Blend(sty->clr.box, sty->clr.text, indT));
            DrawTextW(hdc, buf, -1, &rcRow, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        }
    }

    // The RSI row (phase 39): the value at the candle under the crosshair -
    // rsiLegendIdx IS hoverIdx here, same condition as the legend.
    if (rsiRows > 0) {
        ty += LINE_H;
        RECT rcRow = { bx + PX(7), ty, bx + BOX_W - PX(6), ty + LINE_H };
        SetTextColor(hdc, Blend(sty->clr.box, sty->clr.rsi, rsiT));
        DrawTextW(hdc, L"RSI", -1, &rcRow, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        if (rsiLegendOk) swprintf_s(buf, 64, L"%.2f", rsiLegend);
        else             wcscpy_s(buf, 64, L"-");
        SetTextColor(hdc, Blend(sty->clr.box, sty->clr.text, rsiT));
        DrawTextW(hdc, buf, -1, &rcRow, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }
}
#undef PX
