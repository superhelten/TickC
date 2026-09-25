# Phase 54 — the TickC feed: a shared-memory event stream and a daemon mode

Date: 2026-09-25. Starting point: `main` at `d32a285` (phase 53 merged).
Branch: `phase-54`.

## Purpose

TickC becomes the market-data feed handler for a separate product: a
command-driven terminal on the Bloomberg Professional model, built in its own
repository. The terminal will show charts, an order book and a geographic map
of ship (AIS) and aircraft (ADS-B) traffic next to the prices. This phase is
sub-project (a) of that plan, the foundation the rest builds on:

1. a fixed binary event format, published as a public header (`feed.h`) the
   terminal repository can use as is;
2. a lock-free, single-writer ring in named shared memory, with a per-symbol
   snapshot table;
3. a live Binance WebSocket stream (trades and 1-minute klines) in TickC,
   written into that ring;
4. a `--daemon` mode: TickC without a tray icon, panel or desktop surface;
5. a minimal consumer, `tests/feed_probe.exe`, that proves the data arrives.

TickC keeps everything it does today. The tray icon, the panel, desktop mode
and the REST fetches behind the panel's charts are unchanged. The feed runs
beside them.

## Scope

In scope: everything listed under Purpose, for the four symbols TickC has today
(BTC, ETH, SOL, BNB against USDT).

Out of scope, each a later sub-project: the terminal itself; historical candles
over the feed (a request channel); order-book depth; AIS; ADS-B;
stocks and commodities; deduplication across sources; starting the daemon at
sign-in; showing the feed's state in TickC's own UI.

## Constraints

- Plain C, Win32 and WinHTTP only. No new libraries. `/W4` clean.
- The one-exe build stays. TickC goes from two source files to three
  (`tickc.c`, `chart.c`, `feed.c`), plus `feed.h`. This is a deliberate step
  away from "two files": the feed is a contract of its own.
- The feed must never break the TickC that runs today. If shared memory cannot
  be created, the GUI runs exactly as before, without a feed.
- All repository content is in English.
- Tests never show windows while the user is at the machine (see the probe
  convention in `WORKLOG.md`): run hidden, on an idle machine.

## Architecture

```
Binance WebSocket  wss://stream.binance.com:443/stream?streams=
                   btcusdt@trade/btcusdt@kline_1m/ethusdt@trade/... (8 streams)
        |  WinHTTP WebSocket, thread FeedThread (feed.c)
        v
  parse JSON -> FeedEvent (128 bytes, fixed layout)
        |
        v
  FeedWriter: snapshot table + ring in the mapping Local\TickC.Feed.1
        |  SetEvent on each registered reader
        v
  Readers: tests/feed_probe.exe now, the terminal later (reader code in feed.h)
```

- `FeedThread` is a new thread beside `NetworkThread`. `NetworkThread` and its
  REST fetches are not changed.
- Only the main instance, the process that owns `Local\TickC.MainInstance`,
  runs `FeedThread`. A `--dup` process never does. The protocol allows exactly
  one writer.
- `feed.h` is the public contract: the layout, the constants and the reader
  (as `static __inline` functions). `feed.c` holds the writer, the WebSocket
  client and the parser. `tickc.c` starts and stops the feed and implements
  `--daemon`.

## The event format (`feed.h`)

### Decisions

- **Fixed point, not `double`.** Prices and quantities are `int64_t` scaled
  by `10^exp`, with `exp = -8` for every instrument in version 1. Binance sends
  numbers as strings with at most 8 decimals, so parsing into an `int64_t` is
  exact, with no `strtod` and no rounding. Equal values are bitwise equal,
  which later deduplication depends on. Each instrument carries its own
  exponents, so a stock can use `10^-4` later without a format change. The
  largest value is about 9.22 × 10^10 whole units.
- **Time** is `int64_t` microseconds since 1970-01-01 UTC. Each event carries
  the exchange's time and TickC's receive time. Binance sends milliseconds,
  multiplied by 1000.
- **128-byte slots**, two cache lines. A kline alone is 64 bytes of payload
  behind a 32-byte event header. The spare room leaves space for order-book
  events later without a layout change.
