#pragma once

#include <Arduino.h>
#include <math.h>
#include <U8g2lib.h>
#include <RF24.h>
#include <Preferences.h>
#include "Config.h"

#ifndef TWO_PI
#define TWO_PI 6.28318530717958647692f
#endif

enum Mission : uint8_t {
  MISS_PEAK = 0,       // constructive interference: make the target bright
  MISS_NULL,           // destructive interference: make the target dark
  MISS_BALANCE,        // equal source strength: needed for deep cancellation
  MISS_QUIET,          // spectrum awareness: move to a low-pressure channel
  MISS_RATE,           // link budget: choose a data rate that fits the channel
  MISS_MULTIPATH,      // reflections: moving ghost path makes the pattern harder
  MISS_COUNT
};

enum EditField : uint8_t {
  EDIT_CHANNEL = 0,
  EDIT_DELAY,
  EDIT_PA_A,
  EDIT_PA_B,
  EDIT_RATE,
  EDIT_COUNT
};

enum PlayState : uint8_t {
  STATE_MENU = 0,
  STATE_RESET_CONFIRM,
  STATE_PLAYING,
  STATE_GAME_OVER
};

enum GameMode : uint8_t {
  GAME_WAVE_RANGERS = 0,
  GAME_RELAY_GAUNTLET,
  GAME_CLASSIC
};

constexpr uint8_t MENU_ITEM_COUNT = 4;
constexpr uint8_t RELAY_PATH_LEN = 4;

struct ProbePacket {
  uint8_t magic;
  uint8_t frameType;
  uint16_t seq;
  uint8_t emitterId;
  uint8_t channel;
  uint8_t paCode;
  uint8_t rateCode;
  uint16_t delayUs;
  uint16_t tick;
};

struct __attribute__((packed)) RelayTokenPacket {
  uint8_t version;
  uint8_t msgType;        // 0x01 = TOKEN
  uint8_t matchId;
  uint8_t teamId;
  uint8_t relayIndex;
  uint16_t tokenSeq;
  uint8_t riskMode;       // 0 fast, 1 balanced, 2 endurance
  uint32_t nonce;
  uint8_t ttl;
  uint8_t flags;
  uint16_t crc16;
};

struct __attribute__((packed)) RelayAckPayload {
  uint8_t version;
  uint8_t msgType;        // 0x81 = ACKP
  uint8_t nextRelayIndex;
  uint8_t awardFlags;
  uint16_t newNonce;
  uint8_t obsArcCnt;
  uint8_t obsFlags;
};

static_assert(sizeof(ProbePacket) <= 32, "RF24 payload must fit in 32 bytes");
static_assert(sizeof(RelayTokenPacket) <= 32, "Relay token must fit nRF24 payload");
static_assert(sizeof(RelayAckPayload) <= 32, "Relay ACK payload must fit nRF24 payload");

class AntinodeGame {
public:
  AntinodeGame(U8G2 &displayRef, RF24 &radioARef, RF24 &radioBRef, Preferences &prefsRef)
    : display(displayRef), rA(radioARef), rB(radioBRef), prefs(prefsRef) {}

  void begin() {
    randomSeed((uint32_t)micros());
    initButtons();
    initDisplay();
    initStorage();
    bootScreen(F("RF Game Console"));
    initRadios();
    initSpectrumSensors();
    state = STATE_MENU;
    menuIndex = 0;
  }

  void update() {
    const uint32_t now = millis();

    readControls();

    if (state == STATE_PLAYING) {
      if (now - lastRadioMs >= RADIO_MS) {
        lastRadioMs = now;
        updateRadioMetrics();
      }

      updateSpectrumSensors(now);
      updateRound(now);
    }

    if (now - lastFrameMs >= FRAME_MS) {
      lastFrameMs = now;
      render();
    }
  }

private:
  U8G2 &display;
  RF24 &rA;
  RF24 &rB;
  Preferences &prefs;

  GameMode gameMode = GAME_WAVE_RANGERS;
  uint8_t menuIndex = 0;
  uint32_t highScoreRangers = 0;
  uint32_t highScoreRelay = 0;
  uint32_t highScoreClassic = 0;

  struct ButtonState {
    uint8_t pin;
    bool stablePressed;
    bool lastRawPressed;
    uint32_t lastRawChangeMs;
  };

  ButtonState buttons[5] = {
    {BTN_UP, false, false, 0},
    {BTN_DOWN, false, false, 0},
    {BTN_LEFT, false, false, 0},
    {BTN_RIGHT, false, false, 0},
    {BTN_FIRE, false, false, 0}
  };

  // Player-tuned RF/game controls.
  uint8_t channel = START_CHANNEL;
  uint8_t paA = 1;          // 0 MIN, 1 LOW, 2 HIGH, 3 MAX
  uint8_t paB = 1;
  uint8_t rateIdx = 0;      // 0 1Mbps, 1 2Mbps, 2 250kbps
  uint16_t virtualDelayUs = 750;
  uint16_t phaseWrapUs = 2000;
  float fringePixels = 17.0f;

  // nRF24 status and measured packet metrics.
  bool radioAOk = false;
  bool radioBOk = false;
  bool pVariantA = false;
  bool pVariantB = false;

  float ackA = 0.55f;
  float ackB = 0.55f;
  float arcA = 0.15f;
  float arcB = 0.15f;
  float nrfNoise = 0.05f;       // local nRF scan pressure near selected channel
  uint8_t nrfBins[126] = {0};
  uint8_t scanCursor = 0;
  uint16_t seq = 0;

  // Coexistence/spectrum pressure model.
  float wifiBins[126] = {0};
  float bleBins[126] = {0};
  uint8_t wifiApCount = 0;
  uint8_t bleAdvCount = 0;
  uint8_t storyStormCenter = 42;  // game-world WiFi/BLE storm so offline play still has danger zones
  uint8_t storyStormWidth = 16;
  uint32_t lastWifiScanMs = 0;
  bool wifiScanRunning = false;
  uint32_t lastBleScanMs = 0;

  // Game state.
  PlayState state = STATE_PLAYING;
  Mission mission = MISS_PEAK;
  EditField editField = EDIT_CHANNEL;
  uint8_t level = 1;
  uint8_t lives = START_LIVES;
  uint16_t roundNo = 0;
  uint32_t score = 0;
  uint32_t highScore = 0; // active mode mirror; saved in per-game keys
  uint8_t streak = 0;
  uint32_t roundStartMs = 0;
  uint32_t roundDurationMs = 12000;
  uint32_t feedbackUntilMs = 0;
  int8_t feedback = 0; // +1 rescue, -1 static hit, +2 wait/lock hint, 0 none
  int targetX = OLED_W / 2;
  int targetDir = 1;
  float targetSpeedFactor = 1.0f;
  float multipathSeed = 0.0f;

  uint32_t lastFrameMs = 0;
  uint32_t lastRadioMs = 0;
  uint32_t lastMoveMs = 0;
  uint32_t roundArmedMs = 0;
  uint32_t lastFireMs = 0;
  uint32_t lastLockUpdateMs = 0;
  float lockMeter = 0.0f;

  // Relay Gauntlet state: nRF24 token relay with nonce, risk modes and retry scoring.
  uint8_t relayRiskMode = 1; // 0 FAST, 1 BAL, 2 END
  uint8_t relayIndex = 0;
  uint16_t relaySeq = 0;
  uint16_t relayNonce16 = 0;
  uint8_t relayCompletedLegs = 0;
  uint8_t relayFailures = 0;
  uint8_t relayLastArc = 0;
  uint8_t relayLastFlags = 0;
  float relayLinkEwma = 0.60f;
  float relayArcEwma = 0.20f;
  uint32_t relayCooldownUntilMs = 0;
  uint32_t relayLastScorePulseMs = 0;

  // ---------- Setup ----------
  void initButtons() {
    for (uint8_t i = 0; i < 5; i++) {
      pinMode(buttons[i].pin, INPUT_PULLUP);
      buttons[i].stablePressed = false;
      buttons[i].lastRawPressed = false;
      buttons[i].lastRawChangeMs = millis();
    }
  }

  void initDisplay() {
    display.begin();
    display.setPowerSave(0);
    display.setContrast(180);
    display.clearBuffer();
    display.sendBuffer();
  }

