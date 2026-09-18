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
#define WM_APP_DATA      (WM_APP + 1)   // arbeidertraden har nye data
#ifdef TICKER_PROBE
#define WM_APP_PROBE     (WM_APP + 2)   // bare testbygg: les indre tilstand (fase 18)
#endif
#define ID_TRAY_EXIT     1001
#define ID_TRAY_RESET    1002
#define ID_TRAY_DESKTOP  1003   // skrivebordsmodus av/paa (fase 12)
#define IDM_TOGGLE_AUTOSTART 1004   // start ved paalogging av/paa (fase 13)
// Symbol og intervall fra tray-menyen (fase 17). Punkt i faar FIRST + i.
// Omraadene er 100 brede; #error under tabellene sikrer at de aldri overlapper.
#define ID_TRAY_SYMBOL_FIRST   1100
#define ID_TRAY_INTERVAL_FIRST 1200
#define ID_TRAY_RANGE_W        100
#define TIMER_INTERVAL   3000 // 3 sekunder

// --- Popup / graf ---
#define POPUP_W          1280
#define POPUP_H          720
// Minste storrelse ved manuell skalering, i logiske piksler (96 dpi).
// 400x250 er der headeren fortsatt har plass til pris, prosent og knapperad
// paa en rad, og chart-flaten til hover-boksen (104x74).
#define POPUP_MIN_W      400
#define POPUP_MIN_H      250
#define RESIZE_BORDER    6     // bredde pa sonen som starter storrelsesendring
// 6000 lys a 40 byte = 240 KB. Fire dager paa 1m, seksten aar paa 1d. Fylles
// bakover paa forespoersel (fase 18) og framover mens panelet staar aapent.
// Fullt buffer stopper bakfyllingen (histDone) - levende lys kastes aldri
// for aa gi plass til gamle. Var 1440 (ett dogn paa 1m) til og med fase 17.
#define MAX_CANDLES      6000
#define SEED_COUNT       300   // forste henting: 5 timer i ett jafs
#define DEFAULT_VIEW     300   // synlig utsnitt ved apning
#define MIN_VIEW         8     // minste antall synlige lys ved full zoom
#define ZOOM_STEP        1.2   // per musehjul-hakk
#define TIMER_ANIM_ID    2
#define TIMER_EMBED_ID   3      // skrivebordsmodus: prov WorkerW igjen
// 250 ms: ved omstart av Explorer er ny Progman paa plass etter 290-480 ms
// (maalt). Med 1000 ms sto skrivebordet uten graf i 1,1 s. Timeren gaar bare
// mens flaten mangler.
#define EMBED_RETRY_MS   250
#define ANIM_INTERVAL    16     // ~60 fps
// Tidsbasert interpolasjon, ikke fast steg per tikk: SetTimer(16) fyrer i
// praksis hver ~15,6 ms og slaas sammen under last. Fast steglengde ville
// gitt ulik hastighet avhengig av systembelastning.
// Eksponentiell kurve har en hale: tau=55 gir ~90 % pa 130 ms (der oyet
// ser faden som ferdig) og full innsetting paa ~340 ms. Halen koster kun
// timer-tikk, ikke opptegninger - vi tegner bare naar det avrundede
// niva faktisk endrer seg.
#define ANIM_TAU_FADE    55.0   // tidskonstant overlay-fade (ms)
#define ANIM_DT_MAX      100.0  // klemmer dt, saa en lang pause gir ett hopp

// View-easing. Litt lengre tau enn overlay-faden - en panorering er en storre
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

// Layout innenfor popup-vinduet
#define PAD_L            10
// Hoyre marg = prisaksens kolonne (AXIS_Y_W) + kantsikring (AXIS_PAD_R).
// Aksetekst begynner paa rcChart.right + AXIS_LBL_GAP og slutter senest paa
// W - AXIS_PAD_R. Kolonnen har plass til AXIS_Y_CHARS tegn i aksefonten, som
// er monospace: 8 x 9 px = 72, akkurat "75812.34" (maalt, Lucida Console
// em 15). Bredden er FAST, ikke maalt paa etikettene: PriceDecimals kan skifte
// midt i Y-easingen, og en marg som fulgte teksten ville latt hele grafen
// rykke sideveis mens prisaksen glir.
//
// AXIS_PAD_R holder seg ogsaa over RESIZE_BORDER, saa ingen siffer staar i
// sonen der pekeren blir en storrelsespil.
#define AXIS_Y_CHARS     8
#define AXIS_CHAR_W      9
#define AXIS_LBL_GAP     4
#define AXIS_Y_W         (AXIS_LBL_GAP + AXIS_Y_CHARS * AXIS_CHAR_W)
#define AXIS_PAD_R       8
#define PAD_R            (AXIS_Y_W + AXIS_PAD_R)
// Luft mellom siste lys og aksekanten (fase 15). Lysene slutter paa
// rcChart.right; rutenettet, den stiplede siste-pris-linja og traadkorset
// gaar helt inn til rcChart.edge, der stempelet og etikettene begynner. Uten
// dette kunne kroppen eller veken til siste lys staa klint mot stempelflaten.
// Gjelder begge modi fra fase 16, da skrivebordet ogsaa fikk stempel.
#define PLOT_PAD_R       10
#if PLOT_PAD_R < 8 || PLOT_PAD_R > 12
#error PLOT_PAD_R skal ligge i [8, 12] px
#endif
// Stempelet paa skrivebordet (fase 16). Hoyden folger flatens hoyde, som
// vannmerket: en 16 px pille er uleselig paa 1600 px, og en fast stor pille
// ville sprengt en liten flate. Gulv og tak er i enhetspiksler - flaten er
// per-monitor-bevisst, saa H er allerede fysiske piksler, og den
// proporsjonale delen skalerer seg selv.
#define DESK_PILL_DIV    40
#define DESK_PILL_MIN    16
#define DESK_PILL_MAX    48
#if AXIS_PAD_R < 6 || AXIS_PAD_R > 10
#error AXIS_PAD_R skal ligge i [6, 10] px
#endif
#define HEADER_H         44
// rcChart.top = HEADER_H. Grafen skal aldri kunne krype opp i headerteksten
// eller kontrollknappene (de slutter paa BTN_TOP + BTN_H = 24), saa et gulv
// sjekkes ved kompilering i stedet for aa klemmes ved kjoring.
#define CHART_TOP_MIN    32
#if HEADER_H < CHART_TOP_MIN
#error HEADER_H maa gi rcChart minst CHART_TOP_MIN px klaring fra toppen
#endif

// Kontrollknapper i headeren. Ultrakompakt: 26x18 er nok til en 9 px glyf
// med luft rundt, og lar headerens 44 px fortsatt baere to tekstlinjer.
#define BTN_W            26
#define BTN_H            18
#define BTN_GAP          2
#define BTN_TOP          6
#define BTN_MARGIN_R     8
#define SPAWN_OFFSET     30    // [ + ]: ny instans forskyves saa mye ned og til hoyre
// Knapperadens venstre kant leses fra ButtonStrip, ikke fra en egen
// breddekonstant - DrawChart maaler headeren mot den.
//
// Minste luft mellom to headertekster paa samme rad, og mellom tekst og
// knapperaden.
#define HDR_GAP          8
// Bunnmargen er tidsaksens baand, ikke luft. Aksefonten er 15 px hoy
// (tmHeight), saa 18 px gir tekst fra bottom + 2 til bottom + 17 = H - 1 uten
// aa beroere raden y = bottom, der den laveste veken og nederste
// rutenettlinje staar (klippet er inklusivt der, se DrawChart).
#define PAD_B            18
// Minste avstand mellom to tidsetiketter. Brukes som
// max(TIME_DX_MIN, etikettbredde + TIME_LBL_GAP): "DD.MM HH:MM" er 99 px i
// aksefonten, bredere enn 80, og ville ellers kollidert paa 1t og 4t.
#define TIME_DX_MIN      80
#define TIME_LBL_GAP     12

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
#define CLR_CLOSEHOT     RGB(0xC0, 0x2A, 0x3E)   // rod bakgrunn paa krysset
#define CLR_BTNHOT       RGB(0xFF, 0xFF, 0xFF)   // glyf paa rod bakgrunn
// Aksetekst: 8,05:1 mot CLR_BG (WCAG AA krever 4,5:1 for liten tekst).
// CLR_DIM, som aksene brukte foer, gir 4,12:1. Bare aksene - CLR_DIM styrer
// ogsaa knapper, header og overlay, og de er ikke en del av denne endringen.
#define CLR_AXIS         RGB(0xA0, 0xAA, 0xB8)
// Vannmerket blandes mot hvitt med alfa fra WatermarkAlpha(W). Hvitt fordi
// den gamle faste fargen #15191F var noytral: CLR_BG + 8 i alle kanaler,
// altsaa ~3,3 % mot hvitt.
#define CLR_WM_INK       RGB(0xFF, 0xFF, 0xFF)
#define WM_ALPHA_BASE    0.08
#define WM_ALPHA_MIN     0.04
#define WM_ALPHA_MAX     0.10
#define WM_W_NOMINAL     1920.0
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
// Tray-menyens ID-omraader (fase 17). sizeof kan ikke staa i #if, saa vakten
// er C_ASSERT: vokser en tabell forbi omraadet, stopper bygget her.
C_ASSERT(SYMBOL_COUNT   <= ID_TRAY_RANGE_W);
C_ASSERT(INTERVAL_COUNT <= ID_TRAY_RANGE_W);
C_ASSERT(ID_TRAY_SYMBOL_FIRST + ID_TRAY_RANGE_W <= ID_TRAY_INTERVAL_FIRST);

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
// right/cw er LYSENES flate. edge er aksekanten: stempelet begynner paa
// edge + 1 og etikettene paa edge + AXIS_LBL_GAP. Begge modi har luft mellom
// dem (PLOT_PAD_R); i skrivebordsmodus er margen utenfor edge smalere, fordi
// den bare rommer stempelet og ingen akseetiketter.
typedef struct { int left, top, right, bottom, cw, ch, edge; } ChartRect;

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
    // Pris- og tidsaksen. Monospace, saa etikettene staar stille naar
    // sifrene skifter, og AXIS_Y_W kan regnes i tegn. Graaskala-kantutjevning
    // (ANTIALIASED_QUALITY), ikke ClearType: ingen fargefransing paa tall.
    HFONT hFontAxis;

    int hoverIdx;        // indeks til lyset under pekeren, -1 = ingen
    int hoverY;          // muse-Y i klientkoordinater
    BOOL trackingMouse;  // om WM_MOUSELEAVE er bestilt

    // --- Animasjonsklokke ---
    // En timer driver alt tidsavhengig: overlay-fade, stale-telleren,
    // view- og Y-akse-easing. Den lever bare mens noe faktisk
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
    long long dispShiftSeen;       // frontShift UI har kompensert for

    // --- Arbeidertrad ---
    // Laasen dekker candles[], candleCount, viewStart, viewCount,
    // followLive, lastPrice, hPopup, frontShift, histPending og histDone.
    // Alt annet rores kun av UI-traden.
    CRITICAL_SECTION lock;
    HANDLE hThread;
    HANDLE hStopEvent;   // manuell reset: signaliserer avslutning
    HANDLE hWakeEvent;   // auto reset: hent NA (panelet ble apnet)

    // Nettverkshelse. Laasebeskyttet - arbeidertraden skriver, UI leser.
    ULONGLONG lastOkTick;    // GetTickCount64 ved siste vellykkede henting
    ULONGLONG nextRetryTick; // naar neste forsok er planlagt
    int       netFailures;   // sammenhengende feil, driver backoffen
    // Netto endring FORAN i bufferet, med fortegn: +1 per lys som faller ut
    // (utkasting), -k per k lys lagt foran (bakfylling, fase 18). UI-traden
    // flytter visning, hover og pan-anker like mye (ApplyFrontShift). Var
    // evictedTotal, monoton, til og med fase 17.
    long long frontShift;
    // Bakfylling (fase 18). histPending: UI vil ha eldre lys, traden har ikke
    // hentet enda. histDone: serveren svarte 2xx uten lys, eller bufferet er
    // fullt - ikke spor igjen for denne konfigen.
    BOOL      histPending;
    BOOL      histDone;

    // --- Bufrede GDI-objekter ---
    // Faste farger lages en gang ved oppstart i stedet for 16 ganger
    // per opptegning.
    HPEN   penGrid, penCross;
    HPEN   penBtn, penBtnHot, penBtnWhite;
    // Standardpekere. LoadCursorW returnerer et DELT handtak for disse - de
    // telles ikke som vaare, og skal ikke gjennom DestroyCursor. Bufres
    // likevel: WM_SETCURSOR fyrer ved hver musebevegelse, og et oppslag per
    // melding er unodig arbeid i en sti som ellers er gratis.
    HCURSOR curPan, curArrow;
    HBRUSH brClose;
    // Hvilken knapp musa staar paa, -1 for ingen. UI-eid, aldri roert av
    // arbeidertraden. Treffdeteksjonen henger paa DENNE, ikke paa noe
    // fade-niva - knappene har ingen fade, de skifter farge momentant.
    int    btnHot;
    HPEN   penLastUp, penLastDown;   // stiplet siste-pris-linje
    HBRUSH brBg, brBox, brBoxEdge;


    // --- Vedvarende dobbeltbuffer ---
    // Lever mellom bildene og bygges paa nytt bare naar storrelsen endres.
    // Et nytt buffer per bilde kostet 3840x1600: 7,9 ms paa foerste skriving
    // i den nye bitmapen og 1,7 ms paa frigjoeringen, av 13,2 ms totalt
    // (maalt). bbValid: bufferet inneholder et fullt bilde i denne
    // storrelsen, saa hurtigstien kan tegne knappene rett inn i det.
    HBITMAP bbBmp;
    HDC     bbDC;
    HBITMAP bbOldBmp;
    int     bbW, bbH;
    BOOL    bbValid;

    // --- Vannmerke-cache ---
    // Bakgrunn + vannmerke bakt sammen i en bitmap. Denne ERSTATTER dagens
    // FillRect - den legger ikke til et steg. En DrawTextW med stor font
    // koster 0,05-0,30 ms og hoerer ikke hjemme per bilde. Proevd paa nytt
    // sammen med det vedvarende bufferet: tegnet per bilde kostet vannmerket
    // 0,50-0,53 ms ved 1280x720, mot ~0,28 ms for bliten (maalt).
    HBITMAP wmBmp;
    HDC     wmDC;
    HBITMAP wmOldBmp;
    HFONT   hFontWm;
    HFONT   hFontPill;      // stempelfont i skrivebordsmodus (fase 16)
    int     pillFontH;      // hoyden hFontPill er bygget for
    int     wmFontH;       // fonthoyden cachen ble bygget for
    int     wmW, wmH;      // storrelsen bitmapen ble bygget for
    int     wmSym, wmIv;   // konfigen den ble bygget for
    BOOL    wmValid;
} AppContext;

