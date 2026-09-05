#include "Provisioning.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <Preferences.h>

namespace {
constexpr char ServiceUuid[] = "7ce40001-5f9c-4e77-a14b-4cf9ec720001";
constexpr char ConfigUuid[] = "7ce40002-5f9c-4e77-a14b-4cf9ec720001";
constexpr char StatusUuid[] = "7ce40003-5f9c-4e77-a14b-4cf9ec720001";
BLECharacteristic* statusCharacteristic = nullptr;
char input[512]{};
size_t inputLength = 0;
bool restartPending = false;
uint32_t restartAt = 0;
volatile bool messageReady = false;

bool copy(char* target, size_t size, JsonVariantConst value, bool required) {
    if (!value.is<const char*>()) return !required;
    const char* source = value.as<const char*>();
    size_t length = strlen(source);
    if ((required && !length) || length >= size) return false;
    memcpy(target, source, length + 1);
    return true;
}

void status(const char* value) {
    if (!statusCharacteristic) return;
    statusCharacteristic->setValue(value);
    statusCharacteristic->notify();
}

bool save(const char* json, size_t length) {
    StaticJsonDocument<768> document;
    if (deserializeJson(document, json, length) || !document.is<JsonObject>()) return false;
    JsonObjectConst value = document.as<JsonObjectConst>();
    config::NetworkConfig cfg;
    if (!copy(cfg.ssid, sizeof(cfg.ssid), value["ssid"], true) ||
        !copy(cfg.password, sizeof(cfg.password), value["wifi_password"], false) ||
        !copy(cfg.mqttHost, sizeof(cfg.mqttHost), value["mqtt_host"], true) ||
        !copy(cfg.deviceId, sizeof(cfg.deviceId), value["device_id"], true) ||
        !copy(cfg.mqttUser, sizeof(cfg.mqttUser), value["mqtt_user"], false) ||
        !copy(cfg.mqttPassword, sizeof(cfg.mqttPassword), value["mqtt_password"], false) ||
        !value["mqtt_port"].is<uint16_t>() || !value["mqtt_port"].as<uint16_t>()) return false;
    cfg.mqttPort = value["mqtt_port"];
    Preferences preferences;
    if (!preferences.begin("agentdeck", false)) return false;
    preferences.putBool("configured", false);
    bool ok = preferences.putString("ssid", cfg.ssid) && preferences.putString("mqtt_host", cfg.mqttHost) &&
        preferences.putUShort("mqtt_port", cfg.mqttPort) && preferences.putString("device_id", cfg.deviceId);
    preferences.putString("wifi_pass", cfg.password); preferences.putString("mqtt_user", cfg.mqttUser);
    preferences.putString("mqtt_pass", cfg.mqttPassword);
    if (ok) ok = preferences.putBool("configured", true);
    preferences.end();
    return ok;
}

class ConfigCallbacks final : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        if (messageReady) return;
        std::string chunk = characteristic->getValue();
        for (char byte : chunk) {
            if (byte == '\n') {
                input[inputLength] = 0; messageReady = true;
            } else if (inputLength + 1 < sizeof(input)) input[inputLength++] = byte;
            else { inputLength = 0; status("too_large"); }
        }
    }
};
ConfigCallbacks callbacks;
}

namespace provisioning {
bool saved() {
    Preferences preferences;
    if (!preferences.begin("agentdeck", true)) return false;
    bool result = preferences.getBool("configured", false);
    preferences.end(); return result;
}

bool load(config::NetworkConfig& cfg) {
    Preferences preferences;
    if (!preferences.begin("agentdeck", true) || !preferences.getBool("configured", false)) return false;
    auto read = [&](const char* key, char* target, size_t size) {
        String value = preferences.getString(key, "");
        snprintf(target, size, "%s", value.c_str());
    };
    read("ssid", cfg.ssid, sizeof(cfg.ssid)); read("wifi_pass", cfg.password, sizeof(cfg.password));
    read("mqtt_host", cfg.mqttHost, sizeof(cfg.mqttHost)); read("mqtt_user", cfg.mqttUser, sizeof(cfg.mqttUser));
    read("mqtt_pass", cfg.mqttPassword, sizeof(cfg.mqttPassword)); read("device_id", cfg.deviceId, sizeof(cfg.deviceId));
    cfg.mqttPort = preferences.getUShort("mqtt_port", 1883); preferences.end();
    return cfg.ssid[0] && cfg.mqttHost[0] && cfg.deviceId[0] && cfg.mqttPort;
}

void begin(const char* deviceId) {
    char name[32]; snprintf(name, sizeof(name), "AgentDeck Setup %.12s", deviceId);
    BLEDevice::init(name);
    BLEServer* server = BLEDevice::createServer();
    BLEService* service = server->createService(ServiceUuid);
    BLECharacteristic* configCharacteristic = service->createCharacteristic(ConfigUuid, BLECharacteristic::PROPERTY_WRITE);
    configCharacteristic->setCallbacks(&callbacks);
    statusCharacteristic = service->createCharacteristic(StatusUuid, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    statusCharacteristic->addDescriptor(new BLE2902()); statusCharacteristic->setValue("ready");
    service->start(); BLEAdvertising* advertising = BLEDevice::getAdvertising(); advertising->addServiceUUID(ServiceUuid);
    advertising->setScanResponse(true); BLEDevice::startAdvertising();
    Serial.printf("[ble] provisioning ready as %s\n", name);
}

void poll() {
    if (messageReady) {
        bool ok = inputLength && save(input, inputLength);
        inputLength = 0; messageReady = false;
        if (ok) { status("saved"); restartPending = true; restartAt = millis(); }
        else status("invalid");
    }
    if (restartPending && uint32_t(millis() - restartAt) >= 800) ESP.restart();
    delay(10);
}
}