  void initStorage() {
    prefs.begin("ahpro", false); // keep old namespace so earlier scores can migrate.
    const uint32_t legacy = prefs.getUInt("hi", 0);
    highScoreRangers = prefs.getUInt("hi_wr", 0);
    highScoreRelay = prefs.getUInt("hi_rg", 0);
    highScoreClassic = prefs.getUInt("hi_ah", legacy);
    if (legacy > highScoreClassic) highScoreClassic = legacy;
    highScore = highScoreRangers;
  }

  void bootScreen(const __FlashStringHelper *title) {
    display.clearBuffer();
    display.setFont(u8g2_font_6x10_tf);
    display.setCursor(0, 10);
    display.print(title);
    display.setFont(u8g2_font_5x8_tf);
    display.setCursor(0, 25);
    display.print(F("3 games + real nRF"));
    display.setCursor(0, 38);
#if ENABLE_WIFI_SCAN
    display.print(F("WiFi scan: ON"));
#else
    display.print(F("WiFi scan: OFF"));
#endif
    display.setCursor(0, 51);
#if ENABLE_BLE_SCAN
    display.print(F("BLE scan: ON"));
#else
    display.print(F("BLE scan: optional OFF"));
#endif
    display.sendBuffer();
    delay(950);
  }

  void initRadios() {
#if USE_RADIOS
    radioAOk = rA.begin();
    radioBOk = rB.begin();
    if (radioAOk) pVariantA = rA.isPVariant();
    if (radioBOk) pVariantB = rB.isPVariant();
    applyRadioConfig();
#else
    radioAOk = false;
    radioBOk = false;
#endif
    drawRadioStatus();
    delay(950);
  }

  void drawRadioStatus() {
    display.clearBuffer();
    display.setFont(u8g2_font_6x10_tf);
    display.drawStr(0, 10, "Radio check");
    display.setFont(u8g2_font_5x8_tf);
    display.setCursor(0, 26);
    display.print(F("A: "));
    display.print(radioAOk ? F("OK") : F("not found"));
    if (radioAOk) display.print(pVariantA ? F(" +") : F(" std"));
    display.setCursor(0, 38);
    display.print(F("B: "));
    display.print(radioBOk ? F("OK") : F("not found"));
    if (radioBOk) display.print(pVariantB ? F(" +") : F(" std"));
    display.setCursor(0, 55);
    display.print((radioAOk || radioBOk) ? F("Rescue link ready") : F("Offline training"));
    display.sendBuffer();
  }

  void applyRadioConfig() {
#if USE_RADIOS
    if (radioAOk) configOneRadio(rA, paA, true);
    if (radioBOk) configOneRadio(rB, paB, false);
#endif
  }

  void configOneRadio(RF24 &radio, uint8_t pa, bool isA) {
    const uint8_t addrA[5] = {'A', 'H', 'P', 'A', '1'};
    const uint8_t addrB[5] = {'A', 'H', 'P', 'B', '1'};

    const uint8_t effectiveRate = (gameMode == GAME_RELAY_GAUNTLET) ? relayRateIndex(relayRiskMode) : rateIdx;
    const uint8_t effectivePa = (gameMode == GAME_RELAY_GAUNTLET) ? relayPaIndex(relayRiskMode) : pa;
    const uint8_t retryDelay = (gameMode == GAME_RELAY_GAUNTLET) ? relayRetryDelay(relayRiskMode) : 3;
    const uint8_t retryCount = (gameMode == GAME_RELAY_GAUNTLET) ? relayRetryCount(relayRiskMode) : 6;

    radio.stopListening();
    radio.setChannel(channel);
    if (!radio.setDataRate(rateFromIndex(effectiveRate))) {
      radio.setDataRate(RF24_1MBPS);
    }
    radio.setPALevel(paFromIndex(effectivePa));
    radio.setRetries(retryDelay, retryCount);
    radio.setAutoAck(true);
    radio.setCRCLength(RF24_CRC_16);
    radio.enableDynamicPayloads();
    radio.enableAckPayload();
    radio.setPayloadSize((sizeof(RelayTokenPacket) > sizeof(ProbePacket)) ? sizeof(RelayTokenPacket) : sizeof(ProbePacket));

    if (isA) {
      radio.openWritingPipe(addrB);
      radio.openReadingPipe(1, addrA);
    } else {
      radio.openWritingPipe(addrA);
      radio.openReadingPipe(1, addrB);
    }
    radio.startListening();
  }

  void initSpectrumSensors() {
#if ENABLE_WIFI_SCAN
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    startWifiScan();
#endif
#if ENABLE_BLE_SCAN
    BLEDevice::init("");
#endif
  }

  // ---------- Controls ----------
  bool pressedEdge(uint8_t idx) {
    ButtonState &b = buttons[idx];
    const uint32_t now = millis();
    const bool rawPressed = (digitalRead(b.pin) == LOW);

    if (rawPressed != b.lastRawPressed) {
      b.lastRawPressed = rawPressed;
      b.lastRawChangeMs = now;
    }

    if ((now - b.lastRawChangeMs) >= DEBOUNCE_MS && rawPressed != b.stablePressed) {
      b.stablePressed = rawPressed;
      if (b.stablePressed) return true;
    }
    return false;
  }

  void readControls() {
    const bool up = pressedEdge(0);
    const bool down = pressedEdge(1);
    const bool left = pressedEdge(2);
    const bool right = pressedEdge(3);
    const bool fire = pressedEdge(4);

    if (state == STATE_MENU) {
      if (up) menuIndex = (menuIndex == 0) ? (MENU_ITEM_COUNT - 1) : (uint8_t)(menuIndex - 1);
      if (down) menuIndex = (uint8_t)((menuIndex + 1) % MENU_ITEM_COUNT);
      if (fire) selectMenuItem();
      return;
    }

    if (state == STATE_RESET_CONFIRM) {
      if (left || right || up || down) state = STATE_MENU;
      if (fire) {
        resetHighScores();
        state = STATE_MENU;
        feedback = +2;
        feedbackUntilMs = millis() + 900UL;
      }
      return;
    }

    if (state == STATE_GAME_OVER) {
      if (fire) resetGame();
      if (up || down || left || right) state = STATE_MENU;
      return;
    }

    if (up) {
      if (gameMode == GAME_RELAY_GAUNTLET) editField = (editField == EDIT_CHANNEL) ? EDIT_RATE : EDIT_CHANNEL;
      else editField = (editField == EDIT_CHANNEL) ? EDIT_RATE : (EditField)((uint8_t)editField - 1);
    }
    if (down) {
      if (gameMode == GAME_RELAY_GAUNTLET) editField = (editField == EDIT_CHANNEL) ? EDIT_RATE : EDIT_CHANNEL;
      else editField = (EditField)(((uint8_t)editField + 1) % EDIT_COUNT);
    }
    if (left) adjustSelected(-1);
    if (right) adjustSelected(+1);
    if (fire) capture();
  }

  void selectMenuItem() {
    if (menuIndex == 0) {
      gameMode = GAME_WAVE_RANGERS;
      resetGame();
    } else if (menuIndex == 1) {
      gameMode = GAME_RELAY_GAUNTLET;
      resetGame();
    } else if (menuIndex == 2) {
      gameMode = GAME_CLASSIC;
      resetGame();
    } else {
      state = STATE_RESET_CONFIRM;
    }
  }

  uint32_t activeHighScore() const {
    if (gameMode == GAME_WAVE_RANGERS) return highScoreRangers;
    if (gameMode == GAME_RELAY_GAUNTLET) return highScoreRelay;
    return highScoreClassic;
  }

  void setActiveHighScore(uint32_t v) {
    if (gameMode == GAME_WAVE_RANGERS) {
      highScoreRangers = v;
      prefs.putUInt("hi_wr", highScoreRangers);
    } else if (gameMode == GAME_RELAY_GAUNTLET) {
      highScoreRelay = v;
      prefs.putUInt("hi_rg", highScoreRelay);
    } else {
      highScoreClassic = v;
      prefs.putUInt("hi_ah", highScoreClassic);
    }
  }

  void resetHighScores() {
    highScoreRangers = 0;
    highScoreRelay = 0;
    highScoreClassic = 0;
    highScore = 0;
    prefs.putUInt("hi_wr", 0);
    prefs.putUInt("hi_rg", 0);
    prefs.putUInt("hi_ah", 0);
    prefs.putUInt("hi", 0);
  }

