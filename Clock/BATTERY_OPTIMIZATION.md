# Battery optimization opportunities

There's no battery wired right now (USB-powered DevKitC), but the architecture is already deep-sleep based. This doc surveys what's been done, what's left, and the hardware mods that matter once a battery actually goes on.

> **Currently `DEBUG_MODE=1`** during development — none of the battery numbers below apply while that flag is set, since debug mode keeps WiFi on continuously. Set `DEBUG_MODE=0` (and ideally `PRODUCTION_MODE=1`) before any battery testing.

## Power model

Approximate ESP32-C6 currents (from datasheet + Espressif app notes):

| State                          | Current  |
|--------------------------------|----------|
| Active @ 160 MHz, no radio     | ~25 mA   |
| Active @ 80 MHz, no radio      | ~15 mA   |
| WiFi STA connected, idle       | ~80 mA   |
| WiFi RX peak                   | ~140 mA  |
| WiFi TX peak                   | ~310 mA  |
| Modem-sleep (WiFi assoc, idle) | ~6 mA    |
| Light sleep                    | ~150 µA  |
| **Deep sleep (RTC timer wake)**| **~7 µA**|

Plus the WeAct panel: ~5 µA in hibernate, up to ~30 mA during a refresh.

## Current wake-cycle cost (estimated)

### Cold boot (~12 s)
| Phase                          | Time   | I avg  | Charge   |
|--------------------------------|--------|--------|----------|
| Boot + watchdog + load config  | 0.5 s  | 25 mA  | 12.5 mAs |
| WiFi connect                   | 3 s    | 130 mA | 390 mAs  |
| NTP sync                       | 2 s    | 100 mA | 200 mAs  |
| Display init + full refresh    | 2.5 s  | 50 mA  | 125 mAs  |
| OTA window (radio idle)        | 8 s    | 90 mA  | 720 mAs  |
| **Total**                      | 16 s   | —      | **~1450 mAs** |

### Timer wake — display only (the 99 % case)
| Phase                          | Time   | I avg  | Charge   |
|--------------------------------|--------|--------|----------|
| Boot + watchdog + load config  | 0.5 s  | 25 mA  | 12.5 mAs |
| Display resume + partial refresh | 1.5 s | 50 mA | 75 mAs   |
| Sleep entry overhead           | 0.05 s | 25 mA  | 1.3 mAs  |
| **Total active**               | 2.05 s | —      | **~89 mAs** |

### Timer wake — with NTP re-sync (~once per hour)
≈ 5–6 s active, ~600 mAs.

### Sleep current
60 s − 2 s = 58 s × 7 µA = 0.4 mAs per minute.

### Daily totals (rough)

```
Per minute (display-only):    89 mAs active + 0.4 mAs sleep ≈ 89 mAs
Per hour (incl. NTP):         59 × 89 + 600 ≈ 5,800 mAs
Per day:                      24 × 5,800 ≈ 139,000 mAs ≈ 38.6 mAh
+ cold boots (1/day for OTA): ~1.4 mAh
Daily total:                  ~40 mAh
```

A 1500 mAh 18650 Li-ion would last roughly **37 days** at this rate. That's the baseline.

