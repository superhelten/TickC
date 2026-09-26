// feed.c - the TickC feed's writer (phase 54): the ring and the snapshot
// tables in shared memory, the Binance message parser, and FeedThread, which
// reads Binance's WebSocket stream and publishes it. The contract - the
// layout and the reader - is feed.h. One writer per mapping: only TickC's
// main instance starts the feed.
#include "feed.h"
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

// ---------------------------------------------------------------------------
// The writer
// ---------------------------------------------------------------------------

static FeedEvent* FeedSlot(FeedWriter* w, int64_t seq) {
    return (FeedEvent*)(w->ring + FEED_SLOT_INDEX(seq) * FEED_SLOT_SIZE);
}

// Creates the mapping, or takes over the one a reader still holds from an
// earlier writer. magic is 0 while the header is rewritten and is stored
// last, so a reader never trusts a half-written identity. writeSeq goes on
// from where the earlier session stopped: numbers only rise.
BOOL FeedWriterOpen(FeedWriter* w, const wchar_t* name, const FeedInstrument* ins, unsigned count) {
    FeedHeader* h;
    int64_t seq, sess;
    memset(w, 0, sizeof(*w));
    if (count > FEED_MAX_INSTRUMENTS) return FALSE;
    if (!name) name = FEED_MAPPING_NAME;
    wcscpy_s(w->name, 64, name);
    w->hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, FEED_MAPPING_SIZE, name);
    if (!w->hMap) return FALSE;
    h = (FeedHeader*)MapViewOfFile(w->hMap, FILE_MAP_ALL_ACCESS, 0, 0, FEED_MAPPING_SIZE);
    if (!h) { CloseHandle(w->hMap); w->hMap = NULL; return FALSE; }
    w->hdr  = h;
    w->ring = (uint8_t*)h + FEED_HEADER_SIZE;

    InterlockedExchange((LONG volatile*)(volatile void*)&h->magic, 0);
    seq = FeedLoadAcquire64(&h->writeSeq);
    if (seq < 1) seq = 1;
    h->versionMajor    = FEED_VERSION_MAJOR;
    h->versionMinor    = FEED_VERSION_MINOR;
    h->headerSize      = FEED_HEADER_SIZE;
    h->slotSize        = FEED_SLOT_SIZE;
    h->slotCount       = FEED_SLOT_COUNT;
    h->instrumentCount = count;
    memset(h->instruments, 0, sizeof(h->instruments));
    memcpy(h->instruments, ins, count * sizeof(FeedInstrument));
    for (unsigned i = 0; i < FEED_MAX_INSTRUMENTS; ++i) {
        FeedSnapshot* s = &h->snapshots[i];
        FeedTicker24Snap* t = &h->tickers24[i];
        // A writer that died between FeedPublish's two increments left the
        // lock odd: that writer is still "inside" the critical section, mid
        // write. The restart continues that same section instead of closing
        // it and opening a new one - closing it first (evening the odd lock
        // before the clear) would let a reader positioned between the close
        // and the reopen see an even lock over the still half-written
        // payload and accept it as clean. So only an already-even lock (a
        // clean exit, or a 1.0 writer's untouched, zeroed 24 h table) gets
        // the opening increment below; either way the closing increment,
        // after the clear, is the only place the lock goes back to even
        // (phase 55: measured torn reads without this, see WORKLOG).
        if (!(FeedLoadNoFence64(&s->lock) & 1)) InterlockedIncrement64((LONG64 volatile*)&s->lock);
        s->updatedUs = 0;
        memset(&s->trade, 0, sizeof(s->trade));
        memset(&s->kline, 0, sizeof(s->kline));
        InterlockedIncrement64((LONG64 volatile*)&s->lock);
        if (!(FeedLoadNoFence64(&t->lock) & 1)) InterlockedIncrement64((LONG64 volatile*)&t->lock);
        t->updatedUs = 0;
        memset(&t->t, 0, sizeof(t->t));
        InterlockedIncrement64((LONG64 volatile*)&t->lock);
    }
    h->writerPid = GetCurrentProcessId();
    InterlockedExchange(&h->connState, 0);
    FeedStoreRelease64(&h->writeSeq, seq);
    FeedStoreRelease64(&h->heartbeatUs, FeedNowUs());
    sess = FeedNowUs();
    if (sess <= FeedLoadAcquire64(&h->sessionUs)) sess = FeedLoadAcquire64(&h->sessionUs) + 1;
    FeedStoreRelease64(&h->sessionUs, sess);
    InterlockedExchange((LONG volatile*)(volatile void*)&h->magic, (LONG)FEED_MAGIC);
    return TRUE;
}

// Opens a reader's event the first time its slot is ready (or when another
// process has taken the slot), and signals every ready reader.
static void FeedWake(FeedWriter* w) {
    for (int i = 0; i < FEED_MAX_READERS; ++i) {
        FeedReaderSlot* s = &w->hdr->readers[i];
        LONG owner = s->owner;
        if (owner == 0 || ReadAcquire(&s->ready) == 0) {
            if (w->hWake[i]) { CloseHandle(w->hWake[i]); w->hWake[i] = NULL; w->wakePid[i] = 0; }
            continue;
        }
        if (!w->hWake[i] || w->wakePid[i] != owner) {
            wchar_t evName[96];
            if (w->hWake[i]) CloseHandle(w->hWake[i]);
            swprintf_s(evName, 96, L"%s.R%d", w->name, i);
            w->hWake[i] = OpenEventW(EVENT_MODIFY_STATE, FALSE, evName);
            w->wakePid[i] = owner;
        }
        if (w->hWake[i]) SetEvent(w->hWake[i]);
    }
}

