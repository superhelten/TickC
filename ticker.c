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

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winhttp.lib")
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
#define KLINE_MS         60000 // ett 1m-lys i millisekunder
#define MIN_VIEW         8     // minste antall synlige lys ved full zoom
#define ZOOM_STEP        1.2   // per musehjul-hakk
#define REOPEN_GUARD_MS  250   // hindrer at klikk-for-aa-lukke aapner igjen med en gang
#define SHOW_GRACE_MS    400   // ignorer fokustap rett etter apning
#define TIMER_FADE_ID    2
#define FADE_INTERVAL    16    // ~60 fps
#define FADE_STEP        36    // 255/36 = 8 steg = ~130ms inn/ut
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
    int  chrome;

    // --- Arbeidertrad ---
    // Laasen dekker candles[], candleCount, viewStart, viewCount,
    // followLive, lastPrice og hPopup. Alt annet rores kun av UI-traden.
    CRITICAL_SECTION lock;
    HANDLE hThread;
    HANDLE hStopEvent;   // manuell reset: signaliserer avslutning
    HANDLE hWakeEvent;   // auto reset: hent NA (panelet ble apnet)

    // --- Bufrede GDI-objekter ---
    // Faste farger lages en gang ved oppstart i stedet for 16 ganger
    // per opptegning.
    HPEN   penGrid, penUp, penDown, penCross;
    HBRUSH brBg, brUp, brDown, brBox, brBoxEdge;

    // Blandede farger avhenger av fade-nivaet, sa de bygges bare naar
    // (chrome, closeHot) faktisk endrer seg - ikke hvert bilde.
    int    cacheChrome;
    BOOL   cacheCloseHot;
    BOOL   cacheValid;
    HPEN   penDim, penX;
    HBRUSH brDim, brEdge, brHdr, brClose;
} AppContext;