  void adjustSelected(int delta) {
    if (gameMode == GAME_RELAY_GAUNTLET) {
      if (editField == EDIT_CHANNEL) {
        channel = (uint8_t)clampi((int)channel + delta, 0, 125);
      } else {
        int nextRisk = (int)relayRiskMode + delta;
        if (nextRisk < 0) nextRisk = 2;
        if (nextRisk > 2) nextRisk = 0;
        relayRiskMode = (uint8_t)nextRisk;
        applyRelayRiskToControls();
      }
      applyRadioConfig();
      return;
    }

    switch (editField) {
      case EDIT_CHANNEL:
        channel = (uint8_t)clampi((int)channel + delta, 0, 125);
        applyRadioConfig();
        break;
      case EDIT_DELAY:
        virtualDelayUs = (uint16_t)clampi((int)virtualDelayUs + delta * 125, 0, 4000);
        break;
      case EDIT_PA_A:
        paA = (uint8_t)clampi((int)paA + delta, 0, 3);
        applyRadioConfig();
        break;
      case EDIT_PA_B:
        paB = (uint8_t)clampi((int)paB + delta, 0, 3);
        applyRadioConfig();
        break;
      case EDIT_RATE: {
        int next = (int)rateIdx + delta;
        if (next < 0) next = 2;
        if (next > 2) next = 0;
        rateIdx = (uint8_t)next;
        applyRadioConfig();
        break;
      }
      default:
        break;
    }
  }

  // ---------- Radio metrics ----------
  void updateRadioMetrics() {
#if USE_RADIOS
    if (radioAOk && radioBOk && USE_LOCAL_AIR_LINK) {
      ProbePacket pA = makePacket(0);
      ProbePacket pB = makePacket(1);
      sendOneWay(rA, rB, pA, ackA, arcA);
      delayMicroseconds(80);
      sendOneWay(rB, rA, pB, ackB, arcB);
    } else {
      syntheticLinkMetrics();
    }

    scanNoiseSlice();
#else
    syntheticLinkMetrics();
#endif
  }

  ProbePacket makePacket(uint8_t id) {
    ProbePacket p{};
    p.magic = 0xA5;
    p.frameType = 1;
    p.seq = seq++;
    p.emitterId = id;
    p.channel = channel;
    p.paCode = (id == 0) ? paA : paB;
    p.rateCode = rateIdx;
    p.delayUs = virtualDelayUs;
    p.tick = (uint16_t)(millis() & 0xFFFF);
    return p;
  }

  bool sendOneWay(RF24 &tx, RF24 &rx, const ProbePacket &packet, float &ackEwma, float &arcEwma) {
    rx.startListening();
    tx.stopListening();
    const bool ok = tx.write(&packet, sizeof(packet));
    const uint8_t arc = tx.getARC();
    tx.startListening();
    drain(rx);

    ackEwma = 0.86f * ackEwma + 0.14f * (ok ? 1.0f : 0.0f);
    arcEwma = 0.86f * arcEwma + 0.14f * ((float)arc / 15.0f);
    return ok;
  }

  void drain(RF24 &radio) {
    uint8_t junk[32];
    while (radio.available()) {
      uint8_t len = radio.getDynamicPayloadSize();
      if (len == 0 || len > sizeof(junk)) len = sizeof(junk);
      radio.read(junk, len);
    }
  }

  void scanNoiseSlice() {
    RF24 *scanner = nullptr;
    bool plus = false;
    if (radioAOk) { scanner = &rA; plus = pVariantA; }
    else if (radioBOk) { scanner = &rB; plus = pVariantB; }
    else return;

    scanner->stopListening();
    for (uint8_t i = 0; i < 4; i++) {
      const uint8_t ch = scanCursor++ % 126;
      scanner->setChannel(ch);
      scanner->startListening();
      delayMicroseconds(170);
      const bool busy = plus ? scanner->testRPD() : scanner->testCarrier();
      scanner->stopListening();

      if (busy && nrfBins[ch] < 15) nrfBins[ch]++;
      if (!busy && nrfBins[ch] > 0) nrfBins[ch]--;
    }
    scanner->setChannel(channel);
    scanner->startListening();

    nrfNoise = 0.88f * nrfNoise + 0.12f * localNrfPressure(channel);
  }

  float localNrfPressure(uint8_t ch) const {
    float sum = 0.0f;
    uint8_t count = 0;
    for (int8_t d = -2; d <= 2; d++) {
      const int c = clampi((int)ch + d, 0, 125);
      sum += (float)nrfBins[c] / 15.0f;
      count++;
    }
    return (count > 0) ? (sum / (float)count) : 0.0f;
  }

  void syntheticLinkMetrics() {
    const float p = pressureAround(channel);
    const float phase = TWO_PI * (float)(virtualDelayUs % phaseWrapUs) / (float)phaseWrapUs;
    const float wiggle = 0.5f + 0.5f * cosf(phase);
    const float paBalance = 1.0f - fabsf(paScalar(paA) - paScalar(paB));

    ackA = 0.96f * ackA + 0.04f * clampf(0.82f + 0.16f * paScalar(paA) - 0.22f * p, 0.0f, 1.0f);
    ackB = 0.96f * ackB + 0.04f * clampf(0.70f + 0.22f * paScalar(paB) * wiggle - 0.24f * p, 0.0f, 1.0f);
    arcA = 0.96f * arcA + 0.04f * clampf(0.12f + 0.35f * p + 0.12f * (1.0f - paScalar(paA)), 0.0f, 1.0f);
    arcB = 0.96f * arcB + 0.04f * clampf(0.14f + 0.35f * p + 0.10f * (1.0f - paBalance), 0.0f, 1.0f);
    nrfNoise = 0.995f * nrfNoise + 0.005f * (0.04f + 0.02f * sinf((float)millis() / 1800.0f));
  }

  // ---------- WiFi/BLE spectrum sensing ----------
  void updateSpectrumSensors(uint32_t now) {
#if ENABLE_WIFI_SCAN
    updateWifiScan(now);
#endif
#if ENABLE_BLE_SCAN
    if (now - lastBleScanMs >= BLE_SCAN_INTERVAL_MS) {
      lastBleScanMs = now;
      updateBleScanBlocking();
    }
#endif
  }

#if ENABLE_WIFI_SCAN
  void startWifiScan() {
    if (wifiScanRunning) return;
    // async=true, show_hidden=true, passive=true. Channel 0 means all 2.4 GHz channels.
    const int16_t rc = WiFi.scanNetworks(true, true, true, WIFI_MAX_MS_PER_CHAN, 0);
    wifiScanRunning = (rc == WIFI_SCAN_RUNNING || rc == WIFI_SCAN_FAILED || rc >= 0);
    lastWifiScanMs = millis();
  }

  void updateWifiScan(uint32_t now) {
    if (!wifiScanRunning) {
      if (now - lastWifiScanMs >= WIFI_SCAN_INTERVAL_MS) startWifiScan();
      return;
    }

    const int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;

    if (n >= 0) {
      processWifiResults(n);
      WiFi.scanDelete();
    }

    wifiScanRunning = false;
    lastWifiScanMs = now;
  }

  void processWifiResults(int16_t n) {
    wifiApCount = (uint8_t)clampi(n, 0, 99);

    for (uint8_t i = 0; i < 126; i++) wifiBins[i] *= 0.70f;

    for (int16_t i = 0; i < n; i++) {
      const int32_t rssi = WiFi.RSSI(i);
      const int32_t wifiCh = WiFi.channel(i);
      const int center = wifiChannelToNrfChannel(wifiCh);
      if (center < 0) continue;

      const float strength = clampf(((float)rssi + 92.0f) / 52.0f, 0.0f, 1.0f);
      for (int d = -12; d <= 12; d++) {
        const int b = center + d;
        if (b < 0 || b > 125) continue;
        const float w = 1.0f - ((float)abs(d) / 13.0f);
        wifiBins[b] = clampf(wifiBins[b] + strength * w * 0.55f, 0.0f, 1.0f);
      }
    }
  }
#endif

#if ENABLE_BLE_SCAN
  void updateBleScanBlocking() {
    for (uint8_t i = 0; i < 126; i++) bleBins[i] *= 0.72f;

    BLEScan *scan = BLEDevice::getScan();
    scan->setActiveScan(false);
    BLEScanResults *results = scan->start(BLE_SCAN_SECONDS, false);
    bleAdvCount = results ? (uint8_t)clampi(results->getCount(), 0, 99) : 0;

    // BLE advertising channels are 2402, 2426, 2480 MHz -> nRF channels 2, 26, 80.
    // We cannot know exact BLE data-channel occupancy here, so we add a conservative haze around adv channels.
    addBleHaze(2, 0.25f);
    addBleHaze(26, 0.20f);
    addBleHaze(80, 0.20f);
    scan->clearResults();
  }