- **`#pragma pack(push, 8)` with explicit padding, not `pack(1)`.** Every field
  keeps its natural alignment, which the `Interlocked*` calls and the atomic
  64-bit loads and stores on the sequence fields (below) require (on ARM64 in
  particular). `push, 8` makes the layout independent of a consumer's `/Zp`
  setting. `C_ASSERT` checks every size and the key offsets at compile time.
- **The union is named (`u`).** An anonymous union raises C4201 under `/W4`.
- **Byte order** is little-endian (all Windows targets).

### The header

```c
// feed.h - the TickC feed, version 1: the contract between TickC (the one
// writer) and its readers. A named shared-memory mapping, little-endian,
// natural alignment. Every size and offset is asserted below.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <windows.h>

#define FEED_MAPPING_NAME      L"Local\\TickC.Feed.1"   // major version in the name
#define FEED_MAGIC             0x46434B54u              // "TKCF"
#define FEED_VERSION_MAJOR     1
#define FEED_VERSION_MINOR     0
#define FEED_HEADER_SIZE       4096u
#define FEED_SLOT_SIZE         128u
#define FEED_SLOT_COUNT        32768u                   // a power of two
#define FEED_MAPPING_SIZE      (FEED_HEADER_SIZE + FEED_SLOT_SIZE * FEED_SLOT_COUNT) // 4 198 400
#define FEED_MAX_READERS       8
#define FEED_MAX_INSTRUMENTS   16
#define FEED_SEQ_BUSY          (-1LL)     // FeedEvent.seq while the slot is written
#define FEED_NO_INSTRUMENT     0xFFFFu

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

// Slot s lives at FEED_HEADER_SIZE + (s & (FEED_SLOT_COUNT - 1)) * FEED_SLOT_SIZE.

// The fences around a copy that a sequence number guards: LOAD between the
// reader's copy and its re-check, STORE between the writer's slot copy and
// its release of seq. See "Atomic 64-bit access".
#if defined(_M_ARM64)
  #define FEED_LOAD_FENCE()  __dmb(_ARM64_BARRIER_ISHLD)
  #define FEED_STORE_FENCE() _ReadWriteBarrier()
#else
  #define FEED_LOAD_FENCE()  do { _ReadWriteBarrier(); _mm_lfence(); _ReadWriteBarrier(); } while (0)
  #define FEED_STORE_FENCE() do { _ReadWriteBarrier(); _mm_sfence(); _ReadWriteBarrier(); } while (0)
#endif
```

### Atomic 64-bit access

TickC builds as **32-bit x86** (`vcvars32`); the terminal may be x64. The
layout is the same for both, because the shared structs hold only
fixed-width integers: no pointers, no `size_t`, no `long`.

On x86, `ReadAcquire64`, `ReadNoFence64` and `WriteRelease64` from `winnt.h`
are plain accesses to a `volatile LONG64`, which the compiler splits into two
32-bit moves: a reader could see half an old and half a new sequence number.
`feed.h` therefore has its own three helpers, used for every 64-bit field that
one side writes while the other reads (`FeedEvent.seq`, `writeSeq`,
`heartbeatUs`, `sessionUs`, `FeedSnapshot.lock`):

```c
static __inline int64_t FeedLoadAcquire64(const volatile int64_t* p);
static __inline int64_t FeedLoadNoFence64(const volatile int64_t* p);
static __inline void    FeedStoreRelease64(volatile int64_t* p, int64_t v);
```

- x64 and ARM64: `ReadAcquire64`, `ReadNoFence64` and `WriteRelease64`,
  which are single 64-bit accesses there.
- x86: SSE2 `movq` (`_mm_loadl_epi64` / `_mm_storel_epi64`), which is atomic
  for an 8-byte-aligned address, with `_ReadWriteBarrier()` after a load and
  before a store (x86 keeps the order of loads and of stores; the barrier
  stops the compiler). SSE2 is MSVC's default on x86.
