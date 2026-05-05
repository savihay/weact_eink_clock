#include "DebugLog.h"
#include "WebSerialHandler.h"
#include <cstdarg>

// Default to showing all messages
LogLevel currentLogLevel = LogLevel::LOG_DEBUG;

// ============== Helper: Get Timestamp ==============
static String getTimestamp() {
    unsigned long ms = millis();
    unsigned long seconds = ms / 1000;
    unsigned long minutes = (seconds / 60) % 60;
    unsigned long hours = (seconds / 3600) % 24;
    unsigned long secs = seconds % 60;
    unsigned long millis_part = ms % 1000;

    char buf[16];
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu.%03lu", hours, minutes, secs, millis_part);
    return String(buf);
}

// ============== Core Logging Implementation ==============
static void logWithLevel(LogLevel level, const char* levelStr, const String& msg) {
    if (level > currentLogLevel) return;

    String formatted = "[" + getTimestamp() + "] [" + levelStr + "] " + msg;
#if !PRODUCTION_MODE
    Serial.println(formatted);
#endif
    if (WebSerial.isInitialized()) {
        WebSerial.println(formatted);
    }
}

static void logWithLevelf(LogLevel level, const char* levelStr, const char* format, va_list args) {
    if (level > currentLogLevel) return;

    char msgBuf[256];
    vsnprintf(msgBuf, sizeof(msgBuf), format, args);

    // Remove trailing newline if present (we add our own)
    size_t len = strlen(msgBuf);
    if (len > 0 && msgBuf[len - 1] == '\n') {
        msgBuf[len - 1] = '\0';
    }

    String formatted = "[" + getTimestamp() + "] [" + levelStr + "] " + String(msgBuf);
#if !PRODUCTION_MODE
    Serial.println(formatted);
#endif
    if (WebSerial.isInitialized()) {
        WebSerial.println(formatted);
    }
}

// ============== Public Functions ==============
void logError(const String& msg) {
    logWithLevel(LogLevel::LOG_ERROR, "ERROR", msg);
}

void logInfo(const String& msg) {
    logWithLevel(LogLevel::LOG_INFO, "INFO", msg);
}

void logDebug(const String& msg) {
    logWithLevel(LogLevel::LOG_DEBUG, "DEBUG", msg);
}

void logErrorf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logWithLevelf(LogLevel::LOG_ERROR, "ERROR", format, args);
    va_end(args);
}

void logInfof(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logWithLevelf(LogLevel::LOG_INFO, "INFO", format, args);
    va_end(args);
}

void logDebugf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logWithLevelf(LogLevel::LOG_DEBUG, "DEBUG", format, args);
    va_end(args);
}
