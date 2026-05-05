# Battery optimization opportunities

There's no battery wired right now (USB-powered DevKitC), but the architecture is already deep-sleep based. This doc surveys what to change *before* the battery goes on, ranked by impact.

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

A 1500 mAh 18650 Li-ion would last roughly **37 days** at this rate. That's the baseline to beat.

---

## Optimization opportunities, ranked

The wins live almost entirely in the **timer-wake** path (it runs 1440× per day). One second saved per minute = 24 minutes/day of active CPU eliminated.

### High impact

#### 1. Drop CPU clock to 80 MHz on display-only wakes  *(saves ~30 % of active CPU current)*
Reference EInk_Clock does `setCpuFrequencyMhz(80)` on non-NTP wakes. Display SPI clock is set independently and isn't affected. Display refresh time goes up slightly (~10 %), but average current drops from 25 → 15 mA.
**Estimated daily saving:** ~5 mAh.

```cpp
if (!needSync) {
  setCpuFrequencyMhz(80);
  logInfo("CPU @ 80 MHz (display-only wake)");
}
```
Place right after the `needSync` decision.

#### 2. Skip `Serial.begin()` and the 300 ms post-delay in production  *(saves ~340 ms of boot)*
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

#### 3. Decide whether to redraw *before* initializing the display  *(saves ~1.5 s on early wakes)*
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

#### 4. Tighten WiFi connect timeout  *(saves charge on failed/slow connects)*
Current cap is 40 attempts × 500 ms = 20 s at 130 mA = 720 mAs per failed sync. Drop to 20 attempts (10 s) and let the next NTP cycle retry.
**Estimated daily saving:** small in normal conditions, large if your AP is flaky.

```cpp
while (WiFi.status() != WL_CONNECTED && attempts < 20) { ... }
```

### Medium impact

#### 5. Modem-sleep during the OTA window  *(cuts OTA-window current from 90 → ~30 mA)*
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

#### 7. Hibernate the display *before* WiFi tear-down on cold-boot path
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

### Bug: `lastKnownIP` is not preserved across deep sleep
It's a regular global (`String`), not `RTC_DATA_ATTR`. After a timer wake it resets to `"0.0.0.0"` and the rendered footer shows that until the next NTP-sync wake re-runs `setupWiFi()`. Fix: cache the IP in NVS or `RTC_DATA_ATTR char[16]`.

```cpp
RTC_DATA_ATTR char lastKnownIPbuf[16] = "0.0.0.0";
// after WiFi connect:
strncpy(lastKnownIPbuf, WiFi.localIP().toString().c_str(), sizeof(lastKnownIPbuf));
// for display:
display.print(lastKnownIPbuf);
```

### Concern: 3 NTP retries × 10 s = up to 30 s of WiFi idle on cold boot
`setupNTP()` waits up to 10 s per call, and we call it 3 times before giving up. If your NTP server is slow, that's 30 s × 130 mA on the radio. Consider:
- Cap retries at 2.
- Drop attempts per call to 14 (7 s) so total worst-case stays ~14 s.

### Concern: OTA window has no early exit
If no OTA upload is requested, we sit for the full 8 s. Could exit after 2 s if no `WStype_CONNECTED` event has fired and no OTA packet has arrived — but ArduinoOTA doesn't expose state cleanly. Lowest-risk approach is just shrinking the window.

### Style: redundant `ArduinoOTAClass& OTA = ArduinoOTA;` alias
Defined at line 64, never used. Drop it.

### Minor: `pulldown_rst_mode=false` in `display.init()` — verify documentation
GxEPD2 docs are inconsistent on what this flag does. We inherited it from HelloWorld where it works. Worth confirming whether it should be `true` here given the broken HW reset (some panels prefer the line floating to avoid spurious resets).

---

## Recommended order to apply

1. **#1 + #2** (CPU scaling + production mode) — easiest, biggest win, no risk.
2. **#3** (early-skip redraw) — small refactor, eliminates overshoot waste.
3. **Bug fix: `lastKnownIP`** — visual correctness, basically free.
4. **#5** (modem-sleep) — one-line change, measure result.
5. **#7** (hibernate-before-OTA) — minor, do it when touching that path.
6. After hardware power measurement: **#6** (BLE off), **#8** (window size), **#9** (sleep margin).
7. Hardware: P-MOSFET on VCC for the next PCB revision.

---

## How to measure

You'll need a multimeter that can do mA *and* µA, in series with the board's 3V3 supply, plus ideally a current-shunt logger for the wake transients. Quick-and-dirty:

- **Sleep current:** USB unplugged, battery + multimeter inline, panel rendered. Should read ~7–10 µA.
- **Active current:** harder without a logger; expect ~25 mA average during display refresh.
- **WiFi current:** simplest is to log boot start/end times via serial and divide your battery loss over a few days.

For real numbers: a Nordic Power Profiler Kit II or a Joulescope is the right tool.
