// Channel policy for the one-track DN2 bring-up. No I/O or render state.
#pragma once
#include <stdint.h>
inline bool dn2_channel(uint8_t& status, uint8_t d1, uint8_t d2, unsigned channel) {
  if (channel < 1 || channel > 16 || status < 0x80 || status >= 0xf0 ||
      (status & 15) != channel - 1 || d1 > 127 || d2 > 127) return false;
  const unsigned type = status & 0xf0;
  if (type != 0x80 && type != 0x90 &&
      !(type == 0xb0 && ((d1 >= 16 && d1 <= 31) || d1 == 120 || d1 == 123))) return false;
  status = uint8_t(type); // selected channel always drives voice 0
  return true;
}
