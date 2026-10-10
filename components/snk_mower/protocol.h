#pragma once

// Wire protocol between the display board (ESP32) and the main MCU U13.
// Frames are `&{json}<crc8>#` at 230400 8N1, CRC-8/Maxim over the JSON bytes.
// Directions and meanings are from the U13 and original ESP32 firmware; see
// FIRMWARE_MAP.md and 20261009_claude_investigation.md (§10 for the boot
// handshake and the watchdogs).

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace snk_mower {
namespace proto {

// ── U13 → ESP ────────────────────────────────────────────────────
static constexpr uint32_t MB_POWER_ON      = 0x20000001;
static constexpr uint32_t MB_INIT_ERROR    = 0x20000002;  // rw_init failed, {"error":bits}, every 2 s
static constexpr uint32_t MB_LINK_UP       = 0x20000004;  // handshake done, or "no ESP frame for 3 s"
static constexpr uint32_t MB_PIN_RESULT    = 0x33000021;
static constexpr uint32_t MB_PIN_RESULT2   = 0x33000022;
static constexpr uint32_t MB_STATUS        = 0x330000A0;
static constexpr uint32_t MB_DEVICE_INFO   = 0x330000A1;
static constexpr uint32_t MB_HW_VERSIONS   = 0x330000A2;
static constexpr uint32_t MB_SCHEDULE      = 0x330000A6;
static constexpr uint32_t MB_RAIN_CFG      = 0x330000A7;
static constexpr uint32_t MB_MULTIZONE     = 0x330000A8;
static constexpr uint32_t MB_SCHEDULE_END  = 0x330000AA;
static constexpr uint32_t MB_MAP_CFG       = 0x330000B0;
static constexpr uint32_t MB_BOOT_INIT     = 0x40000008;  // every 20 ms for up to 1 s, wants ESP_INIT
static constexpr uint32_t MB_BOOT_HEART    = 0x40000009;  // every 100 ms for up to 2.5 s, wants ESP_INFO
static constexpr uint32_t MB_RTC           = 0x40000011;
static constexpr uint32_t MB_LIGHT         = 0x40000020;
static constexpr uint32_t MB_LOCK          = 0x41000002;
static constexpr uint32_t MB_ERROR_NOTIFY  = 0x41000004;
static constexpr uint32_t MB_SHUTDOWN      = 0x41000008;
static constexpr uint32_t MB_PIN_ACK       = 0x41000020;  // {"result":1} after a PIN
static constexpr uint32_t MB_BATTERY       = 0x50000021;

// ── ESP → U13 ────────────────────────────────────────────────────
static constexpr uint32_t ESP_INIT         = 0x40000001;  // {"init":3}, answer to MB_BOOT_INIT
static constexpr uint32_t ESP_BOOT         = 0x40000004;
static constexpr uint32_t ESP_INFO         = 0x40000006;  // answer to MB_BOOT_HEART
static constexpr uint32_t ESP_RAIN         = 0x22000000;
static constexpr uint32_t ESP_KEEPALIVE    = 0x30000005;
static constexpr uint32_t ESP_WIFI         = 0x30000021;
static constexpr uint32_t ESP_BT           = 0x30000022;
static constexpr uint32_t ESP_STATE        = 0x30000028;
static constexpr uint32_t ESP_POLL         = 0x300000A1;
static constexpr uint32_t ESP_GET_SCHEDULE = 0x300000A6;  // bare = query; with fields it overwrites the schedule
static constexpr uint32_t ESP_GET_RAIN_CFG = 0x300000A7;
static constexpr uint32_t ESP_GET_ZONES    = 0x300000A8;
static constexpr uint32_t ESP_PIN          = 0x41000005;  // {"pwd":1234}

// Key and remote commands, decoded by U13 at 0x08063808 into action bits.
static constexpr uint32_t KEY_START_CONFIRM = 0x10000001;  // START then OK
static constexpr uint32_t KEY_HOME_CONFIRM  = 0x10000002;  // HOME then OK
static constexpr uint32_t KEY_SELECT        = 0x10000007;  // START or HOME pressed
static constexpr uint32_t REMOTE_EDGE       = 0x10000015;  // edge trim, only from the station
static constexpr uint32_t REMOTE_START      = 0x10000021;
static constexpr uint32_t REMOTE_HOME       = 0x10000022;
static constexpr uint32_t REMOTE_STOP       = 0x10000023;
// 0x10000004, 0x10000014 and 0x10000024 set action bit 0x10, which powers the
// mower off in every U13 process ("Robot manual power off"). Never send them
// by accident.

// Values the original display firmware 3.02.02 reports in ESP_INFO.
static constexpr int DISPLAY_HW_VERSION = 60400;
static constexpr int DISPLAY_SW_VERSION = 30202;

uint8_t crc8(const uint8_t *data, size_t len);

// Wraps `json` (len bytes) into a frame. Returns the frame length, or 0 if
// `out` is too small.
size_t encode_frame(const char *json, size_t len, uint8_t *out, size_t out_size);

}  // namespace proto
}  // namespace snk_mower
}  // namespace esphome
