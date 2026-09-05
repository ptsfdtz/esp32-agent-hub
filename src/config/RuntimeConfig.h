#pragma once
#include <stdio.h>
#include "Config.h"
#if __has_include("Secrets.h")
#include "Secrets.h"
#else
namespace config {
inline NetworkConfig networkConfig() { return NetworkConfig{}; }
}
#endif
