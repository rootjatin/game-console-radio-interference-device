#pragma once

/*
  Antinode Hunter Pro - ESP32 dual-nRF24 radio-wave game

  Target wiring from the user:
  OLED SDA/SCL  -> GPIO 21 / 22
  Buttons       -> GPIO 32, 33, 25, 26, 27, wired to GND, INPUT_PULLUP
  Shared SPI    -> SCK 18, MISO 19, MOSI 23
  nRF24 A       -> CE 4,  CSN 5
  nRF24 B       -> CE 13, CSN 14

  Libraries required in Arduino IDE:
  - U8g2
  - RF24 by nRF24 / TMRh20
  - ESP32 board package

  Optional sensors:
  - WiFi scan is enabled by default. It passively estimates 2.4 GHz congestion.
  - BLE scan is disabled by default because it costs memory and can slow gameplay.
*/

// ---------- OLED ----------
// Most 1.3 inch 128x64 I2C OLED modules are SH1106. Many 0.96 inch modules are SSD1306.
#define OLED_DRIVER_SH1106 1
#define OLED_I2C_ADDR      0x3C
#define OLED_W             128
#define OLED_H             64
#define OLED_ROTATION      U8G2_R0

constexpr int PIN_OLED_SDA = 21;
constexpr int PIN_OLED_SCL = 22;

// ---------- nRF24 shared SPI ----------
constexpr int PIN_SPI_SCK  = 18;
constexpr int PIN_SPI_MISO = 19;
constexpr int PIN_SPI_MOSI = 23;

constexpr int PIN_CE_A     = 4;
constexpr int PIN_CSN_A    = 5;
constexpr int PIN_CE_B     = 13;
constexpr int PIN_CSN_B    = 14;

// ---------- Buttons ----------
// Buttons are wired to GND. Code uses ESP32 internal pullups, so pressed == LOW.
constexpr int BTN_UP       = 32;
constexpr int BTN_DOWN     = 33;
constexpr int BTN_LEFT     = 25;
constexpr int BTN_RIGHT    = 26;
constexpr int BTN_FIRE     = 27;

// ---------- Radio / sensing behaviour ----------
#define USE_RADIOS          1   // set 0 to test/display/play without nRF24 modules
#define USE_LOCAL_AIR_LINK  1   // set 0 if the two radios are too close and ACK/ARC readings stay flat
#define ENABLE_WIFI_SCAN    1   // ESP32 WiFi scan contributes to the spectrum pressure model
#define ENABLE_BLE_SCAN     0   // optional; enable only if your ESP32 BLE library is installed and memory is OK

// ---------- Game behaviour ----------
constexpr uint8_t START_CHANNEL = 76;   // nRF24 channel range is 0..125; RF MHz = 2400 + channel
constexpr uint8_t START_LIVES   = 3;
constexpr uint8_t MAX_LEVEL     = 15;
constexpr uint32_t FRAME_MS     = 33;   // about 30 FPS
constexpr uint32_t RADIO_MS     = 45;   // nRF probe / scan cadence
constexpr uint16_t DEBOUNCE_MS  = 28;

// WiFi/BLE scans are slow compared with rendering. Keep intervals long.
constexpr uint32_t WIFI_SCAN_INTERVAL_MS = 16000;
constexpr uint16_t WIFI_MAX_MS_PER_CHAN  = 95;
constexpr uint32_t BLE_SCAN_INTERVAL_MS  = 23000;
constexpr uint8_t  BLE_SCAN_SECONDS      = 1;

// Scoring thresholds. Raise BASE_CAPTURE_THRESHOLD for a harder game.
constexpr float BASE_CAPTURE_THRESHOLD = 0.70f;
constexpr float MAX_CAPTURE_THRESHOLD  = 0.86f;
