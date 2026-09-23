// chart.h - TickC's chart engine (phase 34). Pure of the app: no window, no
// lock, no network, no registry. The app hands it a ChartState it owns, a
// ChartData view of its buffer for the frame, and a ChartStyle of GDI
// objects it created. Everything here is drawn with GDI into any HDC.
#pragma once
#include <windows.h>

typedef struct {
    long long openTime; // Unix time in milliseconds
    double open;
    double high;
    double low;
    double close;
    double volume;      // base volume in the candle (phase 21). 48 bytes per candle.
} Candle;

// The chart area is computed in two places (painting and mouse hit testing) -
// they MUST agree, otherwise the crosshair points at the wrong candle.
// right/cw is the CANDLES' area. edge is the axis edge: the stamp begins at
// edge + 1 and the labels at edge + AXIS_LBL_GAP. Both modes have space
// between them (PLOT_PAD_R); in desktop mode the margin outside edge is
// narrower, because it only holds the stamp and no axis labels.
// dpi (phase 36) is the one the rectangle was computed for, so the hit tests
// that take a ChartRect scale their zones the same way the drawing did.
// bandTop/bandBottom (phase 39) are the RSI band under the price pane; equal
// (both = bottom) when there is no band. bottom and ch stay the PRICE pane's,
// so every price <-> y function reads them as before; the time axis sits
// under the lowest pane.
typedef struct { int left, top, right, bottom, cw, ch, edge, dpi, bandTop, bandBottom; } ChartRect;

// The chart's own state. viewStart/viewCount/followLive are the TARGET view
// (in TickC written by the UI and read by the worker thread, under the lock);
// the disp* fields are the DISPLAY, eased toward the target by the app's
// animation clock and owned by the UI thread alone. hover* is the crosshair.
typedef struct {
    int  viewStart;
    int  viewCount;      // 0 = show all
    BOOL followLive;     // the view sits at the far right and follows new candles
    int  panAnchorView;  // viewStart when a pan started; shifted with the buffer
    double dispStart, dispCount;   // fractional view
    double dispMin, dispMax;       // animated price axis
    double dispVolMax;             // animated volume scale (phase 21), 0 = no bars
    BOOL   dispValid;              // FALSE = snap on next update
    long long dispShiftSeen;       // frontShift the display has compensated for
    double dispVolF;               // the VOL toggle's display, 0..1
    double dispIndF;               // the indicator toggle's display, 0..1
    double dispRsiF;               // phase 39: the RSI band's content, 0..1
    int  hoverIdx;       // index of the candle under the pointer, -1 = none
    int  hoverY;         // mouse Y in client coordinates
#ifdef TICKER_PROBE
    // Test build only: what the last ChartDrawBody measured. 45: the session
    // blocks (us), 56: yesterday (us), 39: the averages (us), 57: bitmask of
    // level labels drawn, 58: crosshair tag drawn, 59: the label block (us).
    LONGLONG probeSessUs, probePrevUs, probeIndUs, probeLblUs;
    int      probeLblMask, probeCrossTag;
#endif
} ChartState;

// What one frame reads. Pointers borrow the app's memory for the call.
typedef struct {
    const Candle* candles;
    int           count;
    long long     intervalMs;
    BOOL          histDone;     // the history has ended (no older candles exist)
    long long     frontShift;   // net candles dropped at the front, see ApplyFrontShift
    BOOL          desktop;      // desktop mode: no header, no axis labels, one stamp
    const double* alerts;       // price alerts for the shown symbol, signed levels
    int           alertCount;
    int           alertHot;     // the alert tag under the pointer, -1 = none
    int           axisHotY;     // pointer row in the price column, -1 = none
    double        alertFresh;   // the level just set with a click, 0 = none
    double        alertFlashLevel;
    double        alertFlashF;  // afterglow of a fired alert, 1..0
    // Local time = UTC + utcOffsetMs, for every label in the frame (phase 35).
    // The app passes ChartUtcOffsetMs(); the golden tests pass a constant, so
    // their hashes do not change with the machine's DST state.
    long long     utcOffsetMs;
    // The RSI band is on (phase 39). The region follows this at once; its
    // content fades with ChartState.dispRsiF. ChartGeometry takes the same flag.
    BOOL          band;
} ChartData;

