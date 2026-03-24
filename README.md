# ESP32 Steering Wheel Button Box

An ESP32-based BLE gamepad for a steering wheel button box. Supports up to 32 direct-wired buttons, an 8×8 button matrix, and 2 rotary encoders — all transmitted over Bluetooth Low Energy as a standard HID gamepad.

All settings are configurable at runtime via the desktop app over Bluetooth. No reflashing, no WiFi, no special boot sequence required.

---

## Hardware

Pin assignments are fully configurable at runtime. The defaults below match the original wiring.

### Direct buttons (INPUT_PULLUP, default 14 buttons)

| Button | Default pin |
|--------|-------------|
| 1 | 2 |
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

### Encoders (INPUT_PULLUP, default pins)

| Encoder | CLK | DT |
|---------|-----|----|
| 1 | 26 | 27 |
| 2 | 4 | 5 |

---

## BLE Gamepad Button Mapping

Buttons are assigned in order: **direct buttons → matrix buttons → encoder buttons**.

### Normal encoder mode

| Input | Gamepad button |
|-------|---------------|
| Direct buttons 1–N | 1–N |
| Matrix buttons (row×col, left→right, top→bottom) | N+1 … N+rows×cols |
| Encoder 1 CW | next |
| Encoder 1 CCW | next+1 |
| Encoder 2 CW | next+2 |
| Encoder 2 CCW | next+3 |

### Zones encoder mode

One encoder acts as a **master** (zone selector) and the other as a **slave** (action encoder).

- The master tracks an absolute position `0 … zoneSteps−1` (wraps around).
- `zone = (position × zoneCount) / zoneSteps`
- Each zone maps the slave encoder to a different button pair.

**Example — 20 steps, 2 zones:**
- Steps 0–9 → zone 0 → slave CW = button N+1, slave CCW = button N+2
- Steps 10–19 → zone 1 → slave CW = button N+3, slave CCW = button N+4

> Zone count must be a divisor of zone steps (e.g. for 20 steps: 2, 4, 5, 10, 20).

---

## Runtime Configuration

Settings are stored in ESP32 NVS (non-volatile storage) and survive power loss.

### How to configure

1. Power on the ESP32 — it immediately starts the gamepad **and** the config service simultaneously.
2. Open the desktop app — it scans for the device by Bluetooth service UUID and connects automatically.
3. Edit settings and press **Save & Reboot**.
4. The device reboots with the new config. The app reconnects automatically.

No special boot mode is needed. The config service is always available.

### Available settings

| Setting | Default | Description |
|---------|---------|-------------|
| Bluetooth Name | `ESP32-steering-wheel` | BLE device name shown when pairing |
| **Pin Layout** | | |
| Use Direct Buttons | on | Enable/disable GPIO pin buttons |
| Number of Buttons | `14` | How many direct-wired buttons (0–32) |
| Button Pins | see Hardware | GPIO number for each button |
| **Button Matrix** | | |
| Use Matrix | off | Enable row/column scanning (stackable with direct buttons) |
| Rows | `4` | Matrix rows (1–8), driven LOW when scanning |
| Columns | `4` | Matrix columns (1–8), read with INPUT_PULLUP |
| Row Pins / Col Pins | — | GPIO for each row output and column input |
| **Buttons** | | |
| Button Debounce | `5 ms` | Bounce2 debounce interval |
| Button Task Interval | `5 ms` | Button polling rate |
| **Encoders** | | |
| Use Encoders | on | Enable/disable rotary encoders |
| Encoder Pins | see Hardware | CLK/DT GPIO for each encoder |
| Encoder Mode | Normal | Normal (independent) or Zones |
| Master Encoder | Encoder 1 | Zones mode: which encoder selects the active zone |
| Zone Steps | `20` | Total detents of the master encoder |
| Zone Count | `2` | Number of equal zones |
| Zone Reset Combo | *(none)* | Buttons held simultaneously to reset zone position to 0 |
| Encoder Debounce | `1000 µs` | Hardware encoder debounce filter |
| Encoder Press Duration | `100 ms` | Simulated button press length |
| Encoder Task Interval | `5 ms` | Encoder polling rate |
| **Advanced** | | |
| OTA Password | *(empty)* | ArduinoOTA password for WiFi firmware updates (leave empty for none) |

