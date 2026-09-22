# Ticker phase 2 — design

Date: 2026-09-16. Builds on phase 1 as documented in `WORKLOG.md`.
Starting point: commit `f7d3997`.

## Purpose

Phase 1 moved the network onto a worker thread and got painting down to
1.255 ms. The main thread therefore has spare capacity. Phase 2 uses it for four
things: smooth visual response, fault tolerance against unstable connections,
choosing symbol and interval at runtime, and visual context in the chart area
itself.

The goal is to lift the interface from a plain price chart to a coherent, clean
financial tool with direct context — without giving up speed.

The constraints from phase 1 stand: one file, no external dependencies beyond
Win32 and WinHTTP, no `malloc`, clean build at `/W4`, x86, ~3.3 MB footprint.

---

## Architecture

### The thread contract is unchanged

Phase 2 adds no threads and moves no responsibility between the two that exist.
The rules from phase 1 apply unchanged:

- The UI thread never touches WinHTTP.
- `PostMessage` never happens inside the lock.
- The worker thread fetches without the lock and locks only around the merge.

The lock domain is **extended** with: `symIdx`, `ivIdx`, `intervalMs`,
`configGen`, `lastOkTick`, `netFailures`, `evictedTotal`.

### New state split: target vs. display

The key idea in phase 2. `viewStart` and `viewCount` (int, lock-protected)
remain **the target** and are still owned by both threads as today. Next to
them come four **pure UI doubles** that no other thread touches:

| Field | Meaning |
|---|---|
| `dispStart` | animated position, can be fractional |
| `dispCount` | animated width, can be fractional |
| `dispMin` | animated lower price edge |
| `dispMax` | animated upper price edge |

The worker thread never sees the disp fields. It writes the target; the UI
thread eases toward it. This is why the animation does not touch the thread
contract at all.

---

## Part A — the animation timer, backoff and stale indicator

### A1. The animation timer

`TIMER_FADE_ID` is replaced by `TIMER_ANIM_ID`. One 16 ms timer drives
everything time-dependent: chrome fade, view easing, Y-axis easing, overlay
fade and the stale counter.

**Time-based, not step-based.** `SetTimer(16)` in practice fires every
~15.6 ms and gets coalesced under load. A fixed step length per tick therefore
gives different speeds depending on system load. Each tick measures the actual
elapsed time with `GetTickCount64()` against `lastAnimTick`.

Interpolation, per animated value:

```
d += (target - d) * (1.0 - exp(-dt / TAU));
if (fabs(target - d) < snapThreshold) d = target;
```

`TAU = 90.0` ms. `dt` is clamped to at most 100 ms, so that a long pause
(locked screen, heavy load) gives one jump instead of an apparently frozen
animation that then leaps.

The timer is started by any state change that leaves something unsettled, and
is **killed** when chrome, view, Y axis and overlay have all settled *and* the
connection is healthy. At rest no timer runs — the idle footprint is
unchanged.

`FADE_STEP` goes away. The chrome fade uses the same exponential form with a
shorter tau, and keeps today's perceived duration of ~130 ms.

### A2. Exponential backoff

Today: the worker thread silently discards errors with `return` and always
waits `TIMER_INTERVAL` (3000 ms).

```
netFailures = 0 on success and on hWakeEvent
wait        = min(3000 * 2^netFailures, 60000) adjusted by +/- 12.5 % jitter
```

The jitter keeps many clients from synchronizing against the server after a
shared outage. Source: `GetTickCount64()` low bits — no `rand()`, no seeding,
no global state.

The 60 s cap is chosen so that a long outage does not leave minute-long gaps
in the resumption, while we still do not hammer a dead connection.

**After three consecutive errors** `hConnect` is closed and set to NULL.
`HttpGet` creates it again on the next call, so DNS is looked up again.
Without this we stay stuck on an IP that no longer answers.

`hWakeEvent` (the panel was opened) always resets the backoff — a user who
opens the panel should get an attempt immediately, not wait out a minute.

### A3. Stale state

`lastOkTick` (`ULONGLONG`, `GetTickCount64`) is set under the lock on every
successful fetch.

```
stale = (now - lastOkTick) > 3 * TIMER_INTERVAL     // 9 s = two missed cycles
```

