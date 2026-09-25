// tests/feed_test.c - the feed's tests (phase 54): the layout, the ring, the
// parser and the feed thread, with no network and no windows. Built as x86,
// like TickC, so the atomic helpers run their SSE2 path.
//   tests\feed_test.exe                       every test; exit code 1 on a failure
//   tests\feed_test.exe --live N              N seconds against the real Binance (by hand)
//   tests\feed_test.exe --serve NAME FILE S   a replaying writer for feed_probe, S seconds
//   tests\feed_test.exe --child-claim NAME    internal: the reader that dies
#include "feed.h"
#include <stdio.h>
#include <stdlib.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, what) do { g_checks++; \
    if (!(cond)) { g_fails++; printf("FAIL %s (line %d)\n", what, __LINE__); } } while (0)

// A mapping name of its own per test, so tests never share a ring.
static const wchar_t* TestName(const wchar_t* tag) {
    static wchar_t names[16][96];
    static int n = 0;
    wchar_t* out = names[n++ & 15];
    swprintf_s(out, 96, L"Local\\TickCFeedTest.%lu.%s", GetCurrentProcessId(), tag);
    return out;
}

static const FeedInstrument TEST_INS[4] = {
    { "BTCUSDT", "BTC/USDT", FEED_SRC_BINANCE_SPOT, FEED_ASSET_CRYPTO, -8, -8, { 0 } },
    { "ETHUSDT", "ETH/USDT", FEED_SRC_BINANCE_SPOT, FEED_ASSET_CRYPTO, -8, -8, { 0 } },
    { "SOLUSDT", "SOL/USDT", FEED_SRC_BINANCE_SPOT, FEED_ASSET_CRYPTO, -8, -8, { 0 } },
    { "BNBUSDT", "BNB/USDT", FEED_SRC_BINANCE_SPOT, FEED_ASSET_CRYPTO, -8, -8, { 0 } },
};

// --- Task 2 --------------------------------------------------------------

static void TestLayout(void) {
    CHECK(sizeof(FeedEvent) == 128, "FeedEvent is 128 bytes");
    CHECK(offsetof(FeedEvent, u) == 32, "the payload at 32");
    CHECK(offsetof(FeedHeader, writeSeq) == 64, "writeSeq on line 1");
    CHECK(offsetof(FeedHeader, connState) == 80, "connState at 80");
    CHECK(offsetof(FeedHeader, snapshots) == 1024, "snapshots at 1024");
    CHECK(FEED_MAPPING_SIZE == 4198400u, "the mapping is 4 198 400 bytes");
    CHECK(FEED_SLOT_INDEX(1) == 1 && FEED_SLOT_INDEX(FEED_SLOT_COUNT + 5) == 5, "slot index wraps");
}

static void TestAtomics(void) {
    static volatile int64_t v;   // static: 8-byte aligned
    int64_t vals[4] = { 0, -1, 0x0123456789ABCDEFLL, 0x00000001FFFFFFFFLL };
    for (int i = 0; i < 4; ++i) {
        FeedStoreRelease64(&v, vals[i]);
        CHECK(FeedLoadAcquire64(&v) == vals[i], "store/load acquire round trip");
        CHECK(FeedLoadNoFence64(&v) == vals[i], "store/load no-fence round trip");
    }
    int64_t now = FeedNowUs();
    CHECK(now > 1780000000000000LL && now < 4102444800000000LL, "FeedNowUs is microseconds since 1970");
}

// --- Task 3 --------------------------------------------------------------

static FeedEvent MakeTrade(unsigned ins, int64_t n) {
    FeedEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = FEED_TRADE; ev.instrument = (uint16_t)ins; ev.source = FEED_SRC_BINANCE_SPOT;
    ev.tsExchangeUs = n; ev.tsRecvUs = n + 1;
    ev.u.trade.price = n; ev.u.trade.qty = 2 * n; ev.u.trade.tradeId = (uint64_t)n;
    ev.u.trade.side = FEED_SIDE_BUY;
    return ev;
}

static FeedEvent MakeKline(unsigned ins, int64_t n) {
    FeedEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = FEED_KLINE; ev.instrument = (uint16_t)ins; ev.source = FEED_SRC_BINANCE_SPOT;
    ev.tsExchangeUs = n;
    ev.u.kline.openTimeUs = n; ev.u.kline.intervalSec = 60;
    ev.u.kline.open = n; ev.u.kline.high = n + 2; ev.u.kline.low = n - 2; ev.u.kline.close = n + 1;
    return ev;
}

static void TestRoundTrip(void) {
    FeedWriter w; FeedReader r; FeedEvent ev; int64_t lost;
    const wchar_t* name = TestName(L"rt");
    CHECK(FeedWriterOpen(&w, name, TEST_INS, 4), "writer opens");
    CHECK(FeedOpen(&r, name, FALSE) == FEED_OK, "reader opens");
    CHECK(FeedNext(&r, &ev, &lost) == FEED_EMPTY, "empty at first");
    for (int64_t i = 1; i <= 1000; ++i) { FeedEvent t = MakeTrade((unsigned)(i & 3), i); FeedPublish(&w, &t); }
    int64_t n = 0, lastSeq = 0; BOOL same = TRUE;
    while (FeedNext(&r, &ev, &lost) == FEED_OK) {
        n++;
        FeedEvent want = MakeTrade((unsigned)(n & 3), n);
        if (ev.seq != lastSeq + 1 || memcmp((const uint8_t*)&ev + 8, (const uint8_t*)&want + 8, 120) != 0) same = FALSE;
        lastSeq = ev.seq;
    }
    CHECK(n == 1000, "1000 events in give 1000 out");
    CHECK(same, "every event identical, seq rising by one from 1");
    CHECK(r.hdr->instrumentCount == 4 && strcmp(r.hdr->instruments[2].symbol, "SOLUSDT") == 0, "the instrument table");
    FeedClose(&r);
    FeedWriterClose(&w);
}

