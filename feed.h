// feed.h - the TickC feed, version 1: the contract between TickC (the one
// writer) and its readers (phase 54). A named shared-memory mapping,
// little-endian, natural alignment, only fixed-width integers - so a 32-bit
// writer and a 64-bit reader see the same layout. Every size and offset is
// asserted below. The reader is in this file, as static inline functions,
// so every reader runs the same code; the writer is feed.c. The design is
// docs/specs/2026-09-25-feed-daemon-design.md.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <windows.h>
#if defined(_M_IX86) || defined(_M_X64)
#include <emmintrin.h>
#endif

#define FEED_MAPPING_NAME      L"Local\\TickC.Feed.1"   // major version in the name
#define FEED_MAGIC             0x46434B54u              // "TKCF"
#define FEED_VERSION_MAJOR     1
#define FEED_VERSION_MINOR     0
#define FEED_HEADER_SIZE       4096u
#define FEED_SLOT_SIZE         128u
#define FEED_SLOT_COUNT        32768u                   // a power of two
#define FEED_MAPPING_SIZE      (FEED_HEADER_SIZE + FEED_SLOT_SIZE * FEED_SLOT_COUNT)
#define FEED_MAX_READERS       8
#define FEED_MAX_INSTRUMENTS   16
#define FEED_SEQ_BUSY          (-1LL)     // FeedEvent.seq while the slot is written
#define FEED_NO_INSTRUMENT     0xFFFFu
#define FEED_HEARTBEAT_STALE_US 3000000LL // a heartbeat older than this: the writer hangs

enum { FEED_TRADE = 1, FEED_KLINE = 2, FEED_STATUS = 3 };       // FeedEvent.type
enum { FEED_SRC_BINANCE_SPOT = 1 };                             // FeedEvent.source
enum { FEED_SIDE_BUY = 1, FEED_SIDE_SELL = 2 };                 // the aggressor
enum { FEED_ST_CONNECTING = 1, FEED_ST_CONNECTED = 2, FEED_ST_DISCONNECTED = 3 };
enum { FEED_ASSET_CRYPTO = 1 };
#define FEED_KLINE_CLOSED  0x0001u        // FeedEvent.flags: the bar is final

#pragma pack(push, 8)

typedef struct {                  // 32 bytes
    int64_t  price;               // 10^priceExp
    int64_t  qty;                 // 10^qtyExp
    uint64_t tradeId;             // Binance's, rising per symbol
    uint8_t  side;                // FEED_SIDE_*; Binance "m" = buyer is maker -> SELL
    uint8_t  pad[7];
} FeedTrade;

typedef struct {                  // 64 bytes
    int64_t  openTimeUs;
    uint32_t intervalSec;         // 60 in version 1
    uint32_t tradeCount;
    int64_t  open, high, low, close;   // 10^priceExp
    int64_t  volume;              // base asset, 10^qtyExp
    int64_t  quoteVolume;         // quote asset, 10^priceExp
} FeedKline;

typedef struct {                  // 16 bytes; FeedEvent.instrument = FEED_NO_INSTRUMENT
    uint16_t state;               // FEED_ST_*
    uint16_t pad;
    int32_t  error;               // WinHTTP error or WebSocket close status, 0 = none
    int64_t  retryAtUs;           // next attempt while DISCONNECTED, else 0
} FeedStatus;

typedef struct {                  // 128 bytes: one ring slot
    volatile int64_t seq;         //   0  its sequence number; FEED_SEQ_BUSY while written
    int64_t  tsExchangeUs;        //   8  the exchange's event time
    int64_t  tsRecvUs;            //  16  when TickC read it off the socket
    uint16_t type;                //  24  FEED_TRADE / FEED_KLINE / FEED_STATUS
    uint16_t instrument;          //  26  index into FeedHeader.instruments
    uint16_t source;              //  28  FEED_SRC_*
    uint16_t flags;               //  30
    union {                       //  32
        FeedTrade  trade;
        FeedKline  kline;
        FeedStatus status;
        uint8_t    raw[96];
    } u;
} FeedEvent;

typedef struct {                  // 48 bytes
    char     symbol[16];          // "BTCUSDT", NUL-padded
    char     label[16];           // "BTC/USDT"
    uint16_t source;
    uint8_t  assetClass;          // FEED_ASSET_*
    int8_t   priceExp;            // -8
    int8_t   qtyExp;              // -8
    uint8_t  pad[11];
} FeedInstrument;

typedef struct {                  // 16 bytes
    volatile LONG owner;          // the reader's process id; 0 = free
    volatile LONG ready;          // 1 once the reader's wake event exists
    uint8_t  pad[8];
} FeedReaderSlot;

