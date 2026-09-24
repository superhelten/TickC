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
// when there is no band - both at the bottom as it was before the volume
// pane was cut out, so "no band" is bandBottom == bandTop, not a test
// against bottom (phase 45). bottom and ch stay the PRICE pane's,
// so every price <-> y function reads them as before; the time axis sits
// under the lowest pane.
// volTop/volBottom (phase 43) are the volume pane, between the price pane and
// the RSI band; equal (both = bottom) when the volume has no pane of its own.
// The lowest pane's bottom is ChartPanesBottom - the hit test, the crosshair
// and the time axis all read it, so they cannot disagree.
typedef struct { int left, top, right, bottom, cw, ch, edge, dpi, bandTop, bandBottom, volTop, volBottom; } ChartRect;

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
    // Phase 49: how the price is drawn, a CHART_* value (see below). Here and
    // not in ChartData: the app fills ChartData field by field without
    // zeroing it, while its ChartState is static - so 0, the candles, is
    // what every caller that does not know the field gets. The autoscale
    // reads it too (SyncDisp, PriceRangeFor): the display and the app's
    // easing target must scale on the same prices.
    int  chartType;
#ifdef TICKER_PROBE
    // Test build only: what the last ChartDrawBody measured. 45: the session
    // blocks (us), 56: yesterday (us), 39: the averages (us), 57: bitmask of
    // level labels drawn, 58: crosshair tag drawn, 59: the label block (us).
    // Phase 51: bit 0 the view's high label drawn, bit 1 the low's.
    LONGLONG probeSessUs, probePrevUs, probeIndUs, probeLblUs;
    int      probeLblMask, probeCrossTag, probeHiLoMask;
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
    // The volume is on (phase 43): the CHOICE, which decides the region -
    // a pane of its own, or the bars behind the candles when there is no room
    // for one. The bars rise with ChartState.dispVolF; turned off, the
    // region goes at once, as the RSI band's does.
    BOOL          vol;
} ChartData;

// The chart's colors (phase 38). Every color the engine draws with comes from
// here, not from the CLR_ macros: the macros below are the default dark theme
// (ChartThemeDark), which TickC uses, and a second theme is a table, not an
// edit of chart.c. The pens and brushes in ChartStyle are built from it.
typedef struct {
    COLORREF bg, grid, up, down, text, dim, cross, box, boxEdge;
    COLORREF hot, onHot;         // a tag the pointer is on: surface and text
    COLORREF axis, volUp, volDown;
    // Phase 45: the bars in the volume pane. volUp/volDown are muted to stand
    // behind the candles, and still do in the fallback without a pane; in the
    // pane nothing stands in front of them, so they are stronger.
    COLORREF volPaneUp, volPaneDown;
    COLORREF sma, ema, vwap, session, prev;
    COLORREF alert, alertLine;
    COLORREF rsi;                // phase 39
    // Phase 40: the alert as text. onAlert is the number on the filled
    // alert tag, alertText the ghost tag's frame and number on the box. A
    // color cannot both carry light text at 4.5:1 and be text at 4.5:1 on a
    // light background (the luminance gap is at about 0.175), so the light
    // theme keeps its amber surface and needs both; the dark theme uses
    // CLR_BG and CLR_ALERT, as before.
    COLORREF onAlert, alertText;
    // Phase 42: the header's quote line and range field, drawn by the app,
    // kept here so the one palette - and its contrast check - covers them.
    // quote is the values' amber (Bloomberg's data color), accent the
    // selected cell, onAccent its text.
    COLORREF quote, accent, onAccent;
    // Phase 49: the line and mountain chart types. line is the close line,
    // mountain the fill under it (phase 51: Bloomberg's navy, see CLR_LINE).
    // Last in the struct, so the tables' positional initializers keep their
    // order.
    COLORREF line, mountain;
    // Phase 51: the Bloomberg GIP chart area. gridDot is the dotted grid
    // (grid stays the solid rules: the lines between panes, and the
    // desktop's grid); axisLine the price column's axis line and its ticks;
    // stamp and onStamp the last-price stamp of the line and the mountain,
    // which takes the series' color on Bloomberg, where the candles and the
    // bars keep up/down with bg on it.
    COLORREF gridDot, axisLine, stamp, onStamp;
    // fillCell: the cell a level name or the averages' legend stands on
    // where the mountain's fill covers it. The dark theme's navy carries
    // every such text at 4.5:1 (the cell is the fill itself, and only cuts
    // the lines through the text); the light fill does not, so there it is
    // the background, as in phase 49.
    COLORREF fillCell;
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
    HBRUSH brVolPaneUp, brVolPaneDown;   // phase 45
    // Phase 49: the price line of the line and mountain types, clr.line and
    // ChartPx(dpi, 1) wide - the one line the engine scales with the dpi
    // (see below), so it is a pen of its own and not DC_PEN. On a desktop
    // surface 1280 px or higher the frame draws the line with a wider pen
    // of its own instead (DeskLineW).
    HPEN   penLine;
    int    dpi;               // phase 36: the fonts are built for it; 96 = 100 %
    ChartTheme clr;             // phase 38: the colors, copied from the theme
} ChartStyle;