static AppContext g_Ctx;
static int g_savedPanelW = 0;   // panelstorrelse fra registret, 0 = ubrukt
static int g_savedPanelH = 0;
static int g_savedPanelX = 0;   // settes av LoadConfig, GEOM_UNSET = ubrukt
static int g_savedPanelY = 0;
// Startet via [ + ]. Et duplikat skriver aldri til registret - verken
// geometri eller symbol - og avslutter prosessen naar panelet lukkes.
// Registret er hovedinstansens hukommelse; ellers ville den som lukkes sist
// bestemt hvor neste oppstart legger panelet.
static BOOL g_isDuplicate = FALSE;
// --desktop-mode: flaten er barn av skrivebordets WorkerW, bak ikonene, over
// hele primaerskjermen. Samme vindusklasse og samme opptegning som panelet,
// men ingen ramme, ingen knapper, ingen input og ingen geometri i registret.
static BOOL g_desktopMode = FALSE;
static UINT g_msgTaskbarCreated = 0;   // Explorer startet paa nytt
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

// Vannmerkets alfa som funksjon av vindusbredden:
//   clamp(WM_ALPHA_BASE * sqrt(W / WM_W_NOMINAL), WM_ALPHA_MIN, WM_ALPHA_MAX)
// W er klientbredden i enhetspiksler. Med 400 px (minstebredden) gir formelen
// 0,037 og gulvet tar over; ved 1280 er den 0,065; over 3000 px tar taket.
static double WatermarkAlpha(int W) {
    if (W <= 0) return WM_ALPHA_MIN;
    double a = WM_ALPHA_BASE * sqrt((double)W / WM_W_NOMINAL);
    if (a < WM_ALPHA_MIN) a = WM_ALPHA_MIN;
    if (a > WM_ALPHA_MAX) a = WM_ALPHA_MAX;
    return a;
}

// Steglengde S (i lys) mellom tidsetikettene.
//   N = floor(chartW / minDx),  M = ceil(dispCount),
//   S = max(1, ceil((M - 1) / (N - 1)))
// CEIL, ikke floor: med floor gir M = 9, chartW = 320, minDx = 80 S = 2 og
// 71 px mellom etikettene - kollisjon. Med ceil er avstanden S * chartW /
// dispCount >= minDx for alle M og N >= 2 (M >= N: (M-1)N >= (N-1)M; M < N:
// S = 1 og ett lys er alt bredere enn minDx). N < 2 gir en etikett.
static int TimeTickStep(double dispCount, int chartW, int minDx) {
    if (dispCount < 1.0) dispCount = 1.0;
    if (chartW <= 0 || minDx <= 0) return 1;
    int m = (int)ceil(dispCount);
    int nx = chartW / minDx;
    if (nx < 2) return (m > 1) ? m : 1;
    int s = (m - 1 + (nx - 1) - 1) / (nx - 1);
    return (s < 1) ? 1 : s;
}

// Runder S opp til et steg som er et helt antall lys OG et rundt tidsrom
// (5 min, 15 min, 1 t, 6 t, 1 d ...). TimeTickStep alene gir S = 23 paa 300
// 1m-lys ved 1280 px, altsaa etiketter paa 02:48, 03:11, 03:34 (sett i
// PrintWindow). Oppover-avrunding kan bare gjore avstanden STORRE, saa
// kollisjonsgarantien i TimeTickStep holder. Er S storre enn tabellen,
// brukes S som den er.
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

// Skiller "ingen lagret posisjon" fra en ekte koordinat, som godt kan vaere
// negativ paa en skjerm til venstre for eller over den primaere.
#define GEOM_UNSET  ((int)0x80000000)

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

static void LoadConfig(AppContext* ctx, int* outX, int* outY, int* outW, int* outH) {
    ctx->symIdx     = 0;
    ctx->ivIdx      = 0;
    ctx->intervalMs = INTERVALS[0].ms;
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
    RegCloseKey(k);

    // Bundet sjekk. Et register redigert for hand, eller etterlatt av en
    // nyere versjon med flere symboler, skal ikke kunne indeksere utenfor
    // tabellen.
    if (sy < (DWORD)SYMBOL_COUNT)   ctx->symIdx = (int)sy;
    if (iv < (DWORD)INTERVAL_COUNT) {
        ctx->ivIdx      = (int)iv;
        ctx->intervalMs = INTERVALS[iv].ms;
    }
    if (w >= 240 && w <= 8192) *outW = (int)w;
    if (h >= 160 && h <= 8192) *outH = (int)h;

    // Posisjonen kan vaere negativ paa en skjerm til venstre for eller over
    // den primaere, saa den tolkes som signed. PanelHasPos skiller "ikke
    // lagret" fra "lagret som 0,0".
    if (hasPos) { *outX = (int)(LONG)px; *outY = (int)(LONG)py; }
}

// Sist valgte modus fra tray-menyen. Egen verdi og egne funksjoner, ikke en
// del av SaveConfig: den skrives i det brukeren velger, ikke i WM_DESTROY,
// som aldri kjoerer naar prosessen drepes utenfra.
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

// Start ved paalogging (fase 13). Bor i Run-nokkelen, ikke under REG_PATH:
// det er Explorer som leser den ved paalogging. Innholdet er stien til exe-en
// i anforselstegn, saa en sti med mellomrom ikke deles opp til et program og
// argumenter.
#define AUTOSTART_KEY   L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define AUTOSTART_VALUE L"Ticker"

// Stien i anforselstegn, "C:\Mappe med mellomrom\ticker.exe". FALSE naar
// stien ikke passer i MAX_PATH - en avkuttet sti skal aldri havne i registret.
static BOOL AutostartCommand(wchar_t* out, size_t cch) {
    wchar_t exe[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return FALSE;
    return swprintf_s(out, cch, L"\"%s\"", exe) > 0;
}

// Haken i tray-menyen: finnes verdien, uansett type og innhold? Den kan peke
// et annet sted enn exe-en som kjorer; det avgjor ToggleAutostart.
static BOOL AutostartPresent(void) {
    return RegGetValueW(HKEY_CURRENT_USER, AUTOSTART_KEY, AUTOSTART_VALUE,
                        RRF_RT_ANY, NULL, NULL, NULL) == ERROR_SUCCESS;
}

// Klikk paa "Start ved paalogging":
//   verdi == gjeldende sti -> slett
//   ingen verdi            -> skriv gjeldende sti
//   noe annet              -> skriv gjeldende sti (exe-en er flyttet)
// Siste gren er grunnen til at haken betyr "verdien finnes", ikke "verdien
// stemmer": et klikk paa en avkrysset, men foreldet, oppforing skal rette
// stien, ikke skru av autostart.
static void ToggleAutostart(void) {
    if (g_isDuplicate) return;
    wchar_t want[MAX_PATH + 2], have[MAX_PATH + 2];
    if (!AutostartCommand(want, MAX_PATH + 2)) return;
    // Feil type, eller for lang til bufferet (ERROR_MORE_DATA), kan umulig
    // vaere vaar sti og havner i "noe annet".
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

static void SaveConfig(const AppContext* ctx) {
    if (g_isDuplicate) return;
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0,
                        KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS) {
        return;   // ingen skriverett: stille, appen fungerer likevel
    }
    DWORD sy = (DWORD)ctx->symIdx, iv = (DWORD)ctx->ivIdx;
    RegSetValueExW(k, L"SymbolIndex",   0, REG_DWORD, (const BYTE*)&sy, sizeof(sy));
    RegSetValueExW(k, L"IntervalIndex", 0, REG_DWORD, (const BYTE*)&iv, sizeof(iv));
    RegCloseKey(k);
}

// Skrivebordsmodus lagrer ikke: flaten er hele skjermen i WorkerW-koordinater,
// og den ville blitt vanlig modus' "lagrede storrelse" ved neste oppstart.
//
// Globalene oppdateres ogsaa (fase 12): PlacePopupInitially leser dem, og
// panelet lages paa nytt hver gang modus byttes. Uten dette ville en tur
// innom skrivebordsmodus lagt panelet der det sto ved oppstart.
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

// Lagrer posisjon og storrelse slik vinduet staar NAA. Minimert eller
// maksimert vindu lagres ikke som saadan - da ville vi husket en
// oppgavelinje-strimmel eller hele skjermen som "brukerens storrelse".
// GetWindowPlacement gir den gjenopprettede geometrien i begge tilfeller.
static void SaveWindowPlacement(HWND hwnd) {
    if (!hwnd) return;
    WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
    if (!GetWindowPlacement(hwnd, &wp)) return;
    RECT r = wp.rcNormalPosition;
    SaveGeometry(r.left, r.top, r.right - r.left, r.bottom - r.top);
}

// Er den lagrede posisjonen fortsatt paa en skjerm som finnes? En posisjon
// fra en frakoblet skjerm ville lagt vinduet utenfor alt synlig.
static BOOL PlacementIsVisible(int x, int y, int w, int h) {
    RECT r = { x, y, x + w, y + h };
    return MonitorFromRect(&r, MONITOR_DEFAULTTONULL) != NULL;
}

// Sentrerer vinduet paa den skjermen det staar paa, i fabrikkstorrelse.
// SWP_NOZORDER | SWP_NOACTIVATE: vi endrer geometri, ikke stablerekkefolge
// eller fokus - brukeren kan ha trykket Ctrl+0 fra et annet vindu.
static void ResetToDefaultView(HWND hwnd) {
    if (!hwnd) return;

    // Var vinduet maksimert, maa det gjenopprettes forst - ellers ville
    // SetWindowPos skrevet en storrelse som OS-et overstyrer ved restore.
    //
    // IsIconic hoerer med av samme grunn, og av en til: et minimert vindu er
    // fortsatt WS_VISIBLE, saa IsWindowVisible er TRUE og tray-menyens
    // "Standardvisning" hopper over TogglePopup. Uten dette satte
    // SetWindowPos bare den gjenopprettede geometrien mens vinduet ble
    // staaende minimert - menypunktet gjorde ingenting synlig. Maalt etter at
    // minimer-knappen gjorde den stien lett aa naa.
    if (IsZoomed(hwnd) || IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);

    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(MONITORINFO) };
    if (!GetMonitorInfoW(hMon, &mi)) return;
    RECT wa = mi.rcWork;

    // "DPI-skalert 1280x720". Prosessen er DPI-uvitende i dag, saa
    // GetDpiForWindow gir 96 og MulDiv er identitet. Skrudde vi paa
    // DPI-bevissthet ville dette vaert riktig uten flere endringer - i
    // motsetning til et hardkodet 1280, som ville gitt et lite vindu paa en
    // 200 %-skjerm. Vi skrur den IKKE paa her: hele layouten er i raa
    // piksler, og vannmerkets klemmegrenser ville talt skaleringen to
    // ganger (se DPI-kommentaren i EnsureWatermark).
    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) dpi = 96;
    int w = MulDiv(POPUP_W, (int)dpi, 96);
    int h = MulDiv(POPUP_H, (int)dpi, 96);

    // Faar ikke fabrikkstorrelsen plass, klem den. En 1280x720 sentrert paa
    // en 1366x768-skjerm ville ellers lagt knapperaden utenfor
    // arbeidsomraadet.
    int aw = wa.right - wa.left, ah = wa.bottom - wa.top;
    if (w > aw) w = aw;
    if (h > ah) h = ah;

    int x = wa.left + (aw - w) / 2;
    int y = wa.top  + (ah - h) / 2;

    SetWindowPos(hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    SaveWindowPlacement(hwnd);
}

// Windows 11 runder hjornene paa alle vinduer med WS_THICKFRAME, ogsaa naar
// rammen er fjernet i WM_NCCALCSIZE. Radien paa ~8 px spiser hjornet av
// krysset. Attributtet er 33 fra Windows 11 21H2; feiler kallet paa Windows
// 10, finnes det ingen runding aa slaa av - derfor ingen feilhandtering.
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

