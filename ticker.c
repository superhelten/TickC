#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

#define WM_TRAYICON      (WM_USER + 1)
#define WM_APP_DATA      (WM_APP + 1)   // arbeidertraden har nye data
#define ID_TRAY_EXIT     1001
#define TIMER_INTERVAL   3000 // 3 sekunder

// --- Popup / graf ---
#define POPUP_W          380
#define POPUP_H          300
#define POPUP_MIN_W      260
#define POPUP_MIN_H      180
#define RESIZE_BORDER    6     // bredde pa sonen som starter storrelsesendring
#define MAX_CANDLES      1440  // 24 timer med 1m-lys, bygges opp mens panelet star apent
#define SEED_COUNT       300   // forste henting: 5 timer i ett jafs
#define DEFAULT_VIEW     300   // synlig utsnitt ved apning
#define MIN_VIEW         8     // minste antall synlige lys ved full zoom
#define ZOOM_STEP        1.2   // per musehjul-hakk
#define REOPEN_GUARD_MS  250   // hindrer at klikk-for-aa-lukke aapner igjen med en gang
#define SHOW_GRACE_MS    400   // ignorer fokustap rett etter apning
#define TIMER_ANIM_ID    2
#define ANIM_INTERVAL    16     // ~60 fps
// Tidsbasert interpolasjon, ikke fast steg per tikk: SetTimer(16) fyrer i
// praksis hver ~15,6 ms og slaas sammen under last. Fast steglengde ville
// gitt ulik hastighet avhengig av systembelastning.
// Eksponentiell kurve har en hale: tau=55 gir ~90 % pa 130 ms (der oyet
// ser faden som ferdig) og full innsetting paa ~340 ms. Halen koster kun
// timer-tikk, ikke opptegninger - vi tegner bare naar det avrundede
// niva faktisk endrer seg.
#define ANIM_TAU_CHROME  55.0   // tidskonstant chrome-fade (ms)
#define ANIM_DT_MAX      100.0  // klemmer dt, saa en lang pause gir ett hopp

// View-easing. Litt lengre tau enn chromet - en panorering er en storre
// bevegelse enn en fade - men ikke mye: 2,3*tau er der oyet ser den som
// ferdig, altsaa ~160 ms.
#define ANIM_TAU_VIEW    70.0

// Snapp naar det som gjenstaar er mindre enn en kvart piksel PAA SKJERMEN.
// Terskelen regnes derfor om fra piksler til lys (X) og til pris (Y) ved hver
// tikk, i stedet for a vaere et fast tall i enhetene.
//
// Maalt hvorfor: med en fast terskel paa 0,01 lys tok en panorering paa 300
// lys 71 tikk - 1,14 s - for klokka kunne do, og de siste 40 tikkene flyttet
// under en tidels piksel. Og et fast tall i PRIS virker ikke i det hele tatt
// paa tvers av fire symboler: 0,5 dollar er en tredel av SOLs hele spenn og
// under en tusendel av BTCs.
#define SNAP_PX          0.25

// --- Nettverksrobusthet ---
#define NET_RETRY_MAX    60000  // tak for eksponentiell backoff (ms)
#define NET_RECONNECT_AT 3      // antall feil for hConnect slippes (ny DNS)
#define STALE_AFTER      (3 * TIMER_INTERVAL)  // 9 s = to tapte sykluser
#define CLOSE_SZ         20
#define HDR_TEXT_L       (PAD_L + 12)  // plass til grip-prikkene

// Layout innenfor popup-vinduet
#define PAD_L            10
#define PAD_R            54
#define HEADER_H         44
#define PAD_B            10

// Palett (matcher tray-ikonet)
#define CLR_BG           RGB(0x0D, 0x11, 0x17)
#define CLR_GRID         RGB(0x1C, 0x22, 0x2B)
#define CLR_UP           RGB(0x00, 0xFF, 0x66)
#define CLR_DOWN         RGB(0xFF, 0x49, 0x66)
#define CLR_TEXT         RGB(0xC3, 0xBC, 0xDB)
#define CLR_DIM          RGB(0x6E, 0x76, 0x81)
#define CLR_CROSS        RGB(0x55, 0x5F, 0x6E)
#define CLR_BOX          RGB(0x16, 0x1D, 0x27)
#define CLR_BOXEDGE      RGB(0x33, 0x3D, 0x4B)
#define CLR_HDRHOT       RGB(0x16, 0x1D, 0x27)
#define CLR_CLOSEHOT     RGB(0xC0, 0x2A, 0x3E)
#define CLR_WHITE        RGB(0xFF, 0xFF, 0xFF)
#define CLR_WATERMARK    RGB(0x15, 0x19, 0x1F)   // CLR_BG + ~3 %
// Vannmerkets fonthoyde = klemt(chart-hoyde / 5, 32, 120), grensene i
// logiske piksler.
#define WM_FONT_DIV      5
#define WM_FONT_MIN      32
#define WM_FONT_MAX      120

// Kuratert, ikke fritekst. En fast liste betyr at vi kjenner prisomraadet og
// kan formatere ikon, header og prisakse riktig uten a gjette, og at ingen
// henting kan feile paa et ukjent symbol.
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
    { L"1h",  L"1t",  3600000LL },
    { L"4h",  L"4t",  14400000LL },
    { L"1d",  L"1d",  86400000LL },
};
#define SYMBOL_COUNT   ((int)(sizeof(SYMBOLS) / sizeof(SYMBOLS[0])))
#define INTERVAL_COUNT ((int)(sizeof(INTERVALS) / sizeof(INTERVALS[0])))

// 4x9 piksel-font. En rad per byte, bit 3 = venstre kolonne, bit 0 = hoyre.
// Ett linje med 9px hoye sifre er nesten dobbelt saa lesbart som to linjer
// med 5px sifre, og "75.8" fyller noyaktig 16px naar punktumet er 1px bredt.
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
    {0,0,0,0,0,0,0,0,0},                   // . (1px bred, tegnes spesielt)
    {0x8,0x8,0x8,0x9,0xA,0xC,0xA,0x9,0x9}, // k
    {0,0,0,0,0,0,0,0,0}                    // ' ' (space)
};

typedef struct {
    long long openTime; // Unix-tid i millisekunder
    double open;
    double high;
    double low;
    double close;
} Candle;

// Chart-flaten regnes ut to steder (tegning og muse-treff) - de MA
// vaere enige, ellers peker crosshairet pa feil lys.
typedef struct { int left, top, right, bottom, cw, ch; } ChartRect;

typedef struct {
    HWND hWnd;
    HWND hPopup;
    NOTIFYICONDATAW nid;
    HINTERNET hSession;
    HINTERNET hConnect;
    wchar_t fullPriceStr[64];
    double lastPrice;

    // --- Runtime-konfig. Laasebeskyttet: UI skriver, arbeidertraden leser. ---
    int  symIdx;          // indeks i SYMBOLS
    int  ivIdx;           // indeks i INTERVALS
    long long intervalMs; // INTERVALS[ivIdx].ms, kopiert ut for rask lesing
    // Teller opp ved hvert konfigbytte. Arbeidertraden tar en kopi for
    // hentingen og forkaster svaret hvis telleren har endret seg naar den
    // kommer tilbake. Uten dette flettes BTC-lys inn i et ETH-buffer.
    unsigned configGen;

    Candle candles[MAX_CANDLES];
    int candleCount;

    // Synlig utsnitt av lys-arrayet. viewCount = 0 betyr "vis alt".
    int viewStart;
    int viewCount;
    BOOL followLive;     // utsnittet ligger ytterst til hoyre og folger nye lys
    BOOL panning;        // drar grafen sideveis akkurat na
    int  panAnchorX;     // muse-X da panoreringen startet
    int  panAnchorView;  // viewStart da panoreringen startet

    HFONT hFontBig;
    HFONT hFontSmall;
    ULONGLONG lastHideTick;
    ULONGLONG shownTick;

    int hoverIdx;        // indeks til lyset under pekeren, -1 = ingen
    int hoverY;          // muse-Y i klientkoordinater
    BOOL trackingMouse;  // om WM_MOUSELEAVE er bestilt
    BOOL pinned;         // satt nar brukeren drar/endrer storrelse
    BOOL inSizeMove;     // inne i Windows sin modale flytte-/resize-lokke

    // Kontrollene (kryss, grip, ramme) er usynlige i hvile og tones inn
    // nar musa er over vinduet. chrome er fade-nivaet 0-255.
    BOOL windowHot;
    BOOL headerHot;
    BOOL closeHot;
    int  chrome;          // avrundet fade-niva, brukes av tegning og GDI-cache
    double chromeF;       // selve den animerte verdien

    // --- Animasjonsklokke ---
    // En timer driver alt tidsavhengig: chrome-fade, stale-telleren og
    // (fra del C) view- og Y-akse-easing. Den lever bare mens noe faktisk
    // er i bevegelse, og drepes naar alt har satt seg.
    ULONGLONG lastAnimTick;
    BOOL   animRunning;
    int    staleSecsShown;   // sist tegnede sekundtall, hindrer 60 fps paa en teller

    // --- Overlay for symbol-/intervallvalg ---
    // overlayOpen er den LOGISKE tilstanden og styrer treffdeteksjon.
    // overlayF er fade-nivaet og styrer bare tegning. Under uttoning er
    // overlayF > 0 mens overlayOpen er FALSE - da skal klikk ga til grafen.
    BOOL   overlayOpen;
    double overlayF;      // 0-255
    int    overlayHot;    // indeks i rows[], -1 = ingen

    // --- View- og Y-akse-easing ---
    // Rene UI-doubler. Arbeidertraden ser dem ALDRI. viewStart/viewCount er
    // maalet og er laasebeskyttet; disse er visningen og eies av UI-traden
    // alene. Det er derfor easingen ikke rorer traadkontrakten.
    double dispStart, dispCount;   // brokdels-utsnitt
    double dispMin, dispMax;       // animert prisakse
    BOOL   dispValid;              // FALSE = snap ved neste oppdatering
    long long dispEvictedSeen;     // utkastinger UI har kompensert for

    // --- Arbeidertrad ---
    // Laasen dekker candles[], candleCount, viewStart, viewCount,
    // followLive, lastPrice og hPopup. Alt annet rores kun av UI-traden.
    CRITICAL_SECTION lock;
    HANDLE hThread;
    HANDLE hStopEvent;   // manuell reset: signaliserer avslutning
    HANDLE hWakeEvent;   // auto reset: hent NA (panelet ble apnet)

    // Nettverkshelse. Laasebeskyttet - arbeidertraden skriver, UI leser.
    ULONGLONG lastOkTick;    // GetTickCount64 ved siste vellykkede henting
    ULONGLONG nextRetryTick; // naar neste forsok er planlagt
    int       netFailures;   // sammenhengende feil, driver backoffen
    long long evictedTotal;  // lys som har falt ut i front, monotont

    // --- Bufrede GDI-objekter ---
    // Faste farger lages en gang ved oppstart i stedet for 16 ganger
    // per opptegning.
    HPEN   penGrid, penUp, penDown, penCross;
    HPEN   penLastUp, penLastDown;   // stiplet siste-pris-linje
    HBRUSH brBg, brUp, brDown, brBox, brBoxEdge;

    // Blandede farger avhenger av fade-nivaet, sa de bygges bare naar
    // (chrome, closeHot) faktisk endrer seg - ikke hvert bilde.
    int    cacheChrome;
    BOOL   cacheCloseHot;
    BOOL   cacheValid;
    HPEN   penDim, penX;
    HBRUSH brDim, brEdge, brHdr, brClose;

    // --- Vannmerke-cache ---
    // Bakgrunn + vannmerke bakt sammen i en bitmap. Denne ERSTATTER dagens
    // FillRect - den legger ikke til et steg. En DrawTextW med stor font
    // koster 0,05-0,30 ms og hoerer ikke hjemme per bilde.
    HBITMAP wmBmp;
    HDC     wmDC;
    HBITMAP wmOldBmp;
    HFONT   hFontWm;
    int     wmFontH;       // fonthoyden cachen ble bygget for
    int     wmW, wmH;      // storrelsen bitmapen ble bygget for
    int     wmSym, wmIv;   // konfigen den ble bygget for
    BOOL    wmValid;
} AppContext;

