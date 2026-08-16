/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int write_all(const char* data, size_t size) {
    while (size != 0) {
        const ssize_t written = write(STDOUT_FILENO, data, size);
        if (written <= 0) {
            return 1;
        }
        data += written;
        size -= (size_t)(written);
    }
    return 0;
}

static int ready(void) {
    static const char message[] = "ready\n";
    return write_all(message, sizeof(message) - 1);
}

static int wait_for_winsize(void) {
    sigset_t signals;
    if (sigemptyset(&signals) != 0 ||
        sigaddset(&signals, SIGWINCH) != 0 ||
        sigprocmask(SIG_BLOCK, &signals, NULL) != 0 ||
        ready() != 0) {
        return 1;
    }

    int received = 0;
    if (sigwait(&signals, &received) != 0 || received != SIGWINCH) {
        return 1;
    }
    struct winsize size;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &size) != 0) {
        return 1;
    }
    char message[64];
    const int length = snprintf(
        message,
        sizeof(message),
        "%u %u\n",
        (unsigned)(size.ws_row),
        (unsigned)(size.ws_col)
    );
    if (length <= 0 || (size_t)(length) >= sizeof(message)) {
        return 1;
    }
    return write_all(message, (size_t)(length));
}

static int wait_for_hangup(void) {
    sigset_t signals;
    if (signal(SIGHUP, SIG_DFL) == SIG_ERR ||
        sigemptyset(&signals) != 0 ||
        sigaddset(&signals, SIGHUP) != 0 ||
        sigprocmask(SIG_UNBLOCK, &signals, NULL) != 0 ||
        ready() != 0) {
        return 1;
    }
    for (;;) {
        pause();
    }
}

static int flood_until_hangup(void) {
    sigset_t signals;
    if (signal(SIGHUP, SIG_DFL) == SIG_ERR ||
        sigemptyset(&signals) != 0 ||
        sigaddset(&signals, SIGHUP) != 0 ||
        sigprocmask(SIG_UNBLOCK, &signals, NULL) != 0) {
        return 1;
    }
    static const char payload[] = "engaged-flood\n";
    for (;;) {
        if (write_all(payload, sizeof(payload) - 1) != 0) {
            return 1;
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        return 2;
    }
    if (strcmp(argv[1], "winsize") == 0) {
        return wait_for_winsize();
    }
    if (strcmp(argv[1], "hangup") == 0) {
        return wait_for_hangup();
    }
    if (strcmp(argv[1], "flood-hangup") == 0) {
        return flood_until_hangup();
    }
    return 2;
}
