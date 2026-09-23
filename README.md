# ESP32-C3 MQTT LED Controller

A simple IoT project that lets you turn an LED on an ESP32-C3 board on/off remotely
over MQTT. Each device identifies itself with a unique ID (derived from its MAC
address), announces itself on a registration topic when it boots, and listens for
ON/OFF commands on its own personal topic. This lets you control multiple devices
independently without any manual per-device configuration.

This document is written for two audiences: a **quick summary** for non-technical
readers, and **full technical details** below for anyone maintaining or debugging
the system.

---

## For Non-Technical Readers

Think of each device as a lightbulb with a name tag. When you plug it in, it:

1. Connects to Wi-Fi (using credentials saved on the device, or a compile-time
   default).
2. Shouts out its name tag on a shared "roll call" channel so anyone listening
   knows it's alive.
3. Listens quietly on its own personal channel, waiting for someone to tell it
   "turn on" or "turn off."
4. Remembers whatever it was last told, even if it loses power — it won't forget
   or reset to a random state.
5. If it can't find Wi-Fi, its light blinks instead of just glowing solid, so you
   can tell at a glance that it's having connection trouble rather than assuming
   it's broken.
6. **If Wi-Fi keeps failing, it starts advertising itself over Bluetooth and
   waits for the phone app.** The app shows nearby networks, you pick one and
   enter its password, and the device saves it and connects — no USB cable, no
   re-flashing needed. (This is the "provisioning" feature described below.)

To control a device, you (or any support tool) send a plain text message —
literally the word `ON` or `OFF` — to that device's personal channel. You don't
need to touch the device physically to control it once it's online.