// One event into the ring. The snapshot first: a reader that resyncs reads
// writeSeq, then the snapshots, so everything below writeSeq must already be
// in them (spec: Resync).
void FeedPublish(FeedWriter* w, const FeedEvent* ev) {
    FeedHeader* h = w->hdr;
    FeedEvent* slot;
    int64_t seq;
    if (ev->instrument < h->instrumentCount && (ev->type == FEED_TRADE || ev->type == FEED_KLINE)) {
        FeedSnapshot* s = &h->snapshots[ev->instrument];
        InterlockedIncrement64((LONG64 volatile*)&s->lock);   // odd: a reader retries
        s->updatedUs = ev->tsRecvUs;
        if (ev->type == FEED_TRADE) s->trade = ev->u.trade;
        else                        s->kline = ev->u.kline;
        InterlockedIncrement64((LONG64 volatile*)&s->lock);   // even again
    }
    else if (ev->instrument < h->instrumentCount && ev->type == FEED_TICKER24) {
        // 1.1 (phase 55): the 24 h table, before the ring for the same
        // reason as the snapshot above.
        FeedTicker24Snap* t = &h->tickers24[ev->instrument];
        InterlockedIncrement64((LONG64 volatile*)&t->lock);
        t->updatedUs = ev->tsRecvUs;
        t->t = ev->u.ticker24;
        InterlockedIncrement64((LONG64 volatile*)&t->lock);
    }
    seq  = FeedLoadNoFence64(&h->writeSeq);   // only this thread writes it
    slot = FeedSlot(w, seq);
    InterlockedExchange64((LONG64 volatile*)&slot->seq, FEED_SEQ_BUSY);   // a full barrier
    memcpy((uint8_t*)slot + 8, (const uint8_t*)ev + 8, FEED_SLOT_SIZE - 8);
    FEED_STORE_FENCE();   // the whole copy is visible before the number (feed.h)
    FeedStoreRelease64(&slot->seq, seq);
    FeedStoreRelease64(&h->writeSeq, seq + 1);
    FeedWake(w);
}

void FeedSetConn(FeedWriter* w, int state, int32_t error, int64_t retryAtUs) {
    FeedEvent ev;
    memset(&ev, 0, sizeof(ev));
    InterlockedExchange(&w->hdr->connState, state);
    ev.type        = FEED_STATUS;
    ev.instrument  = FEED_NO_INSTRUMENT;
    ev.source      = FEED_SRC_BINANCE_SPOT;
    ev.tsExchangeUs = 0;
    ev.tsRecvUs    = FeedNowUs();
    ev.u.status.state     = (uint16_t)state;
    ev.u.status.error     = error;
    ev.u.status.retryAtUs = retryAtUs;
    FeedPublish(w, &ev);
}

void FeedHeartbeat(FeedWriter* w) {
    FeedStoreRelease64(&w->hdr->heartbeatUs, FeedNowUs());
}

// Frees the slots of readers whose process has exited (spec: Cleanup). A
// process we may not open (access denied) is alive, not dead. A reused
// process id keeps the slot until that process exits too - a known limit.
void FeedSweepReaders(FeedWriter* w) {
    for (int i = 0; i < FEED_MAX_READERS; ++i) {
        FeedReaderSlot* s = &w->hdr->readers[i];
        LONG owner = s->owner;
        HANDLE p;
        BOOL dead;
        if (owner == 0) continue;
        p = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)owner);
        if (p) {
            dead = (WaitForSingleObject(p, 0) == WAIT_OBJECT_0);
            CloseHandle(p);
        } else {
            dead = (GetLastError() == ERROR_INVALID_PARAMETER);   // no such process
        }
        if (!dead) continue;
        if (w->hWake[i]) { CloseHandle(w->hWake[i]); w->hWake[i] = NULL; w->wakePid[i] = 0; }
        InterlockedExchange(&s->ready, 0);
        InterlockedCompareExchange(&s->owner, 0, owner);
    }
}

// magic goes to 0 first: a reader that opens now, or reads on, sees
// FEED_NO_WRITER instead of a writer that is gone.
void FeedWriterClose(FeedWriter* w) {
    if (w->hdr) InterlockedExchange((LONG volatile*)(volatile void*)&w->hdr->magic, 0);
    for (int i = 0; i < FEED_MAX_READERS; ++i) if (w->hWake[i]) CloseHandle(w->hWake[i]);
    if (w->hdr)  UnmapViewOfFile(w->hdr);
    if (w->hMap) CloseHandle(w->hMap);
    memset(w, 0, sizeof(*w));
}

// ---------------------------------------------------------------------------
// The parser
// ---------------------------------------------------------------------------
// Binance's combined stream: {"stream":"btcusdt@trade","data":{...}}. Values
// are found by key, never by position (spec: Parsing), within the bounds of
// the object they belong to. Numbers in quotes are decimals with at most 8
// places, parsed exactly into fixed point; times are milliseconds.

