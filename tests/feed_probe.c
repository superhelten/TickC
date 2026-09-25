// tests/feed_probe.c - the feed's minimal reader (phase 54), and the proof
// that the data arrives. It needs only feed.h.
//   feed_probe --watch [--seconds S] [--name N | --exe PATH]
//       a live table per symbol: last price, trades and klines per second,
//       the mean latency (tsRecvUs - tsExchangeUs); Ctrl+C ends it, or
//       --seconds: after S seconds, exit 0 if any trade arrived, else 1
//   feed_probe --check GOLDEN [--name N | --exe PATH] [--update]
//       every event still in the ring, hashed without seq and tsRecvUs, and
//       compared with (or, --update, written to) the golden file
// --exe PATH: the feed of TickC's TEST build at that path, whose mapping name
// carries a hash of the path, as the test build's MainInstanceNames makes it.
// Exit codes: 0 pass, 1 fail, 2 no feed within 10 s.
#include "feed.h"
#include <stdio.h>
#include <stdlib.h>

static void TestBuildName(const wchar_t* exe, wchar_t* out, size_t cch) {
    DWORD h = 2166136261u;   // FNV-1a, as tickc.c
    for (size_t i = 0; exe[i]; ++i) {
        wchar_t c = exe[i];
        h ^= (DWORD)((c >= L'A' && c <= L'Z') ? c + 32 : c);
        h *= 16777619u;
    }
    swprintf_s(out, cch, L"Local\\TickerTest.Feed.1.%08X", h);
}

static int OpenRetry(FeedReader* r, const wchar_t* name, BOOL wakeups) {
    for (int t = 0; t < 100; ++t) {
        int rc = FeedOpen(r, name, wakeups);
        if (rc == FEED_OK) return FEED_OK;
        if (rc == FEED_BAD_VERSION) return rc;
        Sleep(100);
    }
    return FEED_NO_WRITER;
}

