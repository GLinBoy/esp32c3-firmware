# Firmware: BLE Provisioning — Development Notes

This document records the problems hit while adding BLE Wi-Fi provisioning to the
ESP32-C3 firmware and how each was solved. It is a companion to the main
[`README.md`](../README.md), which describes the final behavior and the BLE protocol.

---

## 1. Device never appeared in any Bluetooth scan

**Symptom:** the ESP32-C3 was not visible in the phone's Bluetooth settings **or**
the app. The serial log showed no `Advertising started` line.

**Root cause:** the primary BLE advertisement exceeded the hard 31-byte limit.
The firmware advertised flags (3 B) + TX power (3 B) + a complete 128-bit service
UUID (18 B) + the device name `ESP32C3-xxxxxx` (14 B) = **38 bytes**.
`ble_gap_adv_set_fields()` rejected it with `BLE_HS_EMSGSIZE`, so
`ble_gap_adv_start()` was never reached and the device silently never advertised.

**Fix** (`src/ble_prov.c`, `start_advertising()`): keep the primary advertisement
at or under 31 bytes — flags (3 B) + 128-bit service UUID (18 B) + the short name
`ESP32C3` (9 B) = **30 bytes** — and move the full unique name `ESP32C3-xxxxxx`
into the **scan response** (`ble_gap_adv_rsp_set_fields()`).

**Lesson:** the 31-byte ADV payload limit is easy to trip; keep the service UUID
in the primary advertisement (needed for client-side discovery filters) and push
anything long into the scan response.

---

## 2. Device advertised but had no name

**Symptom:** the device was visible in scans but showed no name (or the wrong
default name), and the log printed the name as empty/`nimble`.

**Root cause:** this build has `CONFIG_BT_NIMBLE_STATIC_TO_DYNAMIC=y`. With that
setting, `ble_svc_gap_init()` (called from `gatt_svr_init()`) re-initializes the
device name from the configured default (`CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME`,
default `"nimble"`), **overwriting** any name set *before* `gatt_svr_init()`.
The code called `ble_svc_gap_device_name_set()` before service init, so the name
was always reset.

**Fix:** call `ble_svc_gap_device_name_set()` **after** `gatt_svr_init()`, and
again inside `on_sync()` as a safety net.

**Lesson:** when `BLE_STATIC_TO_DYNAMIC` is on, set the GAP device name after the
GAP service is initialized, matching the order used in the reference
`bleprph` example.

---

## 3. Device stopped being discoverable after a phone connected

**Symptom:** after connecting the device once from the phone's Bluetooth
settings, the app could no longer find it.

**Root cause:** `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` was 1, and NimBLE stops
advertising as soon as a connection is established. The OS Bluetooth settings held
the single connection, so the device stopped advertising and there was no slot for
the app to connect anyway.

**Fix:**
- Raise the limit: `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2`.
- Keep advertising after a connection is established (restart advertising in the
  `BLE_GAP_EVENT_CONNECT` handler, and drop the `s_connected` guard from
  `start_advertising()`), so the device stays findable while another client holds
  a connection.

---

## 4. No default Wi-Fi credentials (provisioning on first boot)

**Requirement:** the firmware must not ship with Wi-Fi credentials in the source;
a fresh device should boot straight into BLE provisioning.

**Change:** removed `WIFI_SSID` / `WIFI_PASS` compile-time defines.
`load_wifi_creds()` now reads **only** NVS. On first boot nothing is stored, so
the device enters provisioning mode immediately (LED blinks, BLE advertises).

**Note for testing:** NVS survives re-flashing. If a device previously had valid
credentials stored (e.g. from an earlier provisioning test), it will connect to
Wi-Fi instead of provisioning. Force it back into setup mode with:

```bash
pio run -t erase     # wipes NVS (stored Wi-Fi credentials and LED state)
pio run -t upload
```

---

## 5. Build failed: app image larger than the flash partition

**Symptom:** after enabling BLE/NimBLE, `pio run` failed with:

```
Flash: [==========] 111.8% (used 1172048 bytes from 1048576 bytes)
```

**Root cause:** the board has **2 MB flash** and the default ESP-IDF partition
table gives the app only a 1 MB factory partition. BLE + Wi-Fi + MQTT firmware is
~1.17 MB.

**Fix:** added `partitions_2mb.csv` — NVS, phy_init, and a single 1.75 MB factory
app partition (no OTA) — and wired it up in `platformio.ini`:

```ini
board_build.partitions = partitions_2mb.csv
```

---

## 6. WiFi + BLE coexistence

