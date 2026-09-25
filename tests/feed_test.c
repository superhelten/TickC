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

int wmain(int argc, wchar_t** argv) {
    (void)argc; (void)argv;
    TestLayout();
    TestAtomics();
    printf("%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
