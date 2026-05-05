#pragma once
#include <Arduino.h>

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