static void TestLappedAndResync(void) {
    FeedWriter w; FeedReader r; FeedEvent ev; FeedSnapshot s; int64_t lost = 0;
    const wchar_t* name = TestName(L"lap");
    FeedWriterOpen(&w, name, TEST_INS, 4);
    FeedOpen(&r, name, FALSE);
    const int64_t k = 10, total = FEED_SLOT_COUNT + k;
    for (int64_t i = 1; i <= total; ++i) {
        FeedEvent t = (i % 7 == 0) ? MakeKline(1, i) : MakeTrade(0, i);
        FeedPublish(&w, &t);
    }
    CHECK(FeedNext(&r, &ev, &lost) == FEED_LAPPED, "a reader SLOT_COUNT+k behind is lapped");
    CHECK(lost == total, "lost counts every skipped event");
    CHECK(r.next == total + 1, "the reader jumps to the present");
    CHECK(FeedReadSnapshot(&r, 0, &s) && s.trade.tradeId == (uint64_t)total, "snapshot: the last trade");
    int64_t lastKline = (total / 7) * 7;
    CHECK(FeedReadSnapshot(&r, 1, &s) && s.kline.openTimeUs == lastKline, "snapshot: the last kline");
    CHECK(FeedReadSnapshot(&r, 2, &s) && s.trade.tradeId == 0 && s.kline.openTimeUs == 0, "snapshot: none yet");
    FeedEvent t = MakeTrade(0, 99);
    FeedPublish(&w, &t);
    CHECK(FeedNext(&r, &ev, &lost) == FEED_OK && ev.u.trade.tradeId == 99, "reading goes on after the jump");
    FeedClose(&r);
    FeedWriterClose(&w);
}

// No event is numbered below 1. A slot not yet written holds seq 0 and a
// slot being written holds FEED_SEQ_BUSY (-1), so a reader positioned at 0
// or -1 must not take either for its event; it starts at event 1 (phase 54).
static void TestPositionBelowOne(void) {
    FeedWriter w; FeedReader r; FeedEvent ev; int64_t lost;
    const wchar_t* name = TestName(L"below1");
    FeedWriterOpen(&w, name, TEST_INS, 4);
    FeedOpen(&r, name, FALSE);
    for (int64_t i = 1; i <= 3; ++i) { FeedEvent t = MakeTrade(0, i); FeedPublish(&w, &t); }
    r.next = 0;   // slot 0 holds seq 0 until event 32768
    lost = -7;
    CHECK(FeedNext(&r, &ev, &lost) == FEED_OK && ev.seq == 1 && ev.u.trade.tradeId == 1 && lost == 0,
          "a position of 0 reads event 1, not the unwritten slot 0, and loses nothing");
    // The last slot as a writer leaves it mid-publish.
    ((FeedEvent*)(w.ring + (size_t)(FEED_SLOT_COUNT - 1) * FEED_SLOT_SIZE))->seq = FEED_SEQ_BUSY;
    r.next = -1;
    lost = -7;
    CHECK(FeedNext(&r, &ev, &lost) == FEED_OK && ev.seq == 1 && ev.u.trade.tradeId == 1 && lost == 0,
          "a position of -1 reads event 1, not the busy slot, and loses nothing");
    FeedClose(&r);
    FeedWriterClose(&w);
}

static void TestRewind(void) {
    FeedWriter w; FeedReader r; FeedEvent ev; int64_t lost;
    const wchar_t* name = TestName(L"rew");
    FeedWriterOpen(&w, name, TEST_INS, 4);
    for (int64_t i = 1; i <= 5; ++i) { FeedEvent t = MakeTrade(0, i); FeedPublish(&w, &t); }
    FeedOpen(&r, name, FALSE);
    CHECK(FeedNext(&r, &ev, &lost) == FEED_EMPTY, "a new reader starts at the present");
    FeedRewind(&r);
    int n = 0;
    while (FeedNext(&r, &ev, &lost) == FEED_OK) n++;
    CHECK(n == 5, "after FeedRewind it reads all five");
    FeedClose(&r);
    FeedWriterClose(&w);
}

// Final review item 4: without FEED_REWIND_MARGIN, FeedRewind lands the
// reader exactly on the slot the writer overwrites next, so a live writer
// publishing even one more event before the first FeedNext laps the reader
// and the whole backlog is lost. This publishes SLOT_COUNT + 100 events,
// rewinds, publishes 10 more (reproducing that race), and checks the margin
// keeps the rewound reader safely behind the writer.
static void TestRewindMargin(void) {
    FeedWriter w; FeedReader r; FeedEvent ev; int64_t lost, expect;
    const wchar_t* name = TestName(L"rewm");
    const int64_t total = (int64_t)FEED_SLOT_COUNT + 100;
    FeedWriterOpen(&w, name, TEST_INS, 4);
    for (int64_t i = 1; i <= total; ++i) { FeedEvent t = MakeTrade(0, i); FeedPublish(&w, &t); }
    FeedOpen(&r, name, FALSE);
    FeedRewind(&r);
    expect = r.next;
    for (int64_t i = total + 1; i <= total + 10; ++i) { FeedEvent t = MakeTrade(0, i); FeedPublish(&w, &t); }
    int rc = FeedNext(&r, &ev, &lost);
    CHECK(rc == FEED_OK && ev.seq == expect, "the margin keeps the rewound position ahead of a live writer");
    int n = (rc == FEED_OK) ? 1 : 0;
    while ((rc = FeedNext(&r, &ev, &lost)) == FEED_OK) n++;
    CHECK(rc == FEED_EMPTY, "no FEED_LAPPED: reading goes on to the present");
    CHECK(n == (int)(total + 10 - expect + 1), "every event from the rewound position to the present is read");
    FeedClose(&r);
    FeedWriterClose(&w);
}

