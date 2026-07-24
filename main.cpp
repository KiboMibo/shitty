/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "application.h"
#include "composer.h"
#include "font_resolver.h"
#include "vterm_headless.h"

#include <std/ios/in_fd.h>
#include <std/ios/sys.h>
#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/fd.h>
#include <std/sys/fs.h>
#include <std/sys/throw.h>

#include <chrono>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>

#include <fcntl.h>

namespace stl {}

using namespace stl;

namespace {
    struct PerfFile {
        size_t pathOffset;
    };

    void writeRepeated(ZeroCopyOutput& output, u8 byte, size_t count) {
        if (count == 0) {
            return;
        }
        u8* const bytes = static_cast<u8*>(output.imbue(count).ptr);
        memset(bytes, byte, count);
        output.commit(count);
    }

    void writeTenths(ZeroCopyOutput& output, double value) {
        const u64 tenths = (u64)(value * 10 + 0.5);
        output << tenths / 10 << StringView(u8".") << tenths % 10;
    }

    void showPerfProgress(size_t done, size_t total, size_t bytes, std::chrono::steady_clock::time_point started) {
        constexpr size_t width = 40;
        const size_t filled = total == 0 ? width : done * width / total;
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const double mib = bytes / (1024.0 * 1024.0);
        const double mibPerSecond = elapsed > 0 ? mib / elapsed : 0;
        OutBuf output(stderrStream());
        output << StringView(u8"\r[");
        writeRepeated(output, u8'#', filled);
        writeRepeated(output, u8' ', width - filled);
        output << StringView(u8"] ") << (u64)(done) << StringView(u8"/") << (u64)(total) << StringView(u8" ");
        writeTenths(output, mib);
        output << StringView(u8" MiB, ");
        writeTenths(output, mibPerSecond);
        output << StringView(u8" MiB/s") << flsH;
    }

    int runPerf(int argc, char* argv[]) {
        if (argc < 3) {
            throw std::invalid_argument("usage: st perf DIRECTORY...");
        }

        Buffer paths;
        Vector<PerfFile> files;
        for (int index = 2; index < argc; ++index) {
            const StringView directory(argv[index]);
            listDir(directory, [&](const TPathInfo& entry) {
                if (!entry.isDir) {
                    const PerfFile file{.pathOffset = paths.used()};
                    paths.append(directory.data(), directory.length());
                    if (directory.empty() || directory.back() != '/') {
                        const u8 slash = '/';
                        paths.append(&slash, 1);
                    }
                    paths.append(entry.item.data(), entry.item.length());
                    const u8 zero = 0;
                    paths.append(&zero, 1);
                    files.pushBack(file);
                }
            });
        }

        ObjPool::Ref pool = ObjPool::fromMemory();
        Composer composer{pool.mutPtr()};
        VtermHeadless* vterm = VtermHeadless::create(composer);
        Buffer data;
        size_t bytes = 0;
        const auto started = std::chrono::steady_clock::now();
        showPerfProgress(0, files.length(), bytes, started);
        for (size_t index = 0; index < files.length(); ++index) {
            const char* path = (const char*)paths.data() + files[index].pathOffset;
            const int rawFd = open(path, O_RDONLY);
            if (rawFd < 0) {
                Errno().raise(StringBuilder() << StringView(u8"cannot open ") << StringView(path));
            }

            ScopedFD fd(rawFd);
            data.reset();
            FDInput(fd).readAll(data);
            vterm->feed((const u8*)data.data(), data.used());
            bytes += data.used();
            if ((index + 1) % 256 == 0 || index + 1 == files.length()) {
                showPerfProgress(index + 1, files.length(), bytes, started);
            }
        }
        sysE << endL;
        return 0;
    }
}

int main(int argc, char* argv[]) {
    int status = 1;
    try {
        if (argc > 1 && std::string(argv[1]) == "perf") {
            status = runPerf(argc, argv);
        } else {
            ObjPool::Ref pool = ObjPool::fromMemory();
            Composer composer;
            composer.pool = pool.mutPtr();
            composer.application = Application::create(composer);
            status = composer.application->run(argc, argv);
        }
    } catch (Exception& error) {
        const StringView message = error.description();
        sysE << StringView(u8"Error: ") << message << endL;
    } catch (const std::exception& error) {
        sysE << StringView(u8"Error: ") << StringView(error.what()) << endL;
    }
    finalizeFontconfig();
    return status;
}
