# ESP32 Game Console GPIO Connections

This file has **two complete wiring sections**:

1. **Section 1: Connections WITH nRF24L01 Adapter Board**
2. **Section 2: Connections WITHOUT nRF24L01 Adapter Board**

Use **only one section** depending on your nRF24L01 setup.

---

# Section 1: Connections WITH nRF24L01 Adapter Board

Use this section if your nRF24L01 modules are plugged into adapter boards that accept **5V input**.

## Power System

```text
3.7V LiPo Battery
   → DPDT ON/OFF Switch
   → 5V Boost Converter
   → ESP32 VIN / 5V
```

> Set the boost converter output to **5V** before connecting it to the ESP32.

---

## LiPo Battery, DPDT Switch, and Boost Converter

You are using a **6-legged DPDT switch**, but only one side / one row of 3 legs is needed.

## DPDT Switch Wiring

| DPDT Switch Pin | Connection |
| --------------- | ---------- |
| Middle pin on one side | LiPo Battery **+** |
| One outer pin on same side | Boost Converter **IN+** |
| Other outer pin on same side | Not connected |
| Other 3 switch pins | Not connected |

If the switch turns ON in the opposite direction, move the Boost Converter **IN+** wire to the other outer pin.

## Battery to Boost Converter

| Battery / Boost Pin | Connection |
| ------------------- | ---------- |
| LiPo Battery **+** | DPDT switch middle pin |
| DPDT switch outer pin | Boost Converter **IN+** |
| LiPo Battery **-** | Boost Converter **IN-** |

## Boost Converter to ESP32

| Boost Converter Pin | ESP32 Pin |
| ------------------- | --------- |
| OUT+ | VIN / 5V |
| OUT- | GND |

---

## OLED Screen

| OLED Pin | ESP32 GPIO / Pin |
| -------- | ---------------- |
| SDA | GPIO 21 |
| SCL / SCK | GPIO 22 |
| VCC | 3V3 |
| GND | GND |

---

## Buttons

Each button:

```text
One leg → ESP32 GPIO
Opposite / diagonal leg → GND
```

| Button | ESP32 GPIO |
| ------ | ---------- |
| UP | GPIO 32 |
| DOWN | GPIO 33 |
| LEFT | GPIO 25 |
| RIGHT | GPIO 26 |
| FIRE | GPIO 27 |

Use internal pull-up resistors in code:

```cpp
pinMode(32, INPUT_PULLUP);
pinMode(33, INPUT_PULLUP);
pinMode(25, INPUT_PULLUP);
pinMode(26, INPUT_PULLUP);
pinMode(27, INPUT_PULLUP);
```

When a button is pressed, the GPIO reads **LOW**.

---

## nRF24L01 Module 1 WITH Adapter Board

| nRF24L01 Adapter Pin | ESP32 GPIO / Pin |
| -------------------- | ---------------- |
| CE | GPIO 4 |
| CSN / CS | GPIO 5 |
| SCK | GPIO 18 |
| MOSI | GPIO 23 |
| MISO | GPIO 19 |
| VCC | VIN / 5V |
| GND | GND |
| IRQ | Not connected |

---

## nRF24L01 Module 2 WITH Adapter Board

| nRF24L01 Adapter Pin | ESP32 GPIO / Pin |
| -------------------- | ---------------- |
| CE | GPIO 13 |
| CSN / CS | GPIO 14 |
| SCK | GPIO 18 |
| MOSI | GPIO 23 |
| MISO | GPIO 19 |
| VCC | VIN / 5V |
| GND | GND |
| IRQ | Not connected |

---

## Shared SPI Pins WITH Adapter Board

Both nRF24L01 adapter boards share the same SPI pins.

| SPI Signal | ESP32 GPIO |
| ---------- | ---------- |
| SCK | GPIO 18 |
| MOSI | GPIO 23 |
| MISO | GPIO 19 |

Each adapter has separate **CE** and **CSN / CS** pins.

| Module | CE Pin | CSN / CS Pin |
| ------ | ------ | ------------ |
| nRF24L01 Module 1 | GPIO 4 | GPIO 5 |
| nRF24L01 Module 2 | GPIO 13 | GPIO 14 |