static void TestRestartContinuity(void) {   // Review Focus 5
    FeedWriter w; FeedReader r; FeedEvent ev; int64_t lost;
    const wchar_t* name = TestName(L"rs");
    FeedWriterOpen(&w, name, TEST_INS, 4);
    FeedOpen(&r, name, FALSE);
    for (int64_t i = 1; i <= 3; ++i) { FeedEvent t = MakeTrade(0, i); FeedPublish(&w, &t); }
    while (FeedNext(&r, &ev, &lost) == FEED_OK) {}
    int64_t lastOld = ev.seq;
    // The reader holds the mapping: a new writer finds it and takes over.
    UnmapViewOfFile(w.hdr); CloseHandle(w.hMap);
    FeedWriter w2;
    CHECK(FeedWriterOpen(&w2, name, TEST_INS, 4), "a second writer takes the mapping over");
    FeedEvent t = MakeTrade(0, 42);
    FeedPublish(&w2, &t);
    CHECK(FeedNext(&r, &ev, &lost) == FEED_NEW_SESSION, "the reader sees the new session");
    // Event 42 came before the reader noticed the session: it is below the
    // new position, so it is in the snapshot, not read from the ring.
    CHECK(FeedNext(&r, &ev, &lost) == FEED_EMPTY, "then it is at the present, not a second NEW_SESSION");
    FeedEvent t2 = MakeTrade(0, 43);
    FeedPublish(&w2, &t2);
    int rc;
    while ((rc = FeedNext(&r, &ev, &lost)) == FEED_OK && ev.u.trade.tradeId != 43) {}
    CHECK(rc == FEED_OK && ev.seq > lastOld, "seq continues above the old session's last");
    FeedClose(&r);
    FeedWriterClose(&w2);
}

static void TestRestartOddLock(void) {   // fix round 1: a writer died inside the seqlock
    FeedWriter w; FeedReader r; FeedSnapshot s;
    const wchar_t* name = TestName(L"odd");
    FeedWriterOpen(&w, name, TEST_INS, 4);
    FeedOpen(&r, name, FALSE);   // read-only: keeps the mapping alive after w is abandoned
    InterlockedIncrement64((LONG64 volatile*)&w.hdr->snapshots[0].lock);   // odd: died mid-publish
    // The reader holds the mapping: a new writer finds it and takes over.
    UnmapViewOfFile(w.hdr); CloseHandle(w.hMap);
    FeedWriter w2;
    CHECK(FeedWriterOpen(&w2, name, TEST_INS, 4), "a second writer takes the mapping over");
    FeedEvent t = MakeTrade(0, 7);
    FeedPublish(&w2, &t);
    CHECK((w2.hdr->snapshots[0].lock & 1) == 0, "the restart leaves the lock even, not inverted");
    CHECK(FeedReadSnapshot(&r, 0, &s) && s.trade.tradeId == 7, "the snapshot reads back cleanly");
    FeedClose(&r);
    FeedWriterClose(&w2);
}

static void TestReaderBeforeWriter(void) {   // Review Focus 3
    FeedReader r; FeedWriter w;
    const wchar_t* name = TestName(L"rbw");
    CHECK(FeedOpen(&r, name, FALSE) == FEED_NO_WRITER, "no mapping: FEED_NO_WRITER");
    FeedWriterOpen(&w, name, TEST_INS, 4);
    CHECK(FeedOpen(&r, name, FALSE) == FEED_OK, "once the writer exists, the open succeeds");
    FeedClose(&r);
    FeedWriterClose(&w);
    CHECK(FeedOpen(&r, name, FALSE) == FEED_NO_WRITER, "after the writer closed: FEED_NO_WRITER");
}

static void TestReadOnlyClose(void) {   // Review Focus 4
    FeedReader r; FeedWriter w; FeedEvent ev; int64_t lost;
    const wchar_t* name = TestName(L"ro");
    FeedWriterOpen(&w, name, TEST_INS, 4);
    CHECK(FeedOpen(&r, name, FALSE) == FEED_OK && r.slot == -1 && r.hWake == NULL, "read-only: no slot, no event");
    FeedEvent t = MakeTrade(0, 1);
    FeedPublish(&w, &t);
    CHECK(FeedNext(&r, &ev, &lost) == FEED_OK, "read-only reads");
    FeedClose(&r);   // must not write to the read-only view
    CHECK(r.hdr == NULL, "closed");
    FeedWriterClose(&w);
}

static void TestSlotsAndWake(void) {
    FeedWriter w; FeedReader rs[FEED_MAX_READERS + 1];
    const wchar_t* name = TestName(L"slot");
    FeedWriterOpen(&w, name, TEST_INS, 4);
    for (int i = 0; i < FEED_MAX_READERS; ++i)
        CHECK(FeedOpen(&rs[i], name, TRUE) == FEED_OK && rs[i].slot == i, "a reader claims the next slot");
    CHECK(FeedOpen(&rs[FEED_MAX_READERS], name, TRUE) == FEED_NO_SLOT, "the ninth gets FEED_NO_SLOT");
    CHECK(FeedWait(&rs[0], 0) == WAIT_TIMEOUT, "no data, no wake");
    FeedEvent t = MakeTrade(0, 1);
    FeedPublish(&w, &t);
    CHECK(FeedWait(&rs[0], 1000) == WAIT_OBJECT_0 && FeedWait(&rs[7], 1000) == WAIT_OBJECT_0,
          "a publish wakes every registered reader, two in one process included");
    FeedClose(&rs[3]);
    CHECK(w.hdr->readers[3].owner == 0 && w.hdr->readers[3].ready == 0, "a clean close frees the slot");
    CHECK(FeedOpen(&rs[3], name, TRUE) == FEED_OK && rs[3].slot == 3, "and the slot can be claimed again");
    for (int i = 0; i < FEED_MAX_READERS; ++i) FeedClose(&rs[i]);
    FeedWriterClose(&w);
}

