#include "NetworkService.h"
#include "Protocol.h"
#include <ArduinoOTA.h>
#include <esp_system.h>
#include <time.h>

namespace {
uint32_t epochNow() { time_t t = time(nullptr); return t > 1700000000 ? uint32_t(t) : 0; }
}
bool NetworkService::begin(const config::NetworkConfig& cfg) {
    if (!protocol::identifier(cfg.deviceId) || !cfg.mqttPort) return false;
    cfg_ = cfg;
    snprintf(base_, sizeof(base_), "agentdeck/%s", cfg_.deviceId);
    snprintf(statusTopic_, sizeof(statusTopic_), "%s/status", base_);
    snprintf(boot_, sizeof(boot_), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
    snapshots_ = xQueueCreate(1, sizeof(Snapshot));
    events_ = xQueueCreate(8, sizeof(Event)); commands_ = xQueueCreate(8, sizeof(Command));
    if (snapshots_ && events_ && commands_ &&
        xTaskCreatePinnedToCore(task, "agentdeck-net", 12288, this, 1, nullptr, 0) == pdPASS) return true;
    if (snapshots_) vQueueDelete(snapshots_);
    if (events_) vQueueDelete(events_);
    if (commands_) vQueueDelete(commands_);
    snapshots_ = events_ = commands_ = nullptr;
    return false;
}
bool NetworkService::command(int agent, const char* action, uint32_t now) {
    if (!commands_ || agent < 0 || agent > 2 || strlen(action) >= sizeof(Command::action)) return false;
    Command c{}; c.agent = agent; c.created = now; c.sequence = ++sequence_;
    strcpy(c.action, action);
    return xQueueSend(commands_, &c, 0) == pdTRUE;
}
void NetworkService::notify(const char* text) {
    Event e; snprintf(e.text, sizeof(e.text), "%s", text);
    xQueueSend(events_, &e, 0);
}
void NetworkService::snapshot() {
    Snapshot s{};
    memcpy(s.agents, state_.agents, sizeof(s.agents)); s.pc = state_.pc; s.network = state_.network;
    s.otaReady = state_.device.otaReady;
    time_t t = time(nullptr); struct tm local{};
    if (t > 1700000000 && localtime_r(&t, &local)) {
        s.timeSynced = true; s.minutes = local.tm_hour * 60 + local.tm_min;
    }
    xQueueOverwrite(snapshots_, &s);
}
void NetworkService::message(char* topic, byte* payload, unsigned int length) {
    uint32_t now = millis(), epoch = epochNow();
    uint32_t completed[3];
    for (int i = 0; i < 3; ++i) completed[i] = state_.agents[i].completedAt;
    if (protocol::apply(state_, topic, reinterpret_cast<const char*>(payload), length, now, epoch)) {
        for (int i = 0; i < 3; ++i) {
            const auto& a = state_.agents[i];
            if (a.completedAt && a.completedAt != completed[i] && epoch && a.completedAt <= epoch + 5 &&
                (a.completedAt >= epoch || epoch - a.completedAt <= 15)) {
                char text[22]; snprintf(text, sizeof(text), "%s COMPLETE", a.name); notify(text);
            }
        }
        return;
    }
    if (!length || length > protocol::MaxPayload) return;
    StaticJsonDocument<2048> doc;
    if (deserializeJson(doc, payload, length) || !doc.is<JsonObject>()) return;
    JsonObjectConst o = doc.as<JsonObjectConst>();
    char target[96];
    snprintf(target, sizeof(target), "%s/ack", base_);
    if (!strcmp(topic, target)) {
        const char* id = o["id"] | "";
        if (!epoch || !protocol::recent(o, epoch)) return;
        Pending* pending = nullptr;
        for (auto& p : pending_) if (p.id[0] && !strcmp(p.id, id)) { pending = &p; break; }
        if (!pending) return;
        const char* result = o["status"] | "";
        if (!strcmp(result, "completed")) notify("COMMAND COMPLETE");
        else if (!strcmp(result, "rejected")) notify("COMMAND REJECTED");
        else if (!strcmp(result, "failed")) notify("COMMAND FAILED");
        else return;
        pending->id[0] = 0;
        return;
    }
    // Remote mutations require a synchronized clock and a short-lived envelope.
    if (!epoch || !protocol::recent(o, epoch) || !o["expires_at"].is<uint32_t>() ||
        o["expires_at"].as<uint32_t>() < epoch || o["expires_at"].as<uint32_t>() > epoch + 30) return;
    snprintf(target, sizeof(target), "%s/config", base_);
    if (!strcmp(topic, target)) {
        Event e; e.kind = Event::Settings;
        if (o.containsKey("brightness")) {
            if (!o["brightness"].is<int>() || o["brightness"].as<int>() < 16 || o["brightness"].as<int>() > 255) return;
            e.contrast = o["brightness"];
        }
        if (o.containsKey("motion")) {
            if (!o["motion"].is<int>() || o["motion"].as<int>() < 0 || o["motion"].as<int>() > 2) return;
            e.motion = o["motion"];
        }
        xQueueSend(events_, &e, 0);
    }
    snprintf(target, sizeof(target), "%s/command", base_);
    if (!strcmp(topic, target) && !strcmp(o["action"] | "", "notify")) {
        char text[22]; if (protocol::copyText(text, o["text"])) notify(text);
    }
}
bool NetworkService::connectMqtt() {
    if (!mqtt_.connect(cfg_.deviceId, cfg_.mqttUser[0] ? cfg_.mqttUser : nullptr,
                       cfg_.mqttUser[0] ? cfg_.mqttPassword : nullptr, statusTopic_, 1, true, "offline")) return false;
    bool ok = mqtt_.subscribe("agent/+/status", 1) && mqtt_.subscribe("agent/+/usage", 1) &&
        mqtt_.subscribe("agent/+/task", 1) && mqtt_.subscribe("pc/status", 1);
    for (const char* suffix : {"command", "config", "ack"}) {
        char topic[96]; snprintf(topic, sizeof(topic), "%s/%s", base_, suffix);
        ok = mqtt_.subscribe(topic, 1) && ok;
    }
    if (ok) ok = mqtt_.publish(statusTopic_, "online", true);
    if (!ok) mqtt_.disconnect();
    return ok;
}
void NetworkService::run() {
    const char* names[] = {"Codex", "Claude", "OpenCode"};
    for (int i = 0; i < 3; ++i) strcpy(state_.agents[i].name, names[i]);
    snprintf(state_.network.ssid, sizeof(state_.network.ssid), "%s", cfg_.ssid);
    snprintf(state_.network.mqttHost, sizeof(state_.network.mqttHost), "%s", cfg_.mqttHost);
    WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(false);
    mqtt_.setServer(cfg_.mqttHost, cfg_.mqttPort); mqtt_.setSocketTimeout(1); mqtt_.setKeepAlive(10);
    if (!mqtt_.setBufferSize(1536)) { notify("MQTT MEMORY ERROR"); vTaskDelete(nullptr); return; }
    mqtt_.setCallback([this](char* t, byte* p, unsigned int n) { message(t, p, n); });
    bool attempting = false, wasWifi = false, wasMqtt = false;
    uint32_t wifiStarted = 0, snapAt = 0, telemetryAt = 0;
    if (!cfg_.ssid[0]) notify("CONFIGURE WIFI");
    for (;;) {
        uint32_t now = millis();
        bool wifi = WiFi.status() == WL_CONNECTED;
        if (wifi && !wasWifi) {
            attempting = false; wifiRetry_.reset(); mqttRetry_.reset();
            configTzTime(cfg_.timezone, cfg_.ntpServer);
            if (cfg_.otaPassword[0]) {
                ArduinoOTA.setHostname(cfg_.deviceId); ArduinoOTA.setPassword(cfg_.otaPassword);
                ArduinoOTA.onStart([this]() { state_.device.otaReady = false; notify("OTA UPDATING"); snapshot(); });
                ArduinoOTA.onError([this](ota_error_t) { state_.device.otaReady = true; notify("OTA FAILED"); });
                ArduinoOTA.begin(); otaStarted_ = true; state_.device.otaReady = true;
            }
        }
        if (!wifi) {
            if (wasWifi) {
                transport_.stop(); mqttRetry_.reset();
                if (otaStarted_) { ArduinoOTA.end(); otaStarted_ = false; }
                state_.device.otaReady = false;
                wifiRetry_.fail(now);
            }
            if (attempting && uint32_t(now - wifiStarted) >= 10000) {
                WiFi.disconnect(); attempting = false; wifiRetry_.fail(now);
            }
            if (!attempting && cfg_.ssid[0] && wifiRetry_.ready(now)) {
                WiFi.begin(cfg_.ssid, cfg_.password); wifiStarted = now; attempting = true;
            }
        }
        wasWifi = wifi;
        state_.network.wifi = wifi;
        state_.network.rssi = wifi ? WiFi.RSSI() : 0;
        if (wifi) {
            IPAddress ip = WiFi.localIP();
            snprintf(state_.network.ip, sizeof(state_.network.ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        } else state_.network.ip[0] = 0;
        if (wifi && otaStarted_) ArduinoOTA.handle();
        // Drop commands while disconnected; never replay a control gesture after reconnect.
        Command c{};
        if (!wifi || !mqtt_.connected()) {
            while (xQueueReceive(commands_, &c, 0) == pdTRUE) notify("COMMAND OFFLINE");
        }
        if (wifi && cfg_.mqttHost[0] && !mqtt_.connected() && mqttRetry_.ready(now)) {
            if (connectMqtt()) mqttRetry_.reset(); else mqttRetry_.fail(millis());
        }
        if (mqtt_.connected()) mqtt_.loop();
        state_.network.mqtt = wifi && mqtt_.connected();
        if (!state_.network.mqtt && wasMqtt) {
            for (auto& a : state_.agents) { a.online = false; a.working = false; }
            state_.pc.online = false;
            mqttRetry_.fail(millis());
        }
        wasMqtt = state_.network.mqtt;
        for (int n = 0; n < 4 && xQueueReceive(commands_, &c, 0) == pdTRUE; ++n) {
            now = millis(); uint32_t epoch = epochNow();
            if (!state_.network.mqtt || uint32_t(now - c.created) > 3000 || !epoch ||
                !fresh(state_.agents[c.agent].online, state_.agents[c.agent].lastUpdate, now)) {
                notify("COMMAND UNAVAILABLE"); continue;
            }
            char topic[96], body[384], id[40];
            Pending* pending = nullptr;
            for (auto& p : pending_) if (!p.id[0]) { pending = &p; break; }
            if (!pending) { notify("COMMAND QUEUE FULL"); continue; }
            snprintf(id, sizeof(id), "%s-%lu", boot_, (unsigned long)c.sequence);
            StaticJsonDocument<512> doc;
            doc["id"] = id; doc["device_id"] = cfg_.deviceId; doc["action"] = c.action;
            doc["ts"] = epoch; doc["expires_at"] = epoch + 10;
            serializeJson(doc, body, sizeof(body));
            snprintf(topic, sizeof(topic), "agent/%s/command", protocol::AgentIds[c.agent]);
            if (mqtt_.publish(topic, body, false)) {
                strcpy(pending->id, id); pending->sent = now; notify("COMMAND SENT");
            } else notify("COMMAND FAILED");
            snprintf(topic, sizeof(topic), "%s/input", base_); mqtt_.publish(topic, body, false);
        }
        now = millis();
        for (auto& p : pending_) if (p.id[0] && uint32_t(now - p.sent) >= 12000) {
            p.id[0] = 0; notify("COMMAND TIMEOUT");
        }
        for (auto& a : state_.agents) {
            if (!fresh(a.online, a.lastUpdate, now)) { a.online = false; a.working = false; }
            if (a.usageKnown && uint32_t(now - a.usageUpdated) >= config::StaleMs) a.usageKnown = false;
        }
        if (!fresh(state_.pc.online, state_.pc.lastUpdate, now)) state_.pc.online = false;
        if (uint32_t(now - telemetryAt) >= 5000 && state_.network.mqtt) {
            telemetryAt = now;
            char topic[96], body[384]; StaticJsonDocument<512> doc;
            doc["ts"] = epochNow(); doc["uptime_ms"] = now; doc["heap"] = ESP.getFreeHeap();
            doc["min_heap"] = ESP.getMinFreeHeap(); doc["rssi"] = state_.network.rssi;
            doc["firmware"] = config::Firmware; doc["ota_ready"] = state_.device.otaReady;
            doc["time_synced"] = epochNow() != 0;
            serializeJson(doc, body, sizeof(body));
            snprintf(topic, sizeof(topic), "%s/telemetry", base_); mqtt_.publish(topic, body, false);
        }
        if (uint32_t(now - snapAt) >= 250) { snapshot(); snapAt = now; }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
