// WeAct 4.2" BW Hebrew clock on ESP32-C6.
// See PHASE2_PLAN.md for the staged feature rollout.

#include "DebugLog.h"
#include "WebSerialHandler.h"
#include "HebrewClock.h"
#include "pins.h"
#include "version.h"

#include <GxEPD2_BW.h>
#include <SPI.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include "fonts/NotoSansHebrew24pt.h"

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <esp_sntp.h>
#include <esp_task_wdt.h>
#include <time.h>
#include <sys/time.h>

// ==========================================
// Display
// ==========================================
GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT>
  display(GxEPD2_420_GDEY042T81(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

int ScreenHeight = 0, ScreenWidth = 0;

// ==========================================
// Config (persisted via Preferences)
// ==========================================
struct Config {
  String ssid;
  String password;
  int    ntpSyncIntervalHours;
};

Preferences prefs;
Config config;

// Israel timezone (IST=UTC+2, IDT=UTC+3)
const char* NTP_SERVER1 = "pool.ntp.org";
const char* NTP_SERVER2 = "time.google.com";
const char* NTP_SERVER3 = "216.239.35.0";
const char* TZ_INFO     = "IST-2IDT,M3.4.5/2,M10.5.0/2";

// ==========================================
// State
// ==========================================
RTC_DATA_ATTR time_t lastSyncTime         = 0;
RTC_DATA_ATTR int    lastDisplayedMinute  = -1;
RTC_DATA_ATTR bool   timeValid            = false;
// driftRateUsPerSec: positive = RTC runs fast; used by goToSleep() to
// scale the requested deep-sleep duration so we wake near the real minute.
RTC_DATA_ATTR float  driftRateUsPerSec    = 0.0f;

String lastKnownIP = "0.0.0.0";

WebServer server(80);
ArduinoOTAClass& OTA = ArduinoOTA;

volatile bool ntpSyncCompleted = false;
void ntpTimeSyncCallback(struct timeval* tv) {
  ntpSyncCompleted = true;
}

// Was this boot a cold boot (power-on / hard reset / SW reset)?
// On a cold boot we open an 8 s OTA window before sleeping; on timer wake
// we skip the window so we don't burn battery on every minute.
bool isColdBoot = true;

// ==========================================
// Forward declarations
// ==========================================
void setupDisplay(bool initial);
void renderClock(bool fullRefresh);
void setupWiFi();
bool setupNTP();
void setupWebServer();
void runOtaWindow(uint32_t durationMs);
void goToSleep();
void loadConfig();
void saveConfig();
void setDefaultConfig();

// ==========================================
// Config persistence
// ==========================================
void saveConfig() {
  prefs.begin("settings", false);
  prefs.putString("ssid", config.ssid);
  prefs.putString("password", config.password);
  prefs.putInt("syncHours", config.ntpSyncIntervalHours);
  prefs.end();
  logInfo("Config saved.");
}

void setDefaultConfig() {
  config.ssid = "Avihay";
  config.password = "Tchjh1234";
  config.ntpSyncIntervalHours = 1;
  saveConfig();
}

void loadConfig() {
  prefs.begin("settings", true);
  config.ssid     = prefs.getString("ssid", "");
  config.password = prefs.getString("password", "");
  config.ntpSyncIntervalHours = prefs.getInt("syncHours", 1);
  prefs.end();

  if (config.ssid == "") {
    logInfo("No saved config — loading defaults.");
    setDefaultConfig();
  } else {
    logInfo("Config loaded: ssid=" + config.ssid);
  }
}

// ==========================================
// WiFi
// ==========================================
void setupWiFi() {
  logInfo("Connecting to WiFi: " + config.ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.ssid.c_str(), config.password.c_str());
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    esp_task_wdt_reset();
    attempts++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    lastKnownIP = WiFi.localIP().toString();
    logInfo("WiFi connected, IP: " + lastKnownIP);
  } else {
    logError("WiFi connection failed.");
  }
}

// ==========================================
// NTP
// ==========================================
bool setupNTP() {
  logInfo("Setting up NTP time sync...");

  ntpSyncCompleted = false;
  sntp_set_time_sync_notification_cb(ntpTimeSyncCallback);
  // configTzTime() internally calls sntp_init(); stop any prior instance so
  // it actually restarts (otherwise retries are silent no-ops).
  esp_sntp_stop();
  configTzTime(TZ_INFO, NTP_SERVER2, NTP_SERVER1, NTP_SERVER3);

  int attempts = 0;
  while (!ntpSyncCompleted && attempts < 20) {
    delay(500);
    Serial.print("T");
    esp_task_wdt_reset();
    attempts++;
  }
  Serial.println();

  if (ntpSyncCompleted) {
    timeValid = true;
    struct tm timeinfo;
    getLocalTime(&timeinfo, 0);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    logInfo("NTP time synced: " + String(buf));
    return true;
  }
  logError("NTP sync failed (no server response after " + String(attempts * 500) + "ms)");
  return false;
}

// ==========================================
// Display
// ==========================================
void setupDisplay(bool initial) {
  logInfof("Setting up display (initial=%s)", initial ? "true" : "false");
  // SPI bus shared with the panel only — no SD card on this board.
  SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
  // initial=true  → full panel wipe + init (cold boot, OTA-window wake)
  // initial=false → resume controller; required for fast partial updates after timer wake.
  // 50 ms reset pulse, pulldown_rst_mode=false (matches HelloWorld diagnostic).
  display.init(115200, initial, 50, false);
  display.setTextColor(GxEPD_BLACK);
  display.setRotation(0);  // landscape — 400 wide × 300 tall
  ScreenWidth  = display.width();
  ScreenHeight = display.height();
}

void renderClock(bool fullRefresh) {
  // Compose Hebrew time strings
  int hour = 0, minute = 0;
  bool haveTime = false;
  if (timeValid) {
    struct tm ti;
    if (getLocalTime(&ti, 0)) {
      hour   = ti.tm_hour;
      minute = ti.tm_min;
      haveTime = true;
    }
  }

  String line1 = "—", line2 = "";
  if (haveTime) {
    HebrewClock::getTimeStrings(hour, minute, line1, line2);
  } else {
    line1 = "\xD7\x9C\xD7\x9C\xD7\x90 \xD7\xA9\xD7\x81\xD7\xA2\xD7\x95\xD7\x9F"; // "ללא שעון" (no time)
  }
  logInfo("Line 1: " + line1);
  logInfo("Line 2: " + line2);

  // Background clear
  display.fillRect(0, 0, ScreenWidth, ScreenHeight, GxEPD_WHITE);

  // Two big Hebrew lines, right-aligned
  const GFXfont* hebrewFont = &NotoSansHebrew_Bold24pt8b;
  int16_t rightMargin = ScreenWidth - 12;
  int16_t y1 = 110;  // baseline of line 1
  int16_t y2 = 185;  // baseline of line 2
  HebrewClock::drawHebrewText(display, hebrewFont, line1.c_str(), rightMargin, y1, GxEPD_BLACK);
  HebrewClock::drawHebrewText(display, hebrewFont, line2.c_str(), rightMargin, y2, GxEPD_BLACK);

  // Footer strip — IP / version / NTP status
  display.drawLine(8, 250, ScreenWidth - 8, 250, GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(10, 275);
  display.print(lastKnownIP);
  display.setCursor(10, 293);
  display.print("v");
  display.print(FIRMWARE_VERSION);
  display.setCursor(ScreenWidth - 80, 275);
  display.print(timeValid ? "NTP OK" : "NTP --");

  if (fullRefresh) {
    display.display(false);  // full waveform — clean from any prior content
  } else {
    display.display(true);   // partial waveform — fast, no flicker
  }
}

// ==========================================
// Web server
// ==========================================
void handleGetConfig() {
  JsonDocument doc;
  doc["ssid"] = config.ssid;
  doc["ip"]   = WiFi.localIP().toString();
  doc["ntp_sync_interval_hours"] = config.ntpSyncIntervalHours;
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleSetConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "No data received");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }
  if (doc["ssid"].is<String>())     config.ssid     = doc["ssid"].as<String>();
  if (doc["password"].is<String>()) config.password = doc["password"].as<String>();
  if (doc["ntp_sync_interval_hours"].is<int>())
    config.ntpSyncIntervalHours = doc["ntp_sync_interval_hours"].as<int>();
  saveConfig();
  server.send(200, "text/plain", "Configuration updated, rebooting");
  delay(500);
  ESP.restart();
}