  void addBleHaze(int center, float amount) {
    for (int d = -3; d <= 3; d++) {
      const int b = center + d;
      if (b < 0 || b > 125) continue;
      const float w = 1.0f - ((float)abs(d) / 4.0f);
      bleBins[b] = clampf(bleBins[b] + amount * w, 0.0f, 1.0f);
    }
  }
#endif

  static int wifiChannelToNrfChannel(int wifiCh) {
    if (wifiCh >= 1 && wifiCh <= 13) return 7 + 5 * wifiCh; // ch1=2412 MHz -> nRF ch12.
    if (wifiCh == 14) return 84;                             // 2484 MHz.
    return -1;
  }

  float pressureAt(uint8_t ch) const {
    const uint8_t c = (uint8_t)clampi(ch, 0, 125);
    const float n = (float)nrfBins[c] / 15.0f;
    const float w = wifiBins[c];
    const float b = bleBins[c];
    const float s = storyStormAt(c);
    return clampf(0.36f * n + 0.24f * w + 0.08f * b + 0.42f * s, 0.0f, 1.0f);
  }

  float storyStormAt(uint8_t ch) const {
    const float d = fabsf((float)((int)ch - (int)storyStormCenter));
    const float width = (float)storyStormWidth;
    if (d >= width) return 0.0f;
    const float t = 1.0f - d / width;
    return t * t;
  }

  float pressureAround(uint8_t ch) const {
    float sum = 0.0f;
    float weight = 0.0f;
    for (int8_t d = -3; d <= 3; d++) {
      const int c = clampi((int)ch + d, 0, 125);
      const float w = 1.0f + (3.0f - fabsf((float)d)) * 0.25f;
      sum += pressureAt((uint8_t)c) * w;
      weight += w;
    }
    return (weight > 0.0f) ? clampf(sum / weight, 0.0f, 1.0f) : 0.0f;
  }

  // ---------- Game model ----------
  void resetGame() {
    state = STATE_PLAYING;
    highScore = activeHighScore();
    level = 1;
    lives = START_LIVES;
    score = 0;
    streak = 0;
    roundNo = 0;
    feedback = 0;
    lockMeter = 0.0f;
    lastFireMs = 0;
    if (gameMode == GAME_RELAY_GAUNTLET) initRelayGame();
    else nextRound(false);
  }

  void nextRound(bool successLevelUp) {
    roundNo++;
    if (successLevelUp && level < MAX_LEVEL && (roundNo % 2 == 0)) level++;

    mission = chooseMissionForLevel();
    prepareControlsForMission();
    targetX = random(14, OLED_W - 14);
    targetDir = random(0, 2) ? 1 : -1;
    targetSpeedFactor = 1.0f + 0.08f * (float)(level - 1);
    multipathSeed = (float)random(0, 6283) / 1000.0f;

    roundDurationMs = 12800UL - (uint32_t)(level - 1) * 650UL;
    if (mission == MISS_QUIET) roundDurationMs += 1600UL;
    if (mission == MISS_RATE) roundDurationMs += 900UL;
    if (roundDurationMs < 4800UL) roundDurationMs = 4800UL;

    fringePixels = 18.5f - (float)level * 0.72f;
    if (mission == MISS_MULTIPATH) fringePixels *= 0.92f;
    if (fringePixels < 7.2f) fringePixels = 7.2f;

    const uint32_t now = millis();
    roundStartMs = now;
    roundArmedMs = now + ROUND_ARM_MS + (uint32_t)clampi((int)level * 18, 0, 260);
    lastLockUpdateMs = now;
    lockMeter = 0.0f;
    applyRadioConfig();
  }

  void prepareControlsForMission() {
    // A game-world 2.4 GHz storm gives QUIET/MODE rounds a real target even in a clean room.
    storyStormCenter = (uint8_t)random(10, 116);
    storyStormWidth = (uint8_t)random(10, 21);

    // Each rescue changes at least one knob so holding FIRE cannot solve a chain of rounds.
    switch (mission) {
      case MISS_PEAK:
      case MISS_NULL:
      case MISS_MULTIPATH:
        virtualDelayUs = (uint16_t)((virtualDelayUs + (uint16_t)random(375, 1625)) % phaseWrapUs);
        editField = EDIT_DELAY;
        break;
      case MISS_BALANCE:
        paA = (uint8_t)random(0, 4);
        paB = (uint8_t)random(0, 4);
        if (paA == paB) paB = (uint8_t)((paB + 1) % 4);
        editField = EDIT_PA_A;
        break;
      case MISS_QUIET:
        channel = (uint8_t)clampi((int)storyStormCenter + random(-8, 9), 0, 125);
        editField = EDIT_CHANNEL;
        break;
      case MISS_RATE:
        channel = (uint8_t)clampi((int)storyStormCenter + random(-6, 7), 0, 125);
        rateIdx = (uint8_t)random(0, 3);
        editField = EDIT_RATE;
        break;
      default:
        break;
    }
  }

  Mission chooseMissionForLevel() const {
    // Unlocks: L1 PEAK/NULL, L3 BALANCE, L4 QUIET, L5 RATE, L6 MULTIPATH.
    uint8_t pool = 2;
    if (level >= 3) pool = 3;
    if (level >= 4) pool = 4;
    if (level >= 5) pool = 5;
    if (level >= 6) pool = 6;

    const uint8_t r = random(0, pool);
    switch (r) {
      case 0: return MISS_PEAK;
      case 1: return MISS_NULL;
      case 2: return MISS_BALANCE;
      case 3: return MISS_QUIET;
      case 4: return MISS_RATE;
      default: return MISS_MULTIPATH;
    }
  }

  void updateRound(uint32_t now) {
    if (gameMode == GAME_RELAY_GAUNTLET) {
      updateRelayRound(now);
      return;
    }

    if (now - roundStartMs >= roundDurationMs) {
      miss();
      return;
    }

    const uint16_t moveDelay = (uint16_t)clampi((int)(230.0f / targetSpeedFactor), 52, 230);
    const bool targetMoves = (level >= 2 && mission != MISS_QUIET && mission != MISS_RATE);
    if (targetMoves && now - lastMoveMs >= moveDelay) {
      lastMoveMs = now;
      targetX += targetDir * (1 + level / 5);
      if (targetX < 10 || targetX > OLED_W - 11) {
        targetDir = -targetDir;
        targetX = clampi(targetX, 10, OLED_W - 11);
      }
    }

    updateLockMeter(now);
    if (feedback != 0 && now > feedbackUntilMs) feedback = 0;
  }

  void updateLockMeter(uint32_t now) {
    const uint32_t rawDt = now - lastLockUpdateMs;
    lastLockUpdateMs = now;
    const float dt = clampf((float)rawDt / 1000.0f, 0.0f, 0.14f);
    if (dt <= 0.0f) return;

    if (now < roundArmedMs) {
      lockMeter = clampf(lockMeter - dt * 1.25f, 0.0f, 1.0f);
      return;
    }

    const float m = (gameMode == GAME_RELAY_GAUNTLET) ? relayMerit() : missionMerit();
    const float threshold = (gameMode == GAME_RELAY_GAUNTLET) ? relayCaptureThreshold() : captureThreshold();
    if (m >= threshold) {
      lockMeter += dt * (1.65f + 0.045f * (float)level);
    } else if (m >= threshold - 0.07f) {
      lockMeter += dt * 0.36f;
    } else {
      lockMeter -= dt * (1.15f + 0.55f * pressureAround(channel));
    }
    lockMeter = clampf(lockMeter, 0.0f, 1.0f);
  }

  float sourceAmpA() const { return clampf(0.45f * paScalar(paA) + 0.55f * ackA, 0.0f, 1.0f); }
  float sourceAmpB() const { return clampf(0.45f * paScalar(paB) + 0.55f * ackB, 0.0f, 1.0f); }
  float sourceBalance() const { return clampf(1.0f - fabsf(sourceAmpA() - sourceAmpB()), 0.0f, 1.0f); }
  float stability() const { return clampf(1.0f - 0.50f * (arcA + arcB), 0.0f, 1.0f); }