static AppContext g_Ctx;
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
        in[0].openTime > ctx->candles[ctx->candleCount - 1].openTime + 2 * KLINE_MS) {
        ctx->candleCount = 0;
        ctx->viewStart   = 0;
        ctx->viewCount   = 0;
        ctx->followLive  = TRUE;
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

static HICON RenderMicroFontIcon(const char* str) {
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
                pixels[py * width + px] = 0xFF00FF66;
            }
        } else if (glyphIdx != IDX_SPACE) {
            for (int r = 0; r < GLYPH_H; ++r) {
                unsigned char row = FONT_4X9[glyphIdx][r];
                for (int c = 0; c < GLYPH_W; ++c) {
                    if ((row >> (GLYPH_W - 1 - c)) & 1) {
                        int px = cx + c, py = startY + r;
                        if (px >= 0 && px < width && py >= 0 && py < height) {
                            pixels[py * width + px] = 0xFF00FF66; // Neon gronn
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

static void WorkerFetchKlines(AppContext* ctx) {
    BOOL seed;
    EnterCriticalSection(&ctx->lock);
    seed = (ctx->candleCount == 0);
    if (!seed) {
        long long lastT = ctx->candles[ctx->candleCount - 1].openTime;
        if (NowUnixMs() - lastT > 5 * KLINE_MS) seed = TRUE;
    }
    LeaveCriticalSection(&ctx->lock);

    const wchar_t* path = seed
        ? L"/api/v3/klines?symbol=BTCUSDT&interval=1m&limit=300"
        : L"/api/v3/klines?symbol=BTCUSDT&interval=1m&limit=3";

    // Selve hentingen skjer UTEN laas - den kan ta hundrevis av
    // millisekunder, og UI-traden skal kunne tegne hele tiden.
    if (!HttpGet(ctx, path, s_httpBuf, (DWORD)sizeof(s_httpBuf))) return;

    int n = ParseKlines(s_httpBuf, s_incoming, SEED_COUNT);
    if (n <= 0) return;

    EnterCriticalSection(&ctx->lock);
    MergeCandles(ctx, s_incoming, n);
    if (ctx->candleCount > 0) {
        ctx->lastPrice = ctx->candles[ctx->candleCount - 1].close;
        if (ctx->viewCount <= 0) {
            int vc = (ctx->candleCount < DEFAULT_VIEW) ? ctx->candleCount : DEFAULT_VIEW;
            ctx->viewCount  = vc;
            ctx->viewStart  = ctx->candleCount - vc;
            ctx->followLive = TRUE;
        }
    }
    LeaveCriticalSection(&ctx->lock);
}

static void WorkerFetchPrice(AppContext* ctx) {
    char buf[512];
    if (!HttpGet(ctx, L"/api/v3/ticker/price?symbol=BTCUSDT", buf, (DWORD)sizeof(buf))) return;

    double price = 0.0;
    if (!FastParsePrice(buf, &price)) return;

    EnterCriticalSection(&ctx->lock);
    ctx->lastPrice = price;
    LeaveCriticalSection(&ctx->lock);
}

static DWORD WINAPI NetworkThread(LPVOID param) {
    AppContext* ctx = (AppContext*)param;
    HANDLE waits[2] = { ctx->hStopEvent, ctx->hWakeEvent };

    for (;;) {
        EnterCriticalSection(&ctx->lock);
        HWND hp = ctx->hPopup;
        LeaveCriticalSection(&ctx->lock);

        // Star grafen apen trenger vi lys; ellers holder det med prisen.
        if (hp && IsWindowVisible(hp)) WorkerFetchKlines(ctx);
        else                           WorkerFetchPrice(ctx);

        // PostMessage MA staa utenfor laasen - ellers kan UI-traden sitte
        // og vente paa laasen mens vi venter paa den.
        PostMessageW(ctx->hWnd, WM_APP_DATA, 0, 0);

        if (WaitForMultipleObjects(2, waits, FALSE, TIMER_INTERVAL) == WAIT_OBJECT_0) {
            break;  // hStopEvent
        }
        // WAIT_OBJECT_0 + 1 = hWakeEvent: panelet ble apnet, hent med en gang
    }
    return 0;
}

// Tegner ikon og verktoytips ut fra en pris. Skilt fra hentingen slik at
// vi kan gjenbruke prisen vi allerede har, i stedet for a hente den paa nytt.
static void UpdateIcon(AppContext* ctx, double price) {
    if (price <= 0.0) return;
    swprintf_s(ctx->fullPriceStr, 64, L"BTC/USDT: $%.2f", price);

    // "75.8" = 4 glyfer = noyaktig 16px. Dropper desimalen naar den ikke
    // faar plass ("104"). Malingen ma skje PA den ferdig formaterte
    // strengen, ikke pa en terskel: %.1f runder 99950-99999 opp til
    // "100.0", som er 21px og ville blitt klippet.
    char iconStr[16];
    snprintf(iconStr, sizeof(iconStr), "%.1f", price / 1000.0);
    if (IconTextWidth(iconStr) > 16) {
        snprintf(iconStr, sizeof(iconStr), "%.0f", price / 1000.0);
    }

    HICON hNewIcon = RenderMicroFontIcon(iconStr);
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

// Unix-ms -> lokal "HH:MM"
static void FormatCandleTime(long long unixMs, wchar_t* out, size_t cch) {
    ULONGLONG t = (ULONGLONG)(unixMs / 1000) * 10000000ULL + 116444736000000000ULL;
    FILETIME utc, local;
    utc.dwLowDateTime  = (DWORD)(t & 0xFFFFFFFFULL);
    utc.dwHighDateTime = (DWORD)(t >> 32);
    SYSTEMTIME st;
    if (FileTimeToLocalFileTime(&utc, &local) && FileTimeToSystemTime(&local, &st)) {
        swprintf_s(out, cch, L"%02d:%02d", st.wHour, st.wMinute);
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

static void DrawChart(AppContext* ctx, HDC hdc, int W, int H) {
    RECT rcAll = { 0, 0, W, H };
    FillRect(hdc, &rcAll, ctx->brBg);

    SetBkMode(hdc, TRANSPARENT);

    int n = ctx->candleCount;
    if (n <= 0) {
        SelectObject(hdc, ctx->hFontSmall);
        SetTextColor(hdc, CLR_DIM);
        DrawTextW(hdc, L"Laster data fra Binance...", -1, &rcAll,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER);
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

    // Spennet folger zoomen - "(60m)" ville vaert feil sa snart man zoomer
    wchar_t span[24];
    if (vc < 60) {
        swprintf_s(span, 24, L"%dm", vc);
    } else if (vc % 60 == 0) {
        swprintf_s(span, 24, L"%dt", vc / 60);
    } else {
        swprintf_s(span, 24, L"%dt %dm", vc / 60, vc % 60);
    }

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
    SetTextColor(hdc, CLR_TEXT);
    swprintf_s(buf, 64, L"$%.2f", last);
    DrawTextW(hdc, buf, -1, &rcHdr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(hdc, ctx->hFontSmall);
    SetTextColor(hdc, chgClr);
    swprintf_s(buf, 64, L"%+.2f%%  (%s)", chg, span);
    DrawTextW(hdc, buf, -1, &rcHdr, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

    SetTextColor(hdc, CLR_DIM);
    RECT rcSub = { HDR_TEXT_L, 28, W - 12 - CLOSE_SZ, 42 };
    DrawTextW(hdc, L"BTC/USDT  -  1m", -1, &rcSub, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

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
        swprintf_s(buf, 64, L"%.0f", p);
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
    swprintf_s(buf, 64, L"%.0f", hp);
    RECT rcTag = { right + 1, hy - 8, W - 1, hy + 8 };
    FillRect(hdc, &rcTag, ctx->brBoxEdge);
    SelectObject(hdc, ctx->hFontSmall);
    SetTextColor(hdc, CLR_TEXT);
    RECT rcTagTxt = { right + 4, hy - 8, W - 2, hy + 8 };
    DrawTextW(hdc, buf, -1, &rcTagTxt, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // Hover-boks med tid + OHLC
    wchar_t tbuf[16];
    FormatCandleTime(hc->openTime, tbuf, 16);

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

static void StartChromeFade(HWND hwnd) {
    SetTimer(hwnd, TIMER_FADE_ID, FADE_INTERVAL, NULL);
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

        case WM_EXITSIZEMOVE:
            g_Ctx.inSizeMove = FALSE;
            return 0;

        case WM_SIZE:
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

            if (!wasHot) StartChromeFade(hwnd);
            if (wasHdr != g_Ctx.headerHot || wasClose != g_Ctx.closeHot) {
                InvalidateRect(hwnd, NULL, FALSE);
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
            StartChromeFade(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_FADE_ID) {
                int target = g_Ctx.windowHot ? 255 : 0;
                if (g_Ctx.chrome == target) {
                    KillTimer(hwnd, TIMER_FADE_ID);
                    return 0;
                }
                if (g_Ctx.chrome < target) {
                    g_Ctx.chrome += FADE_STEP;
                    if (g_Ctx.chrome > target) g_Ctx.chrome = target;
                } else {
                    g_Ctx.chrome -= FADE_STEP;
                    if (g_Ctx.chrome < target) g_Ctx.chrome = target;
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_LBUTTONDOWN: {
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
                g_Ctx.lastHideTick = GetTickCount64();
                ShowWindow(hwnd, SW_HIDE);
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
            0, 0, POPUP_W, POPUP_H,
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
    PositionPopup(ctx->hPopup);
    ctx->shownTick = GetTickCount64();
    ShowWindow(ctx->hPopup, SW_SHOWNA);
    ForceForeground(ctx->hPopup);
    SetEvent(ctx->hWakeEvent);   // hent lys na, ikke om opptil 3 sekunder // kreves for at WA_INACTIVE skal utloses senere
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
            EnterCriticalSection(&g_Ctx.lock);
            price = g_Ctx.lastPrice;
            LeaveCriticalSection(&g_Ctx.lock);

            UpdateIcon(&g_Ctx, price);
            if (g_Ctx.hPopup && IsWindowVisible(g_Ctx.hPopup)) {
                InvalidateRect(g_Ctx.hPopup, NULL, FALSE);
            }
            return 0;
        }

        case WM_DESTROY:
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
    g_Ctx.nid.hIcon            = RenderMicroFontIcon("...");
    wcscpy_s(g_Ctx.nid.szTip, 128, L"Kobler til Binance...");

    Shell_NotifyIconW(NIM_ADD, &g_Ctx.nid);

    // Faste GDI-objekter: lages en gang, ikke 16 ganger per opptegning
    g_Ctx.penGrid   = CreatePen(PS_SOLID, 1, CLR_GRID);
    g_Ctx.penUp     = CreatePen(PS_SOLID, 1, CLR_UP);
    g_Ctx.penDown   = CreatePen(PS_SOLID, 1, CLR_DOWN);
    g_Ctx.penCross  = CreatePen(PS_DOT,   1, CLR_CROSS);
    g_Ctx.brBg      = CreateSolidBrush(CLR_BG);
    g_Ctx.brUp      = CreateSolidBrush(CLR_UP);
    g_Ctx.brDown    = CreateSolidBrush(CLR_DOWN);
    g_Ctx.brBox     = CreateSolidBrush(CLR_BOX);
    g_Ctx.brBoxEdge = CreateSolidBrush(CLR_BOXEDGE);

    // Arbeidertraden startes forst naar vinduet og ikonet finnes, siden
    // den poster meldinger til hWnd med en gang.
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
    if (g_Ctx.penDim)   DeleteObject(g_Ctx.penDim);
    if (g_Ctx.penX)     DeleteObject(g_Ctx.penX);
    if (g_Ctx.brDim)    DeleteObject(g_Ctx.brDim);
    if (g_Ctx.brEdge)   DeleteObject(g_Ctx.brEdge);
    if (g_Ctx.brHdr)    DeleteObject(g_Ctx.brHdr);
    if (g_Ctx.brClose)  DeleteObject(g_Ctx.brClose);

    if (g_Ctx.hConnect) WinHttpCloseHandle(g_Ctx.hConnect);
    if (g_Ctx.hSession) WinHttpCloseHandle(g_Ctx.hSession);
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);

    return (int)msg.wParam;
}
