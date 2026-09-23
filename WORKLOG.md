# TickC — work log

Status as of 2026-09-22. Written for agents who continue work on `tickc.c`
(`ticker.c` up to and including phase 29).
Phase 1 is done. Phase 2 part A (animation timer, backoff, stale indicator),
part B (symbol/interval, overlay, watermark, the registry) and part C (last-price
indicator, scaled watermark, view and Y-axis easing) are done. **All of phase 2
is delivered.** After that the panel was moved from a borderless popup to a regular
OS window, and then, in **phase 4**, back to borderless — this time with its own
control buttons and native `HTCAPTION` moving. **Phase 5** added a
state-dependent restore glyph, incremental hover repaint and a
four-way cursor during panning. **Phase 6** tightened the clipping of the chart to
`rcChart` and moved the restore glyph to a 2 px offset. **Phase 7**
measures the header text and raises the minimum size to 400×250. **Phase 8** swaps
`[ ↺ ]` for `[ + ]`, which starts a new instance, moves resetting zoom and
panning to double-click, `R` and `ESC`, and removes the single-instance mutex.
**Phase 9** adds `--desktop-mode`, where the chart area sits in the desktop
behind the icons as a child of WorkerW.
**Phase 10** lets the double buffer live between frames: 5.1 ms vs 11.7 ms at
3840×1600, and 1.33 ms vs 1.59 ms at 1280×720, with the same pixels.
**Phase 11** splits the surface into chart, price column and time band. It adds a
time axis, its own monospace axis font (11 px digits) in #A0AAB8 and a watermark with
alpha that follows the window width.
**Focus flash** removes the classic frame that `DefWindowProc` drew on top of
the chart on every focus change and title change. **Phase 12** switches between panel and
desktop mode from the tray menu while the process runs, and remembers the choice in
the registry. **Phase 13** puts "Start at sign-in" in the tray menu, with
`Ticker` in `HKCU\…\Run`. **Phase 14** makes the desktop surface text-free and edge
to edge: only the curve, grid and watermark. **Phase 15** gives the candles 10 px of room
toward the price axis and lets the dashed last-price line bridge it.
**Phase 16** puts one scaled price stamp on the desktop's right edge —
the surface's only text. **Phase 17** gives the tray menu the submenus "Symbol" and
"Interval", so desktop mode can switch without going through the panel.
**Phase 18** fetches older candles when the user pans into the wall: the buffer
is filled backward to the start of the history or to 6000 candles, without the frame
moving.
**Phase 19** gives the control buttons keyboard shortcuts — `Ctrl`+`N` for `[ + ]`,
`Ctrl`+`M`, `F11` and `Ctrl`+`W` — through the same path as the clicks, and
resets `staleSecsShown` when the connection is back.
**Phase 20** makes the chart keyboard-driven — arrow keys, `PgUp`/`PgDn`,
`Home`/`End` and `+`/`-` through the same `PanView`/`ZoomView` as the wheel — and
releases the panning when another window takes capture (`WM_CAPTURECHANGED`).
**Phase 21** puts volume bars in the bottom 22 % of the chart area, behind
the candles, with a scale that eases like the price axis, and a `V` row in the hover box.
**Phase 22** turns row 2 of the header into a toolbar: a symbol pill (opens
the overlay), one pill per interval and a `VOL` toggle that eases, with `V` and
`1`…`6` from the keyboard and "Volume bars" in the tray menu.
**Phase 23** adds price alerts: a click in the price column sets an amber line at
that price, a click on the tag removes it, and when the price reaches the level the
alert fires once with afterglow, balloon and sound — also with the panel closed.
The test build got **writable** probe fields that inject a price.
**Phase 24** makes the input sane (the parsers reject nan, inf, 0 and insane
candles) and wakes the thread after sleep.
**Phase 25** puts moving averages over the candles — SMA 20 and EMA 50, computed by
a step machine without its own table — with a legend in the chart's corner and a
`MA` toggle in the toolbar, on `M` and in the tray menu.
**Phase 26** gives the desktop surface its own overlay choices — volume and averages are off
there by default, after feedback from use — and re-places the surface
on `WM_DISPLAYCHANGE`.
**Phase 27** ("Bloomberg Essentials") puts today's session over the candles: VWAP
per UTC day as a golden line, today's high and low as dashed lines with
muted axis tags, and SMA, EMA and VWAP as rows in the hover box — all computed
from `candles[]` during painting and all behind the indicator toggle, so
the desktop is untouched. The buffer backfills itself to the day rollover.
**Phase 28** adds yesterday's levels alongside: the previous UTC day's high, low
and close as three cooler, dashed lines with muted axis tags, behind
the same toggle — and the backfill now goes to the *previous* day rollover.
**Phase 29** makes the levels readable: labels (HOD, LOD, PDC, PDH, PDL) at
the lines' left end, and the crosshair's axis tag gets a place in the column's rank,
so it no longer cuts the number below it.
**Phase 30** renames the app to **TickC** ahead of the open-source release:
`HKCU\Software\TickC`, Run value `TickC`, `TickC.exe`, `tickc.c` and
`tickc.manifest`, with settings and autostart migrated on first start. From
phase 30 on, new text in this repo is written in English (see the phase 30
section).
**Phase 31** puts every string the user sees into English (tray menu,
status text, the alert balloon, `1h`/`4h`, ISO dates).
**Phase 32** translates every comment in `tickc.c`, proven comment-only:
the production exe is byte-identical apart from the link timestamp.
**Phase 33** translates this work log and renames it from `ARBEIDSLOGG.md`.
**Phase 34** moves the chart engine into `chart.c` / `chart.h`, pixel-identical,
and gives the test build recorded responses and golden captures.
See **The window** below. Design spec for phase 2:
`docs/specs/2026-09-16-phase2-design.md`.

The code is **two files** from phase 34: `tickc.c` (the app, ~5000 lines) and
`chart.c` behind `chart.h` (the chart engine, ~1650 lines). Next to them is
`tickc.manifest`, which the build embeds (phase 9). No external dependencies
beyond Win32 and WinHTTP.

---

## Build

```
"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
cl /nologo /W4 /O2 tickc.c chart.c /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:tickc.manifest /OUT:TickC.exe
```

**The manifest is not optional** (phase 9). Without `supportedOS` Windows 8+ the
desktop mode surface becomes invisible. The build still succeeds, and panel mode looks
the same, so the bug is not noticed until `--desktop-mode` is run.

Builds **clean on `/W4`** — keep it that way. The target architecture is **x86** (matches
the original exe). The process can run as several instances (phase 8), so
**stop all running instances before linking** — including duplicates started with
`[ + ]` — otherwise it fails with
`LNK1104: cannot open file 'TickC.exe'`.

Footprint: ~3.5 MB private bytes, 203 KB exe after phase 34 (207 360 bytes;
206 336 after phase 30,
205 824 after phase 29, 204 800 after phase 28,
203 776 after phase 27,
199 680 after phase 26, 199 168 after phase 25, 195 584 after phase 24;
195 072 after phase 23, 187 KB after phase 22, ~164 KB in phase 9). (The panel is 1280×720 now, vs
380×300 in phase 1 — the double buffer is 8× larger.)

**`tickc.c` is also valid C++** (measured in phase 24): `cl /TP /W4 /O2` gives
0 errors, 0 warnings and an exe of byte-identical size up to and including phase 29.
Phase 30: the `/TP` build is 206 848 bytes, 512 more than the C build. The file is still built
as C — a language switch alone gains nothing — but the door is open the day a
function actually needs a container. See *Rejected proposals* for what the STL and
nlohmann/json cost.

---

## What the app does

A tray icon that shows the BTC/USDT price as pixel-drawn digits. Left-click
opens a borderless candlestick panel with crosshair, zoom and panning.
Right-click gives "Quit".

---

## Architecture

### Threads

| Thread | Responsibility |
|---|---|
| UI | All windows, all drawing, all input. **Never touches the network.** |
| `NetworkThread` | Owns the WinHTTP handles. Fetches, parses, merges. |

The worker thread runs in `WaitForMultipleObjects(hStopEvent, hWakeEvent, 3000)`.
It fetches **without the lock**, then takes the `CRITICAL_SECTION` only around the merge, and
does `PostMessage(WM_APP_DATA)` **after** the lock is released.

> If you add something here: `PostMessage` must never happen inside the lock. The UI thread
> can be sitting waiting for the same lock in `WM_PAINT`.

**The lock covers:** `candles[]`, `candleCount`, `viewStart`, `viewCount`,
`followLive`, `lastPrice`, `hPopup`. Everything else in `AppContext` is UI-owned.

### Data layer

- `candles[6000]` — fixed static array, 288 KB (48 bytes per candle with volume,
  phase 21; 240 KB up to and including phase 20). Filled forward while the panel
  is open and backward on request (phase 18). **No malloc anywhere.**
- The first fetch seeds 300 candles (`limit=300`, ~50 KB). After that `limit=3` (~500 bytes).
- `MergeCandles()` merges on `openTime`: the same timestamp **updates**
  (the candle being formed changes), newer ones **are appended**, the oldest drop out at the cap.
- If a time gap (> 2 candle intervals) is detected the buffer is reset, so the chart
  does not draw a continuous curve across dead time.
- **Everything from the network goes through a sanity check** (phase 24): `PriceSane`
  (`0 < v < 1e15`, false for NaN) and `CandleSane` (OHLC sane, `high ≥ low`,
  open and close within, volume `≥ 0`, `openTime > 0`). `ParseKlines` skips
  insane candles and candles whose time is not strictly increasing, and keeps
  the rest. `candles[]` and `lastPrice` therefore never hold NaN, inf or 0 from
  a response.

### Drawing

Double-buffered through a memory DC, `WM_ERASEBKGND` returns 1.
Both drawing loops run over **`vc` (visible candles)**, not the whole history —
the drawing work is therefore independent of how much history is stored.

GDI objects are cached: eleven fixed colors are created at startup (nine up to and
including phase 20, plus the two bar brushes from phase 21), the blended
fade colors only when `(chrome, closeHot)` changes.

### The window

The panel is a **borderless window with its own control buttons**:
`WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX`. The whole
non-client frame is removed in `WM_NCCALCSIZE`, so the client area is
exactly as large as the window rectangle — measured 1280×720 vs 1280×720.
A button in the taskbar, not always on top.

> **History, two rounds.** Phase 1 was borderless with a hover chrome that
> faded in. `7c38b6d` tore it all out and moved to `WS_OVERLAPPEDWINDOW` with an
> OS title bar. This round goes back to borderless, but *not* to the
> phase 1 design: the buttons are always visible, without fade, and without auto-hide
> on focus loss. See "Removed in phase 3".

#### Everything hangs on `WM_NCHITTEST`

Without an OS frame it is we who decide what the mouse is over. This is the key
idea — buttons, moving and resizing all fall out of one function:

| Zone | Return | Who acts |
|---|---|---|
| < 6 px from an edge (`RESIZE_BORDER`) | `HTLEFT` … `HTBOTTOMRIGHT` | `DefWindowProc` resizes |
| Button box in the header | `HTCLIENT` | we do, in `WM_LBUTTONDOWN` |
| Elsewhere in the header (`y < 44`) | `HTCAPTION` | `DefWindowProc` moves |
| The rest | `HTCLIENT` | we do |

**`HTCAPTION` is the whole moving mechanism.** `DefWindowProc` generates
`WM_NCLBUTTONDOWN`/`HTCAPTION` itself and runs the OS's drag loop — we write no
drag code, and get Aero Snap, double-click maximize and `Win`+arrow key for free.
All three are measured.

> **The order inside the header is not cosmetic.** An `HTCAPTION` area
> *never* gets `WM_LBUTTONDOWN`. If we return `HTCAPTION` for the whole header, the
> buttons are drawn, but dead — and a click on the close cross starts a
> window move. See pitfall 21.

The edges do **not** resize when the window is maximized: there a click 2 px
from the screen edge would start a drag-resize of something that by definition fills
the screen.

#### Maximizing must be specified manually

A `WS_POPUP` window maximizes to the whole **screen**, not to
the work area — and the OS adds the frame width outside. Measured before `WM_GETMINMAXINFO`
got the limits: **−7,−7 3854×1614** vs `rcWork` **0,0 3840×1552**. The panel
covered the taskbar by 55 px, and the close cross was 7 px outside the screen edge.

`WM_GETMINMAXINFO` therefore sets `ptMaxPosition` and `ptMaxSize` from `rcWork`
itself. `ptMaxPosition` is relative to the *monitor's* corner, not to the desktop.
Afterward a maximized window hits `rcWork` exactly.

> **Two branches are unverified, both because the machine has one monitor.** If you have
> more, these two checks take a minute together:
>
> 1. **`ptMaxPosition` on a secondary monitor.** `rcWork` starts at `0,0` here, so
>    the subtraction `rcWork − rcMonitor` was zero and was never put to the test.
>    Drag the panel to the other monitor and press `□`. Expected: that
>    monitor's `rcWork` exactly.
> 2. **The guard against a disconnected monitor.** If the restored rect falls outside
>    all connected monitors, the `❐` button should center the window instead of
>    restoring it into nowhere. Maximize on the other monitor, disconnect it,
>    and press `❐`. Expected: 1280×720 centered on the one that remains.

> `ptMaxTrackSize` is **not** set. It would clamp *manual* resizing to one
> monitor's work area, so the panel could no longer be stretched across two
> monitors. It is the maximized size that should follow `rcWork`, not the largest
> allowed size.

`DWMWA_WINDOW_CORNER_PREFERENCE = DONOTROUND`: Windows 11 rounds the corners of
`WS_THICKFRAME` windows even when the frame is removed, and the radius clips the close cross.

#### The control buttons

Four buttons at the top right, 26×18 px each, 2 px apart, 8 px from the right
edge. `ButtonLayout(W, out[4])` is the **single source of truth** — drawing,
`WM_NCHITTEST`, hover and click all read it (pitfall 14).

| Button | Action |
|---|---|
| `+` | `SpawnInstance` — a new process with the same symbol, interval and size, +30, +30 px (phase 8) |
| `–` | `ShowWindow(SW_MINIMIZE)` |
| `□` / `❑` | `SW_MAXIMIZE` / `SW_RESTORE` according to `IsZoomed`. **The glyph follows the state:** a maximized window shows two overlapping rectangles |
| `×` | `WM_CLOSE` → hides to the notification area |

**The restore glyph is drawn as two rectangles, not four strokes.** The
back one is an *open* polyline with five points — only the edges that are not
behind the front one — so we avoid filling the front one opaque to
hide the overlap. Two GDI calls vs one for `□`.

`zoomed` is passed into `DrawButtons` and is **not** cached in `AppContext`:
the OS owns the state, and a copy would be one more thing that can get out
of sync. `WM_SIZE` invalidates the whole window on maximize, so the glyph switches
by itself.

**Restoring guards the geometry.** After `SW_RESTORE` it checks that the
restored rect still hits a connected monitor (`PlacementIsVisible`
on the `GetWindowPlacement` result); otherwise `ResetToDefaultView`. The same check
that `PlacePopupInitially` does on opening. `SaveWindowPlacement` is called only in
the maximize branch — on restore the geometry is already saved, and a call
there would have saved the maximized one.

**Pure GDI vectors, no font.** A `DrawTextW` with a Unicode character costs
far more than four `LineTo`, and would depend on the font *having* the glyph
— the same problem as the tray icon's missing `k`.

**No fade.** The color changes instantly on hover, and the hit depends on
`btnHot`, not on any fade level (pitfall 12). At rest: glyph `#6E7681` on the
panel background, no button background. Hover: `#161D27` behind `+ – □`, `#C02A3E`
behind `×`, with a white glyph.

**Drawn from `PaintPopup`, not from `DrawChart`.** `DrawChart` returns
early when the buffer is empty — that is, while it says "Loading data from
Binance..." and during an entire disconnect. Had the drawing been there, the close cross would vanish
exactly when you want to close the panel. Same reason `DrawOverlay` is there.

**The header text shrinks by `BTN_STRIP_W`** (118 px), otherwise the
right-aligned percentage sat right under the close cross.

#### The toolbar (phase 22)

Row 2 of the header (y 28–42) is a toolbar where the symbol line used to be plain
text: `[BTC/USDT ▾]  [1m][5m][15m][1t][4t][1d]  [VOL]`, left-aligned from
`PAD_L`. `ToolbarLayout(W, out[TBAR_COUNT])` is the **single source of truth**,
like `ButtonLayout`: drawing, `WM_NCHITTEST`, hover and click read it.
Widths are **fixed constants**, not measured text — `WM_NCHITTEST` has no
DC. A `C_ASSERT` keeps the whole row within `HeaderRow2Limit` at
`POPUP_MIN_W`; below that pills are hidden from the right, whole, never halved.

| Pill | Action |
|---|---|
| symbol `▾` | opens the overlay (the same as right-click in the chart); active while it is open |
| `1m` … `1d` | `ApplyConfigChoice` — the same path as the overlay and the tray menu; a click on the active one is a no-op |
| `VOL` | `SetShowVolume` — **not** `ApplyConfigChoice`: the buffer should not be emptied for a drawing choice |
| `MA` | `SetShowIndicators` (phase 25) — same form. The only pill outside the minimum width: hidden below 426 px, `M` and the tray menu work regardless |

The pills are `HTCLIENT`, the gaps and the rest of the header `HTCAPTION`
(pitfall 21). `tbHot` follows `btnHot`'s rules: set after
the `TrackMouseEvent` arming, blocked with the overlay open and during
panning, reset in `WM_MOUSELEAVE`. No fade. At rest it is muted text,
hover a `CLR_BOX` surface, active `CLR_BOX` with a `CLR_BOXEDGE` frame. No new
GDI objects. **Drawn from `PaintPopup`**, like the buttons: right after a switch
the buffer is empty, and `DrawChart` returns early.

`showVol` is the choice (the registry, `ShowVolume`); `dispVolF` ∈ [0, 1] is
the display, eased in `WM_TIMER` as the sixth value, regardless of whether there are
candles. The bar height is multiplied by it; at 1.0 the factor is exact. If the surface
is not visible when the choice changes (the tray menu with the panel closed), it snaps.

**The disconnected text** (`frakoblet Ns`) used to be in the symbol line. It is now drawn to
the right of the last pill, from `DrawChart` (the health fields are read under the lock), and
only when the whole text fits before `HeaderRow2Limit`.

#### Price alerts (phase 23)

The price column (`x > edge`, `y ∈ [top, bottom]`) is the alerts' surface. It was
a no-op for clicks up to and including phase 22.

| Action | Behavior |
|---|---|
| Cursor in the column, empty space | hand, **ghost**: a muted amber line across the chart and a *framed* tag with the rounded price. Gray when all eight slots are used |
| Click on empty space | `AlertAdd` at `AlertPriceAtY(y)` — the alert is set, written to the registry |
| Cursor on a tag | the tag turns red like the close button (`alertHot`) — but not a *newly set* tag until the cursor has left it once (`alertFresh`) |
| Click on a tag | `AlertRemove` — the nearest tag within `[y − 8, y + 8)`, the area that is drawn |
| `A` with crosshair | alert at the crosshair's price; without a crosshair nothing |
| "Clear price alerts (N)" in the tray menu | clears the alerts for the symbol shown; gray without alerts |

**The side is stored, not the previous price.** `alerts[sym][i]` is a double with the side
in the sign: `+nivå` was set above the price and fires when the price is ≥, `−nivå`
below and fires when it is ≤. `AlertHit(now, signedLevel)` is a pure function;
`now ≤ 0` never fires (right after a symbol switch `lastPrice` is 0). A level
that was crossed while the app was off or the machine was asleep therefore fires on the first
price afterward, and a symbol switch has no "previous price" to compare wrongly.

**The trigger lives in `WM_APP_DATA`** (`CheckAlerts` after `UpdateIcon`): the one
place every new price passes on the UI thread — panel open, closed and in
desktop mode. Everything alert-related is **UI-owned**; the thread contract is untouched.
An alert fires **once** and is removed. Only the current symbol's alerts
are tested — the price we have is its.

**When it fires** (`FireAlert`): afterglow on the level in full amber that fades out
(`alertFlashF` 1 → 0, `ALERT_TAU_FLASH`, the seventh eased value, only when
the panel is visible), a balloon from the tray icon (`NIF_INFO`, `NIIF_NOSOUND`, from
a **copy** of `nid` — `UpdateIcon` owns the original) and
`MessageBeep(MB_ICONASTERISK)`. One sound, and it comes even when Windows holds
the balloon back.

**Drawing and hit-testing read the same source** (pitfall 14): `AlertY` and
`AlertPriceAtY` read `dispMin`/`dispMax` with the same clamping as the candles.
`AlertRound` rounds to the largest power of ten ≤ one pixel in price, floor 0.01
(pitfall 16). The lines are drawn **behind the candles**, above the bars, within
the clip; the tags on the stamp's surface, and the stamp on top. Grid labels
less than 16 px from a tag are not drawn. `DC_PEN`/`DC_BRUSH` — **no new
GDI objects**. Desktop mode draws the lines, not the tags.

**The double-click area is `[left, edge]`**, not `[left, W)` as up to and including
phase 22: in the column a quick double-click is set + remove (pitfall 38).

#### The fast path for hover repaint

A hover change invalidates **only the button row** (`ButtonStrip(W)`), not the whole
window. `PaintPopup` has one branch for it: if the whole `ps.rcPaint` lies within
the strip, a 110×18 buffer is built and only the buttons are drawn — `DrawChart` and
`DrawOverlay` are skipped.

> Without the branch the invalidation would not have saved anything at all beyond the last
> blit. GDI clips it, but the whole 1280×720 buffer would still have been
> built and the chart redrawn. **Measured: 0.979 ms → 0.062 ms, that is 16×.**

The background in the strip is taken from the **watermark bitmap**, not from `FillRect`. Then
it is guaranteed identical to what the slow path would have put there, without us
needing to know that the watermark text never reaches up into the header. `SetViewportOrgEx`
lets `DrawButtons` keep computing in window coordinates, and is reset before
the blit — otherwise the source point `(0,0)` would have been interpreted logically.

Two exceptions, both measured:

| Case | Why | What happens |
|---|---|---|
| The overlay open | it dims the **whole** client area, the header included | the condition checks `!overlayOpen`, so we take the slow path |
| The animation timer is running | its `InvalidateRect(NULL)` unions with the strip | `rcPaint` becomes the whole surface, and we fall to the slow path by ourselves |

**The fast path does not cover all hover transitions.** Button → button and button →
gap go through it. Button → *free header space* does not: it goes
via `WM_MOUSELEAVE`, which resets `hoverIdx` and `overlayHot` in the same
message — and those affect the chart area, so a full repaint is correct there.
`InvalidateRect(NULL)` is thus left in `WM_MOUSELEAVE` on purpose.

#### The cursor during panning

`WM_SETCURSOR` is **partly back** — contrary to what "Removed in phase 3" says. It
steps in when `panning` is true **and** the hit zone is `HTCLIENT`, and sets
`IDC_SIZEALL`. Everything else passes on with `break`, so `DefWindowProc` keeps
its six resize cursors at the edges and the normal arrow in the header.

> `IDC_SIZEALL`, not `IDC_HAND`. The latter is the link cursor and means "this
> can be clicked", not "this is dragged". Windows has no closed hand among
> the standard cursors.

`SetCursor` is also called directly at panning start and end: `WM_SETCURSOR`
fires only on the next mouse move, so without it the first frame of the drag
still showed the arrow. The cursors are cached in `AppContext`; `LoadCursorW` gives a **shared**
handle for standard cursors, so they do not count as ours and must not go through
`DestroyCursor`.

#### The rest

| Action | Behavior |
|---|---|
| "Quit" in the tray menu | exits the program |
| "Desktop mode" in the tray menu | switches between the panel and the desktop surface; the check mark shows the current mode, the choice is saved (phase 12) |
| Tray click | in front and active → hide; otherwise show, restore and give focus |
| `Ctrl` + `0` / "Default view" | centers 1280×720 on the monitor the window is on (gray in desktop mode) |
| Double-click on the chart | resets zoom and panning (eased). Up to and including phase 22 also on the price axis; that belongs to the alerts now (phase 23) |
| `R` | resets zoom and panning (not while the overlay is open) |
| `ESC` | layered: close the overlay → reset the view → hide to the notification area (duplicate: exit) |
| Double-click in free header space | maximizes / restores |
| `Win` + `↑` / `↓` / `←` | maximize / restore / snap — works without `WS_SYSMENU` |
| `Ctrl` + `N` / `M` / `W`, `F11` | `[ + ]` / minimize / close / maximize–restore — the same path as the buttons, blocked during panning (phase 19) |
| `Alt` + `F4` | close (hide) via `DefWindowProc` → `WM_CLOSE` — works without `WS_SYSMENU`, measured |
| `←` / `→` | one wheel notch backward / forward (`vc / 8` candles), eased (phase 20) |
| `PgUp` / `PgDn` | a whole view backward / forward (phase 20) |
| `Home` / `End` | oldest candle — the wall requests history like a drag — / the live edge with `followLive` (phase 20) |
| `+` / `-` (also numeric keypad, also with `Ctrl`) | one zoom step in / out around the **middle** of the view (phase 20) |
| `V` | the VOL pill: volume bars on/off, eased (phase 22) |
| `1` … `6` | the interval pills in order, 1m … 1d (phase 22) |
| "Volume bars" in the tray menu | switches the choice for **the mode the process is in**: the panel's in panel mode, the desktop's in desktop mode (phase 22, per mode from phase 26) |
| `M` / the `MA` pill / "Indicators" in the tray menu | SMA 20, EMA 50, VWAP, today's high/low and yesterday's high/low/close on/off, faded (phase 25; VWAP and high/low from phase 27, yesterday from phase 28, when the tray item was renamed from "Glidende snitt" (Moving averages)). `Ctrl`+`M` is still minimize. The tray item is per mode, like "Volume bars" (phase 26) |
| Click in the price column / `A` | sets or removes a price alert — see *Price alerts* above (phase 23) |
| "Clear price alerts (N)" in the tray menu | clears the alerts for the symbol shown (phase 23) |
| Lost capture in the middle of a drag | `WM_CAPTURECHANGED` releases the panning and restores the cursor (phase 20) |

**The default view is DPI-scaled:** `MulDiv(1280, GetDpiForWindow(hwnd), 96)`,
clamped to the work area. The process is still **DPI-unaware**, so this gives
exactly 1280×720 today — but it becomes correct automatically if DPI awareness
is turned on later. See "Rejected proposals" for why it is not.

**`UpdatePopupTitle` is kept** even without a title bar: the title is what
the taskbar and `Alt`+`Tab` show.

**`ForceForeground` is kept.** Bug #1 still applies: a tray click does
not give the process foreground rights, and without this the window never gets keyboard focus —
then neither `ESC` nor `Ctrl`+`0` gets through.

**The panel is not owned by the main window.** Ownership would have removed the button in
the taskbar, but it also means Windows does not tear it down for us:
`WM_DESTROY` on the main window does it itself. Measured that the button is there with the panel
and gone when it is hidden — `WS_EX_APPWINDOW` is not needed.

**Geometry in the registry:** `PanelX`, `PanelY`, `PanelWidth`, `PanelHeight`,
plus `PanelHasPos`, which distinguishes "not saved" from "saved as 0,0".
The coordinates are interpreted as *signed* — a monitor can be to the left of the
primary one. The saved position is used only if it still hits a connected
monitor (`MonitorFromRect`), otherwise the window is centered. Saving goes through
`GetWindowPlacement`, so a minimized or maximized window does not remember a
taskbar strip as "the user's size".

**`ResetToDefaultView` restores both a maximized and a minimized window first.**
A minimized window is still `WS_VISIBLE`, so `IsWindowVisible` is `TRUE` and
the tray menu's "Default view" skips `TogglePopup`; without the `IsIconic`
check, `SetWindowPos` only set the restored geometry while the window
stayed minimized at −32000,−32000. So the menu item did nothing
visible. The bug was there before, but the minimize button makes the path easy to reach.

### The animation timer

One timer (`TIMER_ANIM_ID`, 16 ms) drives everything time-dependent. It is
**time-based, not step-based**: each tick measures the actual elapsed time
against `lastAnimTick` and interpolates exponentially through `AnimStep()`.
`SetTimer(16)` in practice fires every ~15.6 ms and gets coalesced under load
— a fixed step length would give different speeds depending on system load.

The timer **lives only while something is moving**. `StartAnim()` is
idempotent and resets `lastAnimTick` only when the timer was actually stopped;
`WM_TIMER` kills itself once everything has settled. At rest no timer runs —
verified, see the measurements.

### Network health

The worker thread counts consecutive failures in `netFailures` and waits
`NetBackoffMs()` instead of a fixed 3 s. After three consecutive failures,
`hConnect` is released, so that `HttpGet` rebuilds the connection and DNS is
looked up again — without that we stay stuck on an IP that no longer answers.

`lastOkTick` drives the stale state (`> 3 * TIMER_INTERVAL` = 9 s, i.e. two
missed cycles, so that one slow request does not flash the indicator). Stale
shows in four places: dimmed header price, a seconds counter in the subtitle,
`(frakoblet)` in the tray tooltip, and dimmed digits in the icon.

**The lock also covers:** `lastOkTick`, `nextRetryTick`, `netFailures`.
`hConnect` is owned by the worker thread alone and needs no lock.

### Runtime config: symbol and interval

`SYMBOLS[]` and `INTERVALS[]` are curated tables. Curated, not free text: a
fixed list means we know the price range, and that no fetch can fail on an
unknown symbol. `KLINE_MS` no longer exists — all three places that used it
read `ctx->intervalMs`.

**`configGen` is what prevents silently wrong data.** The scenario: the thread
is in the middle of a fetch for BTC, the user switches to ETH, the UI thread
empties the buffer, and the BTC response comes back and is merged into a
buffer that now belongs to ETH.

The worker thread takes a copy of `configGen` before the fetch and compares
**at merge time**, not at fetch time — the response can arrive at any point
along the way. The check is in **both** fetch functions. The price has exactly
the same race and drives the tray icon; the design only mentioned the candles.

> A discarded response returns `TRUE`. It is not a network failure, and must
> not bump the backoff every time the user switches symbol.

`ApplyConfigChoice` bumps `configGen` and empties the buffer in the **same
critical section** as the switch.

**The lock also covers:** `symIdx`, `ivIdx`, `intervalMs`, `configGen`.

### The overlay

Drawn inside the panel's client area. **No new HWND** — without a new window
there is no activation change, and we stay entirely out of the territory where
bug #1 and #2 lived.

`OverlayLayout()` fills one array that **both** drawing and hit testing read.
Same discipline as `ChartGeometry`, for the same reason (bug #7).

Called from `PaintPopup`, not from `DrawChart` — `DrawChart` returns early
when the buffer is empty, which is exactly the state right after a config
switch.

Hit testing hangs on `overlayOpen` (logical state), never on `overlayF` (fade
level). During fade-out the box is still visible, but clicks must go to the
chart again.

### The watermark

Background and watermark are baked together into one cached `HBITMAP` that
**replaces** `FillRect`. The cache is invalidated by `WM_SIZE` and by a config
switch, and tears down the old one before building a new one. If the bitmap
fails, `DrawChart` falls back to `FillRect` — the watermark is decoration and
must never prevent painting.

The color is white blended in with `WatermarkAlpha(W)` =
`clamp(0,08 · √(W/1920), 0,04, 0,10)` (phase 11). Since `W` is already part of
the cache key, the alpha costs nothing per frame.

### Target vs. display — the key idea in part C

`viewStart`/`viewCount` (int, lock-protected) are **the target** and are owned
by both threads as before. Next to them sit four **pure UI doubles** that no
other thread touches:

| Field | Meaning |
|---|---|
| `dispStart` | animated position, can be fractional |
| `dispCount` | animated width, can be fractional |
| `dispMin` / `dispMax` | animated price edge |

The worker thread writes the target; the UI thread eases toward it.
**That is why the whole easing does not touch the thread contract** — `disp*`
must never enter the lock domain.

**`DrawChart` and `HitCandle` must both read `disp*`.** If one reads the
target and the other the display, the crosshair points at the wrong candle in
the middle of the animation. That is bug #7 in new clothes, and it is the one
rule that cannot be bent.

The drawing loop runs from `floor(dispStart)` to `ceil(dispStart + dispCount)`
with `IntersectClipRect` against the chart area, so the edge candles do not
bleed into the price axis. The clipping is restored before the axis text and
the chrome are drawn. The loop still runs over **visible** candles —
`i1 - i0` is `dispCount + 1`, not `candleCount`.

**Snapping is a quarter pixel**, converted to the unit being eased on each
tick — not a fixed number in candles or dollars. See the measurements for why.

**What is eased and what is not:**

| Action | Behavior |
|---|---|
| Wheel panning, Ctrl+wheel | eased |
| **Drag panning** | **X follows the mouse directly**, Y eased |
| Y axis on new data | eased |
| Symbol/interval switch, panel open, buffer reset | snaps (`dispValid = FALSE`) |
| Index shift on eviction and backfill | snaps (`ApplyFrontShift`) |

`SyncDisp` does **not** mark itself valid when the buffer is empty. If it did,
the axis would sit at `[0, 1]` through a symbol switch and glide up to the
real range when the data arrived — see bug #15.

### The last-price indicator

A dashed line from the last candle to the right edge, with a filled,
color-coded stamp on the price axis. Drawn after the candles and **before**
the early return in the crosshair block, otherwise it would disappear as soon
as the mouse was outside.

The color follows `dP = P_t - P_t-1` — the last close against the previous
one. That is a different rule from the candles' own (`close` against `open` in
the *same* candle), so they can point in opposite directions. Intentional: the
line answers "where are we against the previous close".