- x86 and x64, the slot copy: `memcpy` may compile to a fast-string
  operation (`rep movs`, as it does in the x86 build), and the SDM (Vol. 3A,
  8.2.4.1) relaxes the order of the accesses inside one. So the reader puts
  an `LFENCE` after its copy, before the re-check of `seq`
  (`FEED_LOAD_FENCE`), and the writer an `SFENCE` after its copy, before the
  release of `seq` (`FEED_STORE_FENCE`). They are a guard, not the fix of a
  seen failure; each costs about 2 ns. x64 gets them too, because the rules
  and the `memcpy` are the same there. ARM64 already has `dmb ishld` on the
  reader and a store-release on the writer.
- Not `InterlockedCompareExchange64(p, 0, 0)` as a load: it writes, and a
  reader may have mapped the region read-only.

The writer's read-modify-writes (`InterlockedExchange64`,
`InterlockedIncrement64`) exist on all three targets. The reader slots are
32-bit `LONG`s, whose aligned accesses are atomic everywhere.
`tests/feed_test.c` is built as x86, like TickC, so the stress test runs the
x86 path.

### Names

- Production mapping: `Local\TickC.Feed.1`. A reader's wake event is the
  mapping's name plus `.R<n>`, where `n` is its slot (`Local\TickC.Feed.1.R3`).
- The test build (`TICKER_PROBE`) uses `Local\TickerTest.Feed.1.<hash>`, with
  the same FNV-1a hash of the exe path as its main-instance mutex, so a test
  run never writes into the feed of the TickC the user runs, and two test exes
  do not share a feed.
- The writer and `FeedOpen` take the mapping name as a parameter; `NULL`
  means `FEED_MAPPING_NAME`.

### Rules for the format

- The first event is `seq` 1, so 0 always means "none". The number only rises,
  also across writer restarts.
- A new minor version may only add event types and use reserved or padding
  space. Anything that moves an offset needs a new major version, and with it
  a new mapping name, so an old reader can never read a new layout.
- 32 768 slots hold at least 30 s of BTC at its busiest bursts and minutes of
  normal traffic. Doubling the ring is a change of one constant, and a new
  major version.

## Writing and reading

### The writer, one event

Only `FeedThread` publishes.

```
s = hdr->writeSeq                              // the writer's own; a plain read
1. update the instrument's snapshot            // BEFORE the ring: see Resync
2. InterlockedExchange64(&slot->seq, FEED_SEQ_BUSY)   // a full barrier
3. copy the fields after seq (tsExchangeUs .. u); FEED_STORE_FENCE()
4. FeedStoreRelease64(&slot->seq, s)               // the contents before the number
5. FeedStoreRelease64(&hdr->writeSeq, s + 1)
6. SetEvent on each reader slot with ready == 1
```

Status events have no instrument and skip step 1. The writer also stores
`connState` before it publishes a status event.

### The reader, `FeedNext`

```
want = max(r->next, 1)
w = FeedLoadAcquire64(&hdr->writeSeq)
if want >= w                   -> FEED_EMPTY
if w - want > FEED_SLOT_COUNT  -> FEED_LAPPED
a = FeedLoadAcquire64(&slot->seq); if a != want -> FEED_LAPPED   // BUSY or a newer seq
memcpy(ev, slot, FEED_SLOT_SIZE)
FEED_LOAD_FENCE()
if FeedLoadNoFence64(&slot->seq) != want        -> FEED_LAPPED   // overwritten during the copy
r->next = want + 1             -> FEED_OK
```

A position below 1 starts at event 1, as `FeedRewind` does, because seq 0
(a slot never written) and `FEED_SEQ_BUSY` (-1, a slot being written) must
never match `want`.

On `FEED_LAPPED` the reader reports how many events it lost and resyncs (below).
It does not try to catch up: it would only be lapped again, and the terminal
needs the present. The loss is a hole in the trade tape, plus any closed 1m
bar that fell into the hole. Filling it needs history, which is out of scope.

### The snapshot table (seqlock)

- The writer: `InterlockedIncrement64(&lock)` (now odd), write `updatedUs`,
  `trade` or `kline`, then `InterlockedIncrement64(&lock)` (even again). Both
  increments are full barriers.
- The reader: `a = FeedLoadAcquire64(&lock)`; if `a` is odd, retry; copy;
  `FEED_LOAD_FENCE()`; if `FeedLoadNoFence64(&lock) != a`, retry. At most 64 tries
  with `YieldProcessor()` between them; the writer holds the lock for well
  under a microsecond. After 64 failed tries `FeedReadSnapshot` returns FALSE.

