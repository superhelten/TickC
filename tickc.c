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

#include "chart.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

#define WM_TRAYICON      (WM_USER + 1)
#define WM_APP_DATA      (WM_APP + 1)   // the worker thread has new data
// A second start hands over to the running main instance (phase 46): posted
// to its main window, found by class and MainInstanceNames' title.
#define WM_APP_SHOW      (WM_APP + 3)
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
#define ID_TRAY_RSI      1008   // the RSI band on/off (phase 39)
#define ID_TRAY_THEME    1009   // the light theme on/off (phase 40)
#define ID_TRAY_OPEN     1010   // show the panel, the bold default item (phase 47)
// Symbol and interval from the tray menu (phase 17). Item i gets FIRST + i.
// The ranges are 100 wide; #error below the tables ensures they never overlap.
#define ID_TRAY_SYMBOL_FIRST   1100
#define ID_TRAY_INTERVAL_FIRST 1200
// The ranges (phase 41). "Range" in ID_TRAY_RANGE_W below is the width of an
// ID block; the time ranges are "periods" here to keep the two apart.
#define ID_TRAY_PERIOD_FIRST   1300
#define ID_TRAY_RANGE_W        100
// The chart types (phase 49): item t is FIRST + t, t a CHART_* value.
#define ID_TRAY_TYPE_FIRST     1400
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
#define PANEL_GRAB_W     120   // header width that must be on a work area when shown (phase 48)
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
#define TIMER_ANIM_ID    2
#define TIMER_EMBED_ID   3      // desktop mode: try WorkerW again
#define TIMER_REFIT_ID   4      // desktop mode: place the surface again after a display change (phase 26)
#define REFIT_SETTLE_MS  1000
// 250 ms: when Explorer restarts, the new Progman is in place after 290-480 ms
// (measured). With 1000 ms the desktop stood without a chart for 1.1 s. The
// timer only runs while the surface is missing.
#define EMBED_RETRY_MS   250
#define EMBED_FAST_TRIES 8        // phase 46: then the delay doubles (EmbedRetryMs)
#define EMBED_RETRY_MAX  10000
// Phase 46: a tray click this soon after the panel lost activation counts as
// a click on a panel that was in front. See TogglePopup.
#define TRAY_CLICK_GRACE_MS 500
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
// width constant - DrawHeader measures the header against it.
//
// Minimum space between two header texts on the same line, and between text
// and the button row.
#define HDR_GAP          8

// Both averages must be defined on the first visible candle when the panel
// opens on the default view (see SEED_COUNT).
C_ASSERT(SEED_COUNT >= DEFAULT_VIEW + IND_EMA_PERIOD);
C_ASSERT(SEED_COUNT >= DEFAULT_VIEW + IND_SMA_PERIOD);
// The watermark is blended towards white with alpha from WatermarkAlpha(W).
// White because the old fixed color #15191F was neutral: CLR_BG + 8 in all
// channels, i.e. ~3.3 % towards white.
#define CLR_WM_INK       RGB(0xFF, 0xFF, 0xFF)

// The app's theme (phase 40): the chart's colors, which the header, the
// buttons, the toolbar and the overlay borrow through ctx->sty.clr, plus the
// one color only the app draws with. White ink on a light background would
// be invisible, so the light theme blends toward its text color instead.
typedef struct {
    const ChartTheme* chart;
    COLORREF wmInk;      // the watermark is blended from clr.bg toward this
} AppTheme;
static const AppTheme APP_THEME_DARK  = { &ChartThemeDark,  CLR_WM_INK };
static const AppTheme APP_THEME_LIGHT = { &ChartThemeLight, RGB(0x1F, 0x23, 0x28) };
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
    { L"1w",  L"1w",  604800000LL },   // phase 41: five years and more
};
#define SYMBOL_COUNT   ((int)(sizeof(SYMBOLS) / sizeof(SYMBOLS[0])))
#define INTERVAL_COUNT ((int)(sizeof(INTERVALS) / sizeof(INTERVALS[0])))

// The ranges (phase 41), on the Bloomberg model: how far back the chart
// goes, chosen apart from the bar size. A range is a DURATION. A click sets
// its default bar size, picked so the view holds 180-365 candles (the old
// default view was 300): enough to read as candles at 1280 px. Another bar
// size can be picked afterwards and the range stays - 1M on 1h is 720
// candles - unless it cannot be shown there (fewer than MIN_VIEW candles,
// or more than the buffer holds: 1Y on 1m), which ends it. YTD counts from
// 1 January 00:00 UTC and grows with the year (phase 47: it is anchored,
// the others are durations - see RangeWantAt); Max is as much as the
// exchange and the buffer give.
#define RANGE_DAYS_YTD  (-1)
#define RANGE_DAYS_MAX  (-2)
typedef struct { const wchar_t* label; int days; long long ivMs; } RangeDef;
static const RangeDef RANGES[] = {
    { L"1D",  1,              300000LL },     // 5m  x 288
    { L"3D",  3,              900000LL },     // 15m x 288
    { L"1M",  30,             14400000LL },   // 4h  x 180
    { L"6M",  182,            86400000LL },   // 1d  x 182
    { L"YTD", RANGE_DAYS_YTD, 86400000LL },   // 1d  x day of the year
    { L"1Y",  365,            86400000LL },   // 1d  x 365
    { L"5Y",  1826,           604800000LL },  // 1w  x 261
    { L"Max", RANGE_DAYS_MAX, 604800000LL },  // 1w  x all
};
#define RANGE_COUNT    ((int)(sizeof(RANGES) / sizeof(RANGES[0])))
#define RANGE_1Y       5     // the last range guaranteed a pill at the minimum width
// The tray menu's ID ranges (phase 17). sizeof cannot appear in #if, so the
// guard is C_ASSERT: if a table grows past its range, the build stops here.
C_ASSERT(SYMBOL_COUNT   <= ID_TRAY_RANGE_W);
C_ASSERT(INTERVAL_COUNT <= ID_TRAY_RANGE_W);
C_ASSERT(ID_TRAY_SYMBOL_FIRST + ID_TRAY_RANGE_W <= ID_TRAY_INTERVAL_FIRST);
C_ASSERT(RANGE_COUNT + 1 <= ID_TRAY_RANGE_W);   // + 1: "None" (phase 47)
C_ASSERT(ID_TRAY_INTERVAL_FIRST + ID_TRAY_RANGE_W <= ID_TRAY_PERIOD_FIRST);
C_ASSERT(ID_TRAY_PERIOD_FIRST + ID_TRAY_RANGE_W <= ID_TRAY_TYPE_FIRST);   // phase 49
C_ASSERT(CHART_TYPE_COUNT <= ID_TRAY_RANGE_W);

#define ALERT_TAU_FLASH    900.0  // the afterglow when an alert fires (ms)

// The toolbar (phase 22): symbol pill, interval pill and VOL, in the
// header's row 2 - where the symbol line stood as plain text. Up to phase 40
// every interval had a pill of its own; phase 41 made the interval (the bar
// size) a dropdown like the symbol, and put one pill per range between it
// and VOL. Fixed
// widths, not measured text: WM_NCHITTEST must be able to compute the pills
// without a DC, and painting and hit testing must read the same numbers
// (pitfall 14).
//
// Phase 42 rebuilt the header on the Bloomberg terminal's model. Row 1 is the
// quote line: the symbol, then Last, Chg, %Chg, Op, Hi, Lo, Vol and At, in the
// band of the control buttons (QL_TOP..QL_TOP + QL_H). Row 2 is the range
// field: 1D ... Max and the interval as cells side by side, one pixel apart,
// with the settings cell (a gear) at the right end, before the price axis's
// top label. 15 px high from y = 28, so y = 43 is the last row above the
// chart area. VOL, MA and RSI left the row for the settings menu.
#define TBAR_TOP           28
#define TBAR_H             15
#define QL_TOP             6     // row 1: the quote line (phase 42)
#define QL_H               18
#define TBAR_SYM_W         74    // "BNB/USDT" + arrow
#define TBAR_IVDD_W        44    // "15m" + arrow (phase 41)
#define TBAR_RANGE_W       22    // "1D" (phase 41)
#define TBAR_RANGE_WIDE_W  28    // "YTD", "Max"
#define TBAR_GEAR_W        22    // the settings cell (phase 42)
#define TBAR_CELL_GAP      1     // between the cells of the range field (phase 42)
#define TBAR_GROUP_GAP     8     // before the settings cell
#define TBAR_SYM           0     // row 1 from phase 42: where the quote line starts
#define TBAR_RANGE_FIRST   1     // one cell per range (phase 41)
#define TBAR_IV            (TBAR_RANGE_FIRST + RANGE_COUNT)   // the interval dropdown, after Max
#define TBAR_GEAR          (TBAR_IV + 1)                      // the settings menu (phase 42)
#define TBAR_COUNT         (TBAR_GEAR + 1)

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
    // The range (phase 41): a period on the Bloomberg model (1D ... Max),
    // independent of the bar size. rangeIdx is the selected one, -1 = none;
    // UI-owned. It is the panel's HOME view: ResetView (R, double-click,
    // opening) goes back to it. rangeWant is how many candles the home view
    // holds at the current interval, 0 while the user has moved the view
    // away (zoom, pan, keys). Lock-protected: the UI writes it, and the
    // worker thread reads it where it sets a followed view - without it a
    // wanted 365 would be clamped to the 360 the first fetch brings, and
    // stay there after the backfill.
    int  rangeIdx;
    int  rangeWant;
    // The home is year-to-date (phase 47), set with rangeWant: the worker
    // thread counts YTD's want again with every candle it merges, as the
    // year grows. Lock-protected like rangeWant, which it qualifies.
    BOOL rangeYtd;
    // The trading day (phase 42): Binance's statistics for the UTC day, as
    // Bloomberg's quote line shows the day's open, high, low and volume.
    // UTC, so it is the day HOD/LOD/PDC draw. Lock-protected: the worker
    // thread writes, the UI reads. dayValid is FALSE until a fetch has
    // succeeded for the symbol shown; a symbol change clears it.
    // lastUpdMs is the wall-clock time of the last candle fetch that
    // succeeded, the quote line's "At".
    BOOL      dayValid;
    double    dayOpen, dayHigh, dayLow, dayVol;
    long long lastUpdMs;
    // Phase 46: the UTC day (days since 1970) the statistics were fetched
    // on - held from another day they are yesterday's: not drawn, and the
    // next cycle fetches today's - and a request from the UI to fetch them on
    // the next cycle,
    // set when the panel is shown. It was every fifth cycle only, and the
    // cycle count stands still while the panel is hidden: reopened, the quote
    // line showed the day as it was when it was hidden, for up to 15 s.
    long long dayUtc;
    BOOL      dayRefresh;

    Candle candles[MAX_CANDLES];
    int candleCount;

    // The chart engine's state (phase 34): the target view, the eased display
    // and the hover. ch.viewStart/viewCount/followLive are lock-protected like
    // candles[]; the rest is UI-owned. See chart.h.
    ChartState ch;
    // The pointer's x when the crosshair was last set from it (phase 47);
    // ch.hoverY is its y. UI-owned. The display eases after a wheel notch,
    // a double-click or a new candle, and the candle under a resting pointer
    // changes with it: the clock asks HitCandle again at this point.
    int  hoverX;
    BOOL panning;        // dragging the chart sideways right now
    int  panAnchorX;     // mouse X when the panning started

    HFONT hFontQuote;
    // The chart's fonts, pens and brushes (phase 35), from ChartStyleCreate.
    // The header, buttons and overlay borrow fontSmall, brBox and brBoxEdge.
    ChartStyle sty;

    BOOL trackingMouse;  // whether WM_MOUSELEAVE has been requested

    // --- Animation clock ---
    // One timer drives everything time-dependent: overlay fade, the stale
    // counter, view and Y-axis easing. It only lives while something is
    // actually moving, and is killed when everything has settled.
    ULONGLONG lastAnimTick;
    BOOL   animRunning;
    int    staleSecsShown;   // last painted seconds value, prevents 60 fps on a counter
    int    emptySecsShown;   // the same for the empty chart's retry countdown (phase 46)

    // --- Overlay for symbol/interval selection ---
    // overlayOpen is the LOGICAL state and drives hit detection.
    // overlayF is the fade level and only drives painting. During fade-out,
    // overlayF > 0 while overlayOpen is FALSE - clicks must then go to the chart.
    BOOL   overlayOpen;
    double overlayF;      // 0-255
    int    overlayHot;    // index into rows[], -1 = none
    // Phase 41: 0 = the picker with both columns (a right-click in the
    // chart; the symbol pill too through phase 44), 1 = the intervals alone as
    // a dropdown under the interval pill. Phase 42: 2 = the settings menu.
    // Phase 45: 3 = the symbols alone as a dropdown under the symbol cell.
    // The same rows and indices; only the layout differs. Kept after
    // closing, so the fade-out draws the box that was open.
    int    overlayKind;

    // --- Worker thread ---
    // The lock covers candles[], candleCount, viewStart, viewCount,
    // followLive, lastPrice, hPopup, frontShift, histPending, histDone,
    // rangeWant (phase 41), rangeYtd (phase 47) and hSession (phase 44: WinMain closes it at exit
    // while the worker may still run). hConnect is the worker's alone.
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
    // When the panel last lost activation (phase 46), 0 = it has not since
    // it was shown. UI-owned. See TogglePopup.
    ULONGLONG popupDeactTick;
    // The tray icon shows the offline dots (phase 46, ShowOfflineIcon).
    // UI-owned.
    BOOL      iconOffline;

    // --- Cached GDI objects ---
    // Fixed colors are created once instead of 16 times per repaint - by
    // ApplyPanelStyle, with the chart style, since phase 40: the colors are
    // the theme's.
    HPEN   penBtn, penBtnHot, penBtnWhite;
    const AppTheme* theme;   // phase 40: the theme sty and the pens were built from
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
    // The caption button the left button went down on, -1 for none (phase
    // 47). It acts on the release, and only if the release is on the same
    // button - Windows' own caption buttons: a press can be taken back by
    // sliding off. While it is down, it lights only while the pointer is on
    // it, and nothing else in the panel takes hover. UI-owned.
    int    btnDown;
    // The toolbar in the header's row 2 (phase 22). tbHot is the pill the
    // mouse is over, -1 for none - same rule as btnHot: logical state, no
    // fade. showVol is the user's choice and is saved in the registry;
    // dispVolF is the DISPLAY of it, 0..1, and is eased in WM_TIMER so the
    // bars sink down instead of blinking away. All three are UI-owned.
    int    tbHot;
    BOOL   showVol;
    // Moving averages (phase 25): SMA 20 and EMA 50 over the candles. Same
    // pair as showVol/dispVolF: showInd is the choice and is saved in the
    // registry, dispIndF is the display, 0..1, and is eased by the clock -
    // here as COLOR towards the background, not as geometry. UI-owned. The
    // averages themselves are not stored: they are computed from candles[]
    // in every repaint (see IndStep).
    BOOL   showInd;
    // The overlays have one choice PER MODE (phase 26). showVol/showInd are
    // the panel's; these two are the desktop surface's, and they are OFF by
    // default: the surface is read peripherally behind the icons (phase 14),
    // and bars and averages are measuring tools, not wallpaper. The tray menu
    // toggles the one for the mode the process is in. Read through
    // ShowVolNow/ShowIndNow, never directly.
    BOOL   showVolDesk;
    BOOL   showIndDesk;
    // The RSI band (phase 39), one choice per mode like the others, and OFF
    // in both by default: it takes a fifth of the chart's height, which is a
    // change to the panel nobody asked for yet, and the desktop stays quiet.
    // The region follows the choice at once (it is geometry); dispRsiF fades
    // the content.
    BOOL   showRsi;
    BOOL   showRsiDesk;
    // The light theme (phase 40), one choice per mode like the overlays, and
    // off in both: dark is the look TickC has had, and the desktop surface is
    // the wallpaper - a white one is a change nobody asked for there.
    BOOL   lightTheme;
    BOOL   lightThemeDesk;
    // The chart type (phase 49), a CHART_* value, one choice per mode like
    // the overlays. Read through ChartTypeNow; the engine draws and scales
    // with ch.chartType, which follows the mode's choice.
    int    chartType;
    int    chartTypeDesk;
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
// The dpi the saved size is in device pixels at (phase 48), 0 = not known:
// geometry from before phase 48, and a duplicate's, which comes from a live
// panel on the same monitor.
static int g_savedPanelDpi = 0;
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
// Failed tries at the desktop surface in a row (phase 46, EmbedRetryMs).
// UI thread only.
static int g_embedFails = 0;
// One main instance per sign-in session (phase 46). The mutex says one runs;
// its hidden main window carries g_mainTitle, so a second start can find it
// among the duplicates' main windows, which share the class (pitfall 10).
// Released when the message loop ends, not at exit - see WinMain.
static HANDLE  g_mainMutex = NULL;
static wchar_t g_mainTitle[48] = L"TickC";

// The panel's lengths at its dpi (phase 37). Every fixed length in the
// header, the buttons, the toolbar and the overlay is given at 96 dpi and
// goes through Dp, the same MulDiv as the chart engine's ChartPx: the
// identity at 96. g_Ctx.sty.dpi is the dpi the panel is drawn for - its
// monitor's, or 96 on the desktop surface (see PanelDpi).
static int Dp(int v) {
    return ChartPx((g_Ctx.sty.dpi > 0) ? g_Ctx.sty.dpi : CHART_DPI_BASE, v);
}
static int  PanelDpi(HWND hwnd);
static void ApplyPanelStyle(AppContext* ctx, int dpi);
static void SelectRange(AppContext* ctx, int r);   // phase 41
// The theme for the mode we are in (phase 40).
static const AppTheme* ThemeNow(const AppContext* ctx) {
    BOOL light = g_desktopMode ? ctx->lightThemeDesk : ctx->lightTheme;
    return light ? &APP_THEME_LIGHT : &APP_THEME_DARK;
}
// The overlay choices for the mode we are in (phase 26). Everything that
// paints, eases, checks items in the menu or answers a probe reads these.
static BOOL ShowVolNow(const AppContext* ctx) {
    return g_desktopMode ? ctx->showVolDesk : ctx->showVol;
}
static BOOL ShowIndNow(const AppContext* ctx) {
    return g_desktopMode ? ctx->showIndDesk : ctx->showInd;
}
static BOOL ShowRsiNow(const AppContext* ctx) {
    return g_desktopMode ? ctx->showRsiDesk : ctx->showRsi;
}
// The chart type for the mode we are in (phase 49). ch.chartType, which the
// engine draws and scales with, is set from this wherever the choice or the
// mode changes (SetChartType, SetDesktopMode, WinMain), never on its own.
static int ChartTypeNow(const AppContext* ctx) {
    return g_desktopMode ? ctx->chartTypeDesk : ctx->chartType;
}
// The chart's rectangle for a W x H surface in the mode we are in (phase 43).
// Painting, the watermark and every hit test go through here: the panes
// under the price follow two choices, and a call site that forgot one would
// point the crosshair at the wrong candle.
static ChartRect PanelGeometry(int W, int H) {
    return ChartGeometry(W, H, g_desktopMode, g_Ctx.sty.dpi,
                         ShowRsiNow(&g_Ctx), ShowVolNow(&g_Ctx));
}
static UINT g_msgTaskbarCreated = 0;   // Explorer restarted
static char s_httpBuf[98304];    // 360 candles give a ~60 KB response
static Candle s_incoming[SEED_COUNT];
#ifdef TICKER_PROBE
// Test build only (phase 21): the duration of the last full repaint in
// microseconds, QPC around the slow path in PaintPopup. Read with
// WM_APP_PROBE 15, so a probe can measure the median over many frames
// without taking screenshots at the same time (pitfall 37).
static LONGLONG g_probePaintUs = 0;
// Phase 42: the quote line's fields drawn in the last frame, bit f for QF_*
// (0 Last ... 7 At), 0x100 when the line is offline. Field 68.
static int g_probeQuoteMask = 0;
// Test build only (phase 23): mutes balloon and sound when an alert fires, so
// a probe can fire many alerts without bothering whoever sits at the machine.
// Set with WM_APP_PROBE 101 to the main window. One run fires one alert
// unmuted and reads the result from Shell_NotifyIconW (field 29).
static BOOL g_probeMute = FALSE;
// TICKER_FORCE_DPI (phase 37): the panel is drawn at this dpi whatever its
// monitor has, so golden.ps1 can prove on a 150 % machine that the layout at
// 96 is unchanged. 0 = the monitor's.
static int g_forceDpi = 0;
// Test build only (phase 34): recorded responses instead of the network.
// TICKER_FIXTURE_DIR names a folder; HttpGet then answers every request from
// a file there and never touches WinHTTP, so a capture shows the same candles
// every time and a probe runs offline. The mapping is by request:
//   /api/v3/klines?symbol=S&interval=I&limit=N   -> klines_S_I.json
//   /api/v3/klines?...&endTime=...               -> hist_S_I.json, or "[]"
//                                                   when the file is missing
//                                                   (the history has ended)
//   /api/v3/ticker/price?symbol=S                -> price_S.json
//   /api/v3/ticker/tradingDay?symbol=S           -> day_S.json (phase 42)
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
// Test build only (phase 30): what the name migration did at startup, as a
// bitmask. WM_APP_PROBE 115 on the main window. Bit 0 old settings copied,
// 1 old key deleted, 2 autostart written under the new name, 3 old autostart
// value deleted.
static int      g_probeMigrate  = 0;
// Phase 46, read on the main window: 117 what the last tray click did (0 none
// yet, 1 hid a panel in front, 2 hid a panel that had just lost activation,
// 3 raised a panel that was open behind others, 4 showed a hidden or new
// one), 118 hand-overs from a second start, 119 the tray icon's state (0
// connecting, 1 a live price, 2 a stale price, 3 offline with no price yet),
// 120 trading-day fetches that succeeded, 121 the WinHTTP access type the
// session was opened with. 78 on the panel: the status text of an empty
// chart (0 none, 1 loading, 2 no connection).
static int      g_probeTrayDecision = 0;
static volatile LONG g_probeHandovers = 0;
static int      g_probeIconState    = 0;
static volatile LONG g_probeDayFetches = 0;
static DWORD    g_probeAccessType   = 0;
static int      g_probeEmptyMsg     = 0;
// 79 on the panel: the seconds that status text counts down (-1 = none).
static int      g_probeEmptySecs    = -1;
// Phase 47, 85 on the panel: ticks of the animation clock since start. A
// timer that should be dead but is not shows as a count that keeps rising.
static volatile LONG g_probeAnimTicks = 0;
// Phase 48: monitors this machine does not have. One monitor at 100 % with
// the taskbar at the bottom shows none of the placement bugs, and the user's
// display settings are not ours to change, so the test build pretends.
// TICKER_FAKE_MON "x0,dpi[,quiet]": every window whose centre lies at screen
// x >= x0 is on a monitor at that dpi (PanelDpi), and the panel gets the
// WM_DPICHANGED Windows sends when a move takes it across - built as field
// 105 builds it - unless quiet is 1, which models a Windows that sends none
// to a hidden window. TICKER_FAKE_WORKAREA "dx,dy": GetPanelPlacement
// returns rcNormalPosition shifted by -dx,-dy, as Windows' workspace
// coordinates are with a taskbar dx wide on the left (or dy tall at the
// top). Read once at startup, before the panel exists.
static int  g_fakeMonX0 = 0, g_fakeMonDpi = 0;
static BOOL g_fakeMonQuiet = FALSE;
static int  g_fakeWorkDx = 0, g_fakeWorkDy = 0;
// 87 on the panel: WM_DPICHANGED messages the panel has handled. 89: what
// the last on-screen check did when the panel was shown (0 none yet, 1 left
// it, 2 moved it onto a monitor's work area).
static volatile LONG g_probeDpiChanges = 0;
static int      g_probeOnScreen = 0;
#endif



static long long NowUnixMs(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (long long)((t - 116444736000000000ULL) / 10000ULL);
}

// The UTC day, as days since 1970 (phase 46): the day Binance's trading-day
// statistics and today's session (phase 27) are counted in.
static long long UtcDayNow(void) {
    return NowUnixMs() / 86400000LL;
}