typedef struct {                  // 128 bytes: the latest per instrument
    volatile int64_t lock;        // seqlock: odd while the writer is in it
    int64_t   updatedUs;
    FeedTrade trade;              // tradeId 0 = none yet
    FeedKline kline;              // openTimeUs 0 = none yet
    uint8_t   pad[16];
} FeedSnapshot;

typedef struct {                  // 4096 bytes: page 0 of the mapping
    // Line 0: identity, written once per writer session. magic is stored last.
    volatile uint32_t magic;      //    0
    uint16_t versionMajor;        //    4
    uint16_t versionMinor;        //    6
    uint32_t headerSize;          //    8
    uint32_t slotSize;            //   12
    uint32_t slotCount;           //   16
    uint32_t instrumentCount;     //   20
    volatile uint32_t writerPid;  //   24
    uint32_t pad0;                //   28
    volatile int64_t sessionUs;   //   32  the writer's start; a change = a new session
    uint8_t  pad1[24];            //   40
    // Line 1: hot, written by the writer only.
    volatile int64_t writeSeq;    //   64  the next seq to be written; the first event is 1
    volatile int64_t heartbeatUs; //   72  every second while the writer process lives
    volatile LONG    connState;   //   80  the current FEED_ST_*
    uint8_t  pad2[44];            //   84
    FeedReaderSlot readers[FEED_MAX_READERS];         //  128
    FeedInstrument instruments[FEED_MAX_INSTRUMENTS]; //  256
    FeedSnapshot   snapshots[FEED_MAX_INSTRUMENTS];   // 1024
    uint8_t  reserved[1024];      // 3072
} FeedHeader;

#pragma pack(pop)

C_ASSERT(sizeof(FeedTrade) == 32);      C_ASSERT(sizeof(FeedKline) == 64);
C_ASSERT(sizeof(FeedStatus) == 16);     C_ASSERT(sizeof(FeedEvent) == FEED_SLOT_SIZE);
C_ASSERT(sizeof(FeedInstrument) == 48); C_ASSERT(sizeof(FeedReaderSlot) == 16);
C_ASSERT(sizeof(FeedSnapshot) == 128);  C_ASSERT(sizeof(FeedHeader) == FEED_HEADER_SIZE);
C_ASSERT(offsetof(FeedEvent, u) == 32);
C_ASSERT(offsetof(FeedHeader, writeSeq) == 64);
C_ASSERT(offsetof(FeedHeader, connState) == 80);
C_ASSERT(offsetof(FeedHeader, readers) == 128);
C_ASSERT(offsetof(FeedHeader, instruments) == 256);
C_ASSERT(offsetof(FeedHeader, snapshots) == 1024);
C_ASSERT((FEED_SLOT_COUNT & (FEED_SLOT_COUNT - 1)) == 0);

// Slot s lives at FEED_HEADER_SIZE + FEED_SLOT_INDEX(s) * FEED_SLOT_SIZE.
#define FEED_SLOT_INDEX(s) ((size_t)((uint64_t)(s) & (FEED_SLOT_COUNT - 1)))

// The fences around a copy that a sequence number guards: a ring slot, or
// a snapshot under its seqlock. FEED_LOAD_FENCE goes between the reader's
// copy and its re-check; FEED_STORE_FENCE goes between the writer's slot
// copy and its release store of seq (a snapshot's writer needs none: the
// InterlockedIncrement64 that closes its seqlock is a full barrier). ARM64
// reorders loads, so its reader needs a real barrier; its writer's release
// store already orders the copy before seq. x86 and x64 keep the order of
// plain loads and of plain stores, but memcpy may compile to a fast-string
// operation (rep movs), and the SDM (Vol. 3A, 8.2.4.1) relaxes the order of
// the accesses inside one: its stores may complete out of order, and it
// says nothing explicit about its loads against a later load. LFENCE (every
// earlier load completes first) and SFENCE (every earlier store becomes
// visible before any later one) guard against
// that ordering; they are defense in depth, not the fix of a seen failure -
// no tear was ever traced to it (phase 54). x64 gets them too: the same
// memcpy may become rep movs there, and the rules are the same.
#if defined(_M_ARM64)
  #define FEED_LOAD_FENCE()  __dmb(_ARM64_BARRIER_ISHLD)
  #define FEED_STORE_FENCE() _ReadWriteBarrier()
#else
  #define FEED_LOAD_FENCE()  do { _ReadWriteBarrier(); _mm_lfence(); _ReadWriteBarrier(); } while (0)
  #define FEED_STORE_FENCE() do { _ReadWriteBarrier(); _mm_sfence(); _ReadWriteBarrier(); } while (0)
