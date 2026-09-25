// feed.c - the TickC feed's writer (phase 54): the ring and the snapshot
// table in shared memory, the Binance message parser, and FeedThread, which
// reads Binance's WebSocket stream and publishes it. The contract - the
// layout and the reader - is feed.h. One writer per mapping: only TickC's
// main instance starts the feed.
#include "feed.h"
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")
