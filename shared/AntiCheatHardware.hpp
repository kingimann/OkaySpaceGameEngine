#pragma once
// Anti-cheat: a salted hardware hash (docs/anticheat.md, "Hardware hash").
//
// A ban can name a machine as well as an account, so a banned cheater can't just
// make a new account. What's sent is only
//     HMAC-SHA256(key = "okayac-hw|" + salt, CanonicalHardware(parts))
// as 64 hex digits; the raw values never leave the machine and aren't stored.
//
// What goes in (whatever the platform has; empty values are left out):
//   machine_guid   Windows: HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid
//                  Linux:   /etc/machine-id (or /var/lib/dbus/machine-id)
//   volume_serial  Windows: the serial number of the volume Windows is on
//   cpu            x86: the CPUID vendor and brand string
// Each part is lower-cased and trimmed, the parts are sorted by name and written
// as "name=value\n" lines, so the hash is stable across runs and reboots. It
// changes when Windows is reinstalled, the system drive is reformatted or the
// CPU is swapped: it's a signal for a reviewer, not proof.
//
// The salt (-DOKAY_AC_HW_SALT, default "okayspace-ac-hw/1") makes one
// deployment's hashes unrelated to another's. It needn't be secret: the
// machine GUID alone is 122 random bits, too many to guess from a hash.
#include <cstdio>
#include <fstream>

#include "AntiCheatChecks.hpp"   // detail::HmacSha256

#if defined(_WIN32)
#include <windows.h>
// Older MinGW headers lack this RegGetValue flag (read the 64-bit registry view
// from a 32-bit process); its value is fixed by the Windows SDK.
#ifndef RRF_SUBKEY_WOW6464KEY
#define RRF_SUBKEY_WOW6464KEY 0x00010000
#endif
#endif
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
#include <cpuid.h>
#define OKAY_AC_HAVE_CPUID 1
#endif

#ifndef OKAY_AC_HW_SALT
#define OKAY_AC_HW_SALT "okayspace-ac-hw/1"
#endif

namespace okayac {

struct HardwareInfo {
    std::vector<std::pair<std::string, std::string>> parts;   // name, raw value (stays on this machine)
};

inline std::string DeploymentSalt() { return OKAY_AC_HW_SALT; }

// The text that's hashed (documented above; tests pin it).
inline std::string CanonicalHardware(const HardwareInfo& h) {
    std::vector<std::pair<std::string, std::string>> v;
    for (const auto& p : h.parts) {
        const std::string name = og::Lower(og::Trim(p.first)), value = og::Lower(og::Trim(p.second));
        if (name.empty() || value.empty()) continue;
        v.emplace_back(name, value);
    }
    std::sort(v.begin(), v.end());
    std::string s;
    for (const auto& p : v) s += p.first + "=" + p.second + "\n";
    return s;
}

// "" when there's nothing to go on.
inline std::string HardwareHash(const HardwareInfo& h, const std::string& salt) {
    const std::string c = CanonicalHardware(h);
    if (c.empty()) return {};
    const auto d = detail::HmacSha256("okayac-hw|" + salt, c);
    return ToHex(d.data(), d.size());
}

namespace detail {
inline std::string FirstLineOf(const char* path) {
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    return og::Trim(line);
}
inline std::string CpuId() {
#ifdef OKAY_AC_HAVE_CPUID
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (!__get_cpuid(0, &a, &b, &c, &d)) return {};
    char vendor[13] = {0};
    std::memcpy(vendor, &b, 4); std::memcpy(vendor + 4, &d, 4); std::memcpy(vendor + 8, &c, 4);
    std::string s = vendor;
    unsigned maxExt = __get_cpuid_max(0x80000000u, nullptr);
    if (maxExt >= 0x80000004u) {
        char brand[49] = {0};
        for (unsigned i = 0; i < 3; ++i) {
            unsigned r[4] = {0, 0, 0, 0};
            __get_cpuid(0x80000002u + i, &r[0], &r[1], &r[2], &r[3]);
            std::memcpy(brand + 16 * i, r, 16);
        }
        s += " " + og::Trim(brand);
    }
    return s;
#else
    return {};
#endif
}
#if defined(_WIN32)
inline std::string Narrow(const wchar_t* w) {
    char buf[512];
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, (int)sizeof(buf), nullptr, nullptr);
    return n > 1 ? std::string(buf, (std::size_t)(n - 1)) : std::string();
}
#endif
} // namespace detail

// This machine's parts (raw: they're hashed by HardwareHash and go nowhere else).
inline HardwareInfo CollectHardware() {
    HardwareInfo h;
#if defined(_WIN32)
    {
        wchar_t guid[128] = L"";
        DWORD size = sizeof(guid);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid",
                         RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, guid, &size) == ERROR_SUCCESS)
            h.parts.push_back({"machine_guid", detail::Narrow(guid)});
    }
    {
        wchar_t win[MAX_PATH] = L"";
        const UINT n = GetSystemWindowsDirectoryW(win, MAX_PATH);
        if (n >= 3 && n < MAX_PATH) {
            wchar_t root[4] = {win[0], L':', L'\\', 0};
            DWORD serial = 0;
            if (GetVolumeInformationW(root, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0)) {
                char b[16];
                std::snprintf(b, sizeof(b), "%08lx", (unsigned long)serial);
                h.parts.push_back({"volume_serial", b});
            }
        }
    }
#elif defined(__linux__)
    std::string id = detail::FirstLineOf("/etc/machine-id");
    if (id.empty()) id = detail::FirstLineOf("/var/lib/dbus/machine-id");
    if (!id.empty()) h.parts.push_back({"machine_guid", id});
#endif
    const std::string cpu = detail::CpuId();
    if (!cpu.empty()) h.parts.push_back({"cpu", cpu});
    return h;
}

// This machine's hash for this deployment ("" if the platform offers nothing).
inline std::string MachineHash() {
    static const std::string h = HardwareHash(CollectHardware(), DeploymentSalt());
    return h;
}

} // namespace okayac