#endif

// Atomic 64-bit access. On x86, winnt.h's ReadAcquire64 and WriteRelease64
// are plain accesses to a volatile LONG64, which the compiler splits into two
// 32-bit moves: a reader could see half an old and half a new number. SSE2's
// movq is one 8-byte access, atomic for an aligned address; the compiler
// barrier keeps the order (x86 itself keeps the order of loads and of
// stores). Not InterlockedCompareExchange64 as a load: it writes, and a
// reader may have mapped the region read-only.
#if defined(_M_IX86)
static __inline int64_t FeedLoadNoFence64(const volatile int64_t* p) {
    int64_t out;
    _mm_storel_epi64((__m128i*)(void*)&out, _mm_loadl_epi64((const __m128i*)(const void*)p));
    return out;
}
static __inline int64_t FeedLoadAcquire64(const volatile int64_t* p) {
    int64_t v = FeedLoadNoFence64(p);
    _ReadWriteBarrier();
    return v;
}
static __inline void FeedStoreRelease64(volatile int64_t* p, int64_t v) {
    _ReadWriteBarrier();
    _mm_storel_epi64((__m128i*)(void*)p, _mm_loadl_epi64((const __m128i*)(const void*)&v));
}
#else
static __inline int64_t FeedLoadNoFence64(const volatile int64_t* p) {
    return ReadNoFence64((LONG64 const volatile*)p);
}
static __inline int64_t FeedLoadAcquire64(const volatile int64_t* p) {
    return ReadAcquire64((LONG64 const volatile*)p);
}
static __inline void FeedStoreRelease64(volatile int64_t* p, int64_t v) {
    WriteRelease64((LONG64 volatile*)p, v);
}
#endif

// Microseconds since 1970-01-01 UTC, the feed's clock.
static __inline int64_t FeedNowUs(void) {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    return (int64_t)(((((uint64_t)ft.dwHighDateTime) << 32) | ft.dwLowDateTime)
                     - 116444736000000000ULL) / 10;
}

// ---------------------------------------------------------------------------
// The reader
// ---------------------------------------------------------------------------

enum { FEED_OK = 0, FEED_EMPTY, FEED_LAPPED, FEED_NEW_SESSION,
       FEED_NO_WRITER, FEED_BAD_VERSION, FEED_NO_SLOT };

typedef struct {
    HANDLE         hMap;
    FeedHeader*    hdr;        // written to only through a claimed slot
    const uint8_t* ring;
    int64_t        next;       // the next seq to read
    int64_t        session;
    int            slot;       // -1 = not registered for wake-ups
    HANDLE         hWake;      // the slot's event, or NULL
    HANDLE         hWriter;    // the writer process, for the wait
    uint32_t       writerPid;
} FeedReader;

static __inline uint32_t FeedMagic(const FeedHeader* h) {
    return (uint32_t)ReadAcquire((LONG const volatile*)(const volatile void*)&h->magic);
}

static __inline void FeedReopenWriter(FeedReader* r) {
    if (r->hWriter) CloseHandle(r->hWriter);
    r->writerPid = r->hdr->writerPid;
    r->hWriter = OpenProcess(SYNCHRONIZE, FALSE, r->writerPid);
}

static __inline void FeedClose(FeedReader* r) {
    // A slot is only ever claimed through a writable view, so a read-only
    // reader never gets here with slot >= 0 and never writes.
    if (r->slot >= 0 && r->hdr) {
        FeedReaderSlot* s = &r->hdr->readers[r->slot];
        InterlockedExchange(&s->ready, 0);
        InterlockedCompareExchange(&s->owner, 0, (LONG)GetCurrentProcessId());
    }
    if (r->hWake)   CloseHandle(r->hWake);
    if (r->hWriter) CloseHandle(r->hWriter);
    if (r->hdr)     UnmapViewOfFile(r->hdr);
    if (r->hMap)    CloseHandle(r->hMap);
    memset(r, 0, sizeof(*r));
    r->slot = -1;
}