static AppContext g_Ctx;
static int g_savedPanelW = 0;   // panelstorrelse fra registret, 0 = ubrukt
static int g_savedPanelH = 0;
static char s_httpBuf[98304];    // 300 lys gir ~50 KB svar
static Candle s_incoming[SEED_COUNT];

// Holder utsnittet innenfor dataene.
static void ClampView(AppContext* ctx) {
    int n = ctx->candleCount;
    if (n <= 0) { ctx->viewStart = 0; ctx->viewCount = 0; return; }
    if (ctx->viewCount < MIN_VIEW) ctx->viewCount = MIN_VIEW;
    if (ctx->viewCount > n)        ctx->viewCount = n;
    if (ctx->viewStart > n - ctx->viewCount) ctx->viewStart = n - ctx->viewCount;
    if (ctx->viewStart < 0)        ctx->viewStart = 0;
}

// Leser ut gjeldende utsnitt, med "vis alt" som standard.
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
// Animasjon og nettverkshelse - rene funksjoner, ingen tilstand.
// Begge er enhetstestbare uten Win32.
// ---------------------------------------------------------------------------

// Eksponentiell interpolasjon mot et mal. Rammeratefri: dobbelt saa lang dt
// gir samme resultat som to halve steg, saa animasjonen gaar like fort enten
// timeren fyrer jevnt eller meldingene slaas sammen under last.
static double AnimStep(double cur, double target, double dt, double tau, double snap) {
    if (dt <= 0.0) return cur;
    if (dt > ANIM_DT_MAX) dt = ANIM_DT_MAX;

    cur += (target - cur) * (1.0 - exp(-dt / tau));
    if (fabs(target - cur) < snap) cur = target;
    return cur;
}

// Eksponentiell backoff med jitter. 3s, 6s, 12s, 24s, 48s, deretter tak paa
// 60s. Jitteren hindrer at mange klienter synkroniserer seg mot serveren
// etter et felles avbrudd - den hentes fra klokkas lavbiter, saa vi slipper
// rand() og global tilstand.
static DWORD NetBackoffMs(int failures, ULONGLONG tickSeed) {
    if (failures <= 0) return TIMER_INTERVAL;

    // Skift i stedet for pow, og stopp for overflow kan bli et tema.
    DWORD base = TIMER_INTERVAL;
    for (int i = 0; i < failures && base < NET_RETRY_MAX; ++i) base *= 2;
    if (base > NET_RETRY_MAX) base = NET_RETRY_MAX;

    // +/- 12,5 %: base/8 spredt over 256 trinn.
    DWORD span  = base / 4;
    DWORD delta = (DWORD)(tickSeed & 0xFF) * span / 255;
    DWORD out   = base - span / 2 + delta;

    // Jitteren legges PAA basen, saa den kan skyve oss over taket. Uten
    // denne klemmingen ga failures>=5 opptil 67,5 s - maalt, ikke antatt.
    if (out > NET_RETRY_MAX) out = NET_RETRY_MAX;
    return out;
}

// ---------------------------------------------------------------------------
// Registret. HKCU\Software\Ticker. Aldri en forutsetning for at appen
// starter - feiler lesningen, faller vi tilbake pa BTC/USDT 1m.
// Plassert her, blant de rene hjelpefunksjonene, fordi ApplyConfigChoice
// lenger nede kaller SaveConfig. Fila har ingen forward-deklarasjoner.
// ---------------------------------------------------------------------------

#define REG_PATH L"Software\\Ticker"

static DWORD RegReadDword(HKEY k, const wchar_t* name, DWORD fallback) {
    DWORD v = 0, cb = sizeof(v), type = 0;
    if (RegQueryValueExW(k, name, NULL, &type, (BYTE*)&v, &cb) == ERROR_SUCCESS &&
        type == REG_DWORD && cb == sizeof(v)) {
        return v;
    }
    return fallback;
}

static void LoadConfig(AppContext* ctx, int* outW, int* outH) {
    // Standardverdiene settes FORST, slik at enhver feilsti nedenfor lander
    // pa noe gyldig uten egen behandling.
    ctx->symIdx     = 0;
    ctx->ivIdx      = 0;
    ctx->intervalMs = INTERVALS[0].ms;
    *outW = 0; *outH = 0;

    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return;
    }
    DWORD sy = RegReadDword(k, L"SymbolIndex",   0);
    DWORD iv = RegReadDword(k, L"IntervalIndex", 0);
    DWORD w  = RegReadDword(k, L"PanelWidth",    0);
    DWORD h  = RegReadDword(k, L"PanelHeight",   0);
    RegCloseKey(k);

    // Bundet sjekk. Et register redigert for hand, eller etterlatt av en
    // nyere versjon med flere symboler, skal ikke kunne indeksere utenfor
    // tabellen.
    if (sy < (DWORD)SYMBOL_COUNT)   ctx->symIdx = (int)sy;
    if (iv < (DWORD)INTERVAL_COUNT) {
        ctx->ivIdx      = (int)iv;
        ctx->intervalMs = INTERVALS[iv].ms;
    }
    if (w >= 240 && w <= 4096) *outW = (int)w;
    if (h >= 160 && h <= 4096) *outH = (int)h;
}

static void SaveConfig(const AppContext* ctx, int panelW, int panelH) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0,
                        KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS) {
        return;   // ingen skriverett: stille, appen fungerer likevel
    }
    DWORD sy = (DWORD)ctx->symIdx, iv = (DWORD)ctx->ivIdx;
    RegSetValueExW(k, L"SymbolIndex",   0, REG_DWORD, (const BYTE*)&sy, sizeof(sy));
    RegSetValueExW(k, L"IntervalIndex", 0, REG_DWORD, (const BYTE*)&iv, sizeof(iv));
    if (panelW > 0 && panelH > 0) {
        DWORD w = (DWORD)panelW, h = (DWORD)panelH;
        RegSetValueExW(k, L"PanelWidth",  0, REG_DWORD, (const BYTE*)&w, sizeof(w));
        RegSetValueExW(k, L"PanelHeight", 0, REG_DWORD, (const BYTE*)&h, sizeof(h));
    }
    RegCloseKey(k);
}

// Spennet folger zoomen - "(60m)" ville vaert feil sa snart man zoomer. Med
// variabelt intervall holder det ikke lenger a telle lys som minutter: 300
// lys a 1d er ti maneder, ikke fem timer.
static void FormatSpan(int vc, long long intervalMs, wchar_t* out, size_t cch) {
    long long mins = (long long)vc * intervalMs / 60000LL;
    if (mins < 60) {
        swprintf_s(out, cch, L"%lldm", mins);
        return;
    }
    long long hours = mins / 60, rm = mins % 60;
    if (hours < 24) {
        if (rm) swprintf_s(out, cch, L"%lldt %lldm", hours, rm);
        else    swprintf_s(out, cch, L"%lldt", hours);
        return;
    }
    long long days = hours / 24, rh = hours % 24;
    if (rh) swprintf_s(out, cch, L"%lldd %lldt", days, rh);
    else    swprintf_s(out, cch, L"%lldd", days);
}

// Slar innkommende lys sammen med bufferet paa openTime: samme tidsstempel
// oppdaterer (det siste lyset endrer seg mens det formes), nyere legges til.
// Dette er det som gjor panorering stabil - uten det ville indeksene
// forskjovet seg for hver henting og utsnittet drevet av gaarde.
static void MergeCandles(AppContext* ctx, const Candle* in, int count) {
    if (count <= 0) return;

    // Hull mellom bufferet og det nye settet (panelet har vaert lukket en
    // stund) -> start pa nytt. Ellers ville grafen tegnet en sammenhengende
    // kurve tvers over dodtid.
    if (ctx->candleCount > 0 &&
        in[0].openTime > ctx->candles[ctx->candleCount - 1].openTime + 2 * ctx->intervalMs) {
        ctx->candleCount = 0;
        ctx->viewStart   = 0;
        ctx->viewCount   = 0;
        ctx->followLive  = TRUE;
        ctx->dispValid   = FALSE;   // nytt buffer: ingenting a ease fra
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

        if (c->openTime == lastT) {          // lyset som holder pa a formes
            ctx->candles[n - 1] = *c;
            continue;
        }

        if (c->openTime > lastT) {           // nytt lys
            if (n >= MAX_CANDLES) {          // eldste faller ut
                memmove(ctx->candles, ctx->candles + 1, (size_t)(n - 1) * sizeof(Candle));
                n--;
                ctx->candleCount = n;
                ctx->evictedTotal++;   // UI-traden forskyver disp-indeksene mot denne
                if (ctx->viewStart > 0) ctx->viewStart--;
            }
            ctx->candles[n]  = *c;
            ctx->candleCount = n + 1;
            continue;
        }

        // Eldre enn siste: oppdater hvis vi allerede har det
        for (int j = n - 2; j >= 0; --j) {
            if (ctx->candles[j].openTime == c->openTime) { ctx->candles[j] = *c; break; }
            if (ctx->candles[j].openTime <  c->openTime) break;
        }
    }

    // Sto utsnittet ytterst til hoyre, skal det folge de nye lysene.
    // Har brukeren panorert bakover, blir det staaende i ro.
    if (ctx->followLive && ctx->viewCount > 0) {
        ctx->viewStart = ctx->candleCount - ctx->viewCount;
    }
    ClampView(ctx);
}

static inline int GlyphIndex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c == '.' || c == ',') return IDX_DOT;
    if (c == 'k' || c == 'K') return 11;
    return IDX_SPACE;
}

// Punktumet er 1px bredt slik at "75.8" akkurat rommes innenfor 16px.
static inline int GlyphWidth(int idx) {
    return (idx == IDX_DOT) ? 1 : GLYPH_W;
}

// Total bredde i piksler, inkludert 1px mellomrom mellom glyfene.
static int IconTextWidth(const char* s) {
    int total = 0;
    for (int i = 0; s[i]; ++i) {
        total += GlyphWidth(GlyphIndex(s[i]));
        if (s[i + 1]) total += 1;
    }
    return total;
}

// Velger divisor og antall desimaler slik at den FERDIG FORMATERTE strengen
// faar plass paa 16 px. Terskler paa selve prisen fanger ikke tilfellet der
// "%.1f" runder 99950 opp til "100.0" - det er bredden som teller. Se feil #5
// i ARBEIDSLOGG.md.
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
    // Ingen kandidat passer: klipp heller enn a vise ingenting. Opptegningen
    // i RenderMicroFontIcon er bundet sjekket.
    snprintf(out, cb, "%.0f", price / 1000000.0);
}