#define FEED_MS_MAX 9223372036854775ULL   // ms * 1000 still fits an int64

// Finds "key": in [p, end) and returns the first byte of its value.
static const char* JsonKey(const char* p, const char* end, const char* key) {
    size_t n = strlen(key);
    for (; p + n + 3 <= end; ++p) {
        if (p[0] == '"' && memcmp(p + 1, key, n) == 0 && p[1 + n] == '"' && p[2 + n] == ':') {
            const char* v = p + n + 3;
            while (v < end && *v == ' ') ++v;
            return (v < end) ? v : NULL;
        }
    }
    return NULL;
}

// The end (one past the '}') of the object that starts at p, or NULL.
static const char* JsonObjectEnd(const char* p, const char* end) {
    int depth = 0;
    BOOL inStr = FALSE;
    for (; p < end; ++p) {
        if (inStr) { if (*p == '"') inStr = FALSE; continue; }
        if (*p == '"') inStr = TRUE;
        else if (*p == '{') ++depth;
        else if (*p == '}' && --depth == 0) return p + 1;
    }
    return NULL;
}

// A quoted string's contents (Binance's values carry no escapes).
static BOOL JsonString(const char* v, const char* end, const char** s, const char** e) {
    const char* q;
    if (!v || v >= end || *v != '"') return FALSE;
    for (q = v + 1; q < end && *q != '"'; ++q) {}
    if (q >= end) return FALSE;
    *s = v + 1;
    *e = q;
    return TRUE;
}

static BOOL JsonU64(const char* v, const char* end, uint64_t* out) {
    uint64_t x = 0;
    const char* p = v;
    if (!v) return FALSE;
    for (; p < end && *p >= '0' && *p <= '9'; ++p) {
        if (x > (UINT64_MAX - 9) / 10) return FALSE;
        x = x * 10 + (uint64_t)(*p - '0');
    }
    if (p == v) return FALSE;
    *out = x;
    return TRUE;
}

static BOOL JsonBool(const char* v, const char* end, int* out) {
    if (v && end - v >= 4 && memcmp(v, "true", 4) == 0)  { *out = 1; return TRUE; }
    if (v && end - v >= 5 && memcmp(v, "false", 5) == 0) { *out = 0; return TRUE; }
    return FALSE;
}

static BOOL JsonFixed8(const char* v, const char* end, int64_t* out) {
    const char *s, *e;
    return JsonString(v, end, &s, &e) && FeedParseFixed8(s, e, out);
}

static BOOL JsonIs(const char* v, const char* end, const char* want) {
    const char *s, *e;
    size_t n = strlen(want);
    return JsonString(v, end, &s, &e) && (size_t)(e - s) == n && memcmp(s, want, n) == 0;
}

static BOOL JsonMs(const char* v, const char* end, int64_t* us) {
    uint64_t ms;
    if (!JsonU64(v, end, &ms) || ms > FEED_MS_MAX) return FALSE;
    *us = (int64_t)ms * 1000;
    return TRUE;
}

// Digits, then optionally a dot and 1-8 digits; nothing else. No sign, no
// exponent. The largest value is INT64_MAX at 10^-8, 92233720368.54775807.
int FeedParseFixed8(const char* s, const char* e, int64_t* out) {
    uint64_t ip = 0, fp = 0;
    int id = 0, fd = 0;
    const char* p = s;
    for (; p < e && *p >= '0' && *p <= '9'; ++p, ++id) {
        if (ip > 92233720368ULL) return 0;
        ip = ip * 10 + (uint64_t)(*p - '0');
    }
    if (id == 0) return 0;
    if (p < e && *p == '.') {
        for (++p; p < e && *p >= '0' && *p <= '9'; ++p, ++fd) {
            if (fd == 8) return 0;
            fp = fp * 10 + (uint64_t)(*p - '0');
        }
        if (fd == 0) return 0;
    }
    if (p != e) return 0;
    for (; fd < 8; ++fd) fp *= 10;
    if (ip > 92233720368ULL || (ip == 92233720368ULL && fp > 54775807ULL)) return 0;
    *out = (int64_t)(ip * 100000000ULL + fp);
    return 1;
}