  float fieldAt(float x) const {
    const float srcA = 12.0f;
    const float srcB = (float)OLED_W - 13.0f;
    const float phase0 = TWO_PI * (float)(virtualDelayUs % phaseWrapUs) / (float)phaseWrapUs;
    const float pathDiff = fabsf(x - srcA) - fabsf(x - srcB);
    const float phase = (pathDiff / fringePixels) * TWO_PI + phase0;

    const float balance = 1.0f - 0.65f * fabsf(sourceAmpA() - sourceAmpB());
    const float pressure = pressureAround(channel);
    const float contrast = clampf(balance * stability() * (1.0f - 0.78f * pressure), 0.04f, 1.0f);
    const float fringe = 0.5f + 0.5f * cosf(phase);
    const float floor = 0.07f + 0.12f * ((ackA + ackB) * 0.5f);
    float v = floor + 0.83f * contrast * fringe;

    if (mission == MISS_MULTIPATH) {
      // A secondary reflected path. It is rendered, not claimed as direct phase measurement.
      const float t = (float)(millis() % 20000UL) / 20000.0f;
      const float drift = TWO_PI * t * (0.35f + 0.04f * (float)level);
      const float ghostPhase = phase * 0.73f + multipathSeed + drift + 0.06f * (float)channel;
      const float ghost = 0.5f + 0.5f * cosf(ghostPhase);
      const float reflection = clampf(0.18f + 0.22f * pressure + 0.03f * level, 0.18f, 0.42f);
      v = (1.0f - reflection) * v + reflection * (floor + 0.83f * contrast * ghost);
    }

    return clampf(v, 0.0f, 1.0f);
  }

  float missionMerit() const {
    const float p = pressureAround(channel);
    const float stable = stability();

    if (mission == MISS_QUIET) {
      // Find a channel where combined nRF/WiFi/BLE pressure is low.
      const float quiet = 1.0f - p;
      const float link = 0.5f * (ackA + ackB);
      return clampf(0.78f * quiet + 0.12f * stable + 0.10f * link, 0.0f, 1.0f);
    }

    if (mission == MISS_BALANCE) {
      // Equal amplitudes are required for deep destructive interference.
      const float bal = sourceBalance();
      const float contrastHealth = clampf(1.0f - p, 0.0f, 1.0f);
      return clampf(0.62f * bal + 0.20f * stable + 0.18f * contrastHealth, 0.0f, 1.0f);
    }

    if (mission == MISS_RATE) {
      // Scientific rule of thumb for this game: noisy channels favour 250kbps,
      // clean channels reward 2Mbps, middle conditions are fine at 1Mbps.
      const float ideal = rateFitnessForPressure(p);
      const float link = 0.5f * (ackA + ackB);
      return clampf(0.52f * ideal + 0.28f * link + 0.20f * stable, 0.0f, 1.0f);
    }

    const float v = fieldAt((float)targetX);
    const float hit = (mission == MISS_NULL) ? (1.0f - v) : v;
    const float quiet = 1.0f - p;
    const float hitWeight = (mission == MISS_MULTIPATH) ? 0.80f : 0.76f;
    return clampf(hitWeight * hit + 0.14f * stable + (1.0f - hitWeight - 0.14f) * quiet, 0.0f, 1.0f);
  }

  float rateFitnessForPressure(float p) const {
    if (p > 0.58f) {
      if (rateIdx == 2) return 1.0f; // 250kbps
      if (rateIdx == 0) return 0.62f; // 1Mbps
      return 0.30f;                   // 2Mbps
    }
    if (p < 0.22f) {
      if (rateIdx == 1) return 1.0f; // 2Mbps
      if (rateIdx == 0) return 0.78f;
      return 0.58f;
    }
    if (rateIdx == 0) return 1.0f;   // 1Mbps middle ground
    if (rateIdx == 2) return 0.82f;
    return 0.72f;
  }

  float captureThreshold() const {
    float threshold = BASE_CAPTURE_THRESHOLD + 0.013f * (float)(level - 1);
    if (mission == MISS_MULTIPATH) threshold += 0.030f;
    if (threshold > MAX_CAPTURE_THRESHOLD) threshold = MAX_CAPTURE_THRESHOLD;
    return threshold;
  }

  // ---------- Relay Gauntlet game model ----------
  void initRelayGame() {
    roundNo = 1;
    relayRiskMode = 1;
    relayIndex = 0;
    relaySeq = 0;
    relayNonce16 = newNonce16();
    relayCompletedLegs = 0;
    relayFailures = 0;
    relayLastArc = 0;
    relayLastFlags = 0;
    relayLinkEwma = 0.60f;
    relayArcEwma = 0.20f;
    relayCooldownUntilMs = 0;
    relayLastScorePulseMs = millis();
    editField = EDIT_RATE;
    applyRelayRiskToControls();

    storyStormCenter = (uint8_t)random(12, 113);
    storyStormWidth = (uint8_t)random(12, 22);

    const uint32_t now = millis();
    roundStartMs = now;
    roundDurationMs = RELAY_MATCH_MS;
    roundArmedMs = now + RELAY_ARM_MS;
    lastLockUpdateMs = now;
    lockMeter = 0.0f;
    applyRadioConfig();
  }

  void updateRelayRound(uint32_t now) {
    if (now - roundStartMs >= roundDurationMs) {
      endGame();
      return;
    }

    relayLinkEwma = 0.94f * relayLinkEwma + 0.06f * (0.5f * (ackA + ackB));
    relayArcEwma = 0.94f * relayArcEwma + 0.06f * (0.5f * (arcA + arcB));

    // Slowly move the story storm so channel discipline matters across the match.
    if (((now - roundStartMs) / 4500UL) != ((relayLastScorePulseMs - roundStartMs) / 4500UL)) {
      storyStormCenter = (uint8_t)clampi((int)storyStormCenter + random(-5, 6), 8, 118);
    }

    updateLockMeter(now);
    if (feedback != 0 && now > feedbackUntilMs) feedback = 0;
    if (now > relayCooldownUntilMs && feedback == -1) feedback = 0;
    relayLastScorePulseMs = now;
  }

  float relayMerit() const {
    const float link = relayLinkEwma;
    const float retryHealth = clampf(1.0f - relayArcEwma, 0.0f, 1.0f);
    const float quiet = clampf(1.0f - pressureAround(channel), 0.0f, 1.0f);
    const float riskDiscipline = (relayRiskMode == 0) ? 0.88f : ((relayRiskMode == 1) ? 0.96f : 1.0f);
    return clampf(riskDiscipline * (0.42f * link + 0.38f * retryHealth + 0.20f * quiet), 0.0f, 1.0f);
  }

  float relayCaptureThreshold() const {
    float threshold = 0.69f + 0.012f * (float)(level - 1);
    if (relayRiskMode == 0) threshold += 0.05f;      // fast mode must be cleaner
    else if (relayRiskMode == 2) threshold -= 0.035f; // endurance is allowed to be safer
    return clampf(threshold, 0.60f, 0.88f);
  }

  void attemptRelayLeg() {
    const uint32_t now = millis();
    if (now - lastFireMs < FIRE_COOLDOWN_MS || now < relayCooldownUntilMs) return;
    lastFireMs = now;

    if (now < roundArmedMs || lockMeter < REQUIRED_LOCK || relayMerit() < relayCaptureThreshold()) {
      feedback = +2;
      feedbackUntilMs = now + 500UL;
      if (lockMeter < 0.35f) relayFail(false);
      return;
    }

#if USE_RADIOS
    bool ok = false;
    RelayAckPayload ack{};
    uint8_t arc = 15;
    if (radioAOk && radioBOk && USE_LOCAL_AIR_LINK) {
      RelayTokenPacket token = makeRelayToken();
      RelayAckPayload expectedAck = makeRelayAck(token);
      if ((relayIndex & 1) == 0) ok = sendRelayToken(rA, rB, token, expectedAck, ack, arc);
      else ok = sendRelayToken(rB, rA, token, expectedAck, ack, arc);
      ok = ok && validateRelayAck(ack, expectedAck);
    } else {
      RelayTokenPacket token = makeRelayToken();
      ok = syntheticRelayAttempt(arc);
      ack = makeRelayAck(token);
    }
#else
    uint8_t arc = 0;
    RelayTokenPacket token = makeRelayToken();
    bool ok = syntheticRelayAttempt(arc);
    RelayAckPayload ack = makeRelayAck(token);
#endif

    relayLastArc = arc;
    relayLastFlags = ack.obsFlags;
    relayArcEwma = 0.82f * relayArcEwma + 0.18f * ((float)arc / 15.0f);
    relayLinkEwma = 0.82f * relayLinkEwma + 0.18f * (ok ? 1.0f : 0.0f);

    if (ok) {
      relaySuccess(ack);
    } else {
      relayFail(true);
    }
  }

