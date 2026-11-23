// One fake `okay::xbox::CaptureHost` for the Xbox tests (the microphone seam, XboxCapture.hpp): a device list, a format, the capability and
// consent answers, and a Start that can refuse. Shared by test_xbox_uwp_pieces.cpp (the capture device) and test_xbox_uwp_runtime.cpp (the
// backend's OpenAudio(capture)), so the two tests do not keep two copies of it.
#pragma once
#include "XboxCapture.hpp"
#include <string>
#include <vector>

struct FakeCaptureHost : okay::xbox::CaptureHost {
    okay::xbox::CaptureFormat fmt{48000, 1, true, 32};
    bool declared = true;
    okay::xbox::MicConsent consent = okay::xbox::MicConsent::Allowed;
    bool startOk = true;
    std::string refuseText = "prompt refused";   // the error Start reports when startOk is false
    std::vector<std::string> names{"Headset", "Room mic"};
    int starts = 0, stops = 0, lastIndex = -1;
    okay::xbox::UwpCaptureDevice* dev = nullptr;   // the device that was started (null after Stop)

    int DeviceCount() override { return (int)names.size(); }
    std::string DeviceName(int i) override { return names[(size_t)i]; }
    bool Format(int, okay::xbox::CaptureFormat& o) override { o = fmt; return true; }
    bool CapabilityDeclared() override { return declared; }
    okay::xbox::MicConsent Consent() override { return consent; }
    bool Start(okay::xbox::UwpCaptureDevice* d, int i, std::string& e) override {
        if (!startOk) { e = refuseText; return false; }
        ++starts; lastIndex = i; dev = d; return true;
    }
    void Stop(okay::xbox::UwpCaptureDevice*) override { ++stops; dev = nullptr; }
};