static void TestStatusAndHeartbeat(void) {
    FeedWriter w; FeedReader r; FeedEvent ev; int64_t lost;
    const wchar_t* name = TestName(L"st");
    FeedWriterOpen(&w, name, TEST_INS, 4);
    FeedOpen(&r, name, FALSE);
    FeedSetConn(&w, FEED_ST_DISCONNECTED, 12002, 1234);
    CHECK(r.hdr->connState == FEED_ST_DISCONNECTED, "connState is stored");
    CHECK(FeedNext(&r, &ev, &lost) == FEED_OK && ev.type == FEED_STATUS && ev.instrument == FEED_NO_INSTRUMENT &&
          ev.u.status.state == FEED_ST_DISCONNECTED && ev.u.status.error == 12002 && ev.u.status.retryAtUs == 1234,
          "a status event carries state, error and retry time");
    FeedStoreRelease64(&w.hdr->heartbeatUs, FeedNowUs() - 10000000);
    CHECK(!FeedWriterAlive(&r), "a heartbeat 10 s old: the writer hangs");
    FeedHeartbeat(&w);
    CHECK(FeedWriterAlive(&r), "a fresh heartbeat: alive");
    FeedClose(&r);
    FeedWriterClose(&w);
}

// --- Task 4 --------------------------------------------------------------

// Stress: one writer, a fast and a slow reader, and a snapshot checker, all at
// once. Every payload byte is derived from the event's own number, so a torn
// event cannot pass: FEED_OK must never return one.
#define STRESS_EVENTS 3000000
typedef struct { FeedReader r; int slow; int64_t ok, lapped, torn, disorder; } StressReader;
static volatile LONG g_stressDone;

static void StressFill(FeedEvent* ev, int64_t n) {
    memset(ev, 0, sizeof(*ev));
    ev->type = FEED_TRADE; ev->instrument = 0;
    ev->tsExchangeUs = n;
    ev->u.trade.price = n; ev->u.trade.qty = 2 * n; ev->u.trade.tradeId = (uint64_t)n;
    for (int i = 32; i < 96; ++i) ev->u.raw[i] = (uint8_t)(n * 31 + i);
}

static BOOL StressValid(const FeedEvent* ev) {
    int64_t n = ev->tsExchangeUs;
    if (ev->seq != n || ev->u.trade.price != n || ev->u.trade.qty != 2 * n || ev->u.trade.tradeId != (uint64_t)n) return FALSE;
    for (int i = 32; i < 96; ++i) if (ev->u.raw[i] != (uint8_t)(n * 31 + i)) return FALSE;
    return TRUE;
}

static DWORD WINAPI StressReaderThread(LPVOID p) {
    StressReader* s = (StressReader*)p;
    FeedEvent ev; int64_t lost, last = 0; unsigned k = 0;
    FeedRewind(&s->r);
    for (;;) {
        int rc = FeedNext(&s->r, &ev, &lost);
        if (rc == FEED_OK) {
            s->ok++;
            if (!StressValid(&ev)) s->torn++;
            if (ev.seq <= last) s->disorder++;
            last = ev.seq;
            if (s->slow && (++k % 2000) == 0) Sleep(1);
        } else if (rc == FEED_LAPPED) {
            s->lapped++;
        } else if (rc == FEED_EMPTY) {
            if (g_stressDone) break;
            YieldProcessor();
        }
    }
    return 0;
}

// Parks right at the lap boundary before every read, rotating over the last
// four slots, so a read races the writer's overwrite instead of trailing it
// (a reader that reads in order never lands on a slot mid-overwrite: the
// arithmetic lap check above catches it first). In the first lap that
// position is below 1, and FeedNext starts such a reader at event 1.
static DWORD WINAPI EdgeReaderThread(LPVOID p) {
    StressReader* s = (StressReader*)p;
    FeedEvent ev; int64_t lost; unsigned k = 0;
    while (!g_stressDone) {
        s->r.next = FeedLoadAcquire64(&s->r.hdr->writeSeq) - (int64_t)FEED_SLOT_COUNT + (int64_t)(k++ & 3);
        if (FeedNext(&s->r, &ev, &lost) == FEED_OK) {
            s->ok++;
            if (!StressValid(&ev)) s->torn++;
        }
    }
    return 0;
}

typedef struct { FeedReader* r; int64_t reads, torn; } SnapChecker;
static DWORD WINAPI SnapCheckerThread(LPVOID p) {
    SnapChecker* c = (SnapChecker*)p;
    FeedSnapshot s;
    while (!g_stressDone) {
        if (!FeedReadSnapshot(c->r, 0, &s)) continue;
        c->reads++;
        if (s.trade.qty != 2 * s.trade.price || s.trade.tradeId != (uint64_t)s.trade.price) c->torn++;
    }
    return 0;
}