// argb: fargen sifrene tegnes med. Dempes naar forbindelsen er borte, slik
// at ikonet forteller at tallet ikke lenger er ferskt.
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

    // Bakgrunn: ARGB mork koksgraa (0xFF0D1117)
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

// Felles GET mot api.binance.com. Leser hele svaret i en lokke - et enkelt
// WinHttpReadData-kall returnerer bare det som tilfeldigvis ligger i bufferet.
static BOOL HttpGet(AppContext* ctx, const wchar_t* path, char* buf, DWORD bufSize) {
    if (bufSize == 0) return FALSE;
    buf[0] = '\0';

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

    WinHttpCloseHandle(hRequest);
    return ok;
}

static BOOL FastParsePrice(const char* json, double* outPrice) {
    const char* key = "\"price\":\"";
    const char* pos = strstr(json, key);
    if (!pos) return FALSE;

    pos += strlen(key);
    *outPrice = atof(pos);
    return TRUE;
}

// Binance klines: [[openTime,"o","h","l","c","v",closeTime,...], ...]
// Vi trenger felt 1-4 (open/high/low/close) fra hver indre array.
static int ParseKlines(const char* json, Candle* out, int maxCount) {
    int count = 0;
    const char* p = json;

    while (*p && *p != '[') p++;
    if (!*p) return 0;
    p++; // forbi ytre '['

    while (*p && count < maxCount) {
        while (*p && *p != '[' && *p != ']') p++;
        if (*p != '[') break; // traff ']' -> slutten av ytre array
        p++;                  // forbi indre '['

        // Felt 0 = openTime (Unix-ms, tall uten hermetegn)
        while (*p == ' ') p++;
        long long openTime = _atoi64(p);
        while (*p && *p != ',') p++;
        if (!*p) break;
        p++;

        double v[4];
        int ok = 1;
        for (int f = 0; f < 4; ++f) {
            while (*p && *p != '"') p++;
            if (!*p) { ok = 0; break; }
            p++;                 // forbi aapnende hermetegn
            v[f] = atof(p);
            while (*p && *p != '"') p++;
            if (!*p) { ok = 0; break; }
            p++;                 // forbi lukkende hermetegn
            while (*p && *p != ',' && *p != ']') p++;
            if (*p == ',') p++;
        }
        if (!ok) break;

        out[count].openTime = openTime;
        out[count].open  = v[0];
        out[count].high  = v[1];
        out[count].low   = v[2];
        out[count].close = v[3];
        count++;

        // Hopp til slutten av denne indre arrayen
        int depth = 1;
        while (*p && depth > 0) {
            if (*p == '[') depth++;
            else if (*p == ']') depth--;
            p++;
        }
    }

    return count;
}

// ---------------------------------------------------------------------------
// Arbeidertrad: ALL nettverkstrafikk skjer her. UI-traden rorer aldri
// WinHTTP, og blir derfor aldri staaende og vente paa linja.
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

    // Selve hentingen skjer UTEN laas - den kan ta hundrevis av
    // millisekunder, og UI-traden skal kunne tegne hele tiden.
    if (!HttpGet(ctx, path, s_httpBuf, (DWORD)sizeof(s_httpBuf))) return FALSE;

    int n = ParseKlines(s_httpBuf, s_incoming, SEED_COUNT);
    if (n <= 0) return FALSE;

    EnterCriticalSection(&ctx->lock);
    // Forkastingen skjer ved FLETTING, ikke ved henting - svaret kan ankomme
    // naar som helst underveis, ogsaa etter at brukeren har byttet symbol.
    // Returnerer TRUE: et forkastet svar er ikke en nettverksfeil, og skal
    // ikke telle opp backoffen hver gang brukeren bytter.
    if (ctx->configGen != gen) {
        LeaveCriticalSection(&ctx->lock);
        return TRUE;
    }
    MergeCandles(ctx, s_incoming, n);

    // Lysgrenen MA ogsaa sette lastPrice. Staar panelet apent, henter traden
    // bare lys - da ble lastPrice aldri skrevet, og etter et symbolbytte
    // (som nullstiller den) returnerte UpdateIcon paa price <= 0. Ikonet og
    // verktoytipset ble staaende paa FORRIGE symbols pris og etikett saa
    // lenge panelet var apent. Maalt: 15 s etter bytte til SOL leste ikonet
    // fortsatt 75.9 - BTC - mens panelet viste SOL.
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

    // Prisen har noyaktig samme kapplop som lysene, og den styrer tray-ikonet.
    // Uten sjekken viser ikonet forrige symbols pris under nytt navn.
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
        HWND hp = ctx->hPopup;
        LeaveCriticalSection(&ctx->lock);

        // Star grafen apen trenger vi lys; ellers holder det med prisen.
        BOOL ok = (hp && IsWindowVisible(hp)) ? WorkerFetchKlines(ctx)
                                              : WorkerFetchPrice(ctx);

        ULONGLONG now = GetTickCount64();
        DWORD wait;
        int failures;

        EnterCriticalSection(&ctx->lock);
        if (ok) {
            ctx->netFailures = 0;
            ctx->lastOkTick  = now;
        } else if (ctx->netFailures < 32) {
            ctx->netFailures++;   // taket hindrer overflow ved lang nedetid
        }
        failures = ctx->netFailures;
        wait = NetBackoffMs(failures, now);
        ctx->nextRetryTick = now + wait;
        LeaveCriticalSection(&ctx->lock);

        // Henger vi fast paa en IP som ikke lenger svarer, hjelper det ikke
        // aa prove igjen mot samme handtak. Slipper forbindelsen saa HttpGet
        // bygger den paa nytt og DNS slaas opp igjen. hConnect eies av denne
        // traden alene, saa den trenger ingen laas - failures derimot maa
        // leses av under laasen over.
        if (!ok && failures == NET_RECONNECT_AT && ctx->hConnect) {
            WinHttpCloseHandle(ctx->hConnect);
            ctx->hConnect = NULL;
        }

        // PostMessage MA staa utenfor laasen - ellers kan UI-traden sitte
        // og vente paa laasen mens vi venter paa den.
        PostMessageW(ctx->hWnd, WM_APP_DATA, 0, 0);

        DWORD wr = WaitForMultipleObjects(2, waits, FALSE, wait);
        if (wr == WAIT_OBJECT_0) break;   // hStopEvent

        // Nullstillingen MA henge paa hWakeEvent alene. Sto den etter
        // hele ventekallet, traff den ogsaa WAIT_TIMEOUT - altsaa hver
        // eneste syklus - og netFailures kom aldri hoyere enn 1.
        // Backoffen sto da fast paa ~6 s og hConnect ble aldri sluppet.
        // Maalt, ikke antatt: logg med feil=1 i 20 sykluser paa rad.
        if (wr == WAIT_OBJECT_0 + 1) {
            // Panelet ble apnet. Brukeren skal faa et forsok med en gang,
            // ikke vente ut et minutt med backoff.
            EnterCriticalSection(&ctx->lock);
            ctx->netFailures = 0;
            LeaveCriticalSection(&ctx->lock);
        }
    }
    return 0;
}