If the price is outside the visible range, nothing is drawn. A stamp clamped
to the edge would place the price somewhere it is not.

### The watermark's font size

`klemt(chart-høyde / 5, 32, 120)`, then **fitted to the width**: the text is
measured with `GetTextExtentPoint32W` and the height is scaled down in the
same ratio if it does not fit. Without the width fit, `BTCUSDT` was clipped on
narrow panels.

DPI scaling applies to **the clamp limits**, not to `H/5`. `g.ch` is already
device pixels, so the proportional part scales itself; if you multiply `H/5`
by DPI as well, you count the scaling twice.

Built in `EnsureWatermark`, which by definition only runs when
`(W, H, symIdx, ivIdx)` changes. The cost per frame is zero.

### The registry

`HKCU\Software\Ticker`, `REG_DWORD`: `SymbolIndex`, `IntervalIndex`,
`PanelWidth`, `PanelHeight`, `PanelX`, `PanelY`, `PanelHasPos`,
`DesktopMode` (phase 12), `ShowVolume` (phase 22) and `ShowIndicators`
(phase 25) — the last two default 1 and **the panel's** — and
`ShowVolumeDesktop` and `ShowIndicatorsDesktop` (phase 26), default 0 and **the
desktop surface's**. All four are written by `SaveConfig` together with the
indices. Read in `WinMain` **before `CreateThread`**, so that the first fetch
goes to the right pair. The indices are bounds-checked. Every failure path
lands on BTC/USDT 1m.

The panel size is captured in `WM_EXITSIZEMOVE`, not at exit — see bug #11.

`DesktopMode` is written the moment the user chooses in the tray menu, not in
`WM_DESTROY`, which never runs when the process is killed from outside. It is
read only when neither `--desktop-mode` nor `--dup` is given. The flag wins for
that run, and a duplicate is always a panel and never writes.

**The price alerts (phase 23)** are the only values that are not `REG_DWORD`:
`Alerts_BTCUSDT`, `Alerts_ETHUSDT` … — one `REG_BINARY` per symbol, doubles
with the side in the sign. The name is the API symbol, not the index, so a
changed symbol table never moves an alert to another symbol. `SaveAlerts`
writes the moment an alert is set, removed or fires (like `DesktopMode`), and
deletes the value with the last alert. `LoadAlerts` discards one by one: wrong
type, a length that is not a whole number of doubles, NaN, 0, ≥ 1e9 and
anything past eight. A duplicate neither reads nor writes them.

**Autostart (phase 13) lives outside `Software\Ticker`:**
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, the value `Ticker`,
`REG_SZ`, the path to the exe in quotes. It is read every time the tray menu
opens and written only on click. The app never reads it at startup. The key is
the macro `AUTOSTART_KEY`, so test builds should point it to a key of their
own.

---

## Key constants

| Name | Value | Meaning |
|---|---|---|
| `MAX_CANDLES` | 6000 | 4 days at 1m, 16 years at 1d; was 1440 until phase 17 |
| `SEED_COUNT` / `DEFAULT_VIEW` | 360 / 300 | 6 h seed and backfill batch / default view. 60 candles of warm-up, so EMA 50 is defined from the left edge (phase 25; was 300 / 300) |
| `MIN_VIEW` | 8 | max zoom-in |
| `ZOOM_STEP` | 1.2 | per wheel notch |
| `TIMER_INTERVAL` | 3000 | fetch frequency (ms) |
| `KLINE_MS` | 60000 | one 1m candle |
| `REOPEN_GUARD_MS` | 250 | stops a close click from reopening |
| `SHOW_GRACE_MS` | 400 | ignore focus loss right after opening |
| `s_httpBuf` | 98304 | ~1.6× margin against a ~60 KB response (360 candles; was 1.94× against 50.7 KB) |
| `ANIM_INTERVAL` | 16 | animation timer (~60 fps) |
| `ANIM_TAU_CHROME` | 55.0 | time constant, chrome fade (ms) |
| `ANIM_DT_MAX` | 100.0 | clamps `dt`, a long pause gives one jump |
| `NET_RETRY_MAX` | 60000 | cap for exponential backoff (ms) |
| `NET_RECONNECT_AT` | 3 | number of failures before `hConnect` is released |
| `STALE_AFTER` | 9000 | 3 × `TIMER_INTERVAL` = two missed cycles |
| `SYMBOL_COUNT` | 4 | BTC, ETH, SOL, BNB |
| `INTERVAL_COUNT` | 6 | 1m, 5m, 15m, 1h, 4h, 1d |
| `CLR_WM_INK` / `WM_ALPHA_*` | white / 0.08 · 0.04 · 0.10 | watermark, `WatermarkAlpha(W)`, nominal at 1920 px (phase 11; replaces `CLR_WATERMARK` `#15191F`) |
| `CLR_AXIS` | `#A0AAB8` | price and time labels, 8.05:1 against `CLR_BG` |
| `AXIS_Y_W` / `AXIS_PAD_R` / `PAD_R` | 76 / 8 / 84 | price column (4 + 8 chars × 9 px) / edge margin / sum |
| `PAD_B` | 18 | the time axis band |
| `TIME_DX_MIN` / `TIME_LBL_GAP` | 80 / 12 | minimum label spacing = max(80, width + 12) |
| `OVL_ROW_H` / `OVL_COL_W` | 22 / 104 | overlay row and column width |
| `ANIM_TAU_VIEW` | 70.0 | time constant, view easing (ms) |
| `SNAP_PX` | 0.25 | snap when less than a quarter pixel remains |
| `WM_FONT_DIV` / `MIN` / `MAX` | 5 / 32 / 120 | the watermark's font height |
| `CHART_TOP_MIN` | 32 | minimum `rcChart.top`, checked with `#error` |
| `POPUP_MIN_W` / `H` | 400 / 250 | minimum size, DPI-scaled in `WM_GETMINMAXINFO` |
| `HDR_GAP` | 8 | minimum gap between header texts and to the button row |
| `VOL_FRAC` | 0.22 | the volume bars' band, share of the chart area's height (phase 21) |
| `CLR_VOL_UP` / `CLR_VOL_DOWN` | `#09542D` / `#51212D` | bar colors, `CLR_UP`/`CLR_DOWN` blended ~28 % toward `CLR_BG` |
| `TBAR_TOP` / `TBAR_H` | 28 / 15 | the toolbar's row: y in [28, 43), below the price baseline and above the chart area (phase 22) |
| `TBAR_SYM_W` / `TBAR_IV_W` / `TBAR_VOL_W` | 74 / 28 / 32 | fixed pill widths; sum with gaps 300 px, ending at x = 310 against the limit 312 at 400 px |
| `TBAR_GAP` / `TBAR_GROUP_GAP` | 2 / 8 | between interval pills / between the groups |
| `TBAR_IND_W` | 26 | the `MA` pill (phase 25), x in [312, 338): outside the minimum width on purpose, hidden below 426 px |
| `IND_SMA_PERIOD` / `IND_EMA_PERIOD` | 20 / 50 | moving averages on the close (phase 25). Prefix `IND_`: `MA_*` belongs to `winuser.h` |
| `CLR_SMA` / `CLR_EMA` | `#3D8FBF` / `#A072D0` | muted steel blue / muted violet, 1 px above the candles. Not found elsewhere on the surface, so a probe can count them |
| `IND_TAU_FADE` | 55.0 | the `MA` toggle's fade (= `ANIM_TAU_FADE`), snap 0.02 |
| `IND_BATCH` | 1024 | points per `Polyline`; the buffer is the volume bars' `s_volPts` |
| `ALERT_MAX` | 8 | price alerts per symbol, fixed slots, 256 bytes in total (phase 23) |
| `CLR_ALERT` / `CLR_ALERT_LINE` | `#FFB020` / `#86601B` | amber: tag and afterglow / the line over the data area, blended halfway toward `CLR_BG`. Not among the eleven fixed colors, so a probe can count them |
| `ALERT_HIT_PX` | 8 | half the tag height: the hit area is the area that is drawn |
| `ALERT_TAU_FLASH` | 900.0 | time constant for the afterglow when an alert fires (ms), snap 0.02 |
| `ALERT_PRICE_MAX` | 1e9 | upper limit for a level, guard against a hand-edited registry |

---

## Functions, in the order they arrived

