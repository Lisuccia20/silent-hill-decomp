/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * pc_ios_http.m - Pc_HttpTransfer via NSURLSession, the native HTTP client on
 * Apple mobile. Compiled ONLY on iOS (CMake), where neither WinHTTP nor libcurl
 * is linked and pc_ra_http.c falls through to its no-op stub. Desktop macOS uses
 * the libcurl path in pc_ra_http.c instead, so this file is not built there.
 *
 * NSURLSession is asynchronous; the save-sync layer calls us blockingly (startup
 * pull, worker-thread push), so we drive one request and wait on a semaphore.
 */
#import <Foundation/Foundation.h>

#include <stdlib.h>
#include <string.h>

#include "pc_ra_http.h"

int Pc_HttpTransfer(const char* method, const char* url, const char* extra_header,
                    const void* body, size_t body_len, char** out_body, size_t* out_len)
{
    if (out_body) *out_body = NULL;
    if (out_len)  *out_len  = 0;

    if (!url || !url[0] || !method || !method[0])
        return 0;

    @autoreleasepool {
        NSURL* nsurl = [NSURL URLWithString:[NSString stringWithUTF8String:url]];
        if (!nsurl)
            return 0;

        NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:nsurl];
        req.HTTPMethod = [NSString stringWithUTF8String:method];
        req.timeoutInterval = 30.0;

        if (extra_header && extra_header[0]) {
            const char* colon = strchr(extra_header, ':');
            if (colon) {
                NSString* name = [[NSString stringWithUTF8String:extra_header]
                                    substringToIndex:(NSUInteger)(colon - extra_header)];
                const char* vp = colon + 1;
                while (*vp == ' ') vp++;
                NSString* val = [NSString stringWithUTF8String:vp];
                [req setValue:val forHTTPHeaderField:name];
            }
        }
        if (body && body_len) {
            req.HTTPBody = [NSData dataWithBytes:body length:body_len];
            [req setValue:@"application/octet-stream" forHTTPHeaderField:@"Content-Type"];
        }

        __block long        status  = 0;
        __block char*       respBuf = NULL;
        __block size_t      respLen = 0;
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);

        NSURLSessionDataTask* task = [[NSURLSession sharedSession]
            dataTaskWithRequest:req
              completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                if (!error && [response isKindOfClass:[NSHTTPURLResponse class]]) {
                    status = (long)[(NSHTTPURLResponse*)response statusCode];
                    if (data.length) {
                        respBuf = (char*)malloc(data.length + 1);
                        if (respBuf) {
                            memcpy(respBuf, data.bytes, data.length);
                            respBuf[data.length] = '\0';
                            respLen = (size_t)data.length;
                        }
                    }
                }
                dispatch_semaphore_signal(sem);
            }];
        [task resume];
        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);

        if (out_body) *out_body = respBuf; else free(respBuf);
        if (out_len)  *out_len  = respLen;
        return (int)status;
    }
}
