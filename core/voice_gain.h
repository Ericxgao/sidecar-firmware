// Shared legacy Sidecar mixer headroom; independent DN2 returns undo this cut.
#pragma once
namespace core {
constexpr float kVoiceGain = 0.35f;
constexpr int kVoiceGainQ15 = int(kVoiceGain * 32768.0f + 0.5f);
}