static void TestStress(void) {
    FeedWriter w; StressReader fast, slow, edge; SnapChecker snap; FeedReader snapR;
    HANDLE th[4];
    const wchar_t* name = TestName(L"stress");
    FeedWriterOpen(&w, name, TEST_INS, 4);
    memset(&fast, 0, sizeof(fast)); memset(&slow, 0, sizeof(slow)); memset(&edge, 0, sizeof(edge));
    memset(&snap, 0, sizeof(snap));
    FeedOpen(&fast.r, name, FALSE); FeedOpen(&slow.r, name, FALSE); FeedOpen(&edge.r, name, FALSE);
    FeedOpen(&snapR, name, FALSE);
    slow.slow = 1; snap.r = &snapR;
    g_stressDone = 0;
    th[0] = CreateThread(NULL, 0, StressReaderThread, &fast, 0, NULL);
    th[1] = CreateThread(NULL, 0, StressReaderThread, &slow, 0, NULL);
    th[2] = CreateThread(NULL, 0, EdgeReaderThread, &edge, 0, NULL);
    th[3] = CreateThread(NULL, 0, SnapCheckerThread, &snap, 0, NULL);
    for (int64_t n = 1; n <= STRESS_EVENTS; ++n) { FeedEvent ev; StressFill(&ev, n); FeedPublish(&w, &ev); }
    InterlockedExchange(&g_stressDone, 1);
    WaitForMultipleObjects(4, th, TRUE, INFINITE);
    for (int i = 0; i < 4; ++i) CloseHandle(th[i]);
    printf("stress: fast ok %lld lapped %lld; slow ok %lld lapped %lld; edge ok %lld torn %lld; snapshots %lld\n",
           fast.ok, fast.lapped, slow.ok, slow.lapped, edge.ok, edge.torn, snap.reads);
    CHECK(fast.torn == 0 && slow.torn == 0, "no torn event is ever returned as FEED_OK");
    CHECK(fast.disorder == 0 && slow.disorder == 0, "seq rises strictly for each reader");
    CHECK(slow.lapped > 0, "the slow reader was lapped (the test exercised the lap path)");
    CHECK(fast.ok > 0 && slow.ok > 0, "both readers read");
    CHECK(snap.reads > 0 && snap.torn == 0, "no torn snapshot");
    CHECK(edge.torn == 0, "no torn event at the lap edge either");
    CHECK(edge.ok > 0, "the edge reader got FEED_OK reads (the check is not vacuous)");
    FeedClose(&fast.r); FeedClose(&slow.r); FeedClose(&edge.r); FeedClose(&snapR);
    FeedWriterClose(&w);
}

// The child claims a slot and exits without closing it.
static int ChildClaim(const wchar_t* name) {
    FeedReader r;
    if (FeedOpen(&r, name, TRUE) != FEED_OK) return 3;
    ExitProcess(0);
}

static void TestDeadReader(void) {
    FeedWriter w; FeedEvent t; FeedReader live;
    wchar_t exe[MAX_PATH], cmd[512];
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    const wchar_t* name = TestName(L"dead");
    FeedWriterOpen(&w, name, TEST_INS, 4);
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    swprintf_s(cmd, 512, L"\"%s\" --child-claim %s", exe, name);
    memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
    CHECK(CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi), "the child starts");
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    CHECK(w.hdr->readers[0].owner == (LONG)pi.dwProcessId, "the dead child still owns slot 0");
    CHECK(FeedOpen(&live, name, TRUE) == FEED_OK && live.slot == 1, "a live reader claims slot 1");
    t = MakeTrade(0, 1);
    FeedPublish(&w, &t);   // opens and caches the dead reader's event
    ULONGLONG t0 = GetTickCount64();
    while (w.hdr->readers[0].owner != 0 && GetTickCount64() - t0 < 2000) { FeedSweepReaders(&w); Sleep(50); }
    CHECK(w.hdr->readers[0].owner == 0 && w.hdr->readers[0].ready == 0, "the sweep frees it within 2 s");
    CHECK(w.hWake[0] == NULL, "and closes the cached event");
    CHECK(w.hdr->readers[live.slot].owner == (LONG)GetCurrentProcessId() && w.hdr->readers[live.slot].ready == 1,
          "a live reader keeps its slot");
    FeedClose(&live);
    FeedWriterClose(&w);
}

// --- Task 5 --------------------------------------------------------------

static void TestFixed8(void) {   // Review Focus 2: short decimals
    static const struct { const char* s; int ok; int64_t v; } rows[] = {
        { "0.00000001", 1, 1 },
        { "1", 1, 100000000LL },
        { "64123", 1, 6412300000000LL },
        { "64123.4", 1, 6412340000000LL },
        { "0.1", 1, 10000000LL },
        { "64123.45000000", 1, 6412345000000LL },
        { "92233720368.54775807", 1, INT64_MAX },
        { "92233720368.54775808", 0, 0 },
        { "92233720369", 0, 0 },
        { "", 0, 0 },
        { "-1.0", 0, 0 },
        { "+1.0", 0, 0 },
        { "1e5", 0, 0 },
        { "1.000000001", 0, 0 },
        { "1.2.3", 0, 0 },
        { ".5", 0, 0 },
        { "5.", 0, 0 },
        { "12a", 0, 0 },
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        int64_t v = -7;
        int ok = FeedParseFixed8(rows[i].s, rows[i].s + strlen(rows[i].s), &v);
        char what[96];
        sprintf_s(what, sizeof(what), "Fixed8 \"%s\"", rows[i].s);
        CHECK(ok == rows[i].ok && (!ok || v == rows[i].v), what);
    }
}

static int Parse(const char* m, FeedEvent* ev) {
    return FeedParseMessage(m, strlen(m), TEST_INS, 4, 777, ev);
}