// Opens the feed. wakeups: claim a reader slot and an event, so FeedWait
// returns as soon as the writer publishes; without it the view is read-only
// and FeedWait only watches the writer. The reader starts at the present.
static __inline int FeedOpen(FeedReader* r, const wchar_t* name, BOOL wakeups) {
    DWORD access = wakeups ? (FILE_MAP_READ | FILE_MAP_WRITE) : FILE_MAP_READ;
    FeedHeader* h;
    memset(r, 0, sizeof(*r));
    r->slot = -1;
    if (!name) name = FEED_MAPPING_NAME;
    r->hMap = OpenFileMappingW(access, FALSE, name);
    if (!r->hMap) return FEED_NO_WRITER;
    h = (FeedHeader*)MapViewOfFile(r->hMap, access, 0, 0, FEED_MAPPING_SIZE);
    if (!h) { FeedClose(r); return FEED_NO_WRITER; }
    r->hdr  = h;
    r->ring = (const uint8_t*)h + FEED_HEADER_SIZE;
    if (FeedMagic(h) != FEED_MAGIC) { FeedClose(r); return FEED_NO_WRITER; }
    if (h->versionMajor != FEED_VERSION_MAJOR || h->headerSize != FEED_HEADER_SIZE ||
        h->slotSize != FEED_SLOT_SIZE || h->slotCount != FEED_SLOT_COUNT) {
        FeedClose(r);
        return FEED_BAD_VERSION;
    }
    r->session = FeedLoadAcquire64(&h->sessionUs);
    r->next    = FeedLoadAcquire64(&h->writeSeq);
    FeedReopenWriter(r);
    if (wakeups) {
        LONG pid = (LONG)GetCurrentProcessId();
        for (int i = 0; i < FEED_MAX_READERS; ++i) {
            wchar_t evName[96];
            if (InterlockedCompareExchange(&h->readers[i].owner, pid, 0) != 0) continue;
            swprintf_s(evName, 96, L"%s.R%d", name, i);
            r->hWake = CreateEventW(NULL, FALSE, FALSE, evName);
            if (!r->hWake) { InterlockedExchange(&h->readers[i].owner, 0); break; }
            r->slot = i;
            InterlockedExchange(&h->readers[i].ready, 1);
            break;
        }
        if (r->slot < 0) { FeedClose(r); return FEED_NO_SLOT; }
    }
    return FEED_OK;
}

// Start at the oldest event still in the ring, not at the present.
static __inline void FeedRewind(FeedReader* r) {
    int64_t w = FeedLoadAcquire64(&r->hdr->writeSeq);
    r->next = (w > (int64_t)FEED_SLOT_COUNT) ? w - (int64_t)FEED_SLOT_COUNT : 1;
}

// The next event. FEED_LAPPED: the writer overtook this reader; *lost events
// were skipped and the reader is now at the present - read the snapshots.
// FEED_NEW_SESSION: the writer restarted; the reader is at the present -
// read the snapshots. The snapshots must be read after this call returns,
// never before: everything below the new position is in them.
static __inline int FeedNext(FeedReader* r, FeedEvent* ev, int64_t* lost) {
    const FeedHeader* h = r->hdr;
    const FeedEvent* slot;
    int64_t want, w, sess;
    if (lost) *lost = 0;
    if (FeedMagic(h) != FEED_MAGIC) return FEED_NO_WRITER;
    sess = FeedLoadAcquire64(&h->sessionUs);
    if (sess != r->session) {
        r->session = sess;
        r->next = FeedLoadAcquire64(&h->writeSeq);
        FeedReopenWriter(r);
        return FEED_NEW_SESSION;
    }
    // No event is numbered below 1: a slot not yet written holds seq 0 and a
    // slot being written holds FEED_SEQ_BUSY, so a position below 1 would
    // match one of them in both checks below and return a slot that is no
    // event. FeedOpen, FeedRewind and a resync never go below 1; a caller
    // that sets next by hand starts at the first event, as FeedRewind does
    // (phase 54).
    want = (r->next < 1) ? 1 : r->next;
    w = FeedLoadAcquire64(&h->writeSeq);
    if (want >= w) return FEED_EMPTY;
    if (w - want > (int64_t)FEED_SLOT_COUNT) goto lapped;
    slot = (const FeedEvent*)(r->ring + FEED_SLOT_INDEX(want) * FEED_SLOT_SIZE);
    if (FeedLoadAcquire64(&slot->seq) != want) goto lapped;     // BUSY or a newer seq
    memcpy(ev, (const void*)slot, FEED_SLOT_SIZE);
    FEED_LOAD_FENCE();
    if (FeedLoadNoFence64(&slot->seq) != want) goto lapped;    // overwritten during the copy
    ev->seq = want;   // the copy's own seq may be torn; the checks above are what count
    r->next = want + 1;
    return FEED_OK;
lapped:
    w = FeedLoadAcquire64(&h->writeSeq);
    if (lost) *lost = w - want;
    r->next = w;
    return FEED_LAPPED;
}

