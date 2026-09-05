#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Backoff.h"
#include "models/Model.h"

class NetworkService {
public:
    struct Snapshot {
        AgentStatus agents[3]; PcStatus pc; NetworkStatus network;
        bool otaReady = false, timeSynced = false;
        uint16_t minutes = 0;
    };
    struct Event {
        enum Kind : uint8_t { Notice, Settings } kind = Notice;
        char text[22] = "";
        int16_t contrast = -1, motion = -1;
    };
    bool begin(const config::NetworkConfig& cfg);
    bool receive(Snapshot& s) { return snapshots_ && xQueueReceive(snapshots_, &s, 0) == pdTRUE; }
    bool event(Event& e) { return events_ && xQueueReceive(events_, &e, 0) == pdTRUE; }
    bool command(int agent, const char* action, uint32_t now);
private:
    struct Command { uint8_t agent; char action[12]; uint32_t created, sequence; };
    config::NetworkConfig cfg_;
    class Transport : public WiFiClient {
        int connect(const char* host, uint16_t port) override { return WiFiClient::connect(host, port, 1000); }
        int connect(IPAddress ip, uint16_t port) override { return WiFiClient::connect(ip, port, 1000); }
    } transport_;
    PubSubClient mqtt_{transport_};
    QueueHandle_t snapshots_ = nullptr, events_ = nullptr, commands_ = nullptr;
    Model state_;
    Backoff wifiRetry_, mqttRetry_;
    char base_[80] = "", statusTopic_[96] = "", boot_[17] = "";
    uint32_t sequence_ = 0;
    bool otaStarted_ = false;
    struct Pending { char id[40] = ""; uint32_t sent = 0; } pending_[8];
    static void task(void* self) { static_cast<NetworkService*>(self)->run(); }
    void run();
    void message(char* topic, byte* payload, unsigned int length);
    void notify(const char* text);
    void snapshot();
    bool connectMqtt();
};
