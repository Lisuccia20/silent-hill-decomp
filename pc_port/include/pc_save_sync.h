/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * pc_save_sync.h - mirror the PSX memory-card files (0.MCD / 1.MCD) to a user's
 * HTTP server. Pull on launch, push a short debounce after each save.
 * Last-writer-wins: the server is the source of truth at launch, the device is
 * the source of truth on save. See docs / config keys save_sync[_url|_token].
 */
#ifndef PC_SAVE_SYNC_H
#define PC_SAVE_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Call once at startup (after PsyX_Initialise, before the game touches the
 * memory card). Relocates the .MCD files to a persistent per-user path on
 * platforms that need it (iOS) regardless of `enabled`. When enabled and `url`
 * is non-empty, registers the write hook and starts the background push worker.
 * `url` is the base, e.g. "https://lizonline.net/sh-saves" (no trailing slash);
 * files are addressed as "<url>/<chan>.MCD". `token` is sent as X-Sync-Token. */
void Pc_SaveSync_Init(int enabled, const char* url, const char* token);

/* Blocking: download both channels from the server and overwrite the local
 * .MCD files when the server returns a full-sized card. No-op unless enabled.
 * Call before the game reads the memory card. */
void Pc_SaveSync_PullAll(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_SAVE_SYNC_H */
