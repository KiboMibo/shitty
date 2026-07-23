/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

/* part of this file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE.GPL3 for the full license.
 */

#pragma once

#include "options.h"

#include <std/sys/types.h>

#include <iomanip>
#include <iostream>
#include <string>

#define zlog          \
    if (opts.quiet) { \
        ;             \
    } else            \
        std::cout

#define vlog                           \
    if (opts.quiet || !opts.verbose) { \
        ;                              \
    } else                             \
        std::cout

#define tlog                         \
    if (opts.quiet || !opts.trace) { \
        ;                            \
    } else                           \
        std::cout

const char* logFileName(const char* path);

#define plog(Ostream, Prefix) Ostream << Prefix << " [" << logFileName(__FILE__) << ":" << std::setw(3) << __LINE__ << "] "

#define logE plog(zlog, "E") << "Error: "
#define logW plog(zlog, "W") << "Warning: "
#define logU plog(tlog, "T") << "(Unimplemented) "
#define logI plog(vlog, "I")
#define logT plog(tlog, "T")

inline void printArgs() {
    zlog << std::endl;
}

template <typename T, typename... Args>
inline void printArgs(T arg, Args... args) {
    zlog << arg;
    printArgs(args...);
}

void redirectFds(int fd);
void restoreFds();

#define logSysE(...) \
    logE;            \
    printArgs(__VA_ARGS__)
#define logSysW(...) \
    logW;            \
    printArgs(__VA_ARGS__)

#define SYS_ERROR(...)                                                 \
    do {                                                               \
        const auto ec = errno;                                         \
        restoreFds();                                                  \
        logSysE(__VA_ARGS__, ": ", strerror(ec), " (errno=", ec, ")"); \
        exit(1);                                                       \
    } while (0);

#define SYS_WARN(...)                                                  \
    do {                                                               \
        const auto ec = errno;                                         \
        logSysW(__VA_ARGS__, ": ", strerror(ec), " (errno=", ec, ")"); \
    } while (0);

std::string dumpBuffer(const unsigned char* start, const unsigned char* end);

inline std::string dumpBuffer(const u8* start, const u8* end) {
    return dumpBuffer((const unsigned char*)start, (const unsigned char*)end);
}
