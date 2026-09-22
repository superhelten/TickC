#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

#define WM_TRAYICON      (WM_USER + 1)
#define WM_APP_DATA      (WM_APP + 1)   // the worker thread has new data
#ifdef TICKER_PROBE
#define WM_APP_PROBE     (WM_APP + 2)   // test build only: read internal state (phase 18)
#endif
#define ID_TRAY_EXIT     1001
#define ID_TRAY_RESET    1002
#define ID_TRAY_DESKTOP  1003   // desktop mode on/off (phase 12)
#define IDM_TOGGLE_AUTOSTART 1004   // start at sign-in on/off (phase 13)
#define ID_TRAY_VOLUME   1005   // volume bars on/off (phase 22)
#define ID_TRAY_ALERTS_CLEAR 1006   // remove the price alerts for the symbol (phase 23)
#define ID_TRAY_INDICATORS 1007   // moving averages on/off (phase 25)
// Symbol and interval from the tray menu (phase 17). Item i gets FIRST + i.
// The ranges are 100 wide; #error below the tables ensures they never overlap.
#define ID_TRAY_SYMBOL_FIRST   1100
#define ID_TRAY_INTERVAL_FIRST 1200
#define ID_TRAY_RANGE_W        100
#define TIMER_INTERVAL   3000 // 3 seconds

// --- Popup / chart ---
#define POPUP_W          1280
#define POPUP_H          720
// Minimum size when resizing manually, in logical pixels (96 dpi).
// 400x250 is where the header still has room for price, percent and the
// button row on one line, and the chart area for the hover box (104x74).
#define POPUP_MIN_W      400
#define POPUP_MIN_H      250
#define RESIZE_BORDER    6     // width of the zone that starts a resize
// 6000 candles at 48 bytes = 288 KB. Four days on 1m, sixteen years on 1d.
// Filled backwards on request (phase 18) and forwards while the panel is open.
// A full buffer stops the backfill (histDone) - live candles are never
// discarded to make room for old ones. Was 1440 (one day on 1m) up to and
// including phase 17.
#define MAX_CANDLES      6000
// First fetch, and every backfill: 6 hours in one go (~60 KB response of
// s_httpBuf's 96). 60 candles more than the default view (phase 25): EMA 50 is
// first defined on candle 49, and with 300 of 300 candles visible the line
// began one sixth into the chart. The warm-up now lies beyond the left edge.
#define SEED_COUNT       360
#define DEFAULT_VIEW     300   // visible view on open
#define MIN_VIEW         8     // minimum number of visible candles at full zoom
#define ZOOM_STEP        1.2   // per mouse wheel notch
#define TIMER_ANIM_ID    2
#define TIMER_EMBED_ID   3      // desktop mode: try WorkerW again
#define TIMER_REFIT_ID   4      // desktop mode: place the surface again after a display change (phase 26)
#define REFIT_SETTLE_MS  1000
// 250 ms: when Explorer restarts, the new Progman is in place after 290-480 ms
// (measured). With 1000 ms the desktop stood without a chart for 1.1 s. The
// timer only runs while the surface is missing.
#define EMBED_RETRY_MS   250
#define ANIM_INTERVAL    16     // ~60 fps
// Time-based interpolation, not a fixed step per tick: SetTimer(16) in
// practice fires every ~15.6 ms and is coalesced under load. A fixed step
// length would give different speeds depending on system load.
// An exponential curve has a tail: tau=55 gives ~90 % at 130 ms (where the
// eye sees the fade as finished) and full settling at ~340 ms. The tail only
// costs timer ticks, not repaints - we only paint when the rounded level
// actually changes.
#define ANIM_TAU_FADE    55.0   // time constant for the overlay fade (ms)
#define ANIM_DT_MAX      100.0  // clamps dt, so a long pause gives one jump

// View easing. Slightly longer tau than the overlay fade - panning is a larger
// movement than a fade - but not by much: 2.3*tau is where the eye sees it as
// finished, i.e. ~160 ms.
#define ANIM_TAU_VIEW    70.0

// Snap when what remains is less than a quarter pixel ON SCREEN.
// The threshold is therefore converted from pixels to candles (X) and to price
// (Y) on every tick, instead of being a fixed number in those units.
//
// Measured why: with a fixed threshold of 0.01 candles, a pan of 300 candles
// took 71 ticks - 1.14 s - before the clock could die, and the last 40 ticks
// moved less than a tenth of a pixel. And a fixed number in PRICE does not
// work at all across four symbols: 0.5 dollars is a third of SOL's whole range
// and less than a thousandth of BTC's.
#define SNAP_PX          0.25

// --- Network robustness ---
#define NET_RETRY_MAX    60000  // cap for exponential backoff (ms)
#define NET_RECONNECT_AT 3      // number of failures before hConnect is dropped (new DNS)
#define STALE_AFTER      (3 * TIMER_INTERVAL)  // 9 s = two missed cycles

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

// Control buttons in the header. Ultra-compact: 26x18 is enough for a 9 px
// glyph with space around it, and still lets the header's 44 px carry two
// lines of text.
#define BTN_W            26
#define BTN_H            18
#define BTN_GAP          2
#define BTN_TOP          6
#define BTN_MARGIN_R     8
#define SPAWN_OFFSET     30    // [ + ]: a new instance is offset this much down and to the right
// The button row's left edge is read from ButtonStrip, not from a separate
// width constant - DrawChart measures the header against it.
//
// Minimum space between two header texts on the same line, and between text
// and the button row.
#define HDR_GAP          8
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
// Both averages must be defined on the first visible candle when the panel
// opens on the default view (see SEED_COUNT).
C_ASSERT(SEED_COUNT >= DEFAULT_VIEW + IND_EMA_PERIOD);
C_ASSERT(SEED_COUNT >= DEFAULT_VIEW + IND_SMA_PERIOD);
// The watermark is blended towards white with alpha from WatermarkAlpha(W).
// White because the old fixed color #15191F was neutral: CLR_BG + 8 in all
// channels, i.e. ~3.3 % towards white.
#define CLR_WM_INK       RGB(0xFF, 0xFF, 0xFF)
#define WM_ALPHA_BASE    0.08
#define WM_ALPHA_MIN     0.04
#define WM_ALPHA_MAX     0.10
#define WM_W_NOMINAL     1920.0
// The watermark's font height = clamp(chart height / 5, 32, 120), the limits
// in logical pixels.
#define WM_FONT_DIV      5
#define WM_FONT_MIN      32
#define WM_FONT_MAX      120

// Curated, not free text. A fixed list means we know the price range and can
// format the icon, header and price axis correctly without guessing, and that
// no fetch can fail on an unknown symbol.
typedef struct { const wchar_t* api; const wchar_t* label; } SymbolDef;
typedef struct { const wchar_t* api; const wchar_t* label; long long ms; } IntervalDef;

static const SymbolDef SYMBOLS[] = {
    { L"BTCUSDT", L"BTC/USDT" },
    { L"ETHUSDT", L"ETH/USDT" },
    { L"SOLUSDT", L"SOL/USDT" },
    { L"BNBUSDT", L"BNB/USDT" },
};
static const IntervalDef INTERVALS[] = {
    { L"1m",  L"1m",  60000LL },
    { L"5m",  L"5m",  300000LL },
    { L"15m", L"15m", 900000LL },
    { L"1h",  L"1h",  3600000LL },
    { L"4h",  L"4h",  14400000LL },
    { L"1d",  L"1d",  86400000LL },
};
#define SYMBOL_COUNT   ((int)(sizeof(SYMBOLS) / sizeof(SYMBOLS[0])))
#define INTERVAL_COUNT ((int)(sizeof(INTERVALS) / sizeof(INTERVALS[0])))
// The tray menu's ID ranges (phase 17). sizeof cannot appear in #if, so the
// guard is C_ASSERT: if a table grows past its range, the build stops here.
C_ASSERT(SYMBOL_COUNT   <= ID_TRAY_RANGE_W);
C_ASSERT(INTERVAL_COUNT <= ID_TRAY_RANGE_W);
C_ASSERT(ID_TRAY_SYMBOL_FIRST + ID_TRAY_RANGE_W <= ID_TRAY_INTERVAL_FIRST);

// Price alerts (phase 23): fixed slots per symbol, no malloc. Eight is more
// than the price axis holds without the tags covering each other at 250 px
// height (16 px per tag), and 4 x 8 doubles is 256 bytes.
#define ALERT_MAX          8
// Amber: not among the eleven fixed colors, so a probe can count pixels,
// and neither up (green) nor down (red) - an alert has no direction before
// it fires. The line over the data area is the same color blended halfway
// down towards CLR_BG: a level is a reference like the grid, not a signal,
// and must not shout louder than the candles. The tag on the axis carries
// the saturated color.
#define CLR_ALERT          RGB(0xFF, 0xB0, 0x20)
#define CLR_ALERT_LINE     RGB(0x86, 0x60, 0x1B)
#define ALERT_HIT_PX       8      // half the tag height: hit = what is drawn
#define ALERT_TAU_FLASH    900.0  // the afterglow when an alert fires (ms)
#define ALERT_PRICE_MAX    1.0e9  // guard against a hand-edited registry

// The toolbar (phase 22): symbol pill, one pill per interval and VOL, in
// the header's row 2 - where the symbol line stood as plain text. Fixed
// widths, not measured text: WM_NCHITTEST must be able to compute the pills
// without a DC, and painting and hit testing must read the same numbers
// (pitfall 14). 15 px high, from y = 28: the price digits in row 1 end at
// the baseline at y ~ 27, so a highlighted pill never covers them, and
// y = 43 is the last row above the chart area.
#define TBAR_TOP           28
#define TBAR_H             15
#define TBAR_SYM_W         74    // "BNB/USDT" + arrow
#define TBAR_IV_W          28    // "15m"
#define TBAR_VOL_W         32
#define TBAR_IND_W         26    // "MA" (phase 25)
#define TBAR_GAP           2     // between the interval pills
#define TBAR_GROUP_GAP     8     // between symbol, intervals and VOL
#define TBAR_SYM           0
#define TBAR_IV_FIRST      1
#define TBAR_VOL           (TBAR_IV_FIRST + INTERVAL_COUNT)
#define TBAR_IND           (TBAR_VOL + 1)   // same group as VOL: overlays
#define TBAR_COUNT         (TBAR_IND + 1)

// 4x9 pixel font. One row per byte, bit 3 = left column, bit 0 = right.
// One line of 9px high digits is almost twice as readable as two lines of
// 5px digits, and "75.8" fills exactly 16px when the dot is 1px wide.
#define GLYPH_H   9
#define GLYPH_W   4
#define IDX_DOT   10
#define IDX_SPACE 12

static const unsigned char FONT_4X9[13][GLYPH_H] = {
    {0xF,0x9,0x9,0x9,0x9,0x9,0x9,0x9,0xF}, // 0
    {0x2,0x6,0x2,0x2,0x2,0x2,0x2,0x2,0x7}, // 1
    {0xF,0x1,0x1,0x1,0xF,0x8,0x8,0x8,0xF}, // 2
    {0xF,0x1,0x1,0x1,0xF,0x1,0x1,0x1,0xF}, // 3
    {0x9,0x9,0x9,0x9,0xF,0x1,0x1,0x1,0x1}, // 4
    {0xF,0x8,0x8,0x8,0xF,0x1,0x1,0x1,0xF}, // 5
    {0xF,0x8,0x8,0x8,0xF,0x9,0x9,0x9,0xF}, // 6
    {0xF,0x1,0x1,0x1,0x1,0x1,0x1,0x1,0x1}, // 7
    {0xF,0x9,0x9,0x9,0xF,0x9,0x9,0x9,0xF}, // 8
    {0xF,0x9,0x9,0x9,0xF,0x1,0x1,0x1,0xF}, // 9
    {0,0,0,0,0,0,0,0,0},                   // . (1px wide, drawn specially)
    {0x8,0x8,0x8,0x9,0xA,0xC,0xA,0x9,0x9}, // k
    {0,0,0,0,0,0,0,0,0}                    // ' ' (space)
};

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
typedef struct { int left, top, right, bottom, cw, ch, edge; } ChartRect;

typedef struct {
    HWND hWnd;
    HWND hPopup;
    NOTIFYICONDATAW nid;
    HINTERNET hSession;
    HINTERNET hConnect;
    wchar_t fullPriceStr[64];
    double lastPrice;

    // --- Runtime config. Lock-protected: the UI writes, the worker thread reads. ---
    int  symIdx;          // index into SYMBOLS
    int  ivIdx;           // index into INTERVALS
    long long intervalMs; // INTERVALS[ivIdx].ms, copied out for fast reading
    // Counts up on every config change. The worker thread takes a copy for
    // the fetch and discards the response if the counter has changed when it
    // comes back. Without this, BTC candles get merged into an ETH buffer.
    unsigned configGen;

    Candle candles[MAX_CANDLES];
    int candleCount;

    // Visible view of the candle array. viewCount = 0 means "show all".
    int viewStart;
    int viewCount;
    BOOL followLive;     // the view sits at the far right and follows new candles
    BOOL panning;        // dragging the chart sideways right now
    int  panAnchorX;     // mouse X when the panning started
    int  panAnchorView;  // viewStart when the panning started

    HFONT hFontBig;
    HFONT hFontSmall;
    // The price and time axes. Monospace, so the labels stand still when
    // the digits change, and AXIS_Y_W can be computed in characters. Grayscale
    // antialiasing (ANTIALIASED_QUALITY), not ClearType: no color fringing on
    // numbers.
    HFONT hFontAxis;

    int hoverIdx;        // index of the candle under the pointer, -1 = none
    int hoverY;          // mouse Y in client coordinates
    BOOL trackingMouse;  // whether WM_MOUSELEAVE has been requested

    // --- Animation clock ---
    // One timer drives everything time-dependent: overlay fade, the stale
    // counter, view and Y-axis easing. It only lives while something is
    // actually moving, and is killed when everything has settled.
    ULONGLONG lastAnimTick;
    BOOL   animRunning;
    int    staleSecsShown;   // last painted seconds value, prevents 60 fps on a counter

    // --- Overlay for symbol/interval selection ---
    // overlayOpen is the LOGICAL state and drives hit detection.
    // overlayF is the fade level and only drives painting. During fade-out,
    // overlayF > 0 while overlayOpen is FALSE - clicks must then go to the chart.
    BOOL   overlayOpen;
    double overlayF;      // 0-255
    int    overlayHot;    // index into rows[], -1 = none

    // --- View and Y-axis easing ---
    // Pure UI doubles. The worker thread NEVER sees them. viewStart/viewCount
    // are the target and are lock-protected; these are the display and are
    // owned by the UI thread alone. That is why the easing does not touch the
    // thread contract.
    double dispStart, dispCount;   // fractional view
    double dispMin, dispMax;       // animated price axis
    double dispVolMax;             // animated volume scale (phase 21), 0 = no bars
    BOOL   dispValid;              // FALSE = snap on next update
    long long dispShiftSeen;       // frontShift the UI has compensated for

    // --- Worker thread ---
    // The lock covers candles[], candleCount, viewStart, viewCount,
    // followLive, lastPrice, hPopup, frontShift, histPending and histDone.
    // Everything else is touched only by the UI thread.
    CRITICAL_SECTION lock;
    HANDLE hThread;
    HANDLE hStopEvent;   // manual reset: signals shutdown
    HANDLE hWakeEvent;   // auto reset: fetch NOW (the panel was opened)

    // Network health. Lock-protected - the worker thread writes, the UI reads.
    ULONGLONG lastOkTick;    // GetTickCount64 at the last successful fetch
    ULONGLONG nextRetryTick; // when the next attempt is scheduled
    int       netFailures;   // consecutive failures, drives the backoff
    // Net change at the FRONT of the buffer, signed: +1 per candle that drops
    // out (eviction), -k per k candles prepended (backfill, phase 18). The UI
    // thread moves the display, hover and pan anchor by the same amount
    // (ApplyFrontShift). Was evictedTotal, monotonic, up to and including
    // phase 17.
    long long frontShift;
    // Backfill (phase 18). histPending: the UI wants older candles, the thread
    // has not fetched yet. histDone: the server answered 2xx with no candles,
    // or the buffer is full - do not ask again for this config.
    BOOL      histPending;
    BOOL      histDone;
    // Wake from sleep (phase 24). The UI thread asks for the connection to be
    // dropped; hConnect is owned by the worker thread, so it does it itself
    // at the start of the next cycle. In the lock domain.
    BOOL      dropConn;

    // --- Cached GDI objects ---
    // Fixed colors are created once at startup instead of 16 times per
    // repaint.
    HPEN   penGrid, penCross;
    HPEN   penBtn, penBtnHot, penBtnWhite;
    // Standard cursors. LoadCursorW returns a SHARED handle for these - they
    // do not count as ours, and must not go through DestroyCursor. Cached
    // anyway: WM_SETCURSOR fires on every mouse move, and a lookup per
    // message is needless work in a path that is otherwise free.
    HCURSOR curPan, curArrow, curHand;   // curHand: the price column (phase 23)
    HBRUSH brClose;
    // Which button the mouse is over, -1 for none. UI-owned, never touched by
    // the worker thread. Hit detection hangs on THIS, not on any fade
    // level - the buttons have no fade, they change color instantly.
    int    btnHot;
    // The toolbar in the header's row 2 (phase 22). tbHot is the pill the
    // mouse is over, -1 for none - same rule as btnHot: logical state, no
    // fade. showVol is the user's choice and is saved in the registry;
    // dispVolF is the DISPLAY of it, 0..1, and is eased in WM_TIMER so the
    // bars sink down instead of blinking away. All three are UI-owned.
    int    tbHot;
    BOOL   showVol;
    double dispVolF;
    // Moving averages (phase 25): SMA 20 and EMA 50 over the candles. Same
    // pair as showVol/dispVolF: showInd is the choice and is saved in the
    // registry, dispIndF is the display, 0..1, and is eased by the clock -
    // here as COLOR towards the background, not as geometry. UI-owned. The
    // averages themselves are not stored: they are computed from candles[]
    // in every repaint (see IndStep).
    BOOL   showInd;
    double dispIndF;
    // The overlays have one choice PER MODE (phase 26). showVol/showInd are
    // the panel's; these two are the desktop surface's, and they are OFF by
    // default: the surface is read peripherally behind the icons (phase 14),
    // and bars and averages are measuring tools, not wallpaper. The tray menu
    // toggles the one for the mode the process is in. Read through
    // ShowVolNow/ShowIndNow, never directly.
    BOOL   showVolDesk;
    BOOL   showIndDesk;
    // Price alerts (phase 23). Everything is UI-owned: the alerts are set
    // from the mouse and tested in WM_APP_DATA, both on the UI thread, so the
    // thread contract is untouched. Per symbol - a level in dollars is
    // meaningless across symbols (pitfall 16). The sign carries the SIDE:
    // +level fires when the price is >= the level (the alert was set above
    // the price), -level when it is <= (set below). The side is stored,
    // instead of comparing the previous and next price, so a level that was
    // crossed while the app was off or the machine was asleep fires on the
    // first price afterwards, and a symbol change cannot compare SOL against
    // BTC.
    // alertHot and axisHotY are hover state in the price column, same rule as
    // btnHot: logical state, -1 for none. alertFlashF is the afterglow of an
    // alert that has fired, 1..0, and is eased by the clock.
    double alerts[SYMBOL_COUNT][ALERT_MAX];
    int    alertCount[SYMBOL_COUNT];
    int    alertHot;
    int    axisHotY;
    // The level of the alert that was JUST set with a click, 0 = none.
    // The pointer then stands on the new tag, and alertHot says (correctly)
    // that one more click removes it - but a tag that turns red the moment it
    // is set reads as an error. It is therefore drawn amber until the pointer
    // has left it once. Only the painting reads the field; the hit hangs on
    // alertHot (pitfall 12). A level, not an index: removal moves the indices.
    double alertFresh;
    double alertFlashLevel;
    double alertFlashF;
    int    alertFired;       // number of alerts that have fired since startup
    double alertLastFired;   // the level of the last one
    BOOL   alertNotifyOk;    // the result from Shell_NotifyIconW on the last balloon
    HPEN   penLastUp, penLastDown;   // dashed last-price line
    HBRUSH brBg, brBox, brBoxEdge;
    HBRUSH brVolUp, brVolDown;       // volume bars (phase 21)


    // --- Persistent double buffer ---
    // Lives between frames and is rebuilt only when the size changes.
    // A new buffer per frame at 3840x1600 cost 7.9 ms on the first write
    // to the new bitmap and 1.7 ms on the release, out of 13.2 ms in total
    // (measured). bbValid: the buffer holds a full frame at this size, so
    // the fast path can draw the buttons straight into it.
    HBITMAP bbBmp;
    HDC     bbDC;
    HBITMAP bbOldBmp;
    int     bbW, bbH;
    BOOL    bbValid;

    // --- Watermark cache ---
    // Background + watermark baked together in one bitmap. This REPLACES the
    // current FillRect - it does not add a step. A DrawTextW with a large font
    // costs 0.05-0.30 ms and does not belong in every frame. Tried again
    // together with the persistent buffer: drawn per frame, the watermark cost
    // 0.50-0.53 ms at 1280x720, against ~0.28 ms for the blit (measured).
    HBITMAP wmBmp;
    HDC     wmDC;
    HBITMAP wmOldBmp;
    HFONT   hFontWm;
    HFONT   hFontPill;      // stamp font in desktop mode (phase 16)
    int     pillFontH;      // the height hFontPill was built for
    int     wmFontH;       // the font height the cache was built for
    int     wmW, wmH;      // the size the bitmap was built for
    int     wmSym, wmIv;   // the config it was built for
    BOOL    wmValid;
} AppContext;

static AppContext g_Ctx;
static int g_savedPanelW = 0;   // panel size from the registry, 0 = unused
static int g_savedPanelH = 0;
static int g_savedPanelX = 0;   // set by LoadConfig, GEOM_UNSET = unused
static int g_savedPanelY = 0;
// Started via [ + ]. A duplicate never writes to the registry - neither
// geometry nor symbol - and exits the process when the panel is closed.
// The registry is the main instance's memory; otherwise whichever closed last
// would decide where the next startup places the panel.
static BOOL g_isDuplicate = FALSE;
// --desktop-mode: the surface is a child of the desktop's WorkerW, behind the
// icons, covering the whole primary monitor. Same window class and same
// painting as the panel, but no frame, no buttons, no input and no geometry
// in the registry.
static BOOL g_desktopMode = FALSE;
// The overlay choices for the mode we are in (phase 26). Everything that
// paints, eases, checks items in the menu or answers a probe reads these.
static BOOL ShowVolNow(const AppContext* ctx) {
    return g_desktopMode ? ctx->showVolDesk : ctx->showVol;
}
static BOOL ShowIndNow(const AppContext* ctx) {
    return g_desktopMode ? ctx->showIndDesk : ctx->showInd;
}
static UINT g_msgTaskbarCreated = 0;   // Explorer restarted
static char s_httpBuf[98304];    // 360 candles give a ~60 KB response
static Candle s_incoming[SEED_COUNT];
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
#ifdef TICKER_PROBE
// Test build only (phase 21): the duration of the last full repaint in
// microseconds, QPC around the slow path in PaintPopup. Read with
// WM_APP_PROBE 15, so a probe can measure the median over many frames
// without taking screenshots at the same time (pitfall 37).
static LONGLONG g_probePaintUs = 0;
// Test build only (phase 25): the time the two DrawIndicator calls took in
// the last full repaint, in microseconds. Read with WM_APP_PROBE 39. The
// overlay costs less than the noise between two measurement series of the
// whole repaint (pitfall 80), so it is measured on its own.
static LONGLONG g_probeIndUs = 0;
// Test build only (phase 23): mutes balloon and sound when an alert fires, so
// a probe can fire many alerts without bothering whoever sits at the machine.
// Set with WM_APP_PROBE 101 to the main window. One run fires one alert
// unmuted and reads the result from Shell_NotifyIconW (field 29).
static BOOL g_probeMute = FALSE;
// Test build only (phase 34): recorded responses instead of the network.
// TICKER_FIXTURE_DIR names a folder; HttpGet then answers every request from
// a file there and never touches WinHTTP, so a capture shows the same candles
// every time and a probe runs offline. The mapping is by request:
//   /api/v3/klines?symbol=S&interval=I&limit=N   -> klines_S_I.json
//   /api/v3/klines?...&endTime=...               -> hist_S_I.json, or "[]"
//                                                   when the file is missing
//                                                   (the history has ended)
//   /api/v3/ticker/price?symbol=S                -> price_S.json
// A missing klines or price file is a failed request, like a network error.
static wchar_t g_fixtureDir[MAX_PATH] = L"";
// Test build only (phase 24): counters read from the main window with
// WM_APP_PROBE 110-112. Fetch cycles in the worker thread (Interlocked: it
// is written there and read on the UI thread), wake-ups seen in
// WM_POWERBROADCAST, and prices/candles the parsers have rejected as unsound.
static volatile LONG g_probeFetches = 0;
static volatile LONG g_probeResumes = 0;
static volatile LONG g_probeRejects = 0;
// 113: connections the worker thread has dropped after a wake-up.
static volatile LONG g_probeConnDrops = 0;
// Test build only (phase 26): WM_DISPLAYCHANGE seen on the main window. Read
// with WM_APP_PROBE 114 there.
static volatile LONG g_probeDisplayChanges = 0;
// Test build only (phase 27): the time the session blocks (today's high/low
// and VWAP) took in the last full repaint, in microseconds. WM_APP_PROBE 45.
// Measured on its own for the same reason as g_probeIndUs.
static LONGLONG g_probeSessUs = 0;
// Test build only (phase 28): the time the yesterday block took in the last
// full repaint, in microseconds. WM_APP_PROBE 56.
static LONGLONG g_probePrevUs = 0;
// Test build only (phase 29). 57: bitmask over the levels (bit q in the LVL
// rank) that got a label in the last repaint. 58: whether the crosshair's
// axis tag was drawn. 59: the time the label block took (us).
static int      g_probeLblMask  = 0;
static int      g_probeCrossTag = 0;
static LONGLONG g_probeLblUs    = 0;
// Test build only (phase 30): what the name migration did at startup, as a
// bitmask. WM_APP_PROBE 115 on the main window. Bit 0 old settings copied,
// 1 old key deleted, 2 autostart written under the new name, 3 old autostart
// value deleted.
static int      g_probeMigrate  = 0;
#endif

// Keeps the view within the data.
static void ClampView(AppContext* ctx) {
    int n = ctx->candleCount;
    if (n <= 0) { ctx->viewStart = 0; ctx->viewCount = 0; return; }
    if (ctx->viewCount < MIN_VIEW) ctx->viewCount = MIN_VIEW;
    if (ctx->viewCount > n)        ctx->viewCount = n;
    if (ctx->viewStart > n - ctx->viewCount) ctx->viewStart = n - ctx->viewCount;
    if (ctx->viewStart < 0)        ctx->viewStart = 0;
}

// Reads out the current view, with "show all" as the default.
static void GetView(const AppContext* ctx, int* vs, int* vc) {
    if (ctx->viewCount <= 0) { *vs = 0; *vc = ctx->candleCount; }
    else                     { *vs = ctx->viewStart; *vc = ctx->viewCount; }
}

static long long NowUnixMs(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (long long)((t - 116444736000000000ULL) / 10000ULL);
}

// ---------------------------------------------------------------------------
// Animation and network health - pure functions, no state.
// Both are unit-testable without Win32.
// ---------------------------------------------------------------------------

// Exponential interpolation toward a target. Frame-rate independent: a dt
// twice as long gives the same result as two half steps, so the animation
// runs equally fast whether the timer fires evenly or the messages are
// coalesced under load.
static double AnimStep(double cur, double target, double dt, double tau, double snap) {
    if (dt <= 0.0) return cur;
    if (dt > ANIM_DT_MAX) dt = ANIM_DT_MAX;

    cur += (target - cur) * (1.0 - exp(-dt / tau));
    if (fabs(target - cur) < snap) cur = target;
    return cur;
}

// The watermark's alpha as a function of the window width:
//   clamp(WM_ALPHA_BASE * sqrt(W / WM_W_NOMINAL), WM_ALPHA_MIN, WM_ALPHA_MAX)
// W is the client width in device pixels. At 400 px (the minimum width) the
// formula gives 0.037 and the floor takes over; at 1280 it is 0.065; above
// 3000 px the ceiling takes over.
static double WatermarkAlpha(int W) {
    if (W <= 0) return WM_ALPHA_MIN;
    double a = WM_ALPHA_BASE * sqrt((double)W / WM_W_NOMINAL);
    if (a < WM_ALPHA_MIN) a = WM_ALPHA_MIN;
    if (a > WM_ALPHA_MAX) a = WM_ALPHA_MAX;
    return a;
}

// Step length S (in candles) between the time labels.
//   N = floor(chartW / minDx),  M = ceil(dispCount),
//   S = max(1, ceil((M - 1) / (N - 1)))
// CEIL, not floor: with floor, M = 9, chartW = 320, minDx = 80 gives S = 2 and
// 71 px between the labels - collision. With ceil the spacing S * chartW /
// dispCount >= minDx for all M and N >= 2 (M >= N: (M-1)N >= (N-1)M; M < N:
// S = 1 and one candle is already wider than minDx). N < 2 gives one label.
static int TimeTickStep(double dispCount, int chartW, int minDx) {
    if (dispCount < 1.0) dispCount = 1.0;
    if (chartW <= 0 || minDx <= 0) return 1;
    int m = (int)ceil(dispCount);
    int nx = chartW / minDx;
    if (nx < 2) return (m > 1) ? m : 1;
    int s = (m - 1 + (nx - 1) - 1) / (nx - 1);
    return (s < 1) ? 1 : s;
}