// Setter tittellinja til det aktive paret. Med ekte OS-ramme er tittelen
// synlig i baade vinduet og oppgavelinja, saa den skal si hva man ser paa.
static void UpdatePopupTitle(AppContext* ctx) {
    if (!ctx->hPopup) return;
    wchar_t title[96];
    swprintf_s(title, 96, L"%s  -  %s",
               SYMBOLS[ctx->symIdx].label, INTERVALS[ctx->ivIdx].label);
    SetWindowTextW(ctx->hPopup, title);
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
        ctx->histPending = FALSE;   // nytt buffer: historikken begynner paa nytt
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

        if (c->openTime == lastT) {          // lyset som holder pa a formes
            ctx->candles[n - 1] = *c;
            continue;
        }

        if (c->openTime > lastT) {           // nytt lys
            if (n >= MAX_CANDLES) {          // eldste faller ut
                memmove(ctx->candles, ctx->candles + 1, (size_t)(n - 1) * sizeof(Candle));
                n--;
                ctx->candleCount = n;
                ctx->frontShift++;     // UI-traden forskyver disp-indeksene mot denne
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
    //
    // viewCount == 0 betyr "ikke satt" - panelet ble aapnet (eller symbolet
    // byttet) for det fantes lys. Det maa bli standardutsnittet HER: ClampView
    // under klemmer 0 opp til MIN_VIEW, og WorkerFetchKlines' egen
    // "0 -> DEFAULT_VIEW" kommer for sent til aa se nullen. Maalt: foerste
    // aapning viste 8 lys i stedet for 300, i alle bygg siden fase 1 - og
    // hvert duplikat fra [ + ] aapner nettopp foer det har data.
    if (ctx->followLive) {
        if (ctx->viewCount <= 0) {
            ctx->viewCount = (ctx->candleCount < DEFAULT_VIEW) ? ctx->candleCount : DEFAULT_VIEW;
        }
        ctx->viewStart = ctx->candleCount - ctx->viewCount;
    }
    ClampView(ctx);
}

// Bakfylling (fase 18): legger eldre lys FORAN bufferet. in er stigende i
// tid, som fra ParseKlines. Kalles under laas.
//
// Lys som ikke er eldre enn candles[0] kastes - endTime i spoerringen er
// candles[0].openTime - 1, saa de skal ikke finnes, men serveren bestemmer.
// Av resten tas de NYESTE som faar plass under MAX_CANDLES; de eldste
// ryker, og bufferet er da fullt. Utsnittet flyttes k plasser saa de samme
// lysene staar under det, og frontShift telles ned saa UI-traden flytter
// visning, hover og pan-anker like mye. followLive er uroert: et utsnitt
// som fulgte siste lys, gjoer det fortsatt.
//
// histDone settes naar bufferet er fullt, og naar ingenting av det som kom
// var brukbart: da har serveren ikke noe eldre, og neste vegg-treff skal
// ikke spoerre igjen.
static void PrependCandles(AppContext* ctx, const Candle* in, int count) {
    if (count <= 0 || ctx->candleCount <= 0) return;

    long long oldest = ctx->candles[0].openTime;
    int usable = 0;
    while (usable < count && in[usable].openTime < oldest) usable++;

    int room = MAX_CANDLES - ctx->candleCount;
    int k    = (usable < room) ? usable : room;
    if (k > 0) {
        const Candle* src = in + (usable - k);   // de nyeste av de brukbare
        memmove(ctx->candles + k, ctx->candles, (size_t)ctx->candleCount * sizeof(Candle));
        memcpy(ctx->candles, src, (size_t)k * sizeof(Candle));
        ctx->candleCount += k;
        ctx->frontShift  -= k;
        // viewCount 0 er "vis alt" (GetView) og skal forbli det: ClampView
        // ville loeftet 0 til MIN_VIEW. Med et satt utsnitt flyttes det k
        // plasser, saa de samme lysene staar under det.
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

        // Statuskoden (fase 18). Foer talte enhver kropp som suksess, ogsaa
        // en 429 med JSON-feilmelding - parserne fanget det stille som "null
        // lys" / "ingen pris". Bakfyllingen trenger skillet: 2xx med null lys
        // betyr "historikken er slutt", alt annet er en feil som skal i
        // backoff. En 4xx paa de gamle stiene gaar naa samme vei, med samme
        // utfall som foer.
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

// Bakfylling (fase 18): SEED_COUNT lys eldre enn det eldste vi har. Kjoeres
// FOER den inkrementelle hentingen i syklusen histPending staar, saa det
// levende lyset holder seg ferskt uansett. TRUE betyr som ellers "ikke en
// nettverksfeil". Feiler HttpGet, slippes flagget: neste vegg-treff spoer
// igjen. Ellers kunne en varig 4xx paa denne stien alene ha sultet ut
// lys-hentingen.
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
    endTime = ctx->candles[0].openTime - 1;   // endTime er inklusiv hos Binance
    LeaveCriticalSection(&ctx->lock);

    wchar_t path[192];
    swprintf_s(path, 192, L"/api/v3/klines?symbol=%s&interval=%s&endTime=%lld&limit=%d",
               SYMBOLS[si].api, INTERVALS[ii].api, endTime, SEED_COUNT);

    BOOL got = HttpGet(ctx, path, s_httpBuf, (DWORD)sizeof(s_httpBuf));
    int  n   = got ? ParseKlines(s_httpBuf, s_incoming, SEED_COUNT) : 0;

    EnterCriticalSection(&ctx->lock);
    if (ctx->configGen == gen) {
        ctx->histPending = FALSE;
        if (got) {
            if (n <= 0) ctx->histDone = TRUE;   // 2xx uten lys: historikken er slutt
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
        HWND hp   = ctx->hPopup;
        BOOL hist = ctx->histPending;
        LeaveCriticalSection(&ctx->lock);

        // Star grafen apen trenger vi lys; ellers holder det med prisen.
        // Vil UI ha eldre lys (fase 18), hentes de foerst, og lysene like
        // etter - to kall i den syklusen, saa det levende lyset ikke venter.
        BOOL ok;
        if (hp && IsWindowVisible(hp)) {
            ok = hist ? WorkerFetchHistory(ctx) : TRUE;
            if (ok) ok = WorkerFetchKlines(ctx);
        } else {
            ok = WorkerFetchPrice(ctx);
        }

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


static BOOL PtInRect2(const RECT* r, int x, int y) {
    return (x >= r->left && x < r->right && y >= r->top && y < r->bottom);
}

// Kontrollknappene i headeren. En ren funksjon av bredden, uten tilstand -
// tegning, WM_NCHITTEST, hover og klikk leser alle denne. Leser to av dem
// ulike kilder, treffer brukeren en annen knapp enn den som lyser.
// Rekkefolge fra venstre: ny instans, minimer, maksimer, lukk. Krysset
// lengst til hoyre, der Windows har vent oyet til det. [ + ] tok plassen til
// gjenopprett-standardvisning-knappen i samme enum-posisjon, saa geometrien,
// WM_NCHITTEST og hover-indeksene er uendret.
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

// Hvilken knapp peker musa paa? -1 utenfor alle.
static int ButtonHit(const RECT* btns, int x, int y) {
    for (int i = 0; i < BTN_COUNT; ++i) {
        if (PtInRect2(&btns[i], x, y)) return i;
    }
    return -1;
}

// Knapperadens samlede rektangel. Avledet av ButtonLayout, ikke regnet ut paa
// nytt - fallgruve 14 gjelder her ogsaa: invaliderer vi et annet felt enn det
// vi tegner, blir en knapp staaende uoppdatert.
static void ButtonStrip(int W, RECT* out) {
    RECT b[BTN_COUNT];
    ButtonLayout(W, b);
    out->left   = b[0].left;
    out->top    = b[0].top;
    out->right  = b[BTN_COUNT - 1].right;
    out->bottom = b[0].bottom;
}

// Kollisjonsregelen i headeren, som ren funksjon: et venstrestilt element som
// slutter paa rightBound og et hoyrestilt som begynner paa leftBound faar
// staa paa samme rad bare med minst HDR_GAP px luft imellom.
static BOOL HeaderFits(int rightBound, int leftBound) {
    return rightBound < leftBound - HDR_GAP;
}

// Felles geometri for tegning og muse-treff.
// Skrivebordsmodus: header og tidsbaand fantes bare for tekst som ikke lenger
// tegnes (fase 14), saa topp, bunn og venstre gaar kant til kant. Hoyre side
// har derimot faatt tilbake en marg (fase 16) - ikke til akseetiketter, men
// til det ene stempelet med siste pris. Margen er smalere enn panelets fordi
// den bare skal romme stempelet.
//
// Alt annet folger herfra: vannmerkets sentrering, rutenettet, lysene,
// klipperegionen og siste-pris-linja.
// Stempelets hoyde, fonthoyde og margbredde i skrivebordsmodus. Rene
// funksjoner av flatens hoyde: ChartGeometry kalles ogsaa fra treffdeteksjon
// og panorering, der det ikke finnes noen DC aa maale i.
static int DeskPillH(int H) {
    int h = H / DESK_PILL_DIV;
    if (h < DESK_PILL_MIN) h = DESK_PILL_MIN;
    if (h > DESK_PILL_MAX) h = DESK_PILL_MAX;
    return h;
}

// Samme forhold som i panelet: 16 px stempel rundt en 15 px font.
static int DeskPillFontH(int H) { return MulDiv(DeskPillH(H), 15, 16); }

// AXIS_CHAR_W er maalt paa em 15. Bredden rundes OPP: en tegnbredde som
// egentlig er 22,2 px ville gitt aatte tegn 1,6 px for lite, og prisen hadde
// falt stille tilbake til aksens oppslosning i stedet for to desimaler.
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

// Hvilket lys peker musa paa? Returnerer absolutt indeks, -1 utenfor.
// MAA lese de samme disp-verdiene som DrawChart. Leser den ene maalet og den
// andre visningen, peker crosshairet paa feil lys midt i animasjonen - det er
// feil #7 fra loggen i ny drakt.
static int HitCandle(const AppContext* ctx, const ChartRect* g, int mx, int my) {
    if (ctx->dispCount <= 0.0 || g->cw <= 0) return -1;
    if (ctx->candleCount <= 0) return -1;
    if (mx < g->left || mx >= g->right || my < g->top || my > g->bottom) return -1;

    double slot = (double)g->cw / ctx->dispCount;
    if (slot <= 0.0) return -1;
    int idx = (int)(ctx->dispStart + (double)(mx - g->left) / slot);

    // Bundet mot candleCount, ikke mot vs + vc: under animasjonen kan
    // visningen henge utenfor maalutsnittet.
    if (idx < 0) idx = 0;
    if (idx >= ctx->candleCount) idx = ctx->candleCount - 1;
    return idx;
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
static void ApplyFrontShift(AppContext* ctx) {
    long long delta = ctx->frontShift - ctx->dispShiftSeen;
    if (delta == 0) return;
    ctx->dispShiftSeen = ctx->frontShift;

    // Ingen easing: en utkasting er ikke en bevegelse brukeren skal se, og en
    // bakfylling (delta < 0, fase 18) skal ikke flytte bildet i det hele tatt.
    ctx->dispStart -= (double)delta;
    if (ctx->dispStart < 0.0) ctx->dispStart = 0.0;
    if (ctx->hoverIdx >= 0) {
        ctx->hoverIdx -= (int)delta;
        if (ctx->hoverIdx < 0) ctx->hoverIdx = -1;
    }
    ctx->panAnchorView -= (int)delta;
    if (ctx->panAnchorView < 0) ctx->panAnchorView = 0;
}

// Brukeren staar i veggen (viewStart == 0) og vil bakover (fase 18). Setter
// histPending og vekker traden - men bare naar linja er frisk: hWakeEvent
// nullstiller backoffen (den er laget for "panelet ble aapnet"), og et drag i
// veggen under en frakobling skal ikke slaa backoffen av. Er traden i
// backoff, ser den flagget paa sin egen syklus. SetEvent staar utenfor
// laasen, som ellers i fila.
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

// Setter visningen lik maalet uten animasjon. Brukes naar en animasjon ikke
// gir mening: forste bilde, nytt buffer etter konfigbytte, panelet apnes.
// Kalles under laas.
static void SyncDisp(AppContext* ctx) {
    int vs, vc;
    GetView(ctx, &vs, &vc);
    ctx->dispStart = (double)vs;
    ctx->dispCount = (vc > 0) ? (double)vc : 1.0;
    ctx->dispShiftSeen = ctx->frontShift;

    // Med tomt buffer finnes det ingen prisakse a synkronisere mot. Markerer
    // vi oss som gyldige her, eases dispMin/dispMax fra [0, 1] opp til det
    // ekte spennet naar dataene kommer - altsaa en Y-akse som glir opp fra
    // null i et halvt sekund etter hvert symbolbytte. Vi blir staaende
    // ugyldige i stedet, saa forste bilde MED data snapper.
    if (ctx->candleCount <= 0 || vc <= 0) {
        ctx->dispMin   = 0.0;
        ctx->dispMax   = 1.0;
        ctx->dispValid = FALSE;
        return;
    }

    PriceRange(ctx, vs, vc, &ctx->dispMin, &ctx->dispMax);
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

// Sorger for at det vedvarende dobbeltbufferet finnes og har storrelsen W x H.
// Bygges bare paa nytt naar storrelsen endres - ikke per bilde. Returnerer
// FALSE hvis GDI ikke ga oss et buffer; da tegner PaintPopup ingenting i
// dette bildet i stedet for aa tegne rett paa skjermen med flimmer.
//
// Riv ned det gamle FORST. Uten dette lekker en HBITMAP og en HDC per
// resize, og GDI-tallet klatrer for hver gang brukeren drar i kanten.
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
    // Alfa folger W, og W er alt en del av cache-noekkelen over - fargen
    // regnes derfor bare ut naar bitmapen bygges. Alt under er ugjennomsiktig
    // CLR_BG, saa Blend mot bakgrunnen ER alfablending.
    SetTextColor(ctx->wmDC, Blend(CLR_BG, CLR_WM_INK,
                                  (int)(WatermarkAlpha(W) * 255.0 + 0.5)));

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

// Stempelfonten for skrivebordsmodus. Bygges bare naar hoyden endrer seg -
// samme moenster som hFontWm. Et modusbytte beholder H for flaten, saa dette
// er ikke en per-bilde-kostnad.
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
        // Skrivebordsmodus: ingen status paa tapetet. Flaten staar med
        // bakgrunn og vannmerke til det finnes lys aa tegne. Meldingen ville
        // vaert den eneste teksten igjen, og den sier ingenting en bruker som
        // ikke kan klikke paa flaten kan gjore noe med.
        if (g_desktopMode) return;

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
    ChartRect g = ChartGeometry(W, H);

    // --- Header-layout, maalt ---
    // Tidligere delte prisen og prosenten ETT rektangel, venstre- og
    // hoyrestilt. Paa et smalt panel moettes de da midt i og tegnes oppi
    // hverandre - DrawTextW klipper mot rektangelet, ikke mot naboteksten.
    // Naa maales hver tekst paa den ferdig formaterte strengen i sin egen font
    // (feil #5), og rad for rad sammenliknes hoyre grense for det
    // venstrestilte med venstre grense for det hoyrestilte.
    //
    // Rad 1 (y 10-30): pris til venstre, prosent og knapperad til hoyre.
    // Rad 2 (y 28-42): symbollinja til venstre. Til hoyre ligger ikke
    // knappene (de slutter paa y = 24), men prisaksens overste etikett, som
    // staar paa y = top +- 8 fra x = right + AXIS_LBL_GAP.
    // Metadata-overlayet (fase 14). Pris, prosent og symbollinje er laget som
    // leses fovealt: brukeren maa stoppe opp og dekode tall. Paa skrivebordet
    // konkurrerer de med ikoner og mapper, og flaten skal leses perifert.
    // Hele blokka staar derfor stille i skrivebordsmodus.
    if (!g_desktopMode) {
        RECT strip;
        ButtonStrip(W, &strip);
        int btnLeft = strip.left;          // X_left_bound for knapperaden

        // Rad 1, venstre: prisen. Rektangelet slutter ved knapperaden, saa selv
        // en pris som ikke faar plass aldri tegnes under knappene.
        SelectObject(hdc, ctx->hFontBig);
        SetTextColor(hdc, stale ? CLR_DIM : CLR_TEXT);
        swprintf_s(buf, 64, L"$%.2f", last);
        int lenPrice = (int)wcslen(buf);
        SIZE szPrice = { 0, 0 };
        GetTextExtentPoint32W(hdc, buf, lenPrice, &szPrice);
        int priceRight = PAD_L + szPrice.cx;   // X_right_bound
        RECT rcPrice = { PAD_L, 10, btnLeft - HDR_GAP, 30 };
        DrawTextW(hdc, buf, lenPrice, &rcPrice, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        // Rad 1, hoyre: prosenten i tre trinn. Hel med spenn, saa uten spenn,
        // saa skjult. Aldri klippet midt i et tall - "+0,5" der det staar
        // "+0,50 %" er en feil verdi, ikke en kortere.
        SelectObject(hdc, ctx->hFontSmall);
        int pctRight = btnLeft - HDR_GAP;
        wchar_t pctFull[48], pctShort[24];
        swprintf_s(pctFull,  48, L"%+.2f%%  (%s)", chg, span);
        swprintf_s(pctShort, 24, L"%+.2f%%", chg);
        int lenFull = (int)wcslen(pctFull), lenShort = (int)wcslen(pctShort);
        SIZE szFull = { 0, 0 }, szShort = { 0, 0 };
        GetTextExtentPoint32W(hdc, pctFull, lenFull, &szFull);

        // Den korte formen maales bare naar den hele ikke fikk plass. Hver
        // GetTextExtentPoint32W er ~20 us, og over minstebredden faar den hele
        // plass med god margin - maalt ved 400 px: ~115 px luft til prisen.
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

        // Rad 2: symbollinja. Den kortes med ellipse mot prisaksens etikett;
        // her er det en merkelapp, ikke et tall, saa en ellipse loyer ikke.
        SetTextColor(hdc, CLR_DIM);
        if (stale) {
            swprintf_s(buf, 64, L"%s  -  %s  -  frakoblet %ds",
                       SYMBOLS[ctx->symIdx].label, INTERVALS[ctx->ivIdx].label, staleSecs);
        } else {
            swprintf_s(buf, 64, L"%s  -  %s",
                       SYMBOLS[ctx->symIdx].label, INTERVALS[ctx->ivIdx].label);
        }
        int lenSub = (int)wcslen(buf);
        SIZE szSub = { 0, 0 };
        GetTextExtentPoint32W(hdc, buf, lenSub, &szSub);
        int subLimit = (g.edge + AXIS_LBL_GAP) - HDR_GAP;   // X_left_bound for aksetiketten
        RECT rcSub = { PAD_L, 28, subLimit, 42 };
        UINT subFlags = DT_LEFT | DT_SINGLELINE | DT_VCENTER;
        if (!HeaderFits(PAD_L + szSub.cx, subLimit + HDR_GAP)) subFlags |= DT_END_ELLIPSIS;
        DrawTextW(hdc, buf, lenSub, &rcSub, subFlags);
    }

    // --- Chart-geometri ---
    int left = g.left, top = g.top, right = g.right, bottom = g.bottom;
    int cw = g.cw, ch = g.ch;
    int edge = g.edge;   // aksekanten; right er der lysene slutter
    if (cw <= 0 || ch <= 0) return;

    // Kalles under laas, saa frontShift kan leses direkte.
    ApplyFrontShift(ctx);
    if (!ctx->dispValid) SyncDisp(ctx);

    // Tegningen leser VISNINGEN. Maalet (vs, vc) brukes bare til spennteksten
    // i headeren og til a regne ut hva visningen skal ease MOT - og det siste
    // skjer i WM_TIMER, ikke her.
    double dStart = ctx->dispStart;
    double dCount = ctx->dispCount;
    if (dCount < 1.0) dCount = 1.0;

    double minP = ctx->dispMin, maxP = ctx->dispMax;
    double range = maxP - minP;
    if (range < 1e-9) range = 1.0;

    // --- Klipping til chart-flaten ---
    // Med dStart = 142,7 finnes det halve lys i begge kanter, og midt i
    // Y-easingen ligger veker over maxP og under minP. Rutenett og lys tegnes
    // derfor innenfor en klipperegion avgrenset til rcChart, og ingenting
    // annet: aksetekstene, stempelet og headeren ligger utenfor flaten og
    // tegnes etter SelectClipRgn(NULL).
    //
    // Hoyre kant er EKSKLUSIV: kolonnen x = edge hoerer til aksemargen, og
    // rutenettet slutter paa edge - 1. Med edge + 1 her maalte vi lyspiksler
    // i den kolonnen i 21 av 240 bilder under panorering, maksimert (fase 6).
    //
    // Klippet gaar til edge, ikke til right: lysene holder seg innenfor right
    // av seg selv (slot regnes av cw), mens rutenettet og siste-pris-linja
    // skal krysse luftrommet og naa helt fram til aksen (fase 15).
    //
    // Bunnen er INKLUSIV (bottom + 1): rutenettlinje i = 4 ligger paa
    // y = bottom, og det samme gjor veken til lyset med laveste pris. Et
    // [top, bottom)-klipp ville visket ut den nederste linja.
    RECT rcChart = { left, top, edge, bottom + 1 };
    IntersectClipRect(hdc, rcChart.left, rcChart.top, rcChart.right, rcChart.bottom);

    // --- Rutenett ---
    // Kant til kant legger linje i = 0 paa y = 0 og i = 4 paa y = H - 1. Det
    // er en 1 px ramme rundt hele skjermen - selve interferensen
    // skrivebordsmodus skal vaere fri for. De tre indre linjene baerer den
    // romlige referanserammen alene.
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
    if (bodyW > 18) bodyW = 18;   // hindrer klumpete lys ved full innzoom

    // Lokka gaar fortsatt over SYNLIGE lys, ikke over hele historikken:
    // i1 - i0 er dCount + 1 avrundet. Ytelseskarakteristikken fra fase 1
    // staar.
    int i0 = (int)floor(dStart);
    int i1 = (int)ceil(dStart + dCount);
    if (i0 < 0) i0 = 0;
    if (i1 > n) i1 = n;

    // Lysene tegnes med systemets DC_PEN og DC_BRUSH, fargelagt per lys, i
    // stedet for fire egne penner og pensler. Det er fire GDI-objekter
    // mindre; det vedvarende bufferet tar to, saa tallet i hvile gaar ned med
    // to. Heltrukket 1 px i begge tilfeller, saa pikslene er de samme.
    // Fargen settes bare naar den skifter.
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

        // Veke
        MoveToEx(hdc, cx, yHigh, NULL);
        LineTo(hdc, cx, yLow);

        // Kropp
        int yTop = (yOpen < yClose) ? yOpen : yClose;
        int yBot = (yOpen < yClose) ? yClose : yOpen;
        if (yBot - yTop < 1) yBot = yTop + 1; // doji -> minst 1px
        Rectangle(hdc, cx - bodyW / 2, yTop, cx - bodyW / 2 + bodyW, yBot);
    }

    // Klippingen MAA vekk for aksetekstene - de ligger i margen til hoyre.
    SelectClipRgn(hdc, NULL);

    SelectObject(hdc, GetStockObject(BLACK_PEN));
    SelectObject(hdc, GetStockObject(NULL_BRUSH));

    // --- Prisetiketter ---
    // Egen lokke etter klippingen, ikke i rutenettlokka: der ville de blitt
    // klippet bort sammen med alt annet utenfor rcChart.
    // Omega_y-axis: x i [edge + AXIS_LBL_GAP, W - AXIS_PAD_R).
    int axL = edge + AXIS_LBL_GAP, axR = W - AXIS_PAD_R;
    SelectObject(hdc, ctx->hFontAxis);
    SetTextColor(hdc, CLR_AXIS);
    // Stempelet for siste pris ligger oppaa etiketten paa samme hoyde (se
    // under). Begge er 16 px hoye, og med 11 px sifre ble en etikett som laa
    // under 16 px unna halvt dekket, med et avkuttet tall synlig under. En
    // etikett som ville kollidert, tegnes derfor ikke. Samme regel og samme
    // yLast som stempelet.
    // Fase 14: ingen maaleverdier paa skrivebordet. Kolonnen finnes ikke der
    // heller - axL ligger utenfor flaten naar geometrien gaar kant til kant.
    int yPill = INT_MIN;
    if (!g_desktopMode) {
        {
            double lp = ctx->candles[n - 1].close;
            int yl = top + (int)(((maxP - lp) / range) * ch);
            if (yl >= top && yl <= bottom) yPill = yl;
        }
        for (int i = 0; i <= 4; ++i) {
            int y = top + (ch * i) / 4;
            if (yPill != INT_MIN && abs(y - yPill) < 16) continue;
            double p = maxP - (range * i) / 4.0;
            swprintf_s(buf, 64, L"%.*f", PriceDecimals(range / 4.0), p);
            RECT rcLbl = { axL, y - 8, axR, y + 8 };
            DrawTextW(hdc, buf, -1, &rcLbl, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }
    }

    // --- Tidsakse (Omega_x-axis) ---
    // Bare tekst, ingen akselinje. Etikettene staar under lysets midtpunkt i
    // baandet [bottom + 2, H - 1], og aldri utenfor [left, right]: under
    // hoyre kolonne ligger prisaksens nederste etikett og stempelet.
    //
    // Hvilke lys som faar etikett, forankres i TIDEN, ikke i indeksen i0.
    // Relativt til i0 ville etikettene hoppe til nye lys i hvert bilde av en
    // panorering; relativt til absolutt indeks ville de hoppe ett lys hver
    // gang et lys kastes ut i front naar bufferet er fullt. openTime /
    // intervalMs er stabil gjennom begge.
    //
    // O(antall etiketter): ett modulo for aa finne foerste etikett, deretter
    // steg paa S rett i candles[]. Ingen allokering.
    if (i0 < i1 && !g_desktopMode) {
        wchar_t tl[24];
        FormatCandleTime(ctx->candles[i0].openTime,
                         ctx->intervalMs, tl, 24);
        int tlLen = (int)wcslen(tl);
        SIZE tsz = { 0, 0 };
        GetTextExtentPoint32W(hdc, tl, tlLen, &tsz);   // aksefonten er valgt
        int minDx = tsz.cx + TIME_LBL_GAP;
        if (minDx < TIME_DX_MIN) minDx = TIME_DX_MIN;
        long long iv = (ctx->intervalMs > 0) ? ctx->intervalMs : 60000LL;
        int step = NiceTimeStep(TimeTickStep(dCount, cw, minDx), iv);

        // Forankret i LOKAL tid, saa 6 t-steg lander paa 00, 06, 12 og 18
        // her og ikke paa 02, 08 ... (UTC + 2 om sommeren). Forskyvningen
        // leses paa i0; et sommertidsskifte midt i utsnittet flytter bare
        // etikettene en time.
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
            int xLast = left + (int)(((double)(n - 1) - dStart + 0.5) * slot);
            if (xLast < left)  xLast = left;
            if (xLast > right) xLast = right;

            // Linja gaar fra siste lys helt inn til stempelet (fase 15).
            // Stiplet over dataflaten, HELTRUKKET over luftrommet: PS_DASH
            // ender der monsteret tilfeldigvis staar, og ved 1004 px bredde
            // landet slutten i et "av"-intervall - maalt som svart hull mot
            // stempelet. Broen over luftrommet er den ene delen som MAA
            // treffe, saa den tegnes uten monster.
            //
            // edge + 1 fordi LineTo ikke tegner sluttpunktet: uten den ene
            // pikselen staar kolonnen x = edge tom, og stempelet begynner
            // foerst paa edge + 1.
            HPEN penLast = lastUp ? ctx->penLastUp : ctx->penLastDown;
            HPEN hOld2 = (HPEN)SelectObject(hdc, penLast);
            MoveToEx(hdc, xLast, yLast, NULL);
            LineTo(hdc, right, yLast);

            SelectObject(hdc, GetStockObject(DC_PEN));
            SetDCPenColor(hdc, lastUp ? CLR_UP : CLR_DOWN);
            MoveToEx(hdc, right, yLast, NULL);
            LineTo(hdc, edge + 1, yLast);
            SelectObject(hdc, hOld2);

            // Aksestempelet overskriver rutenettetiketten paa denne hoyden,
            // slik at det ikke staar to tall oppi hverandre.
            // Flaten faar 3 px luft paa hver side av teksten; teksten selv
            // holder seg innenfor axR.
            //
            // Fase 16: stempelet tegnes i BEGGE modi. Paa skrivebordet er det
            // den eneste teksten som staar igjen - akseetiketter, tidsakse og
            // header er fortsatt borte - og hoyden folger flaten i stedet for
            // panelets faste 16 px.
            {
                int half = g_desktopMode ? DeskPillH(H) / 2 : 8;
                if (g_desktopMode) EnsurePillFont(ctx, H);

                RECT rcPill = { edge + 1, yLast - half, axR + 3, yLast + half };
                SetDCBrushColor(hdc, lastUp ? CLR_UP : CLR_DOWN);
                FillRect(hdc, &rcPill, (HBRUSH)GetStockObject(DC_BRUSH));

                // "Presis verditekst": to desimaler der de faar plass, ellers
                // samme oppslosning som aksen. Bredden maales paa den ferdig
                // formaterte strengen - feil #5 igjen.
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

                // Mork tekst paa den mettede flaten - CLR_TEXT ville druknet.
                // Flaten er lagdelt med LWA_ALPHA 255, ikke fargenokkel, saa
                // CLR_BG er en farge her og ikke et hull ut til tapetet.
                SetTextColor(hdc, CLR_BG);
                RECT rcPillTxt = { axL, yLast - half, axR, yLast + half };
                DrawTextW(hdc, buf, -1, &rcPillTxt, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            }
        }
    }

    // --- Crosshair + hover-boks ---
    // Bundet mot den SYNLIGE flaten, ikke mot maalutsnittet - de faller fra
    // hverandre midt i en animasjon.
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
    // Samme bro som siste-pris-linja: den vannrette naar aksen, ellers ville
    // det staatt et hull mellom krysset og etiketten dets.
    MoveToEx(hdc, left, hy, NULL);     LineTo(hdc, edge, hy);
    SelectObject(hdc, hPrev);

    // Prisetikett pa hoyreaksen der pekeren star
    double hp = maxP - ((double)(hy - top) / (double)ch) * range;
    swprintf_s(buf, 64, L"%.*f", PriceDecimals(range / 4.0), hp);
    RECT rcTag = { edge + 1, hy - 8, axR + 3, hy + 8 };
    FillRect(hdc, &rcTag, ctx->brBoxEdge);
    SelectObject(hdc, ctx->hFontAxis);
    SetTextColor(hdc, CLR_TEXT);
    RECT rcTagTxt = { axL, hy - 8, axR, hy + 8 };
    DrawTextW(hdc, buf, -1, &rcTagTxt, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // Hover-boks med tid + OHLC. Tilbake til den vanlige lille fonten:
    // LINE_H = 13 er maalt paa den, og aksefonten er 15 px hoy.
    SelectObject(hdc, ctx->hFontSmall);
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


// Kontrollknappene. Rene GDI-vektorer - ingen font, ingen glyfoppslag. En
// DrawTextW med et Unicode-tegn koster langt mer enn fire LineTo, og ville
// dessuten vaert avhengig av at fonten HAR glyfen (tray-ikonets manglende
// k-glyf er samme problem lenger nede i loggen).
//
// Ingen fade. Knappene skifter farge momentant paa hover: en fade ville
// krevd enda en animert verdi i WM_TIMER, og et treff som henger paa
// fade-niva i stedet for paa btnHot er nettopp fallgruve 12.
static void DrawButtons(AppContext* ctx, HDC hdc, int W, BOOL zoomed) {
    RECT b[BTN_COUNT];
    ButtonLayout(W, b);

    HPEN   oldPen = (HPEN)SelectObject(hdc, ctx->penBtn);
    HBRUSH oldBr  = (HBRUSH)SelectObject(hdc, (HBRUSH)GetStockObject(NULL_BRUSH));

    for (int i = 0; i < BTN_COUNT; ++i) {
        RECT* r = &b[i];
        BOOL hot = (ctx->btnHot == i);

        // Knappeflaten toemmes ALLTID for vektorene tegnes, ogsaa i hvile:
        // i hvile med CLR_BG, saa knappen fortsatt bare er en glyf paa
        // panelets egen bakgrunn. Da kan ingen tidligere glyf eller
        // hover-farge ligge igjen under, uansett hva DC-en inneholdt fra for.
        // Begge stiene i PaintPopup gaar hit, saa hurtigstien og den trege
        // tegner fortsatt identisk.
        FillRect(hdc, r, hot ? ((i == BTN_CLOSE) ? ctx->brClose : ctx->brBox)
                             : ctx->brBg);

        SelectObject(hdc, hot ? ((i == BTN_CLOSE) ? ctx->penBtnWhite : ctx->penBtnHot)
                              : ctx->penBtn);

        int cx = (r->left + r->right) / 2;
        int cy = (r->top + r->bottom) / 2;
        int g  = 4;   // halv glyfbredde: 9x9 piksler totalt

        switch (i) {
            case BTN_NEW:
                // Plusstegn, 7x7 rundt senterpikselen: x og y i [-3, +3].
                // LineTo tegner ikke sluttpunktet, derfor +4 - det er det
                // som gjor korset symmetrisk. 7 og ikke 9 som de andre: et
                // 9x9 pluss veier optisk tyngre enn krysset ved siden av.
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
                    // Gjenopprett: to overlappende rektangler. Det bakre
                    // tegnes som en APEN polylinje - kun de kantene som ikke
                    // ligger bak det fremre - saa vi slipper aa fylle det
                    // fremre ugjennomsiktig for aa skjule overlappet. To
                    // GDI-kall, ikke fire.
                    //
                    // To 7x7-rektangler forskjovet 2 px diagonalt, innenfor
                    // samme 9x9-fotavtrykk som de andre glyfene. Bakre rekt
                    // er x[-2..+4] y[-4..+2], fremre x[-4..+2] y[-2..+4].
                    // Synlig del av det bakre er alt utenfor det fremre:
                    // venstre kant ned til overlappet, toppen, hoyre kant, og
                    // stubben av bunnen. Polyline tegner ikke siste punkt, saa
                    // den stopper rett for det fremres hoyre kant.
                    POINT bak[5] = {
                        { cx - 2, cy - 2 },
                        { cx - 2, cy - 4 },
                        { cx + 4, cy - 4 },
                        { cx + 4, cy + 2 },
                        { cx + 2, cy + 2 },
                    };
                    Polyline(hdc, bak, 5);
                    // NULL_BRUSH er valgt over, saa Rectangle gir kun omriss.
                    Rectangle(hdc, cx - 4, cy - 2, cx + 3, cy + 5);
                } else {
                    // NULL_BRUSH er valgt over, saa Rectangle gir kun omriss.
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

static void PaintPopup(AppContext* ctx, HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdcDst = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    // Dobbeltbuffer: tegn alt i minnet, blit en gang -> ingen flimmer. Det
    // lever mellom bildene; se bbDC i AppContext for hvorfor.
    if (!EnsureBackBuffer(ctx, hdcDst, W, H)) {
        EndPaint(hwnd, &ps);
        return;
    }
    HDC hdcMem = ctx->bbDC;

    // Hurtigsti: er ALT det skitne innenfor knapperaden, trenger vi verken
    // DrawChart eller DrawOverlay. En hover-endring invaliderer nettopp det
    // rektangelet; uten denne grenen ville den kostet en full opptegning, og
    // den inkrementelle invalideringen ville bare spart den siste blitten.
    //
    // Knappene tegnes rett inn i bufferet, som holder forrige fulle bilde.
    // Mellomrommene i stripa er dermed noyaktig det den trege stien la der,
    // og DrawButtons toemmer hver knappeflate selv. Tre vilkaar gjor det
    // sant: bufferet har et fullt bilde i denne storrelsen (bbValid), og
    // overlayet er verken aapent eller synlig under uttoning - det dimmer
    // HELE klientflaten, headeren inkludert, saa bufferets strimmel ville
    // vaert dimmet mens knappene ikke var det. En samtidig
    // InvalidateRect(NULL) fra animasjonsklokka unionerer med stripa, saa
    // rcPaint blir hele flaten og vi faller ned i den trege stien av oss selv.
    RECT strip;
    ButtonStrip(W, &strip);
    // !g_desktopMode staar her eksplisitt: knapperaden tegnes ikke der, saa
    // hurtigstien ville blitt en stripe uten knapper over grafen.
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

    // Traden kan flette inn nye lys naar som helst; laasen holder
    // bufferet stabilt gjennom hele opptegningen (~1,8 ms).
    EnterCriticalSection(&ctx->lock);
    DrawChart(ctx, hdcMem, W, H);
    LeaveCriticalSection(&ctx->lock);

    // Knappene tegnes HER, ikke i DrawChart. DrawChart returnerer tidlig naar
    // bufferet er tomt - altsaa mens det staar "Laster data fra Binance..."
    // og under hele en frakobling. Laa tegningen der, ville krysset
    // forsvunnet nettopp naar brukeren vil lukke panelet. Samme grunn som
    // DrawOverlay ligger her.
    //
    // Utenfor laasen: btnHot og knappegeometri er UI-eid.
    //
    // Ikke i skrivebordsmodus: flaten tar ikke imot klikk, og en knapp som
    // ikke kan trykkes skal ikke vises. Hurtigstien over naas heller aldri
    // der - uten musemeldinger blir knapperaden aldri invalidert alene.
    if (!g_desktopMode) DrawButtons(ctx, hdcMem, W, IsZoomed(hwnd));

    // Overlayet tegnes UTENFOR laasen: alt det leser (overlayF, overlayHot,
    // symIdx, ivIdx) er UI-eid. Og det maa staa her, ikke i DrawChart, som
    // returnerer tidlig naar bufferet er tomt - nettopp tilstanden rett
    // etter et konfigbytte.
    DrawOverlay(ctx, hdcMem, W, H);

    BitBlt(hdcDst, 0, 0, W, H, hdcMem, 0, 0, SRCCOPY);
    ctx->bbValid = TRUE;

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
//
// Kalles fra overlayet og fra tray-menyen (fase 17). Tar ikke noe HWND:
// panelet kan vaere lukket naar valget kommer fra menyen, og
// InvalidateRect(NULL, ...) ville tegnet hele skrivebordet paa nytt.
// hit: [0, SYMBOL_COUNT) er symbol, [SYMBOL_COUNT, +INTERVAL_COUNT) intervall.
static void ApplyConfigChoice(AppContext* ctx, int hit) {
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
    ctx->histPending = FALSE;   // ny konfig: historikken begynner paa nytt
    ctx->histDone    = FALSE;
    LeaveCriticalSection(&ctx->lock);

    ctx->hoverIdx = -1;
    ctx->wmValid  = FALSE;   // vannmerket viser forrige symbol/intervall
    ctx->dispValid = FALSE;  // nytt buffer: ingenting a ease fra
    UpdatePopupTitle(ctx);   // tittellinja og oppgavelinja skal folge med
    // SetEvent staar utenfor laasen. Den er ikke PostMessage, men samme regel
    // gjelder av samme grunn: ikke hold laasen over noe som vekker den andre
    // traden.
    SetEvent(ctx->hWakeEvent);
    SaveConfig(ctx);
    if (ctx->hPopup) InvalidateRect(ctx->hPopup, NULL, FALSE);
}

// Zoom og panorering tilbake til standardutsnittet: de siste DEFAULT_VIEW
// lysene, festet til hoyre kant og fulgt live. Rorer ikke dispValid, saa
// visningen eases tilbake fra der den staar - samme mekanisme som hjulzoom.
// TogglePopup vil derimot ha snap ved aapning og setter dispValid selv.
// Vindusgeometrien er en annen sak: den eier ResetToDefaultView (Ctrl+0).
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

// Staar utsnittet der ResetView ville satt det? ESC bruker svaret til aa
// velge lag: er det allerede i standard, skjuler ESC panelet i stedet.
static BOOL ViewIsDefault(AppContext* ctx) {
    EnterCriticalSection(&ctx->lock);
    int n = ctx->candleCount, vs, vc;
    GetView(ctx, &vs, &vc);
    int want = (n < DEFAULT_VIEW) ? n : DEFAULT_VIEW;
    BOOL def = (n == 0) || (vc == want && vs + vc >= n);
    LeaveCriticalSection(&ctx->lock);
    return def;
}

// Skjuler panelet til systemstatusfeltet - eller, i et duplikat, avslutter
// prosessen. Et duplikat har ingen hovedinstans-rolle aa vende tilbake til,
// og en hale av skjulte tray-ikoner er ingen funksjon. Avslutningen gaar
// gjennom tray-menyens egen sti, saa ikonet fjernes likt i begge tilfeller.
static void HidePanel(HWND hwnd) {
    g_Ctx.hoverIdx = -1;
    g_Ctx.btnHot   = -1;
    if (g_isDuplicate) {
        ShowWindow(hwnd, SW_HIDE);
        SendMessageW(g_Ctx.hWnd, WM_COMMAND, ID_TRAY_EXIT, 0);
        return;
    }
    SaveWindowPlacement(hwnd);
    ShowWindow(hwnd, SW_HIDE);
}

// Starter en ny, isolert instans av programmet, forskjovet SPAWN_OFFSET
// ned og til hoyre. Geometri, symbol og intervall gaar paa kommandolinja -
// ikke via registret, som hovedinstansen eier og duplikater ikke skriver.
//
// Maksimert vindu: +30 fra et vindu som fyller skjermen ville lagt barnet
// halvveis utenfor. Da brukes den gjenopprettede geometrien. Ellers
// GetWindowRect, som er skjermkoordinater - rcNormalPosition er
// arbeidsomraade-koordinater, og skiller seg naar oppgavelinja staar oppe
// eller til venstre.
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

    // Kaskaden gaar tilbake til hjornet naar neste steg ville skjovet
    // knapperaden ut av arbeidsomraadet - ellers blir [ + ] etter noen klikk
    // et vindu brukeren ikke kan lukke.
    MONITORINFO mi = { sizeof(MONITORINFO) };
    if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        RECT wa = mi.rcWork;
        if (x + w > wa.right)  x = wa.left;
        if (y + h > wa.bottom) y = wa.top;
    }

    wchar_t exe[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return;

    // CreateProcessW kan skrive i kommandolinja, saa den maa ligge i et
    // skrivbart buffer - aldri en strengkonstant.
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

// Klikk paa en kontrollknapp. Egen funksjon fordi to meldinger naar hit:
// WM_LBUTTONDOWN, og WM_LBUTTONDBLCLK - med CS_DBLCLKS blir andre klikk i et
// raskt dobbeltklikk en DBLCLK, og knappene ville ellers spist det.
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
                // Mandatets "eller DPI-skalert 1280x720": OS-et eier den
                // gjenopprettede rekta, og SW_RESTORE bruker den. Men var den
                // lagret paa en skjerm som siden er koblet fra, havner vinduet
                // utenfor alt synlig. Samme sjekk som PlacePopupInitially gjor
                // ved apning.
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
                // Geometrien lagres for vi maksimerer. Ved gjenoppretting er
                // den allerede lagret.
                SaveWindowPlacement(hwnd);
                ShowWindow(hwnd, SW_MAXIMIZE);
            }
            break;
        case BTN_CLOSE:
            // WM_CLOSE, ikke DestroyWindow: den eksisterende handleren lagrer
            // geometri og skjuler til systemstatusfeltet. Tickeren er et
            // tray-program.
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            break;
    }
}

static LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1; // handteres i WM_PAINT

        // Vinduet forsvinner uten at vi ba om det: i skrivebordsmodus er
        // forelderen Explorers WorkerW, og den kan rives ned (Explorer
        // startes paa nytt). Er det vi som river ned, har WM_DESTROY i
        // WndProc allerede nullet hPopup, og da er dette en no-op.
        //
        // animRunning maa ned: timeren dode med vinduet, og StartAnim ville
        // ellers trodd at klokka fortsatt gaar paa neste flate.
        case WM_NCDESTROY:
            if (g_Ctx.hPopup == hwnd) {
                EnterCriticalSection(&g_Ctx.lock);   // traden leser hPopup
                g_Ctx.hPopup = NULL;
                LeaveCriticalSection(&g_Ctx.lock);
                g_Ctx.animRunning = FALSE;
                if (g_desktopMode) {
                    SetTimer(g_Ctx.hWnd, TIMER_EMBED_ID, EMBED_RETRY_MS, NULL);
                }
            }
            break;

        // Ingen NC-opptegning ved fokusbytte. WM_NCCALCSIZE under gjoer
        // klienten like stor som vinduet, men DefWindowProc tegner likevel den
        // klassiske WS_THICKFRAME-rammen i vindus-DC-en - altsaa OPPAA grafen,
        // 3 px dyp, i COLOR_ACTIVEBORDER (#B4B4B4) eller COLOR_INACTIVEBORDER
        // (#F4F7FC) med lyse kanter. Den blir staaende til neste fulle
        // opptegning, opptil 3 s. Maalt fra skjermen: 7,2 millioner
        // rammefargede kantpiksler over 3 fokusbytter, mot 0 med dette.
        //
        // lParam = -1 er den dokumenterte maaten aa si "ikke tegn rammen" paa.
        // DefWindowProc gjoer resten av aktiveringen som foer, i stedet for at
        // vi svarer TRUE og hopper over den helt.
        case WM_NCACTIVATE:
            return DefWindowProcW(hwnd, msg, wParam, -1);

        // Det finnes ingen NC-flate aa tegne. Ingen maalt sti tegnet noe her
        // etter rettelsen over (fokusbytte, WM_SETTEXT, storrelsesendring),
        // saa dette er et vern, ikke rettelsen.
        //
        // DWMNCRP_DISABLED er IKKE brukt: den slaar av DWM-rammen og slipper
        // den klassiske NC-tegningen til igjen. Maalt: 1 296 rammefargede
        // piksler tilbake, og forgrunnsbyttet feilet i to av tre sykluser.
        case WM_NCPAINT:
            return 0;

        // Fjerner hele den ikke-klientaktige rammen: klientflaten blir like
        // stor som vindusrektangelet, og vi tegner alt selv.
        case WM_NCCALCSIZE: {
            if (!wParam) break;
            // Maksimert vindu trenger ingen sarbehandling her -
            // WM_GETMINMAXINFO under gir OS-et eksakt arbeidsomraadet, saa
            // det finnes ikke noe overheng aa trekke fra. Maalt: uten den
            // maksimerte et WS_POPUP seg til hele SKJERMEN utvidet med
            // rammebredden (-7,-7 3854x1614 mot rcWork 0,0 3840x1552), og
            // panelet la seg over oppgavelinja.
            return 0;
        }

        // Uten OS-ramme er det vi som avgjor hva musa staar paa.
        // RESIZE_BORDER-sonene gir OS-ets egen skalering; ledig headerflate
        // gir HTCAPTION, som er det DefWindowProc trenger for aa sende
        // WM_NCLBUTTONDOWN og kjore nativ flytting med Aero Snap.
        case WM_NCHITTEST: {
            // Skrivebordsmodus: ingenting her skal ta musa. WS_EX_TRANSPARENT
            // gjor det samme for systemet; dette holder knappe- og
            // kantlogikken under unna uansett hvem som spor.
            if (g_desktopMode) return HTTRANSPARENT;
            RECT rw;
            GetWindowRect(hwnd, &rw);
            int x = GET_X_LPARAM(lParam) - rw.left;
            int y = GET_Y_LPARAM(lParam) - rw.top;
            int w = rw.right - rw.left, h = rw.bottom - rw.top;

            // Maksimert vindu skal ikke kunne skaleres i kantene - da ville
            // et klikk 2 px fra skjermkanten startet en dra-skalering av noe
            // som per definisjon fyller skjermen.
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
                // Knappene maa vaere HTCLIENT, ellers naar WM_LBUTTONDOWN
                // dem aldri: et HTCAPTION-omraade gir NC-meldinger, og
                // DefWindowProc ville startet en vindusflytting av et klikk
                // paa krysset. Rekkefolgen her ER mekanismen i mandatets
                // punkt 2 og 3 - HTCLIENT der knappene er, HTCAPTION paa
                // ledig flate.
                //
                // NCHITTEST-koordinatene er relative til VINDUET. Med rammen
                // fjernet i WM_NCCALCSIZE er klient og vindu samme
                // rektangel, saa x kan brukes rett mot ButtonLayout.
                RECT btns[BTN_COUNT];
                ButtonLayout(w, btns);
                if (ButtonHit(btns, x, y) >= 0) return HTCLIENT;
                return HTCAPTION;
            }
            return HTCLIENT;
        }

        // Pekeren under panorering. Uten denne tilbakestiller OS-et pekeren
        // til vindusklassens IDC_ARROW ved hver eneste musebevegelse, og et
        // SetCursor fra WM_MOUSEMOVE ville blitt overskrevet med en gang.
        //
        // Kun HTCLIENT og kun mens vi panorerer: kantsonene skal beholde sine
        // egne skaleringspekere, som DefWindowProc gir gratis, og headeren
        // skal ha vanlig pil slik en tittellinje har. Derfor break og ikke
        // return 0 for alt annet.
        case WM_SETCURSOR:
            if (g_Ctx.panning && LOWORD(lParam) == HTCLIENT) {
                SetCursor(g_Ctx.curPan);
                return TRUE;
            }
            break;

        case WM_PAINT:
            PaintPopup(&g_Ctx, hwnd);
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            // DPI-skalert av samme grunn som PlacePopupInitially: prosessen er
            // DPI-uvitende i dag, saa dette er 400x250, men grensen skal
            // folge med den dagen et manifest legges til.
            UINT dpi = GetDpiForWindow(hwnd);
            if (dpi == 0) dpi = 96;
            mmi->ptMinTrackSize.x = MulDiv(POPUP_MIN_W, (int)dpi, 96);
            mmi->ptMinTrackSize.y = MulDiv(POPUP_MIN_H, (int)dpi, 96);

            // Et WS_POPUP-vindu maksimerer seg til hele SKJERMEN, ikke til
            // arbeidsomraadet - og OS-et legger rammebredden utenpaa. Maalt
            // for denne blokka fantes: -7,-7 3854x1614, mot rcWork
            // 0,0 3840x1552. Panelet dekket oppgavelinja, og krysset laa 7 px
            // utenfor skjermkanten. Et WS_OVERLAPPEDWINDOW ville faatt dette
            // gratis; det gjor ikke vi, saa vi oppgir grensene selv.
            //
            // ptMaxPosition er relativ til SKJERMENS hjorne, ikke til
            // skrivebordet - derfor trekkes rcMonitor fra.
            HMONITOR hm = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(MONITORINFO) };
            if (GetMonitorInfoW(hm, &mi)) {
                mmi->ptMaxPosition.x  = mi.rcWork.left - mi.rcMonitor.left;
                mmi->ptMaxPosition.y  = mi.rcWork.top  - mi.rcMonitor.top;
                mmi->ptMaxSize.x      = mi.rcWork.right  - mi.rcWork.left;
                mmi->ptMaxSize.y      = mi.rcWork.bottom - mi.rcWork.top;
                // ptMaxTrackSize settes IKKE. Den ville klemt manuell
                // skalering til en skjerms arbeidsomraade, saa panelet ikke
                // lenger kunne strekkes over to skjermer. Det er MAKSIMERT
                // storrelse som skal folge rcWork, ikke storste tillatte
                // storrelse.
            }
            return 0;
        }

        case WM_EXITSIZEMOVE: {
            // Geometrien fanges naar brukeren slipper, ikke bare ved
            // avslutning. Da panelet var EID av hovedvinduet var det
            // allerede revet ned naar WM_DESTROY naadde dit, og registret
            // sto uten PanelWidth - maalt. Panelet er uavhengig na, saa
            // avslutningsstien virker ogsaa, men dette er fortsatt
            // oyeblikket brukeren faktisk bestemmer storrelsen.
            SaveWindowPlacement(hwnd);
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

            // Knappe-hover. Maa staa etter TrackMouseEvent-armeringen over
            // (fallgruve 13) og for overlay- og panoreringsgrenene, som
            // begge returnerer tidlig.
            //
            // Mens overlayet er apent skal ingen knapp lyse: den kan heller
            // ikke klikkes, og en lysende knapp som ikke svarer er verre enn
            // ingen. Det samme gjelder under panorering - der holder
            // chart-flaten museknappen via SetCapture, saa en dra-bevegelse
            // som passerer over headeren ville tent krysset rodt midt i
            // panoreringen, uten at det gikk an aa klikke det.
            {
                RECT btns[BTN_COUNT];
                ButtonLayout(rc.right, btns);
                int bh = (g_Ctx.overlayOpen || g_Ctx.panning)
                         ? -1 : ButtonHit(btns, mx, my);
                if (bh != g_Ctx.btnHot) {
                    g_Ctx.btnHot = bh;
                    // Kun knapperaden er skitten. PaintPopup har en hurtigsti
                    // for nettopp dette rektangelet - uten den ville
                    // invalideringen bare klippet den siste blitten, mens
                    // hele bufferet ble bygget og grafen tegnet om.
                    RECT strip;
                    ButtonStrip(rc.right, &strip);
                    InvalidateRect(hwnd, &strip, FALSE);
                }
            }

            // Sperren staar etter TrackMouseEvent-armeringen over. Returnerte
            // vi for den, sluttet WM_MOUSELEAVE a fyre og crosshairet ville
            // blitt staaende etter at musa forlot vinduet.
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
                BOOL atWall = FALSE;
                EnterCriticalSection(&g_Ctx.lock);
                // Ankeret maa kompenseres FOER det leses: en bakfylling
                // mellom forrige timer-tikk og dette museflyttet ville
                // ellers gitt ett bilde med hopp paa k lys (fase 18).
                ApplyFrontShift(&g_Ctx);
                int vs2, vc2;
                GetView(&g_Ctx, &vs2, &vc2);
                if (vc2 > 0 && g.cw > 0) {
                    double slot = (double)g.cw / (double)vc2;
                    int shift = (int)((double)(mx - g_Ctx.panAnchorX) / slot);
                    int want  = g_Ctx.panAnchorView - shift;        // dra hoyre = bakover
                    g_Ctx.viewStart = want;
                    ClampView(&g_Ctx);
                    // I veggen glir fingeren: ankeret flyttes hit, saa
                    // overskytingen ikke huskes. Uten dette ville draget
                    // etter en bakfylling (fase 18) hoppet med akkurat det
                    // brukeren dro forbi veggen foer lysene kom, og et drag
                    // tilbake fra veggen ville staatt stille like lenge.
                    if (g_Ctx.viewStart != want) {
                        g_Ctx.panAnchorView = g_Ctx.viewStart;
                        g_Ctx.panAnchorX    = mx;
                    }
                    g_Ctx.followLive =
                        (g_Ctx.viewStart + g_Ctx.viewCount >= g_Ctx.candleCount);
                    atWall = (g_Ctx.viewStart == 0);
                    // Dra-panorering eases IKKE i X. Fingeren og grafen maa
                    // henge sammen; eased dra foles treigt, ikke mykt.
                    // Y-aksen eases fortsatt - den skal gli naar nye topper
                    // og bunner kommer inn i utsnittet.
                    g_Ctx.dispStart = (double)g_Ctx.viewStart;
                    g_Ctx.dispCount = (double)((g_Ctx.viewCount > 0)
                                               ? g_Ctx.viewCount : g_Ctx.candleCount);
                    g_Ctx.hoverIdx = HitCandle(&g_Ctx, &g, mx, my);
                    g_Ctx.hoverY   = my;
                }
                LeaveCriticalSection(&g_Ctx.lock);
                if (atWall) RequestHistory(&g_Ctx);   // fase 18
                StartAnim(hwnd);   // Y-aksen kan ha nytt maal
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

            BOOL atWall = FALSE;
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
                atWall = (g_Ctx.viewStart == 0);
            }
            LeaveCriticalSection(&g_Ctx.lock);

            // Veggen (fase 18). Ogsaa zoom inn med ankeret helt til venstre
            // paa et ferskt panel lander her - ett kall paa 50 KB, ufarlig.
            if (atWall) RequestHistory(&g_Ctx);
            StartAnim(hwnd);   // maalet flyttet seg; visningen skal ease dit
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_MOUSELEAVE:
            g_Ctx.trackingMouse = FALSE;
            g_Ctx.hoverIdx      = -1;
            g_Ctx.overlayHot    = -1;   // ellers blir en rad staaende framhevet
            // Fyrer ogsaa naar pekeren gaar fra en knapp (HTCLIENT) ut i
            // ledig headerflate (HTCAPTION): den forlater klientomraadet uten
            // aa forlate vinduet. Uten dette blir knappen staaende opplyst.
            g_Ctx.btnHot        = -1;
            StartAnim(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

#ifdef TICKER_PROBE
        // Testbygg (fase 18): leser indre tilstand uten aa roere den, saa en
        // probe kan vente paa at en bakfylling har landet og sjekke at
        // utsnittet peker paa de samme lysene foer og etter. Bygges bare med
        // /DTICKER_PROBE; produksjonsbygget har ikke meldingen.
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
                case 8:  r = (LRESULT)(g_Ctx.dispStart * 1000.0); break;   // tusendels lys
                case 9:  r = g_Ctx.netFailures; break;
                case 10: r = g_Ctx.followLive; break;
                case 11: r = g_Ctx.hoverIdx; break;
                default: break;
            }
            LeaveCriticalSection(&g_Ctx.lock);
            return r;
        }
#endif

        // Animasjonsklokka. Driver alt tidsavhengig fra ett sted, og dor
        // naar alt har satt seg - i hvile gaar det ingen timer.
        case WM_TIMER:
            if (wParam == TIMER_ANIM_ID) {
                ULONGLONG now = GetTickCount64();
                double dt = (double)(now - g_Ctx.lastAnimTick);
                g_Ctx.lastAnimTick = now;

                BOOL redraw  = FALSE;
                BOOL settled = TRUE;

                // Overlay-fade.
                double ovlTarget = g_Ctx.overlayOpen ? 255.0 : 0.0;
                if (g_Ctx.overlayF != ovlTarget) {
                    double before = g_Ctx.overlayF;
                    g_Ctx.overlayF = AnimStep(g_Ctx.overlayF, ovlTarget, dt,
                                              ANIM_TAU_FADE, 0.5);
                    if ((int)(before + 0.5) != (int)(g_Ctx.overlayF + 0.5)) redraw = TRUE;
                    if (g_Ctx.overlayF != ovlTarget) settled = FALSE;
                }

                // View- og Y-akse-easing. Maalet leses under laas; selve
                // interpolasjonen skjer utenfor, paa UI-eide felter.
                //
                // Terskelen er en kvart piksel omregnet til den enheten som
                // eases - derfor trengs chart-geometrien her.
                {
                    RECT rcE;
                    GetClientRect(hwnd, &rcE);
                    ChartRect gE = ChartGeometry(rcE.right, rcE.bottom);

                    int tvs = 0, tvc = 0, tn = 0;
                    double tMin = 0.0, tMax = 1.0;
                    EnterCriticalSection(&g_Ctx.lock);
                    ApplyFrontShift(&g_Ctx);
                    tn = g_Ctx.candleCount;
                    GetView(&g_Ctx, &tvs, &tvc);
                    if (tn > 0 && tvc > 0) PriceRange(&g_Ctx, tvs, tvc, &tMin, &tMax);
                    LeaveCriticalSection(&g_Ctx.lock);

                    if (tn > 0 && tvc > 0 && g_Ctx.dispValid &&
                        gE.cw > 0 && gE.ch > 0) {
                        double dc = (g_Ctx.dispCount > 1.0) ? g_Ctx.dispCount : 1.0;
                        double snapX = SNAP_PX * dc / (double)gE.cw;
                        double snapY = SNAP_PX * (tMax - tMin) / (double)gE.ch;
                        if (snapX <= 0.0) snapX = 1e-9;
                        if (snapY <= 0.0) snapY = 1e-9;

                        struct { double* v; double t; double snap; } eases[4] = {
                            { &g_Ctx.dispStart, (double)tvs, snapX },
                            { &g_Ctx.dispCount, (double)tvc, snapX },
                            { &g_Ctx.dispMin,   tMin,        snapY },
                            { &g_Ctx.dispMax,   tMax,        snapY },
                        };
                        for (int e = 0; e < 4; ++e) {
                            if (*eases[e].v == eases[e].t) continue;
                            *eases[e].v = AnimStep(*eases[e].v, eases[e].t, dt,
                                                   ANIM_TAU_VIEW, eases[e].snap);
                            redraw = TRUE;
                            if (*eases[e].v != eases[e].t) settled = FALSE;
                        }
                    }
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

        // Dobbeltklikk paa grafen eller prisaksen nullstiller zoom og
        // panorering. Krever CS_DBLCLKS paa vindusklassen - uten den kommer
        // meldingen aldri. Alt som ikke er graf eller akse - knappene, og
        // hele panelet mens overlayet er apent - faller gjennom til
        // WM_LBUTTONDOWN, slik at andre klikk i et raskt dobbeltklikk
        // oppforer seg som for CS_DBLCLKS kom inn.
        //
        // Forste klikk har allerede startet en panorering, men WM_LBUTTONUP
        // har sluppet den igjen for DBLCLK kommer. Ledig headerflate er
        // HTCAPTION og gir WM_NCLBUTTONDBLCLK (maksimer) - den naar ikke hit.
        case WM_LBUTTONDBLCLK: {
            if (!g_Ctx.overlayOpen) {
                RECT rcD;
                GetClientRect(hwnd, &rcD);
                ChartRect gd = ChartGeometry(rcD.right, rcD.bottom);
                int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
                // [g.left, W): grafen og aksemargen til hoyre for den.
                if (mx >= gd.left && mx < rcD.right && my >= gd.top && my <= gd.bottom) {
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
            // Staar forst med vilje: mens overlayet er apent skal ingen del
            // av panelet ta klikket - heller ikke krysset. Forste klikk
            // lukker overlayet, neste lukker panelet.
            if (g_Ctx.overlayOpen) {
                RECT rcO;
                GetClientRect(hwnd, &rcO);
                OverlayRects orr;
                OverlayLayout(rcO.right, rcO.bottom, &orr);
                int hit = OverlayHit(&orr, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                if (hit >= 0) ApplyConfigChoice(&g_Ctx, hit);
                g_Ctx.overlayOpen = FALSE;   // klikk utenfor lukker uten endring
                g_Ctx.overlayHot  = -1;
                StartAnim(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            RECT rc;
            GetClientRect(hwnd, &rc);
            int dx = GET_X_LPARAM(lParam), dy = GET_Y_LPARAM(lParam);
            // Knappene. Etter overlayet - forste klikk lukker overlayet, ogsaa
            // naar det treffer en knapp - og for panoreringen, som uansett
            // bare gjelder chart-flaten.
            {
                RECT btns[BTN_COUNT];
                ButtonLayout(rc.right, btns);
                int bh = ButtonHit(btns, dx, dy);
                if (bh >= 0) {
                    OnButtonClick(hwnd, bh);
                    return 0;
                }
            }

            ChartRect gg = ChartGeometry(rc.right, rc.bottom);
            if (dx >= gg.left && dx < gg.right && dy >= gg.top && dy <= gg.bottom) {
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
                // WM_SETCURSOR fyrer forst ved neste musebevegelse. Uten
                // dette kallet viser forste bilde av draget fortsatt pil.
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
                // Samme grunn som ved start, motsatt vei: uten dette viser
                // forste bilde etter slipp fortsatt firevegskrysset.
                SetCursor(g_Ctx.curArrow);
            }
            return 0;

        case WM_KEYDOWN: {
            BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            // Ctrl+0: tilbake til fabrikkgeometri, sentrert paa den skjermen
            // vinduet staar paa. Vindusgeometri, ikke zoom - se R under.
            if (wParam == '0' && ctrl) {
                ResetToDefaultView(hwnd);
                return 0;
            }
            // Kontrollknappene fra tastaturet (fase 19): Ctrl+N = [ + ],
            // Ctrl+M = minimer, F11 = maksimer/gjenopprett, Ctrl+W = lukk.
            // Alt+F4 gaar allerede gjennom DefWindowProc til WM_CLOSE, ogsaa
            // uten WS_SYSMENU - maalt. Alle fire gaar gjennom OnButtonClick,
            // saa tast og klikk deler samme sti: geometrien lagres foer
            // maksimering, en gjenopprettet rekt utenfor alt synlig fanges,
            // og et duplikat avsluttes av lukking. Ikke midt i en panorering:
            // en minimering under drag ville hoppet over WM_LBUTTONUP, som
            // slipper capture og setter pekeren tilbake. Ikke i
            // skrivebordsmodus: flaten er et barn av WorkerW og faar aldri
            // tastaturfokus, men SW_MINIMIZE paa den skal ikke engang vaere
            // mulig i teorien.
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
            // ESC er lagvis, innerst forst: lukk overlayet, nullstill
            // utsnittet, skjul panelet. Ingen av lagene forsvinner for et
            // annet - den som vil skjule et zoomet panel trykker to ganger.
            if (wParam == VK_ESCAPE && g_Ctx.overlayOpen) {
                g_Ctx.overlayOpen = FALSE;
                g_Ctx.overlayHot  = -1;
                StartAnim(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            // R: zoom og panorering tilbake til standardutsnittet. Ikke mens
            // overlayet er apent - der eier det tastaturet, som hjulet.
            // VK-kodene for bokstaver er de store ASCII-tegnene.
            if ((wParam == 'R' && !ctrl && !g_Ctx.overlayOpen) ||
                (wParam == VK_ESCAPE && !ViewIsDefault(&g_Ctx))) {
                ResetView(&g_Ctx);
                g_Ctx.hoverIdx = -1;
                StartAnim(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                // Krysset i headeren er det opplagte alternativet, men
                // tastatursnarveien er billig a beholde.
                HidePanel(hwnd);
            }
            return 0;
        }

        case WM_CLOSE:
            // Lukkeknappen skjuler til systemstatusfeltet. Tickeren er et
            // tray-program; "Avslutt Ticker" i tray-menyen avslutter det.
            // Posisjonen lagres for vi forsvinner. Et duplikat avsluttes.
            HidePanel(hwnd);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Plasser popupen i hjornet av arbeidsomraadet naermest musepekeren.
// Handterer oppgavelinje i alle kanter + flere skjermer.


// Plasserer vinduet forste gang det opprettes: lagret posisjon hvis den
// finnes og fortsatt er synlig, ellers sentrert.
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

// Windows nekter SetForegroundWindow fra en prosess som ikke eier
// forgrunnen. Uten dette blir vinduet vist, men aldri aktivert - og
// tastaturfokus havner ingen steder, saa Ctrl+0 og ESC ikke naar frem.
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

// --- Skrivebordsmodus -------------------------------------------------------

// Udokumentert: faar Progman til aa lage WorkerW-vinduet som skrivebordets
// tapetovergang tegnes i.
#define PROGMAN_SPAWN_WORKERW 0x052C

// Klassisk vindustre (for Windows 11 24H2): WorkerW-en vi vil ha er et
// TOPPNIVAvindu, soesken rett etter det vinduet som har SHELLDLL_DefView
// (ikonene) i seg.
static BOOL CALLBACK FindLegacyWorkerW(HWND top, LPARAM lParam) {
    if (FindWindowExW(top, NULL, L"SHELLDLL_DefView", NULL)) {
        *(HWND*)lParam = FindWindowExW(NULL, top, L"WorkerW", NULL);
        return FALSE;
    }
    return TRUE;
}

// Finner WorkerW-en flaten skal ligge i, og lager den om noedvendig.
//
// Maalt paa 26100 (24H2): foer meldingen har Progman ett barn,
// SHELLDLL_DefView. Etter meldingen har den to - DefView oeverst og WorkerW
// under - og det finnes INGEN toppnivaa-WorkerW med DefView i seg. Den
// klassiske traverseringen finner altsaa ingenting der, og proves bare naar
// Progman ikke har WorkerW som barn.
//
// Begge meldingsvariantene sendes: (0xD, 1) er den nyere formen, (0, 0) den
// klassiske. Kombinasjonen er det som er maalt; en av dem alene er ikke.
// Tidsgrense 1 s: en Explorer som henger skal ikke fryse UI-traaden var.
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

// Gjor et nyopprettet WS_POPUP om til skrivebordsflaten.
//
// Rekkefolgen er maalt, ikke valgt. Et vanlig barnevindu under WorkerW blir
// ALDRI synlig paa 24H2 - forelderen har ingen overflate aa tegne i (Progman
// har WS_EX_NOREDIRECTIONBITMAP). Et lagdelt barn faar sin egen. Men:
//   - WS_EX_LAYERED paa et barn krever supportedOS Windows 8+ i manifestet.
//     Uten avvises stilen stille (exstil 0, 0 av 41 punkter synlige).
//   - SetLayeredWindowAttributes maa kalles ETTER SetParent. Satt mens
//     vinduet var toppnivaa, overlever stilen, men flaten vises ikke.
// Med begge paa plass: 28 av 41 skrivebordspunkter fikk flatens farge, og
// resten var ikoner.
//
// WS_EX_TRANSPARENT slipper musa gjennom. Flaten ligger uansett under
// ikonenes SysListView32, men lagdelt maa den vaere, saa det koster ingenting.
static BOOL AttachToDesktop(HWND hwnd) {
    HWND ww = FindDesktopWorkerW();
    if (!ww) return FALSE;

    LONG_PTR st = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, (st & ~(LONG_PTR)WS_POPUP) | WS_CHILD);
    if (!SetParent(hwnd, ww)) return FALSE;

    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED | WS_EX_TRANSPARENT);
    if (!SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA)) return FALSE;

    // Primaerskjermen staar i 0,0 i skjermkoordinater, men WorkerW dekker
    // hele den virtuelle skjermen og har sitt origo i dens hjorne. Paa et
    // oppsett med en skjerm til venstre for den primaere er de ikke det
    // samme.
    POINT org = { 0, 0 };
    MapWindowPoints(NULL, ww, &org, 1);
    SetWindowPos(hwnd, HWND_BOTTOM, org.x, org.y,
                 GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                 SWP_NOACTIVATE);
    return TRUE;
}


// Tray-klikk. Med et vanlig vindu er den forventede oppforselen: er det
// fremme og aktivt, skjul det; ellers vis det og gi det fokus. Minimert
// vindu gjenopprettes.
static void TogglePopup(AppContext* ctx, HINSTANCE hInst) {
    if (ctx->hPopup && IsWindowVisible(ctx->hPopup)) {
        if (!IsIconic(ctx->hPopup) && GetForegroundWindow() == ctx->hPopup) {
            ctx->hoverIdx = -1;
            ctx->overlayOpen = FALSE;
            ctx->overlayF    = 0.0;
            ctx->overlayHot  = -1;
            ctx->btnHot      = -1;
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
        // Rammelost vindu i TradingView/Bloomberg-tradisjon. WS_THICKFRAME
        // beholder OS-ets egen skalering; hele den synlige rammen fjernes i
        // WM_NCCALCSIZE. WS_MINIMIZEBOX og WS_MAXIMIZEBOX tegner ingenting
        // uten tittellinje, men de er det som lar Win+Pil, Aero Snap og
        // gjenoppretting fra oppgavelinje-miniatyren virke.
        //
        // 0,0 og ikke CW_USEDEFAULT: CW_USEDEFAULT er udefinert for WS_POPUP
        // og kan legge vinduet utenfor skjermen. PlacePopupInitially setter
        // riktig geometri like etter.
        //
        // Ingen WS_EX_TOOLWINDOW og ingen eier, saa vinduet beholder knappen
        // i oppgavelinja. Ingen WS_EX_TOPMOST.
        //
        // Skrivebordsmodus: WS_POPUP alene. Ingen ramme aa skalere i, og
        // ingen minimer/maksimer - flaten er skrivebordet. AttachToDesktop
        // gjor den om til WS_CHILD for den vises, og foer hPopup publiseres:
        // traden skal ikke se et vindu som kanskje rives ned igjen.
        DWORD style = g_desktopMode
            ? WS_POPUP
            : (WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);

        // Skrivebordsflaten lages per-monitor-bevisst, resten av prosessen er
        // fortsatt DPI-uvitende. Maalt ved 150 %: uten dette ga
        // SM_CXSCREEN/SM_CYSCREEN virtualiserte 2560x1067, og flaten dekket
        // bare det oevre venstre hjoernet av en WorkerW paa 3840x1600 fysiske
        // piksler. Et vindu laget i denne konteksten beholder den, og
        // WM_PAINT kjoeres i vinduets kontekst - GetClientRect gir da fysiske
        // piksler, og layouten tegnes 1:1 i raa piksler som ved 100 %.
        // Konteksten settes tilbake straks flaten er plassert.
        DPI_AWARENESS_CONTEXT prevDpi = g_desktopMode
            ? SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
            : NULL;
        HWND hp = CreateWindowExW(
            0,
            L"BTCPopupClass", L"BTC Chart",
            style,
            0, 0, POPUP_W, POPUP_H,
            NULL, NULL, hInst, NULL);
        BOOL attached = hp && g_desktopMode && AttachToDesktop(hp);
        if (prevDpi) SetThreadDpiAwarenessContext(prevDpi);
        if (!hp) return;

        if (g_desktopMode) {
            if (!attached) {
                // Ingen WorkerW (Explorer starter, eller kjorer ikke). hPopup
                // er ikke satt, saa WM_NCDESTROY lar timeren vaere - den
                // settes her.
                DestroyWindow(hp);
                SetTimer(ctx->hWnd, TIMER_EMBED_ID, EMBED_RETRY_MS, NULL);
                return;
            }
        } else {
            SquareCorners(hp);
        }

        EnterCriticalSection(&ctx->lock);   // traden leser hPopup
        ctx->hPopup = hp;
        LeaveCriticalSection(&ctx->lock);
        created = TRUE;
    }

    ResetView(ctx);   // utsnittet settes pa nytt; bufferet beholdes

    ctx->panning   = FALSE;
    ctx->hoverIdx  = -1;
    ctx->overlayOpen = FALSE;   // overlayet skal aldri sta apent ved apning
    ctx->overlayF    = 0.0;
    ctx->overlayHot  = -1;
    // btnHot nullstilles av samme grunn som overlayHot over: tilstanden er
    // hover, og hover eier ingenting naar vinduet forsvinner eller aapnes paa
    // nytt. WM_MOUSELEAVE fyrer riktignok paa SW_HIDE og SW_MINIMIZE - maalt,
    // ingen av de to stiene etterlot en opplyst knapp - men det er en
    // meldingsrekkefolge vi ikke styrer, og en rod lukkeknapp som henger igjen
    // ved gjenapning er ikke verdt aa vaere avhengig av den.
    ctx->btnHot      = -1;
    ctx->dispValid   = FALSE;   // panelet skal apne ferdig, ikke gli paa plass

    UpdatePopupTitle(ctx);
    if (g_desktopMode) {
        // Geometrien satte AttachToDesktop. Ingen aktivering og ingen
        // forgrunn: et barn av Explorers WorkerW skal aldri ta fokus fra
        // det brukeren holder paa med.
        ShowWindow(ctx->hPopup, SW_SHOWNA);
    } else {
        if (created) PlacePopupInitially(ctx->hPopup);
        ShowWindow(ctx->hPopup, SW_SHOW);
        ForceForeground(ctx->hPopup);
    }
    SetEvent(ctx->hWakeEvent);   // hent lys na, ikke om opptil 3 sekunder

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

// Bytter mellom panel og skrivebordsflate mens prosessen kjoerer (fase 12).
//
// Vinduet LAGES PAA NYTT, det flyttes ikke med SetParent. DPI-konteksten
// settes idet et vindu lages og kan ikke endres: skrivebordsflaten maa lages
// per-monitor-bevisst (ellers dekker den en firedel av skjermen ved 150 %,
// se TogglePopup), og panelet DPI-uvitende. Et flyttet vindu ville haatt feil
// kontekst i den ene modusen. TogglePopup lager allerede begge riktig, og
// alle g_desktopMode-grenene gjelder et nytt vindu uten mer arbeid.
//
// Det som overlever: candles[], utsnittet, nettverkstraaden, GDI-objektene,
// dobbeltbufferet og vannmerke-cachen. De to siste bygges paa nytt av seg
// selv hvis storrelsen er en annen, og det er den nesten alltid.
static void SetDesktopMode(AppContext* ctx, HWND hWnd, HINSTANCE hInst, BOOL on) {
    if (g_isDuplicate || on == g_desktopMode) return;

    // Timeren prover aa bygge en skrivebordsflate som mangler. Den skal ikke
    // fyre etter at vi har gaatt tilbake til panelet.
    KillTimer(hWnd, TIMER_EMBED_ID);

    if (ctx->hPopup) {
        HWND hp = ctx->hPopup;
        // Lagres FOER flagget endres: SaveGeometry gjoer ingenting i
        // skrivebordsmodus.
        if (!g_desktopMode) SaveWindowPlacement(hp);
        if (GetCapture() == hp) ReleaseCapture();

        // Samme rekkefolge som WM_DESTROY i WndProc: hPopup nulles under
        // laas FOER vinduet rives. Da er WM_NCDESTROY en no-op og starter
        // ikke gjenoppbyggingstimeren.
        EnterCriticalSection(&ctx->lock);
        ctx->hPopup = NULL;
        LeaveCriticalSection(&ctx->lock);
        DestroyWindow(hp);
    }

    // Tilstand som hang paa det gamle vinduet. Timeren doede med det;
    // TrackMouseEvent er bestilt for et vindu som ikke finnes, og uten
    // nullstillingen ville det nye panelet aldri bestilt WM_MOUSELEAVE.
    ctx->animRunning   = FALSE;
    ctx->trackingMouse = FALSE;
    ctx->panning       = FALSE;
    ctx->bbValid       = FALSE;
    // Vannmerket er noklet paa (W, H, symIdx, ivIdx), ikke paa modus, og
    // cachen ligger i ctx - den overlever at vinduet lages paa nytt. Etter
    // fase 14 avhenger plasseringen av geometrien, som avhenger av modus.
    ctx->wmValid       = FALSE;

    g_desktopMode = on;
    SaveDesktopMode(on);

    TogglePopup(ctx, hInst);
}

// Undermenyene for symbol og intervall (fase 17): ett punkt per tabellrad og
// radiohake paa den valgte. Hengt paa hovedmenyen med MF_POPUP eies de av
// den, saa DestroyMenu paa hovedmenyen river dem ned - ingen nye haandtak i
// hvile. Etikettene er tabellenes label, samme tekst som overlayet.
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

// Tray-menyen. Egen funksjon saa hake og innhold kan testes uten et
// tray-ikon.
//
//       Symbol            >   (o) BTC/USDT  ( ) ETH/USDT  ...
//       Intervall         >   (o) 1m  ( ) 5m  ...
//   ---------------------------
//   [x] Skrivebordsmodus
//       Standardvisning   Ctrl+0     (graa i skrivebordsmodus)
//   ---------------------------
//   [x] Start ved paalogging
//   ---------------------------
//       Avslutt Ticker
//
// "Standardvisning" er graa, ikke borte, i skrivebordsmodus: den ville gjort
// flaten om til et 1280x720-vindu inne i WorkerW. Et duplikat faar hverken
// modusvalget eller autostart - det eier ikke registret og avsluttes naar
// panelet lukkes. Symbol og intervall faar det derimot: overlayet lar det
// alt bytte sin egen visning, og SaveConfig hopper over duplikater selv.
// Haken for autostart leses fra Run-nokkelen hver gang.
static HMENU BuildTrayMenu(void) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return NULL;
    {
        HMENU hSym = BuildSymbolMenu();
        HMENU hIv  = BuildIntervalMenu();
        if (hSym) AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSym, L"Symbol");
        if (hIv)  AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hIv,  L"Intervall");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    }
    if (!g_isDuplicate) {
        AppendMenuW(hMenu, MF_STRING | (g_desktopMode ? MF_CHECKED : MF_UNCHECKED),
                    ID_TRAY_DESKTOP, L"Skrivebordsmodus");
    }
    AppendMenuW(hMenu, MF_STRING | (g_desktopMode ? MF_GRAYED : MF_ENABLED),
                ID_TRAY_RESET, L"Standardvisning	Ctrl+0");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    if (!g_isDuplicate) {
        // \x00e5 er aa: fila er ren ASCII, og cl leser den som CP1252.
        AppendMenuW(hMenu, MF_STRING | (AutostartPresent() ? MF_CHECKED : MF_UNCHECKED),
                    IDM_TOGGLE_AUTOSTART, L"Start ved p\x00e5" L"logging");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    }
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Avslutt Ticker");
    return hMenu;
}

// ---------------------------------------------------------------------------

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TRAYICON:
            // Skrivebordsmodus: flaten skal ikke skjules eller faa fokus, saa
            // venstreklikk gjoer ingenting der. Menyen bygges paa nytt ved
            // hvert hoyreklikk, saa haken alltid viser gjeldende modus.
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
            {
                // Symbol og intervall (fase 17). Omraadesjekk foerst: en postet
                // ID utenfor tabellene er en stille no-op, ikke en indeks.
                // Kodingen av hit er den samme som OverlayHit bruker.
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
                // Graa i menyen i skrivebordsmodus; sperres her ogsaa, for en
                // postet melding bryr seg ikke om menyen.
                if (g_desktopMode) return 0;
                // Finnes ikke vinduet enda, lag det forst - ellers ville
                // menypunktet vaert en stille no-op.
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

            // Gjenopprettet forbindelse nullstiller telleren (fase 19). Uten
            // dette sto forrige frakoblings siste sekundtall igjen, og en ny
            // frakobling hoppet over en opptegning naar tallet tilfeldigvis
            // var det samme. Telleren starter paa STALE_AFTER / 1000 = 9, saa
            // 0 er aldri et ekte sekundtall.
            if (!stale) g_Ctx.staleSecsShown = 0;

            UpdateIcon(&g_Ctx, price, stale);
            if (g_Ctx.hPopup && IsWindowVisible(g_Ctx.hPopup)) {
                // Klokka maa ga mens vi er frakoblet, ellers fryser
                // sekundtelleren i undertittelen.
                // Nye lys kan flytte Y-maalet, og frakoblet maa telleren ga.
                StartAnim(g_Ctx.hPopup);
                InvalidateRect(g_Ctx.hPopup, NULL, FALSE);
            }
            return 0;
        }

        case WM_DESTROY:
            SaveConfig(&g_Ctx);
            if (g_Ctx.hPopup) {
                SaveWindowPlacement(g_Ctx.hPopup);
                // Panelet er IKKE eid av hovedvinduet lenger - eierskap ville
                // fjernet knappen i oppgavelinja. Da river ikke Windows det
                // ned for oss, saa vi gjor det selv.
                HWND hp = g_Ctx.hPopup;
                // hPopup er i laasedomenet, og arbeidertraden lever fortsatt
                // her - den stoppes forst etter meldingslokka.
                EnterCriticalSection(&g_Ctx.lock);
                g_Ctx.hPopup = NULL;
                LeaveCriticalSection(&g_Ctx.lock);
                DestroyWindow(hp);
            }
            PostQuitMessage(0);
            break;

        // Skrivebordsmodus uten flate: WorkerW fantes ikke, eller Explorer
        // rev den ned. Proeves til den sitter; TogglePopup setter timeren
        // paa nytt selv om det feiler igjen.
        case WM_TIMER:
            if (wParam == TIMER_EMBED_ID) {
                KillTimer(hwnd, TIMER_EMBED_ID);
                if (g_desktopMode && !g_Ctx.hPopup) {
                    TogglePopup(&g_Ctx, (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
                }
                return 0;
            }
            break;

        default:
            // Explorer er startet paa nytt. Ikonet er borte fra
            // systemstatusfeltet. I skrivebordsmodus har Windows allerede revet
            // ned flaten sammen med den gamle WorkerW-en (maalt: borte innen
            // 20 ms), og WM_NCDESTROY har startet timeren.
            //
            // Timeren kan dermed ha rukket aa bygge en ny flate foer denne
            // meldingen kommer: ny flate etter 1,1 s, TaskbarCreated etter
            // ~1,6 s. Da ble den ferske flaten revet ned og bygget paa nytt,
            // og skrivebordet sto uten graf i et sekund. Rives derfor bare
            // ned hvis den IKKE sitter i dagens WorkerW.
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

    // Ingen single-instance-mutex lenger: [ + ] starter nettopp en instans
    // til. Hver prosess har sin egen arbeidertraad, sitt eget tray-ikon og
    // sine egne vindusklasser (klasser er per prosess, saa navnene kolliderer
    // ikke).

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
    // Maalt med GetGlyphOutlineW(GGO_METRICS) paa '0': Lucida Console em 15
    // gir 11 px sifferhoyde, 9 px tegnbredde og tmHeight 15. Consolas hopper
    // fra 10 til 12 px (em 16 -> 17), Cascadia Mono em 16 gir 11 px men
    // tmHeight 21, som ikke faar plass i tidsaksens 18 px.
    g_Ctx.hFontAxis = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN,
                                  L"Lucida Console");
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
    pwc.style         = CS_DBLCLKS;   // dobbeltklikk nullstiller zoom og panorering
    RegisterClassW(&pwc);

    // Kringkastes til toppnivaavinduer naar Explorer har startet paa nytt.
    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    g_Ctx.hWnd =CreateWindowExW(0, wc.lpszClassName, L"BTC Core Engine", 0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

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
    g_Ctx.penCross  = CreatePen(PS_DOT,   1, CLR_CROSS);
    g_Ctx.penBtn      = CreatePen(PS_SOLID, 1, CLR_DIM);
    g_Ctx.penBtnHot   = CreatePen(PS_SOLID, 1, CLR_TEXT);
    g_Ctx.penBtnWhite = CreatePen(PS_SOLID, 1, CLR_BTNHOT);
    g_Ctx.brClose     = CreateSolidBrush(CLR_CLOSEHOT);
    g_Ctx.curArrow    = LoadCursorW(NULL, IDC_ARROW);
    g_Ctx.curPan      = LoadCursorW(NULL, IDC_SIZEALL);
    g_Ctx.btnHot      = -1;
    // Stiplet, ikke prikket: holder siste-pris-linja visuelt atskilt fra
    // baade rutenettet (heltrukket, dempet) og traadkorset (prikket).
    g_Ctx.penLastUp   = CreatePen(PS_DASH, 1, CLR_UP);
    g_Ctx.penLastDown = CreatePen(PS_DASH, 1, CLR_DOWN);
    g_Ctx.brBg      = CreateSolidBrush(CLR_BG);
    g_Ctx.brBox     = CreateSolidBrush(CLR_BOX);
    g_Ctx.brBoxEdge = CreateSolidBrush(CLR_BOXEDGE);

    // Arbeidertraden startes forst naar vinduet og ikonet finnes, siden
    // den poster meldinger til hWnd med en gang.
    // MA staa for CreateThread: forste henting skal gaa mot riktig par, og
    // vannmerket skal vaere korrekt fra forste bilde.
    LoadConfig(&g_Ctx, &g_savedPanelX, &g_savedPanelY,
               &g_savedPanelW, &g_savedPanelH);

    // Duplikat: "--dup x y w h sym iv", skrevet av SpawnInstance. Overstyrer
    // det LoadConfig leste, med samme grenser - en haandskrevet kommandolinje
    // skal ikke kunne indeksere utenfor tabellene. Feiler sjekken, starter
    // vi som vanlig hovedinstans i stedet for aa gjette.
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        // Skrivebordsmodus tar ingen argumenter og kan ikke kombineres med
        // --dup: et duplikat startes fra [ + ], som ikke finnes her.
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
    // Uten --desktop-mode avgjoer registret: tray-menyen husker sist valgte
    // modus (fase 12). Flagget vinner for denne kjoeringen, og et duplikat
    // er alltid et panel.
    if (!g_desktopMode && !g_isDuplicate) g_desktopMode = LoadDesktopMode();

    InitializeCriticalSection(&g_Ctx.lock);
    g_Ctx.hStopEvent = CreateEventW(NULL, TRUE,  FALSE, NULL);  // manuell reset
    g_Ctx.hWakeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);  // auto reset
    g_Ctx.hThread    = CreateThread(NULL, 0, NetworkThread, &g_Ctx, 0, NULL);

    // Et duplikat er startet fra et klikk og skal vise seg med en gang - en
    // hovedinstans starter i systemstatusfeltet. Etter laasen og hendelsene:
    // TogglePopup gaar inn i laasen og vekker traden. Skrivebordsflaten
    // likesaa - den finnes bare mens den vises.
    if (g_isDuplicate || g_desktopMode) TogglePopup(&g_Ctx, hInstance);

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

    // Bufferet forst: fonter, penner og pensler fra siste bilde kan staa
    // valgt inn i DC-en, og DeleteObject paa et valgt objekt feiler stille.
    FreeBackBuffer(&g_Ctx);

    if (g_Ctx.nid.hIcon) DestroyIcon(g_Ctx.nid.hIcon);
    if (g_Ctx.hFontPill) DeleteObject(g_Ctx.hFontPill);
    if (g_Ctx.hFontBig) DeleteObject(g_Ctx.hFontBig);
    if (g_Ctx.hFontSmall) DeleteObject(g_Ctx.hFontSmall);
    if (g_Ctx.hFontAxis) DeleteObject(g_Ctx.hFontAxis);

    DeleteObject(g_Ctx.penGrid);  DeleteObject(g_Ctx.penCross);
    DeleteObject(g_Ctx.brBg);     DeleteObject(g_Ctx.brBox);
    DeleteObject(g_Ctx.brBoxEdge);
    DeleteObject(g_Ctx.penLastUp);   DeleteObject(g_Ctx.penLastDown);
    DeleteObject(g_Ctx.penBtn);      DeleteObject(g_Ctx.penBtnHot);
    DeleteObject(g_Ctx.penBtnWhite); DeleteObject(g_Ctx.brClose);

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

    return (int)msg.wParam;
}