---

## Desktop App

A standalone native app that connects to the ESP32 over Bluetooth. No browser, no network, no extra setup.

Download the latest release from the [Releases page](https://github.com/emanuel-xavier/ESP32-steering-wheel-button-box/releases) or build from source.

### Features

- Auto-scan and connect on launch
- Auto-reconnect when the device reboots after saving
- **Debug monitor** — toggle with the Debug button to see button/matrix/encoder events in real time, including currently held buttons and a rolling event log
- **OTA firmware update** — in the Advanced section, enter OTA mode to upload a new sketch over WiFi from the Arduino IDE

### Build requirements

| Platform | Requirement |
|----------|-------------|
| Linux | `webkit2gtk` — `sudo pacman -S webkit2gtk` (Arch) / `sudo apt install libwebkit2gtk-4.1-dev libgtk-3-dev` (Ubuntu 22.04+) |
| Linux (Bluetooth) | BlueZ must be running: `sudo usermod -aG bluetooth $USER` |
| Windows cross-compile | `mingw-w64` — `sudo pacman -S mingw-w64-gcc` (Arch) / `sudo apt install gcc-mingw-w64` (Ubuntu) |
| Windows native | Go + gcc via [MSYS2](https://www.msys2.org/) |

### Build

```bash
cd desktop

# Linux only
make linux

# Windows executable (cross-compiled from Linux, requires mingw-w64)
make windows

# Both — skips Windows automatically if mingw-w64 is not installed
make all
```

Binaries are placed in `desktop/build/`.

### Run without building

```bash
cd desktop
go run .
```

---

## OTA Firmware Update

1. In the desktop app, open the **Advanced** section.
2. Optionally set an OTA password and save.
3. Click **Enter OTA Mode** — the device stops BLE and starts a WiFi access point named `ButtonBox-OTA`.
4. Connect your PC to the `ButtonBox-OTA` network.
5. In Arduino IDE: **Tools → Port → buttonbox** (the OTA network port).
6. Upload your sketch normally.

---

## Arduino Dependencies

Install via **Arduino Library Manager**:

| Library | Version |
|---------|---------|
| [Bounce2](https://github.com/thomasfredericks/Bounce2) | any |
| [ESP32-BLE-Gamepad](https://github.com/lemmingDev/ESP32-BLE-Gamepad) | 0.5.4 |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | 1.4.1 |
| [ArduinoJson](https://arduinojson.org) | ≥ 6.x |

Built-in ESP32 Arduino core libraries used: `Preferences`, `WiFi`, `ArduinoOTA`.

---

## Serial Debug Output

Enable serial logging at 115200 baud by keeping this line at the top of the `.ino`:

```cpp
#define SERIAL_DEBUG
```

Logs include: button press/release (pin number), matrix events (row/col), encoder direction and button number, master encoder position and active zone, and BLE config service startup.

---

## File Structure

```
├── ec2-button-box/
│   ├── ec2-button-box.ino   # Entry point, FreeRTOS tasks, setup
│   ├── Config.h             # Config struct, NVS load/save, JSON serialisation
│   ├── ConfigBLE.h          # Always-on BLE config GATT service (NimBLE)
│   ├── OTA.h                # WiFi soft-AP OTA mode
│   └── Encoder.h            # Rotary encoder driver (Enc namespace)
├── desktop/
│   ├── main.go              # BLE central + local HTTP server + webview window
│   ├── index.html           # Config UI (served by the desktop app)
│   ├── Makefile             # Linux and Windows build targets
│   ├── go.mod
│   └── go.sum
├── .github/
│   └── workflows/
│       └── release.yml      # Builds and publishes desktop binaries on tag push
└── docs/
    └── index.html           # GitHub Pages project page
```
