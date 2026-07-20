#include "vterm_host.h"

#include "log.h"

#include <utility>

VtermHostCallbacks::VtermHostCallbacks()
    : onRefresh([](const Frame&) { return true; })
    , onOsc([](int command, const std::string& argument) {
        logU << "OSC: '" << command << ";" << argument << "'" << std::endl;
    })
    , onBell([] { logI << "* Bell *" << std::endl; })
    , onPrinter([](const std::string&) {})
    , onLed([](uint8_t) {})
    , onNotification([](const std::string&, const std::string&,
                        const std::string&, bool) {})
    , onProgress([](uint32_t, uint32_t) {})
    , onWindowOps([](uint32_t, uint32_t, uint32_t) {})
    , onWindowInfo([] { return VtermWindowInfo{}; })
{
}

bool VtermHostCallbacks::present(const Frame& frame) {
    return onRefresh(frame);
}

void VtermHostCallbacks::osc(int command, const std::string& argument) {
    onOsc(command, argument);
}

void VtermHostCallbacks::bell() { onBell(); }
void VtermHostCallbacks::print(const std::string& output) { onPrinter(output); }
void VtermHostCallbacks::leds(uint8_t state) { onLed(state); }
void VtermHostCallbacks::notify(
    const std::string& id, const std::string& title,
    const std::string& body, bool close) {
    onNotification(id, title, body, close);
}
void VtermHostCallbacks::progress(uint32_t state, uint32_t percent) {
    onProgress(state, percent);
}
void VtermHostCallbacks::windowOperation(
    uint32_t operation, uint32_t first, uint32_t second) {
    onWindowOps(operation, first, second);
}
VtermWindowInfo VtermHostCallbacks::windowInfo() { return onWindowInfo(); }

void VtermHostCallbacks::setRefreshHandler(RefreshHandler handler) {
    onRefresh = std::move(handler);
}
void VtermHostCallbacks::setOscHandler(OscHandler handler) {
    haveOscHandler = true;
    onOsc = std::move(handler);
}
void VtermHostCallbacks::setBellHandler(BellHandler handler) {
    onBell = std::move(handler);
}
void VtermHostCallbacks::setPrinterHandler(PrinterHandler handler) {
    onPrinter = std::move(handler);
}
void VtermHostCallbacks::setLedHandler(LedHandler handler) {
    onLed = std::move(handler);
}
void VtermHostCallbacks::setNotificationHandler(NotificationHandler handler) {
    onNotification = std::move(handler);
}
void VtermHostCallbacks::setProgressHandler(ProgressHandler handler) {
    onProgress = std::move(handler);
}
void VtermHostCallbacks::setWindowOpsHandler(WindowOpsHandler handler) {
    onWindowOps = std::move(handler);
}
void VtermHostCallbacks::setWindowInfoHandler(WindowInfoHandler handler) {
    onWindowInfo = std::move(handler);
}
