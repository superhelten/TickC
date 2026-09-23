# TickC

A crypto price ticker for the Windows tray, written in plain C against the Win32 API.
Two source files. No runtime, no installer, no dependencies beyond what ships with Windows.
The whole thing compiles to a single exe of about 200 KB.

I built it because I wanted the BTC price in the corner of my screen without keeping
a browser tab open, and without a 150 MB Electron app. It grew from there.

![The chart panel: candles, volume, SMA 20, EMA 50, VWAP and the day levels](docs/images/panel.png)

## What it does

- **Tray icon** with the live price. Updates every 3 seconds.
- **Chart panel** (left-click the icon): candlesticks, volume bars, SMA 20, EMA 50,
and a daily VWAP. You also get today's high and low, and yesterday's high, low and close
(labelled HOD, LOD, PDH, PDL, PDC).
- **Symbols:** BTC, ETH, SOL and BNB against USDT.
- **Intervals:** 1m, 5m, 15m, 1h, 4h, 1d.
- **Price alerts.** Click the price column to set one. When the price gets there you get a
balloon and a sound, even with the panel closed.
- **Desktop mode.** The chart sits on your wallpaper, behind the desktop icons.
It stays quiet on purpose: no volume or moving averages there unless you turn them on.
- **Dark or light.** A light theme from the tray menu (or `T`), with every text on it
readable at WCAG AA contrast. The panel and desktop mode each have their own choice.
- **Scrolls back in time.** Pan into the left edge and it fetches older candles,
up to 6000 of them.

![Desktop mode: the chart drawn on the wallpaper, behind the icons](docs/images/desktop-mode.png)

## Getting it

Download `TickC.exe` from the [Releases](../../releases) page, put it anywhere, and run it.
No installer. Or build it yourself, below.

## Requirements

- Windows 8 or newer (developed on Windows 11)
- To build: MSVC, either Visual Studio or the free
[Build Tools for Visual Studio](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022),
with the Windows SDK

## Building

Open a **Developer Command Prompt for VS** in the repo folder and run:

```
cl /nologo /W4 /O2 tickc.c chart.c /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:tickc.manifest /OUT:TickC.exe
```

That's it. The libraries are pulled in with `#pragma comment(lib, ...)` in the source,
so there's no build script to keep in sync. Don't skip the manifest: without it
Windows refuses the layered child window, and desktop mode shows up blank.

### Tests

The chart engine (`chart.c`) has golden tests that draw it into a memory DC, with
no window and no network, from seeded synthetic candles:

```
cl /nologo /W4 /O2 /I. /Fo:tests\ /Fe:tests\chart_golden.exe tests\chart_golden.c chart.c user32.lib gdi32.lib
tests\chart_golden.exe
```

Before the pictures, the run checks every text/background pair of the light theme
against WCAG AA (4.5:1). Each case is hashed and compared with `tests/golden/chart.txt`; a failing case
is written to `tests/out/` as a BMP. Text goes through the installed fonts and
the ClearType setting, so the hashes hold for the machine that wrote them. On
another machine, look at the pictures (`--bmp`) and rewrite them with `--update`.

## Usage

Run `TickC.exe`. An icon appears in the tray.


| Action                     | What happens                                                     |
| -------------------------- | ---------------------------------------------------------------- |
| Left-click the tray icon   | Show or hide the chart panel                                     |
| Right-click the tray icon  | Symbol, interval, overlays, theme, desktop mode, autostart, quit |
| `TickC.exe --desktop-mode` | Start with the chart on the desktop                              |


In the chart panel:


| Key                             | Action                                                            |
| ------------------------------- | ----------------------------------------------------------------- |
| Mouse wheel, `+` / `-`          | Zoom                                                              |
| Drag, `←` / `→`                 | Pan                                                               |
| `PgUp` / `PgDn`                 | Jump one screen                                                   |
| `Home` / `End`                  | Oldest / newest candle                                            |
| `1` … `6`                       | Switch interval                                                   |
| `V`                             | Volume bars on/off                                                |
| `M`                             | Indicators on/off (moving averages, VWAP, levels)                 |
| `I`                             | RSI band on/off (RSI 14 under the chart)                          |
| `T`                             | Light theme on/off                                                |
| `A`                             | Set an alert at the crosshair price                               |
| `R`, double-click               | Reset zoom and pan                                                |
| `Esc`                           | Close the symbol picker, then reset the view, then hide the panel |
| `Ctrl`+`0`                      | Reset window size and position                                    |
| `Ctrl`+`N`                      | Open another panel                                                |
| `Ctrl`+`M` / `F11` / `Ctrl`+`W` | Minimize / maximize / close                                       |


## What it writes to your system

Nothing outside your user profile. No admin rights needed.

- Settings live in `HKCU\Software\TickC`.
- "Start at sign-in" adds a `TickC` value under
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.

To remove it completely, untick autostart in the tray menu, quit, and delete
`HKCU\Software\TickC`.

Earlier builds were called Ticker and used `HKCU\Software\Ticker`. The first time
TickC starts, it moves those settings, your price alerts and the autostart entry over
to the new name, then removes the old ones.

## How it's put together

`tickc.c` is the app: tray icon, window, worker thread, registry, alerts. `chart.c`
is the chart engine behind `chart.h`: geometry, view, indicators, sessions and all
the drawing, with no window, lock or network in it, so it can be reused elsewhere.
A worker thread fetches data over HTTPS with WinHTTP.
The UI thread draws with GDI into a back buffer that's kept between frames.
Nothing from the network is trusted: prices that come back as NaN, infinity, zero
or garbage are dropped before they reach the chart.

`WORKLOG.md` is the development diary. It covers why things are the way they
are, what was measured, and the pitfalls I ran into. The `(phase N)` notes in the
code comments point to its sections.

## Data source

Market data comes from the public [Binance API](https://developers.binance.com/docs/binance-spot-api-docs)
(`api.binance.com`). No account or API key is involved.
This project has no connection to Binance, and Binance doesn't endorse it.

Prices are for information only. Don't trade on a tray icon.

## License

MIT. See [LICENSE](LICENSE).