The threshold of three cycles, not one, keeps a single slow request from
blinking the indicator.

### A4. Making it visible

Deliberately muted — the user should be able to see it, not be interrupted by
it.

| Place | At rest | Offline |
|---|---|---|
| Header price | `CLR_TEXT` | `CLR_DIM` |
| Subtitle | `BTC/USDT  -  1m` | `BTC/USDT  -  1m  -  frakoblet 42s` |
| Tray tooltip | `BTC/USDT: $75872.21` | `BTC/USDT: $75872.21 (frakoblet)` |
| Tray icon | full digits | dimmed digits |
| Empty buffer | `Laster data fra Binance...` | `Ingen forbindelse - prover igjen om Ns` |

The last row fixes a real bug: today "Laster data fra Binance..." (Loading
data from Binance...) stays up forever if the connection is down at first
open. The message lies about the state.

`RenderMicroFontIcon` gets a color parameter for the dimmed icon.

The seconds counter requires the animation timer to be kept alive while we are
offline. It repaints **only when the digit actually changes** — not 60 times a
second.

---

## Part B — symbol, interval, overlay, watermark and persistence

### B1. Curated tables

```c
typedef struct { const wchar_t* api; const wchar_t* label; } SymbolDef;
typedef struct { const wchar_t* api; const wchar_t* label; long long ms; } IntervalDef;
```

Symbols: `BTCUSDT`, `ETHUSDT`, `SOLUSDT`, `BNBUSDT`.
Intervals: `1m`, `5m`, `15m`, `1h`, `4h`, `1d` — labeled `1m 5m 15m 1t 4t 1d`.

Curated, not free text. A fixed list means we know the price range and can
format icon, header and price axis correctly without guessing, and that no
fetch can fail on an unknown symbol.

### B2. configGen — the race that must be solved

**The scenario:** the thread is in the middle of a fetch for BTC. The user
switches to ETH. The UI thread empties the buffer. The BTC response comes back
and is merged into a buffer that now belongs to ETH. The chart shows BTC
prices under an ETH label.

This is the most serious failure mode in all of phase 2 — it gives silent,
wrong data instead of a visible crash.

**Solution:** a `configGen` counter.

```
UI thread on a switch (under the lock):
    symIdx/ivIdx = new
    intervalMs   = table lookup
    configGen++
    candleCount = 0; viewStart = 0; viewCount = 0
    followLive = TRUE; lastPrice = 0
  → SetEvent(hWakeEvent)

Worker thread:
    under the lock:  gen = configGen; sym = symIdx; iv = ivIdx
    without it:      build URL from (sym, iv); HttpGet; ParseKlines
    under the lock:  if (configGen != gen) discard; else MergeCandles
```

The discard happens **at merge**, not at fetch — the response can arrive at
any point along the way.

### B3. intervalMs spreads

`KLINE_MS` is a constant today and is used in three places. All become
`ctx->intervalMs`:

1. The gap check in `MergeCandles` (`2 * KLINE_MS`)
2. The seed check in `WorkerFetchKlines` (`5 * KLINE_MS`)
3. The span formatting in the header — `%dm`/`%dt` assumes 1m candles

The span formatting is generalized: the total number of minutes is
`vc * intervalMs / 60000`, formatted as minutes, hours or days.

### B4. Adaptive tray icon

The icon assumes BTC scale today: `price / 1000.0` gives `75.8`. SOL at $150
would give `0.2` — useless.

Divisor (1, 1000, 1 000 000) and number of decimals are chosen so that
`IconTextWidth() <= 16`. This is exactly the technique the log describes under
bug #5: measure the width of the **fully formatted string**, not against a
threshold.

### B5. The overlay

Drawn **inside the popup window's client area**. No new HWND.

This is the most important architecture choice in part B. Without a new
window there is no activation change, and we stay entirely out of the
territory where bugs #1 and #2 lived. A `TrackPopupMenu` would have sent
`WA_INACTIVE` to the panel and triggered auto-hide while the menu was open.

**Layout and hit testing share one function.** `OverlayLayout()` fills an
array of rectangles; both drawing and mouse clicks call it. The same
discipline `ChartGeometry` already follows, for exactly the same reason — the
log is clear that two independent computations of the same surface end up
pointing at different places.

