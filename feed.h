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
#if defined(_M_IX86)
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

// ARM64 needs a real barrier between a copy and its re-check; x64 and x86
// keep load order, so a compiler barrier is enough there.
#if defined(_M_ARM64)
  #define FEED_LOAD_FENCE() __dmb(_ARM64_BARRIER_ISHLD)
#else
  #define FEED_LOAD_FENCE() _ReadWriteBarrier()
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