static void TestParseTrade(void) {
    FeedEvent ev;
    const char* sell = "{\"stream\":\"btcusdt@trade\",\"data\":{\"e\":\"trade\",\"E\":1727222400123,\"s\":\"BTCUSDT\","
                       "\"t\":3871234567,\"p\":\"64123.45000000\",\"q\":\"0.00150000\",\"T\":1727222400121,\"m\":true,\"M\":true}}";
    const char* buy  = "{\"stream\":\"ethusdt@trade\",\"data\":{\"e\":\"trade\",\"E\":1,\"s\":\"ETHUSDT\","
                       "\"t\":5,\"p\":\"2500.1\",\"q\":\"3\",\"T\":1727222400999,\"m\":false,\"M\":true}}";
    CHECK(Parse(sell, &ev) == FEED_PARSE_OK, "a trade parses");
    CHECK(ev.type == FEED_TRADE && ev.instrument == 0 && ev.source == FEED_SRC_BINANCE_SPOT, "trade: type, instrument, source");
    CHECK(ev.u.trade.tradeId == 3871234567ULL && ev.u.trade.price == 6412345000000LL && ev.u.trade.qty == 150000LL, "trade: id, price, qty");
    CHECK(ev.tsExchangeUs == 1727222400121000LL && ev.tsRecvUs == 777, "trade: T in microseconds, the receive time");
    CHECK(ev.u.trade.side == FEED_SIDE_SELL, "m=true: the seller was the aggressor");
    CHECK(Parse(buy, &ev) == FEED_PARSE_OK && ev.instrument == 1 && ev.u.trade.side == FEED_SIDE_BUY &&
          ev.u.trade.price == 250010000000LL && ev.u.trade.qty == 300000000LL, "m=false: BUY; short decimals");
}

static void TestParseKline(void) {
    FeedEvent ev;
    const char* open = "{\"stream\":\"solusdt@kline_1m\",\"data\":{\"e\":\"kline\",\"E\":1727222400500,\"s\":\"SOLUSDT\",\"k\":{"
                       "\"t\":1727222400000,\"T\":1727222459999,\"s\":\"SOLUSDT\",\"i\":\"1m\",\"f\":1,\"L\":2,"
                       "\"o\":\"150.10000000\",\"c\":\"150.20000000\",\"h\":\"150.30000000\",\"l\":\"150.00000000\","
                       "\"v\":\"12.34500000\",\"n\":321,\"x\":false,\"q\":\"1851.00000000\",\"V\":\"6.1\",\"Q\":\"900.1\",\"B\":\"0\"}}}";
    CHECK(Parse(open, &ev) == FEED_PARSE_OK, "a kline parses");
    CHECK(ev.type == FEED_KLINE && ev.instrument == 2 && ev.flags == 0, "kline: type, instrument, open bar");
    CHECK(ev.tsExchangeUs == 1727222400500000LL && ev.u.kline.openTimeUs == 1727222400000000LL, "kline: E and k.t in microseconds");
    CHECK(ev.u.kline.intervalSec == 60 && ev.u.kline.tradeCount == 321, "kline: interval and trade count");
    CHECK(ev.u.kline.open == 15010000000LL && ev.u.kline.close == 15020000000LL &&
          ev.u.kline.high == 15030000000LL && ev.u.kline.low == 15000000000LL, "kline: OHLC");
    CHECK(ev.u.kline.volume == 1234500000LL && ev.u.kline.quoteVolume == 185100000000LL, "kline: volume and quote volume");
    const char* closed = "{\"stream\":\"solusdt@kline_1m\",\"data\":{\"e\":\"kline\",\"E\":1,\"s\":\"SOLUSDT\",\"k\":{"
                         "\"t\":60000,\"i\":\"1m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\",\"v\":\"0\",\"n\":0,\"x\":true,\"q\":\"0\"}}}";
    CHECK(Parse(closed, &ev) == FEED_PARSE_OK && (ev.flags & FEED_KLINE_CLOSED), "x=true: FEED_KLINE_CLOSED");
    const char* fiveMin = "{\"stream\":\"solusdt@kline_5m\",\"data\":{\"e\":\"kline\",\"E\":1,\"s\":\"SOLUSDT\",\"k\":{"
                          "\"t\":60000,\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\",\"v\":\"0\",\"n\":0,\"x\":true,\"q\":\"0\"}}}";
    CHECK(Parse(fiveMin, &ev) == FEED_PARSE_BAD, "another interval than 1m is rejected");
}

static void TestParseReordered(void) {   // Review Focus 1
    FeedEvent ev;
    const char* m = "{\"data\":{\"k\":{\"x\":true,\"q\":\"2\",\"n\":7,\"v\":\"1\",\"l\":\"1\",\"h\":\"3\",\"c\":\"2\",\"o\":\"1\","
                    "\"i\":\"1m\",\"X\":\"new\",\"t\":120000,\"s\":\"BNBUSDT\"},\"s\":\"BNBUSDT\",\"E\":5,\"e\":\"kline\",\"Z\":0},"
                    "\"stream\":\"bnbusdt@kline_1m\"}";
    CHECK(Parse(m, &ev) == FEED_PARSE_OK && ev.instrument == 3 && ev.u.kline.openTimeUs == 120000000LL &&
          ev.u.kline.high == 300000000LL && ev.u.kline.tradeCount == 7 && ev.tsExchangeUs == 5000 &&
          (ev.flags & FEED_KLINE_CLOSED), "keys in another order, with unknown fields, parse the same");
}