**Symptom:** none (this one worked out of the box) — recorded for reference.

On the ESP32-C3, enabling both Wi-Fi and BLE produces:
`CONFIG_ESP_COEX_ENABLED=y` and `CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y` in the
generated `sdkconfig`. The stack arbitrates the shared radio automatically; no
code or config was needed.

---

## 7. Wi-Fi scan results never reached the app (notifications went to the wrong connection)

**Symptom:** the Wi-Fi scan ran on the device (`Scan done: 13 APs` in the log) but
the app received no `scan_result`/`scan_done` messages and timed out. The log showed
`scan_result send failed: ESP_FAIL` from `ble_prov_send`.

**Root cause:** with advertising kept on and `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2`,
the phone's **OS Bluetooth settings** can hold a second connection. The code sent
notifications to `s_conn_handle` — the *most recently connected* handle. When the
OS connected (or its connection lingered), the handle pointed at a client that
never enabled notifications, so `ble_gatts_notify_custom()` failed for the app.

**Fix** (`src/ble_prov.c`): track **which connection subscribed** to the TX
characteristic (`s_notify_conn_handle`, recorded in `BLE_GAP_EVENT_SUBSCRIBE` and
cleared on unsubscribe/disconnect) and send notifications to that handle only.
Also, `ble_prov_send()` no longer depends on the global `s_connected` flag, which
is unreliable with more than one connection.

---

## 8. Wi-Fi scan results dropped mid-burst (BLE link overflow)

**Symptom:** the app received most `scan_result` lines but the last few — and
`scan_done` — were lost, so the list never finished and the app timed out. The
device log showed `scan_result send failed: ESP_FAIL` for the tail of the burst.

**Root cause:** `handle_scan_done()` sent all ~14 results back-to-back in a tight
loop. BLE ATT notifications transmit one packet per connection event; a rapid
burst overflows the controller's TX queue and `ble_gatts_notify_custom()` starts
failing.

**Fix:**
- Pace the burst: a `vTaskDelay(pdMS_TO_TICKS(20))` between each result
  (`src/main.c`, `handle_scan_done()`).
- Retry on busy: `ble_prov_send()` now retries a failed notify up to 20 times
  with a 10 ms delay instead of giving up (`src/ble_prov.c`).

---

## 9. Controlling debug output

The firmware logs are ESP-IDF logs (`ESP_LOGI`/`ESP_LOGW`/`ESP_LOGE`) plus NimBLE
host logs. They are useful during development and are kept in the code.

- **Default verbosity:** controlled by `CONFIG_LOG_DEFAULT_LEVEL` in
  `sdkconfig.defaults` (default `INFO`). Set to `WARNING` or `ERROR` for quieter
  production builds.
- **NimBLE host logs:** controlled by the NimBLE log level config (e.g.
  `CONFIG_BT_NIMBLE_LOG_LEVEL`); the `NimBLE: GAP procedure initiated: ...`
  lines come from here.
- To see only the application's key messages, filter the monitor output, e.g.:

```bash
pio device monitor | grep -E "led_mqtt|ble_prov"
```

---

## 10. App reconnect: BLE connects but service discovery returns 0 services

**Symptom:** re-provisioning a device that the phone had connected to before
(the app's *Change Wi-Fi* flow): the phone connects to the device (GATT
success, MTU negotiated), but `discoverServices()` returns **0 services**, so
the app reports *"Provisioning service not found on device"*. First-time setup
of a fresh device always discovered the NUS service fine.

**Root cause:** Android caches the GATT database per device address. When a
phone reconnects to a peripheral it has connected to before, Android can return
a stale (or empty) cached service list even though a fresh discovery was
requested. This is a well-known Android behaviour, not a firmware problem — the
ESP32-C3's GATT server is intact and works on a fresh connection.

**Fix** (Flutter app, `ble_provisioning_service.dart`): when service discovery
does not find the provisioning service, call `clearGattCache()`
(`BluetoothGatt.refresh()`) and retry discovery once before giving up.

---

## How to verify the device is advertising

From the USB-JTAG console, a healthy provisioning boot looks like:

```
led_mqtt: Wi-Fi credentials NOT set (BLE provisioning mode)
ble_prov: BLE provisioning initialized, device name=ESP32C3-70548c
led_mqtt: Wi-Fi unavailable - starting BLE provisioning
NimBLE: GAP procedure initiated: advertise;
ble_prov: Advertising started as 'ESP32C3-70548c'
```

If you instead see `Wi-Fi credentials loaded` / `got IP`, the device connected to
a stored network and is not in setup mode — see §4.
