# Clock

Hebrew word-clock firmware for **ESP32-C6-DevKitC-1** + **WeAct 4.2" BW** e-paper (SSD1683 / GDEY042T81, 400×300).

Renders the current time as Hebrew words with full Nikud (e.g. `שָׁלוֹשׁ וָרֶבַע אַחַר-הַצָּהֳרַיִם` for 15:15). Syncs over WiFi via NTP, redraws once per minute, and sleeps in between.

## Behaviour

### Cold boot (USB plug, hard reset, SW reset)

1. Connect WiFi
2. NTP sync (with retries; measures RTC drift between syncs)
3. Display init (full panel wipe) → render time (full refresh)
4. **8 s OTA window** — listens for OTA uploads, serves `/status` / `/config` / `/webserial`
5. Deep sleep until the next minute boundary

### Timer wake (every 60 s)

1. Skip WiFi unless an NTP re-sync is due
   - At top of every hour (if ≥50 min since last sync)
   - As a fallback if more than `ntpSyncIntervalHours + 30 min` has passed
2. Display init (controller resume, no full wipe) → partial-refresh render
3. Deep sleep until the next minute boundary

NTP-sync wakes (~once an hour) bring up WiFi but **don't** open the OTA window — that's cold-boot only.

### Minute alignment

Sleep duration is computed in microseconds from `gettimeofday()`, then scaled by the measured RC-oscillator drift rate. Targets ~50 ms past the real minute boundary (the `SLEEP_MARGIN_US` constant absorbs the few-ms latency of entering deep sleep).

## OTA workflow

OTA only listens during the 8 s window after a cold boot. The intended workflow:

1. Power-cycle the board (unplug/replug USB).
2. Wait ~3 s — WiFi connects, NTP syncs, panel renders.
3. While the OTA window is open, run `./build_scripts/upload.sh <ip>`.
4. If you miss the window, just power-cycle again.

This is a deliberate trade-off: there are no buttons on this build to trigger an OTA-mode wake on demand, and keeping WiFi up every minute would waste power.

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

`HebrewClock::getTimeStrings()` (in `HebrewClock.cpp`) splits the full Hebrew phrase into two visually balanced lines.

## Configuration

WiFi credentials and NTP settings are stored in NVS (Preferences namespace `settings`). On first boot, defaults from `setDefaultConfig()` (in `Clock.ino`) are written.

| Key         | Type   | Default   |
|-------------|--------|-----------|
| `ssid`      | String | `Avihay`  |
| `password`  | String | `Tchjh1234` |
| `syncHours` | int    | `1`       |

Override at runtime via `POST /config` (during the OTA window):

```bash
curl -X POST http://<ip>/config \
  -H 'Content-Type: application/json' \
  -d '{"ssid":"NewSSID","password":"NewPass","ntp_sync_interval_hours":1}'
```

The device saves and reboots immediately.

To wipe NVS and restart from defaults: reflash with `esptool.py erase_flash`, or change the keys in code.

## HTTP endpoints (only during OTA window)

| Endpoint        | Method | Purpose |
|-----------------|--------|---------|
| `/status`       | GET    | JSON: version, IP, uptime, free heap, current time, NTP status |
| `/config`       | GET    | JSON: SSID, IP, NTP sync interval |
| `/config`       | POST   | Body: JSON. Updates and reboots. |
| `/webserial`    | GET    | Live log viewer (WebSocket-backed at port 81) |

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

The HW reset line is electrically dead on this WeAct unit (FPC ribbon/PCB issue). GxEPD2 falls back to the SW-reset path (cmd `0x12`), which works for both full and partial updates.

## Files

| File                            | Purpose |
|---------------------------------|---------|
| `Clock.ino`                     | Main sketch — boot flow, render, sleep |
| `pins.h`                        | EPD pin mapping |
| `version.h`                     | `FIRMWARE_VERSION` — auto-incremented by `upload.sh` |
| `HebrewClock.{h,cpp}`           | UTF-8 + Nikud-aware GFX text engine and time-to-Hebrew converter |
| `DebugLog.{h,cpp}`              | Levelled logging (Serial + WebSerial) |
| `WebSerialHandler.{h,cpp}`      | WebSocket-based browser log viewer |
| `fonts/NotoSansHebrew{22,24}pt.h` | GFX-format Hebrew bitmap fonts |
| `build_scripts/`                | Compile / USB upload / OTA upload / monitor |
| `COMMANDS.md`                   | One-page command reference |
| `BATTERY_OPTIMIZATION.md`       | Power analysis + optimization opportunities |

## Build & flash

See [COMMANDS.md](COMMANDS.md) for the full command reference. Quickstart:

```bash
./build_scripts/upload_usb.sh    # builds + flashes via USB
./build_scripts/monitor.sh       # serial output
```

Once the device is on WiFi, OTA flashes are one command:

```bash
./build_scripts/upload.sh 192.168.x.y    # builds + flashes via OTA
```

Both upload scripts build first by default; pass `--no-build` to skip.

## Out of scope

- **No battery / no buttons** in this build. Phase 3 candidates.
- **No fast-partial-from-cold-boot** workaround for the broken HW reset — every cold boot does a full refresh (~2 s flicker). Subsequent timer wakes use partial.
- **No weather / Hebrew calendar / Shabbat times.** Phase 3+.
