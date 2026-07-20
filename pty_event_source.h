#pragma once

struct Composer;
struct Pty;

struct PtyEventHost {
    virtual void wake() = 0;
};

struct PtyEventSource {
    virtual short events() = 0;
    virtual void acknowledge() = 0;
    virtual void setWriteInterest(bool enabled) = 0;

    static PtyEventSource* create(
        Composer& composer, Pty& pty, PtyEventHost& host);
};
