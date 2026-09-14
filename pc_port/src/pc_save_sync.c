/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * pc_save_sync.c - HTTP mirror of the PSX memory-card files. See pc_save_sync.h.
 *
 * Pull is blocking at startup. Push runs on a background worker: the memory-card
 * write hook only flags a channel dirty and stamps the time; the worker uploads
 * a channel once it has been quiet for the debounce window, so one save (many
 * 128-byte frame writes) becomes a single PUT of the whole 128 KB card.
 */
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <PsyX/PsyX_public.h>

#include "pc_ra_http.h"
#include "pc_save_sync.h"
#include "sh_log.h"

#define MC_FILE_SIZE   131072      /* 16 * 8 KB — a full PSX memory card */
#define SYNC_DEBOUNCE_MS 2000
#define SYNC_POLL_MS      500

static int          s_enabled = 0;
static char         s_url[256] = { 0 };
static char         s_authHeader[192] = { 0 };  /* "X-Sync-Token: <token>" */

static SDL_mutex*   s_mtx = NULL;
static SDL_atomic_t s_running;
static int          s_dirty[2] = { 0, 0 };
static Uint32       s_lastWrite[2] = { 0, 0 };

static void Sync_ChannelUrl(int chan, char* out, size_t cap)
{
	snprintf(out, cap, "%s/%d.MCD", s_url, chan & 1);
}

/* Upload the channel's current backing file. Runs on the worker thread. */
static void Sync_Push(int chan)
{
	const char* path = PsyX_MemCard_Path(chan);
	FILE*       f = fopen(path, "rb");
	char*       buf;
	size_t      got;
	char        url[320];
	int         status;

	if (!f) return;

	buf = (char*)malloc(MC_FILE_SIZE);
	if (!buf) { fclose(f); return; }
	got = fread(buf, 1, MC_FILE_SIZE, f);
	fclose(f);

	if (got == MC_FILE_SIZE) {
		Sync_ChannelUrl(chan, url, sizeof(url));
		status = Pc_HttpTransfer("PUT", url, s_authHeader, buf, got, NULL, NULL);
		if (status >= 200 && status < 300)
			SH_DBG("[SAVESYNC] pushed channel %d (%d)", chan, status);
		else
			SH_WARN("[SAVESYNC] push channel %d failed (http %d)", chan, status);
	}
	free(buf);
}

static int SDLCALL Sync_WorkerThread(void* unused)
{
	(void)unused;
	while (SDL_AtomicGet(&s_running)) {
		int c;
		SDL_Delay(SYNC_POLL_MS);
		for (c = 0; c < 2; c++) {
			int due = 0;
			SDL_LockMutex(s_mtx);
			if (s_dirty[c] && (SDL_GetTicks() - s_lastWrite[c]) >= SYNC_DEBOUNCE_MS) {
				s_dirty[c] = 0;
				due = 1;
			}
			SDL_UnlockMutex(s_mtx);
			if (due)
				Sync_Push(c);
		}
	}
	return 0;
}

/* Memory-card write hook (game thread): flag the channel, stamp the time. */
static void Sync_OnCardWrite(int chan)
{
	int c = chan & 1;
	SDL_LockMutex(s_mtx);
	s_dirty[c] = 1;
	s_lastWrite[c] = SDL_GetTicks();
	SDL_UnlockMutex(s_mtx);
}

void Pc_SaveSync_Init(int enabled, const char* url, const char* token)
{
	SDL_Thread* worker;

	/* Persistent save location: on iOS the CWD-relative "gamedata/save" is not a
	 * durable path inside the sandbox, so anchor the .MCD files under the app's
	 * pref path. Desktop keeps the CWD-relative default (no behaviour change). */
#if defined(SH_SAVE_DIR_PREFPATH)
	{
		char* pref = SDL_GetPrefPath("SilentHill", "save");
		if (pref) {
			PsyX_MemCard_SetSaveDir(pref);
			SDL_free(pref);
		}
	}
#endif

	if (!enabled || !url || !url[0]) {
		if (enabled)
			SH_WARN("[SAVESYNC] enabled but save_sync_url is empty — disabled");
		return;
	}

	snprintf(s_url, sizeof(s_url), "%s", url);
	snprintf(s_authHeader, sizeof(s_authHeader), "X-Sync-Token: %s", token ? token : "");
	s_enabled = 1;

	s_mtx = SDL_CreateMutex();
	if (!s_mtx) {
		SH_WARN("[SAVESYNC] SDL_CreateMutex failed — sync disabled");
		s_enabled = 0;
		return;
	}

	PsyX_MemCard_SetWriteHook(Sync_OnCardWrite);

	SDL_AtomicSet(&s_running, 1);
	worker = SDL_CreateThread(Sync_WorkerThread, "sh-savesync", NULL);
	if (worker)
		SDL_DetachThread(worker);
	else
		SH_WARN("[SAVESYNC] worker thread failed to start — pushes disabled");

	SH_DBG("[SAVESYNC] enabled, server %s", s_url);
}

static void Sync_PullChannel(int chan)
{
	char   url[320];
	char*  body = NULL;
	size_t len = 0;
	int    status;

	Sync_ChannelUrl(chan, url, sizeof(url));
	status = Pc_HttpTransfer("GET", url, s_authHeader, NULL, 0, &body, &len);

	if (status == 200 && body && len == MC_FILE_SIZE) {
		const char* path = PsyX_MemCard_Path(chan);
		FILE* f = fopen(path, "wb");
		if (f) {
			fwrite(body, 1, len, f);
			fclose(f);
			SH_DBG("[SAVESYNC] pulled channel %d", chan);
		} else {
			SH_WARN("[SAVESYNC] pull channel %d: cannot write %s", chan, path);
		}
	} else if (status == 404) {
		SH_DBG("[SAVESYNC] channel %d not on server yet", chan);
	} else {
		SH_WARN("[SAVESYNC] pull channel %d skipped (http %d, %zu bytes)",
		        chan, status, len);
	}
	free(body);
}

void Pc_SaveSync_PullAll(void)
{
	if (!s_enabled)
		return;
	Sync_PullChannel(0);
	Sync_PullChannel(1);
}