### Resync

On open, after `FEED_LAPPED` and on a new session:

```
w = FeedLoadAcquire64(&hdr->writeSeq)
read every snapshot and connState
r->next = w
```

Because the writer updates the snapshot before the ring, every event with a
number below `w` is already in the snapshot. Nothing falls between the two.
An event may arrive twice (once in the snapshot, once from the ring); that is
harmless, because trades carry `tradeId` and a kline event is the bar's whole
state.

### Waking readers

- Registration is optional. A reader that polls once per frame (a render loop)
  needs no slot and may map the region read-only. Only a reader that wants to
  be woken needs write access, to claim a slot.
- To register: `InterlockedCompareExchange(&slot->owner, pid, 0)`; create an
  auto-reset event named after the slot; `WriteRelease(&slot->ready, 1)`.
  No slot free gives `FEED_NO_SLOT`.
- The writer opens a reader's event (`OpenEventW`, `EVENT_MODIFY_STATE`) the
  first time it sees `ready` on that slot, and keeps the handle in a cache that
  only `FeedThread` touches.
- A reader waits on its event and on the writer process
  (`OpenProcess(SYNCHRONIZE, writerPid)`), so it learns at once when TickC
  exits. After 1 s without a wake-up it checks `heartbeatUs` and `sessionUs`.
  A heartbeat older than 3 s means the writer hangs: `FEED_NO_WRITER`.

### Cleanup

- Dead readers: at most once per second, between messages and during the
  backoff wait, `FeedThread` checks every slot with an owner. When the
  process is gone, it closes its cached event handle, sets `ready` to 0 and
  then `owner` to 0. A reader that closes cleanly does the same itself.
- A reused process id can keep a dead reader's slot taken until that other
  process also exits. This is a known, documented limit; with 8 slots and one
  terminal it is not expected to matter.

### Writer restarts

If the mapping still exists (a reader holds it open), `CreateFileMappingW`
returns `ERROR_ALREADY_EXISTS`. The writer then sets `magic` to 0, writes the
identity fields, the instrument table, a new `sessionUs` and its own
`writerPid`, and finally stores `magic` with a release. `writeSeq` continues
from where it was. While `magic` is 0 the identity and instrument fields may
be half written, so a reader that sees `magic != FEED_MAGIC` treats it as a
transient `FEED_NO_WRITER` and reads nothing else until `magic` is back.
A reader sees the new `sessionUs`, returns
`FEED_NEW_SESSION`, resyncs, and opens the new writer process for its wait.

### The reader API (`feed.h`, `static __inline`)

```c
typedef struct {
    HANDLE            hMap;
    const FeedHeader* hdr;
    const uint8_t*    ring;
    int64_t           next;
    int64_t           session;
    int               slot;       // -1 = not registered
    HANDLE            hWake;      // the slot's event, or NULL
    HANDLE            hWriter;    // the writer process, for the wait
    uint32_t          writerPid;
} FeedReader;

enum { FEED_OK = 0, FEED_EMPTY, FEED_LAPPED, FEED_NEW_SESSION,
       FEED_NO_WRITER, FEED_BAD_VERSION, FEED_NO_SLOT };

int   FeedOpen(FeedReader* r, const wchar_t* name, BOOL wakeups);
void  FeedRewind(FeedReader* r);           // start at the oldest event still in the ring
int   FeedNext(FeedReader* r, FeedEvent* ev, int64_t* lost);
BOOL  FeedReadSnapshot(const FeedReader* r, unsigned instrument, FeedSnapshot* out);
DWORD FeedWait(FeedReader* r, DWORD ms);   // WAIT_OBJECT_0 = data, +1 = writer gone, WAIT_TIMEOUT
void  FeedClose(FeedReader* r);
```

`FeedOpen` fails with `FEED_NO_WRITER` when the mapping does not exist or
`magic` is not set, and with `FEED_BAD_VERSION` when `versionMajor`,
`headerSize`, `slotSize` or `slotCount` differ from the header the reader was
built with. `FeedNext` returns `FEED_NEW_SESSION` once when `sessionUs` has
changed, after it has resynced. A reader opened without wake-ups has no
event: its `FeedWait` waits on the writer process alone, so it returns on
the timeout or when the writer exits.