// The chart's colors (phase 38). Every color the engine draws with comes from
// here, not from the CLR_ macros: the macros below are the default dark theme
// (ChartThemeDark), which TickC uses, and a second theme is a table, not an
// edit of chart.c. The pens and brushes in ChartStyle are built from it.
typedef struct {
    COLORREF bg, grid, up, down, text, dim, cross, box, boxEdge;
    COLORREF hot, onHot;         // a tag the pointer is on: surface and text
    COLORREF axis, volUp, volDown;
    COLORREF sma, ema, vwap, session, prev;
    COLORREF alert, alertLine;
    COLORREF rsi;                // phase 39
} ChartTheme;

extern const ChartTheme ChartThemeDark;    // the CLR_ values; TickC's look
extern const ChartTheme ChartThemeLight;   // phase 38: light background

// GDI objects the chart draws with (phase 35: built by ChartStyleCreate, so
// the app and the golden tests draw with the very same objects). fontPill
// is NOT part of it: its height follows the surface, and the app builds it
// and sets it per frame. ChartStyleDestroy leaves it alone.
typedef struct {
    HFONT  fontSmall, fontAxis;
    HFONT  fontPill;            // desktop stamp font, NULL = use fontAxis
    HPEN   penGrid, penCross, penLastUp, penLastDown;
    HBRUSH brBg, brBox, brBoxEdge, brVolUp, brVolDown;
    int    dpi;                 // phase 36: the fonts are built for it; 96 = 100 %
    ChartTheme clr;             // phase 38: the colors, copied from the theme
} ChartStyle;

// DPI (phase 36). Every length in this header is given at 96 dpi and is
// scaled with ChartPx when drawn: MulDiv rounds to nearest, and at 96 it is
// the identity, so a 100 % surface keeps exactly its old pixels. Lines stay
// 1 device pixel wide at every dpi - a styled GDI pen (PS_DOT, PS_DASH) only
// keeps its pattern at width 1.
#define CHART_DPI_BASE   96
#define ChartPx(dpi, v)  MulDiv((v), (dpi), CHART_DPI_BASE)

#define MIN_VIEW         8     // minimum number of visible candles at full zoom
#define ZOOM_STEP        1.2   // per mouse wheel notch

// Layout inside the popup window
#define PAD_L            10
// Right margin = the price axis column (AXIS_Y_W) + edge guard (AXIS_PAD_R).
// Axis text begins at rcChart.right + AXIS_LBL_GAP and ends no later than
// W - AXIS_PAD_R. The column has room for AXIS_Y_CHARS characters in the axis
// font, which is monospace: 8 x 9 px = 72, exactly "75812.34" (measured,
// Lucida Console em 15). The width is FIXED, not measured on the labels:
// PriceDecimals can change in the middle of the Y easing, and a margin that
// followed the text would make the whole chart jerk sideways while the price
// axis glides.
//
// AXIS_PAD_R also stays above RESIZE_BORDER, so no digit stands in the zone
// where the pointer becomes a resize arrow.
#define AXIS_Y_CHARS     8
#define AXIS_CHAR_W      9
#define AXIS_LBL_GAP     4
#define AXIS_Y_W         (AXIS_LBL_GAP + AXIS_Y_CHARS * AXIS_CHAR_W)
#define AXIS_PAD_R       8
#define PAD_R            (AXIS_Y_W + AXIS_PAD_R)
// Space between the last candle and the axis edge (phase 15). The candles end
// at rcChart.right; the grid, the dashed last-price line and the crosshair
// run all the way to rcChart.edge, where the stamp and the labels begin.
// Without this, the body or wick of the last candle could stand flush against
// the stamp area. Applies to both modes from phase 16, when the desktop also
// got a stamp.
#define PLOT_PAD_R       10
#if PLOT_PAD_R < 8 || PLOT_PAD_R > 12
#error PLOT_PAD_R must be in [8, 12] px
#endif
// The bottom margin is the time band, not empty space. The axis font is 15 px
// high (tmHeight), so 18 px gives text from bottom + 2 to bottom + 17 = H - 1
// without touching the row y = bottom, where the lowest wick and the bottom
// grid line stand (the clip is inclusive there, see DrawChart).
#define PAD_B            18
// Minimum distance between two time labels. Used as
// max(TIME_DX_MIN, label width + TIME_LBL_GAP): "DD.MM HH:MM" is 99 px in the
// axis font, wider than 80, and would otherwise collide on 1h and 4h.
#define TIME_DX_MIN      80
#define TIME_LBL_GAP     12

