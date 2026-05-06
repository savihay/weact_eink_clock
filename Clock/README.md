# Clock

Hebrew word-clock firmware for **ESP32-C6-DevKitC-1** + **WeAct 4.2" BW** e-paper (SSD1683 / GDEY042T81, 400×300).

Renders the current time as Hebrew words with full Nikud (e.g. `שָׁלוֹשׁ וָרֶבַע אַחַר-הַצָּהֳרַיִם` for 15:15). Syncs over WiFi via NTP, redraws once per minute, sleeps in between.

---

## Build modes

Two compile-time flags in [`DebugLog.h`](DebugLog.h):

| Flag | When `1` | Use for |
|------|----------|---------|
| `DEBUG_MODE` | WiFi + OTA stay up forever, no deep sleep, no time-bounded OTA window. Display still updates per minute, driven from `loop()`. | Active development. Reflash without power-cycling. |
| `PRODUCTION_MODE` | Skip `Serial.begin()` + 300 ms post-delay, suppress `Serial.println` in DebugLog. WebSerial still works when active. | Battery-powered field deployment. |

Both default to `0`. They're mutually exclusive (`#error` if both `1`).

| Mode | Set | Behaviour |
|------|-----|-----------|
| **Default** | both `0` | Deep-sleep clock, 8 s OTA window on cold boot. |
| **Debug** | `DEBUG_MODE=1` | Always-on WiFi + OTA. **Burns battery in hours — never enable on a battery build.** |
| **Production** | `PRODUCTION_MODE=1` | Quietest, fastest cold boot, lowest current. |

> **Currently:** `DEBUG_MODE=1` (development phase — easy OTA flashing).

---

## Behaviour

### Default mode (deep-sleep)

**Cold boot (USB plug, hard reset, SW reset):**
1. Connect WiFi → NTP sync (with 2 retries; measures RTC drift between syncs)
2. Display init → render time (full refresh)
3. Hibernate panel
4. **8 s OTA window** — listens for OTA uploads, serves `/status` / `/config` / `/webserial`. Auto-extends if a transfer starts inside the window.
5. Deep sleep until the next minute boundary

