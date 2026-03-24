# ESP32 Steering Wheel Button Box

An ESP32-based BLE gamepad for a steering wheel button box. Reads 14 physical buttons and 2 rotary encoders, transmitting inputs over Bluetooth Low Energy as a standard HID gamepad. All settings are configurable at runtime via Bluetooth — no reflashing, no WiFi, no browser needed.

---

## Hardware

### Buttons — 14 total (INPUT_PULLUP)

| Button | Pin |
|--------|-----|
| 1 | 2 |
| 2 | 13 |
| 3 | 15 |
| 4 | 14 |
| 5 | 16 |
| 6 *(default config boot button)* | 17 |
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

Settings are stored in ESP32 NVS (non-volatile storage) and survive power loss. Configuration is done via Bluetooth using the desktop app — no WiFi, no browser, no network connection needed.

### Entering config mode

1. **Hold the config boot button (default: Button 6, pin 17) while powering on.**
2. The device advertises itself over Bluetooth as `ButtonBox-Config`.
3. Open the desktop app — it scans and connects automatically.
4. Edit settings and press **Save & Reboot**.
5. The device saves to NVS and reboots into gamepad mode.

> The config boot button can be changed to any of the 14 physical buttons from the app itself.

### Crash-counter fallback

If the device reboots 3 times in a row without completing a successful startup (e.g. due to a bad config causing a crash), it automatically enters config mode so the device is always recoverable without holding any button.

### Available settings

| Setting | Default | Description |
|---------|---------|-------------|
| Bluetooth Name | `ESP32-steering-wheel` | Name shown when pairing via BLE |
| Config Boot Button | `6` | Button number held at boot to enter config mode |
| Button Debounce | `5 ms` | Bounce2 debounce interval |
| Button Task Interval | `5 ms` | Button polling rate |
| Use Encoders | `true` | Enable / disable rotary encoders |
| Encoder Mode | Normal | Normal or Zones |
| Master Encoder | `0` | Zones mode: which encoder selects the zone |
| Zone Steps | `20` | Total steps of the master encoder |
| Zone Count | `2` | Number of equal zones (must divide Zone Steps) |
| Zone Reset Combo | *(none)* | Buttons held simultaneously to reset zone position to 0 |
| Encoder Debounce | `1000 µs` | Hardware encoder filter |
| Encoder Press Duration | `100 ms` | Simulated key-press length |
| Encoder Task Interval | `5 ms` | Encoder polling rate |

---

## Desktop App

A standalone desktop app that connects to the ESP32 over Bluetooth to read and write configuration. Download the latest release from the [Releases page](https://github.com/emanuel-xavier/ESP32-steering-wheel-button-box/releases) or build from source.

### How it works

1. Put the ESP32 into config mode (hold the config button at power-on)
2. Open the app — it scans for `ButtonBox-Config` via Bluetooth automatically
3. Config is read from the device and shown in the UI
4. Edit settings and press **Save & Reboot**

### Build requirements

| Platform | Requirement |
|----------|-------------|
| Linux | `webkit2gtk` — `sudo pacman -S webkit2gtk` (Arch) / `sudo apt install libwebkit2gtk-4.0-dev libgtk-3-dev` |
| Linux (Bluetooth) | BlueZ must be running. User must have permission: `sudo usermod -aG bluetooth $USER` |
| Windows (cross-compile from Linux) | `mingw-w64` — `sudo pacman -S mingw-w64-gcc` (Arch) / `sudo apt install gcc-mingw-w64` |
| Windows (native build) | Go + gcc via [MSYS2](https://www.msys2.org/) |
| macOS | Xcode Command Line Tools — `xcode-select --install` |

### Build

```bash
cd desktop

# Linux
make linux

# Windows executable (cross-compiled from Linux)
make windows

# Both
make
```

Binaries are placed in `desktop/build/`.

### Run without building

```bash
cd desktop
go run .
```

---

## Arduino Dependencies

Install via **Arduino Library Manager**:

| Library | Version |
|---------|---------|
| [Bounce2](https://github.com/thomasfredericks/Bounce2) | any |
| [ESP32-BLE-Gamepad](https://github.com/lemmingDev/ESP32-BLE-Gamepad) | 0.5.4 |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | 1.4.1 |
| [ArduinoJson](https://arduinojson.org) | ≥ 6.x |

Built-in ESP32 Arduino core libraries used: `Preferences`.

---

## Debug Output

Uncomment (or keep) the first line of `ec2-button-box.ino` to enable serial logging at 115200 baud:

```cpp
#define SERIAL_DEBUG
```

Logs include: button press/release, encoder direction and button number, master encoder position and active zone, boot counter, and BLE config mode startup messages.

---

## File Structure

```
├── ec2-button-box/
│   ├── ec2-button-box.ino   # Entry point, tasks, pin assignments
│   ├── Config.h             # Config struct, NVS load/save, JSON helpers
│   ├── ConfigBLE.h          # BLE GATT config server (NimBLE)
│   └── Encoder.h            # Rotary encoder class (Enc namespace)
├── desktop/
│   ├── main.go              # BLE client + local HTTP server + webview window
│   ├── index.html           # Config UI served by the desktop app
│   ├── Makefile             # Linux and Windows build targets
│   ├── go.mod
│   └── go.sum
├── .github/
│   └── workflows/
│       └── release.yml      # Builds and publishes desktop binaries on tag push
└── docs/
    └── index.html           # GitHub Pages project page
```