// DPI (phase 36). Every length in this header is given at 96 dpi and is
// scaled with ChartPx when drawn: MulDiv rounds to nearest, and at 96 it is
// the identity, so a 100 % surface keeps exactly its old pixels. Lines stay
// 1 device pixel wide at every dpi - a styled GDI pen (PS_DOT, PS_DASH) only
// keeps its pattern at width 1. Phase 49: except the price itself when it is
// drawn as a line or as OHLC bars - there it is the only mark, with no body
// to carry it, and 1 px at 200 % is a hairline. ChartPx(dpi, 1) wide.
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
// max(TIME_DX_MIN, label width + TIME_LBL_GAP), the width the widest label
// the axis can show (ChartTimeLabelW). Until phase 45 1h and 4h wrote
// "MM-DD HH:MM", 99 px in the axis font; now every intraday label is "HH:MM"
// or, where a day begins, "21 Sep" - 54 px, so 80 rules.
#define TIME_DX_MIN      80
#define TIME_LBL_GAP     12
// The hover box's width. Its time row is "21 Sep 14:35" on intraday
// intervals (phase 45); tests/chart_golden.c measures that it fits.
#define HOVER_BOX_W      104

// Palette. The up green is the tray icon's; the icon keeps its own colors.
// Phase 51: the background is black, Bloomberg's (it was 0D1117). Every
// color that was a blend toward the old background is derived again from
// black below, and moved one step off the exact blend where a text color is
// the other end (pitfall 87: on black every darker copy of a text color IS
// on its blend line, which anti-aliased text is made of).
#define CLR_BG           RGB(0x00, 0x00, 0x00)
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
// Phase 43: the bars have a pane of their own under the price, on the
// Bloomberg model - VOL_PANE_FRAC of the chart height, at least VOL_PANE_MIN,
// the tallest bar VOL_PANE_PAD under the pane's top line. The RSI band is
// placed first (it is opted into and has nowhere else to go); the volume
// gets its pane only when the price keeps PRICE_PANE_MIN, and otherwise
// stands behind the candles as before, in VOL_FRAC of the price pane. Only
// the smallest panels with RSI on do that: 400x250 keeps 142 px of price
// with one pane and would get 96 with two.
#define VOL_FRAC         0.22
#define VOL_PANE_FRAC    0.20
#define VOL_PANE_MIN     40
#define VOL_PANE_PAD     3
// Phase 51: 72/255 of the way from black to up and down, as the old values
// were from 0D1117, with red (up) and blue (down) one step off the blend.
#define CLR_VOL_UP       RGB(0x01, 0x48, 0x1C)
#define CLR_VOL_DOWN     RGB(0x48, 0x14, 0x1D)
// Phase 45: the bars in the pane. The colors above were made to stand BEHIND
// the candles; in the pane nothing stands in front of them, and at 28 % they
// read as a shadow. About 60 % of the way from CLR_BG to the candle colors:
// direction at a glance, still a step under the candles, which stay the
// brightest thing on the surface. Not exact blends in every channel (pitfall
// 87: up and down are also text colors). Phase 51: on black they are about
// 63 % of the way, still off the blend lines, and are kept.
#define CLR_VOL_PANE_UP   RGB(0x05, 0xA0, 0x46)
#define CLR_VOL_PANE_DOWN RGB(0x9E, 0x33, 0x46)
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
// left out when the price pane would get less than PRICE_PANE_MIN - a 400x250
// panel keeps 142 px of price. Teal: not green (up), not a blue or violet of
// the averages, not a gold or amber of VWAP and the alerts.
// PANE_GAP is the space above every pane under the price (phase 43: the
// volume pane's too).
#define RSI_PERIOD       14
#define RSI_HI           70
#define RSI_LO           30
#define RSI_BAND_FRAC    0.20
#define RSI_BAND_MIN     40
#define PRICE_PANE_MIN   120
#define PANE_GAP         6
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
// Phase 51: a step lighter (6F7B95 until then), so the names PDC, PDH and
// PDL reach 4.5:1 on the mountain's navy (4.76) and the tags on the box
// (4.58, from 3.99); still darker and cooler than CLR_SESSION.
#define CLR_PREV         RGB(0x79, 0x85, 0xA0)
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
// the saturated color. Phase 51: halfway from black, red one step up, off
// the alert's own blend line (805810 is on it).
#define CLR_ALERT          RGB(0xFF, 0xB0, 0x20)
#define CLR_ALERT_LINE     RGB(0x81, 0x58, 0x10)

