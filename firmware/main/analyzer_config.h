#pragma once

/* Channel hopping: 0 = hop across 1..ANALYZER_MAX_CHANNEL, otherwise stay on this channel. */
#ifndef ANALYZER_FIXED_CHANNEL
#define ANALYZER_FIXED_CHANNEL 0
#endif
#define ANALYZER_MAX_CHANNEL 13
#define ANALYZER_DWELL_MS 200

/* Capture ring: slots must be a power of two. 256 * 216 B = 54 KB.
 * Management frames keep ANALYZER_CAPTURE_BYTES so beacon IEs (SSID, DS channel, RSN) fit. */
#define ANALYZER_RING_SLOTS 256
#define ANALYZER_CAPTURE_BYTES 200

#define ANALYZER_REPORT_MS 1000
#define ANALYZER_AP_REPORT_EVERY 2  /* send AP_INFO every N reports */

/* 1 = print human-readable stats instead of emitting the binary protocol (Milestone 1). */
#ifndef ANALYZER_TEXT_MODE
#define ANALYZER_TEXT_MODE 0
#endif