1. **GDI chart in a popup** — candlesticks, grid, price axis, header.
2. **Correct tray icon** — the font table was broken from the start (see
   bug #3).
3. **One line in the icon** — 4×9 font, `75.8` instead of `75k`/`778`.
4. **Crosshair + hover box** — time and OHLC for the candle under the mouse.
5. **Movable / resizable panel** — `WS_THICKFRAME` + `WM_NCCALCSIZE`.
6. **Hover controls** — close cross, grip dots, frame and resize grip that fade
   in on hover and are invisible at rest. **Removed in `7c38b6d`** — replaced by
   the OS frame.
7. **Ctrl + wheel = zoom**, anchored at the mouse pointer.
8. **Accumulating history + panning** — wheel and drag.
9. **Worker thread + GDI cache.**
10. **Animation timer, exponential backoff and stale indicator** (phase 2 A).
11. **Runtime choice of symbol and interval**, overlay, watermark and the
    registry (phase 2 B).
12. **Last-price indicator, scaled watermark, view and Y-axis easing**
    (phase 2 C).
13. **Native window frame**, default view (`Ctrl`+`0`) and position
    persistence.

### Removed in phase 3

The descriptions above stay because they explain *why* the code turned out the
way it did. This is what no longer exists, and where it went:

| What | Why | Commit |
|---|---|---|
| Hover chrome: close cross, grip dots, resize grip, frame, fade | the OS frame has all of it | `7c38b6d` |
| `EnsureChromeCache` and six cached GDI objects | went with the chrome; GDI 35 → 29 | `7c38b6d` |
| Auto-hide on focus loss, `pinned`, `SHOW_GRACE_MS`, `REOPEN_GUARD_MS` | a window with a title bar that disappears when you click in another window is unusable | `7c38b6d` |
| `inSizeMove` (bug #6) | used for the drag frame's color, which is gone | `7c38b6d` |
| `WM_SETCURSOR` | `DefWindowProc` does the job again | `7c38b6d` |
| `CLR_WHITE`, `CLR_HDRHOT` | the chrome was their only user | `7c38b6d` |

The measurements of the fade colors (`#0D1117 → #333D4B`) apply to code that
no longer exists. They remain as a method: *measure pixel colors, don't judge
by eye* is still the rule — see pitfall #7.

> **Three of the rows above came back in phase 4.** `WM_NCCALCSIZE` and
> `WM_NCHITTEST` are needed again as soon as the OS frame is gone, and
> `CLR_CLOSEHOT` is the color behind the close cross. `WM_SETCURSOR` is
> **partly** back in phase 5 — only during panning, to set `IDC_SIZEALL`. It is
> not back for the header:
> the header needed `IDC_SIZEALL` in phase 1 because the whole window was a
> drag surface with hidden chrome; now the normal arrow is right, as it is in a
> title bar. What did not come back is the fade, the auto-hide and the six
> cached chrome objects.

---

## Bugs found and fixed

These are worth knowing about — several were not visible without measuring.

**1. `SetForegroundWindow` was refused.** Windows' foreground lock meant the
popup was shown but never activated, got `WA_INACTIVE` immediately and hid
itself. Fixed with `AttachThreadInput` around the switch (`ForceForeground()`),
plus a 400 ms grace period in `WM_ACTIVATE`.

**2. ESC did not work.** `WM_ACTIVATE` returned 0 on *activation* too, so
`DefWindowProc` — which sets keyboard focus — never ran. `GetGUIThreadInfo`
showed `hwndFocus = 0`. The activation branch now falls through to
`DefWindowProc`.

> Test trap: `PostMessage(WM_KEYDOWN)` bypasses focus and gives a false
> positive. Use `keybd_event`.

**3. The font table was broken.** All the digit values were 19-bit, but the
code reads 15 bits (`bitPos = 14 - (r*3+c)`). The upper bits fell outside, and
the icon drew noise. Ten of twelve glyphs were wrong. The values were
recomputed from the table's own comments and verified by decoding them back.

**4. 512-byte read buffer.** One `WinHttpReadData` call returns only what
happens to be buffered. Real klines responses are 10–50 KB. Everything is now
read in a loop (`HttpGet()`).

**5. `%.1f` rounded 99950–99999 up to "100.0".** The threshold
`price >= 100000` did not catch it — it is the *formatted string* that has to
fit. The width is now measured with `IconTextWidth()` after formatting.

**6. Dragging froze every 3 seconds.** `WM_TIMER` also fires inside the modal
move/resize loop. Fixed with the `inSizeMove` flag — less relevant after the
thread, but still correct.

**7. The crosshair pointed wrong after zoom.** `hoverIdx` is an *absolute*
index; if you zoom without moving the mouse, the view moved under an index
that was not updated. Zoom and panning now recompute via `HitCandle()`.

**8. WinHTTP timeouts were missing.** The default receive timeout is 30 s; at
exit we wait only 3 s for the thread and closed the session under it. Now set
to 5 s.

**9. The backoff never escalated.** The reset of `netFailures` sat after the
whole `WaitForMultipleObjects` call, with only a check on `WAIT_OBJECT_0`
(stop) above it. It therefore also hit `WAIT_TIMEOUT` — that is, every single
cycle. `netFailures` never got higher than 1, the wait was stuck at ~6 s, and
`hConnect` was never released because `failures == NET_RECONNECT_AT` never
became true. The code looked right when read; the log showed `feil=1` for
twenty cycles in a row. The reset now hangs on `wr == WAIT_OBJECT_0 + 1`
alone.

> This is why part A was measured against an actually blocked line and not
> just unit tested. `NetBackoffMs()` was green on all seventeen tests the
> whole time — the bug was in *who called it with which counter*.

**10. The animation timer kept running on a hidden panel.** The stale branch
set `settled = FALSE` to keep the seconds counter alive. Neither hide path
(`WM_ACTIVATE` and `TogglePopup`) kills the timer, so a disconnected line gave
60 ticks a second on a panel nobody saw. The branch is now conditional on
`IsWindowVisible(hwnd)`, and `TogglePopup` restarts the timer on show if we
are offline — otherwise the counter stood still until the next `WM_APP_DATA`,
which during backoff can be a whole minute away.


**11. The panel size was never saved.** `SaveConfig` sat in `WM_DESTROY` and
read `GetWindowRect(hPopup)` there. The panel is **owned** by the main window
and is already torn down when `WM_DESTROY` gets there, so `GetWindowRect` had
nothing to read and `PanelWidth` was never written. The size is now captured
in `WM_EXITSIZEMOVE` — when the user lets go.

> The reasoning in the first version was "read from the window itself, then
> the two can never get out of sync". Right in principle, wrong in practice:
> the window no longer existed. The registry was empty; that is what gave it
> away.

**12. The price axis assumed BTC scale.** `"%.0f"` on all five labels. SOL at
around 97 dollars has a range under one dollar, so all five read `97`. The
decimals are now chosen from *the spacing between the labels*
(`PriceDecimals`). Same class of bug as #5: the format must follow the number
that is actually shown.

**13. `OverlayLayout` was not pure.** The rows past `count` were stack
garbage, so two calls with the same input gave different content. Harmless
today — `OverlayHit` only goes up to `count` — but the unit test "same in =
same out" failed, and it is a class of bug worth closing. The struct is now
zeroed.


**14. The tray icon got stuck on the previous symbol.** `ApplyConfigChoice`
resets `lastPrice`, and `UpdateIcon` returns early on `price <= 0.0`. When the
panel is open the thread fetches **only candles** — and the candle branch
never wrote `lastPrice`. After a symbol switch the icon *and* the tooltip
therefore stayed on the previous symbol's price and label for as long as the
panel was open.

> Measured: 15 s after switching to SOL the icon still read `75.9` — BTC —
> while the panel showed SOL. Exactly the class the design forbids: data
> under the wrong label. All the symbol switches in testing were done with
> the panel open and without looking at the icon, so none of the earlier
> measurements caught it. `WorkerFetchKlines` now sets `lastPrice` from the
> last candle's `close`.


**15. The Y axis would have glided up from zero on every symbol switch.**
`SyncDisp` marked itself valid even with an empty buffer, and then set
`dispMin`/`dispMax` to `[0, 1]`. Right after a switch the buffer is empty, so
the axis stayed *valid* at `[0, 1]` — and when the new candles arrived, it
eased up to the real range. In other words, a price axis that glides up from
zero for half a second on every switch, which is exactly what `SyncDisp`
exists to prevent.

> Caught before it reached the user, but only after the code was written and
> committed. The measurement that revealed it: log `dispMin`/`dispMax` per
> frame and look at the transition `candleCount` 0 → 300. With an empty buffer
> we now stay *invalid*, so the first frame with data snaps.

---

## Measurements

### UI latency (max, measured with `SendMessageTimeout` against the UI thread)

| Version | Mean | Max | Stalls >50 ms |
|---|---|---|---|
| 300 candles, full fetch every 3 s | 648 ms | 929 ms | 4–5 per 13 s |
| Incremental fetch (`limit=3`) | 486 ms | — | — |
| Removed double price call | 233 ms | 247 ms | — |
| **Worker thread** | — | **2.1–4.2 ms** | **none** |

The network call was 130× more expensive than the whole repaint. The thread
was the whole gain.

### Painting

| | Net per repaint |
|---|---|
| Before GDI cache | 1.796 ms |
| After GDI cache | **1.255 ms** (−30 %) |
| Phase 21, before volume bars (1280×720, 300 candles, median of 172 frames) | 1.44 ms |
| Phase 21, with volume bars (`PolyPolygon` in batches) | **1.56 ms** (+0.12 ms; `FillRect` per candle gave 1.84) |
| Phase 22, before the toolbar (red run, median of 177 frames) | 1.48 ms |
| Phase 22, with the toolbar (two runs, 171 and 168 frames) | **1.61 / 1.52 ms** (p90 1.75 / 1.70 against 1.62) |
| Phase 23, without alerts (two runs, median of 150 forced frames) | 1.89 / 1.94 ms |
| Phase 23, with **eight** alerts in the view | **2.01 / 2.11 ms** (+0.11 / +0.17 ms) |

Fully zoomed out is *faster* (1.5 ms) because the candles are then 1 px wide.
The numbers from phase 21 and 22 are measured with QPC around the slow path in
`PaintPopup` in the test build (probe field 15), not with `PrintWindow` in
flight (pitfall 37). Phase 23 forces the frames with
`RedrawWindow(RDW_UPDATENOW)` from the probe instead of waiting for the
animation timer; the baseline is therefore not comparable with phase 22 (red
run, same method: 1.73 ms) — only the difference within the same run is.

### Unit tests on real code

The technique used: extract the function from `ticker.c` with `sed` into a
small harness, so the tests run against **the actual code**, not a copy.

- `ParseKlines` against a real Binance response: **300/300 candles, 0
  mismatches** against `ConvertFrom-Json`.
- `MergeCandles`, 6 cases, all green: seed of 300 · 200 consecutive fetches
  (→ 500 contiguous candles) · double fetch without duplicate · candle still
  forming updated instead of appended · cap at 1440 with the oldest out · time
  gap resets.
- Zoom anchoring, analytically: largest deviation **0.50 candles** (pure
  rounding).

### Verified empirically

- **Zoom anchoring:** the same candle (`01:35`, identical OHLC) sat under the
  mouse both before and after 5 notches, while the span went 5h → 2h and the Y
  axis tightened.
- **Hover box against ground truth:** `02:39`, O 75872.21 / H 75915.49 /
  L 75872.20 / C 75915.45 — exact match against independently fetched Binance
  data.
- **Cursors in all nine zones:** header `IDC_SIZEALL` (must be forced —
  `DefWindowProc` gives the normal arrow for `HTCAPTION`), edges
  `IDC_SIZEWE`/`SIZENS`/`SIZENWSE`/`SIZENESW` (`DefWindowProc` handles these
  itself).
- **Rest vs. hover:** frame pixel `#0D1117` (plain background) at rest,
  `#333D4B` on hover, with the measured fade
  `#0D1117 → #171D25 → #222934 → #27303B → #333D4B`.
- **Stress test:** 15 965 pan/zoom/draw operations in 20 s concurrently with ~7
  merge operations from the thread. No deadlock.
- **Leaks:** GDI and USER flat through all tests. GDI stays constant at **31**
  (up from 18 — intentional, the objects are now held permanently instead of
  being created 16 times per frame).
- **Exit:** 134–228 ms, no leftover process.

### Phase 2 part A

Unit tests against code extracted from `ticker.c` with `sed` — **17/17
green**:

| Unit | What was verified |
|---|---|
| `NetBackoffMs` | schedule 3/6/12/24/48/60/60/60 s, jitter within ±12.5 %, never above the cap, `failures` 8–40 without overflow |
| `AnimStep` | convergence with snap, no overshoot, `dt == 2 × dt/2` (frame-rate independent), `dt` clamped to 100 ms, `dt = 0` is a no-op, ~90 % of the way in 130 ms |

Backoff measured against an actually blocked line (`api.binance.com` blocked,
first via hosts, then via a firewall rule against the resolved IP):

| Failure no. | Wait measured | Expected ±12.5 % | `hConnect` |
|---|---|---|---|
| 1 | 6 491 ms | 5 250–6 750 | kept |
| 2 | 10 770 ms | 10 500–13 500 | kept |
| 3 | 21 470 ms | 21 000–27 000 | **released** |
| 4 | 52 541 ms | 42 000–54 000 | new |
| 5 | 52 617 ms | 52 500–60 000 (clamped) | new |
| 6 | 59 264 ms | 52 500–60 000 | new |
| 7 | 60 000 ms | cap | new |

At the cap the jitter is one-sided — the clamping cuts everything above
60 000 ms. When the line is restored: `feil=0` and the 3 s cadence back on the
first successful call.

**The animation timer at rest.** A separate counter on `WM_TIMER`, logged
together with the network cycles:

| State | Ticks |
|---|---|
| Panel opened, everything settled | 14, then flat |
| Panel open + offline | 624 → 1 527 (~35 ticks/s, drives the seconds counter) |
| **Panel hidden + offline, 54 s** | **2 124 → 2 124 (zero movement)** |
| Line restored | 2 124, still flat |

- **Footprint after part A:** GDI 30 before the panel opens, 31 after — same
  level as phase 1. USER 14. Private bytes 3.98–4.04 MB. CPU at rest 312 ms per
  30 s (~1 %), which is the fetch every 3 seconds, not the timer.
- **`/W4` clean, x86** (PE machine type `0x14C`, verified on the built exe).

### Phase 2 part B

Unit tests: **24/24** in `test_b`, plus part A's **17/17** — both run
against code extracted from the current `ticker.c`.

| Unit | What was verified |
|---|---|
| `FormatSpan` | all six intervals × representative `vc`, including `vc = 0` and the day rollover |
| `FormatIconPrice` | fifteen price ranges from $0.85 to $12.5 M all give `IconTextWidth() <= 16` |
| `OverlayLayout` | pure function, no overlap, everything inside the panel, corners and midpoint hit the right row, holds at minimum size |
| `PriceDecimals` | five labels are always mutually distinct, six span classes |

**The race (`configGen`).** Measured on an instrumented build that logs every
discard, with 80–100 rapid symbol switches:

| Branch | Discards | Example |
|---|---|---|
| Candles | 30 | `gen=1 naa=3` — two switches managed to happen during one fetch |
| Price | 56 | `gen=1 naa=2 pris=2407.47` — an **ETH** price that arrived after the switch |

The price branch had to be forced in the test build (the thread fetches candles as long as the panel
is open). The log is the concrete proof of the bug the design would have let
through: without the check, `lastPrice = 2407.47` would have been written and the tray icon
would have shown the ETH price under another symbol.

**The watermark, measured with `GetPixel` — not by eye:**

| Color | What | Pixel count |
|---|---|---|
| `#0D1117` | plain background | 2481 |
| `#15191F` | the watermark | 318 |

Exactly `CLR_WATERMARK`. A difference of 8 levels = 3.1 % of full scale.

**Painting, `BitBlt` vs. `FillRect`.** Alternating every other frame — same
data, same window, same view, 306 pairs:

| | Median | Mean |
|---|---|---|
| `BitBlt` (with watermark) | 0.341 ms | 0.355 ms |
| `FillRect` (without) | 0.312 ms | 0.332 ms |

Paired difference **+0.0226 ms**, 95 % CI `[0,0062, 0,0390]`, t = 2.70.

> The design claimed that the cached `BitBlt` "is no more expensive than the current
> `FillRect`". That is not true — it is measurably more expensive. The difference is
> real but small: 0.023 ms of a 0.34 ms repaint, versus 0.05–0.30 ms
> to redraw the text every frame. The cache is still the right choice;
> the claim was too strong.

**Handles.** GDI 33 through 30 overlay openings and 20 resizes; 35 after
the watermark is built (+1 `HBITMAP`, +1 `HFONT`, as the design predicted), and
then flat through 16 symbol switches. USER 14 at rest — 15 while
the animation timer runs, because **a timer is a USER object**.

**The registry, round trip on a real run.** Selected SOL + 520×380, quit,
restarted: the tray icon read `97.5` **before the panel was opened** — so
the first fetch went to SOL. The panel opened at 520×380 with the watermark
`SOLUSDT` / `1m`.

| Error path | Result |
|---|---|
| `SymbolIndex=99`, `IntervalIndex=0x7FFFFFFF` | BTC/USDT 1m, no crash |
| `SymbolIndex` as `REG_SZ` | BTC/USDT 1m, no crash |
| No key | BTC/USDT 1m, no crash |

### Phase 2 part C

Unit tests: **55/55** in total (17 part A + 24 part B + 14 part C), all run against
code extracted from the current `ticker.c`.

**Why the snap is a quarter pixel and not a fixed number.** Measured in the harness:

| Threshold | Panning 300 candles | SOL span ($1.5) | BTC span ($3000) |
|---|---|---|---|
| Fixed 0.01 candle | 46 ticks (736 ms) | — | — |
| Fixed 0.5 dollar | — | 5 ticks | 39 ticks |
| **Quarter pixel** | **33 ticks (528 ms)** | **32 ticks** | **32 ticks** |

The fixed price threshold is not just slow, it is useless: 0.5 dollar is
a third of SOL's whole span and under a thousandth of BTC's. `tau` is 70, not
110 as the plan proposed — 110 gave a tail of over a second.

**Crosshair vs. drawing, mid-animation.** Instrumented build that logs
which candle `HitCandle` picked, and which one the draw loop's own formula gives for
the same X:

| | |
|---|---|
| Agreement | **163 of 163, 0 mismatches** |
| Of which frames mid-animation | **43** (`dCount` 9.912 → 9.864 → … → 8.000) |

**Painting during animation**, driven by wheel panning:

| | |
|---|---|
| Median | 0.462 ms |
| p95 | 0.637 ms |
| Max | 0.864 ms |
| **Frames over 1.3 ms** | **0 of 581** |
| Frames with fractional `dispStart` | 415 of 581 |

**The last-price indicator**, measured with and without the block every other frame,
351 pairs:

| | Median | Mean |
|---|---|---|
| With | 0.2713 ms | 0.2828 ms |
| Without | 0.2489 ms | 0.2628 ms |

Paired **+0.0195 ms**, 95 % CI `[0,0134, 0,0255]`, t = 6.28. The specification
estimated "under 0.001 ms" — the actual number is about twenty times higher, and
most of it is `DrawTextW` for the stamp text. Still unproblematic.

The stamp measured with `GetPixel`: 16 px tall in exact `CLR_DOWN` `#FF4966`,
the text with its core in exact `CLR_BG` `#0D1117` and 13 % coverage.

**Symbol switch and the Y axis.** Switch SOL → BNB: `candleCount` goes 302 → 0 → 300,
and **the first frame with data already has the right axis** (713.46–714.77). No
intermediate values.

**Zoom anchoring after easing:** the same candle (index 297) under the pointer before and
after five notches plus settling.

**Handles:** GDI **35**, USER 14. Part C adds no GDI objects; C1 did
(two dashed pens, 33 → 35), and the watermark font is rebuilt on every
size change without leaking — verified flat through 20 resizes.

### Phase 3 — native window

All measured on a real window at 3840×1600:

| Action | Result |
|---|---|
| First open, empty registry | centered `1730,626` 380×300 |
| `SC_MAXIMIZE` | `-8,-8` 3856×1568, `IsZoomed` |
| `SC_RESTORE` | back to `1730,626` 380×300 |
| `SC_MINIMIZE` + tray click | minimized, then restored |
| `Ctrl`+`0` (**real** keystroke) | `120,90` 700×520 → `1730,626` 380×300 |
| The tray menu's "Default view" | same |
| `WM_CLOSE` | hidden; `X=450 Y=320 W=560 H=420` in the registry |
| Restart | reopened at `450,320` 560×420 |
| Quit via the tray menu | 0 leftover windows from the old PID |
| Real `ESC` | hides the window |
| `GetGUIThreadInfo` | `hwndActive == hwndFocus ==` the panel — bug #2 is not back |

**Handles: GDI 35 → 29.** The six cached chrome objects are gone. USER 14.

> Performance in `WM_PAINT` is unchanged — nothing is added to the draw loop, and
> `DrawChrome` is removed from it. The specification's "0.000 ms" is right for
> *this* change, unlike the estimates for the last-price line and
> `BitBlt`.

---

### Phase 4 — borderless window with its own control buttons

**The button drawing** (`QueryPerformanceCounter` around *only* `DrawButtons`, in an
instrumented build with its own mutex name and its own window class — pitfall 10).
Over 1500 repaints per round, driven by continuous zoom:

| Round | Min | Median | Mean | Max |
|---|---|---|---|---|
| Pointer away (the 99 % case) | 0.0126 ms | **0.0153 ms** | 0.0158 ms | 0.1014 ms |
| Pointer on a button (`FillRect` included) | 0.0119 ms | **0.0139 ms** | 0.0143 ms | 0.0873 ms |

The QPC pair's own cost was measured at under 0.00001 ms (fastest of 1000 empty
pairs) and is therefore not a factor.

> **The mandate's budget was < 0.003 ms. It is not met — measured is ~0.015 ms,
> that is five times over.** 3 µs corresponds to two to four GDI calls on a memory DC;
> the four buttons are about ten, plus a `FillRect` on hover. The number stands as
> it is instead of being rounded away.
>
> In context: a full repaint at 1280×720 takes **0.853 ms** (median),
> so the buttons are **1.8 %** of it. The fallback — a pre-drawn row as
> `BitBlt` — is under "Rejected proposals" with the reasoning.

**The whole `PaintPopup` at 1280×720**, same method as the table above:

| | Min | Median | Mean | Max |
|---|---|---|---|---|
| `PaintPopup`, 1280×720 | 0.5367 ms | 0.8527 ms | 0.8988 ms | 5.9505 ms |

Not comparable with the 1.255 ms further up: that measurement was on a
380×300 panel. The surface here is 8× larger, and the repaint is still faster,
because the view is the same number of candles spread over more pixels.

**Handles: GDI 27 → 31** for the four new objects (three pens and a red
close background; the hover background reuses `brBox`). Measured on the same build before and
after, not assumed. The log's earlier 29 applied to the build with the OS frame —
removing the frame took it 29 → 27. USER 14, unchanged.

**Stress test:** 2 × 20 s, ~3.1 million operations per round, with ~79 000
real mouse jiggles *on* the close cross, so the hover path with `FillRect` was part of the load.
No deadlock. Handles 31/14 before and after both rounds; see pitfall 27 on
why they sit at 34/15 *while* running.

**Square corners:** all four 8×8 blocks in the corners are pure
`#0D1117`, read with `CopyFromScreen` — DWM rounding is a compositor effect
and does *not* exist in `PrintWindow` output, so it must be read from the screen.
So `DWMWCP_DONOTROUND` works.

> The first measurement showed light pixels in the two left corners. That was not
> rounding — a quarter circle hits all four equally — but another window that
> lay on top of the panel. The panel is not `WS_EX_TOPMOST`. `BringWindowToTop`
> first, then measure.

**Hover goes off during panning.** The chart surface holds the mouse button via
`SetCapture`, so a drag that passes over the header would otherwise
light the close cross red mid-pan — without it being possible to click it.
Measured `#0D1117` during the drag and `#C02A3E` as soon as the button is released.

**Hit zones:** 22/22, both normal and maximized, queried directly with
`SendMessage(WM_NCHITTEST)`. The boundaries are pixel-exact: `y=5` gives `HTTOP`
and `y=6` gives `HTCLIENT` — the button row begins exactly where `RESIZE_BORDER`
ends.

---

### Phase 5 — glyph, incremental hover and pan cursor

**Hover painting**, measured in an instrumented build that logs which branch in
`PaintPopup` was taken, and `rcPaint` too:

| Branch | `rcPaint` | Min | Median | Mean | Max |
|---|---|---|---|---|---|
| `STRIP` (hover only) | `1162,6,1272,24` | 0.0376 ms | **0.0616 ms** | 0.0654 ms | 0.1544 ms |
| `full` (hover during animation) | `0,0,1280,720` | 0.6487 ms | 0.9787 ms | 1.0639 ms | 6.3758 ms |

`rcPaint` is **exactly** the button row at 1280 wide: `W−118, 6, W−8, 24`.
Hover painting is thus **16× cheaper** than before. The second row is
the proof that the animation union works: with the timer running, `rcPaint` becomes the whole
client area, and we fall to the slow path on our own.

**The fast path gives a pixel-identical result, in both window states.** The whole
button row (110×18 = 1980 pixels) read after a forced full repaint,
then 24–32 fast-path repaints, then read again: **0 mismatches of 1980** both
at 1280×720 (strip `1162..1272`) and maximized at 3840×1552 (strip
`3722..3832`, where the watermark bitmap has just been rebuilt). A fast path that
draws *almost* the same is worse than none.

And the fast path draws the right glyph: hover on the minimize button while the window is
maximized gave `❐` in the maximize button, not `□`, with minimize lit up in
`#161D27`.

**The glyph**, as a pixel grid around the button center `(W−49, 15)`:

```
     normal                   maksimert
   .............           .............
   .............           .............
   ..#########..           ....#######..
   ..#.......#..           ....#.....#..
   ..#.......#..           ..#######.#..
   ..#.......#..           ..#.....#.#..
   ..#.......#..           ..#.....#.#..
   ..#.......#..           ..#.....#.#..
   ..#.......#..           ..#.....###..
   ..#.......#..           ..#.....#....
   ..#########..           ..#######....
   .............           .............
   .............           .............
```

> The grid above is after phase 6: two 7×7 rectangles offset by **2 px**.
> Phase 5 drew two 6×6 offset by 3 px.

No lines through the front rectangle: the back one is drawn as an open
polyline, not as a full rectangle.

Verified both with `SC_MAXIMIZE`/`SC_RESTORE` and with real mouse clicks.

**The pointer**, measured with `GetCursorInfo`, 11/11: `IDC_ARROW` at rest,
`IDC_SIZEALL` mid-drag, `IDC_ARROW` after release, and all six
resize pointers at the edges untouched.

**Budgets.** GDI 31 / USER 14 at rest, unchanged — also after a 20 s
stress test with ~3.2 million operations and ~79 000 real mouse jiggles *on* a
button, which is the fast path's heaviest load. Private bytes 3.53 MB, exe 164 KB,
`/W4` clean, x86. All unchanged from before the round.

**Regression:** 28/28 unit tests, 22/22 hit zones, four button clicks with a real
mouse, 11/11 hover colors, panning, zoom, crosshair, overlay, ESC, tray,
resizing — all green.

### Phase 6 — clipping the chart and polishing the glyph

The mandate came from a screenshot in the maximized state: candles bleeding into
the Y axis margin or the header, an unclear `❐`, and a requirement of at least 32 px
clearance above the chart.

**The bleed was real, but 1 px, and only while the chart is moving.** A
still frame with `PrintWindow` at 3840×1552 showed nothing. It was found
with a stress probe: posted `WM_MOUSEWHEEL` (panning and Ctrl zoom), captured
with `PrintWindow` 15/30/45/90 ms after each notch, that is mid-easing where
`dStart` is fractional and half candles lie at the edges. Each frame is scanned for
exact `CLR_UP`/`CLR_DOWN` in five regions outside `rcChart`: the column
`x = right`, the axis margin `x > right` (excluding the stamp rows), the band
`y ∈ [31, 44)`, everything below `bottom`, and the left margin.

| Build | State | Frames | Frames with candle pixels outside | Pixels |
|---|---|---|---|---|
| before | 3840×1552 | 240 | **21** — all in the column `x = right` | 1819 |
| after | 3840×1552 | 240 + 480 | **0** | 0 |
| after | 1030×581 | 480 | **0** | 0 |

The cause was `IntersectClipRect(..., right + 1, bottom + 1)`. The column
`x = right` belongs to the axis margin — the grid ends at `right − 1` — but
the clip let candle bodies through there. The top and bottom bands were never affected.

**Changed in `DrawChart`:**
- `RECT rcChart = { left, top, right, bottom + 1 }` is set with
  `IntersectClipRect` **before the grid**, not just before the candles.
- The right edge is exclusive. The bottom is **inclusive** on purpose: grid line
  `i = 4` and the wick of the lowest price both lie at `y = bottom`, and a
  `[top, bottom)` clip would have erased the bottom line.
- The price labels are split out into their own loop after `SelectClipRgn(NULL)`.
  Had they stayed in the grid loop, the clip would have cut them.

**Changed in `DrawButtons`:** the button area is **always** filled before the vectors —
`brBg` at rest, `brBox`/`brClose` on hover. The pens were already cosmetic
1 px `PS_SOLID` (`CreatePen(PS_SOLID, 1, …)`) and are untouched. The restore
glyph is two 7×7 rectangles offset by 2 px within the same 9×9 footprint,
still with the back one as an open polyline. See the grid under Phase 5.

**Top clearance:** `rcChart.top = HEADER_H = 44` already met the requirement.
It is now enforced at compile time: `CHART_TOP_MIN 32` and
`#if HEADER_H < CHART_TOP_MIN #error`.

**The fast path is still pixel-identical** after `FillRect` came into the
shared `DrawButtons`: the button row read from the screen after a forced full
repaint, then after 24 hover-in/hover-out: **0 mismatches of 1980** in both
states, with hover actually lit 4/4 maximized. Restored, it lit
only 1/4 and 3/4 — the user was active at the machine during the measurement and moved
both pointer and window, so that row is weaker evidence than the maximized one.

`/W4` clean, x86, 168 KB exe.

### Phase 7 — measured header layout and minimum size 400×250

**The bug, measured before the change.** At 300×200, price and percentage merged
into one patch of ink: `$75534.98-0.27%  (5t 1m)`. They shared one rectangle,
left- and right-aligned, and `DrawTextW` clips against the rectangle, not against
the neighboring text. Ink clusters in the row y 6–27, with merging under 8 px:
`[11-173]` where there should have been two.

**Changed in `DrawChart`.** Each header text is measured with
`GetTextExtentPoint32W` on the fully formatted string in its own font.
The collision rule is the pure function
`HeaderFits(rightBound, leftBound) = rightBound < leftBound − HDR_GAP`, with
`HDR_GAP` = 8.
- **Row 1:** the price sits on the left, with right bound `PAD_L + bredde`.
  The percentage sits on the right and ends at `ButtonStrip().left − 8`. It
  is tried in three steps: full (`−0,27 %  (5t 1m)`), short (`−0,27 %`) and hidden.
  It is never cut in the middle of a number. The short form is only measured when the full one
  does not fit. The price's rectangle also ends at the button row.
- **Row 2:** the symbol line is bounded by the price axis's top label
  (`right + 4 − 8`), not by the buttons, which end at y = 24. It gets
  `DT_END_ELLIPSIS` when the measured width does not fit.
- `BTN_STRIP_W` is removed. The button row's edge is read from `ButtonStrip`.

**`WM_GETMINMAXINFO`:** `ptMinTrackSize` =
`MulDiv(400|250, GetDpiForWindow, 96)`. `SetWindowPos` enforces it too:
an attempt at 300×200 gave **400×250**. A saved registry with a smaller size
is therefore clamped on its own.

**Verified in a test build** (pitfall 10: its own mutex name, its own classes, its own
registry key), driven by `PrintWindow`:

| Size | Row 1, ink clusters | Percentage | Clip stress |
|---|---|---|---|
| 1280×720 | price `11-103`, percentage `1093-1153`, buttons from `1171` | full | 0/80 |
| 3840×1552 | price `11-102`, percentage `3634-3713`, buttons from `3731` | full | 0/80 |
| 400×250 | price `11-102`, percentage `218-273`, buttons from `291` | full | 0/80 |
| 300×200 attempted | clamped to 400×250 | full | 0/80 |

The steps cannot be reached above 400 px. They were therefore run in a separate build with
`POPUP_MIN_W` 200:

| Width | Price | Percentage | Gap to next |
|---|---|---|---|
| 340 | `11-100` | full `153-213` | 52 / 17 px |
| 310 | `11-100` | full `117-183` | 16 / 17 px |
| 280 | `11-103` | **short** `120-153` | 16 / 17 px |
| 250 | `11-103` | **hidden** | 37 px to the buttons |

**Painting.** QPC around `DrawChart` and the whole slow path, and around
the header block alone. The same markers in old and new code. Two passes, with
old and new alternating, driven only by wheel notches with no simultaneous screen capture.
The numbers are from the second pass:

| | Header, median | Whole frame, median | p95 |
|---|---|---|---|
| before, 1280×720 | 124 µs | 0.820 ms | 1.275 ms |
| **after, 1280×720** | **192 µs** | **0.871 ms** | 1.404 ms |
| before, 3840×1552 | 129 µs | 9.281 ms | 10.667 ms |
| **after, 3840×1552** | **221 µs** | **9.988 ms** | 10.800 ms |
| before, 400×250 | 115 µs | 0.952 ms | 1.243 ms |
| **after, 400×250** | **187 µs** | **1.011 ms** | 1.328 ms |

> **The mandate's budget was < 0.46 ms. It is not met, and it was not met
> before the change either.** The header layout costs **~70 µs** extra, mainly
> three `GetTextExtentPoint32W`. The whole frame was already 0.82 ms at 1280×720
> and 9.3 ms maximized. In the first pass, before the short measurement was made lazy,
> the header overhead was ~95 µs. Maximized, the whole frame varies by ±0.7 ms
> between passes, so that row does not show the header overhead.

`/W4` clean, x86, 169 KB exe.

### Phase 8 — `[ + ]`, multiple instances and reset on double-click


**`[ ↺ ]` is gone.** It reset the *window geometry* — `Ctrl`+`0` and the
tray menu still do that. Its place is taken by `[ + ]` in the same
enum position (`BTN_RESET` → `BTN_NEW`), so `ButtonLayout`, `ButtonHit`,
`ButtonStrip`, `WM_NCHITTEST` and hover read the same four rectangles as
before. The glyph is a 7×7 plus sign, two `LineTo` with an exclusive end point
(`cx−3 → cx+4`), versus `Arc` and three strokes before.

**Resetting zoom and panning** is `ResetView`: the last 300 candles,
pinned to the right edge, `followLive`. It does not touch `dispValid`, so the view
eases back as with wheel zoom. `TogglePopup` uses the same function and sets
`dispValid = FALSE` itself, for a snap on open. Three ways in:

- **Double-click** in `[g.left, W) × [g.top, g.bottom]` — the chart and the axis margin.
  Requires `CS_DBLCLKS` on `BTCPopupClass`. Everything else — the buttons, and the whole panel
  while the overlay is open — falls through to `WM_LBUTTONDOWN`, so the second click
  of a quick double-click on `–`/`□`/`×` behaves as before. The button click
  is therefore extracted into `OnButtonClick`. Free header space is `HTCAPTION` and
  still maximizes.
- **`R`**, not while the overlay is open.
- **`ESC`, layered:** overlay open → close. Otherwise, `!ViewIsDefault` →
  reset. Otherwise → `HidePanel`.

**Multiple instances.** The mutex is removed. `SpawnInstance` reads
`GetWindowRect` (or `rcNormalPosition` when maximized), adds 30 px and
starts `ticker.exe --dup x y w h sym iv` with `CreateProcessW`. The command line
lives in a writable buffer. If the window goes past the work area's right or
bottom edge, it cascades back to the corner — otherwise the button row would
end up off screen after a few clicks. The child validates the arguments with
the same bounds as `LoadConfig`, sets `g_isDuplicate` and opens the panel itself
once the lock and the events exist. A **duplicate** writes nothing to
the registry (`SaveConfig` and `SaveGeometry` return early) and exits
the process via the tray menu's own path when the panel is closed (`HidePanel`).

**Deviation during execution: the first open showed 8 candles, not 300.** Found because
the probe used a freshly opened panel as the reference. `TogglePopup` sets `viewCount = 0`
when the buffer is empty, and `MergeCandles` called `ClampView`, which clamps 0 up
to `MIN_VIEW` = 8, *before* `WorkerFetchKlines` got to its own "0 →
`DEFAULT_VIEW`". The old build (`9e64dcb`) also showed "(8m)" on first
open, and the same applied after a symbol switch. The bug is from phase 1, but every
duplicate opens precisely before it has data. Fixed in `MergeCandles`: when
the view follows live and is not set, it becomes the default view there, before
the clamping. Separate commit.

**Verified** with a probe that drives the real pointer and reads with
`PrintWindow`, with the panel set `HWND_TOPMOST` (pitfall 30). Three full
passes: 26/31 (before the fix for the 8-candle bug, with a wrong reference), 30/31 and
29/31. All reds in the last two are hover frames that arrived too late in the normal
state right after opening — see the latency measurement below the table. Every claim is
green in at least one full pass, and the disputed ones were run in isolation:

| Test | Result |
|---|---|
| Hover on every edge pixel of all four buttons, and one outside each edge | 32/32 normal (1037×678), 32/32 maximized (3840×1552) |
| Fast path vs. full repaint, 5 hover states × 6 rounds | **0 mismatches** in 30 pairs (1980 px each) |
| Glyph at rest and on hover | 13 pixels, exactly symmetric 7×7, both states |
| Real double-click on the chart / on the price axis, `R`, `ESC` layer 2 | view back, **0.00 %** deviation from the reference (zoomed: 6.45 %) |
| `ESC` layer 1 / layer 3 | closes the overlay and keeps the zoom / hides the panel |
| `WM_LBUTTONDBLCLK` on `–` | still minimizes |
| Real double-click in free header space | still maximizes |
| Real click on `[ + ]` | new process with visible panel in **~285 ms**, exactly +30, +30, same size, in the foreground |
| `×` in the duplicate | the process exits, the registry **unchanged**, the main instance lives on |
| `[ + ]` near the bottom right corner | the duplicate lands in the work area's corner (0, 0) |
| `ESC` in the default view in a duplicate | the process exits |
| Handles at rest | GDI 31 / USER 14, same as the old build measured the same way |

> **The reds were delay, not drawing bugs.** The fast-path comparison
> differed twice by exactly 468 px = one whole button: the two frames had
> different hover states, not different pixels. In isolation, with the state checked in
> both frames, the same test gave 0 mismatches in 30 pairs (pitfall 31). The glyph test on
> hover failed once for the same reason.
>
> **Latency from `SetCursorPos` to the correct hover frame**, 40 changes per
> run, old and new build alternating, 2.5 s after opening:
>
> | Build | Median | Outliers (> 100 ms) |
> |---|---|---|
> | old (`9e64dcb`) | 24 ms | **6 of 40**, all ~2 s, wrong state at the timeout |
> | new | 29 ms | 0 |
> | old | 25 ms | 0 |
> | new | 24 ms | 1 of 40, 1.76 s, right in the end |
>
> The outliers exist in both builds and were not introduced in phase 8. The cause has not been
> investigated. Candidates are `PrintWindow`/DWM and the real pointer competing
> with `TrackMouseEvent` (pitfall 35).

**Painting.** QPC around `DrawButtons` in both paths, around the whole
fast path (DC, bitmap, blit and buttons, without `BeginPaint`/`EndPaint`) and
around the header text in `DrawChart`. The same markers inserted by script in
old (`9e64dcb`) and new code. Three rounds, old and new alternating, 1037×678,
each round 5 s of hover jiggling and 5 s of `RedrawWindow`, no screen capture
along the way:

| | Before, median | **After, median** | After, min | After, p90 |
|---|---|---|---|---|
| `DrawButtons`, hover fast path | 0.0292 ms | **0.0210 ms** | 0.0124 ms | 0.0237 ms |
| `DrawButtons`, full repaint | 0.0252 ms | **0.0168 ms** | 0.0115 ms | 0.0252 ms |
| Fast path total | 0.0958 ms | **0.0835 ms** | 0.0419 ms | 0.0968 ms |
| Header text | 0.1686 ms | 0.1402 ms | 0.0743 ms | 0.2129 ms |
| **The whole header** (text + buttons, same frame) | 0.1956 ms | **0.1586 ms** | 0.0896 ms | 0.2295 ms |

> **The mandate's budget was < 0.02 ms for the whole header. It is not met, and
> it was not met before the change either.** The vector drawing alone sits
> around the budget: median 0.017–0.021 ms. The plus sign saved ~8 µs versus
> the circular arrow, consistently in all three rounds. The header text is unchanged code;
> the difference in that row is noise between rounds (round medians 107–239 µs).
> The fast path is dominated by `CreateCompatibleDC`/`CreateCompatibleBitmap` and
> the blit, not by the buttons. See "Pre-drawn button row" under
> *Rejected proposals* for what a literal budget would have required.

`/W4` clean, x86, 174 KB exe.

### Phase 9 — desktop mode (`--desktop-mode`)

Developed on
the branch `desktop-mode` (three commits) and merged with `--no-ff`.

**What it does.** `ticker.exe --desktop-mode` (the only argument) starts without
a panel in the notification area and puts the chart area into the desktop: a
child of WorkerW, below `SHELLDLL_DefView` (the icons), covering the whole
primary monitor. Mouse and keyboard go to the desktop. Same `BTCPopupClass`,
`PopupProc` and `PaintPopup` as the panel. Only the creation differs:

- `TogglePopup` creates the window with `WS_POPUP` alone and calls
  `AttachToDesktop` *before* `hPopup` is published. No `SquareCorners`,
  `PlacePopupInitially` or `ForceForeground`. `SW_SHOWNA`.
- `FindDesktopWorkerW` sends `0x052C` (`0xD,1` and `0,0`) with
  `SendMessageTimeoutW`, 1 s and `SMTO_ABORTIFHUNG`. It takes the WorkerW that
  is a child of Progman (24H2), and otherwise the classic sibling WorkerW via
  `EnumWindows`.
- `AttachToDesktop`: `WS_POPUP` → `WS_CHILD`, `SetParent`, **then**
  `WS_EX_LAYERED | WS_EX_TRANSPARENT` and `SetLayeredWindowAttributes(255,
  LWA_ALPHA)`, and finally `SetWindowPos(HWND_BOTTOM)` with
  `SM_CXSCREEN`×`SM_CYSCREEN`. The origin is converted with `MapWindowPoints`.
- `WM_NCHITTEST` → `HTTRANSPARENT`. `DrawButtons` is skipped.
- `SaveGeometry` writes nothing. Tray: left-click does nothing, and the
  menu has only "Quit".
- `WM_NCDESTROY` in `PopupProc` clears `hPopup` and `animRunning` if the surface
  disappears without us asking for it, and starts `TIMER_EMBED_ID` (250 ms).
  `TaskbarCreated` adds the icon again (in both modes) and rebuilds the
  surface, but only if it is not already sitting in the current WorkerW.

**The manifest is what makes the surface visible.** The preliminary
investigation is in the plan. In short: on 26100 a plain GDI child of WorkerW
(or Progman) never becomes visible, because Progman has
`WS_EX_NOREDIRECTIONBITMAP`. A layered child becomes visible, but only when
**both** of these hold: `supportedOS` Windows 8+ in the manifest (without it the
ex-style is 0), and `SetLayeredWindowAttributes` called *after* `SetParent`.
So `WS_EX_LAYERED` is not a choice between two ways of letting the mouse
through. Without it nothing is shown.

**The shell tree, verified** with a probe that reads the window tree from the
outside (`EnumChildWindows`, `GetParent`, `GetWindow`, `GetWindowLong`,
`GetLayeredWindowAttributes`, `WindowFromPoint`):

| Check | Result |
|---|---|
| Parent | `WorkerW` (Explorer pid) |
| WorkerW's parent / the surface's root | Progman / Progman |
| Z-order in Progman | `SHELLDLL_DefView` > `WorkerW` |
| Z-order in WorkerW | the surface is the last child (`HWND_BOTTOM`) |
| Style / ex-style | `0x54000000` (`WS_CHILD`, no `WS_POPUP`/`WS_THICKFRAME`) / `0x00080020` (`LAYERED`, `TRANSPARENT`) |
| Layer | alpha 255, `LWA_ALPHA` |
| Rect | 0,0 3840×1600 = `SM_CXSCREEN`×`SM_CYSCREEN` |
| Visible top-level windows (button on the taskbar) | 0 |
| `WindowFromPoint` on 27 visible desktop points | 27× `SysListView32`, **0** hits on the surface |
| `WM_NCHITTEST` on header, close cross, left edge and chart | −1/−1/−1/−1 (`HTTRANSPARENT`) |
| The button row | one color, no glyphs |
| Screenshot | candles visible between and behind the icons, price at top left |
| Quit via the tray path | process gone, wallpaper back, no leftovers |
| Real Explorer restart (after `fea77f5`) | new surface in new WorkerW after **566 ms**, stays, 14/14 green, tray icon back (`Shell_NotifyIconGetRect` = `S_OK`) |

**Explorer restart**, run with the user's go-ahead: `Stop-Process -Force`
on explorer.exe while `--desktop-mode` was running, and `AutoRestartShell = 1`
started it again. A probe followed the window tree every 100 ms:

| Time | 1st run (`1ba56bd`, retry 1 s) | 2nd run (`365ea4c`, retry 1 s) | 3rd run (`fea77f5`, retry 250 ms) |
|---|---|---|---|
| ~20 ms | old surface **gone** (`IsWindow` false), process alive | same | same |
| ~160 ms | new explorer.exe | same | same |
| 285–480 ms | new Progman | same | same |
| 285 ms | — | — | new surface created, waiting on `0x052C` |
| 566 ms | — | — | **surface in new WorkerW, stays** |
| ~1.1 s | new surface in new WorkerW (the timer) | new surface in new WorkerW, stays | — |
| ~1.66 s | surface **torn down again** | — | — |
| ~2.68 s | new surface again | — | — |

> 3rd run: `SendMessageTimeoutW` to a brand-new Progman took ~280 ms before
> WorkerW existed. The UI thread stalls that long, within the 1 s cap. The
> window is not visible until it sits in WorkerW.

**Panel mode during an Explorer restart** (main instance with the panel open,
`Shell_NotifyIconGetRect` for the icon):

| Build | Process | Panel | Tray icon before → after |
|---|---|---|---|
| master `7bdd604` | alive | alive | `S_OK` → **`E_FAIL`, for good** |
| master `e6c4ea6` | alive | alive | `S_OK` → `S_OK` |

> A build from before phase 9 **does not die** when Explorer restarts, but it
> loses the tray icon, and with it "Quit". The user's instance (pid 30852)
> was gone after the first restart, and the event log shows no crash. Since
> the old build survived the restart in the test, it was probably ended some
> other way, but that is not proven. UI Automation did not find the icon of a
> new main instance even *before* a restart. Use `Shell_NotifyIconGetRect`.

> **Windows tears down a child from another process when the parent dies.**
> So `WM_NCDESTROY` is the path actually used, and the timer rebuilt the
> surface before `TaskbarCreated` arrived. In the first run `TaskbarCreated`
> tore down the fresh surface, and the desktop was without a chart for one more
> second. Fixed in `365ea4c` (own branch, `desktop-mode-explorer-restart`): the
> surface is now torn down only if the parent is not the current WorkerW. After
> the restart UI Automation found `BTC/USDT: $76274.09` in `Shell_TrayWnd`,
> where no other `ticker.exe` was running. After the fix a posted
> `TaskbarCreated` would no longer tear anything down, so the earlier test with
> a posted message has not been rerun.

**Panel mode with the manifest**, new build against master, same `--dup`
geometry: style `0x94070000` and ex-style `0x00000100` in both. Top-level, same
rect, same hit test (`HTCAPTION`/`HTCLIENT`/`HTLEFT`/`HTCLIENT`), and the button
row **0 differences out of 3600 pixels** with `PrintWindow`.

**Painting.** The same QPC markers were inserted by script into master
(`7bdd604`) and into the new build. Total is from before `BeginPaint` to after
`EndPaint` on the slow path. Drawing is from `CreateCompatibleDC` to
`DeleteDC` and is 0.03–0.06 ms below total in every row. Desktop mode at
1280×720 is a measurement build where `DM_W`/`DM_H` override the surface's
size, so the embedding can be compared at the same pixel count. Four rounds,
six configurations alternating. Each round: 5 s after start, then 5 s of
`InvalidateRect` every 16 ms from another process. No screen capture
meanwhile:

| Config | n | **Median** | Min | p90 | Round medians |
|---|---|---|---|---|---|
| master, panel 1280×720 | 838 | **1.94 ms** | 0.92 | 2.68 | 1.79 / 2.11 / 1.83 / 2.29 |
| new, panel 1280×720 | 795 | **1.89 ms** | 0.82 | 2.60 | 2.09 / 1.89 / 1.94 / 1.84 |
| new, desktop 1280×720 | 705 | **2.24 ms** | 0.79 | 2.70 | 1.85 / 2.40 / 1.91 / 2.44 |
| master, panel 3840×1600 | 741 | **13.36 ms** | 10.79 | 14.87 | 12.86 / 13.66 / 13.51 / 13.14 |
| new, panel 3840×1600 | 925 | **13.46 ms** | 10.67 | 14.86 | 12.58 / 13.29 / 14.56 / 13.10 |
| new, desktop 3840×1600 | 705 | **13.49 ms** | 10.68 | 14.74 | 13.40 / 12.61 / 13.72 / 14.05 |

> **The mandate's < 0.85 ms is not met in any mode, and unchanged master does
> not meet it today either.** The number in *Known limitations* (~0.85 ms at
> 1280×720) was measured under other conditions. The machine was in normal use,
> and the user had their own `ticker.exe` running: four when the work started,
> one at the end. Only the fastest single frames are under
> 0.85 ms. **The embedding costs nothing measurable at full size:** 13.49 vs
> 13.46 ms, with round medians that overlap. At 1280×720 desktop mode is
> 0.35 ms slower in median, but here too the round medians overlap
> (1.85–2.44 vs 1.84–2.09). What actually costs is the size: 6.9 times as many
> pixels gives ~7 times the time. Previously measured maximized: ~9.5 ms at
> 3840×1552.

**Memory and handles at rest.** Non-instrumented builds. Measured 15 s after
start, then five readings 2 s apart, two rounds.
`PrivateMemorySize64` and `GetGuiResources`:

| Config | Private bytes | GDI | USER | Kernel handles |
|---|---|---|---|---|
| master, panel 1280×720 | 3.86–4.00 MB | 33–38 | 14–18 | 366–370 |
| **new, panel 1280×720** | **3.59 MB** (all 10) | **31** (all 10) | **14** (all 10) | 358–360 |
| **new, desktop 3840×1600** | **3.36–3.42 MB** | **30** (all 10) | **6** (all 10) | 323 |

> **Desktop mode is below the mandate's ~3.53 MB and 31/14.** Panel mode
> is at 3.59 MB, like "~3.6 MB" under *Build*. Master's rows are **not at
> rest**: GDI 33–38 and USER up to 18 are the count in the middle of a repaint
> (pitfall 27). The open panel got hover traffic during the measurement, so
> those numbers are not compared. That a 3840×1600 surface uses no more private
> memory than the panel fits with the double buffer being created and deleted
> in every frame, and with compatible bitmaps not being counted in the
> process's private bytes. The watermark bitmap is the only thing that lives
> between frames. USER 6 vs 14: the surface gets no mouse messages, and there is
> neither cursor nor hover state to hold on to.

**Scaling above 100 %** (`6699c3c`, own branch `desktop-mode-dpi`), run with
the user's go-ahead. The scaling on the primary monitor was changed with
`DisplayConfigSetDeviceInfo` (type −4, recommended 100 %) and set back to
100 % in a `finally`. The probe is per-monitor aware and counts physical
pixels. It samples points every 37 px where the desktop is on top, and counts
how many have the surface's background `#0D1117`. Bottom right is farthest
from the origin:

| Build | Case | Surface rect (physical) | Bottom right |
|---|---|---|---|
| before | 100 % | 3840×1600 | 784 / 791 |
| before | 100 % → 150 % while running | 3840×1600 | 204 / 220 |
| before | **started at 150 %** | **2560×1067** | **16 / 220** |
| after | 100 % | 3840×1600 | 784 / 791 |
| after | 100 % → 150 % while running | 3840×1600 | 205 / 220 |
| after | started at 150 % | **3840×1600** | **203 / 220** |

> **Cause:** a DPI-unaware process gets virtualized `SM_CXSCREEN`/
> `SM_CYSCREEN` (2560×1067 at 150 %), and that was the size
> `AttachToDesktop` gave the surface. WorkerW is 3840×1600 physical. **Fix:**
> `TogglePopup` sets the thread to `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`
> around `CreateWindowExW` and `AttachToDesktop`, and sets it back
> immediately. The window keeps the context, and `WM_PAINT` runs in it, so
> `GetClientRect` gives physical pixels. The rest of the process, panel mode
> included, is DPI-unaware as before. The window-tree probe at 100 % after the
> fix: 14/14. After the test: `anbefalt=100% naa=100% systemdpi=96`.

`/W4` clean, x86, 177 KB exe.

### Phase 10 — persistent double buffer

Branch `opptegning`, merged with `--no-ff`.

**Where the time went.** QPC markers around each step in `PaintPopup` and
`DrawChart`, inserted by script into both master and the new build. At
3840×1600, **55–60 % went to blitting the watermark into a brand-new double
buffer** (6.5–7.9 ms), and another 1.7 ms to freeing it.
`CreateCompatibleBitmap` took only 0.07 ms. It is lazy, and the cost of 24 MB
comes at the first write and at freeing, in every frame (pitfall 48).

**The change:**
- **Persistent buffer:** `bbDC`/`bbBmp` live between frames and are rebuilt
  only when the size changes (`EnsureBackBuffer`/`FreeBackBuffer`).
- **The fast path:** the buttons are drawn straight into the buffer, and the
  strip is blitted from there. The gaps are the previous full frame. New
  condition: `bbValid`, and the overlay must be neither open nor visible while
  fading out.
- **The candles:** drawn with `DC_PEN`/`DC_BRUSH` and `SetDCPenColor`/
  `SetDCBrushColor`, set only on a color change, instead of `penUp`,
  `penDown`, `brUp` and `brDown`. That is four GDI objects fewer, and the
  buffer takes two of them.
- **The watermark cache is kept.** The first version removed it and drew the
  text per frame. That cost 0.50–0.53 ms at 1280×720 and ate the whole gain in
  panel mode. See the plan's deviations.

**Pixels.** Measurement builds of master and the new build with the *same*
changes: the Binance response read from a file (fixed data), `TrackMouseEvent`
as a no-op, and the window at x = −1270, so the real cursor does not reach it.
All state was driven with posted messages, and each frame was captured with
`PrintWindow` only when two frames in a row were identical (pitfall 50).
15 states: rest; hover on each of four buttons, both fast path and full
repaint; mouse left; crosshair; overlay; overlay with the mouse over a button;
overlay closed; desktop mode 3840×1600. Each state is confirmed against rest:
button 468 px, crosshair 9 417 px, overlay 40 460 px.

| Pass | Master vs new | Fast path vs full (new / master) |
|---|---|---|
| 1 | **0 differences**, 15/15 | 0 / 0, all four buttons |
| 2 | **0 differences**, 15/15 | 0 / 0, all four buttons |

**Painting.** Three rounds, master and new alternating, 5 s of `InvalidateRect`
every 16 ms. Panel mode at x = −1270, because a first run on screen was
polluted by hover (0.2 ms crosshair and up to 941 frames per round):

| Config | Master, median | **New, median** | Round medians master → new |
|---|---|---|---|
| panel 1280×720 | 1.585 ms | **1.330 ms (−16 %)** | 2.26 / 1.48 / 1.53 → 1.23 / 1.36 / 1.33 |
| desktop 3840×1600 | 11.696 ms | **5.128 ms (−56 %)** | 11.75 / 11.75 / 11.66 → 4.99 / 5.08 / 5.32 |

| Step (median) | Panel master → new | Desktop master → new |
|---|---|---|
| Buffer created | 0.236 → 0.000 ms | 0.069 → 0.000 ms |
| Watermark blit | 0.239 → 0.276 ms | **6.500 → 1.817 ms** |
| Header text | 0.206 → 0.235 ms | 0.283 → 0.285 ms |
| Candles | 0.600 → 0.536 ms | 1.461 → 1.340 ms |
| Blit to window | 0.098 → 0.098 ms | 1.389 → 1.393 ms |
| Buffer deleted | 0.011 → 0.000 ms | **1.739 → 0.000 ms** |

> **The mandate's 0.85 ms is still not reached.** What remains at 1280×720 is
> the candles (0.54), the watermark blit (0.28) and the header text (0.24). At
> 3840×1600 it is two full-screen blits of 1.4–1.8 ms each and the candles
> (1.34). Switching pen only on a color change gave no measurable gain on its
> own.

**Handles and memory at rest.** Non-instrumented builds, 15 s after start, five
readings per round, two rounds:

| Config | Private bytes | GDI | USER |
|---|---|---|---|
| master, panel 1280×720 | 3.69–3.92 MB | 31 | 14 |
| **new, panel 1280×720** | 3.59–3.77 MB | **29** | 14 |
| master, desktop 3840×1600 | 3.37–3.48 MB | 28 | 6 |
| **new, desktop 3840×1600** | 3.37–3.41 MB | **26** | 6 |

> The 24 MB buffer does not show in private bytes, just as the watermark cache
> did not. Desktop mode is now at 28 GDI in master. Phase 9 measured 30, before
> the DPI fix, and what makes up the difference has not been investigated.

**After the advisor's review**, two things that were otherwise only claimed:

| Test | Result |
|---|---|
| Real Explorer restart with the new build: the buffer used by a *new* surface | new surface in WorkerW after **594 ms**, window tree 14/14, and **90–98 %** of visible desktop points in all four quadrants have the chart's background color (DPI-aware probe) |
| 30 resizes in panel mode | GDI/USER **29/14 before and 29/14 after** |
| Hover posted right after a resize (1280×720, 900×500, 1500×800) | the fast path identical to a full repaint in all three (`bbValid` blocks the fast path until the buffer has the right size) |

> **The DC state now lives between frames.** Pens, brushes, fonts, text color
> and background mode that the last frame selected are still selected in
> `bbDC` when the next frame begins. The pixel test found no consequences of
> that in the 15 states. New drawing code must still select everything it uses
> itself, and cannot count on a fresh DC.

`/W4` clean, x86.

---

### Phase 11 — axes, contrast and time axis

Branch `akser`.

**The change:**
- **Geometry:** `PAD_R` 54 → 84 (price column 76 + edge margin 8) and `PAD_B`
  10 → 18 (time band). All axis text reads `axL`/`axR`; the magic
  `right + 4`, `W − 4`, `W − 2` and `W − 1` are gone.
- **Axis font:** `hFontAxis` = Lucida Console em 15, `ANTIALIASED_QUALITY`.
  Measured: 11 px digit height, 9 px character width, tmHeight 15. The table of
  other fonts is in the plan. `hFontSmall` is unchanged for header, overlay and
  hover box, where `LINE_H = 13` is measured on it.
- **Axis color:** `CLR_AXIS` #A0AAB8, **8.05:1** (the spec said 4.85:1). The
  old axes in `CLR_DIM` were at 4.12:1, below AA.
- **Watermark:** `WatermarkAlpha(W)`, see *The watermark*.
- **Time axis:** text only, `TimeTickStep` (with ceiling, not floor — floor
  collides, see the plan's deviation 3), rounded up with `NiceTimeStep` and
  anchored in local time via `openTime`. No allocation; O(N_x) per frame.
- **Price label under the stamp** is not drawn when they would overlap
  (< 16 px).

**Unit tests** against the functions extracted from `ticker.c` with `awk`:
**572 940 / 572 940**. `WatermarkAlpha` is tested on floor, nominal width,
ceiling, W ≤ 0, and monotonicity and bounds for every W from 1 to 8000.
`TimeTickStep` is tested on the counterexample against floor (M = 9, W = 320 →
S = 3), degenerate cases and freedom from collisions exhaustively over W_chart
160–4000, Δx 80–118 and dCount 1–1440. `NiceTimeStep` never rounds down for any
interval, and hits the expected step for 1m, 5m, 1h, 4h and 1d.

**Pixels**, `PrintWindow` of `--dup` instances (they do not write to the
registry), live data:

| Size, pair/interval | α (t) | Watermark pixel found | Non-BG in `x ≥ W − 5` | Time band outside `[left, right]` | Row `y = bottom` / `bottom + 1` | Text rows in the band |
|---|---|---|---|---|---|---|
| 400×250 BTC 1m | 0.040 (10) | `#161A20` ✓ | 0 | 0 | 306 / 0 | 235–245 (11 px) |
| 1280×720 BTC 1m | 0.065 (17) | `#1D2026` ✓ | 0 | 0 | 1186 / 0 | 705–715 |
| 1280×720 BTC 1h | 0.065 (17) | `#1D2026` ✓ | 0 | 0 | 1186 / 0 | 705–715 |
| 1280×720 BNB 4h | 0.065 (17) | `#1D2026` ✓ | 0 | 0 | 1186 / 0 | 705–715 |
| 1920×900 ETH 1d | 0.080 (20) | `#1F2329` ✓ | 0 | 0 | 1826 / 0 | 885–895 |
| 3000×900 SOL 5m | 0.100 (26) | `#25292E` ✓ | 0 | 0 | 2906 / 0 | 885–895 |

No pixels to the right of `W − 5`. Between `W − 8` and `W − 5` there is only
the stamp's surface, which gets 3 px of padding around the text, and never
text. The row `y = bottom` is solid (the grid), and `bottom + 1` is empty. All
text pixels in the band are exactly `#A0AAB8`: Lucida Console at 15 px is not
anti-aliased, not even with `ANTIALIASED_QUALITY`. The spacing between the
labels, measured in the frames: 118 px (1m, 1280), 189 px (1h, 2 days), 166 px
(4h, 7 days), 170 px (1d, 1920) and 183 px (400×250). The smallest gap between
two labels is 67 px (4h). The space *inside* `DD.MM HH:MM` is 12 px, and it
must not be confused with the gap between two labels.

**Painting**, QPC around `DrawChart`, 400 frames per run,
`InvalidateRect` every 16 ms, the window almost entirely off screen, live data,
master and new alternating:

| Config | Round 1 master / new | Round 2 | Round 3 |
|---|---|---|---|
| 1280×720 | 1.613 / 1.990 ms | 1.323 / 1.242 ms | 1.325 / 1.289 ms |
| 3000×1200 | 2.306 / 2.769 ms | 2.663 / 2.611 ms | 2.508 / 2.525 ms |

Round 1 is warm-up. After it the difference is within the noise: ~12–25
`ExtTextOutW` per frame are not measurable against the rest.

**GDI at rest** (1280×720, 5 and 8 s after start): master 29 / 29, new
**30 / 30**. The extra one is `hFontAxis`.

`/W4` clean, x86.

---

### Focus flash — classic NC frame on activation

Branch `fokus-blink`, merged with `--no-ff` before phase 12.

**The cause, measured.** A probe read the panel's edge from the fully composed
screen (`BitBlt` from the screen DC) every ~2 ms, while focus went to a
helper window and back. It was **not** a one-frame flash:
`DefWindowProc(WM_NCACTIVATE)` draws the classic `WS_THICKFRAME` frame into the
window DC. `WM_NCCALCSIZE` makes the client as large as the window, so the
frame lands *on top of* the chart, 3 px deep: #E3E3E3 and #FFFFFF outermost,
then #B4B4B4 (`COLOR_ACTIVEBORDER`) or #F4F7FC (`COLOR_INACTIVEBORDER`). It
stays until the next full repaint, that is, until the next data fetch
(measured 1.6 s without noise, up to 3 s). `WM_SETTEXT`, which comes on a
symbol change, draws it too.

**The change:** `WM_NCACTIVATE` → `DefWindowProcW(hwnd, msg, wParam, -1)`, and
`WM_NCPAINT` → 0 as a guard. `DWMWA_NCRENDERING_POLICY = DWMNCRP_DISABLED`
from the mandate is **not** used (pitfall 52).

| Variant, 3 focus cycles | Frame-colored px | `WM_SETTEXT` |
|---|---|---|
| Without fix | 7 181 184 | 230 100 |
| `return TRUE` | 0 | 0 |
| `DefWindowProc(…, -1)` | 0 | 0 |
| `-1` + `WM_NCPAINT` | 0 | 0 |
| `-1` + `WM_NCPAINT` + `DWMNCRP_DISABLED` | **1 296**, foreground switch failed 2 of 3 | 0 |

**After merging with phase 12**, 5 focus cycles × 1.5 s each way. Samples where
another window was in the foreground, or covered one of 8 edge points, are
discarded (pitfall 54):

| Build | Valid samples | Frame-colored px | `WM_SETTEXT` / resize |
|---|---|---|---|
| Control, without fix | 984 | 7 910 988 | 230 100 / 0 |
| `ticker.exe` from master | 955 | **0** | **0 / 0** |
| Panel recreated after two mode switches | 758 | **0** | **0 / 0** |

GDI/USER at rest: 30 / 14. `/W4` clean, x86.

### Phase 12 — mode switching from the tray menu

Branch `modusveksling`, merged with `--no-ff`.

**The change:**
- `ID_TRAY_DESKTOP` (1003) and `BuildTrayMenu()`. The menu is built on every
  right-click: "Desktop mode" with a check mark, "Default view" (grayed in
  desktop mode), separator, "Quit". A duplicate does not get the mode
  choice.
- `SetDesktopMode()` **tears down and recreates the window** through
  `TogglePopup`, instead of moving it with `SetParent` (pitfall 53).
  The order is `KillTimer(TIMER_EMBED_ID)`, then `SaveWindowPlacement` while
  the flag still says panel, then `hPopup = NULL` under the lock before
  `DestroyWindow` (so `WM_NCDESTROY` starts no timer). Then
  `animRunning`, `trackingMouse`, `panning` and `bbValid` are reset, the flag is
  set and saved, and `TogglePopup` is called.
- `SaveGeometry` also updates `g_savedPanelX/Y/W/H`. `PlacePopupInitially`
  reads them, and the panel is now recreated after every trip through the
  desktop.
- `DesktopMode` in the registry, see *The registry*.

**Verified** in a test build with `REG_PATH` = `Software\TickerTest`, driven with
`PostMessage(WM_COMMAND, 1003)`:

| Mode | Parent | `WS_CHILD` | `WS_THICKFRAME` | Layered / transparent | TOPMOST | DPI |
|---|---|---|---|---|---|---|
| Desktop | WorkerW | 1 | 0 | 1 / 1 | 0 | per-monitor |
| Panel | none | 0 | 1 | 0 / 0 | 0 | unaware |

- **The registry** follows the switches, and a new process without arguments
  starts straight in desktop mode, also after the previous process was killed
  in that mode.
- **Geometry:** 300,200 900×500 in, the same out after a round trip, also in the
  registry.
- **Data:** `candleCount` 300 before and in the first frame after each switch.
- **A posted "Default view" in desktop mode** does not change the surface.
- **GDI/USER at rest:** 30 / 14–15 after 20, 40, 60 and 100 round trips. That is
  a plateau, not a leak.
- **The menu** (`BuildTrayMenu` extracted with `awk`): 15/15.

**Switch time**, from `WM_COMMAND` until the first full frame is blitted:

| Direction | Median | Range | Largest step |
|---|---|---|---|
| To desktop 3840×1600 | **28 ms** | 26–43 ms | 16 ms: first write into a new watermark cache and a new buffer (pitfall 48) |
| To panel 1280×720 | **18 ms** | 16–37 ms | 6 ms: placement, show and foreground in `TogglePopup` |

> **The mandate's < 16 ms is not reached, and that is a deliberate choice.** It
> can only be reached by keeping the bitmaps for both sizes, about 48 MB more
> while the panel is in use. The user chose rebuilding on switch: the switch is
> rare and manual, and 28 ms is under two frames at 60 Hz.

### Phase 13 — start at sign-in from the tray menu

Branch `autostart`.

**The change:**
- `IDM_TOGGLE_AUTOSTART` (1004). "Start at sign-in" with a check mark sits
  directly above "Quit", with a separator on both sides. The check mark is read
  from the registry every time the menu is built. A duplicate does not get the
  item.
- `AutostartPresent()` gives the check mark: the value exists, whatever its
  type and content.
- `ToggleAutostart()`: if the value equals the current quoted path
  (`_wcsicmp`), it is deleted. Otherwise the current path is written, also when
  the value points to a moved exe, is unquoted, has the wrong type or is too
  long. A checked item with a stale path is therefore fixed by a click instead
  of being turned off.
- `AutostartCommand()` uses the same `MAX_PATH` guard as `SpawnInstance`. A
  truncated path is never written.
- The mandate mentions `WM_CONTEXTMENU`, but the tray icon delivers
  `WM_RBUTTONUP` through `WM_TRAYICON`. The item therefore lives in
  `BuildTrayMenu()`.

**Verified** in a test build with `AUTOSTART_KEY` = `Software\TickerTestRun`,
from two folders with spaces in the name, **27/27 in three runs**. The tests
cover menu order and text, check mark before and after a click, quoted
`REG_SZ`, deletion, moved exe (the value is updated, not deleted), casing,
unquoted path, `REG_DWORD`, a value that is too long, and a duplicate. The
command line was run through `CreateProcess` parsing and starts the right exe.
**GDI/USER 28/13**, unchanged through 800 clicks and 80 menus. A real sign-in
is not tested.

**After the merge**, in the build in the root folder and against the real Run
key: **14/14**. The menu has the right order and text; autostart writes
`"C:\Users\sysadmin\Desktop\Ticker\ticker.exe"` and deletes it again; desktop
mode off and on in the same run gives surface in WorkerW → top-level panel →
surface in WorkerW, with `DesktopMode` 1 → 0 → 1 and "Default view" grayed only
in desktop mode. The Run key had no `Ticker` value before and after.

---

### Phase 14 — text-free surface on the desktop

Branch `omgivelsesmodus`.

**The premise:** a panel is read foveally — the user stops and decodes
numbers. A surface in the desktop is read peripherally, and then alphanumeric
stamps are interference against icons and folders. Painting is therefore split
into two layers: infrastructure (curve, grid, axis logic) that stays, and
metadata (price, percent, subtitle, axis labels, last-price stamp) that is
disabled when the window is a background structure.

**The change:**
- `ChartGeometry()` returns the whole surface in desktop mode: `0, 0, W, H`.
  One place, and the watermark's centering, the grid, the candles, the clip
  region and the last-price line follow.
- `DrawChart()`: the header block, the price axis labels, the whole time axis
  block and the last-price **stamp** are gated on `!g_desktopMode`. The dashed
  last-price line stays — it is geometry, not a number. The watermark stays as
  an identity marker.
- An empty buffer in desktop mode returns without a message. The surface shows
  background and watermark.
- The grid draws only lines 1–3 there. Edge to edge would have put lines 0 and
  4 on `y = 0` and `y = H - 1`, that is, a 1 px frame around the whole screen.
- `SetDesktopMode()` resets `wmValid`: the watermark cache lives in `ctx`,
  survives the window being recreated, and is keyed on
  `(W, H, symIdx, ivIdx)` — not on mode, which now decides the geometry.
- The fast path for the button row in `WM_PAINT` now requires
  `!g_desktopMode` explicitly.

**Verified** with a dump of the back buffer from the test build (`WM_APP+7`),
not a screenshot: the surface is layered and sits behind the icons. **22/22 in
two runs.** Zero pixels of `CLR_AXIS`, `CLR_TEXT` and `CLR_DIM` on the desktop,
against 1 514 / 230 / 105 in the panel control in the same run. The grid rows
lie at exactly 400, 800 and 1200 of 1600 with extent x 0 … 3839 — the direct
proof of a full surface — against 44, 208, 373, 537, 702 and x 10 … 1195 in
the panel. An empty buffer gives 0 `CLR_DIM` pixels on the desktop and 134 in
the panel. **GDI/USER 30/14**, unchanged through 50 mode switches.

**After the merge**, with the production build running on a real monitor: the
surface sits in WorkerW, 3840×1600, and a 2400×1000 section of the desktop has
0 `CLR_AXIS` and 0 `CLR_DIM` pixels, with 25 210 pixels of grid and candles.
One `CLR_TEXT` hit showed up in one of three runs; `WindowFromPoint` showed
that it belonged to a `XamlWindow`, not the ticker surface (pitfall 54 again,
now on a single pixel). The tray menu still has all six items.

---

### Phase 15 — room before the axis and the price line as a bridge

Branch `prislinje-offset`.

**The change:** `ChartRect` now separates `right`/`cw` (the candles' area)
from `edge` (the axis edge, where the stamp and the labels begin).
`PLOT_PAD_R` = 10 px is the gap between them, with an `#error` guard on
[8, 12]. The grid, the clip region, the dashed last-price line and the
crosshair's horizontal line run to `edge`; the candles stop at `right`,
because `slot` is computed from `cw`. Everything that maps x ↔ candle index
reads `cw` and follows without changes of its own. In desktop mode
`right == edge`: there is no axis there.

**The line is drawn to `edge + 1`.** `LineTo` does not draw the end point, so
with `edge` the column `x = edge` stayed empty — one black hole between the
line and the stamp. The pixel measurement caught it; the eye did not.

**Verified** with a dump of the back buffer, **11/11** at 1280×720: `right` =
1186, `edge` = 1196, the last candle pixel outside the stamp band at
**x = 1184**, and exactly **one** row of candle color in the gap — the dashed
line. `x = edge` and `x = edge + 1` are both `0x00FF66`: the line meets the
stamp without a break. **GDI/USER 30/14**, unchanged through 40 resizes and 40
title changes. Desktop mode unchanged, highest candle pixel x = 3839 of 3839.

**The dashing broke anyway, and was fixed.** The first version drew the whole
line with `PS_DASH` up to `edge + 1`, and 20 of 20 measured widths made
contact. The measurement was not representative: in the production build, at
1004 px, `x = edge` was the background color — the pattern ended in an "off"
interval. `PS_DASH` gives no control over the phase at the end of the line.
The line is now **dashed over the data area and solid over the gap**
(`right → edge + 1`, `DC_PEN` in the same color). After the fix: contact in 20
of 20 widths and in 12 of 12 around 998 … 1009 px, so also the width that
broke. That is by construction, not luck.

**In the production build**, read with `PrintWindow`: **5/5** at 1280×720. One
row of candle color in the gap, the last candle pixel at x = 1184 against the
edge 1186, and `x = edge` and `edge + 1` both `0x00FF66`.

---

### Phase 16 — price stamp in desktop mode

Branch `skrivebordsstempel`.

**The change:** the desktop surface has got a right margin back — not for
axis labels, but for the one stamp with the last price. `ChartGeometry` sets
`edge = W - DeskAxisW(H)` in desktop mode, and `right = edge - PLOT_PAD_R` in
both modes. Left, top and bottom are still edge to edge.

- `DeskPillH(H)` = `H / 40`, clamped to [16, 48]. `DeskPillFontH` is
  `MulDiv(pillH, 15, 16)` — the same ratio as the panel's 16 px stamp around a
  15 px font. `DeskAxisW` rounds the character width **up**; with rounding
  down, eight characters would have been 6 px short, and the price would have
  silently fallen back to the axis resolution.
- All three are pure functions of `H`, because `ChartGeometry` is also called
  from hit detection and panning, where there is no DC to measure in.
- `hFontPill` is cached by height, like `hFontWm`, and freed with the other
  fonts.
- The stamp is now drawn in both modes. Axis labels, time axis and header are
  still gone from the desktop.

**Phase 14's invariant has changed:** "zero alphanumeric pixels on the
desktop" is now "exactly one text element". `CLR_AXIS`, `CLR_TEXT` and
`CLR_DIM` are still zero — the stamp text is `CLR_BG` on a saturated fill —
but the grid rows span to `edge`, not to `W - 1`.

**Verified** with a dump of the back buffer, **15/15**, and the panel probe
from phase 15 still **11/11**. At 3840×1600: stamp 40 px, font 38 px, margin
196 px, so `edge` = 3644. The grid rows lie at 400/800/1200 with extent
x 0 … 3643. One row of candle color in the gap, the last candle pixel at
x = 3618, the stamp height measured at 40 px in column `edge + 1`, and the
text within `[3648, 3832)`. **GDI/USER 31/14**, unchanged through 50 mode
switches.

**The text is safely dark:** the surface is layered with `LWA_ALPHA 255`, not
a color key. With `LWA_COLORKEY` on `CLR_BG` the digits would have become
holes through to the wallpaper.

### Phase 17 — symbol and interval from the tray menu

Branch `tray-symbol-intervall`, merged with `--no-ff`.

**The change:** the tray menu has got two submenus at the top, "Symbol" and
"Interval", with a separator below. One item per row in `SYMBOLS[]` /
`INTERVALS[]`, label the same as the overlay's, radio check on the current
index (`CheckMenuRadioItem`). So desktop mode can switch without going through
the panel, and the panel can switch without opening the overlay.

- `ID_TRAY_SYMBOL_FIRST` (1100) and `ID_TRAY_INTERVAL_FIRST` (1200), both
  100 wide. Three `C_ASSERT` below the tables stop the build if a table grows
  past its range. `#if` does not work: `SYMBOL_COUNT` is `sizeof`.
- `WM_COMMAND` does a range check first, like `ID_TRAY_RESET`: a posted ID
  outside the tables is a silent no-op. Inside, it is translated to the same
  `hit` encoding as `OverlayHit`, and `ApplyConfigChoice` is shared by both
  paths.
- **`ApplyConfigChoice` no longer takes an HWND.** Its only use was
  `InvalidateRect(hwnd)` at the end. From the menu the right window is
  `hPopup`, which is NULL when the panel is closed, and
  `InvalidateRect(NULL, …)` repaints the whole desktop. The function reads
  `hPopup` itself and skips the invalidation when there is no window.
  Everything else in it was already window-safe.
- The submenus are attached with `MF_POPUP` and owned by the main menu;
  `DestroyMenu` in `WM_TRAYICON` tears down all three.
- **Duplicates get the submenus.** The overlay already lets them switch their
  own view, and `SaveConfig` skips duplicates by itself.

**Verified** in a test build against `Software\TickerTest`, **40/40 in two
runs**: menu content, IDs and radio checks; a choice with the panel closed
(the registry is written, no popup, no crash, the panel opens with the right
pair); a switch empties the buffer synchronously and fills it again; the same
choice touches nothing; IDs outside the range touch nothing; desktop mode is
repainted with the new symbol, and the frame with an empty buffer has **zero**
pixels above luminance 120; a duplicate switches locally without touching the
registry. **GDI/USER 33/14** after warm-up and after each of three rounds of
50 menus and 40 choices.

**In the production build**, which runs in desktop mode and was only read, not
clicked: the menu has 9 items in the right order, the submenus 4 and 6, and
the radio checks are on BTC/USDT and 1m, matching `SymbolIndex` 0 and
`IntervalIndex` 0 in `Software\Ticker`. The submenus' own items have ID −1
(`MF_POPUP`), which `WM_COMMAND` never sees.

### Phase 18 — history on demand

Branch `historikk`, merged with `--no-ff`. Chosen by the agent after an open
review; the reasoning is in the plan.

**The change:** when the user pans into the wall (`viewStart` 0), the UI
thread asks for older candles, and the worker thread fetches `SEED_COUNT`
candles with `endTime = candles[0].openTime - 1` before the normal fetch in
the same cycle. `PrependCandles` puts them in front, moves the view by the
same amount and counts `frontShift` down, so the frame does not move.

- **`evictedTotal` has become `frontShift`**, signed: +1 per eviction, −k per
  backfill. `ApplyEviction` has become `ApplyFrontShift` and moves
  `dispStart`, `hoverIdx` and `panAnchorView` both ways. The panning block
  calls it under the lock before it reads the anchor.
- **The anchor slides at the wall.** If `ClampView` clamps, the anchor is
  moved to where the view actually is. Before, it remembered the overshoot: a
  drag 15 candles past the wall gave a jump of 16 candles on the next mouse
  move after the candles arrived, and a drag back from the wall stood still
  for just as long.
- **`HttpGet` checks the status code.** Non-2xx is FALSE. Before, a 429 with a
  JSON body counted as success, and the parsers caught it silently. The
  backfill needs the distinction: 2xx with zero candles is "the history has
  ended", anything else is backoff.
- **`RequestHistory` wakes the thread only when `netFailures` is 0.**
  `hWakeEvent` resets the backoff, and a drag at the wall during a
  disconnection must not turn it off. In backoff the thread sees the flag on
  its own cycle.
- **`histDone`** on 2xx without candles, on a response where nothing was
  older, and on a full buffer. Reset together with `histPending` where
  `candleCount` is set to 0.
- **`MAX_CANDLES` 6000.** Live candles are never discarded to make room for
  old ones.
- **`WM_APP_PROBE`** behind `#ifdef TICKER_PROBE`: the test build's window
  into internal state. The production build does not have the message.

**Verified:** unit harness **36/36** against the actual functions
(`PrependCandles`, `ApplyFrontShift`, `MergeCandles`, `HttpGet` against
Binance with 400, 404, 200 and `[]`). End to end **33/33 in two runs**:
backfill landed after 353–372 ms, `viewStart` 0 → 300 with the same oldest
visible timestamp and a pixel-identical frame; a drag at the wall gives one
candle per slot after landing, not a jump; wheel spam during a fetch;
SOL/USDT 1d is exhausted in 8 rounds back to **2020-08-11**, the listing day;
BTC/USDT 1m fills 6000 in 19 rounds; `R` gives the last 300; **GDI/USER
30/14** flat.

### Phase 19 — keyboard shortcuts for the control buttons

Branch `tastatursnarveier`, merged with `--no-ff`. Chosen by the agent among
four candidates; the reasoning and what was set aside (DPI scaling of the
stamp, the machine is at 100 %) are in the plan.

**The change:** `WM_KEYDOWN` in `PopupProc` knows `Ctrl`+`N` (`[ + ]`),
`Ctrl`+`M` (minimize), `F11` (maximize/restore) and `Ctrl`+`W` (close). All
four go through `OnButtonClick`, so key and click share the same path — the
geometry is saved before maximizing, a restored rect outside everything
visible is caught, and a duplicate exits on close. `Alt`+`F4` already worked:
`DefWindowProc` sends `SC_CLOSE` also without `WS_SYSMENU`, measured.

- **Blocked** in the middle of panning (`panning`): a minimize during a drag
  would have skipped `WM_LBUTTONUP`, which releases capture and restores the
  cursor. Blocked in desktop mode, which never gets keyboard focus anyway.
- **`staleSecsShown`** is reset in `WM_APP_DATA` when the line is up. The
  counter starts at 9, so 0 is never a real number of seconds.
- The shortcuts inherit the `ESC` caveat: they require keyboard focus in the
  panel.

**Verified** end to end, **23/23 in two runs**, in a test build against
`Software\TickerTest`. The keys are sent as real keystrokes with `SendInput`
after the probe has confirmed foreground and focus on the panel; a control
with `Ctrl`+`0` (900×500 → 1280×720) first proves that the injection and the
`Ctrl` state get through. Red run against untouched code: 14 OK, 6 FAIL, with
the control green. `F11` → maximized and back to 1280×720; `Ctrl`+`M` →
minimized; `Ctrl`+`N` → one new process with a panel at +30/+30, ended by
`WM_CLOSE`; `Alt`+`F4` and `Ctrl`+`W` → hidden, opens again; 20 rounds of
`F11`/`F11`/`Ctrl`+`M`/restore; **GDI/USER 30/14** before and after.

**Not tested:** the `staleSecsShown` branch (requires a cut network,
pitfall 9) and the panning block (requires a key in the middle of a real
drag). Both are read.

---

### Phase 20 — keyboard navigation in the chart and lost capture

Branch `tastaturnavigasjon`, merged with `--no-ff`. Chosen by the agent among
five candidates; what was set aside (volume, sleep mode, resolution change,
the DPI stamp) is in the plan with reasons.

**The change, in two commits.** First `PanView` and `ZoomView`, extracted
from `WM_MOUSEWHEEL` without a behavior change, plus probe field 12
(`panning`) — that is the build the red run went against. Then the keys in
`WM_KEYDOWN`, after the phase 19 block and before the `ESC` layers:
`←`/`→` one wheel notch, `PgUp`/`PgDn` a whole view, `Home` to the wall
(which asks for history, phase 18), `End` to the live edge, `+`/`-` one zoom
step about the center — `VK_OEM_PLUS`/`VK_ADD` and
`VK_OEM_MINUS`/`VK_SUBTRACT`, `Ctrl` allowed on these two and not on the
others. The same blocks as the wheel: not with the overlay open, not in
desktop mode, not during panning. Hover is reset as `R` does it —
recomputing the candle under the cursor would have let the crosshair slide
with the candle during the easing and stay offset from the cursor.

**`WM_CAPTURECHANGED`:** if `panning` is set and `lParam` is a window other
than ours, panning is released and the cursor is restored. Our own
`ReleaseCapture` also sends the message, but then `panning` is already
`FALSE`. Probe field 13 reads `GetCapture() == hwnd` from the app's thread.

**Verified** end to end, **52/52 and 51/51 in two runs** (the second without
the `Alt`+`Tab` branch, see below), in a test build against
`Software\TickerTest`. The keys are sent with `SendInput` after a foreground
and focus check as in phase 19; the control first is `Ctrl`+wheel via
`SendMessage` (300 seeded candles and `DEFAULT_VIEW` 300 give the wall from
the start, so the view must be narrowed before panning can be measured).
State is read with `WM_APP_PROBE`.

- **Red run** against part 1: the control green, all the keys red
  (16 OK, 23 FAIL). Red run of the capture path against a build with the
  handler disconnected: the menu took capture, `panning` stayed set,
  `Ctrl`+`M` blocked.
- `←` → `vs − vc/8`, `followLive` off, 46 000–51 000 differing pixels in the
  chart band (`PrintWindow` before/after); `→` back; `PgUp` → the wall, 300
  candles arrived, `PgDn` → the edge; `Home` → the view starts at the oldest
  candle before *and* after the backfill (same `openTime`, `vs` = number of
  new candles); `End` → `followLive`; `+` → `vc / 1,2` about the center, `-`
  back, numpad and `Ctrl`+`+` likewise. The layout's `+` is `VK_OEM_PLUS`
  (`VkKeyScan` 0xBB).
- Overlay open → `←` does nothing; `ESC` → works again. Hover with a real
  cursor → `←` → `hoverIdx = −1`.
- **Capture:** a real drag with `SendInput` (`pan=1`, `cap=1`). `Alt`+`Tab`
  took capture in two of five runs with a real drag and let the panel keep it
  in three — the foreground switched every time. The deterministic thief is
  **the tray menu**:
  `TrackPopupMenu` in the same thread takes capture every time, `panning` is
  released, and `Ctrl`+`M` minimizes afterwards. `ESC` closes the menu
  *before* the mouse button is released (pitfall 56).
- **GDI/USER 30/14** at rest at start; **32/14** after the first overlay and
  **35/14** after the first tray menu, both unchanged over three cycles up to
  and through 30 rounds of `←`/`→`/`+`/`-`. The jumps are one-time
  (pitfall 65), not leaks, and the same in the build without phase 20.

**Not tested:** capture taken by a window in another process (`SetCapture`
across processes), and the `Win` key. Both go through the same message. The
wheel *without* `Ctrl` — panning through `PanView` — is not measured after the
refactoring either; the control used only `Ctrl`+wheel (`ZoomView`, which
gave `vs` 68 / `vc` 173 before and after in each run). The path is read.

---

### Phase 21 — volume bars under the candles

Branch `volum`, merged with `--no-ff`. Chosen by the agent among four
candidates; sleep mode and resolution change were set aside because neither
can be observed in a probe on this machine (the reasoning is in the plan).

**The change, in two commits.** First instrumentation: `Candle.volume`
(48 bytes per candle, the buffer 288 KB), probe field 14 (volume at index,
×100 — `LRESULT` is 32 bits on x86) and 15 (last full repaint in µs, QPC
around the slow path in `PaintPopup`, test build only). Then the feature:
`ParseKlines` reads field 5 (quoted string like OHLC); `VolumeMax` over the
target view where `PriceRange` is computed; `dispVolMax` is eased in
`WM_TIMER` as the fifth value, snap = a quarter pixel of the band height; the
bars are drawn after the grid and before the candles, inside the same clip,
in the bottom `VOL_FRAC` = 22 % of the chart area, `bodyW` wide at the
candle's `cx`, the bottom row at `y = bottom` inclusive. The scale the
drawing reads is *the display*, never the target. The direction is the
candle's own (close against open). One `PolyPolygon` per color and batch of
256 bars with `NULL_PEN`, which fills exactly the `FillRect` pixels. The
hover box gets a `V` row (74 → 87 px) with `K`/`M` format. **Desktop mode
draws the bars too:** phase 14 removed what has to be read foveally
(numbers); bars are read peripherally like the candles. `ChartGeometry`,
`HitCandle`, the axes and the stamp are untouched.

**Verified** end to end, **24/24**, in a test build against
`Software\TickerTest`. Red run against commit 1: volume 0 in all six candles,
0 bar pixels, box 72 px (8 FAIL). Green: volume > 0 in six candles spread
over the buffer (3.16–14.44 BTC on 1m); **5 748** pixels in exactly the bar
color in the panel, all in `[559, 702]` = exactly the band `[bottom + 1 −
144, bottom]`, none above it, none outside the chart area in x, the highest
bar reaches the top of the band; the hover box's longest vertical `CLR_BOX`
run **85** (72 before); the desktop surface 3840×1600 captured with
`PrintWindow` under WorkerW: 47 019 bar pixels in `[1249, 1599]`, band top
1248; GDI/USER stable (32/14 → 33/14 after a mode switch there and back).
The rest level 32/14 is two higher than phase 20's 30/14 because the two bar
brushes are created at startup; the +1 after a mode switch was also in the
build without phase 21 (30 → 31 in the red run).

**Fix after the merge:** `PolyPolygon` filled with ALTERNATE. With more
candles than pixels (`vc > cw`), `slot` is below 1, `bodyW` is clamped to 1,
and neighboring candles land on the same `cx`; two identical rectangles in
the same batch cancel each other under the even-odd rule, so the bar
disappeared. `SetPolyFillMode(WINDING)` around the batches. Measured in the
probe: two identical 10×16 rectangles give 0 pixels under ALTERNATE and 160
under WINDING; fully zoomed out with 1800 candles on 1176 px, the row
`y = bottom` has bar color in 873 of 1176 columns under ALTERNATE (red run)
and 1169 / 1166 of 1176 under WINDING (28/28 in two runs). The fill mode has
no measurable cost (medians 1.46–1.66 ms against 1.58 in the ALTERNATE run).

**Painting at 1280×720, 300 candles, median over 172 full frames, same probe
and same run conditions (no `PrintWindow` meanwhile):** 1.44 ms without bars
(commit 1), 1.84 ms with one `FillRect` per candle (+0.40 ms), **1.56 ms**
with `PolyPolygon` in batches (+0.12 ms). p90 1.63 / 2.08 / 1.75 ms. The
batch variant was chosen on the numbers; the `FillRect` variant also had
24/24. The table in *Measurements* is updated.

**Not tested:** a pair with volume 0 in the whole view (`dispVolMax` 0 → no
bars; the branch is read), and the `K`/`M` format in the hover box (BTC
volume on 1m is under a thousand).

### Phase 22 — toolbar in the header

Branch `verktoylinje`, merged with `--no-ff`. The user put forward two ideas
— a toolbar and price alerts on the price axis — and the agent chose. The
alerts were set aside as a candidate: the trigger (live price crosses a line)
cannot be provoked in a probe without a *writing* probe field, and sound and
balloon cannot be observed. The reasoning is in the plan.

**The change, in two commits.** First instrumentation: `tbHot`, `showVol`,
`dispVolF` and probe fields 16–21 (`ivIdx`, `symIdx`, `showVol`, `tbHot`,
`overlayOpen`, `dispVolF` × 1000). Then the feature: `ToolbarLayout` /
`ToolbarHit` / `ToolbarStrip` (a pure function of the width, fixed pill
widths, `C_ASSERT` against 400 px), `DrawToolbar` from `PaintPopup`,
`HTCLIENT` over the pills in `WM_NCHITTEST`, hover and click alongside the
buttons', `OnToolbarClick` (interval → `ApplyConfigChoice`, symbol → the
overlay, VOL → `SetShowVolume`), `dispVolF` as the sixth eased value, `V` and
`1`…`6`, "Volume bars" in the tray menu (`ID_TRAY_VOLUME` 1005) and
`ShowVolume` in the registry. The symbol line as text is gone; `frakoblet Ns`
sits to the right of the last pill when the whole text fits. See
**The toolbar** under *The window*. `HEADER_H`, `ChartGeometry`, `HitCandle`
and all chart y values are untouched.

**Verified** end to end, **81/81 in two runs**; a red run against commit 1
gave 33 FAIL with the controls green. `HTCLIENT` on all eight pills and
`HTCAPTION` in every gap; pill states read from corner pixels; 0 text pixels
within 3 px of a pill edge; the pills drawn with an empty buffer right after
a switch; `IntervalIndex` and `ShowVolume` in the registry; 19 intermediate
values of `dispVolF` on the way to exactly 0 and 0 bar pixels (6 267 with VOL
on); `DOWN` + `DBLCLK` = two toggles; hover with a real cursor and
`WM_MOUSELEAVE` out into the gap; everything drawn and clickable at 400×250;
the choices survive a restart without animation. Painting 1 479 µs before,
1 606 / 1 517 µs after (median, 168–177 frames). GDI/USER 34/14 before and
after ("before" taken after the first overlay).

**Not tested:** the real tray menu (only the command), the toggle in desktop
mode, hiding of pills below 400 px, and the offline text's new place.

### Phase 23 — price alerts on the price axis

Branch `prisvarsler`, merged with `--no-ff`. The user put forward three
candidates — price alerts with price injection in the probe, re-initialization
after sleep, and a resolution/DPI change in desktop mode — and the agent chose.
Sleep is set aside as the **recommended next phase** (small, but the real
event cannot be driven from a probe, pitfall 47); the DPI change stands as a
known limitation (one monitor). The reasons are in the plan.

**The decision phase 22 was waiting for:** the test build now has **writing**
probe fields. They exist only behind `/DTICKER_PROBE`, live on the main window,
and the injection carries the price in the message and tries the trigger
synchronously (pitfall 70). The production build does not have the message.

**The change, in two commits.** First instrumentation: the state fields in
`AppContext`, reading fields 22–32, writing 100 (inject price) and 101
(mute). Then the feature: `AlertHit` and `AlertRound` (pure), `AlertY` /
`AlertPriceAtY` / `AlertAxisHit` (read `disp*`, pitfall 14), `AlertAdd` /
`AlertRemove` / `AlertsClear`, `FireAlert`, `CheckAlerts` from `WM_APP_DATA`,
`OnAxisClick`, hover in `WM_MOUSEMOVE` (`axisHotY`, `alertHot`, `alertFresh`),
the hand in `WM_SETCURSOR`, the `A` key, `alertFlashF` as the seventh eased
value, `SaveAlerts` / `LoadAlerts`, "Clear price alerts (N)"
(`ID_TRAY_ALERTS_CLEAR` 1006), and the drawing in `DrawChart`: lines behind
the candles, tags, ghost and afterglow. See **Price alerts** under *The window*.
`ChartGeometry`, `HitCandle`, the lock's coverage and the network thread are untouched.

**Fixed along the way, found in a screenshot and not by the probe:** the
last-price stamp lay on top of an alert tag 12 px below, and a number cut
lengthwise stuck out. A tag that is covered by the stamp or by a tag drawn
later (under 16 px) is now drawn as a plain surface. The probe got a check for it.

**Fixed after the merge, found by the production build:** the exe grew from
187 392 to 216 064 bytes. `AlertRound` used `pow(10, floor(log10(x)))`, and
those two calls alone pulled in ~21 KB of CRT math — for a rounding with nine
possible answers. Replaced with a staircase (`q *= 10`) above 1 and division by
10 or 100 below 1 (division, because 0.1 and 0.01 do not exist exactly).
**195 072 bytes** afterwards, +7.7 KB for the whole phase. Unit tests 20/20 and
109/109 in two new runs on that build. All earlier builds in the phase were
test builds; only the production build showed the size (pitfall 75).

**Verified.** Unit tests on real code (the functions pasted out of
`ticker.c`): **20/20** — `AlertHit` on both sides, on the level, price 0 and
negative, level 0; `AlertRound` for BTC 1m/1d and SOL, the floor 0.01, and
the property |rounded − price| ≤ half a pixel over about 1 250 step sizes.
End to end, **109/109 in two runs**; a red run against commit 1 gave
**52 FAIL** with all the controls green, including the writing probe itself
(fields 100 and 101 exist in both builds). A click on row 300 sets the level
within one pixel in price (81 066.00 vs 81 065.82; the line on row 299,
pitfall 73), the sign follows the side, 1 074 amber pixels in the tag and 1 161
in the line with the candles on top; the ghost is framed (365 px) with the line
on the pointer's row; the hand over the column; red tag under a real pointer,
amber while newly set; `A` with and without crosshair; the nearest tag is
removed; a fast double-click is set + remove and **does not reset** the view,
while a double-click in the chart still does. The trigger: one cent below does
not fire, *on* the level fires, once, the registry cleared at once; lower alert
mirrored; price 0 does not fire; two of three alerts passed by the same price
fire in the same check and the right one remains; level 0, duplicate and the
ninth are rejected. Afterglow 1 000 → intermediate value → exactly 0, 1 186
amber-like pixels over the candles and 0 afterwards. The symbols are separate
(ETH has none, the BTC alert survives the round trip). Fires with the panel
hidden, without afterglow. **One alert unmuted per run:**
`Shell_NotifyIconW(NIM_MODIFY, NIF_INFO)` returned
`TRUE`. 400×250 works. Survives a restart with side; a registry written by
hand (crossed level, NaN, −1e12, 0 and one valid) gives one firing on the first
price and one alert left. Painting with **eight** alerts: +0.11 and
+0.17 ms (1 893 → 2 006 and 1 938 → 2 110 µs, median of 150) — eight
`GetTextExtentPoint32W` + `DrawTextW`; one or two alerts are in the noise.
GDI/USER **34/14 before and after** in all three runs.

**Not tested:** that the balloon actually *shows* and the sound is *heard* (only
the return value from `Shell_NotifyIconW`); the real tray menu (only the
command); the alert lines in desktop mode (same `DrawChart`, not captured); the
trigger through a real fetch (only injected — the path from `WM_APP_DATA` onward
is the same); a duplicate's alerts; the gray ghost with a full set (the click
is tested, the color not); a tag covered by *another tag* (only by the
stamp).

### Phase 24 — sane input and wake from sleep

Branch `robuste-inndata`, merged with `--no-ff`.

**The mandate was an architecture directive:** keep C close to Win32, move
"higher logic" to C++ — `std::vector` and RAII instead of `malloc`/`realloc`/`free`,
`std::string` and nlohmann/json for the API responses, classes around SMA/EMA/RSI —
with zero leaks and fault tolerance against network drops and invalid responses
as the overriding requirement, and full freedom to shape it. **The directive
describes a different codebase than this one.** There is no `malloc` to replace
(all buffers are static, see *Data layer*), no indicators to encapsulate, and one
source file. The means were therefore **measured, not adopted** — the numbers are
under *Rejected proposals* — while the goal in the directive's point 3 became the
phase: find what can actually go wrong with input and network, and fix it.

**Found by reading through:** the parsers relied on `atof`, which cannot fail.
`"price":"abc"` gave 0.0 and `TRUE`, `"1e999"` gave inf, `"nan"` gave NaN —
straight into `lastPrice` and `candles[]`. An inf in a candle blows up the Y scale
(and `double → int` in the coordinates is undefined behavior), a NaN price draws a
blank icon. A candle with unquoted fields borrowed the numbers from the *next*
candle, because the search for quote marks did not stop at the brackets.
`PrependCandles` assumes ascending time without anything guaranteeing it. And a
2xx response with nothing but garbage set `histDone` for good. None of this has
been seen from Binance — but the app stays on for weeks, and "the server decides"
(the comment in `PrependCandles`).

**The change, in two commits.** First instrumentation: counters 110–112 on
the main window (fetch cycles, wakes, rejected values), only in the
test build. Then the feature: `PriceSane`, `CandleSane`, `ParseQuotedNumber`
(`strtod` with an end pointer: one whole number between the quote marks, otherwise
NULL); `FastParsePrice` does not touch the output value without a sane price;
`ParseKlines` skips insane candles and counts them in `*rejected`;
`WorkerFetchHistory` treats "zero candles, some rejected" as an error and not as
the end of the history. `WM_POWERBROADCAST` / `PBT_APMRESUMEAUTOMATIC` sets
`dropConn` (new field in the lock domain) and `hWakeEvent`; the thread releases
`hConnect` only in the next cycle (counter 113). `MergeCandles`, `PrependCandles`,
the backoff and all drawing are untouched. **Exe 195 072 → 195 584 bytes (+512).**

**Verified.** Unit tests on real code: **31/31**, a red run against
commit 1 gave **22 FAIL** — text, empty string, 0, negative, nan, inf, overflow,
truncated response and garbage after the number for the price; `high < low`, nan,
inf, zero prices, negative volume, open outside the range, time going backward,
duplicated time, `openTime` 0, unquoted fields, too short an array and garbage
after a number for the candles. End to end (`probe_resume.c`), **22/22 in two
runs**, red run **7 FAIL**: a wake gives a new fetch after **281–297 ms** three of
three in both runs (the idle cycle is 3 000 ms), the connection is released every
time and the next fetch succeeds over the new one; `PBT_APMSUSPEND`,
`PBT_APMRESUMESUSPEND` and
`PBT_APMPOWERSTATUSCHANGE` do not wake; ten wakes in a row give 2
fetches (the auto-reset event merges them); with the panel open,
**300 real candles go through the new parser with 0 rejected**, and a wake
also wakes the candle branch (297 ms). GDI/USER **23/5 before and after** 13
wakes; clean exit, code 0. The probe sends no keys or clicks
and does not need an idle machine.

**Not tested:** real sleep. The probe *sends* `WM_POWERBROADCAST`; that Windows
delivers it to a hidden top-level window is documented, but not measured here, and
pitfall 47 is exactly a sent message that was green while the real
event revealed a bug. The behavior when the network is not up at the first
attempt (expected: one error, then 6 s) is read, not run. The phase 23 probe
(price alerts, 109 checks) was **not run again** — it needs an idle
machine; the path from `WM_APP_DATA` onward is untouched. An insane candle *end to
end* (only in the unit tests — there is no writing probe field for a whole
response), and the backfill (phase 18) through the new parser (same function as
the seed response, but the path with `rejected` is only read).

### Phase 25 — moving averages: SMA 20 and EMA 50

Branch `indikatorer`, merged with `--no-ff`.

**The mandate** listed SMA/EMA in C, `WM_DISPLAYCHANGE`/`WM_DPICHANGED` in
desktop mode, and own initiative. **Chosen: SMA/EMA** — the only one of the
three the user sees every time the panel opens, and what phase 24 promised
("indicators are wanted, build them in C"). The monitor change stands as the next
candidate, with a testable design in the plan file.

**No table.** The averages are not stored. `IndState` is a 40-byte step
machine: `IndStep(&s, candles, i)` is fed one candle and gives the value as it
falls out. The SMA is a rolling sum, started `period − 1` candles before the first
drawn candle (`IndFeedStart`), so the sum never lives longer than one frame and
cannot drift. The EMA is seeded with the SMA of the first 50 candles and continues
with `v += k·(close − v)`, `k = 2/51`; it has infinite memory and is fed
**always from candle 0**, otherwise the line would depend on where the view begins
and move during panning. Only `+ − × ÷` — no `pow`/`log`
(pitfall 75). The points go straight into `s_volPts`, the volume bars' buffer,
which is done being used when the lines begin: **the overlay has zero bytes of its
own static memory.**

**The drawing** (`DrawIndicator`): `Polyline` with `DC_PEN` in chunks of 1024
points, the last point in a chunk is the first in the next. After the candles and
before `SelectClipRgn(NULL)` — *above* the candles (a muted 1 px line behind
saturated candle bodies disappears where it crosses the price), below the
last-price line and the crosshair. The line goes one candle out on each side and
leaves the surface through the clip. `floor` on x, because the candle outside the
left edge has a negative offset where `(int)` rounds toward zero. `y` is clamped to
±16 surface heights (GDI computes in 27 bits). **`PriceRange` is untouched:** the
price axis does not see the averages, and a line outside the price range is
clipped, as in TradingView. Both modes — a curve is not text (phase 14).

**The legend** sits at the top left of the chart area in the lines' own
colors — `SMA 20  81162.66    EMA 50  81196.25` — with the value at the candle
under the crosshair, otherwise the last visible candle. The value falls out of the
same pass that draws the line. Only in the panel, and only when the whole text
fits (~323 px; a truncated number is a wrong number).

**The toggle** follows VOL (phase 22): `showInd` (the registry, `ShowIndicators`,
on by default), `dispIndF` ∈ [0, 1] eased in `WM_TIMER` — here as *color*
toward `CLR_BG` (`Blend`), τ 55 ms, snap 0.02 — and snapped when the surface is
not visible. `MA` pill, the `M` key (without Ctrl; `Ctrl`+`M` minimizes) and
"Glidende snitt" ("Moving averages") in the tray menu (`ID_TRAY_INDICATORS` 1007), which is the way
in for desktop mode. **The toolbar was full** (x = 310 of 312 at 400 px):
the `MA` pill is the one deliberate exception to the `C_ASSERT` rule and is hidden
below 426 px by the rule that has always been there. `POPUP_MIN_W` is not
raised.

**`SEED_COUNT` 300 → 360.** The first screenshot showed the EMA line starting a
sixth of the way into the chart: with 300 of 300 candles visible, candles 0–48 are
undefined. The first fetch (and every backfill chunk) now takes 360 candles; the
default view is still the last 300, so the warm-up lies outside the left edge.
`C_ASSERT(SEED_COUNT >= DEFAULT_VIEW + IND_EMA_PERIOD)`. The response is ~60 KB of
the 96 KB `s_httpBuf`.

**Verified.** Unit tests on real code (`unit_ind`): **20/20**, red against
commit 1 (the functions do not exist). 6000 pseudo-random closes against
independent references: largest deviation SMA 0, EMA 1.5·10⁻¹⁰; rolling sum
started in the middle of the buffer over 3000 candles: 2.5·10⁻¹⁰. The tests found
one real bug before it was committed: the period guard was in `IndInit`, but not
in `IndFeedStart`. End to end (`probe_ind.c`): **54/54 in two runs**, red
run **29 FAIL**. The app's values (fields 36/37) against the probe's own
computation from the closes (field 38) on four candles, within 2 cents; SMA in
1126–1130 and EMA in 1136 of 1136 columns, zero line pixels outside the chart
area, a line pixel within 2 px of the computed (x, y) on three candles per line;
`M` fades `dispIndF` through intermediate values to 0 in ~220–250 ms and writes
the registry at once; the pill at (325, 35) switches on, the gap VOL|MA hits
nothing; the tray command; a restart with `ShowIndicators` = 0 gives zero lines
from the first frame; 410 px hides the pill while `M` works, 430 px shows it;
backfilled to ~4700–5000 candles and zoomed all the way out, the EMA is continuous
in 1136 of 1136 columns. Desktop mode looked at in `PrintWindow` at 3840×1600:
4165 + 3730 line pixels, no legend. **Painting:** the two lines cost
**52–59 µs** per frame with 300 visible candles and **179–181 µs** with 4300–4700
(median, QPC around the block in the test build, field 39) — 3 % of a ~1.9 ms
repaint. Measuring it as the *difference* between the whole repaint with and
without averages could not be done: five alternating rounds gave −139, +63, +78,
+80 and +196 µs at 300 candles in five runs — the noise floor is larger than what
is being measured (pitfall 80). **GDI/USER 32/14 before and after** 12 toggles
with fading and 1200 frames. **Exe
195 584 → 199 168 bytes (+3 584)**; commit 1 alone 0; `/TP` identical; without
`wcscat_s` the same number — the growth is the code, not the CRT.

**The probe needs an idle machine after all.** It sends no `SendInput`
and does not move the pointer, and the first version therefore started without
waiting. In a green run the user came back (last input 0.3 s old when it was
checked) and must have held `Ctrl` as the probe posted `M` — the panel was
minimized with `showInd` untouched, and `Ctrl`+`M` is the only way there: the app
read `Ctrl`+`M`, minimized the panel as it should, and
the probe — which captured a 0×0 window and counted pixels in 1280×720 — died with
an access violation after four FAIL. No product bug; the probe now waits for 25 s
without input, waits out modifier keys before each posted key, and aborts
with code 4 when the capture does not have the size it expects (pitfall 84).

**Not tested:** the crosshair's effect on the legend is seen in a
screenshot (real pointer, 80959.24 at the 02:22 candle vs 81157.62 at the last
candle), not in the probe — a posted `WM_MOUSEMOVE` does not hold hover (pitfall
35). The tray *menu* is not opened; the command it sends is. Eviction at the front
with a full buffer (6000 candles) while the lines are shown is read, not run: the
EMA is fed from the new candle 0 and moves by less than 10⁻⁹ of the price after
~1000 candles. The phase 23 and phase 24 probes were not run again; the
`WM_APP_DATA` path, the parsers and the alerts are untouched, but **`SEED_COUNT`
has changed** and older probes that expect 300 candles after the first fetch
will fail on that number.

### Phase 26 — the desktop surface: its own overlay choices and monitor change

Branch `skrivebordsflate`, merged with `--no-ff`.

**Feedback from use: "now volume and MA show on the wallpaper".** Read
as meaning they do not belong there, and that matches phase 14's own rule:
the surface is read peripherally behind the icons, and everything that has to be
decoded was removed. The volume bars had nevertheless been on the desktop since
phase 21 without any mode check, and phase 25 put the average lines on top with
the reasoning "a curve is not text". That reasoning was wrong — bars and averages
are measuring tools, not wallpaper (pitfall 85). The user was in panel mode during
phase 25 and saw both on the desktop only afterwards.

**One choice per mode, not hardcoded away.** Phase 22 put "Volume bars" in
the tray menu precisely so that desktop mode could toggle. `showVol` /
`showInd` belong to the panel (default on, unchanged); `showVolDesk` / `showIndDesk`
belong to the desktop (`ShowVolumeDesktop` / `ShowIndicatorsDesktop`, **default
off**). `ShowVolNow()` / `ShowIndNow()` give the choice for the mode the process
is in — everything that draws, eases, checks marks in the tray menu or answers a
probe (fields 18/34) reads them, and `SetShowVolume` / `SetShowIndicators` write
the mode's field. `dispVolF` / `dispIndF` snap on a mode change, and
the startup snap is moved to after the mode is known (it was right
after `LoadConfig`, before `--desktop-mode` and `DesktopMode` had been read). Two
clicks in the tray menu bring the overlays back on the desktop.

**Monitor change.** The surface is a `WS_CHILD` of WorkerW and never gets
`WM_DISPLAYCHANGE`; the hidden main window is top-level and gets it.
`PlaceDesktopSurface` is split out of `AttachToDesktop`.
`RefitDesktopSurface` runs in a per-monitor-v2 bracket like `TogglePopup`
(`GetSystemMetrics` follows the thread's context, and the main thread is unaware):
if the surface is not in the current WorkerW, it is torn down and `WM_NCDESTROY`
starts the rebuild (the path from phase 9); otherwise it is placed again.
Unchanged geometry is a no-op; a new size gives `WM_SIZE`, which discards the
watermark, and the double buffer and the stamp font (H/40) are keyed on the size.
`TIMER_REFIT_ID` does the same once more after 1 s, because Explorer places
its own WorkerW again after the same message and the origin is computed in its
coordinates. `lParam` is not read (virtualized). **`WM_DPICHANGED` is not
handled, on purpose:** the main window never gets it, and the surface computes in
physical pixels, so a pure scaling change changes nothing for it.

**Verified** (`probe_desk.c`, per-monitor aware like the surface, waits for an
idle machine): **45/45 in two runs**, red run **26 FAIL**. Clean
desktop: 0 bar and 0 line pixels in a capture at 3840×1600 with ~42 700
candle pixels (red: 35 112 / 4 144 / 3 725). The tray commands write the
desktop's registry values and leave the panel's alone; the pixels follow. The
probe shrinks the surface to 1920×800 (stamp font 38 → 19 px) and sends
`WM_DISPLAYCHANGE`: the same window back at 0,0 3840×1600, the font 38 again,
full frame. Shrunk without a message: the follow-up check fixes it within 1.6 s.
Torn out of WorkerW with `SetParent`: torn down, rebuilt, the choices
survive. In panel mode the message touches nothing. GDI/USER **30/6 before and
after** seven monitor changes. Mode change and restart give each mode its choice
from the first frame, without animation. The phase 25 probe run again: **54/54**.
**Exe 199 168 → 199 680 bytes (+512).**

**Not tested:** a real resolution or monitor change — the probe sends
the message and imitates the effect (pitfall 47). Whether Explorer tears down
WorkerW on a real change, whether a scaling change sends
`WM_DISPLAYCHANGE`, multiple monitors and changing the primary monitor are read,
not run; the machine has one monitor.

### Phase 27 — "Bloomberg Essentials": VWAP, today's high/low and values in the hover box

Branch `fase27-bloomberg-essentials`, merged with `--no-ff`.

**The order:** dashed lines for the session's high and low behind the candles with
a discreet label on the axis, VWAP as a golden line above the candles, and SMA/EMA/VWAP
as exact numbers in the hover box — without new memory and without touching
the desktop.

**Session = the UTC day, not the view.** The order said "day/view" and
"VWAP for the visible view". `PriceRange` adds 8 % air around
the view's high and low, so lines at the *view's* extremes would have stood in the
same place in every frame; and a VWAP anchored at the first visible candle would
have jumped with every candle during panning (pitfall 90). `SessionStartAt` finds
the day's first candle with a binary search in `openTime`; on 1d candles there is
no session. **VWAP resets per day** (`DrawVwap`), typical price (H + L + C) / 3,
and the line breaks at the day rollover — so it is defined also when the view is in
yesterday. Today's high/low applies only today and runs from the day's first candle
in to the axis. Everything is step machines and pure functions as in phase 25: no
table, no new buffers (`s_volPts` is borrowed for `Polyline` and `PolyPolyline`).

**An incomplete day is not drawn — and is fetched.** 360 candles is six hours
at 1m, and "today's high" of the last six hours is a wrong number.
`WM_APP_DATA` asks for older candles (`RequestHistory`, phase 18) until the day is
covered: at most four fetches, only with the panel visible and the indicators on in
the mode the process is in.

**Behind the indicators toggle.** The toolbar is full (`C_ASSERT`, phase 25), so
everything follows `ShowIndNow()` / `dispIndF`: `M`, the `MA` pill and the tray
item, which is now called "Indikatorer" (Indicators). The desktop has
`ShowIndicatorsDesktop` = 0 by default (phase 26) and is thus untouched; no new
registry keys. The dashed lines fade with the rest, and are therefore strokes for
`PolyPolyline` with `DC_PEN` and not a `PS_DASH` pen — the GDI count at rest is
unchanged. The pattern (6 on / 6 off) is anchored at the surface's left edge.
`CLR_VWAP` F2D14B is yellower and lighter than the alerts' amber; `CLR_SESSION`
90939E is a neutral gray (pitfall 87 explains why not 8A93A0). The axis tags are
muted (`CLR_BOX` surface, gray text, no frame) and lowest in rank in the collision
system from phase 23. The legend got a third item in gold, which drops out alone
when the row is narrow; if a session line crosses the legend's row (low panel), the
text gets an opaque background. The hover box is 126 px tall with the indicators on
(87 + 3 × 13) and 87 as before without.

**Verified.** `probe_sess.c` (1280×720, real pointer for hover, waits
for an idle machine): **54/54 in two runs** (and 53/53 before 06:00 UTC, when
the backfill was not needed), red run **14 FAIL** before 06:00 and **18 FAIL**
after. At 06:01 UTC the buffer fetched itself from 360 to 720 candles on its own
and stopped; commit 1 in the same minute stood at `fullstendig = 0`. Today's
high/low and VWAP match the probe's own computation from fields 14 and 47–50 (VWAP
within 5 cents on four candles). Both dashed lines lie on the computed row in exact
`CLR_SESSION` (548 of 1098 px, longest stroke 6 px, nothing before the day's
first candle), VWAP in 1081 of 1081 columns within 2 px of the computed (x, y), 0 px
outside the chart area. Hover box 126 px on / 87 off. 1d: no session, no
backfill, "VWAP  -". 1h: VWAP computed from each day's start, 1088 of 1136
columns. **Cost, measured directly (field 45): 15–21 µs per frame** (the averages
from phase 25: 48–57 µs). GDI/USER **32/14 before and after**. Unit tests 24/24.
The desktop probe (extended) 47/47: **0 px VWAP and 0 px high/low by
default** at 3840×1600, 3860 / 1824 px when they are switched on. The phase 25 probe
54/54 after it learned to wait for the backfill. A capture at 560×300
showed today's high straight through the legend's digits; fixed in commit 3.
The trade-off: the background then also wipes out wicks and average pixels under
the text. **Exe 199 680 → 203 776 bytes (+4 096)**, `/TP` byte-identical.

**Not tested:** a real day rollover with the panel open, and backfill during
network errors (read, not run).

### Phase 28 — yesterday's levels: the previous day's high, low and close

Branch `fase28-gaarsdagens-nivaaer`, merged with `--no-ff`. The mandate was
"continue"; the candidate was in the phase 27 plan.

**Three levels for today.** The previous UTC day's high, low and close are drawn as
horizontal lines from *today's* first candle in to the axis, like today's
high/low: the candles that made them need no stroke over them. `PrevSession`
finds yesterday as [start, today's start) with the same binary searches as
`SessionStartAt`; if the candle before today's first is not in the previous day
(a gap), there is no yesterday. An incomplete yesterday is not drawn, as in phase 27.

**The backfill goes to the previous day rollover.** `SessionsNeedHistory` replaces
the phase 27 condition in `WM_APP_DATA`: true until both today's and yesterday's
session are whole, `histDone`, or yesterday does not exist. At 1m that is up to
2880 candles (eight fetches), at 5m one fetch, from 15m none. Each fetch posts
`WM_APP_DATA`, which asks again — measured **3.3–3.7 s from 360 to 2880 candles**.
Still only with the panel visible and the indicators on in the mode the process is
in; the desktop fetches nothing (measured: 360 candles, field 54 = 0).

**Appearance.** `CLR_PREV` 6F7B95 — cooler and darker than `CLR_SESSION`,
"the same thing, older" — and not on the blend line from `CLR_BG` or `CLR_BOX`
to any text color (smallest gap per channel 0.089). Same period (12) and
anchor as today's lines: high/low **2 on / 10 off**, the close **10 on / 2
off**. The first draft was 2/4; the crosshair's `PS_DOT` is a dense dot pattern in
gray, and 2/10 lies further away. `DrawDashLine` takes the pattern as a
parameter. The five levels (today's high, low, yesterday's close, high, low) are
now in **one table in the axis column's rank**, so the collision rule, the tags,
the grid labels' duty to yield and the legend's "struck" rule are one loop
each instead of a copy per class. No rows in the hover box (the levels are
constants), no pill, no registry key: everything follows `ShowIndNow()`.

**Verified.** `probe_prev.c` (1280×720, waits for an idle machine, does not start
23:50–00:05 UTC): **52/52 in two runs, red against commit 1: 21 FAIL**
(the backfill stopped at 1442 candles, field 54 stood at 1, the levels −1, 0 px).
The values match the probe's linear computation from fields 38 and 47–50
(81400.00 / 76296.00 / 80883.87); yesterday is 1440 candles at 1m, 288 at 5m
(360 → 720 on its own), both days open at 00:00 UTC. Pixels at 15m:
all three lines on the computed row in exact `CLR_PREV` — close 249 of 312 px,
longest stroke 10, 22 whole; high 47 px, longest 2; low 52 px, longest 2 —
nothing before today's first candle, 0 px above and below the chart area. The axis
tag for low stood (1083 px `CLR_BOX`, number in `CLR_PREV`); close yielded to
today's low 4 px away and high to the stamp 2 px away, as the rank says. `M` and
tray command 1007: 348 → 0 px. 1d: no yesterday, no backfill.
**Cost (field 56): 1 µs per frame** with 1440 candles to read; the whole
session block 26 µs (21 before), the averages 48 µs with 2880 candles in the buffer.
GDI/USER **32/14 before and after**. Unit tests `unit_prev` 27/27 (red against
master: does not build). Regression: `probe_ind` 54/54, `probe_desk` 49/49 (**0
px `CLR_PREV` and no backfill on the desktop by default**; 545 px and
2881 candles when the indicators are switched on there), `probe_sess` 52/52 after
the fix of field 14 below (two checks about an incomplete yesterday drop out now
that it is always fetched). Looked at:
captures at 1280×720 and 560×300 (15m).
**Exe 203 776 → 204 800 bytes (+1 024)**, commit 1: 0, `/TP` byte-identical.

**Found along the way:** `probe_sess` (phase 27) failed on "VWAP within 5 cents"
with 5.7 cents. The VWAP code is untouched. The deviations were one-sided (0, +4.0,
+2.6, +5.7 cents), that is, systematic: probe field 14 **truncated** the volume to
hundredths instead of rounding, so every weight in the probe's sum was slightly too
small, and the bias grows with the number of candles — phase 27 ran at 06 UTC with
~360 candles in the session, this one at 19 with 1152. Field 14 now rounds (only
the test build; prod exe unchanged), and the deviation is under 0.5 cents on all
four candles with the tolerance back at 5 cents (pitfall 91).

**Not tested:** a real day rollover with the panel open (unit tested: right
after 00:00, yesterday is "the rest of the buffer, incomplete", and the backfill
starts again), a gap in Binance's history (unit tested), and backfill
during network errors.

### Phase 29 — readable levels: labels and a crosshair tag that does not cut numbers

Branch
`fase29-lesbare-nivaaer`, merged with `--no-ff`. The mandate was "ok, go".
Two readability bugs were chosen ahead of new indicators: phase 28 left five
horizontal lines without names, and the crosshair's axis tag — the only tag
that moves with the hand — was the only one without a collision rule.

**The labels.** HOD, LOD, PDC, PDH, PDL (the trading jargon, like `VWAP` and
`O H L C V` elsewhere) at the line's *left* end, `max(xs, left) + 4`, just
above the line, and below it when there is no room above. Not at the axis:
the newest candles are there, and the axis tag carries the number. Drawn in
the legend's block, not in the level block — that one lies behind the
candles, and text there would be painted over. The axis font, the line's
color, faded with `dispIndF`; the text ends two rows above the line, so the
dash pattern and the pixel probes stay clean. Collision on **rectangles in
rank**, not on y distance (the axis font is taller than 12 px): a label that
would hit the legend or a label ahead of it in rank is not drawn. Below a
200 px surface they all drop out. Not on the desktop.

**The crosshair tag in the rank:** the stamp, then the crosshair tag, then
the rest. `yCross` is computed next to `yPill` with the same visibility test
and clamping as the crosshair block, and goes into `yTag[]`: grid labels and
level tags less than 16 px away are not drawn, an alert tag keeps its surface
and loses its number. Within 16 px of the *stamp* the crosshair tag itself is
not drawn — the last price is the number that must never be cut, and the
hover box carries the candle's numbers. The line is drawn as before. The
pointer in the axis column gives `hoverIdx = −1` (`HitCandle`), so the ghost
tag and the crosshair tag never exist at the same time.

**Verified.** `probe_lbl.c` (1280×720, 15m, real pointer for hover):
**28/28 in two runs**, red against commit 1: **5 FAIL** — among them **101
text pixels of a cut-off label** in the strip above the crosshair tag (the
pointer 11 px below the label) and 1139 px of crosshair tag on top of the
stamp. Green: 0 pixels in the strip, the tag is there (1143 px), 40 px away
the label stands whole (94 px); 9 px from the stamp: 0 px of crosshair tag,
field 58 = 0. Field 57 = 0x1B: HOD, LOD, PDH and PDL got a label (64–93 text
pixels each, 0 px on the neighboring rows, longest run on the line's row 6
and 2 as before), PDC gave way to LOD 3 px away.
**Cost (field 59): 1 µs per frame.** GDI/USER **32/14 before and after**.
Regression: `probe_prev` 52/52, `probe_sess` 52/52, `probe_ind` 54/54,
`probe_desk` 49/49. Looked at: 1280×720 and 560×300.
**Exe 204 800 → 205 824 bytes (+1 024)**, commit 1: 0, `/TP` byte-identical.

**Not tested:** the crosshair tag against an *alert tag* and a level tag is
read, not captured (the same `yTag` loop as the grid labels, which is
captured).

### Phase 30 — the name TickC: registry, autostart and exe renamed, with migration

Branch
`fase30-navnet-tickc`, merged with `--no-ff`. The user chose "fully, with
migration" out of three options before the GitHub release.

**What changed.** `HKCU\Software\Ticker` became `HKCU\Software\TickC`, the
Run value `Ticker` became `TickC`, `ticker.exe` became `TickC.exe`, and
`ticker.c`/`ticker.manifest` became `tickc.c`/`tickc.manifest` (`git mv`, part
3). Visible text: tray menu "Avslutt TickC", window titles "TickC" (were "BTC
Chart" and "BTC Core Engine"), User-Agent `TickC/1.0` (was `BTCTicker
Engine/2.0`). README.md and LICENSE (MIT) were added.

**Kept on purpose:** the window classes `BTCTickerWindowClass` and
`BTCPopupClass` (invisible to users, and every probe finds windows by them),
and the macro `TICKER_PROBE`.

**`MigrateLegacyNames`** runs on every start, just before `LoadConfig`. The
settings tree is copied with `RegCopyTreeW` only when the new key does *not*
exist; if both exist, the new one wins and the old one is left alone. The old
key is deleted only after the copy succeeded; a failed copy deletes the
half-written new key so the next start retries. Price alerts ride along in the
tree. Autostart is written with the path of the exe that is *running*, since
the old value points at `ticker.exe`. The old Run value is also removed when a
new one already exists (otherwise both programs start at sign-in), but never
before the new one is in place.

**The test build can no longer touch real keys.** The test names now live in
the source under `#ifdef TICKER_PROBE`: `REG_PATH` `Software\TickerTest` (as
before, so no older probe had to change), `REG_PATH_OLD`
`Software\TickerTestOld`, `AUTOSTART_KEY` `Software\TickerTestRun`, values
`TickerTest`/`TickerTestOld`. Before phase 30, `mk_test_src.py` rewrote the
`REG_PATH` line; with migration in the code, a test build given the real old
names would have moved the user's settings into the test key and pointed the
user's autostart at `ticker_test.exe`. `mk_test_src.py` is now a plain copy
that refuses a source without the test names.

**Probe field 115** on the main window: the migration bitmask (bit 0 settings
copied, 1 old key deleted, 2 autostart written under the new name, 3 old Run
value deleted).

**Verified.** `probe_migrate.c`, standalone (no keys or clicks, needs no idle
machine), five scenarios: A old key plus old autostart (mask 15, `SymbolIndex`
2 still there after `SaveConfig` wrote on exit, so the moved values were
*read*; the alert came along; autostart points at the running exe), B second
start (mask 0, nothing changed), C both keys present (mask 8, both keys
untouched, old Run value removed), D only old autostart (mask 12), E no
autostart before (mask 3, none written). **24/24 in two runs**, red against
part 1: **19 FAIL**. The first green attempt gave 10 FAIL: `RegCopyTreeW`
answered `ERROR_ACCESS_DENIED` with the target opened for `KEY_WRITE`
(pitfall 97).
Regression: `probe_lbl` 27/27, `probe_sess` 51/51, `probe_ind` 54/54,
`probe_desk` 49/49, `probe_prev` 51/52. All three differences from phase 29
depend on the market or the clock, not on this phase: the `probe_prev` FAIL is
the dash pattern of yesterday's close, which lay one row (148 vs 149) from
today's high (86 625.82 vs 86 620.00), so the lines overlapped; `probe_lbl`
skips "LOD and PDC close together" when they are not, and `probe_sess` skips
the backfill check before 06:00 UTC.
**Production:** the user's registry and Run value were exported to the
scratchpad first. `ticker.exe` (pid 3100) was stopped cleanly *before*
`TickC.exe` started, since the old process writes `Software\Ticker` on exit.
After the start, all 12 values were in `Software\TickC` (`DesktopMode` 1,
the desktop surface visible), `Software\Ticker` was gone, and Run held
`TickC` = `"C:\Users\sysadmin\Desktop\Ticker\TickC.exe"` with no `Ticker`
value. The stale `ticker.exe` was deleted: started again, it would recreate
`Software\Ticker`.
**Exe 205 824 → 206 336 bytes (+512)**, part 1: 0.

**Language from here on.** The user wants the whole repo in English (UI text,
comments, this log, the plans) before the release. New commits and new text
are English from phase 30 on; translating what exists is the next phases.

### Phase 31 — English UI: every string the user sees

Branch
`fase31-english-ui`, merged with `--no-ff`.

**What changed.** All 25 Norwegian string literals: the tray menu
(`Interval`, `Volume bars`, `Indicators`, `Clear price alerts (%d)`,
`Desktop mode`, `Default view`, `Start at sign-in`, `Quit TickC`), the start
tooltip `Connecting to Binance...`, `Loading data from Binance...`,
`No connection - retrying in %ds`, `offline`, the overlay header `INTERVAL`
and the alert balloon (`Price crossed the alert going up/down. Last price:
...`). Hours are `h` (`1h`, `4h`, `2h 30m`). Dates are ISO: `2026-09-22` on
1d, `09-22 14:30` on 1h and 4h — the same width as `22.09.2026` and
`22.09 14:30`, so the monospace time axis lays out the same. The last
non-ASCII escape in the file (`\x00e5` in "paalogging", the old word for sign-in) is gone.

**Verified.** `scan_ui_strings.py` scans every literal outside comments for
Norwegian words, `æøå` escapes, the `t` hour suffix and `dd.mm` dates:
**25 on master, 0 after.** Screenshots at 1280×720 (1h) and 900×400 (1d)
show the `1h` pill, the span `12d 12h` and the axis labels `09-12 00:00` and
`2026-02-16`. No probe reads UI text (grepped first), so none changed.
Regression: `probe_migrate` 24/24, `probe_lbl` 27/27, `probe_sess` 51/51,
`probe_ind` 54/54, `probe_desk` 49/49, `probe_prev` 51/52. The `probe_prev`
FAIL is the one from phase 30, and a **control run against a phase 29 test
build** (a744b50, `REG_PATH` rewritten to the test key) fails the same way
on the same row with the same 40 of 54 pixels: the data, not the code.
**Exe unchanged at 206 336 bytes.**

### Phase 32 — English comments: every comment in tickc.c

Branch
`fase32-english-comments`, merged with `--no-ff`.

**What changed.** All ~1 470 Norwegian comments and the three `#error`
messages, in US spelling and one glossary (phase, pitfall, candle, panel,
surface, stamp, view, backfill, alert, main instance, duplicate, ...). The
file was cut into ten chunks at top-level boundaries, translated in
parallel, joined, and given one consistency pass (spelling, decimal points,
one name per thing). UI text quoted in comments now matches phase 31. One
stale fact was corrected: the `MAX_CANDLES` comment said 40 bytes per candle,
but `Candle` has been 48 bytes since phase 21, so the buffer is 288 KB.

**Verified.** `cl /EP` (comments stripped by the preprocessor) for the
production and the `TICKER_PROBE` configuration: the code with all whitespace
removed is byte-identical. All 199 string and char literals are identical and
in order. The file is ASCII and CRLF. The verifier was tested first against a
changed constant and a changed string (both caught) and a comment-only change
(passed). **The production exe is byte-identical** to the one built from the
old source except for the two copies of the link timestamp (0x108 and
0x2dadc), so the pixel probes were not rerun. `probe_migrate` 24/24.
Exe 206 336 bytes, unchanged.

### Phase 33 — English docs: this work log

Branch `fase33-english-docs`,
merged with `--no-ff`.

**Part 1, names.** `ARBEIDSLOGG.md` became `WORKLOG.md` and the design spec
moved to `docs/specs/`. The references in the docs, README and manifest
were rewritten by script. The manifest's comment is English.

**Part 2, translation.** This log in nine chunks and the design spec, by
parallel agents from one rules file:
one glossary, fixed heading names (so "see **The window** below" and similar
references still match), ISO dates, decimal points, current English UI labels
where prose names a menu item, US spelling, comments inside code blocks
translated. **Kept on purpose:** inline code byte for byte, and inside code
blocks the `git commit -m` message bodies, test assertion strings and old
menu sketches — verbatim records of the Norwegian history, like the git log.

**Verified.** `doc_check.py` per file against the original: code blocks
identical apart from comments, inline code identical as a multiset, links,
counts of headings, numbered items and table rows, no Norwegian-looking prose,
no Norwegian letters or guillemets outside code. This log: 2 470 inline code spans, 72 headings,
111 numbered items, 382 table rows. The agents found two bugs in the checker
(inline code wrapping onto a new line, code blocks indented under list items);
both were fixed and every file was checked again. A final scan of the repo:
0 Norwegian comments, 0 Norwegian UI strings, no Norwegian prose. No code
changed; exe 206 336 bytes.

**The per-phase plan documents are not part of the public repository.** Each
phase section above carries the plan's decisions, measurements and deviations.

### Phase 34 — the chart engine in its own file, pixel-identical

Branch `fase34-chart-split`, merged with `--no-ff`. The user asked for the
chart code to become reusable in other projects, and for what would make it
better afterwards; this is the first step, with no behavior change.

**What moved.** `chart.h` / `chart.c` hold everything that is a pure function
of candles and a DC: geometry (`ChartGeometry`, now with a `desktop`
parameter instead of the global), hit testing, the view (`ClampView`,
`GetView`, `PanView`, `ZoomView`), the display (`ApplyFrontShift`,
`SyncDisp`, `PriceRange`, `VolumeMax`), the sessions, the indicator step
machines, formatting, `Blend`, and the body of the old `DrawChart` as
`ChartDrawBody`. The engine sees three structs: `ChartState` (the target
view, the eased display, the hover; embedded in `AppContext` as `ch`),
`ChartData` (the buffer, interval, alerts and mode for one frame) and
`ChartStyle` (the app's fonts, pens and brushes). No window, lock, network,
registry or global in it. What stayed in `tickc.c` became `DrawHeader` (price,
change, offline text), `DrawEmptyState` and `DrawChartFrame`, which builds the
two views and calls the engine. The desktop stamp font is now built in
`DrawChartFrame` before the body, not lazily inside the stamp block.

**Recorded responses (test build).** `TICKER_FIXTURE_DIR` makes `HttpGet`
answer from files (`klines_<symbol>_<interval>.json`, `price_<symbol>.json`;
a missing `hist_` file is "the history has ended"). Every capture then shows
the same candles, and a probe runs offline. Probe field 104 on the panel sets
the hover as a mouse move would, without a pointer: a posted `WM_MOUSEMOVE` is
undone by `WM_MOUSELEAVE` within a tick.

**Verified.** `golden.ps1`: eight captures from the fixtures (1280x720 at 1m,
15m, 1h, 1d; 560x300 at 15m; 400x250 at 1h; 1h with the crosshair; the
desktop surface at 3840x1600), each hashed on its raw pixels. Two runs of the
same exe are identical, and **the split build is identical to the build
before it in all eight**. `probe_migrate` 24/24. The five regression probes against the split build:
`sess` 51/51, `ind` 54/54, `desk` 49/49, `prev` 47/52 and `lbl` 25/27 - the
last two ran at 00:11 UTC, eleven minutes into today's session, when the
level lines are 14 px wide and carry neither a dash pattern nor a label;
control runs against the build before the split fail on exactly the same
checks. The production `/EP` output was checked unchanged by the fixture
commit.
**Exe 206 336 → 207 360 bytes (+1 024)**, one alignment step from the second
translation unit; `/TP` builds the same size.

**Next for the engine:** golden-image unit tests that draw into a memory DC
without a window, DPI awareness (a `dpi` field in `ChartStyle`), the colors
as data, a time axis that compresses gaps, more panes, and a Direct2D backend
behind the same header.

### Phase 35 — golden tests for the chart engine in a memory DC

Branch `phase-35`, merged with `--no-ff`. The first of the engine steps listed
after phase 34: tests that draw `chart.c` with no window, no app, no network
and no clock. The user chose to keep them in the repository (`tests/`), not in
the scratchpad like the probes.

**Three engine commits first, each pixel-identical.** The test must draw with
what TickC draws with, not with a copy that drifts:
`ChartStyleCreate` / `ChartStyleDestroy` own the chart's fonts, pens and
brushes (`AppContext` holds one `ChartStyle sty`; the header, buttons and
overlay borrow `fontSmall`, `brBox` and `brBoxEdge` from it), and
`ChartPillFontCreate(H)` builds the desktop stamp font, which the app still
owns and rebuilds only when the height changes. The second commit (before
the stamp font) takes the clock out of the drawing: the time labels and the hover box called
`FileTimeToLocalFileTime`, so the picture depended on the machine's DST
state and a golden written in September would break on October 25.
`ChartData.utcOffsetMs` now carries the offset; the app passes
`ChartUtcOffsetMs()`, which is exactly the offset `FileTimeToLocalFileTime`
added (the one in force now, applied to every date - unchanged behavior).

**The test.** `tests/chart_golden.c` builds seeded random-walk candles (the
last one closes at 2026-09-21 14:00 UTC; the 1m buffer reaches past the
previous day's start), sets the state the way TickC does (`SyncDisp`, the
toggle fades, a hover through `HitCandle` like probe field 104), draws with
`ChartDrawBackground` + `ChartDrawBody` into a 32 bpp DIB section and hashes
the RGB of every pixel (FNV-1a 64). Fourteen cases: 1m, 15m, 1h and 1d at
1280x720, 560x300 and 400x250, the crosshair, a panned and zoomed view, the
overlays off and half faded, alerts with a ghost tag and an afterglow, twelve
candles, and the desktop surface at 1920x1080 and 3840x1600 (two stamp
fonts). Every case is drawn twice (the hashes must agree) and once more into
a `CreateCompatibleBitmap` bitmap on the screen DC, the kind TickC's back
buffer is; the two must be pixel-identical, and are in all fourteen.
`--update` rewrites `tests/golden/chart.txt`, `--bmp` writes the pictures to
`tests/out/`, and a failing case is always written there.

**Verified.** Red run: the test built against a scratchpad copy of the engine
with `SESS_DASH_ON` 6 → 5 fails in 9 of 14 cases - exactly those that draw
today's dashed lines; the five that pass have none in view (1d, the view
panned back from today, overlays off, and both desktop cases). Green: two
runs of the real build, 14/14. `golden.ps1` against the test build after
each engine commit: all eight hashes identical to `main`. The pictures were
looked at before the goldens were written: the first draft clamped "show
all" to eight candles and put one alert outside the view (pitfall 99).
**Exe 207 360 → 206 848 bytes (−512).**

**Next for the engine:** DPI awareness (a `dpi` field in `ChartStyle`), the
colors as data, a time axis that compresses gaps, more panes, and a Direct2D
backend behind the same header. Each can now be proven with a golden case
before and after.

### Phase 36 — the chart engine scales with dpi

Branch `phase-36`, merged with `--no-ff`. The second engine step. The panel
is DPI-unaware, so at 150 % Windows stretches it as a bitmap and the text
is soft; the engine drew every length at 96 dpi. This phase is the engine
half: it can now draw at any dpi. TickC still passes 96, and making the
panel per-monitor aware (header, buttons, toolbar, overlay, minimum size
and saved geometry in device pixels) is the app's own phase.

**What changed.** `ChartStyle.dpi` is the dpi the fonts were built for
(`ChartStyleCreate(sty, dpi)`: em 11 and 15 scaled). `ChartGeometry` takes
the dpi and records it in `ChartRect.dpi`, so `AlertAxisHit` scales its
zone the way the drawing scales the tag. Every fixed length in `chart.c`
goes through `ChartPx` (`MulDiv`, rounding to nearest, the identity at 96):
the margins, the price column (`ChartAxisW`: the character width follows
the axis font the way `DeskAxisW` already did, rounded up, so eight
characters always fit), the tag height and every collision distance of 16,
the dash patterns, the legend and label offsets, the time axis gaps, the
hover box and the panel's stamp. The desktop stamp still follows the
surface height. **Lines stay one device pixel wide at every dpi**: a
styled GDI pen (`PS_DOT` for the crosshair, `PS_DASH` for the last price)
keeps its pattern only at width 1, and a scaled width is a decision of its
own.

**Verified.** `chart_golden`: the 14 cases at 96 dpi are unchanged (the
proof that nothing moved at 100 %), and five new cases at 144 and 192 dpi
were looked at before their goldens were written: 840x450 at 144 is the
560x300 panel at 96, 1.5 times larger, with the same labels in the same
places. Red run with the tag height left unscaled (`tagHalf = 8`): exactly
the five dpi cases fail. Green 19/19 twice. `golden.ps1` against the test
build: all eight identical to `main` - after the capture script got a guard
(pitfall 100). **Exe 206 848 → 208 384 bytes (+1 536).**

### Phase 37 — the panel is per-monitor DPI aware

Branch `phase-37`, merged with `--no-ff`. The app half of the DPI work: the
process calls `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)` first in
`WinMain`, so on a 150 % monitor the panel is drawn in device pixels at 144
dpi instead of being drawn at 96 and stretched by Windows.

**What changed.** `Dp(v)` is the app's `ChartPx` at the panel's dpi
(`g_Ctx.sty.dpi`), and every fixed length in the header (the rows at 10 and
30, `PAD_L`, `HDR_GAP`), the button row and its glyphs, the toolbar (and its
arrow), the overlay, the watermark (clamp limits, insets, and the alpha,
which is defined on the logical width), the resize border, the caption
test (`HEADER_H`), the spawn offset, the factory size and the minimum size
goes through it. `PanelDpi` is the monitor's dpi for the panel and 96 for
the desktop surface (unchanged look; its one text, the stamp, already
follows the height). `ApplyPanelDpi` rebuilds the chart style and the price
font when the panel opens and on `WM_DPICHANGED`, which also takes the
rectangle Windows suggests. The watermark reads the panel's dpi instead of
`GetDeviceCaps(LOGPIXELSY)` - in an aware process that is the system dpi,
and the desktop surface would have got larger watermark limits. Lines and
glyph strokes stay one device pixel.

**The minimum width follows the toolbar.** At 96 the `C_ASSERT` keeps every
pill through VOL inside 400 px with 2 px to spare. At 150 % the lengths
round separately (the price column alone is 130, not 126), and 1.5 x 400 =
600 would have hidden VOL. `ToolbarMinW` is the same sum at the current dpi;
the minimum is the larger of the two (601 at 144, 400 at 96).

**Geometry.** The unaware process saved position and size in Windows'
virtualized coordinates. From now on they are device pixels, marked by
`PanelGeomDevice = 1`; values without the mark are scaled once by
`GetDpiForSystem()`. That path cannot run on this machine, which is at
100 % (see below).

**Test build.** `TICKER_FORCE_DPI` draws the panel at a fixed dpi, probe
field 60 reads the dpi, and writing field 105 sends the panel a real
`WM_DPICHANGED` to the dpi in `lParam`, with the rectangle scaled by
new/old (a `RECT` pointer cannot cross from the probe's process).
`shot_dpi.ps1` goes 96 → 144 → 96. While testing, the fixture lookup turned
out to depend on a stale `GetLastError` (pitfall 103); it is cleared first
now.

**Verified.** This machine is at **100 %** now, not 150 % as noted in phase
26 - `GetDpiForSystem` gives 96 - so no capture ran at a real 150 %, and
the scaling was checked with the forced dpi and the synthetic
`WM_DPICHANGED`. `golden.ps1` with the test build forced to 96 and
unforced (the real aware path at 96): all eight identical to `main` in
both. `shot_dpi.ps1`: dpi 144 and 1920x1080 after field 105, the overlay
opened by a click on the symbol pill at its 144 position, minimum
601x375 with VOL still shown, back to 96 and 1280x720, and the 96 capture
after the round trip pixel-identical to the one before; the geometry is
saved with `PanelGeomDevice = 1`. Red run with `ApplyPanelDpi` left out
of `WM_DPICHANGED`: four checks fail (dpi stays 96, the overlay click
misses, the size does not come back, the round trip differs). The 144
captures were looked at: header, buttons, toolbar, overlay, axes, hover
box and watermark all scaled. `chart_golden` 19/19 (the engine is
unchanged). **Exe 208 384 → 211 456 bytes (+3 072).**

**Real scale change (after the merge).** `shot_scale.ps1` opens the test
panel at 100 %, sets the primary monitor to 150 % with
`SPI_SETLOGICALDPIOVERRIDE` (a scratchpad tool, `dpi_scale.exe`; the
script restores the scale in `finally`) and back. Windows' own
`WM_DPICHANGED` took the panel to 144 dpi and 1920x1080 client, the
capture at 150 % was looked at (fixtures, everything scaled), and after
100 → 150 → 100 the panel was at 96 and 1280x720, pixel-identical to the
capture before. 7/7, and the monitor was back at 96 dpi afterwards.

### Phase 38 — the chart's colors as data

Branch `phase-38`, merged with `--no-ff`. The third engine step. Every
color `chart.c` draws with - 21 roles, from the background to the alert
line - now comes from a `ChartTheme` that `ChartStyleCreate(sty, dpi,
theme)` copies into `ChartStyle.clr` and builds the pens and brushes from.
`ChartThemeDark` is the `CLR_` macros field for field, and TickC passes it;
the macros stay, since the app's header, buttons and overlay still use them
(theming the app is a step of its own, with a setting in the tray menu).
`ChartThemeLight` is the second built-in table: near-white background, the
deeper green/red of light trading charts (00FF66 is unreadable on white),
the overlays darker, VWAP dark gold instead of yellow. The replacement was
done in the code part of each line only, so the comments keep naming the
macros they reason about.

**Verified.** `chart_golden`: the 19 dark cases unchanged; three light
cases (15m with alerts and levels, 1h with the crosshair, the desktop
surface), looked at before their goldens were written. Red run with
`volUp` and `volDown` swapped in the dark table: exactly the 16 dark cases
with volume bars in view fail (not "overlays off", not the two desktop
cases, not the light ones). Green 22/22 twice. `golden.ps1`: all eight
identical to `main`. **Exe 211 456 bytes, unchanged.**

### Phase 39 — an RSI band under the chart

Branch `phase-39`, merged with `--no-ff`. The fourth engine step and the
first one the user can see: a second pane. RSI 14 was chosen before MACD
because one line on a fixed 0..100 scale proves the pane without a second
free axis.

**Engine.** RSI with Wilder's smoothing (the first averages are plain means
of 14 changes, then `(avg * 13 + x) / 14`), a step machine next to the
averages and VWAP, always fed from candle 0: like the EMA it has infinite
memory, and started at the view it would move during panning. The band
takes `RSI_BAND_FRAC` (a fifth) of the chart height, at least 40 px, under
the price pane with a 6 px gap and a grid line on its top edge, and it is
left out when the price pane would get less than 120 px (a 400x250 panel
keeps 142). **`ChartRect.bottom` and `ch` stay the price pane's**: every
price <-> y function, the alert hit test, the level lines, the volume bars
and the watermark read them unchanged, and the band is two new fields,
`bandTop`/`bandBottom`. `HitCandle` and the vertical crosshair cover the
band; the time axis sits under it. In the band: 70 and 30 dashed in the
crosshair's gray, the line in the theme's new `rsi` role (teal - not up
green, not an average's blue or violet, not VWAP's gold), the last value as
a tag in the column, "RSI 14" and the value at the crosshair as a legend,
and a level tag for the crosshair. The column's rank is the price column's:
the value tag first, the crosshair's tag not within a tag height of it
(only a strip of its number would show), the 70/30 labels giving way to
both. In a low band the 70 line runs through the legend, which then gets an
opaque background - the price legend's rule. The hover box gets an RSI row.
The region follows the choice at once (it is geometry, which the app's nine
hit-test calls read without state); the content fades with `dispRsiF`.

**App.** One choice per mode, `ShowRsi` and `ShowRsiDesktop`, **off in
both**: the band takes a fifth of the chart, which is a change to the panel
nobody asked for, and the desktop is meant to be quiet. An RSI pill after
MA (the first to go on a narrow panel, `ToolbarMinW` still sums through
VOL), the I key, "RSI band" in the tray menu. Turning it on rebuilds the
watermark (it is centered in the price pane) and drops the hover. Probe
fields 61 (the choice), 62 (`dispRsiF` x1000) and 63 (RSI at the candle in
`lParam` x100, -1 undefined).

**Verified.** `chart_golden`: the 22 earlier cases unchanged with the band
off; six band cases (crosshair in the price pane and in the band, 400x250,
150 %, light theme, desktop), looked at before their goldens were written -
which found the crosshair's tag half over the value tag and the legend
struck by the 70 line, both fixed. Red run with `RSI_PERIOD` 13: exactly the
six band cases fail. Green 28/28 twice. `golden.ps1` with the band off: the
desktop and 400x250 identical to `main`; in the six others every differing
pixel lies in (347-362, 32-39), the label of the new RSI pill, and nowhere
else. `shot_rsi.ps1`: off by default, on and off through tray command 1008,
RSI undefined on candle 13, field 63 equal to an independent RSI 14 (in the
script, over the closes from field 38) on nine candles, and the capture
after on/off pixel-identical to the one before. The first run of the script
failed three checks; one was the script reading `-1` as 4294967295 (a
32-bit `LRESULT` into a 64-bit caller - it now sign-extends); the other two
did not come back in two further runs, and their cause was not found.
**Exe 211 456 → 217 088 bytes (+5 632).**

### Phase 40 — the light theme as a setting

Branch `phase-40`, merged with `--no-ff`. Phase 38 made the chart's colors
data and left the light table unused; this phase makes it a choice in the
app, for the whole panel, and holds it to WCAG AA. Three commits.

**Baseline first.** The RSI pill changed the toolbar in phase 39, so a new
`golden.ps1` reference was taken from `main` (391cf14) before any edit. Two
runs gave the same eight hashes, and they equal the phase 39 captures.

**1. The app's colors from the theme, pixel-identical.** The header, the
button glyphs, the toolbar, the overlay, the empty-state text and the
watermark read `ctx->sty.clr` instead of the `CLR_` macros (the comments
keep the macro names, as in phase 38). The button pens and the close
button's brush are built with the style, and `ApplyPanelDpi` became
`ApplyPanelStyle`. Its early return compares the theme as well as the
dpi (phase 38 had flagged that a dpi-only check would leave a theme
switch undrawn). The watermark ink is the one color only the app draws,
so it sits in a small app table, `AppTheme` (the chart theme plus
`wmInk`): white on dark, as before, and the light theme's text color on
light. White on `FAFAFB` would be invisible. Measured on the captures, the
light watermark is as faint as the dark one: 1.14:1 against 1.16:1. Two
engine roles were added, `onAlert` and `alertText` (see below), with the
dark values `CLR_BG` and `CLR_ALERT`. The theme was still hardwired dark:
`golden.ps1` was identical to the new reference in all eight captures,
`chart_golden` 28/28.

**2. The light table reaches 4.5:1.** An audit of every text/surface pair
the chart and the app draw (33 pairs) found **20 under WCAG AA** in the
phase 38 table. The alert tag's number was **2.65:1** (near-white on
amber `D98A00`; the user had estimated about 3:1). The ghost tag was 2.77,
the price stamp 3.42/3.74, the VWAP legend 3.39 and yesterday's level
labels 2.95. The dark theme was not changed; see known limitations. Each
light role was darkened with its hue kept (HLS), to at least 4.60:1 on
the background. The binding case is the background `FAFAFB`, since the box
is white. One constraint decided the design: **a color cannot both carry
light text at 4.5:1 and be text at 4.5:1 on a light background**. The two
need relative luminance below and above about 0.175. Up and down are text
in the header, the hover box and the overlay, so they became text-safe
(08805A 4.74, D52A3A 4.77), and the stamp keeps the light text, which is
the same pair the other way round. The amber alert tag could not stay
amber that way (darkened, it is ochre next to VWAP's gold), so it keeps
its surface and gets dark text, `onAlert` = 1F2328, at 5.71:1. The ghost
tag, a frame and a number on the box, draws in a darker amber,
`alertText` = A85400, at 5.12. The candidates were also checked for
roles that could be read as each other (CIE76 distance, compared with the
dark theme's pairs). That caught a phase 39 slip: the light `up` 089981
and `rsi` 00897B were almost the same teal (ΔE 8.6). Up is now green and
RSI teal-cyan 007E83 (ΔE 26.6). `hot` went darker (A31D33), away from the
new down. `chart_golden` now checks the 33 pairs of the light theme
before it draws (red on the phase 38 table: exactly the 20). The 24 dark
cases are unchanged, and the four light goldens were rewritten after
their pictures had been looked at.

**3. The setting.** "Light theme" in the tray menu (command 1009) and the
key `T` on the panel. One choice per mode, like the overlays:
`LightTheme` and `LightThemeDesktop`, **both off**. Dark is the look
TickC has had, and the desktop surface is the wallpaper, so a white one is
a change nobody asked for. `SetLightTheme` rebuilds the style at the
current dpi. The switch is instant: a fade would have to blend every
color in the panel per frame. A mode switch needs no code of its own:
it recreates the surface, and `TogglePopup` applies the style.
Probe field 64 reads the theme the panel is **drawn** with (the style's,
not the choice). `golden.ps1 -Light` switches the theme after opening, for
a light reference.

**Verified.** `shot_theme.ps1` (31 checks, in the scratchpad): dark by
default; 1009 → field 64 = 1 and `LightTheme` = 1 with
`LightThemeDesktop` still 0; no pixel of the dark background `0D1117`
left anywhere in the light capture; the header price, toolbar pills,
active-pill frame and button glyphs in the light colors; the overlay in
the light box; an alert tag in amber with dark text; `T` back to dark,
pixel-identical to the first capture; no GDI leak (34 objects after one
round trip and after three more); the panel opens light after a restart;
on the desktop, dark while the panel's choice is light, then light through
1009 with its own registry value; and a live mode switch (tray command
1003) going light panel → dark desktop → light panel. **Red run** with the
early return comparing only the dpi: 16 of 31 fail. The theme never reaches the
drawing, and a restart opens dark, because the style is first built
before the config is read. Green, 31/31, in the final run (earlier runs
without the mode-switch checks: 29/29 twice). `golden.ps1` with the
setting off: identical to `main` in all eight. Two `-Light` runs gave the
same hashes. `shot_dpi.ps1` 9/9 and `shot_rsi.ps1` pass (the dpi path now
goes through `ApplyPanelStyle`). The user was at the machine during the
phase, and the pointer and a click reached the test panel (pitfall 107).
**Exe 217 088 → 218 624 bytes (+1 536).**

**Tests on a hidden desktop (after the merge).** The user plays games at
this machine while a phase runs, and the test panels took focus and caught
the pointer (pitfall 107). The capture scripts in the scratchpad now take
`-Hidden`: the test build is started with `CreateDesktop` +
`STARTUPINFO.lpDesktop` on a desktop of its own, `TickCTest`, and its
windows are found with `EnumDesktopWindows`. That desktop is never
shown, so nothing appears on the user's screen, no real input reaches the
panel, and the tray icon has no taskbar to land in. Posted and sent probe
messages and `PrintWindow` work across desktops. Proof: `golden.ps1
-Hidden` gave all seven panel captures **bit-identical to the reference
from `main`** taken on the real desktop, and the light run was identical
to the light reference. `shot_theme.ps1 -Hidden` passed every panel check.
Desktop mode needs Explorer's WorkerW, which exists only on the real
desktop, so those cases are skipped when hidden (the engine's desktop
drawing is covered by `chart_golden`). The scripts never fall back to
the user's desktop.

### Phase 41 — ranges and the bar size as a dropdown

Branch `phase-41`, merged with `--no-ff`. The user showed two pictures of
Bloomberg's chart and asked what TickC could learn from them. The first
thing: **Bloomberg keeps the range (1D 3D 1M 6M YTD 1Y 5Y Max) apart from
the bar size ("Daily")**. TickC had mixed the two. Its interval pills were
the bar size, and the period was whatever the zoom gave, so the header's
`+9.85% (12d 12h)` described a span nobody had chosen. The quote line and
a settings button are the next two phases.

**1. Plumbing, no visible change.** `rangeIdx` (the selected range, UI)
and `rangeWant` (how many candles its home view holds, lock-protected).
Both `MergeCandles` and `WorkerFetchKlines` clamp a followed view to the
buffer. A wanted 365 would therefore have become the 360 of the first
fetch and stayed there after the backfill. The thread now uses the want
in all three places it sets a followed view (`PrependCandles` included),
so the view grows with the history. `NiceTimeStep` got 30, 60, 91, 182
and 364 days: 13, 26 and 52 weeks suit 1w as well, and a year of 1d
candles had drawn labels 37 days apart. Probe fields 65 (range), 66
(want, 0 = away from home) and 67 (overlay kind). `golden.ps1 -Hidden`
was identical to `main`, `chart_golden` 28/28.

**2. The interval as a dropdown, and 1w.** The six interval pills became
one pill, `1h ▾`, that opens the intervals as a one-column list right
under it (`overlayKind` 1). It uses the picker's rows and indices, with
empty rectangles for the symbols, so click, hover and highlight are the
picker's code. The symbol pill and a right-click still open the
two-column picker. `1w` is appended to the table, so indices 0..5 keep
their meaning in the registry, the tray IDs and the probes. `golden.ps1
-Hidden` differed from `main` only inside the toolbar row (checked with
a bounding box, not by eye).

**3. The ranges.** One pill per range between the dropdown and VOL. A
range is a **duration**. A click sets its default bar size, picked so the
view holds 180-365 candles: 1D is 5m x 288, 3D 15m x 288, 1M 4h x 180, 6M
1d x 182, YTD 1d from 1 January UTC, 1Y 1d x 365, 5Y 1w x 261, and Max 1w
with everything the exchange and the buffer give. Another bar size keeps
the range where it can be shown (6M on 4h is 1092 candles) and ends it
where it cannot (fewer than `MIN_VIEW`, or more than `MAX_CANDLES`: 6M on
1m). That is Bloomberg's behavior, and a pair model (range = fixed bar
size) would have undone the separation the user pointed at. The range is
the panel's **home view**: a zoom, a wheel pan, a drag or a navigation
key leaves it (`rangeWant` = 0, the pill goes out), and R, a double-click,
Esc and opening the panel go back. A range pick changes the interval
through `ApplyConfigChoice`, which sets the want in the same critical
section as it empties the buffer. With the want written after the lock,
the next merge would have given the 300-candle default for one frame.
The header names the range (`-23.80% (1Y)`) once the view holds all of it,
or all there is. Shift+1..8, a "Range" tray submenu (1300 + r), and
`RangeIndex` in the registry. A second pick of the selected range ends
it, back to the free 300-candle view.

**The minimum width.** At 400 px the row guarantees symbol, interval and
1D-1Y (it ends at x = 292 of 312). 5Y, Max and the overlay pills VOL, MA
and RSI hide from the right, and the tray menu and the keys carry them.
The C_ASSERT and `ToolbarMinW` now sum through the 1Y pill. Moving VOL, MA
and RSI into a settings panel (⚙) is phase 43; in this phase they only
hide on narrow panels.

**A bug the ranges exposed.** `PriceRange` pads the axis by 8 % of the
range, and on Max (weekly lows of 3 100 against 126 000) the axis went to
-7 054 and drew it as a grid label. The floor is now 0. No earlier view
came near zero, so every earlier capture and golden is unchanged.

**New fixtures.** 5m, 4h and 1w, and history files for 1d and 1w,
recorded from Binance (1w: 360 + 116 weeks back to 2017). The main build
gave the same seven panel hashes with and without them (a hidden run, so
the desktop case was not part of it).

**Verified,** all on the hidden desktop. `shot_range.ps1` (20 checks): no
range by default; 1D; 1Y growing 360 → 365 through the backfill with no
more history asked for afterwards; a zoom and a pan leave home and R goes
back; 6M kept on 4h with the view holding all 360 when the history ends;
1m ends 6M; the dropdown opened by a click at the pill (kind 1) picks 5m
and keeps 3D (864); the symbol pill opens the picker (kind 0); a second
pick ends a range; Max holds all 476 weeks; a click on the 1M pill; and
after a restart the panel opens on 1M. **Red run** with the thread
ignoring the want: 6 of 20 fail (every view-size check). Green twice.
`chart_golden` 30/30. A price-floor check and two cases were added, 1Y on
1d and 5Y on 1w. Red against the engine without the new steps: only 1Y
fails. At 261 candles, 5Y lands on 26 weeks with or without them, so that
case covers weekly labels, not the change. `golden.ps1 -Hidden`: every
difference from `main` inside the toolbar row. `shot_theme -Hidden`
passes, and `shot_dpi` (9/9) and `shot_rsi` (7/7) got `-Hidden` and pass.
The minimum width at 144 dpi is now 600 (601 in phase 37). The toolbar
sum through 1Y is 574 there, so the 1.5 x 400 floor decides. Shift+1..8
is not exercised by a probe: a posted `WM_KEYDOWN` cannot carry Shift
(pitfall 63's class), so the ranges were reached through the tray
command and the pills. The desktop-mode capture cannot run hidden (no WorkerW) and was
not taken. The desktop draws no toolbar and no time labels, and its
axis stays far from zero, so none of the changes reach it. **Exe
218 624 → 221 696 bytes (+3 072).**

---

## Known limitations

- **The one-time scaling of old geometry is untested** (phase 37). It
  reads `GetDpiForSystem`, which changes only at the next sign-in; the
  scale changes in the tests were per-monitor and immediate. The panel
  itself was tested through a real 100 → 150 → 100 % change.
- **The tray icon is 16x16 at every scale** (phase 37). The micro font
  draws into a fixed 16 px bitmap, and the shell scales it at 150 %.
- **YTD in the first week of January** (phase 41) is fewer than
  `MIN_VIEW` (8) daily candles, so the range cannot be shown on 1d and
  ends. It comes back once the year is eight days old.
- **A view away from a range's home shows spans like `(2100d)`** (phase
  41). `FormatSpan` counts days and hours; months and years are not
  spelled out.
- **The range is one choice for both modes** (phase 41), like the symbol
  and the interval, not one per mode like the overlays. A duplicate from
  [ + ] starts with the saved range and its own interval.
- **The desktop-mode captures need the real desktop** (phase 41). The
  hidden test desktop has no Explorer WorkerW, so `golden.ps1 -Hidden`
  skips `desktop_1m`.
- **The tray icon keeps its green in both themes** (phase 40). It sits on
  the taskbar, which follows Windows' theme, not TickC's.
- **The theme does not follow Windows' light/dark app mode** (phase 40).
  It is an explicit choice, per mode, and dark by default.
- **The dark theme has text pairs under 4.5:1** (phase 40): the muted
  gray `CLR_DIM` on the box (3.69:1, hover box labels and overlay
  headings) and on the background (4.12, toolbar pills at rest), and
  yesterday's levels (`CLR_PREV`, 3.99 on the box, 4.45 on the
  background). These are older, deliberate "muted" choices and were left
  alone. The contrast check in `chart_golden` covers the light theme only.
- **A hovered toolbar pill is fainter in the light theme** (phase 40): the
  white box on `FAFAFB` is 1.05:1, against 1.10:1 in the dark theme. The
  text turning from dim to the text color carries the hover.

- **`probe_prev` cannot check a dash pattern when two levels share a row.**
  When today's high and yesterday's close lie one row apart (86 625.82 and
  86 620.00 on 2026-09-22), one line's dashes break the other's, and the
  pattern check fails on any build (confirmed against phase 29). It clears
  itself when the levels move apart; a fix would skip the check for a level
  within two rows of another.
- **Windows sees `TickC.exe` as a new tray program** (phase 30). The
  notification-area choice "always show this icon" is stored per exe path,
  so after the rename the icon may start out in the overflow menu once.
- **An old `ticker.exe` started after the migration** creates
  `Software\Ticker` again with default settings and does not know about
  `TickC`. The next `TickC.exe` start leaves that key alone (the new key
  wins) but removes a `Ticker` Run value if it finds one.
- **The first time the panel opens,** "Loading data from Binance..." shows
  for ~300 ms until the thread has fetched. Every later opening has data from
  the buffer at once.
- **Size and position survive a restart** (the registry). `Ctrl`+`0` and the
  tray menu's "Default view" reset to **1280×720** centered, clamped to the
  work area if the screen is smaller.
- **Painting does not hold 0.85 ms.** After phase 10 the median is 1.33 ms
  at 1280×720 and 5.13 ms at 3840×1600, measured 2026-09-17. After phase 21
  it is 1.56 ms at 1280×720 with 300 candles (1.44 without the bars, measured
  in the same run with QPC in the test build). The breakdown per stage is in
  phase 10. Older numbers (0.462 ms at ~380×300 in part C, ~0.85 ms at
  1280×720 in phase 7) were measured under other conditions and could not be
  reproduced with unchanged code in phase 9.
- **Price alerts are tested against one price per fetch** (phase 23), that
  is every third second: the candle's close with the panel open, the ticker
  price otherwise. A spike that goes past the level and back between two
  fetches fires nothing — the candle's `high`/`low` is not read. During a
  disconnect nothing is tested; the first price afterwards fires whatever
  has been passed.
- **Only the alerts for the symbol being shown are awake** (phase 23). The
  app fetches one symbol at a time; an alert on ETH sleeps while BTC is
  shown, and fires on the first ETH price after the switch if the level was
  passed in the meantime.
- **An alert outside the visible price range is not drawn** (phase 23), not
  even as a marker at the edge — the same rule as the last-price stamp. It
  is awake all the same. It is removed by panning or zooming until it shows,
  or with "Clear price alerts (N)", which takes all of them for the symbol
  and is the place the count is shown.
- **An alert set close to the price can fire at once** (phase 23). The side
  is chosen against the last candle's close; with the panel closed the
  ticker price is tested, and the two can lie a few cents apart.
- **A duplicate has its own, transient alerts** (phase 23): it does not read
  or write the registry, but alerts set in it fire from its own tray icon and
  die with the panel. Two *main instances* started by hand both fire the
  same alert.
- **The balloon can be held back by "Do not disturb"**, and the sound is the
  system sound "Asterisk" — if it is turned off in Windows, the alert is
  silent. The afterglow shows only when the panel is visible at the moment
  the alert fires.
- **The outermost 6 px of the price column are a resize edge** (`HTRIGHT`),
  not alert surface, when the panel is not maximized.
- **The offline counter is not shown in the header on narrow panels**
  (phase 22). The toolbar ends at x = 338 (with the `MA` pill, phase 25; 310
  without); the text needs ~75 px more before the price axis label, that is
  a panel of ~510 px or more. The dimmed price,
  tray tip and icon carry the state regardless. The symbol line's ellipsis
  branch is gone along with the symbol line.
- **The periods are fixed** (phase 25): SMA 20 and EMA 50, not selectable,
  and both or neither — one toggle.
- **The `MA` pill does not exist below 426 px width** (phase 25). `M` and
  the tray menu work. The legend needs ~335 px of chart width and is gone
  below ~440 px panel width; the lines are drawn regardless.
- **EMA depends on where the buffer begins** (phase 25). It is fed from
  candle 0, so a backfill (phase 18) or an eviction at the front moves the
  seed point. The effect dies out as (49/51)ⁿ: after 300 candles it is below
  10⁻⁵ of the deviation at the seed point. On the first ~150 candles after
  candle 49 in a *short* buffer the line can differ visibly from
  TradingView's, which has a longer history.
- **The averages are undefined on the first 19 / 49 candles in the
  buffer**, and the line begins there. The default view hides it
  (`SEED_COUNT` 360); panned all the way to the start of history, it shows.
- **The lines are 1 px on the desktop too** when turned on there (phase 25;
  off by default from phase 26). The stamp scales
  with H/40; the lines do not (`DC_PEN` is always 1 px), and at
  3840×1600 they are thin. The grid has the same property.
- **The crosshair is drawn over the legend** when the pointer is under it.
- **A duplicate inherits `ShowIndicators` from the registry**, like
  `ShowVolume` below, and never writes it.
- **A duplicate inherits `ShowVolume` from the registry, not from the panel
  it was started from** (phase 22). `--dup` carries symbol and interval, not
  the volume choice; in practice they are the same, because the main
  instance writes the choice the moment it is made. A duplicate never writes.
- **The toolbar has no keyboard focus marker.** The pills are reached with
  `V` and `1`…`6`, not with `Tab`.
- **Several instances share the registry.** Duplicates write nothing, but if
  several *main instances* are started by hand (`ticker.exe` twice), the one
  closed last wins. Each instance also has its own tray icon — a duplicate
  disappears when its panel closes, a main instance stays until
  "Quit".
- **A duplicate hidden from its own tray icon stays hidden** (a tray click
  on an active panel hides it, as for the main instance). It is closed with
  the close cross, `ESC` or the tray menu.
- **The hover repaint sometimes fails to appear for up to ~2 s** in a probe
  that moves the real pointer and reads with `PrintWindow`. Seen in the old
  and the new build (phase 8). Not seen by hand, and the cause has not been
  investigated.
- **There is no system menu** (`Alt`+space), because the window does not
  have `WS_SYSMENU`. The control buttons are reached from the keyboard with
  `Ctrl`+`N`, `Ctrl`+`M`, `F11`, `Ctrl`+`W` and `Alt`+`F4` (phase 19), the
  chart with arrow keys, `PgUp`/`PgDn`, `Home`/`End` and `+`/`-` (phase 20),
  in addition to `Ctrl`+`0`, `R`, `ESC` and `Win`+arrow.
- **`ESC`, the shortcuts and the navigation keys need keyboard focus.** If
  you have clicked in another window, the panel must be clicked first. The
  buttons and the wheel work regardless.
- **A keystroke in the chart removes the crosshair** until the next mouse
  move (phase 20). That was chosen over letting the cross slide with the
  candle during the easing.
- **`Alt`+`Tab` in the middle of a drag does not always release capture.**
  Measured: in two of five runs the task switcher took capture, three times
  the panel kept it, and the drag then continues until the button is
  released. `WM_CAPTURECHANGED` (phase 20) covers the cases where capture is
  actually taken — the tray menu does it every time.
- **The tray icon's scale is implicit.** SOL at $150 and BTC at $150 000 are
  both drawn as `150`. The font has no `k` glyph — phase 1 deliberately chose
  `75.8` over `75k` — and the tooltip carries the exact number.
- **The animation timer runs in short bursts while the panel is open.** With
  the panel closed no timer runs at all. With the panel open every data fetch
  restarts the timer, because the live candle can move the Y target:
  measured **23 ticks in 30 seconds**, against 1800 if it had run
  continuously. So it dies between fetches — this is not a leak.
- **Desktop mode is without a chart for ~0.6 s when Explorer restarts**
  (measured 566 ms). Most of it is Explorer itself: a new Progman arrives
  after ~0.3 s, and WorkerW after ~0.3 s more.
- **Desktop mode: the classic WorkerW branch has not been run.** The machine
  has the 24H2 tree, where WorkerW is a child of Progman.
- **Desktop mode covers only the primary monitor** (the mandate). Several
  monitors are not tested, because the machine has one.
- **Desktop mode draws in physical pixels at scaling above 100 %.** The
  surface covers the whole screen, but text and margins get the same pixel
  size as at 100 %, so smaller on the screen. That follows from the whole
  layout being in raw pixels (see *Rejected proposals*, DPI manifest).
- **A monitor change has only been tried with a sent message** (phase 26).
  `WM_DISPLAYCHANGE` lays the desktop surface over the primary monitor again,
  with a follow-up check after 1 s, but the real event cannot be driven from
  a probe and the machine has one monitor. `WM_DPICHANGED` is not handled:
  the surface computes in physical pixels. Should the surface still end up
  wrong, a trip through panel mode and back rebuilds it.
- **Volume and moving averages are off on the desktop by default** (phase 26)
  and are turned on from the tray menu *while the process is in desktop
  mode*. The panel has its own choices. Anyone who had them on the desktop
  before phase 26 must turn them on again once.
- **Today's session is the UTC day** (phase 27), not local midnight and not
  the view: VWAP resets and "today's" high/low begin at 00:00 UTC (02:00
  Norwegian summer time), like Binance's daily candles. On 1d candles there
  is no session — VWAP shows a dash and the lines are not drawn. Today's
  high/low are drawn only when the level lies within the visible price range
  and the view reaches into the day; the axis tag gives way to the stamp,
  alerts and the ghost tag.
- **A day that is not wholly in the buffer gets no VWAP** (phase 27). For
  *today's* and (from phase 28) yesterday's day, older candles are fetched
  automatically (at most eight fetches at 1m,
  only with the panel visible and the indicators on); for older days at the
  left edge of the buffer the line stays empty until the next day rollover,
  or until the user drags against the wall. The backfill means the buffer at
  1m holds up to 2880 candles shortly after opening, not 360 — private bytes
  are unchanged (`candles[]` is static).
- **Yesterday's levels rarely show in the default view at 1m** (phase 28).
  300 minutes of today have a narrow price range, and a level outside it is
  not drawn — not even as a marker at the edge, the same rule as the alerts
  and today's high/low. Zoom out, or switch to 15m/1h.
- **Two levels a few pixels apart are both drawn** (phase 28; seen in the
  capture: yesterday's close 4 px above today's low). They are two numbers,
  and the distance is information; the axis tag and the label (phase 29) of
  the lower-ranked one give way, so that line stands without a name until
  the levels separate.
- **The labels are transparent text over the candles** (phase 29). At
  today's first candle a wick can run through a letter (seen: "PDH" at 15m).
  An opaque surface would wipe out the candles underneath; the text is three
  capital letters and can take it.
- **The crosshair tag is not shown within 16 px of the last-price stamp**
  (phase 29). The crosshair line and the hover box stay; the number on the
  axis is then the stamp's.
- **Opened late in the day at 1m, up to 2880 candles are fetched** (phase
  28, eight fetches in 3–4 s) when the indicators are on. The EMA seed point
  moves back accordingly (see the EMA item above); private bytes are
  unchanged.
- **Wake from sleep has only been tried with a sent message** (phase 24).
  `PBT_APMRESUMEAUTOMATIC` wakes the thread and drops the connection, but a
  real sleep cannot be driven from a probe. If the network is not up at the
  first attempt, it fails, and the backoff goes 6 s, 12 s, … from there.
  `frakoblet Ns` shows the length of the sleep until the first successful
  fetch (`GetTickCount64` counts the sleep too). Price alerts survive sleep:
  a passed level fires on the first price afterwards.
- **Desktop mode joins the input queues.** A child of a window in another
  process makes Windows attach the threads' input (implicit
  `AttachThreadInput`). If our UI thread hangs, the desktop can hang with it.
  The network runs on its own thread, so the UI thread only paints.