int FeedParseMessage(const char* msg, size_t len, const FeedInstrument* ins, unsigned count,
                     int64_t recvUs, FeedEvent* ev) {
    const char* end = msg + len;
    const char *d, *dEnd, *v, *s0, *s1;
    uint64_t u;
    int b;
    unsigned i;

    d = JsonKey(msg, end, "data");
    if (!d || *d != '{' || !(dEnd = JsonObjectEnd(d, end))) return FEED_PARSE_BAD;
    memset(ev, 0, sizeof(*ev));
    ev->source   = FEED_SRC_BINANCE_SPOT;
    ev->tsRecvUs = recvUs;

    // The symbol. "s" is also inside "k" for a kline; both carry the same one.
    if (!JsonString(JsonKey(d, dEnd, "s"), dEnd, &s0, &s1)) return FEED_PARSE_BAD;
    for (i = 0; i < count; ++i) {
        size_t n = strlen(ins[i].symbol);
        if ((size_t)(s1 - s0) == n && memcmp(s0, ins[i].symbol, n) == 0) break;
    }
    if (i == count) return FEED_PARSE_UNKNOWN;
    ev->instrument = (uint16_t)i;

    v = JsonKey(d, dEnd, "e");
    if (JsonIs(v, dEnd, "trade")) {
        ev->type = FEED_TRADE;
        if (!JsonU64(JsonKey(d, dEnd, "t"), dEnd, &u)) return FEED_PARSE_BAD;
        ev->u.trade.tradeId = u;
        if (!JsonFixed8(JsonKey(d, dEnd, "p"), dEnd, &ev->u.trade.price)) return FEED_PARSE_BAD;
        if (!JsonFixed8(JsonKey(d, dEnd, "q"), dEnd, &ev->u.trade.qty))   return FEED_PARSE_BAD;
        if (!JsonMs(JsonKey(d, dEnd, "T"), dEnd, &ev->tsExchangeUs))      return FEED_PARSE_BAD;
        if (!JsonBool(JsonKey(d, dEnd, "m"), dEnd, &b))                   return FEED_PARSE_BAD;
        ev->u.trade.side = (uint8_t)(b ? FEED_SIDE_SELL : FEED_SIDE_BUY);
        return FEED_PARSE_OK;
    }
    if (JsonIs(v, dEnd, "kline")) {
        const char *k = JsonKey(d, dEnd, "k"), *kEnd;
        if (!k || *k != '{' || !(kEnd = JsonObjectEnd(k, dEnd))) return FEED_PARSE_BAD;
        ev->type = FEED_KLINE;
        // "E" is looked for outside "k": search the part of data before and
        // after the kline object, so a key inside it is never taken.
        v = JsonKey(d, k, "E");
        if (!v) v = JsonKey(kEnd, dEnd, "E");
        if (!JsonMs(v, dEnd, &ev->tsExchangeUs)) return FEED_PARSE_BAD;
        if (!JsonIs(JsonKey(k, kEnd, "i"), kEnd, "1m")) return FEED_PARSE_BAD;
        ev->u.kline.intervalSec = 60;
        if (!JsonMs(JsonKey(k, kEnd, "t"), kEnd, &ev->u.kline.openTimeUs))         return FEED_PARSE_BAD;
        if (!JsonFixed8(JsonKey(k, kEnd, "o"), kEnd, &ev->u.kline.open))           return FEED_PARSE_BAD;
        if (!JsonFixed8(JsonKey(k, kEnd, "h"), kEnd, &ev->u.kline.high))           return FEED_PARSE_BAD;
        if (!JsonFixed8(JsonKey(k, kEnd, "l"), kEnd, &ev->u.kline.low))            return FEED_PARSE_BAD;
        if (!JsonFixed8(JsonKey(k, kEnd, "c"), kEnd, &ev->u.kline.close))          return FEED_PARSE_BAD;
        if (!JsonFixed8(JsonKey(k, kEnd, "v"), kEnd, &ev->u.kline.volume))         return FEED_PARSE_BAD;
        if (!JsonFixed8(JsonKey(k, kEnd, "q"), kEnd, &ev->u.kline.quoteVolume))    return FEED_PARSE_BAD;
        if (!JsonU64(JsonKey(k, kEnd, "n"), kEnd, &u) || u > UINT32_MAX)           return FEED_PARSE_BAD;
        ev->u.kline.tradeCount = (uint32_t)u;
        if (!JsonBool(JsonKey(k, kEnd, "x"), kEnd, &b)) return FEED_PARSE_BAD;
        if (b) ev->flags = (uint16_t)(ev->flags | FEED_KLINE_CLOSED);
        return FEED_PARSE_OK;
    }
    if (JsonIs(v, dEnd, "24hrMiniTicker")) {
        // 1.1 (phase 55): rolling 24 h statistics; a flat object, no "k".
        ev->type = FEED_TICKER24;
        if (!JsonMs(JsonKey(d, dEnd, "E"), dEnd, &ev->tsExchangeUs))               return FEED_PARSE_BAD;
        if (!JsonFixed8(JsonKey(d, dEnd, "o"), dEnd, &ev->u.ticker24.open))        return FEED_PARSE_BAD;
        if (!JsonFixed8(JsonKey(d, dEnd, "h"), dEnd, &ev->u.ticker24.high))        return FEED_PARSE_BAD;
        if (!JsonFixed8(JsonKey(d, dEnd, "l"), dEnd, &ev->u.ticker24.low))         return FEED_PARSE_BAD;
        if (!JsonFixed8(JsonKey(d, dEnd, "c"), dEnd, &ev->u.ticker24.close))       return FEED_PARSE_BAD;
        if (!JsonFixed8(JsonKey(d, dEnd, "v"), dEnd, &ev->u.ticker24.volume))      return FEED_PARSE_BAD;
        if (!JsonFixed8(JsonKey(d, dEnd, "q"), dEnd, &ev->u.ticker24.quoteVolume)) return FEED_PARSE_BAD;
        return FEED_PARSE_OK;
    }
    return FEED_PARSE_BAD;
}