### Security

`Local\` limits the mapping to the user's own Windows session. Any process in
that session can still read and write it. For a local tool this is accepted.

## The Binance stream

### Connection

- One WebSocket to
  `wss://stream.binance.com:443/stream?streams=` with `<sym>@trade` and
  `<sym>@kline_1m` for each of the four symbols. Port 443, not 9443, because 443
  passes firewalls.
- It uses TickC's existing `hSession`, which carries the phase 46 proxy
  handling. WinHTTP session handles are thread-safe.
- The upgrade: `WinHttpOpenRequest`, `WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET`,
  `WinHttpSendRequest`, `WinHttpReceiveResponse`,
  `WinHttpWebSocketCompleteUpgrade`.
- The loop: `WinHttpWebSocketReceive` into a 64 KB buffer. Fragments
  (`WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE`) are joined until the message
  ends (`..._UTF8_MESSAGE_BUFFER_TYPE`). A message over 64 KB is dropped and
  counted. A close frame ends the connection.
- Binance pings every 20 s and closes every connection after 24 h. WinHTTP
  answers pings itself.

### Silence and reconnection

- Binance sends a kline update about every 2 s per symbol, so 10 s without any
  message means the line is dead.
- Task 1's spike measured that the receive timeout, set with
  `WinHttpSetTimeouts` on the WebSocket's own request handle before the
  upgrade, does not bound an ongoing `WinHttpWebSocketReceive`: a receive on
  a silent socket did not return within 45 s. That 10 s value stays on the
  request handle, where it still bounds the upgrade request itself; the
  session keeps its own 5 s for the REST fetches.
- The watchdog is what actually bounds silence. The same thread-pool timer
  that writes the heartbeat also checks, once a second, how long it has been
  since the last message; past 10 s it closes the WebSocket handle from
  outside `FeedThread`. Task 1 measured that closing a WinHTTP handle from
  another thread ends a pending call in about 1 s. The pending receive then
  returns a cancellation, not a timeout; because the watchdog closed the
  handle, not a `FeedStop`, `FeedThread` reports the dead line as
  `ERROR_WINHTTP_TIMEOUT` instead, which is what a dead line means to a
  consumer, and reconnects.
- `FeedStop` closes whichever of the WebSocket, the request and the connect
  handle is open the same way, so a stop asked while still inside
  `FeedConnect` also ends within milliseconds (about 47 ms, measured at
  `WinHttpSendRequest`), instead of waiting out WinHTTP's own resolve,
  connect and send timeouts.
- The wait before the next attempt follows `NetBackoffMs` (the jittered curve
  the rest of TickC uses). A connection that held `CONNECTED` for at least
  60 s, such as Binance's 24 h cut, resets the failure count, so the next
  attempt goes at once; the 60 s is counted from `CONNECTED`, not from
  before the connect attempt.
- Every change is published as `FEED_STATUS` and stored in `connState`:
  `CONNECTING` before an attempt, `CONNECTED` after the upgrade,
  `DISCONNECTED` with the error and `retryAtUs` after a failure.

### Heartbeat

A thread-pool timer writes `heartbeatUs` every second, which means "the
writer process lives and does not hang", independent of the line; the
line's own health is `connState`. The same timer also runs the watchdog
described above, which closes a silent WebSocket.

### Parsing

The combined stream wraps every message: `{"stream":"btcusdt@trade","data":{...}}`.

Trade (`data`):
```json
{"e":"trade","E":1727222400123,"s":"BTCUSDT","t":3871234567,
 "p":"64123.45000000","q":"0.00150000","T":1727222400121,"m":true,"M":true}
```

Kline (`data`):
```json
{"e":"kline","E":1727222400500,"s":"BTCUSDT","k":{"t":1727222400000,
 "T":1727222459999,"s":"BTCUSDT","i":"1m","f":1,"L":2,"o":"64100.00000000",
 "c":"64123.45000000","h":"64130.00000000","l":"64090.10000000",
 "v":"12.34500000","n":321,"x":false,"q":"791234.56780000","V":"6.1",
 "Q":"391000.1","B":"0"}}
```