- **A mode switch takes 18–28 ms (median)**, not < 16 ms. See phase 12.
- **A maximized panel comes back restored** after a trip through desktop
  mode. The geometry that is saved is the restored one.
- **The focus-flash fix has not been run with maximize, minimize or Aero
  Snap.**
- **Above $999 999** the icon text is clipped (4 digits do not fit in 16 px).
  Safe — painting is bounds-checked.
- **Time labels pop in and out at the edges during panning** (phase 11).
  A label that does not fit within `[left, right]` is not drawn at all,
  instead of being clipped in the middle of a number.
- **Phase 11 is not pixel-verified in desktop mode or with the crosshair.**
  Both go through the same `DrawChart`, and the price tag uses the same
  `axL`/`axR` as the stamp. But neither is captured, because that would move
  the real pointer or put a surface on the desktop.
- **History is fetched only when the user asks for it** (phase 18). Freshly
  opened: 5 hours at 1m. Each hit on the wall gives 300 more candles, up to
  6000 or the start of history. Desktop mode has no input and never gets
  more than what it has seen. A gap in Binance's own history (maintenance)
  is prepended as it is: the candles are index-based, so time is compressed
  across the gap.

---

## Rejected proposals, with reasons

- **Circular buffer:** there are no dynamic reallocations to remove. The
  gain would have been one `memmove` of 57 KB per minute (~5 µs) against
  modulo arithmetic in all indexing.
