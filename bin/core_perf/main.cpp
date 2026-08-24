/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

// Feeds a corpus file through the complete headless VT core - parser,
// vterm and screen, with the frame consumed each time - and reports the
// throughput. Unlike parser_perf, which drives the FSM with no-op
// callbacks, this measures the work an embedder actually pays for.
//
// Usage: core_perf <corpus-path> [runs] [save-lines]
//
// save-lines matters: the default Options carries 0, so a run left at
// the default measures a terminal with no scrollback to retain.

#include "composer.h"
#include "num.h"
#include "options.h"
#include "vterm_headless.h"

#include <std/ios/sys.h>
#include <std/ios/output.h>
#include <std/mem/obj_pool.h>
#include <std/str/view.h>

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

using namespace stl;

namespace {
    // One PTY read's worth per feed, so the batching matches what a host
    // draining a pty would hand over.
    constexpr size_t chunkBytes = 64 * 1024;

    static double now() {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (double)(ts.tv_sec) + (double)(ts.tv_nsec) / 1e9;
    }

    static u8* readFile(const char* path, size_t& length) {
        const int fd = open(path, O_RDONLY);
        if (fd < 0) {
            return nullptr;
        }
        struct stat info;
        if (fstat(fd, &info) < 0) {
            close(fd);
            return nullptr;
        }
        length = (size_t)(info.st_size);
        u8* const data = (u8*)(malloc(length));
        size_t taken = 0;
        while (taken < length) {
            const ssize_t got = read(fd, data + taken, length - taken);
            if (got <= 0) {
                break;
            }
            taken += (size_t)(got);
        }
        close(fd);
        length = taken;
        return data;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        sysE << StringView(u8"usage: core_perf <corpus-path> [runs] [save-lines]") << endL;
        return 2;
    }
    i64 runs = 3;
    if (argc > 2 && (!parseI64(StringView(argv[2]), runs) || runs <= 0)) {
        sysE << StringView(u8"usage: core_perf <corpus-path> [runs] [save-lines]") << endL;
        return 2;
    }
    i64 saveLines = 0;
    if (argc > 3 && (!parseI64(StringView(argv[3]), saveLines) || saveLines < 0 || saveLines > 50000)) {
        sysE << StringView(u8"usage: core_perf <corpus-path> [runs] [save-lines]") << endL;
        return 2;
    }

    size_t length = 0;
    u8* const corpus = readFile(argv[1], length);
    if (corpus == nullptr || length == 0) {
        sysE << StringView(u8"core_perf: cannot read corpus") << endL;
        return 2;
    }

    double best = 0.0;
    for (i64 run = 0; run < runs; ++run) {
        // A fresh terminal per run: scrollback accumulated by an earlier
        // run would otherwise pay for the next one's allocations.
        ObjPool::Ref pool = ObjPool::fromMemory();
        Composer* const composer = pool->make<Composer>(pool.mutPtr());
        // Composer builds a default Options; swap in one carrying the
        // requested scrollback, since the default retains none.
        Options* const options = pool->make<Options>();
        options->saveLines = (u16)(saveLines);
        composer->opts = options;
        VtermHeadless* const host = VtermHeadless::create(*composer, nullptr, nullptr);

        const double start = now();
        size_t fed = 0;
        while (fed < length) {
            size_t take = length - fed;
            if (take > chunkBytes) {
                take = chunkBytes;
            }
            host->feed(corpus + fed, take);
            fed += take;
        }
        const double elapsed = now() - start;
        if (best == 0.0 || elapsed < best) {
            best = elapsed;
        }
    }

    const double mebibytes = (double)(length) / (double)(1 << 20);
    sysO << StringView(u8"core_perf ") << StringView(argv[1]) << StringView(u8": ")
         << (i64)(length) << StringView(u8" bytes in ") << (i64)(best * 1e6)
         << StringView(u8" us, ") << (i64)(mebibytes / best)
         << StringView(u8" MiB/s, save_lines=") << saveLines << endL;
    free(corpus);
    return 0;
}
