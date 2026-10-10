#include "protocol.h"

#include <cstring>

namespace esphome {
namespace snk_mower {
namespace proto {

// CRC-8/Maxim (Dallas), reflected polynomial 0x31.
uint8_t crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
      crc = (crc & 1) ? (crc >> 1) ^ 0x8C : crc >> 1;
  }
  return crc;
}

size_t encode_frame(const char *json, size_t len, uint8_t *out, size_t out_size) {
  if (len + 3 > out_size)
    return 0;
  out[0] = '&';
  memcpy(out + 1, json, len);
  out[len + 1] = crc8(reinterpret_cast<const uint8_t *>(json), len);
  out[len + 2] = '#';
  return len + 3;
}

}  // namespace proto
}  // namespace snk_mower
}  // namespace esphome