- **Limiting to the visible view:** already in place since the zoom work.
- **DPI manifest / `SetProcessDpiAwarenessContext`:** "DPI-scaled 1280×720"
  in the mandate was read as `MulDiv` against `GetDpiForWindow`, not as
  making the process DPI-aware. The whole layout is in raw pixels, and the
  watermark's clamp limits would count the scaling twice — see the DPI
  comment in `EnsureWatermark`. If it is to be done, it is a separate job
  that must go through every single constant.
- **Pre-drawn button row as a `BitBlt`:** would have brought the button
  drawing within 0.003 ms (one call, ~1–2 µs) against two more GDI handles
  and a cache that must be invalidated on `WM_SIZE` and on every hover
  change. Measured, the vectors cost 0.015 ms, that is 1.8 % of a paint. Not
  worth the complexity — but the mechanism already exists in
  `EnsureWatermark` if the budget is to be held literally.
- **C++ with STL and nlohmann/json** (the architecture directive behind
  phase 24). Measured with the project's own flags (`/W4 /O2`, x86, static
  CRT) on a minimal program: C with `strstr` + `atof` **102 400 bytes**; the
  same with `std::vector` and `std::string` **114 176** (+11.8 KB, requires
  `/EHsc`); the same with nlohmann/json 3.11.3 **222 720** (+120 KB — 62 %
  of the whole `ticker.exe` — and the header does not build cleanly at
  `/W4`). What nlohmann was to buy, safe parsing, costs 512 bytes as
  `strtod` with an end pointer and `CandleSane`. `std::vector` has nothing to
  replace: there is no `malloc`, and a fixed `candles[6000]` can neither leak
  nor fail an allocation after three weeks of running — a vector can.
  Exceptions across `WndProc` are undefined, so every message handler would
  have needed its own `try`. The language switch itself is free (`ticker.c`
  is valid C++, see *Build*), so the decision can be made again when a
  feature needs a container of unknown size. Indicators (SMA/EMA/RSI) are
  not rejected — they just do not exist yet, and are a loop over `candles[]`
  into a static `double[MAX_CANDLES]`.
