# WeAct E-ink Clock

Hebrew word-clock on **ESP32-C6-DevKitC-1** + **WeAct 4.2" BW** e-paper (SSD1683 / GDEY042T81).

## Layout

| Path | What |
|------|------|
| [Clock/](Clock/) | The clock firmware — WiFi, NTP, OTA, deep-sleep, Hebrew rendering |
| [Documentation/](Documentation/) | Wiring SVG, pin-layout PNG, hardware setup guide |
| [PHASE2_PLAN.md](PHASE2_PLAN.md) | Original task plan for the WiFi/NTP/OTA build-out |

## Hardware

See [Documentation/WeAct_42_EPaper_ESP32C6_Guide.md](Documentation/WeAct_42_EPaper_ESP32C6_Guide.md) and the wiring SVG.

Key facts (carried over from the Phase 1 session):

- **SB4 + SB5 jumpers** closed on the WeAct PCB → LDO mode, 4-line SPI.
- **HW reset is broken** on this WeAct unit (FPC ribbon issue; bypass jumper to GPIO 11 doesn't help). GxEPD2's SW-reset path (cmd 0x12) is what actually drives the panel — verified working including partial updates.
- **No battery, no buttons** on this build — USB-powered DevKitC. Phase 3 candidates.

## Quick start

```bash
cd Clock
./build_scripts/compile.sh
./build_scripts/upload_usb.sh
./build_scripts/monitor.sh
```

After the device is on WiFi, subsequent flashes can go via OTA — see [Clock/README.md](Clock/README.md).