// Tegner ikon og verktoytips ut fra en pris. Skilt fra hentingen slik at
// vi kan gjenbruke prisen vi allerede har, i stedet for a hente den paa nytt.
static void UpdateIcon(AppContext* ctx, double price, BOOL stale) {
    if (price <= 0.0) return;
    swprintf_s(ctx->fullPriceStr, 64, L"%s: $%.2f%s",
               SYMBOLS[ctx->symIdx].label, price, stale ? L" (frakoblet)" : L"");

    // Divisor og desimaler velges etter bredden paa den ferdig formaterte
    // strengen, ikke etter en terskel paa prisen. Se FormatIconPrice.
    char iconStr[16];
    FormatIconPrice(price, iconStr, sizeof(iconStr));

    // Dempede siffer naar tallet ikke lenger er ferskt. Gronn 0xFF00FF66
    // blandet ned mot bakgrunnen gir en synlig, men udramatisk forskjell.
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
// Graf-tegning (GDI, dobbeltbuffret)
// ---------------------------------------------------------------------------

// Lineaer fargeovergang. t=0 gir a, t=255 gir b. Alt tegnes ugjennomsiktig
// over CLR_BG, sa a blande mot bakgrunnen er identisk med ekte gjennomsikt.
static COLORREF Blend(COLORREF a, COLORREF b, int t) {
    if (t <= 0) return a;
    if (t >= 255) return b;
    int r = (GetRValue(a) * (255 - t) + GetRValue(b) * t) / 255;
    int g = (GetGValue(a) * (255 - t) + GetGValue(b) * t) / 255;
    int l = (GetBValue(a) * (255 - t) + GetBValue(b) * t) / 255;
    return RGB(r, g, l);
}

static RECT CloseButtonRect(int W) {
    RECT r = { W - 6 - CLOSE_SZ, 6, W - 6, 6 + CLOSE_SZ };
    return r;
}

static BOOL PtInRect2(const RECT* r, int x, int y) {
    return (x >= r->left && x < r->right && y >= r->top && y < r->bottom);
}

// Felles geometri for tegning og muse-treff.
static ChartRect ChartGeometry(int W, int H) {
    ChartRect g;
    g.left   = PAD_L;
    g.top    = HEADER_H;
    g.right  = W - PAD_R;
    g.bottom = H - PAD_B;
    g.cw     = g.right - g.left;
    g.ch     = g.bottom - g.top;
    return g;
}

// Hvilket lys peker musa paa? Returnerer absolutt indeks, -1 utenfor.
// Brukes bade av WM_MOUSEMOVE og av zoomen, slik at crosshairet folger
// utsnittet selv naar musa staar stille.
static int HitCandle(const AppContext* ctx, const ChartRect* g, int mx, int my) {
    int vs, vc;
    GetView(ctx, &vs, &vc);
    if (vc <= 0 || g->cw <= 0) return -1;
    if (mx < g->left || mx >= g->right || my < g->top || my > g->bottom) return -1;

    double slot = (double)g->cw / (double)vc;
    int rel = (int)((mx - g->left) / slot);
    if (rel < 0)   rel = 0;
    if (rel >= vc) rel = vc - 1;
    return vs + rel;
}

// Antall desimaler paa prisaksen velges fra AVSTANDEN mellom etikettene, ikke
// fra prisens storrelse. SOL rundt 97 dollar har et spenn paa under en dollar:
// med "%.0f" leste alle fem etikettene "97". BTC rundt 75 000 trenger ingen
// desimaler. Samme klasse feil som #5 i loggen - formatet maa folge tallet
// som faktisk skal vises, ikke en antatt storrelsesorden.
static int PriceDecimals(double step) {
    if (step <= 0.0) return 2;
    int d = 0;
    while (step < 2.0 && d < 6) { step *= 10.0; ++d; }
    return d;
}

// Min/maks over synlige lys, med 8% luft over og under.
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

// Faller lys ut i front, flyttes ALT som er en absolutt indeks like mye.
// Uten dette hopper grafen ett lys til venstre hvert minutt saa snart
// bufferet har naadd taket, og hoverIdx peker paa nabolyset.
// Idempotent: delta blir 0 andre gang. Kalles under laas.
static void ApplyEviction(AppContext* ctx) {
    long long delta = ctx->evictedTotal - ctx->dispEvictedSeen;
    if (delta <= 0) return;
    ctx->dispEvictedSeen = ctx->evictedTotal;

    // Ingen easing: en utkasting er ikke en bevegelse brukeren skal se.
    ctx->dispStart -= (double)delta;
    if (ctx->dispStart < 0.0) ctx->dispStart = 0.0;
    if (ctx->hoverIdx >= 0) {
        ctx->hoverIdx -= (int)delta;
        if (ctx->hoverIdx < 0) ctx->hoverIdx = -1;
    }
    ctx->panAnchorView -= (int)delta;
    if (ctx->panAnchorView < 0) ctx->panAnchorView = 0;
}

// Setter visningen lik maalet uten animasjon. Brukes naar en animasjon ikke
// gir mening: forste bilde, nytt buffer etter konfigbytte, panelet apnes.
// Kalles under laas.
static void SyncDisp(AppContext* ctx) {
    int vs, vc;
    GetView(ctx, &vs, &vc);
    ctx->dispStart = (double)vs;
    ctx->dispCount = (vc > 0) ? (double)vc : 1.0;
    if (ctx->candleCount > 0 && vc > 0) {
        PriceRange(ctx, vs, vc, &ctx->dispMin, &ctx->dispMax);
    } else {
        ctx->dispMin = 0.0;
        ctx->dispMax = 1.0;
    }
    ctx->dispEvictedSeen = ctx->evictedTotal;
    ctx->dispValid = TRUE;
}

// Unix-ms -> lokal tid. Paa lange lys er "HH:MM" ikke nok - hvert 1d-lys
// ville lest "00:00". Fra og med 1t tar vi med datoen.
static void FormatCandleTime(long long unixMs, long long intervalMs,
                             wchar_t* out, size_t cch) {
    ULONGLONG t = (ULONGLONG)(unixMs / 1000) * 10000000ULL + 116444736000000000ULL;
    FILETIME utc, local;
    utc.dwLowDateTime  = (DWORD)(t & 0xFFFFFFFFULL);
    utc.dwHighDateTime = (DWORD)(t >> 32);
    SYSTEMTIME st;
    if (FileTimeToLocalFileTime(&utc, &local) && FileTimeToSystemTime(&local, &st)) {
        if (intervalMs >= 86400000LL) {
            swprintf_s(out, cch, L"%02d.%02d.%04d", st.wDay, st.wMonth, st.wYear);
        } else if (intervalMs >= 3600000LL) {
            swprintf_s(out, cch, L"%02d.%02d %02d:%02d",
                       st.wDay, st.wMonth, st.wHour, st.wMinute);
        } else {
            swprintf_s(out, cch, L"%02d:%02d", st.wHour, st.wMinute);
        }
    } else {
        wcscpy_s(out, cch, L"--:--");
    }
}

// De blandede fargene avhenger av fade-nivaet. I stedet for a bygge dem
// paa nytt hvert bilde (60 ganger i sekundet under fading), gjenbrukes de
// saa lenge (chrome, closeHot) staar stille.
static void EnsureChromeCache(AppContext* ctx) {
    if (ctx->cacheValid &&
        ctx->cacheChrome == ctx->chrome &&
        ctx->cacheCloseHot == ctx->closeHot) {
        return;
    }

    if (ctx->penDim)  DeleteObject(ctx->penDim);
    if (ctx->penX)    DeleteObject(ctx->penX);
    if (ctx->brDim)   DeleteObject(ctx->brDim);
    if (ctx->brEdge)  DeleteObject(ctx->brEdge);
    if (ctx->brHdr)   DeleteObject(ctx->brHdr);
    if (ctx->brClose) DeleteObject(ctx->brClose);

    int a = ctx->chrome;
    ctx->penDim  = CreatePen(PS_SOLID, 1, Blend(CLR_BG, CLR_DIM, a));
    ctx->penX    = CreatePen(PS_SOLID, 1,
                             Blend(CLR_BG, ctx->closeHot ? CLR_WHITE : CLR_DIM, a));
    ctx->brDim   = CreateSolidBrush(Blend(CLR_BG, CLR_DIM, a));
    ctx->brEdge  = CreateSolidBrush(Blend(CLR_BG, CLR_BOXEDGE, a));
    ctx->brHdr   = CreateSolidBrush(Blend(CLR_BG, CLR_HDRHOT, a));
    ctx->brClose = CreateSolidBrush(Blend(CLR_BG, CLR_CLOSEHOT, a));

    ctx->cacheChrome   = a;
    ctx->cacheCloseHot = ctx->closeHot;
    ctx->cacheValid    = TRUE;
}

// Layout og treffdeteksjon deler en funksjon. To uavhengige utregninger av
// samme flate ender med a peke forskjellige steder - se feil #7 i loggen.
#define OVL_ROWS_MAX  16
#define OVL_ROW_H     22
#define OVL_COL_W     104
#define OVL_PAD       10
#define OVL_HDR_H     18

typedef struct {
    RECT box;                    // hele overlayet
    RECT rows[OVL_ROWS_MAX];     // en per valg
    int  count;                  // SYMBOL_COUNT forst, sa INTERVAL_COUNT
    RECT symHdr, ivHdr;          // overskriftene
} OverlayRects;

static void OverlayLayout(int W, int H, OverlayRects* r) {
    // Nulles helt ut. De ubrukte radene bak count er ellers stack-soppel, og
    // da er funksjonen ikke lenger ren - to kall med samme inndata gir ulikt
    // innhold. Enhetstesten fanget nettopp det. OverlayHit gaar bare til
    // count, saa soppelet var ufarlig i dag; dette lukker klassen.
    memset(r, 0, sizeof(*r));

    int rowsMax = (SYMBOL_COUNT > INTERVAL_COUNT) ? SYMBOL_COUNT : INTERVAL_COUNT;
    int boxW = OVL_PAD * 3 + OVL_COL_W * 2;
    int boxH = OVL_PAD * 2 + OVL_HDR_H + rowsMax * OVL_ROW_H;

    // Sentrert, men aldri utenfor panelet - panelet kan vaere mindre enn
    // boksen paa minimumsstorrelsen.
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

// Indeks 0..SYMBOL_COUNT-1 er symboler, resten intervaller. -1 = ingen.
static int OverlayHit(const OverlayRects* r, int x, int y) {
    for (int i = 0; i < r->count; ++i) {
        const RECT* q = &r->rows[i];
        if (x >= q->left && x < q->right && y >= q->top && y < q->bottom) return i;
    }
    return -1;
}

// Paletten er CLR_BG/CLR_BOX/CLR_BOXEDGE - identisk med hover-boksen, saa
// overlayet leses som samme element-familie.
// Kalles fra PaintPopup, IKKE fra DrawChart: DrawChart returnerer tidlig naar
// candleCount == 0, og det er nettopp tilstanden rett etter et konfigbytte.
// Laa kallet der, ville uttoningen aldri blitt tegnet etter et bytte.
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
    DrawTextW(hdc, L"INTERVALL", -1, &h2, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

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

    // Penslene lages per bilde med vilje: fargen avhenger av fade-nivaet, som
    // endrer seg hvert bilde MENS overlayet toner. I hvile returnerer
    // funksjonen paa a <= 0, saa GDI-antallet i ro er uendret.
    DeleteObject(brBox);
    DeleteObject(brEdge);
}

// Bygger bakgrunn + vannmerke naar (W, H, symIdx, ivIdx) endrer seg - ikke
// per bilde. Samme disiplin som GDI-cachen fra fase 1.
// Feiler noe her, settes wmValid = FALSE og DrawChart faller tilbake paa
// FillRect. Vannmerket er pynt; det skal aldri hindre opptegning.
static void EnsureWatermark(AppContext* ctx, HDC ref, int W, int H) {
    if (ctx->wmValid && ctx->wmW == W && ctx->wmH == H &&
        ctx->wmSym == ctx->symIdx && ctx->wmIv == ctx->ivIdx) {
        return;
    }
    if (W <= 0 || H <= 0) { ctx->wmValid = FALSE; return; }

    // Riv ned det gamle FORST. Uten dette lekker en HBITMAP og en HDC per
    // resize, og GDI-tallet klatrer for hver gang brukeren drar i kanten.
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
    SetTextColor(ctx->wmDC, CLR_WATERMARK);

    ChartRect g = ChartGeometry(W, H);

    // Fonthoyden folger chart-flatens hoyde, ikke en fast verdi: et lite
    // panel skal ikke faa vannmerket klippet, og et stort skal ikke faa en
    // liten tekst midt i flaten.
    //
    // DPI-skaleringen gjelder KLEMMEGRENSENE, ikke H/5. g.ch er allerede
    // enhetspiksler, saa den proporsjonale delen skalerer seg selv naar
    // vinduet blir storre paa en hoy-DPI skjerm. Grensene er derimot angitt
    // i logiske piksler, og et gulv paa 32 ville vaert 16 logiske piksler
    // paa 200 %. Ganger vi H/5 med DPI ogsaa, teller vi skaleringen to
    // ganger. Prosessen er DPI-uvitende i dag, saa GetDeviceCaps gir 96 og
    // MulDiv er en identitet - dette blir levende i det et manifest legges til.
    int dpi = GetDeviceCaps(ref, LOGPIXELSY);
    if (dpi <= 0) dpi = 96;
    int fMin = MulDiv(WM_FONT_MIN, dpi, 96);
    int fMax = MulDiv(WM_FONT_MAX, dpi, 96);
    int fh = g.ch / WM_FONT_DIV;
    if (fh < fMin) fh = fMin;
    if (fh > fMax) fh = fMax;

    // Bygges her, ikke per bilde: EnsureWatermark kjorer bare naar
    // (W, H, symIdx, ivIdx) faktisk endrer seg, og alle fire paavirker
    // hoyden eller bredden teksten trenger.
    const wchar_t* wmText = SYMBOLS[ctx->symIdx].api;
    int wmLen = (int)wcslen(wmText);

    if (ctx->hFontWm) DeleteObject(ctx->hFontWm);
    ctx->hFontWm = CreateFontW(-fh, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                               L"Segoe UI");

    // Hoydeformelen alene gir klippet tekst paa smale paneler: paa 280 px
    // traff fh gulvet paa 32, og "BTCUSDT" ble bredere enn chart-flaten.
    // Maalt, ikke antatt - samme disiplin som feil #5. Bredden maales paa
    // den faktiske strengen i den faktiske fonten, og hoyden skaleres ned
    // i samme forhold hvis den ikke faar plass.
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

    // Intervallet under hovedlinja, i den vanlige lille fonten.
    SelectObject(ctx->wmDC, ctx->hFontSmall);
    // Avstanden ned til intervallet folger fonthoyden, ellers ville teksten
    // ligge oppi hovedlinja paa store paneler.
    RECT rcIv = { g.left, g.top + (g.ch / 2) + fh / 2 + 4, g.right, g.bottom };
    DrawTextW(ctx->wmDC, INTERVALS[ctx->ivIdx].label, -1, &rcIv,
              DT_CENTER | DT_SINGLELINE | DT_TOP);
    SelectObject(ctx->wmDC, prev);

    ctx->wmW = W; ctx->wmH = H;
    ctx->wmSym = ctx->symIdx; ctx->wmIv = ctx->ivIdx;
    ctx->wmValid = TRUE;
}

static void DrawChart(AppContext* ctx, HDC hdc, int W, int H) {
    RECT rcAll = { 0, 0, W, H };
    // Vannmerket ligger I bakgrunnen, for rutenett, lys og akser - grafen
    // flyter rent over teksten. BitBlt ERSTATTER FillRect, den kommer ikke
    // i tillegg.
    EnsureWatermark(ctx, hdc, W, H);
    if (ctx->wmValid) {
        BitBlt(hdc, 0, 0, W, H, ctx->wmDC, 0, 0, SRCCOPY);
    } else {
        FillRect(hdc, &rcAll, ctx->brBg);   // fallback, vannmerket er pynt
    }

    SetBkMode(hdc, TRANSPARENT);

    // Kalles under laas, saa helsefeltene kan leses direkte.
    ULONGLONG nowTick = GetTickCount64();
    BOOL stale = (ctx->lastOkTick != 0) &&
                 (nowTick - ctx->lastOkTick > STALE_AFTER);
    int staleSecs = stale ? (int)((nowTick - ctx->lastOkTick) / 1000) : 0;

    int n = ctx->candleCount;
    if (n <= 0) {
        wchar_t msg[96];
        SelectObject(hdc, ctx->hFontSmall);
        SetTextColor(hdc, CLR_DIM);

        // Uten forbindelse sto det tidligere "Laster data fra Binance..." i
        // all evighet. Meldingen loy om tilstanden - naa sier den hva som
        // faktisk skjer, og naar vi prover igjen.
        if (ctx->netFailures > 0) {
            ULONGLONG nx = ctx->nextRetryTick;
            int in_s = (nx > nowTick) ? (int)((nx - nowTick + 999) / 1000) : 0;
            swprintf_s(msg, 96, L"Ingen forbindelse - prover igjen om %ds", in_s);
        } else {
            wcscpy_s(msg, 96, L"Laster data fra Binance...");
        }
        DrawTextW(hdc, msg, -1, &rcAll, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        return;
    }

    int vs, vc;
    GetView(ctx, &vs, &vc);
    if (vc <= 0) return;

    // --- Header: pris + endring over det SYNLIGE utsnittet ---
    double first = ctx->candles[vs].open;
    double last  = ctx->candles[vs + vc - 1].close;
    double chg   = (first > 0.0) ? ((last - first) / first) * 100.0 : 0.0;
    COLORREF chgClr = (chg >= 0.0) ? CLR_UP : CLR_DOWN;

    wchar_t span[24];
    FormatSpan(vc, ctx->intervalMs, span, 24);

    wchar_t buf[64];

    // Subtil opplysning av tittellinja nar musa er over den
    if (ctx->headerHot && ctx->chrome > 0) {
        RECT rcHdrBg = { 1, 1, W - 1, HEADER_H };
        EnsureChromeCache(ctx);
        FillRect(hdc, &rcHdrBg, ctx->brHdr);
    }

    // Plass til krysset til hoyre, grip-prikkene til venstre
    RECT rcHdr = { HDR_TEXT_L, 10, W - 12 - CLOSE_SZ, 30 };

    SelectObject(hdc, ctx->hFontBig);
    SetTextColor(hdc, stale ? CLR_DIM : CLR_TEXT);
    swprintf_s(buf, 64, L"$%.2f", last);
    DrawTextW(hdc, buf, -1, &rcHdr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(hdc, ctx->hFontSmall);
    SetTextColor(hdc, chgClr);
    swprintf_s(buf, 64, L"%+.2f%%  (%s)", chg, span);
    DrawTextW(hdc, buf, -1, &rcHdr, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

    SetTextColor(hdc, CLR_DIM);
    RECT rcSub = { HDR_TEXT_L, 28, W - 12 - CLOSE_SZ, 42 };
    if (stale) {
        swprintf_s(buf, 64, L"%s  -  %s  -  frakoblet %ds",
                   SYMBOLS[ctx->symIdx].label, INTERVALS[ctx->ivIdx].label, staleSecs);
    } else {
        swprintf_s(buf, 64, L"%s  -  %s",
                   SYMBOLS[ctx->symIdx].label, INTERVALS[ctx->ivIdx].label);
    }
    DrawTextW(hdc, buf, -1, &rcSub, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // --- Chart-geometri ---
    ChartRect g = ChartGeometry(W, H);
    int left = g.left, top = g.top, right = g.right, bottom = g.bottom;
    int cw = g.cw, ch = g.ch;
    if (cw <= 0 || ch <= 0) return;

    double minP, maxP;
    PriceRange(ctx, vs, vc, &minP, &maxP);
    double range = maxP - minP;

    // --- Rutenett + prisetiketter ---
    HPEN hOldPen = (HPEN)SelectObject(hdc, ctx->penGrid);
    SelectObject(hdc, ctx->hFontSmall);

    for (int i = 0; i <= 4; ++i) {
        int y = top + (ch * i) / 4;
        MoveToEx(hdc, left, y, NULL);
        LineTo(hdc, right, y);

        double p = maxP - (range * i) / 4.0;
        swprintf_s(buf, 64, L"%.*f", PriceDecimals(range / 4.0), p);
        RECT rcLbl = { right + 4, y - 8, W - 4, y + 8 };
        SetTextColor(hdc, CLR_DIM);
        DrawTextW(hdc, buf, -1, &rcLbl, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
    SelectObject(hdc, hOldPen);

    // --- Candlesticks ---
    double slot = (double)cw / (double)vc;
    int bodyW = (int)(slot * 0.62);
    if (bodyW < 1)  bodyW = 1;
    if (bodyW > 18) bodyW = 18;   // hindrer klumpete lys ved full innzoom



    for (int i = 0; i < vc; ++i) {
        Candle* c = &ctx->candles[vs + i];
        int up = (c->close >= c->open);

        int cx     = left + (int)(slot * i + slot / 2.0);
        int yHigh  = top + (int)(((maxP - c->high)  / range) * ch);
        int yLow   = top + (int)(((maxP - c->low)   / range) * ch);
        int yOpen  = top + (int)(((maxP - c->open)  / range) * ch);
        int yClose = top + (int)(((maxP - c->close) / range) * ch);

        SelectObject(hdc, up ? ctx->penUp : ctx->penDown);
        SelectObject(hdc, up ? ctx->brUp  : ctx->brDown);

        // Veke
        MoveToEx(hdc, cx, yHigh, NULL);
        LineTo(hdc, cx, yLow);

        // Kropp
        int yTop = (yOpen < yClose) ? yOpen : yClose;
        int yBot = (yOpen < yClose) ? yClose : yOpen;
        if (yBot - yTop < 1) yBot = yTop + 1; // doji -> minst 1px
        Rectangle(hdc, cx - bodyW / 2, yTop, cx - bodyW / 2 + bodyW, yBot);
    }

    SelectObject(hdc, GetStockObject(BLACK_PEN));
    SelectObject(hdc, GetStockObject(NULL_BRUSH));

    // --- Siste pris: stiplet linje + aksestempel ---
    // Staar HER med vilje: over lysene og rutenettet, under traadkorset og
    // overlayet. Og for den tidlige returen i crosshair-blokka nedenfor -
    // uten hover skal linja fortsatt tegnes.
    //
    // Fargen folger fortegnet til den momentane endringen, dP = P_t - P_t-1,
    // altsaa siste lukkekurs mot den forrige. Det er en annen regel enn
    // lysenes egen (close mot open i SAMME lys), og de kan derfor peke hver
    // sin vei: et gront lys som fortsatt ligger under forrige lukkekurs gir
    // en rod linje. Det er tilsiktet - linja svarer paa "hvor staar vi mot
    // forrige lukking", ikke "hvordan gaar dette lyset".
    {
        const Candle* lastC = &ctx->candles[n - 1];
        double lastP = lastC->close;
        double prevP = (n >= 2) ? ctx->candles[n - 2].close : lastC->open;
        BOOL   lastUp = (lastP >= prevP);

        int yLast = top + (int)(((maxP - lastP) / range) * ch);

        // Utenfor synlig prisomraade tegnes ingenting. Et stempel klemt mot
        // kanten ville plassert prisen et sted den ikke er.
        if (yLast >= top && yLast <= bottom) {
            int xLast = left + (int)(slot * (vc - 1) + slot / 2.0);
            if (xLast < left)  xLast = left;
            if (xLast > right) xLast = right;

            HPEN penLast = lastUp ? ctx->penLastUp : ctx->penLastDown;
            HPEN hOld2 = (HPEN)SelectObject(hdc, penLast);
            MoveToEx(hdc, xLast, yLast, NULL);
            LineTo(hdc, right, yLast);
            SelectObject(hdc, hOld2);

            // Aksestempelet overskriver rutenettetiketten paa denne hoyden,
            // slik at det ikke staar to tall oppi hverandre.
            RECT rcPill = { right + 1, yLast - 8, W - 1, yLast + 8 };
            FillRect(hdc, &rcPill, lastUp ? ctx->brUp : ctx->brDown);

            // "Presis verditekst": to desimaler der de faar plass, ellers
            // samme oppslosning som aksen. Bredden maales paa den ferdig
            // formaterte strengen - feil #5 igjen.
            SelectObject(hdc, ctx->hFontSmall);
            int pillDec = 2;
            swprintf_s(buf, 64, L"%.*f", pillDec, lastP);
            SIZE psz = { 0, 0 };
            int pillAvail = (W - 1) - (right + 4) - 2;
            if (GetTextExtentPoint32W(hdc, buf, (int)wcslen(buf), &psz) &&
                psz.cx > pillAvail) {
                pillDec = PriceDecimals(range / 4.0);
                swprintf_s(buf, 64, L"%.*f", pillDec, lastP);
            }

            // Mork tekst paa den mettede flaten - CLR_TEXT ville druknet.
            SetTextColor(hdc, CLR_BG);
            RECT rcPillTxt = { right + 4, yLast - 8, W - 2, yLast + 8 };
            DrawTextW(hdc, buf, -1, &rcPillTxt, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }
    }

    // --- Crosshair + hover-boks ---
    int rel = ctx->hoverIdx - vs;
    if (ctx->hoverIdx < 0 || rel < 0 || rel >= vc) return;

    const Candle* hc = &ctx->candles[ctx->hoverIdx];
    int hx = left + (int)(slot * rel + slot / 2.0);
    int hy = ctx->hoverY;
    if (hy < top) hy = top;
    if (hy > bottom) hy = bottom;

    HPEN hPrev = (HPEN)SelectObject(hdc, ctx->penCross);
    MoveToEx(hdc, hx, top, NULL);      LineTo(hdc, hx, bottom);
    MoveToEx(hdc, left, hy, NULL);     LineTo(hdc, right, hy);
    SelectObject(hdc, hPrev);

    // Prisetikett pa hoyreaksen der pekeren star
    double hp = maxP - ((double)(hy - top) / (double)ch) * range;
    swprintf_s(buf, 64, L"%.*f", PriceDecimals(range / 4.0), hp);
    RECT rcTag = { right + 1, hy - 8, W - 1, hy + 8 };
    FillRect(hdc, &rcTag, ctx->brBoxEdge);
    SelectObject(hdc, ctx->hFontSmall);
    SetTextColor(hdc, CLR_TEXT);
    RECT rcTagTxt = { right + 4, hy - 8, W - 2, hy + 8 };
    DrawTextW(hdc, buf, -1, &rcTagTxt, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // Hover-boks med tid + OHLC
    wchar_t tbuf[24];
    FormatCandleTime(hc->openTime, ctx->intervalMs, tbuf, 24);

    const int BOX_W = 104, BOX_H = 74, LINE_H = 13;
    int bx = hx + 12;
    if (bx + BOX_W > right) bx = hx - 12 - BOX_W;   // flipp til venstre ved kanten
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

    const wchar_t* lbl[4] = { L"O", L"H", L"L", L"C" };
    double val[4] = { hc->open, hc->high, hc->low, hc->close };
    COLORREF cclr = (hc->close >= hc->open) ? CLR_UP : CLR_DOWN;

    for (int i = 0; i < 4; ++i) {
        ty += LINE_H;
        RECT rcRow = { bx + 7, ty, bx + BOX_W - 6, ty + LINE_H };
        SetTextColor(hdc, CLR_DIM);
        DrawTextW(hdc, lbl[i], -1, &rcRow, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        swprintf_s(buf, 64, L"%.2f", val[i]);
        SetTextColor(hdc, (i == 3) ? cclr : CLR_TEXT);
        DrawTextW(hdc, buf, -1, &rcRow, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }
}

// Kryss, grip-prikker, resize-grip og ramme. Alt tones fra bakgrunnsfargen
// (usynlig) til full farge via ctx->chrome, sa vinduet er et rent, rammelost
// element nar musa ikke er der.
static void DrawChrome(AppContext* ctx, HDC hdc, int W, int H) {
    int a = ctx->chrome;
    EnsureChromeCache(ctx);

    // Ramme: dempet ved hover, aksentgronn mens vinduet flyttes/skaleres
    int borderA = ctx->inSizeMove ? 255 : a;
    if (borderA > 0) {
        RECT rcB = { 0, 0, W, H };
        // Drag-rammen er full styrke og sjelden, sa den bygges ved behov
        if (ctx->inSizeMove) {
            HBRUSH hBr = CreateSolidBrush(CLR_UP);
            FrameRect(hdc, &rcB, hBr);
            DeleteObject(hBr);
        } else {
            FrameRect(hdc, &rcB, ctx->brEdge);
        }
    }

    if (a <= 0) return;

    // Grip-prikker (2 x 3) ytterst til venstre i tittellinja
    for (int col = 0; col < 2; ++col) {
        for (int row = 0; row < 3; ++row) {
            RECT d = { 6 + col * 4, 15 + row * 4, 8 + col * 4, 17 + row * 4 };
            FillRect(hdc, &d, ctx->brDim);
        }
    }

    // Kryss
    RECT rcC = CloseButtonRect(W);
    if (ctx->closeHot) {
        FillRect(hdc, &rcC, ctx->brClose);
    }
    HPEN hOldX = (HPEN)SelectObject(hdc, ctx->penX);
    MoveToEx(hdc, rcC.left + 6, rcC.top + 6, NULL);
    LineTo(hdc, rcC.right - 6, rcC.bottom - 6);
    MoveToEx(hdc, rcC.right - 7, rcC.top + 6, NULL);
    LineTo(hdc, rcC.left + 5, rcC.bottom - 6);
    SelectObject(hdc, hOldX);

    // Resize-grip: tre diagonaler i nederste hoyre hjorne
    HPEN hOldG = (HPEN)SelectObject(hdc, ctx->penDim);
    for (int i = 0; i < 3; ++i) {
        int off = 4 + i * 4;
        MoveToEx(hdc, W - off, H - 4, NULL);
        LineTo(hdc, W - 3, H - off - 1);
    }
    SelectObject(hdc, hOldG);
}

static void PaintPopup(AppContext* ctx, HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdcDst = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    // Dobbeltbuffer: tegn alt i minnet, blit en gang -> ingen flimmer
    HDC hdcMem = CreateCompatibleDC(hdcDst);
    HBITMAP hbm = CreateCompatibleBitmap(hdcDst, W, H);
    HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbm);

    // Traden kan flette inn nye lys naar som helst; laasen holder
    // bufferet stabilt gjennom hele opptegningen (~1,8 ms).
    EnterCriticalSection(&ctx->lock);
    DrawChart(ctx, hdcMem, W, H);
    LeaveCriticalSection(&ctx->lock);

    // Overlayet tegnes UTENFOR laasen: alt det leser (overlayF, overlayHot,
    // symIdx, ivIdx) er UI-eid. Og det maa staa her, ikke i DrawChart, som
    // returnerer tidlig naar bufferet er tomt - nettopp tilstanden rett
    // etter et konfigbytte.
    DrawOverlay(ctx, hdcMem, W, H);

    DrawChrome(ctx, hdcMem, W, H);   // rorer ikke lysdata

    BitBlt(hdcDst, 0, 0, W, H, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbm);
    DeleteDC(hdcMem);

    EndPaint(hwnd, &ps);
}

// ---------------------------------------------------------------------------
// Popup-vindu
// ---------------------------------------------------------------------------

// Starter animasjonsklokka. Idempotent - SetTimer paa en id som allerede
// gaar, restarter den bare. lastAnimTick nullstilles kun naar klokka var
// stanset, ellers ville et nytt kall midt i en animasjon gitt dt = 0.
static void StartAnim(HWND hwnd) {
    if (!g_Ctx.animRunning) {
        g_Ctx.animRunning  = TRUE;
        g_Ctx.lastAnimTick = GetTickCount64();
        SetTimer(hwnd, TIMER_ANIM_ID, ANIM_INTERVAL, NULL);
    }
}

// Bytter symbol eller intervall. Teller opp configGen og tommer bufferet i
// SAMME kritiske seksjon, slik at et svar fra forrige konfig som ankommer
// akkurat naa blir forkastet i stedet for flettet inn.
static void ApplyConfigChoice(AppContext* ctx, HWND hwnd, int hit) {
    BOOL isSym = (hit < SYMBOL_COUNT);
    int  idx   = isSym ? hit : (hit - SYMBOL_COUNT);
    if (isSym  && (idx < 0 || idx >= SYMBOL_COUNT))   return;
    if (!isSym && (idx < 0 || idx >= INTERVAL_COUNT)) return;
    if (isSym  && idx == ctx->symIdx) return;   // ingen endring, ingen tomming
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
    LeaveCriticalSection(&ctx->lock);

    ctx->hoverIdx = -1;
    ctx->wmValid  = FALSE;   // vannmerket viser forrige symbol/intervall
    // SetEvent staar utenfor laasen. Den er ikke PostMessage, men samme regel
    // gjelder av samme grunn: ikke hold laasen over noe som vekker den andre
    // traden.
    SetEvent(ctx->hWakeEvent);
    SaveConfig(ctx, 0, 0);   // 0,0 = ikke ror panelstorrelsen
    InvalidateRect(hwnd, NULL, FALSE);
}

static LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1; // handteres i WM_PAINT

        case WM_PAINT:
            PaintPopup(&g_Ctx, hwnd);
            return 0;

        // Fjerner hele den ikke-klientiske rammen. WS_THICKFRAME gir oss
        // resize-maskineriet, dette gjor selve rammen usynlig.
        case WM_NCCALCSIZE:
            if (wParam) return 0;
            break;

        case WM_NCHITTEST: {
            RECT rw;
            GetWindowRect(hwnd, &rw);
            int x = GET_X_LPARAM(lParam) - rw.left;
            int y = GET_Y_LPARAM(lParam) - rw.top;
            int w = rw.right - rw.left, h = rw.bottom - rw.top;
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
            RECT rcC = CloseButtonRect(w);
            if (PtInRect2(&rcC, x, y)) return HTCLIENT;  // klikk, ikke drag

            if (y < HEADER_H) return HTCAPTION;  // dra vinduet etter headeren
            return HTCLIENT;
        }

        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            mmi->ptMinTrackSize.x = POPUP_MIN_W;
            mmi->ptMinTrackSize.y = POPUP_MIN_H;
            return 0;
        }

        // Drar eller endrer brukeren storrelse, blir vinduet staaende
        // til det lukkes bevisst - ellers ville det forsvinne i det man
        // klikket i et annet vindu.
        case WM_ENTERSIZEMOVE:
            g_Ctx.pinned     = TRUE;
            g_Ctx.inSizeMove = TRUE;
            return 0;

        case WM_EXITSIZEMOVE: {
            g_Ctx.inSizeMove = FALSE;
            // Storrelsen fanges HER, naar brukeren slipper, ikke ved
            // avslutning: panelet eies av hovedvinduet og er allerede revet
            // ned naar WM_DESTROY kommer dit, saa GetWindowRect har ingenting
            // a lese. Maalt - registret sto uten PanelWidth.
            RECT rp;
            if (GetWindowRect(hwnd, &rp)) {
                g_savedPanelW = rp.right - rp.left;
                g_savedPanelH = rp.bottom - rp.top;
            }
            return 0;
        }

        case WM_SIZE:
            g_Ctx.wmValid = FALSE;   // bitmapen er bygget for forrige storrelse
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

            RECT rcC = CloseButtonRect(rc.right);
            BOOL wasHot   = g_Ctx.windowHot;
            BOOL wasHdr   = g_Ctx.headerHot;
            BOOL wasClose = g_Ctx.closeHot;

            g_Ctx.windowHot = TRUE;
            g_Ctx.closeHot  = PtInRect2(&rcC, mx, my);
            g_Ctx.headerHot = (my < HEADER_H) && !g_Ctx.closeHot;

            if (!wasHot) StartAnim(hwnd);
            if (wasHdr != g_Ctx.headerHot || wasClose != g_Ctx.closeHot) {
                InvalidateRect(hwnd, NULL, FALSE);
            }

            // Sperren staar HER, ikke forst i blokka: over den ligger
            // TrackMouseEvent-armeringen og hot-tilstanden. Returnerte vi
            // for dem, sluttet WM_MOUSELEAVE a fyre og windowHot ville
            // hengt fast paa TRUE.
            // Sjekker overlayOpen, ikke overlayF: under uttoning er boksen
            // fortsatt synlig, men musa skal styre grafen igjen.
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
                EnterCriticalSection(&g_Ctx.lock);
                int vs2, vc2;
                GetView(&g_Ctx, &vs2, &vc2);
                if (vc2 > 0 && g.cw > 0) {
                    double slot = (double)g.cw / (double)vc2;
                    int shift = (int)((double)(mx - g_Ctx.panAnchorX) / slot);
                    g_Ctx.viewStart = g_Ctx.panAnchorView - shift;  // dra hoyre = bakover
                    ClampView(&g_Ctx);
                    g_Ctx.followLive =
                        (g_Ctx.viewStart + g_Ctx.viewCount >= g_Ctx.candleCount);
                    g_Ctx.hoverIdx = HitCandle(&g_Ctx, &g, mx, my);
                    g_Ctx.hoverY   = my;
                }
                LeaveCriticalSection(&g_Ctx.lock);
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

        // Ctrl + musehjul zoomer om punktet under pekeren. Merk at lParam
        // her er SKJERM-koordinater, i motsetning til WM_MOUSEMOVE.
        case WM_MOUSEWHEEL: {
            if (g_Ctx.overlayOpen) return 0;
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);   // lParam er SKJERM-koordinater her

            RECT rc;
            GetClientRect(hwnd, &rc);
            ChartRect g = ChartGeometry(rc.right, rc.bottom);
            if (g.cw <= 0) return 0;

            BOOL ctrl   = (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0;
            int notches = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;

            EnterCriticalSection(&g_Ctx.lock);
            int n = g_Ctx.candleCount;
            if (n > 0) {
                int vs, vc;
                GetView(&g_Ctx, &vs, &vc);

                if (!ctrl) {
                    // Uten Ctrl: panorer i tid. Hjul opp = bakover.
                    int step = vc / 8;
                    if (step < 1) step = 1;
                    g_Ctx.viewStart = vs - notches * step;
                    g_Ctx.viewCount = vc;
                } else {
                    // Med Ctrl: zoom om punktet under pekeren
                    double frac = (double)(pt.x - g.left) / (double)g.cw;
                    if (frac < 0.0) frac = 0.0;
                    if (frac > 1.0) frac = 1.0;

                    double anchor = (double)vs + frac * (double)vc;

                    double f = 1.0;
                    for (int k = 0; k < notches; ++k)  f *= ZOOM_STEP;
                    for (int k = 0; k > notches; --k)  f /= ZOOM_STEP;

                    int newCount = (int)((double)vc / f + 0.5);
                    if (newCount < MIN_VIEW) newCount = MIN_VIEW;
                    if (newCount > n)        newCount = n;

                    g_Ctx.viewStart = (int)(anchor - frac * (double)newCount + 0.5);
                    g_Ctx.viewCount = newCount;
                }

                ClampView(&g_Ctx);
                g_Ctx.followLive = (g_Ctx.viewStart + g_Ctx.viewCount >= g_Ctx.candleCount);
                g_Ctx.hoverIdx   = HitCandle(&g_Ctx, &g, pt.x, pt.y);
            }
            LeaveCriticalSection(&g_Ctx.lock);

            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_MOUSELEAVE:
            g_Ctx.trackingMouse = FALSE;
            g_Ctx.windowHot     = FALSE;
            g_Ctx.headerHot     = FALSE;
            g_Ctx.closeHot      = FALSE;
            g_Ctx.hoverIdx      = -1;
            g_Ctx.overlayHot    = -1;   // ellers blir en rad staaende framhevet
            StartAnim(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        // Animasjonsklokka. Driver alt tidsavhengig fra ett sted, og dor
        // naar alt har satt seg - i hvile gaar det ingen timer.
        case WM_TIMER:
            if (wParam == TIMER_ANIM_ID) {
                ULONGLONG now = GetTickCount64();
                double dt = (double)(now - g_Ctx.lastAnimTick);
                g_Ctx.lastAnimTick = now;

                BOOL redraw  = FALSE;
                BOOL settled = TRUE;

                // Chrome-fade
                double target = g_Ctx.windowHot ? 255.0 : 0.0;
                if (g_Ctx.chromeF != target) {
                    g_Ctx.chromeF = AnimStep(g_Ctx.chromeF, target, dt,
                                             ANIM_TAU_CHROME, 0.5);
                    int rounded = (int)(g_Ctx.chromeF + 0.5);
                    if (rounded != g_Ctx.chrome) { g_Ctx.chrome = rounded; redraw = TRUE; }
                    if (g_Ctx.chromeF != target) settled = FALSE;
                }

                // Overlay-fade. Samme kurve som chromet.
                double ovlTarget = g_Ctx.overlayOpen ? 255.0 : 0.0;
                if (g_Ctx.overlayF != ovlTarget) {
                    double before = g_Ctx.overlayF;
                    g_Ctx.overlayF = AnimStep(g_Ctx.overlayF, ovlTarget, dt,
                                              ANIM_TAU_CHROME, 0.5);
                    if ((int)(before + 0.5) != (int)(g_Ctx.overlayF + 0.5)) redraw = TRUE;
                    if (g_Ctx.overlayF != ovlTarget) settled = FALSE;
                }

                // Stale-telleren. Klokka maa ga mens vi er frakoblet, men
                // teksten endrer seg bare en gang i sekundet - vi tegner
                // derfor kun naar sifferet faktisk blir et annet.
                ULONGLONG okTick;
                EnterCriticalSection(&g_Ctx.lock);
                okTick = g_Ctx.lastOkTick;
                LeaveCriticalSection(&g_Ctx.lock);

                // IsWindowVisible er avgjorende: uten den holder en frakoblet
                // linje klokka i live paa et skjult panel, og vi tikker 60
                // ganger i sekundet uten a tegne noe. TogglePopup starter den
                // igjen naar panelet vises.
                if (okTick != 0 && now - okTick > STALE_AFTER &&
                    IsWindowVisible(hwnd)) {
                    int secs = (int)((now - okTick) / 1000);
                    if (secs != g_Ctx.staleSecsShown) {
                        g_Ctx.staleSecsShown = secs;
                        redraw = TRUE;
                    }
                    settled = FALSE;   // hold klokka i live mens vi er borte
                }

                if (redraw) InvalidateRect(hwnd, NULL, FALSE);
                if (settled) {
                    KillTimer(hwnd, TIMER_ANIM_ID);
                    g_Ctx.animRunning = FALSE;
                }
            }
            return 0;

        case WM_LBUTTONDOWN: {
            // Staar forst med vilje: mens overlayet er apent skal ingen del
            // av panelet ta klikket - heller ikke krysset. Forste klikk
            // lukker overlayet, neste lukker panelet.
            if (g_Ctx.overlayOpen) {
                RECT rcO;
                GetClientRect(hwnd, &rcO);
                OverlayRects orr;
                OverlayLayout(rcO.right, rcO.bottom, &orr);
                int hit = OverlayHit(&orr, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                if (hit >= 0) ApplyConfigChoice(&g_Ctx, hwnd, hit);
                g_Ctx.overlayOpen = FALSE;   // klikk utenfor lukker uten endring
                g_Ctx.overlayHot  = -1;
                StartAnim(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            RECT rc;
            GetClientRect(hwnd, &rc);
            RECT rcC = CloseButtonRect(rc.right);
            int dx = GET_X_LPARAM(lParam), dy = GET_Y_LPARAM(lParam);
            ChartRect gg = ChartGeometry(rc.right, rc.bottom);
            if (!PtInRect2(&rcC, dx, dy) &&
                dx >= gg.left && dx < gg.right && dy >= gg.top && dy <= gg.bottom) {
                // Start panorering. SetCapture sikrer at vi faar museslipp
                // ogsaa hvis pekeren forlater vinduet underveis.
                int vs, vc;
                EnterCriticalSection(&g_Ctx.lock);
                GetView(&g_Ctx, &vs, &vc);
                g_Ctx.viewCount     = vc;
                LeaveCriticalSection(&g_Ctx.lock);
                g_Ctx.panning       = TRUE;
                g_Ctx.panAnchorX    = dx;
                g_Ctx.panAnchorView = vs;
                SetCapture(hwnd);
                return 0;
            }
            if (PtInRect2(&rcC, dx, dy)) {
                g_Ctx.pinned    = FALSE;
                g_Ctx.hoverIdx  = -1;
                g_Ctx.windowHot = FALSE;
                g_Ctx.closeHot  = FALSE;
                g_Ctx.headerHot = FALSE;
                g_Ctx.chrome    = 0;
                g_Ctx.chromeF   = 0.0;
                g_Ctx.overlayOpen = FALSE;
                g_Ctx.overlayF    = 0.0;
                g_Ctx.overlayHot  = -1;
                g_Ctx.lastHideTick = GetTickCount64();
                ShowWindow(hwnd, SW_HIDE);
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
                g_Ctx.hoverIdx    = -1;   // crosshairet skal ikke sta igjen under
                StartAnim(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONUP:
            if (g_Ctx.panning) {
                g_Ctx.panning = FALSE;
                ReleaseCapture();
            }
            return 0;

        // HTCAPTION gir vanlig pil fra DefWindowProc - firevegskrysset ma
        // settes selv. Kantene (HTLEFT osv.) handterer DefWindowProc riktig.
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCAPTION) {
                SetCursor(LoadCursorW(NULL, IDC_SIZEALL));
                return TRUE;
            }
            break;

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                if (g_Ctx.pinned) return 0;   // festet: bli staaende
                // Slaar forgrunnsbyttet feil, kommer WA_INACTIVE med en
                // gang. Uten denne nadeperioden ville vinduet blinke opp
                // og forsvinne i stedet for a bli staende.
                if (GetTickCount64() - g_Ctx.shownTick < SHOW_GRACE_MS) return 0;
                g_Ctx.lastHideTick = GetTickCount64();
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            // Ved aktivering MA DefWindowProc fa kjore - det er den som
            // setter tastaturfokus. Returnerer vi 0 her, far vinduet aldri
            // fokus (hwndFocus = 0) og ESC naar aldri frem til WM_KEYDOWN.
            break;

        case WM_KEYDOWN:
            // ESC lukker overlayet FOR den lukker panelet.
            if (wParam == VK_ESCAPE && g_Ctx.overlayOpen) {
                g_Ctx.overlayOpen = FALSE;
                g_Ctx.overlayHot  = -1;
                StartAnim(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                g_Ctx.pinned = FALSE;
                g_Ctx.hoverIdx = -1;
                g_Ctx.lastHideTick = GetTickCount64();
                ShowWindow(hwnd, SW_HIDE);
            }
            return 0;

        case WM_CLOSE:
            g_Ctx.pinned = FALSE;
            g_Ctx.hoverIdx = -1;
            g_Ctx.lastHideTick = GetTickCount64();
            ShowWindow(hwnd, SW_HIDE);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Plasser popupen i hjornet av arbeidsomraadet naermest musepekeren.
// Handterer oppgavelinje i alle kanter + flere skjermer.
static void PositionPopup(HWND hPopup) {
    POINT pt;
    GetCursorPos(&pt);

    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(MONITORINFO) };
    if (!GetMonitorInfoW(hMon, &mi)) return;

    RECT wa = mi.rcWork;

    // Bruk vinduets NAVAERENDE storrelse - har brukeren endret den,
    // skal den beholdes neste gang grafen apnes.
    RECT rw;
    GetWindowRect(hPopup, &rw);
    int w = rw.right - rw.left;
    int h = rw.bottom - rw.top;
    if (w <= 0) w = POPUP_W;
    if (h <= 0) h = POPUP_H;

    // Vannrett: sentrer paa pekeren, men hold innenfor arbeidsomraadet
    int x = pt.x - w / 2;
    if (x < wa.left) x = wa.left;
    if (x + w > wa.right) x = wa.right - w;

    // Loddrett: under pekeren hvis det er plass (oppgavelinje oppe),
    // ellers over (oppgavelinje nede - det vanlige tilfellet)
    int y;
    if (pt.y - wa.top < h && wa.bottom - pt.y > h) {
        y = wa.top;
    } else {
        y = wa.bottom - h;
    }
    if (y < wa.top) y = wa.top;

    SetWindowPos(hPopup, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

// Windows nekter SetForegroundWindow fra en prosess som ikke eier
// forgrunnen. Uten dette blir popupen vist, men aldri aktivert - og far
// WA_INACTIVE med en gang, slik at den skjuler seg selv umiddelbart.
// Losningen er a koble input-koen var til forgrunnstraden mens vi bytter.
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

static void TogglePopup(AppContext* ctx, HINSTANCE hInst) {
    if (ctx->hPopup && IsWindowVisible(ctx->hPopup)) {
        ctx->pinned    = FALSE;
        ctx->hoverIdx  = -1;
        ctx->windowHot = FALSE;
        ctx->chrome    = 0;
        ctx->chromeF   = 0.0;
        ctx->overlayOpen = FALSE;
        ctx->overlayF    = 0.0;
        ctx->overlayHot  = -1;
        ctx->lastHideTick = GetTickCount64();
        ShowWindow(ctx->hPopup, SW_HIDE);
        return;
    }

    // Klikk paa tray-ikonet deaktiverer forst popupen (som skjuler den via
    // WM_ACTIVATE), deretter kommer WM_LBUTTONUP hit. Uten denne sperren
    // ville vinduet aapne seg igjen med en gang.
    if (GetTickCount64() - ctx->lastHideTick < REOPEN_GUARD_MS) return;

    if (!ctx->hPopup) {
        HWND hp = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            L"BTCPopupClass", L"BTC Chart",
            WS_POPUP | WS_THICKFRAME,   // THICKFRAME = resizable; rammen skjules i WM_NCCALCSIZE
            0, 0,
            // Lagret storrelse fra registret. 0 betyr "ikke lagret" og gir
            // standardmaalene. WM_SIZE fyrer som del av opprettelsen, saa
            // vannmerke-cachen bygges for riktig flate uten et eget steg.
            (g_savedPanelW > 0) ? g_savedPanelW : POPUP_W,
            (g_savedPanelH > 0) ? g_savedPanelH : POPUP_H,
            ctx->hWnd, NULL, hInst, NULL);
        if (!hp) return;

        EnterCriticalSection(&ctx->lock);   // traden leser hPopup
        ctx->hPopup = hp;
        LeaveCriticalSection(&ctx->lock);
    }

    EnterCriticalSection(&ctx->lock);
    ctx->viewCount  = 0;   // utsnittet settes pa nytt; bufferet beholdes
    ctx->followLive = TRUE;
    if (ctx->candleCount > 0) {
        int vc = (ctx->candleCount < DEFAULT_VIEW) ? ctx->candleCount : DEFAULT_VIEW;
        ctx->viewCount = vc;
        ctx->viewStart = ctx->candleCount - vc;
    }
    LeaveCriticalSection(&ctx->lock);

    ctx->panning   = FALSE;
    ctx->hoverIdx  = -1;
    ctx->windowHot = FALSE;
    ctx->headerHot = FALSE;
    ctx->closeHot  = FALSE;
    ctx->chrome    = 0;   // apner alltid rent, uten kontroller
    ctx->chromeF   = 0.0;
    ctx->overlayOpen = FALSE;   // overlayet skal aldri sta apent ved apning
    ctx->overlayF    = 0.0;
    ctx->overlayHot  = -1;
    PositionPopup(ctx->hPopup);
    ctx->shownTick = GetTickCount64();
    ShowWindow(ctx->hPopup, SW_SHOWNA);
    ForceForeground(ctx->hPopup);
    SetEvent(ctx->hWakeEvent);   // hent lys na, ikke om opptil 3 sekunder // kreves for at WA_INACTIVE skal utloses senere

    // Er linja nede idet panelet apnes, maa klokka starte her. WM_TIMER
    // lar den do mens panelet er skjult, og neste WM_APP_DATA kan vaere
    // opptil en hel backoff-periode unna - sekundtelleren ville statt
    // stille helt til da.
    ULONGLONG okTick;
    EnterCriticalSection(&ctx->lock);
    okTick = ctx->lastOkTick;
    LeaveCriticalSection(&ctx->lock);
    if (okTick != 0 && GetTickCount64() - okTick > STALE_AFTER) {
        StartAnim(ctx->hPopup);
    }

    InvalidateRect(ctx->hPopup, NULL, FALSE);
}

// ---------------------------------------------------------------------------

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TRAYICON:
            if (lParam == WM_LBUTTONUP) {
                TogglePopup(&g_Ctx, (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
            } else if (lParam == WM_RBUTTONUP) {
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Avslutt Ticker");

                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_TRAY_EXIT) {
                Shell_NotifyIconW(NIM_DELETE, &g_Ctx.nid);
                PostQuitMessage(0);
            }
            break;

        // Arbeidertraden har lagt inn nye data. Alt vi gjor her er a lese
        // prisen under laas og tegne - ingen nettverkstrafikk.
        case WM_APP_DATA: {
            double price;
            ULONGLONG okTick;
            EnterCriticalSection(&g_Ctx.lock);
            price  = g_Ctx.lastPrice;
            okTick = g_Ctx.lastOkTick;
            LeaveCriticalSection(&g_Ctx.lock);

            // okTick == 0 betyr at vi aldri har lykkes enda. Da er vi ikke
            // "frakoblet" - vi har bare ikke kommet i gang.
            BOOL stale = (okTick != 0) &&
                         (GetTickCount64() - okTick > STALE_AFTER);

            UpdateIcon(&g_Ctx, price, stale);
            if (g_Ctx.hPopup && IsWindowVisible(g_Ctx.hPopup)) {
                // Klokka maa ga mens vi er frakoblet, ellers fryser
                // sekundtelleren i undertittelen.
                if (stale) StartAnim(g_Ctx.hPopup);
                InvalidateRect(g_Ctx.hPopup, NULL, FALSE);
            }
            return 0;
        }

        case WM_DESTROY:
            SaveConfig(&g_Ctx, g_savedPanelW, g_savedPanelH);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Global\\BTCTicker_PhD_Instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    memset(&g_Ctx, 0, sizeof(AppContext));
    g_Ctx.hoverIdx = -1;   // 0 fra memset ville betydd "hover pa forste lys"
    g_Ctx.overlayHot = -1; // samme grunn: 0 ville betydd "forste rad framhevet"
    g_Ctx.dispValid  = FALSE; // snap paa forste bilde

    g_Ctx.hSession = WinHttpOpen(L"BTCTicker Engine/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

    if (!g_Ctx.hSession) return 1;

    // Uten dette er standard mottakstimeout 30 sekunder. Da ville en hengende
    // forbindelse holdt arbeidertraden fast lenger enn de 3 sekundene vi
    // venter ved avslutning - og vi ville lukket sesjonen under den.
    WinHttpSetTimeouts(g_Ctx.hSession, 5000, 5000, 5000, 5000);

    g_Ctx.hFontBig = CreateFontW(-19, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_Ctx.hFontSmall = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    // hFontWm lages ikke her: hoyden avhenger av panelstorrelsen, saa den
    // bygges i EnsureWatermark og bare naar hoyden endrer seg.

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
    pwc.hbrBackground = NULL; // vi tegner alt selv
    RegisterClassW(&pwc);

    g_Ctx.hWnd = CreateWindowExW(0, wc.lpszClassName, L"BTC Core Engine", 0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    g_Ctx.nid.cbSize           = sizeof(NOTIFYICONDATAW);
    g_Ctx.nid.hWnd             = g_Ctx.hWnd;
    g_Ctx.nid.uID              = 42;
    g_Ctx.nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_Ctx.nid.uCallbackMessage = WM_TRAYICON;
    g_Ctx.nid.hIcon            = RenderMicroFontIcon("...", 0xFF00FF66);
    wcscpy_s(g_Ctx.nid.szTip, 128, L"Kobler til Binance...");

    Shell_NotifyIconW(NIM_ADD, &g_Ctx.nid);

    // Faste GDI-objekter: lages en gang, ikke 16 ganger per opptegning
    g_Ctx.penGrid   = CreatePen(PS_SOLID, 1, CLR_GRID);
    g_Ctx.penUp     = CreatePen(PS_SOLID, 1, CLR_UP);
    g_Ctx.penDown   = CreatePen(PS_SOLID, 1, CLR_DOWN);
    g_Ctx.penCross  = CreatePen(PS_DOT,   1, CLR_CROSS);
    // Stiplet, ikke prikket: holder siste-pris-linja visuelt atskilt fra
    // baade rutenettet (heltrukket, dempet) og traadkorset (prikket).
    g_Ctx.penLastUp   = CreatePen(PS_DASH, 1, CLR_UP);
    g_Ctx.penLastDown = CreatePen(PS_DASH, 1, CLR_DOWN);
    g_Ctx.brBg      = CreateSolidBrush(CLR_BG);
    g_Ctx.brUp      = CreateSolidBrush(CLR_UP);
    g_Ctx.brDown    = CreateSolidBrush(CLR_DOWN);
    g_Ctx.brBox     = CreateSolidBrush(CLR_BOX);
    g_Ctx.brBoxEdge = CreateSolidBrush(CLR_BOXEDGE);

    // Arbeidertraden startes forst naar vinduet og ikonet finnes, siden
    // den poster meldinger til hWnd med en gang.
    // MA staa for CreateThread: forste henting skal gaa mot riktig par, og
    // vannmerket skal vaere korrekt fra forste bilde.
    LoadConfig(&g_Ctx, &g_savedPanelW, &g_savedPanelH);

    InitializeCriticalSection(&g_Ctx.lock);
    g_Ctx.hStopEvent = CreateEventW(NULL, TRUE,  FALSE, NULL);  // manuell reset
    g_Ctx.hWakeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);  // auto reset
    g_Ctx.hThread    = CreateThread(NULL, 0, NetworkThread, &g_Ctx, 0, NULL);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Stopp traden for vi river ned noe den kan rore
    if (g_Ctx.hStopEvent) SetEvent(g_Ctx.hStopEvent);
    if (g_Ctx.hThread) {
        WaitForSingleObject(g_Ctx.hThread, 3000);
        CloseHandle(g_Ctx.hThread);
    }
    if (g_Ctx.hStopEvent) CloseHandle(g_Ctx.hStopEvent);
    if (g_Ctx.hWakeEvent) CloseHandle(g_Ctx.hWakeEvent);
    DeleteCriticalSection(&g_Ctx.lock);

    if (g_Ctx.nid.hIcon) DestroyIcon(g_Ctx.nid.hIcon);
    if (g_Ctx.hFontBig) DeleteObject(g_Ctx.hFontBig);
    if (g_Ctx.hFontSmall) DeleteObject(g_Ctx.hFontSmall);

    DeleteObject(g_Ctx.penGrid);  DeleteObject(g_Ctx.penUp);
    DeleteObject(g_Ctx.penDown);  DeleteObject(g_Ctx.penCross);
    DeleteObject(g_Ctx.brBg);     DeleteObject(g_Ctx.brUp);
    DeleteObject(g_Ctx.brDown);   DeleteObject(g_Ctx.brBox);
    DeleteObject(g_Ctx.brBoxEdge);
    DeleteObject(g_Ctx.penLastUp);   DeleteObject(g_Ctx.penLastDown);
    if (g_Ctx.penDim)   DeleteObject(g_Ctx.penDim);
    if (g_Ctx.penX)     DeleteObject(g_Ctx.penX);
    if (g_Ctx.brDim)    DeleteObject(g_Ctx.brDim);
    if (g_Ctx.brEdge)   DeleteObject(g_Ctx.brEdge);
    if (g_Ctx.brHdr)    DeleteObject(g_Ctx.brHdr);
    if (g_Ctx.brClose)  DeleteObject(g_Ctx.brClose);

    // Vannmerke-cachen. Rekkefolgen er viktig: bitmapen maa velges ut av
    // DC-en for begge slettes.
    if (g_Ctx.wmDC) {
        if (g_Ctx.wmOldBmp) SelectObject(g_Ctx.wmDC, g_Ctx.wmOldBmp);
        DeleteDC(g_Ctx.wmDC);
    }
    if (g_Ctx.wmBmp)   DeleteObject(g_Ctx.wmBmp);
    if (g_Ctx.hFontWm) DeleteObject(g_Ctx.hFontWm);

    if (g_Ctx.hConnect) WinHttpCloseHandle(g_Ctx.hConnect);
    if (g_Ctx.hSession) WinHttpCloseHandle(g_Ctx.hSession);
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);

    return (int)msg.wParam;
}