---

## Common Ground WITH Adapter Board

All grounds must be connected together.

| Part | Ground Connection |
| ---- | ----------------- |
| ESP32 GND | Common GND |
| Boost Converter OUT- | Common GND |
| OLED GND | Common GND |
| Button GND legs | Common GND |
| nRF24L01 Adapter 1 GND | Common GND |
| nRF24L01 Adapter 2 GND | Common GND |

---

## Power Summary WITH Adapter Board

| Component | Power |
| --------- | ----- |
| ESP32 | 5V from boost converter to VIN / 5V |
| OLED | 3V3 from ESP32 |
| nRF24L01 Adapter Module 1 | VIN / 5V from ESP32 |
| nRF24L01 Adapter Module 2 | VIN / 5V from ESP32 |
| Buttons | GPIO to GND when pressed |

---

## Wiring Diagram WITH Adapter Board

```text
        LiPo Battery
       +          -
       |          |
       |          +----------------------+
       |                                 |
       v                                 v
DPDT switch middle pin              Boost IN-
       |
       v
DPDT switch outer pin
       |
       v
Boost IN+

Boost OUT+ 5V ---------------------> ESP32 VIN / 5V
Boost OUT- ------------------------> ESP32 GND

ESP32 3V3 -------------------------> OLED VCC
ESP32 GND -------------------------> OLED GND

ESP32 VIN / 5V --------------------> nRF24L01 Adapter 1 VCC
ESP32 GND -------------------------> nRF24L01 Adapter 1 GND

ESP32 VIN / 5V --------------------> nRF24L01 Adapter 2 VCC
ESP32 GND -------------------------> nRF24L01 Adapter 2 GND

Buttons:
GPIO pin --------------------------> Button
Button opposite leg ---------------> GND
```

---

# Section 2: Connections WITHOUT nRF24L01 Adapter Board

Use this section if you are using **bare nRF24L01 modules** without adapter boards.

Important:

```text
Bare nRF24L01 VCC must connect to 3V3 only.
Do NOT connect a bare nRF24L01 directly to 5V.
```

## Power System

```text
3.7V LiPo Battery
   → DPDT ON/OFF Switch
   → 5V Boost Converter
   → ESP32 VIN / 5V
```

> Set the boost converter output to **5V** before connecting it to the ESP32.

---

## LiPo Battery, DPDT Switch, and Boost Converter

You are using a **6-legged DPDT switch**, but only one side / one row of 3 legs is needed.

## DPDT Switch Wiring

| DPDT Switch Pin | Connection |
| --------------- | ---------- |
| Middle pin on one side | LiPo Battery **+** |
| One outer pin on same side | Boost Converter **IN+** |
| Other outer pin on same side | Not connected |
| Other 3 switch pins | Not connected |

If the switch turns ON in the opposite direction, move the Boost Converter **IN+** wire to the other outer pin.

## Battery to Boost Converter

| Battery / Boost Pin | Connection |
| ------------------- | ---------- |
| LiPo Battery **+** | DPDT switch middle pin |
| DPDT switch outer pin | Boost Converter **IN+** |
| LiPo Battery **-** | Boost Converter **IN-** |

## Boost Converter to ESP32

| Boost Converter Pin | ESP32 Pin |
| ------------------- | --------- |
| OUT+ | VIN / 5V |
| OUT- | GND |

---

## OLED Screen

| OLED Pin | ESP32 GPIO / Pin |
| -------- | ---------------- |
| SDA | GPIO 21 |
| SCL / SCK | GPIO 22 |
| VCC | 3V3 |
| GND | GND |

---

## Buttons

Each button:

```text
One leg → ESP32 GPIO
Opposite / diagonal leg → GND
```

| Button | ESP32 GPIO |
| ------ | ---------- |
| UP | GPIO 32 |
| DOWN | GPIO 33 |
| LEFT | GPIO 25 |
| RIGHT | GPIO 26 |
| FIRE | GPIO 27 |

Use internal pull-up resistors in code:

```cpp
pinMode(32, INPUT_PULLUP);
pinMode(33, INPUT_PULLUP);
pinMode(25, INPUT_PULLUP);
pinMode(26, INPUT_PULLUP);
pinMode(27, INPUT_PULLUP);
```

When a button is pressed, the GPIO reads **LOW**.

---

## nRF24L01 Module 1 WITHOUT Adapter Board

| nRF24L01 Pin | ESP32 GPIO / Pin |
| ------------ | ---------------- |
| CE | GPIO 4 |
| CSN / CS | GPIO 5 |
| SCK | GPIO 18 |
| MOSI | GPIO 23 |
| MISO | GPIO 19 |
| VCC | 3V3 |
| GND | GND |
| IRQ | Not connected |

---

## nRF24L01 Module 2 WITHOUT Adapter Board

| nRF24L01 Pin | ESP32 GPIO / Pin |
| ------------ | ---------------- |
| CE | GPIO 13 |
| CSN / CS | GPIO 14 |
| SCK | GPIO 18 |
| MOSI | GPIO 23 |
| MISO | GPIO 19 |
| VCC | 3V3 |
| GND | GND |
| IRQ | Not connected |

---

## Shared SPI Pins WITHOUT Adapter Board

Both bare nRF24L01 modules share the same SPI pins.

| SPI Signal | ESP32 GPIO |
| ---------- | ---------- |
| SCK | GPIO 18 |
| MOSI | GPIO 23 |
| MISO | GPIO 19 |

Each module has separate **CE** and **CSN / CS** pins.

| Module | CE Pin | CSN / CS Pin |
| ------ | ------ | ------------ |
| nRF24L01 Module 1 | GPIO 4 | GPIO 5 |
| nRF24L01 Module 2 | GPIO 13 | GPIO 14 |

---

## Common Ground WITHOUT Adapter Board

All grounds must be connected together.

| Part | Ground Connection |
| ---- | ----------------- |
| ESP32 GND | Common GND |
| Boost Converter OUT- | Common GND |
| OLED GND | Common GND |
| Button GND legs | Common GND |
| nRF24L01 Module 1 GND | Common GND |
| nRF24L01 Module 2 GND | Common GND |

---

## Capacitor Recommendation WITHOUT Adapter Board

Bare nRF24L01 modules can be unstable without a capacitor.

Add one capacitor across each module power input:

| Capacitor | Connection |
| --------- | ---------- |
| 10µF to 47µF electrolytic capacitor | Between nRF VCC and GND |
| Capacitor + leg | nRF VCC |
| Capacitor - leg | nRF GND |

---

## Power Summary WITHOUT Adapter Board

| Component | Power |
| --------- | ----- |
| ESP32 | 5V from boost converter to VIN / 5V |
| OLED | 3V3 from ESP32 |
| nRF24L01 Module 1 | 3V3 from ESP32 |
| nRF24L01 Module 2 | 3V3 from ESP32 |
| Buttons | GPIO to GND when pressed |

---

## Wiring Diagram WITHOUT Adapter Board

```text
        LiPo Battery
       +          -
       |          |
       |          +----------------------+
       |                                 |
       v                                 v
DPDT switch middle pin              Boost IN-
       |
       v
DPDT switch outer pin
       |
       v
Boost IN+

Boost OUT+ 5V ---------------------> ESP32 VIN / 5V
Boost OUT- ------------------------> ESP32 GND

ESP32 3V3 -------------------------> OLED VCC
ESP32 GND -------------------------> OLED GND

ESP32 3V3 -------------------------> nRF24L01 Module 1 VCC
ESP32 GND -------------------------> nRF24L01 Module 1 GND

ESP32 3V3 -------------------------> nRF24L01 Module 2 VCC
ESP32 GND -------------------------> nRF24L01 Module 2 GND

Buttons:
GPIO pin --------------------------> Button
Button opposite leg ---------------> GND
```

---

# Final Notes

- Use **Section 1** if you have nRF24L01 adapter boards.
- Use **Section 2** if you have bare nRF24L01 modules.
- Boost converter **5V OUT+** goes to ESP32 **VIN / 5V**.
- Never connect boost converter **5V** directly to ESP32 **3V3**.
- All grounds must be common.
