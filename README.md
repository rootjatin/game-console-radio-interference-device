# RF Console Dual Games

This Arduino sketch turns your ESP32 + OLED + two nRF24L01 adapter boards into a small RF-themed game console.

It now includes **three selectable games**:

1. **Wave Rangers: Signal Rescue**  
   Character/story mode. Rescue Echo, shield against Jammers, dodge WiFi/Bluetooth storm zones, and hold a valid RF lock before firing.

2. **Relay Gauntlet**  
   New research-paper-aligned nRF24 token-relay game. A virtual token advances through a four-hop route only when the link has been held stable long enough. The game uses nonce-based token packets, ACK payloads, retry-count scoring, and risk modes that change data rate, RF power, and auto-retry settings together.

3. **Antinode Hunter Pro**  
   Your original wave/antinodes game, but improved so FIRE spam cannot auto-clear levels. It uses the same lock meter, arming delay, moving targets, spectrum pressure, and high-score saving.

The sketch uses your **adapter-board wiring**:

- OLED SDA/SCL: GPIO 21 / GPIO 22
- Buttons: UP 32, DOWN 33, LEFT 25, RIGHT 26, FIRE 27, each wired to GND
- Shared SPI: SCK 18, MISO 19, MOSI 23
- nRF24 A: CE 4, CSN 5
- nRF24 B: CE 13, CSN 14
- nRF adapter VCC: VIN / 5V from your ESP32/boost system
- All grounds common

## What changed based on the research paper

The paper recommends treating radio as a continuous skill surface rather than letting one lucky packet decide an outcome. This code now applies that idea in three places:

- **Rolling lock before scoring:** FIRE only works after the lock meter is built from repeated ACK, retry, channel-pressure, and stability readings.
- **nRF24 relay mechanics:** Relay Gauntlet uses compact token packets, duplicate-resistant sequence numbers, nonce refresh through ACK payloads, and retry-count feedback.
- **Risk-reward radio settings:** Relay Gauntlet has FAST, BAL, and ENDURE modes:
  - FAST: 2 Mbps, low power, short retry delay, fewer retries, higher points.
  - BAL: 1 Mbps, medium/high power, moderate retries.
  - ENDURE: 250 kbps, higher power, longer retry delay, more retries, safer but lower reward.
- **Anti-spam scoring:** early shots, unstable windows, repeated drops, and MAX_RT-style failures are penalized instead of becoming random wins.

## Does it use the radio antennas?

Yes. With `USE_RADIOS 1` in `Config.h`, the game uses the two nRF24L01 modules in these ways:

- It starts both radios using the wiring above.
- It sends local packets between radio A and radio B.
- It reads ACK success and retry count using `write()` and `getARC()`.
- It enables dynamic payloads and ACK payloads for Relay Gauntlet.
- It scans nearby 2.4 GHz channel pressure using `testRPD()` on plus modules or `testCarrier()` on standard modules.
- It combines nRF readings with optional ESP32 WiFi scan data to affect game difficulty and scoring.

If the radio check screen says `A: not found` or `B: not found`, the sketch falls back to offline training values so the games still run. For the real antenna game, both should show `OK`.

## Controls

### Main menu

- UP / DOWN: select option
- FIRE: choose option

Menu options:

- Wave Rangers
- Relay Gauntlet
- Antinode Pro
- Reset high scores

### Wave Rangers / Antinode Pro

- UP / DOWN: choose which RF setting to edit
- LEFT / RIGHT: adjust selected RF setting
- FIRE: capture only when lock is ready

Selectable RF settings:

- CHAN: nRF channel, 0-125, displayed as 2400 + channel MHz
- DLY: virtual phase delay
- PA_A: antenna A power level
- PA_B: antenna B power level
- RATE: 1M, 2M, or 250K nRF data rate

### Relay Gauntlet

- UP / DOWN: switch between CHAN and RISK
- LEFT / RIGHT on CHAN: move to another nRF channel
- LEFT / RIGHT on RISK: cycle FAST / BAL / ENDURE
- FIRE: attempt the current relay hop only when lock is ready

The screen shows the four-hop route, the active relay position, the current nonce, last retry count (`A`), pressure (`P`), lock (`LK`), lives, and score.

### Game over

- FIRE: replay same game
- Any arrow: return to menu

### Reset high scores

Choose **Reset high scores** from the menu, then press FIRE to confirm. Press any arrow to cancel.

## Why FIRE spam no longer skips levels

Each round now has:

- a short **ARM** period at the start,
- a visible **LOCK** meter,
- a FIRE cooldown,
- randomized setup so the previous setting cannot solve many rounds,
- a penalty for firing before the signal is valid.

You must tune the signal, hold a strong lock, then press FIRE.

## Required libraries

Install these in Arduino IDE:

- U8g2
- RF24 by nRF24 / TMRh20
- ESP32 board package

## OLED type

By default, `Config.h` uses SH1106:

```cpp
#define OLED_DRIVER_SH1106 1
```

If your display is SSD1306, set it to:

```cpp
#define OLED_DRIVER_SH1106 0
```

## Upload steps

1. Open `RFConsoleDualGames.ino` in Arduino IDE.
2. Select your ESP32 board.
3. Install the required libraries.
4. Confirm your nRF modules are adapter-board modules powered from VIN / 5V.
5. Upload.

## Note about true multiplayer

This ZIP keeps your existing single-ESP32 / dual-nRF24 hardware model. Relay Gauntlet implements the packet format, ACK-payload flow, risk modes, and scoring model from the paper locally between radio A and radio B. To turn it into a true multi-handheld relay, split the `RelayTokenPacket`, `RelayAckPayload`, and relay-state helpers into a shared header and run one node as the base/referee while player nodes forward the token.