// Palette (matches the tray icon)
#define CLR_BG           RGB(0x0D, 0x11, 0x17)
#define CLR_GRID         RGB(0x1C, 0x22, 0x2B)
#define CLR_UP           RGB(0x00, 0xFF, 0x66)
#define CLR_DOWN         RGB(0xFF, 0x49, 0x66)
#define CLR_TEXT         RGB(0xC3, 0xBC, 0xDB)
#define CLR_DIM          RGB(0x6E, 0x76, 0x81)
#define CLR_CROSS        RGB(0x55, 0x5F, 0x6E)
#define CLR_BOX          RGB(0x16, 0x1D, 0x27)
#define CLR_BOXEDGE      RGB(0x33, 0x3D, 0x4B)
#define CLR_CLOSEHOT     RGB(0xC0, 0x2A, 0x3E)   // red background on the close cross
#define CLR_BTNHOT       RGB(0xFF, 0xFF, 0xFF)   // glyph on red background
// Axis text: 8.05:1 against CLR_BG (WCAG AA requires 4.5:1 for small text).
// CLR_DIM, which the axes used before, gives 4.12:1. Only the axes - CLR_DIM
// also drives buttons, header and overlay, and they are not part of this
// change.
#define CLR_AXIS         RGB(0xA0, 0xAA, 0xB8)