  RelayTokenPacket makeRelayToken() {
    RelayTokenPacket p{};
    p.version = 1;
    p.msgType = 0x01;
    p.matchId = (uint8_t)(roundNo & 0xFF);
    p.teamId = 1;
    p.relayIndex = relayIndex;
    p.tokenSeq = relaySeq++;
    p.riskMode = relayRiskMode;
    p.nonce = ((uint32_t)relayNonce16 << 16) | (uint32_t)(relaySeq ^ 0x5A5A);
    p.ttl = RELAY_PATH_LEN - relayIndex;
    p.flags = 0;
    p.crc16 = 0;
    p.crc16 = crc16((const uint8_t *)&p, sizeof(RelayTokenPacket) - sizeof(uint16_t));
    return p;
  }

  RelayAckPayload makeRelayAck(const RelayTokenPacket &token) {
    RelayAckPayload ack{};
    ack.version = 1;
    ack.msgType = 0x81;
    ack.nextRelayIndex = (uint8_t)((token.relayIndex + 1) % RELAY_PATH_LEN);
    ack.awardFlags = relayAwardFlags();
    ack.newNonce = newNonce16();
    ack.obsArcCnt = relayLastArc;
    ack.obsFlags = 0;
    if (pressureAround(channel) > 0.58f) ack.obsFlags |= 0x01;
    if (relayRiskMode == 0) ack.obsFlags |= 0x02;
    return ack;
  }

  bool sendRelayToken(RF24 &tx, RF24 &rx, const RelayTokenPacket &token, const RelayAckPayload &expectedAck, RelayAckPayload &ack, uint8_t &arc) {
    rx.startListening();
    tx.stopListening();
    rx.writeAckPayload(1, &expectedAck, sizeof(expectedAck));

    const bool ok = tx.write(&token, sizeof(token));
    arc = tx.getARC();

    if (ok && (tx.isAckPayloadAvailable() || tx.available())) {
      const uint8_t len = tx.getDynamicPayloadSize();
      if (len == sizeof(RelayAckPayload)) tx.read(&ack, sizeof(ack));
      else drain(tx);
    }

    tx.startListening();
    drain(rx);
    return ok;
  }

  bool validateRelayAck(const RelayAckPayload &ack, const RelayAckPayload &expected) const {
    return ack.version == 1 &&
           ack.msgType == 0x81 &&
           ack.nextRelayIndex == expected.nextRelayIndex &&
           ack.newNonce == expected.newNonce;
  }

  bool syntheticRelayAttempt(uint8_t &arc) {
    const float m = relayMerit();
    const float riskPenalty = (relayRiskMode == 0) ? 0.14f : ((relayRiskMode == 1) ? 0.07f : 0.0f);
    const float chance = clampf(m - riskPenalty + 0.08f, 0.05f, 0.98f);
    const uint16_t roll = (uint16_t)random(0, 1000);
    arc = (uint8_t)clampi((int)roundf((1.0f - chance) * 9.0f + pressureAround(channel) * 4.0f), 0, 15);
    return roll < (uint16_t)(chance * 1000.0f);
  }

  void relaySuccess(const RelayAckPayload &ack) {
    const uint32_t now = millis();
    relayNonce16 = ack.newNonce;
    relayIndex = ack.nextRelayIndex;
    relayCompletedLegs++;
    streak = (uint8_t)clampi((int)streak + 1, 0, 9);

    uint32_t points = 5;
    if (relayRiskMode == 0) points += 2;
    if (relayPaIndex(relayRiskMode) <= 1) points += 1;
    if (relayLastArc <= 1) points += 1;
    points += (uint32_t)(lockMeter * 4.0f) + (uint32_t)streak;

    if (relayIndex == 0) {
      points += 10;
      if (level < MAX_LEVEL) level++;
      storyStormCenter = (uint8_t)random(10, 116);
      storyStormWidth = (uint8_t)random(10, 21);
    }

    score += points;
    feedback = +1;
    feedbackUntilMs = now + 520UL;
    relayCooldownUntilMs = now + 180UL;
    lockMeter = clampf(lockMeter - 0.34f, 0.0f, 1.0f);
  }

  void relayFail(bool penalizeLife) {
    const uint32_t now = millis();
    streak = 0;
    relayFailures++;
    relayLastFlags |= 0x80;
    if (score >= 5) score -= 5;
    else score = 0;

    if (penalizeLife && lives > 0) lives--;
    feedback = -1;
    feedbackUntilMs = now + 700UL;
    relayCooldownUntilMs = now + 500UL;
    lockMeter = clampf(lockMeter - 0.45f, 0.0f, 1.0f);

    if (lives == 0) endGame();
  }

  void applyRelayRiskToControls() {
    rateIdx = relayRateIndex(relayRiskMode);
    paA = relayPaIndex(relayRiskMode);
    paB = relayPaIndex(relayRiskMode);
  }

  uint8_t relayAwardFlags() const {
    uint8_t flags = 0;
    if (relayRiskMode == 0) flags |= 0x01;
    if (relayPaIndex(relayRiskMode) <= 1) flags |= 0x02;
    if (relayLastArc <= 1) flags |= 0x04;
    return flags;
  }

  static uint8_t relayRateIndex(uint8_t risk) {
    if (risk == 0) return 1; // 2 Mbps fast
    if (risk == 2) return 2; // 250 kbps endurance
    return 0;                // 1 Mbps balanced
  }

  static uint8_t relayPaIndex(uint8_t risk) {
    if (risk == 0) return 1; // RF24_PA_LOW, finesse bonus
    if (risk == 2) return 3; // RF24_PA_MAX, rescue mode
    return 2;                // RF24_PA_HIGH
  }

  static uint8_t relayRetryDelay(uint8_t risk) {
    if (risk == 0) return 0; // 250 us
    if (risk == 2) return 3; // 1000 us
    return 1;                // 500 us
  }

  static uint8_t relayRetryCount(uint8_t risk) {
    if (risk == 0) return 2;
    if (risk == 2) return 5;
    return 3;
  }

  static uint16_t newNonce16() {
    return (uint16_t)random(1, 65535);
  }