void handleGetStatus() {
  JsonDocument doc;
  doc["version"]    = FIRMWARE_VERSION;
  doc["ip"]         = WiFi.localIP().toString();
  doc["uptime_sec"] = millis() / 1000;
  doc["free_heap"]  = ESP.getFreeHeap();
  doc["time_valid"] = timeValid;
  if (timeValid) {
    struct tm ti;
    if (getLocalTime(&ti, 0)) {
      char buf[32];
      strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
      doc["time"] = buf;
    }
  }
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void setupWebServer() {
  server.on("/config", HTTP_GET,  handleGetConfig);
  server.on("/config", HTTP_POST, handleSetConfig);
  server.on("/status", HTTP_GET,  handleGetStatus);
  server.begin();
  logInfo("HTTP server started on :80");
}

// ==========================================
// OTA window
// ==========================================
void runOtaWindow(uint32_t durationMs) {
  if (WiFi.status() != WL_CONNECTED) {
    logInfo("OTA window skipped — WiFi not connected.");
    return;
  }

  setupWebServer();
  WebSerial.begin(&server);

  ArduinoOTA.setHostname("WeAct-Clock");
  ArduinoOTA.onStart([]() {
    logInfo("OTA: upload starting...");
  });
  ArduinoOTA.onEnd([]() {
    logInfo("OTA: upload complete, rebooting");
  });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
    static int lastPct = -1;
    int pct = (p * 100) / t;
    if (pct != lastPct && pct % 10 == 0) {
      logInfof("OTA: %d%%", pct);
      lastPct = pct;
    }
  });
  ArduinoOTA.onError([](ota_error_t err) {
    logErrorf("OTA error %u", (unsigned)err);
  });
  ArduinoOTA.begin();

  logInfof("OTA window open for %u ms — listening for upload at %s",
           durationMs, lastKnownIP.c_str());

  uint32_t start = millis();
  while (millis() - start < durationMs) {
    ArduinoOTA.handle();
    server.handleClient();
    WebSerial.loop();
    esp_task_wdt_reset();
    delay(5);
  }
  logInfo("OTA window closed.");
}