**Timer wake (every 60 s):**
1. NTP-sync decision — see [NTP sync schedule](#ntp-sync-schedule) below
2. Display init (controller resume, no full wipe) → partial-refresh render (~300 ms, no flicker)
3. Deep sleep until the next minute boundary

NTP-sync wakes (~once an hour) bring up WiFi but do **not** open the OTA window — that's cold-boot only.

### Debug mode (`DEBUG_MODE=1`)

`setup()` falls through to `loop()`. WiFi connects once on boot and stays up; OTA listener stays alive forever; no deep sleep. The display still updates each minute, driven from the run loop instead of cold-boot wakes.

### Minute alignment

Sleep duration is computed in microseconds from `gettimeofday()`, then scaled by the measured RC-oscillator drift rate. Targets ~50 ms past the real minute boundary (`SLEEP_MARGIN_US` absorbs the few-ms overhead of entering deep sleep).

### NTP sync schedule

Decided at the start of every wake (cold boot or timer):

| Wake type | Sync? |
|-----------|-------|
| Cold boot | **Always** |
| Timer wake — `lastSyncTime == 0` | Sync (defensive — first-ever sync) |
| Timer wake — at top of hour AND ≥50 min since last sync | **Sync** |
| Timer wake — `≥ syncInterval × 3600 + 1800` seconds since last sync | **Fallback sync** (catches missed top-of-hour) |
| Anything else | Skip — use RTC time, WiFi stays off |

`syncInterval` = `config.ntpSyncIntervalHours` (defaults to **1**). So in practice: WiFi off ~98 % of wakes, on for one wake per hour near `:00`.

`lastSyncTime` and `driftRateUsPerSec` are stored in `RTC_DATA_ATTR` so the schedule and drift compensation survive deep sleep.

---

## OTA workflow

### With `DEBUG_MODE=1` (the easy path while iterating)

```bash
./build_scripts/upload.sh 192.168.x.y
```

Works any time — no power-cycle, no race against a window. The script builds, ping-checks the device, uploads, bumps `version.h` patch.

### With `DEBUG_MODE=0` (8 s window after cold boot)

1. Power-cycle the board (unplug/replug USB).
2. Watch serial — when you see `OTA window open for 8000 ms — listening for upload at 192.168.x.y`, run:
   ```bash
   ./build_scripts/upload.sh 192.168.x.y
   ```
3. The window auto-extends while a transfer is in progress, so an upload that starts at t=7 s still completes.
4. If you missed the window: power-cycle and try again.

---

## Layout

400×300 landscape, right-aligned (Hebrew RTL):

```
┌─────────────────────────────────────────────┐
│                                             │
│                            <Line 1 — 24pt>  │
│                                             │
│                            <Line 2 — 24pt>  │
│                                             │
│  ─────────────────────────────────────────  │
│  192.168.x.y                       NTP OK   │
│  v0.1.0                                     │
└─────────────────────────────────────────────┘
```

`HebrewClock::getTimeStrings()` (in [HebrewClock.cpp](HebrewClock.cpp)) splits the full Hebrew phrase into two visually balanced lines.

---

## Configuration

WiFi credentials and NTP settings are stored in NVS (Preferences namespace `settings`). On first boot, defaults from `setDefaultConfig()` (in [Clock.ino](Clock.ino)) are written.

| Key         | Type   | Default      |
|-------------|--------|--------------|
| `ssid`      | String | `Avihay`     |
| `password`  | String | `Tchjh1234`  |
| `syncHours` | int    | `1`          |

Override at runtime via `POST /config` (anytime in debug mode, during the OTA window in default mode):

```bash
curl -X POST http://<ip>/config \
  -H 'Content-Type: application/json' \
  -d '{"ssid":"NewSSID","password":"NewPass","ntp_sync_interval_hours":1}'
```

The device saves and reboots immediately.

To wipe NVS and restart from defaults: reflash with `esptool.py erase_flash`, or change the keys in code.

---

## HTTP endpoints

Available whenever the HTTP server is running — always in debug mode, only during the OTA window in default mode.

| Endpoint     | Method | Purpose |
|--------------|--------|---------|
| `/status`    | GET    | JSON: version, IP, uptime, free heap, current time, NTP status |
| `/config`    | GET    | JSON: SSID, IP, NTP sync interval |
| `/config`    | POST   | Body: JSON. Updates and reboots. |
| `/webserial` | GET    | Live log viewer (WebSocket on port 81; auto-reconnects across reboots) |

---

## Hardware

| Signal     | ESP32-C6 GPIO | Wire colour |
|------------|---------------|-------------|
| MOSI / DIN | 7             | yellow      |
| SCK / CLK  | 6             | green       |
| CS         | 10            | blue        |
| DC         | 18            | white       |
| RST        | 11 (bypass)   | extra jumper — **broken trace, not functional** |
| BUSY       | 20            | purple      |
| VCC        | 3V3           | red         |
| GND        | GND           | black       |

The HW reset line is electrically dead on this WeAct unit (FPC ribbon/PCB issue). GxEPD2 falls back to the SW-reset path (cmd `0x12`), which works for both full and partial updates — verified.

---

## Files

| File                              | Purpose |
|-----------------------------------|---------|
| `Clock.ino`                       | Main sketch — boot flow, render, sleep / debug-loop |
| `pins.h`                          | EPD pin mapping |
| `version.h`                       | `FIRMWARE_VERSION` — auto-incremented on every successful upload |
| `HebrewClock.{h,cpp}`             | UTF-8 + Nikud-aware GFX text engine and time-to-Hebrew converter |
| `DebugLog.{h,cpp}`                | Levelled logging (Serial + WebSerial); holds `DEBUG_MODE` / `PRODUCTION_MODE` flags |
| `WebSerialHandler.{h,cpp}`        | WebSocket-based browser log viewer |
| `fonts/NotoSansHebrew{22,24}pt.h` | GFX-format Hebrew bitmap fonts |
| `build_scripts/`                  | Compile / USB upload / OTA upload / monitor / shared `bump_version.sh` |
| `COMMANDS.md`                     | One-page command reference |
| `BATTERY_OPTIMIZATION.md`         | Power analysis + applied/pending optimizations + board-side mods |

---

## Build & flash

See [COMMANDS.md](COMMANDS.md) for the full reference. Quickstart:

```bash
./build_scripts/upload_usb.sh    # builds + flashes via USB, bumps version.h
./build_scripts/monitor.sh       # serial output
```

Once the device is on WiFi:

```bash
./build_scripts/upload.sh 192.168.x.y    # builds + flashes via OTA, bumps version.h
```

Both upload scripts build first by default. Pass `--no-build` to skip compilation.

---

## Out of scope

- **No battery / no buttons** in this build. Phase 3 candidates.
- **No weather / Hebrew calendar / Shabbat times.** Phase 3+.
- **HW reset bypass** — accepted as a permanent property of this WeAct unit; SW reset path is sufficient.
