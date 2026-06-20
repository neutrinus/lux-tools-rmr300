#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "lcd_sweep";

// Select pin set via build_flags in platformio.ini:
//   -D PIN_SET_A  → {33, 18, 32}
//   -D PIN_SET_B  → {25, 32, 33}

#if defined(PIN_SET_B)
#define CLK  25
#define MOSI 32
#define CS   33
#else
#define CLK  33
#define MOSI 18
#define CS   32
#endif

static void shift24(uint32_t bits) {
    gpio_set_level(CS, 0);
    esp_rom_delay_us(2);
    for (int i = 23; i >= 0; i--) {
        gpio_set_level(MOSI, (bits >> i) & 1);
        esp_rom_delay_us(1);
        gpio_set_level(CLK, 1);
        esp_rom_delay_us(1);
        gpio_set_level(CLK, 0);
        esp_rom_delay_us(1);
    }
    esp_rom_delay_us(2);
    gpio_set_level(CS, 1);
}

void app_main(void) {
    gpio_reset_pin(CLK);
    gpio_reset_pin(MOSI);
    gpio_reset_pin(CS);
    gpio_set_direction(CLK, GPIO_MODE_OUTPUT);
    gpio_set_direction(MOSI, GPIO_MODE_OUTPUT);
    gpio_set_direction(CS, GPIO_MODE_OUTPUT);
    gpio_set_level(CLK, 0);
    gpio_set_level(MOSI, 0);
    gpio_set_level(CS, 1);

    ESP_LOGI(TAG, "Single-bit sweep on CLK=%d MOSI=%d CS=%d", CLK, MOSI, CS);

    int step = 0;
    while (1) {
        // Phase 1: single-bit sweep — each position held for 2 seconds
        for (int bit = 0; bit < 24; bit++) {
            uint32_t pattern = 1 << bit;
            ESP_LOGI(TAG, "[phase1] bit %2d: 0x%06x", bit, pattern);
            for (int cycle = 0; cycle < 100; cycle++) {
                shift24(pattern);
                esp_rom_delay_us(4000);  // ~4ms per frame = 16ms full cycle
            }
        }

        // Phase 2: all-on test
        ESP_LOGI(TAG, "[phase2] all-on: 0xffffff");
        for (int cycle = 0; cycle < 200; cycle++) {
            shift24(0xffffff);
            esp_rom_delay_us(4000);
        }

        // Phase 3: all-off test
        ESP_LOGI(TAG, "[phase3] all-off: 0x000000");
        for (int cycle = 0; cycle < 200; cycle++) {
            shift24(0x000000);
            esp_rom_delay_us(4000);
        }

        // Phase 4: each byte all-on (identifies byte boundaries on scope)
        ESP_LOGI(TAG, "[phase4] byte0: 0xff0000");
        for (int cycle = 0; cycle < 100; cycle++) {
            shift24(0xff0000);
            esp_rom_delay_us(4000);
        }
        ESP_LOGI(TAG, "[phase4] byte1: 0x00ff00");
        for (int cycle = 0; cycle < 100; cycle++) {
            shift24(0x00ff00);
            esp_rom_delay_us(4000);
        }
        ESP_LOGI(TAG, "[phase4] byte2: 0x0000ff");
        for (int cycle = 0; cycle < 100; cycle++) {
            shift24(0x0000ff);
            esp_rom_delay_us(4000);
        }

        step++;
        ESP_LOGI(TAG, "--- cycle %d complete ---", step);
    }
}
