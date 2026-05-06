#pragma once
#include <Arduino.h>

// PRODUCTION_MODE=1: skip Serial.begin() in setup, no-op Serial.print in DebugLog.
// Keeps WebSerial output intact (still useful when an OTA window is open).
// Saves ~340 ms of boot time and a few mA from the USB-Serial-JTAG controller.
#ifndef PRODUCTION_MODE
#define PRODUCTION_MODE 0
#endif

// DEBUG_MODE=1: keep WiFi + OTA up forever, no deep sleep, no time-bounded
// OTA window. Rebuild + flash without power-cycling. Burns the battery in
// hours — never enable on a battery build. Mutually exclusive with PRODUCTION_MODE.
#ifndef DEBUG_MODE
#define DEBUG_MODE 1
#endif

#if DEBUG_MODE && PRODUCTION_MODE
#error "DEBUG_MODE and PRODUCTION_MODE are mutually exclusive"
#endif

// ============== Log Levels ==============
enum class LogLevel {
    LOG_ERROR = 0,   // Errors only
    LOG_INFO = 1,    // Info + Errors
    LOG_DEBUG = 2    // All messages
};

// Current minimum log level (messages below this level are suppressed)
extern LogLevel currentLogLevel;

// ============== Level-aware Logging Functions ==============
void logError(const String& msg);
void logInfo(const String& msg);
void logDebug(const String& msg);

void logErrorf(const char* format, ...);
void logInfof(const char* format, ...);
void logDebugf(const char* format, ...);
