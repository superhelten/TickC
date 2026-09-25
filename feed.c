// feed.c - the TickC feed's writer (phase 54): the ring and the snapshot
// table in shared memory, the Binance message parser, and FeedThread, which
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
        InterlockedIncrement64((LONG64 volatile*)&s->lock);
        s->updatedUs = 0;
        memset(&s->trade, 0, sizeof(s->trade));
        memset(&s->kline, 0, sizeof(s->kline));
        InterlockedIncrement64((LONG64 volatile*)&s->lock);
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
    seq  = FeedLoadNoFence64(&h->writeSeq);   // only this thread writes it
    slot = FeedSlot(w, seq);
    InterlockedExchange64((LONG64 volatile*)&slot->seq, FEED_SEQ_BUSY);   // a full barrier
    memcpy((uint8_t*)slot + 8, (const uint8_t*)ev + 8, FEED_SLOT_SIZE - 8);
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

// magic goes to 0 first: a reader that opens now, or reads on, sees
// FEED_NO_WRITER instead of a writer that is gone.
void FeedWriterClose(FeedWriter* w) {
    if (w->hdr) InterlockedExchange((LONG volatile*)(volatile void*)&w->hdr->magic, 0);
    for (int i = 0; i < FEED_MAX_READERS; ++i) if (w->hWake[i]) CloseHandle(w->hWake[i]);
    if (w->hdr)  UnmapViewOfFile(w->hdr);
    if (w->hMap) CloseHandle(w->hMap);
    memset(w, 0, sizeof(*w));
}