// The latest trade and kline of one instrument. FALSE when the index is out
// of range or the writer held the lock through 64 tries.
static __inline BOOL FeedReadSnapshot(const FeedReader* r, unsigned instrument, FeedSnapshot* out) {
    const FeedSnapshot* s;
    if (instrument >= r->hdr->instrumentCount || instrument >= FEED_MAX_INSTRUMENTS) return FALSE;
    s = &r->hdr->snapshots[instrument];
    for (int t = 0; t < 64; ++t) {
        int64_t a = FeedLoadAcquire64(&s->lock);
        if (!(a & 1)) {
            memcpy(out, (const void*)s, sizeof(*out));
            FEED_LOAD_FENCE();
            if (FeedLoadNoFence64(&s->lock) == a) { out->lock = a; return TRUE; }
        }
        YieldProcessor();
    }
    return FALSE;
}

static __inline BOOL FeedWriterAlive(const FeedReader* r) {
    if (FeedMagic(r->hdr) != FEED_MAGIC) return FALSE;
    return FeedNowUs() - FeedLoadAcquire64(&r->hdr->heartbeatUs) <= FEED_HEARTBEAT_STALE_US;
}

// Waits up to ms for data. WAIT_OBJECT_0: the writer published (registered
// readers only). WAIT_OBJECT_0 + 1: the writer exited, or its heartbeat is
// stale. WAIT_TIMEOUT: nothing happened.
static __inline DWORD FeedWait(FeedReader* r, DWORD ms) {
    HANDLE hs[2];
    DWORD n = 0, x;
    if (r->hWake)   hs[n++] = r->hWake;
    if (r->hWriter) hs[n++] = r->hWriter;
    if (n == 0) {
        Sleep(ms);
        x = WAIT_TIMEOUT;
    } else {
        x = WaitForMultipleObjects(n, hs, FALSE, ms);
        if (r->hWriter && x == WAIT_OBJECT_0 + n - 1) return WAIT_OBJECT_0 + 1;
    }
    if (x == WAIT_TIMEOUT && !FeedWriterAlive(r)) return WAIT_OBJECT_0 + 1;
    return x;
}

// ---------------------------------------------------------------------------
// The writer (feed.c; TickC and the tests only)
// ---------------------------------------------------------------------------

typedef struct {
    HANDLE      hMap;
    FeedHeader* hdr;
    uint8_t*    ring;
    HANDLE      hWake[FEED_MAX_READERS];    // the readers' events, opened lazily
    LONG        wakePid[FEED_MAX_READERS];  // the owner each handle was opened for
    wchar_t     name[64];
} FeedWriter;

BOOL FeedWriterOpen(FeedWriter* w, const wchar_t* name, const FeedInstrument* ins, unsigned count);
void FeedPublish(FeedWriter* w, const FeedEvent* ev);
void FeedSetConn(FeedWriter* w, int state, int32_t error, int64_t retryAtUs);
void FeedHeartbeat(FeedWriter* w);
void FeedSweepReaders(FeedWriter* w);
void FeedWriterClose(FeedWriter* w);

// The parser (feed.c). FeedParseFixed8: a decimal string -> an exact int64
// at 10^-8; 1 on success. FeedParseMessage: one combined-stream message ->
// one event, FEED_PARSE_OK, FEED_PARSE_BAD or FEED_PARSE_UNKNOWN (a symbol
// not in the table).
enum { FEED_PARSE_OK = 1, FEED_PARSE_BAD = 0, FEED_PARSE_UNKNOWN = -1 };
int FeedParseFixed8(const char* s, const char* e, int64_t* out);
int FeedParseMessage(const char* msg, size_t len, const FeedInstrument* ins, unsigned count,
                     int64_t recvUs, FeedEvent* ev);

// ---------------------------------------------------------------------------
// The feed thread (feed.c; TickC and the tests only).
// ---------------------------------------------------------------------------

typedef struct {
    const wchar_t*        mappingName;     // NULL = FEED_MAPPING_NAME
    const wchar_t*        replayFile;      // NULL = the network; else a .jsonl replayed through the parser
    void**                pSession;        // a HINTERNET*: the app's WinHTTP session, read under *sessionLock
    CRITICAL_SECTION*     sessionLock;     // may be NULL (tests)
    DWORD               (*backoffMs)(int failures, ULONGLONG seed);
    const FeedInstrument* instruments;
    unsigned              instrumentCount;
} FeedConfig;

typedef struct { LONG published, dropped, connects, state; } FeedStats;

BOOL FeedStart(const FeedConfig* cfg);   // FALSE: no mapping; nothing started
void FeedStop(void);                     // safe to call when not started
void FeedGetStats(FeedStats* out);