Interaction:

| Action | Effect |
|---|---|
| Right-click in the chart area | opens the overlay |
| Click on an option | selects, closes, triggers a fetch |
| Click outside | closes without change |
| Hover | highlights the row |
| ESC | closes the overlay **before** it closes the panel |

While the overlay is open, chart interaction (pan, zoom, crosshair) is
blocked.

Fade in and out via the animation timer, with the same `Blend()` as the rest
of the chrome. The palette is `CLR_BG`/`CLR_BOX`/`CLR_BOXEDGE` — identical to
the hover box.

### B6. Dynamic background watermark

Symbol and interval are stamped into the background in large, clean
typography in a tone that sits ~3 % above the background. The look is taken
from professional financial terminals: the text should read as part of the
surface, not as a layer on top of it.

```c
#define CLR_WATERMARK  RGB(0x15, 0x19, 0x1F)   // #0D1117 + ~3 %
```

`#0D1117` is (13, 17, 23); the watermark is (21, 25, 31). The difference of 8
levels is ~3.1 % of full scale — visible enough to read, faint enough that the
candles and the grid keep all their contrast.

**Content:** the active symbol as the main line (`BTCUSDT`), the interval
below (`5m`). Both are read from the same `symIdx`/`ivIdx` as the overlay and
the header, so they can never get out of step.

**Place in the drawing order:** immediately after the background is set,
before grid, candles and axes. The chart thus flows cleanly over the text
without overlap or visual noise.

**Performance — cached as a bitmap, not redrawn per frame.** A `DrawTextW`
with a large font is not free; measured, it typically costs 0.05–0.30 ms, not
0.005 ms. The solution is to combine background and watermark into one cached
`HBITMAP`:

```
On a (W, H, symIdx, ivIdx) change:
    render background + watermark into wmBmp

Per frame:
    BitBlt(wmBmp) instead of FillRect(brBg)
```

This **replaces** today's `FillRect` — so it does not add a step, it swaps one
out. The net cost is a `BitBlt` of ~380×300 against a `FillRect` of the same
area, which is within measurement noise. The same discipline as the GDI cache
from phase 1: build when the inputs change, not per frame.

The cache is invalidated by `WM_SIZE`, by a config switch, and at startup.

**GDI accounting:** +1 `HBITMAP`, +1 `HFONT`. Phase 1 ended at 31 handles;
phase 2 lands at ~33. The number must be constant after startup — that is the
test.

### B7. Persistence

`HKCU\Software\Ticker`, `REG_DWORD`: `SymbolIndex`, `IntervalIndex`,
`PanelWidth`, `PanelHeight`.

Written on change and on exit. Read at startup, with a **bounds check** on the
indices — a registry that has been edited by hand or left behind by a newer
version with more symbols must not be able to index outside the table.

If the read fails, we fall back to BTC/USDT 1m. The registry is never a
precondition for the app starting.

When starting from the registry, `symIdx`/`ivIdx` are set **before** the
worker thread is started, so that the first fetch goes to the right pair and
the watermark is correct from the first frame.

---

## Part C — view and Y-axis easing

### C1. What is eased, and what is not

| Action | Behavior |
|---|---|
| Wheel panning | eased |
| Ctrl + wheel (zoom) | eased |
| Symbol/interval switch | no easing — the buffer is new |
| **Drag panning** | **follows the mouse directly** |
| Y axis on new data | eased |
| Index shift on eviction | no easing |

Drag panning is set on both the target and the display at the same time. An
eased drag feels sluggish, not smooth — the finger and the chart must stay
together.

### C2. Fractional indices in painting

This — not the easing math — is the real change in `DrawChart`.

With `dispStart = 142.7` there are half candles at both edges. The drawing
loop runs from `floor(dispStart)` to `ceil(dispStart + dispCount)`, with

```
x = left + ((double)i - dispStart) / dispCount * cw
```

and `IntersectClipRect` against the chart area, so that the edge candles do
not bleed into the price axis or the header. The clip region is restored
before the chrome is drawn.

Both loops (bodies and wicks) still run over visible candles, not over the
whole history — the performance characteristics from phase 1 are preserved.

### C3. HitCandle must read the same

