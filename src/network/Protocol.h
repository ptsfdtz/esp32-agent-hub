#pragma once
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>
#include "models/Model.h"

namespace protocol {
constexpr size_t MaxPayload = 1024;
constexpr const char* AgentIds[] = {"codex", "claude", "opencode"};
inline bool identifier(const char* s) {
    if (!s || !*s || strlen(s) > 32) return false;
    for (; *s; ++s) if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
        (*s >= '0' && *s <= '9') || *s == '-' || *s == '_')) return false;
    return true;
}
inline bool percent(JsonVariantConst v) {
    return v.is<float>() && isfinite(v.as<float>()) && v.as<float>() >= 0 && v.as<float>() <= 100;
}
template<size_t N> inline bool copyText(char (&dest)[N], JsonVariantConst v) {
    if (!v.is<const char*>()) return false;
    const char* s = v.as<const char*>();
    if (strlen(s) >= N) return false;
    strcpy(dest, s); return true;
}
inline bool recent(JsonObjectConst o, uint32_t epoch, uint32_t age = 15) {
    if (!o["ts"].is<uint32_t>()) return false;
    uint32_t stamp = o["ts"];
    return stamp <= epoch + 5 && (stamp >= epoch || epoch - stamp <= age);
}
// Parse atomically: malformed payloads never partially mutate live data.
inline bool apply(Model& m, const char* topic, const char* payload, size_t size,
                  uint32_t now, uint32_t epoch) {
    if (!size || size > MaxPayload) return false;
    StaticJsonDocument<2048> doc;
    if (deserializeJson(doc, payload, size) || !doc.is<JsonObject>()) return false;
    JsonObjectConst o = doc.as<JsonObjectConst>();
    // LWT timestamps are created at CONNECT and may be hours old when delivered.
    size_t topicLen = strlen(topic);
    bool offline = topicLen >= 7 && !strcmp(topic + topicLen - 7, "/status") &&
        o["online"].is<bool>() && !o["online"].as<bool>();
    if (epoch && !recent(o, epoch) && !offline) return false;
    if (!strcmp(topic, "pc/status")) {
        PcStatus p = m.pc;
        if (!o["online"].is<bool>()) return false;
        p.online = o["online"];
        if (p.online) {
            if (!percent(o["cpu"]) || !percent(o["ram"]) || (!o["gpu"].isNull() && !percent(o["gpu"])) ||
                !o["down_kbps"].is<uint32_t>()) return false;
            p.cpu = o["cpu"].as<float>(); p.ram = o["ram"].as<float>();
            p.gpuKnown = !o["gpu"].isNull(); p.gpu = o["gpu"].as<float>(); p.downKbps = o["down_kbps"];
        }
        p.lastUpdate = now; m.pc = p; return true;
    }
    for (int i = 0; i < 3; ++i) {
        char base[48]; snprintf(base, sizeof(base), "agent/%s/", AgentIds[i]);
        size_t len = strlen(base);
        if (strncmp(topic, base, len)) continue;
        const char* kind = topic + len;
        AgentStatus a = m.agents[i];
        if (!strcmp(kind, "status")) {
            if (!o["online"].is<bool>() || !o["working"].is<bool>()) return false;
            a.online = o["online"]; a.working = a.online && o["working"].as<bool>();
            if (o.containsKey("model") && !copyText(a.model, o["model"])) return false;
            if (o.containsKey("task") && !copyText(a.task, o["task"])) return false;
            if (o.containsKey("completed_at")) {
                if (!o["completed_at"].is<uint32_t>()) return false;
                a.completedAt = o["completed_at"];
            }
            a.lastUpdate = now;
        } else if (!strcmp(kind, "usage")) {
            if (o["available"].is<bool>() && !o["available"].as<bool>()) {
                a.usageKnown = false; m.agents[i] = a; return true;
            }
            if (!percent(o["five_hour"]) || !percent(o["weekly"]) ||
                !o["five_hour_reset"].is<uint32_t>() || !o["weekly_reset"].is<uint32_t>()) return false;
            a.shortUsage = o["five_hour"].as<float>(); a.weeklyUsage = o["weekly"].as<float>();
            a.shortReset = o["five_hour_reset"]; a.weeklyReset = o["weekly_reset"];
            a.usageKnown = true; a.usageUpdated = now;
        } else if (!strcmp(kind, "task")) {
            if (!copyText(a.task, o["task"])) return false;
        } else return false;
        m.agents[i] = a; return true;
    }
    return false;
}
}
