# Phase 2 — Hebrew Clock + WiFi + OTA on WeAct 4.2" / ESP32-C6

> **Status: ✅ Complete** — all T1–T9 implemented and committed. Display + WiFi + NTP + OTA + minute-aligned deep sleep working. Battery optimizations applied in a follow-up pass; see [Clock/BATTERY_OPTIMIZATION.md](Clock/BATTERY_OPTIMIZATION.md) for the running list.

Status legend: `[ ]` pending  `[~]` in progress  `[x]` done  `[!]` blocked

Goal: port the working `EInk_Clock` (LilyGo T5 V2.2 / 2.9" GxEPD2_290_BS) to this hardware
(ESP32-C6 / WeAct 4.2" GDEY042T81 / SSD1683), with WiFi, NTP, OTA, web config — and a 400×300 layout.

Reference repo: `/Users/avihaybarazany/SW_Projects/EInk_Clock/`
Working sketch:  `HelloWorld/HelloWorld.ino` (display verified, SW reset path works) — *deleted from this repo after the port; vendor reference also excluded via `.gitignore`.*

---

## Architectural decisions (locked in before starting)

1. **New sketch directory:** `Clock/Clock.ino` (sibling of `HelloWorld/`). Keep HelloWorld intact as the diagnostic.
2. **Build flow:** mirror reference — `Clock/build_scripts/{compile.sh,upload_usb.sh,upload_ota.sh,monitor.sh,config.env}`.
3. **FQBN:** `esp32:esp32:esp32c6:FlashSize=8M,PartitionScheme=default_8MB` (same as HelloWorld; the default 8MB scheme already includes OTA app1 + app2 partitions — no change needed).
4. **HW reset is broken, but partial updates work** (verified in HelloWorld). Mirror the reference's pattern: `display.init(115200, true)` on first boot / OTA-window wake, `display.init(115200, false)` on timer wakeups (controller keeps its frame). Use `display.display(true)` for minute-change partial refreshes — should be ~300 ms with no flicker. First boot does one full refresh to clear ghosting from any prior content.
5. **No battery on this build** (per HelloWorld notes — USB-powered DevKitC). Drop all `analogReadMilliVolts(35)` / battery%/low-battery alerting code from the port. Add back later if/when a battery is wired.
6. **No buttons on this build.** Drop button handling and `EXT0` maintenance-mode trigger. Replace maintenance-mode trigger with: **first 8 s after every boot**, hold WiFi up + serve web/OTA, then go to sleep. (Simple and rediscoverable without hardware buttons.)
7. **Deep sleep on C6:** ESP32-C6 deep sleep API is identical to classic ESP32 for `esp_deep_sleep()` and `RTC_DATA_ATTR`. The reference's drift-compensation logic ports as-is. `esp_sleep_enable_ext0_wakeup()` is **not** available on C6 — use `esp_deep_sleep_enable_gpio_wakeup()` instead, but only if/when we add a button. For now: timer wakeup only.
8. **Display layout:** 400×300 landscape (`setRotation(0)`). Hebrew time on two large lines (font bumped from 16pt → ~28pt or 32pt). Footer strip with IP + version + NTP status.
9. **OTA hostname:** `WeAct-Clock`. Port 3232.
10. **WiFi credentials:** keep the reference's `Preferences`-backed config (defaults to "Avihay" SSID for first boot). `/config` POST endpoint to override.

---

## File inventory — what to copy / adapt / drop

| Source file (`EInk_Clock/`)              | Action  | Target (`Clock/`)            | Notes |
|------------------------------------------|---------|------------------------------|-------|
| `EInk_Clock.ino`                         | Adapt   | `Clock.ino`                  | Strip battery + buttons; swap board pins; bigger fonts; new layout |
| `HebrewClock.{h,cpp}`                    | Copy    | `HebrewClock.{h,cpp}`        | Hebrew text engine — works as-is on any GFX target |
| `DebugLog.{h,cpp}`                       | Copy    | `DebugLog.{h,cpp}`           | As-is |
| `WebSerialHandler.{h,cpp}`               | Copy    | `WebSerialHandler.{h,cpp}`   | Pulls in `WebSocketsServer` library — install via arduino-cli |
| `boards.h`                               | Replace | `pins.h`                     | New file, just our 7 pins (SCK/MOSI/CS/DC/RST/BUSY) |
| `version.h`                              | Copy    | `version.h`                  | Reset to `0.1.0` |
| `fonts/NotoSansHebrew{14,16,18,22,24}pt.h` | Copy  | `fonts/`                     | Pick the largest two for the clock; keep 16pt for footer |
| `fonts/Cousine6pt.h`, `MeteoCons*.h`, `Monofonto*.h` | Skip | —                  | Not needed unless we add weather later |
| `tools/gen_gfx_font.py`                  | Copy    | `tools/gen_gfx_font.py`      | Useful if we need bigger Hebrew fonts |
| `build_scripts/{compile,upload,upload_usb,monitor}.sh`, `config.env` | Adapt | `Clock/build_scripts/` | FQBN + sketch name + espota.py path (3.3.5 not 2.0.17) |

---

## Task list

### T1 — Project skeleton  *(small, ~5 min)*
- [x] Create `Clock/` directory with empty `Clock.ino`, `pins.h`, `version.h` (set to `0.1.0`).
- [x] Create `Clock/build_scripts/` and copy `compile.sh`, `monitor.sh` from `HelloWorld/build_scripts/` (already targets the right FQBN). Update `config.env` with `SKETCH_NAME="Clock.ino"`.
- [x] Create `Clock/upload_usb.sh` — copy from reference and adapt port glob.

**Acceptance:** `cd Clock && ./build_scripts/compile.sh` builds an empty sketch successfully.

### T2 — Library setup  *(small, ~5 min)*
- [x] Confirm installed: `arduino-cli lib list | grep -E "GxEPD2|GFX|BusIO|ArduinoJson|WebSockets|ArduinoOTA"`.
- [x] Install missing: `arduino-cli lib install "ArduinoJson"`, `arduino-cli lib install "WebSockets"`.
  (GxEPD2, Adafruit GFX, Adafruit BusIO already verified per session notes.)
- [x] Note: `ArduinoOTA`, `WiFi`, `WebServer`, `Preferences`, `esp_task_wdt`, `esp_sntp` ship with the ESP32 core.

### T3 — WiFi + NTP  *(medium, ~20 min)*
- [x] Port `DebugLog.{h,cpp}` from reference unchanged.
- [x] Port `loadConfig()` / `saveConfig()` / `setDefaultConfig()` / `setupWiFi()` from reference.
- [x] Port `setupNTP()` with the SNTP callback pattern (don't use the broken `getLocalTime() == true` shortcut — see comments in reference).
- [x] On boot: load config → connect WiFi → sync NTP → log real time. (No display yet — verify via serial.)
- [x] If WiFi fails: log error, continue.

**Acceptance:** Serial log shows `NTP time synced: YYYY-MM-DD HH:MM:SS` matching real local time.

### T4 — Web server + OTA + maintenance window  *(medium, ~30 min)*
- [x] Port `WebSerialHandler.{h,cpp}` from reference (depends on `WebSocketsServer` from T2).
- [x] Port `setupWebServer()` + handlers (`/config`, `/status`) from reference. Drop battery fields.
- [x] Port `ArduinoOTA.setHostname("WeAct-Clock"); ArduinoOTA.begin();`.
- [x] **Replace EXT0 maintenance trigger:** on every boot, run an 8-second window where `ArduinoOTA.handle() + server.handleClient() + WebSerial.loop()` are pumped before sleeping/proceeding.
- [x] During this phase, just `delay()` after the 8 s window instead of sleeping (since deep sleep comes in T6).

**Acceptance:**
- `curl http://<ip>/status` returns JSON with version, IP, uptime, NTP status, current time.
- OTA upload (T5 script) flashes new firmware within 8 s of boot.

### T5 — OTA upload script  *(small, ~10 min)*
- [x] Copy `upload.sh` from reference to `Clock/build_scripts/`. Update `ESPOTA` path: `~/Library/Arduino15/packages/esp32/hardware/esp32/3.3.5/tools/espota.py`.
- [x] Verify path exists; if not, find it and update.
- [x] Test: `./build_scripts/compile.sh && ./build_scripts/upload.sh <device-ip>`.
- [x] Confirm `version.h` auto-increments on success.

**Acceptance:** Two consecutive OTA uploads succeed; serial shows `v0.1.0 → 0.1.1 → 0.1.2`.

### T6 — Display + Hebrew clock rendering  *(medium, ~30 min)*
- [x] Copy `HebrewClock.{h,cpp}` from reference unchanged.
- [x] Copy `fonts/NotoSansHebrew22pt.h` and `fonts/NotoSansHebrew24pt.h` from reference.
- [x] Write `pins.h` with the 7 confirmed pin mappings (copy from `HelloWorld.ino:11-17`).
- [x] In `Clock.ino`: include GxEPD2_BW for `GxEPD2_420_GDEY042T81`, render two-line Hebrew time using `HebrewClock::getTimeStrings()` + `drawHebrewText()` from real NTP time (T3).
- [x] First boot: `display.init(115200, true)` + full refresh. Minute updates: `display.display(true)` partial refresh.

**Acceptance:** Sketch boots, displays current Israel time in Hebrew with Nikud, sharp on 400×300. Subsequent minute updates are partial (no flicker).

### T7 — Deep sleep + minute-aligned wake  *(small-medium, ~15 min)*
- [x] Port `goToSleep()` from reference (sub-second precision via `gettimeofday`, drift compensation, 50 ms margin).
- [x] **Drop EXT0 wakeup setup** (no buttons, and C6 needs different API anyway).
- [x] Skip `esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ...)` — verify it's still valid on C6 (it is, per ESP-IDF docs, but no harm if dropped since we don't use fast memory).
- [x] On timer wake: `display.init(115200, false)` (controller keeps its frame), update only if minute changed, partial-refresh (`display.display(true)`).
- [x] On power-on / OTA-window wake: full init + full refresh.

**Acceptance:** Device sleeps to next minute, wakes within ±200 ms of minute boundary, partial-refreshes cleanly without flicker. Verify across 3–5 wake cycles.

### T8 — Layout polish  *(small, ~15 min)*
- [x] Verify text fits at 22pt and 24pt. Try larger fonts (`gen_gfx_font.py` if needed) since the panel is 400×300 vs reference's 296×128.
- [x] Footer strip (~40 px tall): IP left, version center, NTP status right, all in 9pt mono.
- [x] During the 8 s OTA window after a power-on boot, show a small corner indicator (e.g., a dot or "OTA").

**Acceptance:** Layout looks balanced; nothing clipped on left/right/top.

### T9 — Documentation  *(small, ~10 min)*
- [x] Write `Clock/README.md`: build, USB upload, OTA upload, web endpoints, where config lives.
- [x] Add a top-level `README.md` in `WeAct_Eink_Clock/` linking to HelloWorld + Clock + Documentation.

---

## Known risks / things to watch

- **Watchdog timeout during NTP wait** — reference uses a 600 s task WDT. Keep that. ESP32-C6 supports `esp_task_wdt_init` identically.
- **CPU frequency drop to 80 MHz** — reference does `setCpuFrequencyMhz(80)` for non-NTP wakes to halve idle current. **Verify this is supported on C6** before enabling (C6 base is 160 MHz, may not allow 80). If unsupported, skip — the C6 is already lower power than classic ESP32.
- **`btStop()` on C6** — C6 has BLE 5.0 (not classic BT). The function exists but is a no-op for classic BT. Safe to leave; consider replacing with explicit BLE-off if power matters.
- **Font size** — reference uses 16pt on a 296-wide panel; we have 400 wide. 22pt should fit comfortably; 24pt might need split-line tuning for the 11/12 o'clock long-word edge case (reference's `HebrewClock.cpp:312-322` already handles this).
- **Partial-update validation** — partial refresh is verified on the panel itself but we haven't yet tested it across a deep-sleep cycle on this hardware (where HW reset is broken). If `init(115200, false)` after wake refuses to drive partial cleanly, fall back to full refresh on every wake — the visible cost is the flicker, not correctness.
- **Power-only RTT** — without HW reset, if the panel ever locks up such that SPI alone can't recover it, only a USB unplug fixes it. Mitigation lives in Phase 3 (P-MOSFET on panel VCC); not addressed here.

---

## Out of scope (Phase 3+)

- Battery monitoring + low-battery alert (no battery wired)
- Buttons + maintenance-mode entry via button press (no buttons wired)
- P-MOSFET panel power-cycle (failsafe for stuck panel)
- Weather / Shabbat times / Hebrew calendar
- Fast partial updates (would need working HW reset OR research into init(false) without it)

---

## Execution order

T1 → T2 → T3 → T4 → T5 → T6 → T7 → T8 → T9

Rationale: skeleton + libs first, then port the entire network/OTA stack from the reference (where it already works) so subsequent iterations can be flashed wirelessly. Display + clock rendering comes once we have real time. Deep sleep last in the core feature set so the device doesn't sleep during earlier debugging.