// ==========================================
// Deep sleep
// ==========================================
void goToSleep() {
  logInfo("Going to sleep...");

  // Hibernate the panel — bistable, retains image at near-zero power.
  display.hibernate();

  // Note: ESP32-C6 doesn't expose ESP_PD_DOMAIN_RTC_FAST_MEM. The deep-sleep
  // power manager handles unused RTC memory automatically — no manual config.

  // Sub-second precision sleep so we wake on the minute boundary.
  // SLEEP_MARGIN_US absorbs the few ms of overhead entering deep sleep,
  // otherwise we wake just before the boundary, see the old minute, and
  // waste a wake cycle.
  const uint64_t SLEEP_MARGIN_US = 50000ULL;

  struct timeval tv;
  uint64_t timeToSleep;
  if (gettimeofday(&tv, nullptr) == 0 && tv.tv_sec > 0) {
    uint64_t secsLeft  = (uint64_t)(60 - (tv.tv_sec % 60));
    uint64_t usElapsed = (uint64_t)tv.tv_usec;
    if (secsLeft == 0 || secsLeft > 60) secsLeft = 60;
    timeToSleep = secsLeft * 1000000ULL - usElapsed + SLEEP_MARGIN_US;
  } else {
    logInfo("Time not set — sleeping 60 s as fallback.");
    timeToSleep = 60ULL * 1000000ULL;
  }

  // Compensate for RC oscillator drift: the same drifted oscillator counts
  // sleep ticks, so a "60 s" request finishes too early or too late.
  // Clamp to ±5 % (the C6's internal RC oscillator drift envelope).
  if (driftRateUsPerSec != 0.0f && !isnan(driftRateUsPerSec) && !isinf(driftRateUsPerSec)) {
    float clamped = constrain(driftRateUsPerSec, -50000.0f, 50000.0f);
    int64_t correction = (int64_t)(clamped * (int64_t)timeToSleep / 1e6);
    timeToSleep = (uint64_t)((int64_t)timeToSleep + correction);
    logInfof("Drift scaling: %lld us (rate %.0f us/s)",
             (long long)correction, clamped);
  }

  logInfof("Sleeping %llu us", (unsigned long long)timeToSleep);
  esp_deep_sleep(timeToSleep);
}