// ---------------------------------------------------------------------------
// FeedThread
// ---------------------------------------------------------------------------
// One per process. It owns the WebSocket, and while FeedConnect runs, the
// connect and request handles too; FeedStop may close whichever of the
// three is open from outside, under g_feed.lock, which cancels a pending
// call at once (measured in task 1). The watchdog, unlike FeedStop, only
// ever closes hWs, the same way, when the line has gone silent. The
// heartbeat is a thread-pool timer that writes heartbeatUs every tick; it
// also runs that watchdog, so it is no longer "nothing else" (phase 54).

#define FEED_MSG_MAX       65536
// The watchdog's silence bound; also the upgrade request's own timeout,
// which alone does not bound the ongoing receive (task 1's spike) (phase 54).
#define FEED_RECV_MS       10000
#define FEED_STABLE_MS     60000     // a connection this old resets the backoff

static struct {
    BOOL             running;
    FeedWriter       w;
    FeedConfig       cfg;
    wchar_t          replay[MAX_PATH];
    CRITICAL_SECTION lock;           // guards hWs, hConnect and hRequest (phase 54)
    HINTERNET        hWs;
    HINTERNET        hConnect;       // published only while FeedConnect may block on it (phase 54)
    HINTERNET        hRequest;       // published only while FeedConnect may block on it (phase 54)
    HANDLE           hStop, hThread;
    PTP_TIMER        timer;
    ULONGLONG        lastSweep;
    volatile LONG    published, dropped, connects;
    // GetTickCount64() of the last message read; also stamped once at
    // connect, before any message has arrived, so the watchdog has a
    // baseline from the start of a connection, not only after its first
    // message (phase 54).
    volatile LONG64  lastMsgTick;
    volatile LONG    silent;         // 1: the watchdog closed hWs, not FeedStop (phase 54)
    FeedInstrument   ins[FEED_MAX_INSTRUMENTS];   // FeedStart's private copy of the caller's table (phase 54)
} g_feed;

static char g_feedBuf[FEED_MSG_MAX];

static void FeedMaybeSweep(void) {
    ULONGLONG now = GetTickCount64();
    if (now - g_feed.lastSweep < 1000) return;
    g_feed.lastSweep = now;
    FeedSweepReaders(&g_feed.w);
}

static void FeedHandleMessage(const char* msg, size_t len) {
    FeedEvent ev;
    if (FeedParseMessage(msg, len, g_feed.cfg.instruments, g_feed.cfg.instrumentCount, FeedNowUs(), &ev)
        == FEED_PARSE_OK) {
        FeedPublish(&g_feed.w, &ev);
        InterlockedIncrement(&g_feed.published);
    } else {
        InterlockedIncrement(&g_feed.dropped);
    }
    FeedMaybeSweep();
}

// TRUE when stop was asked within ms; sweeps the reader slots once a second.
static BOOL FeedWaitStop(DWORD ms) {
    for (;;) {
        DWORD step = ms < 1000 ? ms : 1000;
        if (WaitForSingleObject(g_feed.hStop, step) == WAIT_OBJECT_0) return TRUE;
        FeedMaybeSweep();
        if (ms <= step) return FALSE;
        ms -= step;
    }
}

