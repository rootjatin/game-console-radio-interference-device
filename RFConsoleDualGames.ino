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
  U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(OLED_ROTATION, U8X8_PIN_NONE);
#else
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(OLED_ROTATION, U8X8_PIN_NONE);
#endif

RF24 radioA(PIN_CE_A, PIN_CSN_A);
RF24 radioB(PIN_CE_B, PIN_CSN_B);
Preferences prefs;

#include "GameLogic.h"

AntinodeGame game(oled, radioA, radioB, prefs);

void setup() {
  Serial.begin(115200);
  delay(150);

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(400000);
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);

  oled.setI2CAddress(OLED_I2C_ADDR << 1); // U8g2 expects 8-bit shifted I2C address.
  game.begin();
}

void loop() {
  game.update();
}
