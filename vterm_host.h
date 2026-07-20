/* This file is part of Zutty.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once
#include <std/sys/types.h>

#include <cstdint>
#include <functional>
#include <string>

class Frame;

struct VtermWindowInfo {
    i32 x = 0;
    i32 y = 0;
    u32 pixelWidth = 0;
    u32 pixelHeight = 0;
    u32 screenPixelWidth = 0;
    u32 screenPixelHeight = 0;
    bool iconified = false;
    bool maximized = false;
    bool fullscreen = false;
};

struct VtermHost {
    virtual bool present(const Frame& frame) = 0;
    virtual void osc(int command, const std::string& argument) = 0;
    virtual bool handlesOsc() const = 0;
    virtual void bell() = 0;
    virtual bool handlesPrinter() const = 0;
    virtual void print(const std::string& output) = 0;
    virtual void leds(u8 state) = 0;
    virtual void notify(const std::string& id, const std::string& title, const std::string& body, bool close) = 0;
    virtual void progress(u32 state, u32 percent) = 0;
    virtual void windowOperation(u32 operation, u32 first, u32 second) = 0;
    virtual VtermWindowInfo windowInfo() = 0;
};

// Adapter used while frontends are being migrated to implement VtermHost
// directly. Vterm itself still depends only on the host interface above.
class VtermHostCallbacks final: public VtermHost {
public:
    using RefreshHandler = std::function<bool(const Frame&)>;
    using OscHandler = std::function<void(int, const std::string&)>;
    using BellHandler = std::function<void()>;
    using PrinterHandler = std::function<void(const std::string&)>;
    using LedHandler = std::function<void(u8)>;
    using NotificationHandler = std::function<void(const std::string&, const std::string&, const std::string&, bool)>;
    using ProgressHandler = std::function<void(u32, u32)>;
    using WindowOpsHandler = std::function<void(u32, u32, u32)>;
    using WindowInfoHandler = std::function<VtermWindowInfo()>;

    VtermHostCallbacks();

    bool present(const Frame& frame) override;
    void osc(int command, const std::string& argument) override;

    bool handlesOsc() const override {
        return haveOscHandler;
    }

    void bell() override;
    bool handlesPrinter() const override;
    void print(const std::string& output) override;
    void leds(u8 state) override;
    void notify(const std::string& id, const std::string& title, const std::string& body, bool close) override;
    void progress(u32 state, u32 percent) override;
    void windowOperation(u32 operation, u32 first, u32 second) override;
    VtermWindowInfo windowInfo() override;

    void setRefreshHandler(RefreshHandler handler);
    void setOscHandler(OscHandler handler);
    void setBellHandler(BellHandler handler);
    void setPrinterHandler(PrinterHandler handler);
    void setLedHandler(LedHandler handler);
    void setNotificationHandler(NotificationHandler handler);
    void setProgressHandler(ProgressHandler handler);
    void setWindowOpsHandler(WindowOpsHandler handler);
    void setWindowInfoHandler(WindowInfoHandler handler);

private:
    RefreshHandler onRefresh;
    OscHandler onOsc;
    bool haveOscHandler = false;
    BellHandler onBell;
    PrinterHandler onPrinter;
    bool havePrinterHandler = false;
    LedHandler onLed;
    NotificationHandler onNotification;
    ProgressHandler onProgress;
    WindowOpsHandler onWindowOps;
    WindowInfoHandler onWindowInfo;
};