- **WebView2 + Lightweight Charts:** would have broken the low-footprint goal
  by 50–100× (Edge subprocesses use 100–200 MB against our 3.3 MB).

---

## Pitfalls for agents

1. **Stop `ticker.exe` before you link.** Otherwise `LNK1104`.
2. **Function order.** The file has no forward declarations. New
   helper functions must come *before* their first use, otherwise `C2371: redefinition`.
   This has bitten three times.
3. **ASCII only in comments.** `æøå` gives `C4819` and broke a heredoc during the work.
4. **`lParam` in `WM_MOUSEWHEEL` is screen coordinates**, not client coordinates
   as in `WM_MOUSEMOVE`. `ScreenToClient` is required.
5. **Synthetic mouse clicks are unreliable for testing.** `PostMessage(WM_MOUSEMOVE)`
   triggers `WM_MOUSELEAVE` at once because the real pointer is outside. Move
   the real pointer and *jiggle* it (several movements) before you measure.
6. **A tray click via `PostMessage` does not grant foreground rights.** The panel can open and
   hide itself right away in test setups. Check `IsWindowVisible` and retry in a loop.
7. **Do not trust your eyes for the subtle controls.** Measure pixel colors with
   `GetPixel` — I drew the wrong conclusion twice from downscaled screenshots.