`DrawChart` and `HitCandle` must **both** read the disp values. If one reads
the target and the other the display, the crosshair points at the wrong
candle in the middle of the animation. That is bug #7 from the log in new
clothes.

### C4. Index shift on eviction

`MergeCandles` adds to `evictedTotal` the number of candles that drop off the
front when the buffer is full. The UI thread keeps its own `dispEvictedSeen`,
and on every repaint:

```
delta = evictedTotal - dispEvictedSeen
dispStart     -= delta      // no easing
hoverIdx      -= delta
panAnchorView -= delta
dispEvictedSeen = evictedTotal
```

Without this the chart jumps one candle to the left every minute as soon as
the buffer has reached 1440.

This also cleans up the documented limitation that `hoverIdx` and
`panAnchorView` lag one candle behind until the next mouse move.

---

## Error handling

| Situation | Behavior |
|---|---|
| Network error | backoff, stale indicator, previous data stays |
| Network down at first open | `Ingen forbindelse - prover igjen om Ns` |
| Response arrives after config switch | discarded via `configGen` |
| Invalid registry content | bounds check, fall back to BTC/USDT 1m |
| Registry unavailable | ignored, the app starts normally |
| Watermark bitmap fails | skip, fall back to `FillRect` |
| Buffer full (1440) | oldest drops off, indices shift synchronously |
| Overlay open during config switch | closed, chart interaction resumes |

**Never:** crash, hang, or data shown under the wrong label.

---

## Testing

The technique from phase 1 applies: pull the function out of `ticker.c` with
`sed` into a small harness, so that the tests run against **the actual code**,
not a copy.

Unit-testable without Win32:

| Unit | What is verified |
|---|---|
| Backoff schedule | 3s, 6s, 12s, 24s, 48s, 60s, 60s; jitter within ±12.5 %; reset |
| Easing step | convergence, snap, clamping of `dt`, no overshoot |
| `configGen` discard | a response from the previous config is never merged |
| Adaptive icon formatting | all four symbols' price ranges give `IconTextWidth() <= 16` |
| Span formatting | all six intervals × representative `vc` |
| Overlay layout | drawing rectangles and hit rectangles are identical |
| Index shift | `dispStart`/`hoverIdx` follow `evictedTotal` exactly |
| `MergeCandles` | the six existing cases, now with variable `intervalMs` |

Empirically, per partial delivery:

- `/W4` clean, x86.
- GDI and USER handles flat through pan, zoom, overlay opening and config
  switch. Expected level after phase 2: ~33, constant.
- Painting stays under 1.3 ms. Easing adds ~60 repaints a second **while
  moving**, so the per-frame ceiling is what decides.
- The watermark is measured in isolation: painting with and without, same
  view, same window size. The claim to be checked is that the cached `BitBlt`
  is no more expensive than today's `FillRect`.
- Overlay and watermark colors are measured with `GetPixel`, not by eye. The
  log documents that judging downscaled screenshots by eye has given the wrong
  conclusion twice. The watermark at ~3 % contrast is exactly the case where
  the eye cannot be used.
- Backoff is verified by blocking `api.binance.com` and logging the actual
  wait times.

---

## Order

Three deliveries, each with its own build, its own measurement and its own
commit.

**A.** Animation timer + backoff + stale indicator — smallest, no dependencies
**B.** Symbol/interval + overlay + watermark + registry — builds on the timer for fade
**C.** View and Y-axis easing — last, touches painting the most

The watermark belongs to B because it reads the same `symIdx`/`ivIdx` as the
overlay and is loaded from the same registry lookup. Building it separately
would have meant introducing that state twice.

---

## Pitfalls that apply to all work in this file

From `WORKLOG.md`, repeated because they have struck before:

1. **Stop `ticker.exe` before you link.** Otherwise `LNK1104`.
2. **No forward declarations in the file.** New helper functions must come
   *before* their first use. This has struck three times.
3. **ASCII only in C comments.** `æøå` gives `C4819`. Unicode in `L""`
   strings is fine.
4. **`lParam` in `WM_MOUSEWHEEL` is screen coordinates**, not client.
5. **Synthetic mouse clicks are unreliable for testing.** Move the real
   pointer and jiggle it.
6. **`PostMessage` never inside the lock.**