// Rounds S up to a step that is a whole number of candles AND a round time
// span (5 min, 15 min, 1 h, 6 h, 1 d ...). TimeTickStep alone gives S = 23 on
// 300 1m candles at 1280 px, i.e. labels at 02:48, 03:11, 03:34 (seen in
// PrintWindow). Rounding up can only make the spacing LARGER, so the
// collision guarantee in TimeTickStep holds. If S is larger than the table,
// S is used as is.
static int NiceTimeStep(int step, long long intervalMs) {
    static const long long NICE_MIN[] = {
        1, 2, 3, 5, 10, 15, 20, 30, 60, 120, 180, 240, 360, 480, 720,
        1440, 2 * 1440, 3 * 1440, 7 * 1440, 14 * 1440, 28 * 1440,
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

// Exponential backoff with jitter. 3s, 6s, 12s, 24s, 48s, then capped at
// 60s. The jitter keeps many clients from synchronizing against the server
// after a shared outage - it is taken from the clock's low bits, so we avoid
// rand() and global state.
static DWORD NetBackoffMs(int failures, ULONGLONG tickSeed) {
    if (failures <= 0) return TIMER_INTERVAL;

    // Shift instead of pow, and stop before overflow can become an issue.
    DWORD base = TIMER_INTERVAL;
    for (int i = 0; i < failures && base < NET_RETRY_MAX; ++i) base *= 2;
    if (base > NET_RETRY_MAX) base = NET_RETRY_MAX;

    // +/- 12.5 %: base/8 spread over 256 steps.
    DWORD span  = base / 4;
    DWORD delta = (DWORD)(tickSeed & 0xFF) * span / 255;
    DWORD out   = base - span / 2 + delta;

    // The jitter is added ON TOP of the base, so it can push us over the cap.
    // Without this clamp, failures>=5 gave up to 67.5 s - measured, not
    // assumed.
    if (out > NET_RETRY_MAX) out = NET_RETRY_MAX;
    return out;
}

// Price alerts (phase 23). Has an alert fired? signedLevel carries the side in
// its sign: +level was set ABOVE the price and fires when the price is >= the
// level, -level was set BELOW and fires when it is <=. now <= 0 is "no price
// yet" (right after a symbol switch lastPrice is 0) and never fires - without
// the guard every lower alert would have fired on 0 (pitfall 17).
static BOOL AlertHit(double now, double signedLevel) {
    if (now <= 0.0 || signedLevel == 0.0) return FALSE;
    return (signedLevel > 0.0) ? (now >= signedLevel) : (now <= -signedLevel);
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
static double AlertRound(double price, double pxStep) {
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
static BOOL IndValueAt(const Candle* c, int n, int period, BOOL ema, int idx, double* out) {
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
static int SessionStart(const Candle* c, int n, long long intervalMs,
                        BOOL histDone, BOOL* complete) {
    return SessionStartAt(c, n, n - 1, intervalMs, histDone, complete);
}

// Highest high and lowest low over [s, n).
static void SessionHiLo(const Candle* c, int s, int n, double* outHi, double* outLo) {
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
static BOOL VwapValueAt(const Candle* c, int n, long long intervalMs,
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
static int PrevSession(const Candle* c, int n, long long intervalMs, BOOL histDone,
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
static BOOL SessionsNeedHistory(const Candle* c, int n, long long intervalMs, BOOL histDone) {
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

// Distinguishes "no saved position" from a real coordinate, which may well be
// negative on a monitor to the left of or above the primary one.
#define GEOM_UNSET  ((int)0x80000000)

// ---------------------------------------------------------------------------
// The registry. HKCU\Software\TickC. Never a precondition for the app
// starting - if reading fails, we fall back to BTC/USDT 1m.
// Placed here, among the pure helper functions, because ApplyConfigChoice
// further down calls SaveConfig. The file has no forward declarations.
// ---------------------------------------------------------------------------

// The test build has its own names here in the source, not in a script that
// rewrites it: then no probe run can touch the user's settings or autostart.
// REG_PATH_OLD is the name from before phase 30; MigrateLegacyNames moves it.
#ifdef TICKER_PROBE
#define REG_PATH     L"Software\\TickerTest"
#define REG_PATH_OLD L"Software\\TickerTestOld"
#else
#define REG_PATH     L"Software\\TickC"
#define REG_PATH_OLD L"Software\\Ticker"
#endif

static DWORD RegReadDword(HKEY k, const wchar_t* name, DWORD fallback) {
    DWORD v = 0, cb = sizeof(v), type = 0;
    if (RegQueryValueExW(k, name, NULL, &type, (BYTE*)&v, &cb) == ERROR_SUCCESS &&
        type == REG_DWORD && cb == sizeof(v)) {
        return v;
    }
    return fallback;
}

static void LoadConfig(AppContext* ctx, int* outX, int* outY, int* outW, int* outH) {
    ctx->symIdx     = 0;
    ctx->ivIdx      = 0;
    ctx->intervalMs = INTERVALS[0].ms;
    ctx->showVol    = TRUE;
    ctx->showInd    = TRUE;
    ctx->showVolDesk = FALSE;   // phase 26: the desktop starts clean
    ctx->showIndDesk = FALSE;
    *outX = GEOM_UNSET; *outY = GEOM_UNSET;
    *outW = 0; *outH = 0;

    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return;
    }
    DWORD sy = RegReadDword(k, L"SymbolIndex",   0);
    DWORD iv = RegReadDword(k, L"IntervalIndex", 0);
    DWORD w  = RegReadDword(k, L"PanelWidth",    0);
    DWORD h  = RegReadDword(k, L"PanelHeight",   0);
    DWORD hasPos = RegReadDword(k, L"PanelHasPos", 0);
    DWORD px = RegReadDword(k, L"PanelX", 0);
    DWORD py = RegReadDword(k, L"PanelY", 0);
    DWORD sv = RegReadDword(k, L"ShowVolume", 1);   // phase 22, on by default
    DWORD si = RegReadDword(k, L"ShowIndicators", 1);   // phase 25, on by default
    DWORD svd = RegReadDword(k, L"ShowVolumeDesktop", 0);       // phase 26, OFF by default
    DWORD sid = RegReadDword(k, L"ShowIndicatorsDesktop", 0);
    RegCloseKey(k);
    ctx->showVol = (sv != 0);
    ctx->showInd = (si != 0);
    ctx->showVolDesk = (svd != 0);
    ctx->showIndDesk = (sid != 0);

    // Bounds check. A registry edited by hand, or left behind by a newer
    // version with more symbols, must not be able to index outside the
    // table.
    if (sy < (DWORD)SYMBOL_COUNT)   ctx->symIdx = (int)sy;
    if (iv < (DWORD)INTERVAL_COUNT) {
        ctx->ivIdx      = (int)iv;
        ctx->intervalMs = INTERVALS[iv].ms;
    }
    if (w >= 240 && w <= 8192) *outW = (int)w;
    if (h >= 160 && h <= 8192) *outH = (int)h;

    // The position can be negative on a monitor to the left of or above
    // the primary one, so it is read as signed. PanelHasPos distinguishes
    // "not saved" from "saved as 0,0".
    if (hasPos) { *outX = (int)(LONG)px; *outY = (int)(LONG)py; }
}

// Last chosen mode from the tray menu. Its own value and its own functions,
// not part of SaveConfig: it is written the moment the user chooses, not in
// WM_DESTROY, which never runs when the process is killed from outside.
static BOOL LoadDesktopMode(void) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return FALSE;
    }
    DWORD v = RegReadDword(k, L"DesktopMode", 0);
    RegCloseKey(k);
    return v == 1;
}

static void SaveDesktopMode(BOOL on) {
    if (g_isDuplicate) return;
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0,
                        KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS) {
        return;
    }
    DWORD v = on ? 1 : 0;
    RegSetValueExW(k, L"DesktopMode", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
    RegCloseKey(k);
}

// Start at sign-in (phase 13). Lives in the Run key, not under REG_PATH: it
// is Explorer that reads it at sign-in. The content is the path to the exe in
// quotation marks, so a path with spaces is not split into a program and
// arguments.
// The test build never writes to the real Run key: a value there would start
// the test build at the next sign-in.
#ifdef TICKER_PROBE
#define AUTOSTART_KEY       L"Software\\TickerTestRun"
#define AUTOSTART_VALUE     L"TickerTest"
#define AUTOSTART_VALUE_OLD L"TickerTestOld"
#else
#define AUTOSTART_KEY       L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define AUTOSTART_VALUE     L"TickC"
#define AUTOSTART_VALUE_OLD L"Ticker"
#endif

// The path in quotation marks, "C:\Folder with spaces\TickC.exe". FALSE when
// the path does not fit in MAX_PATH - a truncated path must never end up in
// the registry.
static BOOL AutostartCommand(wchar_t* out, size_t cch) {
    wchar_t exe[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return FALSE;
    return swprintf_s(out, cch, L"\"%s\"", exe) > 0;
}

// The check mark in the tray menu: does the value exist, whatever its type and
// content? It can point somewhere other than the running exe; ToggleAutostart
// decides that.
static BOOL AutostartPresent(void) {
    return RegGetValueW(HKEY_CURRENT_USER, AUTOSTART_KEY, AUTOSTART_VALUE,
                        RRF_RT_ANY, NULL, NULL, NULL) == ERROR_SUCCESS;
}

// Click on "Start at sign-in":
//   value == current path -> delete
//   no value              -> write current path
//   anything else         -> write current path (the exe has moved)
// The last branch is why the check mark means "the value exists", not "the
// value is correct": a click on a checked but stale entry should fix the
// path, not turn autostart off.
static void ToggleAutostart(void) {
    if (g_isDuplicate) return;
    wchar_t want[MAX_PATH + 2], have[MAX_PATH + 2];
    if (!AutostartCommand(want, MAX_PATH + 2)) return;
    // Wrong type, or too long for the buffer (ERROR_MORE_DATA), cannot
    // possibly be our path and falls under "anything else".
    DWORD cb = sizeof(have);
    BOOL same = RegGetValueW(HKEY_CURRENT_USER, AUTOSTART_KEY, AUTOSTART_VALUE,
                             RRF_RT_REG_SZ, NULL, have, &cb) == ERROR_SUCCESS &&
                _wcsicmp(have, want) == 0;

    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, AUTOSTART_KEY, 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS) {
        return;
    }
    if (same) {
        RegDeleteValueW(k, AUTOSTART_VALUE);
    } else {
        RegSetValueExW(k, AUTOSTART_VALUE, 0, REG_SZ, (const BYTE*)want,
                       (DWORD)((wcslen(want) + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(k);
}

// The rename Ticker -> TickC (phase 30). Runs at every startup, before
// anything is read from the registry, and only does something when the old
// name exists.
//
// The settings are moved only when the new key does not exist: if both
// exist, the new one wins, and the old one is left untouched. The old one is
// deleted only once the copy succeeded; if the copy fails, the half-made new
// key is deleted, so the next startup tries again instead of believing the
// job is done.
//
// Autostart is written with the path to the running exe, not with the old
// value: that points to ticker.exe, which no longer exists. The old value is
// also deleted when a new one already exists - otherwise both start at
// sign-in - but never before the new one is in place.
//
// A duplicate and a main instance can start at the same time. Then one does
// the job and the other finds nothing to do, or both write the same thing.
static void MigrateLegacyNames(void) {
    int done = 0;   // the bitmask in g_probeMigrate
    HKEY kOld, kNew;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PATH_OLD, 0, KEY_READ, &kOld) == ERROR_SUCCESS) {
        if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &kNew) == ERROR_SUCCESS) {
            RegCloseKey(kNew);
            RegCloseKey(kOld);
        // KEY_ALL_ACCESS, not KEY_WRITE: with only KEY_WRITE on the target
        // RegCopyTreeW returns ERROR_ACCESS_DENIED (measured, pitfall 97).
        } else if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0, KEY_ALL_ACCESS,
                                   NULL, &kNew, NULL) == ERROR_SUCCESS) {
            LSTATUS copied = RegCopyTreeW(kOld, NULL, kNew);
            RegCloseKey(kNew);
            RegCloseKey(kOld);
            if (copied == ERROR_SUCCESS) {
                done |= 1;
                if (RegDeleteTreeW(HKEY_CURRENT_USER, REG_PATH_OLD) == ERROR_SUCCESS) done |= 2;
            } else {
                RegDeleteTreeW(HKEY_CURRENT_USER, REG_PATH);
            }
        } else {
            RegCloseKey(kOld);
        }
    }

    if (RegGetValueW(HKEY_CURRENT_USER, AUTOSTART_KEY, AUTOSTART_VALUE_OLD,
                     RRF_RT_ANY, NULL, NULL, NULL) == ERROR_SUCCESS) {
        BOOL haveNew = AutostartPresent();
        wchar_t want[MAX_PATH + 2];
        if (!haveNew && AutostartCommand(want, MAX_PATH + 2)) {
            haveNew = RegSetKeyValueW(HKEY_CURRENT_USER, AUTOSTART_KEY, AUTOSTART_VALUE,
                                      REG_SZ, want,
                                      (DWORD)((wcslen(want) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
            if (haveNew) done |= 4;
        }
        if (haveNew &&
            RegDeleteKeyValueW(HKEY_CURRENT_USER, AUTOSTART_KEY, AUTOSTART_VALUE_OLD) == ERROR_SUCCESS) {
            done |= 8;
        }
    }

#ifdef TICKER_PROBE
    g_probeMigrate = done;
#else
    (void)done;
#endif
}

static void SaveConfig(const AppContext* ctx) {
    if (g_isDuplicate) return;
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0,
                        KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS) {
        return;   // no write access: silent, the app works anyway
    }
    DWORD sy = (DWORD)ctx->symIdx, iv = (DWORD)ctx->ivIdx;
    RegSetValueExW(k, L"SymbolIndex",   0, REG_DWORD, (const BYTE*)&sy, sizeof(sy));
    RegSetValueExW(k, L"IntervalIndex", 0, REG_DWORD, (const BYTE*)&iv, sizeof(iv));
    DWORD sv = ctx->showVol ? 1 : 0;
    RegSetValueExW(k, L"ShowVolume",    0, REG_DWORD, (const BYTE*)&sv, sizeof(sv));
    DWORD si = ctx->showInd ? 1 : 0;
    RegSetValueExW(k, L"ShowIndicators", 0, REG_DWORD, (const BYTE*)&si, sizeof(si));
    DWORD svd = ctx->showVolDesk ? 1 : 0, sid = ctx->showIndDesk ? 1 : 0;
    RegSetValueExW(k, L"ShowVolumeDesktop",     0, REG_DWORD, (const BYTE*)&svd, sizeof(svd));
    RegSetValueExW(k, L"ShowIndicatorsDesktop", 0, REG_DWORD, (const BYTE*)&sid, sizeof(sid));
    RegCloseKey(k);
}

// The price alerts (phase 23): one REG_BINARY per symbol, "Alerts_BTCUSDT",
// with the levels as doubles with the side in the sign. The name is the API
// symbol, not the index: the table may get more symbols or a new order, and an
// alert at 75 000 must never end up on SOL. A separate function, not part of
// SaveConfig, for the same reason as SaveDesktopMode: it is written the moment
// the user sets or removes an alert, and when one fires - not in WM_DESTROY,
// which never runs when the process is killed. A symbol without alerts gets
// its value deleted. A duplicate neither writes nor reads: the alerts it sets
// live with the panel, and an alert in the registry fires once, from the main
// instance.
static void SaveAlerts(const AppContext* ctx) {
    if (g_isDuplicate) return;
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0,
                        KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS) {
        return;
    }
    for (int s = 0; s < SYMBOL_COUNT; ++s) {
        wchar_t name[48];
        swprintf_s(name, 48, L"Alerts_%s", SYMBOLS[s].api);
        int n = ctx->alertCount[s];
        if (n <= 0) {
            RegDeleteValueW(k, name);
        } else {
            RegSetValueExW(k, name, 0, REG_BINARY, (const BYTE*)ctx->alerts[s],
                           (DWORD)(n * sizeof(double)));
        }
    }
    RegCloseKey(k);
}

// Bounds check as in LoadConfig: wrong type, a length that is not a whole
// number of doubles, too many, NaN, zero or a meaningless number are
// discarded one by one, and the rest is kept.
static void LoadAlerts(AppContext* ctx) {
    if (g_isDuplicate) return;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return;
    }
    for (int s = 0; s < SYMBOL_COUNT; ++s) {
        wchar_t name[48];
        swprintf_s(name, 48, L"Alerts_%s", SYMBOLS[s].api);
        double tmp[ALERT_MAX];
        DWORD cb = sizeof(tmp), type = 0;
        ctx->alertCount[s] = 0;
        if (RegQueryValueExW(k, name, NULL, &type, (BYTE*)tmp, &cb) != ERROR_SUCCESS ||
            type != REG_BINARY || cb % sizeof(double) != 0) {
            continue;
        }
        int n = (int)(cb / sizeof(double));
        for (int i = 0; i < n && ctx->alertCount[s] < ALERT_MAX; ++i) {
            double a = fabs(tmp[i]);
            // Written as "not inside", so NaN - which answers no to every
            // comparison - drops out together with infinity.
            if (!(a > 0.0 && a < ALERT_PRICE_MAX)) continue;
            ctx->alerts[s][ctx->alertCount[s]++] = tmp[i];
        }
    }
    RegCloseKey(k);
}

// Desktop mode does not save: the surface is the whole screen in WorkerW
// coordinates, and it would become panel mode's "saved size" at the next
// startup.
//
// The globals are updated too (phase 12): PlacePopupInitially reads them, and
// the panel is recreated every time the mode changes. Without this, a trip
// through desktop mode would put the panel where it stood at startup.
static void SaveGeometry(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0 || g_desktopMode) return;
    g_savedPanelX = x; g_savedPanelY = y;
    g_savedPanelW = w; g_savedPanelH = h;
    if (g_isDuplicate) return;
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0,
                        KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS) {
        return;
    }
    DWORD dw = (DWORD)w, dh = (DWORD)h;
    DWORD dx = (DWORD)(LONG)x, dy = (DWORD)(LONG)y, one = 1;
    RegSetValueExW(k, L"PanelWidth",  0, REG_DWORD, (const BYTE*)&dw, sizeof(dw));
    RegSetValueExW(k, L"PanelHeight", 0, REG_DWORD, (const BYTE*)&dh, sizeof(dh));
    RegSetValueExW(k, L"PanelX",      0, REG_DWORD, (const BYTE*)&dx, sizeof(dx));
    RegSetValueExW(k, L"PanelY",      0, REG_DWORD, (const BYTE*)&dy, sizeof(dy));
    RegSetValueExW(k, L"PanelHasPos", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
    RegCloseKey(k);
}

// Saves position and size as the window stands NOW. A minimized or
// maximized window is not saved as such - then we would remember a
// taskbar strip or the whole screen as "the user's size".
// GetWindowPlacement gives the restored geometry in both cases.
static void SaveWindowPlacement(HWND hwnd) {
    if (!hwnd) return;
    WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
    if (!GetWindowPlacement(hwnd, &wp)) return;
    RECT r = wp.rcNormalPosition;
    SaveGeometry(r.left, r.top, r.right - r.left, r.bottom - r.top);
}

// Is the saved position still on a monitor that exists? A position from a
// disconnected monitor would put the window outside everything visible.
static BOOL PlacementIsVisible(int x, int y, int w, int h) {
    RECT r = { x, y, x + w, y + h };
    return MonitorFromRect(&r, MONITOR_DEFAULTTONULL) != NULL;
}

// Centers the window on the monitor it is on, at the factory size.
// SWP_NOZORDER | SWP_NOACTIVATE: we change geometry, not z-order or
// focus - the user may have pressed Ctrl+0 from another window.
static void ResetToDefaultView(HWND hwnd) {
    if (!hwnd) return;

    // If the window was maximized, it must be restored first - otherwise
    // SetWindowPos would write a size that the OS overrides on restore.
    //
    // IsIconic belongs here for the same reason, and one more: a minimized
    // window is still WS_VISIBLE, so IsWindowVisible is TRUE and the tray
    // menu's "Default view" skips TogglePopup. Without this,
    // SetWindowPos only set the restored geometry while the window stayed
    // minimized - the menu item did nothing visible. Measured after the
    // minimize button made that path easy to reach.
    if (IsZoomed(hwnd) || IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);

    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(MONITORINFO) };
    if (!GetMonitorInfoW(hMon, &mi)) return;
    RECT wa = mi.rcWork;

    // "DPI-scaled 1280x720". The process is DPI-unaware today, so
    // GetDpiForWindow gives 96 and MulDiv is the identity. If we turned on
    // DPI awareness, this would be correct without further changes - unlike
    // a hardcoded 1280, which would give a small window on a 200 % display.
    // We do NOT turn it on here: the whole layout is in raw pixels, and the
    // watermark's clamp limits would count the scaling twice (see the DPI
    // comment in EnsureWatermark).
    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) dpi = 96;
    int w = MulDiv(POPUP_W, (int)dpi, 96);
    int h = MulDiv(POPUP_H, (int)dpi, 96);

    // If the factory size does not fit, clamp it. A 1280x720 centered on
    // a 1366x768 display would otherwise put the button row outside the
    // work area.
    int aw = wa.right - wa.left, ah = wa.bottom - wa.top;
    if (w > aw) w = aw;
    if (h > ah) h = ah;

    int x = wa.left + (aw - w) / 2;
    int y = wa.top  + (ah - h) / 2;

    SetWindowPos(hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    SaveWindowPlacement(hwnd);
}

// Windows 11 rounds the corners of every window with WS_THICKFRAME, even when
// the frame is removed in WM_NCCALCSIZE. The ~8 px radius eats the corner of
// the close cross. The attribute is 33 from Windows 11 21H2; if the call fails
// on Windows 10, there is no rounding to turn off - hence no error handling.
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_DONOTROUND
#define DWMWCP_DONOTROUND 1
#endif
static void SquareCorners(HWND hwnd) {
    DWORD pref = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
}

// Sets the title bar to the active pair. With a real OS frame the title is
// visible in both the window and the taskbar, so it should say what you see.
static void UpdatePopupTitle(AppContext* ctx) {
    if (!ctx->hPopup) return;
    wchar_t title[96];
    swprintf_s(title, 96, L"%s  -  %s",
               SYMBOLS[ctx->symIdx].label, INTERVALS[ctx->ivIdx].label);
    SetWindowTextW(ctx->hPopup, title);
}

// The span follows the zoom - "(60m)" would be wrong as soon as you zoom. With
// a variable interval it is no longer enough to count candles as minutes: 300
// candles of 1d are ten months, not five hours.
static void FormatSpan(int vc, long long intervalMs, wchar_t* out, size_t cch) {
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

// Merges incoming candles with the buffer on openTime: the same timestamp
// updates (the last candle changes while it forms), newer ones are appended.
// This is what makes panning stable - without it the indices would shift
// with every fetch and the view would drift away.
static void MergeCandles(AppContext* ctx, const Candle* in, int count) {
    if (count <= 0) return;

    // Gap between the buffer and the new set (the panel has been closed for
    // a while) -> start over. Otherwise the chart would draw a continuous
    // curve straight across dead time.
    if (ctx->candleCount > 0 &&
        in[0].openTime > ctx->candles[ctx->candleCount - 1].openTime + 2 * ctx->intervalMs) {
        ctx->candleCount = 0;
        ctx->viewStart   = 0;
        ctx->viewCount   = 0;
        ctx->followLive  = TRUE;
        ctx->dispValid   = FALSE;   // new buffer: nothing to ease from
        ctx->histPending = FALSE;   // new buffer: the history starts over
        ctx->histDone    = FALSE;
    }

    for (int i = 0; i < count; ++i) {
        const Candle* c = &in[i];
        int n = ctx->candleCount;

        if (n == 0) {
            ctx->candles[0]  = *c;
            ctx->candleCount = 1;
            continue;
        }

        long long lastT = ctx->candles[n - 1].openTime;

        if (c->openTime == lastT) {          // the candle still forming
            ctx->candles[n - 1] = *c;
            continue;
        }

        if (c->openTime > lastT) {           // new candle
            if (n >= MAX_CANDLES) {          // oldest drops out
                memmove(ctx->candles, ctx->candles + 1, (size_t)(n - 1) * sizeof(Candle));
                n--;
                ctx->candleCount = n;
                ctx->frontShift++;     // the UI thread shifts the disp indices by this
                if (ctx->viewStart > 0) ctx->viewStart--;
            }
            ctx->candles[n]  = *c;
            ctx->candleCount = n + 1;
            continue;
        }

        // Older than the last: update if we already have it
        for (int j = n - 2; j >= 0; --j) {
            if (ctx->candles[j].openTime == c->openTime) { ctx->candles[j] = *c; break; }
            if (ctx->candles[j].openTime <  c->openTime) break;
        }
    }

    // If the view was at the far right, it should follow the new candles.
    // If the user has panned back, it stays put.
    //
    // viewCount == 0 means "not set" - the panel was opened (or the symbol
    // changed) before there were candles. It must become the default view
    // HERE: ClampView below clamps 0 up to MIN_VIEW, and WorkerFetchKlines'
    // own "0 -> DEFAULT_VIEW" comes too late to see the zero. Measured: the
    // first opening showed 8 candles instead of 300, in every build since
    // phase 1 - and every duplicate from [ + ] opens just before it has data.
    if (ctx->followLive) {
        if (ctx->viewCount <= 0) {
            ctx->viewCount = (ctx->candleCount < DEFAULT_VIEW) ? ctx->candleCount : DEFAULT_VIEW;
        }
        ctx->viewStart = ctx->candleCount - ctx->viewCount;
    }
    ClampView(ctx);
}

// Backfill (phase 18): puts older candles IN FRONT of the buffer. in is
// ascending in time, as from ParseKlines. Called under the lock.
//
// Candles that are not older than candles[0] are dropped - endTime in the
// query is candles[0].openTime - 1, so they should not exist, but the server
// decides. Of the rest, the NEWEST that fit under MAX_CANDLES are taken; the
// oldest are lost, and the buffer is then full. The view is moved k places so
// the same candles stand under it, and frontShift is counted down so the UI
// thread moves display, hover and pan anchor by the same amount. followLive
// is untouched: a view that followed the last candle still does.
//
// histDone is set when the buffer is full, and when nothing of what arrived
// was usable: then the server has nothing older, and the next wall hit should
// not ask again.
static void PrependCandles(AppContext* ctx, const Candle* in, int count) {
    if (count <= 0 || ctx->candleCount <= 0) return;

    long long oldest = ctx->candles[0].openTime;
    int usable = 0;
    while (usable < count && in[usable].openTime < oldest) usable++;

    int room = MAX_CANDLES - ctx->candleCount;
    int k    = (usable < room) ? usable : room;
    if (k > 0) {
        const Candle* src = in + (usable - k);   // the newest of the usable ones
        memmove(ctx->candles + k, ctx->candles, (size_t)ctx->candleCount * sizeof(Candle));
        memcpy(ctx->candles, src, (size_t)k * sizeof(Candle));
        ctx->candleCount += k;
        ctx->frontShift  -= k;
        // viewCount 0 is "show all" (GetView) and must stay so: ClampView
        // would raise 0 to MIN_VIEW. With a set view, it is moved k
        // places, so the same candles stand under it.
        if (ctx->viewCount > 0) {
            ctx->viewStart += k;
            ClampView(ctx);
        }
    }
    if (usable == 0 || ctx->candleCount >= MAX_CANDLES) ctx->histDone = TRUE;
}

static inline int GlyphIndex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c == '.' || c == ',') return IDX_DOT;
    if (c == 'k' || c == 'K') return 11;
    return IDX_SPACE;
}

// The dot is 1px wide so that "75.8" just fits within 16px.
static inline int GlyphWidth(int idx) {
    return (idx == IDX_DOT) ? 1 : GLYPH_W;
}

// Total width in pixels, including 1px spacing between the glyphs.
static int IconTextWidth(const char* s) {
    int total = 0;
    for (int i = 0; s[i]; ++i) {
        total += GlyphWidth(GlyphIndex(s[i]));
        if (s[i + 1]) total += 1;
    }
    return total;
}

// Picks divisor and number of decimals so that the FINISHED FORMATTED string
// fits in 16 px. Thresholds on the price itself do not catch the case where
// "%.1f" rounds 99950 up to "100.0" - it is the width that counts. See bug #5
// in WORKLOG.md.
static void FormatIconPrice(double price, char* out, size_t cb) {
    struct { double div; const char* fmt; } cand[] = {
        { 1.0,       "%.2f" },
        { 1.0,       "%.1f" },
        { 1.0,       "%.0f" },
        { 1000.0,    "%.1f" },
        { 1000.0,    "%.0f" },
        { 1000000.0, "%.1f" },
        { 1000000.0, "%.0f" },
    };
    int n = (int)(sizeof(cand) / sizeof(cand[0]));
    for (int i = 0; i < n; ++i) {
        char tmp[16];
        snprintf(tmp, sizeof(tmp), cand[i].fmt, price / cand[i].div);
        if (IconTextWidth(tmp) <= 16) {
            strcpy_s(out, cb, tmp);
            return;
        }
    }
    // No candidate fits: clip rather than show nothing. The painting
    // in RenderMicroFontIcon is bounds checked.
    snprintf(out, cb, "%.0f", price / 1000000.0);
}

// argb: the color the digits are drawn with. Dimmed when the connection is
// gone, so that the icon tells that the number is no longer fresh.
static HICON RenderMicroFontIcon(const char* str, unsigned int argb) {
    int width = 16;
    int height = 16;

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    unsigned int* pixels = NULL;
    HDC hdcScr = GetDC(NULL);
    HBITMAP hBitmap = CreateDIBSection(hdcScr, &bmi, DIB_RGB_COLORS, (void**)&pixels, NULL, 0);
    ReleaseDC(NULL, hdcScr);

    if (!pixels) return NULL;

    // Background: ARGB dark charcoal (0xFF0D1117)
    for (int i = 0; i < width * height; ++i) {
        pixels[i] = 0xFF0D1117;
    }

    int len = (int)strlen(str);
    int startX = (width - IconTextWidth(str)) / 2;
    if (startX < 0) startX = 0;
    int startY = (height - GLYPH_H) / 2;

    int cx = startX;
    for (int i = 0; i < len; ++i) {
        int glyphIdx = GlyphIndex(str[i]);

        if (glyphIdx == IDX_DOT) {
            int px = cx, py = startY + GLYPH_H - 1;
            if (px >= 0 && px < width && py >= 0 && py < height) {
                pixels[py * width + px] = argb;
            }
        } else if (glyphIdx != IDX_SPACE) {
            for (int r = 0; r < GLYPH_H; ++r) {
                unsigned char row = FONT_4X9[glyphIdx][r];
                for (int c = 0; c < GLYPH_W; ++c) {
                    if ((row >> (GLYPH_W - 1 - c)) & 1) {
                        int px = cx + c, py = startY + r;
                        if (px >= 0 && px < width && py >= 0 && py < height) {
                            pixels[py * width + px] = argb;
                        }
                    }
                }
            }
        }

        cx += GlyphWidth(glyphIdx) + 1;
    }

    HBITMAP hMonoMask = CreateBitmap(width, height, 1, 1, NULL);

    ICONINFO ii = {0};
    ii.fIcon    = TRUE;
    ii.hbmMask  = hMonoMask;
    ii.hbmColor = hBitmap;

    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hBitmap);
    DeleteObject(hMonoMask);

    return hIcon;
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------

// Shared GET against api.binance.com. Reads the whole response in a loop - a
// single WinHttpReadData call returns only what happens to be in the buffer.
#ifdef TICKER_PROBE
// The fixture branch of HttpGet (phase 34). Picks the file from the request
// path; see g_fixtureDir. Reads at most bufSize - 1 bytes, like the network
// branch.
static BOOL FixtureGet(const wchar_t* path, char* buf, DWORD bufSize) {
    wchar_t sym[24] = L"", iv[16] = L"", file[MAX_PATH];
    const wchar_t* p = wcsstr(path, L"symbol=");
    if (p) { p += 7; int k = 0; while (*p && *p != L'&' && k < 23) sym[k++] = *p++; sym[k] = 0; }
    p = wcsstr(path, L"interval=");
    if (p) { p += 9; int k = 0; while (*p && *p != L'&' && k < 15) iv[k++] = *p++; iv[k] = 0; }
    BOOL hist = wcsstr(path, L"endTime=") != NULL;
    if (wcsstr(path, L"/klines"))      swprintf_s(file, MAX_PATH, L"%s\\%s_%s_%s.json", g_fixtureDir, hist ? L"hist" : L"klines", sym, iv);
    else if (wcsstr(path, L"/price")) swprintf_s(file, MAX_PATH, L"%s\\price_%s.json", g_fixtureDir, sym);
    else return FALSE;

    HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        if (hist) { strcpy_s(buf, bufSize, "[]"); return TRUE; }
        return FALSE;
    }
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, bufSize - 1, &got, NULL) && got > 0;
    CloseHandle(h);
    buf[ok ? got : 0] = '\0';
    return ok;
}
#endif

static BOOL HttpGet(AppContext* ctx, const wchar_t* path, char* buf, DWORD bufSize) {
    if (bufSize == 0) return FALSE;
    buf[0] = '\0';
#ifdef TICKER_PROBE
    if (g_fixtureDir[0]) return FixtureGet(path, buf, bufSize);
#endif

    if (!ctx->hConnect) {
        ctx->hConnect = WinHttpConnect(ctx->hSession, L"api.binance.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!ctx->hConnect) return FALSE;
    }

    HINTERNET hRequest = WinHttpOpenRequest(ctx->hConnect, L"GET", path, NULL,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    if (!hRequest) return FALSE;

    DWORD total = 0;
    BOOL ok = FALSE;

    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, NULL)) {

        // The status code (phase 18). Before, any body counted as success,
        // even a 429 with a JSON error message - the parsers caught it
        // silently as "zero candles" / "no price". The backfill needs the
        // distinction: 2xx with zero candles means "the history has ended",
        // everything else is an error that goes into backoff. A 4xx on the old
        // paths now goes the same way, with the same outcome as before.
        DWORD status = 0, cb = sizeof(status);
        BOOL  is2xx  = WinHttpQueryHeaders(hRequest,
                                           WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &cb,
                                           WINHTTP_NO_HEADER_INDEX)
                       && status >= 200 && status < 300;

        if (is2xx) {
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;

                DWORD room = bufSize - 1 - total;
                if (room == 0) break;
                if (avail > room) avail = room;

                DWORD got = 0;
                if (!WinHttpReadData(hRequest, buf + total, avail, &got) || got == 0) break;
                total += got;
            }

            buf[total] = '\0';
            ok = (total > 0);
        }
    }

    WinHttpCloseHandle(hRequest);
    return ok;
}

// Sanity check on everything that comes in from the network (phase 24). The
// parsers trusted atof: text became 0.0, "1e999" became inf, "nan" became
// NaN - and everything went on to lastPrice and candles[]. An inf in a candle
// blows up the Y scale (and double -> int in the coordinates is undefined), a
// NaN price draws a blank icon. The comparisons below are false for NaN, so
// it drops out without isnan; the ceiling catches inf without isfinite.
// Neither pulls in anything from the CRT (pitfall 75).
static BOOL PriceSane(double v) {
    return v > 0.0 && v < 1e15;
}

static BOOL CandleSane(const Candle* c) {
    return c->openTime > 0 &&
           PriceSane(c->open) && PriceSane(c->high) &&
           PriceSane(c->low)  && PriceSane(c->close) &&
           c->high >= c->low  &&
           c->high >= c->open && c->high >= c->close &&
           c->low  <= c->open && c->low  <= c->close &&
           c->volume >= 0.0   && c->volume < 1e18;
}

// p points at the opening quote in "123.45". Returns the pointer past the
// closing one, or NULL if what is between the quotes is not ONE whole
// number: empty, text, garbage after the number, or a response cut midway.
static const char* ParseQuotedNumber(const char* p, double* out) {
    char* end;
    double v = strtod(p + 1, &end);
    if (end == p + 1 || *end != '"') return NULL;
    *out = v;
    return end + 1;
}

// outPrice is touched only when the response held a sane price.
static BOOL FastParsePrice(const char* json, double* outPrice) {
    const char* key = "\"price\":";
    const char* pos = strstr(json, key);
    double v;
    if (!pos) return FALSE;

    pos += strlen(key);
    if (*pos != '"' || !ParseQuotedNumber(pos, &v) || !PriceSane(v)) {
#ifdef TICKER_PROBE
        InterlockedIncrement(&g_probeRejects);
#endif
        return FALSE;
    }
    *outPrice = v;
    return TRUE;
}

// Binance klines: [[openTime,"o","h","l","c","v",closeTime,...], ...]
// We need fields 1-5 (open/high/low/close/volume) from each inner array.
//
// A candle that is not sane (CandleSane), that does not have five quoted
// numbers, or whose time is not STRICTLY greater than the previous accepted
// one, IS SKIPPED - the rest of the response is kept. *rejected counts them:
// zero candles with rejects is a broken response, zero candles without is
// "the history has ended" (WorkerFetchHistory). Ascending time is what
// PrependCandles and MergeCandles build on.
static int ParseKlines(const char* json, Candle* out, int maxCount, int* rejected) {
    int count = 0;
    const char* p = json;

    *rejected = 0;
    while (*p && *p != '[') p++;
    if (!*p) return 0;
    p++; // past outer '['

    while (*p && count < maxCount) {
        while (*p && *p != '[' && *p != ']') p++;
        if (*p != '[') break; // hit ']' -> end of outer array
        p++;                  // past inner '['

        // Field 0 = openTime (Unix ms, number without quotes)
        Candle c;
        while (*p == ' ') p++;
        c.openTime = _atoi64(p);

        // Fields 1-5: open, high, low, close, volume - all quoted strings.
        // The search for the quote stops at the brackets: before, a candle
        // with unquoted fields borrowed the numbers from the NEXT candle.
        double v[5];
        int ok = 1;
        for (int f = 0; f < 5; ++f) {
            while (*p && *p != '"' && *p != '[' && *p != ']') p++;
            const char* q = (*p == '"') ? ParseQuotedNumber(p, &v[f]) : NULL;
            if (!q) { ok = 0; break; }
            p = q;
        }

        if (ok) {
            c.open  = v[0];
            c.high  = v[1];
            c.low   = v[2];
            c.close = v[3];
            c.volume = v[4];   // phase 21
            ok = CandleSane(&c) &&
                 (count == 0 || c.openTime > out[count - 1].openTime);
        }
        if (ok) {
            out[count++] = c;
        } else {
            (*rejected)++;
#ifdef TICKER_PROBE
            InterlockedIncrement(&g_probeRejects);
#endif
        }

        // Skip to the end of this inner array. If p is on a quote that
        // could not be read, it is still inside the array.
        while (*p && *p != ']') p++;
        if (*p) p++;
    }

    return count;
}