- `ParseFixed8(const char* s, const char* end, int64_t* out)` turns a decimal
  string into an exact `int64_t` at `10^-8`. It rejects an empty string, a
  sign, an exponent, more than 8 decimals, anything that is not a digit or one
  dot, and overflow.
- Keys are found by an exact match on the quoted key and its colon (`"t":`)
  within the object's bounds. `"t"` and `"T"` are different keys. `"s"`
  appears both in `data` and inside `"k"`, so kline fields are read only
  within `"k":{...}`.
- The symbol (`"s"` in `data`) is looked up in the instrument table. An
  unknown symbol is dropped and counted.
- Trade: `t` → `tradeId`, `p` → `price`, `q` → `qty`, `T` × 1000 →
  `tsExchangeUs`, `m` = true → `FEED_SIDE_SELL`, false → `FEED_SIDE_BUY`.
- Kline: every update is published, not only closed bars. `k.t` × 1000 →
  `openTimeUs`, `o`/`h`/`l`/`c` → prices, `v` → `volume`, `q` →
  `quoteVolume`, `n` → `tradeCount`, `E` × 1000 → `tsExchangeUs`,
  `intervalSec` = 60, and `x` = true sets `FEED_KLINE_CLOSED`.
- `tsRecvUs` comes from `GetSystemTimePreciseAsFileTime` when the message is
  complete.
- A message that fails any of these checks is dropped and counted. The
  connection stays up.
- TickC does not deduplicate. Binance does not replay events after a
  reconnect, and deduplication across sources is the aggregator's job later.

## Where the feed runs

- Only in the main instance, never in a `--dup` process.
- `FeedStart` is called in `WinMain` after the main-instance mutex is taken.
  If it cannot create or map the region, it returns FALSE and TickC runs
  without a feed (in GUI mode).