// The test build's stand-in for the socket: every line of a .jsonl through
// the same parser, then idle. A missing file is a failed connection that is
// never retried - a test build with fixtures must not reach the network.
static void FeedReplay(void) {
    HANDLE f, m = NULL;
    const char *p = NULL, *line, *end;
    DWORD size = 0, err = 0;
    FeedSetConn(&g_feed.w, FEED_ST_CONNECTING, 0, 0);
    f = CreateFileW(g_feed.replay, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) err = GetLastError();
    else {
        size = GetFileSize(f, NULL);
        if (size && size != INVALID_FILE_SIZE) m = CreateFileMappingW(f, NULL, PAGE_READONLY, 0, 0, NULL);
        if (m) p = (const char*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
        if (!p) err = GetLastError() ? GetLastError() : ERROR_HANDLE_EOF;
    }
    if (!p) {
        FeedSetConn(&g_feed.w, FEED_ST_DISCONNECTED, (int32_t)err, 0);
    } else {
        InterlockedIncrement(&g_feed.connects);
        FeedSetConn(&g_feed.w, FEED_ST_CONNECTED, 0, 0);
        for (line = p, end = p + size; line < end; ) {
            const char* nl = (const char*)memchr(line, '\n', (size_t)(end - line));
            const char* le = nl ? nl : end;
            size_t n = (size_t)(le - line);
            if (n && line[n - 1] == '\r') --n;
            if (n) FeedHandleMessage(line, n);
            line = nl ? nl + 1 : end;
        }
        UnmapViewOfFile(p);
    }
    if (m) CloseHandle(m);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
}

// "/stream?streams=btcusdt@trade/btcusdt@kline_1m/btcusdt@miniTicker/..."
// (phase 55: the miniTicker stream, and a length check - _snwprintf_s with
// _TRUNCATE returns -1 instead of calling the invalid-parameter handler).
// cch 0 or out NULL is rejected before that first call, for the same reason
// (final review, phase 55): _snwprintf_s's own _TRUNCATE handling still
// reaches the invalid-parameter handler when the buffer cannot hold even a
// NUL, so the guard has to come first, not fall out of the length check.
int FeedStreamPath(const FeedInstrument* ins, unsigned count, wchar_t* out, size_t cch) {
    int n;
    if (cch == 0 || out == NULL) return -1;
    n = _snwprintf_s(out, cch, _TRUNCATE, L"/stream?streams=");
    if (n < 0) return -1;
    for (unsigned i = 0; i < count; ++i) {
        wchar_t sym[16];
        const char* s = ins[i].symbol;
        int k = 0, m;
        for (; k < 15 && s[k]; ++k) sym[k] = (wchar_t)((s[k] >= 'A' && s[k] <= 'Z') ? s[k] + 32 : s[k]);
        sym[k] = 0;
        m = _snwprintf_s(out + n, cch - (size_t)n, _TRUNCATE, L"%s%s@trade/%s@kline_1m/%s@miniTicker",
                         i ? L"/" : L"", sym, sym, sym);
        if (m < 0) return -1;
        n += m;
    }
    return n;
}

// Publishes a handle in g_feed under the lock so FeedStop can find and
// cancel it at once (task 1: the spike's own 1000 ms was its Closer
// thread's scheduled sleep before it closed the handle, not the time the
// cancel itself took), unless stop was already asked, in which case it is
// closed here and the caller must not touch it again. The
// same publish-or-bail race protects hWs, hConnect and hRequest (phase 54:
// FeedStop during FeedConnect).
static BOOL FeedPublishHandle(HINTERNET* slot, HINTERNET h) {
    BOOL stopped;
    EnterCriticalSection(&g_feed.lock);
    stopped = (WaitForSingleObject(g_feed.hStop, 0) == WAIT_OBJECT_0);
    if (!stopped) *slot = h;
    LeaveCriticalSection(&g_feed.lock);
    if (stopped) WinHttpCloseHandle(h);
    return !stopped;
}

// Closes a published handle unless FeedStop already did; whoever finds it
// non-NULL under the lock is the one that closes it, so it is never closed
// twice (phase 54).
static void FeedCloseTracked(HINTERNET* slot) {
    EnterCriticalSection(&g_feed.lock);
    if (*slot) { WinHttpCloseHandle(*slot); *slot = NULL; }
    LeaveCriticalSection(&g_feed.lock);
}

// Un-publishes a handle the caller goes on owning past this point (hc, kept
// open for the WebSocket's own lifetime): if FeedStop has not raced in to
// close it, only the g_feed slot is cleared and the caller's handle stays
// good; if FeedStop already closed it, the caller's own copy is nulled too,
// so it is never closed a second time (phase 54).
static void FeedUnpublish(HINTERNET* slot, HINTERNET* hOwn) {
    EnterCriticalSection(&g_feed.lock);
    if (*slot) *slot = NULL; else *hOwn = NULL;
    LeaveCriticalSection(&g_feed.lock);
}

// Connects and upgrades. FEED_RECV_MS on this request's handle bounds only
// the upgrade request itself, not the ongoing WebSocket receive (task 1's
// spike); the session keeps its own 5 s for the REST fetches. The connect
// and request handles are published in g_feed under the lock while a
// blocking call may be pending on them, the same way hWs is, so FeedStop can
// close whichever is open and cancel it at once (phase 54: FeedStop during
// FeedConnect).
static HINTERNET FeedConnect(HINTERNET* hc, DWORD* err) {
    wchar_t path[FEED_PATH_CCH];
    HINTERNET hr, ws = NULL;
    DWORD status = 0, cb = sizeof(status);
    *hc = NULL;
    if (g_feed.cfg.sessionLock) EnterCriticalSection(g_feed.cfg.sessionLock);
    if (g_feed.cfg.pSession && *g_feed.cfg.pSession)
        *hc = WinHttpConnect(*g_feed.cfg.pSession, L"stream.binance.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (g_feed.cfg.sessionLock) LeaveCriticalSection(g_feed.cfg.sessionLock);
    if (!*hc) { *err = GetLastError(); return NULL; }
    if (!FeedPublishHandle(&g_feed.hConnect, *hc)) { *hc = NULL; return NULL; }

    if (FeedStreamPath(g_feed.cfg.instruments, g_feed.cfg.instrumentCount, path, FEED_PATH_CCH) < 0) {
        *err = ERROR_INSUFFICIENT_BUFFER;
        FeedUnpublish(&g_feed.hConnect, hc);
        return NULL;
    }
    hr = WinHttpOpenRequest(*hc, L"GET", path, NULL, WINHTTP_NO_REFERER,
                            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hr) {
        *err = GetLastError();
    } else if (FeedPublishHandle(&g_feed.hRequest, hr)) {
        WinHttpSetTimeouts(hr, 5000, 5000, 5000, FEED_RECV_MS);
        // WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET's return is checked like every
        // other failure here (phase 54): unchecked, a failure here would
        // still fall through to WinHttpSendRequest and be reported as
        // whatever that call's own failure was, not the real cause.
        if (WinHttpSetOption(hr, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0) &&
            WinHttpSendRequest(hr, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hr, NULL) &&
            WinHttpQueryHeaders(hr, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &cb, WINHTTP_NO_HEADER_INDEX) &&
            status == 101) {
            ws = WinHttpWebSocketCompleteUpgrade(hr, 0);
        }
        if (!ws) *err = status && status != 101 ? status : GetLastError();
        FeedCloseTracked(&g_feed.hRequest);
    }
    // else: stop was already asked; FeedPublishHandle closed hr itself, and
    // hr must not be touched again.

    FeedUnpublish(&g_feed.hConnect, hc);
    return ws;
}

// Reads messages until the socket fails, closes, or is closed by FeedStop or
// the watchdog (phase 54). Returns the reason: a WinHTTP error or the
// WebSocket close status; FeedThread turns a watchdog close into
// ERROR_WINHTTP_TIMEOUT once it returns, since g_feed.silent says which one
// happened.
static DWORD FeedReceiveLoop(HINTERNET ws) {
    DWORD len = 0;
    BOOL over = FALSE;
    for (;;) {
        DWORD got = 0, e;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE bt;
        e = WinHttpWebSocketReceive(ws, g_feedBuf + len, FEED_MSG_MAX - len, &got, &bt);
        if (e != NO_ERROR) return e;
        if (bt == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            USHORT st = 0;
            DWORD cb = 0;
            WinHttpWebSocketQueryCloseStatus(ws, &st, NULL, 0, &cb);
            return st ? st : ERROR_WINHTTP_CONNECTION_ERROR;
        }
        len += got;
        InterlockedExchange64(&g_feed.lastMsgTick, (LONG64)GetTickCount64());
        if (bt == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE ||
            bt == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE) {
            if (len == FEED_MSG_MAX) { over = TRUE; len = 0; }   // too big: dropped when it ends
            continue;
        }
        if (bt == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE && !over) FeedHandleMessage(g_feedBuf, len);
        else InterlockedIncrement(&g_feed.dropped);
        over = FALSE;
        len = 0;
    }
}

static DWORD WINAPI FeedThread(LPVOID unused) {
    int failures = 0;
    (void)unused;
    if (g_feed.replay[0]) {
        FeedReplay();
        while (!FeedWaitStop(1000)) {}
        return 0;
    }
    for (;;) {
        HINTERNET hc = NULL, ws;
        DWORD err = 0, wait;
        ULONGLONG t0 = 0;
        BOOL connected = FALSE;
        if (WaitForSingleObject(g_feed.hStop, 0) == WAIT_OBJECT_0) break;
        InterlockedExchange(&g_feed.silent, 0);   // this attempt's own flag, not a stale one (phase 54)
        FeedSetConn(&g_feed.w, FEED_ST_CONNECTING, 0, 0);
        ws = FeedConnect(&hc, &err);
        if (ws) {
            BOOL stopped;
            EnterCriticalSection(&g_feed.lock);
            stopped = (WaitForSingleObject(g_feed.hStop, 0) == WAIT_OBJECT_0);
            if (!stopped) {
                InterlockedExchange64(&g_feed.lastMsgTick, (LONG64)GetTickCount64());
                g_feed.hWs = ws;
            }
            LeaveCriticalSection(&g_feed.lock);
            if (stopped) { WinHttpCloseHandle(ws); if (hc) WinHttpCloseHandle(hc); break; }
            InterlockedIncrement(&g_feed.connects);
            connected = TRUE;
            t0 = GetTickCount64();   // the 60 s rule counts from here, not from before the attempt (phase 54)
            FeedSetConn(&g_feed.w, FEED_ST_CONNECTED, 0, 0);
            err = FeedReceiveLoop(ws);
            if (InterlockedCompareExchange(&g_feed.silent, 0, 0))
                err = ERROR_WINHTTP_TIMEOUT;   // the watchdog closed it, not FeedStop: say so, not 12017 (phase 54)
            EnterCriticalSection(&g_feed.lock);
            if (g_feed.hWs) { WinHttpCloseHandle(g_feed.hWs); g_feed.hWs = NULL; }
            LeaveCriticalSection(&g_feed.lock);
        }
        if (hc) WinHttpCloseHandle(hc);
        if (WaitForSingleObject(g_feed.hStop, 0) == WAIT_OBJECT_0) break;
        // A connection that held a minute (Binance's 24 h cut) goes again at
        // once, counted from CONNECTED, not from before the attempt; anything
        // shorter, or a connect that never reached CONNECTED, backs off on
        // TickC's curve (phase 54).
        if (connected && GetTickCount64() - t0 >= FEED_STABLE_MS) {
            failures = 0;
            wait = 0;
        } else {
            if (failures < 32) failures++;
            wait = g_feed.cfg.backoffMs(failures, GetTickCount64());
        }
        FeedSetConn(&g_feed.w, FEED_ST_DISCONNECTED, (int32_t)err, wait ? FeedNowUs() + (int64_t)wait * 1000 : 0);
        if (wait && FeedWaitStop(wait)) break;
    }
    return 0;
}

static VOID CALLBACK FeedTick(PTP_CALLBACK_INSTANCE inst, PVOID ctx, PTP_TIMER timer) {
    (void)inst; (void)ctx; (void)timer;
    FeedHeartbeat(&g_feed.w);
    // Task 1 measured it: no timeout bounds an ongoing WebSocket receive, so
    // a dead line is closed from here after FEED_RECV_MS of silence, the
    // same way FeedStop ends a pending receive. silent records that this
    // close came from here, not from FeedStop, so FeedThread can report the
    // spec's own word for it, a timeout, instead of the cancellation status
    // that closing the handle actually produces (phase 54).
    EnterCriticalSection(&g_feed.lock);
    if (g_feed.hWs && GetTickCount64() - (ULONGLONG)InterlockedCompareExchange64(&g_feed.lastMsgTick, 0, 0) > FEED_RECV_MS) {
        InterlockedExchange(&g_feed.silent, 1);
        WinHttpCloseHandle(g_feed.hWs);
        g_feed.hWs = NULL;
    }
    LeaveCriticalSection(&g_feed.lock);
}

BOOL FeedStart(const FeedConfig* cfg) {
    FILETIME due;
    if (g_feed.running) return TRUE;
    memset(&g_feed, 0, sizeof(g_feed));
    g_feed.cfg = *cfg;
    if (cfg->replayFile) wcscpy_s(g_feed.replay, MAX_PATH, cfg->replayFile);
    g_feed.cfg.replayFile = NULL;
    if (!FeedWriterOpen(&g_feed.w, cfg->mappingName, cfg->instruments, cfg->instrumentCount)) return FALSE;
    // A private copy, not h->instruments (phase 54): the header's table is
    // shared memory any process in the session can write, and the parser
    // and FeedStreamPath must not trust it. FeedWriterOpen has already
    // bounded instrumentCount to FEED_MAX_INSTRUMENTS.
    memcpy(g_feed.ins, cfg->instruments, cfg->instrumentCount * sizeof(FeedInstrument));
    g_feed.cfg.instruments = g_feed.ins;
    InitializeCriticalSection(&g_feed.lock);
    g_feed.hStop = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_feed.timer = g_feed.hStop ? CreateThreadpoolTimer(FeedTick, NULL, NULL) : NULL;
    if (g_feed.timer) {
        ULARGE_INTEGER t;
        t.QuadPart = (ULONGLONG)-10000000LL;   // 1 s from now, relative
        due.dwLowDateTime = t.LowPart;
        due.dwHighDateTime = t.HighPart;
        SetThreadpoolTimer(g_feed.timer, &due, 1000, 0);
    }
    // Without the timer there is no heartbeat and no watchdog, so its
    // failure fails the whole start, not a degraded run (phase 54).
    g_feed.hThread = g_feed.timer ? CreateThread(NULL, 0, FeedThread, NULL, 0, NULL) : NULL;
    if (!g_feed.hThread) {
        if (g_feed.timer) { SetThreadpoolTimer(g_feed.timer, NULL, 0, 0); WaitForThreadpoolTimerCallbacks(g_feed.timer, TRUE); CloseThreadpoolTimer(g_feed.timer); }
        if (g_feed.hStop) CloseHandle(g_feed.hStop);
        DeleteCriticalSection(&g_feed.lock);
        FeedWriterClose(&g_feed.w);
        memset(&g_feed, 0, sizeof(g_feed));
        return FALSE;
    }
    g_feed.running = TRUE;
    return TRUE;
}

// Stop, close whichever of hWs, hRequest and hConnect is open (which cancels
// a pending receive or a pending connect/upgrade call at once - task 6
// measured about 47 ms during a connect; task 1's spike's 1000 ms "cancel"
// figure was its own Closer thread's scheduled sleep before the close, not
// the cancel's own duration), wait up to 10 s, stop the heartbeat, say
// DISCONNECTED and unmap. The request handle closes before its parent
// connect handle. A thread that did not end keeps its resources; the
// process exit is moments away (as NetworkThread) (phase 54: FeedStop
// during FeedConnect). Returns FALSE only if the 10 s join timed out, in
// which case nothing FeedThread can still reach is torn down (see FeedStop
// in feed.h).
BOOL FeedStop(void) {
    BOOL done;
    if (!g_feed.running) return TRUE;
    SetEvent(g_feed.hStop);
    EnterCriticalSection(&g_feed.lock);
    if (g_feed.hWs)      { WinHttpCloseHandle(g_feed.hWs);      g_feed.hWs = NULL; }
    if (g_feed.hRequest) { WinHttpCloseHandle(g_feed.hRequest); g_feed.hRequest = NULL; }
    if (g_feed.hConnect) { WinHttpCloseHandle(g_feed.hConnect); g_feed.hConnect = NULL; }
    LeaveCriticalSection(&g_feed.lock);
    done = (WaitForSingleObject(g_feed.hThread, 10000) == WAIT_OBJECT_0);
    if (g_feed.timer) {
        SetThreadpoolTimer(g_feed.timer, NULL, 0, 0);
        WaitForThreadpoolTimerCallbacks(g_feed.timer, TRUE);
        CloseThreadpoolTimer(g_feed.timer);
        g_feed.timer = NULL;
    }
    g_feed.running = FALSE;
    if (!done) return FALSE;
    CloseHandle(g_feed.hThread);
    CloseHandle(g_feed.hStop);
    DeleteCriticalSection(&g_feed.lock);
    FeedSetConn(&g_feed.w, FEED_ST_DISCONNECTED, 0, 0);
    FeedWriterClose(&g_feed.w);
    memset(&g_feed, 0, sizeof(g_feed));
    return TRUE;
}

void FeedGetStats(FeedStats* out) {
    out->published = g_feed.published;
    out->dropped   = g_feed.dropped;
    out->connects  = g_feed.connects;
    out->state     = (g_feed.running && g_feed.w.hdr) ? g_feed.w.hdr->connState : 0;
}
