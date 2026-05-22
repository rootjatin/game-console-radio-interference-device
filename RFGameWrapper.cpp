#include "RFGameWrapper.h"
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <RF24.h>
#include <Preferences.h>
#include "Config.h"

#if ENABLE_WIFI_SCAN
  #include <WiFi.h>
#endif

#if ENABLE_BLE_SCAN
  #include <BLEDevice.h>
#endif

#if OLED_DRIVER_SH1106
  static U8G2_SH1106_128X64_NONAME_F_HW_I2C rfOled(OLED_ROTATION, U8X8_PIN_NONE);
#else
  static U8G2_SSD1306_128X64_NONAME_F_HW_I2C rfOled(OLED_ROTATION, U8X8_PIN_NONE);
#endif

static RF24 rfRadioA(PIN_CE_A, PIN_CSN_A);
static RF24 rfRadioB(PIN_CE_B, PIN_CSN_B);
static Preferences rfPrefs;

#include "GameLogic.h"

static AntinodeGame rfGame(rfOled, rfRadioA, rfRadioB, rfPrefs);
static bool gameActive = false;
static bool exitRequested = false;
static uint32_t appExitComboStartedAt = 0;

enum RfLaunchTarget : uint8_t {
  RF_LAUNCH_GAMES = 0,
  RF_LAUNCH_WAVEFORGE
};

static bool rawPressed(int pin) {
  return digitalRead(pin) == LOW;
}

static bool universalBackComboPressed() {
  const bool left  = rawPressed(BTN_LEFT);
  const bool right = rawPressed(BTN_RIGHT);
  const bool up    = rawPressed(BTN_UP);
  const bool down  = rawPressed(BTN_DOWN);
  return (left && right) || (up && down);
}

static bool universalBackComboHeld(uint32_t holdMs) {
  const uint32_t now = millis();
  if (universalBackComboPressed()) {
    if (appExitComboStartedAt == 0) appExitComboStartedAt = now;
    return now - appExitComboStartedAt >= holdMs;
  }
  appExitComboStartedAt = 0;
  return false;
}

static void drawRfBackHint(RfLaunchTarget target) {
  rfOled.clearBuffer();
  rfOled.setFont(u8g2_font_7x14B_tf);
  rfOled.drawStr(target == RF_LAUNCH_WAVEFORGE ? 18 : 14, 15, target == RF_LAUNCH_WAVEFORGE ? "WAVEFORGE" : "RF GAMES");
  rfOled.setFont(u8g2_font_6x10_tf);
  rfOled.drawStr(0, 32, "Hold L+R or U+D");
  rfOled.drawStr(0, 44, "to return home");
  rfOled.drawStr(0, 60, "Menu has BACK too");
  rfOled.sendBuffer();
}

static void rfGameStartTarget(RfLaunchTarget target) {
  exitRequested = false;
  appExitComboStartedAt = 0;

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(400000);
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);

  rfOled.setI2CAddress(OLED_I2C_ADDR << 1);
  if (target == RF_LAUNCH_WAVEFORGE) {
    rfGame.beginWaveForgeApp();
  } else {
    rfGame.begin();
  }
  rfGame.clearLauncherRequest();
  drawRfBackHint(target);
  delay(900);
  gameActive = true;
}

void rfGameStart() {
  rfGameStartTarget(RF_LAUNCH_GAMES);
}

void rfGameStartWaveForge() {
  rfGameStartTarget(RF_LAUNCH_WAVEFORGE);
}

void rfGameLoop() {
  if (!gameActive) return;

  if (universalBackComboHeld(900UL)) {
    exitRequested = true;
    return;
  }

  rfGame.update();
}

void rfGameStop() {
  gameActive = false;
  exitRequested = false;
  appExitComboStartedAt = 0;
  rfRadioA.powerDown();
  rfRadioB.powerDown();
  rfPrefs.end();
  delay(100);
}

bool rfGameWantsLauncher() {
  return exitRequested || rfGame.wantsLauncher();
}