8. **`WaitForMultipleObjects` returns more than two things.** `WAIT_TIMEOUT`
   (`0x102`) is not `WAIT_OBJECT_0 + n`. Check the actual return value;
   code that only tests for the stop event and lets the rest fall through
   treats every single timeout as a wake-up. See bug #9.
9. **Blocking via the hosts file does not stop an open connection.** WinHTTP
   holds on to `hConnect` and keep-alive against an IP that is already resolved, so
   the fetch keeps succeeding. To cut the line on an app that is *already
   running*, you must block the IP in the firewall. The hosts file only works if you
   start the app afterwards.
10. **Several processes share window class names.** The mutex is gone (phase 8),
    so a test build starts fine side by side with the real app. But all
    instances have `BTCPopupClass` and `BTCTickerWindowClass`, and they share
    the registry key. Filter windows by process id
    (`GetWindowThreadProcessId`), never by class name alone. Phase 7 and older
    mention "own mutex name" — that applied before phase 8.
11. **Read `GetWindowRect` right before you capture the screenshot.** The window may have
    moved or changed size since last time. (The auto-hide problem that
    made this a nuisance in phase 2 is gone with the OS frame — `pinned`
    no longer exists.)
12. **Hit testing must depend on logical state, not on fade level.** During
    the fade-out the overlay is still visible. If you check `overlayF > 0`,
    the box swallows clicks it no longer owns.
13. **Guards in `WM_MOUSEMOVE` belong after `TrackMouseEvent`.**
    If you return before the arming, `WM_MOUSELEAVE` stops firing and
    `windowHot` gets stuck at `TRUE`.
14. **Drawing and hit testing must read the same source.** `DrawChart` and
    `HitCandle` both read `disp*`. If one reads the target and the other
    the display, the crosshair points at the wrong candle in the middle of an animation — bug #7 in
    new clothes. Applies to everything that converts between pixels and candle indices.
15. **Restore the clipping.** `IntersectClipRect` around the drawing loop must
    be followed by `SelectClipRgn(hdc, NULL)` before axis text and chrome are drawn —
    they lie outside the chart area and would have disappeared.
16. **A threshold in "units" does not work across symbols.** Four symbols
    three orders of magnitude apart make any fixed number in dollars
    meaningless. Convert from pixels instead, on every tick.
17. **Watch out for an empty buffer in all new state.** Right after a
    symbol switch, `candleCount = 0`. State that "syncs itself" then
    syncs against nothing — see bug #15.
18. **`CopyFromScreen` captures whatever is on top.** Without `WS_EX_TOPMOST`
    another window can cover the panel, and the screenshot is of *that*. Use
    `PrintWindow` with `PW_RENDERFULLCONTENT` — it draws the window regardless of
    z-order.
19. **`GetGUIThreadInfo` with the wrong `cbSize` lies silently.** It returned
    `TRUE` and `hwndFocus = 0`, exactly the symptom of bug #2, while
    focus was in fact correct. Set `cbSize` from the *type*, not from a
    boxed instance — and trust a real keystroke more than the probe.
20. **Ownership and taskbar go together.** An owned window does not get its own
    button in the taskbar. If you want the button, the window cannot be owned — and then
    you must tear it down yourself on exit.

21. **An `HTCAPTION` area never gets `WM_LBUTTONDOWN`.** If you draw your own
    buttons in a header that returns `HTCAPTION`, they are visible and dead —
    and a click on them starts a window move. `WM_NCHITTEST` must
    return `HTCLIENT` over each button box. The order within that one
    function *is* the mechanism.
22. **A frameless `WS_POPUP` maximizes to the whole screen.** Not to
    the work area, and the OS adds the frame width outside it: measured
    −7,−7 3854×1614 against `rcWork` 0,0 3840×1552 — the panel covered
    the taskbar. `WM_GETMINMAXINFO` must supply `ptMaxPosition` and `ptMaxSize`
    itself. Subtracting the frame width in `WM_NCCALCSIZE` does *not* fix this;
    that was the first attempt, and the 7 pixels are not the 55 that are missing.
    Leave `ptMaxTrackSize` alone — it clamps manual resizing too.
23. **`WM_MOUSELEAVE` fires when the pointer moves from `HTCLIENT` to `HTCAPTION`
    in the same window.** It leaves the client area without leaving the window. All
    hover state must be reset there, otherwise a button stays lit
    when the mouse moves off it and out into the header.
24. **`Arc` goes counterclockwise *as seen on the screen*.** Start 3 and end 12 gives an
    arc in the upper right quadrant, not three quarters. If you want a
    circular arrow, start at 12 and end at 2. Measure it, do not reason about
    logical coordinates.
25. **`FindWindow` did not find the app's own window classes** in this setup,
    while it found `Shell_TrayWnd`. `EnumWindows` with `GetClassName` worked
    every time. Use it in probes.
26. **The buttons are positioned relative to the right edge.** A probe with hard-coded
    x values only applies to the width it was written for, and hits empty
    surface as soon as the window is maximized. Count backwards from `GetWindowRect`,
    as `ButtonLayout` does.