// How many candles of ivMs a range's home view holds (phase 41; split out
// of RangeWantFor in phase 47, so the worker thread can ask too). days is a
// RANGES[].days value; 0 when the range cannot be shown at this bar size
// (fewer than MIN_VIEW candles, or more than the buffer holds).
//
// A duration rounds UP (phase 47): 5Y is 1826 days, 260.86 weeks, and the
// truncation gave 260 - the view ended short of the five years the table
// promises (261). 1Y on 1w is 53 weeks now, for the same reason.
//
// YTD is anchored, not a duration: from the candle 1 January 00:00 UTC opens
// in to the newest one, which opens at lastOpenMs. The count grows by one
// with each new day at 1d - up to phase 46 it was computed once, and a
// followed view kept that count, so every midnight UTC pushed 1 January out
// while the header still said YTD. Rounded up, so the week that began before
// 1 January is in at 1w. With no candle yet (lastOpenMs 0: the buffer is
// being emptied for a new bar size), up to now by the clock - the same count
// once the forming candle is the newest. A buffer whose newest candle is
// from before 1 January (the year has just turned) counts nothing: 0.
static int RangeWantAt(int days, long long ivMs, long long lastOpenMs) {
    if (ivMs <= 0) return 0;
    if (days == RANGE_DAYS_MAX) return MAX_CANDLES;
    long long want;
    if (days == RANGE_DAYS_YTD) {
        SYSTEMTIME st;
        GetSystemTime(&st);
        SYSTEMTIME jan = { 0 };
        jan.wYear = st.wYear; jan.wMonth = 1; jan.wDay = 1;
        FILETIME ft;
        if (!SystemTimeToFileTime(&jan, &ft)) return 0;
        ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        long long jan1 = (long long)((t - 116444736000000000ULL) / 10000ULL);
        if (lastOpenMs <= 0)
            want = (NowUnixMs() - jan1) / ivMs + 1;   // + 1: the candle still forming
        else if (lastOpenMs < jan1)
            want = 0;
        else
            want = (lastOpenMs - jan1 + ivMs - 1) / ivMs + 1;
    } else {
        want = ((long long)days * 86400000LL + ivMs - 1) / ivMs;
    }
    if (want < MIN_VIEW || want > MAX_CANDLES) return 0;
    return (int)want;
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
// W is in device pixels; the alpha is defined on the logical width (phase
// 37), so a panel keeps its watermark strength when it moves to 150 %.
static double WatermarkAlpha(int W) {
    if (W <= 0) return WM_ALPHA_MIN;
    double a = WM_ALPHA_BASE * sqrt((double)MulDiv(W, 96, Dp(96)) / WM_W_NOMINAL);
    if (a < WM_ALPHA_MIN) a = WM_ALPHA_MIN;
    if (a > WM_ALPHA_MAX) a = WM_ALPHA_MAX;
    return a;
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

// Desktop mode without a surface (phase 46): the delay before the next try
// after `fails` failed tries in a row. The first EMBED_FAST_TRIES keep
// EMBED_RETRY_MS, which is what an Explorer restart needs (the new WorkerW
// is there after ~0.6 s, measured in phase 9); then it doubles to
// EMBED_RETRY_MAX. It was 250 ms forever: without Explorer the UI thread
// looked for Progman four times a second for as long as the process ran,
// and a hung Progman held it up to 1 s per try. TaskbarCreated - Explorer
// is back - starts the count over.
static DWORD EmbedRetryMs(int fails) {
    DWORD ms = EMBED_RETRY_MS;
    for (int i = EMBED_FAST_TRIES; i < fails && ms < EMBED_RETRY_MAX; ++i) ms *= 2;
    return (ms > EMBED_RETRY_MAX) ? EMBED_RETRY_MAX : ms;
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

static void LoadConfig(AppContext* ctx, int* outX, int* outY, int* outW, int* outH, int* outDpi) {
    ctx->symIdx     = 0;
    ctx->ivIdx      = 0;
    ctx->intervalMs = INTERVALS[0].ms;
    ctx->rangeIdx   = -1;   // phase 41: no range
    ctx->showVol    = TRUE;
    ctx->showInd    = TRUE;
    ctx->showVolDesk = FALSE;   // phase 26: the desktop starts clean
    ctx->showIndDesk = FALSE;
    ctx->showRsi     = FALSE;   // phase 39: off in both modes
    ctx->showRsiDesk = FALSE;
    ctx->lightTheme     = FALSE;   // phase 40: dark in both modes
    ctx->lightThemeDesk = FALSE;
    // Phase 49: the panel keeps the candles it has always drawn; the desktop
    // surface draws the close as a line. The surface is the wallpaper and
    // must stay calm (phase 14, pitfall 85): a line is one quiet stroke where
    // the candles are hundreds of green and red bodies, it hides nothing -
    // the mountain's opaque fill would cover the watermark - and at
    // 3840x1600 it draws in 1.0 ms against 6.3 for the candles.
    ctx->chartType     = CHART_CANDLES;
    ctx->chartTypeDesk = CHART_LINE;
    *outX = GEOM_UNSET; *outY = GEOM_UNSET;
    *outW = 0; *outH = 0; *outDpi = 0;

    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return;
    }
    DWORD sy = RegReadDword(k, L"SymbolIndex",   0);
    DWORD iv = RegReadDword(k, L"IntervalIndex", 0);
    DWORD rg = RegReadDword(k, L"RangeIndex", 0);   // phase 41: range + 1, 0 = none
    DWORD w  = RegReadDword(k, L"PanelWidth",    0);
    DWORD h  = RegReadDword(k, L"PanelHeight",   0);
    DWORD hasPos = RegReadDword(k, L"PanelHasPos", 0);
    DWORD px = RegReadDword(k, L"PanelX", 0);
    DWORD py = RegReadDword(k, L"PanelY", 0);
    DWORD dev = RegReadDword(k, L"PanelGeomDevice", 0);   // phase 37
    DWORD gdpi = RegReadDword(k, L"PanelGeomDpi", 0);     // phase 48
    DWORD sv = RegReadDword(k, L"ShowVolume", 1);   // phase 22, on by default
    DWORD si = RegReadDword(k, L"ShowIndicators", 1);   // phase 25, on by default
    DWORD svd = RegReadDword(k, L"ShowVolumeDesktop", 0);       // phase 26, OFF by default
    DWORD sid = RegReadDword(k, L"ShowIndicatorsDesktop", 0);
    DWORD sr  = RegReadDword(k, L"ShowRsi", 0);          // phase 39, off by default
    DWORD srd = RegReadDword(k, L"ShowRsiDesktop", 0);
    DWORD lt  = RegReadDword(k, L"LightTheme", 0);       // phase 40, dark by default
    DWORD ltd = RegReadDword(k, L"LightThemeDesktop", 0);
    // Phase 49: the type itself, not + 1 - RangeIndex adds 1 only because
    // "no range" (-1) is a value; every type is a value, and a missing entry
    // falls back to the mode's default, as ShowVolume's does.
    DWORD ct  = RegReadDword(k, L"ChartType", CHART_CANDLES);
    DWORD ctd = RegReadDword(k, L"ChartTypeDesktop", CHART_LINE);
    RegCloseKey(k);
    ctx->showVol = (sv != 0);
    ctx->showInd = (si != 0);
    ctx->showVolDesk = (svd != 0);
    ctx->showIndDesk = (sid != 0);
    ctx->showRsi     = (sr != 0);
    ctx->showRsiDesk = (srd != 0);
    ctx->lightTheme     = (lt != 0);
    ctx->lightThemeDesk = (ltd != 0);
    // A value out of range (a hand edit, a newer version's type) keeps the
    // default, as the indices below do.
    if (ct  < (DWORD)CHART_TYPE_COUNT) ctx->chartType     = (int)ct;
    if (ctd < (DWORD)CHART_TYPE_COUNT) ctx->chartTypeDesk = (int)ctd;

    // Bounds check. A registry edited by hand, or left behind by a newer
    // version with more symbols, must not be able to index outside the
    // table.
    if (sy < (DWORD)SYMBOL_COUNT)   ctx->symIdx = (int)sy;
    if (iv < (DWORD)INTERVAL_COUNT) {
        ctx->ivIdx      = (int)iv;
        ctx->intervalMs = INTERVALS[iv].ms;
    }
    // Phase 41. Whether it can be shown at the saved bar size is decided by
    // ResetView when the surface opens.
    if (rg >= 1 && rg <= (DWORD)RANGE_COUNT) ctx->rangeIdx = (int)rg - 1;
    if (w >= 240 && w <= 8192) *outW = (int)w;
    if (h >= 160 && h <= 8192) *outH = (int)h;

    // The position can be negative on a monitor to the left of or above
    // the primary one, so it is read as signed. PanelHasPos distinguishes
    // "not saved" from "saved as 0,0".
    if (hasPos) { *outX = (int)(LONG)px; *outY = (int)(LONG)py; }

    // Until phase 37 the process was DPI-unaware, and the geometry was saved
    // in Windows' virtualized coordinates: device pixels divided by the
    // system scale. Without PanelGeomDevice they are scaled up once, so the
    // panel opens where and as large as it was; SaveGeometry writes device
    // pixels and the flag from then on. Exact on one monitor; with monitors
    // of mixed scale the virtualized space was the system dpi's too.
    if (!dev) {
        int sd = (int)GetDpiForSystem();
        if (sd > 0 && sd != 96) {
            if (*outW > 0) *outW = MulDiv(*outW, sd, 96);
            if (*outH > 0) *outH = MulDiv(*outH, sd, 96);
            if (hasPos) { *outX = MulDiv(*outX, sd, 96); *outY = MulDiv(*outY, sd, 96); }
        }
    }
    // Phase 48: the dpi the size was saved at. Device pixels are only half a
    // unit - 1920x1080 at 144 is the panel 1280x720 is at 96 - and
    // PlacePopupInitially scales the size when the panel opens on a monitor
    // at another dpi, as Windows' WM_DPICHANGED would have for a running one.
    // Geometry from phases 37-47 has no dpi and is taken as it is.
    else if (gdpi >= 48 && gdpi <= 480) {
        *outDpi = (int)gdpi;
    }
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

// The path in quotation marks, "C:\Folder with spaces\TickC.exe", and from
// phase 46 the flag that says the start is the sign-in's: a start from
// Explorer shows the panel, one at sign-in stays in the notification area.
// `bare` gives the value as phases 13-45 wrote it, without the flag. FALSE
// when the path does not fit in MAX_PATH - a truncated path must never end up
// in the registry. The buffers are AUTOSTART_CCH: the path, two quotes and
// the flag.
#define AUTOSTART_ARG  L"--autostart"
#define AUTOSTART_CCH  (MAX_PATH + 16)
static BOOL AutostartCommand(wchar_t* out, size_t cch, BOOL bare) {
    wchar_t exe[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return FALSE;
    return swprintf_s(out, cch, bare ? L"\"%s\"" : L"\"%s\" " AUTOSTART_ARG, exe) > 0;
}

// Is the Run value one of ours for this exe, with or without the flag?
// `flagged` says which. A value written by phase 45 or earlier is the bare
// path, and it must still count as "this exe" - otherwise the first click on
// a checked "Start at sign-in" after the update would rewrite the value
// instead of removing it.
static BOOL AutostartIsOurs(BOOL* flagged) {
    wchar_t have[AUTOSTART_CCH], want[AUTOSTART_CCH], bare[AUTOSTART_CCH];
    DWORD cb = sizeof(have);
    *flagged = FALSE;
    if (!AutostartCommand(want, AUTOSTART_CCH, FALSE) ||
        !AutostartCommand(bare, AUTOSTART_CCH, TRUE)) return FALSE;
    // Wrong type, or too long for the buffer (ERROR_MORE_DATA), cannot
    // possibly be our path.
    if (RegGetValueW(HKEY_CURRENT_USER, AUTOSTART_KEY, AUTOSTART_VALUE,
                     RRF_RT_REG_SZ, NULL, have, &cb) != ERROR_SUCCESS) return FALSE;
    if (_wcsicmp(have, want) == 0) { *flagged = TRUE; return TRUE; }
    return _wcsicmp(have, bare) == 0;
}

// The check mark in the tray menu: does the value exist, whatever its type and
// content? It can point somewhere other than the running exe; ToggleAutostart
// decides that.
static BOOL AutostartPresent(void) {
    return RegGetValueW(HKEY_CURRENT_USER, AUTOSTART_KEY, AUTOSTART_VALUE,
                        RRF_RT_ANY, NULL, NULL, NULL) == ERROR_SUCCESS;
}

// Click on "Start at sign-in":
//   value == current path -> delete (with or without --autostart, phase 46)
//   no value              -> write current path and --autostart
//   anything else         -> write current path and --autostart (the exe has moved)
// The last branch is why the check mark means "the value exists", not "the
// value is correct": a click on a checked but stale entry should fix the
// path, not turn autostart off.
static void ToggleAutostart(void) {
    if (g_isDuplicate) return;
    wchar_t want[AUTOSTART_CCH];
    if (!AutostartCommand(want, AUTOSTART_CCH, FALSE)) return;
    BOOL flagged;
    BOOL same = AutostartIsOurs(&flagged);   // phase 46: with or without the flag

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

// Phase 46: a Run value in the bare form (phases 13-45) gets the flag, so the
// next sign-in starts quietly as before - without it, that start would now
// open the panel. Runs at every start of a main instance, and only writes
// when the value is ours and bare; a value pointing elsewhere is left to
// ToggleAutostart, as before.
static void UpgradeAutostart(void) {
    BOOL flagged;
    wchar_t want[AUTOSTART_CCH];
    if (!AutostartIsOurs(&flagged) || flagged) return;
    if (!AutostartCommand(want, AUTOSTART_CCH, FALSE)) return;
    RegSetKeyValueW(HKEY_CURRENT_USER, AUTOSTART_KEY, AUTOSTART_VALUE, REG_SZ, want,
                    (DWORD)((wcslen(want) + 1) * sizeof(wchar_t)));
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
        wchar_t want[AUTOSTART_CCH];
        if (!haveNew && AutostartCommand(want, AUTOSTART_CCH, FALSE)) {
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
    DWORD rg = (DWORD)(ctx->rangeIdx + 1);   // phase 41
    RegSetValueExW(k, L"RangeIndex",    0, REG_DWORD, (const BYTE*)&rg, sizeof(rg));
    DWORD sv = ctx->showVol ? 1 : 0;
    RegSetValueExW(k, L"ShowVolume",    0, REG_DWORD, (const BYTE*)&sv, sizeof(sv));
    DWORD si = ctx->showInd ? 1 : 0;
    RegSetValueExW(k, L"ShowIndicators", 0, REG_DWORD, (const BYTE*)&si, sizeof(si));
    DWORD svd = ctx->showVolDesk ? 1 : 0, sid = ctx->showIndDesk ? 1 : 0;
    RegSetValueExW(k, L"ShowVolumeDesktop",     0, REG_DWORD, (const BYTE*)&svd, sizeof(svd));
    RegSetValueExW(k, L"ShowIndicatorsDesktop", 0, REG_DWORD, (const BYTE*)&sid, sizeof(sid));
    DWORD sr = ctx->showRsi ? 1 : 0, srd = ctx->showRsiDesk ? 1 : 0;
    RegSetValueExW(k, L"ShowRsi",        0, REG_DWORD, (const BYTE*)&sr, sizeof(sr));
    RegSetValueExW(k, L"ShowRsiDesktop", 0, REG_DWORD, (const BYTE*)&srd, sizeof(srd));
    DWORD lt = ctx->lightTheme ? 1 : 0, ltd = ctx->lightThemeDesk ? 1 : 0;
    RegSetValueExW(k, L"LightTheme",        0, REG_DWORD, (const BYTE*)&lt, sizeof(lt));
    RegSetValueExW(k, L"LightThemeDesktop", 0, REG_DWORD, (const BYTE*)&ltd, sizeof(ltd));
    DWORD ct = (DWORD)ctx->chartType, ctd = (DWORD)ctx->chartTypeDesk;   // phase 49
    RegSetValueExW(k, L"ChartType",        0, REG_DWORD, (const BYTE*)&ct, sizeof(ct));
    RegSetValueExW(k, L"ChartTypeDesktop", 0, REG_DWORD, (const BYTE*)&ctd, sizeof(ctd));
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
    g_savedPanelDpi = g_Ctx.sty.dpi;   // phase 48: the unit of w and h
    if (g_isDuplicate) return;
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0,
                        KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS) {
        return;
    }
    DWORD dw = (DWORD)w, dh = (DWORD)h;
    DWORD dx = (DWORD)(LONG)x, dy = (DWORD)(LONG)y, one = 1, gd = (DWORD)g_savedPanelDpi;
    RegSetValueExW(k, L"PanelWidth",  0, REG_DWORD, (const BYTE*)&dw, sizeof(dw));
    RegSetValueExW(k, L"PanelHeight", 0, REG_DWORD, (const BYTE*)&dh, sizeof(dh));
    RegSetValueExW(k, L"PanelX",      0, REG_DWORD, (const BYTE*)&dx, sizeof(dx));
    RegSetValueExW(k, L"PanelY",      0, REG_DWORD, (const BYTE*)&dy, sizeof(dy));
    RegSetValueExW(k, L"PanelHasPos", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
    RegSetValueExW(k, L"PanelGeomDevice", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));   // phase 37
    RegSetValueExW(k, L"PanelGeomDpi", 0, REG_DWORD, (const BYTE*)&gd, sizeof(gd));        // phase 48
    RegCloseKey(k);
}

// The panel's WINDOWPLACEMENT. rcNormalPosition is in workspace coordinates
// (pitfall 42), which equal screen coordinates here; the test build can
// shift them as a taskbar on the left or at the top would (phase 48, see
// g_fakeWorkDx). Every read of the panel's placement goes through here.
static BOOL GetPanelPlacement(HWND hwnd, WINDOWPLACEMENT* wp) {
    wp->length = sizeof(WINDOWPLACEMENT);
    if (!GetWindowPlacement(hwnd, wp)) return FALSE;
#ifdef TICKER_PROBE
    OffsetRect(&wp->rcNormalPosition, -g_fakeWorkDx, -g_fakeWorkDy);
#endif
    return TRUE;
}

// And the one write (phase 48): a saved rectangle goes back through
// SetWindowPlacement, in the coordinates GetWindowPlacement gave it in. Up to
// phase 47 PlacePopupInitially restored it with SetWindowPos, which takes
// screen coordinates, so with a taskbar on the left or at the top every save
// and restore moved the panel by the bar's width (review A3). The pair
// undoes itself whatever origin Windows gives this frameless popup's
// workspace - the docs say the work area's, and a popup that maximizes to
// the whole monitor (pitfall 22) may not get one at all; neither can be seen
// on this machine. Called on the new, hidden panel only: SW_HIDE keeps it so
// until TogglePopup shows it.
static void SetPanelPlacement(HWND hwnd, int x, int y, int w, int h) {
    WINDOWPLACEMENT wp;
    if (!GetPanelPlacement(hwnd, &wp)) return;
    wp.flags   = 0;
    wp.showCmd = SW_HIDE;
    SetRect(&wp.rcNormalPosition, x, y, x + w, y + h);
#ifdef TICKER_PROBE
    OffsetRect(&wp.rcNormalPosition, g_fakeWorkDx, g_fakeWorkDy);
#endif
    SetWindowPlacement(hwnd, &wp);
}

// Saves position and size as the window stands NOW. A minimized or
// maximized window is not saved as such - then we would remember a
// taskbar strip or the whole screen as "the user's size".
// GetWindowPlacement gives the restored geometry in both cases.
static void SaveWindowPlacement(HWND hwnd) {
    if (!hwnd) return;
    WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
    if (!GetPanelPlacement(hwnd, &wp)) return;
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

    // "DPI-scaled 1280x720": the process is per-monitor aware (phase 37), so
    // this is 1920x1080 device pixels on a 150 % monitor.
    int w = Dp(POPUP_W);
    int h = Dp(POPUP_H);

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


// Merges incoming candles with the buffer on openTime: the same timestamp
// updates (the last candle changes while it forms), newer ones are appended.
// This is what makes panning stable - without it the indices would shift
// with every fetch and the view would drift away.
static void MergeCandles(AppContext* ctx, const Candle* in, int count) {
    if (count <= 0) return;

    // Gap between the buffer and the new set (the panel has been closed for
    // a while) -> start over. Otherwise the chart would draw a continuous
    // curve straight across dead time.
    // Phase 44: a gap is any missing candle, one interval of slack and not
    // two. WorkerFetchKlines now sizes the fetch to the gap, so a response
    // reaches back to the last buffered candle and this never fires on it;
    // it fires when the gap is longer than one fetch can close (SEED_COUNT
    // candles) or the clock is far behind the server's. The two-interval
    // slack let a response that began two intervals on through, and the
    // candle between stayed missing for good - with the old fixed limit of 3
    // whenever the newest candle was 4 intervals on, and after a seed at
    // exactly 361 (see WorkerFetchKlines).
    if (ctx->candleCount > 0 &&
        in[0].openTime > ctx->candles[ctx->candleCount - 1].openTime + ctx->intervalMs) {
        ctx->candleCount = 0;
        ctx->ch.viewStart   = 0;
        ctx->ch.viewCount   = 0;
        ctx->ch.followLive  = TRUE;
        ctx->ch.dispValid   = FALSE;   // new buffer: nothing to ease from
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
                if (ctx->ch.viewStart > 0) ctx->ch.viewStart--;
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
    if (ctx->ch.followLive) {
        // YTD (phase 47) holds one more candle with each new day, so 1
        // January stays the first; a duration (1D ... 5Y) keeps its count
        // and moves on. At the turn of the year the count is 0 until a week
        // of the new one is in: the home is left, and the view keeps its
        // size, as after a pan. The cell goes out and the header shows the
        // span; R or the cell asks again, and ends the range then.
        if (ctx->rangeWant > 0 && ctx->rangeYtd && ctx->candleCount > 0) {
            ctx->rangeWant = RangeWantAt(RANGE_DAYS_YTD, ctx->intervalMs,
                                         ctx->candles[ctx->candleCount - 1].openTime);
        }
        if (ctx->rangeWant > 0) {
            // At home in a range (phase 41): as many as it wants, up to what
            // the buffer holds - the backfill brings the rest.
            ctx->ch.viewCount = (ctx->candleCount < ctx->rangeWant) ? ctx->candleCount : ctx->rangeWant;
        } else if (ctx->ch.viewCount <= 0) {
            ctx->ch.viewCount = (ctx->candleCount < DEFAULT_VIEW) ? ctx->candleCount : DEFAULT_VIEW;
        }
        ctx->ch.viewStart = ctx->candleCount - ctx->ch.viewCount;
    }
    ClampView(&ctx->ch, ctx->candleCount);
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
        if (ctx->ch.viewCount > 0) {
            ctx->ch.viewStart += k;
            ClampView(&ctx->ch, ctx->candleCount);
        }
        // At home in a range (phase 41) the view grows with the backfill
        // until it holds what the range wants.
        if (ctx->ch.followLive && ctx->rangeWant > 0) {
            int vc = (ctx->candleCount < ctx->rangeWant) ? ctx->candleCount : ctx->rangeWant;
            ctx->ch.viewCount = vc;
            ctx->ch.viewStart = ctx->candleCount - vc;
            ClampView(&ctx->ch, ctx->candleCount);
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
    else if (wcsstr(path, L"/tradingDay")) swprintf_s(file, MAX_PATH, L"%s\\day_%s.json", g_fixtureDir, sym);
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

    // Shutdown (phase 44): the stop event is set before WinMain closes the
    // session, so a cycle with two or three requests left gives up on the
    // next one instead of starting it.
    if (WaitForSingleObject(ctx->hStopEvent, 0) == WAIT_OBJECT_0) return FALSE;

    if (!ctx->hConnect) {
        // hSession is read and used under the lock (phase 44): at exit
        // WinMain takes it under the lock, clears it and closes it, while
        // this thread may still be running. WinHttpConnect does no network
        // I/O - it only creates the handle - so the lock is held briefly.
        EnterCriticalSection(&ctx->lock);
        if (ctx->hSession)
            ctx->hConnect = WinHttpConnect(ctx->hSession, L"api.binance.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        LeaveCriticalSection(&ctx->lock);
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

// The quoted number after "key": in a flat JSON object (phase 42). FALSE when
// the key is missing or the value is not a number.
static BOOL ParseKeyNumber(const char* json, const char* key, double* out) {
    const char* pos = strstr(json, key);
    if (!pos) return FALSE;
    pos += strlen(key);
    return *pos == '"' && ParseQuotedNumber(pos, out) != NULL;
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
    int limit = SEED_COUNT;

    EnterCriticalSection(&ctx->lock);
    seed = (ctx->candleCount == 0);
    gen  = ctx->configGen;
    si   = ctx->symIdx;
    ii   = ctx->ivIdx;
    ivMs = ctx->intervalMs;
    if (!seed) {
        long long lastT = ctx->candles[ctx->candleCount - 1].openTime;
        long long gap   = NowUnixMs() - lastT;
        if (gap > 5 * ivMs) {
            seed = TRUE;
        } else if (ivMs > 0) {
            // The limit follows the gap (phase 44). It was a fixed 3, and
            // MergeCandles restarts only when the response begins more than
            // two intervals after the last buffered candle. With the newest
            // candle 3 intervals on, the response began right after the last
            // one, which kept its half-formed data for good; 4 on, one candle
            // was never fetched - a permanent hole. gap / iv + 1 candles
            // reach back to the last buffered one, and one more is slack for
            // a clock a little behind the server's, so the response always
            // overlaps it. A clock ahead only makes the overlap larger, a
            // negative gap gives the floor of 3.
            long long want = gap / ivMs + 2;
            if (want < 3)          want = 3;
            if (want > SEED_COUNT) want = SEED_COUNT;
            limit = (int)want;
        }
    }
    LeaveCriticalSection(&ctx->lock);

    wchar_t path[160];
    swprintf_s(path, 160, L"/api/v3/klines?symbol=%s&interval=%s&limit=%d",
               SYMBOLS[si].api, INTERVALS[ii].api, seed ? SEED_COUNT : limit);

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
    ctx->lastUpdMs = NowUnixMs();   // phase 42: the quote line's "At"

    if (ctx->ch.followLive) {
        int vc = (ctx->rangeWant > 0) ? ctx->rangeWant : ctx->ch.viewCount;   // phase 41
        if (vc <= 0) vc = DEFAULT_VIEW;
        if (vc > ctx->candleCount) vc = ctx->candleCount;
        ctx->ch.viewCount = vc;
        ctx->ch.viewStart = ctx->candleCount - vc;
        if (ctx->ch.viewStart < 0) ctx->ch.viewStart = 0;
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

// The trading day (phase 42): one small request for the UTC day's open,
// high, low and volume. Same configGen guard as the candles. Not a network
// health signal: a failure here leaves the old values (or none) and does
// not count toward the backoff - the candles decide that.
static void WorkerFetchDay(AppContext* ctx) {
    unsigned gen;
    int si;
    EnterCriticalSection(&ctx->lock);
    gen = ctx->configGen;
    si  = ctx->symIdx;
    LeaveCriticalSection(&ctx->lock);

    wchar_t path[96];
    swprintf_s(path, 96, L"/api/v3/ticker/tradingDay?symbol=%s", SYMBOLS[si].api);
    char buf[1024];
    if (!HttpGet(ctx, path, buf, (DWORD)sizeof(buf))) return;

    double o, h, l, v;
    if (!ParseKeyNumber(buf, "\"openPrice\":", &o) || !ParseKeyNumber(buf, "\"highPrice\":", &h) ||
        !ParseKeyNumber(buf, "\"lowPrice\":", &l)  || !ParseKeyNumber(buf, "\"volume\":", &v) ||
        !PriceSane(o) || !PriceSane(h) || !PriceSane(l) || l > h || !(v >= 0.0 && v < 1.0e15)) {
#ifdef TICKER_PROBE
        InterlockedIncrement(&g_probeRejects);
#endif
        return;
    }
    EnterCriticalSection(&ctx->lock);
    if (ctx->configGen == gen) {
        ctx->dayOpen = o; ctx->dayHigh = h; ctx->dayLow = l; ctx->dayVol = v;
        ctx->dayValid = TRUE;
        ctx->dayUtc   = UtcDayNow();   // phase 46
        ctx->dayRefresh = FALSE;       // done - a failure leaves it for the next cycle
    }
    LeaveCriticalSection(&ctx->lock);
#ifdef TICKER_PROBE
    InterlockedIncrement(&g_probeDayFetches);
#endif
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
    int dayCycle = 0;   // phase 42: cycles since the last trading-day fetch
    // The history's own backoff (phase 46): failures in a row, when the next
    // try may go, and the config they were counted for - another symbol or
    // interval starts clean. The thread's alone, so no lock.
    int       histFails = 0;
    ULONGLONG histNext  = 0;
    unsigned  histGen   = 0;
    HANDLE waits[2] = { ctx->hStopEvent, ctx->hWakeEvent };

    for (;;) {
        EnterCriticalSection(&ctx->lock);
        HWND hp   = ctx->hPopup;
        BOOL hist = ctx->histPending;
        unsigned gen = ctx->configGen;
        // Phase 42; phase 46: statistics from another UTC day, or a panel
        // just shown, count as none - fetched in this cycle.
        BOOL dayOk = ctx->dayValid && ctx->dayUtc == UtcDayNow() && !ctx->dayRefresh;
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
        //
        // Phase 46: the candles are fetched whatever the history did, and
        // only they decide the line's health. A failed history used to skip
        // them and count as a network failure, so a history path that kept
        // failing froze the live chart and showed "offline" while the live
        // fetches would have worked. WorkerFetchHistory releases histPending
        // on a failure, and the UI asks again; the retry waits out a backoff
        // of its own (NetBackoffMs), so a lasting failure is not asked every
        // three seconds.
        BOOL ok;
        if (hp && IsWindowVisible(hp)) {
            if (gen != histGen) { histGen = gen; histFails = 0; histNext = 0; }
            if (hist && GetTickCount64() >= histNext) {
                if (WorkerFetchHistory(ctx)) {
                    histFails = 0;
                } else {
                    if (histFails < 32) histFails++;
                    ULONGLONG t = GetTickCount64();
                    histNext = t + NetBackoffMs(histFails, t);
                }
            }
            ok = WorkerFetchKlines(ctx);
            // The day's statistics (phase 42): every fifth cycle (15 s) - the
            // day's open does not move, and its high, low and volume are
            // followed live from the last price in between - and at once
            // when there are none for the symbol shown.
            if (ok && (!dayOk || ++dayCycle >= 5)) {
                dayCycle = 0;
                WorkerFetchDay(ctx);
            }
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
        // NIF_MESSAGE stays (phase 44): this nid is also the one the
        // TaskbarCreated handler re-adds after an Explorer restart, and an
        // icon added without it has no callback message - it ignored every
        // click. NIM_MODIFY with the same callback changes nothing.
        ctx->nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        wcscpy_s(ctx->nid.szTip, 128, ctx->fullPriceStr);
        Shell_NotifyIconW(NIM_MODIFY, &ctx->nid);
#ifdef TICKER_PROBE
        g_probeIconState = stale ? 2 : 1;
#endif
        ctx->iconOffline = FALSE;
    }
}

// No price yet and the fetches fail (phase 46): no network at sign-in, a
// proxy, or a geo-block. UpdateIcon returns on a price of 0, and "stale"
// needs a first success, so the icon stood at "..." and the tooltip at
// "Connecting to Binance..." for good, with no hint that anything had
// failed. The dots in the stale price's dimmed green - the icon's one word
// for "not live" - and the tooltip says what the panel's empty chart says.
// Drawn once per outage, not per cycle: the icon does not change in between.
static void ShowOfflineIcon(AppContext* ctx) {
    if (ctx->iconOffline) return;
    HICON hNewIcon = RenderMicroFontIcon("...", 0xFF2F6B45);
    if (!hNewIcon) return;
    if (ctx->nid.hIcon) DestroyIcon(ctx->nid.hIcon);
    ctx->nid.hIcon  = hNewIcon;
    ctx->nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;   // see UpdateIcon
    wcscpy_s(ctx->nid.szTip, 128, L"TickC: no connection to Binance - retrying");
    Shell_NotifyIconW(NIM_MODIFY, &ctx->nid);
    ctx->iconOffline = TRUE;
#ifdef TICKER_PROBE
    g_probeIconState = 3;
#endif
}


// ---------------------------------------------------------------------------
// Chart drawing (GDI, double-buffered)
// ---------------------------------------------------------------------------



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
    int right = W - Dp(BTN_MARGIN_R);
    for (int i = BTN_COUNT - 1; i >= 0; --i) {
        out[i].right  = right;
        out[i].left   = right - Dp(BTN_W);
        out[i].top    = Dp(BTN_TOP);
        out[i].bottom = Dp(BTN_TOP) + Dp(BTN_H);
        right = out[i].left - Dp(BTN_GAP);
    }
}

// Which button is the mouse pointing at? -1 outside all of them.
static int ButtonHit(const RECT* btns, int x, int y) {
    for (int i = 0; i < BTN_COUNT; ++i) {
        if (PtInRect2(&btns[i], x, y)) return i;
    }
    return -1;
}

// The caption button at (x, y) on a panel W wide (phase 47). Maximized, the
// panel's top and right edges are the screen's, and the buttons reach them
// as Windows' own do (Fitts's law): a pointer flung into the top right
// corner is on the close cross. Before, the 6 px above the buttons and the
// 8 px right of the cross were caption, and the fling moved the window
// instead. Restored, those margins are the resize border, and the boxes are
// the drawn ones. Every hit test of the buttons comes here - WM_NCHITTEST,
// hover, press, release and the double-click - while the drawing keeps
// ButtonLayout's boxes (pitfall 14: one source for what a click hits).
static int ButtonHitAt(int W, BOOL zoomed, int x, int y) {
    RECT b[BTN_COUNT];
    ButtonLayout(W, b);
    if (zoomed) {
        for (int i = 0; i < BTN_COUNT; ++i) b[i].top = 0;
        b[BTN_CLOSE].right = W;
    }
    return ButtonHit(b, x, y);
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
    return rightBound < leftBound - Dp(HDR_GAP);
}

// Right limit for the header's row 2: the price axis's top label sits at
// y = top +- 8 from x = edge + AXIS_LBL_GAP, and the row must keep HDR_GAP
// of space to it. The range field and the settings cell read this.
static int HeaderRow2Limit(int W) {
    return W - ChartAxisW(g_Ctx.sty.dpi) + Dp(AXIS_LBL_GAP) - Dp(HDR_GAP);
}

// Row 2 must hold every range cell, the interval cell and the settings cell
// at the minimum width (phase 42; through the 1Y pill in phase 41, through
// VOL before). If a table or a width grows past that, the build stops here -
// and the rule in ToolbarLayout, which hides cells from the right, never
// becomes what the user sees on a panel of legal size. POPUP_MIN_W is not
// raised for a cell: it also guards which geometry the registry may give
// back. At 400 px the row ends at x = 280 of 312.
C_ASSERT(RANGE_COUNT == 8);   // six narrow cells and two wide ones below
C_ASSERT(PAD_L + 6 * TBAR_RANGE_W + 2 * TBAR_RANGE_WIDE_W + RANGE_COUNT * TBAR_CELL_GAP +
         TBAR_IVDD_W + TBAR_GROUP_GAP + TBAR_GEAR_W
         <= POPUP_MIN_W - PAD_R + AXIS_LBL_GAP - HDR_GAP);

// One cell's width at 96 dpi. ToolbarLayout and ToolbarMinW both read it, so
// the sums cannot drift apart.
static int ToolbarCellW(int i) {
    if (i == TBAR_SYM)  return TBAR_SYM_W;
    if (i == TBAR_IV)   return TBAR_IVDD_W;
    if (i == TBAR_GEAR) return TBAR_GEAR_W;
    return (wcslen(RANGES[i - TBAR_RANGE_FIRST].label) >= 3) ? TBAR_RANGE_WIDE_W : TBAR_RANGE_W;
}

// The header's clickable cells. A pure function of the width, like
// ButtonLayout, and for the same reason: painting, WM_NCHITTEST, hover and
// click all read this. The symbol sits in row 1. In row 2 the settings cell
// takes the right end and the range field fills from the left; a cell that
// would reach the settings cell is hidden, and every cell after it - never
// half a cell. Hidden cells are empty rectangles that nothing hits. Returns
// the number of visible cells. The registry accepts a saved width down to
// 240 px, so the hiding can happen even though the C_ASSERT keeps it away
// from 400.
static int ToolbarLayout(int W, RECT out[TBAR_COUNT]) {
    ZeroMemory(out, sizeof(RECT) * TBAR_COUNT);
    int n = 1;
    out[TBAR_SYM].left   = Dp(PAD_L);
    out[TBAR_SYM].right  = Dp(PAD_L) + Dp(TBAR_SYM_W);
    out[TBAR_SYM].top    = Dp(QL_TOP);
    out[TBAR_SYM].bottom = Dp(QL_TOP) + Dp(QL_H);

    int limit = HeaderRow2Limit(W);
    int top = Dp(TBAR_TOP), bot = Dp(TBAR_TOP) + Dp(TBAR_H);
    int gl = limit - Dp(TBAR_GEAR_W);
    int stop = limit;
    if (gl >= Dp(PAD_L)) {
        out[TBAR_GEAR].left = gl;  out[TBAR_GEAR].right  = limit;
        out[TBAR_GEAR].top  = top; out[TBAR_GEAR].bottom = bot;
        stop = gl - Dp(TBAR_GROUP_GAP);
        n++;
    }
    int x = Dp(PAD_L);
    for (int i = TBAR_RANGE_FIRST; i <= TBAR_IV; ++i) {
        int w = Dp(ToolbarCellW(i));
        if (x + w > stop) break;
        out[i].left = x;   out[i].right  = x + w;
        out[i].top  = top; out[i].bottom = bot;
        x += w + Dp(TBAR_CELL_GAP);
        n++;
    }
    return n;
}

// Which cell is the mouse pointing at? -1 outside all of them.
static int ToolbarHit(const RECT* tb, int x, int y) {
    for (int i = 0; i < TBAR_COUNT; ++i) {
        if (PtInRect2(&tb[i], x, y)) return i;
    }
    return -1;
}

// The narrowest panel whose row 2 holds every cell at the current dpi (phase
// 37; the whole range field and the settings cell from phase 42). At 96 it
// is 368, inside POPUP_MIN_W as the C_ASSERT above demands; at 150 % the
// lengths round separately, and the larger of this and 1.5 x 400 decides.
// Same sums as ToolbarLayout and HeaderRow2Limit.
static int ToolbarMinW(void) {
    int need = Dp(PAD_L);
    for (int i = TBAR_RANGE_FIRST; i <= TBAR_IV; ++i) need += Dp(ToolbarCellW(i)) + Dp(TBAR_CELL_GAP);
    need += Dp(TBAR_GROUP_GAP) - Dp(TBAR_CELL_GAP) + Dp(TBAR_GEAR_W);
    return need + ChartAxisW(g_Ctx.sty.dpi) - Dp(AXIS_LBL_GAP) + Dp(HDR_GAP);
}

// The union of the visible cells (see ButtonStrip), both rows. Empty when
// none is visible.
static void ToolbarStrip(int W, RECT* out) {
    RECT tb[TBAR_COUNT];
    ToolbarLayout(W, tb);
    out->left = out->top = out->right = out->bottom = 0;
    BOOL any = FALSE;
    for (int i = 0; i < TBAR_COUNT; ++i) {
        if (tb[i].right <= tb[i].left) continue;
        if (!any) { *out = tb[i]; any = TRUE; continue; }
        if (tb[i].left   < out->left)   out->left   = tb[i].left;
        if (tb[i].top    < out->top)    out->top    = tb[i].top;
        if (tb[i].right  > out->right)  out->right  = tb[i].right;
        if (tb[i].bottom > out->bottom) out->bottom = tb[i].bottom;
    }
}

// The settings menu (phase 42), behind the gear. Bloomberg's "Chart Content"
// holds the studies; this holds TickC's drawing choices for the panel, and
// the theme. Each row toggles and the menu stays open, so several can be
// changed in one visit. The rows sit in the overlay's row table after the
// symbols and intervals, so hover and hit testing are the overlay's.
enum { SET_VOL, SET_IND, SET_RSI, SET_THEME, SET_COUNT };
static const wchar_t* const SET_LABEL[SET_COUNT] = {
    L"Volume", L"Averages, VWAP and levels", L"RSI 14", L"Light theme",
};
// The keys that toggle the same choices from the keyboard (phase 45). The
// menu shows them right-aligned in each row, and the tray menu is built from
// these two tables, so the names and keys cannot drift apart again - up to
// phase 44 the tray said "Volume bars", "Indicators" and "RSI band".
static const wchar_t* const SET_KEY[SET_COUNT] = { L"V", L"M", L"I", L"T" };
#define SET_CHART_ROWS   3   // SET_VOL..SET_RSI under CHART, the rest under APPEARANCE
// The chart types (phase 49) in the engine's CHART_* order: the names in the
// settings menu's CHART TYPE rows and in the tray menu's "Chart type" list,
// from one table as the settings are. The key C steps through them in this
// order; it has no row of its own to stand on, so the menu shows it in the
// section's heading and the tray on the submenu's item.
static const wchar_t* const CHART_TYPE_LABEL[CHART_TYPE_COUNT] = {
    L"Candles", L"OHLC bars", L"Line", L"Mountain",
};
#define CHART_TYPE_KEY   L"C"

static BOOL SettingOn(const AppContext* ctx, int k) {
    switch (k) {
        case SET_VOL:   return ShowVolNow(ctx);
        case SET_IND:   return ShowIndNow(ctx);
        case SET_RSI:   return ShowRsiNow(ctx);
        case SET_THEME: return ThemeNow(ctx) == &APP_THEME_LIGHT;
    }
    return FALSE;
}

static void SetShowVolume(AppContext* ctx, BOOL on);
static void SetShowIndicators(AppContext* ctx, BOOL on);
static void SetShowRsi(AppContext* ctx, BOOL on);
static void SetLightTheme(AppContext* ctx, BOOL on);
static void SetChartType(AppContext* ctx, int t);   // phase 49
static void SettingToggle(AppContext* ctx, int k) {
    BOOL on = !SettingOn(ctx, k);
    switch (k) {
        case SET_VOL:   SetShowVolume(ctx, on);     break;
        case SET_IND:   SetShowIndicators(ctx, on); break;
        case SET_RSI:   SetShowRsi(ctx, on);        break;
        case SET_THEME: SetLightTheme(ctx, on);     break;
    }
}

// Does the UI want older candles right now? Today's and yesterday's
// sessions (phase 27/28, with the indicators on) or a range whose home view
// is longer than the buffer (phase 41, in any mode - the user asked for
// it). One definition, so WM_APP_DATA and probe field 54 cannot disagree
// (pitfall 92). Called under the lock.
static BOOL AppWantsHistory(const AppContext* ctx) {
    if (ctx->candleCount > 0 && !ctx->histDone && ctx->rangeWant > ctx->candleCount) return TRUE;
    return ShowIndNow(ctx) && SessionsNeedHistory(ctx->candles, ctx->candleCount,
                                                  ctx->intervalMs, ctx->histDone);
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





#define OVL_ROWS_MAX  20    // phase 49: 16 until the chart types' four rows
#define OVL_ROW_H     22
#define OVL_ROW_MIN   16    // phase 49: the settings menu's rows on a low panel
#define OVL_COL_W     104
#define OVL_PAD       10
#define OVL_HDR_H     18
#define OVL_DD_W      64    // the interval dropdown (phase 41)
#define OVL_DD_PAD    4
// The symbol dropdown (phase 45): "BNB/USDT" is 53 px in the small font at
// 96 dpi (82 at 144, 105 at 192, measured with GetTextExtentPoint32W), and a
// row gives its label the width less 2 x OVL_DD_PAD and the 6 px inset. 84
// leaves the label 17 px of air, and overhangs the 74 px cell by 10 as the
// interval list overhangs its cell.
#define OVL_SYMDD_W   84
#define OVL_SET_W     208   // the settings menu (phase 42)
#define OVL_SET_FIRST (SYMBOL_COUNT + INTERVAL_COUNT)   // its rows follow the choices
// The chart types' rows (phase 49), after the settings'. A section of their
// own, last: the rows above keep their places and their order for the
// pointer and the arrows, and a radio group reads as one block.
#define OVL_TYPE_FIRST (OVL_SET_FIRST + SET_COUNT)
C_ASSERT(OVL_TYPE_FIRST + CHART_TYPE_COUNT <= OVL_ROWS_MAX);

typedef struct {
    RECT box;                    // the whole overlay
    RECT rows[OVL_ROWS_MAX];     // one per choice
    int  count;                  // SYMBOL_COUNT, INTERVAL_COUNT, then SET_COUNT (phase 42)
    RECT symHdr, ivHdr;          // the headings (the settings menu: CHART, APPEARANCE)
    RECT typeHdr;                // phase 49: the settings menu's CHART TYPE
} OverlayRects;

static void OverlayLayout(int W, int H, OverlayRects* r) {
    // Zeroed completely. The unused rows past count are otherwise stack
    // garbage, and then the function is no longer pure - two calls with the
    // same input give different contents. The unit test caught exactly that.
    // OverlayHit only goes up to count, so the garbage was harmless today;
    // this closes the class.
    memset(r, 0, sizeof(*r));

    // The interval dropdown (phase 41): one column right under the interval
    // pill. The symbol rows stay empty rectangles, so the indices - and the
    // click, hover and highlight code - are the picker's. If the pill is not
    // shown (a panel narrower than the minimum), the picker is laid out.
    // The settings menu (phase 42): right-aligned under the gear, two
    // headed sections. Same fallback as the dropdown.
    // The symbol dropdown (phase 45): the interval dropdown's shape under
    // the symbol cell in row 1, with the interval rows left empty. It opens
    // over the range field, inside the header band - WM_NCHITTEST gives the
    // open box HTCLIENT, otherwise its top row would be caption there.
    if (g_Ctx.overlayKind == 3) {
        RECT tb[TBAR_COUNT];
        ToolbarLayout(W, tb);
        const RECT* a = &tb[TBAR_SYM];
        if (a->right > a->left) {
            const int pad = Dp(OVL_DD_PAD), rowH = Dp(OVL_ROW_H), bw = Dp(OVL_SYMDD_W);
            int bx = a->left, by = a->bottom + Dp(2);
            int bh = pad * 2 + SYMBOL_COUNT * rowH;
            if (bx + bw > W) bx = W - bw;
            if (bx < 0) bx = 0;
            if (by + bh > H) bh = H - by;
            r->box.left = bx; r->box.top = by;
            r->box.right = bx + bw; r->box.bottom = by + bh;
            r->count = SYMBOL_COUNT;
            for (int i = 0; i < SYMBOL_COUNT; ++i) {
                RECT* q = &r->rows[i];
                q->left = bx + pad; q->right = bx + bw - pad;
                q->top = by + pad + i * rowH; q->bottom = q->top + rowH;
                if (q->bottom > r->box.bottom) q->bottom = r->box.bottom;
                if (q->top >= q->bottom) { q->left = q->right = q->top = q->bottom = 0; }
            }
            return;
        }
    }

    if (g_Ctx.overlayKind == 2) {
        RECT tb[TBAR_COUNT];
        ToolbarLayout(W, tb);
        const RECT* a = &tb[TBAR_GEAR];
        if (a->right > a->left) {
            const int pad = Dp(OVL_DD_PAD), hdrH = Dp(OVL_HDR_H), bw = Dp(OVL_SET_W);
            int bx = a->right - bw, by = a->bottom + Dp(2);
            if (bx < 0) bx = 0;
            // Phase 49: a third section, CHART TYPE, with a row per type -
            // 45 + 238 px at 96 dpi, more than a panel lower than 283 px
            // has under the gear. There the rows share the room, down to
            // OVL_ROW_MIN (17 px at the 250 px minimum, where the 11 px
            // font and the 11 px box still fit), so every row stays in the
            // menu; below that the box is cut at H, as before.
            const int nRows = SET_COUNT + CHART_TYPE_COUNT;
            int rowH = Dp(OVL_ROW_H);
            int room = H - by - pad * 2 - hdrH * 3;
            if (nRows * rowH > room) {
                rowH = room / nRows;
                if (rowH < Dp(OVL_ROW_MIN)) rowH = Dp(OVL_ROW_MIN);
            }
            int bh = pad * 2 + hdrH * 3 + nRows * rowH;
            if (by + bh > H) bh = H - by;
            r->box.left = bx; r->box.top = by;
            r->box.right = bx + bw; r->box.bottom = by + bh;
            r->count = OVL_TYPE_FIRST + CHART_TYPE_COUNT;
            int y = by + pad;
            for (int k = 0; k < SET_COUNT + CHART_TYPE_COUNT; ++k) {
                if (k == 0 || k == SET_CHART_ROWS || k == SET_COUNT) {
                    RECT* h = (k == 0) ? &r->symHdr : (k == SET_COUNT) ? &r->typeHdr : &r->ivHdr;
                    h->left = bx + pad; h->right = bx + bw - pad;
                    h->top = y; h->bottom = y + hdrH;
                    if (h->bottom > r->box.bottom) h->bottom = r->box.bottom;
                    if (h->top >= h->bottom) { h->left = h->right = h->top = h->bottom = 0; }
                    y += hdrH;
                }
                RECT* q = &r->rows[OVL_SET_FIRST + k];   // the types follow at OVL_TYPE_FIRST
                q->left = bx + pad; q->right = bx + bw - pad;
                q->top = y; q->bottom = y + rowH;
                if (q->bottom > r->box.bottom) q->bottom = r->box.bottom;
                if (q->top >= q->bottom) { q->left = q->right = q->top = q->bottom = 0; }
                y += rowH;
            }
            return;
        }
    }

    if (g_Ctx.overlayKind == 1) {
        RECT tb[TBAR_COUNT];
        ToolbarLayout(W, tb);
        const RECT* a = &tb[TBAR_IV];
        if (a->right > a->left) {
            const int pad = Dp(OVL_DD_PAD), rowH = Dp(OVL_ROW_H), bw = Dp(OVL_DD_W);
            int bx = a->left, by = a->bottom + Dp(2);
            int bh = pad * 2 + INTERVAL_COUNT * rowH;
            if (bx + bw > W) bx = W - bw;
            if (bx < 0) bx = 0;
            if (by + bh > H) bh = H - by;
            r->box.left = bx; r->box.top = by;
            r->box.right = bx + bw; r->box.bottom = by + bh;
            r->count = SYMBOL_COUNT + INTERVAL_COUNT;
            for (int i = 0; i < INTERVAL_COUNT; ++i) {
                RECT* q = &r->rows[SYMBOL_COUNT + i];
                q->left = bx + pad; q->right = bx + bw - pad;
                q->top = by + pad + i * rowH; q->bottom = q->top + rowH;
                if (q->bottom > r->box.bottom) q->bottom = r->box.bottom;
                if (q->top >= q->bottom) { q->left = q->right = q->top = q->bottom = 0; }
            }
            return;
        }
    }

    int rowsMax = (SYMBOL_COUNT > INTERVAL_COUNT) ? SYMBOL_COUNT : INTERVAL_COUNT;
    const int pad = Dp(OVL_PAD), colW0 = Dp(OVL_COL_W), hdrH = Dp(OVL_HDR_H), rowH = Dp(OVL_ROW_H);
    int boxW = pad * 3 + colW0 * 2;
    int boxH = pad * 2 + hdrH + rowsMax * rowH;

    // Centered, but never outside the panel - the panel can be smaller than
    // the box at the minimum size.
    if (boxW > W) boxW = W;
    if (boxH > H) boxH = H;
    int bx = (W - boxW) / 2, by = (H - boxH) / 2;
    if (bx < 0) bx = 0;
    if (by < 0) by = 0;

    r->box.left = bx; r->box.top = by;
    r->box.right = bx + boxW; r->box.bottom = by + boxH;

    int colW = (boxW - pad * 3) / 2;
    if (colW < 1) colW = 1;
    int c1 = bx + pad, c2 = c1 + colW + pad;
    int y0 = by + pad;

    r->symHdr.left = c1; r->symHdr.right = c1 + colW;
    r->symHdr.top  = y0; r->symHdr.bottom = y0 + hdrH;
    r->ivHdr.left  = c2; r->ivHdr.right  = c2 + colW;
    r->ivHdr.top   = y0; r->ivHdr.bottom = y0 + hdrH;

    int ry = y0 + hdrH;
    r->count = 0;
    for (int i = 0; i < SYMBOL_COUNT && r->count < OVL_ROWS_MAX; ++i) {
        RECT* q = &r->rows[r->count++];
        q->left = c1; q->right = c1 + colW;
        q->top = ry + i * rowH; q->bottom = q->top + rowH;
        if (q->bottom > r->box.bottom) q->bottom = r->box.bottom;
    }
    for (int i = 0; i < INTERVAL_COUNT && r->count < OVL_ROWS_MAX; ++i) {
        RECT* q = &r->rows[r->count++];
        q->left = c2; q->right = c2 + colW;
        q->top = ry + i * rowH; q->bottom = q->top + rowH;
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
// Called from PaintPopup, NOT from DrawChartFrame: that returns early when
// candleCount == 0, and that is exactly the state right after a config change.
// Had the call been there, the fade-out would never be drawn after a change.
static void DrawOverlay(AppContext* ctx, HDC hdc, int W, int H) {
    int a = (int)(ctx->overlayF + 0.5);
    if (a <= 0) return;

    OverlayRects r;
    OverlayLayout(W, H, &r);

    HBRUSH brBox  = CreateSolidBrush(Blend(ctx->sty.clr.bg, ctx->sty.clr.box, a));
    HBRUSH brEdge = CreateSolidBrush(Blend(ctx->sty.clr.bg, ctx->sty.clr.boxEdge, a));
    FillRect(hdc, &r.box, brBox);
    FrameRect(hdc, &r.box, brEdge);

    SelectObject(hdc, ctx->sty.fontSmall);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, Blend(ctx->sty.clr.bg, ctx->sty.clr.dim, a));
    if (r.symHdr.right > r.symHdr.left) {   // the dropdown (phase 41) has no headings
        BOOL set = (ctx->overlayKind == 2);  // phase 42: the settings menu's sections
        RECT h1 = r.symHdr, h2 = r.ivHdr;
        h1.left += Dp(6); h2.left += Dp(6);
        DrawTextW(hdc, set ? L"CHART" : L"SYMBOL",        -1, &h1, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        DrawTextW(hdc, set ? L"APPEARANCE" : L"INTERVAL", -1, &h2, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
    // Phase 49: CHART TYPE, with its key right-aligned where the rows above
    // have theirs - C steps through the four rows under it, it picks none
    // of them, so it stands on the heading and not on a row.
    if (r.typeHdr.right > r.typeHdr.left) {
        RECT h3 = r.typeHdr, k3 = r.typeHdr;
        h3.left += Dp(6);
        k3.right -= Dp(8);
        DrawTextW(hdc, L"CHART TYPE", -1, &h3, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        DrawTextW(hdc, CHART_TYPE_KEY, -1, &k3, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }

    for (int i = 0; i < r.count; ++i) {
        if (r.rows[i].right <= r.rows[i].left) continue;   // not in this layout
        if (i >= OVL_TYPE_FIRST) {
            // A chart type (phase 49): a radio group, so the current type is
            // marked as a selection is everywhere else (phase 45) - the
            // accent row with light text - and not with a check box, which
            // would read as four switches. The label lines up with the
            // settings' labels above it.
            int t = i - OVL_TYPE_FIRST;
            BOOL cur = (t == ChartTypeNow(ctx));
            if (cur) {
                SetDCBrushColor(hdc, Blend(ctx->sty.clr.bg, ctx->sty.clr.accent, a));
                FillRect(hdc, &r.rows[i], (HBRUSH)GetStockObject(DC_BRUSH));
            } else if (i == ctx->overlayHot) {
                HBRUSH brHot = CreateSolidBrush(Blend(ctx->sty.clr.bg, ctx->sty.clr.boxEdge, a / 2));
                FillRect(hdc, &r.rows[i], brHot);
                DeleteObject(brHot);
            }
            RECT t1 = r.rows[i]; t1.left += Dp(6) + Dp(11) + Dp(8);
            SetTextColor(hdc, Blend(ctx->sty.clr.bg, cur ? ctx->sty.clr.onAccent : ctx->sty.clr.text, a));
            DrawTextW(hdc, CHART_TYPE_LABEL[t], -1, &t1, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            continue;
        }
        if (i >= OVL_SET_FIRST) {
            // A setting (phase 42): a check box and its label. Checked is
            // the accent with a white tick, as the selected range cell.
            int k = i - OVL_SET_FIRST;
            if (i == ctx->overlayHot) {
                HBRUSH brHot = CreateSolidBrush(Blend(ctx->sty.clr.bg, ctx->sty.clr.boxEdge, a / 2));
                FillRect(hdc, &r.rows[i], brHot);
                DeleteObject(brHot);
            }
            int cb = Dp(11), cx0 = r.rows[i].left + Dp(6);
            int cy0 = (r.rows[i].top + r.rows[i].bottom - cb) / 2;
            RECT box = { cx0, cy0, cx0 + cb, cy0 + cb };
            HGDIOBJ oldP = SelectObject(hdc, GetStockObject(DC_PEN));
            HGDIOBJ oldB = SelectObject(hdc, GetStockObject(DC_BRUSH));
            if (SettingOn(ctx, k)) {
                SetDCBrushColor(hdc, Blend(ctx->sty.clr.bg, ctx->sty.clr.accent, a));
                FillRect(hdc, &box, (HBRUSH)GetStockObject(DC_BRUSH));
                SetDCPenColor(hdc, Blend(ctx->sty.clr.accent, ctx->sty.clr.onAccent, a));
                // Two pixels thick at 96, three at 144 (phase 48, review
                // C10: it stayed two while the box grew).
                for (int t = 0; t < Dp(2); ++t) {
                    MoveToEx(hdc, cx0 + Dp(2), cy0 + Dp(5) + t, NULL);
                    LineTo(hdc, cx0 + Dp(4), cy0 + Dp(7) + t);
                    LineTo(hdc, cx0 + Dp(9), cy0 + Dp(2) + t);
                }
            } else {
                // The empty box's edge follows the caption glyphs' stroke.
                SetDCBrushColor(hdc, Blend(ctx->sty.clr.bg, ctx->sty.clr.text, a));
                RECT fr = box;
                for (int t = 0; t < Dp(1); ++t) {
                    FrameRect(hdc, &fr, (HBRUSH)GetStockObject(DC_BRUSH));
                    InflateRect(&fr, -1, -1);
                }
            }
            SelectObject(hdc, oldP);
            SelectObject(hdc, oldB);
            RECT t = r.rows[i]; t.left += Dp(6) + cb + Dp(8);
            // The key (phase 45), right-aligned in the dim color as in a
            // menu's accelerator column. Label and key share the row, and
            // DrawTextW clips only against its own rectangle (pitfall 36):
            // the label ends HDR_GAP before the key, and the key is left
            // out if the label would reach it. At 208 px the longest label
            // ends 20 px before "M" at 96 dpi (measured widths: 137 and 10
            // px), 29 at 144 and 50 at 192, so the menu keeps its width.
            RECT k1 = r.rows[i]; k1.right -= Dp(8);
            SIZE ks = { 0, 0 }, ls = { 0, 0 };
            GetTextExtentPoint32W(hdc, SET_KEY[k], (int)wcslen(SET_KEY[k]), &ks);
            GetTextExtentPoint32W(hdc, SET_LABEL[k], (int)wcslen(SET_LABEL[k]), &ls);
            k1.left = k1.right - ks.cx;
            if (t.left + ls.cx + Dp(HDR_GAP) <= k1.left) {
                SetTextColor(hdc, Blend(ctx->sty.clr.bg, ctx->sty.clr.dim, a));
                DrawTextW(hdc, SET_KEY[k], -1, &k1, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
                t.right = k1.left - Dp(HDR_GAP);
            }
            SetTextColor(hdc, Blend(ctx->sty.clr.bg, ctx->sty.clr.text, a));
            DrawTextW(hdc, SET_LABEL[k], -1, &t, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            continue;
        }
        BOOL isSym  = (i < SYMBOL_COUNT);
        int  idx    = isSym ? i : (i - SYMBOL_COUNT);
        BOOL active = isSym ? (idx == ctx->symIdx) : (idx == ctx->ivIdx);
        const wchar_t* lbl = isSym ? SYMBOLS[idx].label : INTERVALS[idx].label;

        // The current choice is a filled accent row with white text (phase
        // 45), the selected range cell's form - Bloomberg marks a selection
        // one way everywhere. Up to phase 44 it was the label in candle-up
        // green, a price color. Hover stays the half-blended edge color, so
        // it never looks like the selection; on the current row it adds
        // nothing, as a hot selected cell in the range field.
        if (active) {
            SetDCBrushColor(hdc, Blend(ctx->sty.clr.bg, ctx->sty.clr.accent, a));
            FillRect(hdc, &r.rows[i], (HBRUSH)GetStockObject(DC_BRUSH));
        } else if (i == ctx->overlayHot) {
            HBRUSH brHot = CreateSolidBrush(Blend(ctx->sty.clr.bg, ctx->sty.clr.boxEdge, a / 2));
            FillRect(hdc, &r.rows[i], brHot);
            DeleteObject(brHot);
        }
        COLORREF fg = active ? ctx->sty.clr.onAccent : ctx->sty.clr.text;
        RECT t = r.rows[i]; t.left += Dp(6);
        // The interval's key, 1..7 (phase 47), right-aligned as the
        // settings' keys are (phase 45): the keys pick an interval with the
        // list closed and open. Dim, and on the accent row its own text
        // color. Left out if the label would reach it, as there.
        if (!isSym) {
            wchar_t key[2] = { (wchar_t)(L'1' + idx), 0 };
            RECT k1 = r.rows[i]; k1.right -= Dp(8);
            SIZE ks = { 0, 0 }, ls = { 0, 0 };
            GetTextExtentPoint32W(hdc, key, 1, &ks);
            GetTextExtentPoint32W(hdc, lbl, (int)wcslen(lbl), &ls);
            k1.left = k1.right - ks.cx;
            if (t.left + ls.cx + Dp(HDR_GAP) <= k1.left) {
                SetTextColor(hdc, Blend(ctx->sty.clr.bg, active ? ctx->sty.clr.onAccent : ctx->sty.clr.dim, a));
                DrawTextW(hdc, key, 1, &k1, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
                t.right = k1.left - Dp(HDR_GAP);
            }
        }
        SetTextColor(hdc, Blend(ctx->sty.clr.bg, fg, a));
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
// If anything fails here, wmValid = FALSE is set and ChartDrawBackground falls back to
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
    FillRect(ctx->wmDC, &rc, ctx->sty.brBg);

    SetBkMode(ctx->wmDC, TRANSPARENT);
    // Alpha follows W, and W is already part of the cache key above - the
    // color is therefore only computed when the bitmap is built. Everything
    // underneath is opaque CLR_BG, so Blend against the background IS alpha
    // blending.
    SetTextColor(ctx->wmDC, Blend(ctx->sty.clr.bg, ctx->theme->wmInk,
                                  (int)(WatermarkAlpha(W) * 255.0 + 0.5)));

    ChartRect g = PanelGeometry(W, H);

    // The font height follows the height of the chart surface, not a fixed
    // value: a small panel must not get the watermark clipped, and a large
    // one must not get a small text in the middle of the surface.
    //
    // The DPI scaling applies to the CLAMP LIMITS, not to H/5. g.ch is already
    // device pixels, so the proportional part scales itself when the window
    // gets larger on a high-DPI screen. The limits, however, are given in
    // logical pixels, and a floor of 32 would be 16 logical pixels at 200 %.
    // If we multiply H/5 by DPI as well, we count the scaling twice. The dpi
    // is the panel's (phase 37), not GetDeviceCaps: in a per-monitor-aware
    // process that is the system dpi, and the desktop surface - drawn at 96
    // - would have got 150 % limits and a larger watermark.
    int fMin = Dp(WM_FONT_MIN);
    int fMax = Dp(WM_FONT_MAX);
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
    int availW = g.cw - Dp(8);
    if (GetTextExtentPoint32W(ctx->wmDC, wmText, wmLen, &sz) &&
        sz.cx > availW && sz.cx > 0 && availW > 0) {
        int fitted = MulDiv(fh, availW, sz.cx);
        if (fitted < Dp(8)) fitted = Dp(8);
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
    SelectObject(ctx->wmDC, ctx->sty.fontSmall);
    // The distance down to the interval follows the font height, otherwise the
    // text would sit inside the main line on large panels.
    RECT rcIv = { g.left, g.top + (g.ch / 2) + fh / 2 + Dp(4), g.right, g.bottom };
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
    ctx->hFontPill = ChartPillFontCreate(H);
    ctx->pillFontH = ctx->hFontPill ? fh : 0;
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
        // Pressed (phase 47: the button acts on the release) is one step
        // stronger than hover, as on Windows' caption buttons: the range
        // cells' surface, and the close cross's red a fifth toward the
        // background.
        if (hot && ctx->btnDown == i) {
            SetDCBrushColor(hdc, (i == BTN_CLOSE) ? Blend(ctx->sty.clr.bg, ctx->sty.clr.hot, 204)
                                                  : ctx->sty.clr.boxEdge);
            FillRect(hdc, r, (HBRUSH)GetStockObject(DC_BRUSH));
        } else {
            FillRect(hdc, r, hot ? ((i == BTN_CLOSE) ? ctx->brClose : ctx->sty.brBox)
                                 : ctx->sty.brBg);
        }

        SelectObject(hdc, hot ? ((i == BTN_CLOSE) ? ctx->penBtnWhite : ctx->penBtnHot)
                              : ctx->penBtn);

        int cx = (r->left + r->right) / 2;
        int cy = (r->top + r->bottom) / 2;
        int g  = Dp(4);   // half glyph width: 9x9 pixels in total at 96 dpi
        // The stroke follows the dpi too (phase 48, review C10): one pixel at
        // 96 and 120, two at 144-192. The glyphs grew with Dp while the pens
        // stayed one device pixel, and at 150-200 % they read as hairlines.
        // A wide GDI pen gets round ends and is centred on the path, which
        // the +1 end-point arithmetic below does not allow for, so each
        // stroke is drawn s times, one pixel further right (or down, or in)
        // each time. Every glyph's box then grows by s - 1 to the right and
        // down, and they stay centred on one another. At s = 1 the loops
        // draw what they drew before, pixel for pixel.
        int s  = Dp(1);
        int e  = s - 1;

        switch (i) {
            case BTN_NEW:
                // Plus sign, 7x7 around the center pixel: x and y in [-3, +3].
                // LineTo does not draw the end point, hence +4 - that is what
                // makes the cross symmetric. 7 and not 9 like the others: a
                // 9x9 plus weighs optically heavier than the X next to it.
                for (int k = 0; k < s; ++k) {
                    MoveToEx(hdc, cx - Dp(3), cy + k, NULL);
                    LineTo(hdc, cx + Dp(3) + 1 + e, cy + k);
                    MoveToEx(hdc, cx + k, cy - Dp(3), NULL);
                    LineTo(hdc, cx + k, cy + Dp(3) + 1 + e);
                }
                break;
            case BTN_MIN:
                for (int k = 0; k < s; ++k) {
                    MoveToEx(hdc, cx - g, cy + Dp(3) + k, NULL);
                    LineTo(hdc, cx + g + 1 + e, cy + Dp(3) + k);
                }
                break;
            case BTN_MAX:
                if (zoomed) {
                    // Restore: two overlapping rectangles. The back one is
                    // drawn as an OPEN polyline - only the edges that do not
                    // lie behind the front one - so we avoid filling the
                    // front one opaque to hide the overlap. Two GDI calls,
                    // not four (per stroke ring from phase 48, inward).
                    //
                    // Two 7x7 rectangles offset 2 px diagonally, within the
                    // same 9x9 footprint as the other glyphs. The back rect
                    // is x[-2..+4] y[-4..+2], the front x[-4..+2] y[-2..+4].
                    // The visible part of the back one is everything outside
                    // the front one: the left edge down to the overlap, the
                    // top, the right edge, and the stub of the bottom.
                    // Polyline does not draw the last point, so it stops just
                    // before the front one's right edge.
                    int d2 = Dp(2);
                    for (int k = 0; k < s; ++k) {
                        POINT bak[5] = {
                            { cx - d2 + k,     cy - d2 },
                            { cx - d2 + k,     cy - g + k },
                            { cx + g + e - k,  cy - g + k },
                            { cx + g + e - k,  cy + d2 + e - k },
                            { cx + d2 + e,     cy + d2 + e - k },
                        };
                        Polyline(hdc, bak, 5);
                        // NULL_BRUSH is selected above, so Rectangle gives only an outline.
                        Rectangle(hdc, cx - g + k, cy - d2 + k, cx + d2 + 1 + e - k, cy + g + 1 + e - k);
                    }
                } else {
                    // NULL_BRUSH is selected above, so Rectangle gives only an outline.
                    for (int k = 0; k < s; ++k)
                        Rectangle(hdc, cx - g + k, cy - g + k, cx + g + 1 + e - k, cy + g + 1 + e - k);
                }
                break;
            case BTN_CLOSE:
                // The diagonals widen to the right: at s = 2 the cross is one
                // pixel wider than tall (14 x 13 at 144), and it still reads
                // as square in the capture.
                for (int k = 0; k < s; ++k) {
                    MoveToEx(hdc, cx - g + k, cy - g, NULL);
                    LineTo(hdc, cx + g + 1 + k, cy + g + 1);
                    MoveToEx(hdc, cx + g + k, cy - g, NULL);
                    LineTo(hdc, cx - g - 1 + k, cy + g + 1);
                }
                break;
        }
    }

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
}

// The header's cells (phase 22; phase 42 made row 2 Bloomberg's range field).
// Flat like the control buttons: no fade, the color changes instantly, and
// the hit hangs on tbHot. The symbol in row 1 is a dropdown with no surface
// at rest; hover and open look as in row 2 (phase 45). In row 2 every cell
// has the boxEdge surface - the box color was
// too close to the background to read as a cell - and the pointer's cell
// gets a frame in the text color. The selected range, or the cell whose list
// is open, is the accent with white text. The contrast check holds both
// text pairs to 4.5:1.
//
// Called from PaintPopup, not from DrawChartFrame, for the same reason as the
// buttons: right after an interval switch the buffer is empty, and that is
// exactly when the user looks for which cell became active. Everything it
// reads is UI-owned or written only by the UI thread.

// A dropdown's arrow: a filled triangle, 7 px wide and 4 tall, at the right
// end of the cell - vector, like the button glyphs.
static void DrawDropArrow(HDC hdc, const RECT* r, COLORREF c) {
    int ax = r->right - Dp(9), ay = (r->top + r->bottom) / 2 - Dp(1);
    int t3 = Dp(3);
    POINT tri[3] = { { ax - t3, ay }, { ax + t3, ay }, { ax, ay + t3 } };
    SetDCPenColor(hdc, c);
    SetDCBrushColor(hdc, c);
    Polygon(hdc, tri, 3);
}

// The settings gear (phase 42): eight teeth around a hole, from a table of
// unit vectors (x1000) - no sin/cos, whose CRT tables once cost 21 KB
// (pitfall 75). 0 = the root circle, 1 = a tooth's tip.
static const short GEAR_PTS[32][3] = {
    {956,-292,0}, {988,-156,1}, {988,156,1}, {956,292,0}, {883,469,0}, {809,588,1}, {588,809,1}, {469,883,0},
    {292,956,0}, {156,988,1}, {-156,988,1}, {-292,956,0}, {-469,883,0}, {-588,809,1}, {-809,588,1}, {-883,469,0},
    {-956,292,0}, {-988,156,1}, {-988,-156,1}, {-956,-292,0}, {-883,-469,0}, {-809,-588,1}, {-588,-809,1}, {-469,-883,0},
    {-292,-956,0}, {-156,-988,1}, {156,-988,1}, {292,-956,0}, {469,-883,0}, {588,-809,1}, {809,-588,1}, {883,-469,0},
};
static void DrawGear(HDC hdc, const RECT* r, COLORREF ink, COLORREF face) {
    int cx = (r->left + r->right) / 2, cy = (r->top + r->bottom) / 2;
    int ro = Dp(6), ri = Dp(4), rh = Dp(2);
    POINT p[32];
    for (int k = 0; k < 32; ++k) {
        int rr = GEAR_PTS[k][2] ? ro : ri;
        p[k].x = cx + MulDiv(GEAR_PTS[k][0], rr, 1000);
        p[k].y = cy + MulDiv(GEAR_PTS[k][1], rr, 1000);
    }
    SetDCPenColor(hdc, ink);
    SetDCBrushColor(hdc, ink);
    Polygon(hdc, p, 32);
    SetDCPenColor(hdc, face);
    SetDCBrushColor(hdc, face);
    Ellipse(hdc, cx - rh, cy - rh, cx + rh + 1, cy + rh + 1);
}

static void DrawToolbar(AppContext* ctx, HDC hdc, int W) {
    RECT tb[TBAR_COUNT];
    if (ToolbarLayout(W, tb) <= 0) return;

    HGDIOBJ oldFont = SelectObject(hdc, ctx->sty.fontSmall);
    HGDIOBJ oldPen  = SelectObject(hdc, GetStockObject(DC_PEN));
    HGDIOBJ oldBr   = SelectObject(hdc, GetStockObject(DC_BRUSH));
    SetBkMode(hdc, TRANSPARENT);

    for (int i = 0; i < TBAR_COUNT; ++i) {
        RECT* r = &tb[i];
        if (r->right <= r->left) continue;
        BOOL hot = (ctx->tbHot == i);
        if (i == TBAR_SYM) {
            // Phase 45: open (its own dropdown, kind 3 - the right-click
            // picker no longer lights it) is the accent with white text and
            // arrow, as the interval cell; hover keeps the box surface and
            // gets the row-2 cells' frame in the text color - the surface
            // alone is 1.05:1 on the light background.
            BOOL on = ctx->overlayOpen && ctx->overlayKind == 3;
            if (on) {
                SetDCBrushColor(hdc, ctx->sty.clr.accent);
                FillRect(hdc, r, (HBRUSH)GetStockObject(DC_BRUSH));
            } else if (hot) {
                FillRect(hdc, r, ctx->sty.brBox);
                SetDCBrushColor(hdc, ctx->sty.clr.text);
                FrameRect(hdc, r, (HBRUSH)GetStockObject(DC_BRUSH));
            }
            SetTextColor(hdc, on ? ctx->sty.clr.onAccent : ctx->sty.clr.text);
            RECT t = *r;
            t.left += Dp(6); t.right -= Dp(14);
            DrawTextW(hdc, SYMBOLS[ctx->symIdx].label, -1, &t, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            DrawDropArrow(hdc, r, on ? ctx->sty.clr.onAccent : ctx->sty.clr.dim);
            continue;
        }
        BOOL sel;
        const wchar_t* lbl = NULL;
        if (i == TBAR_IV)        { sel = ctx->overlayOpen && ctx->overlayKind == 1; lbl = INTERVALS[ctx->ivIdx].label; }
        else if (i == TBAR_GEAR) { sel = ctx->overlayOpen && ctx->overlayKind == 2; }
        else {
            // A range is selected while the view is at its home (phase 41):
            // the moment the user zooms or pans away, it goes out.
            int rg = i - TBAR_RANGE_FIRST;
            sel = (ctx->rangeIdx == rg && ctx->rangeWant > 0);
            lbl = RANGES[rg].label;
        }
        COLORREF face = sel ? ctx->sty.clr.accent : ctx->sty.clr.boxEdge;
        COLORREF ink  = sel ? ctx->sty.clr.onAccent : ctx->sty.clr.text;
        SetDCBrushColor(hdc, face);
        FillRect(hdc, r, (HBRUSH)GetStockObject(DC_BRUSH));
        if (hot && !sel) {
            SetDCBrushColor(hdc, ctx->sty.clr.text);
            FrameRect(hdc, r, (HBRUSH)GetStockObject(DC_BRUSH));
        }
        SetTextColor(hdc, ink);
        if (i == TBAR_GEAR) {
            DrawGear(hdc, r, ink, face);
        } else if (i == TBAR_IV) {
            RECT t = *r;
            t.left += Dp(6); t.right -= Dp(14);
            DrawTextW(hdc, lbl, -1, &t, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            DrawDropArrow(hdc, r, ink);
        } else {
            DrawTextW(hdc, lbl, -1, r, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
    }
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldFont);
}

// The quote line (phase 42), on Bloomberg's model: label, value, label,
// value, in one row after the symbol. The fields in display order; the
// priority decides which stay when the row is narrow - Last and %Chg at the
// minimum width, then Chg, Hi, Lo, Op, Vol and At. The ones that stay keep
// the display order. A value is drawn whole or not at all: a truncated
// price is a wrong number.
enum { QF_LAST, QF_CHG, QF_PCT, QF_OP, QF_HI, QF_LO, QF_VOL, QF_AT, QF_COUNT };
static const wchar_t* const QF_LABEL[QF_COUNT] = {
    L"Last", L"Chg", L"%Chg", L"Op", L"Hi", L"Lo", L"Vol", L"At",
};
static const int QF_PRIORITY[QF_COUNT] = {
    QF_LAST, QF_PCT, QF_CHG, QF_HI, QF_LO, QF_OP, QF_VOL, QF_AT,
};

// The header over the chart (was the top of DrawChart before phase 34).
// Phase 42: row 1 is the quote line (was a large price and the change over
// the view), row 2 carries the change over the range at its right end,
// before the settings cell. App-level, not chart-level: it reads the network
// health, the trading day and the cell layout. Called under the lock, like
// the chart body.
static void DrawHeader(AppContext* ctx, HDC hdc, int W, BOOL stale, int staleSecs,
                       int vs, int vc) {
    if (g_desktopMode) return;   // phase 14: no readings on the desktop
    int n = ctx->candleCount;
    double last = ctx->candles[n - 1].close;
    RECT tb[TBAR_COUNT];
    ToolbarLayout(W, tb);
    RECT strip;
    ButtonStrip(W, &strip);

    // --- Row 1: the quote line ---
    wchar_t val[QF_COUNT][32];
    const wchar_t* lbl[QF_COUNT];
    COLORREF clr[QF_COUNT];
    BOOL have[QF_COUNT];
    for (int f = 0; f < QF_COUNT; ++f) { lbl[f] = QF_LABEL[f]; have[f] = FALSE; val[f][0] = 0; clr[f] = ctx->sty.clr.quote; }

    have[QF_LAST] = TRUE;
    swprintf_s(val[QF_LAST], 32, L"%.2f", last);
    clr[QF_LAST] = ctx->sty.clr.text;
    // Only the statistics of the UTC day it is now (phase 46): just after
    // midnight UTC the held ones are yesterday's until the next cycle
    // fetches today's, and yesterday's open is not today's change.
    if (ctx->dayValid && ctx->dayOpen > 0.0 && ctx->dayUtc == UtcDayNow()) {
        // The day's open is the reference, as Bloomberg's change is the
        // day's; high and low follow the live price between two fetches.
        double chg = last - ctx->dayOpen;
        double hi  = (last > ctx->dayHigh) ? last : ctx->dayHigh;
        double lo  = (last < ctx->dayLow)  ? last : ctx->dayLow;
        COLORREF dir = (chg >= 0.0) ? ctx->sty.clr.up : ctx->sty.clr.down;
        clr[QF_LAST] = clr[QF_CHG] = clr[QF_PCT] = dir;
        swprintf_s(val[QF_CHG], 32, L"%+.2f", chg);
        swprintf_s(val[QF_PCT], 32, L"%+.2f%%", chg / ctx->dayOpen * 100.0);
        swprintf_s(val[QF_OP],  32, L"%.2f", ctx->dayOpen);
        swprintf_s(val[QF_HI],  32, L"%.2f", hi);
        swprintf_s(val[QF_LO],  32, L"%.2f", lo);
        FormatVolume(ctx->dayVol, val[QF_VOL], 32);
        have[QF_CHG] = have[QF_PCT] = have[QF_OP] = have[QF_HI] = have[QF_LO] = have[QF_VOL] = TRUE;
    }
    // At: the last update, local time. Offline, the same place says how long
    // the line has been down - and takes the priority right after Last.
    int prio[QF_COUNT];
    for (int k = 0; k < QF_COUNT; ++k) prio[k] = QF_PRIORITY[k];
    if (stale) {
        lbl[QF_AT] = L"Offline";
        swprintf_s(val[QF_AT], 32, L"%ds", staleSecs);
        clr[QF_AT] = ctx->sty.clr.down;
        have[QF_AT] = TRUE;
        for (int f = 0; f < QF_COUNT; ++f) if (f != QF_AT) clr[f] = ctx->sty.clr.dim;
        for (int k = QF_COUNT - 1; k > 1; --k) prio[k] = prio[k - 1];
        prio[1] = QF_AT;
    } else if (ctx->lastUpdMs > 0) {
        long long atMs = ctx->lastUpdMs;
#ifdef TICKER_PROBE
        // Recorded data: the capture must not follow the clock (phase 42).
        if (g_fixtureDir[0]) atMs = ctx->candles[n - 1].openTime;
#endif
        FormatCandleTime(atMs, 60000LL, ChartUtcOffsetMs(), val[QF_AT], 32);
        have[QF_AT] = TRUE;
    }

    // Widths, then the fields that fit, by priority.
    int lw[QF_COUNT], vw[QF_COUNT];
    for (int f = 0; f < QF_COUNT; ++f) {
        lw[f] = vw[f] = 0;
        if (!have[f]) continue;
        SIZE sz = { 0, 0 };
        SelectObject(hdc, ctx->sty.fontSmall);
        GetTextExtentPoint32W(hdc, lbl[f], (int)wcslen(lbl[f]), &sz);
        lw[f] = sz.cx;
        SelectObject(hdc, ctx->hFontQuote);
        GetTextExtentPoint32W(hdc, val[f], (int)wcslen(val[f]), &sz);
        vw[f] = sz.cx;
    }
    const int lblGap = Dp(4), fieldGap = Dp(14);
    int x0 = tb[TBAR_SYM].right + Dp(10);
    int avail = strip.left - Dp(HDR_GAP) - x0;
    BOOL take[QF_COUNT] = { 0 };
    int used = 0, count = 0;
    for (int k = 0; k < QF_COUNT; ++k) {
        int f = prio[k];
        if (!have[f]) continue;
        int w = lw[f] + lblGap + vw[f] + (count ? fieldGap : 0);
        if (used + w > avail) break;   // strictly by priority: no short field jumps the queue
        take[f] = TRUE; used += w; count++;
    }
    // One baseline for both fonts, in the button row's band.
    int base = Dp(QL_TOP) + Dp(QL_H) - Dp(5);
    UINT oldAlign = SetTextAlign(hdc, TA_LEFT | TA_BASELINE);
    int x = x0, mask = 0;
    for (int f = 0; f < QF_COUNT; ++f) {
        if (!take[f]) continue;
        SelectObject(hdc, ctx->sty.fontSmall);
        SetTextColor(hdc, ctx->sty.clr.text);
        ExtTextOutW(hdc, x, base, 0, NULL, lbl[f], (UINT)wcslen(lbl[f]), NULL);
        x += lw[f] + lblGap;
        SelectObject(hdc, ctx->hFontQuote);
        SetTextColor(hdc, clr[f]);
        ExtTextOutW(hdc, x, base, 0, NULL, val[f], (UINT)wcslen(val[f]), NULL);
        x += vw[f] + fieldGap;
        mask |= 1 << f;
    }
    SetTextAlign(hdc, oldAlign);
#ifdef TICKER_PROBE
    g_probeQuoteMask = mask | (stale ? 0x100 : 0);
#else
    (void)mask;
#endif

    // --- Row 2: the change over the view, at the right end ---
    // Over the range when one is at home and whole (phase 41), otherwise
    // over the span the view covers. Between the last visible cell and the
    // settings cell, whole or not at all.
    double first = ctx->candles[vs].open;
    double lastV = ctx->candles[vs + vc - 1].close;
    double pct   = (first > 0.0) ? ((lastV - first) / first) * 100.0 : 0.0;
    wchar_t span[24], buf[64];
    FormatSpan(vc, ctx->intervalMs, span, 24);
    if (ctx->rangeIdx >= 0 && ctx->rangeWant > 0 && (vc >= ctx->rangeWant || ctx->histDone))
        wcscpy_s(span, 24, RANGES[ctx->rangeIdx].label);
    swprintf_s(buf, 64, L"%+.2f%%  %s", pct, span);
    int cellsRight = Dp(PAD_L);
    for (int i = TBAR_RANGE_FIRST; i <= TBAR_IV; ++i)
        if (tb[i].right > tb[i].left) cellsRight = tb[i].right;
    int right = (tb[TBAR_GEAR].right > tb[TBAR_GEAR].left) ? tb[TBAR_GEAR].left - Dp(HDR_GAP)
                                                           : HeaderRow2Limit(W);
    SelectObject(hdc, ctx->sty.fontSmall);
    SIZE sz = { 0, 0 };
    GetTextExtentPoint32W(hdc, buf, (int)wcslen(buf), &sz);
    if (HeaderFits(cellsRight, right - sz.cx)) {
        SetTextColor(hdc, stale ? ctx->sty.clr.dim : (pct >= 0.0) ? ctx->sty.clr.up : ctx->sty.clr.down);
        RECT rc = { right - sz.cx, Dp(TBAR_TOP), right, Dp(TBAR_TOP) + Dp(TBAR_H) };
        DrawTextW(hdc, buf, -1, &rc, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }
}

// The status text while the buffer is empty (was in DrawChart before phase
// 34). Reads the network health under the lock.
static void DrawEmptyState(AppContext* ctx, HDC hdc, int W, int H, ULONGLONG nowTick) {
    RECT rcAll = { 0, 0, W, H };
    {
        // Desktop mode: no status on the wallpaper. The surface shows
        // background and watermark until there are candles to draw. The
        // message would be the only text left, and it says nothing a user
        // who cannot click on the surface can do anything about.
        if (g_desktopMode) return;

        wchar_t msg[96];
        SelectObject(hdc, ctx->sty.fontSmall);
        SetTextColor(hdc, ctx->sty.clr.dim);

        // Without a connection it used to say "Loading data from Binance..."
        // forever. The message lied about the state - now it says what is
        // actually happening, and when we retry.
        int in_s = -1;
        if (ctx->netFailures > 0) {
            ULONGLONG nx = ctx->nextRetryTick;
            in_s = (nx > nowTick) ? (int)((nx - nowTick + 999) / 1000) : 0;
            swprintf_s(msg, 96, L"No connection - retrying in %ds", in_s);
        } else {
            wcscpy_s(msg, 96, L"Loading data from Binance...");
        }
#ifdef TICKER_PROBE
        g_probeEmptyMsg = (ctx->netFailures > 0) ? 2 : 1;
        g_probeEmptySecs = in_s;
#endif
        DrawTextW(hdc, msg, -1, &rcAll, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        return;
    }
}

// One frame of the chart area (phase 34): background, then the app's own
// header or status text, then the chart engine's body. The engine sees a
// ChartData/ChartStyle view of AppContext and nothing else. Called under
// the lock, so the buffer and the health fields can be read directly.
static void DrawChartFrame(AppContext* ctx, HDC hdc, int W, int H) {
    // The watermark sits IN the background, before grid, candles and axes -
    // the chart floats cleanly over the text. BitBlt REPLACES FillRect, it
    // does not come in addition.
    EnsureWatermark(ctx, hdc, W, H);
    ChartDrawBackground(hdc, W, H, ctx->wmValid ? ctx->wmDC : NULL, ctx->sty.brBg);

    ULONGLONG nowTick = GetTickCount64();
    BOOL stale = (ctx->lastOkTick != 0) &&
                 (nowTick - ctx->lastOkTick > STALE_AFTER);
    int staleSecs = stale ? (int)((nowTick - ctx->lastOkTick) / 1000) : 0;

    int n = ctx->candleCount;
    if (n <= 0) { DrawEmptyState(ctx, hdc, W, H, nowTick); return; }
#ifdef TICKER_PROBE
    g_probeEmptyMsg = 0;
    g_probeEmptySecs = -1;
#endif

    int vs, vc;
    GetView(&ctx->ch, n, &vs, &vc);
    if (vc <= 0) return;

    DrawHeader(ctx, hdc, W, stale, staleSecs, vs, vc);

    // The stamp font follows the surface height on the desktop; built here
    // (was inside the stamp block), so the engine only borrows a handle.
    if (g_desktopMode) EnsurePillFont(ctx, H);

    ChartData in;
    in.candles = ctx->candles;   in.count = n;
    in.intervalMs = ctx->intervalMs;
    in.histDone = ctx->histDone; in.frontShift = ctx->frontShift;
    in.desktop = g_desktopMode;
    in.alerts = ctx->alerts[ctx->symIdx]; in.alertCount = ctx->alertCount[ctx->symIdx];
    in.alertHot = ctx->alertHot; in.axisHotY = ctx->axisHotY;
    in.alertFresh = ctx->alertFresh;
    in.alertFlashLevel = ctx->alertFlashLevel; in.alertFlashF = ctx->alertFlashF;
    in.utcOffsetMs = ChartUtcOffsetMs();
    in.band = ShowRsiNow(ctx);   // phase 39
    in.vol  = ShowVolNow(ctx);   // phase 43

    ChartStyle sty = ctx->sty;
    sty.fontPill = ctx->hFontPill;

    ChartDrawBody(hdc, W, H, &ctx->ch, &in, &sty);
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
    // neither DrawChartFrame nor DrawOverlay. A hover change invalidates exactly
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
    DrawChartFrame(ctx, hdcMem, W, H);
    LeaveCriticalSection(&ctx->lock);

    // The buttons are drawn HERE, not in DrawChartFrame, which returns early
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
    // DrawChartFrame, which returns early when the buffer is empty - exactly the
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

// How many candles a range's home view holds at a bar size (phase 41); 0
// when the range cannot be shown there. See RANGES and RangeWantAt.
// lastOpenMs is the newest candle's open, 0 when there is none.
static int RangeWantFor(int r, long long ivMs, long long lastOpenMs) {
    if (r < 0 || r >= RANGE_COUNT) return 0;
    return RangeWantAt(RANGES[r].days, ivMs, lastOpenMs);
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
    if (isSym) { ctx->symIdx = idx; ctx->dayValid = FALSE; }   // phase 42: another symbol's day
    else       { ctx->ivIdx = idx; ctx->intervalMs = INTERVALS[idx].ms; }
    ctx->configGen++;
    ctx->candleCount = 0;
    ctx->ch.viewStart   = 0;
    ctx->ch.viewCount   = 0;
    ctx->ch.followLive  = TRUE;
    ctx->lastPrice   = 0.0;
    ctx->histPending = FALSE;   // new config: history starts over
    ctx->histDone    = FALSE;
    // Phase 41: a new buffer starts at home. The range's want is set in the
    // SAME critical section as the emptying: the next MergeCandles fills the
    // view, and with rangeWant set after the lock it would first get the
    // 300-candle default. A range that cannot be shown at the new bar size
    // ends here. The buffer is empty, so YTD counts by the clock.
    ctx->rangeWant = RangeWantFor(ctx->rangeIdx, ctx->intervalMs, 0);
    ctx->rangeYtd  = (ctx->rangeIdx >= 0 && RANGES[ctx->rangeIdx].days == RANGE_DAYS_YTD);
    if (ctx->rangeWant == 0) ctx->rangeIdx = -1;
    LeaveCriticalSection(&ctx->lock);

    ctx->ch.hoverIdx = -1;
    // Phase 23: alertHot is an index into the PREVIOUS symbol's alerts, and
    // the afterglow sits at the previous symbol's price level.
    ctx->alertHot    = -1;
    ctx->axisHotY    = -1;
    ctx->alertFresh  = 0.0;
    ctx->alertFlashF = 0.0;
    ctx->wmValid  = FALSE;   // the watermark shows the previous symbol/interval
    ctx->ch.dispValid = FALSE;  // new buffer: nothing to ease from
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
// this is a drawing choice, not a data one. Called from the pill, the V key and the tray
// menu, so desktop mode can switch without a panel (like phase 17). If the
// surface is visible, dispVolF is eased by the timer; otherwise it snaps, so
// a panel opened later does not play an animation nobody asked for.
// Phase 43: the volume is a pane, so the choice is geometry, as the RSI
// band's is - the price pane changes height at once, the watermark centered
// in it is rebuilt, and the hover is dropped.
static void SetShowVolume(AppContext* ctx, BOOL on) {
    if (ShowVolNow(ctx) == on) return;
    if (g_desktopMode) ctx->showVolDesk = on;   // phase 26: one choice per mode
    else               ctx->showVol     = on;
    SaveConfig(ctx);
    ctx->wmValid = FALSE;
    ctx->ch.hoverIdx = -1;
    ctx->axisHotY = -1;
    if (ctx->hPopup && IsWindowVisible(ctx->hPopup)) {
        StartAnim(ctx->hPopup);
        InvalidateRect(ctx->hPopup, NULL, FALSE);
    } else {
        ctx->ch.dispVolF = on ? 1.0 : 0.0;
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
        ctx->ch.dispIndF = on ? 1.0 : 0.0;
    }
}

// The RSI band (phase 39): same shape as the two above, from the pill, the I
// key and the tray menu. The band is geometry, so the price pane changes
// height at once - the watermark, centered in that pane, is rebuilt, and the
// hover is dropped (the candle under the pointer is recomputed on the next
// move). The line fades in and out with dispRsiF.
static void SetShowRsi(AppContext* ctx, BOOL on) {
    if (ShowRsiNow(ctx) == on) return;
    if (g_desktopMode) ctx->showRsiDesk = on;
    else               ctx->showRsi     = on;
    SaveConfig(ctx);
    ctx->wmValid = FALSE;
    ctx->ch.hoverIdx = -1;
    ctx->axisHotY = -1;
    if (ctx->hPopup && IsWindowVisible(ctx->hPopup)) {
        StartAnim(ctx->hPopup);
        InvalidateRect(ctx->hPopup, NULL, FALSE);
    } else {
        ctx->ch.dispRsiF = on ? 1.0 : 0.0;
    }
}

// The light theme (phase 40), for the mode we are in. The style is rebuilt
// at the dpi it has, so only the colors change; ApplyPanelStyle also drops
// the watermark and the back buffer, which hold the old background. No
// fade: every color in the panel would have to be blended per frame.
static void SetLightTheme(AppContext* ctx, BOOL on) {
    if ((ThemeNow(ctx) == &APP_THEME_LIGHT) == on) return;
    if (g_desktopMode) ctx->lightThemeDesk = on;
    else               ctx->lightTheme     = on;
    SaveConfig(ctx);
    ApplyPanelStyle(ctx, ctx->sty.dpi);
    if (ctx->hPopup) InvalidateRect(ctx->hPopup, NULL, FALSE);
}

// The chart type (phase 49), for the mode we are in: from the settings menu,
// the C key and the tray menu. A drawing choice like the overlays, so not
// through ApplyConfigChoice - the candles stay. The price axis has a new
// target (the line and the mountain scale on the closes, PriceRangeFor), and
// on a visible surface the clock eases it there, as after a wheel notch;
// dispValid is left alone, or the axis would jump. Hidden, nothing is
// eased: the next open snaps (TogglePopup clears dispValid, and SyncDisp
// reads ch.chartType). No geometry changes, so the watermark stays.
static void SetChartType(AppContext* ctx, int t) {
    if (t < 0 || t >= CHART_TYPE_COUNT || ChartTypeNow(ctx) == t) return;
    if (g_desktopMode) ctx->chartTypeDesk = t;
    else               ctx->chartType     = t;
    ctx->ch.chartType = t;
    SaveConfig(ctx);
    if (ctx->hPopup && IsWindowVisible(ctx->hPopup)) {
        StartAnim(ctx->hPopup);
        InvalidateRect(ctx->hPopup, NULL, FALSE);
    }
}

// ---------------------------------------------------------------------------
// Price alerts (phase 23). Everything here runs on the UI thread and touches
// only UI-owned fields; the lock is taken only to read the reference price.
// ---------------------------------------------------------------------------

// The alert of the current symbol at level, within half a cent, -1 = none.
// One place for "the same level": AlertAdd refuses a duplicate with it, and
// AxisAlertAt below treats the pointer as on that alert with it - the two
// must never disagree (phase 46).
static int AlertAtLevel(const AppContext* ctx, double level) {
    int s = ctx->symIdx;
    for (int i = 0; i < ctx->alertCount[s]; ++i) {
        if (fabs(fabs(ctx->alerts[s][i]) - level) < 0.005) return i;
    }
    return -1;
}

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

    if (AlertAtLevel(ctx, level) >= 0) return FALSE;
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

// Which alert is the pointer at my in the price column on? The tag under it
// (AlertAxisHit, the phase 23 rule), or else the alert the ROUNDED price
// there already is. Zoomed far in on a small symbol one cent is ~50 px, and
// a pointer 9-15 px from a tag rounds to that tag's own level: the ghost
// stood amber on the alert's row (phase 46 draws it on the rounded price's
// row), and a click was refused by AlertAdd as a duplicate - a preview of an
// alert that could not be set. Now that pointer is on the alert: red tag, no
// ghost, and the click removes it, exactly as on the tag. Only an alert whose
// tag is drawn counts, as in AlertAxisHit. Hover and click both come here,
// so what is shown is what a click does (pitfall 14).
static int AxisAlertAt(const ChartRect* g, int my) {
    int s = g_Ctx.symIdx;
    int hit = AlertAxisHit(&g_Ctx.ch, g, g_Ctx.alerts[s], g_Ctx.alertCount[s], my);
    if (hit >= 0) return hit;
    int i = AlertAtLevel(&g_Ctx, AlertPriceAtY(&g_Ctx.ch, g, my));
    if (i < 0) return -1;
    int y = AlertY(&g_Ctx.ch, g, fabs(g_Ctx.alerts[s][i]));
    return (y >= g->top && y <= g->bottom) ? i : -1;
}

// Click in the price column: on a tag removes it, on empty surface sets a new
// one at the rounded price there. Hover is recomputed at once - the pointer
// sits on the new tag, and a posted click has no WM_MOUSEMOVE ahead of it.
// Its own function because WM_LBUTTONDBLCLK also lands here (pitfall 38):
// a fast double-click is set + remove, not set + reset the view.
static void OnAxisClick(HWND hwnd, const ChartRect* g, int my) {
    int hit = AxisAlertAt(g, my);
    g_Ctx.alertFresh = 0.0;
    if (hit >= 0) {
        AlertRemove(&g_Ctx, hit);
    } else if (AlertAdd(&g_Ctx, AlertPriceAtY(&g_Ctx.ch, g, my))) {
        int s = g_Ctx.symIdx;
        g_Ctx.alertFresh = g_Ctx.alerts[s][g_Ctx.alertCount[s] - 1];
    }
    g_Ctx.axisHotY = my;
    g_Ctx.alertHot = AxisAlertAt(g, my);
    InvalidateRect(hwnd, NULL, FALSE);
}

// The price column's hover for a pointer at row axY, -1 = not in the column.
// WM_MOUSEMOVE sets it, and from phase 47 the clock too, whenever the axis
// eases under a pointer that rests: a tag that slid away from the pointer
// stayed red - "a click removes me" - while the click, which asks
// AxisAlertAt afresh, set a new alert. A fresh tag the pointer is no longer
// on becomes a tag like the others, whichever of the two moved. TRUE when
// what is drawn changes.
static BOOL AxisHoverSet(const ChartRect* g, int axY) {
    int aHot = (axY >= 0) ? AxisAlertAt(g, axY) : -1;
    if (g_Ctx.alertFresh != 0.0 &&
        (aHot < 0 || g_Ctx.alerts[g_Ctx.symIdx][aHot] != g_Ctx.alertFresh)) {
        g_Ctx.alertFresh = 0.0;
    }
    BOOL changed = (axY != g_Ctx.axisHotY || aHot != g_Ctx.alertHot);
    g_Ctx.axisHotY = axY;
    g_Ctx.alertHot = aHot;
    return changed;
}

// Click on a pill in the toolbar. Only WM_LBUTTONDOWN reaches here: from
// phase 44 WM_LBUTTONDBLCLK swallows the second click of a double-click on a
// cell (it deselected the range the first click picked). Before that both
// messages came here, so two fast clicks were two toggles (pitfall 38).
// The intervals go through ApplyConfigChoice, so registry, watermark,
// configGen and the thread are handled exactly as from the overlay and the
// tray menu; a click on the active interval is a no-op there. The symbol cell
// opens the existing overlay - no new menu, no new hit-test code.
// Opens a menu: 0 the picker, 1 the interval dropdown, 2 the settings menu,
// 3 the symbol dropdown. From the keyboard (phase 47) the row the arrows
// start from is the current choice, as in a Windows dropdown - the accent
// row, so the list opens showing where Enter would leave things - or the
// first setting; from the mouse, none, until the pointer is on a row.
static void OpenOverlay(HWND hwnd, int kind, BOOL byKey) {
    g_Ctx.overlayKind = kind;
    g_Ctx.overlayOpen = TRUE;
    g_Ctx.overlayHot  = !byKey ? -1
                      : (kind == 1) ? SYMBOL_COUNT + g_Ctx.ivIdx
                      : (kind == 2) ? OVL_SET_FIRST
                      : g_Ctx.symIdx;
    g_Ctx.ch.hoverIdx = -1;
    g_Ctx.tbHot       = -1;   // no cell lights up while the overlay owns the mouse
    StartAnim(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
}

// A row of the open menu is picked, by a click or by Enter (phase 47: one
// path for both). A setting toggles and the menu stays open (phase 42), so
// several can be changed in one visit; a symbol or an interval is applied
// and the menu closes; -1 - a click outside every row - closes it without a
// change.
static void OverlayPick(HWND hwnd, int hit) {
    // A chart type (phase 49) is picked like a setting: the menu stays open,
    // and the chart behind it eases to the new axis.
    if (hit >= OVL_TYPE_FIRST) {
        SetChartType(&g_Ctx, hit - OVL_TYPE_FIRST);
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }
    if (hit >= OVL_SET_FIRST) {
        SettingToggle(&g_Ctx, hit - OVL_SET_FIRST);
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }
    if (hit >= 0) ApplyConfigChoice(&g_Ctx, hit);
    g_Ctx.overlayOpen = FALSE;
    g_Ctx.overlayHot  = -1;
    StartAnim(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
}

// The next row of the open menu from row from, one step in dir (+1 down, -1
// up), skipping the rows this layout leaves empty (the symbol rows of the
// interval dropdown, for one). From -1 - nothing highlighted - the first or
// the last. The ends stop, as in a dropdown list; they do not wrap.
static int OverlayStep(const OverlayRects* r, int from, int dir) {
    int i = from;
    for (int k = 0; k < r->count; ++k) {
        i = (i < 0) ? ((dir > 0) ? 0 : r->count - 1) : i + dir;
        if (i < 0 || i >= r->count) return from;
        if (r->rows[i].right > r->rows[i].left) return i;
    }
    return from;
}

static void OnToolbarClick(HWND hwnd, int th) {
    if (th == TBAR_SYM || th == TBAR_IV || th == TBAR_GEAR) {
        // The symbol dropdown (phase 45; the two-column picker before), the
        // interval dropdown (phase 41) or the settings menu (phase 42). The
        // picker (kind 0) is the right-click's alone now.
        OpenOverlay(hwnd, (th == TBAR_IV) ? 1 : (th == TBAR_GEAR) ? 2 : 3, FALSE);
    } else if (th >= TBAR_RANGE_FIRST && th < TBAR_IV) {
        SelectRange(&g_Ctx, th - TBAR_RANGE_FIRST);
    }
}

// Zoom and panning back to the default view: the last DEFAULT_VIEW candles,
// pinned to the right edge and followed live. Does not touch dispValid, so
// the display eases back from where it is - the same mechanism as wheel zoom.
// TogglePopup, on the other hand, wants a snap on opening and sets dispValid
// itself. The window geometry is another matter: ResetToDefaultView owns it
// (Ctrl+0).
// With a range selected (phase 41) the default IS the range: R, a
// double-click, Esc and opening the panel all go back to it.
static void ResetView(AppContext* ctx) {
    EnterCriticalSection(&ctx->lock);
    ctx->rangeWant = RangeWantFor(ctx->rangeIdx, ctx->intervalMs,
                                  (ctx->candleCount > 0) ? ctx->candles[ctx->candleCount - 1].openTime : 0);
    ctx->rangeYtd  = (ctx->rangeIdx >= 0 && RANGES[ctx->rangeIdx].days == RANGE_DAYS_YTD);
    if (ctx->rangeWant == 0) ctx->rangeIdx = -1;
    int home = (ctx->rangeWant > 0) ? ctx->rangeWant : DEFAULT_VIEW;
    ctx->ch.viewCount  = 0;
    ctx->ch.followLive = TRUE;
    if (ctx->candleCount > 0) {
        int vc = (ctx->candleCount < home) ? ctx->candleCount : home;
        ctx->ch.viewCount = vc;
        ctx->ch.viewStart = ctx->candleCount - vc;
    }
    LeaveCriticalSection(&ctx->lock);
}

// Is the view where ResetView would have put it? ESC uses the answer to pick
// a layer: if it is already at the default, ESC hides the panel instead.
static BOOL ViewIsDefault(AppContext* ctx) {
    if (ctx->rangeIdx >= 0) return ctx->rangeWant > 0;   // phase 41: at the range's home
    EnterCriticalSection(&ctx->lock);
    int n = ctx->candleCount, vs, vc;
    GetView(&ctx->ch, ctx->candleCount, &vs, &vc);
    int want = (n < DEFAULT_VIEW) ? n : DEFAULT_VIEW;
    BOOL def = (n == 0) || (vc == want && vs + vc >= n);
    LeaveCriticalSection(&ctx->lock);
    return def;
}



// A range is picked (phase 41): from its pill, Shift+1..8 or the tray menu.
// It sets its default bar size - through ApplyConfigChoice, which empties
// the buffer and sets the want in one critical section - or, at the same
// bar size, eases the view home from where it is. Picking the range that is
// already selected, while at home, ends it: the free 300-candle view. -1
// (the tray menu's "None", phase 47) ends the selected one from anywhere.
static void SelectRange(AppContext* ctx, int r) {
    if (r < -1 || r >= RANGE_COUNT) return;
    if (r < 0 || (ctx->rangeIdx == r && ctx->rangeWant > 0)) {
        ctx->rangeIdx = -1;
        ResetView(ctx);
    } else {
        int iv = -1;
        for (int i = 0; i < INTERVAL_COUNT; ++i)
            if (INTERVALS[i].ms == RANGES[r].ivMs) iv = i;
        ctx->rangeIdx = r;
        if (iv >= 0 && iv != ctx->ivIdx) ApplyConfigChoice(ctx, SYMBOL_COUNT + iv);
        else                             ResetView(ctx);
    }
    ctx->ch.hoverIdx = -1;
    SaveConfig(ctx);
    // The backfill starts at once rather than on the next WM_APP_DATA.
    EnterCriticalSection(&ctx->lock);
    BOOL more = (ctx->rangeWant > ctx->candleCount);
    LeaveCriticalSection(&ctx->lock);
    if (more) RequestHistory(ctx);
    if (ctx->hPopup) {
        StartAnim(ctx->hPopup);
        InvalidateRect(ctx->hPopup, NULL, FALSE);
    }
}

// Hides the panel to the notification area - or, in a duplicate, exits the
// process. A duplicate has no main-instance role to return to, and a tail of
// hidden tray icons is not a feature. The exit goes through the tray menu's
// own path, so the icon is removed the same way in both cases.
static void HidePanel(HWND hwnd) {
    g_Ctx.ch.hoverIdx = -1;
    g_Ctx.btnHot   = -1;
    g_Ctx.btnDown  = -1;   // phase 47
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
// or on the left. The duplicate opens there with SetWindowPos (see
// PlacePopupInitially).
static void SpawnInstance(HWND hwnd) {
    RECT r;
    if (IsZoomed(hwnd)) {
        WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
        if (!GetPanelPlacement(hwnd, &wp)) return;
        r = wp.rcNormalPosition;
    } else if (!GetWindowRect(hwnd, &r)) {
        return;
    }
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = r.left + Dp(SPAWN_OFFSET), y = r.top + Dp(SPAWN_OFFSET);

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

// Click on a control button. Its own function because the keyboard shortcuts
// (Ctrl+N, Ctrl+M, F11, Ctrl+W) take the same path as the click. Up to phase
// 43 the second click of a double-click came here too; from phase 44
// WM_LBUTTONDBLCLK swallows it, so [ + ] no longer starts two instances.
// From phase 47 the click comes from WM_LBUTTONUP, the keys at once.
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
                if (GetPanelPlacement(hwnd, &wp)) {
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

#ifdef TICKER_PROBE
// A scale change to nd, as Windows sends it when the panel moves to another
// monitor (phase 37's field 105, shared with the fake monitor in phase 48):
// the real WM_DPICHANGED, with the window rectangle scaled by new / old
// around its top left corner - the logical size is kept. Returns the dpi the
// panel is drawn at afterwards, -1 for a dpi out of range.
static int ProbeDpiChange(HWND hwnd, int nd) {
    int od = g_Ctx.sty.dpi;
    if (nd < 48 || nd > 480 || od <= 0) return -1;
    RECT rw;
    GetWindowRect(hwnd, &rw);
    RECT nr = { rw.left, rw.top,
                rw.left + MulDiv(rw.right - rw.left, nd, od),
                rw.top  + MulDiv(rw.bottom - rw.top, nd, od) };
    SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(nd, nd), (LPARAM)&nr);
    return g_Ctx.sty.dpi;
}
#endif

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

        // When the panel lost activation (phase 46): a tray click that
        // follows within TRAY_CLICK_GRACE_MS found it in front - see
        // TogglePopup. DefWindowProc still does the focus.
        case WM_ACTIVATE:
            g_Ctx.popupDeactTick = (LOWORD(wParam) == WA_INACTIVE) ? GetTickCount64() : 0;
            break;

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
        // The panel moved to a monitor with another scale, or the scale was
        // changed (phase 37). Windows suggests a rectangle that keeps the
        // panel's logical size; the fonts and every length follow the new dpi.
        case WM_DPICHANGED: {
            if (g_desktopMode) return 0;
            const RECT* nr = (const RECT*)lParam;
            g_Ctx.ch.hoverIdx = -1;
            g_Ctx.alertHot = -1;
            g_Ctx.axisHotY = -1;
            // The new dpi is in the message (a probe can send one without a
            // second monitor); the test build's forced dpi still wins.
            int nd = HIWORD(wParam);
#ifdef TICKER_PROBE
            if (g_forceDpi > 0) nd = g_forceDpi;
            InterlockedIncrement(&g_probeDpiChanges);   // field 87 (phase 48)
#endif
            ApplyPanelStyle(&g_Ctx, (nd > 0) ? nd : PanelDpi(hwnd));
            SetWindowPos(hwnd, NULL, nr->left, nr->top,
                         nr->right - nr->left, nr->bottom - nr->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

#ifdef TICKER_PROBE
        // The fake monitor (phase 48, see g_fakeMonX0): a move that takes the
        // panel's centre across x0 changes its dpi, and Windows would answer
        // it with WM_DPICHANGED. DefWindowProc runs first: it is what sends
        // WM_SIZE and WM_MOVE from this message. The second pass, from the
        // SetWindowPos in WM_DPICHANGED, finds the dpi equal and stops.
        case WM_WINDOWPOSCHANGED:
            if (g_fakeMonDpi > 0 && !g_fakeMonQuiet && g_forceDpi == 0 &&
                hwnd == g_Ctx.hPopup && !g_desktopMode && g_Ctx.sty.fontSmall) {
                LRESULT lr = DefWindowProcW(hwnd, msg, wParam, lParam);
                int nd = PanelDpi(hwnd);
                if (nd != g_Ctx.sty.dpi) ProbeDpiChange(hwnd, nd);
                return lr;
            }
            break;
#endif

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
                int rb = Dp(RESIZE_BORDER);
                int lft = (x < rb), rgt = (x >= w - rb);
                int tp  = (y < rb), bot = (y >= h - rb);
                if (tp  && lft) return HTTOPLEFT;
                if (tp  && rgt) return HTTOPRIGHT;
                if (bot && lft) return HTBOTTOMLEFT;
                if (bot && rgt) return HTBOTTOMRIGHT;
                if (lft) return HTLEFT;
                if (rgt) return HTRIGHT;
                if (tp)  return HTTOP;
                if (bot) return HTBOTTOM;
            }

            if (y < Dp(HEADER_H)) {
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
                // ButtonLayout and OverlayLayout.
                //
                // While a menu is open the whole band is client area (phase
                // 47; phase 45 made the open box so, for the symbol dropdown
                // that opens over the range field). Everywhere else the first
                // click closes the menu and does nothing more, but on free
                // header area it was caption: a click there moved the window
                // with the menu open, and a double-click maximized it. Now
                // WM_LBUTTONDOWN gets it and closes the menu. overlayOpen,
                // not the fade level (pitfall 12). The resize border above
                // keeps resizing.
                if (g_Ctx.overlayOpen) return HTCLIENT;
                if (ButtonHitAt(w, IsZoomed(hwnd), x, y) >= 0) return HTCLIENT;
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
            // DPI-scaled like the factory size: 600x375 device pixels at 150 %.
            mmi->ptMinTrackSize.x = Dp(POPUP_MIN_W);
            if (mmi->ptMinTrackSize.x < ToolbarMinW()) mmi->ptMinTrackSize.x = ToolbarMinW();
            mmi->ptMinTrackSize.y = Dp(POPUP_MIN_H);

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
            // Restored (phase 47): minimized, the client has no chart, so
            // the clock skips the easing and dies - an ease cut off by the
            // minimize stood frozen after the restore until the next fetch,
            // and an offline counter stood still. The clock picks both up;
            // with nothing left to move it dies on its first tick. Only for
            // the published surface: CreateWindowExW sends WM_SIZE too, and a
            // desktop surface that fails to attach is destroyed before it is
            // published - its WM_NCDESTROY would not reset animRunning, and
            // the next surface's clock would never start.
            if (wParam != SIZE_MINIMIZED && hwnd == g_Ctx.hPopup) StartAnim(hwnd);
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
            ChartRect g = PanelGeometry(rc.right, rc.bottom);

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
            //
            // A pressed caption button (phase 47) lights only while the
            // pointer is on it - slid off, the release will not act - and
            // owns the mouse until the release: the early return below keeps
            // the pills, the price column and the crosshair dark, as Windows
            // does while its own caption button is held.
            {
                int bh = ButtonHitAt(rc.right, IsZoomed(hwnd), mx, my);
                if (g_Ctx.btnDown >= 0) {
                    if (bh != g_Ctx.btnDown) bh = -1;
                } else if (g_Ctx.overlayOpen || g_Ctx.panning) {
                    bh = -1;
                }
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
                if (g_Ctx.btnDown >= 0) return 0;
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
            // The cursor that has left the newly set tag makes it a tag like
            // all the others, red next time (AxisHoverSet).
            {
                int axY = -1;
                if (!g_Ctx.overlayOpen && !g_Ctx.panning && g_Ctx.ch.dispValid &&
                    mx > g.edge && my >= g.top && my <= g.bottom) {
                    axY = my;
                }
                if (AxisHoverSet(&g, axY)) InvalidateRect(hwnd, NULL, FALSE);
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
                ApplyFrontShift(&g_Ctx.ch, g_Ctx.frontShift);
                int vs2, vc2;
                GetView(&g_Ctx.ch, g_Ctx.candleCount, &vs2, &vc2);
                if (vc2 > 0 && g.cw > 0) {
                    double slot = (double)g.cw / (double)vc2;
                    int shift = (int)((double)(mx - g_Ctx.panAnchorX) / slot);
                    int want  = g_Ctx.ch.panAnchorView - shift;        // drag right = backward
                    int vs0   = g_Ctx.ch.viewStart;
                    g_Ctx.ch.viewStart = want;
                    ClampView(&g_Ctx.ch, g_Ctx.candleCount);
                    if (g_Ctx.ch.viewStart != vs0) g_Ctx.rangeWant = 0;   // phase 41: left home
                    // At the wall the finger slips: the anchor is moved here,
                    // so the overshoot is not remembered. Without this the
                    // drag after a backfill (phase 18) would jump by exactly
                    // what the user dragged past the wall before the candles
                    // arrived, and a drag back from the wall would stand
                    // still for just as long.
                    if (g_Ctx.ch.viewStart != want) {
                        g_Ctx.ch.panAnchorView = g_Ctx.ch.viewStart;
                        g_Ctx.panAnchorX    = mx;
                    }
                    g_Ctx.ch.followLive =
                        (g_Ctx.ch.viewStart + g_Ctx.ch.viewCount >= g_Ctx.candleCount);
                    atWall = (g_Ctx.ch.viewStart == 0);
                    // Drag panning is NOT eased in X. The finger and the
                    // chart must stay together; an eased drag feels sluggish,
                    // not smooth. The Y axis is still eased - it should glide
                    // when new highs and lows enter the view.
                    g_Ctx.ch.dispStart = (double)g_Ctx.ch.viewStart;
                    g_Ctx.ch.dispCount = (double)((g_Ctx.ch.viewCount > 0)
                                               ? g_Ctx.ch.viewCount : g_Ctx.candleCount);
                    g_Ctx.ch.hoverIdx = HitCandle(&g_Ctx.ch, g_Ctx.candleCount, &g, mx, my);
                    g_Ctx.ch.hoverY   = my;
                    g_Ctx.hoverX      = mx;
                }
                LeaveCriticalSection(&g_Ctx.lock);
                if (atWall) RequestHistory(&g_Ctx);   // phase 18
                StartAnim(hwnd);   // the Y axis may have a new target
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            EnterCriticalSection(&g_Ctx.lock);
            int idx = HitCandle(&g_Ctx.ch, g_Ctx.candleCount, &g, mx, my);
            LeaveCriticalSection(&g_Ctx.lock);

            g_Ctx.hoverX = mx;   // phase 47: the clock asks again here while the display eases
            if (idx != g_Ctx.ch.hoverIdx || (idx >= 0 && my != g_Ctx.ch.hoverY)) {
                g_Ctx.ch.hoverIdx = idx;
                g_Ctx.ch.hoverY   = my;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        // Ctrl + mouse wheel zooms about the point under the cursor. Note
        // that lParam here is SCREEN coordinates, unlike WM_MOUSEMOVE.
        case WM_MOUSEWHEEL: {
            // Not during a drag either (phase 47): the drag measures from its
            // anchor, so a notch moved the view only until the next mouse
            // move, which put it back where the finger says - a jump each way.
            if (g_Ctx.overlayOpen || g_Ctx.panning) return 0;
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);   // lParam is SCREEN coordinates here

            RECT rc;
            GetClientRect(hwnd, &rc);
            ChartRect g = PanelGeometry(rc.right, rc.bottom);
            if (g.cw <= 0) return 0;

            BOOL ctrl   = (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0;
            // Whole notches from an accumulated delta (phase 44). A touchpad
            // and a free-spinning wheel send deltas below WHEEL_DELTA, and
            // delta / WHEEL_DELTA truncated every one of them to zero - the
            // chart did not move at all. The remainder carries to the next
            // message; a change of direction, or between pan and zoom
            // (Ctrl), starts over, so a leftover never pushes the wrong way.
            static int  s_wheelAcc  = 0;
            static BOOL s_wheelCtrl = FALSE;
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (ctrl != s_wheelCtrl || (s_wheelAcc < 0 && delta > 0) || (s_wheelAcc > 0 && delta < 0))
                s_wheelAcc = 0;
            s_wheelCtrl = ctrl;
            s_wheelAcc += delta;
            int notches = s_wheelAcc / WHEEL_DELTA;
            s_wheelAcc -= notches * WHEEL_DELTA;
            if (notches == 0) return 0;

            BOOL atWall = FALSE;
            EnterCriticalSection(&g_Ctx.lock);
            if (g_Ctx.candleCount > 0) {
                int ws0, wc0, ws1, wc1;
                GetView(&g_Ctx.ch, g_Ctx.candleCount, &ws0, &wc0);
                if (!ctrl) {
                    // Without Ctrl: pan in time. Wheel up = backward.
                    int vs, vc;
                    GetView(&g_Ctx.ch, g_Ctx.candleCount, &vs, &vc);
                    int step = vc / 8;
                    if (step < 1) step = 1;
                    atWall = PanView(&g_Ctx.ch, g_Ctx.candleCount, -notches * step);
                } else {
                    // With Ctrl: zoom about the point under the cursor
                    double frac = (double)(pt.x - g.left) / (double)g.cw;
                    atWall = ZoomView(&g_Ctx.ch, g_Ctx.candleCount, frac, notches);
                }
                // A view that moved has left the range's home (phase 41).
                GetView(&g_Ctx.ch, g_Ctx.candleCount, &ws1, &wc1);
                if (ws1 != ws0 || wc1 != wc0) g_Ctx.rangeWant = 0;
                // The candle under the pointer NOW; the display has not
                // moved yet. From phase 47 the clock asks again on every
                // eased frame, so the crosshair stays under the pointer and
                // lands on the candle there - before, it rode the candle it
                // started on away from a pointer that had not moved.
                g_Ctx.ch.hoverIdx = HitCandle(&g_Ctx.ch, g_Ctx.candleCount, &g, pt.x, pt.y);
                g_Ctx.ch.hoverY   = pt.y;
                g_Ctx.hoverX      = pt.x;
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
            g_Ctx.ch.hoverIdx      = -1;
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
            GetView(&g_Ctx.ch, g_Ctx.candleCount, &pvs, &pvc);
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
                case 8:  r = (LRESULT)(g_Ctx.ch.dispStart * 1000.0); break;   // thousandths of a candle
                case 9:  r = g_Ctx.netFailures; break;
                case 10: r = g_Ctx.ch.followLive; break;
                case 11: r = g_Ctx.ch.hoverIdx; break;
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
                case 21: r = (LRESULT)(g_Ctx.ch.dispVolF * 1000.0); break;
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
                case 31: r = (LRESULT)floor(g_Ctx.ch.dispMin * 100.0 + 0.5); break;
                case 32: r = (LRESULT)floor(g_Ctx.ch.dispMax * 100.0 + 0.5); break;
                case 33: r = (LRESULT)floor(g_Ctx.alertFresh * 100.0 + 0.5); break;
                // Phase 25: moving averages. 36/37 are SMA/EMA at the candle
                // index in lParam, x100, -1 when the average is not defined
                // there. 38 is the close x100, so the probe can compute the
                // averages itself. 39 is the time the two lines took in the
                // last repaint (us).
                case 34: r = ShowIndNow(&g_Ctx); break;
                case 35: r = (LRESULT)(g_Ctx.ch.dispIndF * 1000.0); break;
                case 36: case 37: {
                    double iv = 0.0;
                    BOOL isE = (wParam == 37);
                    if (IndValueAt(g_Ctx.candles, g_Ctx.candleCount,
                                   isE ? IND_EMA_PERIOD : IND_SMA_PERIOD, isE,
                                   (int)lParam, &iv))
                        r = (LRESULT)floor(iv * 100.0 + 0.5);
                    break;
                }
                case 39: r = (LRESULT)g_Ctx.ch.probeIndUs; break;
                // Phase 26: the height the stamp font is built for (desktop mode).
                case 40: r = g_Ctx.pillFontH; break;
                // 104 (phase 34): WRITING. Sets the hover as a mouse move at
                // client (LOWORD, HIWORD) would, without a pointer in the
                // window - WM_MOUSELEAVE clears a posted move within a tick,
                // and a golden capture needs the crosshair to stand still.
                case 104: {
                    RECT rcH;
                    GetClientRect(hwnd, &rcH);
                    ChartRect gH = PanelGeometry(rcH.right, rcH.bottom);
                    g_Ctx.ch.hoverIdx = HitCandle(&g_Ctx.ch, g_Ctx.candleCount, &gH, LOWORD(lParam), HIWORD(lParam));
                    g_Ctx.ch.hoverY   = HIWORD(lParam);
                    g_Ctx.hoverX      = LOWORD(lParam);
                    InvalidateRect(hwnd, NULL, FALSE);
                    r = g_Ctx.ch.hoverIdx;
                    break;
                }
                // 105 (phase 37): WRITING. A scale change to the dpi in
                // lParam, as Windows sends it when the panel moves to another
                // monitor: the real WM_DPICHANGED, with the window rectangle
                // scaled by new / old around its top left corner. The RECT
                // cannot cross from the probe's process, so it is built here.
                case 105: r = ProbeDpiChange(hwnd, (int)lParam); break;
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
                case 45: r = (LRESULT)g_Ctx.ch.probeSessUs; break;
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
                case 54: r = AppWantsHistory(&g_Ctx); break;
                case 56: r = (LRESULT)g_Ctx.ch.probePrevUs; break;
                case 57: r = g_Ctx.ch.probeLblMask; break;
                case 58: r = g_Ctx.ch.probeCrossTag; break;
                case 59: r = (LRESULT)g_Ctx.ch.probeLblUs; break;
                // 60 (phase 37): the dpi the panel is drawn for.
                case 60: r = g_Ctx.sty.dpi; break;
                // 61-63 (phase 39): the RSI band's choice for this mode, its
                // display x1000, and the RSI at the candle in lParam x100
                // (-1 = not defined there).
                case 61: r = ShowRsiNow(&g_Ctx); break;
                // 64 (phase 40): the theme the panel is DRAWN with, 1 = light -
                // the style's, not the choice, so a switch that never reached
                // ApplyPanelStyle reads as dark.
                case 64: r = (g_Ctx.theme == &APP_THEME_LIGHT); break;
                // 65-67 (phase 41): the selected range (-1 none), the candles
                // its home view wants (0 = the view is away), the overlay's
                // kind (0 picker, 1 interval dropdown, 2 settings menu from
                // phase 42, 3 symbol dropdown from phase 45).
                case 65: r = g_Ctx.rangeIdx; break;
                case 66: r = g_Ctx.rangeWant; break;
                case 67: r = g_Ctx.overlayKind; break;
                // 69-73 (phase 42): the trading day - valid, then open, high,
                // low and volume x100 (-1 while not valid).
                case 68: r = g_probeQuoteMask; break;
                case 69: r = g_Ctx.dayValid; break;
                case 70: r = g_Ctx.dayValid ? (LRESULT)floor(g_Ctx.dayOpen * 100.0 + 0.5) : -1; break;
                case 71: r = g_Ctx.dayValid ? (LRESULT)floor(g_Ctx.dayHigh * 100.0 + 0.5) : -1; break;
                case 72: r = g_Ctx.dayValid ? (LRESULT)floor(g_Ctx.dayLow  * 100.0 + 0.5) : -1; break;
                case 73: r = g_Ctx.dayValid ? (LRESULT)floor(g_Ctx.dayVol  * 100.0 + 0.5) : -1; break;
                // 74-77 (phase 43): where the volume stands - 0 off, 1 a pane
                // of its own, 2 behind the candles - then the pane's top and
                // bottom and the price pane's bottom, in client pixels.
                // 78 (phase 46): the empty chart's status text, see
                // g_probeEmptyMsg.
                case 78: r = g_probeEmptyMsg; break;
                case 79: r = g_probeEmptySecs; break;
                // 80-86 (phase 47): the animation clock is running; the pan
                // anchor (the view a drag measures from); the menu row under
                // the pointer or the keyboard; the candle a pointer at
                // (LOWORD, HIWORD) of lParam would be on now, and the alert a
                // pointer at row lParam in the price column would be on now -
                // both without touching the hover, so a probe can compare
                // them with what the hover says; the clock's ticks since
                // start; the client size, width << 16 | height.
                case 80: r = g_Ctx.animRunning; break;
                case 81: r = g_Ctx.ch.panAnchorView; break;
                case 82: r = g_Ctx.overlayHot; break;
                case 83: case 84: {
                    RECT rcP;
                    GetClientRect(hwnd, &rcP);
                    ChartRect gP = PanelGeometry(rcP.right, rcP.bottom);
                    r = (wParam == 83)
                        ? HitCandle(&g_Ctx.ch, g_Ctx.candleCount, &gP, LOWORD(lParam), HIWORD(lParam))
                        : AxisAlertAt(&gP, (int)lParam);
                    break;
                }
                case 85: r = (LRESULT)g_probeAnimTicks; break;
                // 87-89 (phase 48): WM_DPICHANGED messages handled; the dpi
                // the panel's monitor has now (PanelDpi, fake monitor
                // included), to compare with the dpi it is drawn at (60);
                // the last on-screen check (g_probeOnScreen).
                case 87: r = (LRESULT)g_probeDpiChanges; break;
                case 88: r = PanelDpi(hwnd); break;
                case 89: r = g_probeOnScreen; break;
                // 90-93 (phase 49): the chart type chosen for this mode; the
                // type the engine draws and scales with (ch.chartType - the
                // DRAWN one, as field 64 is the drawn theme); and the price
                // axis's target for the chart type in lParam over the target
                // view, min and max x100, as the clock computes it - -1 with
                // an empty buffer. With 31/32 a probe sees that the display
                // settles on the target of the type drawn.
                case 90: r = ChartTypeNow(&g_Ctx); break;
                case 91: r = g_Ctx.ch.chartType; break;
                case 92: case 93:
                    if (g_Ctx.candleCount > 0 && pvc > 0) {
                        double tmn, tmx;
                        PriceRangeFor(g_Ctx.candles, pvs, pvc, (int)lParam, &tmn, &tmx);
                        r = (LRESULT)floor((wParam == 92 ? tmn : tmx) * 100.0 + 0.5);
                    }
                    break;
                // 94 (phase 51): the view's high and low labels in the last
                // frame, bit 0 the high, bit 1 the low (the engine's
                // probeHiLoMask).
                case 94: r = g_Ctx.ch.probeHiLoMask; break;
                case 86: {
                    RECT rcS;
                    GetClientRect(hwnd, &rcS);
                    r = ((LRESULT)rcS.right << 16) | (rcS.bottom & 0xFFFF);
                    break;
                }
                // 106 (phase 47): WRITING. A backfill of lParam candles (1-50)
                // lands, as PrependCandles does it for the worker, with no
                // repaint after it: the shift is pending until the next frame
                // or timer tick, the moment a drag can start in.
                case 106: {
                    int k = (int)lParam;
                    if (k < 1 || k > 50 || g_Ctx.candleCount <= 0 ||
                        g_Ctx.candleCount + k > MAX_CANDLES) break;
                    Candle pre[50];
                    for (int i = 0; i < k; ++i) {
                        pre[i] = g_Ctx.candles[0];
                        pre[i].openTime -= (long long)(k - i) * g_Ctx.intervalMs;
                    }
                    PrependCandles(&g_Ctx, pre, k);
                    r = g_Ctx.candleCount;
                    break;
                }
                case 74: case 75: case 76: case 77: {
                    RECT rcV;
                    GetClientRect(hwnd, &rcV);
                    ChartRect gV = PanelGeometry(rcV.right, rcV.bottom);
                    if (wParam == 74)
                        r = !ShowVolNow(&g_Ctx) ? 0 : (gV.volBottom > gV.bottom) ? 1 : 2;
                    else
                        r = (wParam == 75) ? gV.volTop : (wParam == 76) ? gV.volBottom : gV.bottom;
                    break;
                }
                case 62: r = (LRESULT)(g_Ctx.ch.dispRsiF * 1000.0); break;
                case 63: {
                    double v;
                    r = RsiValueAt(g_Ctx.candles, g_Ctx.candleCount, (int)lParam, &v)
                            ? (LRESULT)floor(v * 100.0 + 0.5) : -1;
                    break;
                }
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
#ifdef TICKER_PROBE
                InterlockedIncrement(&g_probeAnimTicks);
#endif
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
                    if (g_Ctx.ch.dispIndF != ifT) {
                        g_Ctx.ch.dispIndF = AnimStep(g_Ctx.ch.dispIndF, ifT, dt,
                                                  IND_TAU_FADE, 0.02);
                        redraw = TRUE;
                        if (g_Ctx.ch.dispIndF != ifT) settled = FALSE;
                    }
                }
                // The RSI band's content (phase 39), the same fade.
                {
                    double rfT = ShowRsiNow(&g_Ctx) ? 1.0 : 0.0;
                    if (g_Ctx.ch.dispRsiF != rfT) {
                        g_Ctx.ch.dispRsiF = AnimStep(g_Ctx.ch.dispRsiF, rfT, dt,
                                                  IND_TAU_FADE, 0.02);
                        redraw = TRUE;
                        if (g_Ctx.ch.dispRsiF != rfT) settled = FALSE;
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
                    ChartRect gE = PanelGeometry(rcE.right, rcE.bottom);

                    int tvs = 0, tvc = 0, tn = 0;
                    double tMin = 0.0, tMax = 1.0, tVol = 0.0;
                    EnterCriticalSection(&g_Ctx.lock);
                    ApplyFrontShift(&g_Ctx.ch, g_Ctx.frontShift);
                    tn = g_Ctx.candleCount;
                    GetView(&g_Ctx.ch, g_Ctx.candleCount, &tvs, &tvc);
                    if (tn > 0 && tvc > 0) {
                        // Phase 49: the axis of the type drawn - the same
                        // PriceRangeFor as SyncDisp's snap. With PriceRange
                        // here a line would snap to its closes on the first
                        // frame and then ease out to the wicks' high and low.
                        PriceRangeFor(g_Ctx.candles, tvs, tvc, g_Ctx.ch.chartType, &tMin, &tMax);
                        tVol = VolumeMax(g_Ctx.candles, tvs, tvc);   // phase 21
                    }
                    LeaveCriticalSection(&g_Ctx.lock);

                    // The VOL toggle (phase 22): dispVolF eases towards 0 or 1,
                    // regardless of whether there are candles - a toggle just
                    // before an interval switch must also settle. The snap is a
                    // quarter pixel of the bars' height, as for the others.
                    {
                        double vfT = ShowVolNow(&g_Ctx) ? 1.0 : 0.0;
                        if (g_Ctx.ch.dispVolF != vfT) {
                            double bandPx = (double)ChartVolBarsH(&gE);
                            double snapF  = (bandPx > 1.0) ? SNAP_PX / bandPx : 1.0;
                            g_Ctx.ch.dispVolF = AnimStep(g_Ctx.ch.dispVolF, vfT, dt,
                                                      ANIM_TAU_VIEW, snapF);
                            redraw = TRUE;
                            if (g_Ctx.ch.dispVolF != vfT) settled = FALSE;
                        }
                    }

                    if (tn > 0 && tvc > 0 && g_Ctx.ch.dispValid &&
                        gE.cw > 0 && gE.ch > 0) {
                        double dc = (g_Ctx.ch.dispCount > 1.0) ? g_Ctx.ch.dispCount : 1.0;
                        double snapX = SNAP_PX * dc / (double)gE.cw;
                        double snapY = SNAP_PX * (tMax - tMin) / (double)gE.ch;
                        if (snapX <= 0.0) snapX = 1e-9;
                        if (snapY <= 0.0) snapY = 1e-9;

                        // The volume scale eases like the price axis: a quarter
                        // pixel of the bars' height in volume units (phase 21;
                        // phase 43: the pane's, when the volume has one).
                        // Without easing the bars would jump the moment a larger
                        // candle entered the view, while the candles glide.
                        int volPx = ChartVolBarsH(&gE);
                        double snapV = SNAP_PX * tVol / (double)(volPx > 1 ? volPx : 1);
                        if (snapV <= 0.0) snapV = 1e-9;

                        struct { double* v; double t; double snap; } eases[5] = {
                            { &g_Ctx.ch.dispStart,  (double)tvs, snapX },
                            { &g_Ctx.ch.dispCount,  (double)tvc, snapX },
                            { &g_Ctx.ch.dispMin,    tMin,        snapY },
                            { &g_Ctx.ch.dispMax,    tMax,        snapY },
                            { &g_Ctx.ch.dispVolMax, tVol,        snapV },
                        };
                        BOOL viewMoved = FALSE, axisMoved = FALSE;
                        for (int e = 0; e < 5; ++e) {
                            if (*eases[e].v == eases[e].t) continue;
                            *eases[e].v = AnimStep(*eases[e].v, eases[e].t, dt,
                                                   ANIM_TAU_VIEW, eases[e].snap);
                            redraw = TRUE;
                            if (*eases[e].v != eases[e].t) settled = FALSE;
                            if (e < 2) viewMoved = TRUE;
                            else if (e < 4) axisMoved = TRUE;
                        }

                        // The hover follows the display under a pointer that
                        // rests (phase 47). The candles slide under it after a
                        // wheel notch, a double-click or a new candle, and the
                        // crosshair rode the candle it was set on away from
                        // the pointer until the next mouse move; the keys
                        // clear it instead, which stays so. The price axis
                        // rescales under a pointer in the price column, and
                        // an alert tag that slid away stayed red. Same
                        // functions as the mouse move and the click, on the
                        // display just eased (pitfall 14). Not during a drag,
                        // whose moves set both themselves.
                        if (!g_Ctx.panning) {
                            if (viewMoved && g_Ctx.ch.hoverIdx >= 0)
                                g_Ctx.ch.hoverIdx = HitCandle(&g_Ctx.ch, tn, &gE, g_Ctx.hoverX, g_Ctx.ch.hoverY);
                            if (axisMoved && g_Ctx.axisHotY >= 0 && !g_Ctx.overlayOpen)
                                AxisHoverSet(&gE, g_Ctx.axisHotY);
                        }
                    }
                }

                // The stale counter. The clock must run while we are
                // disconnected, but the text only changes once a second - so
                // we repaint only when the digit actually changes.
                ULONGLONG okTick, retryTick;
                int emptyFails;
                EnterCriticalSection(&g_Ctx.lock);
                okTick = g_Ctx.lastOkTick;
                retryTick = g_Ctx.nextRetryTick;
                emptyFails = (g_Ctx.candleCount == 0) ? g_Ctx.netFailures : 0;
                LeaveCriticalSection(&g_Ctx.lock);

                // The empty chart's "retrying in Ns" (phase 46) counts down
                // the same way. Before, only a fetch repainted it, so the
                // number stood still for the whole backoff - "retrying in
                // 12s" for twelve seconds - while no candle has come yet.
                // Not while minimized (phase 47): IsWindowVisible stays TRUE
                // for a minimized window, and the clock ticked 60 times a
                // second for a countdown nobody could see. WM_SIZE starts it
                // again on restore.
                BOOL onScreen = IsWindowVisible(hwnd) && !IsIconic(hwnd);
                if (emptyFails > 0 && onScreen) {
                    int in_s = (retryTick > now) ? (int)((retryTick - now + 999) / 1000) : 0;
                    if (in_s != g_Ctx.emptySecsShown) {
                        g_Ctx.emptySecsShown = in_s;
                        redraw = TRUE;
                    }
                    if (in_s > 0) settled = FALSE;
                }

                // IsWindowVisible is decisive: without it a disconnected
                // line keeps the clock alive on a hidden panel, and we tick 60
                // times a second without painting anything - and IsIconic
                // for a minimized one (phase 47, onScreen above). TogglePopup
                // starts it again when the panel is shown, WM_SIZE when it is
                // restored.
                if (okTick != 0 && now - okTick > STALE_AFTER && onScreen) {
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

        // A double-click on the chart resets zoom and panning. Requires
        // CS_DBLCLKS on the window class - without it the message never
        // arrives. The price column falls through to WM_LBUTTONDOWN (below).
        //
        // Phase 44: the overlay, the caption buttons and the toolbar SWALLOW
        // the second click. Up to phase 43 they fell through as well, so a
        // double-click was two clicks: on a range cell the second one
        // deselected the range the first had picked, a settings row toggled
        // back, the gear opened and closed its menu, [ + ] started two
        // instances. A double-click there is one click; two separate toggles
        // take two separate clicks. The overlay check comes first: the gear's
        // second click arrives with the menu the first one opened.
        //
        // The first click has already started panning, but WM_LBUTTONUP
        // has released it again before DBLCLK arrives. Free header area is
        // HTCAPTION and gives WM_NCLBUTTONDBLCLK (maximize) - it never gets here.
        case WM_LBUTTONDBLCLK: {
            if (g_Ctx.overlayOpen) return 0;
            {
                RECT rcD;
                GetClientRect(hwnd, &rcD);
                int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
                if (ButtonHitAt(rcD.right, IsZoomed(hwnd), mx, my) >= 0) return 0;
                RECT tb[TBAR_COUNT];
                ToolbarLayout(rcD.right, tb);
                if (ToolbarHit(tb, mx, my) >= 0) return 0;

                ChartRect gd = PanelGeometry(rcD.right, rcD.bottom);
                // [g.left, edge]: the chart and the headroom. Up to and
                // including phase 22 the area went all the way to W, with the
                // axis margin. The price column now belongs to the alerts
                // (phase 23) and falls through to WM_LBUTTONDOWN on purpose:
                // there the second click of a fast double-click is one more
                // click on the mark the first one set (set + remove).
                // Down to the lowest pane (phase 44), as the crosshair: the
                // volume pane and the RSI band show the same candles.
                if (mx >= gd.left && mx <= gd.edge && my >= gd.top && my <= ChartPanesBottom(&gd)) {
                    ResetView(&g_Ctx);
                    EnterCriticalSection(&g_Ctx.lock);
                    g_Ctx.ch.hoverIdx = HitCandle(&g_Ctx.ch, g_Ctx.candleCount, &gd, mx, my);
                    g_Ctx.ch.hoverY   = my;
                    g_Ctx.hoverX      = mx;
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
                // A setting toggles and the menu stays open (phase 42); a
                // click outside every row closes without change.
                OverlayPick(hwnd, OverlayHit(&orr, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
                return 0;
            }
            RECT rc;
            GetClientRect(hwnd, &rc);
            int dx = GET_X_LPARAM(lParam), dy = GET_Y_LPARAM(lParam);
            // The buttons. After the overlay - the first click closes the
            // overlay, even when it hits a button - and before panning, which
            // in any case only applies to the chart area. From phase 47 the
            // press only arms the button, drawn pressed; WM_LBUTTONUP acts if
            // it comes on the same button. Capture brings the release here
            // wherever the pointer has gone.
            {
                int bh = ButtonHitAt(rc.right, IsZoomed(hwnd), dx, dy);
                if (bh >= 0) {
                    g_Ctx.btnDown = bh;
                    g_Ctx.btnHot  = bh;
                    SetCapture(hwnd);
                    RECT strip;
                    ButtonStrip(rc.right, &strip);
                    InvalidateRect(hwnd, &strip, FALSE);
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

            ChartRect gg = PanelGeometry(rc.right, rc.bottom);
            // The price column (phase 23): set or remove an alert. Same
            // area as the hover block in WM_MOUSEMOVE, and same data requirement.
            if (g_Ctx.ch.dispValid && dx > gg.edge && dy >= gg.top && dy <= gg.bottom) {
                OnAxisClick(hwnd, &gg, dy);
                return 0;
            }
            // The drag covers every pane (phase 44), like the crosshair; the
            // price column above stays the price pane's - it is the alerts' axis.
            if (dx >= gg.left && dx < gg.right && dy >= gg.top && dy <= ChartPanesBottom(&gg)) {
                // Start panning. SetCapture ensures we get the mouse release
                // even if the pointer leaves the window along the way.
                // The shift is applied BEFORE the anchor is taken, as the
                // move handler does (phase 47). viewStart is already in the
                // new buffer's indices; a backfill that landed after the
                // last frame left its shift pending, and the first move
                // would have applied it to this anchor again - the view
                // jumped by the backfill, up to 360 candles.
                int vs, vc;
                EnterCriticalSection(&g_Ctx.lock);
                ApplyFrontShift(&g_Ctx.ch, g_Ctx.frontShift);
                GetView(&g_Ctx.ch, g_Ctx.candleCount, &vs, &vc);
                g_Ctx.ch.viewCount     = vc;
                LeaveCriticalSection(&g_Ctx.lock);
                g_Ctx.panning       = TRUE;
                g_Ctx.alertHot      = -1;   // phase 23: the drag owns the mouse
                g_Ctx.axisHotY      = -1;
                g_Ctx.panAnchorX    = dx;
                g_Ctx.ch.panAnchorView = vs;
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
            ChartRect gg = PanelGeometry(rc.right, rc.bottom);
            int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
            // Every pane (phase 44), the same area as the drag.
            if (!g_Ctx.overlayOpen &&
                mx >= gg.left && mx < gg.right && my >= gg.top && my <= ChartPanesBottom(&gg)) {
                g_Ctx.overlayKind = 0;   // phase 41: the picker, not the dropdown
                g_Ctx.overlayOpen = TRUE;
                g_Ctx.overlayHot  = -1;
                g_Ctx.ch.hoverIdx    = -1;   // the crosshair must not remain underneath
                StartAnim(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONUP:
            // A caption button acts here (phase 47), on the button it was
            // pressed on or not at all. btnDown goes first: ReleaseCapture
            // sends WM_CAPTURECHANGED, which must find nothing to cancel.
            if (g_Ctx.btnDown >= 0) {
                int bd = g_Ctx.btnDown;
                g_Ctx.btnDown = -1;
                ReleaseCapture();
                RECT rcU;
                GetClientRect(hwnd, &rcU);
                int hit = ButtonHitAt(rcU.right, IsZoomed(hwnd), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                RECT strip;
                ButtonStrip(rcU.right, &strip);
                InvalidateRect(hwnd, &strip, FALSE);
                if (hit == bd) OnButtonClick(hwnd, bd);
                return 0;
            }
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
            // A pressed caption button (phase 47) is taken back the same way:
            // its release will never come here.
            if (g_Ctx.btnDown >= 0 && (HWND)lParam != hwnd) {
                g_Ctx.btnDown = -1;
                g_Ctx.btnHot  = -1;
                RECT rcC;
                GetClientRect(hwnd, &rcC);
                RECT strip;
                ButtonStrip(rcC.right, &strip);
                InvalidateRect(hwnd, &strip, FALSE);
            }
            return 0;

        case WM_KEYDOWN: {
            // AltGr (phase 44). A plain Alt+key arrives as WM_SYSKEYDOWN, so
            // Alt held down HERE means Ctrl+Alt - which is what AltGr sends.
            // On the Norwegian layout AltGr+0 is '}' and AltGr+M is a
            // character too, and they reset the window and minimized it as
            // Ctrl+0 and Ctrl+M. Clearing ctrl alone would not do: AltGr+7
            // ('{') would then switch the interval and AltGr+M toggle the
            // averages. A composed character is no shortcut, so the key is
            // ignored whole.
            if (GetKeyState(VK_MENU) & 0x8000) return 0;
            // Auto-repeat (phase 44): bit 30 is set when the key was already
            // down. A held V, M, I or T flickered its toggle on and off, a
            // held 1..7 refetched the interval, a held Esc walked through all
            // its layers and hid the panel. Only the navigation keys (arrows,
            // PgUp/PgDn, Home/End, + and -) keep repeating: holding them is
            // how one scrolls. Letters, digits, F11 and Esc are commands, and
            // Enter and Space, which pick a menu row (phase 47) - a held
            // Enter toggled a setting at the repeat rate.
            if ((lParam & 0x40000000) &&
                (wParam == VK_F11 || wParam == VK_ESCAPE || wParam == VK_RETURN || wParam == VK_SPACE ||
                 (wParam >= '0' && wParam <= '9') || (wParam >= 'A' && wParam <= 'Z'))) {
                return 0;
            }
            // A drag owns the keyboard as it owns the mouse (phase 47). The
            // navigation keys and the toggles were blocked, but R and Esc
            // reset the view under the finger - the next move put it back -
            // Esc could hide the panel with the button still down, and
            // Ctrl+0 resized the window mid-drag. The drag ends at the
            // release, or when capture is lost.
            if (g_Ctx.panning) return 0;
            BOOL ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;   // phase 41
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
            // An open menu from the keyboard (phase 47; up to phase 46 only
            // Esc reached it, and the symbol could not be changed from the
            // keyboard at all). Up and Down move the highlighted row, Home
            // and End go to the ends, Enter or Space picks the row as a click
            // would, Left and Right go to the next menu in the header's order
            // - symbol, interval, settings - or, in the right-click picker,
            // to the other column. S, B and G go to their menu, or close it
            // when it is the one open. The keys a menu shows work in it: 1..7
            // in the intervals, V M I T and C (phase 49) in the settings. Esc
            // closes, below.
            if (!g_desktopMode && g_Ctx.overlayOpen && !ctrl) {
                RECT rcK;
                GetClientRect(hwnd, &rcK);
                OverlayRects ok;
                OverlayLayout(rcK.right, rcK.bottom, &ok);
                int kind = g_Ctx.overlayKind, hot = g_Ctx.overlayHot, to = -2;
                switch (wParam) {
                    case VK_DOWN: to = OverlayStep(&ok, hot, +1); break;
                    case VK_UP:   to = OverlayStep(&ok, hot, -1); break;
                    case VK_HOME: to = OverlayStep(&ok, -1, +1); break;
                    case VK_END:  to = OverlayStep(&ok, -1, -1); break;
                    case VK_RETURN: case VK_SPACE:
                        if (hot >= 0) OverlayPick(hwnd, hot);
                        return 0;
                    case VK_LEFT: case VK_RIGHT: {
                        BOOL right = (wParam == VK_RIGHT);
                        if (kind == 0) {
                            int n = right ? INTERVAL_COUNT : SYMBOL_COUNT;
                            int row = (hot < 0) ? (right ? g_Ctx.ivIdx : g_Ctx.symIdx)
                                    : (hot < SYMBOL_COUNT) ? hot : hot - SYMBOL_COUNT;
                            if (row >= n) row = n - 1;
                            to = (right ? SYMBOL_COUNT : 0) + row;
                            break;
                        }
                        static const int ORDER[3] = { 3, 1, 2 };
                        int at = (kind == 3) ? 0 : (kind == 1) ? 1 : 2;
                        OpenOverlay(hwnd, ORDER[(at + (right ? 1 : 2)) % 3], TRUE);
                        return 0;
                    }
                    case 'S': case 'B': case 'G': {
                        int want = (wParam == 'S') ? 3 : (wParam == 'B') ? 1 : 2;
                        if (want == kind) OverlayPick(hwnd, -1);
                        else              OpenOverlay(hwnd, want, TRUE);
                        return 0;
                    }
                    default: break;
                }
                if (to != -2) {
                    if (to != hot) {
                        g_Ctx.overlayHot = to;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    return 0;
                }
                if ((kind == 0 || kind == 1) && !shift &&
                    wParam >= '1' && wParam < (WPARAM)('1' + INTERVAL_COUNT)) {
                    OverlayPick(hwnd, SYMBOL_COUNT + (int)(wParam - '1'));
                    return 0;
                }
                if (kind == 2) {
                    for (int k = 0; k < SET_COUNT; ++k) {
                        if (wParam == (WPARAM)SET_KEY[k][0]) {
                            OverlayPick(hwnd, OVL_SET_FIRST + k);
                            return 0;
                        }
                    }
                    // C (phase 49): the next chart type, the menu open, as
                    // the key the CHART TYPE heading shows.
                    if (wParam == (WPARAM)CHART_TYPE_KEY[0]) {
                        OverlayPick(hwnd, OVL_TYPE_FIRST + (ChartTypeNow(&g_Ctx) + 1) % CHART_TYPE_COUNT);
                        return 0;
                    }
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
                    SetShowVolume(&g_Ctx, !ShowVolNow(&g_Ctx));
                    return 0;
                }
                // S, B and G (phase 47): the header's three menus - the
                // symbol, the interval (the bar size) and the settings - as
                // a click on their cell opens them, with the current row
                // highlighted for the arrows. Not I, the RSI band's key
                // since phase 39.
                if (!ctrl && (wParam == 'S' || wParam == 'B' || wParam == 'G')) {
                    OpenOverlay(hwnd, (wParam == 'S') ? 3 : (wParam == 'B') ? 1 : 2, TRUE);
                    return 0;
                }
                // M (phase 25): the MA pill. Without Ctrl - Ctrl+M minimizes.
                // Also works when the pill is hidden on a narrow panel.
                if (!ctrl && wParam == 'M') {
                    SetShowIndicators(&g_Ctx, !ShowIndNow(&g_Ctx));
                    return 0;
                }
                // I (phase 39): the RSI band. Works when the pill is hidden.
                if (!ctrl && wParam == 'I') {
                    SetShowRsi(&g_Ctx, !ShowRsiNow(&g_Ctx));
                    return 0;
                }
                // T (phase 40): the light theme, as the tray menu's item.
                if (!ctrl && wParam == 'T') {
                    SetLightTheme(&g_Ctx, ThemeNow(&g_Ctx) != &APP_THEME_LIGHT);
                    return 0;
                }
                // C (phase 49): the next chart type - candles, OHLC bars,
                // line, mountain and round again. Without Ctrl, which stays
                // free. A held C does not spin through them (auto-repeat is
                // dropped above for every letter), and a drag owns the keys.
                if (!ctrl && wParam == 'C') {
                    SetChartType(&g_Ctx, (ChartTypeNow(&g_Ctx) + 1) % CHART_TYPE_COUNT);
                    return 0;
                }
                // Shift+1..8 (phase 41): the ranges in the pills' order.
                if (!ctrl && shift && wParam >= '1' && wParam < (WPARAM)('1' + RANGE_COUNT)) {
                    SelectRange(&g_Ctx, (int)(wParam - '1'));
                    return 0;
                }
                // 1..7: the intervals in the dropdown's order (were the pills
                // up to phase 40). Not with Shift, which phase 41 gives the
                // ranges.
                if (!ctrl && !shift && wParam >= '1' && wParam < (WPARAM)('1' + INTERVAL_COUNT)) {
                    ApplyConfigChoice(&g_Ctx, SYMBOL_COUNT + (int)(wParam - '1'));
                    return 0;
                }
                // A (phase 23): set an alert at the crosshair's price - the
                // same number shown in the label on the axis, rounded as a
                // click in the column would. Without a crosshair there is no
                // price to point at, and the key does nothing. hoverY is
                // clamped at the top as in ChartDrawBody. Below the price pane
                // (phase 44) the crosshair stands in the volume pane or the RSI
                // band, which have no price: the key does nothing there either.
                // Up to phase 43 the y was clamped to the price pane's bottom,
                // and an alert appeared at the lowest price in view.
                if (!ctrl && wParam == 'A') {
                    if (g_Ctx.ch.hoverIdx >= 0 && g_Ctx.ch.dispValid) {
                        RECT rcA;
                        GetClientRect(hwnd, &rcA);
                        ChartRect ga = PanelGeometry(rcA.right, rcA.bottom);
                        int hy = g_Ctx.ch.hoverY;
                        if (hy < ga.top) hy = ga.top;
                        if (hy <= ga.bottom) AlertAdd(&g_Ctx, AlertPriceAtY(&g_Ctx.ch, &ga, hy));
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
                        int ks0, kc0, ks1, kc1;
                        GetView(&g_Ctx.ch, g_Ctx.candleCount, &ks0, &kc0);
                        if (zoom != 0) {
                            atWall = ZoomView(&g_Ctx.ch, g_Ctx.candleCount, 0.5, zoom);
                        } else {
                            int vs, vc;
                            GetView(&g_Ctx.ch, g_Ctx.candleCount, &vs, &vc);
                            int step = vc / 8;
                            if (step < 1) step = 1;
                            int delta = 0;
                            if (pan == -1 || pan == 1) delta = pan * step;
                            else if (pan == -2)        delta = -vc;
                            else if (pan == 2)         delta = vc;
                            else if (pan == -3)        delta = -g_Ctx.candleCount;
                            else                       delta = g_Ctx.candleCount;
                            atWall = PanView(&g_Ctx.ch, g_Ctx.candleCount, delta);
                        }
                        GetView(&g_Ctx.ch, g_Ctx.candleCount, &ks1, &kc1);
                        if (ks1 != ks0 || kc1 != kc0) g_Ctx.rangeWant = 0;   // phase 41: left home
                        g_Ctx.ch.hoverIdx = -1;
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
                g_Ctx.ch.hoverIdx = -1;
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


// The saved rectangle, if there is one and it is still on a monitor. TogglePopup
// creates the panel there and PlacePopupInitially puts it there exactly.
static BOOL SavedPanelRect(RECT* r) {
    int w = (g_savedPanelW > 0) ? g_savedPanelW : POPUP_W;
    int h = (g_savedPanelH > 0) ? g_savedPanelH : POPUP_H;
    if (g_savedPanelX == GEOM_UNSET || g_savedPanelY == GEOM_UNSET ||
        !PlacementIsVisible(g_savedPanelX, g_savedPanelY, w, h)) return FALSE;
    SetRect(r, g_savedPanelX, g_savedPanelY, g_savedPanelX + w, g_savedPanelY + h);
    return TRUE;
}

// Places the window the first time it is created: the saved position if it
// exists and is still visible, otherwise centered.
//
// The size is device pixels at g_savedPanelDpi (phase 48). The style is
// already built for the monitor the panel was created on (TogglePopup), and
// if that monitor's dpi is not the one the size was saved at - its scale was
// changed while TickC was not running - the size is scaled once to keep the
// panel's logical size, as WM_DPICHANGED does for a running panel.
static void PlacePopupInitially(HWND hwnd) {
    RECT r;
    if (SavedPanelRect(&r)) {
        int w = r.right - r.left, h = r.bottom - r.top;
        int sd = g_savedPanelDpi, nd = g_Ctx.sty.dpi;
        if (sd > 0 && nd > 0 && sd != nd) { w = MulDiv(w, nd, sd); h = MulDiv(h, nd, sd); }
        // A duplicate's rectangle is its parent's GetWindowRect plus the
        // cascade (SpawnInstance): screen coordinates, not a placement's.
        if (g_isDuplicate) SetWindowPos(hwnd, NULL, r.left, r.top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        else               SetPanelPlacement(hwnd, r.left, r.top, w, h);
        return;
    }
    ResetToDefaultView(hwnd);
}

// Checked on every show (phase 48). Windows moves the VISIBLE windows off a
// monitor that goes away, not the hidden ones: a panel hidden on a laptop's
// external monitor came back after undocking where no monitor is, and up to
// phase 47 only the creation looked (review E9). The header must lie on a
// work area, PANEL_GRAB_W wide and half its height tall, or there is nothing
// to take the panel by - a panel 200 px past the left edge is fine, one with
// its header above the screen is not. Otherwise the panel is moved onto the
// work area of the monitor nearest it, keeping its size unless it is larger.
// A maximized one is moved to fill that work area, where WM_GETMINMAXINFO
// would have maximized it; its restored rectangle is checked on the restore
// (BTN_MAX). A minimized panel is checked when it is restored.
static void EnsurePanelOnScreen(HWND hwnd) {
    if (!hwnd || g_desktopMode || IsIconic(hwnd)) return;
    RECT rw, band, vis;
    GetWindowRect(hwnd, &rw);
    int w = rw.right - rw.left, h = rw.bottom - rw.top;
    SetRect(&band, rw.left, rw.top, rw.right, rw.top + Dp(HEADER_H));
    MONITORINFO mi = { sizeof(MONITORINFO) };
    if (!GetMonitorInfoW(MonitorFromRect(&band, MONITOR_DEFAULTTONEAREST), &mi)) return;
    RECT wa = mi.rcWork;
    int grab = (w < Dp(PANEL_GRAB_W)) ? w : Dp(PANEL_GRAB_W);
    if (IntersectRect(&vis, &band, &wa) && vis.right - vis.left >= grab &&
        vis.bottom - vis.top >= Dp(HEADER_H) / 2) {
#ifdef TICKER_PROBE
        g_probeOnScreen = 1;
#endif
        return;
    }
    int aw = wa.right - wa.left, ah = wa.bottom - wa.top;
    if (IsZoomed(hwnd)) {
        SetWindowPos(hwnd, NULL, wa.left, wa.top, aw, ah, SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        if (w > aw) w = aw;
        if (h > ah) h = ah;
        int x = rw.left, y = rw.top;
        if (x > wa.right - w)  x = wa.right - w;
        if (x < wa.left)       x = wa.left;
        if (y > wa.bottom - h) y = wa.bottom - h;
        if (y < wa.top)        y = wa.top;
        SetWindowPos(hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
#ifdef TICKER_PROBE
    g_probeOnScreen = 2;
#endif
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

    // Phase 46: a Progman that did not answer the first is not asked the
    // second - that halves what a hung Explorer can hold the UI thread for.
    DWORD_PTR res;
    if (SendMessageTimeoutW(progman, PROGMAN_SPAWN_WORKERW, 0xD, 0x1,
                            SMTO_NORMAL | SMTO_ABORTIFHUNG, 1000, &res)) {
        SendMessageTimeoutW(progman, PROGMAN_SPAWN_WORKERW, 0, 0,
                            SMTO_NORMAL | SMTO_ABORTIFHUNG, 1000, &res);
    }

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
// TogglePopup): GetSystemMetrics follows the thread's context. The whole
// process is per-monitor aware from phase 37, so this is now a no-op kept
// as a guard - it made the surface work while the rest was DPI-unaware.
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


// The dpi the surface is drawn for (phase 37). The panel follows its
// monitor; the desktop surface stays at 96 - its only text is the stamp,
// which already follows the surface height, and the desktop is meant to
// look the same as before (it has been per-monitor aware since phase 9).
static int PanelDpi(HWND hwnd) {
    if (g_desktopMode) return CHART_DPI_BASE;
#ifdef TICKER_PROBE
    if (g_forceDpi > 0) return g_forceDpi;
#endif
#ifdef TICKER_PROBE
    if (g_fakeMonDpi > 0 && hwnd) {   // phase 48, see g_fakeMonX0
        RECT rf;
        GetWindowRect(hwnd, &rf);
        if ((rf.left + rf.right) / 2 >= g_fakeMonX0) return g_fakeMonDpi;
    }
#endif
    UINT d = hwnd ? GetDpiForWindow(hwnd) : 0;
    return d ? (int)d : CHART_DPI_BASE;
}

// Rebuilds everything that is sized for the dpi or colored by the theme: the
// chart's style (fonts, colors, and the dpi Dp and the engine read), the
// header's price font and the control buttons' pens and brush. The watermark
// is keyed on the size alone, so it is invalidated here. A no-op when both
// the dpi and the theme are unchanged - it was ApplyPanelDpi until phase 40,
// and comparing the dpi alone would leave a theme switch undrawn.
static void ApplyPanelStyle(AppContext* ctx, int dpi) {
    const AppTheme* th = ThemeNow(ctx);
    if (ctx->sty.fontSmall && ctx->sty.dpi == dpi && ctx->theme == th) return;
    // The back buffer goes FIRST (phase 47). The header, the overlay and the
    // empty chart leave fontSmall - and the header hFontQuote - selected in
    // it between frames, and DeleteObject on a font that is selected in a DC
    // fails by contract. Up to phase 46 only bbValid was cleared, though the
    // comments said the buffer was dropped. Measured, the GDI count did not
    // grow over 50 theme switches or 20 dpi changes, with or without this -
    // GDI seems to reclaim the font when the DC lets it go - but the contract
    // is what is written here, not what one build of Windows does. The
    // buffer is built again on the next frame: one allocation per theme or
    // dpi change.
    FreeBackBuffer(ctx);
    ChartStyleDestroy(&ctx->sty);
    ChartStyleCreate(&ctx->sty, dpi, th->chart);
    ctx->theme = th;
    if (ctx->hFontQuote) DeleteObject(ctx->hFontQuote);
    ctx->hFontQuote = CreateFontW(-ChartPx(dpi, 13), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,   // phase 42: the quote line's values
                                DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (ctx->penBtn)      DeleteObject(ctx->penBtn);
    if (ctx->penBtnHot)   DeleteObject(ctx->penBtnHot);
    if (ctx->penBtnWhite) DeleteObject(ctx->penBtnWhite);
    if (ctx->brClose)     DeleteObject(ctx->brClose);
    ctx->penBtn      = CreatePen(PS_SOLID, 1, ctx->sty.clr.dim);
    ctx->penBtnHot   = CreatePen(PS_SOLID, 1, ctx->sty.clr.text);
    ctx->penBtnWhite = CreatePen(PS_SOLID, 1, ctx->sty.clr.onHot);
    ctx->brClose     = CreateSolidBrush(ctx->sty.clr.hot);
    ctx->wmValid = FALSE;
    ctx->bbValid = FALSE;
}

// Tray click. With a normal window the expected behavior is: if it is in
// front and active, hide it; otherwise show it and give it focus. A
// minimized window is restored.
//
// "In front" is the foreground window OR a panel that lost activation less
// than TRAY_CLICK_GRACE_MS ago (phase 46). A real click on the icon can make
// the taskbar the foreground window on the button's way DOWN, and the
// callback comes on the way up: then the panel is no longer the foreground,
// and the test alone showed it again instead of hiding it. That is the
// review's reading and not measured - the posted clicks of the tests never
// move the foreground (pitfall 6), which is also why no test saw it. Either
// way the panel hides: in front, or just left. The grace is short on
// purpose: the price of a wrong guess - the user left the
// panel and clicked the icon within half a second to bring it back - is a
// hidden panel the next click shows, never one that cannot be reached.
static void TogglePopup(AppContext* ctx, HINSTANCE hInst) {
    if (ctx->hPopup && IsWindowVisible(ctx->hPopup)) {
        BOOL front   = (GetForegroundWindow() == ctx->hPopup);
        BOOL justLeft = !front && ctx->popupDeactTick != 0 &&
                        GetTickCount64() - ctx->popupDeactTick <= TRAY_CLICK_GRACE_MS;
        if (!IsIconic(ctx->hPopup) && (front || justLeft)) {
            ctx->ch.hoverIdx = -1;
            ctx->overlayOpen = FALSE;
            ctx->overlayF    = 0.0;
            ctx->overlayHot  = -1;
            ctx->btnHot      = -1;
            ctx->btnDown     = -1;   // phase 47
            ctx->tbHot       = -1;
            SaveWindowPlacement(ctx->hPopup);
            ShowWindow(ctx->hPopup, SW_HIDE);
            ctx->popupDeactTick = 0;   // SW_HIDE deactivates; that is not a click
#ifdef TICKER_PROBE
            g_probeTrayDecision = front ? 1 : 2;
#endif
            return;
        }
        if (IsIconic(ctx->hPopup)) {
            ShowWindow(ctx->hPopup, SW_RESTORE);
            EnsurePanelOnScreen(ctx->hPopup);   // phase 48
        }
        ForceForeground(ctx->hPopup);
#ifdef TICKER_PROBE
        g_probeTrayDecision = 3;
#endif
        return;
    }
#ifdef TICKER_PROBE
    g_probeTrayDecision = 4;
#endif

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
        // But the panel is created AT its saved rectangle when there is one
        // on a monitor (phase 48). A per-monitor aware window takes the dpi
        // of the monitor it is created on, and the style below reads it
        // (PanelDpi). Created at 0,0 and moved to a saved rectangle on a
        // 150 % monitor, the panel was styled for the primary's dpi and then
        // either got a WM_DPICHANGED that scaled a size already in device
        // pixels - a panel half as large again every session - or, with no
        // message, kept the primary's fonts (review E5). The desktop surface
        // is placed by AttachToDesktop and keeps 0,0.
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

        // The desktop surface is created per-monitor aware (phase 9; the
        // whole process is from phase 37, and this is then a no-op kept as a
        // guard). Measured at 150 % while the process was unaware: without this
        // SM_CXSCREEN/SM_CYSCREEN gave a virtualized 2560x1067, and the
        // surface covered only the upper left corner of a WorkerW of
        // 3840x1600 physical pixels. A window created in this context keeps
        // it, and WM_PAINT runs in the window's context - GetClientRect then
        // gives physical pixels, and the layout is drawn 1:1 in raw pixels as
        // at 100 %. The context is restored as soon as the surface is placed.
        DPI_AWARENESS_CONTEXT prevDpi = g_desktopMode
            ? SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
            : NULL;
        RECT rc0 = { 0, 0, POPUP_W, POPUP_H };
        if (!g_desktopMode) SavedPanelRect(&rc0);   // unchanged when there is none
        HWND hp = CreateWindowExW(
            0,
            L"BTCPopupClass", L"TickC",
            style,
            rc0.left, rc0.top, rc0.right - rc0.left, rc0.bottom - rc0.top,
            NULL, NULL, hInst, NULL);
        BOOL attached = hp && g_desktopMode && AttachToDesktop(hp);
        if (prevDpi) SetThreadDpiAwarenessContext(prevDpi);
        if (!hp) return;

        if (g_desktopMode) {
            if (!attached) {
                // No WorkerW (Explorer is starting, or not running). hPopup
                // is not set, so WM_NCDESTROY leaves the timer alone - it is
                // set here, later for each failure in a row (phase 46).
                DestroyWindow(hp);
                if (g_embedFails < INT_MAX) g_embedFails++;
                SetTimer(ctx->hWnd, TIMER_EMBED_ID, EmbedRetryMs(g_embedFails), NULL);
                return;
            }
            g_embedFails = 0;
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
    ctx->ch.hoverIdx  = -1;
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
    ctx->btnDown     = -1;      // phase 47, same reason
    ctx->alertHot    = -1;      // phase 23, same reason
    ctx->axisHotY    = -1;
    ctx->ch.dispValid   = FALSE;   // the panel opens finished, does not glide into place

    ApplyPanelStyle(ctx, PanelDpi(ctx->hPopup));   // phase 37: before the placement reads Dp
    UpdatePopupTitle(ctx);
    if (g_desktopMode) {
        // AttachToDesktop set the geometry. No activation and no
        // foreground: a child of Explorer's WorkerW must never take focus
        // from what the user is doing.
        ShowWindow(ctx->hPopup, SW_SHOWNA);
    } else {
        if (created) PlacePopupInitially(ctx->hPopup);
        EnsurePanelOnScreen(ctx->hPopup);   // phase 48: new, or hidden since
        ShowWindow(ctx->hPopup, SW_SHOW);
        ForceForeground(ctx->hPopup);
    }
    ctx->popupDeactTick = 0;   // phase 46: shown, so not "just left"
    // The day's statistics with the candles, not up to five cycles later
    // (phase 46, see dayRefresh).
    EnterCriticalSection(&ctx->lock);
    ctx->dayRefresh = TRUE;
    LeaveCriticalSection(&ctx->lock);
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

// A second start of TickC (phase 46): the panel comes forward, whatever it
// was - hidden, minimized, behind other windows or already in front. Not
// TogglePopup, which hides a panel that is in front, or one that has just
// lost activation to the Explorer window the start came from.
static void ShowPanel(AppContext* ctx, HINSTANCE hInst) {
    if (ctx->hPopup && IsWindowVisible(ctx->hPopup)) {
        if (IsIconic(ctx->hPopup)) {
            ShowWindow(ctx->hPopup, SW_RESTORE);
            EnsurePanelOnScreen(ctx->hPopup);   // phase 48
        }
        ForceForeground(ctx->hPopup);
        return;
    }
    TogglePopup(ctx, hInst);
}

// Switches between the panel and the desktop surface while the process runs
// (phase 12).
//
// The window is CREATED AGAIN, it is not moved with SetParent. The DPI
// context is set when a window is created and cannot be changed: the desktop
// surface must be created per-monitor aware (otherwise it covers a quarter
// of the screen at 150 %, see TogglePopup); until phase 37 the panel was
// DPI-unaware, and a moved window would have had the wrong context in one of
// the modes. Both are per-monitor aware now, but the surface is still drawn
// at 96 and the panel at its monitor's dpi (PanelDpi).
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
    g_embedFails = 0;   // phase 46: a new mode starts with fast tries

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
    ctx->btnDown       = -1;   // phase 47: its capture went with the window
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
    ctx->ch.dispVolF = ShowVolNow(ctx) ? 1.0 : 0.0;
    ctx->ch.dispIndF = ShowIndNow(ctx) ? 1.0 : 0.0;
    ctx->ch.dispRsiF = ShowRsiNow(ctx) ? 1.0 : 0.0;
    // And the chart type (phase 49), before TogglePopup: the new surface's
    // first frame snaps its axis with it (SyncDisp).
    ctx->ch.chartType = ChartTypeNow(ctx);

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

// The keys (phase 47): 1..7 pick the intervals in the panel, as the list
// under the interval cell shows. In desktop mode the surface takes no keys,
// and the menu shows none (see BuildTrayMenu).
static HMENU BuildIntervalMenu(void) {
    HMENU h = CreatePopupMenu();
    if (!h) return NULL;
    for (int i = 0; i < INTERVAL_COUNT; i++) {
        wchar_t lbl[24];
        if (g_desktopMode) swprintf_s(lbl, 24, L"%s", INTERVALS[i].label);
        else               swprintf_s(lbl, 24, L"%s\t%d", INTERVALS[i].label, i + 1);
        AppendMenuW(h, MF_STRING, (UINT_PTR)(ID_TRAY_INTERVAL_FIRST + i), lbl);
    }
    CheckMenuRadioItem(h, ID_TRAY_INTERVAL_FIRST, ID_TRAY_INTERVAL_FIRST + INTERVAL_COUNT - 1,
                       (UINT)(ID_TRAY_INTERVAL_FIRST + g_Ctx.ivIdx), MF_BYCOMMAND);
    return h;
}

// The ranges (phase 41). The checked one is the selected range. Phase 47
// adds "None", checked when no range is selected: up to phase 46 a range
// was ended by picking its checked item again, like a second click on its
// pill - which a radio group does not suggest. Picking the checked item now
// does what a radio item does (see WM_COMMAND). Shift+1..8 in the panel.
static HMENU BuildRangeMenu(void) {
    HMENU h = CreatePopupMenu();
    if (!h) return NULL;
    for (int i = 0; i < RANGE_COUNT; i++) {
        wchar_t lbl[24];
        if (g_desktopMode) swprintf_s(lbl, 24, L"%s", RANGES[i].label);
        else               swprintf_s(lbl, 24, L"%s\tShift+%d", RANGES[i].label, i + 1);
        AppendMenuW(h, MF_STRING, (UINT_PTR)(ID_TRAY_PERIOD_FIRST + i), lbl);
    }
    AppendMenuW(h, MF_SEPARATOR, 0, NULL);
    AppendMenuW(h, MF_STRING, (UINT_PTR)(ID_TRAY_PERIOD_FIRST + RANGE_COUNT), L"None");
    CheckMenuRadioItem(h, ID_TRAY_PERIOD_FIRST, ID_TRAY_PERIOD_FIRST + RANGE_COUNT,
                       (UINT)(ID_TRAY_PERIOD_FIRST + ((g_Ctx.rangeIdx >= 0) ? g_Ctx.rangeIdx : RANGE_COUNT)),
                       MF_BYCOMMAND);
    return h;
}

// The chart types (phase 49): a radio group, checked on the mode's type, the
// names the settings menu's CHART TYPE rows have. The key, C, is on the
// submenu's own item (see BuildTrayMenu): it steps through the list and
// picks no one item of it.
static HMENU BuildChartTypeMenu(void) {
    HMENU h = CreatePopupMenu();
    if (!h) return NULL;
    for (int t = 0; t < CHART_TYPE_COUNT; t++)
        AppendMenuW(h, MF_STRING, (UINT_PTR)(ID_TRAY_TYPE_FIRST + t), CHART_TYPE_LABEL[t]);
    CheckMenuRadioItem(h, ID_TRAY_TYPE_FIRST, ID_TRAY_TYPE_FIRST + CHART_TYPE_COUNT - 1,
                       (UINT)(ID_TRAY_TYPE_FIRST + ChartTypeNow(&g_Ctx)), MF_BYCOMMAND);
    return h;
}

// One of the settings menu's choices as a tray item (phase 45): the same
// name and key as the gear menu's row, and the same check - SettingOn reads
// the mode's choice, as the tray items always did. The key only in panel
// mode (phase 47): the desktop surface takes no keys.
static void AppendSettingItem(HMENU h, UINT id, int k) {
    wchar_t lbl[48];
    if (g_desktopMode) swprintf_s(lbl, 48, L"%s", SET_LABEL[k]);
    else               swprintf_s(lbl, 48, L"%s\t%s", SET_LABEL[k], SET_KEY[k]);
    AppendMenuW(h, MF_STRING | (SettingOn(&g_Ctx, k) ? MF_CHECKED : MF_UNCHECKED), id, lbl);
}

// The tray menu. A separate function so the check marks and content can be
// tested without a tray icon.
//
//       Show panel                   (bold, the default; not in desktop mode)
//   ---------------------------
//       Symbol       S    >   (o) BTC/USDT  ( ) ETH/USDT  ...
//       Interval     B    >   (o) 1m  1   ( ) 5m  2  ...
//       Range             >   (o) 1D  Shift+1  ...  ( ) None
//       Chart type   C    >   (o) Candles  ( ) OHLC bars  ( ) Line  ( ) Mountain
//       Volume ... Light theme   V M I T, checked
//       Clear price alerts (n)
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
// time. The keys after a tab are the panel's, and from phase 47 only panel
// mode shows them: the desktop surface takes no keys. There is no "New
// panel" item: Ctrl+N and [ + ] cascade from the panel's own place, which
// the tray does not have when the panel has never been opened.
static HMENU BuildTrayMenu(void) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return NULL;
    // Phase 47: the panel as the bold default item, first - the Windows
    // convention for a tray menu, and the item that says what the left click
    // does. It shows the panel, never hides it (ShowPanel, as a second start
    // does). Not in desktop mode, where the click does nothing either.
    if (!g_desktopMode) {
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN, L"Show panel");
        SetMenuDefaultItem(hMenu, ID_TRAY_OPEN, FALSE);
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    }
    {
        HMENU hSym = BuildSymbolMenu();
        HMENU hIv  = BuildIntervalMenu();
        // S and B open the same lists in the panel (phase 47); desktop mode
        // shows no keys.
        if (hSym) AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSym, g_desktopMode ? L"Symbol" : L"Symbol\tS");
        if (hIv)  AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hIv,  g_desktopMode ? L"Interval" : L"Interval\tB");
        HMENU hRg = BuildRangeMenu();   // phase 41
        if (hRg)  AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hRg,  L"Range");
        // Phase 49: the chart type for the mode we are in - on the desktop
        // the surface's own, which the tray is the only way to change.
        HMENU hCt = BuildChartTypeMenu();
        if (hCt)  AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hCt,
                              g_desktopMode ? L"Chart type" : L"Chart type\t" CHART_TYPE_KEY);
        // The VOL toggle (phase 22; desktop mode has no toolbar), the MA
        // toggle (phase 25; since phase 27 also VWAP and today's high/low),
        // the RSI band (phase 39) and the light theme (phase 40), each for
        // the mode we are in. Phase 45 names them as the gear menu does
        // (were "Volume bars", "Indicators", "RSI band").
        AppendSettingItem(hMenu, ID_TRAY_VOLUME,     SET_VOL);
        AppendSettingItem(hMenu, ID_TRAY_INDICATORS, SET_IND);
        AppendSettingItem(hMenu, ID_TRAY_RSI,        SET_RSI);
        AppendSettingItem(hMenu, ID_TRAY_THEME,      SET_THEME);
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
                ID_TRAY_RESET, g_desktopMode ? L"Default view" : L"Default view\tCtrl+0");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    if (!g_isDuplicate) {
        AppendMenuW(hMenu, MF_STRING | (AutostartPresent() ? MF_CHECKED : MF_UNCHECKED),
                    IDM_TOGGLE_AUTOSTART, L"Start at sign-in");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    }
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Quit TickC");
    return hMenu;
}

// Tears down the panel or the desktop surface at exit: WM_DESTROY and, from
// phase 44, "Quit TickC". The quit path used to leave the window standing
// while WinMain waited for the worker thread - seconds, with a request in
// flight. The panel is NOT owned by the main window (ownership would remove
// its taskbar button), so Windows does not tear it down for us. hPopup is in
// the lock domain and the worker thread is still alive here - it is stopped
// only after the message loop - so it is cleared under the lock BEFORE
// DestroyWindow; WM_NCDESTROY is then a no-op and starts no rebuild timer.
// SaveGeometry skips desktop mode and duplicates by itself. The embed timer
// dies first, as in SetDesktopMode: on the quit path the main window outlives
// the surface, and a pending tick would see hPopup NULL and build it again.
static void DestroyPanelSurface(void) {
    KillTimer(g_Ctx.hWnd, TIMER_EMBED_ID);
    if (!g_Ctx.hPopup) return;
    HWND hp = g_Ctx.hPopup;
    SaveWindowPlacement(hp);
    EnterCriticalSection(&g_Ctx.lock);
    g_Ctx.hPopup = NULL;
    LeaveCriticalSection(&g_Ctx.lock);
    DestroyWindow(hp);
}

// Takes the icon out of the notification area. The test build also counts
// the removals in the registry (phase 46): the process is gone by the time a
// probe could ask, and the hidden test desktop has no notification area to
// look at.
static void RemoveTrayIcon(void) {
    Shell_NotifyIconW(NIM_DELETE, &g_Ctx.nid);
#ifdef TICKER_PROBE
    static DWORD removed = 0;
    removed++;
    RegSetKeyValueW(HKEY_CURRENT_USER, REG_PATH, L"ProbeTrayRemoved", REG_DWORD,
                    &removed, sizeof(removed));
#endif
}

// A second start while the process draws on the desktop (phase 46). The
// surface is already on screen and takes no input, and the start must not
// undo the user's choice of mode, so the answer is a word from the tray
// icon about where the controls are. No sound: nothing is wrong. nid is
// copied, as in FireAlert.
static void ShowRunningNote(AppContext* ctx) {
#ifdef TICKER_PROBE
    if (g_probeMute) return;
#endif
    NOTIFYICONDATAW n = ctx->nid;
    n.uFlags      = NIF_INFO;
    n.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    wcscpy_s(n.szInfoTitle, 64, L"TickC is already running");
    wcscpy_s(n.szInfo, 256, L"It is drawing the chart on the desktop. Right-click this icon "
                            L"for the menu; turn off Desktop mode there for the panel.");
    Shell_NotifyIconW(NIM_MODIFY, &n);
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
                // Phase 46: TPM_RIGHTBUTTON lets the right button pick an
                // item, as in every other tray menu, and the WM_NULL after
                // is the documented follow-up (TrackPopupMenu's remarks):
                // without a message to our window after the menu, the next
                // one can close the moment it opens.
                TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                               pt.x, pt.y, 0, hwnd, NULL);
                PostMessageW(hwnd, WM_NULL, 0, 0);
                DestroyMenu(hMenu);
            }
            break;

        // A second start of TickC has handed over to us (phase 46; see
        // HandOverToMainInstance). Posted, so it arrives in the message loop,
        // after the lock and the thread exist; the hWakeEvent check is the
        // same guard as WM_POWERBROADCAST's all the same. A duplicate's main
        // window has another title and is never sent it.
        case WM_APP_SHOW:
#ifdef TICKER_PROBE
            InterlockedIncrement(&g_probeHandovers);
#endif
            if (g_isDuplicate || !g_Ctx.hWakeEvent) return 0;
            if (g_desktopMode) ShowRunningNote(&g_Ctx);
            else ShowPanel(&g_Ctx, (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
            return 0;

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
            if (LOWORD(wParam) == ID_TRAY_RSI) {
                SetShowRsi(&g_Ctx, !ShowRsiNow(&g_Ctx));
                return 0;
            }
            if (LOWORD(wParam) == ID_TRAY_THEME) {
                SetLightTheme(&g_Ctx, ThemeNow(&g_Ctx) != &APP_THEME_LIGHT);
                return 0;
            }
            if (LOWORD(wParam) == ID_TRAY_ALERTS_CLEAR) {
                AlertsClear(&g_Ctx);
                return 0;
            }
            // The bold default item (phase 47). Not in desktop mode, where it
            // is not in the menu: a posted message does not care about that.
            if (LOWORD(wParam) == ID_TRAY_OPEN) {
                if (!g_desktopMode) ShowPanel(&g_Ctx, (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
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
                // The ranges and "None" (phase 47) as a radio group: the
                // checked item picked again changes nothing - up to phase 46
                // it ended the range, as a second click on its pill does -
                // and a range whose view has moved away goes home.
                if (id >= ID_TRAY_PERIOD_FIRST && id <= ID_TRAY_PERIOD_FIRST + RANGE_COUNT) {
                    int r = id - ID_TRAY_PERIOD_FIRST;
                    if (r == RANGE_COUNT) r = -1;
                    if (r == g_Ctx.rangeIdx && (r < 0 || g_Ctx.rangeWant > 0)) return 0;
                    SelectRange(&g_Ctx, r);
                    return 0;
                }
                // The chart types (phase 49), a radio group: the checked one
                // picked again changes nothing (SetChartType).
                if (id >= ID_TRAY_TYPE_FIRST && id < ID_TRAY_TYPE_FIRST + CHART_TYPE_COUNT) {
                    SetChartType(&g_Ctx, id - ID_TRAY_TYPE_FIRST);
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
                RemoveTrayIcon();
                // Gone before WinMain waits for the worker thread (phase 44).
                DestroyPanelSurface();
                PostQuitMessage(0);
            }
            break;

        // The worker thread has stored new data. All we do here is read the
        // price under the lock and paint - no network traffic.
        case WM_APP_DATA: {
            double price;
            ULONGLONG okTick;
            int failures;
            EnterCriticalSection(&g_Ctx.lock);
            price  = g_Ctx.lastPrice;
            okTick = g_Ctx.lastOkTick;
            failures = g_Ctx.netFailures;
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
            // No price to show and the last fetch failed (phase 46): from the
            // first failure, as the panel's empty chart says "No connection".
            if (price <= 0.0 && failures > 0) ShowOfflineIcon(&g_Ctx);
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
                // Phase 41: a range longer than the buffer asks the same way.
                {
                    EnterCriticalSection(&g_Ctx.lock);
                    BOOL need = AppWantsHistory(&g_Ctx);
                    LeaveCriticalSection(&g_Ctx.lock);
                    if (need) RequestHistory(&g_Ctx);
                }
                // The clock must run while we are disconnected, otherwise
                // the seconds counter in the subtitle freezes.
                // New candles can move the Y target, and when disconnected
                // the counter must run. Not for a minimized panel (phase
                // 47): nothing there eases, and WM_SIZE starts the clock
                // when it is restored.
                if (!IsIconic(g_Ctx.hPopup)) StartAnim(g_Ctx.hPopup);
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
            // 116 (phase 44): the tray icon's flags as the next NIM_ADD after
            // an Explorer restart would send them; NIF_MESSAGE (1) must be set.
            if (wParam == 116) return (LRESULT)g_Ctx.nid.uFlags;
            // 117-121 (phase 46): see g_probeTrayDecision. 122 is the delay
            // before the next try at the desktop surface after lParam failed
            // tries; 123 the network failures in a row, readable with the
            // panel closed (field 9 lives on the panel).
            if (wParam == 117) return (LRESULT)g_probeTrayDecision;
            if (wParam == 118) return (LRESULT)g_probeHandovers;
            if (wParam == 119) return (LRESULT)g_probeIconState;
            if (wParam == 120) return (LRESULT)g_probeDayFetches;
            if (wParam == 121) return (LRESULT)g_probeAccessType;
            if (wParam == 122) return (LRESULT)EmbedRetryMs((int)lParam);
            if (wParam == 123) {
                LRESULT nf;
                EnterCriticalSection(&g_Ctx.lock);
                nf = g_Ctx.netFailures;
                LeaveCriticalSection(&g_Ctx.lock);
                return nf;
            }
            //   124  WRITING (phase 46): the held trading day becomes
            //        yesterday's, as at midnight UTC, and a visible panel is
            //        repainted before SendMessage returns - so field 68 read
            //        right after shows the quote line without the fetch that
            //        the next cycle makes.
            if (wParam == 124) {
                EnterCriticalSection(&g_Ctx.lock);
                g_Ctx.dayUtc -= 1;
                LeaveCriticalSection(&g_Ctx.lock);
                if (g_Ctx.hPopup && IsWindowVisible(g_Ctx.hPopup)) {
                    InvalidateRect(g_Ctx.hPopup, NULL, FALSE);
                    UpdateWindow(g_Ctx.hPopup);
                }
                return 1;
            }
            //   125  READING (phase 47): the tray menu as the next right-click
            //        would build it. The low byte of lParam asks: 0 the
            //        default (bold) item's id, -1 none; 1 how many items,
            //        submenus included, carry a key after a tab; 2 the state
            //        (GetMenuState) of the item with the id in the high word,
            //        -1 when there is none; 3 whether that item's label has a
            //        key. Bit 8 builds it as desktop mode would: the flag is
            //        set around the build alone, on this thread, so nothing
            //        else sees it.
            if (wParam == 125) {
                BOOL wasDesk = g_desktopMode;
                if (lParam & 0x100) g_desktopMode = TRUE;
                HMENU hm = BuildTrayMenu();
                g_desktopMode = wasDesk;
                if (!hm) return -2;
                LRESULT res = -1;
                UINT q = (UINT)(lParam & 0xFF), qid = (UINT)((lParam >> 16) & 0xFFFF);
                if (q == 0) {
                    UINT d = GetMenuDefaultItem(hm, FALSE, 0);
                    res = (d == (UINT)-1) ? -1 : (LRESULT)d;
                } else if (q == 1) {
                    res = 0;
                    HMENU stack[4] = { hm, NULL, NULL, NULL };
                    int depth = 1;
                    while (depth > 0) {
                        HMENU cur = stack[--depth];
                        int cnt = GetMenuItemCount(cur);
                        for (int i = 0; i < cnt; ++i) {
                            wchar_t t[64];
                            if (GetMenuStringW(cur, (UINT)i, t, 64, MF_BYPOSITION) > 0 && wcschr(t, L'\t')) res++;
                            HMENU sub = GetSubMenu(cur, i);
                            if (sub && depth < 4) stack[depth++] = sub;
                        }
                    }
                } else if (q == 2) {
                    UINT st = GetMenuState(hm, qid, MF_BYCOMMAND);
                    res = (st == (UINT)-1) ? -1 : (LRESULT)st;
                } else if (q == 3) {
                    wchar_t t[64];
                    res = (GetMenuStringW(hm, qid, t, 64, MF_BYCOMMAND) > 0) ? (wcschr(t, L'\t') != NULL) : -1;
                }
                DestroyMenu(hm);
                return res;
            }
            //   126  WRITING (phase 47): a new candle, one interval after the
            //        last, is merged as the worker merges a fetch - the next
            //        day at 1d, which a clock cannot be asked for. Returns the
            //        candle count.
            if (wParam == 126) {
                LRESULT cnt;
                EnterCriticalSection(&g_Ctx.lock);
                if (g_Ctx.candleCount > 0) {
                    Candle nc = g_Ctx.candles[g_Ctx.candleCount - 1];
                    nc.openTime += g_Ctx.intervalMs;
                    MergeCandles(&g_Ctx, &nc, 1);
                }
                cnt = g_Ctx.candleCount;
                LeaveCriticalSection(&g_Ctx.lock);
                if (g_Ctx.hPopup) InvalidateRect(g_Ctx.hPopup, NULL, FALSE);
                return cnt;
            }
            //   127  READING (phase 49): the chart type chosen for a mode,
            //        lParam 0 the panel's, 1 the desktop surface's - both
            //        readable in either mode and with the panel hidden, so
            //        the desktop's default and its registry value can be
            //        checked where desktop mode cannot run (the hidden
            //        desktop has no WorkerW).
            if (wParam == 127) return lParam ? g_Ctx.chartTypeDesk : g_Ctx.chartType;
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
        // Display change (phase 26). lParam is not read: until phase 37 the
        // main thread was DPI-unaware and the measurements virtualized, and
        // RefitDesktopSurface measures for itself.
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

        // The main window is destroyed from outside: a WM_CLOSE from
        // taskkill (without /F), an installer or an updater. Up to phase 45
        // only "Quit TickC" removed the icon, and this path left a dead one
        // in the notification area until the pointer passed over it.
        case WM_DESTROY:
            RemoveTrayIcon();
            SaveConfig(&g_Ctx);
            DestroyPanelSurface();   // the panel is not owned: see there
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
                // The flags are set here, not inherited (phase 44): uFlags is
                // whatever the last Shell_NotifyIconW call left, and a re-add
                // without NIF_MESSAGE gave an icon that ignored all clicks.
                g_Ctx.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
                Shell_NotifyIconW(NIM_ADD, &g_Ctx.nid);
                if (g_desktopMode) {
                    if (!g_Ctx.hPopup) {
                        // Explorer is back: fast tries again (phase 46).
                        g_embedFails = 0;
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

// The main instance's mutex and window title (phase 46). "Local\": one per
// sign-in session, as the registry and the tray are. The test build has its
// own names, as it has its own registry key: a test run must never hand over
// to the user's TickC.exe, nor stop it. It also carries a hash of its own
// path, so two test exes (two agents, or a red and a green build) do not
// take each other for the main instance; two runs of the SAME exe do.
static void MainInstanceNames(wchar_t* mutexName, size_t cch) {
#ifdef TICKER_PROBE
    wchar_t exe[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, exe, MAX_PATH), h = 2166136261u;   // FNV-1a
    for (DWORD i = 0; i < len && i < MAX_PATH; ++i) {
        wchar_t c = exe[i];
        h ^= (DWORD)((c >= L'A' && c <= L'Z') ? c + 32 : c);   // paths ignore case
        h *= 16777619u;
    }
    swprintf_s(mutexName, cch, L"Local\\TickerTest.MainInstance.%08X", h);
    swprintf_s(g_mainTitle, 48, L"TickerTest %08X", h);
#else
    wcscpy_s(mutexName, cch, L"Local\\TickC.MainInstance");
    wcscpy_s(g_mainTitle, 48, L"TickC");
#endif
}

// A main instance already runs: bring its panel forward and let this
// process end (phase 46). Its mutex can exist before its window does - both
// starts at once, or a slow first start - so the window is looked for for up
// to 3 s. AllowSetForegroundWindow passes on this start's right to take the
// foreground (the user launched it), without which the panel would open
// behind Explorer.
static void HandOverToMainInstance(void) {
    for (int t = 0; t < 30; ++t) {
        HWND h = FindWindowW(L"BTCTickerWindowClass", g_mainTitle);
        if (h) {
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid) AllowSetForegroundWindow(pid);
            PostMessageW(h, WM_APP_SHOW, 0, 0);
            return;
        }
        Sleep(100);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    // Per-monitor aware (phase 37), before the first window: the panel is
    // drawn in device pixels at its monitor's dpi instead of being stretched
    // as a bitmap by Windows at 150 %. Every length goes through Dp.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // The command line comes first (phase 46): whether this start is a
    // duplicate decides whether it may run beside a main instance, and that
    // is decided before the tray icon or any window exists - a start that
    // hands over must leave nothing behind.
    //   --dup x y w h sym iv   written by SpawnInstance; exactly this form.
    //                          If the check fails, we start as a normal main
    //                          instance instead of guessing.
    //   --desktop-mode         the desktop surface for this run.
    //   --autostart            the Run value's start at sign-in: quiet.
    BOOL argDup = FALSE, argDesktop = FALSE, argAutostart = FALSE;
    int dupV[6] = { 0 };
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv && argc == 8 && wcscmp(argv[1], L"--dup") == 0) {
            for (int i = 0; i < 6; ++i) dupV[i] = _wtoi(argv[2 + i]);
            argDup = dupV[2] >= 240 && dupV[2] <= 8192 && dupV[3] >= 160 && dupV[3] <= 8192 &&
                     dupV[4] >= 0 && dupV[4] < SYMBOL_COUNT &&
                     dupV[5] >= 0 && dupV[5] < INTERVAL_COUNT;
        } else if (argv) {
            for (int i = 1; i < argc; ++i) {
                if (wcscmp(argv[i], L"--desktop-mode") == 0) argDesktop = TRUE;
                else if (wcscmp(argv[i], AUTOSTART_ARG) == 0) argAutostart = TRUE;
            }
        }
        if (argv) LocalFree(argv);
    }

    // One main instance (phase 46). Phase 8 removed the old mutex because it
    // stopped [ + ]; duplicates still start freely - each process has its own
    // worker thread, its own tray icon and its own window classes (classes
    // are per process, so the names do not collide) - but a second plain
    // start stacked another main instance: a second worker, a second icon,
    // two sets of registry writes and, in desktop mode, a second surface.
    // It shows the first one's panel now and ends. A second start at sign-in
    // (--autostart) ends without a word. If the mutex cannot be created at
    // all, we run rather than refuse.
    {
        wchar_t mutexName[64];
        MainInstanceNames(mutexName, 64);
        if (!argDup) {
            SetLastError(ERROR_SUCCESS);   // a success need not clear it (pitfall 103)
            g_mainMutex = CreateMutexW(NULL, FALSE, mutexName);
            if (g_mainMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
                CloseHandle(g_mainMutex);
                if (!argAutostart) HandOverToMainInstance();
                return 0;
            }
        }
    }

    memset(&g_Ctx, 0, sizeof(AppContext));
    g_Ctx.ch.hoverIdx = -1;   // 0 from memset would mean "hover on the first candle"
    g_Ctx.overlayHot = -1; // same reason: 0 would mean "first row highlighted"
    g_Ctx.rangeIdx   = -1; // phase 41: 0 would mean "1D selected"
    g_Ctx.ch.dispValid  = FALSE; // snap on the first frame

    // The proxy (phase 46): AUTOMATIC_PROXY follows the proxy the user has
    // set in Windows, a PAC script included. DEFAULT_PROXY, used up to phase
    // 45, reads only the machine's WinHTTP setting (netsh winhttp), which is
    // empty on nearly every machine, so behind a company or school proxy no
    // request ever got out - and without phase 46's offline state, nothing
    // said so. Windows 8.1 and later; older ones refuse the value and fall
    // back to the old type.
    DWORD access = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
    g_Ctx.hSession = WinHttpOpen(L"TickC/1.0", access,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!g_Ctx.hSession) {
        access = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
        g_Ctx.hSession = WinHttpOpen(L"TickC/1.0", access,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }

    if (!g_Ctx.hSession) return 1;
#ifdef TICKER_PROBE
    g_probeAccessType = access;
#endif

    // Without this the default receive timeout is 30 seconds. Then a hanging
    // connection would hold the worker thread far beyond the wait at exit
    // (10 s from phase 44; see the end of WinMain for what happens then).
    WinHttpSetTimeouts(g_Ctx.hSession, 5000, 5000, 5000, 5000);

    // The chart's fonts, pens and brushes (phase 35: one definition, shared
    // with the golden tests in tests/), the header's price font and the
    // buttons' pens (phase 40: in the theme's colors).
    // 96 dpi until a surface exists: ApplyPanelStyle sets the panel's own dpi
    // when it opens (phase 37), and the theme once the config is read.
    ApplyPanelStyle(&g_Ctx, CHART_DPI_BASE);
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

    // The title is how a second start finds a main instance (phase 46); a
    // duplicate's hidden window gets another one.
    g_Ctx.hWnd =CreateWindowExW(0, wc.lpszClassName, argDup ? L"TickC duplicate" : g_mainTitle,
                                0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    g_Ctx.nid.cbSize           = sizeof(NOTIFYICONDATAW);
    g_Ctx.nid.hWnd             = g_Ctx.hWnd;
    g_Ctx.nid.uID              = 42;
    g_Ctx.nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_Ctx.nid.uCallbackMessage = WM_TRAYICON;
    g_Ctx.nid.hIcon            = RenderMicroFontIcon("...", 0xFF00FF66);
    wcscpy_s(g_Ctx.nid.szTip, 128, L"Connecting to Binance...");

    Shell_NotifyIconW(NIM_ADD, &g_Ctx.nid);

    // Fixed objects: created once, not per repaint (the buttons' pens are
    // built with the style, above)
    g_Ctx.curArrow    = LoadCursorW(NULL, IDC_ARROW);
    g_Ctx.curPan      = LoadCursorW(NULL, IDC_SIZEALL);
    g_Ctx.curHand     = LoadCursorW(NULL, IDC_HAND);
    g_Ctx.btnHot      = -1;
    g_Ctx.btnDown     = -1;     // phase 47: 0 would be a pressed [ + ]
    g_Ctx.tbHot       = -1;
    g_Ctx.alertHot    = -1;     // phase 23: 0 would mean "first alert under the pointer"
    g_Ctx.axisHotY    = -1;
    g_Ctx.showVol     = TRUE;   // phase 22; LoadConfig can turn it off
    g_Ctx.ch.dispVolF    = 1.0;
    g_Ctx.showInd     = TRUE;   // phase 25; LoadConfig can turn it off
    g_Ctx.ch.dispIndF    = 1.0;

    // The worker thread is started only when the window and the icon exist,
    // since it posts messages to hWnd right away.
    // MUST come before CreateThread: the first fetch must go to the right
    // pair, and the watermark must be correct from the first frame.
    MigrateLegacyNames();   // phase 30: before the first read from the registry
    if (!argDup) UpgradeAutostart();   // phase 46
    LoadConfig(&g_Ctx, &g_savedPanelX, &g_savedPanelY,
               &g_savedPanelW, &g_savedPanelH, &g_savedPanelDpi);

    // Duplicate: "--dup x y w h sym iv", read and checked at the top.
    // Overrides what LoadConfig read, with the same limits - a hand-written
    // command line must not be able to index outside the tables. Desktop
    // mode cannot be combined with --dup: a duplicate is started from [ + ],
    // which does not exist there.
    if (argDup) {
        g_isDuplicate    = TRUE;
        g_savedPanelX    = dupV[0];
        g_savedPanelY    = dupV[1];
        g_savedPanelW    = dupV[2];
        g_savedPanelH    = dupV[3];
        g_savedPanelDpi  = 0;   // phase 48: a live panel's size, on this monitor
        g_Ctx.symIdx     = dupV[4];
        g_Ctx.ivIdx      = dupV[5];
        g_Ctx.intervalMs = INTERVALS[dupV[5]].ms;
    } else if (argDesktop) {
        g_desktopMode = TRUE;
    }
    // Without --desktop-mode the registry decides: the tray menu remembers
    // the last chosen mode (phase 12). The flag wins for this run, and a
    // duplicate is always a panel.
    if (!g_desktopMode && !g_isDuplicate) g_desktopMode = LoadDesktopMode();
    // No animation at startup (phases 22 and 25). Here, and not right after
    // LoadConfig: which choice applies depends on the mode (phase 26).
    g_Ctx.ch.dispVolF = ShowVolNow(&g_Ctx) ? 1.0 : 0.0;
    g_Ctx.ch.dispIndF = ShowIndNow(&g_Ctx) ? 1.0 : 0.0;
    g_Ctx.ch.dispRsiF = ShowRsiNow(&g_Ctx) ? 1.0 : 0.0;
    // The chart type (phase 49), for the same reason: the first frame
    // draws and scales with it. A duplicate reads the panel's from the
    // registry here, as it reads the theme and the overlays - SaveConfig
    // writes every change at once, so that is the type of the panel that
    // [ + ] was clicked on, unless that panel was itself a duplicate whose
    // own changes are never saved.
    g_Ctx.ch.chartType = ChartTypeNow(&g_Ctx);
    // The price alerts (phase 23). After the --dup parsing: LoadAlerts skips
    // duplicates, and g_isDuplicate is known only here. Before the thread:
    // the first price must be checked against the alerts from the last run.
    LoadAlerts(&g_Ctx);
#ifdef TICKER_PROBE
    // An alert from the registry can fire on the FIRST price, before a probe
    // has time to send 101. The probe therefore sets the variable in its own
    // environment, and the test build inherits it.
    g_probeMute = GetEnvironmentVariableW(L"TICKER_PROBE_MUTE", NULL, 0) > 0;
    {
        wchar_t fd[16];
        if (GetEnvironmentVariableW(L"TICKER_FORCE_DPI", fd, 16) > 0) {
            int v = _wtoi(fd);
            if (v >= 48 && v <= 480) g_forceDpi = v;
        }
    }
    // Phase 48: the fake monitor and the fake taskbar (see g_fakeMonX0).
    // Comma-separated integers; anything malformed leaves the hook off.
    {
        wchar_t fv[64];
        if (GetEnvironmentVariableW(L"TICKER_FAKE_MON", fv, 64) > 0) {
            wchar_t* e = fv;
            int x0 = (int)wcstol(e, &e, 10);
            int dpi = (*e == L',') ? (int)wcstol(e + 1, &e, 10) : 0;
            int quiet = (*e == L',') ? (int)wcstol(e + 1, &e, 10) : 0;
            if (dpi >= 48 && dpi <= 480) {
                g_fakeMonX0 = x0; g_fakeMonDpi = dpi; g_fakeMonQuiet = (quiet != 0);
            }
        }
        if (GetEnvironmentVariableW(L"TICKER_FAKE_WORKAREA", fv, 64) > 0) {
            wchar_t* e = fv;
            int dx = (int)wcstol(e, &e, 10);
            int dy = (*e == L',') ? (int)wcstol(e + 1, &e, 10) : 0;
            if (dx >= 0 && dx <= 400 && dy >= 0 && dy <= 400) { g_fakeWorkDx = dx; g_fakeWorkDy = dy; }
        }
    }
    // Recorded responses (phase 34); see g_fixtureDir. Read before the thread
    // starts, which is the only reader.
    // GetLastError is not cleared by a call that succeeds, and the
    // TICKER_FORCE_DPI lookup above leaves ERROR_ENVVAR_NOT_FOUND behind when
    // it is unset (phase 37: the fixtures were silently dropped).
    SetLastError(ERROR_SUCCESS);
    if (GetEnvironmentVariableW(L"TICKER_FIXTURE_DIR", g_fixtureDir, MAX_PATH) == 0 ||
        GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
        g_fixtureDir[0] = L'\0';
    }
#endif

    InitializeCriticalSection(&g_Ctx.lock);
    g_Ctx.hStopEvent = CreateEventW(NULL, TRUE,  FALSE, NULL);  // manual reset
    g_Ctx.hWakeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);  // auto reset
    g_Ctx.hThread    = CreateThread(NULL, 0, NetworkThread, &g_Ctx, 0, NULL);

    // A duplicate is started from a click and must show itself at once. So,
    // from phase 46, is a main instance started by hand: up to phase 45 it
    // started in the notification area alone - often in the overflow, where
    // nothing showed that the start had worked, so users started it again.
    // The start at sign-in (--autostart, the Run value) stays in the
    // notification area, as every start did before. After the lock and the
    // events: TogglePopup enters the lock and wakes the thread. The desktop
    // surface is shown at every start - it exists only while it is shown.
    if (g_isDuplicate || g_desktopMode || !argAutostart) TogglePopup(&g_Ctx, hInstance);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // The main instance's name is let go here, not at exit (phase 46): the
    // wait for the worker below can take seconds, and a start in that time
    // would otherwise hand over to a window whose loop has ended - and
    // nothing would open. It becomes the main instance instead. The title
    // goes too, so no start finds this window again.
    if (g_mainMutex) {
        SetWindowTextW(g_Ctx.hWnd, L"");
        CloseHandle(g_mainMutex);
        g_mainMutex = NULL;
    }

    // Stop the thread before we tear down anything it can touch.
    //
    // Phase 44: up to phase 43 the wait was 3 s and its result ignored, and
    // the events, the lock and both WinHTTP handles were closed under a
    // worker that could still be inside a request (each WinHTTP phase may
    // take 5 s). Now: the stop event first (HttpGet starts no new request),
    // then the session is taken out under the lock and closed, which is
    // meant to cut short a request in flight on its children - HttpGet reads
    // hSession under the same lock, so it never connects on a closed one.
    // hConnect is the worker's and is not touched here. Then up to 10 s, and
    // the lock, the events and hConnect are released only if the thread has
    // really ended. Otherwise they are left to the process exit, which is
    // moments away: a leaked handle beats a lock deleted under a live thread.
    BOOL workerDone = (g_Ctx.hThread == NULL);
    if (g_Ctx.hStopEvent) SetEvent(g_Ctx.hStopEvent);
    {
        HINTERNET hs;
        EnterCriticalSection(&g_Ctx.lock);
        hs = g_Ctx.hSession;
        g_Ctx.hSession = NULL;
        LeaveCriticalSection(&g_Ctx.lock);
        if (hs) WinHttpCloseHandle(hs);
    }
    if (g_Ctx.hThread) {
        workerDone = (WaitForSingleObject(g_Ctx.hThread, 10000) == WAIT_OBJECT_0);
        CloseHandle(g_Ctx.hThread);
    }
    if (workerDone) {
        if (g_Ctx.hStopEvent) CloseHandle(g_Ctx.hStopEvent);
        if (g_Ctx.hWakeEvent) CloseHandle(g_Ctx.hWakeEvent);
        DeleteCriticalSection(&g_Ctx.lock);
    }

    // The buffer first: fonts, pens and brushes from the last frame can be
    // selected into the DC, and DeleteObject on a selected object fails
    // silently.
    FreeBackBuffer(&g_Ctx);

    if (g_Ctx.nid.hIcon) DestroyIcon(g_Ctx.nid.hIcon);
    if (g_Ctx.hFontPill) DeleteObject(g_Ctx.hFontPill);
    if (g_Ctx.hFontQuote) DeleteObject(g_Ctx.hFontQuote);
    ChartStyleDestroy(&g_Ctx.sty);

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

    // hConnect belongs to the worker thread: released only once it has ended
    // (phase 44). The session was closed before the wait.
    if (workerDone && g_Ctx.hConnect) WinHttpCloseHandle(g_Ctx.hConnect);

    return (int)msg.wParam;
}
