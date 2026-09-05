#pragma once
#include "config/Config.h"

namespace provisioning {
bool load(config::NetworkConfig& cfg);
bool saved();
void begin(const char* deviceId);
void poll();
}
