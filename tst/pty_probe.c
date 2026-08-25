/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

/* Characterizes the host kernel's pty behavior, no fibers or schedulers
 * in the loop - the ground truth behind the two Darwin-gated pty unit
 * tests. Run it on a Mac and paste the output:
 *
 *   ./build pty_probe && ./pty_probe
 *
 * Probe 1: does TIOCSWINSZ on the master deliver SIGWINCH to the child?
 * Probe 2: how much does the master accept while nobody reads the
 *          slave, and does a blocking write ever push back? */

#if defined(__APPLE__)
    #define _DARWIN_C_SOURCE
#else
    #define _XOPEN_SOURCE 600
    #define _DEFAULT_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <stdlib.h>

/* Not forkpty: <pty.h> is shadowed by the project's own header on the
 * build's include path, and posix_openpt is all the probe needs. */
static int spawn_pty(int* master_out) {
    const int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0) {
        return -1;
    }
    const char* const name = ptsname(master);
    if (name == NULL) {
        close(master);
        return -1;
    }
    const pid_t child = fork();
    if (child < 0) {
        close(master);
        return -1;
    }
    if (child == 0) {
        setsid();
        const int slave = open(name, O_RDWR);
        if (slave < 0) {
            _exit(1);
        }
        ioctl(slave, TIOCSCTTY, 0);
        dup2(slave, 0);
        dup2(slave, 1);
        dup2(slave, 2);
        if (slave > 2) {
            close(slave);
        }
        return 0;
    }
    *master_out = master;
    return (int)(child);
}

static int read_line(int fd, char* out, size_t cap, int timeout_ms) {
    size_t used = 0;
    while (used + 1 < cap) {
        struct pollfd event = {fd, POLLIN, 0};
        const int ready = poll(&event, 1, timeout_ms);
        if (ready <= 0) {
            return -1;
        }
        char byte;
        const ssize_t got = read(fd, &byte, 1);
        if (got <= 0) {
            return -1;
        }
        if (byte == '\n') {
            break;
        }
        out[used++] = byte;
    }
    out[used] = 0;
    return 0;
}

static void probe_winch(void) {
    int master = -1;
    const pid_t child = spawn_pty(&master);
    if (child < 0) {
        perror("spawn_pty");
        return;
    }
    if (child == 0) {
        sigset_t signals;
        sigemptyset(&signals);
        sigaddset(&signals, SIGWINCH);
        sigprocmask(SIG_BLOCK, &signals, NULL);
        printf("ready\n");
        fflush(stdout);
        int received = 0;
        sigwait(&signals, &received);
        struct winsize size;
        ioctl(STDIN_FILENO, TIOCGWINSZ, &size);
        printf("winch %u %u\n", (unsigned)(size.ws_row), (unsigned)(size.ws_col));
        fflush(stdout);
        _exit(0);
    }
    char line[128];
    if (read_line(master, line, sizeof(line), 3000) != 0 || strstr(line, "ready") == NULL) {
        printf("probe 1: no ready handshake\n");
        kill(child, SIGKILL);
        waitpid(child, NULL, 0);
        close(master);
        return;
    }
    struct winsize size = {47, 123, 984, 752};
    if (ioctl(master, TIOCSWINSZ, &size) != 0) {
        perror("TIOCSWINSZ");
    }
    if (read_line(master, line, sizeof(line), 3000) == 0) {
        printf("probe 1: SIGWINCH delivered, child saw '%s'\n", line);
    } else {
        printf("probe 1: SIGWINCH NOT delivered within 3s\n");
    }
    kill(child, SIGKILL);
    waitpid(child, NULL, 0);
    close(master);
}

static void probe_pushback(void) {
    int master = -1;
    const pid_t child = spawn_pty(&master);
    if (child < 0) {
        perror("spawn_pty");
        return;
    }
    if (child == 0) {
        /* Reads nothing, ever. */
        for (;;) {
            pause();
        }
    }
    /* Terminal processing off, or the tty layer's echo and signal
     * handling muddy the accounting. */
    struct termios raw;
    if (tcgetattr(master, &raw) == 0) {
        cfmakeraw(&raw);
        tcsetattr(master, TCSANOW, &raw);
    }
    const int flags = fcntl(master, F_GETFL);
    fcntl(master, F_SETFL, flags | O_NONBLOCK);
    static char chunk[65536];
    memset(chunk, 'x', sizeof(chunk));
    unsigned long long accepted = 0;
    for (;;) {
        const ssize_t wrote = write(master, chunk, sizeof(chunk));
        if (wrote > 0) {
            accepted += (unsigned long long)(wrote);
            if (accepted >= 256ull * 1024 * 1024) {
                printf("probe 2: master swallowed 256MB without EAGAIN - bytes are being dropped\n");
                break;
            }
            continue;
        }
        if (wrote < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            printf("probe 2: master accepted %llu bytes, then EAGAIN (pushback works)\n", accepted);
            break;
        }
        printf("probe 2: write failed after %llu bytes: %s\n", accepted, strerror(errno));
        break;
    }
    fcntl(master, F_SETFL, flags);
    kill(child, SIGKILL);
    waitpid(child, NULL, 0);
    close(master);
}

int main(void) {
    probe_winch();
    probe_pushback();
    return 0;
}
