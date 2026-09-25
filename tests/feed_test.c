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

int wmain(int argc, wchar_t** argv) {
    (void)argc; (void)argv;
    TestLayout();
    TestAtomics();
    TestRoundTrip();
    TestLappedAndResync();
    TestRewind();
    TestRestartContinuity();
    TestRestartOddLock();
    TestReaderBeforeWriter();
    TestReadOnlyClose();
    TestSlotsAndWake();
    TestStatusAndHeartbeat();
    printf("%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