27. **The handle count must be read at rest.** During ongoing painting,
    GDI is at 34 and USER at 15 — the double buffer and the watermark in flight — against
    31/14 when everything has settled. If you measure in the middle of a stress test, you see a
    leak that does not exist. **From phase 10 the resting count is 29/14** in panel
    mode and 26/6 in desktop mode. The double buffer now lives between frames,
    and four pens and brushes are replaced by `DC_PEN`/`DC_BRUSH`. Phases 19
    and 20 measured 30/14; **from phase 21 it is 32/14** — the two bar brushes
    are created in `WinMain`. A mode switch back and forth gives +1 (33/14), also seen
    in the build without phase 21. Phase 22 added nothing: 34/14 before and
    after in both the red and the green build, measured *after* the first overlay (+2,
    pitfall 65). Nor did phase 23: 34/14 in the red and both green
    runs, after ten set/remove, a balloon and a `MessageBeep`.

28. **`WM_SETCURSOR` must return `TRUE` to keep the pointer, and `break` for
    everything else.** If you return `0` in the default branch, the edge zones lose their
    resize pointers — they come from `DefWindowProc`. And without `return TRUE`
    the OS sets the window class pointer back on the next mouse movement, so a
    `SetCursor` from `WM_MOUSEMOVE` is overwritten at once.
29. **A fast path must read the background from the same source as the slow one.**
    `FillRect(brBg)` in the button strip *would* give the right result today, but
    only because the watermark text happens never to reach up into the header. We
    blit from the watermark bitmap instead, and prove they match: 0 differences out of
    1980 pixels.
30. **A window in front of the panel invalidates all screen-based measurement.** `GetPixel`
    on the screen, `GetCursorInfo` and mouse/wheel messages all go to whatever
    is actually on top. Measured: `IDC_HAND` over the chart and `IDC_IBEAM` over
    the header — both from a Chrome window behind, confirmed with `WindowFromPoint`.
    `BringWindowToTop` and `SetForegroundWindow` do **not** work from a probe
    that does not own the foreground; `SetWindowPos` with `HWND_TOPMOST` does, and must
    be reset afterwards. `PrintWindow` is immune and should be used when possible.
31. **One pixel is not a test.** Three assertions in the regression probe failed
    falsely because they depended on one coordinate: one point that was on background in
    both states, a count that happened to give the same number, and one point
    that was crosshair-colored both before and after a pan. Compare a
    **region**, and establish ground truth with `PrintWindow` before you believe a red
    result.
32. **`GetCursorInfo` with the wrong `cbSize` lies silently** — returns `TRUE` and
    `hCursor = 0`. Same trap as pitfall 19, new message. `CURSORINFO` is
    24 bytes in a 64-bit process, not 20. Set `cbSize` from the **type**.
33. **`IntersectClipRect` is exclusive at the right and bottom edges — check what
    actually lives on the boundary.** `right + 1` let candles into the first column of the axis
    margin; `bottom` without `+ 1` would have removed the bottom grid line. Same
    function, opposite answer in the two axes, because the chart is `[left, right)` in
    x and `[top, bottom]` in y.
34. **A clipping bug does not show in a still image.** Half candles at the edges
    exist only when `dStart` is fractional, that is, in the middle of the easing. Post wheel notches and
    capture within ~90 ms, many times, and scan regions for exact candle colors.
35. **A posted `WM_MOUSEMOVE` does not hold hover if the real pointer is
    outside the client area.** `TrackMouseEvent` sees that the pointer is not there and
    sends `WM_MOUSELEAVE` at once, which resets `btnHot`. This also applies
    when the pointer is in the header, which is `HTCAPTION` (pitfall 23).
    Park the real pointer on a button — `HTCLIENT` — and put it back
    afterwards.
36. **`DrawTextW` clips against its own rectangle, not against the neighboring text.** Two
    texts in the same rectangle, left- and right-aligned, are drawn on top of each other
    as soon as they meet. Measure both and compute the boundaries yourself.
37. **A latency budget without measurement conditions is not a budget.** 0.462 ms
    from part C was the median during animation at ~380×300. At 1280×720
    the whole frame is ~0.85 ms, and maximized ~9.5 ms. Always measure before and after in
    the same run, alternating, and not while a probe is taking screenshots
    at the same time: `PrintWindow` made every round noticeably slower.
38. **`WM_LBUTTONDBLCLK` never arrives without `CS_DBLCLKS`** on the window class —
    silently. And when it *is* set, the second click in every fast double click becomes
    a `DBLCLK` instead of `WM_LBUTTONDOWN`, everywhere in the client area. Everything that
    reacts to clicks, and that should not reset the view, must receive the
    double click too — otherwise the buttons eat every other fast click.
39. **`viewCount == 0` is not "show all" after `ClampView`.** `GetView` interprets
    0 as the whole buffer, but `ClampView` clamps 0 up to `MIN_VIEW`. A
    view that is to become the default when data arrives must be set *before* the clamping.
    See phase 8.
40. **A newly opened panel is no ground truth for the default view.** Take the reference
    after an explicit reset, and look at the frame before you believe a deviation
    in percent: a 5 % difference in the chart area was 8 candles against 300.
41. **The Bash tool's heredoc eats backslashes** in this setup, even with
    `<<'EOF'`. `'\\'` became one backslash, and a `rep()` that was meant to match
    `L"Global\\..."` found nothing. Write scripts with the Write tool and run
    the file.
42. **`rcNormalPosition` is in work-area coordinates, not screen.** They are
    equal as long as the taskbar is at the bottom or on the right. `SpawnInstance`
    uses `GetWindowRect` for a normal window and falls back to
    `rcNormalPosition` only when the window is maximized.
43. **PowerShell passes `$null` as `""` to a P/Invoke `string`.**
    `FindWindow("Progman", $null)` gave 0, while `FindWindow("Progman",
    "Program Manager")` found the window: the first one looks for a window with an
    *empty* title. Use `[NullString]::Value`. This is probably the whole explanation
    for pitfall 25.
44. **A plain child window under WorkerW becomes invisible on 24H2, and nothing
    fails.** `SetParent` succeeds, `GetParent` matches, `IsWindowVisible` is
    `TRUE`, `WM_PAINT` arrives, and the wallpaper is still on top. The surface must be
    layered, with `SetLayeredWindowAttributes` called *after* `SetParent`, and
    the exe must have `supportedOS` Windows 8+ in the manifest. A state probe
    proves nothing here. Check pixels on the screen, where the desktop is actually
    visible.
45. **`WindowFromPoint` tests of the desktop require the desktop to be
    visible.** The user's windows move between two runs. Count how
    many points have Progman as root, and treat 0 as "not tested",
    not as green or red.
46. **`0x052C` changes the desktop for everyone.** The message creates a WorkerW that
    stays after the process has exited. It is harmless, and the wallpaper
    looks the same, but the tree is not the same as before the first run. A probe
    that looks at the "before" state must run before anything has sent the message.
47. **A posted `TaskbarCreated` is not a restart of Explorer.** The posted
    message arrives with WorkerW intact. In a real restart the surface is already
    gone and rebuilt before the message arrives. The test with a posted message was
    green and hid a double rebuild that only the real restart
    revealed (phase 9).
48. **`CreateCompatibleBitmap` is lazy.** The call took 0.07 ms for 3840×1600,
    while the first `BitBlt` into the bitmap took 6.5–7.9 ms and `DeleteObject`
    1.7 ms. A QPC marker around the call alone sees a cheap allocation. Measure
    the first write and the release too.
49. **PowerShell functions can collide with built-in aliases.** A
    helper function named `Move` was never called: `Move` is an alias for
    `Move-Item`, and the alias wins. Only error messages came out, and the probe
    continued without hover. Give probe functions names that do not already exist
    (`PostMove`).
50. **Pixel comparison between two builds requires data and input to hold still.**
    Live prices change the frame, and a real pointer competes with
    `TrackMouseEvent` (pitfall 35). Which of them wins varies from
    run to run, and master differed from itself. What worked: the same
    patch in both builds, with `HttpGet` reading a file and `TrackMouseEvent`
    as a no-op, and the window almost entirely off screen. Capture only when two
    frames in a row are identical, and confirm the state against rest before you believe a
    deviation. The test window takes the foreground, so a keystroke from the user (ESC)
    can close a duplicate in the middle of a run.
51. **A frameless `WS_THICKFRAME` window gets the classic frame drawn on top of
    the client area.** When `WM_NCCALCSIZE` returns 0, the window DC and the client are
    the same surface, and `DefWindowProc` for `WM_NCACTIVATE` and `WM_SETTEXT` draws
    the frame straight into the chart. It stays until the next `WM_PAINT`. Answer
    `WM_NCACTIVATE` with `DefWindowProc(…, -1)`. It is measured from the screen;
    whether `PrintWindow` sees it has not been tried.
52. **`DWMNCRP_DISABLED` does not remove NC painting — it turns off the DWM frame and
    lets the classic one in.** Measured: frame pixels back, and
    `SetForegroundWindow` failed in two of three cycles.
53. **A window's DPI context is set when it is created.** A window that is to
    switch between a DPI-unaware panel and a per-monitor-aware desktop surface
    must be created anew. `SetParent` and style changes move it, but the context
    does not follow.
54. **A screen probe must discard samples where other windows are involved.** The user
    may have Chrome or other windows in use while the probe runs. The first
    run after the merge showed 4 121 "frame pixels", which were Chrome over
    the panel's right edge, and `SetForegroundWindow` failed in 3 of 5 cycles.
    Check per sample that the foreground is your own window and that `WindowFromPoint`
    at the edge points hits the panel. Run a control without the fix in the same
    run, so you know the probe sees what it should.
55. **Do not write C escapes through a bash heredoc.** On the way through
    the Bash tool and the heredoc, `\\x00e5` became `\x00e5` before Python saw
    the string, and Python wrote a real
    NUL byte into `ticker.c`. The build was clean at `/W4`, and `file` called
    the source "ASCII text". Only `grep` ("Binary file matches") and the menu text
    ("pe5logging") showed the bug. Write the script to a file with the Write tool,
    and check `grep -c $'\x00'` after machine edits.
56. **A menu probe opens real menus at the pointer.** `TrackPopupMenu` shows
    the menu where the user's pointer is, and a real mouse click selects an item. One
    run with 80 menus got one autostart click too many, and it could not be
    reproduced. Log the state after each block, and run the probe several times
    before you believe a deviation.
57. **`EnumWindows` does not find the desktop surface.** It is a child of WorkerW,
    not a top-level window, so a probe that only enumerates top level sees
    "no surface" in desktop mode — and `GetParent(NULL)` gives 0, which looks
    like "panel without parent". Four false failures and one false green came from this.
    Also search the children of `Progman` and `WorkerW` with `EnumChildWindows`.
58. **A probe must read the mode from the registry, not assume panel.** The app starts in
    the mode `DesktopMode` says. The test assumed panel, while the app started in
    desktop mode, and all the mode asserts were mirrored.
59. **The last-price stamp has the same color as the candles.** It is filled with
    `CLR_UP`/`CLR_DOWN` and covers `yLast ± 8`. A probe that looks for the
    "outermost candle pixel" therefore measures the stamp, not the candles, and phase 15 gave
    a false red until the whole band was excluded. The price inside the stamp is drawn in
    `CLR_BG`, so the row is not fully covered either.
60. **A probe against the panel must use `PrintWindow`, not a screen dump.** A window
    that lies on top of the panel is otherwise measured instead of the panel, and phase 15 got
    zero candle pixels in the headroom for that reason. `PrintWindow(hwnd, dc, 2)` asks
    the window to draw itself. Same lesson as pitfall 54, now on a panel instead
    of the screenshot.
61. **Six seconds is not enough for the candles to be in place.** A dump taken too
    early has background, watermark and no candles, and all pixel checks turn
    red without anything being wrong. Phase 15 hit this once; with 14 seconds
    the same checks were green. Wait for data, not for the clock.
62. **PowerShell `[int]` rounds, it does not floor.** The BMP row length
    `[int]((800 * 3 + 3) / 4) * 4` gave 2404 instead of 2400, and the image came out
    skewed and color-distorted. Use `[Math]::Floor` where C would have used
    integer division.
63. **A posted `WM_KEYDOWN` cannot test `Ctrl` combinations.**
    `GetKeyState(VK_CONTROL)` reads the thread's real key state, which a
    posted message does not touch. Use `SendInput` with the panel in the foreground — and
    confirm the foreground *and* `GetGUIThreadInfo` focus before each press, otherwise
    `Ctrl`+`W` ends up in whatever window happens to be in front. Put a
    **control with a shortcut that already exists** (`Ctrl`+`0`) first in
    the probe: without it a red run cannot tell "the feature is missing" from
    "the probe does not deliver keys".
64. **`Alt`+`Tab` is no reliable capture thief.** In five runs the
    task switcher took capture from a panel in the middle of a drag twice and left it
    alone three times, while the foreground switched every time. A test of
    `WM_CAPTURECHANGED` that depends on `Alt`+`Tab` is therefore red or green
    depending on the weather. Use something that takes capture *every* time: the app's own
    `TrackPopupMenu` (the tray menu) in the same thread. And read the capture state
    from the app's thread (`GetCapture` is per thread), not from the probe. Remember that
    the menu opens at the pointer with the mouse button down: `ESC` before release, otherwise
    the release can select "Quit" (pitfall 56).
65. **The GDI count jumps once at the first overlay and the first menu.** +2
    after the first overlay (two brushes are created and deleted per frame; GDI keeps
    deleted brushes in a small per-process cache) and +3 after the first
    tray menu (USER draws it in our process). Both are unchanged over three
    cycles up to and through 30 rounds of keystrokes, and the same in the build without
    the change. A "before/after" check that takes "before" before the first overlay and
    "after" after the first menu sees a leak that does not exist. Take "before"
    after each mechanism has been used once, and add a
    cycle test (open/close ×3) that separates one-time jumps from growth. Also read
    the minimum over several seconds, not one sample: in the middle of a
    repaint the count is two higher (pitfall 27).
66. **The probe must wait for the machine to be idle, and the focus check must have
    a fallback path.** A run went red on 18 checks because a Chrome window
    took the foreground after the check; the probe correctly refused to send keys,
    but `SetForegroundWindow` from the probe no longer worked. The fallback that
    works is the app's own `ForceForeground` via a posted tray click (only
    when the panel is *not* the foreground, otherwise the click hides it). And
    `GetLastInputInfo` before starting: 25 s without input, otherwise wait. The probe
    itself creates input with `SendInput`, so the measurement only applies before it
    starts.

67. **Your own macro names can collide with `commctrl.h`.** `TB_TOP` exists there
    (`TB_*` are the toolbar messages), and `windows.h` pulls it in even
    with `WIN32_LEAN_AND_MEAN`. The result is `C4005`, not an error — the build
    succeeds with *their* value if the order is different. Phase 22 uses
    `TBAR_*`. Stay away from `TB_`, `LV_`, `TV_`, `SB_`, `WM_`, `CB_`, `LB_`.
68. **Python `read_text`/`write_text` normalize line endings.** The repo has
    `core.autocrlf=true`: all text files are LF in the index and **CRLF in
    the working copy** (`git ls-files --eol`), `ticker.c` included. An
    edit script that reads with `read_text` and writes with
    `newline="\n"` turns the file into LF on disk. Git hides it (the diff is
    clean, only the warning "LF will be replaced by CRLF" gives it away), `cl`
    does not care, and the next `checkout`/`merge` writes CRLF back — but a
    backup taken in between has the wrong line endings. Phase 22 took `bak17`
    that way and had to take it again. Write with `newline="\r\n"`, and take
    `.bakN` after the merge.
69. **A probe that takes "GDI before" must warm up the *red* build with something it
    has.** The overlay is opened with a posted `WM_RBUTTONUP` in the chart and closed with
    a posted `ESC` — both exist in every build since phase 2 — so "before" is
    comparable between the red and the green run (pitfall 65).
70. **A writing probe must carry the value in the message, not put it in a
    shared field.** The obvious solution is to write `lastPrice` under
    the lock and then send `WM_APP_DATA`, which reads the field again.
    The worker thread writes the same field every third second, so
    the injection would be lost when a real fetch landed in between —
    rarely, that is, a test that fails now and then (reasoned, not measured).
    Phase 23 sends the price in `lParam` (`wParam` = 1), and
    `SendMessage` returns only once the trigger has been tried. The writing
    fields (100–103) live on the **main window**, so they work with the panel
    hidden; the reading ones still live on the panel.
71. **A posted click leaves hover state behind.** `OnAxisClick` sets
    `axisHotY` because a real click has the pointer there; a posted click has
    no `WM_MOUSEMOVE` before it and no `WM_MOUSELEAVE` after. The frame
    after a posted click in the price column therefore has a ghost (line and
    framed tag) on the click's row. Pixel checks must either allow for it
    or post `WM_MOUSELEAVE` first.
72. **`grep -c $'\x00'` counts every line.** In bash `$'\x00'` is an empty
    string, and the empty string is found on every line — the check from
    pitfall 55 answers "4837" on a clean file. `grep -P '\x00'` does not work
    in this setup ("supports only unibyte and UTF-8 locales"). Use
    Python: `open(f, 'rb').read().count(b'\x00')`, and count non-ASCII and
    bare LF in the same go (pitfalls 3 and 68).
73. **Price → y → price does not round-trip.** The candles truncate y with `(int)`, and
    the alerts must do the same to lie on the candles' rows (pitfall
    14). A click on row 300 gave the level 80 832.00, which is drawn on row 299.
    A probe that looks for the line must search in `y ± 2` and accept ± 1.
74. **The description in a `Check` is not a format string.** `%%` is printed
    as two percent signs. The red run said "2 %% above the price".
75. **Look at the exe size after the production build, and build it before
    the merge.** One call to `pow` and one to `log10` added 21 KB to the exe
    (static CRT, `pow` has tables); `exp`, `sqrt`, `floor`, `ceil` and
    `fabs` were there already and cost little. `/W4` says nothing, the test build
    is larger for other reasons, and phase 23 discovered it only after
    the `--no-ff` merge. Compare `ticker.exe` with the previous phase's number
    (187 392 after phase 22, 195 072 after phase 23, 195 584 after phase 24)
    before you merge.
76. **`atof` cannot fail.** Text gives 0.0, `"1e999"` gives inf, `"nan"` gives
    NaN (UCRT parses it), and `"12x"` gives 12 — all without a word. Use `strtod`
    with an end pointer and require it to stop at the closing quote, and pass
    the value through a *range* (`v > 0.0 && v < 1e15`): the comparisons are
    false for NaN and the ceiling catches inf, without `isnan`/`isfinite` and without anything new
    from the CRT (pitfall 75).
77. **The main window is created before `InitializeCriticalSection`.** A new
    message handler in `WndProc` that enters the lock can get a *sent*
    message in the window between `CreateWindowExW` and the lock in `WinMain`.
    `WM_POWERBROADCAST` guards itself with `g_Ctx.hWakeEvent` — it is set after
    the lock, so if it is set, the lock exists.
78. **Compare handles in the same state.** The phase 24 probe measured GDI/USER
    with the panel closed, opened the panel, and measured again: 23/5 → 32/12, a red
    check, no leak. "Before" and "after" must be the same set of windows.
79. **Read the code before you take a directive at its word.** The phase 24 mandate asked to
    replace `malloc` with `std::vector` in a codebase without `malloc`. Two
    `grep` runs and three small builds in the scratchpad settled it; the numbers are in
    *Rejected proposals*. Measure the means, deliver the goal.
80. **Measure the small thing directly, not as the difference between two large ones.** The phase
    25 probe measured the whole repaint (~1.9 ms) with and without moving averages
    and got "the overlay costs −61 µs" — and −239 µs against a build *without*
    the overlay. Five alternating rounds with the lowest median per state did not
    help: −139, +63, +78, +80, +196 µs in five runs. QPC around the
    block itself (probe field 39) gives 52 and 59 µs in two runs.
81. **`MA_` is taken, like `TB_`.** `winuser.h` defines `MA_ACTIVATE` …
    `MA_NOACTIVATEANDEAT` (the answers to `WM_MOUSEACTIVATE`). The indicators are named
    `IND_*`. Pitfall 67 in new clothes: check the prefix against the SDK before you
    choose it.
82. **Look at the first screenshot before you write the probe.** The numbers were right
    and the lines continuous, but EMA 50 started one sixth into
    the default view — 300 of 300 candles visible, the first 49 undefined.
    No pixel check would have looked for that. Fixed at the source
    (`SEED_COUNT` 360), and *then* the probe got the check "defined on the first
    visible candle".
83. **A guard in `Init` does not guard the siblings.** `IndInit` clamped the period to
    1; `IndFeedStart` took it raw and computed the start index past the target, so
    the loop never ran. Found by a unit test on period 0, not by anything
    the user can reach — but the next phase makes the periods selectable.
84. **A posted key is not independent of the user.** `WM_KEYDOWN` can
    be posted to a window without focus, but the app reads `Ctrl` with
    `GetKeyState` — the REAL key. If the user holds `Ctrl` in another
    window, the probe's `M` becomes `Ctrl`+`M` (pitfall 63 said that posted
    keys cannot *test* Ctrl; this is the converse: they cannot *avoid*
    it either). "No `SendInput`" does not mean "does not need an idle
    machine". And a capture should check the size it got before it indexes
    with the size it expected: a minimized panel is 0×0.
85. **Test a new thing against the rule, not against one wording of it.** Phase 25
    drew the average lines on the desktop because "a curve is not text" —
    true, but phase 14's rule is that the surface is read *peripherally*, and an average is
    something you read off. The user spoke up the next day. And the volume bars had
    been there since phase 21 because nobody asked at all. New drawing
    in `DrawChart`: decide explicitly for the desktop, and look at the surface.
86. **Startup state that depends on the mode must be set after the mode
    is known.** `dispVolF` was snapped right after `LoadConfig`, but
    `g_desktopMode` is read 35 lines further down (`--desktop-mode`, then
    `DesktopMode`). Invisible as long as the choice was shared by both modes.
87. **A line color must not lie on the blend line between the background and a
    text color.** The first choice for today's high/low (phase 27) was 8A93A0 — which
    is *exactly* `CLR_BG` + 0.85 × (`CLR_AXIS` − `CLR_BG`) in all three channels.
    Anti-aliased axis numbers then contain the same color, and a pixel probe that
    counts "exact line color" counts text. Compute t per channel before the color
    is put to use; 90939E does not lie on the line of any of the text colors.
88. **A legend in the line's color IS pixels in the line's color.** The probe in phase
    27 counted gold in the whole window to show that VWAP is *not* drawn on 1d — and
    found the legend's "VWAP  -", which is gold on purpose. The red run was
    green on that check for the wrong reason (no legend in commit 1). Look
    for the line where the line runs, not in the whole capture.
89. **A path that depends on the clock must be exercised on purpose.** The backfill to
    the day rollover (phase 27) runs only when the day is older than the 360 candles
    from the first fetch — on 1m after 06:00 UTC. The first runs went at
    05:30 and never exercised it; the probe therefore prints the UTC time and whether the path was
    taken, and one green run was scheduled after 06:00.
90. **"For the visible view" in a request is a suggestion for an
    anchor, not a requirement** (same class as 79). VWAP from the first visible
    candle jumps with every candle during panning, and high/low for the view is
    always 8 % from the edges (`PriceRange`). Read what the code does with the view
    before a number is anchored in it.
91. **A probe field should round, not truncate — and one-sided deviations are
    never rounding noise.** Field 14 was `(LRESULT)(volume * 100.0)`. The probe's
    VWAP held within 5 cents with 360 candles in the session (phase 27, at 06 UTC) and broke
    with 1152 (phase 28, at 19: 0, +4.0, +2.6, +5.7 cents — all in the same direction).
    The first explanation was "rounding error that grows with N", and the tolerance
    was raised to 15 cents; but rounding the price to cents can never give more
    than 0.5 cents in a weighted average, whatever N is. Truncated *weights* can.
    `floor(x * 100 + 0.5)` in the field: under 0.5 cents. Do not raise a tolerance
    before the sign of the deviations has been looked at, and run clock-dependent probes
    late in the day too (89).
92. **When a wait rule in the app is extended, the probes that wait for the old
    rule must be extended at the same time.** The phase 27 probes waited for field 46 (today's
    day covered) and then read indices; from phase 28 the backfill continues,
    and the indices move under them. Field 54 is now "the app wants more
    history" — defined as what `WM_APP_DATA` decides, with `ShowIndNow()`,
    otherwise the desktop probe (indicators off) waits forever.
93. **`\t` in a Python heredoc is a tab.** `"$s\ticker_test.exe"`
    written from a bash heredoc became `$s<TAB>icker_test.exe`. Same class as
    41/55: scripts with backslashes are written with the Write tool.
94. **Text that is to lie over the candles cannot be drawn in the block that lies
    behind them.** The level lines (phase 27/28) are drawn before the candle loop; the labels
    (phase 29) had to carry `yLine[]` and `lvlXs` down to the legend's block.
    Ask "what is drawn after this?" before new text gets a place in `DrawChart`.
95. **Collision between texts is decided on rectangles, not on y distance.**
    The first draft of the labels said "less than 12 px away" — the axis font is
    15–16 px tall, and a label can sit above or below its line. The axis tags
    can use `abs(dy) < 16` because they are all 16 px tall and sit in one column.
96. **`CountNear` (±48 per channel) does not tell `CLR_AXIS` from `CLR_TEXT`
    or `CLR_SESSION`.** A strip that is to be empty of label text must
    lie *entirely* outside the crosshair tag's area, and the label being tested must
    be chosen from a capture (it may have made way for the stamp), at least 40 px from
    the stamp and the levels.
97. **`RegCopyTreeW` needs more than `KEY_WRITE` on the target.** With the
    target opened for `KEY_WRITE` it returns `ERROR_ACCESS_DENIED` (5);
    `KEY_ALL_ACCESS` gives 0 (measured with `dbg_copy.c`, phase 30). The
    docs only mention `KEY_CREATE_SUB_KEY`. `RegDeleteTreeW(HKCU, path)`
    deletes the key itself, not only its contents.
98. **A backslash in a path can defeat both a Python heredoc and `sed`.**
    `Ticker\\ticker.c` in a `bash` heredoc and `sed 's/Ticker\\ticker/.../'`
    both left `Desktop\Ticker\ticker.c` unchanged in phase 30, and only a
    printed "changed?" flag per file showed it. Match the
    separator with `.` (`sed -E 's/(Desktop.Ticker.)ticker/...'`) or write
    the script with the Write tool, as in 93.
99. **`ClampView` turns "show all" into eight candles.** `viewCount == 0`
    means the whole buffer (`GetView`), but `ClampView` raises anything below
    `MIN_VIEW`, zero included. TickC never clamps a zero view. A test that
    builds the state has to copy the order the app uses, not the call that
    looks right - the first 1d golden showed 8 of 200 candles, and only the
    picture showed it.
100. **A black capture from `golden.ps1` is a hidden panel, not a difference.**
    One run after phase 35's third commit gave all-black panel captures (every
    pixel differed); the rerun of the same exe was identical to `main`. In
    phase 36 it came back three runs in a row for the new build, and a control
    run of the old build was clean - which looked like a bug in the build.
    Logging `IsWindowVisible` in the capture turned it around: the next pair
    gave the new build right and the OLD build black, with `visible=False`.
    `PrintWindow` on a hidden panel gives black, and the panel is sometimes
    hidden by the time of the capture, whatever the build (the cause of the
    hiding was not found). `golden.ps1` now reopens a hidden panel and refuses
    to hash it (`HIDDEN`). Three black runs in a row are not evidence against
    a build: check the window state, and run the control more than once.
101. **PowerShell variables are case-insensitive.** `foreach ($n in ...)`
    overwrote `$N`, the scratchpad path, and the next `Save` wrote to
    `<case name>\png`. Loop variables get names no outer variable has.
102. **An image viewer's downscale lies about colors.** In the alert golden the
    alert lines looked light gray; the pixels were `86601B`
    (`CLR_ALERT_LINE`), exactly right. Read the pixel values before calling a
    color wrong.
103. **A successful Win32 call does not clear `GetLastError`.** The fixture
    lookup was `GetEnvironmentVariableW(...) == 0 || GetLastError() ==
    ERROR_ENVVAR_NOT_FOUND`. Phase 37 put a lookup of `TICKER_FORCE_DPI` in
    front of it; unset, that left `ERROR_ENVVAR_NOT_FOUND` behind, the
    fixture call succeeded without touching it, and the test build silently
    fetched live data - but only when the forced dpi was NOT set. The runs
    with a forced dpi were clean, the others showed prices that "flickered".
    Call `SetLastError(ERROR_SUCCESS)` before a call whose error you read on
    success, and suspect the data before the layout when a capture differs
    only in numbers.
104. **Check what the machine is before a DPI test.** The notes said the
    user's screen was at 150 %; `GetDpiForSystem` said 96. A capture that
    "did not scale" was a correct capture at 100 %. Read the dpi from the
    process (probe field 60) before judging a picture.
105. **Keys typed into a focused test panel change the capture.** A
    `golden.ps1` run in phase 37 had VOL switched off in every panel
    capture and two hidden panels; the likely cause is the user typing
    while the test panel had focus (not proven - the run was not
    instrumented). `golden.ps1` now reports `USER INPUT DURING RUN`; such a
    run is not evidence until it is repeated.
106. **One color cannot serve both sides of a light theme's text.** A
    role that carries light text at 4.5:1 needs relative luminance below
    about 0.175, and a role that is text at 4.5:1 on a light background
    needs it above that. Up/down are both (the stamp's surface and the
    header's text), and the amber alert is both (the tag's surface and
    the ghost tag's text). A table edit per failing pair runs around in
    circles. Find the roles that serve both sides and split them
    (`onAlert`, `alertText`), or pick the side (up/down: text-safe, light
    stamp text). Check the final table as a whole, since the best text
    color for a surface flips when the surface moves.
107. **The real pointer reaches the test panel.** Phase 40's captures
    were taken while the user worked. In separate runs a light `golden.ps1`
    capture had the symbol overlay open (only a real click or a right-click
    opens it), a ghost tag and its line (the pointer resting in the price
    column), and a hover box. A dark run had an alert in every panel
    capture, set by a click in the price column while the panel was at its
    opening position. The desktop capture, which starts from a fresh
    registry key, was clean, which placed the click in that run.
    `golden.ps1` now places the panel on the half of the screen the pointer
    is not on, and each capture reports `POINTER OVER PANEL`, alerts
    (field 22) and an open overlay (field 20). The pointer can still
    follow the panel. A flagged capture is not evidence, and a clean one
    from another run is.
108. **A clamp that runs on every fetch undoes a wish.** A followed view
    was clamped to the buffer in `MergeCandles` and `WorkerFetchKlines` on
    every fetch, so a range that wants 365 candles would have been cut to
    the 360 of the first fetch, and nothing would ever have asked for it
    to grow back. Keep the wish apart from the state it is clamped into
    (`rangeWant` next to `viewCount`) and reapply it wherever the state is
    set, not only where the wish is made.

---

## Backups

**Only `tickc.c.bak35` and `chart.c.bak35` are left** (2026-09-23). From
phase 34 the code is two files, so the backup is a pair. They are identical
to `tickc.c` and `chart.c` after phase 41 and are the rollback reference for
the build that is running. `ticker.c.bak` … `.bak24`, `tickc.c.bak25` …
`.bak27` and the pairs `.bak28` … `.bak34` (phases 34–40) are deleted: that history is in git.

The order was `.bak` … `.bak7` (phases 1–8), `.bak8` (phase 13), `.bak9`
(phase 14), `.bak10` (phase 15), `.bak11` (phase 16), `.bak12` (phase 17),
`.bak13` (phase 18), `.bak14` (phase 19), `.bak15` (phase 20), `.bak16`
(phase 21), `.bak17` (phase 22), `.bak18` (phase 23), `.bak19` (phase 24), `.bak20` (phase 25), `.bak21` (phase 26), `.bak22` (phase 27), `.bak23` (phase 28), `.bak24` (phase 29), `tickc.c.bak25` (phase 30), `.bak26` (phase 31), `.bak27` (phase 32), then the pairs `.bak28` (phase 34), `.bak29` (phase 35), `.bak30` (phase 36), `.bak31` (phase 37), `.bak32` (phase 38), `.bak33` (phase 39), `.bak34` (phase 40) and `.bak35` (phase 41). The files are ignored by
git; the pattern
is `*.bak[0-9]*`, with an asterisk, because `*.bak[0-9]` alone let the two-digit ones
through.