After the optimizations applied below (#1 + #2 + #3 + #5 + #7 + faster boot via PRODUCTION_MODE), the timer-wake path drops to ~1 s active @ ~12 mA average ≈ ~12 mAs per minute. Daily total drops from ~40 mAh to **~22 mAh** → **68 days** on the same battery, before any hardware changes.

---

## Optimization opportunities, ranked

The wins live almost entirely in the **timer-wake** path (it runs 1440× per day). One second saved per minute = 24 minutes/day of active CPU eliminated.

> **Status legend:** ✅ applied · ⏳ pending hardware measurement · ⬜ not yet applied

### High impact

#### 1. ✅ Drop CPU clock to 80 MHz on display-only wakes  *(saves ~30 % of active CPU current)*
Reference EInk_Clock does `setCpuFrequencyMhz(80)` on non-NTP wakes. Display SPI clock is set independently and isn't affected. Display refresh time goes up slightly (~10 %), but average current drops from 25 → 15 mA.
**Estimated daily saving:** ~5 mAh.

```cpp
if (!needSync) {
  setCpuFrequencyMhz(80);
  logInfo("CPU @ 80 MHz (display-only wake)");
}
```
Place right after the `needSync` decision.

#### 2. ✅ Skip `Serial.begin()` and the 300 ms post-delay in production  *(saves ~340 ms of boot)*
`Serial.begin(115200) + delay(300)` is 300+ ms of pure waste on every wake. On C6 the USB-Serial-JTAG also draws ~5 mA when initialized.
**Estimated daily saving:** ~2 mAh + faster wake-to-sleep.

```cpp
#define PRODUCTION_MODE 0  // set 1 to disable Serial

void setup() {
#if !PRODUCTION_MODE
  Serial.begin(115200);
  delay(300);
#endif
  ...
}
```
Pair with a `DebugLog` change to no-op `Serial.println` when `PRODUCTION_MODE`.

#### 3. ✅ Decide whether to redraw *before* initializing the display  *(saves ~1.5 s on early wakes)*
Right now `setupDisplay(false)` always runs, then we check whether the minute changed. On an early wake (driftrate compensation overshoots → wake at :59.5 instead of :00.05), we init the panel only to discover nothing changed. If we check time first, we can skip display init entirely.
**Estimated daily saving:** depends on drift accuracy — 0–10 mAh.

```cpp
int currentMinute = computeCurrentMinute();
bool redraw = (currentMinute != lastDisplayedMinute) || isColdBoot;
if (redraw) {
  setupDisplay(isColdBoot);
  renderClock(isColdBoot);
  lastDisplayedMinute = currentMinute;
}
```

#### 4. ✅ Tighten WiFi connect timeout  *(saves charge on failed/slow connects)*
Current cap is 40 attempts × 500 ms = 20 s at 130 mA = 720 mAs per failed sync. Drop to 20 attempts (10 s) and let the next NTP cycle retry. Also dropped NTP retries 3 → 2.
**Estimated daily saving:** small in normal conditions, large if your AP is flaky.

```cpp
while (WiFi.status() != WL_CONNECTED && attempts < 20) { ... }
```

### Medium impact

#### 5. ✅ Modem-sleep during the OTA window  *(cuts OTA-window current from 90 → ~30 mA)*
WiFi can sleep between beacons and still receive OTA initiations. Adds ~100 ms of latency to OTA detection but the 8 s window is plenty.
**Estimated daily saving:** ~0.5 mAh (OTA window is rare).

```cpp
WiFi.setSleep(WIFI_PS_MAX_MODEM);  // call after WiFi.begin()
```

#### 6. Disable BLE controller explicitly
The C6 includes BLE 5.0. Even if you never use it, the radio init may consume current during boot. The reference uses `btStop()` (classic-BT only, no-op here). For C6:

```cpp
#include <esp_bt.h>
esp_bt_controller_disable();
esp_bt_controller_deinit();
```
**Estimated daily saving:** small, depends on what the SDK starts by default. Worth measuring with a multimeter.

#### 7. ✅ Hibernate the display *before* WiFi tear-down on cold-boot path
Currently we do everything (render → OTA window → sleep), and `goToSleep()` calls `display.hibernate()` at the end. If the OTA window is long, the panel sits in `powerOn` for 8 s drawing ~5 mA unnecessarily. Move the `hibernate()` call right after `renderClock()` if no further updates are planned in this wake — and re-init for any redraw triggered by a config change.
**Estimated daily saving:** ~0.1 mAh.

#### 8. Reduce OTA window duration  *(linear saving)*
8 s is conservative. If you reliably hit it within 5 s of plug-in, drop to 5 s.
**Estimated daily saving:** depends on how often you cold-boot.

### Low impact / mostly architectural

#### 9. Reduce `SLEEP_MARGIN_US` from 50 ms → 10 ms
Trade-off: smaller margin = fewer "wake just before boundary, see same minute, sleep again" cycles, but more risk of overshooting and skipping a minute on a slow wake. With drift compensation working, 20 ms is probably enough.

#### 10. Check `_HAS_PWR_CTRL_` / panel power gating *(future hardware)*
Add a P-MOSFET in line with panel VCC, controlled by an ESP32 GPIO. Then we can:
- Power the panel completely off during sleep (~5 µA → 0).
- Recover from the rare case where the panel ignores SPI (currently requires manual USB unplug).

This is the single biggest hardware change worth making for battery operation. Not urgent — current panel hibernate is already ~5 µA.

#### 11. External 32 kHz crystal instead of internal RC oscillator
The C6's internal RC drifts up to ±5 %, requiring runtime drift compensation and frequent NTP syncs. An external crystal drifts ~20 ppm. Eliminates correction overhead and lets us cut hourly NTP syncs to once a day.
**Saving:** ~5 mAh/day if NTP syncs reduce from hourly to daily. Hardware change.

#### 12. Wake every 5 minutes if only displaying hour-resolution
If the design moves to hour-only display ("שָׁלוֹשׁ אַחַר-הַצָּהֳרַיִם" without minutes), only need to wake at minute 0 of each hour. **5–10× fewer wake cycles.**
**Saving:** ~30 mAh/day. Major UX change.

---

## Issues spotted during the review (not strictly battery)

### ✅ Bug fixed: `lastKnownIP` is now preserved across deep sleep
It used to be a regular global (`String`) — after a timer wake it reset to `"0.0.0.0"`, leaving the footer blank until the next NTP-sync wake re-ran `setupWiFi()`. Replaced with `RTC_DATA_ATTR char lastKnownIPbuf[16]`.

### ✅ NTP retries reduced
Was 3 retries × 10 s = up to 30 s of WiFi idle. Dropped to 2 retries → ~20 s worst case.
Could go further by dropping attempts-per-call to 14 (7 s each) for a 14 s total cap.

### ✅ OTA window auto-extends during transfer
`otaInProgress` flag (set by `ArduinoOTA.onStart`) keeps the window loop alive while a transfer is active, so an upload that starts at t=7 s of an 8 s window still completes (otherwise the ~12 s transfer would abort halfway). Cleared by `onEnd` and `onError`.

### Open: OTA window early-exit when idle
If no OTA upload is requested, we still sit for the full 8 s. Could exit after 2 s of "no traffic" — but ArduinoOTA doesn't expose receive state cleanly. Practical alternative: shrink the window from 8 s → 5 s.

### ✅ Style fix: dropped redundant `ArduinoOTAClass& OTA = ArduinoOTA;` alias

### Minor: `pulldown_rst_mode=false` in `display.init()` — verify documentation
GxEPD2 docs are inconsistent on what this flag does. We inherited it from HelloWorld where it works. Worth confirming whether it should be `true` here given the broken HW reset (some panels prefer the line floating to avoid spurious resets).

---

## Status

**Applied (this round):** #1 CPU scaling, #2 PRODUCTION_MODE, #3 early-redraw decision, #4 WiFi/NTP retry tightening, #5 modem-sleep, #7 early hibernate, plus the `lastKnownIP` RTC fix and the dead-alias cleanup.

**Still on the list:**

1. **#6** (BLE off explicitly) — measure first to see if it matters on C6 boot.
2. **#8** (shrink OTA window from 8 s → 5 s) — depends on how often you hit it in time.
3. **#9** (sleep margin 50 ms → 20 ms) — needs drift compensation to be proven first.
4. **#10** P-MOSFET on panel VCC — hardware mod for the next PCB revision.
5. **#11** External 32 kHz crystal — hardware mod, biggest win for long-term battery.
6. **#12** Wake every 5 min for hour-only display — UX change.

## Peripherals — software vs hardware

### Software-side (handled in firmware)

| Item                                | Status |
|-------------------------------------|--------|
| Deep sleep (`esp_deep_sleep`)       | ✅ in use — `goToSleep()` always ends here (default mode) |
| Display panel hibernation           | ✅ `display.hibernate()` after every render |
| WiFi explicit teardown before sleep | ✅ `WiFi.disconnect(true) + WIFI_OFF` |
| WiFi modem-sleep when associated    | ✅ `WiFi.setSleep(WIFI_PS_MAX_MODEM)` |
| SPI bus released before sleep       | ✅ `SPI.end()` |
| BLE controller disabled             | ✅ `esp_bt_controller_disable()/deinit()` early in `setup()` |
| USB-Serial-JTAG off                 | ✅ via `PRODUCTION_MODE=1` (skips `Serial.begin`) |
| OTA window auto-extends in transfer | ✅ `otaInProgress` flag — no aborted uploads near window end |
| `lastKnownIP` survives deep sleep   | ✅ `RTC_DATA_ATTR char lastKnownIPbuf[16]` |
| ADC, touch, ULP                     | Never enabled — fine |

### Board-side (the ESP32-C6-DevKitC-1 hardware)

These dominate when running from battery. The MCU's deep-sleep current is **~7 µA**, but the dev board adds:

| Component | Drain | What to do |
|-----------|-------|------------|
| **Power LED (red)** — hardwired across 3V3 | **~2-5 mA** continuously | **Cut the LED trace or de-solder.** ~50× the MCU's deep-sleep current. **Single biggest battery win.** |
| **CP2102 USB-UART bridge** (clone-only) | ~10-15 mA when USB is plugged | Powered from VBUS, not 3V3. Battery via 3V3 pin → CP2102 unpowered. No mod needed. |
| **AMS1117 LDO regulator** | ~5 mA quiescent | Bypass: feed regulated 3.3 V directly to the 3V3 pin from a buck/boost. Skip the LDO. |
| **WS2812 RGB LED on GPIO 8** | 0 mA off | Never driven by us — fine. |
| **TVS, USB protection** | µA range | Negligible. |

**Reality check:** with the stock dev board, the power LED alone limits you to ~5-7 days on a 1500 mAh cell, regardless of how clean the firmware is. De-solder the LED before any serious battery testing.

If/when you cut the LED:
- Sleep current drops to MCU + LDO quiescent ≈ **7 µA + 5 mA ≈ 5 mA**.
- Cut the LDO too (run from regulated 3.3 V) → **~7 µA sleep**.

At ~7 µA sleep + ~22 mAh active per day: **~150 days** on a 1500 mAh cell with the bare-board mods. That's the asymptotic ceiling without redesigning the PCB.

## How to enable PRODUCTION_MODE

Edit `DebugLog.h:5`:

```c
#define PRODUCTION_MODE 1
```

Or pass it as a build flag in `compile.sh`:

```bash
arduino-cli compile ... --build-property "build.extra_flags=-DPRODUCTION_MODE=1"
```

Keep it `0` for development — you lose the serial monitor in production mode (WebSerial during OTA window still works).

---

## How to measure

You'll need a multimeter that can do mA *and* µA, in series with the board's 3V3 supply, plus ideally a current-shunt logger for the wake transients. Quick-and-dirty:

- **Sleep current:** USB unplugged, battery + multimeter inline, panel rendered. Should read ~7–10 µA.
- **Active current:** harder without a logger; expect ~25 mA average during display refresh.
- **WiFi current:** simplest is to log boot start/end times via serial and divide your battery loss over a few days.

For real numbers: a Nordic Power Profiler Kit II or a Joulescope is the right tool.
