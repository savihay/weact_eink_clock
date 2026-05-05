# Commands & scripts

All paths are relative to `Clock/`. Scripts live in `build_scripts/`.

## Build & flash

| Command | What it does |
|---------|--------------|
| `./build_scripts/compile.sh` | Compiles the sketch with `arduino-cli` for the C6 FQBN. Output: `build/Clock.ino.bin`. |
| `./build_scripts/upload_usb.sh` | Auto-detects USB port (`/dev/cu.usbmodem*` then `/dev/cu.usbserial-*`) and flashes. Hard-resets at the end. |
| `./build_scripts/upload.sh <ip>` | OTA flash via `espota.py`. **Must run within the 8 s OTA window after a cold boot.** Auto-bumps `version.h` patch number on success. |
| `./build_scripts/monitor.sh` | Opens the serial monitor at 115200 baud on the same port USB upload uses. Ctrl-C to exit. |

### Common combinations

Build + USB upload + watch:
```bash
./build_scripts/compile.sh && ./build_scripts/upload_usb.sh && ./build_scripts/monitor.sh
```

Build + OTA upload (replace IP with what `monitor.sh` showed last boot):
```bash
./build_scripts/compile.sh && ./build_scripts/upload.sh 192.168.31.180
```

Watch a live OTA cycle (terminal A monitoring, terminal B uploading):
```bash
# Terminal A
./build_scripts/monitor.sh

# Terminal B (within 8 s of cold boot)
./build_scripts/upload.sh 192.168.31.180
```

## Configuration

`build_scripts/config.env`:

```bash
PROJECT_DIR=".."
SKETCH_NAME="Clock.ino"
FQBN="esp32:esp32:esp32c6:FlashSize=8M,PartitionScheme=default_8MB"
DEVICE_IP=""    # optional: default IP for upload.sh
```

If you set `DEVICE_IP` here, you can run `./build_scripts/upload.sh` without an argument.

## HTTP endpoints (during the 8 s OTA window)

```bash
# Get device status
curl http://<ip>/status | jq

# Get current config
curl http://<ip>/config | jq

# Update WiFi credentials (device reboots after)
curl -X POST http://<ip>/config \
  -H 'Content-Type: application/json' \
  -d '{"ssid":"NewSSID","password":"NewPass"}'

# Update NTP sync interval (hours)
curl -X POST http://<ip>/config \
  -H 'Content-Type: application/json' \
  -d '{"ntp_sync_interval_hours":2}'
```

Live browser log viewer:
```
http://<ip>/webserial
```
(Page polls a WebSocket on port 81 — auto-reconnects if you keep the tab open across reboots.)

## Useful `arduino-cli` commands (one-offs)

```bash
# List installed libraries
arduino-cli lib list | grep -E "GxEPD2|GFX|BusIO|ArduinoJson|WebSockets"

# Update the ESP32 core
arduino-cli core update-index
arduino-cli core upgrade esp32:esp32

# Wipe entire flash (also clears NVS — config is lost)
esptool.py --port /dev/cu.usbserial-0001 erase_flash

# Show partition info from the built binary
arduino-cli compile --fqbn esp32:esp32:esp32c6 --show-properties Clock.ino | grep partition
```

## Troubleshooting

| Symptom | Try |
|---------|-----|
| `upload_usb.sh` says "no USB serial port found" | Check the cable is in the **USB** port, not UART. Run `ls /dev/cu.*`. |
| `upload.sh` says "device not reachable" | Confirm IP via serial; device may have already left the OTA window — power-cycle. |
| OTA upload stalls at 0 % | OTA window may have closed mid-upload. Power-cycle and start `upload.sh` 1–2 s after WiFi connect log appears. |
| Display blank after reflash | Power-cycle. The first refresh after a flash always does a full wipe. |
| Time wrong by exactly +/- 1 hour | DST transition in the middle of a sync — wait one hour, it self-corrects on next NTP. Or check `TZ_INFO` in `Clock.ino`. |
| Clock drifts more than ~1 s/min | Check serial for `RTC drift: …` lines — if `driftRateUsPerSec` is way off (>50000), the clamp is hiding the real issue. Most likely PSU noise. |
| Compile fails after pulling new arduino-cli | The ESP32 core API changed between 2.x and 3.x. We're on 3.3.5; see `PHASE2_PLAN.md` for known API renames. |

## Reference paths

| What | Where |
|------|-------|
| ESP32 Arduino core | `~/Library/Arduino15/packages/esp32/hardware/esp32/3.3.5/` |
| `espota.py` | `~/Library/Arduino15/packages/esp32/hardware/esp32/3.3.5/tools/espota.py` |
| Installed libraries | `~/Documents/Arduino/libraries/` |
| Build artifacts | `Clock/build/` (gitignored) |