// Volume bars (phase 21): bottom VOL_FRAC of the chart area, behind the
// candles. The colors are CLR_UP/CLR_DOWN blended ~28 % towards CLR_BG -
// muted enough to sit behind the candles, saturated enough to tell direction.
// Exact values, so a probe can count them.
#define VOL_FRAC         0.22
#define CLR_VOL_UP       RGB(0x09, 0x54, 0x2D)
#define CLR_VOL_DOWN     RGB(0x51, 0x21, 0x2D)
// Moving averages (phase 25): SMA 20 and EMA 50 on the close, drawn as
// 1 px lines over the candles. Muted steel blue and muted violet: neither
// appears elsewhere on the surface (green/red are candles, amber is alerts,
// gray is grid and crosshair), so a line can never be read as something else.
// Exact values, so a probe can count them. The prefix is IND_, not MA_:
// winuser.h owns MA_ACTIVATE and MA_NOACTIVATE (pitfall 67).
#define IND_SMA_PERIOD   20
#define IND_EMA_PERIOD   50
#define CLR_SMA          RGB(0x3D, 0x8F, 0xBF)
#define CLR_EMA          RGB(0xA0, 0x72, 0xD0)
#define IND_TAU_FADE     ANIM_TAU_FADE   // the MA toggle fades the lines, like the overlay
// The RSI band (phase 39): RSI 14 with Wilder's smoothing, in a band under the
// price pane with its own fixed 0..100 scale and the 70/30 levels dashed. The
// band is RSI_BAND_FRAC of the chart height, at least RSI_BAND_MIN, and it is
// left out when the price pane would get less than RSI_PANE_MIN - a 400x250
// panel keeps 142 px of price. Teal: not green (up), not a blue or violet of
// the averages, not a gold or amber of VWAP and the alerts.
#define RSI_PERIOD       14
#define RSI_HI           70
#define RSI_LO           30
#define RSI_BAND_FRAC    0.20
#define RSI_BAND_MIN     40
#define RSI_PANE_MIN     120
#define RSI_GAP          6
#define CLR_RSI          RGB(0x2E, 0xC4, 0xB6)
// Today's session (phase 27). VWAP is gold and a CURVE; the alerts are amber
// and horizontal (CLR_ALERT FFB020, the line 86601B) - yellower and lighter
// here, so the two are not read as the same thing. Today's high/low is
// neutral gray and dashed: solid amber is an alert, dashed green/red is the
// last price, dotted gray is the crosshair. The gray deliberately does NOT
// lie on the blend line between CLR_BG and CLR_AXIS (8A93A0 does): antialiased
// axis numbers would then contain exactly the same color, and a pixel probe
// could not tell the line from the text. The pattern is 6 on / 6 off, anchored
// at the surface's left edge, so the dashes do not crawl during panning.
#define CLR_VWAP         RGB(0xF2, 0xD1, 0x4B)
#define CLR_SESSION      RGB(0x90, 0x93, 0x9E)
#define SESS_DASH_ON     6
#define SESS_DASH_PERIOD 12
// Yesterday's levels (phase 28): same language, one step further back. Cooler
// and darker than CLR_SESSION - "same thing, older" - and it too is not on
// the blend line from CLR_BG or CLR_BOX to any text color (computed per
// channel, pitfall 87). Same period and same anchor as today's lines, so the
// patterns stay in step: high/low is 2 on / 10 off (sparse dots; PS_DOT in
// the crosshair is denser and follows the pointer), the close 10 on / 2 off
// (almost solid - it is the level today's change is computed from).
#define CLR_PREV         RGB(0x6F, 0x7B, 0x95)
#define PREV_DASH_HL     2
#define PREV_DASH_CLOSE  10
// The levels in the price axis rank, highest first: today's high and low,
// then yesterday's close, high and low. A tag yields to all ahead of it.
#define LVL_COUNT        5

// Price alerts (phase 23): fixed slots per symbol, no malloc. Eight is more
// than the price axis holds without the tags covering each other at 250 px
// height (16 px per tag), and 4 x 8 doubles is 256 bytes.
#define ALERT_MAX          8
#define ALERT_HIT_PX       8      // half the tag height: hit = what is drawn
#define ALERT_PRICE_MAX    1.0e9  // guard against a hand-edited registry

// The stamp on the desktop (phase 16). Its height follows the surface height,
// like the watermark: a 16 px pill is unreadable at 1600 px, and a fixed large
// pill would burst a small surface. Floor and ceiling are in device pixels -
// the surface is per-monitor aware, so H is already physical pixels, and the
// proportional part scales itself.
#define DESK_PILL_DIV    40
#define DESK_PILL_MIN    16
#define DESK_PILL_MAX    48
#if AXIS_PAD_R < 6 || AXIS_PAD_R > 10
#error AXIS_PAD_R must be in [6, 10] px
#endif
#define HEADER_H         44
// rcChart.top = HEADER_H. The chart must never be able to creep up into the
// header text or the control buttons (they end at BTN_TOP + BTN_H = 24), so a
// floor is checked at compile time instead of being clamped at run time.
#define CHART_TOP_MIN    32
#if HEADER_H < CHART_TOP_MIN
#error HEADER_H must leave rcChart at least CHART_TOP_MIN px below the top
#endif

// Amber: not among the eleven fixed colors, so a probe can count pixels,
// and neither up (green) nor down (red) - an alert has no direction before
// it fires. The line over the data area is the same color blended halfway
// down towards CLR_BG: a level is a reference like the grid, not a signal,
// and must not shout louder than the candles. The tag on the axis carries
// the saturated color.
#define CLR_ALERT          RGB(0xFF, 0xB0, 0x20)
#define CLR_ALERT_LINE     RGB(0x86, 0x60, 0x1B)