  static uint16_t crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
      crc ^= (uint16_t)data[i] << 8;
      for (uint8_t b = 0; b < 8; b++) {
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
      }
    }
    return crc;
  }

  void capture() {
    if (gameMode == GAME_RELAY_GAUNTLET) {
      attemptRelayLeg();
      return;
    }

    const uint32_t now = millis();
    if (now - lastFireMs < FIRE_COOLDOWN_MS) return;
    lastFireMs = now;

    if (now < roundArmedMs) {
      feedback = +2; // show ARM/WAIT instead of accepting a blind shot.
      feedbackUntilMs = now + 430UL;
      return;
    }

    const float m = missionMerit();
    const float threshold = captureThreshold();

    if (m >= threshold && lockMeter >= REQUIRED_LOCK) {
      streak = (uint8_t)clampi((int)streak + 1, 0, 9);
      const uint32_t base = 18 + level * 8;
      const uint32_t bonus = (uint32_t)(m * 38.0f) + (uint32_t)(lockMeter * 18.0f) + (uint32_t)(streak * 4);
      score += base + bonus;
      feedback = +1;
      feedbackUntilMs = now + 720UL;
      nextRound(true);
    } else {
      // A real capture needs stable lock. FIRE spam now causes a static hit instead of free levels.
      miss();
    }
  }

  void miss() {
    if (gameMode == GAME_RELAY_GAUNTLET) {
      relayFail(true);
      return;
    }

    streak = 0;
    if (lives > 0) lives--;
    feedback = -1;
    feedbackUntilMs = millis() + 720UL;
    if (lives == 0) endGame();
    else nextRound(false);
  }

  void endGame() {
    state = STATE_GAME_OVER;
    if (score > activeHighScore()) {
      setActiveHighScore(score);
      highScore = score;
    } else {
      highScore = activeHighScore();
    }
  }

  // ---------- Rendering ----------
  void render() {
    display.clearBuffer();

    if (state == STATE_MENU) drawMenu();
    else if (state == STATE_RESET_CONFIRM) drawResetConfirm();
    else if (state == STATE_GAME_OVER) drawGameOver();
    else if (gameMode == GAME_RELAY_GAUNTLET) drawRelayGame();
    else {
      display.setFont(u8g2_font_5x8_tf);
      drawHeader();
      drawEditLine();
      if (mission == MISS_QUIET) drawSpectrumView();
      else drawFieldGraph();
      drawStatusLine();
    }

    display.sendBuffer();
  }

  void drawRelayGame() {
    display.setFont(u8g2_font_5x8_tf);
    drawRelayHeader();
    drawRelayEditLine();
    drawRelayTrack();
    drawRelayStatusLine();
  }

  void drawRelayHeader() {
    const uint32_t elapsed = millis() - roundStartMs;
    const uint32_t remainMs = (elapsed < roundDurationMs) ? (roundDurationMs - elapsed) : 0;
    const uint8_t remainS = (uint8_t)((remainMs + 999UL) / 1000UL);

    display.setCursor(0, 7);
    display.print(F("RELAY CH"));
    display.print(channel);
    display.print(F(" T"));
    display.print(remainS);

    display.setCursor(93, 7);
    if (feedback == +1) display.print(F("PASS"));
    else if (feedback == +2) display.print(F("WAIT"));
    else if (feedback < 0) display.print(F("DROP"));
    else if (millis() < roundArmedMs) display.print(F("ARM"));
    else if (lockMeter >= REQUIRED_LOCK) display.print(F("FIRE"));
    else display.print(F("SYNC"));
  }

  void drawRelayEditLine() {
    display.setCursor(0, 16);
    display.print(F("EDIT "));
    display.print(fieldName(editField));
    display.print(F(" "));
    printFieldValue(editField);
    drawBar(82, 11, 44, 6, lockMeter);
  }

  void drawRelayTrack() {
    const int y = 36;
    const int xs[RELAY_PATH_LEN] = {14, 47, 80, 113};

    display.drawHLine(xs[0] + 6, y, xs[RELAY_PATH_LEN - 1] - xs[0] - 12);
    for (uint8_t i = 0; i < RELAY_PATH_LEN; i++) {
      if (i == relayIndex) display.drawDisc(xs[i], y, 6);
      else display.drawCircle(xs[i], y, 6);

      display.setCursor(xs[i] - 2, y + 3);
      display.print(i + 1);
    }

    display.setCursor(0, 27);
    display.print(F("nonce "));
    display.print(relayNonce16, HEX);
    display.setCursor(72, 27);
    display.print(F("leg "));
    display.print(relayCompletedLegs);

    display.setCursor(0, 50);
    display.print(F("ACK next hop + fresh nonce"));
  }

  void drawRelayStatusLine() {
    const float p = pressureAround(channel);
    display.setCursor(0, 63);
    display.print(F("R"));
    display.print(riskShortName(relayRiskMode));
    display.print(F(" A"));
    display.print(relayLastArc);
    display.print(F(" P"));
    display.print((int)(p * 99.0f));

    display.setCursor(72, 63);
    display.print(F("LK"));
    display.print((int)(lockMeter * 9.0f));
    display.print(F(" L"));
    display.print(lives);
    display.print(F(" S"));
    printCompactScore(score);
  }

  void drawHeader() {
    const uint32_t elapsed = millis() - roundStartMs;
    const uint32_t remainMs = (elapsed < roundDurationMs) ? (roundDurationMs - elapsed) : 0;
    const uint8_t remainS = (uint8_t)((remainMs + 999UL) / 1000UL);

    display.setCursor(0, 7);
    display.print(missionName(mission));
    display.print(F(" CH"));
    display.print(channel);
    display.print(F(" T"));
    display.print(remainS);

    display.setCursor(93, 7);
    if (feedback == +1) display.print(gameMode == GAME_WAVE_RANGERS ? F("SAFE") : F("HIT"));
    else if (feedback == +2) display.print(F("WAIT"));
    else if (feedback < 0) display.print(gameMode == GAME_WAVE_RANGERS ? F("ZAP") : F("MISS"));
    else if (millis() < roundArmedMs) display.print(F("ARM"));
    else if (lockMeter >= REQUIRED_LOCK) display.print(F("FIRE"));
    else {
      display.print(F("Lv"));
      display.print(level);
    }
  }

  void drawEditLine() {
    display.setCursor(0, 16);
    const uint32_t age = millis() - roundStartMs;
    const bool showGoal = (age < 2300UL) || (((millis() / 5000UL) & 1UL) == 0UL);
    if (showGoal) {
      display.print(goalHint(mission));
    } else {
      display.print(F("EDIT "));
      display.print(fieldName(editField));
      display.print(F(" "));
      printFieldValue(editField);
    }

    drawBar(82, 11, 44, 6, lockMeter);
  }

  void drawFieldGraph() {
    const int yTop = 22;
    const int yBot = 52;
    int lastY = yBot;

    display.drawHLine(0, yBot, OLED_W);

    for (int x = 0; x < OLED_W; x++) {
      const float v = fieldAt((float)x);
      int y = yBot - (int)roundf(v * (float)(yBot - yTop));
      y = clampi(y, yTop, yBot);
      if (x > 0) display.drawLine(x - 1, lastY, x, y);
      lastY = y;
    }

    display.drawVLine(targetX, yTop, yBot - yTop + 1);
    drawTargetSprite(targetX, yTop + 5);
    drawAntenna(12, yBot + 6);
    drawAntenna(OLED_W - 13, yBot + 6);

    // Tiny spectrum pressure strip under the wave. It reminds players that the RF environment matters.
    for (uint8_t x = 0; x < OLED_W; x += 2) {
      const uint8_t ch = (uint8_t)map((int)x, 0, OLED_W - 1, 0, 125);
      const uint8_t h = (uint8_t)roundf(pressureAt(ch) * 4.0f);
      if (h > 0) display.drawVLine(x, 55 - h, h);
    }
  }

  void drawSpectrumView() {
    const int yTop = 22;
    const int yBot = 54;
    display.drawFrame(0, yTop, OLED_W, yBot - yTop + 1);

    for (uint8_t x = 1; x < OLED_W - 1; x++) {
      const uint8_t ch = (uint8_t)map((int)x, 1, OLED_W - 2, 0, 125);
      const float p = pressureAt(ch);
      const int h = (int)roundf(p * (float)(yBot - yTop - 3));
      if (h > 0) display.drawVLine(x, yBot - h, h);
    }

    const int selX = map((int)channel, 0, 125, 1, OLED_W - 2);
    display.drawVLine(selX, yTop, yBot - yTop + 1);
    display.setCursor(3, yTop + 8);
    display.print(F("dodge WiFi storms"));
  }

  void drawStatusLine() {
    const float p = pressureAround(channel);
    display.setCursor(0, 63);
    display.print(F("A"));
    display.print((int)(ackA * 99.0f));
    display.print(F(" B"));
    display.print((int)(ackB * 99.0f));
    display.print(F(" P"));
    display.print((int)(p * 99.0f));

    display.setCursor(72, 63);
    display.print(F("LK"));
    display.print((int)(lockMeter * 9.0f));
    display.print(F(" L"));
    display.print(lives);
    display.print(F(" S"));
    printCompactScore(score);
  }

  void drawMenu() {
    display.setFont(u8g2_font_6x10_tf);
    display.drawStr(0, 9, "RF Console Menu");
    display.setFont(u8g2_font_5x8_tf);
    drawMenuRow(0, 20, F("Wave Rangers"), highScoreRangers);
    drawMenuRow(1, 32, F("Relay Gauntlet"), highScoreRelay);
    drawMenuRow(2, 44, F("Antinode Pro"), highScoreClassic);
    display.setCursor(0, 56);
    display.print(menuIndex == 3 ? F("> ") : F("  "));
    display.print(F("Reset high scores"));
    display.setCursor(0, 64);
    display.print(F("FIRE ok"));
  }

  void drawMenuRow(uint8_t idx, uint8_t y, const __FlashStringHelper *name, uint32_t best) {
    display.setCursor(0, y);
    display.print(menuIndex == idx ? F("> ") : F("  "));
    display.print(name);
    display.setCursor(89, y);
    display.print(F("HI "));
    printCompactScore(best);
  }

  void drawResetConfirm() {
    display.setFont(u8g2_font_7x14B_tf);
    display.drawStr(16, 16, "RESET HI?");
    display.setFont(u8g2_font_6x10_tf);
    display.setCursor(8, 34);
    display.print(F("This clears both"));
    display.setCursor(8, 46);
    display.print(F("game high scores."));
    display.setCursor(2, 61);
    display.print(F("FIRE yes  any arrow no"));
  }

  void drawGameOver() {
    display.setFont(u8g2_font_7x14B_tf);
    if (gameMode == GAME_WAVE_RANGERS) display.drawStr(13, 16, "LINK LOST");
    else if (gameMode == GAME_RELAY_GAUNTLET) display.drawStr(4, 16, "TOKEN LOST");
    else display.drawStr(20, 16, "GAME OVER");

    display.setFont(u8g2_font_6x10_tf);
    display.setCursor(9, 33);
    display.print(F("Score: "));
    display.print(score);
    display.setCursor(9, 45);
    display.print(F("Best : "));
    display.print(activeHighScore());
    display.setCursor(3, 59);
    display.print(F("FIRE replay  UP menu"));
  }

  void drawAntenna(int x, int baseY) {
    display.drawVLine(x, baseY - 7, 7);
    display.drawLine(x, baseY - 7, x - 4, baseY - 3);
    display.drawLine(x, baseY - 7, x + 4, baseY - 3);
    display.drawCircle(x, baseY - 8, 2);
  }

  void drawTargetSprite(int x, int y) {
    x = clampi(x, 7, OLED_W - 8);
    if (gameMode == GAME_CLASSIC) {
      display.drawCircle(x, y + 4, 4);
      display.drawHLine(x - 6, y + 4, 13);
      display.drawVLine(x, y - 2, 13);
      if (mission == MISS_NULL) display.drawDisc(x, y + 4, 2);
      return;
    }
    if (mission == MISS_NULL) {
      // Jammer: blocky enemy that must sit on a dark null.
      display.drawBox(x - 4, y, 8, 7);
      display.drawPixel(x - 2, y + 2);
      display.drawPixel(x + 2, y + 2);
      display.drawHLine(x - 3, y + 9, 7);
    } else if (mission == MISS_MULTIPATH) {
      // Ghost packet: harder because reflections move.
      display.drawCircle(x, y + 4, 4);
      display.drawPixel(x - 1, y + 3);
      display.drawPixel(x + 2, y + 3);
      display.drawHLine(x - 3, y + 8, 3);
      display.drawHLine(x + 1, y + 8, 3);
    } else {
      // Echo, the rescue robot.
      display.drawFrame(x - 4, y, 8, 7);
      display.drawPixel(x - 2, y + 2);
      display.drawPixel(x + 2, y + 2);
      display.drawHLine(x - 2, y + 5, 5);
      display.drawPixel(x, y - 1);
    }
  }

  void drawBar(uint8_t x, uint8_t y, uint8_t w, uint8_t h, float v) {
    display.drawFrame(x, y, w, h);
    const uint8_t fill = (uint8_t)roundf(clampf(v, 0.0f, 1.0f) * (float)(w - 2));
    if (fill > 0) display.drawBox(x + 1, y + 1, fill, h - 2);
  }

  void printCompactScore(uint32_t s) {
    if (s > 9999UL) {
      display.print(s / 1000UL);
      display.print(F("k"));
    } else {
      display.print(s);
    }
  }

  void printFieldValue(EditField f) {
    if (gameMode == GAME_RELAY_GAUNTLET) {
      if (f == EDIT_CHANNEL) {
        display.print(channel);
        display.print(F("/"));
        display.print(2400 + channel);
      } else {
        display.print(riskName(relayRiskMode));
      }
      return;
    }

    switch (f) {
      case EDIT_CHANNEL:
        display.print(channel);
        display.print(F("/"));
        display.print(2400 + channel);
        break;
      case EDIT_DELAY:
        display.print(virtualDelayUs);
        display.print(F("us"));
        break;
      case EDIT_PA_A:
        display.print(paName(paA));
        break;
      case EDIT_PA_B:
        display.print(paName(paB));
        break;
      case EDIT_RATE:
        display.print(rateName(rateIdx));
        break;
      default:
        break;
    }
  }

  const __FlashStringHelper *missionName(Mission m) const {
    if (gameMode == GAME_CLASSIC) {
      switch (m) {
        case MISS_NULL:      return F("NULL");
        case MISS_BALANCE:   return F("BAL");
        case MISS_QUIET:     return F("QUIET");
        case MISS_RATE:      return F("RATE");
        case MISS_MULTIPATH: return F("MULTI");
        default:             return F("PEAK");
      }
    }
    switch (m) {
      case MISS_NULL:      return F("SHIELD");
      case MISS_BALANCE:   return F("TWIN");
      case MISS_QUIET:     return F("QUIET");
      case MISS_RATE:      return F("MODE");
      case MISS_MULTIPATH: return F("GHOST");
      default:             return F("BEACON");
    }
  }

  const __FlashStringHelper *goalHint(Mission m) const {
    if (gameMode == GAME_CLASSIC) {
      switch (m) {
        case MISS_NULL:      return F("move target to NULL");
        case MISS_BALANCE:   return F("match antenna PA");
        case MISS_QUIET:     return F("low-noise channel");
        case MISS_RATE:      return F("best data RATE");
        case MISS_MULTIPATH: return F("beat reflections");
        default:             return F("move target to PEAK");
      }
    }
    switch (m) {
      case MISS_NULL:      return F("null the Jammer");
      case MISS_BALANCE:   return F("balance twins");
      case MISS_QUIET:     return F("find quiet CH");
      case MISS_RATE:      return F("pick right RATE");
      case MISS_MULTIPATH: return F("hold Ghost lock");
      default:             return F("crest on Echo");
    }
  }

  const __FlashStringHelper *fieldName(EditField f) const {
    if (gameMode == GAME_RELAY_GAUNTLET) {
      if (f == EDIT_CHANNEL) return F("CHAN");
      return F("RISK");
    }

    switch (f) {
      case EDIT_DELAY: return F("DLY");
      case EDIT_PA_A:  return F("PA_A");
      case EDIT_PA_B:  return F("PA_B");
      case EDIT_RATE:  return F("RATE");
      default:         return F("CHAN");
    }
  }

  const __FlashStringHelper *riskName(uint8_t idx) const {
    switch (idx % 3) {
      case 0: return F("FAST");
      case 2: return F("ENDURE");
      default: return F("BAL");
    }
  }

  const __FlashStringHelper *riskShortName(uint8_t idx) const {
    switch (idx % 3) {
      case 0: return F("F");
      case 2: return F("E");
      default: return F("B");
    }
  }

  const __FlashStringHelper *paName(uint8_t idx) const {
    switch (idx > 3 ? 3 : idx) {
      case 0: return F("MIN");
      case 1: return F("LOW");
      case 2: return F("HIGH");
      default: return F("MAX");
    }
  }

  const __FlashStringHelper *rateName(uint8_t idx) const {
    switch (idx % 3) {
      case 1: return F("2M");
      case 2: return F("250K");
      default: return F("1M");
    }
  }

  // ---------- Small helpers ----------
  static float clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
  }

  static int clampi(int x, int lo, int hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
  }

  static float paScalar(uint8_t idx) {
    switch (idx > 3 ? 3 : idx) {
      case 0: return 0.25f;
      case 1: return 0.45f;
      case 2: return 0.72f;
      default: return 1.0f;
    }
  }

  static rf24_pa_dbm_e paFromIndex(uint8_t idx) {
    switch (idx > 3 ? 3 : idx) {
      case 0: return RF24_PA_MIN;
      case 1: return RF24_PA_LOW;
      case 2: return RF24_PA_HIGH;
      default: return RF24_PA_MAX;
    }
  }

  static rf24_datarate_e rateFromIndex(uint8_t idx) {
    switch (idx % 3) {
      case 1: return RF24_2MBPS;
      case 2: return RF24_250KBPS;
      default: return RF24_1MBPS;
    }
  }
};