If a device seems unreachable, the most common causes are: it lost Wi-Fi, its
Wi-Fi password was typed wrong, or (on this specific hardware) its antenna needs
a firm physical connection — see the [Known Hardware Issue](#known-hardware-issue-antenna)
section below.

---

## System Overview

```
[ESP32-C3 device] <---Wi-Fi---> [Router/AP] <---Internet---> [MQTT Broker] <---> [You / control script]
```

- **Broker used for testing:** `test.mosquitto.org:1883` — a free, public, unencrypted
  Eclipse Mosquitto test server. No account or signup required. Suitable for
  development and testing only, not for production or sensitive data, since it's
  a shared public server with no authentication or privacy.
- **Firmware:** ESP-IDF (via PlatformIO), running on an ESP32-C3-DevKitM-1 board
  (or compatible ESP32-C3 boards, including "Super Mini" style modules).
- **Control:** any MQTT client can publish `ON`/`OFF` to a device's topic. A
  ready-made Python test script is included for this.

---

## BLE Wi-Fi Provisioning

When the device cannot reach Wi-Fi — on first boot (no credentials are stored
in the firmware, so every new device starts here), when the saved password is
wrong, or after a few failed connect attempts — it starts advertising a
Bluetooth Low Energy service and waits for the phone app. The LED keeps blinking
the whole time so you can see it is in setup mode.

### GATT service

| Element | UUID |
|---|---|
| Service (Nordic UART Service style) | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| RX characteristic (phone → device, write) | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| TX characteristic (device → phone, notify) | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |

The BLE device advertises as `ESP32C3-<last 3 bytes of MAC>` (e.g.
`ESP32C3-a9e0f3`). The app connects, enables notifications on the TX
characteristic, then exchanges **one JSON message per line** (a message may span
several notifications; the phone buffers until it sees a `\n`). The phone writes
messages to RX; the device sends responses on TX.

### Phone → device messages

| Message | Purpose |
|---|---|
| `{"type":"scan"}` | Start a Wi-Fi scan. Results arrive as `scan_result` messages followed by a `scan_done` summary. |
| `{"type":"connect","ssid":"MyNetwork","password":"secret"}` | Save these credentials to NVS and connect. Device stops advertising BLE. |

### Device → phone messages

| Message | Meaning |
|---|---|
| `{"type":"hello","device_id":"<id>","wifi":"provisioning"\|"connected"}` | Sent right after the phone subscribes to notifications. `<id>` is the 12-hex-char Wi-Fi MAC used for MQTT topics. |
| `{"type":"status","wifi":"...","detail":"..."}` | Status change: `provisioning`, `scanning`, `scan_error`, `connecting`, `error`, `connected`. |
| `{"type":"scan_result","ssid":"...","rssi":-45,"auth":"WPA2","channel":6}` | One per access point found. `auth` is one of `OPEN`, `WEP`, `WPA`, `WPA2`, `WPA/WPA2`, `WPA2-ENT`, `WPA3`, `WPA2/WPA3`, `UNKNOWN`. |
| `{"type":"scan_done","count":N}` | Signals the end of a scan; the app should stop waiting. |

### Flow

1. Device fails to connect (or has no stored credentials) → starts BLE advertising.
2. Phone scans, finds `ESP32C3-...`, connects, enables notifications, receives `hello`.
3. Phone sends `{"type":"scan"}`; device scans and streams `scan_result` lines, then `scan_done`.
4. Phone sends `{"type":"connect",...}` with the chosen network + password.
5. Device saves credentials to NVS, stops advertising, connects. On success the
   LED holds steady and MQTT starts; on repeated failure it re-enters
   provisioning mode.

### Security note

This provisioning channel is **unencrypted** (no BLE pairing/bonding) for
simplicity in this first step. Anyone within radio range could connect while the
device is in provisioning mode. The device only advertises while it is
disconnected from Wi-Fi, and stops advertising as soon as it is provisioned, so
the exposure window is limited. Add pairing (SMP/encryption) before deploying
this in a sensitive setting.

---

## Topic Structure

| Topic | Published by | Purpose |
|---|---|---|
| `devices/registry` | Device → broker | Retained JSON announcement on boot: device ID, its command topic, and current LED state |
| `devices/<device_id>/led/set` | You → device | Send `ON` or `OFF` (plain text) to control that specific device's LED |
| `devices/<device_id>/status` | Device → broker | `online` on connect; automatically set to `offline` by the broker if the device disconnects unexpectedly (Last Will) |

`<device_id>` is a 12-character hex string derived from the device's Wi-Fi MAC
address (e.g. `a1b2c3d4e5f6`), so every physical board gets a distinct, stable
identity automatically — the same firmware can be flashed to any number of
devices without editing anything per-unit.

---

## Firmware Behavior

### Startup sequence
1. Restore last-known LED state from persistent storage (survives power loss).
2. Start blinking the LED (indicates "not yet connected").
3. Load Wi-Fi credentials from NVS (or compile-time defaults). If none exist,
   skip straight to BLE provisioning (step 5).
4. Connect to Wi-Fi; once connected: stop blinking, apply the restored LED
   state, connect to MQTT.
5. If Wi-Fi fails to connect (no credentials, wrong password, or after 3 failed
   attempts), start BLE advertising and wait for the phone app to provision
   new credentials.
5. Once MQTT connects: subscribe to the device's own command topic and publish
   a registration message to `devices/registry`.

### While disconnected from Wi-Fi
- The LED blinks continuously (approx. every 300ms) as a visual "trying to
  connect" indicator.
- Reconnection attempts use **exponential backoff**: 1s, 2s, 4s, 8s... up to a
  60-second maximum between attempts. This is intentional — retrying instantly
  and continuously was causing the device to run hot; backing off the retry
  rate significantly reduces power draw and heat during prolonged outages.

### While connected
- The LED shows whatever state was last commanded (or restored from storage).
- Any `ON`/`OFF` message received on the device's topic immediately updates
  the LED and saves the new state to persistent storage, so it survives a
  future reboot or power cycle.

---

## Hardware Setup

- **Board:** ESP32-C3-DevKitM-1 (or compatible ESP32-C3 module)
- **LED:** connected to GPIO8 (`LED_GPIO` in the code) — this is commonly the
  onboard LED on many ESP32-C3 dev boards
- **LED polarity:** many onboard LEDs on these boards are **active-low**
  (GPIO output `0` = LED on, `1` = LED off). This is already configured
  correctly in the firmware (`LED_ON_LEVEL` / `LED_OFF_LEVEL`). If your specific
  board's LED behaves backwards, swap these two values.

### Known Hardware Issue: Antenna

Some ESP32-C3 boards — particularly compact "Super Mini" style modules — have a
known PCB antenna design issue where the Wi-Fi connection is weak or fails
entirely unless something (like a finger) touches near the antenna to act as a
grounding/capacitive reference. Symptoms include:

- Device won't connect to Wi-Fi at all, or only connects intermittently.
- Touching the end of the board near the antenna suddenly makes it connect.
- Device runs unusually hot while repeatedly trying (and failing) to connect.

This is a documented hardware characteristic on some boards/batches, not a bug
in this firmware or a router misconfiguration. Mitigations, in order of
effectiveness:

1. **Software mitigation (included in this firmware):** reduced maximum Wi-Fi
   transmit power (`esp_wifi_set_max_tx_power`). Counterintuitively, lower
   power sometimes improves reliability on a poorly matched antenna, since an
   overdriven mismatched antenna can distort its own signal.
2. **Physical fix:** reposition or re-solder the antenna trace/connector
   slightly off the edge of the board, or attach a short wire extension to
   the antenna feed point. This has fully resolved the issue for many users
   on affected boards.
3. If neither helps, consider sourcing a different revision/batch of the same
   board, or a board with an external antenna connector (u.FL) instead of an
   onboard PCB antenna.

---

## Software Setup (PlatformIO + ESP-IDF)

### Project files
- `src/main.c` — firmware source (Wi-Fi + MQTT + provisioning logic)
- `src/ble_prov.c` / `src/ble_prov.h` — BLE GATT provisioning service (NimBLE)
- `src/CMakeLists.txt` — declares component dependencies (`mqtt`, `esp_wifi`,
  `esp_netif`, `esp_event`, `nvs_flash`, `driver`, `bt`)
- `src/idf_component.yml` — pulls the `mqtt` component from the official
  ESP-IDF Component Registry (needed because some PlatformIO-packaged versions
  of ESP-IDF ship with an incomplete built-in `mqtt` component)
- `platformio.ini` — board/environment configuration
- `partitions.csv` — OTA-enabled partition table for 4MB-flash boards (two
  1700K app slots, `ota_0` + `ota_1`, so firmware can be updated over the air)
- `tools/publish_ota.py` — publishes an OTA notification to `devices/ota`
- `sdkconfig.defaults` — persisted configuration defaults (console output
  routing, MQTT buffer size, BLE/NimBLE enablement)

### Before building
There are **no default Wi-Fi credentials in the source**. On first boot the
device has nothing to connect with, so it immediately enters BLE provisioning
mode and waits for the phone app (the LED blinks). Once you send it Wi-Fi
credentials through the app, they are saved to NVS and reused on every later
boot. To change the network later, see the provisioning section above.

### Build and flash
In VS Code with the PlatformIO extension installed, use **Upload and Monitor**
(or from a terminal in the project folder):
```bash
pio run --target fullclean   # clean rebuild, recommended after any config change
pio run --target upload
pio device monitor
```

> **Flash size:** this project targets **4MB flash**, so it uses a custom
> OTA-capable partition table (`partitions.csv`, set via `board_build.partitions`
> in `platformio.ini`) with two 1700K app slots for A/B OTA updates. The board
> must be configured with `board_build.flash_size = 4MB`. If your board has less
> flash, you must adjust the partition table and flash size together.

### If the build fails with a missing config/component error
If you change `sdkconfig.defaults` and rebuild but nothing seems to change, the
per-environment generated config file (`sdkconfig.esp32-c3-devkitm-1`) may be
stale. Delete it and do a full clean rebuild:
```bash
rm sdkconfig.esp32-c3-devkitm-1
pio run --target fullclean
pio run
```

---

## Testing From Your Computer (Python)

A ready-made script (`mqtt_test_client.py`) lets you watch for devices and send
commands without needing any other tool.

### Setup
```bash
python -m venv .venv
source .venv/bin/activate        # on Windows: .venv\Scripts\activate
pip install paho-mqtt
```

### Run
```bash
python mqtt_test_client.py
```

### Usage
```
> list              # show all devices seen so far, with an index number
> 0 on               # turn ON the LED on device with index 0
> 0 off              # turn OFF the LED on device with index 0
> quit               # exit
```

New devices appear automatically in the `list` output within a few seconds of
powering them on and connecting to Wi-Fi — no manual setup needed per device.

---

## Troubleshooting Guide

| Symptom | Likely Cause | Fix |
|---|---|---|
| No serial output after flashing | Console output routed to UART0 instead of USB | Set `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` in `sdkconfig.defaults`, delete generated `sdkconfig.<env>` file, rebuild |
| Build fails: `mqtt_client.h: No such file or directory` | `mqtt` component not declared as a dependency, or built-in component is broken/empty | Add `mqtt` to `REQUIRES` in `src/CMakeLists.txt`; if that alone doesn't work, add `src/idf_component.yml` to pull it from the Component Registry instead |
| Wi-Fi connects then immediately disconnects, repeating (`auth -> init` / `assoc -> init` in logs) | Router/AP security or channel misconfiguration (seen with some MikroTik CAPsMAN setups), or weak/mismatched antenna | Check AP security profile (avoid mixed EAP+PSK, consider disabling PMF/management-protection), or address antenna issue above |
| LED behaves backwards (ON command turns it off, and vice versa) | LED wired active-low or active-high, opposite of firmware assumption | Swap `LED_ON_LEVEL` / `LED_OFF_LEVEL` values in `main.c` |
| LED is solid on/off while Wi-Fi is disconnected instead of blinking | Older firmware version without dedicated blink task | Use the current firmware version — includes a separate blink task that runs only while disconnected |
| LED forgets its state after a power cycle | Older firmware version without persistent storage | Use the current firmware version — LED state is saved to NVS (non-volatile storage) on every change |
| Device runs hot, especially when it can't connect to Wi-Fi | Immediate/rapid reconnect retry loop, or antenna issue causing repeated failed high-power transmit attempts | Current firmware uses exponential backoff (1s → 60s max) between retries, and reduced max TX power; also see antenna section above |
| Device only connects when touched near the antenna | Known PCB antenna design issue on some ESP32-C3 boards | See [Known Hardware Issue](#known-hardware-issue-antenna) section |

---

## Security Notes (Read Before Any Real-World Use)

This setup is intended for **prototyping and testing only**:

- `test.mosquitto.org` is a public, shared, unauthenticated broker. Anyone in
  the world can subscribe to the same topics if they guess or discover your
  topic names, and there is no encryption on the connection (`mqtt://`, not
  `mqtts://`).
- Wi-Fi credentials provisioned over BLE are stored in plain text in NVS
  (on-chip flash); they are not encrypted at rest.

Before using this in any production, home-security-relevant, or otherwise
sensitive context, you should:
- Move to a private broker (self-hosted Mosquitto, or a managed service like
  HiveMQ Cloud or EMQX Cloud) with username/password or certificate
  authentication.
- Use TLS (`mqtts://`, typically port 8883) instead of plain `mqtt://`.
- Avoid hardcoding Wi-Fi credentials in source control; use the built-in BLE
  provisioning flow so credentials live only in device NVS.

---

## Glossary (for non-technical readers)

- **MQTT** — a lightweight messaging protocol where devices "publish" short
  messages to named "topics," and other devices/programs "subscribe" to those
  topics to receive them. Think of it like a set of labeled radio channels.
- **Broker** — the server that routes MQTT messages between publishers and
  subscribers. Devices and control tools both connect to the same broker.
- **Topic** — a named channel, e.g. `devices/a1b2c3d4e5f6/led/set`.
- **Retained message** — a message the broker remembers and immediately
  re-sends to any new subscriber, even if they connect after it was
  originally sent.
- **NVS (Non-Volatile Storage)** — a small persistent storage area on the
  chip that keeps its contents even when power is removed, used here to
  remember the LED's last commanded state.
- **GPIO** — General Purpose Input/Output; a physical pin on the chip that
  can be set high or low to control something like an LED, or read a sensor.

---

## CI/CD Pipeline

This project uses GitHub Actions for continuous integration and releases.

### Workflows

- **`firmware-ci.yml`**: Builds firmware on every push/PR to verify compilation
- **`firmware-release.yml`**: Builds and publishes firmware binaries on semver tags

### Creating a Release

```bash
git tag v1.1.0
git push origin v1.1.0
```

GitHub Actions will:

1. Build firmware with version `1.1.0` injected
2. Generate SHA-256 checksum
3. Create GitHub Release with `firmware.bin`, `partitions.bin`, and checksum

### OTA Updates

Devices automatically check for updates via MQTT topic `devices/ota`:

- **Push notification**: Backend publishes update metadata to `devices/ota` (retained)
- **Boot-time check**: Device checks `devices/ota` on every boot as fallback

To trigger OTA manually:

```bash
python tools/publish_ota.py 1.1.0 https://github.com/GLinBoy/esp32c3-firmware/releases/download/v1.1.0/firmware.bin <sha256>
```

**Important**: MQTT broker URL is configurable via NVS, default is `test.mosquitto.org:1883`.
See `PLAN_ASSISTANCE_CONTROL.md` Phase A for migration to private authenticated broker (EMQX/HiveMQ).

---

## Partition Table

**WARNING**: This project uses a custom OTA-enabled partition table (`partitions.csv`) with two 1700K app slots.

If you modify the partition table:

1. Delete `.pio/build` directory
2. Delete generated `sdkconfig` file
3. Run `pio run --target fullclean`
4. Rebuild: `pio run`

Incremental builds do NOT reliably pick up partition table changes and will cause silent boot failures.