// ==========================================
// Setup / loop
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();

  esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
  isColdBoot = (wakeup_cause != ESP_SLEEP_WAKEUP_TIMER);

  logInfof("=== WeAct Clock v%s boot (cause=%d, cold=%s) ===",
           FIRMWARE_VERSION, wakeup_cause, isColdBoot ? "yes" : "no");

  // Watchdog — long timeout to cover slow WiFi connects (ESP32 core 3.x API).
  esp_task_wdt_config_t wdtCfg = {
    .timeout_ms    = 600 * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  esp_task_wdt_reconfigure(&wdtCfg);
  esp_task_wdt_add(NULL);

  setenv("TZ", TZ_INFO, 1);
  tzset();

  loadConfig();

  // Decide whether we need to sync NTP this wake.
  bool needSync = false;
  if (isColdBoot) {
    needSync = true;
  } else {
    struct tm ti;
    if (getLocalTime(&ti, 0)) {
      time_t now; time(&now);
      double secsSinceSync = (lastSyncTime > 0) ? difftime(now, lastSyncTime) : 1e9;
      bool isTopOfHour = (ti.tm_min == 0);
      if (lastSyncTime == 0) {
        needSync = true;
      } else if (isTopOfHour && secsSinceSync >= 3000) {
        needSync = true;
        logInfo("Top-of-hour NTP sync triggered.");
      } else if (secsSinceSync >= (double)config.ntpSyncIntervalHours * 3600.0 + 1800.0) {
        needSync = true;
        logInfo("Fallback NTP sync (overdue).");
      }
    } else {
      needSync = true;
    }
  }

  // Network bring-up only when needed.
  if (needSync) {
    setupWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      time_t preSync = 0;
      uint32_t syncStartMs = 0;
      if (lastSyncTime > 0 && timeValid) {
        time(&preSync);
        syncStartMs = millis();
      }

      bool synced = false;
      for (int retry = 0; retry < 3 && !synced; retry++) {
        if (retry > 0) logInfof("NTP retry %d/2", retry);
        synced = setupNTP();
      }

      if (synced) {
        time_t ntpTime; time(&ntpTime);
        // Drift measurement — same logic as reference.
        if (preSync > 0 && lastSyncTime > 0) {
          float syncDurationSec = (millis() - syncStartMs) / 1000.0f;
          double rtcElapsed = difftime(preSync, lastSyncTime);
          double ntpElapsed = difftime(ntpTime, lastSyncTime) - syncDurationSec;
          if (rtcElapsed > 60 && ntpElapsed > 60) {
            double driftSec = rtcElapsed - ntpElapsed;
            driftRateUsPerSec = (float)(driftSec * 1e6 / ntpElapsed);
            logInfof("RTC drift: %.3fs over %.0fs (%.2f us/s)",
                     driftSec, ntpElapsed, driftRateUsPerSec);
          }
        }
        lastSyncTime = ntpTime;
        timeValid = true;
      } else {
        logError("NTP sync failed; continuing with RTC time.");
      }
    }
  } else {
    timeValid = true;
    logInfo("NTP sync skipped this wake.");
  }

  // Bring up the display.
  setupDisplay(isColdBoot);

  // Decide whether to redraw. Skip if minute hasn't changed (e.g. early wake).
  int currentMinute = -1;
  if (timeValid) {
    struct tm ti;
    if (getLocalTime(&ti, 0)) currentMinute = ti.tm_hour * 60 + ti.tm_min;
  }
  bool redraw = (currentMinute != lastDisplayedMinute) || isColdBoot;
  if (redraw) {
    renderClock(/*fullRefresh=*/isColdBoot);
    lastDisplayedMinute = currentMinute;
  } else {
    logInfo("Minute unchanged — skipping redraw.");
  }

  // OTA window only on cold boot (USB plug, hard reset, SW reset).
  // On timer wakes we go straight to sleep.
  if (isColdBoot) {
    runOtaWindow(8000);
  }

  goToSleep();
}

void loop() {
  // Unreached — setup() always ends in deep sleep.
  esp_task_wdt_reset();
  delay(100);
}
