/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vterm_trace.h"

#include "composer.h"

#include <std/mem/obj_pool.h>

#include <algorithm>
#include <limits>
#include <vector>

namespace stl {}

using namespace stl;

namespace {

    struct TraceEvent {
        std::string type;
        std::string data;
    };

    struct VtermTraceImpl final: public VtermTrace {
        void text(const u8* data, size_t size) override;
        void control(u8 ch) override;
        void escapeBegin() override;
        void escapeByte(u8 ch) override;
        void escapeEnd() override;
        void escapeCancel() override;
        void csi(u8 finalByte, const std::string& privatePrefix, const std::string& intermediates, const u32* parameters, const unsigned char* separators, size_t parameterCount, bool hadParameters) override;
        void stringBegin(VtermTraceString type) override;
        void stringData(const u8* data, size_t size) override;
        void stringEnd() override;
        void stringCancel() override;
        std::string drain() override;
        void clear() override;

        constexpr static size_t noEvent = std::numeric_limits<size_t>::max();

        size_t add(const char* type);
        void erase(size_t& index);
        void consumeDcsHeader(const u8* data, size_t size);
        static const char* stringName(VtermTraceString type);
        static std::string encodeHex(const std::string& input);

        std::vector<TraceEvent> events;
        size_t escapeEvent = noEvent;
        size_t stringEvent = noEvent;
        bool dcsHeaderComplete = false;
        bool dcsHeaderIntermediate = false;
        bool dcsHeaderInvalid = false;
    };

}

size_t VtermTraceImpl::add(const char* type) {
    events.push_back(TraceEvent{type, {}});
    return events.size() - 1;
}

void VtermTraceImpl::erase(size_t& index) {
    if (index == noEvent) {
        return;
    }
    events.erase(events.begin() + index);
    if (escapeEvent != noEvent && escapeEvent > index) {
        --escapeEvent;
    }
    if (stringEvent != noEvent && stringEvent > index) {
        --stringEvent;
    }
    index = noEvent;
}

const char* VtermTraceImpl::stringName(VtermTraceString type) {
    switch (type) {
        case VtermTraceString::Osc:
            return "osc";
        case VtermTraceString::Dcs:
            return "dcs";
        case VtermTraceString::Apc:
            return "apc";
        case VtermTraceString::Pm:
            return "pm";
        case VtermTraceString::Sos:
            return "sos";
    }
    return "string";
}

std::string VtermTraceImpl::encodeHex(const std::string& input) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(input.size() * 2);
    for (const unsigned char ch : input) {
        result.push_back(digits[ch >> 4]);
        result.push_back(digits[ch & 15]);
    }
    return result;
}

void VtermTraceImpl::text(const u8* data, size_t size) {
    if (!size) {
        return;
    }
    if (events.empty() || events.back().type != "text") {
        add("text");
    }
    events.back().data.append((const char*)(data), size);
}

void VtermTraceImpl::control(u8 ch) {
    const size_t index = add("control");
    events[index].data.push_back((char)(ch));
}

void VtermTraceImpl::escapeBegin() {
    escapeCancel();
    escapeEvent = add("escape");
}

void VtermTraceImpl::escapeByte(u8 ch) {
    if (escapeEvent != noEvent) {
        events[escapeEvent].data.push_back((char)(ch));
    }
}

void VtermTraceImpl::escapeEnd() {
    escapeEvent = noEvent;
}

void VtermTraceImpl::escapeCancel() {
    erase(escapeEvent);
}

void VtermTraceImpl::csi(u8 finalByte, const std::string& privatePrefix, const std::string& intermediates, const u32* parameters, const unsigned char* separators, size_t parameterCount, bool hadParameters) {
    escapeCancel();
    const size_t index = add("csi");
    std::string& sequence = events[index].data;
    sequence = privatePrefix;
    if (hadParameters) {
        for (size_t k = 0; k < parameterCount; ++k) {
            if (k) {
                sequence.push_back((char)(separators[k]));
            }
            sequence += std::to_string(parameters[k]);
        }
    }
    sequence += intermediates;
    sequence.push_back((char)(finalByte));
}

void VtermTraceImpl::stringBegin(VtermTraceString type) {
    escapeCancel();
    stringCancel();
    stringEvent = add(stringName(type));
    dcsHeaderComplete = false;
    dcsHeaderIntermediate = false;
    dcsHeaderInvalid = false;
}

void VtermTraceImpl::stringData(const u8* data, size_t size) {
    if (stringEvent != noEvent) {
        if (events[stringEvent].type == "dcs") {
            consumeDcsHeader(data, size);
        }
        events[stringEvent].data.append((const char*)(data), size);
    }
}

void VtermTraceImpl::stringEnd() {
    if (stringEvent != noEvent && events[stringEvent].type == "dcs" && (!dcsHeaderComplete || dcsHeaderInvalid)) {
        erase(stringEvent);
        return;
    }
    stringEvent = noEvent;
}

void VtermTraceImpl::stringCancel() {
    erase(stringEvent);
}

void VtermTraceImpl::consumeDcsHeader(const u8* data, size_t size) {
    for (size_t k = 0; k < size && !dcsHeaderComplete && !dcsHeaderInvalid; ++k) {
        const u8 ch = data[k];
        if (ch >= 0x40 && ch <= 0x7e) {
            dcsHeaderComplete = true;
        } else if (ch >= 0x20 && ch <= 0x2f) {
            dcsHeaderIntermediate = true;
        } else if (ch < 0x30 || ch > 0x3f || dcsHeaderIntermediate) {
            dcsHeaderInvalid = true;
        }
    }
}

std::string VtermTraceImpl::drain() {
    std::string result;
    size_t count = events.size();
    if (escapeEvent != noEvent) {
        count = std::min(count, escapeEvent);
    }
    if (stringEvent != noEvent) {
        count = std::min(count, stringEvent);
    }
    for (size_t k = 0; k < count; ++k) {
        result += events[k].type + " " + encodeHex(events[k].data) + "\n";
    }
    events.erase(events.begin(), events.begin() + count);
    if (escapeEvent != noEvent) {
        escapeEvent -= count;
    }
    if (stringEvent != noEvent) {
        stringEvent -= count;
    }
    return result;
}

void VtermTraceImpl::clear() {
    events.clear();
    escapeEvent = noEvent;
    stringEvent = noEvent;
}

VtermTrace* VtermTrace::create(Composer& composer) {
    return composer.pool->make<VtermTraceImpl>();
}