- `FeedStop` runs at exit, **before the main-instance mutex is released**
  (`WinMain` releases it when the message loop ends, ahead of the network
  thread's join). Otherwise a TickC started in that window could take the
  mutex and the mapping while the old `FeedThread` still publishes: two
  writers. `FeedStop` sets the stop event, then closes whichever of the
  WebSocket, the request and the connect handle is open, under the feed's
  lock, in that order (child before parent), to cancel a pending call; waits
  for `FeedThread`; stops the heartbeat timer; publishes `DISCONNECTED`; and
  unmaps. Task 1 measured that closing a handle from another thread cancels
  a pending call within milliseconds: about 1 s for a receive, and about
  47 ms during a connect. It never waits longer than the network thread's
  existing bound (10 s).
- `FeedThread` reads `hSession` under the same lock as `HttpGet`, because
  `WinMain` closes the session under that lock at exit.

## `--daemon`

| Situation | Behaviour |
|---|---|
| `TickC.exe --daemon`, no TickC running | A main instance without a tray icon, panel or desktop surface. It creates the hidden main window with the main title (for the hand-over below) and starts `FeedThread`. `NetworkThread` is not started: nothing shows its price. |
| `--daemon` while a normal TickC runs | Exits silently with code 0: the feed already runs. It takes the `--autostart` path in the mutex check, so it does not hand over and the running TickC's panel does not come forward. |
| A normal start while the daemon runs | The existing hand-over (`HandOverToMainInstance`) posts `WM_APP_SHOW`. The daemon **becomes a normal TickC**: it adds the tray icon, starts `NetworkThread`, and then does what a plain start does with the saved configuration: the desktop surface if desktop mode is saved, the panel otherwise. The daemon reads the configuration (`LoadConfig`) at start for this. This is also how a daemon is stopped: show it, then Exit from the tray menu. |
| `--daemon` combined with `--dup` or `--desktop-mode` | `--daemon` wins; the others are ignored. |
| Explorer restarts (`TaskbarCreated`) | A daemon adds no tray icon. Once it has become a normal TickC, the icon is re-added as today. |
| Sign-in | Autostart is not changed by this phase. |
| The mapping fails in daemon mode | Exit with code 2. A daemon has no other job. |

## Error handling

| Failure | Behaviour |
|---|---|
| Unparseable message, unknown symbol, oversized message | Dropped and counted. The connection stays up. |
| Connection or upgrade fails | `DISCONNECTED` with the error and `retryAtUs`, then backoff. |
| 10 s of silence | Reconnect. |
| Mapping already exists | Take over the session (Writer restarts). |
| Mapping cannot be created | GUI: run without a feed. Daemon: exit code 2. |
| A reader process dies | Its slot is freed within about a second while messages flow or during a backoff wait, and at the next message after a connect attempt or a silence. |
| The writer process dies or hangs | Readers see it through the process wait, or a heartbeat older than 3 s. |

## Testing

Red before green, as the project's convention requires: every test is seen to
fail before the code that makes it pass.

### 1. `tests/feed_test.c`

Built like `tests/chart_golden.c`, linked with `feed.c`, with no network and
no windows:

- `ParseFixed8` table tests: the smallest step (`"0.00000001"` → 1), whole
  numbers, 8 decimals, the largest value, and every rejection (empty, sign,
  exponent, 9 decimals, two dots, overflow).
- Message parsing on recorded Binance messages: a trade with `m` true and
  false, an open kline, a closed kline, an unknown symbol, a missing field.
- The ring in one process: N events in give N identical events out.
  Publishing `FEED_SLOT_COUNT + k` events without reading gives `FEED_LAPPED`
  on the next read, with the lost count right. After the resync the snapshot
  holds the last trade and kline of each instrument.
- A new session gives `FEED_NEW_SESSION` once. A ninth registering reader
  gets `FEED_NO_SLOT`.
- The seqlock: a reader never returns a snapshot with a torn trade while a
  writer thread updates it.
- Stress: one writer thread publishes millions of events whose payload is
  derived from their own sequence number, so every byte can be checked. A fast
  and a deliberately slow reader thread run at the same time. No `FEED_OK`
  event may be torn, and sequence numbers must rise strictly. About 5 s.
- Dead reader: a child process claims a slot and exits. The writer frees the
  slot within 2 s.

### 2. Integration, by the probe convention

- In the `TICKER_PROBE` build with `TICKER_FIXTURE_DIR` set, `FeedThread`
  replays `ws_stream.jsonl` from the fixture directory through the same parser
  instead of opening a socket. This mirrors the HTTP fixtures TickC has today.
- `tests/feed_probe.exe --check <name>` reads the test build's mapping, hashes
  the events without `tsRecvUs`, and compares them with
  `tests/golden/feed.txt`.
- New `WM_APP_PROBE` fields report events published, messages dropped,
  `connState`, and whether the tray icon was added. Their numbers are chosen in
  the plan, after a search of the fields in use.
- Daemon tests: in `--daemon` there is no tray icon and the feed can be read;
  a normal start turns the daemon into a normal TickC. All of it runs hidden
  and on an idle machine.

### 3. The consumer

`tests/feed_probe.exe --watch` is the minimal consumer of the decomposition: a
header shows the mean latency (`tsRecvUs − tsExchangeUs`) once, across every
symbol, and a live table shows the last price, trades/s and klines/s per
symbol. The smoke test against the real Binance is run once by hand and is
not part of the automated suite.

## Files

- New: `feed.h`, `feed.c`, `tests/feed_test.c`, `tests/feed_probe.c`, the
  fixture `ws_stream.jsonl`, `tests/golden/feed.txt`.
- Changed: `tickc.c` (arguments, start, the daemon's hand-over, shutdown, the
  probe fields), `README.md` (the feed, `--daemon`, the build line, three
  source files), `WORKLOG.md` (phase 54).
- The build line gains `feed.c`. The exe is expected to grow by 10–15 KB.

## Risks

- **The WebSocket receive timeout.** Resolved by Task 1's spike: no timeout
  bounds an ongoing `WinHttpWebSocketReceive` (a silent line did not return
  in 45 s, with 3 s on the request and 5 s on the session). The watchdog
  described in "Silence and reconnection" is the built mechanism, not a
  fallback, and is proven on the real network (Task 6).
- **The ring's size** is estimated, not measured. The watch mode reports
  events per second, and the live smoke test records the peak rate, to confirm
  the 30 s margin.
- **Binance's JSON** could change a field. The parser drops what it cannot
  read and counts it; the probe fields make a sudden rise visible.