// Batches for PolyPolygon / Polyline; see chart.c.
#define VOL_BATCH 256
#define IND_BATCH (VOL_BATCH * 4)

// --- Geometry and hit testing ---
ChartRect ChartGeometry(int W, int H, BOOL desktop, int dpi, BOOL band);
int       ChartAxisW(int dpi);
int       DeskPillH(int H);
int       DeskPillFontH(int H);
int       DeskAxisW(int H, int dpi);
int       HitCandle(const ChartState* st, int n, const ChartRect* g, int mx, int my);
int       AlertY(const ChartState* st, const ChartRect* g, double level);
double    AlertPriceAtY(const ChartState* st, const ChartRect* g, int y);
int       AlertAxisHit(const ChartState* st, const ChartRect* g, const double* alerts, int alertCount, int my);
double    AlertRound(double price, double pxStep);

// --- View and display ---
void      ClampView(ChartState* st, int n);
void      GetView(const ChartState* st, int n, int* vs, int* vc);
BOOL      PanView(ChartState* st, int n, int delta);
BOOL      ZoomView(ChartState* st, int n, double frac, int notches);
void      PriceRange(const Candle* candles, int vs, int vc, double* outMin, double* outMax);
double    VolumeMax(const Candle* candles, int vs, int vc);
void      ApplyFrontShift(ChartState* st, long long frontShift);
void      SyncDisp(ChartState* st, const Candle* candles, int n);

// --- Sessions (UTC days) ---
int       SessionStart(const Candle* c, int n, long long intervalMs, BOOL histDone, BOOL* complete);
void      SessionHiLo(const Candle* c, int s, int n, double* outHi, double* outLo);
int       PrevSession(const Candle* c, int n, long long intervalMs, BOOL histDone, int* outEnd, BOOL* complete);
BOOL      SessionsNeedHistory(const Candle* c, int n, long long intervalMs, BOOL histDone);
#ifdef TICKER_PROBE
BOOL      IndValueAt(const Candle* c, int n, int period, BOOL ema, int idx, double* out);
BOOL      VwapValueAt(const Candle* c, int n, long long intervalMs, BOOL histDone, int idx, double* out);
BOOL      RsiValueAt(const Candle* c, int n, int idx, double* out);
#endif

// --- Formatting and colors ---
int       PriceDecimals(double step);
void      FormatSpan(int vc, long long intervalMs, wchar_t* out, size_t cch);
void      FormatCandleTime(long long unixMs, long long intervalMs, long long utcOffsetMs, wchar_t* out, size_t cch);
long long ChartUtcOffsetMs(void);
void      FormatVolume(double v, wchar_t* out, size_t cch);
void      FormatTagPrice(HDC hdc, double p, double range, int avail, wchar_t* out, size_t cch);
COLORREF  Blend(COLORREF a, COLORREF b, int t);
int       TimeTickStep(double dispCount, int chartW, int minDx);
int       NiceTimeStep(int step, long long intervalMs);

// --- Style ---
// Creates every object in ChartStyle except fontPill (set to NULL), with the
// fonts sized for dpi (0 = 96) and the colors from theme (NULL = dark).
// FALSE when any creation failed; the rest are then still valid or NULL, and
// ChartStyleDestroy frees them.
BOOL      ChartStyleCreate(ChartStyle* sty, int dpi, const ChartTheme* theme);
void      ChartStyleDestroy(ChartStyle* sty);
// The desktop stamp font for a surface H px high (DeskPillFontH). The caller
// owns it and sets it as ChartStyle.fontPill.
HFONT     ChartPillFontCreate(int H);

// --- Drawing ---
// Background: the cached watermark bitmap when there is one, else brBg.
void      ChartDrawBackground(HDC hdc, int W, int H, HDC wmDC, HBRUSH brBg);
// Everything from the grid to the hover box. Needs count > 0.
void      ChartDrawBody(HDC hdc, int W, int H, ChartState* st, const ChartData* in, const ChartStyle* sty);
