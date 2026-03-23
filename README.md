# ESP32 Steering Wheel Button Box

An ESP32-based BLE gamepad for a steering wheel button box. Reads 14 physical buttons and 2 rotary encoders, transmitting inputs over Bluetooth Low Energy as a standard HID gamepad. All settings are configurable at runtime via a built-in WiFi web interface — no reflashing required.

---

## Hardware

### Buttons — 14 total (INPUT_PULLUP)

| Button | Pin |
|--------|-----|
| 1 *(config boot pin)* | 2 |
| 2 | 13 |
| 3 | 15 |
| 4 | 14 |
| 5 | 16 |
| 6 | 17 |
| 7 | 18 |
| 8 | 19 |
| 9 | 21 |
| 10 | 22 |
| 11 | 23 |
| 12 | 25 |
| 13 | 32 |
| 14 | 33 |

### Encoders — 2 total (INPUT_PULLUP)

| Encoder | CLK | DT |
|---------|-----|----|
| 1 | 26 | 27 |
| 2 | 4 | 5 |

---

## BLE Gamepad Button Mapping

### Normal encoder mode
| Input | Gamepad button |
|-------|---------------|
| Physical buttons 1–14 | 1–14 |
| Encoder 1 CW | 15 |
| Encoder 1 CCW | 16 |
| Encoder 2 CW | 17 |
| Encoder 2 CCW | 18 |

### Zones encoder mode
| Input | Gamepad button |
|-------|---------------|
| Physical buttons 1–14 | 1–14 |
| Slave encoder CW  — zone 0 | 15 |
| Slave encoder CCW — zone 0 | 16 |
| Slave encoder CW  — zone 1 | 17 |
| Slave encoder CCW — zone 1 | 18 |
| *(more pairs for each additional zone)* | … |

---

## Encoder Modes

### Normal mode
Both encoders work independently. Each rotation direction maps to a fixed gamepad button press.

### Zones mode
One encoder acts as a **master** (zone selector) and the other as a **slave** (action encoder).

- The master encoder tracks an absolute position from `0` to `zoneSteps − 1` (wraps around).
- That position is divided into equal-sized zones: `zone = (position × zoneCount) / zoneSteps`.
- The slave encoder triggers a different button pair for each zone, effectively multiplying the number of distinct inputs.

**Example — 20 steps, 2 zones:**
- Steps 0–9 → zone 0 → slave CW = button 15, slave CCW = button 16
- Steps 10–19 → zone 1 → slave CW = button 17, slave CCW = button 18

> Zone count must be a divisor of zone steps (e.g. for 20 steps: 2, 4, 5, 10, or 20).

---

## Runtime Configuration

Settings are stored in ESP32 NVS (non-volatile storage) and survive power loss.

### Entering config mode

1. **Hold Button 1 (pin 2) while powering on.**
2. The device creates a WiFi access point named `ButtonBox-Config`.
3. Connect to that network on any device.
4. Open `http://192.168.4.1` in any browser.
5. Edit settings and press **Save & Reboot**.
6. The device saves to NVS and reboots into gamepad mode.

### Available settings

| Setting | Default | Description |
|---------|---------|-------------|
| Use Encoders | `true` | Enable / disable rotary encoders |
| Button Debounce | `5 ms` | Bounce2 debounce interval |
| Button Task Interval | `5 ms` | Button polling rate |
| Encoder Debounce | `1000 µs` | Hardware encoder filter |
| Encoder Press Duration | `100 ms` | Simulated key-press length |
| Encoder Task Interval | `5 ms` | Encoder polling rate |
| Encoder Mode | Normal | Normal or Zones |
| Master Encoder | 0 | Zones mode: which encoder selects the zone |
| Zone Steps | `20` | Total steps of the master encoder |
| Zone Count | `2` | Number of equal zones (must divide Zone Steps) |

---

## Dependencies

Install via **Arduino Library Manager**:

| Library | Version |
|---------|---------|
| [Bounce2](https://github.com/thomasfredericks/Bounce2) | any |
| [ESP32-BLE-Gamepad](https://github.com/lemmingDev/ESP32-BLE-Gamepad) | 0.5.4 |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | 1.4.1 |
| [ArduinoJson](https://arduinojson.org) | ≥ 6.x |

Built-in ESP32 Arduino core libraries used: `Preferences`, `WiFi`, `WebServer`.

---

## Debug Output

Uncomment the first line of `ec2-button-box.ino` to enable serial logging at 115200 baud:

```cpp
#define SERIAL_DEBUG
```

Logs include: button press/release, encoder direction and button number, master encoder position and active zone, and WiFi config mode startup messages.

---

## File Structure

```
├── ec2-button-box/
│   ├── ec2-button-box.ino   # Entry point, tasks, pin assignments
│   ├── Config.h             # Config struct, NVS load/save, JSON helpers
│   ├── Encoder.h            # Rotary encoder class (Enc namespace)
│   └── ConfigWiFi.h         # WiFi AP, HTTP server, embedded config page
└── docs/
    └── index.html           # GitHub Pages setup guide
```