static uint64_t Fnv(uint64_t h, const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

static int Check(const wchar_t* name, const wchar_t* golden, BOOL update) {
    FeedReader r; FeedEvent ev; int64_t lost, last = -1;
    long long events = 0, trades = 0, klines = 0, status = 0, lapped = 0;
    uint64_t h = 14695981039346656037ULL;
    char line[160], want[160] = "";
    FILE* f;
    int rc = OpenRetry(&r, name, FALSE);
    if (rc != FEED_OK) { printf("NO FEED (%d)\n", rc); return 2; }
    // Wait until the writer is CONNECTED and the ring has stopped growing
    // for a second: a replay is then complete.
    for (int t = 0; t < 100; ++t) {
        int64_t w = FeedLoadAcquire64(&r.hdr->writeSeq);
        if (r.hdr->connState == FEED_ST_CONNECTED && w == last) break;
        last = w;
        Sleep(1000);
    }
    FeedRewind(&r);
    while ((rc = FeedNext(&r, &ev, &lost)) != FEED_EMPTY) {
        if (rc == FEED_LAPPED) { lapped++; continue; }
        if (rc != FEED_OK) break;
        events++;
        if (ev.type == FEED_TRADE) trades++; else if (ev.type == FEED_KLINE) klines++; else status++;
        h = Fnv(h, (const uint8_t*)&ev + 8, 8);      // tsExchangeUs
        h = Fnv(h, (const uint8_t*)&ev + 24, 104);   // type .. payload
    }
    FeedClose(&r);
    sprintf_s(line, sizeof(line), "events %lld trades %lld klines %lld status %lld hash 0x%016llX",
              events, trades, klines, status, (unsigned long long)h);
    printf("%s\n", line);
    if (lapped) { printf("FAIL lapped %lld times\n", lapped); return 1; }
    if (update) {
        if (_wfopen_s(&f, golden, L"wb") != 0) { printf("FAIL cannot write the golden\n"); return 1; }
        fprintf(f, "%s\n", line);
        fclose(f);
        printf("GOLDEN WRITTEN\n");
        return 0;
    }
    if (_wfopen_s(&f, golden, L"rb") != 0) { printf("FAIL no golden\n"); return 1; }
    if (!fgets(want, sizeof(want), f)) want[0] = 0;
    fclose(f);
    want[strcspn(want, "\r\n")] = 0;
    if (strcmp(want, line) != 0) { printf("FAIL golden is: %s\n", want); return 1; }
    printf("PASS\n");
    return 0;
}

static int Watch(const wchar_t* name, int seconds) {
    FeedReader r; FeedEvent ev; FeedSnapshot s; int64_t lost;
    long long tr[FEED_MAX_INSTRUMENTS], kl[FEED_MAX_INSTRUMENTS], latSum = 0, latN = 0, lapped = 0, anyTrade = 0;
    ULONGLONG begin = GetTickCount64(), limit = (ULONGLONG)seconds * 1000;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(out, &mode)) SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    for (;;) {
        if (limit && GetTickCount64() - begin >= limit) return anyTrade ? 0 : 1;
        printf("waiting for TickC's feed...\n");
        if (OpenRetry(&r, name, TRUE) != FEED_OK) continue;
        memset(tr, 0, sizeof(tr)); memset(kl, 0, sizeof(kl));
        ULONGLONG t0 = GetTickCount64();
        for (;;) {
            DWORD x = FeedWait(&r, 100);
            if (x == WAIT_OBJECT_0 + 1) break;   // the writer is gone
            int rc;
            while ((rc = FeedNext(&r, &ev, &lost)) != FEED_EMPTY && rc != FEED_NO_WRITER) {
                if (rc == FEED_LAPPED) { lapped++; continue; }
                if (rc != FEED_OK) continue;
                // The shared mapping is writable by any process in the
                // session (feed.h: Security), so a hostile or buggy writer
                // could publish an out-of-range instrument; skip it rather
                // than index tr[]/kl[] out of bounds. Terminal authors will
                // copy this code, so the check belongs here (phase 54).
                if (ev.instrument >= FEED_MAX_INSTRUMENTS || ev.instrument >= r.hdr->instrumentCount) continue;
                if (ev.type == FEED_TRADE) { tr[ev.instrument]++; latSum += ev.tsRecvUs - ev.tsExchangeUs; latN++; anyTrade++; }
                else if (ev.type == FEED_KLINE) kl[ev.instrument]++;
            }
            if (rc == FEED_NO_WRITER) break;
            ULONGLONG now = GetTickCount64();
            if (limit && now - begin >= limit) { FeedClose(&r); return anyTrade ? 0 : 1; }
            if (now - t0 < 1000) continue;
            double secs = (now - t0) / 1000.0;
            printf("\x1b[H\x1b[J TickC feed  state %ld  lapped %lld  latency %.1f ms\n\n",
                   r.hdr->connState, lapped, latN ? latSum / 1000.0 / latN : 0.0);
            printf(" %-10s %16s %10s %10s\n", "symbol", "last", "trades/s", "klines/s");
            for (unsigned i = 0; i < r.hdr->instrumentCount; ++i) {
                double last = FeedReadSnapshot(&r, i, &s) ? s.trade.price / 1e8 : 0.0;
                printf(" %-10s %16.2f %10.1f %10.1f\n", r.hdr->instruments[i].label, last, tr[i] / secs, kl[i] / secs);
                tr[i] = kl[i] = 0;
            }
            latSum = latN = 0;
            t0 = now;
        }
        FeedClose(&r);
    }
}

int wmain(int argc, wchar_t** argv) {
    wchar_t name[96];
    const wchar_t* golden = NULL;
    BOOL watch = FALSE, update = FALSE;
    int seconds = 0;
    wcscpy_s(name, 96, FEED_MAPPING_NAME);
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--watch") == 0) watch = TRUE;
        else if (wcscmp(argv[i], L"--update") == 0) update = TRUE;
        else if (wcscmp(argv[i], L"--seconds") == 0 && i + 1 < argc) seconds = _wtoi(argv[++i]);
        else if (wcscmp(argv[i], L"--check") == 0 && i + 1 < argc) golden = argv[++i];
        else if (wcscmp(argv[i], L"--name") == 0 && i + 1 < argc) wcscpy_s(name, 96, argv[++i]);
        else if (wcscmp(argv[i], L"--exe") == 0 && i + 1 < argc) TestBuildName(argv[++i], name, 96);
    }
    if (watch) return Watch(name, seconds);
    if (golden) return Check(name, golden, update);
    printf("usage: feed_probe --watch [--seconds S] | --check GOLDEN [--update]  [--name N | --exe PATH]\n");
    return 1;
}
