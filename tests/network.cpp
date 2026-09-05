#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <iostream>
#include <string>
#include "network/Protocol.h"
#include "network/Backoff.h"

int main(int argc, char**) {
    Model m;
    if (argc > 1) {
        std::string topic, body;
        while (std::getline(std::cin, topic) && std::getline(std::cin, body)) {
            bool ok = protocol::apply(m, topic.c_str(), body.c_str(), body.size(), 1000, 0);
            std::cout << (ok ? "ok" : "invalid") << std::endl;
        }
        return 0;
    }
    auto apply = [&](const char* topic, const char* body, uint32_t epoch = 0) {
        return protocol::apply(m, topic, body, strlen(body), 1000, epoch);
    };
    assert(apply("agent/codex/status", R"({"online":true,"working":true,"model":"real","task":"compile","ts":2000})", 2000));
    assert(m.agents[0].online && m.agents[0].working);
    assert(!apply("agent/codex/status", R"({"online":false,"working":"yes"})"));
    assert(m.agents[0].online);
    assert(!apply("agent/codex/status", R"({"online":true,"working":true,"ts":1900})", 2000));
    assert(apply("agent/codex/status", R"({"online":false,"working":false,"ts":100})", 2000));
    assert(!m.agents[0].online); // LWT does not expire with its connection-time timestamp.
    assert(apply("agent/codex/status", R"({"online":false,"working":false,"completed_at":2000,"ts":2000})", 2000));
    assert(m.agents[0].completedAt == 2000);
    assert(apply("agent/codex/usage", R"({"five_hour":42.5,"weekly":20,"five_hour_reset":123,"weekly_reset":456})"));
    assert(m.agents[0].shortUsage == 42 && m.agents[0].usageKnown && m.agents[0].usageUpdated == 1000);
    // Regression: usage appeared only before NTP because Bridge omitted envelope ts.
    assert(!apply("agent/codex/usage", R"({"five_hour":42.5,"weekly":20,"five_hour_reset":123,"weekly_reset":456})", 2000));
    assert(apply("agent/codex/usage", R"({"five_hour":42.5,"weekly":20,"five_hour_reset":123,"weekly_reset":456,"measured_at":1000,"cache_age_seconds":1000,"ts":2000})", 2000));
    assert(!apply("agent/codex/usage", R"({"five_hour":101,"weekly":20,"five_hour_reset":123,"weekly_reset":456})"));
    assert(m.agents[0].shortUsage == 42);
    assert(!apply("agent/codex/usage", R"({"five_hour":50,"weekly":-1,"five_hour_reset":123,"weekly_reset":456})"));
    assert(!apply("agent/codex/usage", R"({"five_hour":true,"weekly":20,"five_hour_reset":123,"weekly_reset":456})"));
    assert(!apply("agent/codex/usage", R"({"five_hour":50,"weekly":20,"five_hour_reset":-1,"weekly_reset":456})"));
    assert(apply("agent/codex/usage", R"({"available":false})"));
    assert(!m.agents[0].usageKnown);
    assert(apply("pc/status", R"({"online":true,"cpu":25,"ram":50,"gpu":null,"down_kbps":123})"));
    assert(m.pc.online && !m.pc.gpuKnown && m.pc.downKbps == 123);
    assert(apply("pc/status", R"({"online":true,"cpu":25,"ram":50,"gpu":65,"down_kbps":123})"));
    assert(m.pc.gpuKnown && m.pc.gpu == 65);
    assert(!apply("pc/status", R"({"online":true,"cpu":25,"ram":50,"gpu":"65","down_kbps":123})"));
    assert(apply("pc/status", R"({"online":false,"ts":1})", 2000));
    assert(!m.pc.online);
    assert(!apply("agent/unknown/status", R"({"online":true,"working":true})"));
    assert(!apply("pc/status", "[]")); assert(!apply("pc/status", "{"));
    std::string oversized(1025, 'a');
    assert(!protocol::apply(m, "pc/status", oversized.c_str(), oversized.size(), 0, 0));
    std::string longName = "{\"online\":true,\"working\":true,\"model\":\"" + std::string(32, 'x') + "\"}";
    assert(!protocol::apply(m, "agent/codex/status", longName.c_str(), longName.size(), 0, 0));
    assert(protocol::identifier("agentdeck-01") && !protocol::identifier("a/+"));
    assert(!protocol::identifier("") && !protocol::identifier("x/y"));
    Backoff backoff;
    assert(backoff.ready(0)); backoff.fail(UINT32_MAX - 499);
    assert(!backoff.ready(499)); assert(backoff.ready(500));
    backoff.fail(500); assert(!backoff.ready(2499)); assert(backoff.ready(2500));
    for (int i = 0; i < 10; ++i) backoff.fail(5000);
    assert(!backoff.ready(64999)); assert(backoff.ready(65000));
    backoff.reset(); assert(backoff.ready(0)); backoff.fail(0); assert(backoff.ready(1000));
    std::cout << "Network protocol, atomic rejection, LWT, unknown data, bounds and backoff passed\n";
}