// ---------------------------------------------------------------------------
// Worker thread: ALL network traffic happens here. The UI thread never
// touches WinHTTP, and so is never left waiting on the line.
// ---------------------------------------------------------------------------

static BOOL WorkerFetchKlines(AppContext* ctx) {
    BOOL seed;
    unsigned gen;
    int si, ii;
    long long ivMs;

    EnterCriticalSection(&ctx->lock);
    seed = (ctx->candleCount == 0);
    gen  = ctx->configGen;
    si   = ctx->symIdx;
    ii   = ctx->ivIdx;
    ivMs = ctx->intervalMs;
    if (!seed) {
        long long lastT = ctx->candles[ctx->candleCount - 1].openTime;
        if (NowUnixMs() - lastT > 5 * ivMs) seed = TRUE;
    }
    LeaveCriticalSection(&ctx->lock);

    wchar_t path[160];
    swprintf_s(path, 160, L"/api/v3/klines?symbol=%s&interval=%s&limit=%d",
               SYMBOLS[si].api, INTERVALS[ii].api, seed ? SEED_COUNT : 3);

    // The fetch itself happens WITHOUT the lock - it can take hundreds of
    // milliseconds, and the UI thread must be able to paint all the time.
    if (!HttpGet(ctx, path, s_httpBuf, (DWORD)sizeof(s_httpBuf))) return FALSE;

    int rejected;
    int n = ParseKlines(s_httpBuf, s_incoming, SEED_COUNT, &rejected);
    if (n <= 0) return FALSE;

    EnterCriticalSection(&ctx->lock);
    // The discarding happens at MERGE, not at fetch - the response can arrive
    // at any time along the way, even after the user has changed symbol.
    // Returns TRUE: a discarded response is not a network error, and must
    // not count up the backoff every time the user switches.
    if (ctx->configGen != gen) {
        LeaveCriticalSection(&ctx->lock);
        return TRUE;
    }
    MergeCandles(ctx, s_incoming, n);

    // The candle branch MUST also set lastPrice. When the panel is open, the
    // thread fetches only candles - then lastPrice was never written, and
    // after a symbol change (which resets it) UpdateIcon returned on
    // price <= 0. The icon and tooltip stayed on the PREVIOUS symbol's price
    // and label as long as the panel was open. Measured: 15 s after switching
    // to SOL the icon still read 75.9 - BTC - while the panel showed SOL.
    if (ctx->candleCount > 0) {
        ctx->lastPrice = ctx->candles[ctx->candleCount - 1].close;
    }

    if (ctx->followLive) {
        int vc = ctx->viewCount;
        if (vc <= 0) vc = DEFAULT_VIEW;
        if (vc > ctx->candleCount) vc = ctx->candleCount;
        ctx->viewCount = vc;
        ctx->viewStart = ctx->candleCount - vc;
        if (ctx->viewStart < 0) ctx->viewStart = 0;
    }
    LeaveCriticalSection(&ctx->lock);
    return TRUE;
}

// Backfill (phase 18): SEED_COUNT candles older than the oldest we have. Runs
// BEFORE the incremental fetch in the cycle where histPending is set, so the
// live candle stays fresh regardless. TRUE means, as elsewhere, "not a
// network error". If HttpGet fails, the flag is released: the next wall hit
// asks again. Otherwise a lasting 4xx on this path alone could have starved
// the candle fetch.
static BOOL WorkerFetchHistory(AppContext* ctx) {
    unsigned  gen;
    int       si, ii;
    long long endTime;

    EnterCriticalSection(&ctx->lock);
    if (!ctx->histPending) { LeaveCriticalSection(&ctx->lock); return TRUE; }
    if (ctx->candleCount <= 0 || ctx->histDone) {
        ctx->histPending = FALSE;
        LeaveCriticalSection(&ctx->lock);
        return TRUE;
    }
    if (ctx->candleCount >= MAX_CANDLES) {
        ctx->histPending = FALSE;
        ctx->histDone    = TRUE;
        LeaveCriticalSection(&ctx->lock);
        return TRUE;
    }
    gen     = ctx->configGen;
    si      = ctx->symIdx;
    ii      = ctx->ivIdx;
    endTime = ctx->candles[0].openTime - 1;   // endTime is inclusive at Binance
    LeaveCriticalSection(&ctx->lock);

    wchar_t path[192];
    swprintf_s(path, 192, L"/api/v3/klines?symbol=%s&interval=%s&endTime=%lld&limit=%d",
               SYMBOLS[si].api, INTERVALS[ii].api, endTime, SEED_COUNT);

    int  rejected = 0;
    BOOL got = HttpGet(ctx, path, s_httpBuf, (DWORD)sizeof(s_httpBuf));
    int  n   = got ? ParseKlines(s_httpBuf, s_incoming, SEED_COUNT, &rejected) : 0;
    // 2xx, but only unsound candles (phase 24): a broken response, not the end
    // of the history. Without this, histDone was set for good on garbage.
    if (n <= 0 && rejected > 0) got = FALSE;

    EnterCriticalSection(&ctx->lock);
    if (ctx->configGen == gen) {
        ctx->histPending = FALSE;
        if (got) {
            if (n <= 0) ctx->histDone = TRUE;   // 2xx without candles: the history has ended
            else        PrependCandles(ctx, s_incoming, n);
        }
    }
    LeaveCriticalSection(&ctx->lock);
    return got;
}

static BOOL WorkerFetchPrice(AppContext* ctx) {
    unsigned gen;
    int si;

    EnterCriticalSection(&ctx->lock);
    gen = ctx->configGen;
    si  = ctx->symIdx;
    LeaveCriticalSection(&ctx->lock);

    wchar_t path[96];
    swprintf_s(path, 96, L"/api/v3/ticker/price?symbol=%s", SYMBOLS[si].api);

    char buf[512];
    if (!HttpGet(ctx, path, buf, (DWORD)sizeof(buf))) return FALSE;

    double price = 0.0;
    if (!FastParsePrice(buf, &price)) return FALSE;

    // The price has exactly the same race as the candles, and it drives the
    // tray icon. Without the check the icon shows the previous symbol's price
    // under the new name.
    EnterCriticalSection(&ctx->lock);
    if (ctx->configGen == gen) ctx->lastPrice = price;
    LeaveCriticalSection(&ctx->lock);
    return TRUE;
}

static DWORD WINAPI NetworkThread(LPVOID param) {
    AppContext* ctx = (AppContext*)param;
    HANDLE waits[2] = { ctx->hStopEvent, ctx->hWakeEvent };

    for (;;) {
        EnterCriticalSection(&ctx->lock);
        HWND hp   = ctx->hPopup;
        BOOL hist = ctx->histPending;
        BOOL drop = ctx->dropConn;
        ctx->dropConn = FALSE;
        LeaveCriticalSection(&ctx->lock);

        // The machine has slept (phase 24). The connection from before the
        // sleep is dead, but WinHTTP does not know until a call has timed
        // out, and NET_RECONNECT_AT only releases it after three failures in
        // a row. Released here, so the first attempt after wakeup looks up
        // DNS and negotiates TLS anew.
        if (drop && ctx->hConnect) {
            WinHttpCloseHandle(ctx->hConnect);
            ctx->hConnect = NULL;
#ifdef TICKER_PROBE
            InterlockedIncrement(&g_probeConnDrops);
#endif
        }

        // If the chart is open we need candles; otherwise the price is enough.
        // If the UI wants older candles (phase 18), they are fetched first,
        // and the candles right after - two calls in that cycle, so the live
        // candle does not wait.
        BOOL ok;
        if (hp && IsWindowVisible(hp)) {
            ok = hist ? WorkerFetchHistory(ctx) : TRUE;
            if (ok) ok = WorkerFetchKlines(ctx);
        } else {
            ok = WorkerFetchPrice(ctx);
        }
#ifdef TICKER_PROBE
        InterlockedIncrement(&g_probeFetches);
#endif

        ULONGLONG now = GetTickCount64();
        DWORD wait;
        int failures;

        EnterCriticalSection(&ctx->lock);
        if (ok) {
            ctx->netFailures = 0;
            ctx->lastOkTick  = now;
        } else if (ctx->netFailures < 32) {
            ctx->netFailures++;   // the cap prevents overflow on long downtime
        }
        failures = ctx->netFailures;
        wait = NetBackoffMs(failures, now);
        ctx->nextRetryTick = now + wait;
        LeaveCriticalSection(&ctx->lock);

        // If we are stuck on an IP that no longer answers, it does not help
        // to retry against the same handle. Releases the connection so HttpGet
        // builds it anew and DNS is looked up again. hConnect is owned by this
        // thread alone, so it needs no lock - failures, on the other hand,
        // must be read under the lock above.
        if (!ok && failures == NET_RECONNECT_AT && ctx->hConnect) {
            WinHttpCloseHandle(ctx->hConnect);
            ctx->hConnect = NULL;
        }

        // PostMessage MUST be outside the lock - otherwise the UI thread can
        // sit waiting for the lock while we wait for it.
        PostMessageW(ctx->hWnd, WM_APP_DATA, 0, 0);

        DWORD wr = WaitForMultipleObjects(2, waits, FALSE, wait);
        if (wr == WAIT_OBJECT_0) break;   // hStopEvent

        // The reset MUST hang on hWakeEvent alone. When it stood after
        // the whole wait call, it also hit WAIT_TIMEOUT - that is, every
        // single cycle - and netFailures never got higher than 1.
        // The backoff was then stuck at ~6 s and hConnect was never released.
        // Measured, not assumed: log with failures=1 in 20 cycles in a row.
        if (wr == WAIT_OBJECT_0 + 1) {
            // The panel was opened. The user should get an attempt right
            // away, not wait out a minute of backoff.
            EnterCriticalSection(&ctx->lock);
            ctx->netFailures = 0;
            LeaveCriticalSection(&ctx->lock);
        }
    }
    return 0;
}

// Draws icon and tooltip from a price. Separate from the fetch so that
// we can reuse the price we already have, instead of fetching it again.
static void UpdateIcon(AppContext* ctx, double price, BOOL stale) {
    if (price <= 0.0) return;
    swprintf_s(ctx->fullPriceStr, 64, L"%s: $%.2f%s",
               SYMBOLS[ctx->symIdx].label, price, stale ? L" (offline)" : L"");

    // Divisor and decimals are chosen by the width of the finished formatted
    // string, not by a threshold on the price. See FormatIconPrice.
    char iconStr[16];
    FormatIconPrice(price, iconStr, sizeof(iconStr));

    // Dimmed digits when the number is no longer fresh. Green 0xFF00FF66
    // blended down toward the background gives a visible but undramatic
    // difference.
    HICON hNewIcon = RenderMicroFontIcon(iconStr, stale ? 0xFF2F6B45 : 0xFF00FF66);
    if (hNewIcon) {
        if (ctx->nid.hIcon) DestroyIcon(ctx->nid.hIcon);
        ctx->nid.hIcon = hNewIcon;
        ctx->nid.uFlags = NIF_ICON | NIF_TIP;
        wcscpy_s(ctx->nid.szTip, 128, ctx->fullPriceStr);
        Shell_NotifyIconW(NIM_MODIFY, &ctx->nid);
    }
}


// ---------------------------------------------------------------------------
// Chart drawing (GDI, double-buffered)
// ---------------------------------------------------------------------------

// Linear color blend. t=0 gives a, t=255 gives b. Everything is drawn opaque
// over CLR_BG, so blending toward the background is identical to real
// transparency.
static COLORREF Blend(COLORREF a, COLORREF b, int t) {
    if (t <= 0) return a;
    if (t >= 255) return b;
    int r = (GetRValue(a) * (255 - t) + GetRValue(b) * t) / 255;
    int g = (GetGValue(a) * (255 - t) + GetGValue(b) * t) / 255;
    int l = (GetBValue(a) * (255 - t) + GetBValue(b) * t) / 255;
    return RGB(r, g, l);
}


static BOOL PtInRect2(const RECT* r, int x, int y) {
    return (x >= r->left && x < r->right && y >= r->top && y < r->bottom);
}

// The control buttons in the header. A pure function of the width, without
// state - painting, WM_NCHITTEST, hover and click all read this. If two of
// them read different sources, the user hits a different button than the one
// that lights up. Order from the left: new instance, minimize, maximize,
// close. The cross at the far right, where Windows has trained the eye to
// expect it. [ + ] took the place of the restore-default-view button in the
// same enum position, so the geometry, WM_NCHITTEST and the hover indices are
// unchanged.
typedef enum { BTN_NEW = 0, BTN_MIN, BTN_MAX, BTN_CLOSE, BTN_COUNT } BtnId;

static void ButtonLayout(int W, RECT out[BTN_COUNT]) {
    int right = W - BTN_MARGIN_R;
    for (int i = BTN_COUNT - 1; i >= 0; --i) {
        out[i].right  = right;
        out[i].left   = right - BTN_W;
        out[i].top    = BTN_TOP;
        out[i].bottom = BTN_TOP + BTN_H;
        right = out[i].left - BTN_GAP;
    }
}

// Which button is the mouse pointing at? -1 outside all of them.
static int ButtonHit(const RECT* btns, int x, int y) {
    for (int i = 0; i < BTN_COUNT; ++i) {
        if (PtInRect2(&btns[i], x, y)) return i;
    }
    return -1;
}

// The button row's combined rectangle. Derived from ButtonLayout, not computed
// anew - pitfall 14 applies here too: if we invalidate a different area than
// the one we paint, a button is left un-updated.
static void ButtonStrip(int W, RECT* out) {
    RECT b[BTN_COUNT];
    ButtonLayout(W, b);
    out->left   = b[0].left;
    out->top    = b[0].top;
    out->right  = b[BTN_COUNT - 1].right;
    out->bottom = b[0].bottom;
}

// The collision rule in the header, as a pure function: a left-aligned element
// that ends at rightBound and a right-aligned one that starts at leftBound may
// share a row only with at least HDR_GAP px of space between them.
static BOOL HeaderFits(int rightBound, int leftBound) {
    return rightBound < leftBound - HDR_GAP;
}

// Right limit for the header's row 2: the price axis's top label sits at
// y = top +- 8 from x = edge + AXIS_LBL_GAP, and the row must keep HDR_GAP
// of space to it. The toolbar and the offline text both read this.
static int HeaderRow2Limit(int W) {
    return W - PAD_R + AXIS_LBL_GAP - HDR_GAP;
}

// The toolbar up to and including VOL must fit at the minimum width. If a
// table or a pill width grows past that, the build stops here - and the rule
// in ToolbarLayout, which hides pills from the right, never becomes what the
// user sees on a panel of legal size.
//
// The MA pill (phase 25) is the one deliberate exception: the row ends at
// x = 310 of 312 at the minimum width, and 28 px more does not exist. The pill
// sits at the far right, so the hiding rule takes it and only it: below 426 px
// width it is gone, and the M key and the tray menu carry the toggle alone.
// POPUP_MIN_W is not raised for this - it also guards which geometry the
// registry is allowed to give back.
C_ASSERT(PAD_L + TBAR_SYM_W + TBAR_GROUP_GAP + INTERVAL_COUNT * TBAR_IV_W +
         (INTERVAL_COUNT - 1) * TBAR_GAP + TBAR_GROUP_GAP + TBAR_VOL_W
         <= POPUP_MIN_W - PAD_R + AXIS_LBL_GAP - HDR_GAP);

// The toolbar's pills. A pure function of the width, like ButtonLayout, and
// for the same reason: painting, WM_NCHITTEST, hover and click all read this.
// Returns the number of visible pills; the rest are empty rectangles that
// nothing hits. A pill that does not fit before HeaderRow2Limit is hidden
// entirely, and all after it - never half a pill, and never VOL without the
// intervals in front. The registry accepts a saved width down to 240 px, so
// the branch can be reached even though the C_ASSERT above keeps it away
// from 400.
static int ToolbarLayout(int W, RECT out[TBAR_COUNT]) {
    int limit = HeaderRow2Limit(W);
    int x = PAD_L, n = 0;
    BOOL cut = FALSE;
    for (int i = 0; i < TBAR_COUNT; ++i) {
        int w = (i == TBAR_SYM) ? TBAR_SYM_W : (i == TBAR_VOL) ? TBAR_VOL_W
              : (i == TBAR_IND) ? TBAR_IND_W : TBAR_IV_W;
        if (i == TBAR_IV_FIRST || i == TBAR_VOL) x += TBAR_GROUP_GAP;
        else if (i > 0)                          x += TBAR_GAP;
        if (!cut && x + w > limit) cut = TRUE;
        if (cut) {
            out[i].left = out[i].top = out[i].right = out[i].bottom = 0;
            continue;
        }
        out[i].left   = x;
        out[i].right  = x + w;
        out[i].top    = TBAR_TOP;
        out[i].bottom = TBAR_TOP + TBAR_H;
        x += w;
        n = i + 1;
    }
    return n;
}

// Which pill is the mouse pointing at? -1 outside all of them.
static int ToolbarHit(const RECT* tb, int x, int y) {
    for (int i = 0; i < TBAR_COUNT; ++i) {
        if (PtInRect2(&tb[i], x, y)) return i;
    }
    return -1;
}