// The header (phase 42), on the Bloomberg terminal's model: values in amber,
// the selected range in blue with white text. The amber is Bloomberg's
// orange-amber, deeper than the alert tag's FFB020, and it stays in the
// header while the alert tags stay in the price column.
#define CLR_QUOTE          RGB(0xFB, 0x8B, 0x1E)
#define CLR_ACCENT         RGB(0x2F, 0x5D, 0xA8)

// Chart types (phase 49), on the Bloomberg GP model: ChartState.chartType.
// Candles are 0, so a caller that does not set the field draws what it drew
// before. OHLC bars are the high-low line with the open ticked to the left
// and the close to the right, in the candle's up/down color; the line is the
// close; the mountain is the same line with a fill under it to the price
// pane's bottom. Values outside the range draw candles. The prefix is CHART_
// like the rest of the header (CT_ is winnls.h's, pitfall 67).
#define CHART_CANDLES      0
#define CHART_OHLC         1
#define CHART_LINE         2
#define CHART_MOUNTAIN     3
#define CHART_TYPE_COUNT   4
// The line: a cool near-white, the strongest mark on the surface and one
// hue no other curve has (phase 25's rule) - green/red are the candles and
// the stamp, amber the header's values and the alerts, gold VWAP, blue and
// violet the averages, teal RSI. Not CLR_TEXT, the header's price: the line
// is not text, and a probe must be able to count it apart from text
// (pitfall 87; checked per channel against every text role and the
// watermark's white).
// Phase 51, Bloomberg's GIP chart: a white line over a dark navy fill. The
// navy is sampled from the user's screenshot of the terminal (011A31, the
// fill's most common pixel); the white keeps a cool tint so it is not on the
// gray ramp from black to the watermark's white. The fill is no longer a
// blend of the line; neither is on a text role's blend line.
#define CLR_LINE           RGB(0xF4, 0xF6, 0xF9)
#define CLR_MOUNTAIN       RGB(0x01, 0x1A, 0x31)
// The grid is dotted in both directions (phase 51): one pixel in
// GRID_DOT_PERIOD, horizontally at the price labels and vertically at the
// time labels, anchored at the chart's left and top edges so the dots stand
// still while the view pans. The period is in device pixels, as GDI's own
// PS_DOT is: a dot pattern is a line style, and lines stay one device pixel
// at every dpi. Brighter than CLR_GRID, which stays the solid rules between
// panes, because a quarter of the pixels carry it; darker than the
// crosshair, whose dotted line follows the hand and is the one to read. The
// crosshair's PS_DOT is 3 on 3 off in a lighter gray, yesterday's high and
// low 2 on 10 off in CLR_PREV - the three dotted lines stay apart.
// The price column is an axis (phase 51): a line on its edge down each pane
// and a tick at each price label, in CLR_AXIS_LINE, dimmer than the labels.
#define GRID_DOT_PERIOD    4
#define GRID_TICK_LEN      3
#define CLR_GRID_DOT       RGB(0x47, 0x4E, 0x5B)
#define CLR_AXIS_LINE      RGB(0x73, 0x7D, 0x8C)
// The last-price stamp of the line and the mountain (phase 51): the series'
// white with a black number, as the terminal draws it. The candles and the
// bars keep the up/down stamp, and so does every type on the desktop.
#define CLR_STAMP          RGB(0xFF, 0xFF, 0xFF)
#define CLR_ON_STAMP       RGB(0x00, 0x00, 0x00)

// Batches for PolyPolygon / Polyline; see chart.c.
#define VOL_BATCH 256
#define IND_BATCH (VOL_BATCH * 4)

// --- Geometry and hit testing ---
ChartRect ChartGeometry(int W, int H, BOOL desktop, int dpi, BOOL band, BOOL vol);
int       ChartPanesBottom(const ChartRect* g);
int       ChartVolBarsH(const ChartRect* g);
int       ChartAxisW(int dpi);
int       DeskPillH(int H);
int       DeskPillFontH(int H);
int       DeskAxisW(int H, int dpi);
int       DeskLineW(int H);   // phase 49: the price line's width on the desktop
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
// Phase 49: the price axis for a chart type. The line and the mountain scale
// on the closes they plot, the candles and the bars on high and low.
// PriceRange is the candles' (type 0). The app's easing target must call this
// with the same ChartState.chartType the frame is drawn with.
void      PriceRangeFor(const Candle* candles, int vs, int vc, int chartType, double* outMin, double* outMax);
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
// A moment as a clock (phase 45): "HH:MM" local under 1 day, the UTC date
// "YYYY-MM-DD" from 1 day up. The chart's own labels go through the same
// helper in chart.c, with the date where a day begins.
void      FormatCandleTime(long long unixMs, long long intervalMs, long long utcOffsetMs, wchar_t* out, size_t cch);
// The widest time-axis label for the interval in the font selected into hdc
// (phase 45): the spacing of the labels is measured on it.
int       ChartTimeLabelW(HDC hdc, long long intervalMs);
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
