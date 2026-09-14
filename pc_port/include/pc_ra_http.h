/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * pc_ra_http.h - blocking HTTP used by the RetroAchievements client.
 *
 * Implemented in pc_ra_http.c, which is the only translation unit allowed to
 * include <windows.h> (it collides with the PSX headers). Call from the RA
 * worker thread only — it blocks.
 */
#ifndef PC_RA_HTTP_H
#define PC_RA_HTTP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GET when `post` is NULL/empty, otherwise form-encoded POST. On success
 * *out_body is a malloc'd NUL-terminated buffer the caller frees. Returns the
 * HTTP status code, or 0 if the request could not be made. */
/* Resolve one of the exe's own exported globals by name (NULL if absent). */
void* Pc_RaSymbolLookup(const char* name);

int Pc_RaHttpRequest(const char* url, const char* post, char** out_body, size_t* out_len);

/* General blocking HTTP, needed by the save-sync layer (RA's helper above only
 * does GET / form-POST). `method` is "GET"/"PUT"/"POST". `extra_header`, if
 * non-NULL, is one full header line WITHOUT trailing CRLF (e.g.
 * "X-Sync-Token: abc"). `body`/`body_len` is a request body that MAY contain
 * NUL bytes (a raw .MCD), or NULL/0 for none. On success *out_body is a malloc'd
 * buffer (NUL-terminated for convenience, but *out_len is the true byte count)
 * the caller frees; pass NULL for out_body to discard the response. Returns the
 * HTTP status code, or 0 if the request could not be made. Blocks — call off the
 * main thread (or at startup, where a stall is acceptable). */
int Pc_HttpTransfer(const char* method, const char* url, const char* extra_header,
                    const void* body, size_t body_len, char** out_body, size_t* out_len);

#ifdef __cplusplus
}
#endif

#endif /* PC_RA_HTTP_H */