// The toolbar's combined rectangle, derived from ToolbarLayout (see
// ButtonStrip). Empty when no pill fits.
static void ToolbarStrip(int W, RECT* out) {
    RECT tb[TBAR_COUNT];
    int n = ToolbarLayout(W, tb);
    out->left = out->top = out->right = out->bottom = 0;
    if (n <= 0) return;
    out->left   = tb[0].left;
    out->top    = tb[0].top;
    out->right  = tb[n - 1].right;
    out->bottom = tb[0].bottom;
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
static int DeskPillH(int H) {
    int h = H / DESK_PILL_DIV;
    if (h < DESK_PILL_MIN) h = DESK_PILL_MIN;
    if (h > DESK_PILL_MAX) h = DESK_PILL_MAX;
    return h;
}

// Same ratio as in the panel: a 16 px stamp around a 15 px font.
static int DeskPillFontH(int H) { return MulDiv(DeskPillH(H), 15, 16); }

// AXIS_CHAR_W is measured at em 15. The width is rounded UP: a character width
// that is really 22.2 px would have given eight characters 1.6 px too little,
// and the price would silently have fallen back to the axis's resolution
// instead of two decimals.
static int DeskAxisW(int H) {
    int cw = (DeskPillFontH(H) * AXIS_CHAR_W + 14) / 15;
    return AXIS_LBL_GAP + AXIS_Y_CHARS * cw + AXIS_PAD_R;
}

static ChartRect ChartGeometry(int W, int H) {
    ChartRect g;
    g.left   = g_desktopMode ? 0 : PAD_L;
    g.top    = g_desktopMode ? 0 : HEADER_H;
    g.edge   = g_desktopMode ? W - DeskAxisW(H) : W - PAD_R;
    g.right  = g.edge - PLOT_PAD_R;
    g.bottom = g_desktopMode ? H : H - PAD_B;
    g.cw     = g.right - g.left;
    g.ch     = g.bottom - g.top;
    return g;
}

// Which candle is the mouse over? Returns an absolute index, -1 outside.
// MUST read the same disp values as DrawChart. If one reads the target and the
// other the display, the crosshair points at the wrong candle mid-animation -
// that is bug #7 from the log in new clothes.
static int HitCandle(const AppContext* ctx, const ChartRect* g, int mx, int my) {
    if (ctx->dispCount <= 0.0 || g->cw <= 0) return -1;
    if (ctx->candleCount <= 0) return -1;
    if (mx < g->left || mx >= g->right || my < g->top || my > g->bottom) return -1;

    double slot = (double)g->cw / ctx->dispCount;
    if (slot <= 0.0) return -1;
    int idx = (int)(ctx->dispStart + (double)(mx - g->left) / slot);

    // Bounded by candleCount, not by vs + vc: during the animation the
    // display can hang outside the target view.
    if (idx < 0) idx = 0;
    if (idx >= ctx->candleCount) idx = ctx->candleCount - 1;
    return idx;
}

// Price alerts (phase 23): price <-> y. Drawing and hit-testing MUST read the
// same source (pitfall 14), so both directions go through these two, and both
// read the DISPLAY (dispMin/dispMax) with the same range guard and the same
// truncation as the candles in DrawChart. The clamp before (int) is for a
// level far outside the view: 75 000 on a SOL axis with a span of 1 gives 5e7
// pixels, and an alert from the registry can be anything below ALERT_PRICE_MAX.
static int AlertY(const AppContext* ctx, const ChartRect* g, double level) {
    double range = ctx->dispMax - ctx->dispMin;
    if (range < 1e-9) range = 1.0;
    double yd = ((ctx->dispMax - level) / range) * (double)g->ch;
    if (yd < -100000.0) yd = -100000.0;
    if (yd >  100000.0) yd =  100000.0;
    return g->top + (int)yd;
}

// The price at height y, rounded to below one pixel (AlertRound).
static double AlertPriceAtY(const AppContext* ctx, const ChartRect* g, int y) {
    double range = ctx->dispMax - ctx->dispMin;
    if (range < 1e-9) range = 1.0;
    if (g->ch <= 0) return 0.0;
    double p = ctx->dispMax - ((double)(y - g->top) / (double)g->ch) * range;
    return AlertRound(p, range / (double)g->ch);
}

// Which alert is the pointer on in the price column? The nearest tag within
// ALERT_HIT_PX - that is, exactly the area the tag is drawn on - and -1
// otherwise. Tags outside [top, bottom] are not drawn and cannot be hit.
static int AlertAxisHit(const AppContext* ctx, const ChartRect* g, int my) {
    int best = -1, bestD = ALERT_HIT_PX + 1;
    int s = ctx->symIdx;
    for (int i = 0; i < ctx->alertCount[s]; ++i) {
        int y = AlertY(ctx, g, fabs(ctx->alerts[s][i]));
        if (y < g->top || y > g->bottom) continue;
        // The tag is [y - 8, y + 8): FillRect is exclusive at the bottom.
        if (my < y - ALERT_HIT_PX || my >= y + ALERT_HIT_PX) continue;
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
static int PriceDecimals(double step) {
    if (step <= 0.0) return 2;
    int d = 0;
    while (step < 2.0 && d < 6) { step *= 10.0; ++d; }
    return d;
}

// Min/max over the visible candles, with 8% headroom above and below.
static void PriceRange(const AppContext* ctx, int vs, int vc, double* outMin, double* outMax) {
    double mn = ctx->candles[vs].low, mx = ctx->candles[vs].high;
    for (int i = 1; i < vc; ++i) {
        const Candle* c = &ctx->candles[vs + i];
        if (c->low  < mn) mn = c->low;
        if (c->high > mx) mx = c->high;
    }
    double range = mx - mn;
    if (range < 1e-9) range = 1.0;
    double pad = range * 0.08;
    *outMin = mn - pad;
    *outMax = mx + pad;
}

// Largest volume in the view (phase 21): the scale of the bars. 0 when no
// candle has volume - then no bars are drawn, instead of the height becoming
// NaN. Called under the lock, like PriceRange.
static double VolumeMax(const AppContext* ctx, int vs, int vc) {
    double mx = 0.0;
    for (int i = 0; i < vc; ++i) {
        double v = ctx->candles[vs + i].volume;
        if (v > mx) mx = v;
    }
    return mx;
}

// When candles drop out at the front, EVERYTHING that is an absolute index is
// shifted by the same amount. Without this the chart jumps one candle to the
// left every minute once the buffer has reached its cap, and hoverIdx points
// at the neighboring candle.
// Idempotent: delta is 0 the second time. Called under the lock.
static void ApplyFrontShift(AppContext* ctx) {
    long long delta = ctx->frontShift - ctx->dispShiftSeen;
    if (delta == 0) return;
    ctx->dispShiftSeen = ctx->frontShift;

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

// The user is up against the wall (viewStart == 0) and wants to go back
// (phase 18). Sets histPending and wakes the thread - but only when the line
// is healthy: hWakeEvent resets the backoff (it is made for "the panel was
// opened"), and a drag against the wall during a disconnect must not switch
// the backoff off. If the thread is in backoff, it sees the flag on its own
// cycle. SetEvent stays outside the lock, as elsewhere in the file.
static void RequestHistory(AppContext* ctx) {
    BOOL wake = FALSE;
    EnterCriticalSection(&ctx->lock);
    if (!ctx->histDone && !ctx->histPending && ctx->candleCount > 0) {
        ctx->histPending = TRUE;
        wake = (ctx->netFailures == 0);
    }
    LeaveCriticalSection(&ctx->lock);
    if (wake) SetEvent(ctx->hWakeEvent);
}

// Sets the display equal to the target without animation. Used when an
// animation makes no sense: first frame, new buffer after a config change,
// the panel opens. Called under the lock.
static void SyncDisp(AppContext* ctx) {
    int vs, vc;
    GetView(ctx, &vs, &vc);
    ctx->dispStart = (double)vs;
    ctx->dispCount = (vc > 0) ? (double)vc : 1.0;
    ctx->dispShiftSeen = ctx->frontShift;

    // With an empty buffer there is no price axis to sync against. If we mark
    // ourselves valid here, dispMin/dispMax ease from [0, 1] up to the real
    // span when the data arrives - that is, a Y axis that slides up from zero
    // for half a second after every symbol change. We stay invalid instead,
    // so the first frame WITH data snaps.
    if (ctx->candleCount <= 0 || vc <= 0) {
        ctx->dispMin   = 0.0;
        ctx->dispMax   = 1.0;
        ctx->dispVolMax = 0.0;
        ctx->dispValid = FALSE;
        return;
    }

    PriceRange(ctx, vs, vc, &ctx->dispMin, &ctx->dispMax);
    ctx->dispVolMax = VolumeMax(ctx, vs, vc);   // phase 21
    ctx->dispValid = TRUE;
}

// Unix ms -> local time. On long candles "HH:MM" is not enough - every 1d
// candle would read "00:00". From 1h upward we include the date.
static void FormatCandleTime(long long unixMs, long long intervalMs,
                             wchar_t* out, size_t cch) {
    ULONGLONG t = (ULONGLONG)(unixMs / 1000) * 10000000ULL + 116444736000000000ULL;
    FILETIME utc, local;
    utc.dwLowDateTime  = (DWORD)(t & 0xFFFFFFFFULL);
    utc.dwHighDateTime = (DWORD)(t >> 32);
    SYSTEMTIME st;
    if (FileTimeToLocalFileTime(&utc, &local) && FileTimeToSystemTime(&local, &st)) {
        if (intervalMs >= 86400000LL) {
            swprintf_s(out, cch, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
        } else if (intervalMs >= 3600000LL) {
            swprintf_s(out, cch, L"%02d-%02d %02d:%02d",
                       st.wMonth, st.wDay, st.wHour, st.wMinute);
        } else {
            swprintf_s(out, cch, L"%02d:%02d", st.wHour, st.wMinute);
        }
    } else {
        wcscpy_s(out, cch, L"--:--");
    }
}


// Layout and hit detection share one function. Two independent calculations of
// the same area end up pointing at different places - see bug #7 in the log.
// Volume for the hover box (phase 21): compact, so DOGE volume in millions
// fits in 104 px. Below a thousand two decimals, otherwise K/M with one decimal.
static void FormatVolume(double v, wchar_t* out, size_t cch) {
    if (v >= 1e6)      swprintf_s(out, cch, L"%.1fM", v / 1e6);
    else if (v >= 1e3) swprintf_s(out, cch, L"%.1fK", v / 1e3);
    else               swprintf_s(out, cch, L"%.2f", v);
}

#define OVL_ROWS_MAX  16
#define OVL_ROW_H     22
#define OVL_COL_W     104
#define OVL_PAD       10
#define OVL_HDR_H     18

typedef struct {
    RECT box;                    // the whole overlay
    RECT rows[OVL_ROWS_MAX];     // one per choice
    int  count;                  // SYMBOL_COUNT first, then INTERVAL_COUNT
    RECT symHdr, ivHdr;          // the headings
} OverlayRects;

static void OverlayLayout(int W, int H, OverlayRects* r) {
    // Zeroed completely. The unused rows past count are otherwise stack
    // garbage, and then the function is no longer pure - two calls with the
    // same input give different contents. The unit test caught exactly that.
    // OverlayHit only goes up to count, so the garbage was harmless today;
    // this closes the class.
    memset(r, 0, sizeof(*r));

    int rowsMax = (SYMBOL_COUNT > INTERVAL_COUNT) ? SYMBOL_COUNT : INTERVAL_COUNT;
    int boxW = OVL_PAD * 3 + OVL_COL_W * 2;
    int boxH = OVL_PAD * 2 + OVL_HDR_H + rowsMax * OVL_ROW_H;

    // Centered, but never outside the panel - the panel can be smaller than
    // the box at the minimum size.
    if (boxW > W) boxW = W;
    if (boxH > H) boxH = H;
    int bx = (W - boxW) / 2, by = (H - boxH) / 2;
    if (bx < 0) bx = 0;
    if (by < 0) by = 0;

    r->box.left = bx; r->box.top = by;
    r->box.right = bx + boxW; r->box.bottom = by + boxH;

    int colW = (boxW - OVL_PAD * 3) / 2;
    if (colW < 1) colW = 1;
    int c1 = bx + OVL_PAD, c2 = c1 + colW + OVL_PAD;
    int y0 = by + OVL_PAD;

    r->symHdr.left = c1; r->symHdr.right = c1 + colW;
    r->symHdr.top  = y0; r->symHdr.bottom = y0 + OVL_HDR_H;
    r->ivHdr.left  = c2; r->ivHdr.right  = c2 + colW;
    r->ivHdr.top   = y0; r->ivHdr.bottom = y0 + OVL_HDR_H;

    int ry = y0 + OVL_HDR_H;
    r->count = 0;
    for (int i = 0; i < SYMBOL_COUNT && r->count < OVL_ROWS_MAX; ++i) {
        RECT* q = &r->rows[r->count++];
        q->left = c1; q->right = c1 + colW;
        q->top = ry + i * OVL_ROW_H; q->bottom = q->top + OVL_ROW_H;
        if (q->bottom > r->box.bottom) q->bottom = r->box.bottom;
    }
    for (int i = 0; i < INTERVAL_COUNT && r->count < OVL_ROWS_MAX; ++i) {
        RECT* q = &r->rows[r->count++];
        q->left = c2; q->right = c2 + colW;
        q->top = ry + i * OVL_ROW_H; q->bottom = q->top + OVL_ROW_H;
        if (q->bottom > r->box.bottom) q->bottom = r->box.bottom;
    }
}

// Index 0..SYMBOL_COUNT-1 are symbols, the rest intervals. -1 = none.
static int OverlayHit(const OverlayRects* r, int x, int y) {
    for (int i = 0; i < r->count; ++i) {
        const RECT* q = &r->rows[i];
        if (x >= q->left && x < q->right && y >= q->top && y < q->bottom) return i;
    }
    return -1;
}

// The palette is CLR_BG/CLR_BOX/CLR_BOXEDGE - identical to the hover box, so
// the overlay reads as the same family of elements.
// Called from PaintPopup, NOT from DrawChart: DrawChart returns early when
// candleCount == 0, and that is exactly the state right after a config change.
// Had the call been there, the fade-out would never be drawn after a change.
static void DrawOverlay(AppContext* ctx, HDC hdc, int W, int H) {
    int a = (int)(ctx->overlayF + 0.5);
    if (a <= 0) return;

    OverlayRects r;
    OverlayLayout(W, H, &r);

    HBRUSH brBox  = CreateSolidBrush(Blend(CLR_BG, CLR_BOX, a));
    HBRUSH brEdge = CreateSolidBrush(Blend(CLR_BG, CLR_BOXEDGE, a));
    FillRect(hdc, &r.box, brBox);
    FrameRect(hdc, &r.box, brEdge);

    SelectObject(hdc, ctx->hFontSmall);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, Blend(CLR_BG, CLR_DIM, a));
    RECT h1 = r.symHdr, h2 = r.ivHdr;
    h1.left += 6; h2.left += 6;
    DrawTextW(hdc, L"SYMBOL",    -1, &h1, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextW(hdc, L"INTERVAL",  -1, &h2, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    for (int i = 0; i < r.count; ++i) {
        BOOL isSym  = (i < SYMBOL_COUNT);
        int  idx    = isSym ? i : (i - SYMBOL_COUNT);
        BOOL active = isSym ? (idx == ctx->symIdx) : (idx == ctx->ivIdx);
        const wchar_t* lbl = isSym ? SYMBOLS[idx].label : INTERVALS[idx].label;

        if (i == ctx->overlayHot) {
            HBRUSH brHot = CreateSolidBrush(Blend(CLR_BG, CLR_BOXEDGE, a / 2));
            FillRect(hdc, &r.rows[i], brHot);
            DeleteObject(brHot);
        }
        COLORREF fg = active ? CLR_UP : CLR_TEXT;
        SetTextColor(hdc, Blend(CLR_BG, fg, a));
        RECT t = r.rows[i]; t.left += 6;
        DrawTextW(hdc, lbl, -1, &t, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }

    // The brushes are created per frame on purpose: the color depends on the
    // fade level, which changes every frame WHILE the overlay fades. At rest
    // the function returns on a <= 0, so the idle GDI count is unchanged.
    DeleteObject(brBox);
    DeleteObject(brEdge);
}

// Makes sure the persistent double buffer exists and has the size W x H.
// Rebuilt only when the size changes - not per frame. Returns FALSE if GDI
// did not give us a buffer; then PaintPopup draws nothing in this frame
// instead of drawing straight to the screen with flicker.
//
// Tear down the old one FIRST. Without this an HBITMAP and an HDC leak per
// resize, and the GDI count climbs every time the user drags the edge.
static void FreeBackBuffer(AppContext* ctx) {
    if (ctx->bbDC) {
        if (ctx->bbOldBmp) SelectObject(ctx->bbDC, ctx->bbOldBmp);
        DeleteDC(ctx->bbDC);
    }
    if (ctx->bbBmp) DeleteObject(ctx->bbBmp);
    ctx->bbDC = NULL;
    ctx->bbBmp = NULL;
    ctx->bbOldBmp = NULL;
    ctx->bbW = ctx->bbH = 0;
    ctx->bbValid = FALSE;
}

static BOOL EnsureBackBuffer(AppContext* ctx, HDC ref, int W, int H) {
    if (ctx->bbDC && ctx->bbW == W && ctx->bbH == H) return TRUE;
    FreeBackBuffer(ctx);
    if (W <= 0 || H <= 0) return FALSE;

    ctx->bbDC  = CreateCompatibleDC(ref);
    ctx->bbBmp = CreateCompatibleBitmap(ref, W, H);
    if (!ctx->bbDC || !ctx->bbBmp) {
        FreeBackBuffer(ctx);
        return FALSE;
    }
    ctx->bbOldBmp = (HBITMAP)SelectObject(ctx->bbDC, ctx->bbBmp);
    ctx->bbW = W;
    ctx->bbH = H;
    return TRUE;
}

// Builds background + watermark when (W, H, symIdx, ivIdx) changes - not
// per frame. Same discipline as the GDI cache from phase 1.
// If anything fails here, wmValid = FALSE is set and DrawChart falls back to
// FillRect. The watermark is decoration; it must never block painting.
static void EnsureWatermark(AppContext* ctx, HDC ref, int W, int H) {
    if (ctx->wmValid && ctx->wmW == W && ctx->wmH == H &&
        ctx->wmSym == ctx->symIdx && ctx->wmIv == ctx->ivIdx) {
        return;
    }
    if (W <= 0 || H <= 0) { ctx->wmValid = FALSE; return; }

    // Tear down the old one FIRST. Without this an HBITMAP and an HDC leak per
    // resize, and the GDI count climbs every time the user drags the edge.
    if (ctx->wmDC) {
        if (ctx->wmOldBmp) SelectObject(ctx->wmDC, ctx->wmOldBmp);
        DeleteDC(ctx->wmDC);
        ctx->wmDC = NULL;
        ctx->wmOldBmp = NULL;
    }
    if (ctx->wmBmp) { DeleteObject(ctx->wmBmp); ctx->wmBmp = NULL; }

    ctx->wmDC  = CreateCompatibleDC(ref);
    ctx->wmBmp = CreateCompatibleBitmap(ref, W, H);
    if (!ctx->wmDC || !ctx->wmBmp) {
        if (ctx->wmDC)  { DeleteDC(ctx->wmDC);      ctx->wmDC  = NULL; }
        if (ctx->wmBmp) { DeleteObject(ctx->wmBmp); ctx->wmBmp = NULL; }
        ctx->wmValid = FALSE;
        return;
    }
    ctx->wmOldBmp = (HBITMAP)SelectObject(ctx->wmDC, ctx->wmBmp);

    RECT rc = { 0, 0, W, H };
    FillRect(ctx->wmDC, &rc, ctx->brBg);

    SetBkMode(ctx->wmDC, TRANSPARENT);
    // Alpha follows W, and W is already part of the cache key above - the
    // color is therefore only computed when the bitmap is built. Everything
    // underneath is opaque CLR_BG, so Blend against the background IS alpha
    // blending.
    SetTextColor(ctx->wmDC, Blend(CLR_BG, CLR_WM_INK,
                                  (int)(WatermarkAlpha(W) * 255.0 + 0.5)));

    ChartRect g = ChartGeometry(W, H);

    // The font height follows the height of the chart surface, not a fixed
    // value: a small panel must not get the watermark clipped, and a large
    // one must not get a small text in the middle of the surface.
    //
    // The DPI scaling applies to the CLAMP LIMITS, not to H/5. g.ch is already
    // device pixels, so the proportional part scales itself when the window
    // gets larger on a high-DPI screen. The limits, however, are given in
    // logical pixels, and a floor of 32 would be 16 logical pixels at 200 %.
    // If we multiply H/5 by DPI as well, we count the scaling twice. The
    // process is DPI-unaware today, so GetDeviceCaps gives 96 and MulDiv is
    // an identity - this comes alive the moment a manifest is added.
    int dpi = GetDeviceCaps(ref, LOGPIXELSY);
    if (dpi <= 0) dpi = 96;
    int fMin = MulDiv(WM_FONT_MIN, dpi, 96);
    int fMax = MulDiv(WM_FONT_MAX, dpi, 96);
    int fh = g.ch / WM_FONT_DIV;
    if (fh < fMin) fh = fMin;
    if (fh > fMax) fh = fMax;

    // Built here, not per frame: EnsureWatermark only runs when
    // (W, H, symIdx, ivIdx) actually changes, and all four affect the
    // height or width the text needs.
    const wchar_t* wmText = SYMBOLS[ctx->symIdx].api;
    int wmLen = (int)wcslen(wmText);

    if (ctx->hFontWm) DeleteObject(ctx->hFontWm);
    ctx->hFontWm = CreateFontW(-fh, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                               L"Segoe UI");

    // The height formula alone gives clipped text on narrow panels: at 280 px
    // fh hit the floor of 32, and "BTCUSDT" became wider than the chart
    // surface. Measured, not assumed - same discipline as bug #5. The width is
    // measured on the actual string in the actual font, and the height is
    // scaled down in the same ratio if it does not fit.
    HFONT prevFit = (HFONT)SelectObject(ctx->wmDC, ctx->hFontWm);
    SIZE sz = { 0, 0 };
    int availW = g.cw - 8;
    if (GetTextExtentPoint32W(ctx->wmDC, wmText, wmLen, &sz) &&
        sz.cx > availW && sz.cx > 0 && availW > 0) {
        int fitted = MulDiv(fh, availW, sz.cx);
        if (fitted < 8) fitted = 8;
        if (fitted < fh) {
            SelectObject(ctx->wmDC, prevFit);
            DeleteObject(ctx->hFontWm);
            ctx->hFontWm = CreateFontW(-fitted, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                       L"Segoe UI");
            fh = fitted;
            prevFit = (HFONT)SelectObject(ctx->wmDC, ctx->hFontWm);
        }
    }
    ctx->wmFontH = fh;

    HFONT prev = prevFit;
    RECT rcSym = { g.left, g.top, g.right, g.bottom };
    DrawTextW(ctx->wmDC, SYMBOLS[ctx->symIdx].api, -1, &rcSym,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    // The interval below the main line, in the usual small font.
    SelectObject(ctx->wmDC, ctx->hFontSmall);
    // The distance down to the interval follows the font height, otherwise the
    // text would sit inside the main line on large panels.
    RECT rcIv = { g.left, g.top + (g.ch / 2) + fh / 2 + 4, g.right, g.bottom };
    DrawTextW(ctx->wmDC, INTERVALS[ctx->ivIdx].label, -1, &rcIv,
              DT_CENTER | DT_SINGLELINE | DT_TOP);
    SelectObject(ctx->wmDC, prev);

    ctx->wmW = W; ctx->wmH = H;
    ctx->wmSym = ctx->symIdx; ctx->wmIv = ctx->ivIdx;
    ctx->wmValid = TRUE;
}

// The stamp font for desktop mode. Built only when the height changes -
// same pattern as hFontWm. A mode switch keeps H for the surface, so this
// is not a per-frame cost.
static void EnsurePillFont(AppContext* ctx, int H) {
    int fh = DeskPillFontH(H);
    if (ctx->hFontPill && ctx->pillFontH == fh) return;
    if (ctx->hFontPill) DeleteObject(ctx->hFontPill);
    ctx->hFontPill = CreateFontW(-fh, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN,
                                 L"Lucida Console");
    ctx->pillFontH = ctx->hFontPill ? fh : 0;
}

// The text in a tag on the price axis (phase 23): two decimals where they
// fit, otherwise the axis resolution - same rule as the stamp for the last
// price, measured on the fully formatted string (bug #5). The font must be
// selected into hdc before the call.
static void FormatTagPrice(HDC hdc, double p, double range, int avail,
                           wchar_t* out, size_t cch) {
    SIZE sz = { 0, 0 };
    swprintf_s(out, cch, L"%.2f", p);
    if (GetTextExtentPoint32W(hdc, out, (int)wcslen(out), &sz) && sz.cx > avail) {
        swprintf_s(out, cch, L"%.*f", PriceDecimals(range / 4.0), p);
    }
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
static BOOL DrawIndicator(HDC hdc, const AppContext* ctx, const ChartRect* g,
                          int period, BOOL ema, COLORREF clr,
                          double dStart, double slot, int i0, int i1,
                          double maxP, double range,
                          int legendIdx, double* legendVal) {
    int n = ctx->candleCount;
    int first = (i0 > 0) ? i0 - 1 : 0;
    int last  = (i1 < n) ? i1 : n - 1;
    double yLo = -16.0 * (double)g->ch, yHi = 17.0 * (double)g->ch;

    IndState s;
    IndInit(&s, period, ema);
    BOOL haveLegend = FALSE;
    int k = 0;
    SetDCPenColor(hdc, clr);
    for (int i = IndFeedStart(first, period, ema); i <= last; ++i) {
        if (!IndStep(&s, ctx->candles, i)) continue;
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
static BOOL DrawVwap(HDC hdc, const AppContext* ctx, const ChartRect* g, COLORREF clr,
                     double dStart, double slot, int i0, int i1,
                     double maxP, double range,
                     int legendIdx, double* legendVal) {
    int n = ctx->candleCount;
    const Candle* c = ctx->candles;
    int first = (i0 > 0) ? i0 - 1 : 0;
    int last  = (i1 < n) ? i1 : n - 1;
    double yLo = -16.0 * (double)g->ch, yHi = 17.0 * (double)g->ch;

    BOOL live = FALSE;
    int s = SessionStartAt(c, n, first, ctx->intervalMs, ctx->histDone, &live);
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

// A dashed horizontal line over [x0, x1) on row y (phase 27: today's high and
// low). Not a PS_DASH pen: the line fades with dispIndF, and a pen cannot
// change color per frame without being recreated. The dashes go to
// PolyPolyline in batches, with DC_PEN - no new GDI objects. The pattern is
// anchored at anchor (the surface's left edge), not at x0, so the dashes stand
// still when the session start slides during panning. GDI does not draw the
// end point, so [a, b) is exactly dashOn pixels. The period is shared
// (SESS_DASH_PERIOD); dashOn separates today's lines from yesterday's (phase 28).
static void DrawDashLine(HDC hdc, int x0, int x1, int y, int anchor, int dashOn) {
    if (x0 < anchor) x0 = anchor;
    int x = anchor + ((x0 - anchor) / SESS_DASH_PERIOD) * SESS_DASH_PERIOD;
    int k = 0;
    for (; x < x1; x += SESS_DASH_PERIOD) {
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

static void DrawChart(AppContext* ctx, HDC hdc, int W, int H) {
    RECT rcAll = { 0, 0, W, H };
    // The watermark sits IN the background, before grid, candles and axes -
    // the chart floats cleanly over the text. BitBlt REPLACES FillRect, it
    // does not come in addition.
    EnsureWatermark(ctx, hdc, W, H);
    if (ctx->wmValid) {
        BitBlt(hdc, 0, 0, W, H, ctx->wmDC, 0, 0, SRCCOPY);
    } else {
        FillRect(hdc, &rcAll, ctx->brBg);   // fallback, the watermark is decoration
    }

    SetBkMode(hdc, TRANSPARENT);

    // Called under the lock, so the health fields can be read directly.
    ULONGLONG nowTick = GetTickCount64();
    BOOL stale = (ctx->lastOkTick != 0) &&
                 (nowTick - ctx->lastOkTick > STALE_AFTER);
    int staleSecs = stale ? (int)((nowTick - ctx->lastOkTick) / 1000) : 0;

    int n = ctx->candleCount;
    if (n <= 0) {
        // Desktop mode: no status on the wallpaper. The surface shows
        // background and watermark until there are candles to draw. The
        // message would be the only text left, and it says nothing a user
        // who cannot click on the surface can do anything about.
        if (g_desktopMode) return;

        wchar_t msg[96];
        SelectObject(hdc, ctx->hFontSmall);
        SetTextColor(hdc, CLR_DIM);

        // Without a connection it used to say "Loading data from Binance..."
        // forever. The message lied about the state - now it says what is
        // actually happening, and when we retry.
        if (ctx->netFailures > 0) {
            ULONGLONG nx = ctx->nextRetryTick;
            int in_s = (nx > nowTick) ? (int)((nx - nowTick + 999) / 1000) : 0;
            swprintf_s(msg, 96, L"No connection - retrying in %ds", in_s);
        } else {
            wcscpy_s(msg, 96, L"Loading data from Binance...");
        }
        DrawTextW(hdc, msg, -1, &rcAll, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        return;
    }

    int vs, vc;
    GetView(ctx, &vs, &vc);
    if (vc <= 0) return;

    // --- Header: price + change over the VISIBLE view ---
    double first = ctx->candles[vs].open;
    double last  = ctx->candles[vs + vc - 1].close;
    double chg   = (first > 0.0) ? ((last - first) / first) * 100.0 : 0.0;
    COLORREF chgClr = (chg >= 0.0) ? CLR_UP : CLR_DOWN;

    wchar_t span[24];
    FormatSpan(vc, ctx->intervalMs, span, 24);

    wchar_t buf[64];
    ChartRect g = ChartGeometry(W, H);

    // --- Header layout, measured ---
    // Previously the price and the percentage shared ONE rectangle, left- and
    // right-aligned. On a narrow panel they then meet in the middle and are
    // drawn on top of each other - DrawTextW clips to the rectangle, not to
    // the neighboring text. Now each text is measured on the fully formatted
    // string in its own font (bug #5), and row by row the right bound of the
    // left-aligned text is compared with the left bound of the right-aligned.
    //
    // Row 1 (y 10-30): price on the left, percentage and button row on the
    // right. Row 2 (y 28-42): the symbol line on the left. On the right there
    // are not the buttons (they end at y = 24), but the price axis's top
    // label, which sits at y = top +- 8 from x = right + AXIS_LBL_GAP.
    // The metadata overlay (phase 14). Price, percentage and symbol line are
    // the layer that is read foveally: the user has to stop and decode
    // numbers. On the desktop they compete with icons and folders, and the
    // surface is meant to be read peripherally. The whole block is therefore
    // idle in desktop mode.
    if (!g_desktopMode) {
        RECT strip;
        ButtonStrip(W, &strip);
        int btnLeft = strip.left;          // X_left_bound for the button row

        // Row 1, left: the price. The rectangle ends at the button row, so even
        // a price that does not fit is never drawn under the buttons.
        SelectObject(hdc, ctx->hFontBig);
        SetTextColor(hdc, stale ? CLR_DIM : CLR_TEXT);
        swprintf_s(buf, 64, L"$%.2f", last);
        int lenPrice = (int)wcslen(buf);
        SIZE szPrice = { 0, 0 };
        GetTextExtentPoint32W(hdc, buf, lenPrice, &szPrice);
        int priceRight = PAD_L + szPrice.cx;   // X_right_bound
        RECT rcPrice = { PAD_L, 10, btnLeft - HDR_GAP, 30 };
        DrawTextW(hdc, buf, lenPrice, &rcPrice, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        // Row 1, right: the percentage in three steps. Full with span, then
        // without span, then hidden. Never clipped in the middle of a number -
        // "+0.5" where it says "+0.50%" is a wrong value, not a shorter one.
        SelectObject(hdc, ctx->hFontSmall);
        int pctRight = btnLeft - HDR_GAP;
        wchar_t pctFull[48], pctShort[24];
        swprintf_s(pctFull,  48, L"%+.2f%%  (%s)", chg, span);
        swprintf_s(pctShort, 24, L"%+.2f%%", chg);
        int lenFull = (int)wcslen(pctFull), lenShort = (int)wcslen(pctShort);
        SIZE szFull = { 0, 0 }, szShort = { 0, 0 };
        GetTextExtentPoint32W(hdc, pctFull, lenFull, &szFull);

        // The short form is measured only when the full one did not fit. Each
        // GetTextExtentPoint32W is ~20 us, and above the minimum width the full
        // one fits with a good margin - measured at 400 px: ~115 px of air to
        // the price.
        const wchar_t* pct = NULL;
        int lenPct = 0;
        if (HeaderFits(priceRight, pctRight - szFull.cx)) {
            pct = pctFull;  lenPct = lenFull;
        } else {
            GetTextExtentPoint32W(hdc, pctShort, lenShort, &szShort);
            if (HeaderFits(priceRight, pctRight - szShort.cx)) {
                pct = pctShort; lenPct = lenShort;
            }
        }
        if (pct) {
            SetTextColor(hdc, chgClr);
            RECT rcPct = { priceRight + HDR_GAP, 10, pctRight, 30 };
            DrawTextW(hdc, pct, lenPct, &rcPct, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        }

        // Row 2: the toolbar (phase 22) sits where the symbol line used to,
        // and carries symbol and interval itself. It is drawn from PaintPopup
        // - also when the buffer is empty. All that is left here is the
        // offline text, which reads the health fields under the lock: to the
        // right of the last pill, and only when the WHOLE text fits before the
        // price axis's label. Same rule as the percentage: a truncated seconds
        // count is a wrong number. Dimmed price, the tray tip and the icon
        // carry the state at any width.
        if (stale) {
            RECT tb[TBAR_COUNT];
            int tbN = ToolbarLayout(W, tb);
            int subLeft  = (tbN > 0) ? tb[tbN - 1].right + HDR_GAP : PAD_L;
            int subLimit = HeaderRow2Limit(W);
            swprintf_s(buf, 64, L"offline %ds", staleSecs);
            int lenSub = (int)wcslen(buf);
            SIZE szSub = { 0, 0 };
            GetTextExtentPoint32W(hdc, buf, lenSub, &szSub);
            if (subLeft + szSub.cx <= subLimit) {
                SetTextColor(hdc, CLR_DIM);
                RECT rcSub = { subLeft, TBAR_TOP, subLimit, TBAR_TOP + TBAR_H };
                DrawTextW(hdc, buf, lenSub, &rcSub, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            }
        }
    }

    // --- Chart geometry ---
    int left = g.left, top = g.top, right = g.right, bottom = g.bottom;
    int cw = g.cw, ch = g.ch;
    int edge = g.edge;   // the axis edge; right is where the candles end
    if (cw <= 0 || ch <= 0) return;

    // Called under the lock, so frontShift can be read directly.
    ApplyFrontShift(ctx);
    if (!ctx->dispValid) SyncDisp(ctx);

    // The drawing reads the DISPLAY. The target (vs, vc) is used only for the
    // span text in the header and to work out what the display should ease
    // TOWARDS - and the latter happens in WM_TIMER, not here.
    double dStart = ctx->dispStart;
    double dCount = ctx->dispCount;
    if (dCount < 1.0) dCount = 1.0;

    double minP = ctx->dispMin, maxP = ctx->dispMax;
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
    // [top, bottom) clip would erase the bottom line.
    RECT rcChart = { left, top, edge, bottom + 1 };
    IntersectClipRect(hdc, rcChart.left, rcChart.top, rcChart.right, rcChart.bottom);

    // --- Grid ---
    // Edge to edge puts line i = 0 at y = 0 and i = 4 at y = H - 1. That is a
    // 1 px frame around the whole screen - the very interference desktop mode
    // is supposed to be free of. The three inner lines carry the spatial
    // frame of reference alone.
    HPEN hOldPen = (HPEN)SelectObject(hdc, ctx->penGrid);
    int gi0 = g_desktopMode ? 1 : 0, gi1 = g_desktopMode ? 3 : 4;
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
    if (bodyW > 18) bodyW = 18;   // prevents chunky candles at full zoom-in

    // The loop still runs over VISIBLE candles, not over the whole history:
    // i1 - i0 is dCount + 1 rounded. The performance characteristics from
    // phase 1 stand.
    int i0 = (int)floor(dStart);
    int i1 = (int)ceil(dStart + dCount);
    if (i0 < 0) i0 = 0;
    if (i1 > n) i1 = n;

    // --- Volume bars (phase 21) ---
    // Behind the candles, in the bottom VOL_FRAC of the surface, inside the
    // same clip. The scale is dispVolMax - the DISPLAY, which is eased in
    // WM_TIMER - not the target, otherwise the bars jump while the candles
    // glide. The direction is the candle's own (close vs open), the same rule
    // as the candle color and a different one from the last-price line's
    // (see there). The bottom row is y = bottom, inclusive, as for wicks and
    // grid line 4 (pitfall 33); FillRect is exclusive at the bottom, hence
    // bottom + 1. No geometry or hit-test code is touched: the bars are an
    // overlay in the candles' own surface.
    //
    // One PolyPolygon per color and batch of VOL_BATCH bars, with NULL_PEN:
    // the polygon fill leaves out the right and bottom edges like Rectangle,
    // so the corners [x0, x1) x [bottom + 1 - h, bottom + 1) fill exactly the
    // same pixels FillRect would have.
    //
    // WINDING, not ALTERNATE: with more candles than pixels (vc > cw) slot is
    // below 1, bodyW is clamped to 1, and neighboring candles land on the
    // same cx. Two identical rectangles in the same batch CANCEL each other
    // under ALTERNATE (even/odd), so the bar disappears. FillRect overdrew;
    // polygon fill counts edges. With WINDING and the same winding direction
    // on all the rectangles they add up, and the union - the tallest -
    // remains. Measured in the probe: two identical rectangles give 0 pixels
    // under ALTERNATE and w x h under WINDING.
    //
    // dispVolF (phase 22) is the VOL toggle's display, 0..1: the bars sink
    // into the bottom when switched off, and rise again. At 1.0 the factor
    // is exact, so the pixels are the same as before the toggle existed.
    if (ctx->dispVolMax > 0.0 && ctx->dispVolF > 0.0) {
        int bandH = (int)((double)ch * VOL_FRAC);
        int oldFill = SetPolyFillMode(hdc, WINDING);
        HGDIOBJ oldPenV = SelectObject(hdc, GetStockObject(NULL_PEN));
        for (int pass = 0; pass < 2; ++pass) {          // 0 = up, 1 = down
            SelectObject(hdc, pass == 0 ? ctx->brVolUp : ctx->brVolDown);
            int k = 0;
            for (int i = i0; i < i1; ++i) {
                const Candle* c = &ctx->candles[i];
                if ((c->close >= c->open) != (pass == 0)) continue;
                int h = (int)(c->volume / ctx->dispVolMax * (double)bandH * ctx->dispVolF + 0.5);
                if (h <= 0) continue;
                if (h > bandH) h = bandH;   // mid-easing a candle can lie above the scale
                int cx = left + (int)(((double)i - dStart + 0.5) * slot);
                int x0 = cx - bodyW / 2, x1 = x0 + bodyW;
                int y0 = bottom + 1 - h, y1 = bottom + 1;
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

    // --- Alert lines (phase 23) ---
    // Behind the candles and above the bars, inside the same clip, from left
    // to edge like the grid: a level is a reference, and the candles are what
    // is read. Muted amber (CLR_ALERT_LINE), solid - dashed is the last price,
    // and dotted is the crosshair. DC_PEN, so no new GDI objects. Drawn in
    // both modes: on the desktop the line is a spatial reference like the
    // grid, while the tag with the number exists only in the panel (phase 14).
    // Outside [top, bottom] nothing is drawn, same rule as the last price.
    {
        int na = ctx->alertCount[ctx->symIdx];
        if (na > 0) {
            SelectObject(hdc, GetStockObject(DC_PEN));
            SetDCPenColor(hdc, CLR_ALERT_LINE);
            for (int a = 0; a < na; ++a) {
                int y = AlertY(ctx, &g, fabs(ctx->alerts[ctx->symIdx][a]));
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
    int    indT = (int)(ctx->dispIndF * 255.0 + 0.5);
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
        int  sessS = SessionStart(ctx->candles, n, ctx->intervalMs, ctx->histDone, &sessFull);
        int  xs = (sessS >= 0) ? left + (int)floor(((double)sessS - dStart) * slot) : edge;
        if (sessS >= 0 && sessFull && xs < edge) {
            SessionHiLo(ctx->candles, sessS, n, &lvlP[0], &lvlP[1]);
            lvlOn[0] = lvlOn[1] = TRUE;
#ifdef TICKER_PROBE
            QueryPerformanceCounter(&prevQ0);
#endif
            int  prevE = -1;
            BOOL prevFull = FALSE;
            int  prevS = PrevSession(ctx->candles, n, ctx->intervalMs, ctx->histDone, &prevE, &prevFull);
            if (prevS >= 0 && prevFull) {
                lvlP[2] = ctx->candles[prevE - 1].close;
                SessionHiLo(ctx->candles, prevS, prevE, &lvlP[3], &lvlP[4]);
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
                SetDCPenColor(hdc, Blend(CLR_BG, (q < 2) ? CLR_SESSION : CLR_PREV, indT));
                DrawDashLine(hdc, xs, edge, y, left, LVL_DASH[q]);
                yLvl[q] = y;
                yLine[q] = y;
            }
        }
    }
#ifdef TICKER_PROBE
    QueryPerformanceCounter(&sessQ1);
    QueryPerformanceFrequency(&sessQf);
    g_probeSessUs = (sessQ1.QuadPart - sessQ0.QuadPart) * 1000000LL / sessQf.QuadPart;
    // Field 56: the computation of yesterday (binary search + one pass over
    // the day). The three lines are in field 45 together with today's.
    g_probePrevUs = (prevQ1.QuadPart - prevQ0.QuadPart) * 1000000LL / sessQf.QuadPart;
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
        Candle* c = &ctx->candles[i];
        int up = (c->close >= c->open);

        int cx     = left + (int)(((double)i - dStart + 0.5) * slot);
        int yHigh  = top + (int)(((maxP - c->high)  / range) * ch);
        int yLow   = top + (int)(((maxP - c->low)   / range) * ch);
        int yOpen  = top + (int)(((maxP - c->open)  / range) * ch);
        int yClose = top + (int)(((maxP - c->close) / range) * ch);

        if (up != curUp) {
            SetDCPenColor(hdc,   up ? CLR_UP : CLR_DOWN);
            SetDCBrushColor(hdc, up ? CLR_UP : CLR_DOWN);
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
    if (indT <= 0) g_probeIndUs = 0;
#endif
    if (indT > 0) {
        int legendIdx = i1 - 1;
        if (ctx->hoverIdx >= 0 && ctx->hoverIdx < n) {
            double hr = (double)ctx->hoverIdx - dStart;
            if (hr >= 0.0 && hr < dCount) legendIdx = ctx->hoverIdx;
        }
        SelectObject(hdc, GetStockObject(DC_PEN));
        indOk[0] = DrawIndicator(hdc, ctx, &g, IND_SMA_PERIOD, FALSE,
                                 Blend(CLR_BG, CLR_SMA, indT), dStart, slot, i0, i1,
                                 maxP, range, legendIdx, &indVal[0]);
        indOk[1] = DrawIndicator(hdc, ctx, &g, IND_EMA_PERIOD, TRUE,
                                 Blend(CLR_BG, CLR_EMA, indT), dStart, slot, i0, i1,
                                 maxP, range, legendIdx, &indVal[1]);
#ifdef TICKER_PROBE
        {
            LARGE_INTEGER indQ1, indQf;
            QueryPerformanceCounter(&indQ1);
            QueryPerformanceFrequency(&indQf);
            g_probeIndUs = (indQ1.QuadPart - indQ0.QuadPart) * 1000000LL / indQf.QuadPart;
            QueryPerformanceCounter(&sessQ0);
        }
#endif
        indOk[2] = DrawVwap(hdc, ctx, &g, Blend(CLR_BG, CLR_VWAP, indT),
                            dStart, slot, i0, i1, maxP, range, legendIdx, &indVal[2]);
#ifdef TICKER_PROBE
        QueryPerformanceCounter(&sessQ1);
        g_probeSessUs += (sessQ1.QuadPart - sessQ0.QuadPart) * 1000000LL / sessQf.QuadPart;
#endif
    }

    // The clipping MUST be removed for the axis texts - they lie in the
    // margin on the right.
    SelectClipRgn(hdc, NULL);

    SelectObject(hdc, GetStockObject(BLACK_PEN));
    SelectObject(hdc, GetStockObject(NULL_BRUSH));

    // --- Price labels ---
    // A separate loop after the clipping, not in the grid loop: there they
    // would have been clipped away along with everything else outside rcChart.
    // Omega_y-axis: x in [edge + AXIS_LBL_GAP, W - AXIS_PAD_R).
    int axL = edge + AXIS_LBL_GAP, axR = W - AXIS_PAD_R;
    SelectObject(hdc, ctx->hFontAxis);
    SetTextColor(hdc, CLR_AXIS);
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
    if (!g_desktopMode) {
        {
            double lp = ctx->candles[n - 1].close;
            int yl = top + (int)(((maxP - lp) / range) * ch);
            if (yl >= top && yl <= bottom) yPill = yl;
        }
        // The alert tags (phase 23) and the ghost tag under the pointer lie in
        // the same column and are equally tall, so they get the same collision
        // rule as the stamp: a label less than 16 px away is not drawn.
        int yTag[ALERT_MAX + 2], nTag = 0;
        int sA = ctx->symIdx, nA = ctx->alertCount[sA];
        // The crosshair's axis tag (phase 29) is the only tag that moves with
        // the hand, and it had no collision rule: when the pointer was 1-15 px
        // from a label, a strip of the number stuck out beneath it. The rank
        // is the stamp, then the crosshair tag, then the rest. Same visibility
        // test and same clamping as the crosshair block at the bottom; within
        // 16 px of the stamp the crosshair tag is not drawn (the last price is
        // the number that must never be cut), and then nothing yields to it
        // either.
        if (ctx->hoverIdx >= 0 && ctx->hoverIdx < n) {
            double hrelT = (double)ctx->hoverIdx - dStart;
            if (hrelT >= 0.0 && hrelT < dCount) {
                int hyT = ctx->hoverY;
                if (hyT < top) hyT = top;
                if (hyT > bottom) hyT = bottom;
                if (yPill == INT_MIN || abs(hyT - yPill) >= 16) yCross = hyT;
            }
        }
        if (yCross != INT_MIN) yTag[nTag++] = yCross;
        for (int a = 0; a < nA; ++a) {
            int y = AlertY(ctx, &g, fabs(ctx->alerts[sA][a]));
            if (y >= top && y <= bottom) yTag[nTag++] = y;
        }
        BOOL ghost = (ctx->axisHotY >= top && ctx->axisHotY <= bottom &&
                      (ctx->alertHot < 0 || ctx->alertHot >= nA));
        if (ghost) yTag[nTag++] = ctx->axisHotY;

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
            BOOL hide = (yPill != INT_MIN && abs(yLvl[q] - yPill) < 16);
            for (int t = 0; t < nTag && !hide; ++t) if (abs(yLvl[q] - yTag[t]) < 16) hide = TRUE;
            for (int p = 0; p < q && !hide; ++p)
                if (yLvl[p] != INT_MIN && abs(yLvl[q] - yLvl[p]) < 16) hide = TRUE;
            if (hide) yLvl[q] = INT_MIN;
        }

        for (int i = 0; i <= 4; ++i) {
            int y = top + (ch * i) / 4;
            if (yPill != INT_MIN && abs(y - yPill) < 16) continue;
            BOOL hidden = FALSE;
            for (int t = 0; t < nTag; ++t) if (abs(y - yTag[t]) < 16) hidden = TRUE;
            for (int q = 0; q < LVL_COUNT; ++q) if (yLvl[q] != INT_MIN && abs(y - yLvl[q]) < 16) hidden = TRUE;
            if (hidden) continue;
            double p = maxP - (range * i) / 4.0;
            swprintf_s(buf, 64, L"%.*f", PriceDecimals(range / 4.0), p);
            RECT rcLbl = { axL, y - 8, axR, y + 8 };
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
            double lvl = fabs(ctx->alerts[sA][a]);
            int y = AlertY(ctx, &g, lvl);
            if (y < top || y > bottom) continue;
            BOOL hot = (a == ctx->alertHot && ctx->alerts[sA][a] != ctx->alertFresh);
            RECT rcA = { edge + 1, y - 8, axR + 3, y + 8 };
            SetDCBrushColor(hdc, hot ? CLR_CLOSEHOT : CLR_ALERT);
            FillRect(hdc, &rcA, (HBRUSH)GetStockObject(DC_BRUSH));
            // If the stamp or a tag drawn later lies on top of this one, only
            // a strip of the surface sticks out - and with it a number cut
            // lengthwise. Same rule as the labels: a clipped number is worse
            // than no number (seen in PrintWindow: "81034.00" halfway under
            // the stamp). The surface is drawn, the text is not.
            BOOL covered = (yPill != INT_MIN && abs(y - yPill) < 16) ||
                           (yCross != INT_MIN && abs(y - yCross) < 16);
            for (int b = a + 1; b < nA && !covered; ++b) {
                int yb = AlertY(ctx, &g, fabs(ctx->alerts[sA][b]));
                if (yb >= top && yb <= bottom && abs(y - yb) < 16) covered = TRUE;
            }
            if (covered) continue;
            FormatTagPrice(hdc, lvl, range, axR - axL, buf, 64);
            SetTextColor(hdc, hot ? CLR_BTNHOT : CLR_BG);
            RECT rcAT = { axL, y - 8, axR, y + 8 };
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
            RECT rcS = { edge + 1, y - 8, axR + 3, y + 8 };
            SetDCBrushColor(hdc, Blend(CLR_BG, CLR_BOX, indT));
            FillRect(hdc, &rcS, (HBRUSH)GetStockObject(DC_BRUSH));
            FormatTagPrice(hdc, lvlP[q], range, axR - axL, buf, 64);
            SetTextColor(hdc, Blend(CLR_BG, (q < 2) ? CLR_SESSION : CLR_PREV, indT));
            RECT rcST = { axL, y - 8, axR, y + 8 };
            DrawTextW(hdc, buf, -1, &rcST, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }

        // The ghost tag: the pointer is in the price column on an empty spot,
        // and a click SETS an alert here. Framed, not filled, with the line
        // across the chart, so the user sees which candles the level cuts
        // before the click. If all slots are used, it is gray, and the click
        // does nothing. The price is the ROUNDED one - the one actually set.
        if (ghost) {
            int y = ctx->axisHotY;
            BOOL full = (nA >= ALERT_MAX);
            COLORREF gc = full ? CLR_DIM : CLR_ALERT;
            SelectObject(hdc, GetStockObject(DC_PEN));
            SetDCPenColor(hdc, full ? CLR_CROSS : CLR_ALERT_LINE);
            MoveToEx(hdc, left, y, NULL);
            LineTo(hdc, edge, y);
            RECT rcG = { edge + 1, y - 8, axR + 3, y + 8 };
            FillRect(hdc, &rcG, ctx->brBox);
            SetDCBrushColor(hdc, gc);
            FrameRect(hdc, &rcG, (HBRUSH)GetStockObject(DC_BRUSH));
            FormatTagPrice(hdc, AlertPriceAtY(ctx, &g, y), range, axR - axL, buf, 64);
            SetTextColor(hdc, gc);
            RECT rcGT = { axL, y - 8, axR, y + 8 };
            DrawTextW(hdc, buf, -1, &rcGT, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }
        SetTextColor(hdc, CLR_AXIS);   // the time axis below inherits the color
    }

    // --- Afterglow (phase 23) ---
    // An alert that has fired is REMOVED; left behind is a line in full amber
    // that fades out over a couple of seconds (alertFlashF, 1..0, eased by the
    // clock), so whoever looks at the panel sees WHERE it went off. Blended
    // against CLR_BG like everything else here - opaque over the background
    // is identical to alpha. Over the candles, not behind: this is a signal,
    // not a reference. Both modes.
    if (ctx->alertFlashF > 0.0) {
        int y = AlertY(ctx, &g, ctx->alertFlashLevel);
        if (y >= top && y <= bottom) {
            int t = (int)(ctx->alertFlashF * 255.0 + 0.5);
            SelectObject(hdc, GetStockObject(DC_PEN));
            SetDCPenColor(hdc, Blend(CLR_BG, CLR_ALERT, t));
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
    if (i0 < i1 && !g_desktopMode) {
        wchar_t tl[24];
        FormatCandleTime(ctx->candles[i0].openTime,
                         ctx->intervalMs, tl, 24);
        int tlLen = (int)wcslen(tl);
        SIZE tsz = { 0, 0 };
        GetTextExtentPoint32W(hdc, tl, tlLen, &tsz);   // the axis font is selected
        int minDx = tsz.cx + TIME_LBL_GAP;
        if (minDx < TIME_DX_MIN) minDx = TIME_DX_MIN;
        long long iv = (ctx->intervalMs > 0) ? ctx->intervalMs : 60000LL;
        int step = NiceTimeStep(TimeTickStep(dCount, cw, minDx), iv);

        // Anchored in LOCAL time, so 6 h steps land on 00, 06, 12 and 18
        // here and not on 02, 08 ... (UTC + 2 in summer). The offset is read
        // at i0; a DST change in the middle of the view only moves the
        // labels one hour.
        long long t0 = ctx->candles[i0].openTime, tzMs = 0;
        {
            ULONGLONG ft = (ULONGLONG)(t0 / 1000) * 10000000ULL + 116444736000000000ULL;
            FILETIME fu, fl;
            fu.dwLowDateTime  = (DWORD)(ft & 0xFFFFFFFFULL);
            fu.dwHighDateTime = (DWORD)(ft >> 32);
            if (FileTimeToLocalFileTime(&fu, &fl)) {
                ULONGLONG lt = ((ULONGLONG)fl.dwHighDateTime << 32) | fl.dwLowDateTime;
                tzMs = ((long long)lt - (long long)ft) / 10000LL;
            }
        }
        long long slotNo = (t0 + tzMs) / iv;
        int rem = (int)(slotNo % step);
        int k = i0 + ((rem == 0) ? 0 : (step - rem));

        UINT oldAlign = SetTextAlign(hdc, TA_CENTER | TA_TOP);
        for (; k < i1; k += step) {
            int x = left + (int)(((double)k - dStart + 0.5) * slot);
            if (x - tsz.cx / 2 < left || x + (tsz.cx + 1) / 2 > right) continue;
            FormatCandleTime(ctx->candles[k].openTime, ctx->intervalMs, tl, 24);
            ExtTextOutW(hdc, x, bottom + 2, 0, NULL, tl, (int)wcslen(tl), NULL);
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
    if (!(indT > 0 && !g_desktopMode)) g_probeLblMask = 0;
#endif
    if (indT > 0 && !g_desktopMode) {
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
        int lx = left + 6, ly = top + 4;
        if (lx + szAll.cx <= right - 6 && ly + szAll.cy <= bottom) {
            // Today's high (phase 27) lies 8 % below the surface's top when
            // today's top is the view's, and on a short panel that is in the
            // middle of this row: the dashes ran right through the digits
            // (seen in a capture at 560x300). Then - and only then - the text
            // gets an opaque background, so the numbers stay whole and the
            // line continues behind them.
            BOOL struck = FALSE;
            for (int q = 0; q < LVL_COUNT; ++q)
                if (yLine[q] != INT_MIN && yLine[q] >= ly - 1 && yLine[q] <= ly + szAll.cy) struck = TRUE;
            if (struck) { SetBkColor(hdc, CLR_BG); SetBkMode(hdc, OPAQUE); }
            SetTextColor(hdc, Blend(CLR_BG, CLR_SMA, indT));
            ExtTextOutW(hdc, lx, ly, 0, NULL, lg, len1, NULL);
            SetTextColor(hdc, Blend(CLR_BG, CLR_EMA, indT));
            ExtTextOutW(hdc, lx + sz1.cx, ly, 0, NULL, lg + len1, lenAll - len1, NULL);
            if (lx + sz3.cx <= right - 6) {
                SetTextColor(hdc, Blend(CLR_BG, CLR_VWAP, indT));
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
        g_probeLblMask = 0;
#endif
        if (right - left >= 200) {
            RECT placed[LVL_COUNT + 1];
            int  nPlaced = 0;
            if (rcLegend.right > rcLegend.left) placed[nPlaced++] = rcLegend;
            for (int q = 0; q < LVL_COUNT; ++q) {
                if (yLine[q] == INT_MIN) continue;
                SIZE szN = { 0, 0 };
                GetTextExtentPoint32W(hdc, LVL_NAME[q], 3, &szN);
                RECT rcN = { lvlXs + 4, yLine[q] - 2 - szN.cy, lvlXs + 4 + szN.cx, yLine[q] - 2 };
                if (rcN.top < top) { rcN.top = yLine[q] + 3; rcN.bottom = rcN.top + szN.cy; }
                if (rcN.bottom > bottom || rcN.right > right) continue;
                BOOL hit = FALSE;
                for (int t = 0; t < nPlaced && !hit; ++t) {
                    RECT tmp;
                    if (IntersectRect(&tmp, &rcN, &placed[t])) hit = TRUE;
                }
                if (hit) continue;
                placed[nPlaced++] = rcN;
                SetTextColor(hdc, Blend(CLR_BG, (q < 2) ? CLR_SESSION : CLR_PREV, indT));
                ExtTextOutW(hdc, rcN.left, rcN.top, 0, NULL, LVL_NAME[q], 3, NULL);
#ifdef TICKER_PROBE
                g_probeLblMask |= (1 << q);
#endif
            }
        }
#ifdef TICKER_PROBE
        QueryPerformanceCounter(&lblQ1);
        QueryPerformanceFrequency(&lblQf);
        g_probeLblUs = (lblQ1.QuadPart - lblQ0.QuadPart) * 1000000LL / lblQf.QuadPart;
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
        const Candle* lastC = &ctx->candles[n - 1];
        double lastP = lastC->close;
        double prevP = (n >= 2) ? ctx->candles[n - 2].close : lastC->open;
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
            HPEN penLast = lastUp ? ctx->penLastUp : ctx->penLastDown;
            HPEN hOld2 = (HPEN)SelectObject(hdc, penLast);
            MoveToEx(hdc, xLast, yLast, NULL);
            LineTo(hdc, right, yLast);

            SelectObject(hdc, GetStockObject(DC_PEN));
            SetDCPenColor(hdc, lastUp ? CLR_UP : CLR_DOWN);
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
                int half = g_desktopMode ? DeskPillH(H) / 2 : 8;
                if (g_desktopMode) EnsurePillFont(ctx, H);

                RECT rcPill = { edge + 1, yLast - half, axR + 3, yLast + half };
                SetDCBrushColor(hdc, lastUp ? CLR_UP : CLR_DOWN);
                FillRect(hdc, &rcPill, (HBRUSH)GetStockObject(DC_BRUSH));

                // "Precise value text": two decimals where they fit, otherwise
                // the same resolution as the axis. The width is measured on
                // the fully formatted string - bug #5 again.
                HFONT fPill = (g_desktopMode && ctx->hFontPill) ? ctx->hFontPill
                                                                : ctx->hFontAxis;
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
                SetTextColor(hdc, CLR_BG);
                RECT rcPillTxt = { axL, yLast - half, axR, yLast + half };
                DrawTextW(hdc, buf, -1, &rcPillTxt, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            }
        }
    }

    // --- Crosshair + hover box ---
    // Bound to the VISIBLE surface, not to the target view - the two come
    // apart in the middle of an animation.
#ifdef TICKER_PROBE
    g_probeCrossTag = 0;
#endif
    if (ctx->hoverIdx < 0 || ctx->hoverIdx >= n) return;
    double hrel = (double)ctx->hoverIdx - dStart;
    if (hrel < 0.0 || hrel >= dCount) return;

    const Candle* hc = &ctx->candles[ctx->hoverIdx];
    int hx = left + (int)((hrel + 0.5) * slot);
    int hy = ctx->hoverY;
    if (hy < top) hy = top;
    if (hy > bottom) hy = bottom;

    HPEN hPrev = (HPEN)SelectObject(hdc, ctx->penCross);
    MoveToEx(hdc, hx, top, NULL);      LineTo(hdc, hx, bottom);
    // Same bridge as the last-price line: the horizontal reaches the axis,
    // otherwise there would be a gap between the cross and its label.
    MoveToEx(hdc, left, hy, NULL);     LineTo(hdc, edge, hy);
    SelectObject(hdc, hPrev);

    // Price label on the right axis where the pointer is. Not when it would
    // partly cover the stamp (phase 29, see yCross above): the line is
    // drawn, the tag is not.
    if (yCross != INT_MIN) {
        double hp = maxP - ((double)(hy - top) / (double)ch) * range;
        swprintf_s(buf, 64, L"%.*f", PriceDecimals(range / 4.0), hp);
        RECT rcTag = { edge + 1, hy - 8, axR + 3, hy + 8 };
        FillRect(hdc, &rcTag, ctx->brBoxEdge);
        SelectObject(hdc, ctx->hFontAxis);
        SetTextColor(hdc, CLR_TEXT);
        RECT rcTagTxt = { axL, hy - 8, axR, hy + 8 };
        DrawTextW(hdc, buf, -1, &rcTagTxt, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
#ifdef TICKER_PROBE
    g_probeCrossTag = (yCross != INT_MIN);
#endif

    // Hover box with time + OHLC. Back to the normal small font:
    // LINE_H = 13 is measured on it, and the axis font is 15 px tall.
    SelectObject(hdc, ctx->hFontSmall);
    wchar_t tbuf[24];
    FormatCandleTime(hc->openTime, ctx->intervalMs, tbuf, 24);

    // BOX_H: 4 px top + time row + O/H/L/C/V (phase 21) = 4 + 6 * 13 + 5.
    // With the indicators on (phase 27) three more rows are added: SMA, EMA
    // and VWAP at the candle under the crosshair - the same numbers the
    // legend shows, since legendIdx IS hoverIdx when this block runs (same
    // condition). The rows stay as long as the lines are visible (indT > 0)
    // and fade with them, in the lines' own colors; an average that is not
    // defined at the candle gets a dash, as in the legend.
    const int indRows = (indT > 0) ? 3 : 0;
    const int BOX_W = 104, BOX_H = 87 + indRows * 13, LINE_H = 13;
    int bx = hx + 12;
    if (bx + BOX_W > right) bx = hx - 12 - BOX_W;   // flip to the left at the edge
    if (bx < left) bx = left;
    int by = hy - BOX_H / 2;
    if (by < top) by = top;
    if (by + BOX_H > bottom) by = bottom - BOX_H;

    RECT rcBox = { bx, by, bx + BOX_W, by + BOX_H };
    FillRect(hdc, &rcBox, ctx->brBox);
    FrameRect(hdc, &rcBox, ctx->brBoxEdge);

    int ty = by + 4;
    RECT rcL = { bx + 7, ty, bx + BOX_W - 6, ty + LINE_H };
    SetTextColor(hdc, CLR_TEXT);
    DrawTextW(hdc, tbuf, -1, &rcL, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    const wchar_t* lbl[5] = { L"O", L"H", L"L", L"C", L"V" };
    double val[5] = { hc->open, hc->high, hc->low, hc->close, hc->volume };
    COLORREF cclr = (hc->close >= hc->open) ? CLR_UP : CLR_DOWN;

    for (int i = 0; i < 5; ++i) {
        ty += LINE_H;
        RECT rcRow = { bx + 7, ty, bx + BOX_W - 6, ty + LINE_H };
        SetTextColor(hdc, CLR_DIM);
        DrawTextW(hdc, lbl[i], -1, &rcRow, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        if (i == 4) FormatVolume(val[i], buf, 64);   // phase 21
        else        swprintf_s(buf, 64, L"%.2f", val[i]);
        SetTextColor(hdc, (i == 3) ? cclr : CLR_TEXT);
        DrawTextW(hdc, buf, -1, &rcRow, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }

    // The indicator rows (phase 27). The label in the line's color, the
    // value in the text color - both faded against the box, not CLR_BG.
    if (indRows > 0) {
        const wchar_t* ilbl[3] = { L"SMA", L"EMA", L"VWAP" };
        const COLORREF iclr[3] = { CLR_SMA, CLR_EMA, CLR_VWAP };
        for (int i = 0; i < 3; ++i) {
            ty += LINE_H;
            RECT rcRow = { bx + 7, ty, bx + BOX_W - 6, ty + LINE_H };
            SetTextColor(hdc, Blend(CLR_BOX, iclr[i], indT));
            DrawTextW(hdc, ilbl[i], -1, &rcRow, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            if (indOk[i]) swprintf_s(buf, 64, L"%.2f", indVal[i]);
            else          wcscpy_s(buf, 64, L"-");
            SetTextColor(hdc, Blend(CLR_BOX, CLR_TEXT, indT));
            DrawTextW(hdc, buf, -1, &rcRow, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        }
    }
}


// The control buttons. Pure GDI vectors - no font, no glyph lookup. A
// DrawTextW with a Unicode character costs far more than four LineTo, and
// would also depend on the font HAVING the glyph (the tray icon's missing
// k glyph is the same problem further down the log).
//
// No fade. The buttons change color instantly on hover: a fade would have
// required yet another animated value in WM_TIMER, and a hit that hangs on
// the fade level instead of on btnHot is exactly pitfall 12.
static void DrawButtons(AppContext* ctx, HDC hdc, int W, BOOL zoomed) {
    RECT b[BTN_COUNT];
    ButtonLayout(W, b);

    HPEN   oldPen = (HPEN)SelectObject(hdc, ctx->penBtn);
    HBRUSH oldBr  = (HBRUSH)SelectObject(hdc, (HBRUSH)GetStockObject(NULL_BRUSH));

    for (int i = 0; i < BTN_COUNT; ++i) {
        RECT* r = &b[i];
        BOOL hot = (ctx->btnHot == i);

        // The button surface is ALWAYS cleared before the vectors are drawn,
        // also at rest: at rest with CLR_BG, so the button is still just a
        // glyph on the panel's own background. Then no earlier glyph or
        // hover color can be left underneath, whatever the DC held before.
        // Both paths in PaintPopup come here, so the fast path and the slow
        // one still paint identically.
        FillRect(hdc, r, hot ? ((i == BTN_CLOSE) ? ctx->brClose : ctx->brBox)
                             : ctx->brBg);

        SelectObject(hdc, hot ? ((i == BTN_CLOSE) ? ctx->penBtnWhite : ctx->penBtnHot)
                              : ctx->penBtn);

        int cx = (r->left + r->right) / 2;
        int cy = (r->top + r->bottom) / 2;
        int g  = 4;   // half glyph width: 9x9 pixels in total

        switch (i) {
            case BTN_NEW:
                // Plus sign, 7x7 around the center pixel: x and y in [-3, +3].
                // LineTo does not draw the end point, hence +4 - that is what
                // makes the cross symmetric. 7 and not 9 like the others: a
                // 9x9 plus weighs optically heavier than the X next to it.
                MoveToEx(hdc, cx - 3, cy, NULL);
                LineTo(hdc, cx + 4, cy);
                MoveToEx(hdc, cx, cy - 3, NULL);
                LineTo(hdc, cx, cy + 4);
                break;
            case BTN_MIN:
                MoveToEx(hdc, cx - g, cy + 3, NULL);
                LineTo(hdc, cx + g + 1, cy + 3);
                break;
            case BTN_MAX:
                if (zoomed) {
                    // Restore: two overlapping rectangles. The back one is
                    // drawn as an OPEN polyline - only the edges that do not
                    // lie behind the front one - so we avoid filling the
                    // front one opaque to hide the overlap. Two GDI calls,
                    // not four.
                    //
                    // Two 7x7 rectangles offset 2 px diagonally, within the
                    // same 9x9 footprint as the other glyphs. The back rect
                    // is x[-2..+4] y[-4..+2], the front x[-4..+2] y[-2..+4].
                    // The visible part of the back one is everything outside
                    // the front one: the left edge down to the overlap, the
                    // top, the right edge, and the stub of the bottom.
                    // Polyline does not draw the last point, so it stops just
                    // before the front one's right edge.
                    POINT bak[5] = {
                        { cx - 2, cy - 2 },
                        { cx - 2, cy - 4 },
                        { cx + 4, cy - 4 },
                        { cx + 4, cy + 2 },
                        { cx + 2, cy + 2 },
                    };
                    Polyline(hdc, bak, 5);
                    // NULL_BRUSH is selected above, so Rectangle gives only an outline.
                    Rectangle(hdc, cx - 4, cy - 2, cx + 3, cy + 5);
                } else {
                    // NULL_BRUSH is selected above, so Rectangle gives only an outline.
                    Rectangle(hdc, cx - g, cy - g, cx + g + 1, cy + g + 1);
                }
                break;
            case BTN_CLOSE:
                MoveToEx(hdc, cx - g, cy - g, NULL);
                LineTo(hdc, cx + g + 1, cy + g + 1);
                MoveToEx(hdc, cx + g, cy - g, NULL);
                LineTo(hdc, cx - g - 1, cy + g + 1);
                break;
        }
    }

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
}

// The toolbar. Flat like the control buttons: no fade, the color changes
// instantly, and the hit hangs on tbHot. Three states: rest (muted text, no
// surface), hover (CLR_BOX surface) and active (CLR_BOX surface with a
// CLR_BOXEDGE frame) - active is the current interval, VOL when the bars
// are shown, and the symbol pill while the overlay it opens is open. The
// palette is the hover box's and the overlay's. No new GDI objects: the
// brushes exist, and the arrow is drawn with DC_PEN/DC_BRUSH.
//
// Called from PaintPopup, not from DrawChart, for the same reason as the
// buttons: right after an interval switch the buffer is empty, and that is
// exactly when the user looks for which pill became active. Everything it
// reads is UI-owned or written only by the UI thread (symIdx, ivIdx).
static void DrawToolbar(AppContext* ctx, HDC hdc, int W) {
    RECT tb[TBAR_COUNT];
    int n = ToolbarLayout(W, tb);
    if (n <= 0) return;

    HGDIOBJ oldFont = SelectObject(hdc, ctx->hFontSmall);
    SetBkMode(hdc, TRANSPARENT);

    for (int i = 0; i < n; ++i) {
        RECT* r = &tb[i];
        BOOL hot = (ctx->tbHot == i);
        BOOL on;
        const wchar_t* lbl;
        if (i == TBAR_SYM)      { on = ctx->overlayOpen; lbl = SYMBOLS[ctx->symIdx].label; }
        else if (i == TBAR_VOL) { on = ShowVolNow(ctx);  lbl = L"VOL"; }
        else if (i == TBAR_IND) { on = ShowIndNow(ctx);  lbl = L"MA"; }
        else { on = (i - TBAR_IV_FIRST == ctx->ivIdx);   lbl = INTERVALS[i - TBAR_IV_FIRST].label; }

        if (hot || on) FillRect(hdc, r, ctx->brBox);
        if (on)        FrameRect(hdc, r, ctx->brBoxEdge);

        COLORREF fg = (hot || on || i == TBAR_SYM) ? CLR_TEXT : CLR_DIM;
        SetTextColor(hdc, fg);
        if (i == TBAR_SYM) {
            // Left-aligned text and a down arrow at the right end: the pill
            // opens a list, it does not switch by itself. The arrow is a
            // filled triangle, 7 px wide and 4 tall - vector, like the
            // button glyphs.
            RECT t = *r;
            t.left += 6; t.right -= 14;
            DrawTextW(hdc, lbl, -1, &t, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            int ax = r->right - 9, ay = (r->top + r->bottom) / 2 - 1;
            POINT tri[3] = { { ax - 3, ay }, { ax + 3, ay }, { ax, ay + 3 } };
            HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
            HGDIOBJ oldBr  = SelectObject(hdc, GetStockObject(DC_BRUSH));
            SetDCPenColor(hdc, CLR_DIM);
            SetDCBrushColor(hdc, CLR_DIM);
            Polygon(hdc, tri, 3);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBr);
        } else {
            DrawTextW(hdc, lbl, -1, r, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
    }
    SelectObject(hdc, oldFont);
}

static void PaintPopup(AppContext* ctx, HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdcDst = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    // Double buffer: draw everything in memory, blit once -> no flicker. It
    // lives between frames; see bbDC in AppContext for why.
    if (!EnsureBackBuffer(ctx, hdcDst, W, H)) {
        EndPaint(hwnd, &ps);
        return;
    }
    HDC hdcMem = ctx->bbDC;

    // Fast path: if ALL of the dirty area is inside the button row, we need
    // neither DrawChart nor DrawOverlay. A hover change invalidates exactly
    // that rectangle; without this branch it would cost a full repaint, and
    // the incremental invalidation would only save the final blit.
    //
    // The buttons are drawn straight into the buffer, which holds the previous
    // full frame. The gaps in the strip are therefore exactly what the slow
    // path left there, and DrawButtons clears each button face itself. Three
    // conditions make that true: the buffer holds a full frame at this size
    // (bbValid), and the overlay is neither open nor visible while fading
    // out - it dims the WHOLE client area, header included, so the buffer's
    // strip would have been dimmed while the buttons were not. A concurrent
    // InvalidateRect(NULL) from the animation timer unions with the strip, so
    // rcPaint becomes the whole surface and we fall into the slow path on our
    // own.
    RECT strip;
    ButtonStrip(W, &strip);
    // !g_desktopMode is explicit here: the button row is not drawn there, so
    // the fast path would have become a strip without buttons over the chart.
    if (ctx->bbValid && !g_desktopMode && !ctx->overlayOpen && (int)(ctx->overlayF + 0.5) <= 0 &&
        ps.rcPaint.left   >= strip.left  && ps.rcPaint.top    >= strip.top &&
        ps.rcPaint.right  <= strip.right && ps.rcPaint.bottom <= strip.bottom) {

        DrawButtons(ctx, hdcMem, W, IsZoomed(hwnd));
        BitBlt(hdcDst, strip.left, strip.top,
               strip.right - strip.left, strip.bottom - strip.top,
               hdcMem, strip.left, strip.top, SRCCOPY);
        EndPaint(hwnd, &ps);
        return;
    }

#ifdef TICKER_PROBE
    LARGE_INTEGER qpc0;
    QueryPerformanceCounter(&qpc0);
#endif

    // The thread can merge in new candles at any time; the lock keeps
    // the buffer stable through the whole repaint (~1.8 ms).
    EnterCriticalSection(&ctx->lock);
    DrawChart(ctx, hdcMem, W, H);
    LeaveCriticalSection(&ctx->lock);

    // The buttons are drawn HERE, not in DrawChart. DrawChart returns early
    // when the buffer is empty - that is, while it says "Loading data from
    // Binance..." and during an entire disconnect. Had the drawing been there,
    // the cross would vanish exactly when the user wants to close the panel.
    // Same reason DrawOverlay sits here.
    //
    // Outside the lock: btnHot and the button geometry are UI-owned.
    //
    // Not in desktop mode: the surface does not take clicks, and a button
    // that cannot be pressed should not be shown. The fast path above is
    // never reached there either - without mouse messages the button row is
    // never invalidated on its own.
    if (!g_desktopMode) DrawButtons(ctx, hdcMem, W, IsZoomed(hwnd));
    // The toolbar (phase 22): same place, same reason, same exception. Before
    // the overlay, which must lie on top of everything.
    if (!g_desktopMode) DrawToolbar(ctx, hdcMem, W);

    // The overlay is drawn OUTSIDE the lock: everything it reads (overlayF,
    // overlayHot, symIdx, ivIdx) is UI-owned. And it must be here, not in
    // DrawChart, which returns early when the buffer is empty - exactly the
    // state right after a config change.
    DrawOverlay(ctx, hdcMem, W, H);

    BitBlt(hdcDst, 0, 0, W, H, hdcMem, 0, 0, SRCCOPY);
    ctx->bbValid = TRUE;

#ifdef TICKER_PROBE
    {
        LARGE_INTEGER qpc1, qpf;
        QueryPerformanceCounter(&qpc1);
        QueryPerformanceFrequency(&qpf);
        g_probePaintUs = (qpc1.QuadPart - qpc0.QuadPart) * 1000000LL / qpf.QuadPart;
    }
#endif

    EndPaint(hwnd, &ps);
}

// ---------------------------------------------------------------------------
// Popup window
// ---------------------------------------------------------------------------

// Starts the animation timer. Idempotent - SetTimer on an id that is already
// running just restarts it. lastAnimTick is reset only when the timer was
// stopped, otherwise a new call in the middle of an animation would give
// dt = 0.
static void StartAnim(HWND hwnd) {
    if (!g_Ctx.animRunning) {
        g_Ctx.animRunning  = TRUE;
        g_Ctx.lastAnimTick = GetTickCount64();
        SetTimer(hwnd, TIMER_ANIM_ID, ANIM_INTERVAL, NULL);
    }
}

// Switches symbol or interval. Bumps configGen and empties the buffer in the
// SAME critical section, so that a response from the previous config that
// arrives right now is discarded instead of merged in.
//
// Called from the overlay and from the tray menu (phase 17). Takes no HWND:
// the panel may be closed when the choice comes from the menu, and
// InvalidateRect(NULL, ...) would repaint the whole desktop.
// hit: [0, SYMBOL_COUNT) is symbol, [SYMBOL_COUNT, +INTERVAL_COUNT) interval.
static void ApplyConfigChoice(AppContext* ctx, int hit) {
    BOOL isSym = (hit < SYMBOL_COUNT);
    int  idx   = isSym ? hit : (hit - SYMBOL_COUNT);
    if (isSym  && (idx < 0 || idx >= SYMBOL_COUNT))   return;
    if (!isSym && (idx < 0 || idx >= INTERVAL_COUNT)) return;
    if (isSym  && idx == ctx->symIdx) return;   // no change, no emptying
    if (!isSym && idx == ctx->ivIdx)  return;

    EnterCriticalSection(&ctx->lock);
    if (isSym) ctx->symIdx = idx;
    else       { ctx->ivIdx = idx; ctx->intervalMs = INTERVALS[idx].ms; }
    ctx->configGen++;
    ctx->candleCount = 0;
    ctx->viewStart   = 0;
    ctx->viewCount   = 0;
    ctx->followLive  = TRUE;
    ctx->lastPrice   = 0.0;
    ctx->histPending = FALSE;   // new config: history starts over
    ctx->histDone    = FALSE;
    LeaveCriticalSection(&ctx->lock);

    ctx->hoverIdx = -1;
    // Phase 23: alertHot is an index into the PREVIOUS symbol's alerts, and
    // the afterglow sits at the previous symbol's price level.
    ctx->alertHot    = -1;
    ctx->axisHotY    = -1;
    ctx->alertFresh  = 0.0;
    ctx->alertFlashF = 0.0;
    ctx->wmValid  = FALSE;   // the watermark shows the previous symbol/interval
    ctx->dispValid = FALSE;  // new buffer: nothing to ease from
    UpdatePopupTitle(ctx);   // the title bar and the taskbar must follow along
    // SetEvent sits outside the lock. It is not PostMessage, but the same rule
    // applies for the same reason: do not hold the lock across anything that
    // wakes the other thread.
    SetEvent(ctx->hWakeEvent);
    SaveConfig(ctx);
    if (ctx->hPopup) InvalidateRect(ctx->hPopup, NULL, FALSE);
}

// The VOL toggle (phase 22). Not through ApplyConfigChoice: that empties
// the buffer and bumps configGen, and the volume is already in the candles -
// this is a pure drawing choice. Called from the pill, the V key and the tray
// menu, so desktop mode can switch without a panel (like phase 17). If the
// surface is visible, dispVolF is eased by the timer; otherwise it snaps, so
// a panel opened later does not play an animation nobody asked for.
static void SetShowVolume(AppContext* ctx, BOOL on) {
    if (ShowVolNow(ctx) == on) return;
    if (g_desktopMode) ctx->showVolDesk = on;   // phase 26: one choice per mode
    else               ctx->showVol     = on;
    SaveConfig(ctx);
    if (ctx->hPopup && IsWindowVisible(ctx->hPopup)) {
        StartAnim(ctx->hPopup);
        InvalidateRect(ctx->hPopup, NULL, FALSE);
    } else {
        ctx->dispVolF = on ? 1.0 : 0.0;
    }
}

// The MA toggle (phase 25): moving averages off and on. Same shape and same
// reasons as SetShowVolume above - a pure drawing choice, from the pill, the
// M key and the tray menu, eased when the surface is visible and snapped
// otherwise.
static void SetShowIndicators(AppContext* ctx, BOOL on) {
    if (ShowIndNow(ctx) == on) return;
    if (g_desktopMode) ctx->showIndDesk = on;   // phase 26: one choice per mode
    else               ctx->showInd     = on;
    SaveConfig(ctx);
    if (ctx->hPopup && IsWindowVisible(ctx->hPopup)) {
        StartAnim(ctx->hPopup);
        InvalidateRect(ctx->hPopup, NULL, FALSE);
    } else {
        ctx->dispIndF = on ? 1.0 : 0.0;
    }
}

// ---------------------------------------------------------------------------
// Price alerts (phase 23). Everything here runs on the UI thread and touches
// only UI-owned fields; the lock is taken only to read the reference price.
// ---------------------------------------------------------------------------

// Sets an alert at level for the current symbol. The side is decided against
// the reference price NOW: the last candle's close when the chart has data
// (that is the one the user sees the stamp for), otherwise lastPrice. FALSE
// when there is no price to pick a side against, when the level IS the price
// (no side, and it would fire on the next fetch), when the level already
// exists, or when all the slots are used.
static BOOL AlertAdd(AppContext* ctx, double level) {
    int s = ctx->symIdx;
    if (!(level > 0.0 && level < ALERT_PRICE_MAX)) return FALSE;
    if (ctx->alertCount[s] >= ALERT_MAX) return FALSE;

    double ref;
    EnterCriticalSection(&ctx->lock);
    ref = (ctx->candleCount > 0) ? ctx->candles[ctx->candleCount - 1].close
                                 : ctx->lastPrice;
    LeaveCriticalSection(&ctx->lock);
    if (ref <= 0.0 || level == ref) return FALSE;

    for (int i = 0; i < ctx->alertCount[s]; ++i) {
        if (fabs(fabs(ctx->alerts[s][i]) - level) < 0.005) return FALSE;
    }
    ctx->alerts[s][ctx->alertCount[s]++] = (level > ref) ? level : -level;
    SaveAlerts(ctx);
    if (ctx->hPopup) InvalidateRect(ctx->hPopup, NULL, FALSE);
    return TRUE;
}

// The order does not matter, so the hole is filled with the last one.
static void AlertRemove(AppContext* ctx, int i) {
    int s = ctx->symIdx;
    if (i < 0 || i >= ctx->alertCount[s]) return;
    ctx->alerts[s][i] = ctx->alerts[s][--ctx->alertCount[s]];
    ctx->alertHot = -1;   // the index no longer points at the same thing
    SaveAlerts(ctx);
    if (ctx->hPopup) InvalidateRect(ctx->hPopup, NULL, FALSE);
}

// The tray menu's "Clear price alerts": the current symbol, not all - the menu
// shows the count for that symbol, and those are the lines the user sees.
static void AlertsClear(AppContext* ctx) {
    if (ctx->alertCount[ctx->symIdx] == 0) return;
    ctx->alertCount[ctx->symIdx] = 0;
    ctx->alertHot = -1;
    SaveAlerts(ctx);
    if (ctx->hPopup) InvalidateRect(ctx->hPopup, NULL, FALSE);
}

// An alert has fired. Three channels, because the user can be in three places:
// looking at the panel (the afterglow on the line), looking at something else
// (the balloon from the tray icon), or not looking at the screen (the sound).
// The balloon is NIIF_NOSOUND and the sound our own MessageBeep: one sound,
// not two, and it also comes when Windows holds the balloon back (Do not
// disturb). nid is copied: UpdateIcon owns the original and sets uFlags
// itself, and a NIF_INFO left standing there would show the balloon again on
// every price update.
static void FireAlert(AppContext* ctx, double signedLevel, double price) {
    double level = fabs(signedLevel);
    ctx->alertFired++;
    ctx->alertLastFired = level;

    if (ctx->hPopup && IsWindowVisible(ctx->hPopup)) {
        ctx->alertFlashLevel = level;
        ctx->alertFlashF     = 1.0;
        StartAnim(ctx->hPopup);
        InvalidateRect(ctx->hPopup, NULL, FALSE);
    }

#ifdef TICKER_PROBE
    if (g_probeMute) return;
#endif
    NOTIFYICONDATAW n = ctx->nid;
    n.uFlags      = NIF_INFO;
    n.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    swprintf_s(n.szInfoTitle, 64, L"%s  %.2f", SYMBOLS[ctx->symIdx].label, level);
    swprintf_s(n.szInfo, 256, L"Price crossed the alert going %s. Last price: $%.2f",
               (signedLevel > 0.0) ? L"up" : L"down", price);
    ctx->alertNotifyOk = Shell_NotifyIconW(NIM_MODIFY, &n);
    MessageBeep(MB_ICONASTERISK);
}

// Tests the alerts for the current symbol against a new price. Called from
// WM_APP_DATA, that is once per fetch - also with the panel closed, where the
// thread fetches the ticker price, and in desktop mode. An alert fires ONCE
// and is removed: a price that wobbles around the level would otherwise beep
// every third second. Backwards, because the AlertRemove pattern moves the
// last one into the hole. Only the current symbol: the price we have is its.
static void CheckAlerts(AppContext* ctx, double price) {
    int s = ctx->symIdx;
    BOOL any = FALSE;
    for (int i = ctx->alertCount[s] - 1; i >= 0; --i) {
        double a = ctx->alerts[s][i];
        if (!AlertHit(price, a)) continue;
        ctx->alerts[s][i] = ctx->alerts[s][--ctx->alertCount[s]];
        any = TRUE;
        FireAlert(ctx, a, price);
    }
    if (any) {
        ctx->alertHot = -1;
        SaveAlerts(ctx);
    }
}

// Click in the price column: on a tag removes it, on empty surface sets a new
// one at the rounded price there. Hover is recomputed at once - the pointer
// sits on the new tag, and a posted click has no WM_MOUSEMOVE ahead of it.
// Its own function because WM_LBUTTONDBLCLK also lands here (pitfall 38):
// a fast double-click is set + remove, not set + reset the view.
static void OnAxisClick(HWND hwnd, const ChartRect* g, int my) {
    int hit = AlertAxisHit(&g_Ctx, g, my);
    g_Ctx.alertFresh = 0.0;
    if (hit >= 0) {
        AlertRemove(&g_Ctx, hit);
    } else if (AlertAdd(&g_Ctx, AlertPriceAtY(&g_Ctx, g, my))) {
        int s = g_Ctx.symIdx;
        g_Ctx.alertFresh = g_Ctx.alerts[s][g_Ctx.alertCount[s] - 1];
    }
    g_Ctx.axisHotY = my;
    g_Ctx.alertHot = AlertAxisHit(&g_Ctx, g, my);
    InvalidateRect(hwnd, NULL, FALSE);
}

// Click on a pill in the toolbar. Its own function for the same reason as
// OnButtonClick: WM_LBUTTONDOWN and WM_LBUTTONDBLCLK both reach here, so two
// fast clicks on VOL are two toggles and not one (pitfall 38).
// The intervals go through ApplyConfigChoice, so registry, watermark,
// configGen and the thread are handled exactly as from the overlay and the
// tray menu; a click on the active interval is a no-op there. The symbol pill
// opens the existing overlay - no new menu, no new hit-test code.
static void OnToolbarClick(HWND hwnd, int th) {
    if (th == TBAR_SYM) {
        g_Ctx.overlayOpen = TRUE;
        g_Ctx.overlayHot  = -1;
        g_Ctx.hoverIdx    = -1;
        g_Ctx.tbHot       = -1;   // no pill lights up while the overlay owns the mouse
        StartAnim(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
    } else if (th == TBAR_VOL) {
        SetShowVolume(&g_Ctx, !ShowVolNow(&g_Ctx));
    } else if (th == TBAR_IND) {
        SetShowIndicators(&g_Ctx, !ShowIndNow(&g_Ctx));
    } else if (th >= TBAR_IV_FIRST && th < TBAR_VOL) {
        ApplyConfigChoice(&g_Ctx, SYMBOL_COUNT + (th - TBAR_IV_FIRST));
    }
}

// Zoom and panning back to the default view: the last DEFAULT_VIEW candles,
// pinned to the right edge and followed live. Does not touch dispValid, so
// the display eases back from where it is - the same mechanism as wheel zoom.
// TogglePopup, on the other hand, wants a snap on opening and sets dispValid
// itself. The window geometry is another matter: ResetToDefaultView owns it
// (Ctrl+0).
static void ResetView(AppContext* ctx) {
    EnterCriticalSection(&ctx->lock);
    ctx->viewCount  = 0;
    ctx->followLive = TRUE;
    if (ctx->candleCount > 0) {
        int vc = (ctx->candleCount < DEFAULT_VIEW) ? ctx->candleCount : DEFAULT_VIEW;
        ctx->viewCount = vc;
        ctx->viewStart = ctx->candleCount - vc;
    }
    LeaveCriticalSection(&ctx->lock);
}

// Is the view where ResetView would have put it? ESC uses the answer to pick
// a layer: if it is already at the default, ESC hides the panel instead.
static BOOL ViewIsDefault(AppContext* ctx) {
    EnterCriticalSection(&ctx->lock);
    int n = ctx->candleCount, vs, vc;
    GetView(ctx, &vs, &vc);
    int want = (n < DEFAULT_VIEW) ? n : DEFAULT_VIEW;
    BOOL def = (n == 0) || (vc == want && vs + vc >= n);
    LeaveCriticalSection(&ctx->lock);
    return def;
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
static BOOL PanView(AppContext* ctx, int delta) {
    int vs, vc;
    GetView(ctx, &vs, &vc);
    ctx->viewStart = vs + delta;
    ctx->viewCount = vc;
    ClampView(ctx);
    ctx->followLive = (ctx->viewStart + ctx->viewCount >= ctx->candleCount);
    return (ctx->viewStart == 0);
}

// ZoomView: notches > 0 zooms in, < 0 out, ZOOM_STEP per notch, around an
// anchor given as a fraction [0, 1] of the view - the pointer's position for
// the wheel, the middle for the keys.
static BOOL ZoomView(AppContext* ctx, double frac, int notches) {
    int n = ctx->candleCount;
    int vs, vc;
    GetView(ctx, &vs, &vc);
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
    ClampView(ctx);
    ctx->followLive = (ctx->viewStart + ctx->viewCount >= ctx->candleCount);
    return (ctx->viewStart == 0);
}

// Hides the panel to the notification area - or, in a duplicate, exits the
// process. A duplicate has no main-instance role to return to, and a tail of
// hidden tray icons is not a feature. The exit goes through the tray menu's
// own path, so the icon is removed the same way in both cases.
static void HidePanel(HWND hwnd) {
    g_Ctx.hoverIdx = -1;
    g_Ctx.btnHot   = -1;
    g_Ctx.tbHot    = -1;
    g_Ctx.alertHot = -1;
    g_Ctx.axisHotY = -1;
    if (g_isDuplicate) {
        ShowWindow(hwnd, SW_HIDE);
        SendMessageW(g_Ctx.hWnd, WM_COMMAND, ID_TRAY_EXIT, 0);
        return;
    }
    SaveWindowPlacement(hwnd);
    ShowWindow(hwnd, SW_HIDE);
}

// Starts a new, isolated instance of the program, offset SPAWN_OFFSET
// down and to the right. Geometry, symbol and interval go on the command
// line - not via the registry, which the main instance owns and duplicates
// do not write.
//
// Maximized window: +30 from a window that fills the screen would put the
// child halfway outside. Then the restored geometry is used. Otherwise
// GetWindowRect, which is screen coordinates - rcNormalPosition is
// work-area coordinates, and differs when the taskbar sits at the top
// or on the left.
static void SpawnInstance(HWND hwnd) {
    RECT r;
    if (IsZoomed(hwnd)) {
        WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
        if (!GetWindowPlacement(hwnd, &wp)) return;
        r = wp.rcNormalPosition;
    } else if (!GetWindowRect(hwnd, &r)) {
        return;
    }
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = r.left + SPAWN_OFFSET, y = r.top + SPAWN_OFFSET;

    // The cascade goes back to the corner when the next step would push the
    // button row out of the work area - otherwise [ + ] after a few clicks
    // becomes a window the user cannot close.
    MONITORINFO mi = { sizeof(MONITORINFO) };
    if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        RECT wa = mi.rcWork;
        if (x + w > wa.right)  x = wa.left;
        if (y + h > wa.bottom) y = wa.top;
    }

    wchar_t exe[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return;

    // CreateProcessW may write to the command line, so it must live in a
    // writable buffer - never a string constant.
    wchar_t cmd[MAX_PATH + 96];
    swprintf_s(cmd, MAX_PATH + 96, L"\"%s\" --dup %d %d %d %d %d %d",
               exe, x, y, w, h, g_Ctx.symIdx, g_Ctx.ivIdx);

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION pi;
    if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

// Click on a control button. Its own function because two messages reach
// here: WM_LBUTTONDOWN, and WM_LBUTTONDBLCLK - with CS_DBLCLKS the second
// click of a fast double-click becomes a DBLCLK, and the buttons would
// otherwise have swallowed it.
static void OnButtonClick(HWND hwnd, int bh) {
    switch (bh) {
        case BTN_NEW:
            SpawnInstance(hwnd);
            break;
        case BTN_MIN:
            ShowWindow(hwnd, SW_MINIMIZE);
            break;
        case BTN_MAX:
            if (IsZoomed(hwnd)) {
                ShowWindow(hwnd, SW_RESTORE);
                // The mandate's "or DPI-scaled 1280x720": the OS owns the
                // restored rect, and SW_RESTORE uses it. But if it was saved
                // on a monitor that has since been disconnected, the window
                // ends up outside everything visible. Same check as
                // PlacePopupInitially does on opening.
                WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
                if (GetWindowPlacement(hwnd, &wp)) {
                    RECT* nr = &wp.rcNormalPosition;
                    if (!PlacementIsVisible(nr->left, nr->top,
                                            nr->right - nr->left,
                                            nr->bottom - nr->top)) {
                        ResetToDefaultView(hwnd);
                    }
                }
            } else {
                // The geometry is saved before we maximize. On restore it is
                // already saved.
                SaveWindowPlacement(hwnd);
                ShowWindow(hwnd, SW_MAXIMIZE);
            }
            break;
        case BTN_CLOSE:
            // WM_CLOSE, not DestroyWindow: the existing handler saves the
            // geometry and hides to the notification area. The ticker is a
            // tray program.
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            break;
    }
}

static LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1; // handled in WM_PAINT

        // The window disappears without us asking for it: in desktop mode the
        // parent is Explorer's WorkerW, and it can be torn down (Explorer
        // is restarted). If we are the ones tearing down, WM_DESTROY in
        // WndProc has already zeroed hPopup, and then this is a no-op.
        //
        // animRunning must go down: the timer died with the window, and
        // StartAnim would otherwise think the clock is still running on the
        // next surface.
        case WM_NCDESTROY:
            if (g_Ctx.hPopup == hwnd) {
                EnterCriticalSection(&g_Ctx.lock);   // the thread reads hPopup
                g_Ctx.hPopup = NULL;
                LeaveCriticalSection(&g_Ctx.lock);
                g_Ctx.animRunning = FALSE;
                if (g_desktopMode) {
                    SetTimer(g_Ctx.hWnd, TIMER_EMBED_ID, EMBED_RETRY_MS, NULL);
                }
            }
            break;

        // No NC painting on focus change. WM_NCCALCSIZE below makes the
        // client as large as the window, but DefWindowProc still paints the
        // classic WS_THICKFRAME frame in the window DC - that is, ON TOP of
        // the chart, 3 px deep, in COLOR_ACTIVEBORDER (#B4B4B4) or
        // COLOR_INACTIVEBORDER (#F4F7FC) with light edges. It stays until the
        // next full repaint, up to 3 s. Measured from the screen: 7.2 million
        // frame-colored edge pixels over 3 focus changes, versus 0 with this.
        //
        // lParam = -1 is the documented way to say "do not paint the frame".
        // DefWindowProc does the rest of the activation as before, instead of
        // us answering TRUE and skipping it entirely.
        case WM_NCACTIVATE:
            return DefWindowProcW(hwnd, msg, wParam, -1);

        // There is no NC surface to paint. No measured path painted anything
        // here after the fix above (focus change, WM_SETTEXT, resize), so
        // this is a guard, not the fix.
        //
        // DWMNCRP_DISABLED is NOT used: it turns off the DWM frame and lets
        // the classic NC painting back in. Measured: 1 296 frame-colored
        // pixels back, and the foreground switch failed in two of three cycles.
        case WM_NCPAINT:
            return 0;

        // Removes the whole non-client frame: the client area becomes as
        // large as the window rectangle, and we paint everything ourselves.
        case WM_NCCALCSIZE: {
            if (!wParam) break;
            // A maximized window needs no special handling here -
            // WM_GETMINMAXINFO below gives the OS the exact work area, so
            // there is no overhang to subtract. Measured: without it a
            // WS_POPUP maximized to the whole SCREEN extended by the frame
            // width (-7,-7 3854x1614 versus rcWork 0,0 3840x1552), and the
            // panel covered the taskbar.
            return 0;
        }

        // Without an OS frame it is we who decide what the mouse is over.
        // The RESIZE_BORDER zones give the OS's own resizing; free header
        // area gives HTCAPTION, which is what DefWindowProc needs to send
        // WM_NCLBUTTONDOWN and run native moving with Aero Snap.
        case WM_NCHITTEST: {
            // Desktop mode: nothing here should take the mouse.
            // WS_EX_TRANSPARENT does the same for the system; this keeps the
            // button and edge logic below out of it no matter who asks.
            if (g_desktopMode) return HTTRANSPARENT;
            RECT rw;
            GetWindowRect(hwnd, &rw);
            int x = GET_X_LPARAM(lParam) - rw.left;
            int y = GET_Y_LPARAM(lParam) - rw.top;
            int w = rw.right - rw.left, h = rw.bottom - rw.top;

            // A maximized window must not be resizable at the edges - then a
            // click 2 px from the screen edge would start a drag-resize of
            // something that by definition fills the screen.
            if (!IsZoomed(hwnd)) {
                int lft = (x < RESIZE_BORDER), rgt = (x >= w - RESIZE_BORDER);
                int tp  = (y < RESIZE_BORDER), bot = (y >= h - RESIZE_BORDER);
                if (tp  && lft) return HTTOPLEFT;
                if (tp  && rgt) return HTTOPRIGHT;
                if (bot && lft) return HTBOTTOMLEFT;
                if (bot && rgt) return HTBOTTOMRIGHT;
                if (lft) return HTLEFT;
                if (rgt) return HTRIGHT;
                if (tp)  return HTTOP;
                if (bot) return HTBOTTOM;
            }

            if (y < HEADER_H) {
                // The buttons must be HTCLIENT, otherwise WM_LBUTTONDOWN
                // never reaches them: an HTCAPTION area gives NC messages,
                // and DefWindowProc would start a window move from a click
                // on the close cross. The order here IS the mechanism in the
                // mandate's points 2 and 3 - HTCLIENT where the buttons are,
                // HTCAPTION on free area.
                //
                // The NCHITTEST coordinates are relative to the WINDOW. With
                // the frame removed in WM_NCCALCSIZE, client and window are
                // the same rectangle, so x can be used directly against
                // ButtonLayout.
                RECT btns[BTN_COUNT];
                ButtonLayout(w, btns);
                if (ButtonHit(btns, x, y) >= 0) return HTCLIENT;
                // The pills in the toolbar (phase 22) are buttons of the
                // same kind, with the same requirement: HTCLIENT, otherwise
                // they are painted and dead, and a click on 5m moves the
                // window (pitfall 21). The gaps between the pills are still
                // HTCAPTION.
                RECT tb[TBAR_COUNT];
                ToolbarLayout(w, tb);
                if (ToolbarHit(tb, x, y) >= 0) return HTCLIENT;
                return HTCAPTION;
            }
            return HTCLIENT;
        }

        // The cursor during panning. Without this the OS resets the cursor
        // to the window class's IDC_ARROW on every single mouse move, and a
        // SetCursor from WM_MOUSEMOVE would be overwritten at once.
        //
        // Only HTCLIENT and only while we pan: the edge zones keep their own
        // resize cursors, which DefWindowProc gives for free, and the header
        // should have the normal arrow the way a title bar does. Hence break
        // and not return 0 for everything else.
        case WM_SETCURSOR:
            if (g_Ctx.panning && LOWORD(lParam) == HTCLIENT) {
                SetCursor(g_Ctx.curPan);
                return TRUE;
            }
            // The price column (phase 23) is clickable - set or remove an
            // alert - and says so with the hand. axisHotY is -1 during
            // panning and with the overlay open, so the branch above and the
            // overlay win.
            if (g_Ctx.axisHotY >= 0 && LOWORD(lParam) == HTCLIENT) {
                SetCursor(g_Ctx.curHand);
                return TRUE;
            }
            break;

        case WM_PAINT:
            PaintPopup(&g_Ctx, hwnd);
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            // DPI-scaled for the same reason as PlacePopupInitially: the
            // process is DPI-unaware today, so this is 400x250, but the limit
            // should follow along the day a manifest is added.
            UINT dpi = GetDpiForWindow(hwnd);
            if (dpi == 0) dpi = 96;
            mmi->ptMinTrackSize.x = MulDiv(POPUP_MIN_W, (int)dpi, 96);
            mmi->ptMinTrackSize.y = MulDiv(POPUP_MIN_H, (int)dpi, 96);

            // A WS_POPUP window maximizes to the whole SCREEN, not to the
            // work area - and the OS adds the frame width outside. Measured
            // before this block existed: -7,-7 3854x1614, versus rcWork
            // 0,0 3840x1552. The panel covered the taskbar, and the close
            // cross sat 7 px outside the screen edge. A WS_OVERLAPPEDWINDOW
            // would get this for free; we do not, so we state the limits
            // ourselves.
            //
            // ptMaxPosition is relative to the MONITOR's corner, not to the
            // desktop - hence rcMonitor is subtracted.
            HMONITOR hm = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(MONITORINFO) };
            if (GetMonitorInfoW(hm, &mi)) {
                mmi->ptMaxPosition.x  = mi.rcWork.left - mi.rcMonitor.left;
                mmi->ptMaxPosition.y  = mi.rcWork.top  - mi.rcMonitor.top;
                mmi->ptMaxSize.x      = mi.rcWork.right  - mi.rcWork.left;
                mmi->ptMaxSize.y      = mi.rcWork.bottom - mi.rcWork.top;
                // ptMaxTrackSize is NOT set. It would clamp manual resizing
                // to one monitor's work area, so the panel could no longer
                // be stretched across two monitors. It is the MAXIMIZED
                // size that should follow rcWork, not the largest allowed
                // size.
            }
            return 0;
        }

        case WM_EXITSIZEMOVE: {
            // The geometry is captured when the user lets go, not only on
            // exit. When the panel was OWNED by the main window it was
            // already torn down by the time WM_DESTROY got there, and the
            // registry was left without PanelWidth - measured. The panel is
            // independent now, so the exit path works too, but this is still
            // the moment the user actually decides the size.
            SaveWindowPlacement(hwnd);
            return 0;
        }

        case WM_SIZE:
            g_Ctx.wmValid = FALSE;   // the bitmap is built for the previous size
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case WM_MOUSEMOVE: {
            if (!g_Ctx.trackingMouse) {
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT) };
                tme.dwFlags   = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                g_Ctx.trackingMouse = TRUE;
            }

            int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            ChartRect g = ChartGeometry(rc.right, rc.bottom);

            // Button hover. Must come after the TrackMouseEvent arming above
            // (pitfall 13) and before the overlay and panning branches, which
            // both return early.
            //
            // While the overlay is open no button should light up: it cannot
            // be clicked either, and a lit button that does not respond is
            // worse than none. The same applies during panning - there the
            // chart surface holds the mouse button via SetCapture, so a drag
            // that passes over the header would light the close cross red in
            // the middle of the panning, without it being possible to click
            // it.
            {
                RECT btns[BTN_COUNT];
                ButtonLayout(rc.right, btns);
                int bh = (g_Ctx.overlayOpen || g_Ctx.panning)
                         ? -1 : ButtonHit(btns, mx, my);
                if (bh != g_Ctx.btnHot) {
                    g_Ctx.btnHot = bh;
                    // Only the button row is dirty. PaintPopup has a fast
                    // path for exactly this rectangle - without it the
                    // invalidation would only clip the final blit, while
                    // the whole buffer was built and the chart repainted.
                    RECT strip;
                    ButtonStrip(rc.right, &strip);
                    InvalidateRect(hwnd, &strip, FALSE);
                }
            }

            // Pill hover (phase 22). Same place and same guards as the
            // buttons. Only the toolbar's strip is dirty; it goes through
            // the slow path (the fast path applies to the button row), but
            // the blit is clipped to the strip, and a hover change happens
            // at most once per pill the cursor passes.
            {
                RECT tb[TBAR_COUNT];
                ToolbarLayout(rc.right, tb);
                int th = (g_Ctx.overlayOpen || g_Ctx.panning)
                         ? -1 : ToolbarHit(tb, mx, my);
                if (th != g_Ctx.tbHot) {
                    g_Ctx.tbHot = th;
                    RECT tstrip;
                    ToolbarStrip(rc.right, &tstrip);
                    InvalidateRect(hwnd, &tstrip, FALSE);
                }
            }

            // The price column (phase 23). Same place and same guards as the
            // buttons and the pills: before the early returns, and nothing
            // lights up while the overlay or a drag owns the mouse. dispValid
            // is FALSE without data - then there is no axis to point at.
            // x > edge: the column x = edge is the line's last pixel, the
            // tags start at edge + 1. The whole surface is dirty: the ghost
            // line runs straight across the chart, like the crosshair.
            {
                int axY = -1, aHot = -1;
                if (!g_Ctx.overlayOpen && !g_Ctx.panning && g_Ctx.dispValid &&
                    mx > g.edge && my >= g.top && my <= g.bottom) {
                    axY  = my;
                    aHot = AlertAxisHit(&g_Ctx, &g, my);
                }
                // The cursor has left the newly set tag: from now on it is
                // a tag like all the others, and turns red next time.
                if (g_Ctx.alertFresh != 0.0 &&
                    (aHot < 0 || g_Ctx.alerts[g_Ctx.symIdx][aHot] != g_Ctx.alertFresh)) {
                    g_Ctx.alertFresh = 0.0;
                }
                if (axY != g_Ctx.axisHotY || aHot != g_Ctx.alertHot) {
                    g_Ctx.axisHotY = axY;
                    g_Ctx.alertHot = aHot;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }

            // The guard comes after the TrackMouseEvent arming above. If we
            // returned before it, WM_MOUSELEAVE would stop firing and the
            // crosshair would stay after the mouse left the window.
            // Checks overlayOpen, not overlayF: during fade-out the box is
            // still visible, but the mouse should control the chart again.
            if (g_Ctx.overlayOpen) {
                OverlayRects orr;
                OverlayLayout(rc.right, rc.bottom, &orr);
                int hot = OverlayHit(&orr, mx, my);
                if (hot != g_Ctx.overlayHot) {
                    g_Ctx.overlayHot = hot;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }

            if (g_Ctx.panning) {
                BOOL atWall = FALSE;
                EnterCriticalSection(&g_Ctx.lock);
                // The anchor must be compensated BEFORE it is read: a
                // backfill between the previous timer tick and this mouse
                // move would otherwise give one frame with a jump of k
                // candles (phase 18).
                ApplyFrontShift(&g_Ctx);
                int vs2, vc2;
                GetView(&g_Ctx, &vs2, &vc2);
                if (vc2 > 0 && g.cw > 0) {
                    double slot = (double)g.cw / (double)vc2;
                    int shift = (int)((double)(mx - g_Ctx.panAnchorX) / slot);
                    int want  = g_Ctx.panAnchorView - shift;        // drag right = backward
                    g_Ctx.viewStart = want;
                    ClampView(&g_Ctx);
                    // At the wall the finger slips: the anchor is moved here,
                    // so the overshoot is not remembered. Without this the
                    // drag after a backfill (phase 18) would jump by exactly
                    // what the user dragged past the wall before the candles
                    // arrived, and a drag back from the wall would stand
                    // still for just as long.
                    if (g_Ctx.viewStart != want) {
                        g_Ctx.panAnchorView = g_Ctx.viewStart;
                        g_Ctx.panAnchorX    = mx;
                    }
                    g_Ctx.followLive =
                        (g_Ctx.viewStart + g_Ctx.viewCount >= g_Ctx.candleCount);
                    atWall = (g_Ctx.viewStart == 0);
                    // Drag panning is NOT eased in X. The finger and the
                    // chart must stay together; an eased drag feels sluggish,
                    // not smooth. The Y axis is still eased - it should glide
                    // when new highs and lows enter the view.
                    g_Ctx.dispStart = (double)g_Ctx.viewStart;
                    g_Ctx.dispCount = (double)((g_Ctx.viewCount > 0)
                                               ? g_Ctx.viewCount : g_Ctx.candleCount);
                    g_Ctx.hoverIdx = HitCandle(&g_Ctx, &g, mx, my);
                    g_Ctx.hoverY   = my;
                }
                LeaveCriticalSection(&g_Ctx.lock);
                if (atWall) RequestHistory(&g_Ctx);   // phase 18
                StartAnim(hwnd);   // the Y axis may have a new target
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            EnterCriticalSection(&g_Ctx.lock);
            int idx = HitCandle(&g_Ctx, &g, mx, my);
            LeaveCriticalSection(&g_Ctx.lock);

            if (idx != g_Ctx.hoverIdx || (idx >= 0 && my != g_Ctx.hoverY)) {
                g_Ctx.hoverIdx = idx;
                g_Ctx.hoverY   = my;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        // Ctrl + mouse wheel zooms about the point under the cursor. Note
        // that lParam here is SCREEN coordinates, unlike WM_MOUSEMOVE.
        case WM_MOUSEWHEEL: {
            if (g_Ctx.overlayOpen) return 0;
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);   // lParam is SCREEN coordinates here

            RECT rc;
            GetClientRect(hwnd, &rc);
            ChartRect g = ChartGeometry(rc.right, rc.bottom);
            if (g.cw <= 0) return 0;

            BOOL ctrl   = (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0;
            int notches = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;

            BOOL atWall = FALSE;
            EnterCriticalSection(&g_Ctx.lock);
            if (g_Ctx.candleCount > 0) {
                if (!ctrl) {
                    // Without Ctrl: pan in time. Wheel up = backward.
                    int vs, vc;
                    GetView(&g_Ctx, &vs, &vc);
                    int step = vc / 8;
                    if (step < 1) step = 1;
                    atWall = PanView(&g_Ctx, -notches * step);
                } else {
                    // With Ctrl: zoom about the point under the cursor
                    double frac = (double)(pt.x - g.left) / (double)g.cw;
                    atWall = ZoomView(&g_Ctx, frac, notches);
                }
                g_Ctx.hoverIdx = HitCandle(&g_Ctx, &g, pt.x, pt.y);
            }
            LeaveCriticalSection(&g_Ctx.lock);

            // The wall (phase 18). Zooming in with the anchor at the far left
            // on a fresh panel also lands here - one call of 50 KB, harmless.
            if (atWall) RequestHistory(&g_Ctx);
            StartAnim(hwnd);   // the target moved; the display should ease there
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_MOUSELEAVE:
            g_Ctx.trackingMouse = FALSE;
            g_Ctx.hoverIdx      = -1;
            g_Ctx.overlayHot    = -1;   // otherwise a row stays highlighted
            // Also fires when the cursor goes from a button (HTCLIENT) out
            // onto free header area (HTCAPTION): it leaves the client area
            // without leaving the window. Without this the button stays lit.
            g_Ctx.btnHot        = -1;
            g_Ctx.tbHot         = -1;
            g_Ctx.alertHot      = -1;   // phase 23: the price column is right
            g_Ctx.axisHotY      = -1;   // at the edge, the cursor often exits here
            g_Ctx.alertFresh    = 0.0;
            StartAnim(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

#ifdef TICKER_PROBE
        // Test build (phase 18): reads internal state without touching it,
        // so a probe can wait for a backfill to land and check that the view
        // points at the same candles before and after. Built only with
        // /DTICKER_PROBE; the production build does not have the message.
        case WM_APP_PROBE: {
            LRESULT r = -1;
            EnterCriticalSection(&g_Ctx.lock);
            int pvs, pvc;
            GetView(&g_Ctx, &pvs, &pvc);
            switch (wParam) {
                case 0:  r = g_Ctx.candleCount; break;
                case 1:  r = pvs; break;
                case 2:  r = pvc; break;
                case 3:  r = (LRESULT)g_Ctx.frontShift; break;
                case 4:  r = g_Ctx.histDone; break;
                case 5:  r = g_Ctx.histPending; break;
                case 6:  r = (g_Ctx.candleCount > 0)
                             ? (LRESULT)(g_Ctx.candles[0].openTime / 1000) : 0; break;
                case 7:  r = (pvs < g_Ctx.candleCount)
                             ? (LRESULT)(g_Ctx.candles[pvs].openTime / 1000) : 0; break;
                case 8:  r = (LRESULT)(g_Ctx.dispStart * 1000.0); break;   // thousandths of a candle
                case 9:  r = g_Ctx.netFailures; break;
                case 10: r = g_Ctx.followLive; break;
                case 11: r = g_Ctx.hoverIdx; break;
                case 12: r = g_Ctx.panning; break;   // phase 20
                case 13: r = (GetCapture() == hwnd); break;   // phase 20, own thread
                // Phase 21. lParam is the candle index. The volume is
                // multiplied by 100: LRESULT is 32 bit on x86, and the probe
                // runs BTC (1m volume in the tens), so two decimals fit with
                // a good margin.
                case 14: r = ((int)lParam >= 0 && (int)lParam < g_Ctx.candleCount)
                             ? (LRESULT)floor(g_Ctx.candles[(int)lParam].volume * 100.0 + 0.5) : -1; break;   // rounded, not truncated (pitfall 91)
                case 15: r = (LRESULT)g_probePaintUs; break;
                // Phase 22: the toolbar.
                case 16: r = g_Ctx.ivIdx; break;
                case 17: r = g_Ctx.symIdx; break;
                case 18: r = ShowVolNow(&g_Ctx); break;   // the mode's choice (phase 26)
                case 19: r = g_Ctx.tbHot; break;
                case 20: r = g_Ctx.overlayOpen; break;
                case 21: r = (LRESULT)(g_Ctx.dispVolF * 1000.0); break;
                // Phase 23: price alerts. Levels and prices are multiplied by
                // 100 for the same reason as the volume; BTC x 100 is seven
                // digits. 23 carries the sign (the side), lParam is the slot.
                // The WRITING fields (100 and up) live in WndProc, on the
                // main window, so an alert can be fired with the panel hidden.
                case 22: r = g_Ctx.alertCount[g_Ctx.symIdx]; break;
                case 23: r = ((int)lParam >= 0 && (int)lParam < g_Ctx.alertCount[g_Ctx.symIdx])
                             ? (LRESULT)floor(g_Ctx.alerts[g_Ctx.symIdx][(int)lParam] * 100.0 + 0.5) : 0; break;
                case 24: r = g_Ctx.alertFired; break;
                case 25: r = (LRESULT)floor(g_Ctx.alertLastFired * 100.0 + 0.5); break;
                case 26: r = g_Ctx.alertHot; break;
                case 27: r = g_Ctx.axisHotY; break;
                case 28: r = (LRESULT)(g_Ctx.alertFlashF * 1000.0); break;
                case 29: r = g_Ctx.alertNotifyOk; break;
                case 30: r = (LRESULT)floor(g_Ctx.lastPrice * 100.0 + 0.5); break;
                case 31: r = (LRESULT)floor(g_Ctx.dispMin * 100.0 + 0.5); break;
                case 32: r = (LRESULT)floor(g_Ctx.dispMax * 100.0 + 0.5); break;
                case 33: r = (LRESULT)floor(g_Ctx.alertFresh * 100.0 + 0.5); break;
                // Phase 25: moving averages. 36/37 are SMA/EMA at the candle
                // index in lParam, x100, -1 when the average is not defined
                // there. 38 is the close x100, so the probe can compute the
                // averages itself. 39 is the time the two lines took in the
                // last repaint (us).
                case 34: r = ShowIndNow(&g_Ctx); break;
                case 35: r = (LRESULT)(g_Ctx.dispIndF * 1000.0); break;
                case 36: case 37: {
                    double iv = 0.0;
                    BOOL isE = (wParam == 37);
                    if (IndValueAt(g_Ctx.candles, g_Ctx.candleCount,
                                   isE ? IND_EMA_PERIOD : IND_SMA_PERIOD, isE,
                                   (int)lParam, &iv))
                        r = (LRESULT)floor(iv * 100.0 + 0.5);
                    break;
                }
                case 39: r = (LRESULT)g_probeIndUs; break;
                // Phase 26: the height the stamp font is built for (desktop mode).
                case 40: r = g_Ctx.pillFontH; break;
                // 104 (phase 34): WRITING. Sets the hover as a mouse move at
                // client (LOWORD, HIWORD) would, without a pointer in the
                // window - WM_MOUSELEAVE clears a posted move within a tick,
                // and a golden capture needs the crosshair to stand still.
                case 104: {
                    RECT rcH;
                    GetClientRect(hwnd, &rcH);
                    ChartRect gH = ChartGeometry(rcH.right, rcH.bottom);
                    g_Ctx.hoverIdx = HitCandle(&g_Ctx, &gH, LOWORD(lParam), HIWORD(lParam));
                    g_Ctx.hoverY   = HIWORD(lParam);
                    InvalidateRect(hwnd, NULL, FALSE);
                    r = g_Ctx.hoverIdx;
                    break;
                }
                // Phase 27: today's session. 41 is VWAP at the candle index
                // in lParam, x100, -1 when it is not defined there. 42/43 are
                // today's high/low x100. 44 is the session's first candle
                // (-1 = no session), 46 whether the buffer reaches back to
                // the day rollover. 45 is the time the session blocks took in
                // the last repaint (us). 47-50 are the candle in lParam, so
                // the probe can compute everything itself: open time
                // (seconds), typical price (H + L + C) / 3, high and low, the
                // prices x100.
                case 41: case 42: case 43: case 44: case 46: {
                    BOOL full = FALSE;
                    int ss = SessionStart(g_Ctx.candles, g_Ctx.candleCount,
                                          g_Ctx.intervalMs, g_Ctx.histDone, &full);
                    if (wParam == 44) { r = ss; break; }
                    if (wParam == 46) { r = full; break; }
                    if (ss < 0) break;
                    if (wParam == 41) {
                        double vv = 0.0;
                        if (VwapValueAt(g_Ctx.candles, g_Ctx.candleCount, g_Ctx.intervalMs,
                                        g_Ctx.histDone, (int)lParam, &vv))
                            r = (LRESULT)floor(vv * 100.0 + 0.5);
                    } else {
                        double shi, slo;
                        SessionHiLo(g_Ctx.candles, ss, g_Ctx.candleCount, &shi, &slo);
                        r = (LRESULT)floor((wParam == 42 ? shi : slo) * 100.0 + 0.5);
                    }
                    break;
                }
                case 45: r = (LRESULT)g_probeSessUs; break;
                // Phase 28: yesterday. 51/52/53 are the previous UTC day's
                // high, low and close x100, -1 when the day is not wholly in
                // the buffer. 55 is yesterday's first candle (-1 = does not
                // exist). 54 is what WM_APP_DATA decides: does the app want
                // older candles to make the sessions whole? Probes wait for
                // 54 == 0 and field 5 == 0 before they read indices. 56 is
                // the time the yesterday block took in the last repaint (us).
                case 51: case 52: case 53: case 55: {
                    int pe = -1;
                    BOOL pfull = FALSE;
                    int ps = PrevSession(g_Ctx.candles, g_Ctx.candleCount,
                                         g_Ctx.intervalMs, g_Ctx.histDone, &pe, &pfull);
                    if (wParam == 55) { r = ps; break; }
                    if (ps < 0 || !pfull) break;
                    if (wParam == 53) {
                        r = (LRESULT)floor(g_Ctx.candles[pe - 1].close * 100.0 + 0.5);
                    } else {
                        double phi, plo;
                        SessionHiLo(g_Ctx.candles, ps, pe, &phi, &plo);
                        r = (LRESULT)floor((wParam == 51 ? phi : plo) * 100.0 + 0.5);
                    }
                    break;
                }
                case 54: r = ShowIndNow(&g_Ctx) &&
                             SessionsNeedHistory(g_Ctx.candles, g_Ctx.candleCount,
                                                 g_Ctx.intervalMs, g_Ctx.histDone); break;
                case 56: r = (LRESULT)g_probePrevUs; break;
                case 57: r = g_probeLblMask; break;
                case 58: r = g_probeCrossTag; break;
                case 59: r = (LRESULT)g_probeLblUs; break;
                case 47: r = ((int)lParam >= 0 && (int)lParam < g_Ctx.candleCount)
                             ? (LRESULT)(g_Ctx.candles[(int)lParam].openTime / 1000) : -1; break;
                case 48: {
                    int ti = (int)lParam;
                    if (ti >= 0 && ti < g_Ctx.candleCount) {
                        const Candle* tc = &g_Ctx.candles[ti];
                        r = (LRESULT)floor((tc->high + tc->low + tc->close) / 3.0 * 100.0 + 0.5);
                    }
                    break;
                }
                case 49: case 50:
                    if ((int)lParam >= 0 && (int)lParam < g_Ctx.candleCount) {
                        const Candle* tc = &g_Ctx.candles[(int)lParam];
                        r = (LRESULT)floor((wParam == 49 ? tc->high : tc->low) * 100.0 + 0.5);
                    }
                    break;
                case 38: r = ((int)lParam >= 0 && (int)lParam < g_Ctx.candleCount)
                             ? (LRESULT)floor(g_Ctx.candles[(int)lParam].close * 100.0 + 0.5) : -1; break;
                default: break;
            }
            LeaveCriticalSection(&g_Ctx.lock);
            return r;
        }
#endif

        // The animation clock. Drives everything time-dependent from one
        // place, and dies once everything has settled - at rest no timer runs.
        case WM_TIMER:
            if (wParam == TIMER_ANIM_ID) {
                ULONGLONG now = GetTickCount64();
                double dt = (double)(now - g_Ctx.lastAnimTick);
                g_Ctx.lastAnimTick = now;

                BOOL redraw  = FALSE;
                BOOL settled = TRUE;

                // Overlay fade.
                double ovlTarget = g_Ctx.overlayOpen ? 255.0 : 0.0;
                if (g_Ctx.overlayF != ovlTarget) {
                    double before = g_Ctx.overlayF;
                    g_Ctx.overlayF = AnimStep(g_Ctx.overlayF, ovlTarget, dt,
                                              ANIM_TAU_FADE, 0.5);
                    if ((int)(before + 0.5) != (int)(g_Ctx.overlayF + 0.5)) redraw = TRUE;
                    if (g_Ctx.overlayF != ovlTarget) settled = FALSE;
                }

                // The afterglow of an alert that has fired (phase 23): 1 -> 0.
                // Snaps at 0.02: five of 242 color steps above CLR_BG in the
                // strongest channel (red), so the last jump is not visible.
                if (g_Ctx.alertFlashF > 0.0) {
                    g_Ctx.alertFlashF = AnimStep(g_Ctx.alertFlashF, 0.0, dt,
                                                 ALERT_TAU_FLASH, 0.02);
                    redraw = TRUE;
                    if (g_Ctx.alertFlashF > 0.0) settled = FALSE;
                }

                // The MA toggle (phase 25): dispIndF fades the lines and
                // the legend towards 0 or 1. Snaps at 0.02 like the
                // afterglow - the last color steps above CLR_BG are not visible.
                {
                    double ifT = ShowIndNow(&g_Ctx) ? 1.0 : 0.0;
                    if (g_Ctx.dispIndF != ifT) {
                        g_Ctx.dispIndF = AnimStep(g_Ctx.dispIndF, ifT, dt,
                                                  IND_TAU_FADE, 0.02);
                        redraw = TRUE;
                        if (g_Ctx.dispIndF != ifT) settled = FALSE;
                    }
                }

                // View and Y-axis easing. The target is read under the lock;
                // the interpolation itself happens outside, on UI-owned fields.
                //
                // The threshold is a quarter pixel converted to the unit being
                // eased - hence the chart geometry is needed here.
                {
                    RECT rcE;
                    GetClientRect(hwnd, &rcE);
                    ChartRect gE = ChartGeometry(rcE.right, rcE.bottom);

                    int tvs = 0, tvc = 0, tn = 0;
                    double tMin = 0.0, tMax = 1.0, tVol = 0.0;
                    EnterCriticalSection(&g_Ctx.lock);
                    ApplyFrontShift(&g_Ctx);
                    tn = g_Ctx.candleCount;
                    GetView(&g_Ctx, &tvs, &tvc);
                    if (tn > 0 && tvc > 0) {
                        PriceRange(&g_Ctx, tvs, tvc, &tMin, &tMax);
                        tVol = VolumeMax(&g_Ctx, tvs, tvc);   // phase 21
                    }
                    LeaveCriticalSection(&g_Ctx.lock);

                    // The VOL toggle (phase 22): dispVolF eases towards 0 or 1,
                    // regardless of whether there are candles - a toggle just
                    // before an interval switch must also settle. The snap is a
                    // quarter pixel of the band height, as for the others.
                    {
                        double vfT = ShowVolNow(&g_Ctx) ? 1.0 : 0.0;
                        if (g_Ctx.dispVolF != vfT) {
                            double bandPx = (double)gE.ch * VOL_FRAC;
                            double snapF  = (bandPx > 1.0) ? SNAP_PX / bandPx : 1.0;
                            g_Ctx.dispVolF = AnimStep(g_Ctx.dispVolF, vfT, dt,
                                                      ANIM_TAU_VIEW, snapF);
                            redraw = TRUE;
                            if (g_Ctx.dispVolF != vfT) settled = FALSE;
                        }
                    }

                    if (tn > 0 && tvc > 0 && g_Ctx.dispValid &&
                        gE.cw > 0 && gE.ch > 0) {
                        double dc = (g_Ctx.dispCount > 1.0) ? g_Ctx.dispCount : 1.0;
                        double snapX = SNAP_PX * dc / (double)gE.cw;
                        double snapY = SNAP_PX * (tMax - tMin) / (double)gE.ch;
                        if (snapX <= 0.0) snapX = 1e-9;
                        if (snapY <= 0.0) snapY = 1e-9;

                        // The volume scale eases like the price axis: a quarter
                        // pixel of the band height in volume units (phase 21).
                        // Without easing the bars would jump the moment a larger
                        // candle entered the view, while the candles glide.
                        double snapV = SNAP_PX * tVol / ((double)gE.ch * VOL_FRAC);
                        if (snapV <= 0.0) snapV = 1e-9;

                        struct { double* v; double t; double snap; } eases[5] = {
                            { &g_Ctx.dispStart,  (double)tvs, snapX },
                            { &g_Ctx.dispCount,  (double)tvc, snapX },
                            { &g_Ctx.dispMin,    tMin,        snapY },
                            { &g_Ctx.dispMax,    tMax,        snapY },
                            { &g_Ctx.dispVolMax, tVol,        snapV },
                        };
                        for (int e = 0; e < 5; ++e) {
                            if (*eases[e].v == eases[e].t) continue;
                            *eases[e].v = AnimStep(*eases[e].v, eases[e].t, dt,
                                                   ANIM_TAU_VIEW, eases[e].snap);
                            redraw = TRUE;
                            if (*eases[e].v != eases[e].t) settled = FALSE;
                        }
                    }
                }

                // The stale counter. The clock must run while we are
                // disconnected, but the text only changes once a second - so
                // we repaint only when the digit actually changes.
                ULONGLONG okTick;
                EnterCriticalSection(&g_Ctx.lock);
                okTick = g_Ctx.lastOkTick;
                LeaveCriticalSection(&g_Ctx.lock);

                // IsWindowVisible is decisive: without it a disconnected
                // line keeps the clock alive on a hidden panel, and we tick 60
                // times a second without painting anything. TogglePopup starts
                // it again when the panel is shown.
                if (okTick != 0 && now - okTick > STALE_AFTER &&
                    IsWindowVisible(hwnd)) {
                    int secs = (int)((now - okTick) / 1000);
                    if (secs != g_Ctx.staleSecsShown) {
                        g_Ctx.staleSecsShown = secs;
                        redraw = TRUE;
                    }
                    settled = FALSE;   // keep the clock alive while we are away
                }

                if (redraw) InvalidateRect(hwnd, NULL, FALSE);
                if (settled) {
                    KillTimer(hwnd, TIMER_ANIM_ID);
                    g_Ctx.animRunning = FALSE;
                }
            }
            return 0;

        // A double-click on the chart or the price axis resets zoom and
        // panning. Requires CS_DBLCLKS on the window class - without it the
        // message never arrives. Everything that is not chart or axis - the
        // buttons, and the whole panel while the overlay is open - falls
        // through to WM_LBUTTONDOWN, so the second click of a fast
        // double-click behaves as it did before CS_DBLCLKS came in.
        //
        // The first click has already started panning, but WM_LBUTTONUP
        // has released it again before DBLCLK arrives. Free header area is
        // HTCAPTION and gives WM_NCLBUTTONDBLCLK (maximize) - it never gets here.
        case WM_LBUTTONDBLCLK: {
            if (!g_Ctx.overlayOpen) {
                RECT rcD;
                GetClientRect(hwnd, &rcD);
                ChartRect gd = ChartGeometry(rcD.right, rcD.bottom);
                int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
                // [g.left, edge]: the chart and the headroom. Up to and
                // including phase 22 the area went all the way to W, with the
                // axis margin. The price column now belongs to the alerts
                // (phase 23) and falls through to WM_LBUTTONDOWN like the
                // buttons: there the second click of a fast double-click is
                // one more click on the mark the first one set.
                if (mx >= gd.left && mx <= gd.edge && my >= gd.top && my <= gd.bottom) {
                    ResetView(&g_Ctx);
                    EnterCriticalSection(&g_Ctx.lock);
                    g_Ctx.hoverIdx = HitCandle(&g_Ctx, &gd, mx, my);
                    g_Ctx.hoverY   = my;
                    LeaveCriticalSection(&g_Ctx.lock);
                    StartAnim(hwnd);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
        }
        // fall through
        case WM_LBUTTONDOWN: {
            // First on purpose: while the overlay is open no part of the
            // panel may take the click - not even the close cross. The first
            // click closes the overlay, the next closes the panel.
            if (g_Ctx.overlayOpen) {
                RECT rcO;
                GetClientRect(hwnd, &rcO);
                OverlayRects orr;
                OverlayLayout(rcO.right, rcO.bottom, &orr);
                int hit = OverlayHit(&orr, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                if (hit >= 0) ApplyConfigChoice(&g_Ctx, hit);
                g_Ctx.overlayOpen = FALSE;   // a click outside closes without change
                g_Ctx.overlayHot  = -1;
                StartAnim(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            RECT rc;
            GetClientRect(hwnd, &rc);
            int dx = GET_X_LPARAM(lParam), dy = GET_Y_LPARAM(lParam);
            // The buttons. After the overlay - the first click closes the
            // overlay, even when it hits a button - and before panning, which
            // in any case only applies to the chart area.
            {
                RECT btns[BTN_COUNT];
                ButtonLayout(rc.right, btns);
                int bh = ButtonHit(btns, dx, dy);
                if (bh >= 0) {
                    OnButtonClick(hwnd, bh);
                    return 0;
                }
            }
            // The toolbar (phase 22), same place in the order.
            {
                RECT tb[TBAR_COUNT];
                ToolbarLayout(rc.right, tb);
                int th = ToolbarHit(tb, dx, dy);
                if (th >= 0) {
                    OnToolbarClick(hwnd, th);
                    return 0;
                }
            }

            ChartRect gg = ChartGeometry(rc.right, rc.bottom);
            // The price column (phase 23): set or remove an alert. Same
            // area as the hover block in WM_MOUSEMOVE, and same data requirement.
            if (g_Ctx.dispValid && dx > gg.edge && dy >= gg.top && dy <= gg.bottom) {
                OnAxisClick(hwnd, &gg, dy);
                return 0;
            }
            if (dx >= gg.left && dx < gg.right && dy >= gg.top && dy <= gg.bottom) {
                // Start panning. SetCapture ensures we get the mouse release
                // even if the pointer leaves the window along the way.
                int vs, vc;
                EnterCriticalSection(&g_Ctx.lock);
                GetView(&g_Ctx, &vs, &vc);
                g_Ctx.viewCount     = vc;
                LeaveCriticalSection(&g_Ctx.lock);
                g_Ctx.panning       = TRUE;
                g_Ctx.alertHot      = -1;   // phase 23: the drag owns the mouse
                g_Ctx.axisHotY      = -1;
                g_Ctx.panAnchorX    = dx;
                g_Ctx.panAnchorView = vs;
                SetCapture(hwnd);
                // WM_SETCURSOR only fires on the next mouse move. Without
                // this call the first frame of the drag still shows the arrow.
                SetCursor(g_Ctx.curPan);
                return 0;
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            ChartRect gg = ChartGeometry(rc.right, rc.bottom);
            int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
            if (!g_Ctx.overlayOpen &&
                mx >= gg.left && mx < gg.right && my >= gg.top && my <= gg.bottom) {
                g_Ctx.overlayOpen = TRUE;
                g_Ctx.overlayHot  = -1;
                g_Ctx.hoverIdx    = -1;   // the crosshair must not remain underneath
                StartAnim(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONUP:
            if (g_Ctx.panning) {
                g_Ctx.panning = FALSE;
                ReleaseCapture();
                // Same reason as at the start, the other way round: without
                // this the first frame after release still shows the four-way arrow.
                SetCursor(g_Ctx.curArrow);
            }
            return 0;

        // Capture taken from us in the middle of a drag (phase 20): Alt+Tab,
        // the Win key, a menu or another window calling SetCapture.
        // WM_LBUTTONUP then never arrives here, and panning stayed TRUE until
        // the next click in the chart - with the phase 19 shortcuts blocked
        // for that long. Measured in the probe: pan=1 after Alt+Tab, the
        // mouse release went to another window.
        //
        // Our own ReleaseCapture in WM_LBUTTONUP also sends this one, but then
        // panning is already FALSE, and lParam is the window taking over -
        // both make the handler a no-op there. No ReleaseCapture in here:
        // it is documented as forbidden in this message.
        case WM_CAPTURECHANGED:
            if (g_Ctx.panning && (HWND)lParam != hwnd) {
                g_Ctx.panning = FALSE;
                SetCursor(g_Ctx.curArrow);
            }
            return 0;

        case WM_KEYDOWN: {
            BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            // Ctrl+0: back to factory geometry, centered on the monitor the
            // window is on. Window geometry, not zoom - see R below.
            if (wParam == '0' && ctrl) {
                ResetToDefaultView(hwnd);
                return 0;
            }
            // The control buttons from the keyboard (phase 19): Ctrl+N = [ + ],
            // Ctrl+M = minimize, F11 = maximize/restore, Ctrl+W = close.
            // Alt+F4 already goes through DefWindowProc to WM_CLOSE, even
            // without WS_SYSMENU - measured. All four go through OnButtonClick,
            // so key and click share the same path: the geometry is saved
            // before maximizing, a restored rect outside everything visible is
            // caught, and a duplicate exits on close. Not in the middle of
            // panning: a minimize during a drag would skip WM_LBUTTONUP, which
            // releases capture and restores the pointer. Not in desktop mode:
            // the surface is a child of WorkerW and never gets keyboard focus,
            // but SW_MINIMIZE on it should not even be possible in theory.
            if (!g_desktopMode && !g_Ctx.panning) {
                int bh = -1;
                if (ctrl && wParam == 'N')      bh = BTN_NEW;
                else if (ctrl && wParam == 'M') bh = BTN_MIN;
                else if (ctrl && wParam == 'W') bh = BTN_CLOSE;
                else if (!ctrl && wParam == VK_F11) bh = BTN_MAX;
                if (bh >= 0) {
                    OnButtonClick(hwnd, bh);
                    return 0;
                }
            }
            // Navigation in the chart (phase 20), through the same
            // PanView/ZoomView as the wheel. Left/right is one wheel notch
            // (vc / 8 candles), PgUp/PgDn a whole view, Home the oldest candle
            // (the wall requests history as a drag would, phase 18), End the
            // live edge. + and - are one zoom step about the MIDDLE of the
            // view - the wheel zooms about the pointer, but a key has no
            // pointer. Both the main keyboard's OEM codes and the numeric
            // keypad's. Ctrl is allowed on + and - (Ctrl++ as in a browser),
            // but not on the others, so Ctrl+arrow stays free.
            //
            // Same blocks as the wheel and the shortcuts above: not with the
            // overlay open (it owns the wheel and R), not in desktop mode, not
            // in the middle of panning (the anchor would jump). Hover is reset
            // as R does: recomputing the candle under the pointer would let
            // the crosshair glide with the candle during the easing and stay
            // offset from the pointer until the next mouse move.
            if (!g_desktopMode && !g_Ctx.panning && !g_Ctx.overlayOpen) {
                // The toolbar from the keyboard (phase 22): V is the VOL pill,
                // 1..6 the interval pills in order. Same path as the click.
                // Without Ctrl - Ctrl+0 is the default view, and Ctrl+digit
                // stays free.
                if (!ctrl && wParam == 'V') {
                    OnToolbarClick(hwnd, TBAR_VOL);
                    return 0;
                }
                // M (phase 25): the MA pill. Without Ctrl - Ctrl+M minimizes.
                // Also works when the pill is hidden on a narrow panel.
                if (!ctrl && wParam == 'M') {
                    OnToolbarClick(hwnd, TBAR_IND);
                    return 0;
                }
                if (!ctrl && wParam >= '1' && wParam < (WPARAM)('1' + INTERVAL_COUNT)) {
                    OnToolbarClick(hwnd, TBAR_IV_FIRST + (int)(wParam - '1'));
                    return 0;
                }
                // A (phase 23): set an alert at the crosshair's price - the
                // same number shown in the label on the axis, rounded as a
                // click in the column would. Without a crosshair there is no
                // price to point at, and the key does nothing. hoverY is
                // clamped as in DrawChart: the crosshair is never drawn outside
                // [top, bottom], so the alert must not end up there either.
                if (!ctrl && wParam == 'A') {
                    if (g_Ctx.hoverIdx >= 0 && g_Ctx.dispValid) {
                        RECT rcA;
                        GetClientRect(hwnd, &rcA);
                        ChartRect ga = ChartGeometry(rcA.right, rcA.bottom);
                        int hy = g_Ctx.hoverY;
                        if (hy < ga.top)    hy = ga.top;
                        if (hy > ga.bottom) hy = ga.bottom;
                        AlertAdd(&g_Ctx, AlertPriceAtY(&g_Ctx, &ga, hy));
                    }
                    return 0;
                }
                int  pan = 0, zoom = 0, navKey = 1;
                switch (wParam) {
                    case VK_LEFT:       pan = -1; break;    // notch
                    case VK_RIGHT:      pan = +1; break;
                    case VK_PRIOR:      pan = -2; break;    // whole view
                    case VK_NEXT:       pan = +2; break;
                    case VK_HOME:       pan = -3; break;    // to the wall
                    case VK_END:        pan = +3; break;    // to the edge
                    case VK_OEM_PLUS:   case VK_ADD:      zoom = +1; break;
                    case VK_OEM_MINUS:  case VK_SUBTRACT: zoom = -1; break;
                    default:            navKey = 0; break;
                }
                if (navKey && (zoom != 0 || !ctrl)) {
                    BOOL atWall = FALSE;
                    EnterCriticalSection(&g_Ctx.lock);
                    if (g_Ctx.candleCount > 0) {
                        if (zoom != 0) {
                            atWall = ZoomView(&g_Ctx, 0.5, zoom);
                        } else {
                            int vs, vc;
                            GetView(&g_Ctx, &vs, &vc);
                            int step = vc / 8;
                            if (step < 1) step = 1;
                            int delta = 0;
                            if (pan == -1 || pan == 1) delta = pan * step;
                            else if (pan == -2)        delta = -vc;
                            else if (pan == 2)         delta = vc;
                            else if (pan == -3)        delta = -g_Ctx.candleCount;
                            else                       delta = g_Ctx.candleCount;
                            atWall = PanView(&g_Ctx, delta);
                        }
                        g_Ctx.hoverIdx = -1;
                    }
                    LeaveCriticalSection(&g_Ctx.lock);
                    if (atWall) RequestHistory(&g_Ctx);
                    StartAnim(hwnd);   // the target moved; the display eases there
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
            // ESC works in layers, innermost first: close the overlay, reset
            // the view, hide the panel. No layer disappears for another -
            // whoever wants to hide a zoomed panel presses twice.
            if (wParam == VK_ESCAPE && g_Ctx.overlayOpen) {
                g_Ctx.overlayOpen = FALSE;
                g_Ctx.overlayHot  = -1;
                StartAnim(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            // R: zoom and panning back to the default view. Not while the
            // overlay is open - there it owns the keyboard, like the wheel.
            // The VK codes for letters are the upper-case ASCII characters.
            if ((wParam == 'R' && !ctrl && !g_Ctx.overlayOpen) ||
                (wParam == VK_ESCAPE && !ViewIsDefault(&g_Ctx))) {
                ResetView(&g_Ctx);
                g_Ctx.hoverIdx = -1;
                StartAnim(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                // The cross in the header is the obvious alternative, but
                // the keyboard shortcut is cheap to keep.
                HidePanel(hwnd);
            }
            return 0;
        }

        case WM_CLOSE:
            // The close button hides to the notification area. The ticker is
            // a tray program; "Quit TickC" in the tray menu exits it.
            // The position is saved before we disappear. A duplicate exits.
            HidePanel(hwnd);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Place the popup in the corner of the work area nearest the mouse pointer.
// Handles a taskbar on any edge + multiple monitors.


// Places the window the first time it is created: the saved position if it
// exists and is still visible, otherwise centered.
static void PlacePopupInitially(HWND hwnd) {
    int w = (g_savedPanelW > 0) ? g_savedPanelW : POPUP_W;
    int h = (g_savedPanelH > 0) ? g_savedPanelH : POPUP_H;

    if (g_savedPanelX != GEOM_UNSET && g_savedPanelY != GEOM_UNSET &&
        PlacementIsVisible(g_savedPanelX, g_savedPanelY, w, h)) {
        SetWindowPos(hwnd, NULL, g_savedPanelX, g_savedPanelY, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }
    ResetToDefaultView(hwnd);
}

// Windows refuses SetForegroundWindow from a process that does not own
// the foreground. Without this the window is shown but never activated - and
// keyboard focus ends up nowhere, so Ctrl+0 and ESC never get through.
// The fix is to attach our input queue to the foreground thread while we switch.
static void ForceForeground(HWND hwnd) {
    HWND hFore = GetForegroundWindow();
    if (hFore == hwnd) return;

    DWORD thisThread = GetCurrentThreadId();
    DWORD foreThread = hFore ? GetWindowThreadProcessId(hFore, NULL) : 0;

    if (foreThread && foreThread != thisThread) {
        AttachThreadInput(foreThread, thisThread, TRUE);
        SetForegroundWindow(hwnd);
        SetActiveWindow(hwnd);
        SetFocus(hwnd);
        AttachThreadInput(foreThread, thisThread, FALSE);
    } else {
        SetForegroundWindow(hwnd);
        SetActiveWindow(hwnd);
        SetFocus(hwnd);
    }
}

// --- Desktop mode -----------------------------------------------------------

// Undocumented: makes Progman create the WorkerW window that the desktop's
// wallpaper transition is drawn in.
#define PROGMAN_SPAWN_WORKERW 0x052C

// Classic window tree (before Windows 11 24H2): the WorkerW we want is a
// TOP-LEVEL window, the sibling right after the window that has
// SHELLDLL_DefView (the icons) in it.
static BOOL CALLBACK FindLegacyWorkerW(HWND top, LPARAM lParam) {
    if (FindWindowExW(top, NULL, L"SHELLDLL_DefView", NULL)) {
        *(HWND*)lParam = FindWindowExW(NULL, top, L"WorkerW", NULL);
        return FALSE;
    }
    return TRUE;
}

// Finds the WorkerW the surface is to live in, and creates it if necessary.
//
// Measured on 26100 (24H2): before the message Progman has one child,
// SHELLDLL_DefView. After the message it has two - DefView on top and WorkerW
// below - and there is NO top-level WorkerW with DefView in it. So the
// classic traversal finds nothing there, and is only tried when Progman does
// not have WorkerW as a child.
//
// Both message variants are sent: (0xD, 1) is the newer form, (0, 0) the
// classic one. The combination is what was measured; either alone was not.
// Timeout 1 s: a hung Explorer must not freeze our UI thread.
static HWND FindDesktopWorkerW(void) {
    HWND progman = FindWindowW(L"Progman", NULL);
    if (!progman) return NULL;

    DWORD_PTR res;
    SendMessageTimeoutW(progman, PROGMAN_SPAWN_WORKERW, 0xD, 0x1,
                        SMTO_NORMAL | SMTO_ABORTIFHUNG, 1000, &res);
    SendMessageTimeoutW(progman, PROGMAN_SPAWN_WORKERW, 0, 0,
                        SMTO_NORMAL | SMTO_ABORTIFHUNG, 1000, &res);

    HWND ww = FindWindowExW(progman, NULL, L"WorkerW", NULL);
    if (ww) return ww;

    HWND legacy = NULL;
    EnumWindows(FindLegacyWorkerW, (LPARAM)&legacy);
    return legacy;
}

// Turns a newly created WS_POPUP into the desktop surface.
//
// The order is measured, not chosen. A plain child window under WorkerW is
// NEVER visible on 24H2 - the parent has no surface to draw in (Progman
// has WS_EX_NOREDIRECTIONBITMAP). A layered child gets its own. But:
//   - WS_EX_LAYERED on a child requires supportedOS Windows 8+ in the manifest.
//     Without it the style is silently rejected (exstyle 0, 0 of 41 points visible).
//   - SetLayeredWindowAttributes must be called AFTER SetParent. Set while
//     the window was top-level, the style survives, but the surface is not shown.
// With both in place: 28 of 41 desktop points got the surface's color, and
// the rest were icons.
//
// WS_EX_TRANSPARENT lets the mouse through. The surface lies under the
// icons' SysListView32 anyway, but it has to be layered, so it costs nothing.
// Places the surface over the primary monitor. The primary monitor is at 0,0 in
// screen coordinates, but WorkerW covers the whole virtual screen and has
// its origin in that screen's corner. On a setup with a monitor to the left of
// the primary one they are not the same. Split out of AttachToDesktop in phase
// 26, so a monitor change can place the surface again without recreating it.
static void PlaceDesktopSurface(HWND hwnd, HWND ww) {
    POINT org = { 0, 0 };
    MapWindowPoints(NULL, ww, &org, 1);
    SetWindowPos(hwnd, HWND_BOTTOM, org.x, org.y,
                 GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                 SWP_NOACTIVATE);
}

static BOOL AttachToDesktop(HWND hwnd) {
    HWND ww = FindDesktopWorkerW();
    if (!ww) return FALSE;

    LONG_PTR st = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, (st & ~(LONG_PTR)WS_POPUP) | WS_CHILD);
    if (!SetParent(hwnd, ww)) return FALSE;

    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED | WS_EX_TRANSPARENT);
    if (!SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA)) return FALSE;

    PlaceDesktopSurface(hwnd, ww);
    return TRUE;
}

// The screen is different from when the surface was placed (phase 26): new
// resolution, new primary monitor, a monitor connected or disconnected.
// WM_DISPLAYCHANGE goes only to top-level windows, so it is the hidden main
// window that gets it - the surface is a child of WorkerW and hears nothing.
// Also called once more a second later: Explorer places its own WorkerW
// again after the same message, and the origin is computed in ITS
// coordinates.
//
// If the surface no longer sits in the current WorkerW, it is torn down;
// WM_NCDESTROY then starts the rebuild, the same path as when Explorer is
// restarted. Otherwise it is placed again. SetWindowPos with unchanged
// geometry is a no-op; if the size changes, WM_SIZE arrives, which discards
// the watermark, and the double buffer and the stamp font (H/40) are keyed
// on the size and rebuild themselves.
//
// Per-monitor aware around the calls, as when the surface was created (see
// TogglePopup): GetSystemMetrics follows the thread's context, and the main
// thread is DPI-unaware - without this the surface would get virtualized
// measurements.
//
// A pure SCALING change (100 % -> 150 %, same resolution) needs nothing:
// the surface works in physical pixels. The watermark's font limits read
// DPI, so it is discarded anyway.
static void RefitDesktopSurface(void) {
    if (!g_desktopMode || !g_Ctx.hPopup) return;
    DPI_AWARENESS_CONTEXT prev =
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND ww = FindDesktopWorkerW();
    if (!ww || GetParent(g_Ctx.hPopup) != ww) {
        DestroyWindow(g_Ctx.hPopup);
    } else {
        PlaceDesktopSurface(g_Ctx.hPopup, ww);
        g_Ctx.wmValid = FALSE;
        InvalidateRect(g_Ctx.hPopup, NULL, FALSE);
    }
    if (prev) SetThreadDpiAwarenessContext(prev);
}


// Tray click. With a normal window the expected behavior is: if it is in
// front and active, hide it; otherwise show it and give it focus. A
// minimized window is restored.
static void TogglePopup(AppContext* ctx, HINSTANCE hInst) {
    if (ctx->hPopup && IsWindowVisible(ctx->hPopup)) {
        if (!IsIconic(ctx->hPopup) && GetForegroundWindow() == ctx->hPopup) {
            ctx->hoverIdx = -1;
            ctx->overlayOpen = FALSE;
            ctx->overlayF    = 0.0;
            ctx->overlayHot  = -1;
            ctx->btnHot      = -1;
            ctx->tbHot       = -1;
            SaveWindowPlacement(ctx->hPopup);
            ShowWindow(ctx->hPopup, SW_HIDE);
            return;
        }
        if (IsIconic(ctx->hPopup)) ShowWindow(ctx->hPopup, SW_RESTORE);
        ForceForeground(ctx->hPopup);
        return;
    }

    BOOL created = FALSE;
    if (!ctx->hPopup) {
        // Frameless window in the TradingView/Bloomberg tradition.
        // WS_THICKFRAME keeps the OS's own resizing; the whole visible frame
        // is removed in WM_NCCALCSIZE. WS_MINIMIZEBOX and WS_MAXIMIZEBOX draw
        // nothing without a title bar, but they are what makes Win+Arrow,
        // Aero Snap and restoring from the taskbar thumbnail work.
        //
        // 0,0 and not CW_USEDEFAULT: CW_USEDEFAULT is undefined for WS_POPUP
        // and can put the window off screen. PlacePopupInitially sets the
        // correct geometry right after.
        //
        // No WS_EX_TOOLWINDOW and no owner, so the window keeps its button
        // in the taskbar. No WS_EX_TOPMOST.
        //
        // Desktop mode: WS_POPUP alone. No frame to resize by, and no
        // minimize/maximize - the surface is the desktop. AttachToDesktop
        // turns it into WS_CHILD before it is shown, and before hPopup is
        // published: the thread must not see a window that may be torn down
        // again.
        DWORD style = g_desktopMode
            ? WS_POPUP
            : (WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);

        // The desktop surface is created per-monitor aware; the rest of the
        // process is still DPI-unaware. Measured at 150 %: without this
        // SM_CXSCREEN/SM_CYSCREEN gave a virtualized 2560x1067, and the
        // surface covered only the upper left corner of a WorkerW of
        // 3840x1600 physical pixels. A window created in this context keeps
        // it, and WM_PAINT runs in the window's context - GetClientRect then
        // gives physical pixels, and the layout is drawn 1:1 in raw pixels as
        // at 100 %. The context is restored as soon as the surface is placed.
        DPI_AWARENESS_CONTEXT prevDpi = g_desktopMode
            ? SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
            : NULL;
        HWND hp = CreateWindowExW(
            0,
            L"BTCPopupClass", L"TickC",
            style,
            0, 0, POPUP_W, POPUP_H,
            NULL, NULL, hInst, NULL);
        BOOL attached = hp && g_desktopMode && AttachToDesktop(hp);
        if (prevDpi) SetThreadDpiAwarenessContext(prevDpi);
        if (!hp) return;

        if (g_desktopMode) {
            if (!attached) {
                // No WorkerW (Explorer is starting, or not running). hPopup
                // is not set, so WM_NCDESTROY leaves the timer alone - it is
                // set here.
                DestroyWindow(hp);
                SetTimer(ctx->hWnd, TIMER_EMBED_ID, EMBED_RETRY_MS, NULL);
                return;
            }
        } else {
            SquareCorners(hp);
        }

        EnterCriticalSection(&ctx->lock);   // the thread reads hPopup
        ctx->hPopup = hp;
        LeaveCriticalSection(&ctx->lock);
        created = TRUE;
    }

    ResetView(ctx);   // the view is set again; the buffer is kept

    ctx->panning   = FALSE;
    ctx->hoverIdx  = -1;
    ctx->overlayOpen = FALSE;   // the overlay must never be open on opening
    ctx->overlayF    = 0.0;
    ctx->overlayHot  = -1;
    // btnHot is reset for the same reason as overlayHot above: the state is
    // hover, and hover owns nothing when the window disappears or is opened
    // again. WM_MOUSELEAVE does fire on SW_HIDE and SW_MINIMIZE - measured,
    // neither of the two paths left a lit button - but it is a message order
    // we do not control, and a red close button lingering on reopening is
    // not worth depending on it.
    ctx->btnHot      = -1;
    ctx->alertHot    = -1;      // phase 23, same reason
    ctx->axisHotY    = -1;
    ctx->dispValid   = FALSE;   // the panel opens finished, does not glide into place

    UpdatePopupTitle(ctx);
    if (g_desktopMode) {
        // AttachToDesktop set the geometry. No activation and no
        // foreground: a child of Explorer's WorkerW must never take focus
        // from what the user is doing.
        ShowWindow(ctx->hPopup, SW_SHOWNA);
    } else {
        if (created) PlacePopupInitially(ctx->hPopup);
        ShowWindow(ctx->hPopup, SW_SHOW);
        ForceForeground(ctx->hPopup);
    }
    SetEvent(ctx->hWakeEvent);   // fetch candles now, not in up to 3 seconds

    // If the line is down as the panel opens, the clock must start here.
    // WM_TIMER lets it die while the panel is hidden, and the next
    // WM_APP_DATA can be up to a whole backoff period away - the seconds
    // counter would stand still until then.
    ULONGLONG okTick;
    EnterCriticalSection(&ctx->lock);
    okTick = ctx->lastOkTick;
    LeaveCriticalSection(&ctx->lock);
    if (okTick != 0 && GetTickCount64() - okTick > STALE_AFTER) {
        StartAnim(ctx->hPopup);
    }

    InvalidateRect(ctx->hPopup, NULL, FALSE);
}

// Switches between the panel and the desktop surface while the process runs
// (phase 12).
//
// The window is CREATED AGAIN, it is not moved with SetParent. The DPI
// context is set when a window is created and cannot be changed: the desktop
// surface must be created per-monitor aware (otherwise it covers a quarter
// of the screen at 150 %, see TogglePopup), and the panel DPI-unaware. A
// moved window would have the wrong context in one of the modes.
// TogglePopup already creates both correctly, and all the g_desktopMode
// branches apply to a new window without more work.
//
// What survives: candles[], the view, the network thread, the GDI objects,
// the double buffer and the watermark cache. The last two rebuild
// themselves if the size is different, and it nearly always is.
static void SetDesktopMode(AppContext* ctx, HWND hWnd, HINSTANCE hInst, BOOL on) {
    if (g_isDuplicate || on == g_desktopMode) return;

    // The timer tries to build a missing desktop surface. It must not fire
    // after we have gone back to the panel.
    KillTimer(hWnd, TIMER_EMBED_ID);

    if (ctx->hPopup) {
        HWND hp = ctx->hPopup;
        // Saved BEFORE the flag changes: SaveGeometry does nothing in
        // desktop mode.
        if (!g_desktopMode) SaveWindowPlacement(hp);
        if (GetCapture() == hp) ReleaseCapture();

        // Same order as WM_DESTROY in WndProc: hPopup is cleared under the
        // lock BEFORE the window is torn down. Then WM_NCDESTROY is a no-op
        // and does not start the rebuild timer.
        EnterCriticalSection(&ctx->lock);
        ctx->hPopup = NULL;
        LeaveCriticalSection(&ctx->lock);
        DestroyWindow(hp);
    }

    // State that belonged to the old window. The timer died with it;
    // TrackMouseEvent was requested for a window that does not exist, and
    // without the reset the new panel would never request WM_MOUSELEAVE.
    ctx->animRunning   = FALSE;
    ctx->trackingMouse = FALSE;
    ctx->panning       = FALSE;
    ctx->bbValid       = FALSE;
    // The watermark is keyed on (W, H, symIdx, ivIdx), not on mode, and the
    // cache lives in ctx - it survives the window being created again. Since
    // phase 14 the placement depends on the geometry, which depends on mode.
    ctx->wmValid       = FALSE;

    g_desktopMode = on;
    SaveDesktopMode(on);
    // The overlays follow the mode (phase 26). The new surface does not
    // exist yet, so the display snaps - the same rule as SetShowVolume when
    // nothing is visible.
    ctx->dispVolF = ShowVolNow(ctx) ? 1.0 : 0.0;
    ctx->dispIndF = ShowIndNow(ctx) ? 1.0 : 0.0;

    TogglePopup(ctx, hInst);
}

// The submenus for symbol and interval (phase 17): one item per table row
// and a radio check on the selected one. Attached to the main menu with
// MF_POPUP they are owned by it, so DestroyMenu on the main menu tears them
// down - no new handles at rest. The labels are the tables' label, the same
// text as the overlay.
static HMENU BuildSymbolMenu(void) {
    HMENU h = CreatePopupMenu();
    if (!h) return NULL;
    for (int i = 0; i < SYMBOL_COUNT; i++)
        AppendMenuW(h, MF_STRING, (UINT_PTR)(ID_TRAY_SYMBOL_FIRST + i), SYMBOLS[i].label);
    CheckMenuRadioItem(h, ID_TRAY_SYMBOL_FIRST, ID_TRAY_SYMBOL_FIRST + SYMBOL_COUNT - 1,
                       (UINT)(ID_TRAY_SYMBOL_FIRST + g_Ctx.symIdx), MF_BYCOMMAND);
    return h;
}

static HMENU BuildIntervalMenu(void) {
    HMENU h = CreatePopupMenu();
    if (!h) return NULL;
    for (int i = 0; i < INTERVAL_COUNT; i++)
        AppendMenuW(h, MF_STRING, (UINT_PTR)(ID_TRAY_INTERVAL_FIRST + i), INTERVALS[i].label);
    CheckMenuRadioItem(h, ID_TRAY_INTERVAL_FIRST, ID_TRAY_INTERVAL_FIRST + INTERVAL_COUNT - 1,
                       (UINT)(ID_TRAY_INTERVAL_FIRST + g_Ctx.ivIdx), MF_BYCOMMAND);
    return h;
}

// The tray menu. A separate function so the check marks and content can be
// tested without a tray icon.
//
//       Symbol            >   (o) BTC/USDT  ( ) ETH/USDT  ...
//       Interval          >   (o) 1m  ( ) 5m  ...
//   ---------------------------
//   [x] Desktop mode
//       Default view      Ctrl+0     (grayed in desktop mode)
//   ---------------------------
//   [x] Start at sign-in
//   ---------------------------
//       Quit TickC
//
// "Default view" is grayed, not gone, in desktop mode: it would turn the
// surface into a 1280x720 window inside WorkerW. A duplicate gets neither
// the mode choice nor autostart - it does not own the registry and exits
// when the panel is closed. It does get symbol and interval, though: the
// overlay already lets it switch its own view, and SaveConfig skips
// duplicates itself. The autostart check is read from the Run key every
// time.
static HMENU BuildTrayMenu(void) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return NULL;
    {
        HMENU hSym = BuildSymbolMenu();
        HMENU hIv  = BuildIntervalMenu();
        if (hSym) AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSym, L"Symbol");
        if (hIv)  AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hIv,  L"Interval");
        // The VOL toggle (phase 22) - desktop mode has no toolbar.
        AppendMenuW(hMenu, MF_STRING | (ShowVolNow(&g_Ctx) ? MF_CHECKED : MF_UNCHECKED),
                    ID_TRAY_VOLUME, L"Volume bars	V");
        // The MA toggle (phase 25), same reason. Since phase 27 it also
        // carries VWAP and today's high/low, so it is named for what it is.
        AppendMenuW(hMenu, MF_STRING | (ShowIndNow(&g_Ctx) ? MF_CHECKED : MF_UNCHECKED),
                    ID_TRAY_INDICATORS, L"Indicators	M");
        // The price alerts (phase 23) are set in the panel's price column,
        // but must be clearable from here: desktop mode draws the lines and
        // has no input. The count applies to the symbol shown. Grayed, not
        // gone, without alerts - the item is also where the user sees THAT
        // they exist.
        {
            wchar_t lbl[48];
            int na = g_Ctx.alertCount[g_Ctx.symIdx];
            swprintf_s(lbl, 48, L"Clear price alerts (%d)", na);
            AppendMenuW(hMenu, MF_STRING | (na > 0 ? MF_ENABLED : MF_GRAYED),
                        ID_TRAY_ALERTS_CLEAR, lbl);
        }
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    }
    if (!g_isDuplicate) {
        AppendMenuW(hMenu, MF_STRING | (g_desktopMode ? MF_CHECKED : MF_UNCHECKED),
                    ID_TRAY_DESKTOP, L"Desktop mode");
    }
    AppendMenuW(hMenu, MF_STRING | (g_desktopMode ? MF_GRAYED : MF_ENABLED),
                ID_TRAY_RESET, L"Default view	Ctrl+0");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    if (!g_isDuplicate) {
        AppendMenuW(hMenu, MF_STRING | (AutostartPresent() ? MF_CHECKED : MF_UNCHECKED),
                    IDM_TOGGLE_AUTOSTART, L"Start at sign-in");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    }
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Quit TickC");
    return hMenu;
}

// ---------------------------------------------------------------------------

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TRAYICON:
            // Desktop mode: the surface must not be hidden or get focus, so
            // a left click does nothing there. The menu is rebuilt on every
            // right click, so the check mark always shows the current mode.
            if (lParam == WM_LBUTTONUP && !g_desktopMode) {
                TogglePopup(&g_Ctx, (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
            } else if (lParam == WM_RBUTTONUP) {
                HMENU hMenu = BuildTrayMenu();
                if (!hMenu) break;

                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_TRAY_DESKTOP) {
                SetDesktopMode(&g_Ctx, hwnd, (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE),
                               !g_desktopMode);
                return 0;
            }
            if (LOWORD(wParam) == IDM_TOGGLE_AUTOSTART) {
                ToggleAutostart();
                return 0;
            }
            if (LOWORD(wParam) == ID_TRAY_VOLUME) {
                SetShowVolume(&g_Ctx, !ShowVolNow(&g_Ctx));
                return 0;
            }
            if (LOWORD(wParam) == ID_TRAY_INDICATORS) {
                SetShowIndicators(&g_Ctx, !ShowIndNow(&g_Ctx));
                return 0;
            }
            if (LOWORD(wParam) == ID_TRAY_ALERTS_CLEAR) {
                AlertsClear(&g_Ctx);
                return 0;
            }
            {
                // Symbol and interval (phase 17). Range check first: a posted
                // ID outside the tables is a silent no-op, not an index.
                // The hit encoding is the same as OverlayHit uses.
                int id = (int)LOWORD(wParam);
                if (id >= ID_TRAY_SYMBOL_FIRST && id < ID_TRAY_SYMBOL_FIRST + SYMBOL_COUNT) {
                    ApplyConfigChoice(&g_Ctx, id - ID_TRAY_SYMBOL_FIRST);
                    return 0;
                }
                if (id >= ID_TRAY_INTERVAL_FIRST && id < ID_TRAY_INTERVAL_FIRST + INTERVAL_COUNT) {
                    ApplyConfigChoice(&g_Ctx, SYMBOL_COUNT + (id - ID_TRAY_INTERVAL_FIRST));
                    return 0;
                }
            }
            if (LOWORD(wParam) == ID_TRAY_RESET) {
                // Grayed in the menu in desktop mode; blocked here too, since
                // a posted message does not care about the menu.
                if (g_desktopMode) return 0;
                // If the window does not exist yet, create it first -
                // otherwise the menu item would be a silent no-op.
                if (!g_Ctx.hPopup || !IsWindowVisible(g_Ctx.hPopup)) {
                    TogglePopup(&g_Ctx, (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
                }
                ResetToDefaultView(g_Ctx.hPopup);
                return 0;
            }
            if (LOWORD(wParam) == ID_TRAY_EXIT) {
                Shell_NotifyIconW(NIM_DELETE, &g_Ctx.nid);
                PostQuitMessage(0);
            }
            break;

        // The worker thread has stored new data. All we do here is read the
        // price under the lock and paint - no network traffic.
        case WM_APP_DATA: {
            double price;
            ULONGLONG okTick;
            EnterCriticalSection(&g_Ctx.lock);
            price  = g_Ctx.lastPrice;
            okTick = g_Ctx.lastOkTick;
            LeaveCriticalSection(&g_Ctx.lock);
#ifdef TICKER_PROBE
            // Injected price (phase 23): wParam 1 carries the price x 100 in
            // lParam and takes precedence over lastPrice. The worker thread
            // can write a real price between the probe's write and this
            // read; then the injection would be lost about once in a
            // thousand, and a test of a trigger that fails now and then is
            // worse than none.
            if (wParam == 1) price = (double)(LONG)lParam / 100.0;
#endif

            // okTick == 0 means we have never succeeded yet. Then we are not
            // "disconnected" - we just have not got started.
            BOOL stale = (okTick != 0) &&
                         (GetTickCount64() - okTick > STALE_AFTER);

            // A restored connection resets the counter (phase 19). Without
            // this the previous disconnect's last seconds value was left
            // behind, and a new disconnect skipped a repaint when the number
            // happened to be the same. The counter starts at
            // STALE_AFTER / 1000 = 9, so 0 is never a real seconds value.
            if (!stale) g_Ctx.staleSecsShown = 0;

            UpdateIcon(&g_Ctx, price, stale);
            // The price alerts (phase 23) are checked HERE, not in the thread
            // and not in MergeCandles: this is the one place every new price
            // passes on the UI thread, with the panel open (the candles'
            // close), closed (the ticker price) and in desktop mode. After
            // UpdateIcon, so the balloon comes from an icon that already
            // shows the price that fired. A disconnected line gives no new
            // price, and the same price twice fires nothing new: the alert
            // is gone after the first time.
            CheckAlerts(&g_Ctx, price);
            if (g_Ctx.hPopup && IsWindowVisible(g_Ctx.hPopup)) {
                // Today's session (phase 27): VWAP and today's high/low need
                // the candles back to 00:00 UTC, and the 360 from the first
                // fetch are six hours at 1m. If the buffer does not reach
                // back, we ask for older candles - the same backfill as a
                // drag into the wall (phase 18). Yesterday's levels (phase
                // 28) need one more day, so the target is the PREVIOUS day
                // rollover: at most eight fetches (2880 candles at 1m), one
                // at 5m, none from 15m up. Each fetch posts WM_APP_DATA,
                // which asks again - the chain is done in a few seconds.
                // Only while the indicators are on in the mode we are in:
                // the desktop, where they are off by default, fetches
                // nothing extra.
                // RequestHistory is idempotent and stops at histDone.
                if (ShowIndNow(&g_Ctx)) {
                    EnterCriticalSection(&g_Ctx.lock);
                    BOOL need = SessionsNeedHistory(g_Ctx.candles, g_Ctx.candleCount,
                                                    g_Ctx.intervalMs, g_Ctx.histDone);
                    LeaveCriticalSection(&g_Ctx.lock);
                    if (need) RequestHistory(&g_Ctx);
                }
                // The clock must run while we are disconnected, otherwise
                // the seconds counter in the subtitle freezes.
                // New candles can move the Y target, and when disconnected
                // the counter must run.
                StartAnim(g_Ctx.hPopup);
                InvalidateRect(g_Ctx.hPopup, NULL, FALSE);
            }
            return 0;
        }

#ifdef TICKER_PROBE
        // Test build (phase 23): the WRITING probe fields. Up to and
        // including phase 22 the probe only read; a trigger that depends on
        // the live price crossing a line cannot be provoked that way. The
        // fields live on the main window, not on the panel, so an alert can
        // fire while the panel is hidden or does not exist. The production
        // build does not have the message, so there the probe is still
        // read-only - it does not exist.
        //   100  inject price: lParam = price x 100. Writes lastPrice and
        //        runs WM_APP_DATA SYNCHRONOUSLY, so the trigger has been
        //        checked when SendMessage returns.
        //   101  mute balloon and sound: lParam 0/1.
        case WM_APP_PROBE:
            if (wParam == 100) {
                EnterCriticalSection(&g_Ctx.lock);
                g_Ctx.lastPrice = (double)(LONG)lParam / 100.0;
                LeaveCriticalSection(&g_Ctx.lock);
                SendMessageW(hwnd, WM_APP_DATA, 1, lParam);
                return 1;
            }
            if (wParam == 101) {
                g_probeMute = (lParam != 0);
                return 1;
            }
            //   102  set an alert at an EXACT level: lParam = level x 100.
            //        Through AlertAdd, so side, cap and duplicate guard are
            //        the real ones. Keeps the trigger tests independent of
            //        y -> price, which is tested separately with posted clicks.
            //   103  remove the alerts for the symbol (same as the tray item).
            if (wParam == 102) {
                return AlertAdd(&g_Ctx, (double)(LONG)lParam / 100.0) ? 1 : 0;
            }
            if (wParam == 103) {
                AlertsClear(&g_Ctx);
                return 1;
            }
            //   110-112  READING (phase 24), here and not on the panel because
            //        wake-up and the price branch must be testable with the
            //        panel closed: fetch cycles, wake-ups, rejected values.
            if (wParam == 110) return (LRESULT)g_probeFetches;
            if (wParam == 111) return (LRESULT)g_probeResumes;
            if (wParam == 112) return (LRESULT)g_probeRejects;
            if (wParam == 113) return (LRESULT)g_probeConnDrops;
            if (wParam == 114) return (LRESULT)g_probeDisplayChanges;   // phase 26
            if (wParam == 115) return (LRESULT)g_probeMigrate;          // phase 30
            return 0;
#endif

        // Wake from sleep (phase 24). Without this the first attempt could
        // be a whole backoff cap (60 s) away, against a connection that died
        // while the machine slept. hWakeEvent resets the backoff and gives an
        // attempt at once, as when the panel is opened; dropConn makes the
        // thread release hConnect first. Only AUTOMATIC: it arrives on EVERY
        // wake-up, RESUMESUSPEND only in addition when a user is behind it,
        // and two wake-ups are one fetch too many. The network is often not
        // up yet - then the attempt fails, and the backoff takes it from
        // there with 3 s, 6 s, ... instead of staying where it was before
        // the sleep. SetEvent outside the lock, as elsewhere in the file.
        //
        // The hWakeEvent check is a guard, not decoration: the window is
        // created BEFORE InitializeCriticalSection in WinMain, and a sent
        // message can be delivered in that window. hWakeEvent is set after
        // the lock, so if it is set, the lock exists.
        // Display change (phase 26). lParam is not read: the main thread is
        // DPI-unaware, so the measurements there are virtualized.
        case WM_DISPLAYCHANGE:
#ifdef TICKER_PROBE
            InterlockedIncrement(&g_probeDisplayChanges);
#endif
            RefitDesktopSurface();
            if (g_desktopMode) SetTimer(hwnd, TIMER_REFIT_ID, REFIT_SETTLE_MS, NULL);
            break;

        case WM_POWERBROADCAST:
            if (wParam == PBT_APMRESUMEAUTOMATIC && g_Ctx.hWakeEvent) {
                EnterCriticalSection(&g_Ctx.lock);
                g_Ctx.dropConn = TRUE;
                LeaveCriticalSection(&g_Ctx.lock);
                SetEvent(g_Ctx.hWakeEvent);
#ifdef TICKER_PROBE
                InterlockedIncrement(&g_probeResumes);
#endif
            }
            return TRUE;

        case WM_DESTROY:
            SaveConfig(&g_Ctx);
            if (g_Ctx.hPopup) {
                SaveWindowPlacement(g_Ctx.hPopup);
                // The panel is NOT owned by the main window any more -
                // ownership would remove the button in the taskbar. Then
                // Windows does not tear it down for us, so we do it ourselves.
                HWND hp = g_Ctx.hPopup;
                // hPopup is in the lock domain, and the worker thread is still
                // alive here - it is stopped only after the message loop.
                EnterCriticalSection(&g_Ctx.lock);
                g_Ctx.hPopup = NULL;
                LeaveCriticalSection(&g_Ctx.lock);
                DestroyWindow(hp);
            }
            PostQuitMessage(0);
            break;

        // Desktop mode without a surface: WorkerW did not exist, or Explorer
        // tore it down. Retried until it sticks; TogglePopup sets the timer
        // again itself if it fails again.
        case WM_TIMER:
            if (wParam == TIMER_REFIT_ID) {   // phase 26: once more, when Explorer has settled
                KillTimer(hwnd, TIMER_REFIT_ID);
                RefitDesktopSurface();
                return 0;
            }
            if (wParam == TIMER_EMBED_ID) {
                KillTimer(hwnd, TIMER_EMBED_ID);
                if (g_desktopMode && !g_Ctx.hPopup) {
                    TogglePopup(&g_Ctx, (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
                }
                return 0;
            }
            break;

        default:
            // Explorer has been restarted. The icon is gone from the
            // notification area. In desktop mode Windows has already torn
            // down the surface along with the old WorkerW (measured: gone
            // within 20 ms), and WM_NCDESTROY has started the timer.
            //
            // The timer may thus have had time to build a new surface before
            // this message arrives: new surface after 1.1 s, TaskbarCreated
            // after ~1.6 s. Then the fresh surface was torn down and rebuilt,
            // and the desktop stood without a chart for a second. So it is
            // torn down only if it does NOT sit in the current WorkerW.
            if (msg == g_msgTaskbarCreated && g_msgTaskbarCreated != 0) {
                Shell_NotifyIconW(NIM_ADD, &g_Ctx.nid);
                if (g_desktopMode) {
                    if (!g_Ctx.hPopup) {
                        SetTimer(hwnd, TIMER_EMBED_ID, EMBED_RETRY_MS, NULL);
                    } else if (GetParent(g_Ctx.hPopup) != FindDesktopWorkerW()) {
                        DestroyWindow(g_Ctx.hPopup);
                    }
                }
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    // No single-instance mutex any more: [ + ] starts precisely one more
    // instance. Each process has its own worker thread, its own tray icon and
    // its own window classes (classes are per process, so the names do not
    // collide).

    memset(&g_Ctx, 0, sizeof(AppContext));
    g_Ctx.hoverIdx = -1;   // 0 from memset would mean "hover on the first candle"
    g_Ctx.overlayHot = -1; // same reason: 0 would mean "first row highlighted"
    g_Ctx.dispValid  = FALSE; // snap on the first frame

    g_Ctx.hSession = WinHttpOpen(L"TickC/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

    if (!g_Ctx.hSession) return 1;

    // Without this the default receive timeout is 30 seconds. Then a hanging
    // connection would hold the worker thread longer than the 3 seconds we
    // wait at exit - and we would close the session under it.
    WinHttpSetTimeouts(g_Ctx.hSession, 5000, 5000, 5000, 5000);

    g_Ctx.hFontBig = CreateFontW(-19, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_Ctx.hFontSmall = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    // Measured with GetGlyphOutlineW(GGO_METRICS) on '0': Lucida Console em 15
    // gives 11 px digit height, 9 px character width and tmHeight 15. Consolas
    // jumps from 10 to 12 px (em 16 -> 17), Cascadia Mono em 16 gives 11 px
    // but tmHeight 21, which does not fit in the time axis's 18 px.
    g_Ctx.hFontAxis = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN,
                                  L"Lucida Console");
    // hFontWm is not created here: the height depends on the panel size, so
    // it is built in EnsureWatermark and only when the height changes.

    WNDCLASSW wc = {0};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"BTCTickerWindowClass";
    RegisterClassW(&wc);

    WNDCLASSW pwc = {0};
    pwc.lpfnWndProc   = PopupProc;
    pwc.hInstance     = hInstance;
    pwc.lpszClassName = L"BTCPopupClass";
    pwc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    pwc.hbrBackground = NULL; // we paint everything ourselves
    pwc.style         = CS_DBLCLKS;   // double click resets zoom and panning
    RegisterClassW(&pwc);

    // Broadcast to top-level windows when Explorer has restarted.
    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    g_Ctx.hWnd =CreateWindowExW(0, wc.lpszClassName, L"TickC", 0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    g_Ctx.nid.cbSize           = sizeof(NOTIFYICONDATAW);
    g_Ctx.nid.hWnd             = g_Ctx.hWnd;
    g_Ctx.nid.uID              = 42;
    g_Ctx.nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_Ctx.nid.uCallbackMessage = WM_TRAYICON;
    g_Ctx.nid.hIcon            = RenderMicroFontIcon("...", 0xFF00FF66);
    wcscpy_s(g_Ctx.nid.szTip, 128, L"Connecting to Binance...");

    Shell_NotifyIconW(NIM_ADD, &g_Ctx.nid);

    // Fixed GDI objects: created once, not 16 times per repaint
    g_Ctx.penGrid   = CreatePen(PS_SOLID, 1, CLR_GRID);
    g_Ctx.penCross  = CreatePen(PS_DOT,   1, CLR_CROSS);
    g_Ctx.penBtn      = CreatePen(PS_SOLID, 1, CLR_DIM);
    g_Ctx.penBtnHot   = CreatePen(PS_SOLID, 1, CLR_TEXT);
    g_Ctx.penBtnWhite = CreatePen(PS_SOLID, 1, CLR_BTNHOT);
    g_Ctx.brClose     = CreateSolidBrush(CLR_CLOSEHOT);
    g_Ctx.curArrow    = LoadCursorW(NULL, IDC_ARROW);
    g_Ctx.curPan      = LoadCursorW(NULL, IDC_SIZEALL);
    g_Ctx.curHand     = LoadCursorW(NULL, IDC_HAND);
    g_Ctx.btnHot      = -1;
    g_Ctx.tbHot       = -1;
    g_Ctx.alertHot    = -1;     // phase 23: 0 would mean "first alert under the pointer"
    g_Ctx.axisHotY    = -1;
    g_Ctx.showVol     = TRUE;   // phase 22; LoadConfig can turn it off
    g_Ctx.dispVolF    = 1.0;
    g_Ctx.showInd     = TRUE;   // phase 25; LoadConfig can turn it off
    g_Ctx.dispIndF    = 1.0;
    // Dashed, not dotted: keeps the last-price line visually distinct from
    // both the grid (solid, muted) and the crosshair (dotted).
    g_Ctx.penLastUp   = CreatePen(PS_DASH, 1, CLR_UP);
    g_Ctx.penLastDown = CreatePen(PS_DASH, 1, CLR_DOWN);
    g_Ctx.brBg      = CreateSolidBrush(CLR_BG);
    g_Ctx.brBox     = CreateSolidBrush(CLR_BOX);
    g_Ctx.brBoxEdge = CreateSolidBrush(CLR_BOXEDGE);
    g_Ctx.brVolUp   = CreateSolidBrush(CLR_VOL_UP);     // phase 21
    g_Ctx.brVolDown = CreateSolidBrush(CLR_VOL_DOWN);

    // The worker thread is started only when the window and the icon exist,
    // since it posts messages to hWnd right away.
    // MUST come before CreateThread: the first fetch must go to the right
    // pair, and the watermark must be correct from the first frame.
    MigrateLegacyNames();   // phase 30: before the first read from the registry
    LoadConfig(&g_Ctx, &g_savedPanelX, &g_savedPanelY,
               &g_savedPanelW, &g_savedPanelH);

    // Duplicate: "--dup x y w h sym iv", written by SpawnInstance. Overrides
    // what LoadConfig read, with the same limits - a hand-written command
    // line must not be able to index outside the tables. If the check fails,
    // we start as a normal main instance instead of guessing.
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        // Desktop mode takes no arguments and cannot be combined with
        // --dup: a duplicate is started from [ + ], which does not exist here.
        if (argv && argc == 2 && wcscmp(argv[1], L"--desktop-mode") == 0) {
            g_desktopMode = TRUE;
        }
        if (argv && argc == 8 && wcscmp(argv[1], L"--dup") == 0) {
            int v[6];
            for (int i = 0; i < 6; ++i) v[i] = _wtoi(argv[2 + i]);
            if (v[2] >= 240 && v[2] <= 8192 && v[3] >= 160 && v[3] <= 8192 &&
                v[4] >= 0 && v[4] < SYMBOL_COUNT &&
                v[5] >= 0 && v[5] < INTERVAL_COUNT) {
                g_isDuplicate    = TRUE;
                g_savedPanelX    = v[0];
                g_savedPanelY    = v[1];
                g_savedPanelW    = v[2];
                g_savedPanelH    = v[3];
                g_Ctx.symIdx     = v[4];
                g_Ctx.ivIdx      = v[5];
                g_Ctx.intervalMs = INTERVALS[v[5]].ms;
            }
        }
        if (argv) LocalFree(argv);
    }
    // Without --desktop-mode the registry decides: the tray menu remembers
    // the last chosen mode (phase 12). The flag wins for this run, and a
    // duplicate is always a panel.
    if (!g_desktopMode && !g_isDuplicate) g_desktopMode = LoadDesktopMode();
    // No animation at startup (phases 22 and 25). Here, and not right after
    // LoadConfig: which choice applies depends on the mode (phase 26).
    g_Ctx.dispVolF = ShowVolNow(&g_Ctx) ? 1.0 : 0.0;
    g_Ctx.dispIndF = ShowIndNow(&g_Ctx) ? 1.0 : 0.0;
    // The price alerts (phase 23). After the --dup parsing: LoadAlerts skips
    // duplicates, and g_isDuplicate is known only here. Before the thread:
    // the first price must be checked against the alerts from the last run.
    LoadAlerts(&g_Ctx);
#ifdef TICKER_PROBE
    // An alert from the registry can fire on the FIRST price, before a probe
    // has time to send 101. The probe therefore sets the variable in its own
    // environment, and the test build inherits it.
    g_probeMute = GetEnvironmentVariableW(L"TICKER_PROBE_MUTE", NULL, 0) > 0;
    // Recorded responses (phase 34); see g_fixtureDir. Read before the thread
    // starts, which is the only reader.
    if (GetEnvironmentVariableW(L"TICKER_FIXTURE_DIR", g_fixtureDir, MAX_PATH) == 0 ||
        GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
        g_fixtureDir[0] = L'\0';
    }
#endif

    InitializeCriticalSection(&g_Ctx.lock);
    g_Ctx.hStopEvent = CreateEventW(NULL, TRUE,  FALSE, NULL);  // manual reset
    g_Ctx.hWakeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);  // auto reset
    g_Ctx.hThread    = CreateThread(NULL, 0, NetworkThread, &g_Ctx, 0, NULL);

    // A duplicate is started from a click and must show itself at once - a
    // main instance starts in the notification area. After the lock and the
    // events: TogglePopup enters the lock and wakes the thread. The desktop
    // surface likewise - it exists only while it is shown.
    if (g_isDuplicate || g_desktopMode) TogglePopup(&g_Ctx, hInstance);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Stop the thread before we tear down anything it can touch
    if (g_Ctx.hStopEvent) SetEvent(g_Ctx.hStopEvent);
    if (g_Ctx.hThread) {
        WaitForSingleObject(g_Ctx.hThread, 3000);
        CloseHandle(g_Ctx.hThread);
    }
    if (g_Ctx.hStopEvent) CloseHandle(g_Ctx.hStopEvent);
    if (g_Ctx.hWakeEvent) CloseHandle(g_Ctx.hWakeEvent);
    DeleteCriticalSection(&g_Ctx.lock);

    // The buffer first: fonts, pens and brushes from the last frame can be
    // selected into the DC, and DeleteObject on a selected object fails
    // silently.
    FreeBackBuffer(&g_Ctx);

    if (g_Ctx.nid.hIcon) DestroyIcon(g_Ctx.nid.hIcon);
    if (g_Ctx.hFontPill) DeleteObject(g_Ctx.hFontPill);
    if (g_Ctx.hFontBig) DeleteObject(g_Ctx.hFontBig);
    if (g_Ctx.hFontSmall) DeleteObject(g_Ctx.hFontSmall);
    if (g_Ctx.hFontAxis) DeleteObject(g_Ctx.hFontAxis);

    DeleteObject(g_Ctx.penGrid);  DeleteObject(g_Ctx.penCross);
    DeleteObject(g_Ctx.brBg);     DeleteObject(g_Ctx.brBox);
    DeleteObject(g_Ctx.brBoxEdge);
    DeleteObject(g_Ctx.brVolUp);     DeleteObject(g_Ctx.brVolDown);
    DeleteObject(g_Ctx.penLastUp);   DeleteObject(g_Ctx.penLastDown);
    DeleteObject(g_Ctx.penBtn);      DeleteObject(g_Ctx.penBtnHot);
    DeleteObject(g_Ctx.penBtnWhite); DeleteObject(g_Ctx.brClose);

    // The watermark cache. The order matters: the bitmap must be selected
    // out of the DC before both are deleted.
    if (g_Ctx.wmDC) {
        if (g_Ctx.wmOldBmp) SelectObject(g_Ctx.wmDC, g_Ctx.wmOldBmp);
        DeleteDC(g_Ctx.wmDC);
    }
    if (g_Ctx.wmBmp)   DeleteObject(g_Ctx.wmBmp);
    if (g_Ctx.hFontWm) DeleteObject(g_Ctx.hFontWm);

    if (g_Ctx.hConnect) WinHttpCloseHandle(g_Ctx.hConnect);
    if (g_Ctx.hSession) WinHttpCloseHandle(g_Ctx.hSession);

    return (int)msg.wParam;
}