static void TestParseRejects(void) {
    FeedEvent ev;
    CHECK(Parse("{\"stream\":\"xrpusdt@trade\",\"data\":{\"e\":\"trade\",\"E\":1,\"s\":\"XRPUSDT\",\"t\":1,\"p\":\"1\",\"q\":\"1\",\"T\":1,\"m\":true}}", &ev)
          == FEED_PARSE_UNKNOWN, "an unknown symbol");
    CHECK(Parse("{\"stream\":\"btcusdt@trade\",\"data\":{\"e\":\"trade\",\"E\":1,\"s\":\"BTCUSDT\",\"t\":1,\"p\":\"1e5\",\"q\":\"1\",\"T\":1,\"m\":true}}", &ev)
          == FEED_PARSE_BAD, "an exponent price");
    CHECK(Parse("{\"stream\":\"btcusdt@trade\",\"data\":{\"e\":\"trade\",\"E\":1,\"s\":\"BTCUSDT\",\"t\":1,\"q\":\"1\",\"T\":1,\"m\":true}}", &ev)
          == FEED_PARSE_BAD, "a missing price");
    CHECK(Parse("{\"stream\":\"btcusdt@trade\",\"data\":{\"e\":\"trade\",\"E\":1,\"s\":\"BTCUSDT\",\"t\":3,\"p\":\"64123.4", &ev)
          == FEED_PARSE_BAD, "a truncated message");
    CHECK(Parse("{\"result\":null,\"id\":1}", &ev) == FEED_PARSE_BAD, "a non-event message");
    CHECK(Parse("", &ev) == FEED_PARSE_BAD, "an empty message");
}

// Every line of the recorded stream parses, except the three bad ones at the end.
static void FixturePath(wchar_t* out) {
    wchar_t* slash;
    GetModuleFileNameW(NULL, out, MAX_PATH);
    slash = wcsrchr(out, L'\\');
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - out), L"fixtures\\ws_stream.jsonl");
}

static void TestParseFixture(void) {
    wchar_t path[MAX_PATH];
    FILE* f;
    static char line[70000];
    int good = 0, bad = 0, unknown = 0, lines = 0, closed = 0;
    FixturePath(path);
    CHECK(_wfopen_s(&f, path, L"rb") == 0, "the fixture opens");
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        FeedEvent ev;
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (!n) continue;
        lines++;
        int rc = FeedParseMessage(line, n, TEST_INS, 4, 1, &ev);
        if (rc == FEED_PARSE_OK) { good++; if (ev.type == FEED_KLINE && (ev.flags & FEED_KLINE_CLOSED)) closed++; }
        else if (rc == FEED_PARSE_UNKNOWN) unknown++;
        else bad++;
    }
    fclose(f);
    printf("fixture: %d lines, %d good, %d unknown, %d bad, %d closed bars\n", lines, good, unknown, bad, closed);
    CHECK(good == lines - 3 && unknown == 1 && bad == 2, "every recorded line parses; the three bad ones do not");
    CHECK(closed >= 4, "the recording holds a closed bar per symbol");
}

// --- Task 6 --------------------------------------------------------------

static DWORD TestBackoff(int failures, ULONGLONG seed) { (void)failures; (void)seed; return 200; }

static int FixtureGood(void) {   // lines that parse = all but the last three
    wchar_t path[MAX_PATH]; FILE* f; static char line[70000]; int n = 0;
    FixturePath(path);
    if (_wfopen_s(&f, path, L"rb") != 0) return -1;
    while (fgets(line, sizeof(line), f)) if (line[0] == '{') n++;
    fclose(f);
    return n - 3;
}

static void TestReplay(void) {
    FeedConfig cfg; FeedStats st; FeedReader r; FeedEvent ev; int64_t lost;
    wchar_t path[MAX_PATH];
    const wchar_t* name = TestName(L"replay");
    int good = FixtureGood(), trades = 0, klines = 0, status = 0, rc;
    FixturePath(path);
    memset(&cfg, 0, sizeof(cfg));
    cfg.mappingName = name; cfg.replayFile = path; cfg.backoffMs = TestBackoff;
    cfg.instruments = TEST_INS; cfg.instrumentCount = 4;
    CHECK(FeedStart(&cfg), "FeedStart with a replay file");
    ULONGLONG t0 = GetTickCount64();
    do { Sleep(20); FeedGetStats(&st); } while (st.published + st.dropped < good + 3 && GetTickCount64() - t0 < 5000);
    CHECK(st.published == good && st.dropped == 3, "every good line published, the three bad ones dropped");
    CHECK(st.connects == 1 && st.state == FEED_ST_CONNECTED, "a replay is one connection, CONNECTED");
    CHECK(FeedOpen(&r, name, FALSE) == FEED_OK, "a reader opens the feed");
    FeedRewind(&r);
    while ((rc = FeedNext(&r, &ev, &lost)) == FEED_OK) {
        if (ev.type == FEED_TRADE) trades++; else if (ev.type == FEED_KLINE) klines++; else status++;
    }
    CHECK(rc == FEED_EMPTY && trades + klines == good && status == 2, "the ring: every event, CONNECTING and CONNECTED");
    int64_t hb = FeedLoadAcquire64(&r.hdr->heartbeatUs);
    Sleep(1600);
    CHECK(FeedLoadAcquire64(&r.hdr->heartbeatUs) > hb, "the heartbeat timer ticks");
    FeedStop();
    CHECK(r.hdr->connState == FEED_ST_DISCONNECTED,
          "FeedStop published DISCONNECTED; the reader's own view survives the writer's unmap");
    CHECK(FeedNext(&r, &ev, &lost) == FEED_NO_WRITER, "after FeedStop: FEED_NO_WRITER");
    FeedClose(&r);
    FeedGetStats(&st);
    CHECK(st.state == 0, "stopped: no state");
}

