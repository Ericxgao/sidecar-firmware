// Experimental DN2 profile, opt-in at boot only.
// USB product ID and audio layout: Overwitch 8b7b0a6 res/devices.json.
// Native controls/returns use the matching DN2 bridge; no remote screen or project exchange.
#pragma once
#include "device/device.h"
#include "fx/out.h"

namespace core { namespace device {
constexpr uint16_t kIdDn2 = 0x3f02; // provisional profile ID, pending upstream assignment
constexpr uint16_t kDn2Vid = 0x1935, kDn2Pid = 0x0b34;
inline constexpr out::Device kDn2Audio = {
    "Digitone II (experimental)", 8, {0, 1}, {0, 1},
    {255, 255, 255, 255, 255, 255, 255, 255}, {0, 1, 2, 3, 4, 5, 6, 7},
    {{0, -1}, {1, -1}, {2, -1}, {3, -1}, {4, -1}, {5, -1}, {6, -1}, {7, -1}},
    {10, 11}, 0, 42, out::kStereoMix, out::kObDn2};
inline constexpr Profile kDn2 = {
    kIdDn2, "Digitone II (experimental)", kDn2Vid, kDn2Pid,
    16, 8, 16, // eight native parts; stock MIDI still selects one channel -> voice 0
    {0x7d, 0x52, 0x54}, 0x04, 0x03, 0x11,
    0, 0, 0, {}, {}, {}, nullptr, "/.dn2",
    {1u << 0x03 | 1u << 0x04 | 1u << 0x11}, // local note adapter's reports only
    kBracketMix, &kDn2Audio, nullptr, false, true, 0, false, nullptr, "DN2"};
}}
