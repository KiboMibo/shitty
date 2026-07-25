/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "heap_profile.h"

#include <std/ios/out_fd.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/fd.h>
#include <std/sys/throw.h>

#include <gperftools/heap-profiler.h>
#include <gperftools/malloc_extension.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

#include <stdexcept>
#include <string>

using namespace stl;

namespace {
    constexpr const char* defaultProfilePath = "heap.prof";
    constexpr const char* profilePathEnvironment = "SHITTY_HEAP_PROFILE";
    constexpr const char* sampleEnvironment = "TCMALLOC_SAMPLE_PARAMETER";
    constexpr size_t defaultSampleInterval = 4096;
}

void initializeHeapProfile() {
    if (IsHeapProfilerRunning() != 0) {
        throw std::runtime_error("HEAPPROFILE cannot be combined with sampled heap profiling");
    }
    size_t sampleInterval = defaultSampleInterval;
    if (const char* configured = getenv(sampleEnvironment); configured != nullptr) {
        char* end = nullptr;
        errno = 0;
        const unsigned long long parsed = strtoull(configured, &end, 10);
        if (errno != 0 || end == configured || *end != 0 || parsed == 0) {
            throw std::invalid_argument("TCMALLOC_SAMPLE_PARAMETER must be a positive integer");
        }
        sampleInterval = parsed;
    }
    if (!MallocExtension::instance()->SetNumericProperty("tcmalloc.sample_parameter", sampleInterval)) {
        throw std::runtime_error("TCMalloc heap sampling is unavailable");
    }
}

void dumpHeapProfile() {
    const char* path = getenv(profilePathEnvironment);
    if (path == nullptr || *path == 0) {
        path = defaultProfilePath;
    }

    std::string profile;
    MallocExtension::instance()->GetHeapSample(&profile);

    const int rawFd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (rawFd < 0) {
        Errno().raise(StringBuilder() << StringView(u8"cannot open heap profile ") << StringView(path));
    }

    ScopedFD fd(rawFd);
    FDRegular output(fd);
    output.writeC(profile.data(), profile.size());
}