static void TestReplayMissing(void) {
    FeedConfig cfg; FeedStats st; FeedReader r; FeedEvent ev; int64_t lost; int rc;
    void* fake = (void*)1;   // never dereferenced by the replay path; a network attempt would crash on it
    const wchar_t* name = TestName(L"missing");
    memset(&cfg, 0, sizeof(cfg));
    cfg.mappingName = name; cfg.replayFile = L"C:\\no\\such\\ws_stream.jsonl"; cfg.backoffMs = TestBackoff;
    cfg.pSession = &fake;
    cfg.instruments = TEST_INS; cfg.instrumentCount = 4;
    CHECK(FeedStart(&cfg), "FeedStart with a missing replay file still starts");
    Sleep(1500);
    FeedGetStats(&st);
    CHECK(st.state == FEED_ST_DISCONNECTED && st.connects == 0 && st.published == 0, "a missing file: DISCONNECTED, never the network");
    CHECK(FeedOpen(&r, name, FALSE) == FEED_OK, "a reader opens the feed");
    FeedRewind(&r);
    int n = 0, error = -1; BOOL order = TRUE;
    while ((rc = FeedNext(&r, &ev, &lost)) == FEED_OK) {
        n++;
        if (ev.type != FEED_STATUS) order = FALSE;
        else if (n == 1 && ev.u.status.state != FEED_ST_CONNECTING) order = FALSE;
        else if (n == 2 && ev.u.status.state != FEED_ST_DISCONNECTED) order = FALSE;
        if (n == 2) error = ev.u.status.error;
    }
    CHECK(rc == FEED_EMPTY && n == 2 && order && error == ERROR_PATH_NOT_FOUND,
          "exactly two status events, CONNECTING then DISCONNECTED with the error: a fake session was never touched");
    FeedClose(&r);
    FeedStop();
}

static void TestStopTwice(void) {
    FeedStop();   // not started: must return at once
    CHECK(TRUE, "FeedStop without FeedStart returns");
}

// --live N: the real Binance, by hand. Prints what arrived per symbol.
static int Live(int seconds) {
    FeedConfig cfg; FeedStats st; FeedReader r; FeedEvent ev; int64_t lost;
    HINTERNET hs = WinHttpOpen(L"TickC/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    long long trades[4] = { 0 }, klines[4] = { 0 }, latSum = 0, latN = 0;
    const wchar_t* name = TestName(L"live");
    memset(&cfg, 0, sizeof(cfg));
    cfg.mappingName = name; cfg.pSession = &hs; cfg.backoffMs = TestBackoff;
    cfg.instruments = TEST_INS; cfg.instrumentCount = 4;
    if (!hs || !FeedStart(&cfg)) { printf("LIVE: no start\n"); return 1; }
    FeedOpen(&r, name, TRUE);
    FeedRewind(&r);
    ULONGLONG t0 = GetTickCount64();
    while (GetTickCount64() - t0 < (ULONGLONG)seconds * 1000) {
        FeedWait(&r, 500);
        while (FeedNext(&r, &ev, &lost) == FEED_OK) {
            if (ev.type == FEED_TRADE) { trades[ev.instrument]++; latSum += ev.tsRecvUs - ev.tsExchangeUs; latN++; }
            else if (ev.type == FEED_KLINE) klines[ev.instrument]++;
            else printf("status %u error %d\n", ev.u.status.state, ev.u.status.error);
        }
    }
    FeedGetStats(&st);
    for (int i = 0; i < 4; ++i) printf("%s trades %lld klines %lld\n", TEST_INS[i].symbol, trades[i], klines[i]);
    printf("LIVE: published %ld dropped %ld connects %ld state %ld, mean latency %.1f ms\n",
           st.published, st.dropped, st.connects, st.state, latN ? latSum / 1000.0 / latN : 0.0);
    FeedClose(&r);
    FeedStop();
    WinHttpCloseHandle(hs);
    return (st.state == FEED_ST_CONNECTED && trades[0] > 0 && klines[0] > 0) ? 0 : 1;
}

// --serve NAME FILE SECONDS: a replaying writer for feed_probe's tests.
static int Serve(const wchar_t* name, const wchar_t* file, int seconds) {
    FeedConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.mappingName = name; cfg.replayFile = file; cfg.backoffMs = TestBackoff;
    cfg.instruments = TEST_INS; cfg.instrumentCount = 4;
    if (!FeedStart(&cfg)) return 1;
    Sleep((DWORD)seconds * 1000);
    FeedStop();
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    if (argc >= 3 && wcscmp(argv[1], L"--child-claim") == 0) return ChildClaim(argv[2]);
    if (argc >= 3 && wcscmp(argv[1], L"--live") == 0) return Live(_wtoi(argv[2]));
    if (argc >= 5 && wcscmp(argv[1], L"--serve") == 0) return Serve(argv[2], argv[3], _wtoi(argv[4]));
    TestLayout();
    TestAtomics();
    TestRoundTrip();
    TestLappedAndResync();
    TestPositionBelowOne();
    TestRewind();
    TestRewindMargin();
    TestRestartContinuity();
    TestRestartOddLock();
    TestReaderBeforeWriter();
    TestReadOnlyClose();
    TestSlotsAndWake();
    TestStatusAndHeartbeat();
    TestStress();
    TestDeadReader();
    TestFixed8();
    TestParseTrade();
    TestParseKline();
    TestParseReordered();
    TestParseRejects();
    TestParseFixture();
    TestStopTwice();
    TestReplay();
    TestReplayMissing();
    printf("%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
