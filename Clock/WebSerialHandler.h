#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

class WebSerialHandler : public Print {
public:
    WebSerialHandler();
    void begin(WebServer *server, const char* url = "/webserial");
    void loop();  // Must be called from loop() to service WebSocket
    bool isInitialized() const { return _initialized; }

    // Print interface implementation
    virtual size_t write(uint8_t c) override;
    virtual size_t write(const uint8_t *buffer, size_t size) override;

private:
    WebSocketsServer _wsServer;
    bool _initialized;
    uint8_t _clientCount;

    void onWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
};

extern WebSerialHandler WebSerial;
