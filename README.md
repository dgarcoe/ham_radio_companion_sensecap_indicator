# Ham Radio Companion — SenseCAP Indicator D1L

An ESP-IDF + LVGL companion for the Seeed Studio **SenseCAP Indicator D1L**
(ESP32-S3, 4" 480×480 touch). Built bit-by-bit, starting with a UTC watch.

## Status

Bootstrap, plus first feature:

- [x] ESP-IDF project skeleton (ESP32-S3, 16 MB flash, 8 MB PSRAM)
- [x] LVGL 9 + `esp_lvgl_port` wired up (headless — see "Hardware drivers" below)
- [x] Dark + neon-accent theme
- [x] **UTC watch screen** (HH:MM with neon seconds, date, day-of-year)
- [x] NVS-backed config (callsign, locator, WiFi creds, TZ)
- [x] WiFi: STA with stored creds, fallback to AP portal on first boot / failure
- [x] First-run web portal at `http://192.168.4.1/` (callsign, locator, SSID, PSK)
- [x] SNTP UTC sync
- [x] On-device settings screen (read-only for now; web portal does edits)

## Roadmap

- [ ] On-screen keyboard so callsign / SSID can be edited on-device
- [ ] Bottom tab bar (Watch · Solar · Bands · Sensors · Settings)
- [ ] Solar / band conditions widget (hamqsl XML)
- [ ] DX cluster spots
- [ ] Indicator sensors (CO₂, TVOC, T/H) on a "shack" screen
- [ ] LoRa APRS RX (D1L only)

## Build

Requires **ESP-IDF ≥ 5.1** with the `esp32s3` target.

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

The first build pulls `lvgl/lvgl` and `espressif/esp_lvgl_port` from the
ESP component registry.

## Hardware drivers (next step)

This commit boots LVGL with a **headless display** so the WiFi / portal /
SNTP / UI logic all run end-to-end — but the panel stays dark until a
real driver is plugged in. There's a `TODO` block in `main/bsp.c`
spelling out the two options:

1. **Vendor Seeed's reference drivers.** Clone
   [`Seeed-Studio/sensecap-indicator-esp32`](https://github.com/Seeed-Studio/sensecap-indicator-esp32)
   and copy its `indicator_display` / `indicator_touch` components into
   `./components/`, then call their init from `bsp_display_start()`.
2. **Use Espressif's registry drivers.** Add
   `espressif/esp_lcd_st7701` and `espressif/esp_lcd_touch_chsc6x` to
   `main/idf_component.yml`, build the `esp_lcd_panel_handle_t`, and
   register it with `lvgl_port_add_disp()` / `lvgl_port_add_touch()`
   instead of the headless display.

Either way, only `bsp.c` changes — the UI and app code stay put.

## First boot

1. On power-up, an open WiFi AP appears: `HamCompanion-XXXX`.
2. Connect from your phone, open `http://192.168.4.1/`.
3. Fill in callsign, grid locator, your home WiFi SSID & password, hit **Save**.
4. The device reconnects to your WiFi, syncs UTC via SNTP, and the watch goes
   live with a green WiFi icon.

To reset config, erase NVS: `idf.py erase-flash`.

## Layout

```
main/
├── main.c            entry point + wiring
├── app_nvs.[ch]      persisted config
├── app_wifi.[ch]     STA + AP fallback
├── app_portal.[ch]   first-run web setup form
├── app_time.[ch]     SNTP / UTC
├── bsp.[ch]          thin wrapper over the Seeed BSP
└── ui/
    ├── ui.[ch]       screen router + status bar
    ├── ui_theme.[ch] dark + neon palette
    ├── ui_watch.c    UTC watch face
    └── ui_settings.c read-only settings view
```
